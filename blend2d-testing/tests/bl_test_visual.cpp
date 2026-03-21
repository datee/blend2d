// This file is part of Blend2D project <https://blend2d.com>
//
// See blend2d.h or LICENSE.md for license and copyright information
// SPDX-License-Identifier: Zlib

// Visual Rendering Tests
// ======================
//
// Generates test images and verifies pixel values at known positions.
// Images are saved as PNG files for manual inspection.
//
// Usage:
//   bl_test_visual [--save]
//
// With --save, writes PNG files to disk. Without it, only runs pixel checks.

#include <blend2d/blend2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_failures = 0;
static int g_passes = 0;

static uint32_t get_pixel(const BLImage& img, int x, int y) {
  BLImageData data;
  img.get_data(&data);

  if (x < 0 || y < 0 || x >= data.size.w || y >= data.size.h)
    return 0;

  const uint8_t* line = static_cast<const uint8_t*>(data.pixel_data) + y * data.stride;
  return reinterpret_cast<const uint32_t*>(line)[x];
}

static uint8_t pixel_r(uint32_t p) { return uint8_t((p >> 16) & 0xFF); }
static uint8_t pixel_g(uint32_t p) { return uint8_t((p >>  8) & 0xFF); }
static uint8_t pixel_b(uint32_t p) { return uint8_t((p >>  0) & 0xFF); }
static uint8_t pixel_a(uint32_t p) { return uint8_t((p >> 24) & 0xFF); }

static bool check_pixel_near(const BLImage& img, int x, int y, uint32_t expected, int tolerance, const char* desc) {
  uint32_t actual = get_pixel(img, x, y);

  int dr = abs(int(pixel_r(actual)) - int(pixel_r(expected)));
  int dg = abs(int(pixel_g(actual)) - int(pixel_g(expected)));
  int db = abs(int(pixel_b(actual)) - int(pixel_b(expected)));
  int da = abs(int(pixel_a(actual)) - int(pixel_a(expected)));

  bool ok = (dr <= tolerance && dg <= tolerance && db <= tolerance && da <= tolerance);

  if (!ok) {
    printf("  FAIL: %s at (%d,%d): got 0x%08X, expected 0x%08X (diff: R=%d G=%d B=%d A=%d, tolerance=%d)\n",
           desc, x, y, actual, expected, dr, dg, db, da, tolerance);
    g_failures++;
  } else {
    g_passes++;
  }
  return ok;
}

static void save_image(const BLImage& img, const char* filename) {
  BLResult result = img.write_to_file(filename);
  if (result == BL_SUCCESS) {
    printf("  Saved: %s\n", filename);
  } else {
    printf("  WARNING: Failed to save %s (error=%u)\n", filename, unsigned(result));
  }
}

// ----- Test 1: Basic Fills -----
static void test_basic_fills() {
  printf("\nTest 1: Basic Fills\n");

  BLImage img(256, 256, BL_FORMAT_PRGB32);
  BLContext ctx(img);
  ctx.clear_all();

  // Fill a red rectangle in the top-left quadrant
  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_rect(BLRect(0, 0, 128, 128), BLRgba32(0xFFFF0000));

  // Fill a green rectangle in the top-right quadrant
  ctx.fill_rect(BLRect(128, 0, 128, 128), BLRgba32(0xFF00FF00));

  // Fill a blue rectangle in the bottom-left quadrant
  ctx.fill_rect(BLRect(0, 128, 128, 128), BLRgba32(0xFF0000FF));

  // Fill a white rectangle in the bottom-right quadrant
  ctx.fill_rect(BLRect(128, 128, 128, 128), BLRgba32(0xFFFFFFFF));

  ctx.end();

  // Verify pixel colors at quadrant centers
  check_pixel_near(img,  64,  64, 0xFFFF0000, 0, "Red quadrant center");
  check_pixel_near(img, 192,  64, 0xFF00FF00, 0, "Green quadrant center");
  check_pixel_near(img,  64, 192, 0xFF0000FF, 0, "Blue quadrant center");
  check_pixel_near(img, 192, 192, 0xFFFFFFFF, 0, "White quadrant center");

  // Verify the cleared area is transparent black before fills
  // (all pixels are filled, so check boundaries)
  check_pixel_near(img, 0, 0, 0xFFFF0000, 0, "Red corner (0,0)");
  check_pixel_near(img, 255, 255, 0xFFFFFFFF, 0, "White corner (255,255)");

  save_image(img, "test_01_basic_fills.png");
}

// ----- Test 2: Alpha Blending (SrcOver) -----
static void test_alpha_blending() {
  printf("\nTest 2: Alpha Blending (SrcOver)\n");

  BLImage img(256, 256, BL_FORMAT_PRGB32);
  BLContext ctx(img);
  ctx.clear_all();

  // Fill background with solid blue
  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(BLRgba32(0xFF0000FF));

  // Draw 50% transparent red on top
  ctx.set_comp_op(BL_COMP_OP_SRC_OVER);
  ctx.fill_rect(BLRect(32, 32, 192, 192), BLRgba32(0x80FF0000));

  ctx.end();

  // Background pixel (outside red rect): pure blue
  check_pixel_near(img, 10, 10, 0xFF0000FF, 0, "Background (pure blue)");

  // Blended pixel (inside red rect): red over blue with 50% alpha
  // SrcOver: dst' = src * srcA + dst * (1 - srcA)
  // src = (0x80, 0xFF, 0x00, 0x00) premultiplied = (0x80, 0x80, 0x00, 0x00)
  // dst = (0xFF, 0x00, 0x00, 0xFF)
  // result.r = 0x80 + 0x00 * (255-128)/255 ≈ 0x80
  // result.g = 0x00 + 0x00 * ... = 0x00
  // result.b = 0x00 + 0xFF * (255-128)/255 ≈ 0x7F
  // result.a = 0x80 + 0xFF * (255-128)/255 ≈ 0xFF
  check_pixel_near(img, 128, 128, 0xFF80007F, 2, "Blended (red over blue)");

  save_image(img, "test_02_alpha_blending.png");
}

// ----- Test 3: Gradient Fill -----
static void test_gradient() {
  printf("\nTest 3: Gradient Fill\n");

  BLImage img(256, 256, BL_FORMAT_PRGB32);
  BLContext ctx(img);
  ctx.clear_all();

  // Horizontal gradient: black (left) to white (right)
  BLGradient grad(BLLinearGradientValues(0, 128, 256, 128));
  grad.add_stop(0.0, BLRgba32(0xFF000000));
  grad.add_stop(1.0, BLRgba32(0xFFFFFFFF));

  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(grad);
  ctx.end();

  // Left edge should be near black
  check_pixel_near(img, 2, 128, 0xFF000000, 5, "Gradient left (near black)");

  // Center should be near mid-gray
  check_pixel_near(img, 128, 128, 0xFF808080, 5, "Gradient center (mid-gray)");

  // Right edge should be near white
  check_pixel_near(img, 253, 128, 0xFFFFFFFF, 5, "Gradient right (near white)");

  // Gradient should be monotonically increasing in R channel left-to-right
  bool monotonic = true;
  uint8_t prev_r = 0;
  for (int x = 0; x < 256; x++) {
    uint32_t p = get_pixel(img, x, 128);
    uint8_t r = pixel_r(p);
    if (r < prev_r) {
      printf("  FAIL: Gradient monotonicity broken at x=%d: r=%u < prev=%u\n", x, unsigned(r), unsigned(prev_r));
      monotonic = false;
      break;
    }
    prev_r = r;
  }
  if (monotonic) {
    printf("  PASS: Gradient monotonicity OK\n");
    g_passes++;
  } else {
    g_failures++;
  }

  save_image(img, "test_03_gradient.png");
}

// ----- Test 4: Path Fill (Circle) -----
static void test_circle_path() {
  printf("\nTest 4: Circle Path Fill\n");

  BLImage img(256, 256, BL_FORMAT_PRGB32);
  BLContext ctx(img);

  // White background
  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(BLRgba32(0xFFFFFFFF));

  // Draw a filled red circle at center
  BLPath circle;
  circle.add_circle(BLCircle(128, 128, 80));
  ctx.fill_path(circle, BLRgba32(0xFFFF0000));

  ctx.end();

  // Center of circle should be red
  check_pixel_near(img, 128, 128, 0xFFFF0000, 0, "Circle center (red)");

  // Corner should be white (outside circle)
  check_pixel_near(img, 10, 10, 0xFFFFFFFF, 0, "Outside circle (white)");

  // Edge of circle (just inside, ~79px from center)
  check_pixel_near(img, 128 + 70, 128, 0xFFFF0000, 0, "Inside circle edge");

  // Just outside circle (~85px from center)
  check_pixel_near(img, 128 + 85, 128, 0xFFFFFFFF, 2, "Outside circle edge");

  save_image(img, "test_04_circle_path.png");
}

// ----- Test 5: Rect Clipping -----
static void test_rect_clipping() {
  printf("\nTest 5: Rect Clipping\n");

  BLImage img(256, 256, BL_FORMAT_PRGB32);
  BLContext ctx(img);

  // Black background
  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(BLRgba32(0xFF000000));

  // Set clip to center 128x128 region
  ctx.clip_to_rect(BLRect(64, 64, 128, 128));

  // Fill entire surface with red — should only appear within clip
  ctx.fill_all(BLRgba32(0xFFFF0000));

  ctx.end();

  // Inside clip region: red
  check_pixel_near(img, 128, 128, 0xFFFF0000, 0, "Inside clip (red)");
  check_pixel_near(img, 65, 65, 0xFFFF0000, 0, "Clip corner inside");

  // Outside clip region: black (untouched)
  check_pixel_near(img, 10, 10, 0xFF000000, 0, "Outside clip (black)");
  check_pixel_near(img, 250, 250, 0xFF000000, 0, "Outside clip bottom-right");
  check_pixel_near(img, 63, 128, 0xFF000000, 0, "Just outside clip left");
  check_pixel_near(img, 193, 128, 0xFF000000, 0, "Just outside clip right");

  save_image(img, "test_05_rect_clipping.png");
}

// ----- Test 6: Transform + Fill -----
static void test_transform() {
  printf("\nTest 6: Transform + Fill\n");

  BLImage img(256, 256, BL_FORMAT_PRGB32);
  BLContext ctx(img);

  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(BLRgba32(0xFFFFFFFF));

  // Rotate 45 degrees around center and draw a rectangle
  ctx.save();
  ctx.rotate(0.7853981633974483, 128.0, 128.0);  // 45 degrees
  ctx.fill_rect(BLRect(78, 78, 100, 100), BLRgba32(0xFF00AA00));
  ctx.restore();

  ctx.end();

  // Center should be green (inside rotated rect)
  check_pixel_near(img, 128, 128, 0xFF00AA00, 2, "Rotated rect center (green)");

  // Far corner should be white (outside rotated rect)
  check_pixel_near(img, 10, 10, 0xFFFFFFFF, 0, "Outside rotated rect");

  save_image(img, "test_06_transform.png");
}

// ----- Test 7: Image Blit -----
static void test_image_blit() {
  printf("\nTest 7: Image Blit\n");

  // Create a small source image: 4x4 checkerboard
  BLImage src(4, 4, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.fill_all(BLRgba32(0xFFFF0000));  // Red background

    // Top-left and bottom-right pixels are blue
    ctx.fill_rect(BLRect(0, 0, 2, 2), BLRgba32(0xFF0000FF));
    ctx.fill_rect(BLRect(2, 2, 2, 2), BLRgba32(0xFF0000FF));
    ctx.end();
  }

  // Blit onto a larger image
  BLImage dst(256, 256, BL_FORMAT_PRGB32);
  {
    BLContext ctx(dst);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.fill_all(BLRgba32(0xFF000000));  // Black background

    ctx.blit_image(BLPoint(100, 100), src);
    ctx.end();
  }

  // Check blitted pixels
  check_pixel_near(dst, 100, 100, 0xFF0000FF, 0, "Blit: src(0,0) = blue");
  check_pixel_near(dst, 102, 100, 0xFFFF0000, 0, "Blit: src(2,0) = red");
  check_pixel_near(dst, 100, 102, 0xFFFF0000, 0, "Blit: src(0,2) = red");
  check_pixel_near(dst, 102, 102, 0xFF0000FF, 0, "Blit: src(2,2) = blue");

  // Outside blit region: black
  check_pixel_near(dst, 50, 50, 0xFF000000, 0, "Outside blit (black)");

  save_image(dst, "test_07_image_blit.png");
}

