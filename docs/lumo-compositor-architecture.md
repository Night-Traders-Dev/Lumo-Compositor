# Lumo Compositor Architecture

This repo is currently a Sway-specific shell stack. The compositor rewrite should split the system into:

- a wlroots-based compositor core
- shell clients for launcher, on-screen keyboard, bar, and test surfaces
- a narrow protocol between the two

The compositor should own all input routing, output transforms, focus, and compositor-reserved gesture areas. Shell clients should own presentation.

## Module Graph

```text
                +------------------------------+
                |   Shell clients / UI layer   |
                |  launcher | osk | bar | dbg  |
                +-------------+----------------+
                              |
                              | custom shell protocol
                              | plus standard Wayland protocols
                              v
       +----------------------+----------------------+
       |      Lumo compositor core on wlroots        |
       | backend | input | output | protocol | seat  |
       +----------------------+----------------------+
                              |
                              | wlroots backend / render / seat
                              v
                   +-------------------------+
                   | DRM / libinput / scene  |
                   +-------------------------+
```

## Core Modules

### `backend`

- boots wlroots
- selects DRM/Wayland/headless backends
- owns session startup and shutdown
- initializes the renderer and event loop

### `input`

- handles keyboards, pointers, touch, tablets, and gesture streams
- classifies devices into seats
- applies output rotation to touch coordinates
- routes edge swipes and reserved zones before normal client dispatch

### `output`

- tracks connected outputs and their logical geometry
- applies rotation, scale, and mapping
- owns output hotplug and re-layout
- exposes per-output state to the shell layer

### `protocol`

- exposes the Lumo shell protocol to clients
- carries compositor state changes to shell surfaces
- accepts shell requests such as launcher open/close, keyboard visibility, and reserved hitbox registration

### `seat`

- tracks active keyboard, pointer, and touch focus
- handles text input focus changes
- coordinates with the OSK and shortcut inhibition

## Protocol Strategy

Use standard Wayland protocols where they already solve the problem:

- `xdg-shell` for regular app windows
- `wlr-layer-shell` for shell surfaces such as launcher, bar, and OSK
- `text-input-v3` and `input-method-v2` for text entry flows
- `virtual-keyboard-v1` for synthetic keyboard events when needed
- `pointer-gestures-v1` for swipe and gesture recognition
- `keyboard-shortcuts-inhibit-v1` for text entry and OSK focus

Then layer a small private shell protocol on top for compositor-specific state.

## Working Shell Protocol Sketch

Working name: `lumo-shell-v1`

### Objects

- `lumo_shell_manager_v1`
  - singleton global
  - created by the compositor
  - used by shell clients to bind to compositor state

- `lumo_shell_output_v1`
  - one object per output
  - reports geometry, scale, rotation, and enablement

- `lumo_shell_seat_v1`
  - one object per seat
  - reports focus, keyboard visibility hints, and touch state

- `lumo_shell_region_v1`
  - shell-defined hitbox or reserved region
  - used for launcher buttons, OSK keys, edge gestures, and scrims

### Requests from shell clients

- `register_hitbox(rect, kind, flags)`
- `clear_hitboxes()`
- `request_launcher()`
- `request_keyboard_visible(bool)`
- `request_rotation_sync(output_name)`

### Events from compositor

- `output_added`
- `output_removed`
- `output_changed`
- `focus_changed`
- `keyboard_visibility_changed`
- `gesture_triggered`

## Touch Hitboxes And Rotation

Touch hitboxes should be logical-space rectangles, not pixel-perfect widget bounds. That lets the compositor expand tap targets for:

- launcher tiles
- OSK keys
- close scrims
- edge gesture zones

Rotation should be applied at the compositor boundary, before hitbox testing and before app dispatch. That keeps touch, pointer, and output geometry aligned after screen rotation.

## Current Repo Mapping

- `lumo_launcher.py` becomes the launcher shell client
- `lumo_gesture.py` becomes either a shell gesture surface or a compositor-managed edge zone, depending on how much logic we want in core
- `overlay_controller.py` is the current Sway bridge and will be replaced by compositor internals
- `config` becomes a session launcher for the new compositor
- `i3blocks.conf` becomes shell-bar configuration

