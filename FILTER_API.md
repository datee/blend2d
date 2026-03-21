# Blend2D Image Filter & Effect API

## Overview

Blend2D provides two levels of image processing:

1. **Low-level filters** (`BLImage::filter`) — direct access to blur algorithms with manual quality control
2. **High-level effects** (`BLImage::blur`, `glow`, `drop_shadow`, etc.) — automatic algorithm selection and compositing

All operations work on `BLImage` objects and support PRGB32, XRGB32, and A8 pixel formats.

---

## Low-Level Filter API

### `BLImage::filter`

```cpp
static BLResult BLImage::filter(
    BLImage& dst,           // Output image
    const BLImage& src,     // Input image
    BLImageFilterType type,  // BL_IMAGE_FILTER_TYPE_BOX_BLUR or GAUSSIAN_BLUR
    double radius,           // Blur radius in pixels
    double quality = 0.5     // 0.0 (fastest) to 1.0 (highest quality)
);
```

**Filter types:**

| Type | Algorithm | Speed | Use case |
|------|-----------|-------|----------|
| `BOX_BLUR` | Single-pass sliding window | O(1) per pixel | Real-time, uniform blur |
| `GAUSSIAN_BLUR` | 3-pass box blur approximation | O(1) per pixel × 3 | Smooth, natural-looking blur |

**Quality parameter:**
- `0.0` — aggressive downscale before blur (fastest, ~5ms at 1080p)
- `0.5` — moderate downscale (balanced, ~7ms at 1080p)
- `1.0` — full resolution, no downscale (~8ms at 1080p)

**Performance (1920×1080 PRGB32, Release build):**

| Operation | Time |
|-----------|------|
| Box blur (any radius) | 1.5ms |
| Gaussian q=0.0 | 5ms |
| Gaussian q=0.5 | 7ms |
| Gaussian q=1.0 | 8ms |

**C API:**
```c
BLResult bl_image_filter(BLImageCore* dst, const BLImageCore* src,
                          BLImageFilterType type, double radius, double quality);
```

---

## High-Level Effect API

### Effect Types

| Type | Description |
|------|-------------|
| `BL_IMAGE_EFFECT_TYPE_BLUR` | Blur with auto algorithm selection |
| `BL_IMAGE_EFFECT_TYPE_GLOW` | Blur + color tint + composite |
| `BL_IMAGE_EFFECT_TYPE_DROP_SHADOW` | Alpha extraction → blur → colorize → offset |
| `BL_IMAGE_EFFECT_TYPE_BRIGHTNESS_CONTRAST` | Per-pixel brightness and contrast adjustment |
| `BL_IMAGE_EFFECT_TYPE_SATURATION` | Color saturation adjustment |

### Effect Flags

| Flag | Description |
|------|-------------|
| `BL_IMAGE_EFFECT_FLAG_INNER` | Effect renders inside the shape instead of outside |
| `BL_IMAGE_EFFECT_FLAG_KNOCKOUT` | Original shape is removed, leaving only the effect |

Flags can be combined: `INNER | KNOCKOUT` gives inner effect only with original removed.

---

### `BLImage::blur`

```cpp
static BLResult BLImage::blur(
    BLImage& dst, const BLImage& src,
    double radius,
    double quality = 0.5
);
```

Automatic algorithm selection:
- `quality < 0.3` → single box blur (fastest)
- `quality >= 0.3` → 3-pass Gaussian approximation

**Example:**
```cpp
BLImage blurred;
BLImage::blur(blurred, source, 20.0);        // Default quality
BLImage::blur(blurred, source, 20.0, 0.0);   // Fastest (box blur)
```

---

### `BLImage::glow`

```cpp
static BLResult BLImage::glow(
    BLImage& dst, const BLImage& src,
    double radius,
    BLRgba32 color,
    double quality = 0.5
);
```

Blurs the source and composites the blurred version behind (outer) or inside (inner) the original.

**Modes (via `BLImage::apply_effect` with flags):**

| Mode | Flags | Result |
|------|-------|--------|
| Outer glow | `NONE` | Glow behind original |
| Outer knockout | `KNOCKOUT` | Glow only, original removed |
| Inner glow | `INNER` | Glow along inside edges |
| Inner knockout | `INNER \| KNOCKOUT` | Inner glow ring only |

**Example:**
```cpp
// Simple outer glow
BLImage result;
BLImage::glow(result, source, 15.0, BLRgba32(0xFFFF4400));

// Inner glow with knockout
BLImageEffectOptions opts{};
opts.type = BL_IMAGE_EFFECT_TYPE_GLOW;
opts.radius = 15.0;
opts.quality = 0.5;
opts.color = 0xFFFF0000;
opts.flags = BL_IMAGE_EFFECT_FLAG_INNER | BL_IMAGE_EFFECT_FLAG_KNOCKOUT;
BLImage::apply_effect(result, source, opts);
```

---

### `BLImage::drop_shadow`

```cpp
static BLResult BLImage::drop_shadow(
    BLImage& dst, const BLImage& src,
    double radius,
    double offset_x, double offset_y,
    BLRgba32 color,
    double quality = 0.5
);
```

