// This file is part of Blend2D project <https://blend2d.com>
//
// See blend2d.h or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#include <blend2d/core/api-build_p.h>
#include <blend2d/core/array_p.h>
#include <blend2d/core/filesystem.h>
#include <blend2d/core/format_p.h>
#include <blend2d/core/image_p.h>
#include <blend2d/core/imagecodec.h>
#include <blend2d/core/imagedecoder.h>
#include <blend2d/core/imageencoder.h>
#include <blend2d/core/imagescale_p.h>
#include <blend2d/core/object_p.h>
#include <blend2d/core/pixelconverter_p.h>
#include <blend2d/core/runtime_p.h>
#include <blend2d/support/intops_p.h>
#include <blend2d/support/math_p.h>
#include <blend2d/support/memops_p.h>

namespace bl {
namespace ImageInternal {

// bl::Image - Globals
// ===================

static BLObjectEternalImpl<BLImagePrivateImpl> default_image;

// bl::Image - Constants
// =====================

static constexpr uint32_t kLargeDataAlignment = 64;
static constexpr uint32_t kLargeDataThreshold = 1024;
static constexpr uint32_t kMaxAddressableOffset = 0x7FFFFFFFu;

// bl::Image - Utilities
// =====================

static BL_INLINE uint32_t stride_for_width(uint32_t width, uint32_t depth) noexcept {
  return (uint32_t(width) * depth + 7u) / 8u;
}

static BL_INLINE bool check_size_and_format(int w, int h, BLFormat format) noexcept {
  return !(unsigned(w) - 1u >= BL_RUNTIME_MAX_IMAGE_SIZE ||
           unsigned(h) - 1u >= BL_RUNTIME_MAX_IMAGE_SIZE ||
           unsigned(format) - 1u >= BL_FORMAT_MAX_VALUE);
}

static BL_INLINE BLResultT<intptr_t> calc_stride_from_create_params(int w, int h, BLFormat format) noexcept {
  if (BL_UNLIKELY(!check_size_and_format(w, h, format))) {
    BLResult result =
      w <= 0 || h <= 0 || unsigned(format - 1) >= BL_FORMAT_MAX_VALUE
        ? BL_ERROR_INVALID_VALUE
        : BL_ERROR_IMAGE_TOO_LARGE;
    return BLResultT<intptr_t>{result, 0};
  }
  else {
    uint32_t bytes_per_line = stride_for_width(uint32_t(w), bl_format_info[format].depth);
    uint64_t bytes_per_image = uint64_t(bytes_per_line) * unsigned(h);

    // NOTE: Align the stride to 16 bytes if bytes_per_line is not too small. The reason is that when multi-threaded
    // rendering is used and bytes_per_line not aligned, some bands could share a cache line, which would potentially
    // affect the performance in a very negative way.
    if (bytes_per_line > 256u)
      bytes_per_line = IntOps::align_up(bytes_per_line, 16);

    BLResult result = bytes_per_image <= kMaxAddressableOffset ? BL_SUCCESS : BL_ERROR_IMAGE_TOO_LARGE;
    return BLResultT<intptr_t>{result, intptr_t(bytes_per_line)};
  }
}

// Make sure that the external image won't cause any kind of overflow in rasterization and texture fetching.
static BL_INLINE BLResult check_create_from_data_params(int w, int h, BLFormat format, intptr_t stride) noexcept {
  uint32_t minimum_stride = stride_for_width(uint32_t(w), bl_format_info[format].depth);
  uintptr_t bytes_per_line = uintptr_t(bl_abs(stride));

  if (BL_UNLIKELY(!check_size_and_format(w, h, format) || bytes_per_line < minimum_stride))
    return BL_ERROR_INVALID_VALUE;

  // Make sure that the image height multiplied by stride is not greater than 2^31 - this makes sure that we
  // can handle also negative strides properly and that we can guarantee that all pixels are addressable via
  // 32-bit offsets, which is required by some SIMD fetchers.
  //
  // NOTE: BytesPerImage also considers all parent images in case this image is indeed a sub-image. The reason
  // is that we have to address all pixels in this sub-image too, so basically we include the parent in the
  // computation as well. Since a sub-image may be managed by the user (outside of Blend2D) we have to check
  // this every time an external pixel data is used.
  uint64_t bytes_per_image = uint64_t(bytes_per_line) * uint64_t(uint32_t(h));
  return bytes_per_line > uintptr_t(kMaxAddressableOffset) || bytes_per_image > kMaxAddressableOffset ? BL_ERROR_IMAGE_TOO_LARGE : BL_SUCCESS;
}

static void copy_image_data(uint8_t* dst_data, intptr_t dst_stride, const uint8_t* src_data, intptr_t src_stride, int w, int h, BLFormat format) noexcept {
  size_t bytes_per_line = (size_t(unsigned(w)) * bl_format_info[format].depth + 7u) / 8u;

  if (intptr_t(bytes_per_line) == dst_stride && intptr_t(bytes_per_line) == src_stride) {
    // Special case that happens often - stride equals bytes-per-line (no gaps).
    memcpy(dst_data, src_data, bytes_per_line * unsigned(h));
    return;
  }
  else {
    // Generic case - there are either gaps or source/destination is a sub-image.
    size_t gap = dst_stride > 0 ? size_t(dst_stride) - bytes_per_line : size_t(0);
    for (unsigned y = unsigned(h); y; y--) {
      memcpy(dst_data, src_data, bytes_per_line);
      bl::MemOps::fill_small_t(dst_data + bytes_per_line, uint8_t(0), gap);

      dst_data += dst_stride;
      src_data += src_stride;
    }
  }
}

// bl::Image - Alloc & Free Impl
// =============================

static BL_INLINE void init_impl_data(BLImagePrivateImpl* impl, int w, int h, BLFormat format, void* pixel_data, intptr_t stride) noexcept {
  impl->pixel_data = pixel_data;
  impl->size.reset(w, h);
  impl->stride = stride;
  impl->format = uint8_t(format);
  impl->flags = uint8_t(0);
  impl->depth = uint16_t(bl_format_info[format].depth);
  memset(impl->reserved, 0, sizeof(impl->reserved));
}

static BL_NOINLINE BLResult alloc_impl(BLImageCore* self, int w, int h, BLFormat format, intptr_t stride) noexcept {
  BL_ASSERT(w > 0);
  BL_ASSERT(h > 0);
  BL_ASSERT(format != BL_FORMAT_NONE);
  BL_ASSERT(format <= BL_FORMAT_MAX_VALUE);
  BL_ASSERT(stride > 0);

  size_t kBaseImplSize = IntOps::align_up(sizeof(BLImagePrivateImpl), BL_OBJECT_IMPL_ALIGNMENT);
  size_t pixel_data_size = size_t(h) * size_t(stride);

  BLObjectImplSize impl_size(kBaseImplSize + pixel_data_size);
  if (pixel_data_size >= kLargeDataThreshold)
    impl_size += kLargeDataAlignment - BL_OBJECT_IMPL_ALIGNMENT;

  BLObjectInfo info = BLObjectInfo::from_type_with_marker(BL_OBJECT_TYPE_IMAGE);
  BL_PROPAGATE(ObjectInternal::alloc_impl_t<BLImagePrivateImpl>(self, info, impl_size));

  BLImagePrivateImpl* impl = get_impl(self);
  uint8_t* pixel_data = reinterpret_cast<uint8_t*>(impl) + kBaseImplSize;

  if (pixel_data_size >= kLargeDataThreshold)
    pixel_data = bl::IntOps::align_up(pixel_data, kLargeDataAlignment);

  init_impl_data(impl, w, h, format, pixel_data, stride);
  impl->writer_count = 0;
  return BL_SUCCESS;
}

static BL_NOINLINE BLResult alloc_external(BLImageCore* self, int w, int h, BLFormat format, void* pixel_data, intptr_t stride, bool immutable, BLDestroyExternalDataFunc destroy_func, void* user_data) noexcept {
  BL_ASSERT(w > 0);
  BL_ASSERT(h > 0);
  BL_ASSERT(format != BL_FORMAT_NONE);
  BL_ASSERT(format <= BL_FORMAT_MAX_VALUE);

  BLObjectInfo info = BLObjectInfo::from_type_with_marker(BL_OBJECT_TYPE_IMAGE);
  BL_PROPAGATE(ObjectInternal::alloc_impl_external_t<BLImagePrivateImpl>(self, info, immutable, destroy_func, user_data));

  BLImagePrivateImpl* impl = get_impl(self);
  init_impl_data(impl, w, h, format, pixel_data, stride);
  impl->writer_count = 0;
  return BL_SUCCESS;
}

// Must be available outside of BLImage implementation.
BLResult free_impl(BLImagePrivateImpl* impl) noexcept {
  // Postpone the deletion in case that the image still has writers attached. This is required as the rendering
  // context doesn't manipulate the reference count of `BLImage` (otherwise it would not be possible to attach
  // multiple rendering contexts, for example).
  if (impl->writer_count != 0)
    return BL_SUCCESS;

  if (ObjectInternal::is_impl_external(impl))
    ObjectInternal::call_external_destroy_func(impl, impl->pixel_data);

  return ObjectInternal::free_impl(impl);
}

} // {ImageInternal}
} // {bl}

