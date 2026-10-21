# The terminal UI, `nexchat --tui` (docs/tui-design.md): nexchat-tui, the
# frontend (fe-tui.c + the windows), on a kit of nexchat-only widgets
# (widgets/) drawn with maya (subprojects/maya, its view layer: no jaal,
# GLib's loop drives it).

tui_opt = get_option('tui')
if tui_opt.disabled()
  subdir_done()
endif
# POSIX only for now: the loop watches the tty with g_unix_fd_add
if host_machine.system() == 'windows'
  if tui_opt.enabled()
    error('the TUI does not build on Windows yet')
  endif
  subdir_done()
endif
fs = import('fs')

# maya is C++ (C++23 at least: AppleClang 21 and gcc 13+ manage it); it
# builds with CMake, which meson drives through its cmake module.
if not add_languages('cpp', native: false, required: tui_opt)
  subdir_done()
endif
if not meson.get_compiler('cpp').has_argument('-std=c++23') and not meson.get_compiler('cpp').has_argument('-std=c++2b')
  if tui_opt.enabled()
    error('the TUI needs a C++23 compiler (gcc 13+, clang 17+)')
  endif
  subdir_done()
endif
cmake_prog = find_program('cmake', required: tui_opt)
if not cmake_prog.found() or not fs.exists(meson.project_source_root() / 'subprojects/maya/CMakeLists.txt')
  if tui_opt.enabled()
    error('the TUI needs subprojects/maya: git submodule update --init --recursive')
  endif
  subdir_done()
endif

cmake = import('cmake')
maya_opts = cmake.subproject_options()
maya_opts.add_cmake_defines({
  'MAYA_BUILD_APP': false,        # the jaal runtime; nexchat runs GLib's loop
  'MAYA_BUILD_EXAMPLES': false,
  'MAYA_BUILD_TESTS': false,
  'MAYA_NATIVE_TUNING': false,    # a portable binary, not -march=native
  'MAYA_CCACHE': false,
  'MAYA_HEAVY_OPT': false,
  'CMAKE_BUILD_TYPE': 'Release',
  'CMAKE_POSITION_INDEPENDENT_CODE': true,
})
# maya's CMake asks for C++26 (CXX_STANDARD 26), which meson's cmake module
# turns into cpp_std=gnu++26; a meson older than that standard (Ubuntu
# 24.04's 1.3) drops it with a warning and maya compiles as C++17. Give the
# flag ourselves: the newest the compiler takes, C++23 at least.
foreach std : ['-std=gnu++26', '-std=gnu++2c', '-std=gnu++23', '-std=gnu++2b']
  if meson.get_compiler('cpp').has_argument(std)
    maya_opts.append_compile_args('cpp', std, target: 'maya')
    break
  endif
endforeach
maya_proj = cmake.subproject('maya', options: maya_opts)
# Its include dirs by hand: an older meson's cmake module also lists
# subprojects/maya/__CMake_build in the source tree, which doesn't exist, and
# -Werror=missing-include-dirs (the C sources get them too) fails on it.
maya_dep = declare_dependency(
  dependencies: maya_proj.dependency('maya').partial_dependency(
    compile_args: true, link_args: true, links: true, sources: true),
  include_directories: include_directories('../../subprojects/maya/include'),
)

tui_cpp_args = []
cxx = meson.get_compiler('cpp')
foreach std : ['-std=c++23', '-std=c++2b']
  if cxx.has_argument(std)
    tui_cpp_args += std
    break
  endif
endforeach
# Per target, as cpp_args: a dependency's compile_args reach C sources too.

tui_widgets = static_library('nexchat_tui_widgets',
  'widgets/rich.cpp',
  'widgets/theme.cpp',
  'widgets/ircfmt.cpp',
  'widgets/chat.cpp',
  'widgets/chantree.cpp',
  'widgets/userlist.cpp',
  'widgets/inputline.cpp',
  'widgets/bars.cpp',
  'widgets/switcher.cpp',
  'widgets/layout.cpp',
  'widgets/menu.cpp',
  'widgets/dialog.cpp',
  'widgets/form.cpp',
  'widgets/panel.cpp',
  'widgets/sheet.cpp',
  'widgets/cmdpopup.cpp',
  cpp_args: tui_cpp_args,
  dependencies: maya_dep,
  override_options: ['cpp_std=none'],
)
tui_widgets_dep = declare_dependency(
  link_with: tui_widgets,
  include_directories: include_directories('widgets'),
  dependencies: maya_dep,
)

tui_widget_tests = executable('tui-widget-tests', 'widgets/tests.cpp',
  cpp_args: tui_cpp_args,
  dependencies: tui_widgets_dep,
  override_options: ['cpp_std=none'],
)
test('tui-widgets', tui_widget_tests, timeout: 30)

# A look at every widget in a real terminal: build/src/fe-tui/tui-gallery
if host_machine.system() != 'windows'
  executable('tui-gallery', 'widgets/gallery.cpp',
    cpp_args: tui_cpp_args,
    dependencies: tui_widgets_dep,
    override_options: ['cpp_std=none'],
  )
endif

