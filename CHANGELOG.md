# Changelog

All notable changes to this project will be documented in this file.

## [0.0.1] - 2026-03-26
- Established the compositor repository structure.
- Added the initial compositor scaffold and architecture docs.
- Set the project version baseline to `0.0.1`.

## [0.0.2] - 2026-03-26
- Added the first mobile-first input pipeline: touch buffering, edge gesture capture, replay-on-release, and shell-vs-app surface classification.
- Wired the compositor for pointer, keyboard, touch, and gesture signals with wlroots seat integration.
- Added a C test harness for compositor state helpers and a repository-level `.gitignore`.

## [0.0.3] - 2026-03-26
- Added build-time generation for the wlroots XDG shell and layer-shell protocol headers.
- Vendored the layer-shell XML needed to keep the compositor build self-contained.
- Kept the compositor moving toward a touch-first shell with input-method and overlay support.

## [0.0.4] - 2026-03-26
- Added xWayland startup, DISPLAY export, and seat hookup so X11 clients can run beside native Wayland apps.
- Added scene-backed xWayland surface wrappers so X11 windows participate in rendering and hit testing.
- Kept the compositor mobile-first while preserving the Wayland and xWayland split for future shell work.

## [0.0.5] - 2026-03-26
- Added a repo-level README for the renamed Lumo project.
- Documented the touch-first, Wayland, and xWayland direction for the OrangePi RV2 test device.
- Kept the build and runtime entry points easy to find while the compositor and shell continue to evolve.

## [0.0.6] - 2026-03-26
- Added xWayland workarea synchronization from the compositor's usable output geometry.
- Added a focus helper so xWayland surfaces can be activated through the same input path as native Wayland surfaces.
- Added tests for xWayland workarea collection so the policy can be debugged without a live display.

## [0.0.7] - 2026-03-26
- Added a C `lumo-shell` client scaffold for launcher, on-screen keyboard, and gesture surfaces.
- Added pure shell layout helpers so surface geometry and reserved zones can be tested without a compositor.
- Added shell UI tests and protocol generation for the layer-shell client path.

## [0.0.8] - 2026-03-26
- Added compositor-owned shell hitboxes for launcher scrims, OSK zones, and the bottom gesture strip.
- Refined touch capture so reserved shell regions no longer fall through to app surfaces or launcher gestures.
- Added tests that verify shell hitbox refresh and hitbox prioritization on the compositor side.
