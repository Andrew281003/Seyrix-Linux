project('seyrix-protocol-probe', 'c', version: '0.1.0', default_options: ['c_std=c17', 'warning_level=2', 'werror=true'])

wayland_client = dependency('wayland-client')
protocol_data = get_option('wayland_protocols_dir')
scanner = find_program('wayland-scanner')

xdg_shell_xml = join_paths(protocol_data, 'stable', 'xdg-shell', 'xdg-shell.xml')
xdg_decoration_xml = join_paths(protocol_data, 'unstable', 'xdg-decoration', 'xdg-decoration-unstable-v1.xml')

xdg_shell_header = custom_target(
  'xdg-shell-client-header', input: xdg_shell_xml,
  output: 'xdg-shell-client-protocol.h',
  command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
xdg_shell_code = custom_target(
  'xdg-shell-protocol-code', input: xdg_shell_xml,
  output: 'xdg-shell-protocol.c',
  command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
xdg_decoration_header = custom_target(
  'xdg-decoration-client-header', input: xdg_decoration_xml,
  output: 'xdg-decoration-client-protocol.h',
  command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
xdg_decoration_code = custom_target(
  'xdg-decoration-protocol-code', input: xdg_decoration_xml,
  output: 'xdg-decoration-protocol.c',
  command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

executable(
  'seyrix-protocol-probe',
  files('src/probe.c'),
  xdg_shell_header, xdg_shell_code,
  xdg_decoration_header, xdg_decoration_code,
  dependencies: wayland_client,
  install: true,
  install_dir: get_option('bindir'),
)
