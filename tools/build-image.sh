#!/bin/bash
#
# build-image.sh — Build a minimal Lumo OS image for OrangePi RV2
#
# Takes the official OrangePi Ubuntu Noble GNOME image, strips it down
# to a minimal system, and installs Lumo compositor as the default session.
#
# Usage: sudo ./tools/build-image.sh
#
# Requirements:
#   - Official OrangePi RV2 image at ~/Orangepirv2_*.img
#   - qemu-user-static + binfmt-support (for riscv64 chroot)
#   - ~10GB free disk space
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
REAL_HOME="/home/kraken"
SRC_IMG="$REAL_HOME/Orangepirv2_1.0.0_ubuntu_noble_desktop_gnome_linux6.6.63.img"
OUT_IMG="$REAL_HOME/LumoOS_0.0.80_orangepi-rv2_ubuntu-noble_riscv64.img"
MOUNT_DIR="/tmp/lumo-image-rootfs"

# colors
RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

log() { echo -e "${GREEN}[LUMO]${NC} $*"; }
err() { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }

# must be root
[[ $EUID -eq 0 ]] || err "Must run as root: sudo $0"

# check source image
[[ -f "$SRC_IMG" ]] || err "Source image not found: $SRC_IMG"

# check qemu
[[ -f /proc/sys/fs/binfmt_misc/qemu-riscv64 ]] || err "qemu-riscv64 binfmt not registered"

# ── Step 1: Copy image ──────────────────────────────────────────────
log "Copying source image..."
cp "$SRC_IMG" "$OUT_IMG"
log "Image copied ($(du -h "$OUT_IMG" | cut -f1))"

# ── Step 2: Mount image ─────────────────────────────────────────────
log "Setting up loop device..."
LOOP=$(losetup --find --show --partscan "$OUT_IMG")
PART="${LOOP}p1"

# wait for partition device
sleep 1
[[ -b "$PART" ]] || err "Partition $PART not found"

mkdir -p "$MOUNT_DIR"
mount "$PART" "$MOUNT_DIR"
log "Mounted $PART at $MOUNT_DIR"

# ── Step 3: Prepare chroot ──────────────────────────────────────────
log "Preparing chroot..."
mount --bind /proc "$MOUNT_DIR/proc"
mount --bind /sys "$MOUNT_DIR/sys"
mount --bind /dev "$MOUNT_DIR/dev"
mount --bind /dev/pts "$MOUNT_DIR/dev/pts"

# copy qemu binary into chroot
cp /usr/bin/qemu-riscv64 "$MOUNT_DIR/usr/bin/qemu-riscv64" 2>/dev/null || true

# copy DNS resolution
cp /etc/resolv.conf "$MOUNT_DIR/etc/resolv.conf" 2>/dev/null || true

# ── Step 4: Strip GNOME bloat ────────────────────────────────────────
log "Stripping GNOME desktop and bloat packages..."
chroot "$MOUNT_DIR" /bin/bash -c '
export DEBIAN_FRONTEND=noninteractive

# Remove GNOME desktop environment
apt-get remove --purge -y \
    gnome-shell gnome-session gnome-session-bin gnome-control-center \
    gnome-terminal gnome-terminal-data gnome-system-monitor \
    gnome-screenshot gnome-disk-utility gnome-bluetooth \
    gnome-online-accounts gnome-weather gnome-calendar gnome-clocks \
    gnome-calculator gnome-characters gnome-font-viewer gnome-maps \
    gnome-music gnome-photos gnome-contacts gnome-text-editor \
    gnome-tweaks gnome-shell-extensions gnome-shell-extension-prefs \
    gnome-software gnome-remote-desktop gnome-power-manager \
    mutter nautilus nautilus-extension-gnome-terminal \
    evince eog totem totem-plugins cheese baobab \
    rhythmbox shotwell simple-scan yelp remmina file-roller \
    gedit seahorse aisleriot gnome-mines gnome-sudoku gnome-mahjongg \
    transmission-gtk usb-creator-gtk ubuntu-desktop ubuntu-desktop-minimal \
    ubuntu-gnome-desktop ubuntu-session yaru-theme-gnome-shell \
    2>/dev/null || true

