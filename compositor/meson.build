project(
  'lumo-compositor',
  'c',
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ],
)

lumo_inc = include_directories('include')

lumo_sources = files(
  'src/backend.c',
  'src/compositor.c',
  'src/input.c',
  'src/main.c',
  'src/output.c',
  'src/protocol.c',
)

executable(
  'lumo-compositor',
  lumo_sources,
  include_directories: lumo_inc,
  install: false,
)

# Add wlroots, wayland-server, libinput, xkbcommon, pixman, libdrm,
# GBM, libseat, and wayland-protocols once the real backend lands.

