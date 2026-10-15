# nexchat roadmap

nexchat is HexChat moved to GTK4 and renamed. The UI and UX stay exactly as they are in HexChat. Same window layout, same menus in the same order, same dialogs and preferences pages, same keybindings, same tab and tree behaviour, same right-click menus, same text colours and event formats. Only the toolkit underneath changes. Think Pale Moon to Firefox: the same app on a newer base.

Every change is checked against one rule: **would a HexChat user notice?** If yes, it's a bug, unless GTK4 or Wayland makes it impossible. In that case we write it down in the gap list (section 9) and pick the closest behaviour we can get.

## 1. Why GTK4 and not Qt

Qt Widgets is actually better at classic desktop UX than GTK4. It has real menus, a tray icon, window positioning and mnemonics out of the box. We still pick GTK4 for these reasons:

- **The code stays.** The frontend is 34k lines of C written against GTK. Moving to GTK4 means editing that code file by file, and it keeps building and running the whole way. Moving to Qt means rewriting all of it in C++ before anything works again. Pale Moon kept its engine; this is the same idea.
- **It keeps the HexChat look.** HexChat looks like a GTK app and follows the user's GTK theme. A Qt build would look like a different app on GNOME, XFCE, Cinnamon and MATE, which is where most HexChat users are.
- **The core doesn't care.** `src/common` runs on the GLib main loop and talks to the UI only through `fe.h`. Both toolkits can sit on top of it, so the core gives neither one an advantage.
- **Plugins keep working.** Python, Perl and Lua plugins get a `GtkWindow*` from `gtkwin_ptr`. With GTK4 it's still a GtkWindow. With Qt it would break.

Places where GTK4 makes the classic UX harder (tray, window position, urgency, menus) are known problems with known fixes. They're listed in section 9. None of them is big enough to justify a full C++ rewrite.

Do **not** use libadwaita. It would change the look and go against the whole point of this project.

## 2. The path: GTK2, then GTK3, then GTK4

GTK's own migration guide assumes you're starting from GTK3 with no deprecated calls left. Jumping straight from GTK2 to GTK4 means fighting two API breaks at once with nothing working in between. So we go in steps:

1. **GTK3** still has everything HexChat uses: GtkMenu, GtkStatusIcon, `gtk_window_move`, `gtk_dialog_run`. Everything still works, so we can compare the UX screen by screen.
2. **GTK3 with no deprecations.** Build with `GDK_DISABLE_DEPRECATED` / `GTK_DISABLE_DEPRECATED` and remove every deprecated call. This is where most of the real work happens.
3. **GTK4.** By this point it's mostly mechanical: event controllers, snapshots, the new menu model and async dialogs.

Each step leaves us with a working app we could ship.

## 3. How big the job is

Measured in `src/fe-gtk` (34k lines):

| API | Uses | Files | Happens in |
|-----|-----:|------:|-------|
| GtkTreeView / tree models | 372 / 71 | 18 / 17 | GTK4 (deprecated in 4.10, still there) |
| GtkTable | 137 | 7 | GTK3 (switch to GtkGrid) |
| gtk_box_pack_* | 114 | 20 | GTK4 (append/prepend) |
| GtkMenu / menu items | 105 | 7 | GTK4 (GMenuModel + GAction) |
| gtk_container_add | 84 | 16 | GTK4 |
| GtkHBox / GtkVBox | 53 | 11 | GTK3 |
| GdkGC / gdk_draw_* / GdkPixmap | 81 | 6 | GTK3 (cairo) |
| GtkStatusIcon | 16 | 1 | GTK4 (removed) |
| gtk_dialog_run | 3 | 2 | GTK4 (removed) |
| gtk_window_move / get_position | 5 | 3 | GTK4 (removed) |

The hardest files:

- `xtext.c` (5k lines): the custom chat text widget. Today it draws with GdkGC. It moves to cairo in GTK3, then to the snapshot API in GTK4.
- `menu.c` (2.5k): the main menu, every popup menu, and `/MENU` entries that users and plugins add at runtime.
- `maingui.c` (3.9k): the main window, the layout of tabs, user list and input box, and the panes.
- `sexy-spell-entry.c` (1.7k): the input box with spell checking. It subclasses GtkEntry and draws its own underlines.
- `fkeys.c` (1.8k): keybindings and tab completion, driven by key events.
- `setup.c` (2.4k) and `servlistgui.c` (2.3k): the big dialogs, mostly GtkTable and tree views.
- `plugin-tray.c`: the tray icon.