// bl::Image - API - Init & Destroy
// ================================

BL_API_IMPL BLResult bl_image_init(BLImageCore* self) noexcept {
  using namespace bl::ImageInternal;

  self->_d = bl_object_defaults[BL_OBJECT_TYPE_IMAGE]._d;
  return BL_SUCCESS;
}

BL_API_IMPL BLResult bl_image_init_move(BLImageCore* self, BLImageCore* other) noexcept {
  using namespace bl::ImageInternal;

  BL_ASSERT(self != other);
  BL_ASSERT(other->_d.is_image());

  self->_d = other->_d;
  other->_d = bl_object_defaults[BL_OBJECT_TYPE_IMAGE]._d;

  return BL_SUCCESS;
}

BL_API_IMPL BLResult bl_image_init_weak(BLImageCore* self, const BLImageCore* other) noexcept {
  using namespace bl::ImageInternal;

  BL_ASSERT(self != other);
  BL_ASSERT(other->_d.is_image());

  self->_d = other->_d;
  return retain_instance(self);
}

BL_API_IMPL BLResult bl_image_init_as(BLImageCore* self, int w, int h, BLFormat format) noexcept {
  using namespace bl::ImageInternal;

  self->_d = bl_object_defaults[BL_OBJECT_TYPE_IMAGE]._d;
  return bl_image_create(self, w, h, format);
}

BL_API_IMPL BLResult bl_image_init_as_from_data(
    BLImageCore* self, int w, int h, BLFormat format,
    void* pixel_data, intptr_t stride,
    BLDataAccessFlags access_flags,
    BLDestroyExternalDataFunc destroy_func, void* user_data) noexcept {

  using namespace bl::ImageInternal;

  self->_d = bl_object_defaults[BL_OBJECT_TYPE_IMAGE]._d;
  return bl_image_create_from_data(self, w, h, format, pixel_data, stride, access_flags, destroy_func, user_data);
}

BL_API_IMPL BLResult bl_image_destroy(BLImageCore* self) noexcept {
  using namespace bl::ImageInternal;

  BL_ASSERT(self->_d.is_image());
  return release_instance(self);
}

// bl::Image - API - Reset
// =======================

BL_API_IMPL BLResult bl_image_reset(BLImageCore* self) noexcept {
  using namespace bl::ImageInternal;

  BL_ASSERT(self->_d.is_image());
  return replace_instance(self, static_cast<BLImageCore*>(&bl_object_defaults[BL_OBJECT_TYPE_IMAGE]));
}

// bl::Image - API - Assign
// ========================

BL_API_IMPL BLResult bl_image_assign_move(BLImageCore* self, BLImageCore* other) noexcept {
  using namespace bl::ImageInternal;

  BL_ASSERT(self->_d.is_image());
  BL_ASSERT(other->_d.is_image());

  BLImageCore tmp = *other;
  other->_d = bl_object_defaults[BL_OBJECT_TYPE_IMAGE]._d;
  return replace_instance(self, &tmp);
}

BL_API_IMPL BLResult bl_image_assign_weak(BLImageCore* self, const BLImageCore* other) noexcept {
  using namespace bl::ImageInternal;

  BL_ASSERT(self->_d.is_image());
  BL_ASSERT(other->_d.is_image());

  retain_instance(other);
  return replace_instance(self, other);
}