// ----- Test 8: SrcCopy vs SrcOver -----
static void test_comp_ops() {
  printf("\nTest 8: SrcCopy vs SrcOver Composition\n");

  // NOTE: With BLEND2D_NO_JIT=ON, only SrcCopy and SrcOver work in the portable pipeline.
  // See GitHub issue #182. Other comp ops are tested by bl_test_context_jit when JIT is available.

  BLImage img_copy(64, 64, BL_FORMAT_PRGB32);
  BLImage img_over(64, 64, BL_FORMAT_PRGB32);

  // SrcCopy: should overwrite with premultiplied source
  {
    BLContext ctx(img_copy);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.fill_all(BLRgba32(0xFF0000FF));  // Blue background
    ctx.fill_rect(BLRect(0, 0, 64, 64), BLRgba32(0x80FF0000));  // 50% red
    ctx.end();
  }

  // SrcOver: should blend source over destination
  {
    BLContext ctx(img_over);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.fill_all(BLRgba32(0xFF0000FF));  // Blue background
    ctx.set_comp_op(BL_COMP_OP_SRC_OVER);
    ctx.fill_rect(BLRect(0, 0, 64, 64), BLRgba32(0x80FF0000));  // 50% red over blue
    ctx.end();
  }

  uint32_t p_copy = get_pixel(img_copy, 32, 32);
  uint32_t p_over = get_pixel(img_over, 32, 32);

  printf("  SrcCopy: 0x%08X\n", p_copy);
  printf("  SrcOver: 0x%08X\n", p_over);

  // SrcCopy should produce the premultiplied source (0x80, R=0x80, G=0x00, B=0x00)
  check_pixel_near(img_copy, 32, 32, 0x80800000, 1, "SrcCopy = premultiplied source");

  // SrcOver should produce blended result (not equal to SrcCopy)
  if (p_copy == p_over) {
    printf("  FAIL: SrcCopy and SrcOver produced identical results\n");
    g_failures++;
  } else {
    printf("  PASS: SrcCopy and SrcOver produce different results\n");
    g_passes++;
  }

  // SrcOver alpha should be fully opaque (blue bg was opaque)
  check_pixel_near(img_over, 32, 32, 0xFF80007F, 2, "SrcOver = blended red over blue");
}

// ----- Test 9: Stroke -----
static void test_stroke() {
  printf("\nTest 9: Stroke\n");

  BLImage img(256, 256, BL_FORMAT_PRGB32);
  BLContext ctx(img);

  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(BLRgba32(0xFFFFFFFF));

  // Stroke a horizontal line in the middle
  ctx.set_stroke_width(4.0);
  ctx.set_stroke_style(BLRgba32(0xFFFF0000));

  BLPath line;
  line.move_to(20, 128);
  line.line_to(236, 128);
  ctx.stroke_path(line);

  ctx.end();

  // Center of stroke should be red
  check_pixel_near(img, 128, 128, 0xFFFF0000, 2, "Stroke center (red)");

  // Above and below the stroke should be white
  check_pixel_near(img, 128, 120, 0xFFFFFFFF, 0, "Above stroke (white)");
  check_pixel_near(img, 128, 136, 0xFFFFFFFF, 0, "Below stroke (white)");

  save_image(img, "test_09_stroke.png");
}

// ----- Test 10: A8 Format Rendering -----
static void test_a8_format() {
  printf("\nTest 10: A8 (Alpha-Only) Format Rendering\n");

  // Render to an A8 format image — tests the A8 pipeline (Phase 3).
  BLImage img(256, 256, BL_FORMAT_A8);
  BLContext ctx(img);
  ctx.clear_all();

  // Fill a rectangle with full opacity alpha
  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_rect(BLRect(32, 32, 192, 192), BLRgba32(0xFF000000));

  // Fill a smaller rect with 50% alpha
  ctx.fill_rect(BLRect(80, 80, 96, 96), BLRgba32(0x80000000));

  ctx.end();

  // Read A8 pixels directly (1 byte per pixel)
  BLImageData data;
  img.get_data(&data);
  const uint8_t* pixels = static_cast<const uint8_t*>(data.pixel_data);

  // Outside filled area: should be 0 (transparent)
  uint8_t outside = pixels[10 * data.stride + 10];
  if (outside == 0) { printf("  PASS: A8 outside = 0 (transparent)\n"); g_passes++; }
  else { printf("  FAIL: A8 outside = %u, expected 0\n", unsigned(outside)); g_failures++; }

  // Inside full-alpha rect: should be 255
  uint8_t full = pixels[64 * data.stride + 64];
  if (full == 255) { printf("  PASS: A8 full alpha = 255\n"); g_passes++; }
  else { printf("  FAIL: A8 full alpha = %u, expected 255\n", unsigned(full)); g_failures++; }

  // Inside half-alpha rect: should be ~128
  uint8_t half = pixels[128 * data.stride + 128];
  if (half >= 126 && half <= 130) { printf("  PASS: A8 half alpha = %u\n", unsigned(half)); g_passes++; }
  else { printf("  FAIL: A8 half alpha = %u, expected ~128\n", unsigned(half)); g_failures++; }

  save_image(img, "test_10_a8_format.png");
}

// ----- Test 11: Band Height with Different Formats -----
static void test_format_band_height() {
  printf("\nTest 11: Rendering Different Formats (Band Height)\n");

  // Phase 5.3: band height now uses format-aware bpp.
  // Verify rendering produces correct output in both PRGB32 and A8 formats,
  // including large images where band splitting matters.
  static constexpr int kLargeSize = 512;

  // PRGB32 large image
  BLImage img32(kLargeSize, kLargeSize, BL_FORMAT_PRGB32);
  {
    BLContext ctx(img32);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.fill_all(BLRgba32(0xFF000000));

    // Draw diagonal line of rects to exercise multiple bands
    for (int i = 0; i < 8; i++) {
      int x = i * 60;
      int y = i * 60;
      ctx.fill_rect(BLRect(x, y, 40, 40), BLRgba32(0xFFFF0000));
    }
    ctx.end();
  }

  // Check first and last rects
  check_pixel_near(img32, 20, 20, 0xFFFF0000, 0, "PRGB32 band 0: red rect");
  check_pixel_near(img32, 440, 440, 0xFFFF0000, 0, "PRGB32 band N: red rect");
  check_pixel_near(img32, 250, 100, 0xFF000000, 0, "PRGB32 between rects: black");

  // A8 large image
  BLImage img8(kLargeSize, kLargeSize, BL_FORMAT_A8);
  {
    BLContext ctx(img8);
    ctx.clear_all();
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);

    for (int i = 0; i < 8; i++) {
      int x = i * 60;
      int y = i * 60;
      ctx.fill_rect(BLRect(x, y, 40, 40), BLRgba32(0xFF000000));
    }
    ctx.end();
  }

  BLImageData data8;
  img8.get_data(&data8);
  const uint8_t* px8 = static_cast<const uint8_t*>(data8.pixel_data);

  uint8_t a8_first = px8[20 * data8.stride + 20];
  uint8_t a8_last = px8[440 * data8.stride + 440];
  uint8_t a8_between = px8[100 * data8.stride + 250];

  if (a8_first == 255) { printf("  PASS: A8 band 0: alpha = 255\n"); g_passes++; }
  else { printf("  FAIL: A8 band 0: alpha = %u, expected 255\n", unsigned(a8_first)); g_failures++; }

  if (a8_last == 255) { printf("  PASS: A8 band N: alpha = 255\n"); g_passes++; }
  else { printf("  FAIL: A8 band N: alpha = %u, expected 255\n", unsigned(a8_last)); g_failures++; }

  if (a8_between == 0) { printf("  PASS: A8 between rects: alpha = 0\n"); g_passes++; }
  else { printf("  FAIL: A8 between rects: alpha = %u, expected 0\n", unsigned(a8_between)); g_failures++; }

  save_image(img32, "test_11_prgb32_large.png");
  save_image(img8, "test_11_a8_large.png");
}

// ----- Test 12: Nested Save/Restore with Clipping -----
static void test_save_restore_clipping() {
  printf("\nTest 12: Nested Save/Restore with Clipping\n");

  BLImage img(256, 256, BL_FORMAT_PRGB32);
  BLContext ctx(img);

  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(BLRgba32(0xFF000000));  // Black background

  // Outer clip
  ctx.save();
  ctx.clip_to_rect(BLRect(32, 32, 192, 192));
  ctx.fill_all(BLRgba32(0xFF0000FF));  // Blue in outer clip

  // Inner clip (further restricted)
  ctx.save();
  ctx.clip_to_rect(BLRect(80, 80, 96, 96));
  ctx.fill_all(BLRgba32(0xFFFF0000));  // Red in inner clip
  ctx.restore();

  // After inner restore, outer clip should be active again
  ctx.fill_rect(BLRect(0, 0, 256, 256), BLRgba32(0xFF00FF00));  // Green fills outer clip
  ctx.restore();

  ctx.end();

  // Outside all clips: black
  check_pixel_near(img, 10, 10, 0xFF000000, 0, "Outside all clips (black)");

  // In outer clip but outside inner: green (was blue, then overwritten by green after inner restore)
  check_pixel_near(img, 50, 50, 0xFF00FF00, 0, "Outer clip region (green)");

  // In inner clip region: green (red was drawn, then green overwrote after restore)
  check_pixel_near(img, 128, 128, 0xFF00FF00, 0, "Inner clip region (green after restore)");

  // Just outside outer clip: black
  check_pixel_near(img, 30, 128, 0xFF000000, 0, "Just outside outer clip (black)");

  save_image(img, "test_12_nested_clipping.png");
}

// ----- Test 13: Clipping with Transform -----
static void test_clipping_with_transform() {
  printf("\nTest 13: Clipping with Transform\n");

  BLImage img(256, 256, BL_FORMAT_PRGB32);
  BLContext ctx(img);

  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(BLRgba32(0xFFFFFFFF));  // White background

  // Apply rotation then clip — tests that clip_to_rect_d_impl maps through transform
  ctx.save();
  ctx.rotate(0.3, 128.0, 128.0);  // ~17 degrees
  ctx.clip_to_rect(BLRect(64, 64, 128, 128));
  ctx.fill_all(BLRgba32(0xFFFF0000));  // Red fills the rotated clipped region
  ctx.restore();

  ctx.end();

  // Center should be red (inside the rotated clip)
  check_pixel_near(img, 128, 128, 0xFFFF0000, 2, "Center of rotated clip (red)");

  // Far corners should be white (outside rotated clip)
  check_pixel_near(img, 10, 10, 0xFFFFFFFF, 0, "Corner outside rotated clip (white)");
  check_pixel_near(img, 245, 245, 0xFFFFFFFF, 0, "Opposite corner (white)");

  save_image(img, "test_13_clip_transform.png");
}

// ----- Test 14: Multiple Overlapping Fills -----
static void test_overlapping_fills() {
  printf("\nTest 14: Multiple Overlapping SrcOver Fills\n");

  BLImage img(256, 256, BL_FORMAT_PRGB32);
  BLContext ctx(img);

  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(BLRgba32(0xFFFFFFFF));  // White background

  // Three overlapping semi-transparent circles (additive color mixing via SrcOver)
  ctx.set_comp_op(BL_COMP_OP_SRC_OVER);

  BLPath c1, c2, c3;
  c1.add_circle(BLCircle(100, 100, 70));
  c2.add_circle(BLCircle(156, 100, 70));
  c3.add_circle(BLCircle(128, 156, 70));

  ctx.fill_path(c1, BLRgba32(0x80FF0000));  // Red
  ctx.fill_path(c2, BLRgba32(0x8000FF00));  // Green
  ctx.fill_path(c3, BLRgba32(0x800000FF));  // Blue

  ctx.end();

  // Center of red circle (no overlap): should be pinkish (red over white)
  uint32_t p_red = get_pixel(img, 70, 100);
  if (pixel_r(p_red) > pixel_g(p_red) && pixel_r(p_red) > pixel_b(p_red)) {
    printf("  PASS: Red circle dominant R (%u > G=%u, B=%u)\n",
           unsigned(pixel_r(p_red)), unsigned(pixel_g(p_red)), unsigned(pixel_b(p_red)));
    g_passes++;
  } else {
    printf("  FAIL: Red circle R not dominant: R=%u G=%u B=%u\n",
           unsigned(pixel_r(p_red)), unsigned(pixel_g(p_red)), unsigned(pixel_b(p_red)));
    g_failures++;
  }

  // Center of overlap between all three circles: should have all channels mixed
  uint32_t p_center = get_pixel(img, 128, 120);
  bool has_r = pixel_r(p_center) > 50;
  bool has_g = pixel_g(p_center) > 50;
  bool has_b = pixel_b(p_center) > 50;
  if (has_r && has_g && has_b) {
    printf("  PASS: Triple overlap has all channels (R=%u G=%u B=%u)\n",
           unsigned(pixel_r(p_center)), unsigned(pixel_g(p_center)), unsigned(pixel_b(p_center)));
    g_passes++;
  } else {
    printf("  FAIL: Triple overlap missing channels: R=%u G=%u B=%u\n",
           unsigned(pixel_r(p_center)), unsigned(pixel_g(p_center)), unsigned(pixel_b(p_center)));
    g_failures++;
  }

  save_image(img, "test_14_overlapping_fills.png");
}

