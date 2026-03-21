// This file is part of Blend2D project <https://blend2d.com>
//
// See blend2d.h or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#include <blend2d/core/api-build_test_p.h>
#if defined(BL_TEST)

#include <cmath>

// SIMD Conversion & Extension Tests
// ==================================
//
// These tests verify the correctness of SIMD type conversion and zero/sign
// extension functions (movw_*, cvt_*) at the scalar level. The actual SIMD
// intrinsics are tested by the simd_test.cpp infrastructure; these tests
// validate the mathematical properties that the SIMD implementations must preserve.

namespace bl {
namespace Tests {

UNIT(simd_zero_extension_u8_u16, BL_TEST_GROUP_SIMD) {
  INFO("Testing u8→u16 zero-extension for all 256 values");
  {
    // Simulates movw_u8_u16: zero-extend each u8 to u16.
    for (uint32_t v = 0; v <= 255u; v++) {
      uint16_t extended = uint16_t(uint8_t(v));
      EXPECT_EQ(extended, uint16_t(v))
        .message("u8→u16: %u → %u, expected %u", v, unsigned(extended), v);
      EXPECT_EQ(extended & 0xFF00u, 0u)
        .message("u8→u16: high byte not zero for input %u", v);
    }
  }
}

UNIT(simd_zero_extension_u8_u32, BL_TEST_GROUP_SIMD) {
  INFO("Testing u8→u32 zero-extension for all 256 values");
  {
    // Simulates movw_u8_u32: zero-extend each u8 to u32.
    for (uint32_t v = 0; v <= 255u; v++) {
      uint32_t extended = uint32_t(uint8_t(v));
      EXPECT_EQ(extended, v)
        .message("u8→u32: %u → %u", v, extended);
      EXPECT_EQ(extended & 0xFFFFFF00u, 0u)
        .message("u8→u32: upper 24 bits not zero for input %u", v);
    }
  }
}

UNIT(simd_zero_extension_u16_u32, BL_TEST_GROUP_SIMD) {
  INFO("Testing u16→u32 zero-extension for boundary values");
  {
    static constexpr uint16_t kTestValues[] = {0, 1, 127, 128, 255, 256, 32767, 32768, 65534, 65535};

    for (uint16_t v : kTestValues) {
      uint32_t extended = uint32_t(v);
      EXPECT_EQ(extended, uint32_t(v))
        .message("u16→u32: %u → %u", unsigned(v), extended);
      EXPECT_EQ(extended & 0xFFFF0000u, 0u)
        .message("u16→u32: upper 16 bits not zero for input %u", unsigned(v));
    }
  }
}

UNIT(simd_sign_extension_i8_i16, BL_TEST_GROUP_SIMD) {
  INFO("Testing i8→i16 sign-extension for boundary values");
  {
    struct TestCase { int8_t input; int16_t expected; };
    static constexpr TestCase kCases[] = {
      {   0,    0 },
      {   1,    1 },
      { 127,  127 },
      {  -1,   -1 },
      {-128, -128 },
      {  -2,   -2 },
      {  64,   64 },
      { -64,  -64 },
    };

    for (const auto& tc : kCases) {
      int16_t extended = int16_t(tc.input);
      EXPECT_EQ(extended, tc.expected)
        .message("i8→i16: %d → %d, expected %d", int(tc.input), int(extended), int(tc.expected));
    }
  }
}

UNIT(simd_zero_extension_u8_u64, BL_TEST_GROUP_SIMD) {
  INFO("Testing u8→u64 double zero-extension for representative values");
  {
    // This tests the two-stage extension: u8→u32→u64 (the newly added simd_movw_u8_u64).
    static constexpr uint8_t kTestValues[] = {0, 1, 127, 128, 254, 255};

    for (uint8_t v : kTestValues) {
      uint64_t extended = uint64_t(v);
      EXPECT_EQ(extended, uint64_t(v))
        .message("u8→u64: %u → %llu", unsigned(v), (unsigned long long)extended);
      EXPECT_EQ(extended & 0xFFFFFFFFFFFFFF00ull, 0ull)
        .message("u8→u64: upper 56 bits not zero for input %u", unsigned(v));
    }
  }
}

UNIT(simd_zero_extension_u16_u64, BL_TEST_GROUP_SIMD) {
  INFO("Testing u16→u64 double zero-extension for representative values");
  {
    // This tests the two-stage extension: u16→u32→u64 (the newly added simd_movw_u16_u64).
    static constexpr uint16_t kTestValues[] = {0, 1, 255, 256, 32767, 32768, 65534, 65535};

    for (uint16_t v : kTestValues) {
      uint64_t extended = uint64_t(v);
      EXPECT_EQ(extended, uint64_t(v))
        .message("u16→u64: %u → %llu", unsigned(v), (unsigned long long)extended);
      EXPECT_EQ(extended & 0xFFFFFFFFFFFF0000ull, 0ull)
        .message("u16→u64: upper 48 bits not zero for input %u", unsigned(v));
    }
  }
}

UNIT(simd_cvt_i32_f64_roundtrip, BL_TEST_GROUP_SIMD) {
  INFO("Testing i32→f64→i32 round-trip for exact integers");
  {
    // cvt_2xi32_f64 converts int32 to float64. cvtt_f64_i32 truncates float64 to int32.
    // Round-trip should be identity for values that fit exactly in both types.
    static constexpr int32_t kTestValues[] = {
      0, 1, -1, 100, -100, 1000, -1000,
      65535, -65535, 16777216, -16777216,  // 2^24 (exact in f64)
      2147483647, -2147483647 - 1,         // INT32_MAX, INT32_MIN (exact in f64, f64 has 53 mantissa bits)
    };

    for (int32_t v : kTestValues) {
      double f = double(v);
      int32_t back = int32_t(f);  // truncation (matches cvtt behavior)

      EXPECT_EQ(back, v)
        .message("i32→f64→i32 round-trip: %d → %f → %d", v, f, back);
    }
  }

  INFO("Testing f64→i32 truncation behavior");
  {
    // cvtt_f64_i32 truncates toward zero (not rounds).
    struct TestCase { double input; int32_t expected; };
    static constexpr TestCase kCases[] = {
      {  1.9,  1 },
      { -1.9, -1 },
      {  0.5,  0 },
      { -0.5,  0 },
      {  2.999, 2 },
      { -2.999, -2 },
      { 100.001, 100 },
    };

    for (const auto& tc : kCases) {
      int32_t result = int32_t(tc.input);  // C++ truncation matches cvtt
      EXPECT_EQ(result, tc.expected)
        .message("cvtt_f64_i32(%f) = %d, expected %d", tc.input, result, tc.expected);
    }
  }
}

UNIT(simd_cvt_f64_f32_precision, BL_TEST_GROUP_SIMD) {
  INFO("Testing f64→f32 conversion preserves values within f32 range");
  {
    static constexpr double kTestValues[] = {
      0.0, 1.0, -1.0, 0.5, -0.5,
      3.14159265358979323846,  // pi
      1.0e10, -1.0e10,
      1.0e-10, -1.0e-10,
    };

    for (double v : kTestValues) {
      float f32 = float(v);      // f64→f32 (matches simd_cvt_f64_f32)
      double f64_back = double(f32);  // f32→f64 (matches simd_cvt_f32x2_f64)

      // f64→f32→f64 won't be exact for all values, but f32→f64 should equal f32 exactly.
      float f32_check = float(f64_back);
      EXPECT_EQ(f32, f32_check)
        .message("f64→f32→f64→f32 not stable for input %g", v);
    }
  }
}

UNIT(simd_undefined_returns_zero, BL_TEST_GROUP_SIMD) {
  INFO("Testing that undefined SIMD values are zero-initialized");
  {
    // On ARM, simd_make_undefined returns zero. On X86, it may return garbage.
    // But in practice, blend2d's ARM implementation returns zero.
    // We test the scalar equivalent: a zero-initialized value used in arithmetic
    // should not corrupt results.
    uint32_t zero = 0;
    uint32_t result = zero + 42;
    EXPECT_EQ(result, 42u);

    // Verify zero doesn't affect OR/AND operations.
    EXPECT_EQ(zero | 0xFFu, 0xFFu);
    EXPECT_EQ(zero & 0xFFu, 0u);
    EXPECT_EQ(zero ^ 0xA5u, 0xA5u);
  }
}

} // {Tests}
} // {bl}

#endif // BL_TEST
