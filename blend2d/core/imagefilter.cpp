// This file is part of Blend2D project <https://blend2d.com>
//
// See blend2d.h or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#include <blend2d/core/api-build_p.h>
#include <cmath>
#include <blend2d/core/context.h>
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

// bl::ImageFilter - True Gaussian Kernel Convolution
// ==================================================

// Horizontal pass with weighted kernel for true Gaussian blur.
static void BL_CDECL gaussian_conv_horz_prgb32(
    uint8_t* BL_RESTRICT dst, intptr_t dst_stride,
    const uint8_t* BL_RESTRICT src, intptr_t src_stride,
    int w, int h, int y_start, int y_end,
    const int32_t* weights, int kernel_radius, int weight_shift) noexcept {

  const uint8_t* src_line = src + y_start * src_stride;
  uint8_t* dst_line = dst + y_start * dst_stride;

  for (int y = y_start; y < y_end; y++) {
    const uint32_t* sp = reinterpret_cast<const uint32_t*>(src_line);
    uint32_t* dp = reinterpret_cast<uint32_t*>(dst_line);

    for (int x = 0; x < w; x++) {
      int32_t acc_b = 0, acc_g = 0, acc_r = 0, acc_a = 0;

      for (int k = -kernel_radius; k <= kernel_radius; k++) {
        int xi = bl_clamp(x + k, 0, w - 1);
        uint32_t p = sp[xi];
        int32_t wt = weights[k + kernel_radius];
        acc_b += int32_t((p >>  0) & 0xFF) * wt;
        acc_g += int32_t((p >>  8) & 0xFF) * wt;
        acc_r += int32_t((p >> 16) & 0xFF) * wt;
        acc_a += int32_t((p >> 24) & 0xFF) * wt;
      }

      dp[x] = uint32_t(bl_clamp(acc_b >> weight_shift, 0, 255))       |
              uint32_t(bl_clamp(acc_g >> weight_shift, 0, 255)) << 8  |
              uint32_t(bl_clamp(acc_r >> weight_shift, 0, 255)) << 16 |
              uint32_t(bl_clamp(acc_a >> weight_shift, 0, 255)) << 24;
    }

    src_line += src_stride;
    dst_line += dst_stride;
  }
}

// Vertical pass with weighted kernel for true Gaussian blur.
static void BL_CDECL gaussian_conv_vert_prgb32(
    uint8_t* BL_RESTRICT dst, intptr_t dst_stride,
    const uint8_t* BL_RESTRICT src, intptr_t src_stride,
    int w, int h, int x_start, int x_end,
    const int32_t* weights, int kernel_radius, int weight_shift) noexcept {

  for (int x = x_start; x < x_end; x++) {
    for (int y = 0; y < h; y++) {
      int32_t acc_b = 0, acc_g = 0, acc_r = 0, acc_a = 0;

      for (int k = -kernel_radius; k <= kernel_radius; k++) {
        int yi = bl_clamp(y + k, 0, h - 1);
        uint32_t p = *reinterpret_cast<const uint32_t*>(src + yi * src_stride + x * 4);
        int32_t wt = weights[k + kernel_radius];
        acc_b += int32_t((p >>  0) & 0xFF) * wt;
        acc_g += int32_t((p >>  8) & 0xFF) * wt;
        acc_r += int32_t((p >> 16) & 0xFF) * wt;
        acc_a += int32_t((p >> 24) & 0xFF) * wt;
      }

      *reinterpret_cast<uint32_t*>(dst + y * dst_stride + x * 4) =
        uint32_t(bl_clamp(acc_b >> weight_shift, 0, 255))       |
        uint32_t(bl_clamp(acc_g >> weight_shift, 0, 255)) << 8  |
        uint32_t(bl_clamp(acc_r >> weight_shift, 0, 255)) << 16 |
        uint32_t(bl_clamp(acc_a >> weight_shift, 0, 255)) << 24;
    }
  }
}

// Apply true Gaussian kernel convolution (separable: H then V).
static BLResult true_gaussian_blur(BLImage& dst, const BLImage& src, double radius) noexcept {
  BLImageData src_data;
  BL_PROPAGATE(src.get_data(&src_data));

  int w = src_data.size.w;
  int h = src_data.size.h;
  if (w <= 0 || h <= 0 || radius <= 0.0)
    return BL_SUCCESS;

  double sigma = radius / 3.0;
  int kernel_radius = int(Math::ceil(sigma * 3.0));
  if (kernel_radius < 1) kernel_radius = 1;
  int kernel_size = kernel_radius * 2 + 1;

  // Compute kernel weights as fixed-point (shift=16).
  constexpr int kWeightShift = 16;
  int32_t* weights = static_cast<int32_t*>(malloc(size_t(kernel_size) * sizeof(int32_t)));
  if (!weights) return bl_make_error(BL_ERROR_OUT_OF_MEMORY);

  double weight_sum = 0.0;
  for (int i = 0; i < kernel_size; i++) {
    double x = double(i - kernel_radius);
    double w_f = ::exp(-(x * x) / (2.0 * sigma * sigma));
    weights[i] = int32_t(w_f * 65536.0);
    weight_sum += w_f;
  }

  // Normalize so weights sum to (1 << kWeightShift).
  double normalize = double(1 << kWeightShift) / weight_sum;
  for (int i = 0; i < kernel_size; i++) {
    double x = double(i - kernel_radius);
    weights[i] = int32_t(::exp(-(x * x) / (2.0 * sigma * sigma)) * normalize + 0.5);
  }

  uint32_t format = src_data.format;

  // Horizontal pass: src → tmp
  BLImage tmp(w, h, BLFormat(format));
  BLImageData tmp_data;
  BL_PROPAGATE(tmp.make_mutable(&tmp_data));

  gaussian_conv_horz_prgb32(
    static_cast<uint8_t*>(tmp_data.pixel_data), tmp_data.stride,
    static_cast<const uint8_t*>(src_data.pixel_data), src_data.stride,
    w, h, 0, h, weights, kernel_radius, kWeightShift);

  // Vertical pass: tmp → dst
  BL_PROPAGATE(bl_image_create(static_cast<BLImageCore*>(&dst), w, h, BLFormat(format)));
  BLImageData dst_data;
  BL_PROPAGATE(dst.make_mutable(&dst_data));

  gaussian_conv_vert_prgb32(
    static_cast<uint8_t*>(dst_data.pixel_data), dst_data.stride,
    static_cast<uint8_t*>(tmp_data.pixel_data), tmp_data.stride,
    w, h, 0, w, weights, kernel_radius, kWeightShift);

  free(weights);
  return BL_SUCCESS;
}

