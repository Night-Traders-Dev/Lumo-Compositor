# Lumo Migration Plan

This plan gets us from the current Sway-specific stack to a compositor plus shell clients.

## Phase 0: Freeze Current Behavior

Goal: keep the current launcher and gesture interactions working while we extract the architecture.

- retain the existing launcher UI and gesture surface for now
- keep the current socket-based bridge as a temporary compatibility layer
- document the exact startup order and expected shell surfaces

Exit criteria:

- launcher opens reliably
- gesture trigger still opens the launcher
- OSK and bar continue to start in the session

## Phase 1: Create The Compositor Core

Goal: create a wlroots-backed compositor library and a thin daemon around it.

- add backend, input, output, seat, and protocol modules
- wire wlroots backend startup
- wire output hotplug and rotation
- wire touch and pointer routing
- add a compositor-owned edge gesture path

Exit criteria:

- compositor boots on at least one output
- xdg-shell apps can map and receive focus
- output rotation updates geometry and input mapping

## Phase 2: Define The Shell Contract

Goal: remove direct Sway IPC dependencies from shell clients.

- replace ad-hoc socket messages with `lumo-shell-v1`
- keep standard Wayland protocols for input-method and layer-shell clients
- make shell clients state-driven instead of compositor-specific

Exit criteria:

- shell clients can discover output state and keyboard visibility
- launcher can request open/close without talking to compositor internals
- hitbox registration works for launcher and OSK
- OSK commits text into focused text-input clients

## Phase 3: Port The Shell Clients

Goal: move the visible UI to the new shell contract.

- launcher: keep the GTK UI, retarget its control channel
- OSK: make it a shell client that follows compositor focus and text-input state, then commit text into focused clients
- bar: move status and debug UI to the shell layer
- gesture surface: either keep it as a client or absorb it into the compositor, depending on latency and gesture quality

Exit criteria:

- no client depends on Sway IPC
- launcher, OSK, and bar work against the new compositor
- reserved hitboxes and edge gestures feel correct after rotation

## Phase 4: Remove Sway Glue

Goal: delete the old bridge once parity is good enough.

- retire `overlay_controller.py`
- retire the Sway config entry points
- replace the i3blocks debug path with compositor-native or shell-native status surfaces

Exit criteria:

- the new compositor starts the session
- all shell interactions are driven by the new protocol
- the old Sway config is no longer needed

## Current File Mapping

- `overlay_controller.py` -> compositor core
- `lumo_launcher.py` -> shell launcher
- `lumo_gesture.py` -> shell gesture surface or compositor edge zone
- `lumo_test_button.py` -> shell debug/test client
- `config` -> compositor session startup
- `i3blocks.conf` -> shell bar config or replacement