Rebrand surface: about 6400 matches for `hexchat`/`xchat` in 310 files. Most are the plugin API (`hexchat_*` names) and the plugins themselves. Those are mostly kept for compatibility (see phase 1).

## Phase 0: baseline and reference

You can't keep the UX the same if you haven't recorded what it is first.

- [ ] Get the unchanged GTK2 build running. gtk2 isn't installed on this machine. Install it, or use the Flathub HexChat build (`io.github.Hexchat`) as the reference.
- [ ] Make a **UX reference pack** in `docs/ux-reference/`:
  - a screenshot of every window, dialog, preferences page and menu (main menu, tab menu, user list menu, nick menu, URL menu, channel menu, tray menu), in both tab and tree mode, light and dark theme;
  - the default keybindings, dumped from `default_kb_cfg` in `fkeys.c`;
  - the full menu tree with its mnemonics and accelerators;
  - every pref in `vars[]` (`cfgfiles.c`) and the widget that edits it in `setup.c`.
- [ ] Write a **parity checklist** from that pack. Each port step ticks items off it.
- [ ] CI: keep the Ubuntu, MSYS2 and Flatpak builds green the whole way. Drop the MSVC `.vcxproj` files and build Windows with meson only, because GTK4 on Windows is built with meson/gvsbuild anyway.
- [ ] Add a smoke test: start the client against a small fake IRC server (a Python script), connect, join, send and receive a message, quit. Run it once with `fe-text` and once with the GTK frontend under `xvfb`/`weston --headless`. There are no tests right now, so this is the safety net.

## Phase 1: rebrand (no toolkit changes)

This is done first and kept separate, so the port diffs only contain port changes.

What users see:
- [ ] Project name, binary `nexchat`, window titles, about dialog, translation strings (`po/`, 59 languages). Change the names in the strings without breaking the translations.
- [ ] A new app ID, e.g. `io.github.nexchat.Nexchat` (to be decided), plus the .desktop file, metainfo, icons, man page and Flatpak manifest.
- [ ] New icon art. `data/icons/hexchat*.svg` and the tray PNGs need replacements. Keep the tree and user-list icons as they are.
- [ ] Windows installer (`win32/installer`) and Windows version resources.

User data:
- [ ] Config dir moves to `~/.config/nexchat` (`%APPDATA%\nexchat` on Windows). On first run, if it's missing and `~/.config/hexchat` exists, **copy** the old one over, never move it. Keep the same file names inside (`hexchat.conf`, `servlist.conf`, `pevents.conf`…), or rename them with a fallback that reads the old names. Users must keep their networks, logs and themes.
- [ ] Theme files (`.hct`) from HexChat must keep working.

Plugin compatibility (nothing breaks):
- [ ] Keep the `hexchat_plugin_init` symbol and the `hexchat_*` API struct as they are. Existing compiled plugins must load.
- [ ] Ship `nexchat-plugin.h` as a thin alias header, and keep `hexchat-plugin.h` too. Install both `.pc` files.
- [ ] Python: keep `import hexchat` and `import xchat`, and add `import nexchat`. Perl: keep `HexChat::` and `Xchat::`. Lua: keep the `hexchat` table and add `nexchat`.
- [ ] Keep `hexchat_get_info(ph, "gtkwin_ptr")`. Document that from phase 3 it returns a GTK4 window.
- [ ] D-Bus: register the new name, and the old `org.hexchat.service` name too, if it's free.
- [ ] Keep the CTCP VERSION reply honest: "nexchat x.y (HexChat fork)".

## Phase 2: GTK3

Goal: it builds against GTK3 and looks the same as the GTK2 build, item by item on the checklist.