# Remove snap
systemctl disable --now snapd.service snapd.socket 2>/dev/null || true
apt-get remove --purge -y snapd 2>/dev/null || true
rm -rf /snap /var/snap /var/lib/snapd /var/cache/snapd 2>/dev/null || true

# Remove server bloat
apt-get remove --purge -y \
    docker.io containerd \
    samba samba-common samba-common-bin samba-libs samba-vfs-modules \
    cups cups-daemon cups-client cups-common \
    avahi-daemon avahi-autoipd avahi-utils \
    openvpn network-manager-openvpn \
    syncthing dnsmasq \
    chromium-bsu chromium-bsu-data \
    thunderbird libreoffice-core \
    2>/dev/null || true

# Autoremove orphaned deps
apt-get autoremove --purge -y 2>/dev/null || true
apt-get clean

# Remove leftover GNOME config
rm -rf /usr/share/gnome-shell 2>/dev/null || true
rm -rf /usr/share/gnome 2>/dev/null || true

echo "STRIP_DONE"
'

# ── Step 5: Install Lumo build dependencies ──────────────────────────
log "Installing runtime dependencies..."
chroot "$MOUNT_DIR" /bin/bash -c '
export DEBIAN_FRONTEND=noninteractive
apt-get update -y

# Fix any broken deps from GNOME removal
apt-get install -f -y 2>/dev/null || true

# Install runtime deps (not build deps — we use pre-built binaries)
apt-get install -y --no-install-recommends \
    gdm3 \
    libwayland-server0 libwayland-client0 \
    libwlroots-0.18-0 libxkbcommon0 libpixman-1-0 \
    libdrm2 libinput10 libseat1 \
    libpng16-16t64 libjpeg-turbo8 libcurl4t64 \
    libsystemd0 libgdk-pixbuf-2.0-0 \
    alsa-utils \
    network-manager wireless-tools wpasupplicant \
    plymouth \
    fonts-ubuntu \
    ssh \
    2>/dev/null || true

# Optional: build tools for users who want to rebuild Lumo
apt-get install -y --no-install-recommends \
    build-essential meson ninja-build pkg-config git \
    2>/dev/null || true

apt-get clean
echo "DEPS_DONE"
'

# ── Step 6: Install pre-built Lumo binaries ──────────────────────────
log "Installing pre-built Lumo binaries..."
BINARIES_DIR="/tmp/lumo-binaries"
if [[ -d "$BINARIES_DIR" ]] && [[ -f "$BINARIES_DIR/lumo-compositor" ]]; then
    install -m 755 "$BINARIES_DIR/lumo-compositor" "$MOUNT_DIR/usr/local/bin/"
    install -m 755 "$BINARIES_DIR/lumo-shell" "$MOUNT_DIR/usr/local/bin/"
    install -m 755 "$BINARIES_DIR/lumo-app" "$MOUNT_DIR/usr/local/bin/"
    install -m 755 "$BINARIES_DIR/lumo-screenshot" "$MOUNT_DIR/usr/local/bin/"
    install -m 755 "$BINARIES_DIR/lumo-browser" "$MOUNT_DIR/usr/local/bin/" 2>/dev/null || true
    install -m 755 "$BINARIES_DIR/lumo-webview" "$MOUNT_DIR/usr/local/bin/" 2>/dev/null || true
    install -m 755 "$BINARIES_DIR/lumo-session.sh" "$MOUNT_DIR/usr/local/bin/" 2>/dev/null || true
    install -m 755 "$BINARIES_DIR/lumo-preload.sh" "$MOUNT_DIR/usr/local/bin/" 2>/dev/null || true
    mkdir -p "$MOUNT_DIR/usr/local/share/wayland-sessions"
    install -m 644 "$BINARIES_DIR/lumo.desktop" "$MOUNT_DIR/usr/local/share/wayland-sessions/" 2>/dev/null || true
    install -m 644 "$BINARIES_DIR/lumo-headless.desktop" "$MOUNT_DIR/usr/local/share/wayland-sessions/" 2>/dev/null || true
    mkdir -p "$MOUNT_DIR/etc/udev/rules.d"
    install -m 644 "$BINARIES_DIR/99-lumo-orangepi-touch.rules" "$MOUNT_DIR/etc/udev/rules.d/" 2>/dev/null || true
    log "Installed Lumo binaries from $BINARIES_DIR"
