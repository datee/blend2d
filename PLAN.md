# Blend2D Issue Resolution Plan

## Phase 1: Low-Hanging Fruit & Cleanup (Low complexity, high confidence)

### 1.1 Remove deprecated/legacy code
- **`core/api-internal_p.h:596`** — Remove `bl_assign_func()` template. Search codebase for all usages, replace with direct assignment or `reinterpret_cast`.
- **`support/bitops_p.h:503`** — Remove `BitWordIterator<T>` class. Grep for all usages, migrate callers to the newer bit iteration API.
- **`core/matrix.h:121`** — Remove the union from `BLMatrix2D`, keep only `m[6]` array. Grep for all uses of named members (`m00`, `m01`, `m10`, `m11`, `m20`, `m21`) and rewrite as `m[0]`..`m[5]`.
- **`opentype/otlayout.cpp:362`** — Remove code marked for removal.

### 1.2 Documentation & trivial fixes
- **`raster/rasterdefs_p.h:63`** — Document queue flags in `ContextFlags` enum.
- **`core/context.h:346`** — Document `command_queue_limit` field (even if no-op, document the intent and current status).
- **`codec/jpegcodec.cpp:163`** — Resolve the `// TODO: Is this necessary?` — confirm JFIF spec behavior, add clarifying comment or remove dead check.
- **`codec/jpegcodec.cpp:524`** — Handle `kDensityOnlyAspect` case in APP0 parsing (set aspect ratio metadata without DPI).

### 1.3 Platform detection
- **`threading/futex.cpp:30`** — Implement OpenBSD futex detection. Check for `__OpenBSD_version` and `futex(2)` syscall availability (added in OpenBSD 6.2+).

---

## Phase 2: JIT Optimizations — Quick Wins (Low-Medium complexity)

### 2.1 X86 alpha multiplication optimization
- **`pipeline/jit/fetchutilspixelaccess.cpp:1516`** — In `fetch_mask_a8_into_pc_by_expanding_to_32bits()`: reorder operations to extend u8→u16, multiply with global alpha, then extend u16→u32 and shuffle. Saves multiplication instructions.
- **`pipeline/jit/fetchutilspixelaccess.cpp:1637`** — In `fetch_mask_a8_into_pc()`: implement the specialized X86 path that zero-extends to U16, multiplies, then expands to U32 (mirroring the approach from 1516).

### 2.2 AVX-512 masking re-evaluation
- **`pipeline/jit/fetchpatternpart.cpp:1896`** — Benchmark AVX-512 masking (`vpcmpgtd` + `kmovdqa32`) against current XOR-blend approach on modern CPUs (Sapphire Rapids+). Conditionally enable if faster; otherwise, add a comment with benchmark results.

### 2.3 Relax blit alignment constraint
- **`pipeline/jit/fetchpatternpart.cpp:734`** — In `FetchSimplePatternPart::start_at_x()`: relax the `is_rect_fill()` assertion for aligned blits to support non-rectangular fills. Add branching logic for non-rect cases.

### 2.4 AArch64 packed coverage format
- **`pipeline/jit/compoppart.cpp:98`** — Extend `PixelCoverageFormat::kPacked` to all composition modes on AArch64 (not just src_copy/src_over/screen). Validate with benchmarks per comp op.

---

## Phase 3: A8 Pipeline Completion (Medium complexity, high impact)

All A8 (alpha-only) issues are interconnected and should be tackled together.

### 3.1 A8 satisfy-pixel implementation
- **`pipeline/jit/fetchutilspixelaccess.cpp:2981`** — Implement PI (inverted packed alpha) from UA (unpacked alpha) in `satisfy_pixels_a8()`. Model after equivalent RGBA32 logic: pack UA, then invert.
- **`pipeline/jit/fetchutilspixelaccess.cpp:2988`** — Implement UA/UI flag handling when UA is empty. Unpack from PA if available.
- **`pipeline/jit/fetchutilspixelaccess.cpp:3203`** — Complete `satisfy_solid_pixels_a8()` for remaining flags beyond PA/PI. Handle UA and UI flags.