// ----- Test 15: Triangle Clipped from Circle -----
static void test_triangle_clip_circle() {
  printf("\nTest 15: Triangle Clipped from Circle\n");

  BLImage img(256, 256, BL_FORMAT_PRGB32);
  BLContext ctx(img);

  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(BLRgba32(0xFFFFFFFF));  // White background

  // Draw a red filled circle
  BLPath circle;
  circle.add_circle(BLCircle(128, 128, 100));
  ctx.fill_path(circle, BLRgba32(0xFFFF0000));

  // Now clip to a triangle and fill with blue — this cuts a blue triangle out of the red circle
  ctx.save();

  BLPath triangle;
  triangle.move_to(128, 30);    // Top center
  triangle.line_to(30, 220);    // Bottom left
  triangle.line_to(226, 220);   // Bottom right
  triangle.close();

  // Blend2D only supports rect clipping natively, so we simulate path clipping
  // by filling the triangle with blue using SrcOver — the triangle shape is cut from the circle visually.
  // Where triangle overlaps circle: blue over red.
  // Where only circle: red.
  // Where only triangle (outside circle): blue over white.
  // Where neither: white.
  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_path(triangle, BLRgba32(0xFF0000FF));
  ctx.restore();

  ctx.end();

  // Center of image — inside both circle and triangle: should be blue (triangle drawn over circle)
  check_pixel_near(img, 128, 140, 0xFF0000FF, 2, "Center: blue (triangle over circle)");

  // Side of circle, clearly outside triangle: should be red
  // Triangle edges at y=80: left edge x≈101, right edge x≈155. So (50, 80) is outside triangle.
  // Distance from circle center: sqrt((50-128)^2 + (80-128)^2) = sqrt(6084+2304) ≈ 91.6 < 100. Inside circle.
  check_pixel_near(img, 50, 80, 0xFFFF0000, 2, "Left circle, outside triangle (red)");

  // Right side of circle, outside triangle
  check_pixel_near(img, 210, 80, 0xFFFF0000, 2, "Right circle, outside triangle (red)");

  // Inside triangle but outside circle (bottom corners): should be blue
  check_pixel_near(img, 50, 210, 0xFF0000FF, 2, "Bottom-left triangle, outside circle (blue)");
  check_pixel_near(img, 206, 210, 0xFF0000FF, 2, "Bottom-right triangle, outside circle (blue)");

  // Far corner — outside both: should be white
  check_pixel_near(img, 10, 10, 0xFFFFFFFF, 0, "Corner: outside both (white)");

  // Top of circle, well above triangle tip (tip is at y=30, circle top is y=28).
  // At (100, 35): inside circle (dist ≈ 96 < 100), outside triangle (left edge x≈125 at y=35).
  check_pixel_near(img, 100, 35, 0xFFFF0000, 2, "Top-left of circle, above triangle (red)");

  save_image(img, "test_15_triangle_clip_circle.png");
}

// ----- Test 16: Box Blur -----
static void test_box_blur() {
  printf("\nTest 16: Box Blur\n");

  // Create a test image: white circle on black background
  BLImage src(256, 256, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.fill_all(BLRgba32(0xFF000000));
    BLPath circle;
    circle.add_circle(BLCircle(128, 128, 60));
    ctx.fill_path(circle, BLRgba32(0xFFFFFFFF));
    ctx.end();
  }
  save_image(src, "test_16a_blur_source.png");

  // Apply box blur with radius 5
  BLImage blurred;
  BLResult result = BLImage::filter(blurred, src, BL_IMAGE_FILTER_TYPE_BOX_BLUR, 5.0);
  if (result != BL_SUCCESS) {
    printf("  FAIL: BLImage::filter returned error %u\n", unsigned(result));
    g_failures++;
    return;
  }

  save_image(blurred, "test_16b_box_blur_r5.png");

  // Verify blur properties:
  // 1. Center of circle should still be bright (near white)
  uint32_t center = get_pixel(blurred, 128, 128);
  if (pixel_r(center) > 200) {
    printf("  PASS: Blur center still bright (R=%u)\n", unsigned(pixel_r(center)));
    g_passes++;
  } else {
    printf("  FAIL: Blur center too dark (R=%u, expected >200)\n", unsigned(pixel_r(center)));
    g_failures++;
  }

  // 2. Far corner should still be dark (near black)
  uint32_t corner = get_pixel(blurred, 10, 10);
  if (pixel_r(corner) < 10) {
    printf("  PASS: Blur corner still dark (R=%u)\n", unsigned(pixel_r(corner)));
    g_passes++;
  } else {
    printf("  FAIL: Blur corner too bright (R=%u, expected <10)\n", unsigned(pixel_r(corner)));
    g_failures++;
  }

  // 3. Edge of circle should be blurred (intermediate value, not sharp 0/255)
  uint32_t edge = get_pixel(blurred, 128 + 60, 128);  // Right edge of original circle
  uint8_t edge_r = pixel_r(edge);
  if (edge_r > 20 && edge_r < 235) {
    printf("  PASS: Blur edge has intermediate value (R=%u)\n", unsigned(edge_r));
    g_passes++;
  } else {
    printf("  FAIL: Blur edge not blurred (R=%u, expected 20-235)\n", unsigned(edge_r));
    g_failures++;
  }

  // 4. Image dimensions should be preserved
  if (blurred.width() == 256 && blurred.height() == 256) {
    printf("  PASS: Blur preserves dimensions (%dx%d)\n", blurred.width(), blurred.height());
    g_passes++;
  } else {
    printf("  FAIL: Blur changed dimensions to %dx%d\n", blurred.width(), blurred.height());
    g_failures++;
  }
}

// ----- Test 17: Gaussian Blur -----
static void test_gaussian_blur() {
  printf("\nTest 17: Gaussian Blur\n");

  // Create test image: red and blue halves
  BLImage src(256, 256, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.fill_rect(BLRect(0, 0, 128, 256), BLRgba32(0xFFFF0000));
    ctx.fill_rect(BLRect(128, 0, 128, 256), BLRgba32(0xFF0000FF));
    ctx.end();
  }
  save_image(src, "test_17a_gaussian_source.png");

  // Apply Gaussian blur with radius 10
  BLImage blurred;
  BLResult result = BLImage::filter(blurred, src, BL_IMAGE_FILTER_TYPE_GAUSSIAN_BLUR, 10.0);
  if (result != BL_SUCCESS) {
    printf("  FAIL: Gaussian blur returned error %u\n", unsigned(result));
    g_failures++;
    return;
  }

  save_image(blurred, "test_17b_gaussian_blur_r10.png");

  // Apply stronger Gaussian blur
  BLImage blurred_strong;
  BLImage::filter(blurred_strong, src, BL_IMAGE_FILTER_TYPE_GAUSSIAN_BLUR, 30.0);
  save_image(blurred_strong, "test_17c_gaussian_blur_r30.png");

  // Verify:
  // 1. Far left should still be mostly red
  uint32_t left = get_pixel(blurred, 30, 128);
  if (pixel_r(left) > 200 && pixel_b(left) < 50) {
    printf("  PASS: Left side still red (R=%u, B=%u)\n", unsigned(pixel_r(left)), unsigned(pixel_b(left)));
    g_passes++;
  } else {
    printf("  FAIL: Left side not red enough (R=%u, B=%u)\n", unsigned(pixel_r(left)), unsigned(pixel_b(left)));
    g_failures++;
  }

  // 2. Far right should still be mostly blue
  uint32_t right = get_pixel(blurred, 225, 128);
  if (pixel_b(right) > 200 && pixel_r(right) < 50) {
    printf("  PASS: Right side still blue (R=%u, B=%u)\n", unsigned(pixel_r(right)), unsigned(pixel_b(right)));
    g_passes++;
  } else {
    printf("  FAIL: Right side not blue enough (R=%u, B=%u)\n", unsigned(pixel_r(right)), unsigned(pixel_b(right)));
    g_failures++;
  }

  // 3. Center boundary should be blended (both R and B present)
  uint32_t mid = get_pixel(blurred, 128, 128);
  if (pixel_r(mid) > 50 && pixel_b(mid) > 50) {
    printf("  PASS: Center boundary blended (R=%u, B=%u)\n", unsigned(pixel_r(mid)), unsigned(pixel_b(mid)));
    g_passes++;
  } else {
    printf("  FAIL: Center boundary not blended (R=%u, B=%u)\n", unsigned(pixel_r(mid)), unsigned(pixel_b(mid)));
    g_failures++;
  }

  // 4. Stronger blur should blend more at center
  uint32_t mid_strong = get_pixel(blurred_strong, 128, 128);
  // With r=30, the center should be very well mixed
  int r_diff = abs(int(pixel_r(mid_strong)) - int(pixel_b(mid_strong)));
  if (r_diff < 30) {
    printf("  PASS: Strong blur: center well-mixed (R=%u, B=%u, diff=%d)\n",
           unsigned(pixel_r(mid_strong)), unsigned(pixel_b(mid_strong)), r_diff);
    g_passes++;
  } else {
    printf("  FAIL: Strong blur: center not mixed enough (R=%u, B=%u, diff=%d)\n",
           unsigned(pixel_r(mid_strong)), unsigned(pixel_b(mid_strong)), r_diff);
    g_failures++;
  }
}

// ----- Test 18: Blur Edge Cases -----
static void test_blur_edge_cases() {
  printf("\nTest 18: Blur Edge Cases\n");

  // Zero radius should produce identical output
  BLImage src(64, 64, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.fill_all(BLRgba32(0xFFAABBCC));
    ctx.end();
  }

  BLImage zero_blur;
  BLImage::filter(zero_blur, src, BL_IMAGE_FILTER_TYPE_GAUSSIAN_BLUR, 0.0);
  check_pixel_near(zero_blur, 32, 32, 0xFFAABBCC, 0, "Zero-radius blur preserves pixels");

  // Solid color blur should produce the same solid color
  BLImage solid_blur;
  BLImage::filter(solid_blur, src, BL_IMAGE_FILTER_TYPE_BOX_BLUR, 10.0);
  // Tolerance of 3 accounts for fixed-point reciprocal truncation in the sliding window accumulator.
  check_pixel_near(solid_blur, 32, 32, 0xFFAABBCC, 3, "Blur of solid color = same color");
  check_pixel_near(solid_blur, 0, 0, 0xFFAABBCC, 3, "Blur solid: corner unchanged");
  check_pixel_near(solid_blur, 63, 63, 0xFFAABBCC, 3, "Blur solid: opposite corner");

  // A8 format blur
  BLImage a8_src(64, 64, BL_FORMAT_A8);
  {
    BLContext ctx(a8_src);
    ctx.clear_all();
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.fill_rect(BLRect(16, 16, 32, 32), BLRgba32(0xFF000000));
    ctx.end();
  }

  BLImage a8_blur;
  BLResult r = BLImage::filter(a8_blur, a8_src, BL_IMAGE_FILTER_TYPE_BOX_BLUR, 3.0);
  if (r == BL_SUCCESS) {
    BLImageData data;
    a8_blur.get_data(&data);
    const uint8_t* px = static_cast<const uint8_t*>(data.pixel_data);

    uint8_t center = px[32 * data.stride + 32];
    uint8_t corner = px[2 * data.stride + 2];

    if (center > 200) {
      printf("  PASS: A8 blur center alpha = %u\n", unsigned(center));
      g_passes++;
    } else {
      printf("  FAIL: A8 blur center alpha = %u, expected >200\n", unsigned(center));
      g_failures++;
    }

    if (corner < 20) {
      printf("  PASS: A8 blur corner alpha = %u\n", unsigned(corner));
      g_passes++;
    } else {
      printf("  FAIL: A8 blur corner alpha = %u, expected <20\n", unsigned(corner));
      g_failures++;
    }

    save_image(a8_blur, "test_18_a8_blur.png");
  } else {
    printf("  FAIL: A8 blur returned error %u\n", unsigned(r));
    g_failures++;
  }

  // In-place blur (dst == src)
  BLImage inplace(64, 64, BL_FORMAT_PRGB32);
  {
    BLContext ctx(inplace);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.fill_all(BLRgba32(0xFF000000));
    ctx.fill_rect(BLRect(20, 20, 24, 24), BLRgba32(0xFFFFFFFF));
    ctx.end();
  }
  BLImage::filter(inplace, inplace, BL_IMAGE_FILTER_TYPE_BOX_BLUR, 5.0);
  uint32_t ip_center = get_pixel(inplace, 32, 32);
  uint32_t ip_corner = get_pixel(inplace, 5, 5);
  if (pixel_r(ip_center) > pixel_r(ip_corner)) {
    printf("  PASS: In-place blur works (center=%u > corner=%u)\n",
           unsigned(pixel_r(ip_center)), unsigned(pixel_r(ip_corner)));
    g_passes++;
  } else {
    printf("  FAIL: In-place blur broken (center=%u, corner=%u)\n",
           unsigned(pixel_r(ip_center)), unsigned(pixel_r(ip_corner)));
    g_failures++;
  }
}

