# Lumo Compositor Skeleton

This directory is the starting point for the wlroots-based compositor core.

It is intentionally small right now:

- `src/backend.c` will own wlroots backend startup and shutdown
- `src/input.c` will own seats, touch, pointer, keyboard, and gesture routing
- `src/output.c` will own output layout, scale, and rotation
- `src/protocol.c` will own the custom `lumo-shell` protocol
- `src/compositor.c` will orchestrate the modules
- `src/main.c` will become the compositor entrypoint

The real wlroots integration will come next. This scaffold gives us the module boundary first so shell-client work can move in parallel.

When the implementation lands, this compositor should:

- accept xdg-shell app windows
- support layer-shell shell surfaces
- manage output rotation and touch mapping
- expose shell state to launcher, OSK, bar, and gesture surfaces
- forward OSK text into focused text-input-v3 clients before falling back to lower-level keyboard handling

## Build Toggles

The compositor build accepts a Meson feature flag for xWayland support. From
the repository root, the same flag is available through `build.sh`:

```sh
./build.sh --xwayland disabled --test
```

Leave the option enabled for normal desktop-app compatibility. Disable it for
smaller builds or environments where xWayland is not available.

The build also installs a Wayland session file when you run `meson install`:

```sh
meson install -C build
```

That session entry launches `lumo-compositor --backend drm --shell lumo-shell`
so the compositor and bundled shell clients start together after login.
Use `sudo` for `meson install` if the install prefix points at a system
directory.

## Runtime Backends

Lumo can be launched with an explicit backend mode:

```sh
./build/lumo-compositor --backend drm
./build/lumo-compositor --backend headless
./build/lumo-compositor --backend wayland
./build/lumo-compositor --backend x11
```

The OrangePi RV2 on Ubuntu 24.04 riscv64 should normally use `drm` for the
physical touchscreen. `headless`, `wayland`, and `x11` are there to make
debugging and nested bring-up easier when we want to isolate backend problems.
`drm` needs a local VT or seat-managed login. SSH is fine for inspecting logs,
but it is only a good fit when you are intentionally using `headless` or another
nested backend.
If you leave the backend in `auto` from SSH or any other non-VT shell, Lumo
now prefers the safest available nested or headless backend instead of waiting
for the DRM path to time out.
SSH shells fall back to `headless` first; a local GUI session can still steer
`auto` toward nested `wayland` or `x11` when that makes more sense.
