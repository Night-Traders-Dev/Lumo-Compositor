# Lumo Compositor Skeleton

This directory is the starting point for the wlroots-based compositor core.

It is intentionally small right now, but the source tree is now grouped by
category so the compositor can keep scaling without turning into one giant
flat directory:

- `src/core/` owns backend startup, compositor orchestration, input, output, XWayland, and the main entrypoint
- `src/protocol/` owns the custom `lumo-shell` protocol and shared shell wire helpers
- `src/shell/` owns shell launch and the shell-side UI helpers such as launcher and OSK layout
- `src/tools/` owns standalone utilities such as `lumo-screenshot`
- `tests/core/`, `tests/shell/`, and `tests/tools/` mirror those runtime categories

The real wlroots integration will come next. This scaffold gives us the module boundary first so shell-client work can move in parallel.

When the implementation lands, this compositor should:

- accept xdg-shell app windows
- support layer-shell shell surfaces
- manage output rotation and touch mapping
- expose shell state to launcher, OSK, bar, and gesture surfaces
- forward OSK text into focused text-input-v3 clients before falling back to lower-level keyboard handling
- keep shared touch-audit geometry and saved device profiles close to the input pipeline so OrangePi bring-up stays debuggable

## Build Toggles

The compositor build accepts a Meson feature flag for xWayland support. From
the repository root, the same flag is available through `build.sh`:

```sh
./build.sh --xwayland disabled --test
```

Leave the option enabled for normal desktop-app compatibility. Disable it for
smaller builds or environments where xWayland is not available.

The OrangePi RV2 touchscreen quirk install is also available through Meson and
`build.sh`:

```sh
./build.sh --touch-quirks enabled --test
```

That install path drops a narrow udev override for the known `hotlotus
wcidtest` panel so legacy 180-degree libinput calibration rules do not fight
Lumo's compositor-owned touch rotation logic.

The build also installs a Wayland session file when you run `meson install`:

```sh
meson install -C build
```

That install step drops both the normal `Lumo` session and the
`Lumo Headless Debug` session into the Wayland session directory.
The normal session launches `lumo-compositor --backend drm --shell lumo-shell`
so the compositor and bundled shell clients start together after login.
When GDM launches the session, the compositor can still use DRM even if the
process has no controlling tty.
If one of the bundled shell clients exits unexpectedly, the compositor now
reaps it and respawns that specific mode instead of leaving the session
without a launcher or keyboard.
The debug session launches `lumo-compositor --backend headless --debug --session
lumo-headless --socket lumo-shell-headless --shell lumo-shell` for remote or
headless bring-up.
Use `sudo` for `meson install` if the install prefix points at a system
directory.

## Runtime Backends

Lumo can be launched with an explicit backend mode:

```sh
./build/lumo-compositor --backend drm
./build/lumo-compositor --backend headless
./build/lumo-compositor --backend wayland
./build/lumo-compositor --backend x11
./build/lumo-screenshot --output live.png
```

The OrangePi RV2 on Ubuntu 24.04 riscv64 should normally use `drm` for the
physical touchscreen and for the login-screen session. `headless`, `wayland`,
and `x11` are there to make debugging and nested bring-up easier when we want
to isolate backend problems.
Lumo maps explicit DRM mode to wlroots `libinput,drm` so touchscreen and
keyboard devices stay available in the direct-display session instead of
starting a scanout-only backend.
The current mobile edge behavior reserves:

- top edge for future system surfaces in normal sessions; touch audit entry is kept to debug flows
- left edge for dismiss or back-style shell actions
- right edge for launcher open
- bottom edge for launcher open

Touch coordinates are corrected from the active output transform inside the
compositor, which keeps rotated outputs and gesture hitboxes aligned without
depending on a global 180-degree touchscreen flip rule.
`drm` is still the wrong choice for SSH and other non-seat shells, but it does
not require a visible tty when GDM or another display manager starts the
session.
If you leave the backend in `auto` from SSH or any other non-VT shell, Lumo
now prefers the safest available nested or headless backend instead of waiting
for the DRM path to time out.
SSH shells fall back to `headless` first; a local GUI session can still steer
`auto` toward nested `wayland` or `x11` when that makes more sense.

The build also installs `lumo-screenshot`, a small Wayland client that captures
the first available output through Lumo's screencopy manager and writes a PNG.
It defaults to the compositor's `lumo-shell` socket name, which makes it handy
for OrangePi session review over SSH:

```sh
XDG_RUNTIME_DIR=/run/user/$(id -u) /usr/local/bin/lumo-screenshot --output live.png
```

## Session Troubleshooting

If GDM returns straight to the greeter, inspect the compositor logs from the
display-manager session first:

```sh
journalctl -b --no-pager | grep -iE 'gdm-wayland-session|lumo-compositor|lumo-shell'
```

The key breakpoint is whether Lumo reaches `output ...: ready` and starts the
bundled shell clients. If it does, the problem is usually in compositor
surface bring-up rather than the GDM session entry itself.
If the device shows the launcher rectangles and keyboard outline before the
session drops, the crash is later in rendering or surface teardown, not in the
session selection path.
If the session stays up but the launcher does not open, tap or drag on the
bottom gesture pill and look for the temporary touch debug marker on that
surface. That marker confirms the compositor is receiving and classifying touch
input, which helps narrow the problem to gesture policy versus missing device
events.
For broader OrangePi touch audits, Lumo now also logs touch-down audit lines
with raw percentages, logical percentages, and edge or corner region names, so
we can compare the physical panel input against compositor hitboxes without a
separate calibration tool.
Once the in-session touch audit overlay completes its 8-point walk, Lumo also
saves a per-device JSON profile under the user's `lumo/touch-profiles`
directory for later review. In production-style sessions, that audit is
intentionally entered through debug tooling rather than the top-edge gesture so
the launcher path stays predictable on touch devices.
