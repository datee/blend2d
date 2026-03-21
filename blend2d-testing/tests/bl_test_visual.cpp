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

  printf("\n==============================\n");
  printf("Results: %d passed, %d failed\n", g_passes, g_failures);

  if (g_failures > 0) {
    printf("FAILED\n");
    return 1;
  }

  printf("All visual tests passed!\n");
  return 0;
}