Extracts the alpha channel, blurs it, colorizes with the shadow color, offsets, and composites behind the original.

**Modes:**

| Mode | Flags | Result |
|------|-------|--------|
| Outer shadow | `NONE` | Shadow behind original, image expanded |
| Outer knockout | `KNOCKOUT` | Shadow blob only |
| Inner shadow | `INNER` | Shadow inside shape edges |
| Inner knockout | `INNER \| KNOCKOUT` | Inner shadow only |

**Example:**
```cpp
BLImage result;
BLImage::drop_shadow(result, source, 8.0, 5.0, 5.0, BLRgba32(0xC0000000));
```

**Note:** Outer shadow expands the output image to fit both the shadow offset and the original.

---

### `BLImage::brightness_contrast`

```cpp
static BLResult BLImage::brightness_contrast(
    BLImage& dst, const BLImage& src,
    double brightness,   // -1.0 (black) to +1.0 (white), 0.0 = unchanged
    double contrast       // -1.0 (flat gray) to +1.0 (max contrast), 0.0 = unchanged
);
```

Per-pixel LUT-based adjustment. Correctly handles premultiplied alpha (unpremultiplies, adjusts, repremultiplies).

**Example:**
```cpp
BLImage result;
BLImage::brightness_contrast(result, source, 0.2, 0.3);  // Slightly brighter, more contrast
BLImage::brightness_contrast(result, source, -0.5, 0.0);  // Much darker
```

---

### `BLImage::saturation`

```cpp
static BLResult BLImage::saturation(
    BLImage& dst, const BLImage& src,
    double factor   // 0.0 = grayscale, 1.0 = unchanged, 2.0 = double saturation
);
```

Lerps between grayscale luminance (BT.709 weights: 0.2126R + 0.7152G + 0.0722B) and the original color.

**Example:**
```cpp
BLImage result;
BLImage::saturation(result, source, 0.0);   // Grayscale
BLImage::saturation(result, source, 1.5);   // More vivid
```

---

### Effect Chaining

```cpp
static BLResult BLImage::apply_effects(
    BLImage& dst, const BLImage& src,
    const BLImageEffectOptions* effects,
    uint32_t count
);
```

Applies multiple effects to the same source and composites all layers together. Each effect is applied independently to the original source, then layered in order.

**Example — drop shadow + outer glow + inner glow:**
```cpp
BLImageEffectOptions effects[3] = {};

// Layer 0: outer glow
effects[0].type = BL_IMAGE_EFFECT_TYPE_GLOW;
effects[0].radius = 25.0;
effects[0].color = 0xFF0066FF;

// Layer 1: inner glow
effects[1].type = BL_IMAGE_EFFECT_TYPE_GLOW;
effects[1].radius = 10.0;
effects[1].color = 0xFFFF0000;
effects[1].flags = BL_IMAGE_EFFECT_FLAG_INNER;

// Layer 2: drop shadow
effects[2].type = BL_IMAGE_EFFECT_TYPE_DROP_SHADOW;
effects[2].radius = 8.0;
effects[2].offset_x = 6.0;
effects[2].offset_y = 6.0;
effects[2].color = 0xAA000000;

BLImage result;
BLImage::apply_effects(result, source, effects, 3);
```

---

## BLImageEffectOptions Struct

```cpp
struct BLImageEffectOptions {
    uint32_t type;       // BLImageEffectType
    double   radius;     // Blur radius (or brightness for BRIGHTNESS_CONTRAST, or saturation factor)
    double   quality;    // 0.0-1.0 speed/quality tradeoff (or contrast for BRIGHTNESS_CONTRAST)
    double   offset_x;   // Shadow offset X
    double   offset_y;   // Shadow offset Y
    uint32_t color;      // Effect color (0xAARRGGBB)
    uint32_t flags;      // BLImageEffectFlags (INNER, KNOCKOUT)
};
```

**Field usage by effect type:**

| Field | BLUR | GLOW | DROP_SHADOW | BRIGHTNESS_CONTRAST | SATURATION |
|-------|------|------|-------------|---------------------|------------|
| `radius` | blur radius | blur radius | blur radius | brightness (-1..+1) | saturation factor |
| `quality` | speed/quality | speed/quality | speed/quality | contrast (-1..+1) | unused |
| `offset_x` | — | — | shadow offset X | — | — |
| `offset_y` | — | — | shadow offset Y | — | — |
| `color` | — | glow color | shadow color | — | — |
| `flags` | — | INNER, KNOCKOUT | INNER, KNOCKOUT | — | — |

---

## Implementation Details

- **Threading:** Uses blend2d's global thread pool for parallel row/column processing
- **SIMD:** SSE2 and AVX2 optimized vertical blur passes with runtime dispatch
- **Downscale optimization:** Large-radius Gaussian automatically downscales, blurs at reduced resolution, then upscales back (quality-controlled)
- **Premultiplied alpha:** All operations correctly handle PRGB32 (unpremultiply → process → repremultiply)
- **In-place:** All operations support `dst == src`
