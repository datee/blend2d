// This file is part of Blend2D project <https://blend2d.com>
//
// See blend2d.h or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#include <blend2d/core/api-build_p.h>
#include <blend2d/core/image_p.h>
#include <blend2d/core/imagefilter_p.h>
#include <blend2d/core/imagescale_p.h>
#include <blend2d/core/runtime_p.h>
#include <blend2d/support/intops_p.h>
#include <blend2d/support/math_p.h>
#include <blend2d/threading/threadpool_p.h>
#include <blend2d/threading/thread_p.h>
#include <blend2d/threading/atomic_p.h>

namespace bl {

// bl::ImageFilter - Ops Table
// ===========================

ImageFilterOps image_filter_ops;

// bl::ImageFilter - Scalar Implementations
// ========================================

// Horizontal box blur for PRGB32/XRGB32 — processes rows [y_start, y_end).
static void BL_CDECL box_blur_horz_prgb32(
    uint8_t* BL_RESTRICT dst, intptr_t dst_stride,
    const uint8_t* BL_RESTRICT src, intptr_t src_stride,
    int w, int h, int y_start, int y_end, int radius) noexcept {

  bl_unused(h);
  int kernel = radius * 2 + 1;
  uint32_t reciprocal = (1u << 24) / uint32_t(kernel);

  const uint8_t* src_line = src + y_start * src_stride;
  uint8_t* dst_line = dst + y_start * dst_stride;

  for (int y = y_start; y < y_end; y++) {
    const uint32_t* sp = reinterpret_cast<const uint32_t*>(src_line);
    uint32_t* dp = reinterpret_cast<uint32_t*>(dst_line);

    uint32_t acc_b = 0, acc_g = 0, acc_r = 0, acc_a = 0;
    for (int i = -radius; i <= radius; i++) {
      int xi = bl_clamp(i, 0, w - 1);
      uint32_t p = sp[xi];
      acc_b += (p >>  0) & 0xFF;
      acc_g += (p >>  8) & 0xFF;
      acc_r += (p >> 16) & 0xFF;
      acc_a += (p >> 24) & 0xFF;
    }

    for (int x = 0; x < w; x++) {
      dp[x] = ((acc_b * reciprocal) >> 24)       | (((acc_g * reciprocal) >> 24) << 8) |
              (((acc_r * reciprocal) >> 24) << 16)| (((acc_a * reciprocal) >> 24) << 24);

      int xi_add = bl_min(x + radius + 1, w - 1);
      int xi_sub = bl_max(x - radius, 0);
      uint32_t p_add = sp[xi_add], p_sub = sp[xi_sub];
      acc_b += ((p_add >>  0) & 0xFF) - ((p_sub >>  0) & 0xFF);
      acc_g += ((p_add >>  8) & 0xFF) - ((p_sub >>  8) & 0xFF);
      acc_r += ((p_add >> 16) & 0xFF) - ((p_sub >> 16) & 0xFF);
      acc_a += ((p_add >> 24) & 0xFF) - ((p_sub >> 24) & 0xFF);
    }

    src_line += src_stride;
    dst_line += dst_stride;
  }
}

// Vertical box blur for PRGB32/XRGB32 — scalar fallback, processes columns [y_start, y_end) is unused,
// processes ALL rows but only columns in range. Actually for vertical we split by column groups.
// Re-designed: processes all rows, columns [x_start, x_end) passed via y_start/y_end repurposed.
// Actually, let's keep the signature consistent: y_start/y_end for row range in horizontal,
// and for vertical we just process all rows but restrict to a column chunk.
// For threading, vertical pass splits columns, not rows.
static void BL_CDECL box_blur_vert_prgb32(
    uint8_t* BL_RESTRICT dst, intptr_t dst_stride,
    const uint8_t* BL_RESTRICT src, intptr_t src_stride,
    int w, int h, int x_start, int x_end, int radius) noexcept {

  bl_unused(w);
  int kernel = radius * 2 + 1;
  uint32_t reciprocal = (1u << 24) / uint32_t(kernel);

  for (int x = x_start; x < x_end; x++) {
    const uint8_t* src_col = src + x * 4;

    uint32_t acc_b = 0, acc_g = 0, acc_r = 0, acc_a = 0;
    for (int i = -radius; i <= radius; i++) {
      int yi = bl_clamp(i, 0, h - 1);
      uint32_t p = *reinterpret_cast<const uint32_t*>(src_col + yi * src_stride);
      acc_b += (p >>  0) & 0xFF;
      acc_g += (p >>  8) & 0xFF;
      acc_r += (p >> 16) & 0xFF;
      acc_a += (p >> 24) & 0xFF;
    }

    uint8_t* dst_col = dst + x * 4;
    for (int y = 0; y < h; y++) {
      uint32_t* dp = reinterpret_cast<uint32_t*>(dst_col + y * dst_stride);
      *dp = ((acc_b * reciprocal) >> 24)       | (((acc_g * reciprocal) >> 24) << 8) |
            (((acc_r * reciprocal) >> 24) << 16)| (((acc_a * reciprocal) >> 24) << 24);

      int yi_add = bl_min(y + radius + 1, h - 1);
      int yi_sub = bl_max(y - radius, 0);
      uint32_t p_add = *reinterpret_cast<const uint32_t*>(src_col + yi_add * src_stride);
      uint32_t p_sub = *reinterpret_cast<const uint32_t*>(src_col + yi_sub * src_stride);
      acc_b += ((p_add >>  0) & 0xFF) - ((p_sub >>  0) & 0xFF);
      acc_g += ((p_add >>  8) & 0xFF) - ((p_sub >>  8) & 0xFF);
      acc_r += ((p_add >> 16) & 0xFF) - ((p_sub >> 16) & 0xFF);
      acc_a += ((p_add >> 24) & 0xFF) - ((p_sub >> 24) & 0xFF);
    }
  }
}

// Horizontal box blur for A8 — processes rows [y_start, y_end).
static void BL_CDECL box_blur_horz_a8(
    uint8_t* BL_RESTRICT dst, intptr_t dst_stride,
    const uint8_t* BL_RESTRICT src, intptr_t src_stride,
    int w, int h, int y_start, int y_end, int radius) noexcept {

  bl_unused(h);
  int kernel = radius * 2 + 1;
  uint32_t reciprocal = (1u << 24) / uint32_t(kernel);

  const uint8_t* src_line = src + y_start * src_stride;
  uint8_t* dst_line = dst + y_start * dst_stride;

  for (int y = y_start; y < y_end; y++) {
    uint32_t acc = 0;
    for (int i = -radius; i <= radius; i++)
      acc += src_line[bl_clamp(i, 0, w - 1)];

    for (int x = 0; x < w; x++) {
      dst_line[x] = uint8_t((acc * reciprocal) >> 24);
      int xi_add = bl_min(x + radius + 1, w - 1);
      int xi_sub = bl_max(x - radius, 0);
      acc += src_line[xi_add] - src_line[xi_sub];
    }

    src_line += src_stride;
    dst_line += dst_stride;
  }
}

// Vertical box blur for A8 — processes columns [x_start, x_end).
static void BL_CDECL box_blur_vert_a8(
    uint8_t* BL_RESTRICT dst, intptr_t dst_stride,
    const uint8_t* BL_RESTRICT src, intptr_t src_stride,
    int w, int h, int x_start, int x_end, int radius) noexcept {

  bl_unused(w);
  int kernel = radius * 2 + 1;
  uint32_t reciprocal = (1u << 24) / uint32_t(kernel);

  for (int x = x_start; x < x_end; x++) {
    uint32_t acc = 0;
    for (int i = -radius; i <= radius; i++)
      acc += src[bl_clamp(i, 0, h - 1) * src_stride + x];

    for (int y = 0; y < h; y++) {
      dst[y * dst_stride + x] = uint8_t((acc * reciprocal) >> 24);
      int yi_add = bl_min(y + radius + 1, h - 1);
      int yi_sub = bl_max(y - radius, 0);
      acc += src[yi_add * src_stride + x] - src[yi_sub * src_stride + x];
    }
  }
}

// bl::ImageFilter - Threading
// ===========================

struct BlurWorkData {
  ImageFilterBlurFunc func;
  uint8_t* dst;
  intptr_t dst_stride;
  const uint8_t* src;
  intptr_t src_stride;
  int w, h, range_start, range_end, radius;
  std::atomic<uint32_t>* done_count;
};

static void BL_CDECL blur_thread_func(BLThread* thread, void* data) noexcept {
  bl_unused(thread);
  BlurWorkData* work = static_cast<BlurWorkData*>(data);
  work->func(work->dst, work->dst_stride, work->src, work->src_stride,
             work->w, work->h, work->range_start, work->range_end, work->radius);
  work->done_count->fetch_add(1, std::memory_order_release);
}

// Run a blur function with threading if threads are available.
static void run_blur_threaded(
    ImageFilterBlurFunc func,
    uint8_t* dst, intptr_t dst_stride,
    const uint8_t* src, intptr_t src_stride,
    int w, int h, int total_range, int radius) noexcept {

  // Try to acquire threads from global pool.
  BLThreadPool* pool = bl_thread_pool_global();

  constexpr int kMinRangePerThread = 32;
  uint32_t max_threads = uint32_t(bl_max(total_range / kMinRangePerThread, 1));
  max_threads = bl_min(max_threads, 8u);

  BLThread* threads[8];
  BLResult reason = BL_SUCCESS;
  uint32_t acquired = pool->acquire_threads(threads, max_threads, 0, &reason);

  if (acquired == 0) {
    // No threads available — run single-threaded.
    func(dst, dst_stride, src, src_stride, w, h, 0, total_range, radius);
    return;
  }

  // Split range across acquired threads + main thread.
  uint32_t total_workers = acquired + 1; // +1 for main thread
  int range_per_worker = total_range / int(total_workers);
  int remainder = total_range % int(total_workers);

  std::atomic<uint32_t> done_count{0};
  BlurWorkData work_items[8];

  int range_pos = 0;
  for (uint32_t i = 0; i < acquired; i++) {
    int this_range = range_per_worker + (int(i) < remainder ? 1 : 0);
    work_items[i].func = func;
    work_items[i].dst = dst;
    work_items[i].dst_stride = dst_stride;
    work_items[i].src = src;
    work_items[i].src_stride = src_stride;
    work_items[i].w = w;
    work_items[i].h = h;
    work_items[i].range_start = range_pos;
    work_items[i].range_end = range_pos + this_range;
    work_items[i].radius = radius;
    work_items[i].done_count = &done_count;
    range_pos += this_range;

    threads[i]->run(blur_thread_func, &work_items[i]);
  }

  // Main thread handles the remaining range.
  func(dst, dst_stride, src, src_stride, w, h, range_pos, total_range, radius);

  // Wait for worker threads to finish.
  while (done_count.load(std::memory_order_acquire) < acquired) {
    // Spin-wait. For short blur operations this is more efficient than futex.
  }

  pool->release_threads(threads, acquired);
}

// bl::ImageFilter - Box Blur Pass
// ================================

static BLResult box_blur_pass(BLImage& dst, const BLImage& src, int radius) noexcept {
  BLImageData src_data;
  BL_PROPAGATE(src.get_data(&src_data));

  int w = src_data.size.w;
  int h = src_data.size.h;
  uint32_t format = src_data.format;

  if (radius <= 0 || w <= 0 || h <= 0) {
    if (&dst != &src)
      return bl_image_assign_deep(static_cast<BLImageCore*>(&dst), static_cast<const BLImageCore*>(&src));
    return BL_SUCCESS;
  }

  if (!image_filter_ops.box_blur_horz[format] || !image_filter_ops.box_blur_vert[format])
    return bl_make_error(BL_ERROR_INVALID_VALUE);

  // Temporary image for intermediate horizontal pass result.
  BLImage tmp(w, h, BLFormat(format));
  BLImageData tmp_data;
  BL_PROPAGATE(tmp.make_mutable(&tmp_data));

  const uint8_t* src_pixels = static_cast<const uint8_t*>(src_data.pixel_data);
  uint8_t* tmp_pixels = static_cast<uint8_t*>(tmp_data.pixel_data);

  // Horizontal pass: src → tmp. Split by rows.
  run_blur_threaded(
    image_filter_ops.box_blur_horz[format],
    tmp_pixels, tmp_data.stride, src_pixels, src_data.stride,
    w, h, h, radius);

  // Vertical pass: tmp → dst. Split by columns.
  BL_PROPAGATE(bl_image_create(static_cast<BLImageCore*>(&dst), w, h, BLFormat(format)));
  BLImageData dst_data;
  BL_PROPAGATE(dst.make_mutable(&dst_data));
  uint8_t* dst_pixels = static_cast<uint8_t*>(dst_data.pixel_data);

  run_blur_threaded(
    image_filter_ops.box_blur_vert[format],
    dst_pixels, dst_data.stride, tmp_pixels, tmp_data.stride,
    w, h, w, radius);

  return BL_SUCCESS;
}

} // {bl}

