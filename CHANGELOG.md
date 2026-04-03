# Changelog

All notable changes to this project will be documented in this file.

## [0.0.69] - 2026-04-03

### Sidebar Multitasking Bar (Ubuntu Touch-style)

- Added left-edge sidebar showing running app icons with app drawer button.
- Tap app icon to switch, long-press for Open/Close context menu.
- AI-generated Lumo icon (flowing light ribbons) as app drawer button.
- Sidebar auto-hides after 10 seconds, dismisses on outside tap.
- Theme-matching gradient colors that transition with time-of-day/weather.
- Sidebar renders below status bar to avoid visual overlap.

### Gesture Rework

- **Bottom swipe** → go home (minimize all running apps, not close).
- **Right swipe** → back gesture (minimize focused app).
- **Left swipe** → show sidebar.
- Removed bottom gesture handle pill indicator (invisible edge zone only).
- Gesture handle tap now goes home instead of opening app drawer.

### Performance: Wave Animation CPU 200% → 0%

- Pre-rendered 60-second seamless wave loop into RAM (146 MB, 600 frames at 10fps).
- 2-second crossfade at loop boundary ensures infinite seamless playback.
- Playback is memcpy from pre-rendered buffer — zero wave computation at runtime.
- Background shell CPU dropped from 200% to 0% (was 8 threads computing waves at 30fps).
- Compositor CPU dropped from 92% to 21%.
- Row-skip optimization in composite pass skips rows with no wave glow (~50% of frame).
- Background stops rendering entirely when an app covers it (scrim-aware).

### Smooth Gradient Transitions

- Background gradient smoothly interpolates between hour palettes based on current minute.
- No hard color jumps at hour boundaries — continuous lerp between current and next palette.
- Weather hue shifts applied to both endpoints of the interpolation.

### Half-Resolution Wave Rendering

- Waves computed at 400×640 (quarter pixels) then upscaled 2× onto full-res gradient.
- Soft glow effect masks upscaling artifacts — visually identical to full-res.
- Two-pass pipeline: Pass 0 (wave glow) + Pass 1 (gradient fill + composite).

### Panel Fixes

- Time/date/weather panel centered horizontally under its trigger zone.
- Panel visibility fix: launcher surface now redraws when panels are active.
- Removed NVMe surface cache writes from render loop (was 120 MB/s I/O blocking).

### Boot Splash

- Lumo icon + "LUMO" text + Ubuntu-style three-dot loading indicator during wave prerender.
- Dots cycle through dim aubergine to orange, one at a time.
- Status bar hidden during boot splash via `/tmp/lumo-boot-active` flag.
- All edge gestures blocked until splash completes.
- Version string displayed at screen edge.

### WiFi Signal Bars

- Dynamic wifi signal strength using `iw dev wlan0 link` (fallback for riscv64 kernels without `/proc/net/wireless`).
- Signal refreshed every 10 seconds: >-50dBm=4 bars, >-60=3, >-70=2, >-90=1.
- Falls back to checking interface operstate if iw unavailable.

### Time Panel Sync

- Time panel on launcher surface now redraws periodically when visible, keeping the clock in sync with the status bar.

### 5-Minute Wave Loop

- Extended pre-rendered wave loop from 60 seconds to 5 minutes (1500 frames at 5fps).
- 3-second crossfade at boundary for seamless infinite looping.
- ~366 MB RAM usage (safe for 8GB devices).
- 5fps wave playback — sufficient for slow-moving glow effects.

### Duplicate App Prevention

- Tapping a launcher tile for an already-running app focuses the existing instance.
- Terminal and Browser exempt — allow multiple windows.
- "NEW WINDOW" option in sidebar context menu for terminal/browser (long-press).

### Sidebar Improvements

- Portrait mode: 25% of screen width (200-320px).
- Landscape mode: 10% of screen width (100-160px).
- Context menu repositioned above icon to avoid off-screen rendering.
- App icons show dark purple background with white letter for visibility.
- Running app list uses 80-field protocol (bumped from 48).