// bl::ImageFilter - Spread (Choke Alpha)
// =======================================

// Apply spread to an A8 mask: thickens the alpha by lerping toward fully opaque.
// spread=0 → unchanged, spread=1 → fully hard edge (binary alpha).
static BLResult apply_spread_a8(BLImage& mask, double spread) noexcept {
  if (spread <= 0.0) return BL_SUCCESS;
  spread = bl_clamp(spread, 0.0, 1.0);

  BLImageData data;
  BL_PROPAGATE(mask.make_mutable(&data));

  int w = data.size.w;
  int h = data.size.h;
  uint8_t* line = static_cast<uint8_t*>(data.pixel_data);

  // Spread: remap alpha curve. spread=0 → linear, spread=1 → step function at threshold.
  // Formula: new_alpha = clamp(alpha * (1 + spread * factor) - spread * threshold, 0, 255)
  // Simpler: use a power curve. spread=0 → gamma=1 (linear), spread=1 → gamma→0 (step).
  double gamma = 1.0 - spread * 0.9; // gamma from 1.0 down to 0.1

  uint8_t lut[256];
  for (int i = 0; i < 256; i++) {
    double v = double(i) / 255.0;
    double adjusted = ::pow(v, gamma);
    lut[i] = uint8_t(bl_clamp(int(adjusted * 255.0 + 0.5), 0, 255));
  }

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++)
      line[x] = lut[line[x]];
    line += data.stride;
  }
  return BL_SUCCESS;
}

// bl::ImageFilter - Opacity
// =========================