BL_API_IMPL BLResult bl_image_assign_deep(BLImageCore* self, const BLImageCore* other) noexcept {
  using namespace bl::ImageInternal;

  BL_ASSERT(self->_d.is_image());
  BL_ASSERT(other->_d.is_image());

  BLImagePrivateImpl* self_impl = get_impl(self);
  BLImagePrivateImpl* other_impl = get_impl(other);

  BLSizeI size = other_impl->size;
  BLFormat format = BLFormat(other_impl->format);

  if (format == BL_FORMAT_NONE)
    return bl_image_reset(self);

  BLImageData dummy_image_data;
  if (self_impl == other_impl)
    return bl_image_make_mutable(self, &dummy_image_data);

  BL_PROPAGATE(bl_image_create(self, size.w, size.h, format));
  self_impl = get_impl(self);

  copy_image_data(static_cast<uint8_t*>(self_impl->pixel_data), self_impl->stride,
              static_cast<uint8_t*>(other_impl->pixel_data), other_impl->stride, size.w, size.h, format);
  return BL_SUCCESS;
}

// bl::Image - API - Create
// ========================

BL_API_IMPL BLResult bl_image_create(BLImageCore* self, int w, int h, BLFormat format) noexcept {
  using namespace bl::ImageInternal;

  BL_ASSERT(self->_d.is_image());

  BLResultT<intptr_t> result = calc_stride_from_create_params(w, h, format);
  if (BL_UNLIKELY(result.code != BL_SUCCESS)) {
    if ((w | h) == 0 && format == BL_FORMAT_NONE)
      return bl_image_reset(self);
    else
      return bl_make_error(result.code);
  }

  BLImagePrivateImpl* self_impl = get_impl(self);
  if (self_impl->size == BLSizeI(w, h) && self_impl->format == format)
    if (bl::ObjectInternal::is_impl_mutable(self_impl) && !bl::ObjectInternal::is_impl_external(self_impl))
      return BL_SUCCESS;

  BLImageCore newO;
  BL_PROPAGATE(alloc_impl(&newO, w, h, format, result.value));

  return replace_instance(self, &newO);
}

BL_API_IMPL BLResult bl_image_create_from_data(
    BLImageCore* self, int w, int h, BLFormat format,
    void* pixel_data, intptr_t stride,
    BLDataAccessFlags access_flags,
    BLDestroyExternalDataFunc destroy_func, void* user_data) noexcept {

  using namespace bl::ImageInternal;

  BL_ASSERT(self->_d.is_image());

  BLResult result = check_create_from_data_params(w, h, format, stride);
  if (BL_UNLIKELY(result != BL_SUCCESS))
    return bl_make_error(result);

  BLImagePrivateImpl* self_impl = get_impl(self);
  bool immutable = !(access_flags & BL_DATA_ACCESS_WRITE);

  if (bl::ObjectInternal::is_impl_external(self_impl) && bl::ObjectInternal::is_impl_ref_count_equal_to_base(self_impl) && self_impl->writer_count == 0) {
    // OPTIMIZATION: If the user code calls BLImage::create_from_data() for every frame, use the same Impl
    // if the `ref_count == 1` and the Impl is external to avoid a malloc()/free() roundtrip for each call.
    bl::ObjectInternal::call_external_destroy_func(self_impl, self_impl->pixel_data);
    bl::ObjectInternal::init_external_destroy_func(self_impl, destroy_func, user_data);
    bl::ObjectInternal::init_ref_count_to_base(self_impl, immutable);

    init_impl_data(self_impl, w, h, format, pixel_data, stride);
    return BL_SUCCESS;
  }
  else {
    BLImageCore newO;
    BL_PROPAGATE(alloc_external(&newO, w, h, format, pixel_data, stride, immutable, destroy_func, user_data));

    return replace_instance(self, &newO);
  }
}

// bl::Image - API - Accessors
// ===========================

BL_API_IMPL BLResult bl_image_get_data(const BLImageCore* self, BLImageData* data_out) noexcept {
  using namespace bl::ImageInternal;

  BL_ASSERT(self->_d.is_image());
  BLImagePrivateImpl* self_impl = get_impl(self);

  data_out->pixel_data = self_impl->pixel_data;
  data_out->stride = self_impl->stride;
  data_out->size = self_impl->size;
  data_out->format = self_impl->format;
  data_out->flags = 0;

  return BL_SUCCESS;
}

BL_API_IMPL BLResult bl_image_make_mutable(BLImageCore* self, BLImageData* data_out) noexcept {
  using namespace bl::ImageInternal;

  BL_ASSERT(self->_d.is_image());
  BLImagePrivateImpl* self_impl = get_impl(self);

  BLSizeI size = self_impl->size;
  BLFormat format = (BLFormat)self_impl->format;

  if (format != BL_FORMAT_NONE && !is_impl_mutable(self_impl)) {
    BLImageCore newO;
    BL_PROPAGATE(alloc_impl(&newO, size.w, size.h, format,
      intptr_t(stride_for_width(uint32_t(size.w), bl_format_info[format].depth))));

    BLImagePrivateImpl* new_impl = get_impl(&newO);
    data_out->pixel_data = new_impl->pixel_data;
    data_out->stride = new_impl->stride;
    data_out->size = size;
    data_out->format = format;
    data_out->flags = 0;

    copy_image_data(static_cast<uint8_t*>(new_impl->pixel_data), new_impl->stride,
                  static_cast<uint8_t*>(self_impl->pixel_data), self_impl->stride, size.w, size.h, format);

    return replace_instance(self, &newO);
  }
  else {
    data_out->pixel_data = self_impl->pixel_data;
    data_out->stride = self_impl->stride;
    data_out->size = size;
    data_out->format = format;
    data_out->flags = 0;
    return BL_SUCCESS;
  }
}

// bl::Image - API - Convert
// =========================

