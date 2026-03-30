# Lumo Compositor Architecture

Lumo is a touch-first Wayland compositor and mobile shell built on
wlroots 0.18 for the OrangePi RV2 (riscv64, pixman software rendering).

## System Overview

```text
                 +----------------------------------+
                 |        Shell Clients (5)          |
                 | background | launcher | osk |     |
                 | gesture    | status              |
                 +---------------+------------------+
                                 |
                    bridge protocol (Unix socket)
                    + standard Wayland protocols
                                 |
    +----------------------------+----------------------------+
    |           Lumo Compositor Core (lumo-compositor)        |
    | backend | input | output | protocol | shell_launch      |
    +----------------------------+----------------------------+
                                 |
                    wlroots 0.18 (DRM + libinput + scene)
                                 |
    +----------------------------+----------------------------+
    |  App Clients: lumo-app (12 native) | lumo-browser (GTK) |
    +-----------------------------------------------------+
```

## Compositor Core (`src/core/`)

### backend.c
- Selects DRM, Wayland, headless, or X11 backend
- Initializes wlroots renderer and event loop
- Manages session startup/shutdown

### input.c (~2500 lines)
- Touch coordinate transform with rotation support
- Priority-based touch dispatch:
  1. System bottom-edge zone (always priority for bottom swipes)
  2. Shell-reserved hitboxes (OSK keys, launcher scrim)
  3. Edge gesture hitboxes (gesture handle, top edge)
  4. System edge zones (left, right, top)
  5. Shell surface redirect (invisible launcher → toplevel)
  6. Normal surface targets (app toplevels)
- Gesture detection with velocity (800px/s), distance (32px), angle (15°),
  and iOS-style projection on release
- Virtual keyboard fallback for OSK → app key delivery
- Auto-show keyboard for text-capable apps (messages, notes)

### output.c
- Output hotplug, rotation, and scene graph management
- Frame callback with wlroots scene-graph damage tracking
- Rotation persistence via `~/.lumo-rotation`

### compositor.c
- Main entry point and lifecycle
- systemd `sd_notify` for GDM session registration
- Parent NOTIFY_SOCKET inheritance

## Protocol Layer (`src/protocol/`)

### protocol.c
- Manages xdg-shell toplevels and popups
- Layer shell surface lifecycle (create, configure, destroy)
- Text-input-v3 binding with enable/commit/disable listeners
- Keyboard visibility state machine with `keyboard_auto_shown` flag
- Shell hitbox registration and refresh
- OSK scene node enable/disable to prevent touch interception
- Weather timer (5-minute fetch via wttr.in)

### shell_protocol.c
- Line-based frame protocol parser
- Frame format: `LUMO/1 <kind> <name> id=<n>\n<key>=<value>\n...\n\n`
- Max 36 fields, 512-byte line buffer
- Token validation (no spaces/tabs in keys or values)

## Shell Layer (`src/shell/`)

### shell_launch.c
- Spawns and supervises 5 shell client processes
- Bridge protocol server (Unix socket at `$XDG_RUNTIME_DIR/lumo-shell-state.sock`)
- State broadcast with dirty-flag coalescing (flushed per output frame)
- 36 state fields: visibility, rotation, weather, volume, brightness, toast, etc.
- Request handlers: activate_target, set_keyboard_visible, set_volume,
  set_brightness, show_toast, cycle_rotation, reload_session
- OSK key routing: shift toggle → page toggle → close → text-input-v3 → virtual keyboard fallback → search bar
- Weather fetcher (fork + curl, 500ms poll timeout)
- Volume/brightness control (fork + pactl/sysfs, non-blocking)
- Boot chime (generated WAV, played via aplay)

### shell_client.c (~4000 lines)
- Single binary, 5 modes: background, launcher, osk, gesture, status
- SHM double-buffered rendering with force-recycle on size transitions
- Dynamic theme engine (7 time-of-day palettes × 7 weather conditions)
- Renderers:
  - **Background**: animated gradient with wave glow and light streaks
  - **Launcher**: GNOME 3.x grid (4×3), search bar with live filtering
  - **OSK**: Lomiri-style dark charcoal keys (33 keys, 4 rows, 2 pages: QWERTY + symbols)
  - **Gesture**: subtle opacity gradient pill
  - **Status**: clock, WiFi bars, LUMO branding
  - **Quick settings**: WiFi, display, session, device, volume/brightness sliders, reload/rotate
  - **Time panel**: large clock, date, week, weather (temp, condition, humidity, wind)
- Toast notifications (fade-in/out pills)
- State socket connect with 5-retry, 200ms delay

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

### browser.c (separate binary: lumo-browser)
- GTK4 + WebKitGTK 6.0 standalone Wayland client
- Local HTML start page (instant load, no network)
- Smart URL bar (auto-https, localhost→http, percent-encoded search)
- Hardware acceleration disabled for riscv64 performance
- Conditional build (only when webkitgtk-6.0 + gtk4 deps present)

## Build System

- Meson 0.58+ with C11
- 20 build targets: compositor, shell, app, browser, screenshot, 5 test suites
- Dependencies: wlroots-0.18, wayland-server/client, xkbcommon, libpng, libjpeg
- Optional: webkitgtk-6.0 + gtk4 (browser), xwayland
- Cross-compiles for riscv64 (tested on OrangePi RV2, Ubuntu 24.04)

## Key Design Decisions

1. **Pixman-only rendering**: No GPU on the SpacemiT K1 SoC. All surfaces
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

5. **Weather-driven theme**: Single palette computation shared by background,
   status bar, panels, drawer, and all app renderers via `lumo_theme_update()`
   (shell) and `lumo_app_theme_get()` (apps).
