# Lumo Native Apps

All apps are built as a single `lumo-app` binary with `--app <name>` to
select the app. They use xdg-shell as Wayland clients with SHM rendering.
The browser tile launches system Chromium (v122) via Wayland.

## OSK Input

The on-screen keyboard sends key presses to apps via a **virtual keyboard
fallback**. When text-input-v3 protocol fails (common race condition with
`focused_surface`), the compositor converts OSK characters to Linux keycodes
and sends them as `wl_keyboard` key events via `wlr_seat_keyboard_notify_key()`.
Apps receive these in their standard `wl_keyboard` listener.

When the app drawer is showing, OSK keys are routed to the search bar instead.

### OSK Layout

The OSK has two pages switchable via a toggle key:

**QWERTY page (default):**

- Row 1: `q w e r t y u i o p`
- Row 2: `a s d f g h j k l <-`
- Row 3: `^ z x c v b n m 123`
- Row 4: `. SPACE ENTER v`

**Symbols page:**

- Row 1: `1 2 3 4 5 6 7 8 9 0`
- Row 2: `@ # $ % & - + ( ) <-`
- Row 3: `^ ! ? , ; : ' / ABC`
- Row 4: `. SPACE ENTER v`

Special keys:

- `^` (shift): toggles uppercase/lowercase, auto-clears after one letter
- `<-` (backspace): deletes one character
- `123`/`ABC`: switches between QWERTY and symbols pages
- `v` (close): hides the on-screen keyboard
- OSK resets to QWERTY page when hidden

### Case Support

Both the shell and app bitmap fonts have distinct uppercase (A-Z) and
lowercase (a-z) glyphs. The OSK shows lowercase labels by default and
uppercase when shift is active. Text typed in the terminal and other apps
renders with proper case distinction.

## Functional Apps

### Terminal (messages)
- Real PTY shell via `forkpty()` running `/bin/sh`
- Blinking cursor (500ms cycle)
- Menu: tap "LUMO TERMINAL" → New, Keyboard, Settings, About
- OSK auto-shows on focus
- Text-input-v3 for OSK key delivery to PTY
- Shift key support: uppercase letters and shifted symbols (shift+1=!, shift+2=@, etc.)
- Text wrapping at display width boundary
- Top-down layout: scrollback renders from header downward, prompt follows last output line
- Pinch-to-zoom font scaling (scale 1-6, default 2)
- `\r` (carriage return) handled correctly without wiping line buffer
- `exit` command closes the terminal app window and returns to desktop

### Clock
- 4 tabs: Clock, Alarm, Stopwatch, Timer
- Large digital clock (scale 8) with HH:MM:SS, day name, date, timezone, UNIX timestamp
- Alarm: set hour/minute, enable/disable toggle, flashing "!! ALARM RINGING !!" banner when triggered
- Alarm sound: plays `alarm-clock-elapsed.oga` via PipeWire/ALSA on trigger (once per minute)
- Stopwatch with start/stop/reset, centisecond display
- Timer with +1M/+5M presets, countdown progress bar, "TIMER COMPLETE!" indicator

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
- Note list with card-style rows, number badges, text preview
- Full-screen editor (Google Keep style): tap note twice to open editor with large text area, word wrap, blinking cursor, character count (128 max)
- "DONE" button (top-right) exits editor, "DELETE" button removes note
- Add note via orange "+ ADD NOTE" button
- Persistence to `~/.lumo-notes`
- OSK auto-shows when entering edit mode, hides on done/delete

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

### Phone
- 3 tabs: Dialer, Contacts, Call Log
- Full T9-style dial pad with digit buttons (1-9, *, 0, #) and letter sub-labels (ABC, DEF, etc.)
- Number display with backspace, green CALL button (saves number as contact)
- Contacts list loaded/saved from `~/.lumo-contacts`
- Call log tab (placeholder for future VoIP integration)

### Camera
- V4L2 camera detection: scans `/dev/video*` for capture devices
- Viewfinder UI with crosshair, corner brackets, device path, blinking REC indicator
- Graceful "NO CAMERA FOUND" fallback with USB/CSI camera instructions
- Capture button (circle shutter), gallery toggle, camera switch button
- Gallery mode: 3-column grid of photos from `~/Pictures/`

### Maps
- 3 tabs: Compass, Places, Info
- Compass rose with cardinal (N/E/S/W) and intercardinal ticks, N highlighted in orange
- Saved places list with tap-to-select, tap-again-to-edit, add/save to `~/.lumo-places`
- OSK auto-shows when editing place names
- Device location info: GPS status, coordinates (Ashland KY), timezone, altitude
- "CONNECT USB GPS FOR LIVE TRACKING" prompt for future serial GPS support

### Browser (lumo-browser)

- Custom WebKitGTK 6.0 + GTK4 browser with full Lumo-themed UI
- Tabbed browsing: up to 8 tabs with tab bar, + button, X to close
- GTK4 CSS theming: Lumo purple/orange/aubergine color scheme
- Smart URL bar: auto-detects URLs, domains, localhost, search queries (DuckDuckGo)
- Navigation: back, forward, reload buttons
- Bookmarks: persistent to `~/.lumo-bookmarks`, star button to save current page
- Default bookmarks: DuckDuckGo, Wikipedia, GitHub
- Custom start page with search box and quick links
- Performance tuning for riscv64: hardware accel off, media/WebGL/WebAudio disabled
- Mobile user-agent (Android 14 / Chrome 131)
- Builds natively on device — no cross-compilation needed

### Epiphany (GNOME Web)

- Installed as secondary browser (`epiphany-browser`)
- WebKit-based, GTK-native, Wayland-compatible
- Can be launched manually from terminal

## Theme Integration

All apps use `lumo_app_theme_get()` which computes colors from the current
hour using the same time-of-day palette as the shell. Weather data is not
available to apps (only the shell has the bridge protocol), but the time-based
colors still shift.
