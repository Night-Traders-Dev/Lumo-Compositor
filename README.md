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

The compositor currently builds from the `compositor/` subdirectory with Meson:

```sh
cd compositor
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

To build without xWayland support, pass the Meson feature flag at setup time:

```sh
meson setup build -Dxwayland=disabled
```

## Run

The compositor binary accepts a few useful flags:

```sh
./build/lumo-compositor --help
./build/lumo-compositor --debug
./build/lumo-compositor --rotation 180
./build/lumo-shell --mode launcher
./build/lumo-shell --mode osk
./build/lumo-shell --mode gesture
```

## Design Notes

Lumo is being built around a few core ideas:

- the compositor owns input arbitration, output transforms, and reserved gesture zones
- the shell/UI is made of separate C clients for launcher, on-screen keyboard, bar, and overlays
- the on-screen keyboard commits text through text-input-v3 when a field is focused, with compositor-managed focus tracking
- xWayland support is optional at build time so minimal images can omit it when needed
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
