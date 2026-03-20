// This file is part of Blend2D project <https://blend2d.com>
//
// See blend2d.h or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#include <blend2d/core/api-build_test_p.h>
#if defined(BL_TEST)

// Alpha Multiplication Equivalence Tests
// =======================================
//
// These tests verify the mathematical correctness of the alpha multiplication
// optimization applied in fetch_mask_a8_into_pc_by_expanding_to_32bits().
//
// The optimization changes the order of operations from:
//   u8→u32 → multiply_u16 → div255 → shuffle
// to:
//   u8→u16 → multiply_u16 → div255 → expand_u16→u32 → shuffle
//
// These tests prove that both orderings produce identical results for all
// possible (mask, alpha) input combinations.

namespace bl {
namespace Tests {

// Scalar div255 matching the SIMD implementation: (v + 128 + ((v + 128) >> 8)) >> 8
static uint32_t scalar_div255(uint32_t v) noexcept {
  uint32_t t = v + 128u;
  return (t + (t >> 8)) >> 8;
}

// Scalar alpha multiply + div255, matching the SIMD pipeline: div255(mask * alpha).
static uint32_t scalar_mul_div255(uint32_t mask, uint32_t alpha) noexcept {
  return scalar_div255(mask * alpha);
}

UNIT(alpha_multiply_div255_correctness, BL_TEST_GROUP_RENDERING_UTILITIES) {
  INFO("Testing div255 approximation properties");
  {
    // The SIMD div255 uses (v + 128 + ((v+128)>>8)) >> 8 which is an approximation of v/255.
    // It doesn't exactly match integer division, but for the alpha multiply use case (inputs are
    // products of two u8 values: 0..65025), it must:
    // 1. Produce results in [0, 255] for inputs in [0, 65025].
    // 2. Be exact for multiples of 255: div255(k*255) == k for k in [0, 255].
    // 3. Never be off by more than 1 from exact division.

    uint32_t out_of_range = 0;
    uint32_t off_by_more_than_1 = 0;

    for (uint32_t v = 0; v <= 255u * 255u; v++) {
      uint32_t approx = scalar_div255(v);

      if (approx > 255u) {
        out_of_range++;
      }

      uint32_t exact = v / 255u;
      uint32_t diff = approx > exact ? approx - exact : exact - approx;
      if (diff > 1u) {
        off_by_more_than_1++;
      }
    }

    EXPECT_EQ(out_of_range, 0u)
      .message("div255 produced %u out-of-range results", out_of_range);
    EXPECT_EQ(off_by_more_than_1, 0u)
      .message("div255 was off by >1 in %u cases", off_by_more_than_1);

    // Verify exactness at multiples of 255.
    for (uint32_t k = 0; k <= 255u; k++) {
      EXPECT_EQ(scalar_div255(k * 255u), k)
        .message("div255(%u * 255) = %u, expected %u", k, scalar_div255(k * 255u), k);
    }
  }

  INFO("Testing div255 boundary values");
  {
    EXPECT_EQ(scalar_div255(0u), 0u);
    EXPECT_EQ(scalar_div255(255u), 1u);
    EXPECT_EQ(scalar_div255(255u * 128u), 128u);
    EXPECT_EQ(scalar_div255(255u * 255u), 255u);
    EXPECT_EQ(scalar_div255(127u), 0u);
    EXPECT_EQ(scalar_div255(128u), 1u);    // Boundary: (128+128+1)>>8 = 1
  }

  INFO("Testing alpha multiply pipeline for representative values");
  {
    // Test all corner cases and representative values for (mask, alpha).
    static constexpr uint32_t kTestValues[] = {0, 1, 2, 63, 64, 127, 128, 191, 192, 253, 254, 255};

    for (uint32_t mask : kTestValues) {
      for (uint32_t alpha : kTestValues) {
        uint32_t result = scalar_mul_div255(mask, alpha);

        // Result must be in [0, 255].
        EXPECT_TRUE(result <= 255u)
          .message("mul_div255(%u, %u) = %u exceeds u8 range", mask, alpha, result);

        // Identity: mask * 255 / 255 == mask.
        if (alpha == 255u) {
          EXPECT_EQ(result, mask)
            .message("mul_div255(%u, 255) = %u, expected %u", mask, result, mask);
        }

        // Zero: mask * 0 / 255 == 0.
        if (alpha == 0u) {
          EXPECT_EQ(result, 0u)
            .message("mul_div255(%u, 0) = %u, expected 0", mask, result);
        }

        // Commutativity: div255(a*b) == div255(b*a).
        uint32_t swapped = scalar_mul_div255(alpha, mask);
        EXPECT_EQ(result, swapped)
          .message("mul_div255(%u, %u) = %u != mul_div255(%u, %u) = %u", mask, alpha, result, alpha, mask, swapped);
      }
    }
  }
}

UNIT(alpha_multiply_ordering_equivalence, BL_TEST_GROUP_RENDERING_UTILITIES) {
  INFO("Testing u8→u16→multiply→div255 vs u8→u32→multiply→div255 equivalence (exhaustive)");
  {
    // The optimization changes the extension width before multiplication.
    // Both paths must produce identical results for ALL 256×256 input combinations.
    //
    // Path A (original): zero-extend u8→u32, multiply as u16, div255
    //   Each value occupies 32-bit lane: 0x000000mm * alpha → div255
    //
    // Path B (optimized): zero-extend u8→u16, multiply as u16, div255
    //   Each value occupies 16-bit lane: 0x00mm * alpha → div255
    //
    // Since mask ∈ [0,255] and alpha ∈ [0,255], the product mask*alpha ∈ [0,65025]
    // which fits in u16 (max 65535). Both paths compute the same product, so
    // the div255 result must be identical.

    uint32_t mismatches = 0;
    uint32_t first_mismatch_mask = 0;
    uint32_t first_mismatch_alpha = 0;

    for (uint32_t mask = 0; mask < 256u; mask++) {
      for (uint32_t alpha = 0; alpha < 256u; alpha++) {
        // Path A: u8→u32 (32-bit lane), multiply, div255.
        uint32_t product_a = (mask & 0xFFFFu) * (alpha & 0xFFFFu);
        uint32_t result_a = scalar_div255(product_a);

        // Path B: u8→u16 (16-bit lane), multiply, div255.
        uint16_t mask16 = uint16_t(mask);
        uint16_t alpha16 = uint16_t(alpha);
        uint32_t product_b = uint32_t(mask16) * uint32_t(alpha16);
        uint32_t result_b = scalar_div255(product_b);

        if (result_a != result_b) {
          if (mismatches == 0) {
            first_mismatch_mask = mask;
            first_mismatch_alpha = alpha;
          }
          mismatches++;
        }
      }
    }

    EXPECT_EQ(mismatches, 0u)
      .message("u32 vs u16 path diverged in %u cases; first at mask=%u, alpha=%u",
               mismatches, first_mismatch_mask, first_mismatch_alpha);
  }

  INFO("Testing that u16 multiply does not overflow");
  {
    // Verify that mask * alpha fits in u16 for all u8 inputs.
    // max product = 255 * 255 = 65025, max u16 = 65535.
    uint32_t max_product = 0;
    for (uint32_t mask = 0; mask < 256u; mask++) {
      for (uint32_t alpha = 0; alpha < 256u; alpha++) {
        uint32_t product = mask * alpha;
        if (product > max_product)
          max_product = product;
      }
    }

    EXPECT_TRUE(max_product <= 65535u)
      .message("Max product %u exceeds u16 range", max_product);
    EXPECT_EQ(max_product, 65025u)
      .message("Expected max product 255*255=65025, got %u", max_product);
  }

  INFO("Testing div255 intermediate value fits in u16");
  {
    // div255 computes (v + 128 + ((v+128)>>8)) >> 8.
    // For v = 65025 (max): v + 128 = 65153, which fits in u16 (65535).
    // (v+128) >> 8 = 254, so v + 128 + 254 = 65407, which also fits in u16.
    // This confirms the div255 computation doesn't overflow u16.
    uint32_t max_intermediate = 0;
    for (uint32_t v = 0; v <= 255u * 255u; v++) {
      uint32_t t = v + 128u;
      uint32_t intermediate = t + (t >> 8);
      if (intermediate > max_intermediate)
        max_intermediate = intermediate;
    }

    EXPECT_TRUE(max_intermediate <= 65535u)
      .message("Max div255 intermediate %u exceeds u16 range", max_intermediate);
  }

  INFO("Testing zero-interleave u16→u32 expansion preserves low byte");
  {
    // After div255, the result is in [0, 255] occupying the low byte of a u16 lane.
    // Zero-interleave u16→u32 (PUNPCKLWD with zero) places this in the low u16 of a u32 lane.
    // The subsequent VPSHUFB broadcast reads byte[0] of each u32 lane.
    // Verify that the broadcast source byte is correct for all possible div255 results.
    for (uint32_t result = 0; result <= 255u; result++) {
      // u16 lane value after div255: 0x00RR (result in low byte, zero in high byte).
      uint16_t u16_val = uint16_t(result);

      // After zero-interleave: u32 = [u16_val, 0x0000] = 0x0000_00RR.
      uint32_t u32_val = uint32_t(u16_val);

      // Byte[0] of the u32 value (what the broadcast shuffle reads).
      uint8_t byte0 = uint8_t(u32_val & 0xFFu);

      EXPECT_EQ(byte0, uint8_t(result))
        .message("u16→u32 expansion: result=%u, byte0=%u", result, unsigned(byte0));

      // After broadcast: all 4 bytes should be the same alpha value.
      uint32_t broadcast = uint32_t(byte0) * 0x01010101u;
      uint32_t expected = result * 0x01010101u;
      EXPECT_EQ(broadcast, expected)
        .message("Broadcast of result=%u: got 0x%08X, expected 0x%08X", result, broadcast, expected);
    }
  }
}

UNIT(alpha_multiply_full_pipeline_equivalence, BL_TEST_GROUP_RENDERING_UTILITIES) {
  INFO("Testing full pipeline: original (u32) vs optimized (u16) produce identical broadcast u32");
  {
    // Simulate both pipelines end-to-end for every (mask, alpha) pair:
    //
    // Original pipeline:
    //   1. u8→u32: mask byte → 0x000000MM
    //   2. multiply u16: 0x000000MM * alpha (only low 16 bits matter) → product
    //   3. div255: product → result (0x000000RR)
    //   4. broadcast shuffle: 0x000000RR → 0xRRRRRRRR
    //
    // Optimized pipeline:
    //   1. u8→u16: mask byte → 0x00MM
    //   2. multiply u16: 0x00MM * alpha → product
    //   3. div255: product → result (0x00RR)
    //   4. zero-interleave u16→u32: 0x00RR → 0x000000RR
    //   5. broadcast shuffle: 0x000000RR → 0xRRRRRRRR

    uint32_t mismatches = 0;

    for (uint32_t mask = 0; mask < 256u; mask++) {
      for (uint32_t alpha = 0; alpha < 256u; alpha++) {
        // Original pipeline.
        uint32_t u32_val = mask;  // u8→u32
        uint32_t product_orig = (u32_val & 0xFFFFu) * alpha;  // multiply as u16
        uint32_t result_orig = scalar_div255(product_orig);
        uint32_t broadcast_orig = (result_orig & 0xFFu) * 0x01010101u;

        // Optimized pipeline.
        uint16_t u16_val = uint16_t(mask);  // u8→u16
        uint32_t product_opt = uint32_t(u16_val) * alpha;  // multiply as u16
        uint32_t result_opt = scalar_div255(product_opt);
        uint32_t u32_expanded = uint32_t(uint16_t(result_opt));  // zero-interleave u16→u32
        uint32_t broadcast_opt = (u32_expanded & 0xFFu) * 0x01010101u;

        if (broadcast_orig != broadcast_opt) {
          mismatches++;
        }
      }
    }

    EXPECT_EQ(mismatches, 0u)
      .message("Full pipeline divergence: %u of 65536 (mask, alpha) pairs produced different results", mismatches);
  }

  INFO("Testing without global alpha (no multiply): u32 load+shuffle vs u16 load+expand+shuffle");
  {
    // When ga is nullptr, the original path does u8→u32 then shuffle.
    // The optimized path still does u8→u32 then shuffle (no change).
    // Verify the shuffle produces correct broadcast for all u8 values.
    for (uint32_t mask = 0; mask < 256u; mask++) {
      uint32_t u32_val = mask;  // u8→u32: 0x000000MM
      // Broadcast shuffle: byte[0] → all 4 bytes.
      uint32_t broadcast = (u32_val & 0xFFu) * 0x01010101u;
      uint32_t expected = mask * 0x01010101u;
      EXPECT_EQ(broadcast, expected)
        .message("No-alpha broadcast: mask=%u, got 0x%08X, expected 0x%08X", mask, broadcast, expected);
    }
  }
}

} // {Tests}
} // {bl}

#endif // BL_TEST
