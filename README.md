# Lumo

Lumo is a touch-first Wayland compositor and shell stack for small mobile-style devices.

The current target is an OrangePi RV2 driving a 7 inch touchscreen, so the design
leans toward:

- touch and gesture-first input
- large hit targets and reserved edge zones
- screen rotation and coordinate mapping
- native Wayland apps plus optional xWayland compatibility
- compositor-owned state for launcher, OSK, scrim, and focus handling

## Current Layout

- `compositor/` contains the wlroots-based compositor core in C.
  The compositor sources are grouped by category under `src/core`, `src/protocol`,
  `src/shell`, and `src/tools`, with matching grouped test directories.
- `docs/` contains the architecture and migration notes.
- `Design.md` captures the touch-first shell direction and visual language.
- `CHANGELOG.md` tracks semver-style progress starting at `0.0.1`.

The compositor module already has a working scaffold for:

- backend startup and shutdown
- output management and rotation
- touch-first input routing
- layer-shell support
- text input and input-method hooks
- xWayland startup and basic window management
- a companion `lumo-shell` client scaffold for launcher, OSK, and gesture surfaces
- compositor-owned shell hitboxes for scrims, OSK zones, and edge gestures

## Build

The easiest way to build Lumo is with the repo-root wrapper:

```sh
./build.sh
```

That script wraps the Meson build in `compositor/` and exposes the common
knobs we use today:

```sh
./build.sh --xwayland disabled
./build.sh --buildtype debugoptimized --test
./build.sh --build-dir build-noxwayland --xwayland disabled --test
```

If you prefer to run Meson directly, the underlying build still works from the
`compositor/` subdirectory:

```sh
cd compositor
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

## Install

The compositor build installs the Wayland session entry plus the compositor and
shell binaries side by side:

```sh
meson install -C compositor/build
```

That installs both `lumo.desktop` and `lumo-headless.desktop` into the Wayland
session directory and points the login screen at the right startup mode for
each session.
The normal session launches `lumo-compositor --backend drm --shell lumo-shell`.
When the session is started by GDM, the compositor can still use DRM even if
there is no controlling tty in the process.
The debug session launches `lumo-compositor --backend headless --debug --session
lumo-headless --socket lumo-shell-headless --shell lumo-shell`.
In both cases, the compositor still owns shell startup, so the bundled launcher,
OSK, and gesture surfaces come up automatically after login or launch.
When the OrangePi touch-quirk install is enabled, Lumo also installs a narrow
udev override that resets the known `hotlotus wcidtest` panel to an identity
libinput calibration matrix. That keeps old panel-rotation scripts from
fighting Lumo's own compositor-driven rotation and touch mapping.
Use `sudo` for the install step if your prefix points at a system directory
like `/usr` or `/usr/local`.

## Run

The compositor binaries land in `compositor/build/` by default:

```sh
./compositor/build/lumo-compositor --help
./compositor/build/lumo-compositor --debug
./compositor/build/lumo-compositor --rotation 180
./compositor/build/lumo-compositor --backend drm
./compositor/build/lumo-compositor --backend headless
./compositor/build/lumo-shell --mode launcher
./compositor/build/lumo-shell --mode osk
./compositor/build/lumo-shell --mode gesture
./compositor/build/lumo-screenshot --output live.png
```

On the OrangePi RV2 running Ubuntu 24.04 riscv64, `--backend drm` is the
normal direct-to-display path and the normal GDM login-session path. Use
`--backend headless`, `--backend wayland`, or `--backend x11` when you want to
debug nested sessions or isolate backend bring-up issues from SSH or another
remote shell.
In Lumo, explicit DRM mode maps to wlroots `libinput,drm` so direct-display
sessions still bring up touchscreen and keyboard devices alongside the scanout
backend.
Touch rotation is corrected dynamically from the active output transform inside
the compositor, so rotated displays should not need a global 180-degree
libinput flip rule to keep touch and scanout aligned.
`--backend drm` is still a bad fit for SSH and other non-seat shells, but it no
longer requires a visible tty when a display manager is launching the session.
When you leave the compositor in `--backend auto` and launch it from SSH or
another non-VT shell, Lumo now picks the safest available nested backend
instead of waiting on DRM to time out.
SSH sessions fall back to `headless` first; local GUI shells can still steer
`auto` toward nested `wayland` or `x11` when those are available.
Avoid launching `lumo-shell` directly with `sudo`; the compositor owns shell
startup and the shell expects a normal Wayland runtime.
The `lumo-screenshot` helper connects to the current Wayland session and writes
out a PNG using the compositor's screencopy protocol. It defaults to the
`lumo-shell` socket name, so on the OrangePi you can usually capture the live
session with:

```sh
XDG_RUNTIME_DIR=/run/user/$(id -u) ./compositor/build/lumo-screenshot --output live.png
```

You can also override the socket explicitly:

```sh
./compositor/build/lumo-screenshot --socket lumo-shell --output live.png
```

## Troubleshooting

If the `Lumo` session drops back to GDM, start with the session log that GDM
captures for the compositor:

```sh
journalctl -b --no-pager | grep -iE 'gdm-wayland-session|lumo-compositor|lumo-shell'
```

On the OrangePi RV2, the most useful markers are:

- whether `lumo-compositor` gets as far as `output ...: ready`
- whether the shell bridge starts and `lumo-shell` connects
- whether tapping or dragging the bottom gesture pill leaves a temporary touch
  debug marker on that surface, which tells us the compositor is receiving and
  classifying touchscreen input even if the launcher does not open yet
- whether `lumo-compositor` logs `input: touch ... audit ...` lines with the
  expected raw and logical percentages plus `top-left`, `bottom-center`, and
  similar region names during a live edge or corner sweep
- whether the device still has a leftover `/etc/udev/rules.d/99-7ep-caplcd-touch.rules`
  style calibration file forcing `LIBINPUT_CALIBRATION_MATRIX=-1 0 1 0 -1 1`,
  which can invert the panel even when Lumo's own rotation is correct
- whether the failure happens before or after the first shell surface arrives

If you see the colored launcher tiles and keyboard outline on-screen, the
session has already made it through DRM startup, shell autostart, and initial
layer-shell creation. At that point the remaining bugs are in live rendering or
surface lifecycle handling, not in the GDM session entry.

That split makes it much easier to tell session-manager problems apart from
compositor bring-up bugs.

## Design Notes

Lumo is being built around a few core ideas:

- the compositor owns input arbitration, output transforms, and reserved gesture zones
- the shell/UI is made of separate C clients for launcher, on-screen keyboard, bar, and overlays
- the on-screen keyboard commits text through text-input-v3 when a field is focused, with compositor-managed focus tracking
- xWayland support is optional at build time so minimal images can omit it when needed
- the repo-root `build.sh` script keeps the common Meson options in one place
- the runtime backend mode can be forced for OrangePi RV2 bring-up and nested debugging
- auto backend selection now falls back to nested or headless modes when no local VT is available
- the installed Wayland sessions launch the compositor, and the compositor autostarts the bundled shell clients
- touch hitboxes and OSK behavior need to work well on a compact display, not a desktop monitor
- the shared shell geometry helper keeps compositor hitboxes and shell surfaces aligned

More detailed notes live in:

- `docs/lumo-compositor-architecture.md`
- `docs/lumo-migration-plan.md`
- `compositor/README.md`

## Status

The repository is in active compositor bring-up. The next milestones focus on:

- xWayland focus and workarea policy
- xWayland build toggle and feature gating
- shell/UI clients in C
- compositor-side gesture disambiguation for mobile touch interaction
