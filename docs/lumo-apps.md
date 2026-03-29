# Lumo Native Apps

All apps are built as a single `lumo-app` binary with `--app <name>` to
select the app. They use xdg-shell as Wayland clients with SHM rendering.
The browser is a separate `lumo-browser` binary using GTK4+WebKitGTK.

## OSK Input

The on-screen keyboard sends key presses to apps via a **virtual keyboard
fallback**. When text-input-v3 protocol fails (common race condition with
`focused_surface`), the compositor converts OSK characters to Linux keycodes
and sends them as `wl_keyboard` key events via `wlr_seat_keyboard_notify_key()`.
Apps receive these in their standard `wl_keyboard` listener.

When the app drawer is showing, OSK keys are routed to the search bar instead.

## Functional Apps

### Terminal (messages)
- Real PTY shell via `forkpty()` running `/bin/sh`
- Blinking cursor (500ms cycle)
- Menu: tap "LUMO TERMINAL" → New, Keyboard, Settings, About
- OSK auto-shows on focus
- Text-input-v3 for OSK key delivery to PTY

### Clock
- Large digital clock (scale 8) with HH:MM:SS
- Alarm card (placeholder)
- Stopwatch with start/stop/reset (tap cards)

### Files
- Directory browser using `opendir`/`readdir`
- Touch-drag scrolling
- File sizes via `stat()` (B/K/M)
- Tap folder to navigate, tap path bar to go up
- Directory traversal protection (rejects `..` entries)

### Settings
- 8 categorized rows: Network, Display, Storage, Memory, System, About, Lumo, CPU
- Sub-pages with info rows, toggle switches, usage bars
- Reads from `/proc` for system info

### Notes
- Note list with add/select
- Persistence to `~/.lumo-notes`
- OSK appears when a note enters edit mode

### Music
- Scans `~/Music/` for .mp3, .wav, .ogg, .flac, .m4a, .aac
- Track list with selection highlight
- Now Playing bar with animated progress
- Play/pause via `mpv --no-video`
- Creates `~/Music/` if missing

### Photos
- Scans `~/Pictures/` for .jpg, .jpeg, .png, .bmp, .gif, .webp
- 3-column grid with real thumbnails for each discovered image
- Selection highlight with orange border
- Info bar showing selected filename
- Creates `~/Pictures/` if missing

### Videos
- Scans `~/Videos/` for .mp4, .mkv, .avi, .mov, .webm
- Preview area with play button triangle
- Library list with play icons
- Playback via `mpv`
- Animated progress bar when playing
- Creates `~/Videos/` if missing

## Stub Apps

Phone, Camera, Maps — show app title, subtitle, and card layout
placeholders. All follow the dynamic time/weather theme.

### Browser

- Standalone `lumo-browser` binary using GTK4 + WebKitGTK 6.0
- URL bar with smart input: URLs, domains (auto-https), localhost (auto-http), search queries (percent-encoded, DuckDuckGo Lite)
- Back/Forward/Reload navigation buttons
- Fullscreen Wayland client, hardware acceleration disabled for riscv64
- Falls back to `lumo-app --app browser` stub if WebKitGTK not available
- Built conditionally — only compiled when `webkitgtk-6.0` and `gtk4` deps are present

## Theme Integration

All apps use `lumo_app_theme_get()` which computes colors from the current
hour using the same time-of-day palette as the shell. Weather data is not
available to apps (only the shell has the bridge protocol), but the time-based
colors still shift.