// Apply opacity to a PRGB32 image: scale all channels by opacity factor.
static BLResult apply_opacity_prgb32(BLImage& img, double opacity) noexcept {
  if (opacity >= 1.0) return BL_SUCCESS;
  opacity = bl_clamp(opacity, 0.0, 1.0);

  BLImageData data;
  BL_PROPAGATE(img.make_mutable(&data));

  int w = data.size.w;
  int h = data.size.h;
  uint32_t op = uint32_t(opacity * 256.0 + 0.5);
  uint8_t* line = static_cast<uint8_t*>(data.pixel_data);

  for (int y = 0; y < h; y++) {
    uint32_t* row = reinterpret_cast<uint32_t*>(line);
    for (int x = 0; x < w; x++) {
      uint32_t p = row[x];
      uint32_t b = ((p >>  0) & 0xFF) * op >> 8;
      uint32_t g = ((p >>  8) & 0xFF) * op >> 8;
      uint32_t r = ((p >> 16) & 0xFF) * op >> 8;
      uint32_t a = ((p >> 24) & 0xFF) * op >> 8;
      row[x] = b | (g << 8) | (r << 16) | (a << 24);
    }
    line += data.stride;
  }
  return BL_SUCCESS;
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

// bl::ImageFilter - Effect Helpers
// =================================

namespace bl {

// Extract alpha channel from PRGB32 image to A8 image.
static BLResult extract_alpha(BLImage& dst_a8, const BLImage& src_prgb32) noexcept {
  BLImageData src_data;
  BL_PROPAGATE(src_prgb32.get_data(&src_data));

  int w = src_data.size.w;
  int h = src_data.size.h;

  BL_PROPAGATE(bl_image_create(static_cast<BLImageCore*>(&dst_a8), w, h, BL_FORMAT_A8));
  BLImageData dst_data;
  BL_PROPAGATE(dst_a8.make_mutable(&dst_data));

  const uint8_t* src_line = static_cast<const uint8_t*>(src_data.pixel_data);
  uint8_t* dst_line = static_cast<uint8_t*>(dst_data.pixel_data);

  for (int y = 0; y < h; y++) {
    const uint32_t* sp = reinterpret_cast<const uint32_t*>(src_line);
    for (int x = 0; x < w; x++) {
      dst_line[x] = uint8_t(sp[x] >> 24);
    }
    src_line += src_data.stride;
    dst_line += dst_data.stride;
  }
  return BL_SUCCESS;
}

// Colorize an A8 mask into a PRGB32 image with the given color.
static BLResult colorize_mask(BLImage& dst_prgb32, const BLImage& mask_a8, uint32_t color) noexcept {
  BLImageData mask_data;
  BL_PROPAGATE(mask_a8.get_data(&mask_data));

  int w = mask_data.size.w;
  int h = mask_data.size.h;

  BL_PROPAGATE(bl_image_create(static_cast<BLImageCore*>(&dst_prgb32), w, h, BL_FORMAT_PRGB32));
  BLImageData dst_data;
  BL_PROPAGATE(dst_prgb32.make_mutable(&dst_data));

  // Premultiply color.
  uint32_t ca = (color >> 24) & 0xFF;
  uint32_t cr = ((color >> 16) & 0xFF) * ca / 255;
  uint32_t cg = ((color >>  8) & 0xFF) * ca / 255;
  uint32_t cb = ((color >>  0) & 0xFF) * ca / 255;

  const uint8_t* mask_line = static_cast<const uint8_t*>(mask_data.pixel_data);
  uint8_t* dst_line = static_cast<uint8_t*>(dst_data.pixel_data);

  for (int y = 0; y < h; y++) {
    uint32_t* dp = reinterpret_cast<uint32_t*>(dst_line);
    for (int x = 0; x < w; x++) {
      uint32_t a = mask_line[x];
      // Scale premultiplied color by mask alpha.
      uint32_t ob = (cb * a / 255);
      uint32_t og = (cg * a / 255);
      uint32_t or_ = (cr * a / 255);
      uint32_t oa = (ca * a / 255);
      dp[x] = ob | (og << 8) | (or_ << 16) | (oa << 24);
    }
    mask_line += mask_data.stride;
    dst_line += dst_data.stride;
  }
  return BL_SUCCESS;
}

} // {bl}

// bl::ImageFilter - Apply Effect (Public API)
// ============================================

BL_API_IMPL BLResult bl_image_apply_effect(BLImageCore* dst, const BLImageCore* src, const BLImageEffectOptions* options) noexcept {
  using namespace bl::ImageInternal;

  BL_ASSERT(dst->_d.is_image());
  BL_ASSERT(src->_d.is_image());

  if (!options || options->type == BL_IMAGE_EFFECT_TYPE_NONE) {
    if (dst != src)
      return bl_image_assign_deep(dst, src);
    return BL_SUCCESS;
  }

  if (options->type > BL_IMAGE_EFFECT_TYPE_MAX_VALUE)
    return bl_make_error(BL_ERROR_INVALID_VALUE);

  double radius = options->radius;
  double quality = bl_clamp(options->quality, 0.0, 1.0);

  double opacity = (options->opacity > 0.0) ? bl_clamp(options->opacity, 0.0, 1.0) : 1.0;
  double spread = bl_clamp(options->spread, 0.0, 1.0);

  // Automatic algorithm selection based on quality tier.
  auto select_blur = [&](BLImage& blur_dst, const BLImage& blur_src) -> BLResult {
    if (quality < 0.3) {
      // Low quality: single box blur pass (fastest).
      return bl_image_filter(
        static_cast<BLImageCore*>(&blur_dst),
        static_cast<const BLImageCore*>(&blur_src),
        BL_IMAGE_FILTER_TYPE_BOX_BLUR, radius, quality);
    }
    else if (quality < 0.95) {
      // Medium/High: Gaussian approximation via 3-pass box blur.
      return bl_image_filter(
        static_cast<BLImageCore*>(&blur_dst),
        static_cast<const BLImageCore*>(&blur_src),
        BL_IMAGE_FILTER_TYPE_GAUSSIAN_BLUR, radius, quality);
    }
    else {
      // Ultra: true Gaussian kernel convolution (mathematically exact).
      return bl::true_gaussian_blur(blur_dst, blur_src, radius);
    }
  };

  if (options->type == BL_IMAGE_EFFECT_TYPE_BLUR) {
    // Simple blur — delegate to the right algorithm.
    BLImage src_copy;
    const BLImageCore* actual_src = src;
    if (dst == src) {
      src_copy = src->dcast();
      actual_src = static_cast<const BLImageCore*>(&src_copy);
    }
    return select_blur(dst->dcast(), actual_src->dcast());
  }

  if (options->type == BL_IMAGE_EFFECT_TYPE_GLOW) {
    BLImagePrivateImpl* si = get_impl(src);
    int w = si->size.w;
    int h = si->size.h;
    uint32_t flags = options->flags;
    bool inner = (flags & BL_IMAGE_EFFECT_FLAG_INNER) != 0;
    bool knockout = (flags & BL_IMAGE_EFFECT_FLAG_KNOCKOUT) != 0;

    // Step 1: Extract alpha, apply spread (choke), then blur to create the glow shape.
    BLImage alpha_mask;
    BL_PROPAGATE(bl::extract_alpha(alpha_mask, src->dcast()));
    if (spread > 0.0)
      BL_PROPAGATE(bl::apply_spread_a8(alpha_mask, spread));

    BLImage blurred_mask;
    BL_PROPAGATE(bl_image_filter(
      static_cast<BLImageCore*>(&blurred_mask),
      static_cast<const BLImageCore*>(&alpha_mask),
      (quality < 0.3) ? BL_IMAGE_FILTER_TYPE_BOX_BLUR : BL_IMAGE_FILTER_TYPE_GAUSSIAN_BLUR,
      radius, quality));

    // Step 2: Colorize the blurred mask with the glow color.
    BLImage glow_layer;
    BL_PROPAGATE(bl::colorize_mask(glow_layer, blurred_mask, options->color));

    // Apply strength: multiply glow brightness (values > 1 make it brighter).
    double strength = (options->strength > 0.0) ? options->strength : 1.0;
    if (strength != 1.0) {
      BLImageData gd;
      glow_layer.make_mutable(&gd);
      uint8_t* gp = static_cast<uint8_t*>(gd.pixel_data);
      int32_t s = int32_t(strength * 256.0 + 0.5);
      for (int y = 0; y < h; y++) {
        uint32_t* row = reinterpret_cast<uint32_t*>(gp);
        for (int x = 0; x < w; x++) {
          uint32_t p = row[x];
          uint32_t b = bl_min(uint32_t(int32_t((p >>  0) & 0xFF) * s >> 8), 255u);
          uint32_t g = bl_min(uint32_t(int32_t((p >>  8) & 0xFF) * s >> 8), 255u);
          uint32_t r = bl_min(uint32_t(int32_t((p >> 16) & 0xFF) * s >> 8), 255u);
          uint32_t a = bl_min(uint32_t(int32_t((p >> 24) & 0xFF) * s >> 8), 255u);
          row[x] = b | (g << 8) | (r << 16) | (a << 24);
        }
        gp += gd.stride;
      }
    }

    // Step 3: For inner glow, mask the glow to only appear inside the original shape.
    // For outer glow, the glow naturally appears outside (blurred edges extend beyond shape).
    if (inner) {
      // Inner glow: intersect glow with original alpha (only visible inside the shape).
      // Use DstIn: keeps glow pixels only where original has alpha.
      BLImage masked_glow(w, h, BL_FORMAT_PRGB32);
      {
        BLContext ctx(masked_glow);
        ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
        ctx.blit_image(BLPoint(0, 0), glow_layer);
        // Invert the glow first: for inner glow we want the blur falloff going inward.
        // Inner glow = blur of the *inverted* alpha, then mask to original shape.
        ctx.end();
      }

      // Better approach: invert the alpha mask, blur it, colorize, then mask with original shape.
      // Inverted alpha: opaque outside shape, transparent inside → blur creates soft edge inside.
      BLImage inv_alpha(w, h, BL_FORMAT_A8);
      {
        BLImageData src_a, dst_a;
        alpha_mask.get_data(&src_a);
        inv_alpha.make_mutable(&dst_a);
        const uint8_t* sp = static_cast<const uint8_t*>(src_a.pixel_data);
        uint8_t* dp = static_cast<uint8_t*>(dst_a.pixel_data);
        for (int y = 0; y < h; y++) {
          for (int x = 0; x < w; x++)
            dp[x] = uint8_t(255 - sp[x]);
          sp += src_a.stride;
          dp += dst_a.stride;
        }
      }

      BLImage blurred_inv;
      BL_PROPAGATE(bl_image_filter(
        static_cast<BLImageCore*>(&blurred_inv),
        static_cast<const BLImageCore*>(&inv_alpha),
        (quality < 0.3) ? BL_IMAGE_FILTER_TYPE_BOX_BLUR : BL_IMAGE_FILTER_TYPE_GAUSSIAN_BLUR,
        radius, quality));

      BLImage inner_glow;
      BL_PROPAGATE(bl::colorize_mask(inner_glow, blurred_inv, options->color));

      // Mask inner glow to only appear inside original shape (DstIn with original alpha).
      glow_layer = BLImage(w, h, BL_FORMAT_PRGB32);
      {
        BLContext ctx(glow_layer);
        ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
        ctx.blit_image(BLPoint(0, 0), inner_glow);

        // Clip to original shape by blitting source alpha as mask via DstIn.
        // Since we can't use DstIn comp op in portable pipeline, manually mask pixel-by-pixel.
        ctx.end();
      }

      // Manual alpha masking: multiply glow alpha by source alpha.
      {
        BLImageData glow_data, alpha_data;
        glow_layer.make_mutable(&glow_data);
        alpha_mask.get_data(&alpha_data);

        uint8_t* gp = static_cast<uint8_t*>(glow_data.pixel_data);
        const uint8_t* ap = static_cast<const uint8_t*>(alpha_data.pixel_data);

        for (int y = 0; y < h; y++) {
          uint32_t* grow = reinterpret_cast<uint32_t*>(gp);
          for (int x = 0; x < w; x++) {
            uint32_t pixel = grow[x];
            uint32_t mask_a = ap[x];
            // Scale each channel by mask alpha.
            uint32_t pb = ((pixel >>  0) & 0xFF) * mask_a / 255;
            uint32_t pg = ((pixel >>  8) & 0xFF) * mask_a / 255;
            uint32_t pr = ((pixel >> 16) & 0xFF) * mask_a / 255;
            uint32_t pa = ((pixel >> 24) & 0xFF) * mask_a / 255;
            grow[x] = pb | (pg << 8) | (pr << 16) | (pa << 24);
          }
          gp += glow_data.stride;
          ap += alpha_data.stride;
        }
      }
    }

    // Apply opacity to glow layer.
    if (opacity < 1.0)
      bl::apply_opacity_prgb32(glow_layer, opacity);

    // Step 4: Composite.
    BLImage result(w, h, BL_FORMAT_PRGB32);
    {
      BLContext ctx(result);
      ctx.clear_all();
      ctx.set_comp_op(BL_COMP_OP_SRC_OVER);

      if (!inner && !knockout) {
        // Outer glow: glow behind original.
        ctx.blit_image(BLPoint(0, 0), glow_layer);
        ctx.blit_image(BLPoint(0, 0), src->dcast());
      }
      else if (!inner && knockout) {
        // Outer glow knockout: only the glow, no original shape.
        ctx.blit_image(BLPoint(0, 0), glow_layer);
      }
      else if (inner && !knockout) {
        // Inner glow: original + glow on top (inside shape).
        ctx.blit_image(BLPoint(0, 0), src->dcast());
        ctx.blit_image(BLPoint(0, 0), glow_layer);
      }
      else {
        // Inner glow knockout: only the inner glow, no original.
        ctx.blit_image(BLPoint(0, 0), glow_layer);
      }
      ctx.end();
    }

    dst->dcast() = result;
    return BL_SUCCESS;
  }

  if (options->type == BL_IMAGE_EFFECT_TYPE_DROP_SHADOW) {
    BLImagePrivateImpl* si = get_impl(src);
    int w = si->size.w;
    int h = si->size.h;
    uint32_t flags = options->flags;
    bool inner = (flags & BL_IMAGE_EFFECT_FLAG_INNER) != 0;
    bool knockout = (flags & BL_IMAGE_EFFECT_FLAG_KNOCKOUT) != 0;

    int offset_x = int(options->offset_x);
    int offset_y = int(options->offset_y);

    // Step 1: Extract alpha, apply spread, from source.
    BLImage alpha_mask;
    BL_PROPAGATE(bl::extract_alpha(alpha_mask, src->dcast()));
    if (spread > 0.0)
      BL_PROPAGATE(bl::apply_spread_a8(alpha_mask, spread));

    BLImage shadow_layer;

    if (inner) {
      // Inner shadow: invert alpha, blur, colorize, mask to original shape, offset inside.
      BLImage inv_alpha(w, h, BL_FORMAT_A8);
      {
        BLImageData src_a, dst_a;
        alpha_mask.get_data(&src_a);
        inv_alpha.make_mutable(&dst_a);
        const uint8_t* sp = static_cast<const uint8_t*>(src_a.pixel_data);
        uint8_t* dp = static_cast<uint8_t*>(dst_a.pixel_data);
        for (int y = 0; y < h; y++) {
          for (int x = 0; x < w; x++)
            dp[x] = uint8_t(255 - sp[x]);
          sp += src_a.stride;
          dp += dst_a.stride;
        }
      }

      BLImage blurred_inv;
      BL_PROPAGATE(bl_image_filter(
        static_cast<BLImageCore*>(&blurred_inv),
        static_cast<const BLImageCore*>(&inv_alpha),
        (quality < 0.3) ? BL_IMAGE_FILTER_TYPE_BOX_BLUR : BL_IMAGE_FILTER_TYPE_GAUSSIAN_BLUR,
        radius, quality));

      BLImage inner_shadow;
      BL_PROPAGATE(bl::colorize_mask(inner_shadow, blurred_inv, options->color));

      // Mask to original shape and apply offset.
      shadow_layer = BLImage(w, h, BL_FORMAT_PRGB32);
      {
        BLContext ctx(shadow_layer);
        ctx.clear_all();
        ctx.set_comp_op(BL_COMP_OP_SRC_OVER);
        ctx.blit_image(BLPoint(offset_x, offset_y), inner_shadow);
        ctx.end();
      }

      // Mask with original alpha.
      {
        BLImageData shd_data, alp_data;
        shadow_layer.make_mutable(&shd_data);
        alpha_mask.get_data(&alp_data);
        uint8_t* sp = static_cast<uint8_t*>(shd_data.pixel_data);
        const uint8_t* ap = static_cast<const uint8_t*>(alp_data.pixel_data);
        for (int y = 0; y < h; y++) {
          uint32_t* row = reinterpret_cast<uint32_t*>(sp);
          for (int x = 0; x < w; x++) {
            uint32_t p = row[x];
            uint32_t ma = ap[x];
            row[x] = (((p >>  0) & 0xFF) * ma / 255)       |
                     ((((p >>  8) & 0xFF) * ma / 255) << 8) |
                     ((((p >> 16) & 0xFF) * ma / 255) << 16)|
                     ((((p >> 24) & 0xFF) * ma / 255) << 24);
          }
          sp += shd_data.stride;
          ap += alp_data.stride;
        }
      }

      // Apply opacity.
      if (opacity < 1.0)
        bl::apply_opacity_prgb32(shadow_layer, opacity);

      // Composite: inner shadow on/with original.
      BLImage result(w, h, BL_FORMAT_PRGB32);
      {
        BLContext ctx(result);
        ctx.clear_all();
        ctx.set_comp_op(BL_COMP_OP_SRC_OVER);
        if (!knockout) {
          ctx.blit_image(BLPoint(0, 0), src->dcast());
        }
        ctx.blit_image(BLPoint(0, 0), shadow_layer);
        ctx.end();
      }
      dst->dcast() = result;
      return BL_SUCCESS;
    }

    // Outer shadow path.
    // Expand output to fit both shadow and original.
    int out_w = w + bl_abs(offset_x);
    int out_h = h + bl_abs(offset_y);
    int src_x = bl_max(-offset_x, 0);
    int src_y = bl_max(-offset_y, 0);
    int shd_x = bl_max(offset_x, 0);
    int shd_y = bl_max(offset_y, 0);

    BLImage blurred_mask;
    BL_PROPAGATE(bl_image_filter(
      static_cast<BLImageCore*>(&blurred_mask),
      static_cast<const BLImageCore*>(&alpha_mask),
      (quality < 0.3) ? BL_IMAGE_FILTER_TYPE_BOX_BLUR : BL_IMAGE_FILTER_TYPE_GAUSSIAN_BLUR,
      radius, quality));

    BLImage shadow;
    BL_PROPAGATE(bl::colorize_mask(shadow, blurred_mask, options->color));
    if (opacity < 1.0)
      bl::apply_opacity_prgb32(shadow, opacity);

    BLImage result(out_w, out_h, BL_FORMAT_PRGB32);
    {
      BLContext ctx(result);
      ctx.clear_all();
      ctx.set_comp_op(BL_COMP_OP_SRC_OVER);
      ctx.blit_image(BLPoint(shd_x, shd_y), shadow);
      if (!knockout) {
        ctx.blit_image(BLPoint(src_x, src_y), src->dcast());
      }
      ctx.end();
    }

    dst->dcast() = result;
    return BL_SUCCESS;
  }

  if (options->type == BL_IMAGE_EFFECT_TYPE_BRIGHTNESS_CONTRAST) {
    // Brightness/contrast: per-pixel color adjustment.
    // brightness = options->radius  (-1 to +1)
    // contrast   = options->quality (-1 to +1)
    BLImagePrivateImpl* si = get_impl(src);
    int w = si->size.w;
    int h = si->size.h;

    double brightness = bl_clamp(options->radius, -1.0, 1.0);
    double contrast = bl_clamp(options->quality, -1.0, 1.0);

    // Build lookup table for speed.
    // Contrast: scale around midpoint (128). contrast_factor = (1 + contrast) for positive, tan-based for full range.
    double cf = (contrast >= 0.0) ? (1.0 + contrast * 3.0) : (1.0 + contrast);
    int32_t lut[256];
    for (int i = 0; i < 256; i++) {
      double v = double(i) / 255.0;
      v += brightness;                          // Brightness shift
      v = (v - 0.5) * cf + 0.5;                // Contrast around midpoint
      int iv = int(v * 255.0 + 0.5);
      lut[i] = bl_clamp(iv, 0, 255);
    }

    // Deep copy source if needed.
    BLImage src_copy;
    if (dst == src) {
      src_copy = src->dcast();
      BL_PROPAGATE(bl_image_create(dst, w, h, BLFormat(si->format)));
    } else {
      BL_PROPAGATE(bl_image_create(dst, w, h, BLFormat(si->format)));
    }

    BLImageData src_data, dst_data;
    if (src_copy.width() > 0)
      src_copy.get_data(&src_data);
    else
      src->dcast().get_data(&src_data);
    dst->dcast().make_mutable(&dst_data);

    const uint8_t* sp = static_cast<const uint8_t*>(src_data.pixel_data);
    uint8_t* dp = static_cast<uint8_t*>(dst_data.pixel_data);

    if (si->format == BL_FORMAT_PRGB32 || si->format == BL_FORMAT_XRGB32) {
      for (int y = 0; y < h; y++) {
        const uint32_t* srow = reinterpret_cast<const uint32_t*>(sp);
        uint32_t* drow = reinterpret_cast<uint32_t*>(dp);
        for (int x = 0; x < w; x++) {
          uint32_t p = srow[x];
          uint32_t a = (p >> 24) & 0xFF;
          // Unpremultiply for correct brightness/contrast, then repremultiply.
          uint32_t r, g, b;
          if (a > 0 && a < 255) {
            r = bl_min(uint32_t(((p >> 16) & 0xFF) * 255 / a), 255u);
            g = bl_min(uint32_t(((p >>  8) & 0xFF) * 255 / a), 255u);
            b = bl_min(uint32_t(((p >>  0) & 0xFF) * 255 / a), 255u);
          } else {
            r = (p >> 16) & 0xFF;
            g = (p >>  8) & 0xFF;
            b = (p >>  0) & 0xFF;
          }

          r = uint32_t(lut[r]);
          g = uint32_t(lut[g]);
          b = uint32_t(lut[b]);

          // Repremultiply.
          if (a < 255) {
            r = r * a / 255;
            g = g * a / 255;
            b = b * a / 255;
          }
          drow[x] = b | (g << 8) | (r << 16) | (a << 24);
        }
        sp += src_data.stride;
        dp += dst_data.stride;
      }
    }
    else if (si->format == BL_FORMAT_A8) {
      // A8: brightness only (no color to adjust contrast on).
      for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++)
          dp[x] = uint8_t(lut[sp[x]]);
        sp += src_data.stride;
        dp += dst_data.stride;
      }
    }

    return BL_SUCCESS;
  }

  if (options->type == BL_IMAGE_EFFECT_TYPE_SATURATION) {
    // Saturation: lerp between grayscale and original.
    // factor = options->radius (0 = grayscale, 1 = unchanged, >1 = oversaturated)
    BLImagePrivateImpl* si = get_impl(src);
    int w = si->size.w;
    int h = si->size.h;

    double factor = bl_max(options->radius, 0.0);

    BLImage src_copy;
    if (dst == src) {
      src_copy = src->dcast();
      BL_PROPAGATE(bl_image_create(dst, w, h, BLFormat(si->format)));
    } else {
      BL_PROPAGATE(bl_image_create(dst, w, h, BLFormat(si->format)));
    }

    BLImageData src_data, dst_data;
    if (src_copy.width() > 0)
      src_copy.get_data(&src_data);
    else
      src->dcast().get_data(&src_data);
    dst->dcast().make_mutable(&dst_data);

    const uint8_t* sp = static_cast<const uint8_t*>(src_data.pixel_data);
    uint8_t* dp = static_cast<uint8_t*>(dst_data.pixel_data);

    if (si->format == BL_FORMAT_PRGB32 || si->format == BL_FORMAT_XRGB32) {
      // Fixed-point factor: 8.8 format.
      int32_t f = int32_t(factor * 256.0 + 0.5);
      int32_t inv_f = 256 - f;

      for (int y = 0; y < h; y++) {
        const uint32_t* srow = reinterpret_cast<const uint32_t*>(sp);
        uint32_t* drow = reinterpret_cast<uint32_t*>(dp);
        for (int x = 0; x < w; x++) {
          uint32_t p = srow[x];
          uint32_t a = (p >> 24) & 0xFF;
          uint32_t r = (p >> 16) & 0xFF;
          uint32_t g = (p >>  8) & 0xFF;
          uint32_t b = (p >>  0) & 0xFF;

          // Unpremultiply.
          uint32_t ur = r, ug = g, ub = b;
          if (a > 0 && a < 255) {
            ur = bl_min(r * 255 / a, 255u);
            ug = bl_min(g * 255 / a, 255u);
            ub = bl_min(b * 255 / a, 255u);
          }

          // Luminance (BT.709).
          int32_t lum = int32_t(ur * 54 + ug * 183 + ub * 19) >> 8; // ~0.2126R + 0.7152G + 0.0722B

          // Lerp: result = lum + factor * (channel - lum)
          int32_t nr = bl_clamp(int32_t((lum * inv_f + int32_t(ur) * f) >> 8), 0, 255);
          int32_t ng = bl_clamp(int32_t((lum * inv_f + int32_t(ug) * f) >> 8), 0, 255);
          int32_t nb = bl_clamp(int32_t((lum * inv_f + int32_t(ub) * f) >> 8), 0, 255);

          // Repremultiply.
          if (a < 255) {
            nr = nr * int32_t(a) / 255;
            ng = ng * int32_t(a) / 255;
            nb = nb * int32_t(a) / 255;
          }
          drow[x] = uint32_t(nb) | (uint32_t(ng) << 8) | (uint32_t(nr) << 16) | (a << 24);
        }
        sp += src_data.stride;
        dp += dst_data.stride;
      }
    }

    return BL_SUCCESS;
  }

  if (options->type == BL_IMAGE_EFFECT_TYPE_TINT) {
    // Flash-style tint: lerp between original color and tint color.
    // result = original * (1 - amount) + tintColor * amount
    // Alpha is preserved from the original.
    BLImagePrivateImpl* si = get_impl(src);
    int w = si->size.w;
    int h = si->size.h;

    double amount = bl_clamp(options->radius, 0.0, 1.0);
    uint32_t tint_color = options->color;
    uint32_t tr = (tint_color >> 16) & 0xFF;
    uint32_t tg = (tint_color >>  8) & 0xFF;
    uint32_t tb = (tint_color >>  0) & 0xFF;

    // Fixed-point: 8.8
    int32_t amt = int32_t(amount * 256.0 + 0.5);
    int32_t inv = 256 - amt;

    BLImage src_copy;
    if (dst == src) {
      src_copy = src->dcast();
      BL_PROPAGATE(bl_image_create(dst, w, h, BLFormat(si->format)));
    } else {
      BL_PROPAGATE(bl_image_create(dst, w, h, BLFormat(si->format)));
    }

    BLImageData src_data, dst_data;
    if (src_copy.width() > 0)
      src_copy.get_data(&src_data);
    else
      src->dcast().get_data(&src_data);
    dst->dcast().make_mutable(&dst_data);

    const uint8_t* sp = static_cast<const uint8_t*>(src_data.pixel_data);
    uint8_t* dp = static_cast<uint8_t*>(dst_data.pixel_data);

    if (si->format == BL_FORMAT_PRGB32 || si->format == BL_FORMAT_XRGB32) {
      for (int y = 0; y < h; y++) {
        const uint32_t* srow = reinterpret_cast<const uint32_t*>(sp);
        uint32_t* drow = reinterpret_cast<uint32_t*>(dp);
        for (int x = 0; x < w; x++) {
          uint32_t p = srow[x];
          uint32_t a = (p >> 24) & 0xFF;

          // Unpremultiply.
          uint32_t r, g, b;
          if (a > 0 && a < 255) {
            r = bl_min(((p >> 16) & 0xFF) * 255 / a, 255u);
            g = bl_min(((p >>  8) & 0xFF) * 255 / a, 255u);
            b = bl_min(((p >>  0) & 0xFF) * 255 / a, 255u);
          } else {
            r = (p >> 16) & 0xFF;
            g = (p >>  8) & 0xFF;
            b = (p >>  0) & 0xFF;
          }

          // Lerp: original * (1-amount) + tint * amount
          r = uint32_t((int32_t(r) * inv + int32_t(tr) * amt) >> 8);
          g = uint32_t((int32_t(g) * inv + int32_t(tg) * amt) >> 8);
          b = uint32_t((int32_t(b) * inv + int32_t(tb) * amt) >> 8);

          // Repremultiply.
          if (a < 255) {
            r = r * a / 255;
            g = g * a / 255;
            b = b * a / 255;
          }
          drow[x] = b | (g << 8) | (r << 16) | (a << 24);
        }
        sp += src_data.stride;
        dp += dst_data.stride;
      }
    }

    return BL_SUCCESS;
  }

  if (options->type == BL_IMAGE_EFFECT_TYPE_COLOR_MATRIX) {
    if (!options->color_matrix)
      return bl_make_error(BL_ERROR_INVALID_VALUE);

    BLImagePrivateImpl* si = get_impl(src);
    int w = si->size.w;
    int h = si->size.h;

    const double* m = options->color_matrix;

    // Convert matrix to fixed-point (8.8) for speed.
    int32_t mi[20];
    for (int i = 0; i < 20; i++)
      mi[i] = int32_t(m[i] * 256.0 + 0.5);

    BLImage src_copy;
    if (dst == src) {
      src_copy = src->dcast();
      BL_PROPAGATE(bl_image_create(dst, w, h, BLFormat(si->format)));
    } else {
      BL_PROPAGATE(bl_image_create(dst, w, h, BLFormat(si->format)));
    }

    BLImageData src_data, dst_data;
    if (src_copy.width() > 0) src_copy.get_data(&src_data);
    else src->dcast().get_data(&src_data);
    dst->dcast().make_mutable(&dst_data);

    const uint8_t* sp = static_cast<const uint8_t*>(src_data.pixel_data);
    uint8_t* dp = static_cast<uint8_t*>(dst_data.pixel_data);

    if (si->format == BL_FORMAT_PRGB32 || si->format == BL_FORMAT_XRGB32) {
      for (int y = 0; y < h; y++) {
        const uint32_t* srow = reinterpret_cast<const uint32_t*>(sp);
        uint32_t* drow = reinterpret_cast<uint32_t*>(dp);
        for (int x = 0; x < w; x++) {
          uint32_t p = srow[x];
          uint32_t a = (p >> 24) & 0xFF;
          uint32_t r, g, b;

          // Unpremultiply.
          if (a > 0 && a < 255) {
            r = bl_min(((p >> 16) & 0xFF) * 255 / a, 255u);
            g = bl_min(((p >>  8) & 0xFF) * 255 / a, 255u);
            b = bl_min(((p >>  0) & 0xFF) * 255 / a, 255u);
          } else {
            r = (p >> 16) & 0xFF; g = (p >> 8) & 0xFF; b = p & 0xFF;
          }

          // Apply 5x4 matrix: [R' G' B' A'] = M * [R G B A 1]
          int32_t nr = (int32_t(r) * mi[0] + int32_t(g) * mi[1] + int32_t(b) * mi[2] + int32_t(a) * mi[3] + mi[4] * 255) >> 8;
          int32_t ng = (int32_t(r) * mi[5] + int32_t(g) * mi[6] + int32_t(b) * mi[7] + int32_t(a) * mi[8] + mi[9] * 255) >> 8;
          int32_t nb = (int32_t(r) * mi[10]+ int32_t(g) * mi[11]+ int32_t(b) * mi[12]+ int32_t(a) * mi[13]+ mi[14]* 255) >> 8;
          int32_t na = (int32_t(r) * mi[15]+ int32_t(g) * mi[16]+ int32_t(b) * mi[17]+ int32_t(a) * mi[18]+ mi[19]* 255) >> 8;

          nr = bl_clamp(nr, 0, 255);
          ng = bl_clamp(ng, 0, 255);
          nb = bl_clamp(nb, 0, 255);
          na = bl_clamp(na, 0, 255);

          // Repremultiply.
          if (na < 255) {
            nr = nr * na / 255;
            ng = ng * na / 255;
            nb = nb * na / 255;
          }
          drow[x] = uint32_t(nb) | (uint32_t(ng) << 8) | (uint32_t(nr) << 16) | (uint32_t(na) << 24);
        }
        sp += src_data.stride;
        dp += dst_data.stride;
      }
    }

    return BL_SUCCESS;
  }

  return bl_make_error(BL_ERROR_INVALID_VALUE);
}