else
    log "WARNING: Pre-built binaries not found at $BINARIES_DIR"
    log "  Build on OrangePi first, then copy to /tmp/lumo-binaries/"
    log "  Continuing without Lumo binaries..."
fi

# Also copy Lumo source for users who want to rebuild
log "Copying Lumo source into image..."
mkdir -p "$MOUNT_DIR/home/lumo/Lumo-Compositor"
rsync -a --exclude='build/' --exclude='builddir/' --exclude='.git/' \
    --exclude='*.o' --exclude='*.a' --exclude='chromium/' \
    "$REPO_DIR/" "$MOUNT_DIR/home/lumo/Lumo-Compositor/"

# ── Step 7: Configure system ─────────────────────────────────────────
log "Configuring Lumo OS system..."

# GDM auto-login (will be overridden by first-boot wizard)
cat > "$MOUNT_DIR/etc/gdm3/custom.conf" << 'GDMCONF'
[daemon]
AutomaticLoginEnable = false
WaylandEnable = true
DefaultSession = lumo.desktop

[security]

[xdmcp]

[chooser]

[debug]
GDMCONF

# Environment variables for GPU compositing
cat >> "$MOUNT_DIR/etc/environment" << 'ENVCONF'
WLR_RENDERER=gles2
WLR_RENDER_DRM_DEVICE=/dev/dri/renderD128
ENVCONF

# System tuning
cat > "$MOUNT_DIR/etc/sysctl.d/99-lumo.conf" << 'SYSCTL'
vm.swappiness = 10
vm.vfs_cache_pressure = 50
SYSCTL

# Disable unnecessary services
chroot "$MOUNT_DIR" /bin/bash -c '
for svc in snapd.service docker.service containerd.service \
    cups.service smbd.service nmbd.service samba-ad-dc.service \
    avahi-daemon.service openvpn.service dnsmasq.service \
    adbd.service accounts-daemon.service unattended-upgrades.service \
    anacron.service sysstat.service camera.service multimedia.service \
    jpu.service; do
    systemctl disable "$svc" 2>/dev/null || true
done
'

# ── Step 8: Create first-boot setup service ──────────────────────────
log "Installing first-boot setup..."

cat > "$MOUNT_DIR/usr/local/bin/lumo-first-boot" << 'FIRSTBOOT'
#!/bin/bash
# Lumo OS First Boot Setup
# Creates user account, sets timezone, configures auto-login

SETUP_FLAG="/etc/lumo-setup-complete"
if [[ -f "$SETUP_FLAG" ]]; then
    exit 0
fi

# Wait for display
sleep 2

# Default values
DEFAULT_USER="lumo"
DEFAULT_PASS="lumo"
DEFAULT_TZ="America/New_York"
DEFAULT_HOSTNAME="lumo-rv2"

# Create user
if ! id "$DEFAULT_USER" &>/dev/null; then
    useradd -m -s /bin/bash -G sudo,video,audio,input,render "$DEFAULT_USER"
    echo "$DEFAULT_USER:$DEFAULT_PASS" | chpasswd
    echo "$DEFAULT_USER ALL=(ALL) NOPASSWD: ALL" > "/etc/sudoers.d/$DEFAULT_USER"
    chmod 440 "/etc/sudoers.d/$DEFAULT_USER"
fi

