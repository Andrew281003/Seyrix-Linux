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

executable(
  'seyrix-compositor',
  files(
    'src/main.c',
    'src/output.c',
    'src/input.c',
    'src/window.c',
  ),
  c_args: ['-DWLR_USE_UNSTABLE', '-D_POSIX_C_SOURCE=200809L'],
  dependencies: [wlroots, wayland_server, xkbcommon],
  install: true,
  install_dir: get_option('bindir'),
)
