# Lumo GPU Acceleration

## Hardware

The OrangePi RV2 has an **Imagination PowerVR BXE-2-32** GPU integrated
into the SpacemiT X1 SoC. The GPU and display controller are separate
DRM devices:

- `/dev/dri/card0` (renderD128) — PowerVR GPU, driver: `pvrsrvkm`
- `/dev/dri/card1` — Display controller, driver: `ky-drm-drv`

## Current Status

The GPU is present and the kernel driver is active, but the userspace
driver chain is incomplete:

| Component | Status |
|-----------|--------|
| Kernel driver (pvrsrvkm) | Built-in, running |
| Firmware (rgx.fw/rgx.sh) | Installed (BVNC 36.29.52.182) |
| Vulkan (libVK_IMG.so) | Installed (1.3.277), blocked by DRM node bug |
| OpenCL (libPVROCL.so) | Installed |
| GLES blobs (libGLESv2_PVR_MESA.so) | Installed |
| PVR DRI support (libpvr_dri_support.so) | Installed |
| **pvr_dri.so (Mesa DRI driver)** | **MISSING — must be built from source** |

## Building pvr_dri.so

The `pvr_dri.so` Mesa DRI driver bridges Mesa's EGL/GBM to the
proprietary PowerVR GLES stack. It's built from SpacemiT's Mesa fork:

```sh
./gpu/build-mesa-pvr.sh
sudo meson install -C ~/mesa3d/build-pvr
sudo systemctl restart gdm
```

See `build-mesa-pvr.sh` for full details and prerequisites.

## wlroots Vulkan Patch

The `wlroots-powervr-drm-fallback.patch` works around a bug in the
PowerVR Vulkan driver where `VK_EXT_physical_device_drm` reports empty
DRM node info. Applied to `/home/orangepi/wlroots` on the device.

Note: the Vulkan path is blocked by a separate issue (split DRM devices
cause empty DMA-BUF format tables). The EGL/GLES2 path via pvr_dri.so
is the working solution.

## Architecture

```
App (SHM buffer) → Compositor (wlroots) → EGL/GLES2 → Mesa pvr_dri.so
    → libpvr_dri_support.so → pvrsrvkm kernel driver → PowerVR GPU
    → display scanout via ky-drm-drv (card1)
```

The key insight: `pvr_dri.so` is a Gallium frontend that wraps the
proprietary GLES implementation. Mesa loads it when the DRM device
matches the `gallium-pvr-alias` (set to `ky-drm-drv` at build time).
This allows EGL on the display device to use the GPU for rendering.

## Current Status: GPU initializes but display scanout fails

As of v0.0.57, `pvr_dri.so` is built and installed. The GLES2 renderer
initializes successfully on the PowerVR GPU:

```
Creating GLES2 renderer
Using OpenGL ES 3.2 build 24.2@6603887
GL vendor: Imagination Technologies
GL renderer: PowerVR B-Series BXE-2-32
```

However, **the display output is blank** when using the GPU renderer.
The GPU renders to buffers allocated on `/dev/dri/card0` (renderD128),
but the display controller (`ky-drm-drv` on `/dev/dri/card1`) cannot
scan out those buffers because they are on a different DRM device.

This is the **split DRM device** problem: GPU and display are separate
kernel DRM devices with no shared DMA-BUF import/export path.

### Workaround

Force pixman (CPU) rendering in `/etc/environment`:

```
WLR_RENDERER=pixman
```

### What would fix it

1. A kernel driver update from SpacemiT that exposes GPU-rendered
   buffers to the display controller via DMA-BUF import (PRIME)
2. Or a unified DRM device driver that combines GPU and display
3. Or wlroots multi-GPU support (rendering on card0, scanout on card1
   with automatic buffer copies)

The `pvr_dri.so` build and wlroots patch are ready — once the kernel
DRM integration is fixed, GPU acceleration will just work.
