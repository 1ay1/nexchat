dbus_deps = [
  libgio_dep
]

dbus_sources = [
  'dbus-plugin.c',
  'dbus-client.c'
]

dbus_includes = [
 include_directories('..'),
 config_h_include
]

dbus_service_dir = join_paths(get_option('datadir'), 'dbus-1/services')

# bus name is nexchat's own so --existing never reaches a running HexChat.
# object paths and interfaces stay org.hexchat.* so remote scripts keep working.
if get_option('dbus-service-use-appid')
  dbus_service_name = 'io.github.nexchat'
  dbus_output_file = 'io.github.nexchat.service'
  dbus_cargs = '-DDBUS_SERVICE="io.github.nexchat"'
else
  dbus_service_name = 'io.github.nexchat.service'
  dbus_output_file = 'io.github.nexchat.service.service'
  dbus_cargs = '-DDBUS_SERVICE="io.github.nexchat.service"'
endif

dbus_conf = configuration_data()
dbus_conf.set('bindir', join_paths(get_option('prefix'), get_option('bindir')))
dbus_conf.set('service_name', dbus_service_name)
configure_file(
  input: 'nexchat.service.in',
  output: dbus_output_file,
  configuration: dbus_conf,
  install: true,
  install_dir: dbus_service_dir
)

hexchat_dbus = static_library('hexchatdbus',
  sources: dbus_sources,
  c_args: dbus_cargs,
  dependencies: common_deps + dbus_deps,
  include_directories: dbus_includes,
  pic: true
)

hexchat_dbus_dep = declare_dependency(
  link_with: hexchat_dbus
)