### Version

- Bumped to 0.0.69.

## [0.0.68] - 2026-04-03

### Lumo OS Identity

- Branded the system as "Lumo OS 0.0.68" — `/etc/os-release` and `/etc/lsb-release` now show Lumo instead of Ubuntu. Package management (apt) still works via Ubuntu noble repos.
- Created Lumo Plymouth boot theme: aubergine-to-dark gradient background, orange "LUMO" text, version display, status messages during boot, progress bar.
- Theme installed at `/usr/share/plymouth/themes/lumo/` with `plymouthd.conf` set to use it.

### Boot Preloader

- Created `lumo-preload.service` — systemd oneshot that runs before GDM to warm caches:
  - Sets CPU governor to performance on all 8 cores
  - Tunes VM (swappiness=10, vfs_cache_pressure=50)
  - Preloads WebKit and GTK4 shared libraries into page cache
  - Initializes LumoCache directories on NVMe
  - Reads cached surfaces from NVMe into page cache for instant restore
  - Reports progress to Plymouth during boot splash
- Service enabled at `graphical.target` via `Before=gdm.service`.

### LumoCache Active

- Shell surfaces (background, status bar) now write to `/data/lumo-cache/surfaces/` on every render.
- App surfaces cached for instant restore.
- Background: 4MB, Status bar: 153KB cached to NVMe with btrfs zstd compression.
- Settings Memory panel shows per-directory cache stats.

### Version

- Bumped to 0.0.68.

## [0.0.67] - 2026-04-03

### LumoCache — NVMe-Backed Persistent Cache

- Implemented two-tier cache system: hot (tmpfs RAM) + warm (NVMe btrfs with zstd:3 compression).
- Cache directories at `/data/lumo-cache/`: warm, surfaces, webkit, fonts, state, hot (symlink to tmpfs).
- WebKit disk cache now uses NVMe persistent storage — pages cached across reboots, btrfs compression gives ~3:1 ratio on web resources.
- Cache stats visible in Settings Memory panel: per-directory file counts and sizes.
- API: `lumo_cache_init()`, `lumo_cache_put/get()`, `lumo_cache_put/get_surface()`, `lumo_cache_stats()`.
- NVMe benchmarks: 646 MB/s read, 210 MB/s write.

### GPU Investigation Update

- `DRIVER_PRIME` flag doesn't exist in kernel 6.6 — PRIME/dmabuf import is implicit for all DRM drivers.
- GBM fails on the display controller (`card1/ky-drm`) due to Mesa's `dri_gbm.so` backend not supporting this DRM device — NOT a kernel issue.
- GBM works on the PVR GPU (`card0`, `renderD128`) — confirmed via Python test.
- wlroots GLES2 renderer activates on PVR but format negotiation fails between GPU-allocated buffers and display scanout. Investigating copy-back or format override approaches.

### Version

- Bumped to 0.0.67.

## [0.0.66] - 2026-04-03

### GPU Discovery: Imagination PowerVR BXE-2-32

- **Discovered working GPU** on OrangePi RV2: Imagination PowerVR BXE-2-32 with OpenGL ES 3.2, Vulkan 1.3.277, and OpenCL support. The GPU was present with driver loaded (`pvr` kernel module, BVNC 36.29.52.182) but never used because GBM device creation failed on the display DRM node.
- **Root cause identified**: The display controller (`ky-drm-drv`) doesn't support GBM buffer allocation. wlroots creates a GBM device on the display node for scanout buffers, which fails. Setting `WLR_RENDER_DRM_DEVICE=/dev/dri/renderD128` successfully enables the GLES2 renderer but the allocator still fails on the display node.
- **GPU confirmed working**: GBM device creation succeeds on the render node (`/dev/dri/renderD128`). EGL 1.5, GLES 3.2 via Imagination Technologies, Vulkan 1.3 via `libVK_IMG.so`. WebKitGTK can use the GPU independently for page compositing.
- **WebKit GPU acceleration enabled**: Removed `WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER` and `GSK_RENDERER=cairo` from lumo-webview. WebKit now uses hardware acceleration when available.

