// This file is part of Blend2D project <https://blend2d.com>
//
// See blend2d.h or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#include <blend2d/core/api-build_test_p.h>
#if defined(BL_TEST)

#include <blend2d/core/matrix_p.h>
#include <blend2d/support/math_p.h>

// BLTransform - Dispatch Tests
// ============================
//
// Tests that the matrix function pointer dispatch table works correctly.
// This exercises the function pointers assigned during runtime initialization
// (previously via bl_assign_func, now via direct cast assignment).

namespace BLTransformDispatchTests {

static constexpr size_t kPointCount = 16u;

static bool approx_eq(double a, double b) noexcept {
  return bl_abs(a - b) < 1e-10;
}

UNIT(matrix_dispatch, BL_TEST_GROUP_GEOMETRY_UTILITIES) {
  BLPoint src[kPointCount];
  BLPoint dst[kPointCount];

  for (size_t i = 0; i < kPointCount; i++) {
    src[i].reset(double(i) * 1.5 + 1.0, double(i) * 2.5 + 3.0);
  }

  INFO("Testing identity dispatch");
  {
    BLMatrix2D m = BLMatrix2D::make_identity();
    EXPECT_SUCCESS(bl_matrix2d_map_pointd_array(&m, dst, src, kPointCount));

    for (size_t i = 0; i < kPointCount; i++) {
      EXPECT_TRUE(approx_eq(dst[i].x, src[i].x))
        .message("Identity: point %zu x mismatch", i);
      EXPECT_TRUE(approx_eq(dst[i].y, src[i].y))
        .message("Identity: point %zu y mismatch", i);
    }
  }

  INFO("Testing translation dispatch");
  {
    double tx = 10.0, ty = 20.0;
    BLMatrix2D m = BLMatrix2D::make_translation(tx, ty);
    EXPECT_SUCCESS(bl_matrix2d_map_pointd_array(&m, dst, src, kPointCount));

    for (size_t i = 0; i < kPointCount; i++) {
      EXPECT_TRUE(approx_eq(dst[i].x, src[i].x + tx))
        .message("Translate: point %zu x mismatch", i);
      EXPECT_TRUE(approx_eq(dst[i].y, src[i].y + ty))
        .message("Translate: point %zu y mismatch", i);
    }
  }

  INFO("Testing scale dispatch");
  {
    double sx = 2.0, sy = 3.0;
    BLMatrix2D m = BLMatrix2D::make_scaling(sx, sy);
    EXPECT_SUCCESS(bl_matrix2d_map_pointd_array(&m, dst, src, kPointCount));

    for (size_t i = 0; i < kPointCount; i++) {
      EXPECT_TRUE(approx_eq(dst[i].x, src[i].x * sx))
        .message("Scale: point %zu x mismatch", i);
      EXPECT_TRUE(approx_eq(dst[i].y, src[i].y * sy))
        .message("Scale: point %zu y mismatch", i);
    }
  }

  INFO("Testing swap dispatch");
  {
    // Swap matrix: m00=0, m01=1, m10=1, m11=0, m20=0, m21=0
    BLMatrix2D m(0.0, 1.0, 1.0, 0.0, 0.0, 0.0);
    EXPECT_EQ(m.type(), BL_TRANSFORM_TYPE_SWAP);
    EXPECT_SUCCESS(bl_matrix2d_map_pointd_array(&m, dst, src, kPointCount));

    for (size_t i = 0; i < kPointCount; i++) {
      EXPECT_TRUE(approx_eq(dst[i].x, src[i].y))
        .message("Swap: point %zu x mismatch", i);
      EXPECT_TRUE(approx_eq(dst[i].y, src[i].x))
        .message("Swap: point %zu y mismatch", i);
    }
  }

  INFO("Testing affine dispatch");
  {
    // General affine: rotation by ~45 degrees + translation
    BLMatrix2D m = BLMatrix2D::make_rotation(0.7853981633974483);
    m.post_translate(BLPoint(5.0, 10.0));
    EXPECT_EQ(m.type(), BL_TRANSFORM_TYPE_AFFINE);
    EXPECT_SUCCESS(bl_matrix2d_map_pointd_array(&m, dst, src, kPointCount));

    for (size_t i = 0; i < kPointCount; i++) {
      double expected_x = src[i].x * m.m00 + src[i].y * m.m10 + m.m20;
      double expected_y = src[i].x * m.m01 + src[i].y * m.m11 + m.m21;
      EXPECT_TRUE(approx_eq(dst[i].x, expected_x))
        .message("Affine: point %zu x mismatch", i);
      EXPECT_TRUE(approx_eq(dst[i].y, expected_y))
        .message("Affine: point %zu y mismatch", i);
    }
  }

  INFO("Testing scale with translation dispatch");
  {
    // make_scaling only sets m00/m11, leaving m20/m21 at 0. The scale dispatch function
    // also applies m20/m21, so we must test with non-zero translation to catch bugs.
    BLMatrix2D m = BLMatrix2D::make_scaling(2.0, 3.0);
    m.m20 = 100.0;
    m.m21 = 200.0;
    // Verify it's still classified as a scale transform (m00 != 0, m11 != 0, m01 == 0, m10 == 0).
    EXPECT_EQ(m.type(), BL_TRANSFORM_TYPE_SCALE);
    EXPECT_SUCCESS(bl_matrix2d_map_pointd_array(&m, dst, src, kPointCount));

    for (size_t i = 0; i < kPointCount; i++) {
      EXPECT_TRUE(approx_eq(dst[i].x, src[i].x * 2.0 + 100.0))
        .message("Scale+Translate: point %zu x mismatch", i);
      EXPECT_TRUE(approx_eq(dst[i].y, src[i].y * 3.0 + 200.0))
        .message("Scale+Translate: point %zu y mismatch", i);
    }
  }

  INFO("Testing swap with translation dispatch");
  {
    // Swap with non-trivial coefficients and translation offset.
    // m01=2, m10=3, m20=50, m21=60 (m00=0, m11=0)
    BLMatrix2D m(0.0, 2.0, 3.0, 0.0, 50.0, 60.0);
    EXPECT_EQ(m.type(), BL_TRANSFORM_TYPE_SWAP);
    EXPECT_SUCCESS(bl_matrix2d_map_pointd_array(&m, dst, src, kPointCount));

    for (size_t i = 0; i < kPointCount; i++) {
      // swap dispatch: dst.x = src.y * m10 + m20, dst.y = src.x * m01 + m21
      EXPECT_TRUE(approx_eq(dst[i].x, src[i].y * 3.0 + 50.0))
        .message("Swap+Translate: point %zu x mismatch", i);
      EXPECT_TRUE(approx_eq(dst[i].y, src[i].x * 2.0 + 60.0))
        .message("Swap+Translate: point %zu y mismatch", i);
    }
  }

  INFO("Testing in-place transform (dst == src)");
  {
    BLPoint pts[kPointCount];
    for (size_t i = 0; i < kPointCount; i++) {
      pts[i] = src[i];
    }

    double tx = 7.0, ty = 11.0;
    BLMatrix2D m = BLMatrix2D::make_translation(tx, ty);
    EXPECT_SUCCESS(bl_matrix2d_map_pointd_array(&m, pts, pts, kPointCount));

    for (size_t i = 0; i < kPointCount; i++) {
      EXPECT_TRUE(approx_eq(pts[i].x, src[i].x + tx))
        .message("In-place: point %zu x mismatch", i);
      EXPECT_TRUE(approx_eq(pts[i].y, src[i].y + ty))
        .message("In-place: point %zu y mismatch", i);
    }
  }

  INFO("Testing small count (< 16, always uses affine path)");
  {
    // When count < BL_MATRIX_TYPE_MINIMUM_SIZE (16), the function skips type detection
    // and always dispatches through the affine function pointer.
    BLPoint small_src[4] = {
      BLPoint(1.0, 2.0), BLPoint(3.0, 4.0), BLPoint(5.0, 6.0), BLPoint(7.0, 8.0)
    };
    BLPoint small_dst[4];

    // Use a translation matrix — but because count < 16, it goes through the affine path.
    double tx = 10.0, ty = 20.0;
    BLMatrix2D m = BLMatrix2D::make_translation(tx, ty);
    EXPECT_SUCCESS(bl_matrix2d_map_pointd_array(&m, small_dst, small_src, 4));

    for (size_t i = 0; i < 4; i++) {
      EXPECT_TRUE(approx_eq(small_dst[i].x, small_src[i].x + tx))
        .message("Small count: point %zu x mismatch", i);
      EXPECT_TRUE(approx_eq(small_dst[i].y, small_src[i].y + ty))
        .message("Small count: point %zu y mismatch", i);
    }

    // Also test with scale matrix through affine fallback.
    BLMatrix2D sm(2.0, 0.5, 0.3, 3.0, 10.0, 20.0);
    EXPECT_SUCCESS(bl_matrix2d_map_pointd_array(&sm, small_dst, small_src, 4));

    for (size_t i = 0; i < 4; i++) {
      double ex = small_src[i].x * sm.m00 + small_src[i].y * sm.m10 + sm.m20;
      double ey = small_src[i].x * sm.m01 + small_src[i].y * sm.m11 + sm.m21;
      EXPECT_TRUE(approx_eq(small_dst[i].x, ex))
        .message("Small count affine: point %zu x mismatch", i);
      EXPECT_TRUE(approx_eq(small_dst[i].y, ey))
        .message("Small count affine: point %zu y mismatch", i);
    }
  }

  INFO("Testing zero count");
  {
    BLMatrix2D m = BLMatrix2D::make_translation(999.0, 999.0);
    // Should succeed without touching any memory.
    EXPECT_SUCCESS(bl_matrix2d_map_pointd_array(&m, dst, src, 0));
  }

  INFO("Testing dispatch consistency across transform types");
  {
    // Verify that the specialized path produces the same result as the general affine formula
    // for each transform type. This is the strongest test: if the dispatch table has the wrong
    // function pointer for a type, the specialized result will diverge from the affine formula.
    struct TestCase {
      const char* name;
      BLMatrix2D matrix;
    };

    TestCase cases[] = {
      { "Identity",    BLMatrix2D::make_identity() },
      { "Translate",   BLMatrix2D::make_translation(17.5, -33.2) },
      { "Scale",       BLMatrix2D(2.5, 0.0, 0.0, -1.3, 11.0, 22.0) },
      { "Swap",        BLMatrix2D(0.0, -1.7, 2.3, 0.0, -5.0, 8.0) },
      { "Affine",      BLMatrix2D(1.1, 0.3, -0.4, 1.2, 5.5, -3.3) },
    };

    BLPoint affine_dst[kPointCount];

    for (const auto& tc : cases) {
      // Compute expected with general affine formula.
      for (size_t i = 0; i < kPointCount; i++) {
        affine_dst[i].reset(
          src[i].x * tc.matrix.m00 + src[i].y * tc.matrix.m10 + tc.matrix.m20,
          src[i].x * tc.matrix.m01 + src[i].y * tc.matrix.m11 + tc.matrix.m21);
      }

      // Compute via dispatch (uses specialized function pointer for the type).
      EXPECT_SUCCESS(bl_matrix2d_map_pointd_array(&tc.matrix, dst, src, kPointCount));

      for (size_t i = 0; i < kPointCount; i++) {
        EXPECT_TRUE(approx_eq(dst[i].x, affine_dst[i].x))
          .message("%s: point %zu x: dispatch=%.15g expected=%.15g", tc.name, i, dst[i].x, affine_dst[i].x);
        EXPECT_TRUE(approx_eq(dst[i].y, affine_dst[i].y))
          .message("%s: point %zu y: dispatch=%.15g expected=%.15g", tc.name, i, dst[i].y, affine_dst[i].y);
      }
    }
  }
}

} // {BLTransformDispatchTests}

#endif // BL_TEST
