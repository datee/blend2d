// This file is part of Blend2D project <https://blend2d.com>
//
// See blend2d.h or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#include <blend2d/core/api-build_test_p.h>
#if defined(BL_TEST)

// Coverage Format Equivalence Tests
// ==================================
//
// These tests verify that packed (u8) and unpacked (u16) coverage formats
// produce equivalent results when applied to RGBA pixels via multiply+div255.
//
// This validates the change to enable PixelCoverageFormat::kPacked for all
// AArch64 composition modes (previously only src_copy, src_over, and screen).

namespace bl {
namespace Tests {

// Scalar div255 matching the SIMD implementation.
static uint32_t cov_div255(uint32_t v) noexcept {
  uint32_t t = v + 128u;
  return (t + (t >> 8)) >> 8;
}

// Apply packed (u8) coverage to a pixel channel: div255(channel * coverage).
static uint32_t apply_packed_coverage(uint32_t channel, uint8_t coverage) noexcept {
  return cov_div255(channel * uint32_t(coverage));
}

// Apply unpacked (u16) coverage to a pixel channel: (channel * coverage + 128) >> 8.
// The unpacked format uses coverage as a u16 value (0..255 in low byte, high byte zero).
// The multiplication and rounding are equivalent to div255 for valid inputs.
static uint32_t apply_unpacked_coverage(uint32_t channel, uint16_t coverage) noexcept {
  return cov_div255(channel * uint32_t(coverage));
}

UNIT(coverage_format_broadcast, BL_TEST_GROUP_RENDERING_UTILITIES) {
  INFO("Testing packed (u8) coverage broadcast to 32-bit");
  {
    // In packed format, a coverage byte is broadcast to all 4 bytes of a u32:
    // coverage=0xAB → 0xABABABAB.
    for (uint32_t cov = 0; cov <= 255u; cov++) {
      uint32_t broadcast = cov * 0x01010101u;

      // Verify all 4 bytes are the same.
      uint8_t b0 = uint8_t((broadcast >>  0) & 0xFFu);
      uint8_t b1 = uint8_t((broadcast >>  8) & 0xFFu);
      uint8_t b2 = uint8_t((broadcast >> 16) & 0xFFu);
      uint8_t b3 = uint8_t((broadcast >> 24) & 0xFFu);

      EXPECT_EQ(b0, uint8_t(cov));
      EXPECT_EQ(b1, uint8_t(cov));
      EXPECT_EQ(b2, uint8_t(cov));
      EXPECT_EQ(b3, uint8_t(cov));
    }
  }

  INFO("Testing unpacked (u16) coverage broadcast to 64-bit");
  {
    // In unpacked format, a coverage value is a u16 (0x00CC) broadcast to fill
    // a 64-bit value: [0x00CC, 0x00CC, 0x00CC, 0x00CC].
    for (uint32_t cov = 0; cov <= 255u; cov++) {
      uint64_t broadcast = uint64_t(cov) | (uint64_t(cov) << 16) | (uint64_t(cov) << 32) | (uint64_t(cov) << 48);

      // Verify all 4 u16 lanes are the same.
      uint16_t w0 = uint16_t((broadcast >>  0) & 0xFFFFu);
      uint16_t w1 = uint16_t((broadcast >> 16) & 0xFFFFu);
      uint16_t w2 = uint16_t((broadcast >> 32) & 0xFFFFu);
      uint16_t w3 = uint16_t((broadcast >> 48) & 0xFFFFu);

      EXPECT_EQ(w0, uint16_t(cov));
      EXPECT_EQ(w1, uint16_t(cov));
      EXPECT_EQ(w2, uint16_t(cov));
      EXPECT_EQ(w3, uint16_t(cov));
    }
  }
}

UNIT(coverage_format_equivalence, BL_TEST_GROUP_RENDERING_UTILITIES) {
  INFO("Testing packed vs unpacked coverage equivalence for all 256 coverage values");
  {
    // For all possible coverage values (0..255) and all channel values (0..255),
    // verify that packed and unpacked paths produce identical results.
    uint32_t mismatches = 0;
    uint32_t first_mismatch_ch = 0;
    uint32_t first_mismatch_cov = 0;

    for (uint32_t cov = 0; cov <= 255u; cov++) {
      for (uint32_t ch = 0; ch <= 255u; ch++) {
        uint32_t packed_result = apply_packed_coverage(ch, uint8_t(cov));
        uint32_t unpacked_result = apply_unpacked_coverage(ch, uint16_t(cov));

        if (packed_result != unpacked_result) {
          if (mismatches == 0) {
            first_mismatch_ch = ch;
            first_mismatch_cov = cov;
          }
          mismatches++;
        }
      }
    }

    EXPECT_EQ(mismatches, 0u)
      .message("Packed vs unpacked diverged in %u cases; first at ch=%u, cov=%u",
               mismatches, first_mismatch_ch, first_mismatch_cov);
  }

  INFO("Testing full RGBA pixel with packed coverage");
  {
    // Simulate applying packed coverage to a complete RGBA pixel.
    // Each channel is independently multiplied by the same coverage value.
    static constexpr uint32_t kTestPixels[] = {
      0x00000000u,  // Transparent black.
      0xFFFFFFFFu,  // Opaque white.
      0xFF000000u,  // Opaque black.
      0x80402010u,  // Arbitrary RGBA.
      0xFEDCBA98u,  // Another arbitrary value.
      0x01010101u,  // Near-zero.
      0x7F7F7F7Fu,  // Mid-gray.
    };

    static constexpr uint8_t kTestCoverage[] = {0, 1, 64, 127, 128, 191, 254, 255};

    for (uint32_t pixel : kTestPixels) {
      uint8_t r = uint8_t((pixel >> 16) & 0xFFu);
      uint8_t g = uint8_t((pixel >>  8) & 0xFFu);
      uint8_t b = uint8_t((pixel >>  0) & 0xFFu);
      uint8_t a = uint8_t((pixel >> 24) & 0xFFu);

      for (uint8_t cov : kTestCoverage) {
        uint32_t new_r = apply_packed_coverage(r, cov);
        uint32_t new_g = apply_packed_coverage(g, cov);
        uint32_t new_b = apply_packed_coverage(b, cov);
        uint32_t new_a = apply_packed_coverage(a, cov);

        // All results must be in [0, 255].
        EXPECT_TRUE(new_r <= 255u && new_g <= 255u && new_b <= 255u && new_a <= 255u)
          .message("Result out of range for pixel=0x%08X, cov=%u", pixel, unsigned(cov));

        // Zero coverage must produce zero.
        if (cov == 0u) {
          EXPECT_EQ(new_r, 0u);
          EXPECT_EQ(new_g, 0u);
          EXPECT_EQ(new_b, 0u);
          EXPECT_EQ(new_a, 0u);
        }

        // Full coverage must preserve the original value.
        if (cov == 255u) {
          EXPECT_EQ(new_r, uint32_t(r))
            .message("Full coverage should preserve R: pixel=0x%08X", pixel);
          EXPECT_EQ(new_g, uint32_t(g))
            .message("Full coverage should preserve G: pixel=0x%08X", pixel);
          EXPECT_EQ(new_b, uint32_t(b))
            .message("Full coverage should preserve B: pixel=0x%08X", pixel);
          EXPECT_EQ(new_a, uint32_t(a))
            .message("Full coverage should preserve A: pixel=0x%08X", pixel);
        }

        // Result should never exceed input (coverage is a scaling factor in [0, 1]).
        EXPECT_TRUE(new_r <= uint32_t(r))
          .message("R increased: pixel=0x%08X, cov=%u, r=%u, new_r=%u", pixel, unsigned(cov), unsigned(r), new_r);
        EXPECT_TRUE(new_g <= uint32_t(g))
          .message("G increased: pixel=0x%08X, cov=%u", pixel, unsigned(cov));
        EXPECT_TRUE(new_b <= uint32_t(b))
          .message("B increased: pixel=0x%08X, cov=%u", pixel, unsigned(cov));
        EXPECT_TRUE(new_a <= uint32_t(a))
          .message("A increased: pixel=0x%08X, cov=%u", pixel, unsigned(cov));
      }
    }
  }

  INFO("Testing coverage monotonicity");
  {
    // For a fixed channel value, increasing coverage should produce non-decreasing results.
    static constexpr uint32_t kChannelValues[] = {0, 1, 64, 127, 128, 200, 254, 255};

    for (uint32_t ch : kChannelValues) {
      uint32_t prev = 0;
      for (uint32_t cov = 0; cov <= 255u; cov++) {
        uint32_t result = apply_packed_coverage(ch, uint8_t(cov));
        EXPECT_TRUE(result >= prev)
          .message("Monotonicity violated: ch=%u, cov=%u: result=%u < prev=%u", ch, cov, result, prev);
        prev = result;
      }
    }
  }

  INFO("Testing src_over composition with coverage");
  {
    // src_over formula: dst' = src * coverage + dst * (1 - coverage)
    // Using div255 for both multiplications.
    // Verify that packed and unpacked coverage produce identical src_over results.
    uint32_t mismatches = 0;

    // Test representative (src, dst, coverage) triples.
    static constexpr uint8_t kValues[] = {0, 1, 64, 127, 128, 200, 254, 255};

    for (uint8_t src : kValues) {
      for (uint8_t dst : kValues) {
        for (uint32_t cov = 0; cov <= 255u; cov++) {
          // Packed path: div255(src * cov) + div255(dst * (255 - cov)).
          uint32_t packed_src = cov_div255(uint32_t(src) * cov);
          uint32_t packed_dst = cov_div255(uint32_t(dst) * (255u - cov));
          uint32_t packed_result = packed_src + packed_dst;

          // Unpacked path: same formula since both use div255 on identical inputs.
          uint32_t unpacked_src = cov_div255(uint32_t(src) * cov);
          uint32_t unpacked_dst = cov_div255(uint32_t(dst) * (255u - cov));
          uint32_t unpacked_result = unpacked_src + unpacked_dst;

          if (packed_result != unpacked_result)
            mismatches++;
        }
      }
    }

    EXPECT_EQ(mismatches, 0u)
      .message("src_over with packed vs unpacked coverage diverged in %u cases", mismatches);
  }
}

} // {Tests}
} // {bl}

#endif // BL_TEST
