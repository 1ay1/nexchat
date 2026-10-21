hexchat_gtk_sources = [
  'ascii.c',
  'cmdpopup.c',
  'jumpto.c',
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
  'platform-style.c',
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
  'bncsetup.c',
  'bncwin.c',
  'setup.c',
  'sexy-spell-entry.c',
  'textgui.c',
  'urlgrab.c',
  'mentions.c',
  'replybar.c',
  'userlistgui.c',
  'xtext.c'
]

# link previews and uploads: the HTTP(S) client in common/httpget.c
if libssl_dep.found()
  hexchat_gtk_sources += ['urlpreview.c', 'upload.c', 'nximage.c']
endif

gtk_dep = dependency('gtk4', version: '>= 4.14.0')
# the tray: a StatusNotifierItem over D-Bus on Unix desktops; Windows:
# Shell_NotifyIcon; macOS: NSStatusItem
if host_machine.system() == 'windows'
  hexchat_gtk_sources += ['tray-backend-win32.c', 'win32-menu.c']
elif host_machine.system() == 'darwin'
  hexchat_gtk_sources += ['tray-backend-macos.m', 'platform-macos.m', 'macos-app.m']
  add_languages('objc', native: false)
else
  hexchat_gtk_sources += 'tray-backend-sni.c'
endif
hexchat_gtk_deps = [
  hexchat_common_dep,
  libgmodule_dep, # used by libsexy
  gtk_dep,
  # libm: pow() in xtext.c (part of libc on macOS and Windows, not on Linux)
  meson.get_compiler('c').find_library('m', required: false),
]
if host_machine.system() == 'darwin'
  hexchat_gtk_deps += dependency('appleframeworks',
                                 modules: ['AppKit', 'UserNotifications', 'Network', 'Security'])
endif

# GTK builds several backends, see 'targets'
if gtk_dep.get_variable(pkgconfig: 'targets').split().contains('x11')
  hexchat_gtk_deps += dependency('x11')
endif

# no deprecated GTK4 API; the few kept on purpose (X11 getters, the
# font/colour chooser dialogs) sit behind G_GNUC_BEGIN_IGNORE_DEPRECATIONS
hexchat_gtk_cflags = cc.get_supported_arguments('-Werror=deprecated-declarations')

hexchat_gtk_ldflags = []

if host_machine.system() == 'windows'
  hexchat_gtk_sources += ['notifications/notification-windows.c',
                          'notifications/notification-balloon.c',
                          'notifications/notification-plain.c']
  # dark title bar
  hexchat_gtk_deps += cc.find_library('dwmapi')

  # TODO: mingw doesn't have these headers or libs
  # add_languages('cpp')
  # shared_module('hcnotifications-winrt',
  #  sources: 'notifications/notification-winrt.cpp'
  #)

elif host_machine.system() == 'darwin'
  hexchat_gtk_sources += 'notifications/notification-osx.m'
else
  hexchat_gtk_sources += ['notifications/notification-freedesktop.c',
                          'notifications/notification-plain.c']
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
  # nexchat.exe's icon and version info (VERSIONINFO wants a,b,c,d)
  ver = (meson.project_version() + '.0.0.0').split('.')
  rc = configure_file(input: '../../win32/nexchat.rc.in', output: 'nexchat.rc',
    configuration: {
      'VERSION': meson.project_version(),
      'COMMA_VERSION': ','.join([ver[0], ver[1], ver[2], ver[3]]),
      'ICON': (meson.project_source_root() / 'data' / 'icons' / 'nexchat.ico').replace('\\', '/'),
    })
  hexchat_gtk_sources += import('windows').compile_resources(rc)
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
  # the AppKit glue is written for ARC: without it the NSStatusItem is never
  # retained, gets freed, and the first icon update crashes
  objc_args: hexchat_gtk_cflags + ['-fobjc-arc'],
  link_args: hexchat_gtk_ldflags,
  pie: true,
  install: true,
  win_subsystem: 'windows',
)