### RISC-V Vector 1.0 SIMD

- Added RVV 1.0 vectorized pixel fill to both shell (`lumo_fill_span`) and app (`lumo_app_fill_span`) SHM renderers. Uses `vsetvl_e32m2` + `vmv_v_x_u32m2` + `vse32_v_u32m2` for LMUL=2 wide vector stores.
- ISA: `rv64imafdcv` with `zve32x`, `zve64d`, `zvfh`, `zba`, `zbb`, `zbs` — full vector and bit-manipulation extensions.
- Scalar unrolled fallback preserved for non-RVV builds.

### Settings App: Display Info

- Display sub-page now shows dynamic renderer detection (reads compositor journal for renderer type).
- Added GPU info line showing "IMG BXE-2-32 (VULKAN 1.3)" when PowerVR render node is present.
- Renderer field updates automatically: shows "PIXMAN" for software, "GLES2" for GPU when enabled.

### Version

- Bumped to 0.0.66.

## [0.0.65] - 2026-04-03

### Browser Performance Optimization

- **System tuning**: Set CPU governor to `performance`, VM swappiness to 10, VFS cache pressure to 50. Persistent via `/etc/rc.local`. WebKit shared libraries preloaded into page cache at boot.
- **tmpfs disk cache**: WebKit disk cache directed to `/tmp/lumo-webkit-cache` (RAM-speed I/O instead of NVMe). Set via `XDG_CACHE_HOME` environment variable.
- **WebKit settings**: Disabled favicons, media, WebAudio, MediaStream, WebGL. Limited web process count to 1.
- **Fallback chain**: Browser URL launches try lumo-webview → netsurf-gtk → epiphany in order, giving users a fast fallback when WebKit is slow.
- **Warm webview**: Watches `/tmp/lumo-webview-url` for URLs, presents fullscreen when URL received, hides on close instead of terminating (stays warm for next load).

### riscv64 Web Rendering Research

- Benchmarked 4 browser engines on OrangePi RV2:
  - lumo-webview (WebKitGTK): 15s cold start, 37MB RSS, full modern web
  - NetSurf: 8s cold start, 61MB RSS, limited CSS3/JS
  - Dillo: ~2s cold start, ~10MB RSS, basic HTML only
  - Native SHM browser: 1s, 7.5MB RSS, home/bookmarks UI only
- Found WebKit riscv64 LLVM code generation bug (WebKit bug 224134) affecting JavaScript compilation — explains slow JS execution on this platform.
- WPE WebKit (embedded-optimized WebKit) not packaged for riscv64 Ubuntu.
- Servo browser engine has experimental riscv64 support but no stable builds.

### Version

- Bumped to 0.0.65.

## [0.0.64] - 2026-04-02

### Browser Keyboard Input

- Fixed typing not working in the browser URL bar. The OSK sends key events via virtual keyboard (wl_keyboard), but the browser's keyboard handler only processed keys for the terminal app. Added keyboard event handling for the browser: letter/number/symbol keys append to URL bar, backspace deletes, Enter launches the URL, shift toggles uppercase.

### Shell Startup Reliability

- Added 50ms delay + `wl_display_flush_clients()` between each shell client spawn during autostart. This gives the Wayland socket time to process the previous client's connection before spawning the next one, preventing the intermittent startup race where some shell clients failed to register.
- Removed webview pre-warm from compositor boot — it was spawning in the boot sound fork's child process, creating multiple instances and breaking shell initialization.

### Performance Baseline

- Measured on OrangePi RV2 (riscv64, 8GB RAM):
  - Native app launch: ~1000ms to first paint
  - Compositor idle: 26 MB RSS
  - Shell clients (5): ~24 MB total
  - Total Lumo idle: ~50 MB
  - System: 893 MB used / 7836 MB total

### Version

- Bumped to 0.0.64.