// bl::ImageFilter - Public API
// ============================

BL_API_IMPL BLResult bl_image_filter(BLImageCore* dst, const BLImageCore* src, BLImageFilterType type, double radius, double quality) noexcept {
  using namespace bl::ImageInternal;

  BL_ASSERT(dst->_d.is_image());
  BL_ASSERT(src->_d.is_image());

  BLImagePrivateImpl* src_impl = get_impl(src);
  if (src_impl->format == BL_FORMAT_NONE)
    return bl_image_reset(dst);

  if (radius <= 0.0 || type == BL_IMAGE_FILTER_TYPE_NONE) {
    if (dst != src)
      return bl_image_assign_deep(dst, src);
    return BL_SUCCESS;
  }

  if (type > BL_IMAGE_FILTER_TYPE_MAX_VALUE)
    return bl_make_error(BL_ERROR_INVALID_VALUE);

  // Deep copy source if dst == src.
  BLImage src_copy;
  const BLImageCore* actual_src = src;
  if (dst == src) {
    src_copy = src->dcast();
    actual_src = static_cast<const BLImageCore*>(&src_copy);
  }

  if (type == BL_IMAGE_FILTER_TYPE_BOX_BLUR) {
    int r = int(radius + 0.5);
    if (r < 1) r = 1;
    return bl::box_blur_pass(dst->dcast(), actual_src->dcast(), r);
  }

  if (type == BL_IMAGE_FILTER_TYPE_GAUSSIAN_BLUR) {
    BLImagePrivateImpl* si = get_impl(actual_src);
    int orig_w = si->size.w;
    int orig_h = si->size.h;

    // Downscale optimization for large radii.
    double q = bl_clamp(quality, 0.0, 1.0);
    constexpr int kMinDownscaledSize = 16;

    double effective_radius = radius;
    bool use_downscale = (q < 1.0) && (orig_w > kMinDownscaledSize * 2) && (orig_h > kMinDownscaledSize * 2);
    BLImage downscaled;
    int scale_factor = 1;

    if (use_downscale) {
      double threshold = 4.0 + 36.0 * q;
      int max_scale = (q < 0.25) ? 16 : (q < 0.5) ? 8 : (q < 0.75) ? 4 : 2;

      while (effective_radius > threshold && scale_factor < max_scale &&
             orig_w / (scale_factor * 2) >= kMinDownscaledSize &&
             orig_h / (scale_factor * 2) >= kMinDownscaledSize) {
        scale_factor *= 2;
        effective_radius /= 2.0;
      }

      if (scale_factor > 1) {
        BLSizeI small_size(orig_w / scale_factor, orig_h / scale_factor);
        BL_PROPAGATE(BLImage::scale(downscaled, actual_src->dcast(), small_size, BL_IMAGE_SCALE_FILTER_BILINEAR));
        actual_src = static_cast<const BLImageCore*>(&downscaled);
      }
    }

    // 3-pass box blur approximation using ping-pong buffers to avoid repeated allocation.
    double sigma = effective_radius / 3.0;
    int widths[3];
    bl::gaussian_box_widths(sigma, widths);

    BLImage buf_a, buf_b;

    // Pass 1: src → buf_a
    BL_PROPAGATE(bl::box_blur_pass(buf_a, actual_src->dcast(), widths[0] / 2));

    // Pass 2: buf_a → buf_b (reuses buf_b allocation)
    if (widths[1] / 2 > 0) {
      BL_PROPAGATE(bl::box_blur_pass(buf_b, buf_a, widths[1] / 2));
    }
    else {
      buf_b = buf_a;
    }

    // Pass 3: buf_b → buf_a (reuses buf_a allocation)
    BLImage& blurred = buf_a;
    if (widths[2] / 2 > 0) {
      BL_PROPAGATE(bl::box_blur_pass(buf_a, buf_b, widths[2] / 2));
    }
    else {
      blurred = buf_b;
    }

    // Upscale back if downscaled.
    if (scale_factor > 1) {
      BLSizeI orig_size(orig_w, orig_h);
      BL_PROPAGATE(BLImage::scale(dst->dcast(), blurred, orig_size, BL_IMAGE_SCALE_FILTER_BILINEAR));
    }
    else {
      dst->dcast() = blurred;
    }

    return BL_SUCCESS;
  }

  return bl_make_error(BL_ERROR_INVALID_VALUE);
}