# colors.conf values must survive GdkRGBA unchanged (no GTK needed)
test('palette-roundtrip', executable('palette-roundtrip',
  sources: ['../../tests/palette-roundtrip.c', 'palette-c16.c'],
  dependencies: libgio_dep,
))

# gtkcompat.h's packing helpers must keep their semantics
gtkcompat_test = executable('gtkcompat-test',
  sources: ['../../tests/gtkcompat-test.c'],
  include_directories: include_directories('.'),
  dependencies: gtk_dep,
)
if not gtk_dep.get_variable(pkgconfig: 'targets').split().contains('x11')
  # native backend (Quartz/Win32): no X server needed
  test('gtkcompat', gtkcompat_test)
endif

# nxmenu.c: menu semantics over GMenu
nxmenu_test_sources = ['../../tests/nxmenu-test.c', 'nxmenu.c']
if host_machine.system() == 'windows'
  nxmenu_test_sources += 'win32-menu.c'
endif
nxmenu_test = executable('nxmenu-test',
  sources: nxmenu_test_sources,
  include_directories: include_directories('.'),
  dependencies: gtk_dep,
)
if not gtk_dep.get_variable(pkgconfig: 'targets').split().contains('x11')
  test('nxmenu', nxmenu_test)
endif

# nxlist.c: list/tree semantics
nxlist_test = executable('nxlist-test',
  sources: ['../../tests/nxlist-test.c', 'nxlist.c'],
  include_directories: include_directories('.'),
  dependencies: gtk_dep,
)
if not gtk_dep.get_variable(pkgconfig: 'targets').split().contains('x11')
  test('nxlist', nxlist_test)
endif

