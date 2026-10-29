project(
  'seyrix-settings',
  'c',
  version: '0.1.0',
  default_options: [
    'c_std=c17',
    'warning_level=2',
    'werror=true',
  ],
)

gtk4 = dependency('gtk4', version: '>=4.12')
glib = dependency('glib-2.0')
gio_unix = dependency('gio-unix-2.0')
threads = dependency('threads')
settings_sources = files(
  'src/main.c',
  'src/settings-window.c',
  'src/settings-client.c',
  'src/settings-model.c',
  'src/system-info.c',
)

executable(
  'seyrix-settings',
  settings_sources,
  dependencies: [gtk4],
  install: true,
  install_dir: get_option('bindir'),
)

install_data(
  'data/seyrix-settings.desktop',
  install_dir: join_paths(get_option('datadir'), 'applications'),
)

ipc_test = executable(
  'settings-ipc-test',
  '../../tests/settings-ipc-test.c',
  dependencies: [glib],
)
test('settings IPC frame codec', ipc_test)

model_test = executable(
  'settings-model-test',
  files('../../tests/settings-model-test.c', 'src/settings-model.c'),
  dependencies: [glib],
)
test('settings snapshot model', model_test)

client_test = executable(
  'settings-client-test',
  files('../../tests/settings-client-test.c', 'src/settings-client.c',
    'src/settings-model.c'),
  dependencies: [glib, gio_unix, threads],
)
test('settings client reconnect', client_test, timeout: 20)

endpoint_probe = executable(
  'settings-endpoint-probe',
  files('../../tests/settings-endpoint-probe.c', 'src/settings-model.c'),
  dependencies: [glib],
  install: true,
  install_dir: join_paths(get_option('libexecdir'), 'seyrix-tests'),
)
