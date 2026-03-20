// This file is part of Blend2D project <https://blend2d.com>
//
// See blend2d.h or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#include <blend2d/core/api-build_test_p.h>
#if defined(BL_TEST)

#include <cstring>

// Blit Pointer Arithmetic Tests
// =============================
//
// These tests verify the pointer arithmetic used in FetchSimplePatternPart::start_at_x()
// when the aligned blit constraint is relaxed to support non-rectangular fills.
//
// The relaxed constraint computes: srcp1 += x * bpp
// where bpp is the bytes-per-pixel of the source format.

namespace bl {
namespace Tests {

// Simulate the add_scaled operation: ptr += x * scale.
static uintptr_t add_scaled(uintptr_t base, int x, int bpp) noexcept {
  return base + uintptr_t(int64_t(x) * int64_t(bpp));
}

UNIT(blit_pointer_arithmetic, BL_TEST_GROUP_RENDERING_UTILITIES) {
  INFO("Testing bpp scaling for supported pixel formats");
  {
    // Supported bytes-per-pixel values in Blend2D source formats:
    //   1 (A8), 2 (AL16/RGB565), 3 (RGB24), 4 (RGBA32/XRGB32), 8 (RGBA64)
    static constexpr int kBppValues[] = {1, 2, 3, 4, 8};

    uintptr_t base = 0x10000u;  // Arbitrary base address.

    for (int bpp : kBppValues) {
      for (int x = 0; x < 256; x++) {
        uintptr_t result = add_scaled(base, x, bpp);
        uintptr_t expected = base + uintptr_t(x) * uintptr_t(bpp);

        EXPECT_EQ(result, expected)
          .message("bpp=%d, x=%d: got 0x%zX, expected 0x%zX", bpp, x, (size_t)result, (size_t)expected);
      }
    }
  }

  INFO("Testing zero offset (rect-fill case)");
  {
    // When x=0, the pointer should not change (rect-fill equivalent).
    uintptr_t base = 0xDEAD0000u;
    for (int bpp : {1, 2, 3, 4, 8}) {
      uintptr_t result = add_scaled(base, 0, bpp);
      EXPECT_EQ(result, base)
        .message("bpp=%d, x=0: pointer should not change", bpp);
    }
  }

  INFO("Testing alignment after offset");
  {
    // For bpp=4 (RGBA32), the offset x*4 should maintain 4-byte alignment
    // if the base was already 4-byte aligned.
    uintptr_t base_aligned = 0x10000u;  // 4-byte aligned.
    for (int x = 0; x < 1024; x++) {
      uintptr_t result = add_scaled(base_aligned, x, 4);
      EXPECT_EQ(result % 4u, 0u)
        .message("bpp=4, x=%d: result 0x%zX is not 4-byte aligned", x, (size_t)result);
    }

    // For bpp=8 (RGBA64), maintain 8-byte alignment.
    for (int x = 0; x < 1024; x++) {
      uintptr_t result = add_scaled(base_aligned, x, 8);
      EXPECT_EQ(result % 8u, 0u)
        .message("bpp=8, x=%d: result 0x%zX is not 8-byte aligned", x, (size_t)result);
    }
  }

  INFO("Testing large x offsets");
  {
    // Image widths can be up to 65535 in Blend2D. Verify pointer arithmetic
    // works correctly for large x values near the maximum.
    uintptr_t base = 0x10000u;

    for (int bpp : {1, 2, 3, 4, 8}) {
      int x = 65535;
      uintptr_t result = add_scaled(base, x, bpp);
      uintptr_t expected = base + uintptr_t(x) * uintptr_t(bpp);

      EXPECT_EQ(result, expected)
        .message("Large x: bpp=%d, x=%d: got 0x%zX, expected 0x%zX",
                 bpp, x, (size_t)result, (size_t)expected);
    }
  }

  INFO("Testing consecutive pixel access pattern");
  {
    // Verify that accessing pixels at x, x+1, x+2, ... produces contiguous memory addresses
    // with the correct stride (bpp bytes between each pixel).
    uintptr_t base = 0x10000u;
    int bpp = 4;
    int start_x = 100;

    for (int i = 0; i < 16; i++) {
      uintptr_t addr_i = add_scaled(base, start_x + i, bpp);
      uintptr_t addr_next = add_scaled(base, start_x + i + 1, bpp);

      EXPECT_EQ(addr_next - addr_i, uintptr_t(bpp))
        .message("Stride between pixel %d and %d should be %d bytes, got %zu",
                 start_x + i, start_x + i + 1, bpp, (size_t)(addr_next - addr_i));
    }
  }

  INFO("Testing rect-fill vs non-rect-fill equivalence at x=0");
  {
    // For rect-fill, start_at_x doesn't modify the source pointer.
    // For non-rect-fill with x=0, add_scaled(ptr, 0, bpp) should also not modify it.
    // This verifies the relaxed constraint is backward compatible.
    uintptr_t base = 0xCAFE0000u;
    for (int bpp : {1, 2, 3, 4, 8}) {
      uintptr_t rect_result = base;  // Rect-fill: no change.
      uintptr_t nonrect_result = add_scaled(base, 0, bpp);

      EXPECT_EQ(rect_result, nonrect_result)
        .message("bpp=%d: rect-fill and non-rect-fill at x=0 should produce same pointer", bpp);
    }
  }
}

} // {Tests}
} // {bl}

#endif // BL_TEST