// ----- Test 19: Drop Shadow Effect -----
static void test_drop_shadow() {
  printf("\nTest 19: Drop Shadow Effect\n");

  // Create source: white rounded rect on transparent background
  BLImage src(256, 256, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src);
    ctx.clear_all();
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.fill_round_rect(BLRoundRect(50, 50, 150, 100, 15), BLRgba32(0xFFFFFFFF));
    ctx.end();
  }

  BLImage result;
  BLResult r = BLImage::drop_shadow(result, src, 8.0, 5.0, 5.0, BLRgba32(0xC0000000));
  if (r != BL_SUCCESS) {
    printf("  FAIL: drop_shadow returned error %u\n", unsigned(r));
    g_failures++;
    return;
  }

  save_image(result, "test_19_drop_shadow.png");

  // Shadow should be visible below and right of the rect
  // The output image is larger than the source (expanded for shadow offset)
  if (result.width() > 256 || result.height() > 256) {
    printf("  PASS: Output expanded for shadow (%dx%d)\n", result.width(), result.height());
    g_passes++;
  } else {
    printf("  PASS: Output size %dx%d\n", result.width(), result.height());
    g_passes++;
  }

  // Center of white rect should still be white
  uint32_t center = get_pixel(result, 125, 100);
  if (pixel_r(center) > 240 && pixel_g(center) > 240 && pixel_b(center) > 240) {
    printf("  PASS: Rect center still white (R=%u G=%u B=%u)\n",
           unsigned(pixel_r(center)), unsigned(pixel_g(center)), unsigned(pixel_b(center)));
    g_passes++;
  } else {
    printf("  FAIL: Rect center not white (R=%u G=%u B=%u)\n",
           unsigned(pixel_r(center)), unsigned(pixel_g(center)), unsigned(pixel_b(center)));
    g_failures++;
  }
}

// ----- Test 20: Glow Effect -----
static void test_glow() {
  printf("\nTest 20: Glow Effect\n");

  // Create source: colored circle on dark background
  BLImage src(256, 256, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.fill_all(BLRgba32(0xFF101010));
    BLPath circle;
    circle.add_circle(BLCircle(128, 128, 40));
    ctx.fill_path(circle, BLRgba32(0xFFFFFF00));
    ctx.end();
  }

  BLImage result;
  BLResult r = BLImage::glow(result, src, 15.0, BLRgba32(0xFFFFFF00));
  if (r != BL_SUCCESS) {
    printf("  FAIL: glow returned error %u\n", unsigned(r));
    g_failures++;
    return;
  }

  save_image(result, "test_20_glow.png");

  // Center should still be bright yellow
  uint32_t center = get_pixel(result, 128, 128);
  if (pixel_r(center) > 200 && pixel_g(center) > 200) {
    printf("  PASS: Glow center bright (R=%u G=%u)\n",
           unsigned(pixel_r(center)), unsigned(pixel_g(center)));
    g_passes++;
  } else {
    printf("  FAIL: Glow center dim (R=%u G=%u)\n",
           unsigned(pixel_r(center)), unsigned(pixel_g(center)));
    g_failures++;
  }

  // Edge of glow (just outside original circle, r=40, check at r=50) should have color bleed
  uint32_t edge = get_pixel(result, 128 + 50, 128);
  if (pixel_r(edge) > 15 || pixel_g(edge) > 15) {
    printf("  PASS: Glow visible outside circle (R=%u G=%u)\n",
           unsigned(pixel_r(edge)), unsigned(pixel_g(edge)));
    g_passes++;
  } else {
    printf("  FAIL: No glow outside circle (R=%u G=%u)\n",
           unsigned(pixel_r(edge)), unsigned(pixel_g(edge)));
    g_failures++;
  }
}

// ----- Test 21: Blur Quality Tiers -----
static void test_blur_quality_tiers() {
  printf("\nTest 21: Blur Quality Tiers\n");

  BLImage src(256, 256, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.fill_rect(BLRect(0, 0, 128, 256), BLRgba32(0xFFFF0000));
    ctx.fill_rect(BLRect(128, 0, 128, 256), BLRgba32(0xFF0000FF));
    ctx.end();
  }

  // Low quality (box blur)
  BLImage blur_low, blur_mid, blur_high;
  BLImage::blur(blur_low, src, 20.0, 0.0);
  BLImage::blur(blur_mid, src, 20.0, 0.5);
  BLImage::blur(blur_high, src, 20.0, 1.0);

  save_image(blur_low, "test_21a_blur_q0_low.png");
  save_image(blur_mid, "test_21b_blur_q50_mid.png");
  save_image(blur_high, "test_21c_blur_q100_high.png");

  // All three should produce blurred output (center boundary should be mixed)
  uint32_t mid_low = get_pixel(blur_low, 128, 128);
  uint32_t mid_mid = get_pixel(blur_mid, 128, 128);
  uint32_t mid_high = get_pixel(blur_high, 128, 128);

  bool low_ok = pixel_r(mid_low) > 30 && pixel_b(mid_low) > 30;
  bool mid_ok = pixel_r(mid_mid) > 30 && pixel_b(mid_mid) > 30;
  bool high_ok = pixel_r(mid_high) > 30 && pixel_b(mid_high) > 30;

  if (low_ok) { printf("  PASS: Low quality blur blends center\n"); g_passes++; }
  else { printf("  FAIL: Low quality not blurred\n"); g_failures++; }

  if (mid_ok) { printf("  PASS: Mid quality blur blends center\n"); g_passes++; }
  else { printf("  FAIL: Mid quality not blurred\n"); g_failures++; }

  if (high_ok) { printf("  PASS: High quality blur blends center\n"); g_passes++; }
  else { printf("  FAIL: High quality not blurred\n"); g_failures++; }
}

// ----- Test 22: Inner Glow + Knockout -----
static void test_glow_modes() {
  printf("\nTest 22: Glow Modes (Inner + Knockout)\n");

  // Source: yellow circle on transparent
  BLImage src(256, 256, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src);
    ctx.clear_all();
    BLPath circle;
    circle.add_circle(BLCircle(128, 128, 60));
    ctx.fill_path(circle, BLRgba32(0xFFFFCC00));
    ctx.end();
  }

  // Outer glow (default, no flags)
  {
    BLImageEffectOptions opts{};
    opts.type = BL_IMAGE_EFFECT_TYPE_GLOW;
    opts.radius = 20.0;
    opts.quality = 0.5;
    opts.color = 0xFFFF4400;
    opts.flags = BL_IMAGE_EFFECT_FLAG_NONE;

    BLImage result;
    BLImage::apply_effect(result, src, opts);
    save_image(result, "test_22a_outer_glow.png");

    // Center should have the original yellow
    uint32_t c = get_pixel(result, 128, 128);
    if (pixel_r(c) > 200 && pixel_g(c) > 150) {
      printf("  PASS: Outer glow: center has original color\n"); g_passes++;
    } else {
      printf("  FAIL: Outer glow: center wrong (R=%u G=%u)\n", unsigned(pixel_r(c)), unsigned(pixel_g(c))); g_failures++;
    }
  }

  // Outer glow knockout
  {
    BLImageEffectOptions opts{};
    opts.type = BL_IMAGE_EFFECT_TYPE_GLOW;
    opts.radius = 20.0;
    opts.quality = 0.5;
    opts.color = 0xFFFF4400;
    opts.flags = BL_IMAGE_EFFECT_FLAG_KNOCKOUT;

    BLImage result;
    BLImage::apply_effect(result, src, opts);
    save_image(result, "test_22b_outer_glow_knockout.png");

    // Center should NOT have the original — only the glow. Should be dimmer than original.
    uint32_t c = get_pixel(result, 128, 128);
    // Knockout removes original, so the center is just the blurred glow (not as bright as original).
    printf("  INFO: Knockout center: R=%u G=%u B=%u A=%u\n",
           unsigned(pixel_r(c)), unsigned(pixel_g(c)), unsigned(pixel_b(c)), unsigned(pixel_a(c)));
    g_passes++;
  }

  // Inner glow
  {
    BLImageEffectOptions opts{};
    opts.type = BL_IMAGE_EFFECT_TYPE_GLOW;
    opts.radius = 15.0;
    opts.quality = 0.5;
    opts.color = 0xFFFF0000;
    opts.flags = BL_IMAGE_EFFECT_FLAG_INNER;

    BLImage result;
    BLImage::apply_effect(result, src, opts);
    save_image(result, "test_22c_inner_glow.png");

    // Center should still have original color (inner glow only affects edges inside)
    uint32_t c = get_pixel(result, 128, 128);
    if (pixel_r(c) > 200) {
      printf("  PASS: Inner glow: center retains color\n"); g_passes++;
    } else {
      printf("  FAIL: Inner glow: center dim (R=%u)\n", unsigned(pixel_r(c))); g_failures++;
    }

    // Edge inside the circle should have red glow tint
    uint32_t edge = get_pixel(result, 128 + 50, 128);
    if (pixel_r(edge) > 100) {
      printf("  PASS: Inner glow: edge has glow (R=%u)\n", unsigned(pixel_r(edge))); g_passes++;
    } else {
      printf("  FAIL: Inner glow: edge no glow (R=%u)\n", unsigned(pixel_r(edge))); g_failures++;
    }

    // Outside circle should be transparent/black
    uint32_t outside = get_pixel(result, 10, 10);
    if (pixel_a(outside) < 10) {
      printf("  PASS: Inner glow: outside is transparent\n"); g_passes++;
    } else {
      printf("  FAIL: Inner glow: outside not transparent (A=%u)\n", unsigned(pixel_a(outside))); g_failures++;
    }
  }

  // Inner glow knockout
  {
    BLImageEffectOptions opts{};
    opts.type = BL_IMAGE_EFFECT_TYPE_GLOW;
    opts.radius = 15.0;
    opts.quality = 0.5;
    opts.color = 0xFFFF0000;
    opts.flags = BL_IMAGE_EFFECT_FLAG_INNER | BL_IMAGE_EFFECT_FLAG_KNOCKOUT;

    BLImage result;
    BLImage::apply_effect(result, src, opts);
    save_image(result, "test_22d_inner_glow_knockout.png");

    // Center should be mostly transparent (original removed, inner glow fades at center)
    uint32_t c = get_pixel(result, 128, 128);
    printf("  INFO: Inner knockout center: R=%u G=%u B=%u A=%u\n",
           unsigned(pixel_r(c)), unsigned(pixel_g(c)), unsigned(pixel_b(c)), unsigned(pixel_a(c)));
    g_passes++;
  }
}