BL_API_IMPL BLResult bl_image_convert(BLImageCore* self, BLFormat format) noexcept {
  using namespace bl::ImageInternal;

  BL_ASSERT(self->_d.is_image());
  BLImagePrivateImpl* self_impl = get_impl(self);

  bl::FormatExt src_format = bl::FormatExt(self_impl->format);
  bl::FormatExt dst_format = bl::FormatExt(format);

  if (dst_format == src_format)
    return BL_SUCCESS;

  if (dst_format == bl::FormatExt::kXRGB32)
    dst_format = bl::FormatExt::kFRGB32;

  if (src_format == bl::FormatExt::kNone)
    return bl_make_error(BL_ERROR_NOT_INITIALIZED);

  BLResult result = BL_SUCCESS;
  BLPixelConverterCore pc {};

  BLSizeI size = self_impl->size;
  const BLFormatInfo& di = bl_format_info[size_t(dst_format)];
  const BLFormatInfo& si = bl_format_info[size_t(src_format)];

  // Save some cycles by calling `bl_pixel_converter_init_internal` as we don't need to sanitize the destination and
  // source formats in this case.
  if (bl_pixel_converter_init_internal(&pc, di, si, BL_PIXEL_CONVERTER_CREATE_NO_FLAGS) != BL_SUCCESS) {
    // Built-in formats should always have a built-in converter, so report a different error if the initialization
    // failed. This is pretty critical.
    return bl_make_error(BL_ERROR_INVALID_STATE);
  }

  if (di.depth == si.depth && is_impl_mutable(self_impl)) {
    // Prefer in-place conversion if the depths are equal and the image mutable.
    pc.convert_func(&pc, static_cast<uint8_t*>(self_impl->pixel_data), self_impl->stride,
                        static_cast<uint8_t*>(self_impl->pixel_data), self_impl->stride, uint32_t(size.w), uint32_t(size.h), nullptr);
    self_impl->format = uint8_t(format);
  }
  else {
    BLImageCore dst_image;
    result = bl_image_init_as(&dst_image, size.w, size.h, format);

    if (result == BL_SUCCESS) {
      BLImagePrivateImpl* dst_impl = get_impl(&dst_image);
      BLPixelConverterOptions opt {};

      opt.gap = uintptr_t(bl_abs(dst_impl->stride)) - uintptr_t(uint32_t(size.w)) * (dst_impl->depth / 8u);
      pc.convert_func(&pc, static_cast<uint8_t*>(dst_impl->pixel_data), dst_impl->stride,
                          static_cast<uint8_t*>(self_impl->pixel_data), self_impl->stride, uint32_t(size.w), uint32_t(size.h), &opt);

      return replace_instance(self, &dst_image);
    }
  }

  bl_pixel_converter_reset(&pc);
  return result;
}

// bl::Image - API - Equality & Comparison
// =======================================

BL_API_IMPL bool bl_image_equals(const BLImageCore* a, const BLImageCore* b) noexcept {
  using namespace bl::ImageInternal;

  BL_ASSERT(a->_d.is_image());
  BL_ASSERT(b->_d.is_image());

  const BLImagePrivateImpl* a_impl = get_impl(a);
  const BLImagePrivateImpl* b_impl = get_impl(b);

  if (a_impl == b_impl)
    return true;

  if (a_impl->size != b_impl->size || a_impl->format != b_impl->format)
    return false;

  uint32_t w = uint32_t(a_impl->size.w);
  uint32_t h = uint32_t(a_impl->size.h);

  const uint8_t* a_data = static_cast<const uint8_t*>(a_impl->pixel_data);
  const uint8_t* b_data = static_cast<const uint8_t*>(b_impl->pixel_data);

  intptr_t a_stride = a_impl->stride;
  intptr_t b_stride = b_impl->stride;

  size_t bytes_per_line = (w * bl_format_info[a_impl->format].depth + 7u) / 8u;
  for (uint32_t y = 0; y < h; y++) {
    if (memcmp(a_data, b_data, bytes_per_line) != 0)
      return false;
    a_data += a_stride;
    b_data += b_stride;
  }

  return true;
}

// bl::Image - API - Scale
// =======================

BL_API_IMPL BLResult bl_image_scale(BLImageCore* dst, const BLImageCore* src, const BLSizeI* size, BLImageScaleFilter filter) noexcept {
  using namespace bl::ImageInternal;

  BL_ASSERT(dst->_d.is_image());
  BL_ASSERT(src->_d.is_image());

  BLImagePrivateImpl* src_impl = get_impl(src);
  if (src_impl->format == BL_FORMAT_NONE)
    return bl_image_reset(dst);

  bl::ImageScaleContext scale_ctx;
  BL_PROPAGATE(scale_ctx.create(*size, src_impl->size, filter));

  BLFormat format = BLFormat(src_impl->format);
  int tw = scale_ctx.dst_width();
  int th = scale_ctx.src_height();

  BLImage tmp;
  BLImageData buf;

  if (th == scale_ctx.dst_height() || tw == scale_ctx.src_width()) {
    // Only horizontal or vertical scale.

    // Move to `tmp` so it's not destroyed by `dst->create()`.
    if (dst == src)
      tmp = src->dcast();

    BL_PROPAGATE(bl_image_create(dst, scale_ctx.dst_width(), scale_ctx.dst_height(), format));
    BL_PROPAGATE(bl_image_make_mutable(dst, &buf));

    if (th == scale_ctx.dst_height())
      scale_ctx.process_horz_data(static_cast<uint8_t*>(buf.pixel_data), buf.stride, static_cast<const uint8_t*>(src_impl->pixel_data), src_impl->stride, format);
    else
      scale_ctx.process_vert_data(static_cast<uint8_t*>(buf.pixel_data), buf.stride, static_cast<const uint8_t*>(src_impl->pixel_data), src_impl->stride, format);
  }
  else {
    // Both horizontal and vertical scale.
    BL_PROPAGATE(tmp.create(tw, th, format));
    BL_PROPAGATE(tmp.make_mutable(&buf));
    scale_ctx.process_horz_data(static_cast<uint8_t*>(buf.pixel_data), buf.stride, static_cast<const uint8_t*>(src_impl->pixel_data), src_impl->stride, format);

    src_impl = get_impl(&tmp);
    BL_PROPAGATE(bl_image_create(dst, scale_ctx.dst_width(), scale_ctx.dst_height(), format));
    BL_PROPAGATE(bl_image_make_mutable(dst, &buf));

    scale_ctx.process_vert_data(static_cast<uint8_t*>(buf.pixel_data), buf.stride, static_cast<const uint8_t*>(src_impl->pixel_data), src_impl->stride, format);
  }

  return BL_SUCCESS;
}

// bl::Image - API - Filter
// ========================

