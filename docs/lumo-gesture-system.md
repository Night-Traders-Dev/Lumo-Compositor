# Lumo Gesture System

## Edge Zones

The compositor defines 4 edge zones around the screen perimeter. Touches
that start within `gesture_threshold` pixels (default 32px) of an edge are
captured for gesture detection.

| Edge | Zone | Action |
|------|------|--------|
| Bottom | Gesture handle (48-80px) | Tap: toggle launcher. Swipe up: close launcher/keyboard/app (priority chain) |
| Top | Top 32px | Left half: toggle time panel. Right half: toggle quick settings |
| Left | Left 32px | Dismiss: cascades through audit → panels → launcher → keyboard |
| Right | Right 32px | Open launcher |

## Detection Algorithm

Gestures are detected using **distance**, **velocity**, and **angle**:

- **Distance threshold**: 32px (configurable via `gesture_threshold`)
- **Velocity threshold**: 800 px/second
- **Angle tolerance**: 15 degrees from edge normal (Android's OVERVIEW_MIN_DEGREES)
- **Projection on release**: iOS-style — progress + velocity * 150ms must exceed threshold
- Distance OR velocity triggers, AND angle must be within tolerance

The gesture handle at the bottom (48-80px tall) uses capture-and-wait:
- **Tap** (progress < 12px on release): toggles the launcher
- **Swipe** (distance >= threshold OR velocity > 800px/s, within 15° of vertical): closes the focused app
- **Projected swipe** (on release, projected endpoint crosses threshold): also closes

Edge tap replays are **dropped** when a toplevel is focused and the launcher
is hidden, preventing input bleed-through to tiles behind the active app.

## Bottom-Edge Swipe Action Chain

When a bottom-edge swipe is triggered, the compositor evaluates in order:

1. **Launcher visible** → close launcher (and keyboard if showing)
2. **Keyboard visible** (no launcher) → close keyboard
3. **App focused** → close focused app
4. **Nothing to close** → open launcher

## Touch Dispatch Order

1. System bottom-edge zone (always takes priority for bottom swipes)
2. Shell-reserved hitboxes (OSK keys, launcher scrim)
3. Edge gesture hitboxes (gesture handle, top edge)
4. System edge zones (left, right, top edges)
5. Shell surface redirect (invisible launcher → toplevel)
6. Normal surface target (app toplevels)

The bottom-edge system zone is checked before hitboxes so that swipe-to-close
works even when the OSK or launcher surface covers the gesture area.

## Easing Curves

Animations use Material Design curves:
- **Show**: standard ease-in-out `cubic-bezier(0.4, 0.0, 0.2, 1.0)`
- **Hide**: decelerate `cubic-bezier(0.0, 0.0, 0.2, 1.0)`
- **Launcher**: 350ms show, 250ms hide
- **OSK**: 300ms show, 200ms hide
