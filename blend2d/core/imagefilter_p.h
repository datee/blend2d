// This file is part of Blend2D project <https://blend2d.com>
//
// See blend2d.h or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#ifndef BLEND2D_IMAGEFILTER_P_H_INCLUDED
#define BLEND2D_IMAGEFILTER_P_H_INCLUDED

#include <blend2d/core/image.h>
#include <blend2d/support/math_p.h>

//! \cond INTERNAL
//! \addtogroup blend2d_internal
//! \{

namespace bl {

//! Function signature for box blur horizontal/vertical pass.
typedef void (BL_CDECL* ImageFilterBlurFunc)(
  uint8_t* BL_RESTRICT dst, intptr_t dst_stride,
  const uint8_t* BL_RESTRICT src, intptr_t src_stride,
  int w, int h, int y_start, int y_end, int radius) noexcept;

//! Function pointer table for image filter operations, dispatched at runtime
//! based on CPU features. Follows the same pattern as `ImageScaleOps`.
struct ImageFilterOps {
  //! Horizontal box blur, indexed by BLFormat.
  ImageFilterBlurFunc box_blur_horz[BL_FORMAT_MAX_VALUE + 1];
  //! Vertical box blur, indexed by BLFormat.
  ImageFilterBlurFunc box_blur_vert[BL_FORMAT_MAX_VALUE + 1];
};

BL_HIDDEN extern ImageFilterOps image_filter_ops;

//! Compute 3 box widths that approximate a Gaussian with the given sigma.
//! Based on the W3C CSS specification for filter: blur().
static BL_INLINE void gaussian_box_widths(double sigma, int widths[3]) noexcept {
  if (sigma <= 0.0) {
    widths[0] = widths[1] = widths[2] = 1;
    return;
  }

  double ideal = Math::sqrt(12.0 * sigma * sigma / 3.0 + 1.0);
  int wl = int(ideal);
  if (wl % 2 == 0) wl--;
  int wu = wl + 2;

  double m_ideal = (12.0 * sigma * sigma - double(wl) * double(wl) * 3.0 - 4.0 * double(wl) - 3.0) / (-4.0 * double(wl) - 4.0);
  int m = int(m_ideal + 0.5);

  widths[0] = (0 < m) ? wl : wu;
  widths[1] = (1 < m) ? wl : wu;
  widths[2] = (2 < m) ? wl : wu;
}

} // {bl}

//! \}
//! \endcond

struct BLRuntimeContext;

// SSE2 optimized functions (declared here, defined in imagefilter_sse2.cpp).
BL_HIDDEN void bl_image_filter_box_blur_vert_prgb32_sse2(
  uint8_t* BL_RESTRICT dst, intptr_t dst_stride,
  const uint8_t* BL_RESTRICT src, intptr_t src_stride,
  int w, int h, int y_start, int y_end, int radius) noexcept;

BL_HIDDEN void bl_image_filter_box_blur_vert_prgb32_avx2(
  uint8_t* BL_RESTRICT dst, intptr_t dst_stride,
  const uint8_t* BL_RESTRICT src, intptr_t src_stride,
  int w, int h, int y_start, int y_end, int radius) noexcept;

// Runtime registration.
BL_HIDDEN void bl_image_filter_rt_init(BLRuntimeContext* rt) noexcept;

#endif // BLEND2D_IMAGEFILTER_P_H_INCLUDED
