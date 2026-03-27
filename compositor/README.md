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

The compositor build accepts a Meson feature flag for xWayland support:

```sh
meson setup build -Dxwayland=disabled
meson compile -C build
meson test -C build --print-errorlogs
```

Leave the option enabled for normal desktop-app compatibility. Disable it for
smaller builds or environments where xWayland is not available.
