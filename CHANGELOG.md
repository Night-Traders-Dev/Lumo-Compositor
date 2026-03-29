# Changelog

All notable changes to this project will be documented in this file.

## [0.0.56] - 2026-03-29

- OSK virtual keyboard fallback: when text-input-v3 fails (race condition), OSK keys are sent as real wl_keyboard key events via wlr_seat_keyboard_notify_key(). Terminal, notes, and all apps now receive OSK input reliably.
- Functional search bar in app drawer: tap "TYPE TO SEARCH..." to activate OSK, type to filter apps by name (case-insensitive substring match), backspace removes characters, search clears when drawer closes.
- GNOME 3.x style app drawer: 4x3 grid with centered icons, translucent overlay background, search bar at top, adaptive spacing.
- Functional browser (lumo-browser): standalone GTK4+WebKitGTK 6.0 Wayland client with local start page, URL bar with smart input (auto-https, localhost→http, percent-encoded search), back/forward/reload. Falls back to stub if WebKitGTK unavailable.
- Functional Photos app: PNG/JPEG decoding via libpng/libjpeg, fullscreen viewer with aspect-fit scaling, 3-column grid with scroll, tap-to-select then tap-to-view.
- Functional Clock app: 4 tabs (Clock, Alarm, Stopwatch, Timer) with persistent settings via ~/.lumo-clock.
- Functional Notes app: three-state editing (select→edit→done), blinking cursor, OSK text input via virtual keyboard, backspace support, persistence via ~/.lumo-notes.
- Fixed input bleed-through: edge tap replays are dropped when a toplevel is focused and the launcher is hidden, preventing tiles behind the active app from receiving accidental touches.
- Fixed browser binary fallback: launcher tries sibling builddir path, PATH lookup, then lumo-app stub.
- Fixed browser URL handling: localhost gets http:// prefix, search queries are percent-encoded.
- Fixed close_focused_app: fallback closes first toplevel when no surface has keyboard focus (GTK fullscreen apps).
- Fixed weather fetch: poll timeout reduced from 5s to 500ms, weather timer cleaned up on shell stop.
- Fixed hitbox leak: hitboxes cleared before protocol_started early-return check.
- Fixed gesture zone: increased to 48-80px height for more reliable bottom-edge swipes.
- Removed dead code: close button renderer emptied, unused variables suppressed.

## [0.0.55] - 2026-03-29

- Functional Music app: scans ~/Music for audio files (.mp3/.wav/.ogg/.flac/.m4a), track list with selection, play/pause via mpv, animated progress bar.
- Functional Photos app: scans ~/Pictures for images (.jpg/.png/.bmp/.gif/.webp), 3-column grid with real thumbnails, selection highlight, info bar.
- Photos thumbnails: the grid now renders real decoded thumbnails for PNG, JPEG, BMP, GIF, and WebP images instead of hash-colored placeholders, and the viewer falls back to a generic decoder for non-PNG/JPEG formats.
- Functional Videos app: scans ~/Videos for video files (.mp4/.mkv/.avi/.mov/.webm), preview area with play button, library list, playback via mpv.
- Improved swipe gesture reliability: added velocity-based detection (800px/s triggers regardless of distance). Disabled immediate launcher toggle on gesture handle — now uses capture+swipe for both tap (launcher toggle) and swipe (app close).
- Terminal menu: tap "LUMO TERMINAL" title for centered fullscreen menu with New, Keyboard, Settings, About items.
- OSK auto-show: keyboard appears automatically when terminal or notes apps get focus, with `keyboard_auto_shown` flag preventing `refresh_keyboard_visibility` from undoing the show.
- Fixed Notes OSK gating: Notes now only enables text-input while a note is actively being edited, so the keyboard no longer appears in a non-editable state and OSK commits always target real note edits.
- Volume/brightness sliders: switched from ALSA amixer to PipeWire pactl to match GDM system volume. Removed toast from slider changes to prevent launcher surface redraw freeze.
- Fixed stale header directory on OrangePi (`compositor/lumo/`) that caused build failures by shadowing `compositor/include/lumo/`.
- Shell startup reliability: state socket connect retries up to 5 times with 200ms delays.
- Removed unused functions (lumo_u32_max, lumo_ease_out_cubic, lumo_refresh_vol_bright).

