# Lumo Compositor Architecture

Lumo is a touch-first Wayland compositor and mobile shell built on
wlroots 0.18 for the OrangePi RV2 (riscv64, pixman software rendering).

## System Overview

```text
                 +--------------------------------------+
                 |          Shell Clients (6)            |
                 | background | launcher | osk | sidebar |
                 | gesture    | status                   |
                 +----------------+---------------------+
                                  |
                     bridge protocol (Unix socket)
                     + standard Wayland protocols
                                  |
    +-----------------------------+-----------------------------+
    |            Lumo Compositor Core (lumo-compositor)         |
    | backend | input | input_touch | input_pointer | output    |
    | protocol | protocol_setters                               |
    | shell_launch | shell_bridge | shell_hw                    |
    +-----------------------------+-----------------------------+
                                  |
                     wlroots 0.18 (DRM + libinput + scene)
                                  |
    +-----------------------------+-----------------------------+
    |  App Clients: lumo-app (12 native) | lumo-browser (SHM)  |
    +-------------------------------------------------------+
```

## Compositor Core (`src/core/`)

### backend.c
- Selects DRM, Wayland, headless, or X11 backend
- Initializes wlroots renderer and event loop
- Manages session startup/shutdown

### input.c + input_touch.c + input_pointer.c (~2900 lines total)

Input is split into three files plus a shared internal header
(`input_internal.h`, 169 lines):

- `input.c` (1,366 lines) — common input setup, keyboard handling,
  pinch-to-zoom gesture detection, virtual keyboard fallback
- `input_touch.c` (963 lines) — touch coordinate transform with rotation,
  priority-based touch dispatch, edge gesture detection
- `input_pointer.c` (490 lines) — pointer/mouse event handling

Priority-based touch dispatch:
  1. System bottom-edge zone (always priority for bottom swipes)
  2. Shell-reserved hitboxes (OSK keys, launcher scrim)
  3. Edge gesture hitboxes (gesture handle, top edge)
  4. System edge zones (left, right, top)
  5. Shell surface redirect (invisible launcher → toplevel)
  6. Normal surface targets (app toplevels)
- Gesture detection with velocity (800px/s), distance (32px), angle (15deg),
  and iOS-style projection on release
- Pinch-to-zoom: two-finger gesture tracking with KEY_ZOOMIN/KEY_ZOOMOUT
  events sent to focused app when scale crosses 0.1 boundaries
- Top-edge touch passthrough to focused app when no panels are open
- Virtual keyboard fallback for OSK → app key delivery
- Auto-show keyboard via direct text_input_commit handler
- Text-input-v3 keyboard auto-show: direct show in text_input_commit
  handler, bypassing the refresh_keyboard_visibility list scan

### output.c
- Output hotplug, rotation, and scene graph management
- Frame callback with wlroots scene-graph damage tracking
- Rotation persistence via `~/.lumo-rotation`

### compositor.c
- Main entry point and lifecycle
- systemd `sd_notify` for GDM session registration
- Parent NOTIFY_SOCKET inheritance

## Protocol Layer (`src/protocol/`)

### protocol.c (~1200 lines)
- Manages xdg-shell toplevels and popups
- Layer shell surface lifecycle (create, configure, destroy)
- Text-input-v3 binding with enable/commit/disable listeners
- Keyboard visibility state machine with `keyboard_auto_shown` flag
- Shell hitbox registration and refresh
- OSK scene node enable/disable to prevent touch interception

### protocol_setters.c (~420 lines)
- All `lumo_protocol_set_*` state setter functions
- Sidebar auto-hide timer (10-second wl_event_loop timer)
- Hitbox registration and lookup
- Layer surface configuration and arrangement
- Shared types via `protocol_internal.h`

### shell_protocol.c
- Line-based frame protocol parser
- Frame format: `LUMO/1 <kind> <name> id=<n>\n<key>=<value>\n...\n\n`
- Max 80 fields, 512-byte line buffer
- Protocol version handshake (`hello` event on connect)
- Token validation (no spaces/tabs in keys or values)

## Shell Layer (`src/shell/`)

### shell_launch.c (~765 lines)

- Spawns and supervises 6 shell client processes
- Process lifecycle, binary resolution, child signal handling

### shell_bridge.c (~1508 lines)

- Bridge protocol server (Unix socket at `$XDG_RUNTIME_DIR/lumo-shell-state.sock`)
- State broadcast with dirty-flag coalescing (flushed per output frame)
- 80 state fields: visibility, rotation, weather, volume, brightness, toast,
  osk_page, running apps, sidebar state, etc.
- Protocol hello handshake with version and feature flags
- Request handlers: activate_target, set_keyboard_visible, set_volume,
  set_brightness, show_toast, cycle_rotation, reload_session, close_app,
  focus_app, new_window, open_drawer, minimize_focused
- OSK key routing: shift toggle → page toggle → close → text-input-v3 → virtual keyboard fallback → search bar
- Duplicate app prevention (focuses existing; terminal/browser exempt)

### shell_hw.c (~471 lines)

- Volume/brightness control (fork + pactl/sysfs, non-blocking)
- Weather fetcher (fork + curl, 500ms poll timeout)
- Screenshot capture (async fork + lumo-screenshot)
- Boot chime (generated WAV, played via aplay)
- Platform-specific paths (HAL candidates — see `include/lumo/hal.h`)

### shell_client.c (~1300 lines)

