# Lumo Native Apps

All apps are built as a single `lumo-app` binary with `--app <name>` to
select the app. They use xdg-shell as Wayland clients with SHM rendering.

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
- OSK auto-shows on focus

### Music
- Scans `~/Music/` for .mp3, .wav, .ogg, .flac, .m4a, .aac
- Track list with selection highlight
- Now Playing bar with animated progress
- Play/pause via `mpv --no-video`
- Creates `~/Music/` if missing

### Photos
- Scans `~/Pictures/` for .jpg, .jpeg, .png, .bmp, .gif, .webp
- 3-column grid with colored thumbnail placeholders
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

Phone, Browser, Camera, Maps — show app title, subtitle, and card layout
placeholders. All follow the dynamic time/weather theme.

## Theme Integration

All apps use `lumo_app_theme_get()` which computes colors from the current
hour using the same time-of-day palette as the shell. Weather data is not
available to apps (only the shell has the bridge protocol), but the time-based
colors still shift.
