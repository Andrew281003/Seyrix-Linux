project(
  'seyrix-session-supervisor',
  'c',
  version: '0.1.0',
  default_options: [
    'c_std=c17',
    'warning_level=2',
    'werror=true',
  ],
)

wayland_client = dependency('wayland-client')

supervisor = executable(
  'seyrix-session-supervisor',
  ['main.c', 'wayland-probe.c'],
  dependencies: [wayland_client],
  install: true,
  install_dir: get_option('bindir'),
)

policy_test = executable(
  'seyrix-session-supervisor-policy-test',
  '../../tests/session-supervisor-policy-test.c',
)
test('session supervisor restart policy', policy_test)

python = find_program('python3')
test('session supervisor control protocol', python,
  args: [
    join_paths(meson.current_source_dir(),
      '../../tests/session-supervisor-control-test.py'),
    supervisor.full_path(),
  ],
  timeout: 15,
)

wayland_probe_test = executable(
  'seyrix-session-supervisor-wayland-test',
  ['../../tests/session-supervisor-wayland-test.c', 'wayland-probe.c'],
  dependencies: [wayland_client],
)
test('session supervisor bounded Wayland probe', wayland_probe_test, timeout: 5)
