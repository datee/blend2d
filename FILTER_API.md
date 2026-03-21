# Blend2D Image Filter & Effect API

## Overview

Blend2D provides two levels of image processing:

1. **Low-level filters** (`BLImage::filter`) — direct blur algorithm control
2. **High-level effects** (`BLImage::blur`, `glow`, `drop_shadow`, `tint`, etc.) — automatic algorithm selection, compositing, and parameter control

All operations work on `BLImage` objects. Supported formats: PRGB32, XRGB32, A8 (some effects PRGB32 only).

---

## Quick Reference

```cpp
// Blur
BLImage::blur(dst, src, 20.0);                              // default q=0.5
BLImage::blur(dst, src, 20.0, 0.0);                         // fastest (box blur)
BLImage::blur(dst, src, 20.0, 1.0);                         // ultra (true Gaussian)

// Drop shadow
BLImage::drop_shadow(dst, src, 8.0, 5.0, 5.0, BLRgba32(0xC0000000));

// Glow
BLImage::glow(dst, src, 15.0, BLRgba32(0xFFFF4400));

// Brightness / contrast
BLImage::brightness_contrast(dst, src, 0.3, 0.0);           // brighten
BLImage::brightness_contrast(dst, src, 0.0, 0.8);           // high contrast

// Saturation
BLImage::saturation(dst, src, 0.0);                          // grayscale
BLImage::saturation(dst, src, 2.0);                          // oversaturated

// Flash-style tint
BLImage::tint(dst, src, BLRgba32(0xFF0000FF), 0.5);         // 50% toward blue

// Chain multiple effects
BLImageEffectOptions effects[3] = { /* ... */ };
BLImage::apply_effects(dst, src, effects, 3);
```

---

## Effect Types

| Type | Description |
|------|-------------|
| `BLUR` | Blur with auto algorithm selection based on quality |
| `GLOW` | Blur alpha → colorize → composite (inner/outer/knockout) |
| `DROP_SHADOW` | Alpha → blur → colorize → offset → composite (inner/outer/knockout) |
| `BRIGHTNESS_CONTRAST` | Per-pixel LUT-based brightness and contrast adjustment |
| `SATURATION` | Lerp between grayscale luminance (BT.709) and original |
| `TINT` | Flash-style: lerp(original, tintColor, amount), alpha preserved |

---

## BLImageEffectOptions Struct

```cpp
struct BLImageEffectOptions {
    uint32_t type;       // BLImageEffectType
    double   radius;     // Blur radius / brightness / saturation factor / tint amount
    double   quality;    // 0.0-1.0 speed/quality (or contrast for BRIGHTNESS_CONTRAST)
    double   offset_x;   // Shadow offset X
    double   offset_y;   // Shadow offset Y
    uint32_t color;      // Effect color (0xAARRGGBB)
    uint32_t flags;      // INNER, KNOCKOUT
    double   opacity;    // Effect layer opacity (0.0-1.0, default 1.0)
    double   spread;     // Alpha choke before blur (0.0=soft, 1.0=hard edge)
    double   strength;   // Glow brightness multiplier (>1.0 = brighter)
};
```

**Field usage by effect type:**

| Field | BLUR | GLOW | DROP_SHADOW | BRIGHTNESS_CONTRAST | SATURATION | TINT |
|-------|------|------|-------------|---------------------|------------|------|
| `radius` | blur radius | blur radius | blur radius | brightness (-1..+1) | factor (0=gray) | amount (0..1) |
| `quality` | algorithm | algorithm | algorithm | contrast (-1..+1) | — | — |
| `offset_x/y` | — | — | shadow offset | — | — | — |
| `color` | — | glow color | shadow color | — | — | tint color |
| `flags` | — | INNER, KO | INNER, KO | — | — | — |
| `opacity` | — | layer opacity | layer opacity | — | — | — |
| `spread` | — | alpha choke | alpha choke | — | — | — |
| `strength` | — | brightness × | — | — | — | — |

---

## Quality Tiers

| Quality | Blur Algorithm | Downscale | Performance (1080p) |
|---------|---------------|-----------|---------------------|
| **0.0–0.3** | Single box blur pass | Aggressive | ~2ms |
| **0.3–0.7** | 3-pass box blur (Gaussian approx) | Moderate | ~7ms |
| **0.7–0.95** | 3-pass box blur (Gaussian approx) | None | ~8ms |
| **0.95–1.0** | True Gaussian kernel convolution | None | ~25ms |

---

## Effect Flags

| Flag | Description |
|------|-------------|
| `BL_IMAGE_EFFECT_FLAG_INNER` | Effect renders inside shape (inverts alpha before blur) |
| `BL_IMAGE_EFFECT_FLAG_KNOCKOUT` | Original shape removed, only effect visible |

Combinable: `INNER | KNOCKOUT` gives inner effect ring with transparent center.

---

## Implementation Details

- **Threading:** Box blur uses blend2d's global thread pool (rows for H pass, columns for V pass)
- **SIMD:** SSE2 and AVX2 optimized vertical blur passes with runtime dispatch via `ImageFilterOps` table
- **Downscale:** Large-radius Gaussian auto-downscales, blurs at reduced resolution, upscales back (quality-controlled)
- **Premultiplied alpha:** Color adjustments unpremultiply → process → repremultiply
- **In-place:** All operations support `dst == src`
- **Architecture:** Separate files following blend2d patterns: `imagefilter.cpp` (scalar + dispatch), `imagefilter_sse2.cpp`, `imagefilter_avx2.cpp`, registered via `bl_image_filter_rt_init()` in `runtime.cpp`

---

## Future Improvements

These are not implemented but would enhance the filter system:

| Feature | Description | Priority |
|---------|-------------|----------|
| **Color matrix** | 5×4 matrix transform — subsumes brightness, contrast, saturation, hue rotate, invert as special cases. Standard in CSS/SVG/Photoshop. | High |
| **Hue rotation** | Rotate colors by N degrees in HSL space. Currently requires manual RGB↔HSL conversion. | Medium |
| **Invert** | Simple 255-x per channel. Trivial to add. | Low |
| **Sharpen / unsharp mask** | `result = original + strength × (original - blurred)`. Built on existing blur. | Medium |
| **BLContext::applyFilter()** | Context-integrated filtering with clip awareness. Currently filters only work on standalone images. Requires flush + read-back + filter + write-back within the rendering pipeline. | High |
| **True Gaussian threading** | The ultra-quality kernel convolution doesn't use the thread pool. Box blur does. | Medium |
| **A8 format for all effects** | Tint, saturation, true Gaussian currently only support PRGB32. | Low |
| **Color overlay / multiply** | Multiply image by a color (lens filter effect). Different from tint which lerps. | Low |

---

## Test Coverage

- **124 pixel checks** across 35 test scenes
- **50+ PNG files** generated for visual inspection
- Tests cover: all effect types, all flag combinations (inner/outer/knockout), quality tiers, edge cases (zero radius, in-place, alpha preservation, solid color identity, max/min brightness), effect chaining, spread, opacity, strength, true Gaussian vs approximation
- Run: `bl_test_visual.exe` (generates PNGs + pixel verification)
- Unit tests: `bl_test_runner.exe` (includes `imagefilter_test.cpp`)
