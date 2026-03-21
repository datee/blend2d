// This file is part of Blend2D project <https://blend2d.com>
//
// See blend2d.h or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

#include <blend2d/core/api-build_test_p.h>
#if defined(BL_TEST)

#include <blend2d/core/context.h>
#include <blend2d/core/image.h>
#include <blend2d/core/path.h>

namespace bl {
namespace Tests {

UNIT(image_filter_box_blur, BL_TEST_GROUP_IMAGE_CODEC_ROUNDTRIP) {
  INFO("Box blur: zero radius returns identical image");
  {
    BLImage src(64, 64, BL_FORMAT_PRGB32);
    { BLContext ctx(src); ctx.fill_all(BLRgba32(0xFFAABBCC)); ctx.end(); }

    BLImage dst;
    EXPECT_SUCCESS(BLImage::filter(dst, src, BL_IMAGE_FILTER_TYPE_BOX_BLUR, 0.0));
    EXPECT_EQ(dst.width(), 64);
    EXPECT_EQ(dst.height(), 64);
  }

  INFO("Box blur: solid color image stays solid");
  {
    BLImage src(64, 64, BL_FORMAT_PRGB32);
    { BLContext ctx(src); ctx.fill_all(BLRgba32(0xFF808080)); ctx.end(); }

    BLImage dst;
    EXPECT_SUCCESS(BLImage::filter(dst, src, BL_IMAGE_FILTER_TYPE_BOX_BLUR, 10.0));

    BLImageData data;
    dst.get_data(&data);
    const uint32_t* pixels = static_cast<const uint32_t*>(data.pixel_data);

    // All pixels should be near the original value (within fixed-point tolerance).
    uint32_t center = pixels[32 * data.stride / 4 + 32];
    uint32_t r = (center >> 16) & 0xFF;
    uint32_t g = (center >>  8) & 0xFF;
    uint32_t b = (center >>  0) & 0xFF;

    EXPECT_TRUE(r >= 0x7D && r <= 0x83).message("R=%u", r);
    EXPECT_TRUE(g >= 0x7D && g <= 0x83).message("G=%u", g);
    EXPECT_TRUE(b >= 0x7D && b <= 0x83).message("B=%u", b);
  }

  INFO("Box blur: preserves image dimensions");
  {
    BLImage src(200, 100, BL_FORMAT_PRGB32);
    { BLContext ctx(src); ctx.fill_all(BLRgba32(0xFFFF0000)); ctx.end(); }

    BLImage dst;
    EXPECT_SUCCESS(BLImage::filter(dst, src, BL_IMAGE_FILTER_TYPE_BOX_BLUR, 5.0));
    EXPECT_EQ(dst.width(), 200);
    EXPECT_EQ(dst.height(), 100);
  }

  INFO("Box blur: in-place operation");
  {
    BLImage img(64, 64, BL_FORMAT_PRGB32);
    {
      BLContext ctx(img);
      ctx.fill_all(BLRgba32(0xFF000000));
      ctx.fill_rect(BLRect(20, 20, 24, 24), BLRgba32(0xFFFFFFFF));
      ctx.end();
    }

    EXPECT_SUCCESS(BLImage::filter(img, img, BL_IMAGE_FILTER_TYPE_BOX_BLUR, 5.0));
    EXPECT_EQ(img.width(), 64);
    EXPECT_EQ(img.height(), 64);
  }
}

UNIT(image_filter_gaussian_blur, BL_TEST_GROUP_IMAGE_CODEC_ROUNDTRIP) {
  INFO("Gaussian blur: blurs sharp edge");
  {
    BLImage src(256, 64, BL_FORMAT_PRGB32);
    {
      BLContext ctx(src);
      ctx.fill_rect(BLRect(0, 0, 128, 64), BLRgba32(0xFFFF0000));
      ctx.fill_rect(BLRect(128, 0, 128, 64), BLRgba32(0xFF0000FF));
      ctx.end();
    }

    BLImage dst;
    EXPECT_SUCCESS(BLImage::filter(dst, src, BL_IMAGE_FILTER_TYPE_GAUSSIAN_BLUR, 10.0));

    BLImageData data;
    dst.get_data(&data);
    const uint32_t* pixels = static_cast<const uint32_t*>(data.pixel_data);

    // Far left should be mostly red.
    uint32_t left = pixels[32 * data.stride / 4 + 20];
    EXPECT_TRUE(((left >> 16) & 0xFF) > 200);

    // Far right should be mostly blue.
    uint32_t right = pixels[32 * data.stride / 4 + 235];
    EXPECT_TRUE(((right >> 0) & 0xFF) > 200);

    // Center should be blended.
    uint32_t mid = pixels[32 * data.stride / 4 + 128];
    EXPECT_TRUE(((mid >> 16) & 0xFF) > 30); // Some red
    EXPECT_TRUE(((mid >> 0) & 0xFF) > 30);  // Some blue
  }

  INFO("Gaussian blur: quality parameter works");
  {
    BLImage src(128, 128, BL_FORMAT_PRGB32);
    {
      BLContext ctx(src);
      ctx.fill_all(BLRgba32(0xFF000000));
      BLPath circle; circle.add_circle(BLCircle(64, 64, 30));
      ctx.fill_path(circle, BLRgba32(0xFFFFFFFF));
      ctx.end();
    }

    BLImage dst_low, dst_high;
    EXPECT_SUCCESS(BLImage::filter(dst_low, src, BL_IMAGE_FILTER_TYPE_GAUSSIAN_BLUR, 20.0, 0.0));
    EXPECT_SUCCESS(BLImage::filter(dst_high, src, BL_IMAGE_FILTER_TYPE_GAUSSIAN_BLUR, 20.0, 1.0));

    EXPECT_EQ(dst_low.width(), 128);
    EXPECT_EQ(dst_high.width(), 128);
  }

  INFO("Gaussian blur: A8 format support");
  {
    BLImage src(64, 64, BL_FORMAT_A8);
    {
      BLContext ctx(src);
      ctx.clear_all();
      ctx.fill_rect(BLRect(16, 16, 32, 32), BLRgba32(0xFF000000));
      ctx.end();
    }

    BLImage dst;
    EXPECT_SUCCESS(BLImage::filter(dst, src, BL_IMAGE_FILTER_TYPE_GAUSSIAN_BLUR, 3.0));
    EXPECT_EQ(dst.width(), 64);
    EXPECT_EQ(dst.height(), 64);

    BLImageData data;
    dst.get_data(&data);
    const uint8_t* pixels = static_cast<const uint8_t*>(data.pixel_data);

    // Center should still have high alpha.
    EXPECT_TRUE(pixels[32 * data.stride + 32] > 200);
    // Corner should be low alpha.
    EXPECT_TRUE(pixels[2 * data.stride + 2] < 20);
  }
}

} // {Tests}
} // {bl}

#endif // BL_TEST
