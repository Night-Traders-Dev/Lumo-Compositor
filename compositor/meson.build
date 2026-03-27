project(
  'lumo-compositor',
  'c',
  version: '0.0.2',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ],
)

lumo_inc = include_directories('include')

lumo_deps = [
  dependency('wlroots-0.18', required: true),
  dependency('wayland-server', required: true),
  dependency('xkbcommon', required: true),
]

add_project_arguments('-DWLR_USE_UNSTABLE', language: 'c')

lumo_sources = files(
  'src/backend.c',
  'src/compositor.c',
  'src/input.c',
  'src/output.c',
  'src/protocol.c',
)

executable(
  'lumo-compositor',
  lumo_sources + files('src/main.c'),
  include_directories: lumo_inc,
  dependencies: lumo_deps,
  install: false,
)

test_sources = lumo_sources + files('tests/test_lumo.c')

test_exe = executable(
  'lumo-compositor-tests',
  test_sources,
  include_directories: lumo_inc,
  dependencies: lumo_deps,
  install: false,
)

test('lumo-compositor', test_exe)

# Add wlroots, wayland-server, libinput, xkbcommon, pixman, libdrm,
# GBM, libseat, and wayland-protocols once the real backend lands.
