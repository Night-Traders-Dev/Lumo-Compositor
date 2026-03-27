# Lumo Design

## Direction

Lumo's shell is intentionally mobile-first and touch-first.

The visual and interaction direction blends:

- Ubuntu Touch for the app drawer and on-screen keyboard ergonomics
- webOS for the mood, motion, depth, and cinematic surface treatment

That means the shell should feel:

- comfortable to operate on a 7-inch touchscreen
- spacious, calm, and dark rather than busy
- animated with confident slides and reveals instead of abrupt pops
- readable from arm's length with large targets and strong contrast

## Shell Surfaces

### Launcher

The launcher is a bottom-rising app drawer rather than a desktop grid.

Design goals:

- full-output layer surface when active, compact when hidden
- transparent outer surface with a dark drawer sheet inside it
- 3-column touch cards with clear labels and generous spacing
- accent color bars and simplified icon blocks instead of debug rectangles
- bottom gesture trigger acts as a true toggle, and a top-corner close control is always available when the drawer is open

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

Current keyboard layout:

- row 1: `Q W E R T Y U I O P`
- row 2: `A S D F G H J K L`
- row 3: `Z X C V B N M`
- row 4: `, . SPACE ? RETURN`

### Gesture Handle

The gesture surface is always available as a narrow centered pill at the
bottom edge.

Design goals:

- webOS-style handle instead of a full-width debug strip
- visually lightweight when idle
- compositor-owned gesture logic, shell-owned presentation
- multi-edge system behavior rather than a single bottom-only trigger

Current system-edge roles:

- bottom edge: open the launcher drawer
- right edge: open the launcher drawer from the side reserve zone
- left edge: dismiss launcher, audit, or keyboard state like a mobile back gesture
- top edge: reserved for future system surfaces in normal sessions; touch audit is only exposed from debug sessions and explicit tooling so the app drawer cannot be displaced accidentally

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

- launcher slide: bottom-up reveal, `240ms`, ease-out
- keyboard slide: bottom-up reveal, `190ms`, ease-out
- hidden launcher and keyboard surfaces compact down to a minimal footprint after the hide animation completes

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

- compositor state remains the source of truth for launcher visibility, keyboard visibility, scrim state, rotation, and gesture thresholds
- compositor state also owns touch-audit progress and per-device profile persistence
- shell clients consume that state over the shell bridge protocol
- the OSK is modularized into its own source file now so it can continue to evolve toward a dedicated client or binary without dragging unrelated shell code with it