# The frontend: nexchat-tui (`nexchat --tui` runs it). The core is C and
# GLib; ui.cpp is the only C++ that talks to it, through bridge.h.
if host_machine.system() != 'windows'
  # link previews decode with gdk-pixbuf when it's there
  tui_pixbuf_dep = dependency('gdk-pixbuf-2.0', required: false)
  tui_c_args = tui_pixbuf_dep.found() ? ['-DHAVE_GDK_PIXBUF'] : []
  nexchat_tui = executable('nexchat-tui',
    'fe-tui.c',
    'networks.c',
    'bouncer.c',
    'tools.c',
    'keys.c',
    'cmds.c',
    'notify.c',
    'preview.c',
    '../fe-gtk/plugin-notification.c',
    'netlist.cpp',
    'prefs.cpp',
    'chanlist.cpp',
    'lists.cpp',
    'transfers.cpp',
    'tools.cpp',
    'bouncerwin.cpp',
    'ui.cpp',
    dependencies: [hexchat_common_dep, tui_widgets_dep, tui_pixbuf_dep],
    c_args: tui_c_args,
    cpp_args: tui_cpp_args + ['-DNX_VERSION="' + meson.project_version() + '"'],
    override_options: ['cpp_std=none'],
    install: true,
  )
  # `nexchat --tui` finds it here too (fe-gtk.c exec_tui)
  install_symlink('nexchat-tui',
    install_dir: get_option('libexecdir') / 'nexchat',
    pointing_to: get_option('prefix') / get_option('bindir') / 'nexchat-tui',
  )

  test('tui-smoke', find_program('sh'),
    args: [files('../../tests/tui-smoke.sh'), nexchat_tui],
    timeout: 90,
  )
  test('tui-netlist', find_program('sh'),
    args: [files('../../tests/tui-smoke.sh'), nexchat_tui, 'tui-netlist.py'],
    timeout: 90,
  )
  test('tui-prefs', find_program('sh'),
    args: [files('../../tests/tui-smoke.sh'), nexchat_tui, 'tui-prefs.py'],
    timeout: 90,
  )
  test('tui-chanlist', find_program('sh'),
    args: [files('../../tests/tui-smoke.sh'), nexchat_tui, 'tui-chanlist.py'],
    timeout: 90,
  )
  test('tui-lists', find_program('sh'),
    args: [files('../../tests/tui-smoke.sh'), nexchat_tui, 'tui-lists.py'],
    timeout: 90,
  )
  test('tui-transfers', find_program('sh'),
    args: [files('../../tests/tui-smoke.sh'), nexchat_tui, 'tui-transfers.py'],
    timeout: 90,
  )
  test('tui-tools', find_program('sh'),
    args: [files('../../tests/tui-smoke.sh'), nexchat_tui, 'tui-tools.py'],
    timeout: 90,
  )
  test('tui-find', find_program('sh'),
    args: [files('../../tests/tui-smoke.sh'), nexchat_tui, 'tui-find.py'],
    timeout: 90,
  )
  test('tui-keys', find_program('sh'),
    args: [files('../../tests/tui-smoke.sh'), nexchat_tui, 'tui-keys.py'],
    timeout: 90,
  )
  test('tui-tabs', find_program('sh'),
    args: [files('../../tests/tui-smoke.sh'), nexchat_tui, 'tui-tabs.py'],
    timeout: 90,
  )
  test('tui-live', find_program('sh'),
    args: [files('../../tests/tui-smoke.sh'), nexchat_tui, 'tui-live.py'],
    timeout: 90,
  )
  test('tui-fuzz', find_program('sh'),
    args: [files('../../tests/tui-smoke.sh'), nexchat_tui, 'tui-fuzz.py'],
    env: {'TUI_FUZZ_SEED': '3', 'TUI_FUZZ_STEPS': '300'},
    timeout: 180,
  )
  test('tui-preview', find_program('sh'),
    args: [files('../../tests/tui-smoke.sh'), nexchat_tui, 'tui-preview.py'],
    timeout: 90,
  )
  test('tui-history', find_program('sh'),
    args: [files('../../tests/tui-smoke.sh'), nexchat_tui, 'tui-history.py'],
    timeout: 90,
  )
  test('tui-hooks', find_program('sh'),
    args: [files('../../tests/tui-smoke.sh'), nexchat_tui, 'tui-hooks.py'],
    timeout: 90,
  )
  test('tui-hover', find_program('sh'),
    args: [files('../../tests/tui-smoke.sh'), nexchat_tui, 'tui-hover.py'],
    timeout: 90,
  )
  test('tui-cmdpop', find_program('sh'),
    args: [files('../../tests/tui-smoke.sh'), nexchat_tui, 'tui-cmdpop.py'],
    timeout: 90,
  )
  test('tui-sizes', find_program('sh'),
    args: [files('../../tests/tui-smoke.sh'), nexchat_tui, 'tui-sizes.py'],
    timeout: 180,
  )
  test('tui-app-theme', find_program('sh'),
    args: [files('../../tests/tui-smoke.sh'), nexchat_tui, 'tui-app-theme.py'],
    timeout: 150,
  )
  # against real soju and Ergo (tests/bnc/fetch.sh; skipped without)
  test('tui-bouncer', find_program('sh'),
    args: [files('../../tests/tui-smoke.sh'), nexchat_tui, 'tui-bouncer.py'],
    timeout: 240,
  )
endif
