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
