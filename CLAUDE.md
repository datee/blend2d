# Blend2D — Claude Code Notes

## Build

```bash
# Debug with tests (no JIT — asmjit not available)
cmake -B build -DBLEND2D_TEST=ON -DBLEND2D_NO_JIT=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Release for benchmarks
cmake -B build_rel -DBLEND2D_TEST=ON -DBLEND2D_NO_JIT=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build_rel --config Release
```

## Test

```bash
./build/Debug/bl_test_runner.exe      # Unit tests (all priority groups)
./build/Debug/bl_test_visual.exe      # Visual tests (generates 60+ PNGs)
./build_rel/Release/blur_benchmark.exe # Blur performance benchmark
```

## Branch

Working branch: `claude/repo-overview-MB519`
All work committed and pushed. 40+ commits ahead of master.

## Key Files We Added/Modified

### Filter system
- `blend2d/core/imagefilter.cpp` — scalar implementation + dispatch + threading
- `blend2d/core/imagefilter_sse2.cpp` — SSE2 optimized vertical blur
- `blend2d/core/imagefilter_avx2.cpp` — AVX2 optimized vertical blur
- `blend2d/core/imagefilter_p.h` — internal ops table, types
- `blend2d/core/imagefilter_test.cpp` — unit tests
- `blend2d/core/image.h` — public API (filter types, effect types, options structs)

### Path clipping
- `blend2d/raster/rastercontext.cpp` — clip_to_path_impl, fill dispatch redirects, save/restore
- `blend2d/raster/rastercontext_p.h` — clip_mask/clip_path fields on context impl
- `blend2d/raster/statedata_p.h` — clip_mask/clip_path fields on SavedState
- `blend2d/core/context.h` — clip_to_path vtable entry, C/C++ API
- `blend2d/core/context.cpp` — C API wrappers

### Visual tests
- `blend2d-testing/tests/bl_test_visual.cpp` — 48 test scenes, 159 pixel checks

### Documentation
- `PLAN.md` — master plan with status
- `PROGRESS.md` — detailed progress report
- `FILTER_API.md` — filter/effect API reference

## Patterns to Follow

- **SIMD files:** name `_sse2.cpp`, `_avx2.cpp` — CMake auto-applies compile flags
- **Runtime dispatch:** `ImageFilterOps` function pointer table, registered in `bl_*_rt_init()`
- **Threading:** `bl_thread_pool_global()->acquire_threads()`, spin-wait completion
- **Tests:** add to `bl_test_visual.cpp`, use `save_image()` + `check_pixel_near()`
- **Context vtable:** add function pointer, C API in `context.cpp`, C++ wrapper in `context.h`

## Known Limitations

- No asmjit — JIT pipeline changes compile but can't be tested at runtime
- Async path clipping falls back to sync (temp-image-composite can't be deferred)
- Phase 5.1 public mask API needs serializer infrastructure (plan saved)
- Only PRGB32/XRGB32 for most filter effects (A8 partial)
