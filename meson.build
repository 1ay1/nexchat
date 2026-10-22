project('nexchat', 'c',
  version: '0.2.1',
  meson_version: '>= 0.59.0',
  default_options: [
    'c_std=gnu11',
    'buildtype=debugoptimized',
    'warning_level=1',
  ]
)

i18n = import('i18n')
gnome = import('gnome')
cc = meson.get_compiler('c')


libgio_dep = dependency('gio-2.0', version: '>= 2.36.0')
libgmodule_dep = dependency('gmodule-2.0')

libcanberra_dep = dependency('libcanberra', version: '>= 0.22',
                             required: get_option('libcanberra'))
# the remote API and --existing are GDBus (gio); off on Windows
use_dbus = get_option('dbus').allowed() and host_machine.system() != 'windows'
if get_option('dbus').enabled() and not use_dbus
  error('dbus: not supported on Windows')
endif

global_deps = []
if cc.get_id() == 'msvc'
  libssl_dep = cc.find_library('libssl')
else
  libssl_dep = dependency('openssl', version: '>= 0.9.8',
                          required: get_option('tls'))
endif

config_h = configuration_data()
config_h.set_quoted('PACKAGE_VERSION', meson.project_version())
config_h.set_quoted('PACKAGE_NAME', meson.project_name())
config_h.set_quoted('GETTEXT_PACKAGE', 'nexchat')
config_h.set_quoted('LOCALEDIR', join_paths(get_option('prefix'),
                                 get_option('datadir'), 'locale'))
config_h.set_quoted('G_LOG_DOMAIN', 'nexchat')
config_h.set10('ENABLE_NLS', true)

# Optional features
config_h.set('USE_OPENSSL', libssl_dep.found())
config_h.set('USE_LIBCANBERRA', libcanberra_dep.found())
config_h.set('USE_DBUS', use_dbus)
config_h.set('USE_PLUGIN', get_option('plugin'))

config_h.set('G_DISABLE_SINGLE_INCLUDES', true)
# No deprecated API. These hide every deprecated declaration, so
# reintroducing one is a compile error, not a warning.
config_h.set('GTK_DISABLE_DEPRECATED', true)
config_h.set('GDK_DISABLE_DEPRECATED', true)
config_h.set('GTK_DISABLE_SINGLE_INCLUDES', true)
config_h.set('GDK_PIXBUF_DISABLE_SINGLE_INCLUDES', true)
# GTK 4.14 needs GLib 2.76 itself, so the build may use what 2.76 has
config_h.set('GLIB_VERSION_MAX_ALLOWED', 'GLIB_VERSION_2_76')
config_h.set('GLIB_VERSION_MIN_REQUIRED', 'GLIB_VERSION_2_76')

# Detected features
config_h.set('HAVE_MEMRCHR', cc.has_function('memrchr'))
config_h.set('HAVE_STRINGS_H', cc.has_header('strings.h'))

config_h.set_quoted('HEXCHATLIBDIR',
  join_paths(get_option('prefix'), get_option('libdir'), 'nexchat/plugins')
)
# where `nexchat --tui` looks for nexchat-tui (src/fe-gtk/fe-gtk.c)
config_h.set_quoted('NEXCHAT_LIBEXECDIR',
  join_paths(get_option('prefix'), get_option('libexecdir'), 'nexchat')
)

if libssl_dep.found()
  config_h.set('HAVE_X509_GET_SIGNATURE_NID',
    cc.has_function('X509_get_signature_nid', dependencies: libssl_dep)
  )
  config_h.set('HAVE_SSL_CTX_GET_SSL_METHOD',
    cc.has_function('SSL_CTX_get_ssl_method', dependencies: libssl_dep)
  )
  config_h.set('HAVE_DH_SET0_PQG',
    cc.has_function('DH_set0_pqg', dependencies: libssl_dep)
  )
  config_h.set('HAVE_DH_GET0_KEY',
    cc.has_function('DH_get0_key', dependencies: libssl_dep)
  )
  config_h.set('HAVE_DH_SET0_KEY',
    cc.has_function('DH_set0_key', dependencies: libssl_dep)
  )
  config_h.set('HAVE_ERR_REMOVE_THREAD_STATE',
    cc.has_function('ERR_remove_thread_state', dependencies: libssl_dep)
  )
  config_h.set('HAVE_ASN1_STRING_GET0_DATA',
    cc.has_function('ASN1_STRING_get0_data', dependencies: libssl_dep)
  )