# Set timezone
timedatectl set-timezone "$DEFAULT_TZ" 2>/dev/null || true

# Set hostname
hostnamectl set-hostname "$DEFAULT_HOSTNAME" 2>/dev/null || true

# Enable GDM auto-login for new user
cat > /etc/gdm3/custom.conf << EOF
[daemon]
AutomaticLoginEnable = true
AutomaticLogin = $DEFAULT_USER
WaylandEnable = true
DefaultSession = lumo.desktop

[security]

[xdmcp]

[chooser]

[debug]
EOF

# Copy Lumo config to new user home
mkdir -p "/home/$DEFAULT_USER/.config/lumo"
chown -R "$DEFAULT_USER:$DEFAULT_USER" "/home/$DEFAULT_USER"

# Mark setup complete
touch "$SETUP_FLAG"

# Restart GDM to apply auto-login
systemctl restart gdm
FIRSTBOOT
chmod +x "$MOUNT_DIR/usr/local/bin/lumo-first-boot"

# Systemd service for first boot
cat > "$MOUNT_DIR/etc/systemd/system/lumo-first-boot.service" << 'UNIT'
[Unit]
Description=Lumo OS First Boot Setup
After=network.target gdm.service
ConditionPathExists=!/etc/lumo-setup-complete

[Service]
Type=oneshot
ExecStart=/usr/local/bin/lumo-first-boot
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
UNIT

chroot "$MOUNT_DIR" systemctl enable lumo-first-boot.service 2>/dev/null || true

# ── Step 9: Set OS branding ──────────────────────────────────────────
log "Setting Lumo OS branding..."

cat > "$MOUNT_DIR/etc/os-release" << 'OSRELEASE'
PRETTY_NAME="Lumo OS 0.0.80 (Noble)"
NAME="Lumo OS"
VERSION_ID="0.0.80"
VERSION="0.0.80 (Noble)"
VERSION_CODENAME=noble
ID=lumo
ID_LIKE=ubuntu
HOME_URL="https://github.com/Night-Traders-Dev/Lumo-Compositor"
BUG_REPORT_URL="https://github.com/Night-Traders-Dev/Lumo-Compositor/issues"
OSRELEASE

# ── Step 10: Cleanup ─────────────────────────────────────────────────
log "Cleaning up..."
rm -rf "$MOUNT_DIR/var/cache/apt/archives/"*.deb 2>/dev/null || true
rm -rf "$MOUNT_DIR/tmp/"* 2>/dev/null || true

# Remove qemu from image
rm -f "$MOUNT_DIR/usr/bin/qemu-riscv64" 2>/dev/null || true

# ── Step 11: Unmount ─────────────────────────────────────────────────
log "Unmounting image..."
sync
umount "$MOUNT_DIR/dev/pts" 2>/dev/null || true
umount "$MOUNT_DIR/dev" 2>/dev/null || true
umount "$MOUNT_DIR/sys" 2>/dev/null || true
umount "$MOUNT_DIR/proc" 2>/dev/null || true
umount "$MOUNT_DIR"
losetup -d "$LOOP"
rmdir "$MOUNT_DIR" 2>/dev/null || true

# ── Step 12: Compress ────────────────────────────────────────────────
log "Compressing image..."
if command -v xz &>/dev/null; then
    xz -9 -T0 -k "$OUT_IMG"
    log "Compressed: $(du -h "${OUT_IMG}.xz" | cut -f1)"
fi

sha256sum "$OUT_IMG" > "${OUT_IMG}.sha256"

log ""
log "═══════════════════════════════════════════════════"
log "  Lumo OS image built successfully!"
log "  Image: $OUT_IMG"
log "  Size:  $(du -h "$OUT_IMG" | cut -f1)"
log ""
log "  Default login: lumo / lumo"
log "  Flash with:"
log "    sudo dd if=$OUT_IMG of=/dev/sdX bs=4M status=progress"
log "═══════════════════════════════════════════════════"