## [0.0.63] - 2026-04-02

### OSK Trigger Fix (Definitive)

- Fixed OSK not appearing when tapping the browser URL bar. The root cause was a protocol-level race condition: the compositor's `text_input_commit` event never fired because `wlroots` hadn't sent `text_input_enter` to the surface before the client called `enable()+commit()`. The commit handler checked `current_enabled` which was still false.
- **Fix**: The compositor now shows the keyboard directly on `text_input_enable` instead of waiting for `text_input_commit`. The enable event represents the client's intent to input text and should be sufficient to trigger the OSK.
- This fix applies to ALL apps that use text-input-v3 (Notes, Maps, Browser, Terminal).

### Theme Conformity Audit

- Fixed 2 hardcoded colors that didn't respect the time-of-day theme:
  - Browser URL bar background: hardcoded `#1D0014` → `theme.bg`
  - Settings toggle off-state: hardcoded `(0x5E, 0x3A, 0x56)` → `theme.card_stroke`
- All shell panels (notifications, time/weather, quick settings) already fully theme-compliant.
- Semantic colors (green=call, red=delete, orange=accent) intentionally kept constant.

### Version

- Bumped to 0.0.63.

## [0.0.62] - 2026-04-02

### All-in-One Browser

- Added `lumo-webview` — minimal fullscreen WebKitGTK renderer launched as subprocess when user navigates to a URL from the native SHM browser. No toolbar, just web content. `GSK_RENDERER=cairo` and `GTK_USE_PORTAL=0` pre-set to skip renderer probing.
- Browser now uses `lumo-webview` → `epiphany` → `xdg-open` fallback chain for URL launching.
- Flow: tap URL bar → OSK opens → type URL → press Enter/GO → fullscreen web page appears → swipe gesture handle to close and return to browser home.

### OSK Trigger Fix

- Fixed OSK not appearing when tapping the browser URL bar. The `wl_display_dispatch_pending` call was insufficient — the compositor's `text_input_enter` event hadn't arrived yet. Changed to `wl_display_roundtrip` which blocks until the compositor responds, ensuring the text-input protocol handshake completes before enable+commit.

### Settings Memory Panel

- Added Lumo process memory breakdown to the Settings Memory sub-page. Shows VmRSS (resident memory) for: COMPOSITOR, SHELL (x5), NATIVE APP, WEB VIEW, BROWSER GTK, WEBKIT PROC. Scans `/proc/<pid>/status` for each process. Shows individual and total Lumo memory usage.

### Version

- Bumped to 0.0.62.

## [0.0.61] - 2026-04-02

### Input System Audit — 6 Bug Fixes

- **Bug #1**: Removed duplicate bottom-edge suppression block that had weaker conditions than the primary block, causing inconsistent touch routing when panels were visible.
- **Bug #2**: Fixed fallback touch delivery sending coordinates (0,0) when no surface was hit-tested. Now correctly computes surface-local coordinates from the scene tree node position.
- **Bug #3**: Fixed right-edge gesture (gesture handle) closing an app but not opening the launcher afterward. The early return after `close_focused_app` was removed — launcher now always opens after closing.
- **Bug #4**: Added `wlr_seat_touch_notify_frame()` after `touch_notify_down` in `deliver_now()`. Some Wayland clients batch touch processing and require frame events.
- **Bug #5**: Fixed replay logic gap where captured-but-not-triggered touches were silently dropped when a toplevel was focused but the launcher wasn't visible. Touches are now replayed to the launcher surface when visible.
- Removed all pointer emulation code (was causing GTK4 input conflicts). Native touch events are now the sole input path for all apps.

### Native SHM Browser