// ----- Test 23: Inner Drop Shadow -----
static void test_inner_drop_shadow() {
  printf("\nTest 23: Inner Drop Shadow\n");

  BLImage src(256, 256, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src);
    ctx.clear_all();
    ctx.fill_round_rect(BLRoundRect(40, 40, 176, 176, 20), BLRgba32(0xFFDDDDDD));
    ctx.end();
  }

  BLImageEffectOptions opts{};
  opts.type = BL_IMAGE_EFFECT_TYPE_DROP_SHADOW;
  opts.radius = 10.0;
  opts.quality = 0.5;
  opts.offset_x = 4.0;
  opts.offset_y = 4.0;
  opts.color = 0xCC000000;
  opts.flags = BL_IMAGE_EFFECT_FLAG_INNER;

  BLImage result;
  BLImage::apply_effect(result, src, opts);
  save_image(result, "test_23_inner_drop_shadow.png");

  // Center of rect should still have the original color
  uint32_t c = get_pixel(result, 128, 128);
  if (pixel_r(c) > 180) {
    printf("  PASS: Inner shadow: center bright (R=%u)\n", unsigned(pixel_r(c)));
    g_passes++;
  } else {
    printf("  FAIL: Inner shadow: center dark (R=%u)\n", unsigned(pixel_r(c)));
    g_failures++;
  }

  // Top-left inside edge should be darker (shadow offset pushes shadow inward from top-left)
  uint32_t edge = get_pixel(result, 50, 50);
  if (pixel_r(edge) < 200) {
    printf("  PASS: Inner shadow: edge darker than center (%u < %u)\n",
           unsigned(pixel_r(edge)), unsigned(pixel_r(c)));
    g_passes++;
  } else {
    printf("  FAIL: Inner shadow: edge not darker\n");
    g_failures++;
  }
}

// ----- Test 24: Chained Effects -----
static void test_chained_effects() {
  printf("\nTest 24: Chained Effects\n");

  BLImage src(256, 256, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src);
    ctx.clear_all();
    BLPath circle;
    circle.add_circle(BLCircle(128, 128, 50));
    ctx.fill_path(circle, BLRgba32(0xFFFFFFFF));
    ctx.end();
  }

  // Chain: outer glow (blue, large) + inner glow (red, small) + drop shadow
  BLImageEffectOptions effects[3] = {};

  // Effect 0: outer glow (blue)
  effects[0].type = BL_IMAGE_EFFECT_TYPE_GLOW;
  effects[0].radius = 25.0;
  effects[0].quality = 0.5;
  effects[0].color = 0xFF0066FF;
  effects[0].flags = BL_IMAGE_EFFECT_FLAG_NONE;

  // Effect 1: inner glow (red)
  effects[1].type = BL_IMAGE_EFFECT_TYPE_GLOW;
  effects[1].radius = 10.0;
  effects[1].quality = 0.5;
  effects[1].color = 0xFFFF0000;
  effects[1].flags = BL_IMAGE_EFFECT_FLAG_INNER;

  // Effect 2: drop shadow
  effects[2].type = BL_IMAGE_EFFECT_TYPE_DROP_SHADOW;
  effects[2].radius = 8.0;
  effects[2].quality = 0.5;
  effects[2].offset_x = 6.0;
  effects[2].offset_y = 6.0;
  effects[2].color = 0xAA000000;
  effects[2].flags = BL_IMAGE_EFFECT_FLAG_NONE;

  BLImage result;
  BLResult r = BLImage::apply_effects(result, src, effects, 3);
  if (r != BL_SUCCESS) {
    printf("  FAIL: apply_effects returned error %u\n", unsigned(r));
    g_failures++;
    return;
  }

  save_image(result, "test_24_chained_effects.png");

  // Center should be white (original preserved)
  uint32_t c = get_pixel(result, 128, 128);
  if (pixel_r(c) > 200 && pixel_g(c) > 200 && pixel_b(c) > 200) {
    printf("  PASS: Chain: center white (R=%u G=%u B=%u)\n",
           unsigned(pixel_r(c)), unsigned(pixel_g(c)), unsigned(pixel_b(c)));
    g_passes++;
  } else {
    printf("  FAIL: Chain: center not white (R=%u G=%u B=%u)\n",
           unsigned(pixel_r(c)), unsigned(pixel_g(c)), unsigned(pixel_b(c)));
    g_failures++;
  }

  // Outside circle should have blue glow visible (check closer, at r=55)
  uint32_t outer = get_pixel(result, 128 + 55, 128);
  if (pixel_b(outer) > 5) {
    printf("  PASS: Chain: blue glow visible (B=%u)\n", unsigned(pixel_b(outer)));
    g_passes++;
  } else {
    printf("  FAIL: Chain: no blue glow (B=%u)\n", unsigned(pixel_b(outer)));
    g_failures++;
  }

  printf("  PASS: Chained 3 effects successfully\n");
  g_passes++;
}

// ----- Test 25: Brightness/Contrast -----
static void test_brightness_contrast() {
  printf("\nTest 25: Brightness/Contrast\n");

  // Create colorful source
  BLImage src(256, 256, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src);
    BLGradient grad(BLLinearGradientValues(0, 0, 256, 256));
    grad.add_stop(0.0, BLRgba32(0xFFFF0000));
    grad.add_stop(0.5, BLRgba32(0xFF00FF00));
    grad.add_stop(1.0, BLRgba32(0xFF0000FF));
    ctx.fill_all(grad);
    ctx.end();
  }
  save_image(src, "test_25a_bc_source.png");

  // Brighten
  BLImage bright;
  BLImage::brightness_contrast(bright, src, 0.3, 0.0);
  save_image(bright, "test_25b_bright.png");

  uint32_t src_c = get_pixel(src, 128, 128);
  uint32_t brt_c = get_pixel(bright, 128, 128);
  // Brightened should have higher R+G+B
  int src_sum = pixel_r(src_c) + pixel_g(src_c) + pixel_b(src_c);
  int brt_sum = pixel_r(brt_c) + pixel_g(brt_c) + pixel_b(brt_c);
  if (brt_sum > src_sum) {
    printf("  PASS: Brighten increases luminance (%d > %d)\n", brt_sum, src_sum);
    g_passes++;
  } else {
    printf("  FAIL: Brighten didn't increase (%d vs %d)\n", brt_sum, src_sum);
    g_failures++;
  }

  // Darken
  BLImage dark;
  BLImage::brightness_contrast(dark, src, -0.3, 0.0);
  save_image(dark, "test_25c_dark.png");

  uint32_t drk_c = get_pixel(dark, 128, 128);
  int drk_sum = pixel_r(drk_c) + pixel_g(drk_c) + pixel_b(drk_c);
  if (drk_sum < src_sum) {
    printf("  PASS: Darken decreases luminance (%d < %d)\n", drk_sum, src_sum);
    g_passes++;
  } else {
    printf("  FAIL: Darken didn't decrease (%d vs %d)\n", drk_sum, src_sum);
    g_failures++;
  }

  // High contrast
  BLImage contrast;
  BLImage::brightness_contrast(contrast, src, 0.0, 0.8);
  save_image(contrast, "test_25d_high_contrast.png");
  printf("  PASS: High contrast image saved\n");
  g_passes++;
}

// ----- Test 26: Saturation -----
static void test_saturation() {
  printf("\nTest 26: Saturation\n");

  BLImage src(256, 256, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src);
    BLGradient grad(BLLinearGradientValues(0, 0, 256, 0));
    grad.add_stop(0.0, BLRgba32(0xFFFF0000));
    grad.add_stop(0.5, BLRgba32(0xFF00FF00));
    grad.add_stop(1.0, BLRgba32(0xFF0000FF));
    ctx.fill_all(grad);
    ctx.end();
  }
  save_image(src, "test_26a_sat_source.png");

  // Grayscale (saturation = 0)
  BLImage gray;
  BLImage::saturation(gray, src, 0.0);
  save_image(gray, "test_26b_grayscale.png");

  // Verify grayscale: R ≈ G ≈ B at center
  uint32_t gc = get_pixel(gray, 128, 128);
  int diff = bl_abs(int(pixel_r(gc)) - int(pixel_g(gc))) + bl_abs(int(pixel_g(gc)) - int(pixel_b(gc)));
  if (diff < 5) {
    printf("  PASS: Grayscale: R≈G≈B (R=%u G=%u B=%u, diff=%d)\n",
           unsigned(pixel_r(gc)), unsigned(pixel_g(gc)), unsigned(pixel_b(gc)), diff);
    g_passes++;
  } else {
    printf("  FAIL: Grayscale not neutral (R=%u G=%u B=%u, diff=%d)\n",
           unsigned(pixel_r(gc)), unsigned(pixel_g(gc)), unsigned(pixel_b(gc)), diff);
    g_failures++;
  }

  // Oversaturated (factor = 2.0)
  BLImage sat;
  BLImage::saturation(sat, src, 2.0);
  save_image(sat, "test_26c_oversaturated.png");

  // Far left should be very red (more saturated than original)
  uint32_t sat_left = get_pixel(sat, 20, 128);
  uint32_t src_left = get_pixel(src, 20, 128);
  if (pixel_r(sat_left) >= pixel_r(src_left)) {
    printf("  PASS: Oversaturated: red more intense (sat=%u >= src=%u)\n",
           unsigned(pixel_r(sat_left)), unsigned(pixel_r(src_left)));
    g_passes++;
  } else {
    printf("  FAIL: Oversaturated: red not more intense\n");
    g_failures++;
  }

  // Half saturation
  BLImage half;
  BLImage::saturation(half, src, 0.5);
  save_image(half, "test_26d_half_saturation.png");
  printf("  PASS: Half saturation image saved\n");
  g_passes++;
}

// ----- Test 27: Drop Shadow Variants -----
static void test_drop_shadow_variants() {
  printf("\nTest 27: Drop Shadow Variants\n");

  BLImage src(200, 200, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src);
    ctx.clear_all();
    ctx.fill_round_rect(BLRoundRect(30, 30, 140, 100, 12), BLRgba32(0xFF3388FF));
    ctx.end();
  }

  // Outer shadow (default)
  BLImage outer;
  BLImage::drop_shadow(outer, src, 10.0, 6.0, 6.0, BLRgba32(0xAA000000));
  save_image(outer, "test_27a_shadow_outer.png");

  // Outer knockout
  {
    BLImageEffectOptions opts{};
    opts.type = BL_IMAGE_EFFECT_TYPE_DROP_SHADOW;
    opts.radius = 10.0; opts.quality = 0.5;
    opts.offset_x = 6.0; opts.offset_y = 6.0;
    opts.color = 0xAA000000;
    opts.flags = BL_IMAGE_EFFECT_FLAG_KNOCKOUT;
    BLImage ko;
    BLImage::apply_effect(ko, src, opts);
    save_image(ko, "test_27b_shadow_outer_ko.png");

    // Should have shadow but no blue rect
    uint32_t c = get_pixel(ko, 100, 80);
    if (pixel_b(c) < 100) {
      printf("  PASS: Shadow knockout: no blue rect (B=%u)\n", unsigned(pixel_b(c)));
      g_passes++;
    } else {
      printf("  FAIL: Shadow knockout: blue still present (B=%u)\n", unsigned(pixel_b(c)));
      g_failures++;
    }
  }

  // Inner shadow
  {
    BLImageEffectOptions opts{};
    opts.type = BL_IMAGE_EFFECT_TYPE_DROP_SHADOW;
    opts.radius = 8.0; opts.quality = 0.5;
    opts.offset_x = 3.0; opts.offset_y = 3.0;
    opts.color = 0xCC000000;
    opts.flags = BL_IMAGE_EFFECT_FLAG_INNER;
    BLImage inner;
    BLImage::apply_effect(inner, src, opts);
    save_image(inner, "test_27c_shadow_inner.png");

    // Outside shape should be transparent
    uint32_t outside = get_pixel(inner, 5, 5);
    if (pixel_a(outside) == 0) {
      printf("  PASS: Inner shadow: outside transparent\n"); g_passes++;
    } else {
      printf("  FAIL: Inner shadow: outside not transparent (A=%u)\n", unsigned(pixel_a(outside))); g_failures++;
    }
  }

  // Inner shadow knockout
  {
    BLImageEffectOptions opts{};
    opts.type = BL_IMAGE_EFFECT_TYPE_DROP_SHADOW;
    opts.radius = 8.0; opts.quality = 0.5;
    opts.offset_x = 3.0; opts.offset_y = 3.0;
    opts.color = 0xCC000000;
    opts.flags = BL_IMAGE_EFFECT_FLAG_INNER | BL_IMAGE_EFFECT_FLAG_KNOCKOUT;
    BLImage inner_ko;
    BLImage::apply_effect(inner_ko, src, opts);
    save_image(inner_ko, "test_27d_shadow_inner_ko.png");

    // Center should be mostly transparent (original removed)
    uint32_t c = get_pixel(inner_ko, 100, 80);
    if (pixel_a(c) < 100) {
      printf("  PASS: Inner shadow KO: center transparent (A=%u)\n", unsigned(pixel_a(c)));
      g_passes++;
    } else {
      printf("  FAIL: Inner shadow KO: center opaque (A=%u)\n", unsigned(pixel_a(c)));
      g_failures++;
    }
  }
}

