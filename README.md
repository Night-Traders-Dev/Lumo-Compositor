# Lumo

Lumo is a touch-first Wayland compositor and shell stack for small mobile-style devices.

The current target is an OrangePi RV2 driving a 7 inch touchscreen, so the design
leans toward:

- touch and gesture-first input
- large hit targets and reserved edge zones
- screen rotation and coordinate mapping
- native Wayland apps plus optional xWayland compatibility
- compositor-owned state for launcher, OSK, sidebar, scrim, and focus handling
- real-time PS4 Flow wave animated background (GPU-composited) (60s seamless loop, 0% CPU)
- sidebar multitasking bar with running app icons and AI-generated Lumo icon
- mobile gesture navigation: bottom=home, right=back, left=sidebar

## Current Layout

- `compositor/` contains the wlroots-based compositor core in C.
  The compositor sources are grouped by category under `src/apps`, `src/core`,
  `src/protocol`, `src/shell`, and `src/tools`, with matching grouped test
  directories.
- `docs/` contains the architecture and migration notes.
- `Design.md` captures the touch-first shell direction and visual language.
- `CHANGELOG.md` tracks semver-style progress starting at `0.0.1`.

The compositor module already has a working scaffold for:

- backend startup and shutdown
- output management and rotation
- touch-first input routing
- layer-shell support
- text input and input-method hooks
- xWayland startup and basic window management
- a companion `lumo-shell` client scaffold for launcher, OSK, and gesture surfaces
- a native `lumo-app` client that powers the launcher tiles with built-in Lumo apps
- compositor-owned shell hitboxes for scrims, OSK zones, and edge gestures
- multi-edge mobile gesture zones plus a built-in touch audit flow that saves per-device profiles without taking over the launcher path in normal sessions

## Build

The easiest way to build Lumo is with the repo-root wrapper:

```sh
./build.sh
```

That script wraps the Meson build in `compositor/` and exposes the common
knobs we use today:

```sh
./build.sh --xwayland disabled
./build.sh --buildtype debugoptimized --test
./build.sh --build-dir build-noxwayland --xwayland disabled --test
```

If you prefer to run Meson directly, the underlying build still works from the
`compositor/` subdirectory:

```sh
cd compositor
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

## Install

The compositor build installs the Wayland session entry plus the compositor and
shell binaries side by side:

```sh
meson install -C compositor/build
```

That installs both `lumo.desktop` and `lumo-headless.desktop` into the Wayland
session directory and points the login screen at the right startup mode for
each session.
The normal session launches `lumo-compositor --backend drm --shell lumo-shell`.
When the session is started by GDM, the compositor can still use DRM even if
there is no controlling tty in the process.
The debug session launches `lumo-compositor --backend headless --debug --session
lumo-headless --socket lumo-shell-headless --shell lumo-shell`.
In both cases, the compositor still owns shell startup, so the bundled launcher,
OSK, and gesture surfaces come up automatically after login or launch.
If one of those bundled shell clients exits unexpectedly at runtime, the
compositor now reaps and respawns it so the session can recover without
throwing away the whole login.
When the OrangePi touch-quirk install is enabled, Lumo also installs a narrow
udev override that resets the known `hotlotus wcidtest` panel to an identity
libinput calibration matrix. That keeps old panel-rotation scripts from
fighting Lumo's own compositor-driven rotation and touch mapping.
Use `sudo` for the install step if your prefix points at a system directory
like `/usr` or `/usr/local`.

## Run

The compositor binaries land in `compositor/build/` by default:

```sh
./compositor/build/lumo-compositor --help
./compositor/build/lumo-compositor --debug
./compositor/build/lumo-compositor --rotation 180
./compositor/build/lumo-compositor --backend drm
./compositor/build/lumo-compositor --backend headless
./compositor/build/lumo-shell --mode launcher
./compositor/build/lumo-shell --mode osk
./compositor/build/lumo-shell --mode gesture
./compositor/build/lumo-app --app browser
./compositor/build/lumo-screenshot --output live.png
```

On the OrangePi RV2 running Ubuntu 24.04 riscv64, `--backend drm` is the
normal direct-to-display path and the normal GDM login-session path. Use
`--backend headless`, `--backend wayland`, or `--backend x11` when you want to
debug nested sessions or isolate backend bring-up issues from SSH or another
remote shell.
In Lumo, explicit DRM mode maps to wlroots `libinput,drm` so direct-display
sessions still bring up touchscreen and keyboard devices alongside the scanout
backend.
The current reserved edge behavior is:

- top-left third opens the notification panel
- top-center third opens the time/date/weather panel
- top-right third opens the quick settings panel
- left edge dismisses any open panel, launcher, audit, or keyboard state
- right edge opens the launcher
- bottom gesture handle toggles the launcher drawer
- bottom-edge swipe (via gesture handle): closes launcher (if open) → hides keyboard (if visible) → closes focused app → opens launcher
- bottom-edge taps pass through to the focused app when no launcher is open (allows toolbar interaction)
- tapping outside an open panel dismisses it

Touch rotation is corrected dynamically from the active output transform inside
the compositor, so rotated displays should not need a global 180-degree
libinput flip rule to keep touch and scanout aligned.
`--backend drm` is still a bad fit for SSH and other non-seat shells, but it no
longer requires a visible tty when a display manager is launching the session.
When you leave the compositor in `--backend auto` and launch it from SSH or
another non-VT shell, Lumo now picks the safest available nested backend
instead of waiting on DRM to time out.
SSH sessions fall back to `headless` first; local GUI shells can still steer
`auto` toward nested `wayland` or `x11` when those are available.
Avoid launching `lumo-shell` directly with `sudo`; the compositor owns shell
startup and the shell expects a normal Wayland runtime.
The `lumo-screenshot` helper connects to the current Wayland session and writes
out a PNG using the compositor's screencopy protocol. It defaults to the
`lumo-shell` socket name, so on the OrangePi you can usually capture the live
session with:

```sh
XDG_RUNTIME_DIR=/run/user/$(id -u) ./compositor/build/lumo-screenshot --output live.png
```

You can also override the socket explicitly:

```sh
./compositor/build/lumo-screenshot --socket lumo-shell --output live.png
```

When you want to walk the touchscreen itself, use a debug-enabled `Lumo`
session or an explicit shell-protocol request to open the built-in touch audit
overlay. It guides you
through 8 edge and corner targets and saves a JSON profile under
`$XDG_CONFIG_HOME/lumo/touch-profiles/` or `~/.config/lumo/touch-profiles/`
when the pass completes.

## Troubleshooting

If the `Lumo` session drops back to GDM, start with the session log that GDM
captures for the compositor:

```sh
journalctl -b --no-pager | grep -iE 'gdm-wayland-session|lumo-compositor|lumo-shell'
```

On the OrangePi RV2, the most useful markers are:

- whether `lumo-compositor` gets as far as `output ...: ready`
- whether the shell bridge starts and `lumo-shell` connects
- whether tapping or dragging the bottom gesture pill leaves a temporary touch
  debug marker on that surface, which tells us the compositor is receiving and
  classifying touchscreen input even if the launcher does not open yet
- whether the touch-audit overlay advances through its 8 target points and
  writes a profile file into the user's `lumo/touch-profiles` directory
- whether `lumo-compositor` logs `input: touch ... audit ...` lines with the
  expected raw and logical percentages plus `top-left`, `bottom-center`, and
  similar region names during a live edge or corner sweep
- whether the device still has a leftover `/etc/udev/rules.d/99-7ep-caplcd-touch.rules`
  style calibration file forcing `LIBINPUT_CALIBRATION_MATRIX=-1 0 1 0 -1 1`,
  which can invert the panel even when Lumo's own rotation is correct
- whether the failure happens before or after the first shell surface arrives

If you see the colored launcher tiles and keyboard outline on-screen, the
session has already made it through DRM startup, shell autostart, and initial
layer-shell creation. At that point the remaining bugs are in live rendering or
surface lifecycle handling, not in the GDM session entry.

That split makes it much easier to tell session-manager problems apart from
compositor bring-up bugs.

## Design Notes

Lumo is being built around a few core ideas:

- the compositor owns input arbitration, output transforms, and reserved gesture zones
- the shell/UI is made of separate C clients for launcher, on-screen keyboard, bar, overlays, and native touch apps
- the on-screen keyboard commits text through text-input-v3 when a field is focused, with compositor-managed focus tracking
- xWayland support is optional at build time so minimal images can omit it when needed
- the repo-root `build.sh` script keeps the common Meson options in one place
- the runtime backend mode can be forced for OrangePi RV2 bring-up and nested debugging
- auto backend selection now falls back to nested or headless modes when no local VT is available
- the installed Wayland sessions launch the compositor, and the compositor autostarts the bundled shell clients
- touch hitboxes and OSK behavior need to work well on a compact display, not a desktop monitor
- the shared shell geometry helper keeps compositor hitboxes and shell surfaces aligned
- launcher tiles are native Lumo apps rather than wrappers around external desktop programs
- touch audit and saved device profiles are part of the compositor workflow, but normal mobile sessions keep audit entry out of the default edge-gesture path so the launcher stays reliable
- bottom-edge close gestures are compositor-owned so app dismissal stays consistent across native Lumo apps, Wayland toplevels, and XWayland windows

More detailed notes live in:

- `docs/lumo-compositor-architecture.md`
- `docs/lumo-migration-plan.md`
- `compositor/README.md`

## Status

The project is at v0.0.82 (Lumo OS) running on OrangePi RV2 hardware.

Current capabilities:

- full DRM compositor with GPU compositing (PowerVR BXE-2-32 GLES 3.2)
- continuous theme engine with smoothstep time-of-day interpolation across 12 color stops, exponential-approach weather blending, Ubuntu/Sailfish/webOS palettes
- animated procedural background with weather-aware hue shifts (fetches wttr.in every 5 min)
- boot chime on shell startup
- GNOME 3.x style app drawer with 4x3 grid, search bar, and translucent overlay
- functional search: type on OSK to filter apps by name in real-time
- status bar with live clock, WiFi signal bars, and LUMO branding
- three top panels triggered by status bar thirds: notifications (left), time/date/weather (center), quick settings (right)
- notification panel with ring buffer (8 max), accent-colored dots, reverse-chronological display
- quick settings panel with WiFi, display, volume/brightness sliders, session info, reload/rotate
- time/date panel with large clock, date, weather (temperature, condition, humidity, wind)
- on-screen keyboard (Lomiri style) with shift key, close button, QWERTY + symbols + terminal pages (ESC, TAB, Ctrl+C/D/Z/L, arrows, PgUp/Dn, pipes), virtual keyboard fallback
- full uppercase/lowercase bitmap font rendering in both shell and app surfaces
- functional Browser (custom WebKitGTK 6.0 + GTK4, tabbed browsing, Lumo-themed CSS, bookmarks, smart URL bar, DuckDuckGo search)
- functional Terminal with VT100/xterm-256color emulator (cell grid, ANSI CSI parser, 256-color SGR, cursor movement, scroll regions, alternate screen buffer), runs btop/vim/nano/top, pinch-to-zoom font scaling (1-6x), blinking block cursor, menu, exit closes app
- functional Browser with WebKitGTK 6.0, tabbed browsing, URL bar with auto-https, bookmarks, GPU page compositing, persistent cookies
- functional Clock with 4 tabs (Clock, Alarm, Stopwatch, Timer), persistent settings
- functional Files with directory browsing, scroll, navigation, file sizes
- functional Settings with 8 categorized sub-pages
- functional Notes with full-screen editor, word-wrap, delete, OSK input, persistence
- functional Phone with dialer pad, contacts, call log
- functional Camera with V4L2 detection, viewfinder UI, gallery
- functional Maps with compass, saved places, location info
- functional Music player (scans ~/Music, playback via mpv)
- functional Photos with PNG/JPEG decoding, fullscreen viewer, grid with scroll
- functional Videos (scans ~/Videos, playback via mpv)
- pinch-to-zoom: two-finger gesture detection with real-time scale tracking, works in all native apps
- swipe gestures: velocity + angle + projection detection (iOS/Android research-based)
- screen rotation via quick settings with touch matrix remapping
- shell child supervision with automatic respawn
- double-buffered SHM rendering for shell clients
- toast notifications (Android-style pills)
- functional Clock with alarm sound (pw-play/aplay), timer countdown, visual alarm indicator
- 7 test suites: compositor, shell, app, browser, screenshot, fuzz/stress, and perf
- functional Calculator with standard operations, wide zero, operator buttons
- functional Calendar with month grid, day highlight, prev/next navigation
- functional Weather with temperature, condition, humidity, wind, UV cards
- functional Contacts with avatar initials, add/edit/delete, shared data
- functional Recorder with waveform visualization, record/play/delete
- functional Tasks with checklist, toggle done, add/delete
- functional Downloads viewer (~/Downloads with file type icons)
- functional Package Manager (dpkg query, installed list with versions/sizes)
- functional System Log viewer (journalctl, color-coded severity, scroll)
- touch ripple effect on all shell surfaces (launcher, OSK, gesture, status, sidebar, background)
- GitHub app with full README rendering, markdown file viewer, syntax highlighting
- Maps app with compass, saved places, location info
- GPU-accelerated wave background via GLES2 fragment shader on PowerVR (CPU 252% → 49%)
- first-boot setup wizard (user, WiFi, timezone) with GUI
- WiFi management in Settings (scan, connect, password entry)
- OS image builder (tools/build-image.sh) for distributable Lumo OS
- multiple security audits: 40+ issues fixed across all subsystems

Next milestones:

- custom login screen to replace GDM greeter
- window management UX for third-party Wayland apps
- browser history and find-in-page
- Vulkan/EGL rendering path when hardware supports it
