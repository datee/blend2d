#include <blend2d/blend2d.h>
#include <stdio.h>
#include <chrono>

int main() {
  // Create a 1920x1080 HD image with a complex scene
  BLImage img(1920, 1080, BL_FORMAT_PRGB32);
  {
    BLContext ctx(img);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);

    // Gradient background
    BLGradient grad(BLLinearGradientValues(0, 0, 1920, 1080));
    grad.add_stop(0.0, BLRgba32(0xFF1A2B3C));
    grad.add_stop(0.5, BLRgba32(0xFFFF6644));
    grad.add_stop(1.0, BLRgba32(0xFF2244AA));
    ctx.fill_all(grad);

    // Scatter some shapes
    ctx.set_comp_op(BL_COMP_OP_SRC_OVER);
    for (int i = 0; i < 50; i++) {
      BLPath circle;
      double x = (i * 137) % 1920;
      double y = (i * 251) % 1080;
      double r = 20 + (i * 17) % 80;
      circle.add_circle(BLCircle(x, y, r));
      uint32_t color = 0x80000000 | ((i * 0x112233) & 0x00FFFFFF);
      ctx.fill_path(circle, BLRgba32(color));
    }
    ctx.end();
  }

  img.write_to_file("bench_source_1080p.png");
  printf("Source: 1920x1080 PRGB32\n\n");

  // Benchmark various radii
  struct TestCase { const char* name; BLImageFilterType type; double radius; double quality; };
  TestCase cases[] = {
    {"Box blur r=5",            BL_IMAGE_FILTER_TYPE_BOX_BLUR,      5.0,  0.5},
    {"Box blur r=50",           BL_IMAGE_FILTER_TYPE_BOX_BLUR,      50.0, 0.5},
    {"Gauss r=50 q=0.0",       BL_IMAGE_FILTER_TYPE_GAUSSIAN_BLUR, 50.0, 0.0},
    {"Gauss r=50 q=0.5",       BL_IMAGE_FILTER_TYPE_GAUSSIAN_BLUR, 50.0, 0.5},
    {"Gauss r=50 q=1.0",       BL_IMAGE_FILTER_TYPE_GAUSSIAN_BLUR, 50.0, 1.0},
    {"Gauss r=100 q=0.0",      BL_IMAGE_FILTER_TYPE_GAUSSIAN_BLUR, 100.0, 0.0},
    {"Gauss r=100 q=0.5",      BL_IMAGE_FILTER_TYPE_GAUSSIAN_BLUR, 100.0, 0.5},
    {"Gauss r=100 q=1.0",      BL_IMAGE_FILTER_TYPE_GAUSSIAN_BLUR, 100.0, 1.0},
  };

  printf("%-25s %10s %10s\n", "Filter", "Time (ms)", "MPixels/s");
  printf("%-25s %10s %10s\n", "-------------------------", "----------", "----------");

  for (const auto& tc : cases) {
    BLImage dst;

    // Warm up
    BLImage::filter(dst, img, tc.type, tc.radius, tc.quality);

    // Benchmark (average of 5 runs)
    int runs = 5;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < runs; i++) {
      BLImage::filter(dst, img, tc.type, tc.radius, tc.quality);
    }
    auto end = std::chrono::high_resolution_clock::now();

    double ms = std::chrono::duration<double, std::milli>(end - start).count() / runs;
    double mpixels = (1920.0 * 1080.0) / 1000000.0;
    double mpps = mpixels / (ms / 1000.0);

    printf("%-25s %8.1f ms %8.1f\n", tc.name, ms, mpps);
  }

  // Save quality comparison images
  for (double q : {0.0, 0.5, 1.0}) {
    BLImage dst;
    BLImage::filter(dst, img, BL_IMAGE_FILTER_TYPE_GAUSSIAN_BLUR, 50.0, q);
    char filename[64];
    snprintf(filename, sizeof(filename), "bench_gauss_r50_q%.0f.png", q * 100);
    dst.write_to_file(filename);
    printf("Saved: %s\n", filename);
  }

  printf("\nSaved: bench_source_1080p.png, bench_gaussian_r50_1080p.png, bench_gaussian_r100_1080p.png\n");
  return 0;
}