## [0.0.54] - 2026-03-28

- Dynamic theme system: all UI elements (status bar, panels, drawer, app backgrounds) now shift with time-of-day and weather conditions using blended Ubuntu/Sailfish/webOS palettes.
- Apps receive theme via `lumo_app_theme_get()` — terminal, stub apps, and background all follow the dynamic palette.
- Terminal cursor now blinks (500ms on/off cycle).
- OSK trigger on touch: terminal and notes apps enable text-input on first tap if the enter event was missed at startup.
- Volume and brightness sliders in quick settings panel with live readback from ALSA and sysfs backlight.
- Toast notification system: Android-style pills render below status bar with fade-in/out animation.
- Weather panel shows temperature (Fahrenheit), condition, humidity, and wind speed.
- Fixed critical bug: empty toast_msg field broke protocol token validator, silently aborting ALL state broadcasts to shell clients (no panels or drawer would show).
- Fixed replay coordinate bug: touches replayed from OSK bootstrap surface had (0,0) coordinates instead of actual tap position.
- Fixed bottom-edge swipe: now closes focused app when no launcher is visible (iOS/Android behavior).
- Material Design animation curves: standard ease-in-out for show, decelerate for hide (350ms/250ms launcher, 300ms/200ms OSK).
- Keyboard only shows when text-input-v3 is explicitly enabled (not for every toplevel focus).
- Version header file (version.h) as single source of truth for version string across all binaries.
- Boot chime plays two-tone C5/E5 WAV at shell startup.
- Live device test harness with touch injection and log verification.
- HID multitouch driver rebind workaround for USB touchscreen firmware freezes.

## [0.0.52] - 2026-03-28

