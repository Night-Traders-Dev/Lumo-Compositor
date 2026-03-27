project(
  'lumo-compositor',
  'c',
  version: '0.0.4',
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
  dependency('wayland-protocols', required: true),
]

add_project_arguments('-DWLR_USE_UNSTABLE', language: 'c')
add_project_arguments('-D_POSIX_C_SOURCE=200809L', language: 'c')

wayland_scanner = find_program('wayland-scanner', required: true)
wayland_protocols = dependency('wayland-protocols', required: true)
wayland_protocols_dir = wayland_protocols.get_variable(
  pkgconfig: 'pkgdatadir',
)

protocol_headers = [
  custom_target(
    'xdg-shell-protocol',
    input: join_paths(wayland_protocols_dir, 'stable/xdg-shell/xdg-shell.xml'),
    output: 'xdg-shell-protocol.h',
    command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
  ),
  custom_target(
    'wlr-layer-shell-protocol',
    input: files('protocol/wlr-layer-shell-unstable-v1.xml'),
    output: 'wlr-layer-shell-unstable-v1-protocol.h',
    command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
  ),
]

lumo_sources = files(
  'src/backend.c',
  'src/compositor.c',
  'src/input.c',
  'src/output.c',
  'src/protocol.c',
  'src/xwayland.c',
) + protocol_headers

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