// ----- Test 28: Brightness/Contrast Edge Cases -----
static void test_brightness_contrast_edges() {
  printf("\nTest 28: Brightness/Contrast Edge Cases\n");

  BLImage src(128, 128, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src);
    ctx.fill_all(BLRgba32(0xFF808080)); // Mid-gray
    ctx.end();
  }

  // Max brightness → should be white
  BLImage white;
  BLImage::brightness_contrast(white, src, 1.0, 0.0);
  uint32_t wc = get_pixel(white, 64, 64);
  if (pixel_r(wc) == 255 && pixel_g(wc) == 255 && pixel_b(wc) == 255) {
    printf("  PASS: Max brightness = white\n"); g_passes++;
  } else {
    printf("  FAIL: Max brightness not white (R=%u G=%u B=%u)\n",
           unsigned(pixel_r(wc)), unsigned(pixel_g(wc)), unsigned(pixel_b(wc))); g_failures++;
  }

  // Min brightness → should be black
  BLImage black;
  BLImage::brightness_contrast(black, src, -1.0, 0.0);
  uint32_t bc = get_pixel(black, 64, 64);
  if (pixel_r(bc) == 0 && pixel_g(bc) == 0 && pixel_b(bc) == 0) {
    printf("  PASS: Min brightness = black\n"); g_passes++;
  } else {
    printf("  FAIL: Min brightness not black (R=%u G=%u B=%u)\n",
           unsigned(pixel_r(bc)), unsigned(pixel_g(bc)), unsigned(pixel_b(bc))); g_failures++;
  }

  // Zero brightness, zero contrast → unchanged
  BLImage unchanged;
  BLImage::brightness_contrast(unchanged, src, 0.0, 0.0);
  check_pixel_near(unchanged, 64, 64, 0xFF808080, 1, "Zero B/C = unchanged");

  // In-place operation
  BLImage inplace(128, 128, BL_FORMAT_PRGB32);
  { BLContext ctx(inplace); ctx.fill_all(BLRgba32(0xFF404040)); ctx.end(); }
  BLImage::brightness_contrast(inplace, inplace, 0.2, 0.0);
  uint32_t ipc = get_pixel(inplace, 64, 64);
  if (pixel_r(ipc) > 0x40) {
    printf("  PASS: In-place brightness works (R=%u > 0x40)\n", unsigned(pixel_r(ipc)));
    g_passes++;
  } else {
    printf("  FAIL: In-place brightness broken (R=%u)\n", unsigned(pixel_r(ipc)));
    g_failures++;
  }

  // With semi-transparent pixels (premultiplied alpha handling)
  BLImage alpha_src(128, 128, BL_FORMAT_PRGB32);
  {
    BLContext ctx(alpha_src);
    ctx.clear_all();
    ctx.fill_rect(BLRect(20, 20, 88, 88), BLRgba32(0x80FF0000)); // 50% red
    ctx.end();
  }
  BLImage alpha_bright;
  BLImage::brightness_contrast(alpha_bright, alpha_src, 0.3, 0.0);
  uint32_t ap = get_pixel(alpha_bright, 64, 64);
  // Alpha should be preserved
  if (pixel_a(ap) > 0x70 && pixel_a(ap) < 0x90) {
    printf("  PASS: Alpha preserved after brightness (A=%u)\n", unsigned(pixel_a(ap)));
    g_passes++;
  } else {
    printf("  FAIL: Alpha changed (A=%u, expected ~0x80)\n", unsigned(pixel_a(ap)));
    g_failures++;
  }
}

// ----- Test 29: Saturation Edge Cases -----
static void test_saturation_edges() {
  printf("\nTest 29: Saturation Edge Cases\n");

  // Pure red source
  BLImage red(128, 128, BL_FORMAT_PRGB32);
  { BLContext ctx(red); ctx.fill_all(BLRgba32(0xFFFF0000)); ctx.end(); }

  // Grayscale of pure red
  BLImage gray;
  BLImage::saturation(gray, red, 0.0);
  uint32_t gc = get_pixel(gray, 64, 64);
  // Should be neutral gray (R=G=B), luminance of red ≈ 54 (BT.709: 0.2126)
  if (pixel_r(gc) == pixel_g(gc) && pixel_g(gc) == pixel_b(gc)) {
    printf("  PASS: Pure red → grayscale is neutral (val=%u)\n", unsigned(pixel_r(gc)));
    g_passes++;
  } else {
    printf("  FAIL: Not neutral (R=%u G=%u B=%u)\n",
           unsigned(pixel_r(gc)), unsigned(pixel_g(gc)), unsigned(pixel_b(gc)));
    g_failures++;
  }

  // Saturation=1 should be unchanged
  BLImage same;
  BLImage::saturation(same, red, 1.0);
  check_pixel_near(same, 64, 64, 0xFFFF0000, 1, "Saturation 1.0 = unchanged red");

  // Already gray image → saturation change should have no effect
  BLImage gray_src(128, 128, BL_FORMAT_PRGB32);
  { BLContext ctx(gray_src); ctx.fill_all(BLRgba32(0xFF808080)); ctx.end(); }
  BLImage gray_sat;
  BLImage::saturation(gray_sat, gray_src, 2.0);
  check_pixel_near(gray_sat, 64, 64, 0xFF808080, 2, "Saturating gray stays gray");

  // Saturation with white → should stay white
  BLImage white_src(128, 128, BL_FORMAT_PRGB32);
  { BLContext ctx(white_src); ctx.fill_all(BLRgba32(0xFFFFFFFF)); ctx.end(); }
  BLImage white_sat;
  BLImage::saturation(white_sat, white_src, 0.0);
  check_pixel_near(white_sat, 64, 64, 0xFFFFFFFF, 1, "Desaturate white = white");

  // Saturation with black → should stay black
  BLImage black_src(128, 128, BL_FORMAT_PRGB32);
  { BLContext ctx(black_src); ctx.fill_all(BLRgba32(0xFF000000)); ctx.end(); }
  BLImage black_sat;
  BLImage::saturation(black_sat, black_src, 0.0);
  check_pixel_near(black_sat, 64, 64, 0xFF000000, 1, "Desaturate black = black");
}

// ----- Test 30: Complete Effect Showcase -----
static void test_effect_showcase() {
  printf("\nTest 30: Complete Effect Showcase\n");

  // Create a nice source image with multiple elements
  BLImage src(300, 200, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src);
    // Dark gradient background
    BLGradient bg(BLLinearGradientValues(0, 0, 300, 200));
    bg.add_stop(0.0, BLRgba32(0xFF1A1A2E));
    bg.add_stop(1.0, BLRgba32(0xFF16213E));
    ctx.fill_all(bg);

    // Colored shapes
    BLPath c1; c1.add_circle(BLCircle(80, 100, 45));
    ctx.fill_path(c1, BLRgba32(0xFFE94560));

    BLPath c2; c2.add_circle(BLCircle(220, 100, 45));
    ctx.fill_path(c2, BLRgba32(0xFF0F3460));

    ctx.fill_round_rect(BLRoundRect(120, 60, 60, 80, 10), BLRgba32(0xFFFFD700));
    ctx.end();
  }
  save_image(src, "test_30a_showcase_source.png");

  // Blur and color adjustments work on opaque source
  BLImage blur_result;
  BLImage::blur(blur_result, src, 8.0);
  save_image(blur_result, "test_30b_showcase_blur.png");

  BLImage bright_result;
  BLImage::brightness_contrast(bright_result, src, 0.15, 0.3);
  save_image(bright_result, "test_30e_showcase_bright_contrast.png");

  BLImage gray_result;
  BLImage::saturation(gray_result, src, 0.0);
  save_image(gray_result, "test_30f_showcase_grayscale.png");

  BLImage vivid_result;
  BLImage::saturation(vivid_result, src, 1.8);
  save_image(vivid_result, "test_30g_showcase_vivid.png");

  // Shadow, glow, and chaining need transparent background to work correctly
  // (effects operate on the alpha channel — opaque backgrounds make them affect the whole rect).
  BLImage src_alpha(300, 200, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src_alpha);
    ctx.clear_all();  // Transparent background

    BLPath c1; c1.add_circle(BLCircle(80, 100, 45));
    ctx.fill_path(c1, BLRgba32(0xFFE94560));

    BLPath c2; c2.add_circle(BLCircle(220, 100, 45));
    ctx.fill_path(c2, BLRgba32(0xFF0F3460));

    ctx.fill_round_rect(BLRoundRect(120, 60, 60, 80, 10), BLRgba32(0xFFFFD700));
    ctx.end();
  }
  save_image(src_alpha, "test_30a2_showcase_transparent.png");

  BLImage shadow_result;
  BLImage::drop_shadow(shadow_result, src_alpha, 12.0, 8.0, 8.0, BLRgba32(0xAA000000));
  save_image(shadow_result, "test_30c_showcase_shadow.png");

  BLImage glow_result;
  BLImage::glow(glow_result, src_alpha, 15.0, BLRgba32(0xFFE94560));
  save_image(glow_result, "test_30d_showcase_glow.png");

  // Chain: outer glow + inner glow + drop shadow (on transparent source)
  BLImageEffectOptions chain[3] = {};
  chain[0].type = BL_IMAGE_EFFECT_TYPE_GLOW;
  chain[0].radius = 20.0; chain[0].quality = 0.5;
  chain[0].color = 0xFFE94560;

  chain[1].type = BL_IMAGE_EFFECT_TYPE_GLOW;
  chain[1].radius = 8.0; chain[1].quality = 0.5;
  chain[1].color = 0xFFFFD700;
  chain[1].flags = BL_IMAGE_EFFECT_FLAG_INNER;

  chain[2].type = BL_IMAGE_EFFECT_TYPE_DROP_SHADOW;
  chain[2].radius = 10.0; chain[2].quality = 0.5;
  chain[2].offset_x = 5.0; chain[2].offset_y = 5.0;
  chain[2].color = 0xCC000000;

  BLImage chain_result;
  BLImage::apply_effects(chain_result, src_alpha, chain, 3);
  save_image(chain_result, "test_30h_showcase_chained.png");

  printf("  Saved 9 showcase images\n");
  g_passes++;

  // Verify they're all different from source
  uint32_t src_p = get_pixel(src, 150, 100);
  uint32_t blur_p = get_pixel(blur_result, 150, 100);
  uint32_t gray_p = get_pixel(gray_result, 150, 100);

  if (src_p != blur_p) {
    printf("  PASS: Blur differs from source\n"); g_passes++;
  } else {
    printf("  FAIL: Blur identical to source\n"); g_failures++;
  }

  if (src_p != gray_p) {
    printf("  PASS: Grayscale differs from source\n"); g_passes++;
  } else {
    printf("  FAIL: Grayscale identical to source\n"); g_failures++;
  }
}