- Single binary, 6 modes: background, launcher, osk, gesture, status, sidebar
- SHM double-buffered rendering with force-recycle on size transitions
- State socket connect with 5-retry, 200ms delay
- Sidebar auto-hide integration, boot splash flag management

### shell_render.c (~1400 lines)

- Main render dispatch (`lumo_render_surface`)
- UI renderers:
  - **Launcher**: GNOME 3.x grid (4×3), search bar with live filtering (no X button)
  - **OSK**: Lomiri-style dark charcoal keys (33 keys, 4 rows, 2 pages)
  - **Gesture**: invisible edge zone (no visual indicator)
  - **Status**: clock, dynamic WiFi bars (via `iw`), LUMO branding
  - **Sidebar**: Ubuntu Touch-style narrow icon strip with Lumo icon drawer button
  - **Quick settings**: WiFi, display, session, device, volume/brightness sliders
  - **Time panel**: large clock, date, week, weather
- Toast notifications (fade-in/out pills)

### shell_theme.c (~200 lines)

- 12 time-of-day color stops with smoothstep interpolation
- 7 weather condition hue shifts
- Exponential approach blending for smooth transitions
- Derived UI colors (bar, panel, tile, accent, text)

### shell_background.c (~1050 lines)

- Pre-rendered 5-minute PS4 Flow wave loop (1500 frames at 5fps, 366 MB)
- 8-core thread pool for parallel wave computation
- Half-res glow buffer (400×640) with 2× upscale composite
- Seamless 3-second crossfade at loop boundary
- Boot splash: Lumo icon + Ubuntu-style three-dot loading indicator
- Smooth gradient interpolation between hour palettes

### shell_osk.c

- 2-page keyboard: QWERTY (letters) and symbols (numbers/punctuation)
- 33 keys per page: 10+10+9+4 (with shift, backspace, page toggle, close, space, enter)
- Dynamic key rect calculation for any screen size
- Page state managed via lumo_shell_osk_toggle_page() / lumo_shell_osk_set_page()

### shell_ui.c
- Layout geometry for launcher tiles, gesture handle, status bar, OSK
- Tile command mapping (app launch strings)
- Surface configuration per mode (dimensions, anchors, exclusive zones)
- Animation duration constants (Material Design: 350/250ms launcher, 300/200ms OSK)

## App Clients (`src/apps/`)

### app_client.c (~1900 lines)
- Wayland client with xdg-shell, wl_seat (pointer + touch + keyboard)
- Text-input-v3 client for OSK (with roundtrip-based enable)
- PTY backend for terminal (forkpty + /bin/sh)
- Image loading (libpng, libjpeg) for photo viewer
- Media file scanning and mpv playback for music/videos
- Poll-based event loop with multiple fd sources (display + PTY)
- Clock data persistence (~/.lumo-clock), notes persistence (~/.lumo-notes)

### Individual app renderers
- `app_terminal.c`: PTY output, blinking cursor, dropdown menu
- `app_clock.c`: 4-tab clock (Clock, Alarm, Stopwatch, Timer)
- `app_files.c`: directory browser with scroll and navigation
- `app_settings.c`: 8 sub-pages with info rows and toggles
- `app_notes.c`: three-state editing with blinking cursor
- `app_music.c`: track list with now-playing bar
- `app_photos.c`: 3-column grid with fullscreen JPEG/PNG viewer
- `app_videos.c`: library list with preview area
- `app_ui.c`: shared rendering (fill_rect, gradient, rounded_rect, text, theme)

### Browser (native SHM)

- Native SHM browser with toolbar, bookmarks, URL bar
- Launches `lumo-webview` (WebKitGTK) for web content
- `--ozone-platform=wayland --single-process --disable-gpu --enable-wayland-ime`
- DuckDuckGo Lite as start page
- Text-input-v3 integration for OSK auto-show in input fields
- `--single-process` required on riscv64 (multi-process hangs)
- Chromium source in `chromium/` submodule for Lumo theme customization
- Legacy `lumo-browser` (GTK4 + WebKitGTK 6.0) still builds conditionally as fallback

## Build System

- Meson 0.58+ with C11
- 20+ build targets: compositor, shell, app, browser, screenshot, 6 test suites
- Dependencies: wlroots-0.18, wayland-server/client, xkbcommon, libpng, libjpeg
- Optional: webkitgtk-6.0 + gtk4 (browser), xwayland
- Cross-compiles for riscv64 (tested on OrangePi RV2, Ubuntu 24.04)

## Key Design Decisions

1. **Pixman-only rendering**: No GPU on the SpacemiT X1 SoC. All surfaces
   rendered in software via SHM buffers.

2. **Bridge protocol over Wayland**: Shell clients communicate state via a
   custom line-based protocol over a Unix socket, not Wayland protocol
   extensions. This avoids XML protocol definitions and versioning.

3. **Virtual keyboard fallback**: text-input-v3 has an unfixable race
   condition where `focused_surface` is never set before `commit()`. OSK
   keys fall back to `wlr_seat_keyboard_notify_key()` with Linux keycodes.

4. **Scene-graph Z-ordering**: OSK at LAYER_OVERLAY, raised on show,
   disabled on hide. Launcher at LAYER_OVERLAY. Apps at xdg-shell layer.
   Fullscreen toplevels can occlude LAYER_TOP but not OVERLAY.

5. **Weather-driven theme**: Continuous time-of-day interpolation with
   smoothstep easing plus smooth weather blending via exponential approach,
   shared by background, status bar, panels, drawer, and all app renderers
   via `lumo_theme_update()` (shell) and `lumo_app_theme_get()` (apps).