### 3.2 A8 solid color preprocessing
- **`pipeline/jit/compoppart.cpp:370`** — Implement A8-specific solid color preprocessing in `CompOpPart::src_fetch()`. Extract alpha channel from solid color, pack into A8 format registers.

### 3.3 A8 predicated fetch
- **`pipeline/jit/fetchutilspixelaccess.cpp:553`** — Implement multi-vector predicated fetch for `n > 16` in `fetch_predicated_vec8_v128()`. Extend the existing branching pattern (≤2, ≤4, ≤8, ≤16) to handle ≤32, ≤48, ≤64 etc.
- **`pipeline/jit/fetchutilspixelaccess.cpp:843`** — Extend `fetch_predicated_vec32_v128()` beyond the current `vec_count == 2` constraint.

---

## Phase 4: AArch64 Optimizations (Medium complexity)

### 4.1 STP (Store Pair) instruction usage
- **`pipeline/jit/fetchutilspixelaccess.cpp:3621`** — In `store_pixels_and_advance()` (A8 path, n > 16): modify the store loop to use AArch64 STP instruction for pairs of vector registers. Requires either enhancing `v_storeavec()` to accept register pairs or emitting STP directly.
- **`pipeline/jit/fetchutilspixelaccess.cpp:3668`** — Same optimization for RGBA32 path (n > 4). Identical approach, different pixel format.

### 4.2 ARM SIMD abstractions
- **`simd/simdarm_p.h:529`** — Investigate whether returning zero for "undefined" SIMD is acceptable. If so, add a comment explaining the rationale. If not, use `vreinterpret` from an uninitialized register (with compiler-specific pragmas to suppress warnings).
- **`simd/simdarm_p.h:2294`** — Implement the commented-out `movw_*` (move-with-zero-extend) template functions using NEON intrinsics (`vmovl_u8`, `vmovl_u16`, etc.).
- **`simd/simdarm_p.h:2842`** — Refactor conversion workaround functions into `Internal` namespace with proper API.
- **`simd/simdx86_p.h:5503`** — Same refactoring for X86 conversion workarounds.

### 4.3 X86 SIMD wrapping
- **`simd/simdx86_p.h:1335`** — Implement `simd_cvt_f64_i32`, `simd_cvtt_f64_i32`, `simd_cvt_f64_f32`, `simd_cvt_2xi32_f64`, `simd_cvt_f32x2_f64` using SSE2/AVX intrinsics (`_mm_cvtsd_si32`, `_mm_cvtsi32_sd`, etc.).

---

## Phase 5: Rendering Context Features (Medium-High complexity)

### 5.1 Masking support
- **`raster/rastercontext.cpp:3633`** — Implement pixel-aligned masked rectangle fill. Uncomment and complete the serializer initialization for mask fetch data.
- **`raster/rastercontext.cpp:3642`** — Implement non-aligned masked fill with sub-pixel fx/fy positioning.
- **`raster/rastercontext.cpp:3658`** — Implement general-case masking with full matrix transform.
- **`raster/rendercommandprocasync_p.h:169,193,232,465`** — Implement the fetch-function-based fill paths in `fill_box_a()`, `fill_box_u()`, `fillBoxMaskA()`, and `fill_analytic()`. These are required for masking to work in the async pipeline.

### 5.2 FetchData offloading
- **`raster/rastercontext.cpp:2459,2472`** — Enable and test the commented-out FetchData calculation offloading to worker jobs. Requires validation that pending FetchData computation completes before pipeline execution.

