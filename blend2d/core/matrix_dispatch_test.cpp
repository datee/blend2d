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
}

} // {BLTransformDispatchTests}

#endif // BL_TEST
