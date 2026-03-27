# Lumo

Lumo is a touch-first Wayland compositor and shell stack for small mobile-style devices.

The current target is an OrangePi RV2 driving a 7 inch touchscreen, so the design
leans toward:

- touch and gesture-first input
- large hit targets and reserved edge zones
- screen rotation and coordinate mapping
- native Wayland apps plus xWayland compatibility
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

## Build

The compositor currently builds from the `compositor/` subdirectory with Meson:

```sh
cd compositor
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

## Run

The compositor binary accepts a few useful flags:

```sh
./build/lumo-compositor --help
./build/lumo-compositor --debug
./build/lumo-compositor --rotation 180
```

## Design Notes

Lumo is being built around a few core ideas:

- the compositor owns input arbitration, output transforms, and reserved gesture zones
- the shell/UI is made of separate C clients for launcher, on-screen keyboard, bar, and overlays
- xWayland stays enabled so desktop apps can run beside native Wayland apps
- touch hitboxes and OSK behavior need to work well on a compact display, not a desktop monitor

More detailed notes live in:

- `docs/lumo-compositor-architecture.md`
- `docs/lumo-migration-plan.md`
- `compositor/README.md`

## Status

The repository is in active compositor bring-up. The next milestones focus on:

- xWayland focus and workarea policy
- shell/UI clients in C
- compositor-side gesture disambiguation for mobile touch interaction