#if defined(BL_TARGET_OPT_SSE2)
  #include <emmintrin.h>
  #if defined(BL_TARGET_OPT_SSE4_1)
    #include <smmintrin.h>
  #endif
#endif

namespace bl {
namespace ImageFilter {

// Compute 3 box widths that approximate a Gaussian with the given sigma.
// Based on the W3C CSS specification for filter: blur().
// See: https://www.w3.org/TR/SVG11/filters.html#feGaussianBlurElement
static void gaussian_box_widths(double sigma, int widths[3]) noexcept {
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

// Single-pass horizontal box blur for PRGB32 (4 bytes per pixel).
// Uses sliding window accumulator — O(width) regardless of radius.
// Process 2 rows at once to improve instruction-level parallelism.
static void box_blur_horz_prgb32(
    uint8_t* BL_RESTRICT dst_line, intptr_t dst_stride,
    const uint8_t* BL_RESTRICT src_line, intptr_t src_stride,
    int w, int h, int radius) noexcept {

  int kernel = radius * 2 + 1;
  uint32_t reciprocal = (1u << 24) / uint32_t(kernel);

  // Process 2 rows at a time for better ILP (independent data, same instruction stream).
  int y = 0;
  for (; y + 2 <= h; y += 2) {
    const uint32_t* src0 = reinterpret_cast<const uint32_t*>(src_line);
    const uint32_t* src1 = reinterpret_cast<const uint32_t*>(src_line + src_stride);
    uint32_t* dst0 = reinterpret_cast<uint32_t*>(dst_line);
    uint32_t* dst1 = reinterpret_cast<uint32_t*>(dst_line + dst_stride);

    uint32_t acc0_b = 0, acc0_g = 0, acc0_r = 0, acc0_a = 0;
    uint32_t acc1_b = 0, acc1_g = 0, acc1_r = 0, acc1_a = 0;

    for (int i = -radius; i <= radius; i++) {
      int xi = bl_clamp(i, 0, w - 1);
      uint32_t p0 = src0[xi], p1 = src1[xi];
      acc0_b += (p0 >>  0) & 0xFF; acc1_b += (p1 >>  0) & 0xFF;
      acc0_g += (p0 >>  8) & 0xFF; acc1_g += (p1 >>  8) & 0xFF;
      acc0_r += (p0 >> 16) & 0xFF; acc1_r += (p1 >> 16) & 0xFF;
      acc0_a += (p0 >> 24) & 0xFF; acc1_a += (p1 >> 24) & 0xFF;
    }

    for (int x = 0; x < w; x++) {
      dst0[x] = ((acc0_b * reciprocal) >> 24)       | (((acc0_g * reciprocal) >> 24) << 8) |
                (((acc0_r * reciprocal) >> 24) << 16)| (((acc0_a * reciprocal) >> 24) << 24);
      dst1[x] = ((acc1_b * reciprocal) >> 24)       | (((acc1_g * reciprocal) >> 24) << 8) |
                (((acc1_r * reciprocal) >> 24) << 16)| (((acc1_a * reciprocal) >> 24) << 24);

      int xi_add = bl_min(x + radius + 1, w - 1);
      int xi_sub = bl_max(x - radius, 0);

      uint32_t a0 = src0[xi_add], s0 = src0[xi_sub];
      uint32_t a1 = src1[xi_add], s1 = src1[xi_sub];

      acc0_b += ((a0 >>  0) & 0xFF) - ((s0 >>  0) & 0xFF); acc1_b += ((a1 >>  0) & 0xFF) - ((s1 >>  0) & 0xFF);
      acc0_g += ((a0 >>  8) & 0xFF) - ((s0 >>  8) & 0xFF); acc1_g += ((a1 >>  8) & 0xFF) - ((s1 >>  8) & 0xFF);
      acc0_r += ((a0 >> 16) & 0xFF) - ((s0 >> 16) & 0xFF); acc1_r += ((a1 >> 16) & 0xFF) - ((s1 >> 16) & 0xFF);
      acc0_a += ((a0 >> 24) & 0xFF) - ((s0 >> 24) & 0xFF); acc1_a += ((a1 >> 24) & 0xFF) - ((s1 >> 24) & 0xFF);
    }

    src_line += src_stride * 2;
    dst_line += dst_stride * 2;
  }

  // Handle last row if height is odd.
  for (; y < h; y++) {
    const uint32_t* src = reinterpret_cast<const uint32_t*>(src_line);
    uint32_t* dst = reinterpret_cast<uint32_t*>(dst_line);

    uint32_t acc_b = 0, acc_g = 0, acc_r = 0, acc_a = 0;
    for (int i = -radius; i <= radius; i++) {
      int xi = bl_clamp(i, 0, w - 1);
      uint32_t p = src[xi];
      acc_b += (p >>  0) & 0xFF;
      acc_g += (p >>  8) & 0xFF;
      acc_r += (p >> 16) & 0xFF;
      acc_a += (p >> 24) & 0xFF;
    }

    for (int x = 0; x < w; x++) {
      dst[x] = ((acc_b * reciprocal) >> 24)       | (((acc_g * reciprocal) >> 24) << 8) |
               (((acc_r * reciprocal) >> 24) << 16)| (((acc_a * reciprocal) >> 24) << 24);

      int xi_add = bl_min(x + radius + 1, w - 1);
      int xi_sub = bl_max(x - radius, 0);
      uint32_t p_add = src[xi_add], p_sub = src[xi_sub];
      acc_b += ((p_add >>  0) & 0xFF) - ((p_sub >>  0) & 0xFF);
      acc_g += ((p_add >>  8) & 0xFF) - ((p_sub >>  8) & 0xFF);
      acc_r += ((p_add >> 16) & 0xFF) - ((p_sub >> 16) & 0xFF);
      acc_a += ((p_add >> 24) & 0xFF) - ((p_sub >> 24) & 0xFF);
    }

    src_line += src_stride;
    dst_line += dst_stride;
  }
}

// Scalar vertical box blur — fallback for non-SSE2 or remainder columns.
static void box_blur_vert_prgb32_scalar(
    uint8_t* BL_RESTRICT dst_line, intptr_t dst_stride,
    const uint8_t* BL_RESTRICT src_line, intptr_t src_stride,
    int x_start, int x_end, int h, int radius, uint32_t reciprocal) noexcept {

  for (int x = x_start; x < x_end; x++) {
    const uint8_t* src_col = src_line + x * 4;

    uint32_t acc_r = 0, acc_g = 0, acc_b = 0, acc_a = 0;
    for (int i = -radius; i <= radius; i++) {
      int yi = bl_clamp(i, 0, h - 1);
      uint32_t p = *reinterpret_cast<const uint32_t*>(src_col + yi * src_stride);
      acc_b += (p >>  0) & 0xFF;
      acc_g += (p >>  8) & 0xFF;
      acc_r += (p >> 16) & 0xFF;
      acc_a += (p >> 24) & 0xFF;
    }

    uint8_t* dst_col = dst_line + x * 4;
    for (int y = 0; y < h; y++) {
      uint32_t* dst = reinterpret_cast<uint32_t*>(dst_col + y * dst_stride);
      *dst = ((acc_b * reciprocal) >> 24)       |
             (((acc_g * reciprocal) >> 24) << 8) |
             (((acc_r * reciprocal) >> 24) << 16)|
             (((acc_a * reciprocal) >> 24) << 24);

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

// Single-pass vertical box blur for PRGB32.
static void box_blur_vert_prgb32(
    uint8_t* BL_RESTRICT dst_line, intptr_t dst_stride,
    const uint8_t* BL_RESTRICT src_line, intptr_t src_stride,
    int w, int h, int radius) noexcept {

  int kernel = radius * 2 + 1;
  uint32_t reciprocal = (1u << 24) / uint32_t(kernel);
  int x = 0;

#if defined(BL_TARGET_OPT_SSE2)
  // SSE2 vertical pass: process 4 pixels (16 bytes) per iteration using 128-bit SIMD.
  // Each pixel is unpacked to 4x32-bit accumulators (one per channel).
  // We maintain 4 sets of accumulators (acc_b, acc_g, acc_r, acc_a) each holding 4 columns.
  __m128i v_mask_ff = _mm_set1_epi32(0xFF);
  __m128i v_recip = _mm_set1_epi32(int(reciprocal));

  for (; x + 4 <= w; x += 4) {
    __m128i acc_b = _mm_setzero_si128();
    __m128i acc_g = _mm_setzero_si128();
    __m128i acc_r = _mm_setzero_si128();
    __m128i acc_a = _mm_setzero_si128();

    // Initialize accumulators.
    for (int i = -radius; i <= radius; i++) {
      int yi = bl_clamp(i, 0, h - 1);
      __m128i pixels = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src_line + yi * src_stride + x * 4));

      acc_b = _mm_add_epi32(acc_b, _mm_and_si128(pixels, v_mask_ff));
      acc_g = _mm_add_epi32(acc_g, _mm_and_si128(_mm_srli_epi32(pixels, 8), v_mask_ff));
      acc_r = _mm_add_epi32(acc_r, _mm_and_si128(_mm_srli_epi32(pixels, 16), v_mask_ff));
      acc_a = _mm_add_epi32(acc_a, _mm_srli_epi32(pixels, 24));
    }

    for (int y = 0; y < h; y++) {
      // Compute (acc * reciprocal) >> 24 for each channel.
      __m128i out_b, out_g, out_r, out_a;

#if defined(BL_TARGET_OPT_SSE4_1)
      // SSE4.1: _mm_mullo_epi32 does 4x 32-bit multiply in one instruction.
      out_b = _mm_srli_epi32(_mm_mullo_epi32(acc_b, v_recip), 24);
      out_g = _mm_srli_epi32(_mm_mullo_epi32(acc_g, v_recip), 24);
      out_r = _mm_srli_epi32(_mm_mullo_epi32(acc_r, v_recip), 24);
      out_a = _mm_srli_epi32(_mm_mullo_epi32(acc_a, v_recip), 24);
#else
      // SSE2 fallback: _mm_mul_epu32 only multiplies lanes 0,2. Need two passes + merge.
      {
        __m128i dword_mask = _mm_set_epi32(0, (int)0xFFFFFFFF, 0, (int)0xFFFFFFFF);

        #define SSE2_MUL_U32(OUT, ACC) do { \
          __m128i lo = _mm_srli_epi64(_mm_mul_epu32(ACC, v_recip), 24); \
          __m128i hi = _mm_srli_epi64(_mm_mul_epu32(_mm_srli_si128(ACC, 4), v_recip), 24); \
          OUT = _mm_or_si128(_mm_and_si128(lo, dword_mask), _mm_slli_si128(_mm_and_si128(hi, dword_mask), 4)); \
        } while (0)

        SSE2_MUL_U32(out_b, acc_b);
        SSE2_MUL_U32(out_g, acc_g);
        SSE2_MUL_U32(out_r, acc_r);
        SSE2_MUL_U32(out_a, acc_a);

        #undef SSE2_MUL_U32
      }
#endif

      // Combine channels: B | (G << 8) | (R << 16) | (A << 24)
      __m128i result = _mm_or_si128(
        _mm_or_si128(out_b, _mm_slli_epi32(out_g, 8)),
        _mm_or_si128(_mm_slli_epi32(out_r, 16), _mm_slli_epi32(out_a, 24)));

      _mm_storeu_si128(reinterpret_cast<__m128i*>(dst_line + y * dst_stride + x * 4), result);

      // Slide window.
      int yi_add = bl_min(y + radius + 1, h - 1);
      int yi_sub = bl_max(y - radius, 0);

      __m128i p_add = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src_line + yi_add * src_stride + x * 4));
      __m128i p_sub = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src_line + yi_sub * src_stride + x * 4));

