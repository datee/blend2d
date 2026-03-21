// This file is part of Blend2D project <https://blend2d.com>
//
// See blend2d.h or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#include <blend2d/core/api-build_p.h>
#ifdef BL_TARGET_OPT_AVX2

#include <blend2d/core/imagefilter_p.h>
#include <immintrin.h>

// bl::ImageFilter - AVX2 Vertical Box Blur (PRGB32)
// ==================================================
//
// Processes 8 columns (32 bytes) per iteration using 256-bit AVX2 vectors.
// 32 bytes per row read = half a cache line, dramatically reducing cache miss cost
// compared to SSE2 (16 bytes) or scalar (4 bytes).

BL_HIDDEN void bl_image_filter_box_blur_vert_prgb32_avx2(
    uint8_t* BL_RESTRICT dst, intptr_t dst_stride,
    const uint8_t* BL_RESTRICT src, intptr_t src_stride,
    int w, int h, int x_start, int x_end, int radius) noexcept {

  bl_unused(w);
  int kernel = radius * 2 + 1;
  __m256i v_recip = _mm256_set1_epi32(int((1u << 24) / uint32_t(kernel)));
  __m256i v_mask_ff = _mm256_set1_epi32(0xFF);

  int x = x_start;

  // AVX2 path: 8 columns at a time (32 bytes per row read).
  for (; x + 8 <= x_end; x += 8) {
    __m256i acc_b = _mm256_setzero_si256();
    __m256i acc_g = _mm256_setzero_si256();
    __m256i acc_r = _mm256_setzero_si256();
    __m256i acc_a = _mm256_setzero_si256();

    // Initialize accumulators.
    for (int i = -radius; i <= radius; i++) {
      int yi = bl_clamp(i, 0, h - 1);
      __m256i pixels = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + yi * src_stride + x * 4));
      acc_b = _mm256_add_epi32(acc_b, _mm256_and_si256(pixels, v_mask_ff));
      acc_g = _mm256_add_epi32(acc_g, _mm256_and_si256(_mm256_srli_epi32(pixels, 8), v_mask_ff));
      acc_r = _mm256_add_epi32(acc_r, _mm256_and_si256(_mm256_srli_epi32(pixels, 16), v_mask_ff));
      acc_a = _mm256_add_epi32(acc_a, _mm256_srli_epi32(pixels, 24));
    }

    for (int y = 0; y < h; y++) {
      // AVX2 has _mm256_mullo_epi32 natively.
      __m256i out_b = _mm256_srli_epi32(_mm256_mullo_epi32(acc_b, v_recip), 24);
      __m256i out_g = _mm256_srli_epi32(_mm256_mullo_epi32(acc_g, v_recip), 24);
      __m256i out_r = _mm256_srli_epi32(_mm256_mullo_epi32(acc_r, v_recip), 24);
      __m256i out_a = _mm256_srli_epi32(_mm256_mullo_epi32(acc_a, v_recip), 24);

      __m256i result = _mm256_or_si256(
        _mm256_or_si256(out_b, _mm256_slli_epi32(out_g, 8)),
        _mm256_or_si256(_mm256_slli_epi32(out_r, 16), _mm256_slli_epi32(out_a, 24)));

      _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + y * dst_stride + x * 4), result);

      // Slide window.
      int yi_add = bl_min(y + radius + 1, h - 1);
      int yi_sub = bl_max(y - radius, 0);

      __m256i p_add = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + yi_add * src_stride + x * 4));
      __m256i p_sub = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + yi_sub * src_stride + x * 4));

      acc_b = _mm256_add_epi32(acc_b, _mm256_sub_epi32(_mm256_and_si256(p_add, v_mask_ff), _mm256_and_si256(p_sub, v_mask_ff)));
      acc_g = _mm256_add_epi32(acc_g, _mm256_sub_epi32(_mm256_and_si256(_mm256_srli_epi32(p_add, 8), v_mask_ff), _mm256_and_si256(_mm256_srli_epi32(p_sub, 8), v_mask_ff)));
      acc_r = _mm256_add_epi32(acc_r, _mm256_sub_epi32(_mm256_and_si256(_mm256_srli_epi32(p_add, 16), v_mask_ff), _mm256_and_si256(_mm256_srli_epi32(p_sub, 16), v_mask_ff)));
      acc_a = _mm256_add_epi32(acc_a, _mm256_sub_epi32(_mm256_srli_epi32(p_add, 24), _mm256_srli_epi32(p_sub, 24)));
    }
  }

  // SSE2/scalar remainder for last 0-7 columns — delegate to SSE2 function.
  if (x < x_end) {
    bl_image_filter_box_blur_vert_prgb32_sse2(dst, dst_stride, src, src_stride, w, h, x, x_end, radius);
  }
}

#endif // BL_TARGET_OPT_AVX2