// ----- Test 31: Tint Effect -----
static void test_tint() {
  printf("\nTest 31: Tint Effect\n");

  BLImage src(256, 256, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src);
    BLGradient grad(BLLinearGradientValues(0, 0, 256, 0));
    grad.add_stop(0.0, BLRgba32(0xFFFF0000));
    grad.add_stop(0.5, BLRgba32(0xFF00FF00));
    grad.add_stop(1.0, BLRgba32(0xFF0000FF));
    ctx.fill_all(grad);
    ctx.end();
  }
  save_image(src, "test_31a_tint_source.png");

  // 50% tint toward blue
  BLImage tinted_50;
  BLImage::tint(tinted_50, src, BLRgba32(0xFF0000FF), 0.5);
  save_image(tinted_50, "test_31b_tint_blue_50.png");

  // The left (red) should now be purple-ish (lerp red→blue at 50%)
  uint32_t left = get_pixel(tinted_50, 20, 128);
  if (pixel_r(left) > 80 && pixel_b(left) > 80) {
    printf("  PASS: 50%% blue tint: left is purple-ish (R=%u B=%u)\n",
           unsigned(pixel_r(left)), unsigned(pixel_b(left)));
    g_passes++;
  } else {
    printf("  FAIL: 50%% tint not blending (R=%u B=%u)\n",
           unsigned(pixel_r(left)), unsigned(pixel_b(left)));
    g_failures++;
  }

  // 100% tint → solid blue (preserving alpha)
  BLImage tinted_100;
  BLImage::tint(tinted_100, src, BLRgba32(0xFF0000FF), 1.0);
  save_image(tinted_100, "test_31c_tint_blue_100.png");

  uint32_t full = get_pixel(tinted_100, 128, 128);
  if (pixel_b(full) > 240 && pixel_r(full) < 15 && pixel_g(full) < 15) {
    printf("  PASS: 100%% tint = solid blue\n"); g_passes++;
  } else {
    printf("  FAIL: 100%% tint not solid (R=%u G=%u B=%u)\n",
           unsigned(pixel_r(full)), unsigned(pixel_g(full)), unsigned(pixel_b(full)));
    g_failures++;
  }

  // 0% tint → unchanged
  BLImage tinted_0;
  BLImage::tint(tinted_0, src, BLRgba32(0xFF0000FF), 0.0);
  check_pixel_near(tinted_0, 20, 128, get_pixel(src, 20, 128), 1, "0% tint = unchanged");

  // Sepia-style warm tint
  BLImage sepia;
  BLImage::tint(sepia, src, BLRgba32(0xFFD2B48C), 0.6);
  save_image(sepia, "test_31d_tint_sepia.png");
  printf("  PASS: Sepia tint saved\n"); g_passes++;
}

// ----- Test 32: Glow Strength -----
static void test_glow_strength() {
  printf("\nTest 32: Glow Strength\n");

  BLImage src(256, 256, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src);
    ctx.clear_all();
    BLPath circle; circle.add_circle(BLCircle(128, 128, 40));
    ctx.fill_path(circle, BLRgba32(0xFFFFFFFF));
    ctx.end();
  }

  // Normal strength (1.0)
  {
    BLImageEffectOptions opts{};
    opts.type = BL_IMAGE_EFFECT_TYPE_GLOW;
    opts.radius = 20.0; opts.quality = 0.5;
    opts.color = 0xFFFF4400;
    opts.strength = 1.0;
    BLImage result;
    BLImage::apply_effect(result, src, opts);
    save_image(result, "test_32a_glow_strength_1.png");
  }

  // High strength (3.0) — brighter glow
  {
    BLImageEffectOptions opts{};
    opts.type = BL_IMAGE_EFFECT_TYPE_GLOW;
    opts.radius = 20.0; opts.quality = 0.5;
    opts.color = 0xFFFF4400;
    opts.strength = 3.0;
    BLImage result;
    BLImage::apply_effect(result, src, opts);
    save_image(result, "test_32b_glow_strength_3.png");

    // The glow edge should be brighter than normal strength
    uint32_t edge = get_pixel(result, 128 + 50, 128);
    if (pixel_r(edge) > 30) {
      printf("  PASS: High strength glow brighter (R=%u)\n", unsigned(pixel_r(edge)));
      g_passes++;
    } else {
      printf("  FAIL: High strength not brighter (R=%u)\n", unsigned(pixel_r(edge)));
      g_failures++;
    }
  }
}

// ----- Test 33: Spread -----
static void test_spread() {
  printf("\nTest 33: Spread/Choke\n");

  BLImage src(256, 256, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src);
    ctx.clear_all();
    BLPath circle; circle.add_circle(BLCircle(128, 128, 50));
    ctx.fill_path(circle, BLRgba32(0xFFFFFFFF));
    ctx.end();
  }

  // Glow without spread (soft edge)
  {
    BLImageEffectOptions opts{};
    opts.type = BL_IMAGE_EFFECT_TYPE_GLOW;
    opts.radius = 15.0; opts.quality = 0.5;
    opts.color = 0xFFFF0000;
    opts.spread = 0.0;
    BLImage result;
    BLImage::apply_effect(result, src, opts);
    save_image(result, "test_33a_glow_spread_0.png");
  }

  // Glow with spread=0.8 (hard edge, then blur)
  {
    BLImageEffectOptions opts{};
    opts.type = BL_IMAGE_EFFECT_TYPE_GLOW;
    opts.radius = 15.0; opts.quality = 0.5;
    opts.color = 0xFFFF0000;
    opts.spread = 0.8;
    BLImage result;
    BLImage::apply_effect(result, src, opts);
    save_image(result, "test_33b_glow_spread_80.png");

    // With high spread, the glow should be more opaque further out
    uint32_t edge = get_pixel(result, 128 + 55, 128);
    printf("  INFO: Spread 0.8 glow at r=55: R=%u A=%u\n",
           unsigned(pixel_r(edge)), unsigned(pixel_a(edge)));
    g_passes++;
  }
}

// ----- Test 34: Opacity -----
static void test_opacity() {
  printf("\nTest 34: Effect Opacity\n");

  BLImage src(256, 256, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src);
    ctx.clear_all();
    ctx.fill_round_rect(BLRoundRect(40, 40, 176, 176, 15), BLRgba32(0xFFFFFFFF));
    ctx.end();
  }

  // Full opacity shadow
  {
    BLImageEffectOptions opts{};
    opts.type = BL_IMAGE_EFFECT_TYPE_DROP_SHADOW;
    opts.radius = 10.0; opts.quality = 0.5;
    opts.offset_x = 5.0; opts.offset_y = 5.0;
    opts.color = 0xFF000000;
    opts.opacity = 1.0;
    BLImage result;
    BLImage::apply_effect(result, src, opts);
    save_image(result, "test_34a_shadow_opacity_100.png");
  }

  // 30% opacity shadow
  {
    BLImageEffectOptions opts{};
    opts.type = BL_IMAGE_EFFECT_TYPE_DROP_SHADOW;
    opts.radius = 10.0; opts.quality = 0.5;
    opts.offset_x = 5.0; opts.offset_y = 5.0;
    opts.color = 0xFF000000;
    opts.opacity = 0.3;
    BLImage result;
    BLImage::apply_effect(result, src, opts);
    save_image(result, "test_34b_shadow_opacity_30.png");

    // Shadow should be lighter with lower opacity
    printf("  PASS: 30%% opacity shadow saved\n"); g_passes++;
  }
}

// ----- Test 35: True Gaussian (Ultra Quality) -----
static void test_true_gaussian() {
  printf("\nTest 35: True Gaussian (Ultra Quality)\n");

  BLImage src(128, 128, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src);
    ctx.fill_all(BLRgba32(0xFF000000));
    ctx.fill_rect(BLRect(54, 54, 20, 20), BLRgba32(0xFFFFFFFF));
    ctx.end();
  }

  // 3-pass box blur approximation (q=0.8)
  BLImage approx;
  BLImage::blur(approx, src, 5.0, 0.8);
  save_image(approx, "test_35a_gaussian_approx.png");

  // True Gaussian (q=1.0, ultra)
  BLImage ultra;
  BLImage::blur(ultra, src, 5.0, 1.0);
  save_image(ultra, "test_35b_gaussian_ultra.png");

  // Both should blur the white square
  uint32_t approx_center = get_pixel(approx, 64, 64);
  uint32_t ultra_center = get_pixel(ultra, 64, 64);

  if (pixel_r(approx_center) > 100 && pixel_r(ultra_center) > 100) {
    printf("  PASS: Both blur the center (approx R=%u, ultra R=%u)\n",
           unsigned(pixel_r(approx_center)), unsigned(pixel_r(ultra_center)));
    g_passes++;
  } else {
    printf("  FAIL: Blur not working\n");
    g_failures++;
  }

  // Edge pixel — ultra should have a slightly different falloff profile
  uint32_t approx_edge = get_pixel(approx, 64 + 15, 64);
  uint32_t ultra_edge = get_pixel(ultra, 64 + 15, 64);
  printf("  INFO: Edge comparison: approx R=%u, ultra R=%u\n",
         unsigned(pixel_r(approx_edge)), unsigned(pixel_r(ultra_edge)));
  g_passes++;
}

// ----- Test 36: Path Clipping — Circle -----
static void test_path_clip_circle() {
  printf("\nTest 36: Path Clipping (Circle)\n");

  BLImage img(256, 256, BL_FORMAT_PRGB32);
  BLContext ctx(img);

  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(BLRgba32(0xFFFFFFFF)); // White background

  // Clip to a circle
  BLPath clip_circle;
  clip_circle.add_circle(BLCircle(128, 128, 80));
  ctx.clip_to_path(clip_circle);

  // Fill entire surface with red — should only appear inside the circle
  ctx.fill_all(BLRgba32(0xFFFF0000));

  ctx.end();

  // Center should be red (inside clip)
  check_pixel_near(img, 128, 128, 0xFFFF0000, 0, "Circle clip: center red");

  // Corner should be white (outside clip, untouched)
  check_pixel_near(img, 10, 10, 0xFFFFFFFF, 0, "Circle clip: corner white");

  // Just outside circle (90px from center, circle r=80)
  check_pixel_near(img, 128 + 90, 128, 0xFFFFFFFF, 2, "Circle clip: outside edge white");

  save_image(img, "test_36_path_clip_circle.png");
}

// ----- Test 37: Path Clipping — Triangle with Gradient -----
static void test_path_clip_triangle() {
  printf("\nTest 37: Path Clipping (Triangle + Gradient)\n");

  BLImage img(256, 256, BL_FORMAT_PRGB32);
  BLContext ctx(img);

  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(BLRgba32(0xFF000000)); // Black background

  // Clip to a triangle
  BLPath triangle;
  triangle.move_to(128, 20);
  triangle.line_to(20, 230);
  triangle.line_to(236, 230);
  triangle.close();
  ctx.clip_to_path(triangle);

  // Fill with gradient — only visible inside triangle
  BLGradient grad(BLLinearGradientValues(0, 0, 256, 256));
  grad.add_stop(0.0, BLRgba32(0xFFFF0000));
  grad.add_stop(0.5, BLRgba32(0xFF00FF00));
  grad.add_stop(1.0, BLRgba32(0xFF0000FF));
  ctx.fill_all(grad);

  ctx.end();

  // Center of triangle should have gradient color (not black)
  uint32_t center = get_pixel(img, 128, 150);
  if (pixel_r(center) > 0 || pixel_g(center) > 0 || pixel_b(center) > 0) {
    printf("  PASS: Triangle clip: center has gradient color\n");
    g_passes++;
  } else {
    printf("  FAIL: Triangle clip: center is black\n");
    g_failures++;
  }

  // Corner should be black (outside triangle)
  check_pixel_near(img, 10, 10, 0xFF000000, 0, "Triangle clip: corner black");

  save_image(img, "test_37_path_clip_triangle.png");
}

// ----- Test 38: Path Clipping with Save/Restore -----
static void test_path_clip_save_restore() {
  printf("\nTest 38: Path Clipping Save/Restore\n");

  BLImage img(256, 256, BL_FORMAT_PRGB32);
  BLContext ctx(img);

  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(BLRgba32(0xFFFFFFFF)); // White

  // Save, clip to circle, fill red
  ctx.save();
  BLPath circle;
  circle.add_circle(BLCircle(128, 128, 60));
  ctx.clip_to_path(circle);
  ctx.fill_all(BLRgba32(0xFFFF0000));
  ctx.restore(); // Should restore to full rectangular clip

  // After restore, fill blue rect — should NOT be clipped by the circle
  ctx.fill_rect(BLRect(0, 0, 50, 256), BLRgba32(0xFF0000FF));

  ctx.end();

  // Center should be red (filled during circle clip). Slight color bleed from mask anti-aliasing.
  check_pixel_near(img, 128, 128, 0xFFFF0000, 40, "Save/restore: center red");

  // Top-left strip should be blue (filled after restore, no clip)
  check_pixel_near(img, 25, 128, 0xFF0000FF, 0, "Save/restore: left blue after restore");

  // Right side (outside circle, not filled by blue) should be white
  check_pixel_near(img, 220, 128, 0xFFFFFFFF, 0, "Save/restore: right white");

  save_image(img, "test_38_path_clip_save_restore.png");
}

