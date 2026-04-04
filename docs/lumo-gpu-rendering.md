# Lumo GPU Rendering and Performance

## Current Rendering Pipeline

Lumo uses **pixman software rendering** via wlroots 0.18. All compositing
happens on the CPU. The pipeline:

```
Shell processes (SHM buffers) → Compositor (pixman scene graph) → DRM output
```

### Background Animation

The PS4 Flow wave animation is pre-rendered at boot into a RAM buffer:

- **Loop duration**: 5 minutes (1500 frames at 5fps)
- **Resolution**: Half-res glow (400x640 uint8), upscaled 2x for composite
- **RAM**: ~366 MB
- **Prerender time**: ~80 seconds on 8x SpacemiT X60 cores
- **Runtime CPU**: 0% (memcpy playback from pre-rendered buffer)
- **Composite**: gradient fill + upscale blend, ~10% single-core CPU at 10fps

### RISC-V Vector Extensions

The build system enables RVV 1.0 on riscv64 when the compiler supports it:

```
-march=rv64gcv_zba_zbb_zbs -ftree-vectorize
```

The `lumo_fill_span()` function uses RVV intrinsics for pixel fills:
- `vsetvl_e32m2` + `vmv_v_x_u32m2` + `vse32_v_u32m2`
- VLEN=256 on SpacemiT K1 gives 8 pixels per vector operation

## GPU Hardware

The OrangePi RV2's SpacemiT K1 SoC has an Imagination PowerVR BXE-2-32 GPU:

- **Graphics**: OpenGL ES 3.2, Vulkan 1.3
- **Compute**: OpenCL 3.0 via `/usr/lib/libPVROCL.so`
- **Performance**: ~20 GFLOPS at 819 MHz
- **Display**: Separate ky-drm display controller (no render node)

### Split GPU/Display Architecture

The GPU (card0/renderD128) and display controller (card1) are separate devices:
- GBM buffer allocation works on the GPU render node
- Display controller only accepts LINEAR DMA-BUF imports
- wlroots' multi-GPU format negotiation requires LINEAR in mgpu_formats

### Current Status (v0.0.72)

GPU compositing is **partially working** — the GLES2 renderer initializes
successfully on the PowerVR GPU, but the output swapchain fails:

**What works:**
- `WLR_RENDER_DRM_DEVICE=/dev/dri/renderD128` correctly opens the GPU
- `WLR_RENDERER=gles2` initializes OpenGL ES 3.2 on PowerVR BXE-2-32
- EGL 1.5 with DMA-BUF import/export extensions
- GBM buffer allocation on renderD128
- DMA-BUF import into card1 (display controller)

**What fails:**
- `wlr_gbm_allocator_create()` fails on card1 (no GBM support on ky-drm)
- The allocator fallback finds renderD128 and creates GBM there
- But output swapchain format negotiation fails — the GPU's GBM formats
  don't match the display controller's scanout formats
- wlroots falls back to pixman

**Root cause:** wlroots 0.18 doesn't recognize card0 (GPU) and card1 (display)
as a multi-GPU pair because card0 has 0 CRTCs (fails `drmIsKMS()`). The
multi-GPU copy-back path (`backend/drm/renderer.c`) only activates when
both devices are registered as DRM backends with a parent-child relationship.

### Enabling GPU Rendering (Requires wlroots Patches)

Three patches are needed in `/home/kraken/wlroots-0.18/`:

1. **`render/wlr_renderer.c`**: In `open_preferred_drm_fd()`, when the backend
   DRM device has no render node, fall through to the arbitrary render node
   search instead of using the backend's fd directly.

2. **`backend/drm/drm.c`**: Skip `DRM_PRIME_CAP_EXPORT` check when the parent
   GPU has a render node (pvrsrvkm reports import-only but export works via GBM).

3. **`backend/backend.c`**: Modify `wlr_session_find_gpus()` to also enumerate
   non-KMS DRM devices that have render nodes, registering them as parent GPUs
   for KMS-only display controllers.

### Quick Test (No Patches)

```bash
# Add to /etc/environment:
WLR_RENDER_DRM_DEVICE=/dev/dri/renderD128
WLR_RENDERER=gles2

# Result: GLES2 renderer initializes, but output scanout fails
# Falls back to pixman automatically
```

### OpenCL Compute

The GPU supports OpenCL 3.0 which could accelerate:
- Wave glow computation (currently pre-rendered on CPU)
- Image processing for Photos/Camera apps
- Text rendering acceleration

No ML framework currently supports PowerVR BXE on riscv64.

## Performance Benchmarking

### Manual Profiling

```bash
# CPU usage by process
top -bn1 | grep lumo

# Background render timing (check logs)
journalctl _PID=$(pgrep -f "mode background") | grep "wave\|prerender"

# Memory usage
free -m
```

### Key Metrics

| Component | Target | Current |
| --------- | ------ | ------- |
| Background shell CPU | < 5% | ~0% (pre-rendered) |
| Compositor CPU | < 30% | ~20% |
| Wave prerender | < 120s | ~80s |
| Boot to desktop | < 45s | ~85s (incl. prerender) |
| Touch latency | < 16ms | ~20ms |
| RAM (total shell) | < 500MB | ~400MB |

### Optimization Opportunities

1. **GPU compositing**: Would offload pixman scene graph to GPU GLES
2. **Partial damage**: Only recompose regions that changed
3. **Direct scanout**: Skip compositing for fullscreen apps
4. **OpenCL waves**: GPU-accelerate the prerender computation
5. **Reduced prerender**: Shorter loop or lower FPS reduces boot time