### 5.3 Format-aware band height calculation
- **`raster/rastercontext.cpp:4155`** — Replace hardcoded assumptions with format-aware byte-per-pixel calculation in `calculate_band_height()`.
- **`raster/rastercontext.cpp:4164`** — Detect CPU cache size at runtime using platform APIs (`sysconf(_SC_LEVEL2_CACHE_SIZE)` on Linux, `GetLogicalProcessorInformation` on Windows, `sysctlbyname("hw.l2cachesize")` on macOS).
- **`raster/rastercontext.cpp:4220`** — Replace hardcoded `kPixelComponentUInt8` in `attach_async_impl()` with format-derived component type.

### 5.4 Partial fetch pixel granularity
- **`pipeline/jit/compoppart.cpp:418`** — Generalize `enter_partial_mode()` beyond the current 4-pixel-only constraint. Support 1, 2, and 8-pixel granularities.

### 5.5 Max pixels for aligned pad patterns
- **`pipeline/jit/fetchpatternpart.cpp:213`** — Re-implement multi-pixel fetch for aligned-pad patterns to raise `_max_pixels` from 4 back to 8. Must handle boundary conditions correctly.

### 5.6 Affine bilinear fetch4
- **`pipeline/jit/fetchpatternpart.cpp:1686`** — Implement 4-pixel-at-a-time affine bilinear fetch. Requires replicating interpolation logic across 4 pixel positions with proper register allocation. High complexity — consider incremental approach (fetch2 first).

### 5.7 Path-based clipping
- **`raster/rastercontext.cpp:832,2210`** — Implement path-based clipping as an alternative to box-based clipping. This is a significant feature requiring:
  1. Path storage in clip state
  2. Path intersection during rasterization
  3. Integration with `restore_clipping_from_state()` and `clip_to_rect_d_impl()`

### 5.8 Async pipeline TODOs
- **`raster/rendercommandprocasync_p.h:254`** — Resolve unused `nextBandFy0` parameter (implement or remove).
- **`raster/rendercommandprocasync_p.h:289`** — Uncomment edge storage bounds check optimization to skip empty edge ranges.

---

## Phase 6: OpenType Font Support (Medium-High complexity)

### 6.1 Context substitution matching (GSUB Types 5 & 6)
These 6 TODOs follow the same pattern — implement lookup application after match detection:
- **`otlayout.cpp:1961`** — Context Substitution Format 1 (simple glyph coverage)
- **`otlayout.cpp:1985`** — Context Substitution Format 2 (class-based). Also fix suspicious `glyph_in_end++` that should be `glyph_in_ptr++`.
- **`otlayout.cpp:2020`** — Context Substitution Format 3 (coverage-based)
- **`otlayout.cpp:2068`** — Chained Context Substitution Format 1
- **`otlayout.cpp:2104`** — Chained Context Substitution Format 2
- **`otlayout.cpp:2166`** — Chained Context Substitution Format 3

Implementation pattern (same for all 6):
1. After match is confirmed, iterate `lookup_record_array`
2. For each record, call the appropriate GSUB lookup by type
3. Adjust glyph pointers to account for substitutions that change glyph count

### 6.2 Alternate substitution selection
- **`otlayout.cpp:1692`** — Implement proper alternate glyph selection in `AlternateSubst1`. Per OpenType spec, the index should come from the feature parameter (user/application-selected) or default to 0. Add a callback or context parameter for alternate selection.

### 6.3 GSUB nested lookups
- **`otlayout.cpp:1925`** — Implement `apply_gsubNestedLookup()`. This requires:
  1. Recursive lookup invocation with depth limit (prevent infinite loops)
  2. Glyph buffer management across nested calls
  3. Proper cursor advancement after nested substitution

### 6.4 GPOS Mark attachment (3 types)
These are critical for complex scripts (Arabic, Devanagari, etc.):
- **`otlayout.cpp:2798`** — MarkToBase: Attach mark glyphs to base glyphs using anchor points. Parse MarkArray and BaseArray tables, compute anchor offsets, apply positioning.
- **`otlayout.cpp:2803`** — MarkToLigature: Similar to MarkToBase but with per-component anchors for ligature glyphs. Parse LigatureArray with component-specific anchor points.
- **`otlayout.cpp:2808`** — MarkToMark: Attach marks to other marks (stacked diacritics). Parse Mark2Array, compute relative positioning.

