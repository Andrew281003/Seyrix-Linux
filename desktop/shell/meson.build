project(
  'seyrix-shell',
  'c',
  version: '0.1.0',
  default_options: [
    'c_std=c17',
    'warning_level=2',
    'werror=true',
  ],
)

wayland_client = dependency('wayland-client')
wayland_protocols = dependency('wayland-protocols')
wlr_protocols = dependency('wlr-protocols')
wayland_scanner = find_program('wayland-scanner')
layer_shell_xml = join_paths(
  wlr_protocols.get_variable(pkgconfig: 'pkgdatadir'),
  'unstable',
  'wlr-layer-shell-unstable-v1.xml',
)
xdg_shell_xml = join_paths(
  wayland_protocols.get_variable(pkgconfig: 'pkgdatadir'),
  'stable',
  'xdg-shell',
  'xdg-shell.xml',
)

layer_shell_header = custom_target(
  'layer-shell-client-header',
  input: layer_shell_xml,
  output: 'wlr-layer-shell-unstable-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)
layer_shell_code = custom_target(
  'layer-shell-client-code',
  input: layer_shell_xml,
  output: 'wlr-layer-shell-unstable-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)
xdg_shell_code = custom_target(
  'xdg-shell-client-code',
  input: xdg_shell_xml,
  output: 'xdg-shell-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

executable(
  'seyrix-shell',
  files('src/main.c') + [layer_shell_header, layer_shell_code, xdg_shell_code],
  c_args: ['-D_POSIX_C_SOURCE=200809L'],
  dependencies: [wayland_client],
  install: true,
  install_dir: get_option('bindir'),
)