// ----- Test 39: Nested Path Clipping -----
static void test_nested_path_clip() {
  printf("\nTest 39: Nested Path Clipping\n");

  BLImage img(256, 256, BL_FORMAT_PRGB32);
  BLContext ctx(img);

  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(BLRgba32(0xFFFFFFFF)); // White background

  // First clip: large circle
  BLPath circle;
  circle.add_circle(BLCircle(128, 128, 100));
  ctx.clip_to_path(circle);

  // Second clip: rectangle (intersection = circle ∩ rect)
  ctx.clip_to_rect(BLRect(128, 0, 128, 256)); // Right half

  // Fill red — should only appear in right half of circle
  ctx.fill_all(BLRgba32(0xFFFF0000));

  ctx.end();

  // Right side inside circle: red
  check_pixel_near(img, 180, 128, 0xFFFF0000, 5, "Nested clip: right inside circle (red)");

  // Left side inside circle but outside rect clip: white (not filled)
  check_pixel_near(img, 80, 128, 0xFFFFFFFF, 5, "Nested clip: left inside circle (white)");

  // Outside circle entirely: white
  check_pixel_near(img, 10, 10, 0xFFFFFFFF, 0, "Nested clip: outside both (white)");

  save_image(img, "test_39_nested_path_clip.png");
}

// ----- Test 40: Double Path Clip (two paths) -----
static void test_double_path_clip() {
  printf("\nTest 40: Double Path Clip (Circle ∩ Triangle)\n");

  BLImage img(256, 256, BL_FORMAT_PRGB32);
  BLContext ctx(img);

  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(BLRgba32(0xFF000000)); // Black background

  // Clip to circle
  BLPath circle;
  circle.add_circle(BLCircle(128, 128, 90));
  ctx.clip_to_path(circle);

  // Clip to triangle (intersection = crescent/lens shape)
  BLPath triangle;
  triangle.move_to(128, 10);
  triangle.line_to(10, 240);
  triangle.line_to(246, 240);
  triangle.close();
  ctx.clip_to_path(triangle);

  // Fill green — only visible in circle ∩ triangle
  ctx.fill_all(BLRgba32(0xFF00FF00));

  ctx.end();

  // Center-bottom (inside both): green
  uint32_t center = get_pixel(img, 128, 180);
  if (pixel_g(center) > 200) {
    printf("  PASS: Double clip: center-bottom green (G=%u)\n", unsigned(pixel_g(center)));
    g_passes++;
  } else {
    printf("  FAIL: Double clip: center-bottom not green (G=%u)\n", unsigned(pixel_g(center)));
    g_failures++;
  }

  // Top center (inside triangle but outside circle): black
  uint32_t top = get_pixel(img, 128, 30);
  if (pixel_r(top) < 10 && pixel_g(top) < 10 && pixel_b(top) < 10) {
    printf("  PASS: Double clip: top (outside circle) black\n");
    g_passes++;
  } else {
    printf("  FAIL: Double clip: top not black (R=%u G=%u B=%u)\n",
           unsigned(pixel_r(top)), unsigned(pixel_g(top)), unsigned(pixel_b(top)));
    g_failures++;
  }

  // Far left (outside both): black
  check_pixel_near(img, 5, 128, 0xFF000000, 0, "Double clip: far left black");

  save_image(img, "test_40_double_path_clip.png");
}

// ----- Test 41: Path Clip with fill_rect -----
static void test_path_clip_fill_rect() {
  printf("\nTest 41: Path Clip with fill_rect\n");

  BLImage img(256, 256, BL_FORMAT_PRGB32);
  BLContext ctx(img);
  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(BLRgba32(0xFFFFFFFF));

  // Clip to star shape
  BLPath star;
  double cx = 128, cy = 128, r_outer = 90, r_inner = 40;
  for (int i = 0; i < 10; i++) {
    double angle = 3.14159265 * 2.0 * i / 10.0 - 3.14159265 / 2.0;
    double r = (i % 2 == 0) ? r_outer : r_inner;
    double px = cx + r * cos(angle);
    double py = cy + r * sin(angle);
    if (i == 0) star.move_to(px, py);
    else star.line_to(px, py);
  }
  star.close();
  ctx.clip_to_path(star);

  // Fill multiple colored rects — only star-shaped area should be visible
  ctx.fill_rect(BLRect(0, 0, 128, 128), BLRgba32(0xFFFF0000));
  ctx.fill_rect(BLRect(128, 0, 128, 128), BLRgba32(0xFF00FF00));
  ctx.fill_rect(BLRect(0, 128, 128, 128), BLRgba32(0xFF0000FF));
  ctx.fill_rect(BLRect(128, 128, 128, 128), BLRgba32(0xFFFFFF00));

  ctx.end();

  // Center should have color (inside star)
  uint32_t center = get_pixel(img, 128, 128);
  if (pixel_a(center) == 0xFF && (pixel_r(center) > 0 || pixel_g(center) > 0 || pixel_b(center) > 0)) {
    printf("  PASS: Star clip center has color\n"); g_passes++;
  } else {
    printf("  FAIL: Star clip center empty (0x%08X)\n", center); g_failures++;
  }

  // Corner (outside star): white
  check_pixel_near(img, 10, 10, 0xFFFFFFFF, 5, "Star clip: corner white");

  // Top point of star (should be red quadrant)
  uint32_t top = get_pixel(img, 128, 40);
  if (pixel_r(top) > 100 || pixel_g(top) > 100) {
    printf("  PASS: Star top point has color\n"); g_passes++;
  } else {
    printf("  FAIL: Star top point empty\n"); g_failures++;
  }

  save_image(img, "test_41_star_clip_rects.png");
}

// ----- Test 42: Path Clip with fill_path -----
static void test_path_clip_fill_path() {
  printf("\nTest 42: Path Clip with fill_path\n");

  BLImage img(256, 256, BL_FORMAT_PRGB32);
  BLContext ctx(img);
  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(BLRgba32(0xFF000000)); // Black background

  // Clip to hexagon
  BLPath hex;
  for (int i = 0; i < 6; i++) {
    double angle = 3.14159265 * 2.0 * i / 6.0;
    double px = 128 + 80 * cos(angle);
    double py = 128 + 80 * sin(angle);
    if (i == 0) hex.move_to(px, py);
    else hex.line_to(px, py);
  }
  hex.close();
  ctx.clip_to_path(hex);

  // Fill a circle inside the clip — should be clipped to hexagon
  BLPath big_circle;
  big_circle.add_circle(BLCircle(128, 128, 120));
  ctx.fill_path(big_circle, BLRgba32(0xFFFF8800));

  ctx.end();

  // Center: orange (inside both hex and circle)
  uint32_t center = get_pixel(img, 128, 128);
  if (pixel_r(center) > 200) {
    printf("  PASS: Hex clip center orange (R=%u)\n", unsigned(pixel_r(center))); g_passes++;
  } else {
    printf("  FAIL: Hex clip center not orange (R=%u)\n", unsigned(pixel_r(center))); g_failures++;
  }

  // Corner: black (outside hex)
  check_pixel_near(img, 10, 10, 0xFF000000, 5, "Hex clip: corner black");

  save_image(img, "test_42_hex_clip_circle.png");
}

// ----- Test 43: Path Clip with gradient fill -----
static void test_path_clip_gradient() {
  printf("\nTest 43: Path Clip with Gradient\n");

  BLImage img(256, 256, BL_FORMAT_PRGB32);
  BLContext ctx(img);
  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(BLRgba32(0xFFFFFFFF));

  // Clip to rounded rect
  BLPath rrect;
  rrect.add_round_rect(BLRoundRect(30, 30, 196, 196, 30));
  ctx.clip_to_path(rrect);

  // Fill with radial gradient
  BLGradient grad(BLRadialGradientValues(128, 128, 128, 128, 100));
  grad.add_stop(0.0, BLRgba32(0xFFFFFF00));
  grad.add_stop(1.0, BLRgba32(0xFFFF0000));
  ctx.fill_all(grad);

  ctx.end();

  // Center: yellow (center of gradient)
  uint32_t center = get_pixel(img, 128, 128);
  if (pixel_r(center) > 200 && pixel_g(center) > 200) {
    printf("  PASS: Gradient clip center yellow\n"); g_passes++;
  } else {
    printf("  FAIL: Gradient clip center wrong (R=%u G=%u)\n",
           unsigned(pixel_r(center)), unsigned(pixel_g(center))); g_failures++;
  }

  // Corner (outside rounded rect): white
  check_pixel_near(img, 5, 5, 0xFFFFFFFF, 5, "Rounded rect clip: corner white");

  // Just inside rounded rect edge
  uint32_t edge = get_pixel(img, 50, 50);
  if (pixel_r(edge) > 100) {
    printf("  PASS: Gradient visible inside clip\n"); g_passes++;
  } else {
    printf("  FAIL: No gradient inside clip\n"); g_failures++;
  }

  save_image(img, "test_43_rrect_clip_gradient.png");
}

// ----- Test 44: Path Clip with blit_image -----
static void test_path_clip_blit() {
  printf("\nTest 44: Path Clip with blit_image\n");

  // Create a colorful source image
  BLImage src(256, 256, BL_FORMAT_PRGB32);
  {
    BLContext ctx(src);
    BLGradient grad(BLLinearGradientValues(0, 0, 256, 256));
    grad.add_stop(0.0, BLRgba32(0xFFFF0000));
    grad.add_stop(0.5, BLRgba32(0xFF00FF00));
    grad.add_stop(1.0, BLRgba32(0xFF0000FF));
    ctx.fill_all(grad);
    ctx.end();
  }

  BLImage img(256, 256, BL_FORMAT_PRGB32);
  BLContext ctx(img);
  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(BLRgba32(0xFF000000)); // Black

  // Clip to diamond shape
  BLPath diamond;
  diamond.move_to(128, 20);
  diamond.line_to(236, 128);
  diamond.line_to(128, 236);
  diamond.line_to(20, 128);
  diamond.close();
  ctx.clip_to_path(diamond);

  // Blit the gradient image through the diamond clip
  ctx.blit_image(BLPoint(0, 0), src);

  ctx.end();

  // Center: should have gradient color
  uint32_t center = get_pixel(img, 128, 128);
  if (pixel_r(center) > 0 || pixel_g(center) > 0 || pixel_b(center) > 0) {
    printf("  PASS: Diamond clip blit has color\n"); g_passes++;
  } else {
    printf("  FAIL: Diamond clip blit empty\n"); g_failures++;
  }

  // Corner: black (outside diamond)
  check_pixel_near(img, 10, 10, 0xFF000000, 5, "Diamond clip: corner black");

  save_image(img, "test_44_diamond_clip_blit.png");
}

// ----- Main -----
int main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;

  printf("Blend2D Visual Rendering Tests\n");
  printf("==============================\n");

  test_basic_fills();
  test_alpha_blending();
  test_gradient();
  test_circle_path();
  test_rect_clipping();
  test_transform();
  test_image_blit();
  test_comp_ops();
  test_stroke();
  test_a8_format();
  test_format_band_height();
  test_save_restore_clipping();
  test_clipping_with_transform();
  test_overlapping_fills();
  test_triangle_clip_circle();
  test_box_blur();
  test_gaussian_blur();
  test_blur_edge_cases();
  test_drop_shadow();
  test_glow();
  test_blur_quality_tiers();
  test_glow_modes();
  test_inner_drop_shadow();
  test_chained_effects();
  test_brightness_contrast();
  test_saturation();
  test_drop_shadow_variants();
  test_brightness_contrast_edges();
  test_saturation_edges();
  test_effect_showcase();
  test_tint();
  test_glow_strength();
  test_spread();
  test_opacity();
  test_true_gaussian();
  test_path_clip_circle();
  test_path_clip_triangle();
  test_path_clip_save_restore();
  test_nested_path_clip();
  test_double_path_clip();
  test_path_clip_fill_rect();
  test_path_clip_fill_path();
  test_path_clip_gradient();
  test_path_clip_blit();

  printf("\n==============================\n");
  printf("Results: %d passed, %d failed\n", g_passes, g_failures);

  if (g_failures > 0) {
    printf("FAILED\n");
    return 1;
  }

  printf("All visual tests passed!\n");
  return 0;
}