### 6.5 GPOS nested lookups
- **`otlayout.cpp:2814`** — Implement `apply_gpos_nested_lookups()`. Same recursive pattern as GSUB nested lookups but for positioning data instead of substitution.

### 6.6 CMAP Format 14
- **`otcmap.cpp:540`** — Implement Unicode Variation Sequences support. Parse Format 14 subtable with default and non-default UVS tables. Map (base character + variation selector) → glyph ID.

### 6.7 GLYF composite glyph point matching
- **`otglyfsimdimpl_p.h:1039`** — Implement point-index-based component positioning (when `ArgsAreXYValues` flag is NOT set). Requires resolving point indices from the base glyph's point array and using those coordinates for component placement.

### 6.8 CFF font improvements
- **`otcff.cpp:1378`** — CFF hinting: Low priority. Currently ignored without visible artifacts in most cases. Implement Type 1 hinting operators (hstem, vstem, etc.) if font quality improvement is desired.
- **`otcff.cpp:1391,1398`** — CFF Variations (VSINDEX + BLEND operators): Requires variable font infrastructure. Parse ItemVariationStore, compute deltas based on design space coordinates, apply to charstring operands.

---

## Phase 7: Image Codec Enhancements (Medium-High complexity)

### 7.1 16-BPC support (cross-cutting)
This affects multiple codecs and the pixel converter:
- **`codec/pngcodec.cpp:65`** — Remove the early `depth == 16` rejection in `check_color_type_and_bit_depth()`.
- **`codec/pngcodec.cpp:1244`** — Implement format descriptor setup for 16-bit grayscale PNG.
- **`codec/jpegcodec.cpp:142`** — Extend JPEG decoder for 16-BPC (rare but exists in medical/scientific imaging).
- **`core/pixelconverter.cpp:1988`** — Implement 16-bit depth handling in palette-to-RGB converter.
- **`core/pixelconverter.cpp:2044`** — Implement indexed (palettized) source format support in RGB32 converter.

### 7.2 PNG/APNG improvements
- **`codec/pngcodec.cpp:1537`** — Implement `kAPNGBlendOpOver` for animated PNG frame compositing. Apply source-over alpha blending when compositing frames.
- **`codec/pngopssimdimpl_p.h:637`** — Complete SIMD implementations for BPP==2 and BPP==3 PNG filter operations. These are awkward sizes for vectorization — consider partial SIMD + scalar fallback.

### 7.3 JPEG decoder improvements
- **`codec/jpegcodec.cpp:128`** — Support delayed height (height=0 in SOF). Requires buffering decoded data until DNL marker provides actual height.
- **`codec/jpegcodec.cpp:1015`** — Recalculate MCU dimensions per SOS marker instead of reusing stored values. Important for multi-scan progressive JPEGs.
- **`codec/jpegcodec.cpp:617`** — Implement EXIF metadata parsing (APP1 marker). Requires TIFF IFD parser for EXIF data extraction.

### 7.4 JPEG encoder
- **`codec/jpegcodec.cpp:1557,1619`** — Implement complete JPEG encoder:
  1. DCT forward transform
  2. Quantization with configurable quality tables
  3. Huffman encoding (or arithmetic coding)
  4. JFIF/EXIF header writing
  5. MCU organization and scan emission
  6. Register encoder virtual functions in `jpeg_codec_on_init()`

### 7.5 Pixel converter gaps
- **`core/pixelconverter.cpp:893`** — Implement ByteShuffle pixel conversion path.
- **`core/pixelconverter.cpp:1981`** — Implement LUM (grayscale) to RGB conversion.
- **`core/pixelconverter.cpp:2300`** — Implement multi-step pixel converter for format chains that can't be done in one step.

---

## Phase 8: Future API Features (Low priority)