- [ ] meson: `gtk+-3.0`. Keep a short-lived `-Dgtk=2|3` switch only if it helps compare.
- [ ] Boxes and tables: `gtk_hbox/vbox_new` becomes `gtk_box_new`, GtkTable becomes GtkGrid. Copy the padding and spacing values exactly so the dialogs line up the same way.
- [ ] Colours: GdkColor becomes GdkRGBA. Keep `palette.c` and the `colors.conf` format as they are.
- [ ] Styling: `gtk_widget_modify_*` and `gtk_rc_*` become a GtkCssProvider. The rules: the HexChat-specific styles stay (the input box and user list fonts and colours follow the preferences), and nothing else gets a new look. Theme-wide CSS is not allowed.
- [ ] `xtext.c` drawing: GdkGC, GdkPixmap and `gdk_draw_*` become cairo, `expose-event` becomes `draw`. Keep the same layout code (line wrapping, indent, separator line, marker line, selection, URL hover). Only the drawing calls change.
- [ ] `xtext` scrolling: move the GtkAdjustment to the GtkScrollable interface.
- [ ] Spell entry: port `sexy-spell-entry.c` to GTK3's GtkEntry (it can still be subclassed in GTK3) and enchant-2.
- [ ] Remove the transparent background feature that reads `_XROOTPMAP_ID` from the root window. It doesn't work under compositors. Keep the window opacity pref.
- [ ] Get rid of GtkObject, `gtk_signal_*` and `GTK_OBJECT` (5 uses).
- [ ] Check it on X11, Wayland, Windows and macOS.

Exit criteria: the parity checklist matches the GTK2 reference, and the smoke test passes.

## Phase 3: GTK3 with no deprecations

Goal: it builds with `-DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED`. After this phase, the move to GTK4 is mostly mechanical.

- [ ] Stop using `GtkStock` names and stock icons. Use named icons, and keep the same images where they're used today.
- [ ] Menus: build all menus from GMenuModel + GAction while still on GTK3, using `gtk_menu_new_from_model` and `gtk_menu_bar_new_from_model`. It's the biggest job in this phase:
  - main menu, popups, user list buttons, `/MENU` and plugin menu entries, toggles and radio groups (`menu_entry`: `group`, `state`, `ucmd`), icons, markup labels, enabling and disabling at runtime;
  - mnemonics (`_Server`, `_Window`…) and accelerators must stay exactly the same;
  - `fe_menu_add/del/update` keep the same behaviour for plugins.
- [ ] `GtkUIManager`/`GtkAction`, if any: replace with GAction.
- [ ] Dialogs: stop using `gtk_dialog_run`. Write async versions with the same buttons, the same order and the same default response.
- [ ] Key handling in `fkeys.c`: write it as one function that takes (keyval, state) and returns a result, so it doesn't matter how the key event was delivered. Tab completion, history, and the `/KEYBINDING` list stay exactly the same.
- [ ] Hide the tray behind a small interface (`tray_backend.h`): GtkStatusIcon on GTK3 for now, so phase 4 can swap in the new backends.
- [ ] Hide window position and urgency behind small functions too (see section 9).

## Phase 4: GTK4

- [ ] meson: `gtk4`. No libadwaita.
- [ ] `gtk_main` becomes a GtkApplication. The single-instance behaviour of today's D-Bus `hexchat_remote()` (`-e`, `--existing`, `-c` command, URL handling) moves to the GApplication `open`/`command-line` hooks. The same command-line flags keep working.
- [ ] Containers: `gtk_box_pack_*`, `gtk_container_add` and `gtk_widget_show_all` become `gtk_box_append`, `gtk_window_set_child` and so on. Widgets are visible by default now, so check that everything that was hidden stays hidden.
- [ ] Events: `key-press-event`, `button-press-event` and `motion-notify` become GtkEventControllerKey, GtkGestureClick and GtkEventControllerMotion. Right-click, middle-click, double-click and scroll must behave the same everywhere (tabs, tree, user list, xtext, input box).
- [ ] `xtext`: drawing moves from cairo to snapshot. Keep using cairo inside `gtk_snapshot_append_cairo` at first so the drawing code stays the same. Text selection and copy move to GdkClipboard. The primary selection must still work on X11/Wayland.
- [ ] Menus: GtkMenuBar becomes GtkPopoverMenuBar, and popups become GtkPopoverMenu built from the same models phase 3 made. Use CSS to make the popovers look like plain menus: no arrow (`has-arrow` FALSE), flat, positioned at the pointer. This is the most visible risk in the whole project, so check it against the screenshots.
- [ ] Spell entry: GTK4 entries delegate to an internal GtkText. Rebuild the spell checking on top of that, with the underline drawn as a Pango attribute. Keep the same right-click suggestions menu.
- [ ] Tree views: the channel tree, user list, channel list, ban list, DCC, server list, prefs tree, URL grabber, ignore list, notify list and plugin list stay on GtkTreeView first. It's deprecated, but it still exists in GTK4 and it gives the exact same look. Turn off deprecation warnings only for those files.
- [ ] Drag and drop (tab reordering, files dropped onto nicks for DCC): GtkDragSource and GtkDropTarget.
- [ ] Clipboard, file chooser (GtkFileDialog), font chooser (GtkFontDialog) and colour chooser (GtkColorDialog). The file and font dialogs will look like the platform's dialogs. That's fine.
- [ ] Tray backends: see section 9.
- [ ] Pixmaps: GdkPixbuf becomes GdkTexture/GdkPaintable for the tree and user-list icons.
- [ ] Opacity uses `gtk_widget_set_opacity` on the window.

