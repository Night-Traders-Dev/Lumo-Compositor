# Changelog

All notable changes to this project will be documented in this file.

## [0.0.42] - 2026-03-27
- Added compositor-side shell client supervision so launcher, OSK, gesture, and status processes are reaped and respawned if they exit unexpectedly during a live session.
- Fixed a layer-shell configure loop by only marking layer layout dirty when a commit changes actual layer-shell arrangement state, instead of on every redraw buffer commit.
- Added regression coverage for shell mode slot mapping and the new layer-surface reconfigure policy, and removed the noisy temporary launcher apply log from the shell client.

## [0.0.41] - 2026-03-27
- Fixed the launcher shell bootstrap so the app drawer is created as a full-screen overlay from the start instead of a hidden `1x1` layer surface.
- Fixed the layer arrangement path so newly created layer surfaces are configured immediately instead of being skipped until after initialization.
- Added a regression test for the launcher bootstrap geometry so the drawer no longer depends on a later resize to become visible.

## [0.0.40] - 2026-03-27
- Fixed a layer-shell timing bug where the app drawer could be reconfigured before the launcher client committed its full-screen overlay request, which left it stuck at the hidden `1x1` bootstrap size.
- Added a shared `lumo_protocol_mark_layers_dirty()` path that schedules output frames instead of forcing immediate reconfiguration, and wired layer-surface commits into that dirty path so shell resizes are applied after client commits arrive.

## [0.0.39] - 2026-03-27
- Scheduled explicit output repaints after layer-shell reconfiguration so launcher and audit surface resizes do not depend on incidental DRM frame timing.
- Added shell-side configure logging for launcher, keyboard, gesture, and status clients so live OrangePi sessions now tell us the actual surface size each layer receives during bring-up and visibility changes.

## [0.0.38] - 2026-03-27
- Fixed the app drawer visibility path so launcher, keyboard, and touch-audit state changes now trigger immediate layer-shell reconfiguration instead of leaving clients stuck in their hidden bootstrap size.
- Broadcast scrim-state changes alongside launcher and keyboard visibility updates so shell clients stay visually in sync with compositor modal state.
- Added regression coverage to ensure those shell visibility setters mark the layer configuration dirty path that drives real surface resizing.

## [0.0.37] - 2026-03-27
- Made the top-edge touch-audit gesture debug-only so normal mobile sessions no longer displace the launcher with an accidental calibration overlay.
- Restored an escape path from touch-audit mode by allowing the left-edge dismiss gesture even while audit is active.
- Added regression coverage for the new audit-gesture policy and updated the design and README docs to match the production launcher behavior.

## [0.0.36] - 2026-03-27
- Fixed a touch-path crash where non-surface scene buffers, including the compositor background rect, could be misclassified as real client surfaces during touch hit-testing.
- Unified pointer and touch scene-buffer lookup behind a shared helper so both input paths now agree on what counts as an actual `wlr_surface`.
- Added regression coverage to prove that plain scene rect buffers do not resolve as touchable surfaces.

## [0.0.35] - 2026-03-27
- Added app launching so launcher tiles now spawn real applications: Browser opens Epiphany, Notes opens gnome-text-editor, Files opens Nautilus, and Settings opens gnome-control-center.
- Added a status bar shell surface anchored to the top edge with a live clock, LUMO branding, and rotation indicator.
- Replaced the pure black compositor background with a dark navy scene rect so the idle desktop has depth and matches the shell's dark theme.
- Added systemd ready notification so the compositor signals session readiness to GDM and other service managers.
- Removed the broken `X-GDM-SessionRegisters` flag from session desktop entries so GDM's own wrapper handles session registration.

## [0.0.34] - 2026-03-27
- Added explicit top, left, right, and bottom compositor-owned system edge zones so Lumo now has a fuller mobile gesture map instead of only the bottom launcher path.
- Taught the shell to use tighter input regions that match the visible launcher drawer panel and the gesture pill, which keeps transparent shell surfaces from swallowing unrelated touches.
- Added an in-session touch audit flow with a full-screen overlay, ordered eight-point edge targets, and saved per-device JSON profiles under the user's Lumo config directory.
- Added shared shell geometry helpers and regression coverage for launcher panel layout, audit target layout, edge-zone naming, and the new touch-audit compositor state.

