project(
  'seyrix-compositor',
  'c',
  version: '0.1.0',
  default_options: [
    'c_std=c17',
    'warning_level=2',
    'werror=true',
  ],
)

wlroots = dependency('wlroots-0.20', version: ['>=0.20.0', '<0.21.0'])
wayland_server = dependency('wayland-server')
xkbcommon = dependency('xkbcommon')
wlr_protocols = dependency('wlr-protocols')
wayland_scanner = find_program('wayland-scanner')

layer_shell_protocol = custom_target(
  'layer-shell-protocol-header',
  input: join_paths(
    wlr_protocols.get_variable(pkgconfig: 'pkgdatadir'),
    'unstable',
    'wlr-layer-shell-unstable-v1.xml',
  ),
  output: 'wlr-layer-shell-unstable-v1-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

output_management_protocol = custom_target(
  'output-management-protocol-header',
  input: join_paths(
    wlr_protocols.get_variable(pkgconfig: 'pkgdatadir'),
    'unstable',
    'wlr-output-management-unstable-v1.xml',
  ),
  output: 'wlr-output-management-unstable-v1-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

executable(
  'seyrix-compositor',
  files(
    'src/main.c',
    'src/output.c',
    'src/input.c',
    'src/window.c',
    'src/window-state.c',
    'src/popup.c',
    'src/decoration.c',
    'src/layer-shell.c',
    'src/shell.c',
    'src/workspace.c',
    'src/layout-engine.c',
    'src/wm-config.c',
    'src/window-management.c',
    '../shell/src/shell-config.c',
  ) + [layer_shell_protocol, output_management_protocol],
  c_args: ['-DWLR_USE_UNSTABLE', '-D_POSIX_C_SOURCE=200809L'],
  dependencies: [wlroots, wayland_server, xkbcommon],
  install: true,
  install_dir: get_option('bindir'),
)

geometry_test = executable(
  'compositor-geometry-test',
  '../../tests/compositor-geometry-test.c',
  dependencies: [wlroots, wayland_server],
)
test('compositor geometry', geometry_test)

window_management_test = executable(
  'compositor-window-management-test',
  files(
    '../../tests/compositor-window-management-test.c',
    'src/layout-engine.c',
    'src/wm-config.c',
  ),
  c_args: ['-D_POSIX_C_SOURCE=200809L'],
)
test('compositor window management', window_management_test)