- Full codebase security and stability audit: fixed 26 issues across critical, high, medium, and low severity.
- Fixed critical use-after-free: touch points with destroyed surfaces are now always freed instead of left as zombies.
- Fixed division-by-zero crash in touch coordinate transform for degenerate output dimensions.
- Fixed duplicate LUMO_SHELL_MODE_BACKGROUND check in animation state machine (copy-paste bug).
- Fixed SHM buffer use-after-free when allocation fails during force-recycle.
- Removed hardcoded debug touch log file (/home/orangepi/lumo-touch.log) that caused disk I/O on every touch event.
- Fixed socket path null-termination in sd_notify to prevent buffer overrun.
- Added zombie process reaping for app-launch children (previously only tracked shell children).
- Added PTY write error checking in terminal app.
- Added hitbox coordinate guards to prevent signed/unsigned wraparound on small displays.
- Added unsigned-wrap protection in OSK key rect calculation for tiny displays.
- Added directory traversal validation (reject `..` entries) in the file browser.
- Fixed dangling output pointers during compositor shutdown.
- Added weather-based dynamic theme: compositor fetches weather for ZIP 41101 every 5 minutes via wttr.in.
- Background hue now shifts based on weather (clear/cloudy/rain/storm/snow/fog) combined with time-of-day.
- Weather data broadcast to all shell clients via the bridge protocol.
- Added boot chime: two-tone C5/E5 WAV generated and played via aplay at shell startup.
- Restyled OSK to match ubuntu-frame-osk (Lomiri): dark charcoal keys, orange enter, subtle grab handle.
- Added shift and backspace keys to OSK layout (33 keys, up from 31).
- Added real PTY terminal backend: terminal app now executes /bin/sh via forkpty() with live output.
- Added text-input-v3 client support for OSK text delivery to apps.
- Fixed OSK not appearing: removed keyboard visibility race conditions, disabled OSK scene node when hidden, raised to top when visible.
- Fixed app drawer tile taps not working due to invisible OSK surface intercepting wlr_scene_node_at.
- Removed close button from all apps (use bottom-edge swipe gesture to dismiss).
- Reordered touch dispatch: hitbox checks (edges, gestures, OSK) now run before shell surface redirect.
- Added 17 new bitmap font glyphs (., !, (, ), %, @, #, _, >, <, =, *, ~, $, &, [, ]).
- Added protocol line-overflow logging for DoS detection.
- Added weather fetch timeout (5s poll) to prevent compositor stall on slow networks.
- New fuzz/stress test suite: protocol parsing with garbage/oversized input, degenerate coordinate transforms, extreme layout dimensions, all-app rendering, type coercion checks, hitbox helpers.
- Version bumped to 0.0.52 with 5 test suites (compositor, shell, app, screenshot, fuzz).

## [0.0.51] - 2026-03-27
- Reduced compositor idle CPU from 26% to ~10% by slowing the animated background refresh from 500ms to 2s and caching gradient rows.
- Fixed OSK activation: touches on app toplevels now correctly focus the surface and auto-show the on-screen keyboard, resolving the scene-tree ordering bug where the invisible OSK layer surface intercepted touches.
- Fixed WiFi signal bars to dynamically update based on actual signal strength from /proc/net/wireless, handling both link quality and dBm formats.
- Fixed OSK keyboard layout: corrected minimum width check for the 5-key bottom row, added overflow guard for narrow screens, and added INT_MAX safety for rect coordinate casting.
- Fixed `lumo_rect_contains` to cast coordinates to double before addition to prevent signed integer overflow.
- Removed close button and accent bar from the app drawer for a cleaner look.
- Redesigned gesture pill as a subtle opacity gradient in warm grey (webOS style).
- Redesigned app close button as a minimal circular X icon with orange highlight.
- Tightened app drawer tile layout with smaller insets and gaps for better fit on all screen sizes.
- Added functional terminal app with keyboard input handling (letters, numbers, backspace, enter, command history).
- Added persistent notes (saved to ~/.lumo-notes).
- Added file sizes (B/K/M) to the file manager via stat().
- Added rotation persistence across reboots via ~/.lumo-rotation.
- Rebuilt Settings app with 8 categorized rows: Network, Display, Storage, Memory, System, About, Lumo, and CPU.
- Fixed panel tap bleed-through: tapping in panel area no longer accidentally launches app tiles behind it.
- Fixed app drawer flash on panel close with instant hide instead of animation.
- Slowed animated background to reduce scene damage frequency.
- Applied shell_osk.c header fixes (limits.h, stdint.h).
- No NPU available on OrangePi RV2 (SpacemiT K1/X1 SoC).

## [0.0.50] - 2026-03-27
- Switched the entire shell to Ubuntu's stock color palette: aubergine backgrounds, orange accents, warm grey secondary text, and white primary text.
- Removed the gesture pill touch debug overlay so the bottom handle is a clean orange bar with no diagnostic dots or crosshairs.
- Added a procedurally generated animated background that adapts its hue to the time of day: warm sunrise mornings, standard midday, deep evening reds, and dark night purples.
- Added a time/date panel triggered by swiping down from the top-left edge, showing a large clock, full date, day name, and week number.
- Added RELOAD and ROTATE buttons to the quick settings panel for session restart and screen rotation cycling.
- Moved panel rendering from the status bar surface to the launcher overlay so panels no longer occlude the animated background.
- Fixed touch on launcher tiles and panel buttons by replaying scrim-captured taps to the overlay layer surface.
- Fixed screen rotation touch mapping with correct inverse transform for 90, 180, and 270 degree rotations.
- Registered a compositor-owned top-edge hitbox so top-edge gestures work even when apps are focused.
- Modularized native apps into separate source files: app_clock.c, app_files.c, app_settings.c, app_notes.c with a shared app_render.h API.
- Made Clock app live-updating with seconds display and interactive stopwatch (tap to start/stop, tap to reset).
- Made Files app navigable with tap-to-enter folders, tap path bar to go up, swipe-to-scroll, file selection highlight, and storage info.
- Made Settings app show live system data: hostname, kernel, uptime, memory, WiFi status with 5-second refresh.
- Added Notes app with add/select touch interaction.
- Implemented double-buffered SHM rendering for shell clients to eliminate per-frame mmap/munmap overhead.
- Implemented broadcast coalescing via dirty flag so rapid state changes produce one broadcast per output frame instead of one per change.
- Pre-format the state broadcast buffer once and send the same bytes to all bridge clients.
- Replaced the blocking popen(nmcli) call in Settings with instant /proc/net/wireless reads.
- Fixed protocol frame field limit from 24 to 32 to accommodate quick_settings_visible and time_panel_visible state fields.
- Added digits 0-9, colon, dash, slash, and plus to the shell client bitmap font.
- Bumped process array from 4 to 5 for the background shell client.

## [0.0.48] - 2026-03-27
- Added a visible clock to the status bar rendered at scale 3 in the center, with WiFi signal bars on the right side and LUMO branding on the left.
- Added a quick settings panel triggered by dragging down from the top-right edge of the screen, showing WiFi status, display rotation, session info, and device name.
- Wired the top-right edge zone to toggle quick settings while keeping the top-left debug audit gesture intact, and added left-edge dismiss support for closing the quick settings panel.

## [0.0.47] - 2026-03-27
- Added a compositor-owned bottom-edge close gesture for focused applications, so an upward swipe from the bottom reserve zone now closes both native `lumo-app` clients and regular Wayland or XWayland app toplevels.
- Kept the visible bottom gesture handle on its existing launcher-toggle path, which preserves the quick drawer open or close behavior while reserving full bottom-edge swipes for app dismissal.
- Added regression coverage for the new bottom-edge close policy, the focused-app close helper defaults, and documented the updated mobile gesture behavior.

## [0.0.46] - 2026-03-27
- Made the launcher behave like a proper toggle: the bottom gesture trigger now closes the app drawer when it is already open, and the drawer's new top-corner close control routes through the same shell target path.
- Added a native `lumo-app` Wayland client and wired all 12 launcher tiles to open built-in Lumo apps instead of external desktop applications, giving the app drawer a fully native touch-first launch path.
- Added app-catalog and renderer coverage, integrated the new apps category into the Meson build, and updated the docs to describe the new `src/apps/` runtime slice.

## [0.0.45] - 2026-03-27
- Reduced launcher trigger latency by letting the compositor open the drawer directly from the bottom gesture hitbox before that touch falls through to the gesture shell surface.
- Shortened shell transition timings for the launcher and OSK, and lowered the default gesture timeout from `180ms` to `90ms` so touch response feels more immediate on the OrangePi panel.
- Added regression coverage for the immediate gesture-handle policy and the new shell transition duration helpers.

## [0.0.44] - 2026-03-27
- Fixed launcher reopen churn by basing layer-shell reconfigure decisions on actual committed layout state changes instead of wlroots commit flags, which keeps redraw-only commits from retriggering fresh layout configures.
- Added a compositor-side shell child polling fallback on output frames so a dead launcher client is reaped and respawned even if the `SIGCHLD` path is missed during a live session.
- Kept the local open-close-open launcher harness green without the earlier broken-pipe and configure-flood behavior.

## [0.0.43] - 2026-03-27
- Suppressed duplicate layer-shell configures by caching the last arranged launcher and shell layout snapshot per layer surface and skipping no-op reconfigurations.
- Stopped forcing repeated layer configuration frames while new layer surfaces are waiting for their first commit, which reduces startup and reopen configure churn on slower devices like the OrangePi RV2.
- Kept the local shell reopen harness green with repeated open-close-open cycles while preserving the earlier shell child supervision and launcher recovery work.

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
