# Lumo Gesture System

## Edge Zones

The compositor defines 4 edge zones around the screen perimeter. Touches
that start within `gesture_threshold` pixels (default 32px) of an edge are
captured for gesture detection.

| Edge | Zone | Action |
|------|------|--------|
| Bottom | Gesture handle + bottom 32px | Tap: toggle launcher. Swipe up: close app |
| Top | Top 32px | Left half: toggle time panel. Right half: toggle quick settings |
| Left | Left 32px | Dismiss: cascades through audit → panels → launcher → keyboard |
| Right | Right 32px | Open launcher |

## Detection Algorithm

Gestures are detected using both **distance** and **velocity**:

- **Distance threshold**: 32px (configurable via `gesture_threshold`)
- **Velocity threshold**: 800 px/second
- Either condition triggers the gesture action

The gesture handle at the bottom uses the same capture-and-wait mechanism
as edge zones. A tap (distance < threshold on release) toggles the launcher.
A swipe (distance >= threshold during motion, or velocity > 800px/s) closes
the focused app.

## Touch Dispatch Order

1. Shell-reserved hitboxes (launcher scrim, OSK keys)
2. Edge gesture hitboxes (gesture handle, top edge)
3. System edge zones (left, right, bottom edges)
4. Shell surface redirect (invisible launcher → toplevel)
5. Normal surface target (app toplevels)

## Easing Curves

Animations use Material Design curves:
- **Show**: standard ease-in-out `cubic-bezier(0.4, 0.0, 0.2, 1.0)`
- **Hide**: decelerate `cubic-bezier(0.0, 0.0, 0.2, 1.0)`
- **Launcher**: 350ms show, 250ms hide
- **OSK**: 300ms show, 200ms hide
