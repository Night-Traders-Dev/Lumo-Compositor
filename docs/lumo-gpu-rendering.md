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

### Current Status

GPU compositing is **not enabled** due to:
1. wlroots format negotiation fails for the split GPU/display setup
2. The display controller's `ky-drm` driver needs LINEAR modifiers in mgpu_formats
3. Mesa GBM backend support for ky-drm is incomplete

### Enabling GPU Rendering

To enable GPU compositing when driver support improves:

1. Set `WLR_RENDER_DRM_DEVICE=/dev/dri/renderD128` in the session environment
2. Ensure wlroots detects the GPU render node for GBM allocation
3. Verify LINEAR modifier is available in format negotiation
4. Test with `WLR_RENDERER=gles2` or `WLR_RENDERER=vulkan`

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