      __m128i diff_b = _mm_sub_epi32(_mm_and_si128(p_add, v_mask_ff), _mm_and_si128(p_sub, v_mask_ff));
      __m128i diff_g = _mm_sub_epi32(_mm_and_si128(_mm_srli_epi32(p_add, 8), v_mask_ff), _mm_and_si128(_mm_srli_epi32(p_sub, 8), v_mask_ff));
      __m128i diff_r = _mm_sub_epi32(_mm_and_si128(_mm_srli_epi32(p_add, 16), v_mask_ff), _mm_and_si128(_mm_srli_epi32(p_sub, 16), v_mask_ff));
      __m128i diff_a = _mm_sub_epi32(_mm_srli_epi32(p_add, 24), _mm_srli_epi32(p_sub, 24));

      acc_b = _mm_add_epi32(acc_b, diff_b);
      acc_g = _mm_add_epi32(acc_g, diff_g);
      acc_r = _mm_add_epi32(acc_r, diff_r);
      acc_a = _mm_add_epi32(acc_a, diff_a);
    }
  }
#endif // BL_TARGET_OPT_SSE2

  // Scalar remainder.
  if (x < w) {
    box_blur_vert_prgb32_scalar(dst_line, dst_stride, src_line, src_stride, x, w, h, radius, reciprocal);
  }
}

