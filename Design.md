# Lumo Design

## Direction

Lumo's shell is intentionally mobile-first and touch-first.

The visual direction uses a dynamic theme engine that blends Ubuntu, Sailfish,
and webOS palettes across 7 time-of-day periods and 7 weather conditions:

- Ubuntu Orange (`#E95420`) as the primary accent
- Ubuntu Aubergine (`#2C001E`, `#77216F`) for backgrounds and panels
- Sailfish and webOS tones mixed in by time-of-day (warm sunrise, cool midday, deep evening, dark night)
- Weather-aware hue shifts (clear, cloudy, rainy, snowy, stormy, foggy, windy)
- White for primary text and active elements

The interaction direction blends:

- Ubuntu Touch for the app drawer and on-screen keyboard ergonomics
- webOS for the mood, motion, depth, and cinematic surface treatment
- PlayStation 3-5 for the animated procedural background
- GNOME 3.x for the app drawer grid layout and search

That means the shell should feel:

- comfortable to operate on a 7-inch touchscreen
- spacious, warm, and rich rather than busy
- animated with confident slides and reveals instead of abrupt pops
- readable from arm's length with large targets and strong contrast
- time-aware with background hues that shift throughout the day

## Shell Surfaces

### Launcher

The launcher is a GNOME 3.x style fullscreen app drawer with a search bar.

Design goals:

- full-output layer surface when active, compact when hidden
- translucent overlay with a dark drawer sheet
- 4×3 grid with large touch cards, clear labels, and generous spacing
- accent color bars and simplified icon blocks instead of debug rectangles
- bottom gesture trigger acts as a true toggle, and a top-corner close control is always available when the drawer is open
- search bar at top for live filtering by app name (OSK keys routed to search when drawer is visible)

Current app drawer labels:

- Phone
- Messages
- Browser
- Camera
- Maps
- Music
- Photos
- Videos
- Clock
- Notes
- Files
- Settings

### Native Apps

Launcher tiles now open native `lumo-app` clients instead of desktop wrappers.

Design goals:

- every launcher tile should resolve to a working in-session app
- apps should be full-screen, touch-first Wayland clients
- app visuals should carry the same Ubuntu Touch plus webOS blend as the shell
- each app should feel coherent on its own even before deeper data integrations land

Current native app set:

- Phone
- Messages
- Browser
- Camera
- Maps
- Music
- Photos
- Videos
- Clock
- Notes
- Files
- Settings

### On-Screen Keyboard

The OSK is separated into its own source module at
`compositor/src/shell/shell_osk.c`.

Design goals:

- hidden by default
- shown only when a `text-input-v3` surface is both focused and enabled
- Ubuntu Touch-inspired row spacing and large thumb targets
- a dedicated bottom keyboard panel with a visible grabber and strong key contrast

Current keyboard layout (33 keys):

- row 1: `Q W E R T Y U I O P` (10 keys)
- row 2: `A S D F G H J K L` (9 keys)
- row 3: `^ Z X C V B N M ←` (9 keys, shift + backspace)
- row 4: `, SPACE . ? ↵` (5 keys)

### Gesture Handle

The gesture surface is always available as a narrow centered pill at the
bottom edge.

Design goals:

- webOS-style handle instead of a full-width debug strip
- visually lightweight when idle
- compositor-owned gesture logic, shell-owned presentation
- multi-edge system behavior rather than a single bottom-only trigger

Current system-edge roles:

- bottom gesture handle: toggle the launcher drawer
- bottom edge swipe up: close the currently focused native Lumo app, Wayland app, or XWayland app
- right edge: open the launcher drawer from the side reserve zone
- left edge: dismiss any open panel, launcher, audit, or keyboard state like a mobile back gesture
- top-left edge: toggle the time/date panel
- top-right edge: toggle the quick settings panel

### Status Bar

The status bar is a thin always-visible layer surface at the top edge.

Contents:

- left: LUMO branding in accent orange
- center: live clock (HH:MM) at scale 3
- right: WiFi signal strength bars

### Quick Settings Panel

Triggered by swiping down from the top-right edge. Renders as a dropdown overlay through the launcher surface.

