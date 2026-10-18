pkg_conf = configuration_data()
prefix = get_option('prefix')
pkg_conf.set('prefix', prefix)
pkg_conf.set('VERSION', meson.project_version())
pkg_conf.set('hexchatlibdir', join_paths('${prefix}', plugindir))
pkg_conf.set('includedir', join_paths('${prefix}', get_option('includedir')))
# without the hexchat-named copies hexchat-plugin.h lives in include/nexchat
pkg_conf.set('extra_cflags', get_option('install-hexchat-compat') ? '' : ' -I${includedir}/nexchat')

# same content under both names so plugins that look for hexchat-plugin still build
pcs = get_option('install-hexchat-compat') ? ['nexchat-plugin.pc', 'hexchat-plugin.pc'] : ['nexchat-plugin.pc']
foreach pc : pcs
  configure_file(
    input: 'nexchat-plugin.pc.in',
    output: pc,
    configuration: pkg_conf,
    install: true,
    install_dir: join_paths(get_option('libdir'), 'pkgconfig'),
  )
endforeach
