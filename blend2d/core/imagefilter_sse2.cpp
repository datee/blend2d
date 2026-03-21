// This file is part of Blend2D project <https://blend2d.com>
//
// See blend2d.h or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#include <blend2d/core/api-build_p.h>
#ifdef BL_TARGET_OPT_SSE2

#include <blend2d/core/imagefilter_p.h>
#include <emmintrin.h>

#ifdef BL_TARGET_OPT_SSE4_1
  #include <smmintrin.h>
#endif

// bl::ImageFilter - SSE2 Vertical Box Blur (PRGB32)
// ==================================================
//
// Processes 4 columns at a time using 128-bit SSE2 vectors.
// Each pixel (BGRA) is unpacked into 4 separate 32-bit accumulator vectors.
// This gives 4x better cache utilization vs scalar (16 bytes per row read
// instead of 4 bytes).

// Helper: 4-lane 32-bit multiply, result = (a * b) >> shift.
static BL_INLINE __m128i mul_shr_u32(__m128i a, __m128i b, int shift) noexcept {
#ifdef BL_TARGET_OPT_SSE4_1
  return _mm_srli_epi32(_mm_mullo_epi32(a, b), shift);
#else
  // SSE2 fallback: _mm_mul_epu32 only multiplies lanes 0,2.
  __m128i mask02 = _mm_set_epi32(0, -1, 0, -1);
  __m128i lo = _mm_srli_epi64(_mm_mul_epu32(a, b), shift);
  __m128i hi = _mm_srli_epi64(_mm_mul_epu32(_mm_srli_si128(a, 4), b), shift);
  return _mm_or_si128(_mm_and_si128(lo, mask02), _mm_slli_si128(_mm_and_si128(hi, mask02), 4));
#endif
}

void bl_image_filter_box_blur_vert_prgb32_sse2(
    uint8_t* BL_RESTRICT dst, intptr_t dst_stride,
    const uint8_t* BL_RESTRICT src, intptr_t src_stride,
    int w, int h, int x_start, int x_end, int radius) noexcept {

  bl_unused(w);
  int kernel = radius * 2 + 1;
  __m128i v_recip = _mm_set1_epi32(int((1u << 24) / uint32_t(kernel)));
  __m128i v_mask_ff = _mm_set1_epi32(0xFF);

  int x = x_start;

  // SSE2 path: 4 columns at a time.
  for (; x + 4 <= x_end; x += 4) {
    __m128i acc_b = _mm_setzero_si128();
    __m128i acc_g = _mm_setzero_si128();
    __m128i acc_r = _mm_setzero_si128();
    __m128i acc_a = _mm_setzero_si128();

    // Initialize accumulators.
    for (int i = -radius; i <= radius; i++) {
      int yi = bl_clamp(i, 0, h - 1);
      __m128i pixels = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + yi * src_stride + x * 4));
      acc_b = _mm_add_epi32(acc_b, _mm_and_si128(pixels, v_mask_ff));
      acc_g = _mm_add_epi32(acc_g, _mm_and_si128(_mm_srli_epi32(pixels, 8), v_mask_ff));
      acc_r = _mm_add_epi32(acc_r, _mm_and_si128(_mm_srli_epi32(pixels, 16), v_mask_ff));
      acc_a = _mm_add_epi32(acc_a, _mm_srli_epi32(pixels, 24));
    }

    for (int y = 0; y < h; y++) {
      // Compute output: (acc * reciprocal) >> 24, pack channels.
      __m128i out_b = mul_shr_u32(acc_b, v_recip, 24);
      __m128i out_g = mul_shr_u32(acc_g, v_recip, 24);
      __m128i out_r = mul_shr_u32(acc_r, v_recip, 24);
      __m128i out_a = mul_shr_u32(acc_a, v_recip, 24);

      __m128i result = _mm_or_si128(
        _mm_or_si128(out_b, _mm_slli_epi32(out_g, 8)),
        _mm_or_si128(_mm_slli_epi32(out_r, 16), _mm_slli_epi32(out_a, 24)));

      _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + y * dst_stride + x * 4), result);

      // Slide window.
      int yi_add = bl_min(y + radius + 1, h - 1);
      int yi_sub = bl_max(y - radius, 0);

      __m128i p_add = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + yi_add * src_stride + x * 4));
      __m128i p_sub = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + yi_sub * src_stride + x * 4));

      acc_b = _mm_add_epi32(acc_b, _mm_sub_epi32(_mm_and_si128(p_add, v_mask_ff), _mm_and_si128(p_sub, v_mask_ff)));
      acc_g = _mm_add_epi32(acc_g, _mm_sub_epi32(_mm_and_si128(_mm_srli_epi32(p_add, 8), v_mask_ff), _mm_and_si128(_mm_srli_epi32(p_sub, 8), v_mask_ff)));
      acc_r = _mm_add_epi32(acc_r, _mm_sub_epi32(_mm_and_si128(_mm_srli_epi32(p_add, 16), v_mask_ff), _mm_and_si128(_mm_srli_epi32(p_sub, 16), v_mask_ff)));
      acc_a = _mm_add_epi32(acc_a, _mm_sub_epi32(_mm_srli_epi32(p_add, 24), _mm_srli_epi32(p_sub, 24)));
    }
  }

  // Scalar remainder for last 0-3 columns.
  if (x < x_end) {
    int kernel_s = radius * 2 + 1;
    uint32_t reciprocal = (1u << 24) / uint32_t(kernel_s);

    for (; x < x_end; x++) {
      const uint8_t* src_col = src + x * 4;
      uint32_t ab = 0, ag = 0, ar = 0, aa = 0;

      for (int i = -radius; i <= radius; i++) {
        int yi = bl_clamp(i, 0, h - 1);
        uint32_t p = *reinterpret_cast<const uint32_t*>(src_col + yi * src_stride);
        ab += (p >>  0) & 0xFF; ag += (p >>  8) & 0xFF;
        ar += (p >> 16) & 0xFF; aa += (p >> 24) & 0xFF;
      }

      uint8_t* dst_col = dst + x * 4;
      for (int y = 0; y < h; y++) {
        *reinterpret_cast<uint32_t*>(dst_col + y * dst_stride) =
          ((ab * reciprocal) >> 24) | (((ag * reciprocal) >> 24) << 8) |
          (((ar * reciprocal) >> 24) << 16) | (((aa * reciprocal) >> 24) << 24);

        int yi_add = bl_min(y + radius + 1, h - 1);
        int yi_sub = bl_max(y - radius, 0);
        uint32_t pa = *reinterpret_cast<const uint32_t*>(src_col + yi_add * src_stride);
        uint32_t ps = *reinterpret_cast<const uint32_t*>(src_col + yi_sub * src_stride);
        ab += ((pa >>  0) & 0xFF) - ((ps >>  0) & 0xFF);
        ag += ((pa >>  8) & 0xFF) - ((ps >>  8) & 0xFF);
        ar += ((pa >> 16) & 0xFF) - ((ps >> 16) & 0xFF);
        aa += ((pa >> 24) & 0xFF) - ((ps >> 24) & 0xFF);
      }
    }
  }
}

#endif // BL_TARGET_OPT_SSE2
