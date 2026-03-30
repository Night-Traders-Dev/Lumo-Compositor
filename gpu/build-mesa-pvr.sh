#!/usr/bin/env bash
set -euo pipefail

# Build Mesa with PowerVR PVR Gallium driver for SpacemiT X1 (BXE-2-32).
#
# This script clones SpacemiT's Mesa fork and builds pvr_dri.so, which
# bridges Mesa's EGL/GLES to the proprietary PowerVR GPU driver stack
# (libpvr_dri_support.so from img-gpu-powervr).
#
# Prerequisites:
#   - img-gpu-powervr package installed (provides libpvr_dri_support.so)
#   - PowerVR kernel driver (pvrsrvkm) loaded
#   - Build deps: meson ninja-build python3-mako libdrm-dev libwayland-dev
#                 wayland-protocols libexpat1-dev libxshmfence-dev
#
# Usage:
#   ./build-mesa-pvr.sh            # build and install
#   ./build-mesa-pvr.sh --clean    # wipe build dir and rebuild
#
# After install, set WLR_DRM_DEVICES=/dev/dri/card1 and the compositor
# will use EGL/GLES2 via the PowerVR GPU instead of pixman software.

MESA_REPO="https://gitee.com/spacemit-buildroot/mesa3d.git"
MESA_BRANCH="k1-bl-v2.2.y"
MESA_DIR="${HOME}/mesa3d"
BUILD_DIR="${MESA_DIR}/build-pvr"

# The display DRM driver name on OrangePi RV2 (SpacemiT X1)
DRM_ALIAS="ky-drm-drv"

die() { echo "ERROR: $*" >&2; exit 1; }

if [[ "${1:-}" == "--clean" ]] && [[ -d "$BUILD_DIR" ]]; then
    echo "==> Removing old build directory"
    rm -rf "$BUILD_DIR"
fi

# Clone if needed
if [[ ! -d "$MESA_DIR" ]]; then
    echo "==> Cloning SpacemiT Mesa (branch ${MESA_BRANCH})"
    git clone --depth 1 "$MESA_REPO" -b "$MESA_BRANCH" "$MESA_DIR"
fi

# Verify PVR frontend exists
if [[ ! -d "${MESA_DIR}/src/gallium/frontends/pvr" ]]; then
    die "Mesa source missing PVR Gallium frontend at ${MESA_DIR}/src/gallium/frontends/pvr"
fi

# Verify proprietary support library
if [[ ! -f /usr/lib/libpvr_dri_support.so ]]; then
    die "libpvr_dri_support.so not found. Install img-gpu-powervr first."
fi

cd "$MESA_DIR"

# Configure
if [[ ! -f "${BUILD_DIR}/build.ninja" ]]; then
    echo "==> Configuring Mesa with PVR Gallium driver (alias=${DRM_ALIAS})"
    meson setup "$BUILD_DIR" \
        -Dgallium-drivers=pvr \
        -Dgallium-pvr-alias="$DRM_ALIAS" \
        -Dvulkan-drivers= \
        -Dglx=disabled \
        -Dplatforms=wayland \
        -Degl=enabled \
        -Dgles1=enabled \
        -Dgles2=enabled \
        -Dshared-glapi=enabled \
        -Dgbm=enabled \
        -Ddri3=disabled \
        -Dllvm=disabled \
        -Dbuildtype=release \
        -Dprefix=/usr/local \
        -Dlibdir=lib/riscv64-linux-gnu
fi

# Build
echo "==> Compiling Mesa (this may take 10-30 minutes on RISC-V)"
meson compile -C "$BUILD_DIR"

# Verify pvr_dri.so was built
if [[ ! -f "${BUILD_DIR}/src/gallium/targets/dri/pvr_dri.so" ]] && \
   [[ ! -f "${BUILD_DIR}/src/gallium/targets/dri/libgallium_dri.so" ]]; then
    die "pvr_dri.so was not produced by the build"
fi

echo "==> Build complete."
echo ""
echo "To install (requires sudo):"
echo "  sudo meson install -C ${BUILD_DIR}"
echo ""
echo "After install, create the DRI symlink if needed:"
echo "  sudo ln -sf pvr_dri.so /usr/local/lib/riscv64-linux-gnu/dri/ky-drm-drv_dri.so"
echo ""
echo "Then restart the compositor with:"
echo "  sudo systemctl restart gdm"
echo ""
echo "The compositor should now use EGL/GLES2 via PowerVR GPU."
