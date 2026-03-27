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

That install step drops both the normal `Lumo` session and the
`Lumo Headless Debug` session into the Wayland session directory.
The normal session launches `lumo-compositor --backend drm --shell lumo-shell`
so the compositor and bundled shell clients start together after login.
When GDM launches the session, the compositor can still use DRM even if the
process has no controlling tty.
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
```

The OrangePi RV2 on Ubuntu 24.04 riscv64 should normally use `drm` for the
physical touchscreen and for the login-screen session. `headless`, `wayland`,
and `x11` are there to make debugging and nested bring-up easier when we want
to isolate backend problems.
`drm` is still the wrong choice for SSH and other non-seat shells, but it does
not require a visible tty when GDM or another display manager starts the
session.
If you leave the backend in `auto` from SSH or any other non-VT shell, Lumo
now prefers the safest available nested or headless backend instead of waiting
for the DRM path to time out.
SSH shells fall back to `headless` first; a local GUI session can still steer
`auto` toward nested `wayland` or `x11` when that makes more sense.