# Backend-independent smoke test: connect, join, render (no XTest needed).
# On X11 backends it runs under a private Xvfb. These share the one
# desktop session (macOS, Windows): one at a time, or they starve each
# other on a small CI runner (prefs-live, select timed out on macOS).
if not gtk_dep.get_variable(pkgconfig: 'targets').split().contains('x11')
  test('smoke-gui', find_program('sh'),
    args: [files('../../tests/smoke-gui.sh'), hexchat_gtk],
    timeout: 60, is_parallel: false)
  # the tab switcher: clicks switch, many tabs scroll, no criticals
  # (not on Windows: tabs, select and scroll fail on the CI runner's
  # desktop: the click on a scrolled-off tab, and pixel diffs between a
  # partial and a full redraw; not reproduced off that runner yet)
  win = host_machine.system() == 'windows'
  if not win
  test('tabs', find_program('sh'),
    args: [files('../../tests/ux/tabs.sh'), hexchat_gtk],
    env: ['TABPOS=6'], timeout: 60, is_parallel: false)
  endif
  # Preferences: a scheme picked applies and saves at once, no OK
  test('prefs-live', find_program('sh'),
    args: [files('../../tests/ux/prefs-live.sh'), hexchat_gtk],
    timeout: 240, is_parallel: false)
  # selecting text leaves no row half drawn
  if not win
  test('select', find_program('sh'),
    args: [files('../../tests/ux/select.sh'), hexchat_gtk],
    timeout: 600, is_parallel: false)
  # trackpad pixel scroll, wheel steps, flick glide: all drawn whole
  test('scroll', find_program('sh'),
    args: [files('../../tests/ux/scroll.sh'), hexchat_gtk],
    timeout: 90, is_parallel: false)
  endif
  # emoji, accents, CJK, broken UTF-8: drawn, selected, copied whole
  # (not on Windows yet: on the runner Pango finds no font fallback for
  # the emoji and nexchat.exe aborts, exit 3, mid-drag on that line)
  if not win
  test('utf8', find_program('python3'),
    args: [files('../../tests/ux/utf8.py'), hexchat_gtk],
    timeout: 240, is_parallel: false)
  endif
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
  test('wayland-nxmenu', find_program('sh'),
    args: [wl_with, nxmenu_test], env: wl_env, timeout: 60)
  test('wayland-nxlist', find_program('sh'),
    args: [wl_with, nxlist_test], env: wl_env, timeout: 60)
  if use_dbus
    test('wayland-dbus-remote', find_program('sh'),
      args: [wl_with, sh, files('../../tests/dbus-remote.sh'), hexchat_gtk, dbus_service_name],
      env: wl_env, timeout: 90)
    test('wayland-sni', find_program('sh'),
      args: [wl_with, sh, files('../../tests/sni.sh'), hexchat_gtk],
      # no seat: a focused window doesn't blink the tray
      env: wl_env + ['NX_NO_SEAT=1'], timeout: 60)
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
    test('gui-sni', find_program('sh'),
      args: [files('../../tests/sni.sh'), hexchat_gtk],
      env: gui_env + ['DISP=:72'], timeout: 60)
  endif
  test('gui-wm', find_program('sh'),
    args: [files('../../tests/ux/wm.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:79'], timeout: 90)
  test('gtkcompat', find_program('sh'),
    args: [files('../../tests/ux/with-xvfb.sh'), gtkcompat_test],
    env: gui_env + ['DISP=:84'], timeout: 60)
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
  test('gui-jumpto', find_program('sh'),
    args: [files('../../tests/ux/jumpto.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:93'], timeout: 120)
  test('gui-unread', find_program('sh'),
    args: [files('../../tests/ux/unread.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:94'], timeout: 120)
  test('gui-paste', find_program('sh'),
    args: [files('../../tests/ux/paste.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:92'], timeout: 120)
  test('gui-urltip', find_program('sh'),
    args: [files('../../tests/ux/urltip.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:91'], timeout: 120)
  test('gui-lastlog-all', find_program('sh'),
    args: [files('../../tests/ux/lastlog-all.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:90'], timeout: 120)
  test('gui-cmdpopup', find_program('sh'),
    args: [files('../../tests/ux/cmdpopup.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:89'], timeout: 180)
  test('gui-cmdpopup-all', find_program('sh'),
    args: [files('../../tests/ux/cmdpopup-all.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:96'], timeout: 120)
  test('gui-history-unread', find_program('sh'),
    args: [files('../../tests/ux/history-unread.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:95'], timeout: 120)
  test('gui-readmarker', find_program('sh'),
    args: [files('../../tests/ux/readmarker.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:97'], timeout: 120)
  test('gui-typing', find_program('sh'),
    args: [files('../../tests/ux/typing.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:98'], timeout: 120)
  test('gui-multiline', find_program('sh'),
    args: [files('../../tests/ux/multiline.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:99'], timeout: 120)
  # against real soju, ZNC and Ergo (tests/bnc/fetch.sh; skipped without)
  test('gui-bncwin', find_program('sh'),
    args: [files('../../tests/ux/bncwin.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:77'], timeout: 300)
  test('gui-bncremote', find_program('sh'),
    args: [files('../../tests/ux/bncremote.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:78'], timeout: 180)
  test('gui-detect', find_program('sh'),
    args: [files('../../tests/ux/detect.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:78'], timeout: 240)
  test('gui-stayonline', find_program('sh'),
    args: [files('../../tests/ux/stayonline.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:80'], timeout: 480)
  # flood of channel traffic while the UI is clicked, scrolled and zoomed
  test('gui-stress', find_program('sh'),
    args: [files('../../tests/stress.sh'), hexchat_gtk, '15'],
    env: gui_env + ['DISP=:91', 'EXIT_WAIT=60'], timeout: 120)
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
  test('gui-schemes', find_program('sh'),
    args: [files('../../tests/ux/schemes.sh'), hexchat_gtk],
    env: gui_env + ['DISP=:86'], timeout: 180)
endif