// bl::ImageFilter - Apply Effects Chain (Public API)
// ==================================================

BL_API_IMPL BLResult bl_image_apply_effects(BLImageCore* dst, const BLImageCore* src, const BLImageEffectOptions* options, uint32_t count) noexcept {
  using namespace bl::ImageInternal;

  BL_ASSERT(dst->_d.is_image());
  BL_ASSERT(src->_d.is_image());

  if (count == 0) {
    if (dst != src)
      return bl_image_assign_deep(dst, src);
    return BL_SUCCESS;
  }

  if (count == 1) {
    return bl_image_apply_effect(dst, src, &options[0]);
  }

  // For chaining: each effect is applied to the original source, then all effect layers
  // are composited together. Effects marked KNOCKOUT don't include the original.
  // The compositing order is: effects[0] (bottom) → effects[1] → ... → original (top, unless all knockout).
  BLImagePrivateImpl* si = get_impl(src);
  int w = si->size.w;
  int h = si->size.h;

  BLImage result(w, h, BL_FORMAT_PRGB32);
  {
    BLContext ctx(result);
    ctx.clear_all();
    ctx.set_comp_op(BL_COMP_OP_SRC_OVER);

    bool need_original = false;

    // Apply each effect and composite the result layer onto the canvas.
    for (uint32_t i = 0; i < count; i++) {
      BLImage effect_result;
      BLResult r = bl_image_apply_effect(
        static_cast<BLImageCore*>(&effect_result),
        src,
        &options[i]);

      if (r != BL_SUCCESS)
        continue;

      // If this effect has knockout, it already omits the original.
      // If not, the effect layer includes the original — we only need the effect part.
      bool is_knockout = (options[i].flags & BL_IMAGE_EFFECT_FLAG_KNOCKOUT) != 0;

      if (!is_knockout) {
        // Non-knockout effects include the original composited in.
        // For chaining, we want just the effect layer. Apply it with knockout to get the effect only,
        // then we'll add the original at the end.
        BLImageEffectOptions ko_opts = options[i];
        ko_opts.flags |= BL_IMAGE_EFFECT_FLAG_KNOCKOUT;
        bl_image_apply_effect(
          static_cast<BLImageCore*>(&effect_result),
          src,
          &ko_opts);
        need_original = true;
      }

      ctx.blit_image(BLPoint(0, 0), effect_result);
    }

    // Add original on top if any non-knockout effect was used.
    if (need_original) {
      ctx.blit_image(BLPoint(0, 0), src->dcast());
    }

    ctx.end();
  }

  dst->dcast() = result;
  return BL_SUCCESS;
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
