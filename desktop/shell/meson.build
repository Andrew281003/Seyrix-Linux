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
gio_unix = dependency('gio-unix-2.0')
cairo = dependency('cairo')
pango = dependency('pango')
pangocairo = dependency('pangocairo')
gdk_pixbuf = dependency('gdk-pixbuf-2.0')
xkbcommon = dependency('xkbcommon')
math = meson.get_compiler('c').find_library('m', required: true)
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
  files(
    'src/main.c',
    'src/app-catalog.c',
    'src/shell-config.c',
    'src/shell-state.c',
    'src/shell-ui.c',
  ) + [layer_shell_header, layer_shell_code, xdg_shell_code],
  c_args: ['-D_POSIX_C_SOURCE=200809L'],
	dependencies: [wayland_client, gio_unix, cairo, pango, pangocairo,
		gdk_pixbuf, xkbcommon, math],
  install: true,
  install_dir: get_option('bindir'),
)

install_data('data/Manual.txt', install_dir: join_paths(get_option('datadir'), 'seyrix'))

shell_state_test = executable(
  'seyrix-shell-state-test',
  files('src/shell-state.c', '../../tests/shell-state-test.c'),
	include_directories: include_directories('src'),
  c_args: ['-D_POSIX_C_SOURCE=200809L'],
)
test('shell state model', shell_state_test)

shell_config_test = executable(
  'seyrix-shell-config-test',
  files('src/shell-config.c', '../../tests/shell-config-test.c'),
	include_directories: include_directories('src'),
  c_args: ['-D_POSIX_C_SOURCE=200809L'],
)
test('shell configuration', shell_config_test)

shell_app_catalog_test = executable(
  'seyrix-shell-app-catalog-test',
  files('src/app-catalog.c', '../../tests/shell-app-catalog-test.c'),
	include_directories: include_directories('src'),
  dependencies: [gio_unix],
)
test('shell application catalog', shell_app_catalog_test)

shell_ui_test = executable(
	'seyrix-shell-ui-test',
	files(
		'src/shell-ui.c',
		'src/shell-config.c',
		'src/shell-state.c',
		'src/app-catalog.c',
		'../../tests/shell-ui-test.c',
	),
	include_directories: include_directories('src'),
	dependencies: [gio_unix, cairo, pango, pangocairo, gdk_pixbuf, math],
)
test('shell user interface rendering', shell_ui_test)

session_lock_test = find_program('../../tests/session-lock-test.sh')
test('session lock lifecycle', session_lock_test, timeout: 5)
