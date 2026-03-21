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

static bool g_save = false;
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
  if (g_save) {
    BLResult result = img.write_to_file(filename);
    if (result == BL_SUCCESS) {
      printf("  Saved: %s\n", filename);
    } else {
      printf("  WARNING: Failed to save %s (error=%u)\n", filename, unsigned(result));
    }
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

// ----- Main -----
int main(int argc, char* argv[]) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--save") == 0) {
      g_save = true;
    }
    if (strcmp(argv[i], "--help") == 0) {
      printf("Usage: bl_test_visual [--save]\n");
      printf("  --save  Write test PNG files to disk for visual inspection\n");
      return 0;
    }
  }

  printf("Blend2D Visual Rendering Tests\n");
  printf("==============================\n");
  if (g_save)
    printf("Saving PNG files enabled.\n");

  test_basic_fills();
  test_alpha_blending();
  test_gradient();
  test_circle_path();
  test_rect_clipping();
  test_transform();
  test_image_blit();
  test_comp_ops();
  test_stroke();

  printf("\n==============================\n");
  printf("Results: %d passed, %d failed\n", g_passes, g_failures);

  if (g_failures > 0) {
    printf("FAILED\n");
    return 1;
  }

  printf("All visual tests passed!\n");
  return 0;
}
