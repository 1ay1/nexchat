hexchat_gtk_sources = [
  'ascii.c',
  'banlist.c',
  'chanlist.c',
  'chanview.c',
  'dccgui.c',
  'editlist.c',
  'fe-gtk.c',
  'fkeys.c',
  'gtkutil.c',
  'gtkevents.c',
  'gtkdnd.c',
  'nxmenu.c',
  'nxlist.c',
  'nxdialog.c',
  'ignoregui.c',
  'joind.c',
  'menu.c',
  'maingui.c',
  'notifygui.c',
  'palette.c',
  'palette-c16.c',
  'pixmaps.c',
  'plugin-tray.c',
  'plugin-notification.c',
  'rawlog.c',
  'servlistgui.c',
  'setup.c',
  'sexy-spell-entry.c',
  'textgui.c',
  'urlgrab.c',
  'userlistgui.c',
  'xtext.c'
]

if get_option('gtk') == '4'
  gtk_dep = dependency('gtk4', version: '>= 4.14.0')
  # no GtkStatusIcon in GTK4: a StatusNotifierItem over D-Bus on Unix
  # desktops; Windows/macOS have no tray on GTK4 yet
  if host_machine.system() in ['windows', 'darwin']
    hexchat_gtk_sources += 'tray-backend-none.c'
  else
    hexchat_gtk_sources += 'tray-backend-sni.c'
  endif
else
  gtk_dep = dependency('gtk+-3.0', version: '>= 3.22.0')
  hexchat_gtk_sources += 'tray-backend-statusicon.c'
endif
hexchat_gtk_deps = [
  hexchat_common_dep,
  libgmodule_dep, # used by libsexy
  gtk_dep
]

# GTK2 exposed a single 'target'; GTK3 builds several backends, see 'targets'
if gtk_dep.get_variable(pkgconfig: 'targets').split().contains('x11')
  hexchat_gtk_deps += dependency('x11')
endif

hexchat_gtk_cflags = []
# phase 5: no deprecated GTK4 API either; the few kept on purpose (X11
# getters, the font/colour chooser dialogs) sit behind
# G_GNUC_BEGIN_IGNORE_DEPRECATIONS
if get_option('gtk') == '4'
  hexchat_gtk_cflags += cc.get_supported_arguments('-Werror=deprecated-declarations')
endif

hexchat_gtk_ldflags = []

if host_machine.system() == 'windows'
  hexchat_gtk_sources += 'notifications/notification-windows.c'

  # TODO: mingw doesn't have these headers or libs
  # add_languages('cpp')
  # shared_module('hcnotifications-winrt',
  #  sources: 'notifications/notification-winrt.cpp'
  #)

else
  hexchat_gtk_sources += 'notifications/notification-freedesktop.c'
endif

iso_codes = dependency('iso-codes', required: false)
if iso_codes.found()
  hexchat_gtk_sources += 'sexy-iso-codes.c'
  iso_codes_prefix = iso_codes.get_pkgconfig_variable('prefix')
  hexchat_gtk_cflags += '-DISO_CODES_PREFIX="@0@"'.format(iso_codes_prefix)
  hexchat_gtk_cflags += '-DISO_CODES_LOCALEDIR="@0@"'.format(
                        join_paths(iso_codes_prefix, 'share/locale'))
endif

if get_option('plugin')
  hexchat_gtk_sources += 'plugingui.c'
endif

resources = gnome.compile_resources('resources',
  '../../data/hexchat.gresource.xml',
  source_dir: '../../data', # TODO: Fix upstream
  c_name: 'hexchat',
  extra_args: ['--manual-register']
)

if host_machine.system() == 'windows'
  hexchat_gtk_ldflags += '-Wl,-e,mainCRTStartup'
endif