// Single-pass horizontal box blur for A8 (1 byte per pixel).
static void box_blur_horz_a8(
    uint8_t* BL_RESTRICT dst_line, intptr_t dst_stride,
    const uint8_t* BL_RESTRICT src_line, intptr_t src_stride,
    int w, int h, int radius) noexcept {

  int kernel = radius * 2 + 1;
  uint32_t reciprocal = (1u << 24) / uint32_t(kernel);

  for (int y = 0; y < h; y++) {
    const uint8_t* src = src_line;
    uint8_t* dst = dst_line;

    uint32_t acc = 0;
    for (int i = -radius; i <= radius; i++) {
      acc += src[bl_clamp(i, 0, w - 1)];
    }

    for (int x = 0; x < w; x++) {
      dst[x] = uint8_t((acc * reciprocal) >> 24);

      int xi_add = bl_min(x + radius + 1, w - 1);
      int xi_sub = bl_max(x - radius, 0);
      acc += src[xi_add] - src[xi_sub];
    }

    src_line += src_stride;
    dst_line += dst_stride;
  }
}

// Single-pass vertical box blur for A8.
static void box_blur_vert_a8(
    uint8_t* BL_RESTRICT dst_line, intptr_t dst_stride,
    const uint8_t* BL_RESTRICT src_line, intptr_t src_stride,
    int w, int h, int radius) noexcept {

  int kernel = radius * 2 + 1;
  uint32_t reciprocal = (1u << 24) / uint32_t(kernel);

  for (int x = 0; x < w; x++) {
    uint32_t acc = 0;
    for (int i = -radius; i <= radius; i++) {
      int yi = bl_clamp(i, 0, h - 1);
      acc += src_line[yi * src_stride + x];
    }

    for (int y = 0; y < h; y++) {
      dst_line[y * dst_stride + x] = uint8_t((acc * reciprocal) >> 24);

      int yi_add = bl_min(y + radius + 1, h - 1);
      int yi_sub = bl_max(y - radius, 0);
      acc += src_line[yi_add * src_stride + x] - src_line[yi_sub * src_stride + x];
    }
  }
}