- Rewrote browser as a native SHM app (`lumo-app --app browser`) instead of a separate GTK4/WebKitGTK binary. Launches instantly (~2s) like all other Lumo apps.
- Toolbar: Home (H), Bookmark (*), URL bar (tap to search), Go/Reload (GO/R), Back (<), Forward (>).
- Bookmarks persisted to `~/.lumo-browser-bookmarks` with default DuckDuckGo/Wikipedia/GitHub.
- Quick links in editing mode: DuckDuckGo, Wikipedia, GitHub, YouTube.
- OSK triggered when URL bar is tapped for text input.
- URLs resolved via Epiphany (`epiphany-browser`) or `xdg-open` as subprocess.
- GSK_RENDERER=cairo and GTK_USE_PORTAL=0 set for the GTK browser fallback to skip 20s+ renderer probing on riscv64.

### Version

- Bumped to 0.0.61.

## [0.0.60] - 2026-04-02

### Browser Features

- Added find-in-page: "F" button toggles search bar with live incremental search via `WebKitFindController`. Enter key advances to next match. Themed with Lumo purple/orange.
- Added page zoom controls: "A+" and "A-" buttons adjust zoom level 0.5x–3.0x via `webkit_web_view_set_zoom_level`.
- Added target=_blank handling: links that open new windows now open in a new tab via `create` signal → `add_tab`.
- Added download manager: files download to `~/Downloads/` automatically, directory created if missing. Uses `WebKitDownload` decide-destination signal.
- Added TLS error bypass for local network (localhost, 192.168.*, 10.*, 127.0.*) — allows self-signed certs on development servers.
- Added progress bar (themed orange, 3px) between web content and toolbar, auto-hides on load completion.
- Added back/forward button sensitivity — grayed out (opacity 0.3) when navigation unavailable.

### Browser Bug Fixes

- Fixed tab close corrupting GtkStack child names — remaining tabs are properly re-added with sequential names after removal.
- Fixed tab bar button cleanup — now compares widget pointers instead of unreliable `gtk_widget_get_name` type names.
- Shared `WebKitNetworkSession` across all tabs to reduce memory and connection overhead.
- Extracted URL handling into `browser_url.h` shared header for testability.
- Added comprehensive browser test suite (URL resolution, encoding, bookmark persistence, GTK UI components).

### Bottom-Edge Touch Fix

- Fixed app toolbar buttons being captured by the bottom-edge gesture zone. When a toplevel app is focused, bottom-edge system zone touches now fall through to the app surface instead of being captured as gestures. The gesture handle hitbox (`LUMO_HITBOX_EDGE_GESTURE`) still provides swipe-to-close functionality.
- Fixed suppressed edge taps being silently dropped — they are now replayed to the focused app surface so GTK buttons, input fields, and other touch targets in the bottom region of the display receive input.
- This fix applies to ALL apps, not just the browser — any app with interactive elements near the bottom edge of the rotated display will now receive touches correctly.

### Chromium Removal

- Removed Chromium v122 submodule (47GB on host, 20GB on OrangePi) and all cross-compile infrastructure.
- Replaced with custom `lumo-browser` built on WebKitGTK 6.0 + GTK4 — builds natively on device in seconds.
- Installed Epiphany (GNOME Web) as secondary browser.
- Removed `extensions/lumo-theme/` Chrome extension directory.

### Version

- Bumped to 0.0.60. Single source of truth in `compositor/include/lumo/version.h`.

## [0.0.59] - 2026-04-02

### New Native Apps

- **Phone**: Full dialer pad with T9-style digit buttons and letter sub-labels, contacts list (persisted to `~/.lumo-contacts`), call log tab. Dialing a number saves it as a contact.
- **Camera**: V4L2 camera detection with viewfinder UI (crosshair, corner brackets, live indicator). Graceful "NO CAMERA FOUND" fallback with connection instructions. Gallery mode to browse `~/Pictures`. Capture button with circular shutter design.
- **Maps**: Compass rose with cardinal/intercardinal ticks, saved places list (persisted to `~/.lumo-places`) with tap-to-select and tap-again-to-edit, device location info tab showing GPS status, coordinates (38.4784N 82.6379W), timezone, and altitude.

### Notification Panel