// bl::ImageFilter - Runtime Registration
// =======================================

void bl_image_filter_rt_init(BLRuntimeContext* rt) noexcept {
  bl::image_filter_ops.box_blur_horz[BL_FORMAT_PRGB32] = bl::box_blur_horz_prgb32;
  bl::image_filter_ops.box_blur_horz[BL_FORMAT_XRGB32] = bl::box_blur_horz_prgb32;
  bl::image_filter_ops.box_blur_horz[BL_FORMAT_A8    ] = bl::box_blur_horz_a8;

  bl::image_filter_ops.box_blur_vert[BL_FORMAT_PRGB32] = bl::box_blur_vert_prgb32;
  bl::image_filter_ops.box_blur_vert[BL_FORMAT_XRGB32] = bl::box_blur_vert_prgb32;
  bl::image_filter_ops.box_blur_vert[BL_FORMAT_A8    ] = bl::box_blur_vert_a8;

#ifdef BL_BUILD_OPT_SSE2
  if (bl_runtime_has_sse2(rt)) {
    bl::image_filter_ops.box_blur_vert[BL_FORMAT_PRGB32] = bl_image_filter_box_blur_vert_prgb32_sse2;
    bl::image_filter_ops.box_blur_vert[BL_FORMAT_XRGB32] = bl_image_filter_box_blur_vert_prgb32_sse2;
  }
#endif

#ifdef BL_BUILD_OPT_AVX2
  if (bl_runtime_has_avx2(rt)) {
    bl::image_filter_ops.box_blur_vert[BL_FORMAT_PRGB32] = bl_image_filter_box_blur_vert_prgb32_avx2;
    bl::image_filter_ops.box_blur_vert[BL_FORMAT_XRGB32] = bl_image_filter_box_blur_vert_prgb32_avx2;
  }
#endif

#if !defined(BL_BUILD_OPT_SSE2) && !defined(BL_BUILD_OPT_AVX2)
  bl_unused(rt);
#endif
}