endif

configure_file(output: 'config.h', configuration: config_h)
config_h_include = include_directories('.')

if host_machine.system() == 'windows'
  add_project_arguments(
    '-DWIN32',
    '-DNTDDI_VERSION=NTDDI_WIN7',
    '-D_WIN32_WINNT=_WIN32_WINNT_WIN7',
     language: 'c')
endif


global_cflags = []
test_cflags = [
  '-funsigned-char',
  '-Wno-conversion',
  '-Wno-pointer-sign',
  '-Wno-padded',
  '-Wno-unused-parameter',
  '-Wno-missing-prototypes',
  '-Winline',
  '-Wstrict-prototypes',
  '-Werror=implicit-function-declaration',
  '-Werror=pointer-arith',
  '-Werror=init-self',
  ['-Werror=format-security', '-Werror=format=1'],
  '-Werror=missing-include-dirs',
  '-Werror=date-time',
]
foreach cflag : test_cflags
  if cc.has_multi_arguments(cflag)
    global_cflags += cflag
  endif
endforeach
if get_option('buildtype') != 'plain'
  if cc.has_argument('-fstack-protector-strong') and cc.links('''
     int main (void) {
       char buffer[16];
       strcpy(buffer, "foo");
       return 0;
     }
     ''', args: '-fstack-protector-all')
    global_cflags += '-fstack-protector-strong'

    if host_machine.system() == 'windows'
      global_deps += cc.find_library('ssp')
    endif
  endif
endif
add_project_arguments(global_cflags, language: 'c')


global_ldflags = []
test_ldflags = [
  '-Wl,-z,relro',
  '-Wl,-z,now',
  # mingw
  '-Wl,--nxcompat',
]
if not (host_machine.system() == 'windows' and get_option('debug'))
 test_ldflags += '-Wl,--dynamicbase'
endif
foreach ldflag : test_ldflags
  has_arg = cc.has_link_argument(ldflag)

  if has_arg and cc.links('int main (void) { return 0; }', args: ldflag)
    global_ldflags += ldflag
  endif
endforeach
add_project_link_arguments(global_ldflags, language: 'c')

subdir('src')
if get_option('plugin')
  subdir('plugins')
endif
if cc.get_id() != 'msvc'
  subdir('data')
  subdir('po') # FIXME: build xgettext

  meson.add_install_script('meson_post_install.py',
    '@0@'.format(get_option('theme-manager'))
  )
endif

# macOS: `ninja -C build app` makes a self-contained build/nexchat.app
# (osx/make-app.py); osx/check-app.sh checks it runs on its own
if host_machine.system() == 'darwin' and get_option('gtk-frontend')
  app_deps = [hexchat_gtk]
  if is_variable('nexchat_tui')
    app_deps += nexchat_tui
  endif
  run_target('app',
    command: [find_program('python3'), files('osx/make-app.py'), meson.current_build_dir()],
    depends: app_deps,
  )
endif

summary({
  'prefix': get_option('prefix'),
  'bindir': get_option('bindir'),
  'libdir': get_option('libdir'),
  'datadir': get_option('datadir'),
}, section: 'Directories')

summary({
  'GTK': get_option('gtk-frontend') ? '4' : 'none',
  'TLS (openssl)': libssl_dep.found(),
  'Plugin Support': get_option('plugin'),
  'DBus Support': use_dbus,
  'libcanberra': libcanberra_dep.found(),
}, section: 'Features')

summary({
  'Lua': get_option('with-lua'),
  'Python': get_option('with-python'),
  'Perl': get_option('with-perl'),
  'Perl Legacy API': get_option('with-perl-legacy-api'),
  'FiSH': get_option('with-fishlim'),
  'Sysinfo': get_option('with-sysinfo'),
  'DCC Checksum': get_option('with-checksum'),
}, section: 'Plugins')
