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
- `docs/` contains the architecture and migration notes.
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
```

On the OrangePi RV2 running Ubuntu 24.04 riscv64, `--backend drm` is the
normal direct-to-display path. Use `--backend headless`, `--backend wayland`,
or `--backend x11` when you want to debug nested sessions or isolate backend
bring-up issues.
Avoid launching `lumo-shell` directly with `sudo`; the compositor owns shell
startup and the shell expects a normal Wayland runtime.

## Design Notes

Lumo is being built around a few core ideas:

- the compositor owns input arbitration, output transforms, and reserved gesture zones
- the shell/UI is made of separate C clients for launcher, on-screen keyboard, bar, and overlays
- the on-screen keyboard commits text through text-input-v3 when a field is focused, with compositor-managed focus tracking
- xWayland support is optional at build time so minimal images can omit it when needed
- the repo-root `build.sh` script keeps the common Meson options in one place
- the runtime backend mode can be forced for OrangePi RV2 bring-up and nested debugging
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
