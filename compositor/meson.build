project(
  'lumo-compositor',
  'c',
  version: '0.0.34',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ],
)

lumo_inc = include_directories('include')
xwayland_option = get_option('xwayland')
xwayland_enabled = not xwayland_option.disabled()
orangepi_touch_quirks_option = get_option('orangepi_touch_quirks')
orangepi_touch_quirks_enabled = not orangepi_touch_quirks_option.disabled()
udev_rules_dir = get_option('udev_rules_dir')

lumo_deps = [
  dependency('wlroots-0.18', required: true),
  dependency('wayland-server', required: true),
  dependency('xkbcommon', required: true),
  dependency('wayland-protocols', required: true),
]

shell_deps = [
  dependency('wayland-client', required: true),
  dependency('wayland-protocols', required: true),
]

screenshot_deps = [
  dependency('wayland-client', required: true),
  dependency('wayland-protocols', required: true),
  dependency('libpng', required: true),
]

add_project_arguments('-DWLR_USE_UNSTABLE', language: 'c')
add_project_arguments('-D_POSIX_C_SOURCE=200809L', language: 'c')
add_project_arguments(
  '-DLUMO_ENABLE_XWAYLAND=' + (xwayland_enabled ? '1' : '0'),
  language: 'c',
)

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

shell_protocol_files = [
  custom_target(
    'xdg-shell-client-protocol-code',
    input: join_paths(wayland_protocols_dir, 'stable/xdg-shell/xdg-shell.xml'),
    output: 'xdg-shell-client-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  ),
  custom_target(
    'wlr-layer-shell-client-protocol-header',
    input: files('protocol/wlr-layer-shell-unstable-v1.xml'),
    output: 'wlr-layer-shell-unstable-v1-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  ),
  custom_target(
    'wlr-layer-shell-client-protocol-code',
    input: files('protocol/wlr-layer-shell-unstable-v1.xml'),
    output: 'wlr-layer-shell-unstable-v1-client-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  ),
]

screenshot_protocol_files = [
  custom_target(
    'wlr-screencopy-client-protocol-header',
    input: files('protocol/wlr-screencopy-unstable-v1.xml'),
    output: 'wlr-screencopy-unstable-v1-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  ),
  custom_target(
    'wlr-screencopy-client-protocol-code',
    input: files('protocol/wlr-screencopy-unstable-v1.xml'),
    output: 'wlr-screencopy-unstable-v1-client-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  ),
]

lumo_sources = files(
  'src/core/backend.c',
  'src/core/compositor.c',
  'src/core/input.c',
  'src/core/output.c',
  'src/core/touch_audit.c',
  'src/protocol/protocol.c',
  'src/shell/shell_launch.c',
  'src/shell/shell_osk.c',
  'src/protocol/shell_protocol.c',
  'src/shell/shell_ui.c',
  'src/core/xwayland.c',
) + protocol_headers

shell_sources = files(
  'src/shell/shell_osk.c',
  'src/shell/shell_ui.c',
  'src/shell/shell_client.c',
  'src/protocol/shell_protocol.c',
) + shell_protocol_files

screenshot_sources = files(
  'src/tools/screenshot.c',
  'src/tools/screenshot_common.c',
) + screenshot_protocol_files

session_conf = configuration_data()
session_conf.set('lumo_bindir', join_paths(get_option('prefix'), get_option('bindir')))
session_desktop = configure_file(
  input: 'data/lumo.desktop.in',
  output: 'lumo.desktop',
  configuration: session_conf,
)
session_debug_desktop = configure_file(
  input: 'data/lumo-headless.desktop.in',
  output: 'lumo-headless.desktop',
  configuration: session_conf,
)

executable(
  'lumo-compositor',
  lumo_sources + files('src/core/main.c'),
  include_directories: lumo_inc,
  dependencies: lumo_deps,
  install: true,
)

executable(
  'lumo-shell',
  shell_sources,
  include_directories: lumo_inc,
  dependencies: shell_deps,
  install: true,
)

executable(
  'lumo-screenshot',
  screenshot_sources,
  include_directories: lumo_inc,
  dependencies: screenshot_deps,
  install: true,
)

install_data(
  session_desktop,
  session_debug_desktop,
  install_dir: join_paths(get_option('datadir'), 'wayland-sessions'),
)

if orangepi_touch_quirks_enabled
  install_data(
    'data/99-lumo-orangepi-touch.rules',
    install_dir: udev_rules_dir,
  )
endif

test_sources = lumo_sources + files('tests/core/test_lumo.c')

test_exe = executable(
  'lumo-compositor-tests',
  test_sources,
  include_directories: lumo_inc,
  dependencies: lumo_deps,
  install: false,
)

test('lumo-compositor', test_exe)

shell_test_sources = files(
  'src/shell/shell_osk.c',
  'src/shell/shell_ui.c',
  'tests/shell/test_shell.c',
)

shell_test_exe = executable(
  'lumo-shell-tests',
  shell_test_sources,
  include_directories: lumo_inc,
  install: false,
)

test('lumo-shell', shell_test_exe)

screenshot_test_exe = executable(
  'lumo-screenshot-tests',
  files(
    'src/tools/screenshot_common.c',
    'tests/tools/test_screenshot.c',
  ),
  include_directories: lumo_inc,
  install: false,
)

test('lumo-screenshot', screenshot_test_exe)

# Add wlroots, wayland-server, libinput, xkbcommon, pixman, libdrm,
# GBM, libseat, and wayland-protocols once the real backend lands.