Exit criteria: the parity checklist matches, and the smoke test passes on X11, Wayland, Windows and macOS.

## Phase 5: getting rid of deprecated widgets ("100%")

After phase 4 it runs on GTK4, but it still uses GtkTreeView, which is deprecated. To finish the job:

- [ ] Move each tree view to GtkColumnView/GtkListView, one widget at a time, each in its own commit. Start with the simple ones (URL grabber, ignore, notify, plugins), then the big ones (channel list, ban list, DCC, server list), and the user list and channel tree last.
- [ ] Row height, column widths, sort order, the icons in the user list, coloured nicks, the selection colour and keyboard navigation must all match the screenshots.
- [ ] Remove `custom-list.c` (the custom tree model) once the channel list uses GListModel.
- [ ] Build with deprecations as errors. No exceptions left.

## Phase 6: release

- [ ] Packages: Flatpak, AUR (`nexchat-git` and a release package), Windows installer (meson + gvsbuild GTK4), and a macOS bundle if there's demand.
- [ ] A migration note for HexChat users: config import, plugin compatibility, and the small differences that couldn't be avoided (from section 9).
- [ ] Version: start at nexchat 3.0 so it's obviously after HexChat 2.16.

## 9. Places where GTK4/Wayland fight the HexChat UX

| HexChat behaviour | Problem | Plan |
|---|---|---|
| Tray icon (`plugin-tray.c`) | GtkStatusIcon is gone in GTK4 | Linux: our own StatusNotifierItem over GDBus, with a dbusmenu for the tray menu. Windows: `Shell_NotifyIcon`. macOS: none, same as today. Same icons, same blinking, same menu. |
| Remembers where the window was | `gtk_window_move` is gone; Wayland doesn't allow it | X11: Xlib through `gdk_x11`. Windows: Win32 API. Wayland: not possible, the compositor decides. Window size is still remembered. |
| Taskbar flashing on highlight (urgency hint) | `gtk_window_set_urgency_hint` is gone | X11: `XSetWMHints` urgency. Windows: `FlashWindowEx` (already there). Wayland: xdg-activation request. |
| Menus look like menus | GTK4 menus are popovers | CSS: no arrow, square corners, normal padding. Keep the mnemonics. |
| Modal dialogs | `gtk_dialog_run` is gone | Async callbacks. The flow stays the same for the user. |
| Hiding/showing the window from the tray | Wayland can't put a window back where it was | Use `gtk_window_present`. On X11, restore the position as before. |
| Fonts and colours in the chat view | No more `modify_*` calls | A per-widget CSS provider, generated from the prefs. |
| Transparent background (old X11 trick) | Doesn't work with compositors | Dropped in phase 2. Window opacity still works. |

## Rules for every PR

- One area per PR, and it builds and runs.
- Before/after screenshots for any change to something the user sees.
- No new features, no "while I'm here" redesigns, no default changes. Ideas go into a `later/` list for after 3.0.
- Never change `src/common` just to make the port easier, unless it's the `fe.h` boundary.
- Plugin API and config files stay compatible.