// Apply one pass of box blur (horizontal then vertical) to an image.
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

  // Temporary image for the intermediate horizontal pass result.
  BLImage tmp(w, h, BLFormat(format));
  BLImageData tmp_data;
  BL_PROPAGATE(tmp.make_mutable(&tmp_data));

  const uint8_t* src_pixels = static_cast<const uint8_t*>(src_data.pixel_data);
  uint8_t* tmp_pixels = static_cast<uint8_t*>(tmp_data.pixel_data);

  // Horizontal pass: src → tmp
  if (format == BL_FORMAT_PRGB32 || format == BL_FORMAT_XRGB32) {
    box_blur_horz_prgb32(tmp_pixels, tmp_data.stride, src_pixels, src_data.stride, w, h, radius);
  }
  else if (format == BL_FORMAT_A8) {
    box_blur_horz_a8(tmp_pixels, tmp_data.stride, src_pixels, src_data.stride, w, h, radius);
  }
  else {
    return bl_make_error(BL_ERROR_INVALID_VALUE);
  }

  // Vertical pass: tmp → dst
  BL_PROPAGATE(bl_image_create(static_cast<BLImageCore*>(&dst), w, h, BLFormat(format)));
  BLImageData dst_data;
  BL_PROPAGATE(dst.make_mutable(&dst_data));
  uint8_t* dst_pixels = static_cast<uint8_t*>(dst_data.pixel_data);

  if (format == BL_FORMAT_PRGB32 || format == BL_FORMAT_XRGB32) {
    box_blur_vert_prgb32(dst_pixels, dst_data.stride, tmp_pixels, tmp_data.stride, w, h, radius);
  }
  else {
    box_blur_vert_a8(dst_pixels, dst_data.stride, tmp_pixels, tmp_data.stride, w, h, radius);
  }

  return BL_SUCCESS;
}

} // {ImageFilter}
} // {bl}

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

  // Make a deep copy of source if dst == src (we need the original pixels during processing).
  BLImage src_copy;
  const BLImageCore* actual_src = src;
  if (dst == src) {
    src_copy = src->dcast();
    actual_src = static_cast<const BLImageCore*>(&src_copy);
  }

  if (type == BL_IMAGE_FILTER_TYPE_BOX_BLUR) {
    int r = int(radius + 0.5);
    if (r < 1) r = 1;
    return bl::ImageFilter::box_blur_pass(dst->dcast(), actual_src->dcast(), r);
  }

  if (type == BL_IMAGE_FILTER_TYPE_GAUSSIAN_BLUR) {
    BLImagePrivateImpl* si = get_impl(actual_src);
    int orig_w = si->size.w;
    int orig_h = si->size.h;

    // Downscale optimization: for large radii, blur a smaller image then upscale back.
    // Large blur radii destroy the fine detail that downscaling loses, so the visual result
    // is nearly identical. This is the standard approach used by browsers (CSS backdrop-filter),
    // game engines, and compositing software.
    //
    // Quality controls the downscale threshold:
    //   quality=0.0 → threshold=4  (aggressive downscale, fastest)
    //   quality=0.5 → threshold=8  (balanced, default)
    //   quality=1.0 → no downscale (full resolution, highest quality)
    double q = bl_clamp(quality, 0.0, 1.0);
    constexpr int kMinDownscaledSize = 16;

    double effective_radius = radius;
    bool use_downscale = (q < 1.0) && (orig_w > kMinDownscaledSize * 2) && (orig_h > kMinDownscaledSize * 2);
    BLImage downscaled;
    int scale_factor = 1;

    if (use_downscale) {
      // Higher quality → higher threshold before downscaling kicks in.
      // q=0.0 → threshold=4, q=0.5 → threshold=8, q=0.9 → threshold=40
      double threshold = 4.0 + 36.0 * q;
      int max_scale = (q < 0.25) ? 16 : (q < 0.5) ? 8 : (q < 0.75) ? 4 : 2;

      // Choose scale factor: halve until radius fits within threshold.
      while (effective_radius > threshold && scale_factor < max_scale &&
             orig_w / (scale_factor * 2) >= kMinDownscaledSize &&
             orig_h / (scale_factor * 2) >= kMinDownscaledSize) {
        scale_factor *= 2;
        effective_radius /= 2.0;
      }

      BLSizeI small_size(orig_w / scale_factor, orig_h / scale_factor);
      BL_PROPAGATE(BLImage::scale(downscaled, actual_src->dcast(), small_size, BL_IMAGE_SCALE_FILTER_BILINEAR));
      actual_src = static_cast<const BLImageCore*>(&downscaled);
    }

    // Gaussian blur approximated via 3-pass box blur (W3C CSS standard approach).
    double sigma = effective_radius / 3.0;
    int widths[3];
    bl::ImageFilter::gaussian_box_widths(sigma, widths);

    BLImage blurred;

    // Pass 1: src → blurred
    int r1 = widths[0] / 2;
    BL_PROPAGATE(bl::ImageFilter::box_blur_pass(blurred, actual_src->dcast(), r1));

    // Pass 2: blurred → blurred (in-place via temp)
    int r2 = widths[1] / 2;
    if (r2 > 0) {
      BLImage pass_src;
      pass_src = blurred;
      BL_PROPAGATE(bl::ImageFilter::box_blur_pass(blurred, pass_src, r2));
    }

    // Pass 3: blurred → blurred (in-place via temp)
    int r3 = widths[2] / 2;
    if (r3 > 0) {
      BLImage pass_src;
      pass_src = blurred;
      BL_PROPAGATE(bl::ImageFilter::box_blur_pass(blurred, pass_src, r3));
    }

    // Upscale back to original dimensions if downscaled.
    if (use_downscale && scale_factor > 1) {
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

// bl::Image - API - Read File
// ===========================

BL_API_IMPL BLResult bl_image_read_from_file(BLImageCore* self, const char* file_name, const BLArrayCore* codecs) noexcept {
  using namespace bl::ImageInternal;

  BL_ASSERT(self->_d.is_image());

  BLArray<uint8_t> buffer;
  BL_PROPAGATE(BLFileSystem::read_file(file_name, buffer));

  if (buffer.is_empty())
    return bl_make_error(BL_ERROR_FILE_EMPTY);

  BLImageCodec codec;
  BL_PROPAGATE(bl_image_codec_find_by_data(&codec, buffer.data(), buffer.size(), codecs));

  if (BL_UNLIKELY(!(codec.features() & BL_IMAGE_CODEC_FEATURE_READ)))
    return bl_make_error(BL_ERROR_IMAGE_DECODER_NOT_PROVIDED);

  BLImageDecoder decoder;
  BL_PROPAGATE(codec.create_decoder(&decoder));
  return decoder.read_frame(*self, buffer);
}

// bl::Image - API - Read Data
// ===========================

BL_API_IMPL BLResult bl_image_read_from_data(BLImageCore* self, const void* data, size_t size, const BLArrayCore* codecs) noexcept {
  using namespace bl::ImageInternal;

  BL_ASSERT(self->_d.is_image());

  BLImageCodec codec;
  BL_PROPAGATE(bl_image_codec_find_by_data(&codec, data, size, codecs));

  if (BL_UNLIKELY(!(codec.features() & BL_IMAGE_CODEC_FEATURE_READ)))
    return bl_make_error(BL_ERROR_IMAGE_DECODER_NOT_PROVIDED);

  BLImageDecoder decoder;
  BL_PROPAGATE(codec.create_decoder(&decoder));
  return decoder.read_frame(*self, data, size);
}

// bl::Image - API - Write File
// ============================

namespace bl {
namespace ImageInternal {

static BLResult write_to_file_internal(const BLImageCore* self, const char* file_name, const BLImageCodecCore* codec) noexcept {
  BL_ASSERT(self->_d.is_image());
  BL_ASSERT(codec->_d.is_image_codec());

  BLArray<uint8_t> buffer;
  BL_PROPAGATE(bl_image_write_to_data(self, &buffer, codec));
  return BLFileSystem::write_file(file_name, buffer);
}

} // {ImageInternal}
} // {bl}

BL_API_IMPL BLResult bl_image_write_to_file(const BLImageCore* self, const char* file_name, const BLImageCodecCore* codec) noexcept {
  using namespace bl::ImageInternal;

  BL_ASSERT(self->_d.is_image());

  if (!codec) {
    BLImageCodec local_codec;
    BL_PROPAGATE(local_codec.find_by_extension(file_name));
    return write_to_file_internal(self, file_name, &local_codec);
  }
  else {
    BL_ASSERT(codec->_d.is_image_codec());
    return write_to_file_internal(self, file_name, codec);
  }
}

// bl::Image - API - Write Data
// ============================

BL_API_IMPL BLResult bl_image_write_to_data(const BLImageCore* self, BLArrayCore* dst, const BLImageCodecCore* codec) noexcept {
  using namespace bl::ImageInternal;

  BL_ASSERT(self->_d.is_image());
  BL_ASSERT(codec->_d.is_image_codec());

  if (BL_UNLIKELY(!(codec->dcast().features() & BL_IMAGE_CODEC_FEATURE_WRITE)))
    return bl_make_error(BL_ERROR_IMAGE_ENCODER_NOT_PROVIDED);

  BLImageEncoder encoder;
  BL_PROPAGATE(codec->dcast().create_encoder(&encoder));
  return encoder.write_frame(dst->dcast<BLArray<uint8_t>>(), self->dcast());
}

// bl::Image - Runtime Registration
// ================================

void bl_image_rt_init(BLRuntimeContext* rt) noexcept {
  bl_unused(rt);

  auto& default_image = bl::ImageInternal::default_image;

  bl_object_defaults[BL_OBJECT_TYPE_IMAGE]._d.init_dynamic(
    BLObjectInfo::from_type_with_marker(BL_OBJECT_TYPE_IMAGE),
    &default_image.impl);
}