- Added left-side notification panel, triggered by tapping the left third of the status bar. Displays notifications in reverse-chronological order with accent-colored dots.
- Restructured top-edge panel triggers from halves to thirds: left=notifications, center=date/time/weather, right=quick settings. All three panels are mutually exclusive.
- Notifications are stored in a ring buffer (8 max) on the compositor and broadcast to shell clients via the state protocol.

### Clock App Fixes

- Fixed timer countdown: `timer_remaining_sec` was always set to `timer_total_sec` in the render context, so the displayed time never counted down. Now properly computed from elapsed time.
- Added alarm firing indicator: flashing "!! ALARM RINGING !!" banner with red/accent alternation when the current time matches the set alarm.
- Added alarm sound: forks `pw-play` (PipeWire) or `aplay` (ALSA) to play `alarm-clock-elapsed.oga` when the alarm fires. Sound plays once per trigger minute.
- Added timer completion indicator: "TIMER COMPLETE!" banner when timer reaches zero.

### Notes App Improvements

- Added full-screen editor (Google Keep style): tapping a note opens a large text area with word-wrap, blinking cursor, character count (0/128), "DONE" button, and "DELETE" button.
- Added delete note functionality in both list view (red "DELETE NOTE" button when a note is selected) and editor view.
- Notes and Maps apps now have 500ms periodic redraws for cursor blink animation.

### OSK Integration

- Maps places tab now triggers the OSK when editing a place name. Uses the same `lumo_app_wants_osk` pattern as Notes.
- Added test coverage for OSK trigger policy: `test_notes_osk_trigger` and `test_maps_osk_trigger` verify hit-test zones and `wants_osk` returns.

### Browser Rewrite

- Replaced system Chromium with custom `lumo-browser` built on WebKitGTK 6.0 + GTK4.
- Full Lumo-themed UI via GTK4 CSS: purple/orange/aubergine color scheme matching the shell.
- Tabbed browsing with GtkStack: up to 8 tabs, + button to add, X to close.
- Smart URL bar: auto-detects schemes, localhost, domains, and search queries (DuckDuckGo).
- Bookmark system: star button saves current page to `~/.lumo-bookmarks`, persisted across sessions.
- Custom Lumo start page with search box and quick bookmark links.
- Performance tuning for riscv64: hardware acceleration off, media/WebGL/WebAudio disabled, mobile user-agent.
- Toolbar positioned at bottom of portrait buffer (right side after rotation) to avoid bottom-edge gesture zone.
- Removed Chromium submodule (47GB on host, 20GB on device) and all cross-compile infrastructure.
- Installed Epiphany (GNOME Web) as secondary browser option.
- Added comprehensive browser test suite: URL resolution, encoding, bookmark persistence, and GTK UI component tests.

### Panel Fix

- Fixed top-edge panel zone calculation for rotated displays. Uses `wlr_output_layout_get_box` for rotated width instead of `wlr_output->width` which returns native/unrotated dimensions.

### Version

- Bumped to 0.0.59. Single source of truth in `compositor/include/lumo/version.h`.

## [0.0.58] - 2026-03-31

### App Drawer Close Fix

- Fixed app drawer not closing: tapping outside the launcher panel now dismisses the drawer, matching the existing quick-settings and time-panel dismiss behavior.
- Fixed launcher and OSK close animation leaving ghost artifacts on screen. The non-unified shell client `tick_animation()` was drawing the final frame after deactivating the animation flag, causing `redraw()` to bail out and leave the previous frame's stale pixels visible forever. The final frame is now drawn before deactivation.
- Fixed launcher and OSK renderers not clearing the pixel buffer when visibility reaches zero. The launcher skipped the full-buffer clear as an optimization (it normally fills every pixel with an overlay), but when the close animation returned early at `visibility=0`, stale pixels from the previous frame remained.
- Added `finish_hide_if_needed()` call to the non-unified `tick_animation()` path, matching the unified render loop behavior, so the layer surface is properly reconfigured after the close animation completes.

### Browser Overhaul

