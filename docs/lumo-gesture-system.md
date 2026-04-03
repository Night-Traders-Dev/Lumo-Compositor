# Lumo Gesture System

## Edge Zones

The compositor defines 4 edge zones around the screen perimeter. Touches
that start within `gesture_threshold` pixels (default 32px) of an edge are
captured for gesture detection.

| Edge | Zone | Action |
|------|------|--------|
| Bottom | Gesture handle (48-80px) | Go home: minimize all apps, close launcher/keyboard/sidebar |
| Top | Top 32px | Left third: notifications. Center third: time panel. Right third: quick settings |
| Left | Left 32px | Show sidebar (running apps multitasking bar, auto-hides after 10s) |
| Right | Right 32px | Back: minimize focused app, close panels/keyboard/sidebar |

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

## Bottom-Edge Swipe Action Chain (Go Home)

When a bottom-edge swipe is triggered, the compositor evaluates in order:

1. **Launcher visible** → close launcher (and keyboard if showing)
2. **Sidebar visible** → close sidebar
3. **Keyboard visible** → close keyboard
4. **Apps running** → minimize all (disable scene nodes, clear focus)

## Right-Edge Swipe Action Chain (Back)

1. Dismiss any open panels (notifications, time, quick settings)
2. Close sidebar if visible
3. Close keyboard if visible
4. Close launcher if visible
5. **App focused** → minimize focused app (hide but keep alive)

## Left-Edge Swipe Action (Sidebar)

Shows the sidebar multitasking bar. Auto-hides after 10 seconds.
Tapping outside the sidebar dismisses it immediately.
The sidebar shows running app icons and the Lumo app drawer button.

## Sidebar Interactions

- **Tap app icon** → switch to that app (re-enable scene node, give focus)
- **Long-press app icon (>500ms)** → context menu: Open / Close
- **Tap drawer button (Lumo icon)** → open app drawer (launcher)

## Touch Dispatch Order

1. System bottom-edge zone (always takes priority for bottom swipes)
2. Shell-reserved hitboxes (OSK keys, launcher scrim)
3. Edge gesture hitboxes (gesture handle, top edge)
4. System edge zones (left, right, top edges)
5. Shell surface redirect (invisible launcher → toplevel)
6. Normal surface target (app toplevels)

The bottom-edge system zone is checked before hitboxes so that swipe-to-close
works even when the OSK or launcher surface covers the gesture area.

## Pinch-to-Zoom

Two-finger pinch gestures are detected on app surfaces:

- The compositor tracks two simultaneous touch points and computes
  inter-finger distance each frame
- When the scale ratio crosses a 0.1 boundary, KEY_ZOOMIN or KEY_ZOOMOUT
  is sent to the focused app via `wlr_seat_keyboard_notify_key()`
- The original single-touch delivery is cancelled via
  `wlr_seat_touch_send_cancel` to prevent accidental taps during pinch
- Auto-shown keyboard is hidden when a pinch begins
- Terminal font scale adjusts in real-time (scale 1-6, default 2)
- Zoom state persists across pinch gestures (cumulative)

## Top-Edge Touch Passthrough

Top-edge touches pass through to the focused app when no panels (quick
settings or time panel) are currently open. This allows app controls near
the top of the screen (e.g., browser tab close buttons) to receive taps
without being captured by the system edge zone.

## Easing Curves

Animations use Material Design curves:
- **Show**: standard ease-in-out `cubic-bezier(0.4, 0.0, 0.2, 1.0)`
- **Hide**: decelerate `cubic-bezier(0.0, 0.0, 0.2, 1.0)`
- **Launcher**: 350ms show, 250ms hide
- **OSK**: 300ms show, 200ms hide