# Wayland has no urgency hint: ask through xdg-activation instead
wl_client = dependency('wayland-client', required: false)
wl_protocols = dependency('wayland-protocols', required: false)
wl_scanner = find_program('wayland-scanner', required: false)
if gtk_dep.get_variable(pkgconfig: 'targets').split().contains('wayland') and \
   wl_client.found() and wl_protocols.found() and wl_scanner.found()
  xml = wl_protocols.get_variable(pkgconfig: 'pkgdatadir') / 'staging/xdg-activation/xdg-activation-v1.xml'
  hexchat_gtk_sources += [
    'wlactivate.c',
    custom_target('xdg-activation-client-header', input: xml,
      output: 'xdg-activation-v1-client-protocol.h',
      command: [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@']),
    custom_target('xdg-activation-code', input: xml,
      output: 'xdg-activation-v1-protocol.c',
      command: [wl_scanner, 'private-code', '@INPUT@', '@OUTPUT@']),
  ]
  hexchat_gtk_deps += wl_client
  hexchat_gtk_cflags += '-DHAVE_WLACTIVATE'
endif

hexchat_gtk = executable('nexchat',
  sources:  resources + hexchat_gtk_sources,
  dependencies: hexchat_gtk_deps,
  c_args: hexchat_gtk_cflags,
  link_args: hexchat_gtk_ldflags,
  pie: true,
  install: true,
  gui_app: true,
)

# colors.conf values must survive GdkRGBA unchanged (no GTK needed)
test('palette-roundtrip', executable('palette-roundtrip',
  sources: ['../../tests/palette-roundtrip.c', 'palette-c16.c'],
  dependencies: libgio_dep,
))

# gtkcompat.h must give GTK3's packing order/semantics; run against the
# GTK this frontend builds with (the GTK4 run is in CI / by hand)
gtkcompat_test = executable('gtkcompat-test',
  sources: ['../../tests/gtkcompat-test.c'],
  include_directories: include_directories('.'),
  dependencies: gtk_dep,
)
if not gtk_dep.get_variable(pkgconfig: 'targets').split().contains('x11')
  # native backend (Quartz/Win32): no X server needed
  test('gtkcompat', gtkcompat_test)
endif

# gtkdnd.c must set up the same drag targets the old code did (GTK3 only)
gtkdnd_test = disabler ()
if get_option('gtk') == '3'
gtkdnd_test = executable('gtkdnd-test',
  sources: ['../../tests/gtkdnd-test.c', 'gtkdnd.c'],
  include_directories: include_directories('.'),
  dependencies: gtk_dep,
)
endif
if not gtk_dep.get_variable(pkgconfig: 'targets').split().contains('x11')
  test('gtkdnd', gtkdnd_test)
endif

# nxmenu.c: the same menu semantics on GTK3 (GtkMenu) and GTK4 (GMenu)
nxmenu_test = executable('nxmenu-test',
  sources: ['../../tests/nxmenu-test.c', 'nxmenu.c'],
  include_directories: include_directories('.'),
  dependencies: gtk_dep,
)
if not gtk_dep.get_variable(pkgconfig: 'targets').split().contains('x11')
  test('nxmenu', nxmenu_test)
endif

# nxlist.c: the same list/tree semantics on GTK3 (GtkTreeView) and GTK4
nxlist_test = executable('nxlist-test',
  sources: ['../../tests/nxlist-test.c', 'nxlist.c'],
  include_directories: include_directories('.'),
  dependencies: gtk_dep,
)
if not gtk_dep.get_variable(pkgconfig: 'targets').split().contains('x11')
  test('nxlist', nxlist_test)
endif

# Backend-independent smoke test: connect, join, render (no XTest needed).
# On X11 backends it runs under a private Xvfb.
if not gtk_dep.get_variable(pkgconfig: 'targets').split().contains('x11')
  test('smoke-gui', find_program('sh'),
    args: [files('../../tests/smoke-gui.sh'), hexchat_gtk],
    timeout: 60)
endif

# The tests that only need a display also run on Wayland, under a private
# headless weston (tests/ux/with-weston.sh).
weston = find_program('weston', required: false)
if weston.found() and gtk_dep.get_variable(pkgconfig: 'targets').split().contains('wayland')
  sh = find_program('sh').full_path()
  wl_env = ['WESTON=' + weston.full_path(), 'G_DEBUG=fatal-criticals']
  wl_with = files('../../tests/ux/with-weston.sh')
  test('wayland-smoke', find_program('sh'),
    args: [wl_with, sh, files('../../tests/smoke-gui.sh'), hexchat_gtk],
    env: wl_env, timeout: 60)
  test('wayland-gtkcompat', find_program('sh'),
    args: [wl_with, gtkcompat_test], env: wl_env, timeout: 60)
  if get_option('gtk') == '3'
    test('wayland-gtkdnd', find_program('sh'),
      args: [wl_with, gtkdnd_test], env: wl_env, timeout: 60)
  endif
  test('wayland-nxmenu', find_program('sh'),
    args: [wl_with, nxmenu_test], env: wl_env, timeout: 60)
  test('wayland-nxlist', find_program('sh'),
    args: [wl_with, nxlist_test], env: wl_env, timeout: 60)
  if use_dbus
    test('wayland-dbus-remote', find_program('sh'),
      args: [wl_with, sh, files('../../tests/dbus-remote.sh'), hexchat_gtk, dbus_service_name],
      env: wl_env, timeout: 90)
    if get_option('gtk') == '4'
      test('wayland-sni', find_program('sh'),
        args: [wl_with, sh, files('../../tests/sni.sh'), hexchat_gtk],
        # no seat: a focused window doesn't blink the tray
        env: wl_env + ['NX_NO_SEAT=1'], timeout: 60)
    endif
  endif
endif

# Urgency on Wayland: sway reports it over IPC, weston doesn't
sway = find_program('sway', required: false)
if sway.found() and gtk_dep.get_variable(pkgconfig: 'targets').split().contains('wayland') and use_dbus
  test('wayland-urgency', find_program('sh'),
    args: [files('../../tests/ux/with-sway.sh'), find_program('sh').full_path(),
      files('../../tests/ux/wl-urgency.sh'), hexchat_gtk],
    env: ['SWAY=' + sway.full_path(), 'G_DEBUG=fatal-criticals'], timeout: 60)
endif

# GUI tests: drive the real binary on a private Xvfb with XTest keys.
# Need Xvfb + libXtst (+ xwd for colors); skipped (exit 77) where missing.
xvfb = find_program('Xvfb', '/opt/X11/bin/Xvfb', required: false)
if xvfb.found() and gtk_dep.get_variable(pkgconfig: 'targets').split().contains('x11')
  # a GTK critical anywhere fails the test (the client aborts). Each test
  # has its own display, config dir, ircd port and (dbus-remote) bus, so
  # they run in parallel.
  gui_env = ['XVFB=' + xvfb.full_path(), 'G_DEBUG=fatal-criticals']
  if use_dbus
    test('dbus-remote', find_program('sh'),
      args: [files('../../tests/dbus-remote.sh'), hexchat_gtk, dbus_service_name],
      env: gui_env + ['DISP=:89'], timeout: 90)
    if get_option('gtk') == '4'
      test('gui-sni', find_program('sh'),
        args: [files('../../tests/sni.sh'), hexchat_gtk],
        env: gui_env + ['DISP=:72'], timeout: 60)
    endif
  endif
  test('gui-wm', find_program('sh'),
    args: [files('../../tests/ux/wm.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:79'], timeout: 90)
  test('gtkcompat', find_program('sh'),
    args: [files('../../tests/ux/with-xvfb.sh'), gtkcompat_test],
    env: gui_env + ['DISP=:84'], timeout: 60)
  test('gtkdnd', find_program('sh'),
    args: [files('../../tests/ux/with-xvfb.sh'), gtkdnd_test],
    env: gui_env + ['DISP=:86'], timeout: 60)
  test('nxmenu', find_program('sh'),
    args: [files('../../tests/ux/with-xvfb.sh'), nxmenu_test],
    env: gui_env + ['DISP=:87'], timeout: 60)
  test('nxlist', find_program('sh'),
    args: [files('../../tests/ux/with-xvfb.sh'), nxlist_test],
    env: gui_env + ['DISP=:88'], timeout: 60)
  test('smoke-gui', find_program('sh'),
    args: [files('../../tests/ux/with-xvfb.sh'), find_program('sh').full_path(),
           files('../../tests/smoke-gui.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:73'], timeout: 60)
  test('gui-keys', find_program('sh'),
    args: [files('../../tests/ux/keys.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:81'], timeout: 120)
  test('gui-mouse', find_program('sh'),
    args: [files('../../tests/ux/mouse.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:85'], timeout: 120)
  test('gui-tray', find_program('sh'),
    args: [files('../../tests/ux/tray.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:74'], timeout: 60)
  test('gui-spell', find_program('sh'),
    args: [files('../../tests/ux/spell.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:76'], timeout: 90)
  test('gui-menus', find_program('sh'),
    args: [files('../../tests/ux/menus.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:82'], timeout: 180)
  test('gui-colors-roundtrip', find_program('sh'),
    args: [files('../../tests/ux/colors-roundtrip.sh'), hexchat_gtk],
    env: gui_env + ['SHOT_DISPLAY=:83'], timeout: 120)
endif