- Replaced custom WebKitGTK browser (`lumo-browser`) with system Chromium (v122) for reliable modern web support. Launches via `chromium-browser --ozone-platform=wayland --single-process --disable-gpu --enable-wayland-ime` with DuckDuckGo Lite as the start page.
- Added shell command argument support: launcher commands containing spaces are now executed via `sh -c` so Chromium flags are properly parsed.
- Chromium's `--single-process` flag is critical on riscv64 — the multi-process renderer hangs with `RESULT_CODE_HUNG` on this hardware.
- Chromium uses text-input-v3 on Wayland, so tapping input fields triggers the compositor's OSK auto-show.
- Added `close_app` bridge protocol command to programmatically close the focused app via `xdg_toplevel_send_close`.
- Fixed OSK not auto-showing when tapping input fields in Chromium: the `text_input_commit` handler now directly shows the keyboard when `enabled=true`, bypassing the `refresh_keyboard_visibility` list scan that failed to match the text-input resource.
- Fixed top-of-screen controls (tab close button, menus) in apps being captured by the system top-edge gesture zone. Top-edge touches now pass through to the focused app when no panels are open.

### Chromium Source & Lumo Theme

- Added Chromium v122.0.6261.128 as a git submodule (shallow clone) under `chromium/` for source-level UI customization.
- Created `lumo_color_mixer.cc` — a Chromium color mixer that overrides 40+ color IDs with the Lumo palette (Ubuntu Orange #E95420, Aubergine #2C001E, dark surfaces, warm secondary text).
- Wired Lumo mixer into `chrome_color_mixers.cc` after all standard mixers so Lumo colors take precedence over Chrome defaults.
- Registered in `BUILD.gn` for the color mixers source set.

### Pinch-to-Zoom

- Implemented two-finger pinch-to-zoom gesture for all native Lumo apps. The compositor detects two simultaneous touch points on an app surface, tracks inter-finger distance, and sends KEY_ZOOMIN/KEY_ZOOMOUT events as the scale crosses 0.1 boundaries.
- Terminal font scale adjusts in real-time during pinch: default scale 2, pinch-out zooms up to scale 6, pinch-in down to scale 1. All layout (chars per line, line height, wrapping) adapts automatically.
- Pinch cancels the original single-touch delivery via `wlr_seat_touch_send_cancel` to prevent accidental taps during the gesture.
- Auto-shown keyboard is hidden when a pinch begins.
- Zoom state persists across pinch gestures (cumulative).

### Terminal Overhaul

- Fixed terminal command output not rendering: `\r` (carriage return) was wiping the line buffer before `\n` could commit it. The PTY sends `output\r\n` for each line; `\r` is now ignored.
- Added text wrapping: long lines wrap at the display width boundary instead of running off screen.
- Changed to top-down layout: scrollback lines render from the header downward, with the prompt following immediately after the last output line.
- Terminal font size responds to pinch-to-zoom (scale 1-6).

### OSK Improvements

- Fixed OSK page toggle (123/ABC) not updating visually: the page state was not broadcast to the shell client process. Added `osk_page` field to the state protocol frame.
- Fixed OSK key press visual feedback not showing in unified mode: the redraw path now calls `lumo_shell_client_redraw_unified()` when the active target changes.
- Increased `LUMO_SHELL_PROTOCOL_MAX_FIELDS` from 36 to 48 — the state frame had grown to 41 fields, silently failing and breaking all UI interaction.

### Input Modularization

- Split `input.c` (2,715 lines) into three files: `input.c` (1,236), `input_touch.c` (963), `input_pointer.c` (490), plus `input_internal.h` (169) for shared declarations.
- Zero behavioral changes — pure structural refactor verified by the full UI test suite.

### Weather Fix

- Fixed double C→F conversion: the `&u` flag works from the OrangePi (returns °F directly), so the parser now detects the unit suffix and skips conversion when already Fahrenheit.

### Top Panel Close Flash Fix

- Fixed app drawer briefly flashing when closing a top-bar panel (quick settings or time panel). The launcher surface was rendering the app drawer grid during its close animation instead of clearing to transparent. Now skips drawer rendering when `compositor_launcher_visible` is false.

### Weather Display Fix

- Fixed weather temperature displaying Celsius value with Fahrenheit label. The wttr.in API always returns Celsius with custom format strings regardless of the `&u` parameter. The parser now converts C to F (`temp * 9/5 + 32`) before storing.
- Fixed wind speed displaying in km/h instead of mph. Wind values from wttr.in are now converted (`km/h * 0.621`) for US-unit display.

## [0.0.57] - 2026-03-30

### Codebase Audit & Fixes

- Fixed 3 memory leaks in protocol.c: missing free() on early return in new_toplevel, new_popup, new_layer_surface.
- Fixed list reinitialization bug in output.c: removed duplicate wl_list_init that orphaned existing outputs.
- Fixed incorrect wl_list membership check in input.c cleanup path.
- Added integer overflow protection for stride/size calculations in shell_client.c and app_client.c.
- Added snprintf truncation detection in shell_protocol.c.
- Added range validation for sscanf-parsed alarm/timer values in app_client.c.
- Added hitbox dirty-flag optimization: hitbox refresh skipped in configure_layers when not dirty.
- Fixed pixbuf rowstride arithmetic to use size_t casts preventing int overflow.
- Replaced strcpy with strncpy in test_app.c.

### On-Screen Keyboard Overhaul

- Fixed off-by-one keymap bug in app_client.c: terminal keymap was missing \0 entries for Tab, LeftCtrl, LeftShift, and Backslash, causing every letter to map one key to the right.
- Added `<`, `>`, `^` glyphs to shell bitmap font so backspace (`<-`) and shift (`^`) labels render visibly.
- Added full lowercase glyph set (a-z) to both shell and app bitmap fonts; removed toupper() so uppercase and lowercase render distinctly.
- Added shift key toggle: tap shift to enable, next letter commits uppercase then auto-clears. Shift key highlights orange when active. OSK labels switch between lowercase and uppercase.
- Added shift state broadcast via bridge protocol so shell client can render shift visual feedback.
- Added shifted keycode support in virtual keyboard path (LEFT_SHIFT press/release wraps letter keycodes).
- Added full shifted keymap in terminal app (shift+1=!, shift+2=@, etc.).

### OSK Layout Changes

- Replaced `?` key (bottom-right) with close-OSK button (`v` label) that hides the keyboard.
- Replaced `,` key (row 3) with page toggle (`123`/`ABC`) that switches between QWERTY and symbols.
- Added symbols page: `1 2 3 4 5 6 7 8 9 0 / @ # $ % & - + ( ) / ^ ! ? , ; : ' / ABC / . SPACE ENTER v`.
- Added 12 new glyphs to shell font: `! @ # $ % & + ( ) ; '` and `^`.
- OSK resets to QWERTY page when keyboard is hidden.
- Extended virtual keycode map with shifted symbols (!@#$%^&*()_+?:).

### Terminal & App Lifecycle

- Terminal `exit` command now closes the app window (sets client.running=false on PTY POLLHUP) instead of showing "[shell exited]".
- Keyboard auto-hides when last app toplevel is destroyed (added to lumo_protocol_teardown_toplevel).

### Touch Input Fixes

- Fixed OSK/launcher hitbox priority: launcher hitbox now registered before OSK so OSK takes priority in overlap region. Fixes typing in search accidentally opening apps behind the keyboard.
- Bottom-edge swipe now takes priority over all shell hitboxes: system edge zone detection for LUMO_EDGE_BOTTOM runs before hitbox checks, ensuring swipe-to-close always works even when OSK or launcher covers the gesture area.
- Simplified bottom-swipe action chain: launcher visible → close launcher + keyboard; keyboard visible → close keyboard; app focused → close app; nothing → open launcher.

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
- No NPU available on OrangePi RV2 (SpacemiT X1 SoC).

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