### 8.1 BitArray boolean operations
- **`core/bitarray.h:84,330`** — Implement `bl_bit_array_combine()` with AND, OR, XOR, AND_NOT, NOT_AND operations. Uncomment C and C++ API declarations.

### 8.2 BitSet boolean operations
- **`core/bitset.h:145,427`** — Implement `bl_bit_set_combine()` with same operations. Uncomment C and C++ API declarations.

### 8.3 Command queue limiting
- **`core/context.h:346`** — Design and implement command queue depth limiting for `BLContextCreateInfo::command_queue_limit`.

---

## Phase 9: Compiler Workarounds (Monitor only)

These are working workarounds — no action needed unless compiler support changes:
- **`core/compopinfo.cpp:78`** — MSVC constexpr initialization hack. Remove when MSVC reliably handles constexpr.
- **`core/compopsimplifyimpl_p.h:1089`** — MSVC ternary chain split. Remove when MSVC can handle the full chain.

---

## Dependency Graph

```
Phase 1 (Cleanup) ─── no dependencies, do first
Phase 2 (JIT Quick Wins) ─── no dependencies
Phase 3 (A8 Pipeline) ─── blocks some Phase 5 work
Phase 4 (AArch64) ─── independent
Phase 5 (Rendering Context) ─── depends on Phase 3 for A8 paths
Phase 6 (OpenType) ─── 6.1-6.5 are interconnected; 6.6-6.8 independent
Phase 7 (Codecs) ─── 7.1 (16-BPC) cross-cuts PNG+JPEG+PixelConverter
Phase 8 (Future API) ─── independent, lowest priority
Phase 9 (Workarounds) ─── monitor only
```

## Current Status

| Phase | Items | Status | Notes |
|-------|-------|--------|-------|
| 1 | 8 | **DONE** | Cleanup, deprecated code, docs, futex |
| 2 | 5 | **DONE** | JIT alpha multiply, AVX-512 docs, blit alignment, AArch64 |
| 3 | 5 | **DONE** | A8 satisfy-pixel, solid preprocessing, predicated fetch docs |
| 4 | 7 | **DONE** | SIMD movw_ functions, conversion refactor, docs |
| 5 | 12 | **PARTIAL** | 5.3a band height done, 5.7 path clipping done, rest documented |
| 6 | 12 | Deferred | OpenType fonts |
| 7 | 11 | Deferred | Image codecs |
| 8 | 3 | Deferred | Future API |
| 9 | 2 | Monitor | Compiler workarounds |

## New Features (not in original plan)

| Feature | Status |
|---------|--------|
| Image filter system (blur, Gaussian, quality tiers) | **DONE** — threaded, SSE2/AVX2, downscale opt |
| Effect API (glow, shadow, tint, saturation, brightness, color matrix) | **DONE** — inner/outer/knockout, chaining, opacity/spread/strength |
| Path clipping (clip_to_path) | **DONE** — nested clips, save/restore, sync+async |
| BLContext::apply_filter() | **DONE** — flush + extract + filter + writeback |
| Visual test infrastructure (bl_test_visual) | **DONE** — 48 scenes, 159 checks, 60+ PNGs |

## Phase 5 Detail

| Item | Status |
|------|--------|
| 5.1 Masking (public mask API) | Investigated — needs serializer init_fetch_data_for_mask. Plan saved. |
| 5.2 FetchData offloading | Documented — threading-sensitive, deferred |
| 5.3a Band height (bpp) | **DONE** |
| 5.3b CPU cache detection | Documented — kept 256KB default |
| 5.3c Component type | Documented — all formats 8bpc |
| 5.4 Partial fetch granularity | Documented — JIT, needs asmjit |
| 5.5 Aligned pad max pixels | Documented — JIT, fetch2x4 removed |
| 5.6 Affine bilinear fetch4 | Documented — JIT, high complexity |
| 5.7 Path-based clipping | **DONE** |
| 5.8 Async pipeline docs | Documented |