## [0.0.33] - 2026-03-27
- Added explicit touch-audit helpers and live compositor logging for touch-down events so OrangePi debugging now reports raw percentages, logical percentages, output name, edge or corner region, and hitbox or surface classification in one line.
- Added regression coverage for edge and corner region naming so full-screen touch audits stay stable as the gesture and shell hitbox model evolves.
- Updated the troubleshooting docs to point at the richer touch-audit log format for live panel and gesture verification.

## [0.0.32] - 2026-03-27
- Made touch rotation correction explicit and testable in the compositor by factoring the output-transform mapping into a shared helper with regression coverage for normal, 90, 180, and 270 degree layouts.
- Added a bundled OrangePi RV2 touchscreen udev override that resets the known `hotlotus wcidtest` panel to an identity libinput calibration matrix so legacy 180-degree panel scripts do not invert touch under Lumo.
- Extended `build.sh` and the install docs with the new touch-quirk options and documented that Lumo treats compositor rotation, not global touchscreen flip rules, as the source of truth.

## [0.0.31] - 2026-03-27
- Tightened launcher gesture policy around the mobile shell model: the generic launcher edge now only tracks the bottom edge, and bottom-edge taps or swipes can open the launcher even when they miss the small visual pill hitbox.
- Added a compositor helper and regression coverage for launcher-edge touch captures so the bottom-edge gesture path stays testable as we keep refining touchscreen behavior on the OrangePi.

## [0.0.30] - 2026-03-27
- Fixed a compositor startup crash in the direct DRM plus libinput session path by initializing the `input_devices` wl_list before input devices are attached.
- Added regression coverage for the compositor default state so uninitialized input-device list heads are caught during tests.

## [0.0.29] - 2026-03-27
- Fixed direct-display DRM sessions so explicit `--backend drm` now requests `libinput,drm` from wlroots instead of `drm` alone, which restores touchscreen and keyboard devices in the OrangePi login-session path.
- Added backend helper coverage so the wlroots backend environment mapping is tested alongside the existing backend mode parsing logic.
- Updated the runtime docs to explain that direct DRM mode in Lumo includes libinput as part of the explicit backend set.

## [0.0.28] - 2026-03-27
- Taught the compositor input pipeline to treat a short tap on the bottom `shell-gesture` handle as a launcher-open action, so the gesture pill works even before a full edge-swipe gesture is completed.
- Added a temporary compositor-driven touch debug overlay on the gesture surface and expanded shell protocol state so we can see touch position and routing while debugging the OrangePi touchscreen path.
- Added regression coverage for the new touch debug helpers and shell coordinate mapping utilities.

## [0.0.27] - 2026-03-27
- Reorganized the compositor source tree into category directories under `src/core`, `src/protocol`, `src/shell`, and `src/tools`, with matching grouped test directories.
- Kept the new screenshot utility in the tools category and broadened its wl_shm format handling so screencopy works with common XRGB and XBGR-style buffers.
- Updated the compositor docs to reflect the categorized layout and the new screenshot utility path.

## [0.0.26] - 2026-03-27
- Added a compositor-backed screencopy manager and a new `lumo-screenshot` client that captures the active output to PNG for remote review and debugging.
- Added shared screenshot helpers and unit coverage for runtime-dir fallback, socket-name selection, row orientation, and pixel conversion.
- Updated the Meson build and project docs so screenshot capture builds, installs, and runs cleanly beside the compositor and shell binaries.

## [0.0.25] - 2026-03-27
- Reworked the shell toward a mobile UI direction that blends Ubuntu Touch app-drawer and keyboard ergonomics with webOS-inspired surface styling and motion.
- Removed the shell client's hardcoded `1280x720` bootstrap sizing in favor of live layer-shell sizing and compositor-provided output dimensions.
- Hid launcher and OSK surfaces by default, added animated show and hide behavior, and kept the gesture surface as the always-available bottom handle.
- Replaced placeholder launcher blocks and keyboard outlines with labeled launcher tiles, real key legends, and a modularized OSK source file at `compositor/src/shell/shell_osk.c`.
- Wired keyboard visibility to focused and enabled `text-input-v3` state on the compositor side and documented the shell design direction in `Design.md`.

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