Contents:

- WiFi status with signal bars
- Display rotation indicator
- Session version
- Device name
- Volume slider with live control (PipeWire/pactl)
- Brightness slider with live control (sysfs backlight)
- RELOAD button (orange) - restarts the compositor session
- ROTATE button (aubergine) - cycles through 0/90/180/270 degree rotation

### Time/Date Panel

Triggered by swiping down from the top-left edge. Renders as a dropdown overlay through the launcher surface.

Contents:

- large clock display (scale 5)
- full date (YYYY-MM-DD)
- day of week name
- week number
- weather: temperature (°F), condition, humidity, wind speed (fetched from wttr.in)

### Animated Background

The background is a full-screen layer surface at the BACKGROUND z-level that renders a procedurally generated animated gradient.

Design goals:

- PS3/PS5-inspired flowing wave patterns with light streaks
- Dynamic palette blending Ubuntu, Sailfish, and webOS tones
- time-of-day adaptive colors (warm sunrise mornings, standard midday, deep evening, dark night)
- weather-aware hue shifts (fetches wttr.in every 5 minutes)
- low CPU overhead (~10% at 5fps on riscv64)
- transparent to touch input (empty input region)

### Touch Audit

Lumo now includes a built-in touch audit mode for device bring-up.

Design goals:

- no external calibration script required during normal shell testing
- visible 8-point walk through for corners and edge centers
- saved per-device profile data after a successful pass
- geometry shared between compositor hit-testing and shell rendering so the audit UI and the compositor agree on what counts as a correct tap
- debug-only gesture entry so production touch use keeps the launcher path stable

## Motion

Lumo should move like a mobile shell, not like floating desktop widgets.

Current motion rules:

- launcher slide: bottom-up reveal, `350ms` show / `250ms` hide, Material Design ease-in-out / decelerate
- keyboard slide: bottom-up reveal, `300ms` show / `200ms` hide, Material Design ease-in-out / decelerate
- hidden launcher and keyboard surfaces compact down to a minimal footprint after the hide animation completes
- gesture pill and status bar are always visible with no animation
- background animates at 5fps with time-of-day and weather-aware color shifts
- toast notifications: Android-style pills with fade-in/out

Animation principles:

- one strong movement is better than several weak micro-animations
- surfaces should reveal from the edge they conceptually belong to
- motion should clarify state changes, not decorate them

## Layout Rules

- shell clients should size themselves from live layer-shell configure events and compositor state, not fixed bootstrap resolutions
- touch hit targets should remain large even when the visuals are compact
- transparent outer surfaces are preferred when only a sheet or panel needs to be visible
- launcher and OSK visuals should reserve visual breathing room around the edge of the panel
- launcher input capture should follow the visible drawer panel, not the entire transparent fullscreen shell surface
- touch rotation should follow the compositor's active output transform dynamically, so display rotation stays the source of truth instead of legacy system-wide touchscreen flip rules

## Architecture Notes

- compositor state remains the source of truth for launcher visibility, keyboard visibility, scrim state, rotation, quick settings, time panel, and gesture thresholds
- compositor state also owns touch-audit progress and per-device profile persistence
- shell clients consume that state over the shell bridge protocol (up to 36 fields per state frame)
- the shell protocol uses coalesced broadcasts via a dirty flag, flushed once per output frame
- shell clients use double-buffered SHM rendering to avoid per-frame allocation overhead
- the OSK is modularized into its own source file
- native apps are modularized into separate source files per app (app_terminal.c, app_clock.c, app_files.c, app_settings.c, app_notes.c, app_music.c, app_photos.c, app_videos.c)
- the shared app rendering API lives in app_render.h with common helpers (fill, gradient, rounded rect, text, glyph table)
- app clients use a poll-based event loop with configurable timeout for periodic redraws (1s for Clock, 5s for Settings)
- touch on scrim hitboxes is replayed to the overlay layer surface so launcher tiles and panel buttons receive taps
- the compositor links against libsystemd when available for GDM session ready notification
- screen rotation updates the output transform, refreshes all layer surfaces and hitboxes, and applies the correct touch coordinate transform
