# Changelog

All notable changes to this project will be documented in this file.

## [0.0.24] - 2026-03-27
- Fixed layer-surface teardown so the compositor no longer dereferences wlroots scene helper internals after a shell client disconnects.
- Switched layer-shell configuration from a per-frame path to an event-driven path so Lumo stops flooding shell clients with repeated configure traffic during DRM rendering.
- Added regression coverage for dirty layer configuration state and bumped the compositor version.

## [0.0.23] - 2026-03-27
- Fixed the protocol listener ownership bug so new xdg-shell and layer-shell callbacks recover the real compositor pointer instead of casting the listener through the wrong struct.
- Added regression coverage for protocol listener ownership and tightened the output listener wiring order to avoid startup-time races on fast DRM outputs.
- Documented the login-session troubleshooting path for GDM bring-up and bumped the compositor version.

## [0.0.22] - 2026-03-27
- Fixed the XWayland startup ordering so workarea sync waits for the XWayland `ready` event instead of dereferencing the XCB connection too early.
- Added a compositor readiness flag and unit coverage so the output path can safely call into XWayland during startup without crashing the headless or login-session paths.
- Bumped the compositor version to reflect the XWayland bring-up fix.

## [0.0.21] - 2026-03-27
- Relaxed the DRM session preflight so the normal GDM login-session path can start Lumo even when there is no controlling tty.
- Kept the SSH remote-shell guard in place so explicit DRM still points people toward the nested or headless debug backends.
- Bumped the compositor version to reflect the login-session bring-up fix.

## [0.0.20] - 2026-03-27
- Added a second installable Wayland session for headless debug bring-up with distinct session and socket names.
- Updated the install rules so both the normal `Lumo` session and the debug session are bundled automatically.
- Refreshed the README and compositor notes to explain the two session entries and what each one launches.

## [0.0.19] - 2026-03-27
- Added an installable Wayland session entry so the login screen can start Lumo directly as a selectable session.
- Installed the compositor and shell binaries side by side and pointed the session entry at `lumo-compositor --backend drm --shell lumo-shell`.
- Updated the README and compositor notes to document the login-screen flow and the bundled shell startup path.

## [0.0.18] - 2026-03-27
- Added a runtime preflight for DRM startup so explicit `--backend drm` now fails fast when the compositor is not on a local VT.
- Taught `--backend auto` to prefer nested or headless backends when the session is clearly not a local VT, which makes SSH debugging on the OrangePi RV2 much easier.
- Added unit coverage for the VT-detection and auto-backend selection helpers.
- Updated the README and compositor notes to document the new session-aware backend behavior.

## [0.0.17] - 2026-03-27
- Clarified DRM startup failures so the compositor now points at local VT and seat requirements as well as wlroots access.
- Updated the README and compositor notes to explain why SSH sessions should use nested or headless backends instead of DRM.
- Kept the OrangePi RV2 bring-up flow and backend selector documented together.

## [0.0.16] - 2026-03-27
- Added a runtime `--backend` mode so the compositor can be forced into DRM, nested Wayland, X11, or headless debug paths.
- Added backend helper parsing tests and clearer failure logs for backend bring-up issues.
- Updated the README and compositor notes for the OrangePi RV2 Ubuntu 24.04 riscv64 target.

## [0.0.15] - 2026-03-27
- Added a repo-root `build.sh` wrapper for the common Meson configure, build, and test flow.
- Exposed the xWayland feature toggle, build directory, build type, and test switch through the wrapper.
- Updated the README and compositor notes to document the new build entry point.

## [0.0.14] - 2026-03-27
- Added text-input focus tracking so OSK commits can reach focused app fields.
- Added an OSK text helper and unit tests so shell key indices map to deterministic text.
- Updated the architecture notes and README to reflect text-input-based mobile typing.

## [0.0.13] - 2026-03-27
- Added a Meson feature flag so xWayland support can be enabled or disabled at build time.
- Added xWayland no-op stubs for disabled builds and a test that exercises the toggle path.
- Updated the root README and compositor notes to document the build switch.

## [0.0.12] - 2026-03-27
- Added a framed bidirectional shell protocol with typed events, requests, responses, and parser helpers.
- Wired launcher activation requests from the shell client back into the compositor so touch selections can drive compositor state.
- Added protocol roundtrip tests so the compositor and shell share one debuggable wire format.

## [0.0.11] - 2026-03-27
- Added a compositor-to-shell state socket so shell clients can receive launcher, keyboard, scrim, rotation, and gesture updates.
- Added line-format helpers and socket path helpers for the bridge so the protocol is easy to inspect and unit test.
- Tied the shell client's render state to compositor broadcasts so the UI reflects live compositor state changes.

## [0.0.10] - 2026-03-27
- Added compositor-resolved shell hitboxes and redraw-aware launch/keyboard gesture highlighting.
- Added pointer and touch handling to the C shell client so launcher and OSK surfaces react to real input.
- Added pure shell layout helpers for launcher tiles, OSK keys, and gesture hit targets.

## [0.0.9] - 2026-03-27
- Added compositor-managed autostart for the shell clients.
- Added shell binary path resolution and argv construction helpers for easier debugging and testing.
- Added supervised shell process shutdown so the compositor can cleanly reap its child clients.

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
