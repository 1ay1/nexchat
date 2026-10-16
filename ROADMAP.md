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

Done. Tests: `tests/migrate-config.sh` (meson test), `tests/plugin-aliases.sh <install prefix>` (python/lua/perl, old and new names).

This is done first and kept separate, so the port diffs only contain port changes.

What users see:
- [x] Project name, binary `nexchat` / `nexchat-text`, window titles, about dialog ("Based on HexChat"), menu `ne_xchat` (Alt+X still works). Translations: `po/rebrand-po.py` rewrote the brand msgids and the existing translations in 53 languages, so nothing fell back to English.
- [x] App ID `io.github.nexchat`: .desktop, appdata, plugin metainfo, theme manager, man page, Flatpak manifest and CI.
- [x] Icon: the same hexagon recoloured blue (`data/icons/nexchat.svg`/`.png`). Tree, user-list and tray status icons unchanged.
- [ ] Windows installer and macOS bundle: moved to phase 6, since those scripts get rewritten for GTK4 anyway.

User data:
- [x] Config dir is `~/.config/nexchat` (`%APPDATA%\nexchat`). On first run, if it's missing and the HexChat dir exists, the HexChat dir is **copied** (symlinks skipped, a failed copy is set aside as `.partial`). File names inside stay the same (`hexchat.conf` etc.), so a copied config just works. `-d/--cfgdir` never migrates. Portable Windows installs keep `config\` next to the exe.
- [x] Theme files (`.hct`): format unchanged.

Plugin compatibility (nothing breaks):
- [x] `hexchat_plugin_init` and the `hexchat_*` API are unchanged. Existing compiled plugins load.
- [x] `nexchat-plugin.h` alias header plus `hexchat-plugin.h`; both `nexchat-plugin.pc` and `hexchat-plugin.pc` are installed. Plugins go in `lib/nexchat/plugins`; `NEXCHAT_LIBDIR` overrides it, and `HEXCHAT_LIBDIR` still works.
- [x] Python: `import nexchat` / `hexchat` / `xchat`. Lua: `nexchat` and `hexchat` globals. Perl: `Nexchat::` / `HexChat::` / `Xchat::`.
- [ ] `gtkwin_ptr` stays. Document what it returns once it's a GTK4 window (phase 4).
- [x] D-Bus: bus name `io.github.nexchat.service`, so `--existing` never talks to a running HexChat. Object paths and interfaces stay `org.hexchat.*`, so remote-control scripts only need the new bus name.
- [x] CTCP VERSION reply is "nexchat 2.16.2 / <os>".

## Phase 2: GTK3

Goal: it builds against GTK3 and looks the same as the GTK2 build, item by item on the checklist.

- [x] meson: `gtk+-3.0`. No `-Dgtk=2|3` switch: dual-building would mean `#ifdef`s through 34k lines. The GTK2 reference is built from `b544ac33` in a worktree instead (`tests/ux/build-gtk2-reference.sh`). `GTK_DISABLE_DEPRECATED` is off until phase 3 turns it back on.
- [x] Boxes and tables: `gtk_hbox/vbox_new` becomes `gtk_box_new`, GtkTable becomes GtkGrid. Copy the padding and spacing values exactly so the dialogs line up the same way.
  - All H/V boxes, button boxes, panes, separators, the scale and the chat scrollbar use the orientation-based constructors. Homogeneous boxes go through `gtkutil_box_new()`. GTK2's `gtk_container_add` into a box meant expand+fill; GTK3's doesn't, so every add goes through `gtkutil_container_add()`, which keeps the old behaviour for boxes (without it the main window collapsed into a corner).
  - Every GtkTable is a GtkGrid. `gtkutil_grid_attach()` takes the old `gtk_table_attach` arguments (cell edges, EXPAND/FILL as `GU_*`, padding) and maps them to hexpand/align/margins. The main window table had per-row/column spacing that GtkGrid can't express; `mg_grid_*` in `maingui.c` stores it on the grid and applies it as margins.
  - Checked by screenshot against the GTK2 build: main window, Preferences (Appearance, Colors), Network List, network edit. Same rows, columns and order everywhere; widgets are GTK3-theme sized (taller entries).
  - Found on the way: Preferences > Colors swatches were 990px wide on GTK3 (theme button padding) and are back to GTK2's small squares; empty `GtkMenuItem`s used as menu separators draw as blank gaps on GTK3 and are real `GtkSeparatorMenuItem`s now.
- [x] Colours: GdkColor becomes GdkRGBA. Keep `palette.c` and the `colors.conf` format as they are.
  - `colors[]` is `GdkRGBA`, initialised from the same 16-bit numbers (`C16()`). `colors.conf` is still read and written as 16-bit hex; `C16R/G/B` and `C8R/G/B` give back exactly the old integers for Pango attributes, CSS and markup.
  - Tree views use `foreground-rgba` / `GDK_TYPE_RGBA`. xtext draws straight from the doubles.
  - Proven: `palette-roundtrip` (meson test) checks all 65536 channel values round-trip exactly; `tests/ux/colors-roundtrip.sh` loads an awkward `colors.conf`, saves it from Preferences, and requires it byte-identical (passes on GTK2 and GTK3).
  - The colour picker is still `GtkColorSelectionDialog` (via its `_rgba` calls). Whether to keep the GTK2-look picker or use `GtkColorChooserDialog` is a phase 3 call.
- [~] Styling: `gtk_widget_modify_*` and `gtk_rc_*` become a GtkCssProvider. The rules: the HexChat-specific styles stay (the input box and user list fonts and colours follow the preferences), and nothing else gets a new look. Theme-wide CSS is not allowed.
  - Done: `fe_apply_css()` in `fe-gtk.c` generates CSS from prefs + `colors.conf`, scoped to `#hexchat-inputbox`, `#hexchat-userlist`, `#hexchat-tree` only, and is rebuilt when prefs are applied. Input boxes are flat and square again (no rounded frame or focus glow); user list and channel tree use the text font again.
  - Left: the GTK3-only differences below. Done since: selected rows in the user list and channel tree use the palette's "selected text" colours; the chat scrollbar has stepper arrows again; the colour swatches in Preferences are painted with per-button CSS (on GTK3 they had silently lost their colour); channel tabs, nick button and user-count box are compact and square again with a sunken current tab; icons on buttons and menu items are shown again (GTK3 hides them by default; `gtkutil_force_images()` turns them back on for every button/menu item, plugins' too); Ignore List stats entries no longer push the window to ~1100px; Character Chart cells are GTK2-sized; Text Events opens with the first event selected again (GTK2 selected on focus, GTK3 doesn't).
  - Checked by screenshot against GTK2 (`tests/ux/shot.sh`): main window, main menu, tabs layout, Preferences (Appearance, Colors), Network List, network edit, Channel List, Ban List, File Transfers, Friends List, Ignore List, Plugins and Scripts, Raw Log, URL Grabber, Character Chart, Text Events, Keyboard Shortcuts.
  - GTK3 theme differences we keep (not worth fighting the theme over, and phase 4 changes them again): spin buttons are wide `- +` pairs instead of stacked arrows; entries, combo boxes and buttons are a few px taller; checkboxes and radios are the theme's; tree view headers are flat.
- [x] `xtext.c` drawing: GdkGC, GdkPixmap and `gdk_draw_*` became cairo, `expose-event` became `draw`. The render code is untouched; it draws into an off-screen image surface at the moment it used to draw to the window, and `::draw` copies it out. The scroll fast path (copy the backing up/down, render only the new strip) now also runs on macOS, where GTK2 had it `#ifdef`'d off. The background image is a GdkPixbuf tiled with a cairo pattern.
- [x] `xtext` scrolling: GTK3 has no `set_scroll_adjustments`; xtext owns its adjustment from init and every place that wrote `adj->value`/`upper` directly goes through `gtk_adjustment_configure` with our own handler blocked, so value-changed fires exactly when it used to. GtkScrollable isn't needed: nothing puts xtext in a GtkScrolledWindow except two small read-only views.
- [~] Spell entry: compiles and draws on GTK3 (`draw`, preedit length tracked via `preedit-changed`, click position via `gtk_entry_get_layout_offsets`). Still to check: enchant-2 on all platforms, IM preedit with a real input method.
- [x] Transparent background (`_XROOTPMAP_ID`): already gone upstream; nothing left to remove.
- [x] GtkObject, `gtk_signal_*`, `GTK_OBJECT`: gone.
- [ ] Check it on X11, Wayland, Windows and macOS.
  - X11 (Xvfb on macOS, `/opt/X11/bin/Xvfb`): connects, joins, renders. Screenshot diff vs GTK2 below.

Known visible differences on the first GTK3 build, all from the GTK3 theme (Adwaita) not from our code, and all to be fixed by the CSS step above:
- Channel tree and user list rows are shorter and use the theme's smaller default font; GTK2 inherited `hexchat.conf`'s font through `gtk_widget_set_style` and GTK3 ignores GtkStyle for treeviews.
- Input box gets Adwaita's blue focus ring and rounded frame.
- Tree selection is Adwaita blue instead of the grey GTK2 Raleigh selection.
- Scrollbar is the GTK3 overlay-style slider instead of the stepper scrollbar.
- The text view itself (fonts, colours, indent, separator, wrapping, nick colours, bold) matches.

Exit criteria: the parity checklist matches the GTK2 reference, and the smoke test passes.

## Phase 3: GTK3 with no deprecations

Goal: it builds with `-DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED`. After this phase, the move to GTK4 is mostly mechanical.

**Status: the build goal is met.** `meson.build` sets `GTK_DISABLE_DEPRECATED` and `GDK_DISABLE_DEPRECATED` for the whole tree, so using a deprecated GTK3 call is now a compile error. The only exception is `tray-backend-statusicon.c` (GtkStatusIcon has no GTK3 replacement). The items below that aren't deprecations in GTK3 (menu models, async dialogs, key handling) are still open; they're needed for GTK4.

- [x] Stop using `GtkStock` names and stock icons. Use named icons, and keep the same images where they're used today.
  - `gtkutil_stock_icon_name()` maps every stock id we use to the icon name GTK 3.24's own stock table used (`gtk/deprecated/gtkiconfactory.c`); ids whose image only exists under the stock name (gtk-ok, gtk-cancel, gtk-connect, …) are named icons GTK3 ships under the same name, checked with `tests/ux/iconprobe.c`. Button labels come from GTK's own translated stock labels (`gtk30` catalogue, `gtkutil_stock_label()`), so every language keeps its button text. `GTK_STOCK_*` became `NX_STOCK_*` plain strings.
- [x] Other deprecated calls, all replaced: GtkImageMenuItem (menu item with [icon][accel label], shortcuts still shown), GtkFontSelectionDialog → GtkFontChooserDialog, GtkColorSelectionDialog → GtkColorChooserDialog (opens in its editor; same RGB out), GtkStyle `input_style` (CSS already did it), `gtk_misc_*`, `gtk_menu_popup` (context menus → `popup_at_pointer`), `gtk_tree_view_set_rules_hint` (dropped, striping is the theme's), `gdk_beep`, viewport/treeview adjustments, `gdk_window_get_pointer`, `gtk_adjustment_changed`, widget state, dialog action area, `add_with_viewport`, GtkArrow, GtkAlignment, window opacity, `gdk_screen_get_number`.
- [ ] Menus: build all menus from GMenuModel + GAction while still on GTK3, using `gtk_menu_new_from_model` and `gtk_menu_bar_new_from_model`. It's the biggest job in this phase:
  - main menu, popups, user list buttons, `/MENU` and plugin menu entries, toggles and radio groups (`menu_entry`: `group`, `state`, `ucmd`), icons, markup labels, enabling and disabling at runtime;
  - mnemonics (`_Server`, `_Window`…) and accelerators must stay exactly the same;
  - `fe_menu_add/del/update` keep the same behaviour for plugins.
- [x] `GtkUIManager`/`GtkAction`, if any: none used.
- [x] Dialogs: stop using `gtk_dialog_run`. The quit dialog is async now (same buttons, order, modality; checked by driving Escape/Cancel/Quit on GTK2 and GTK3). The two message boxes that must block (startup error before the main loop, `FE_MSG_WAIT`) use `gtkutil_dialog_run_blocking()`, a nested main loop with the same semantics.
- [x] Key handling in `fkeys.c`: `key_handle (wid, keyval, state, sess)` returns `KEY_NOT_HANDLED / KEY_HANDLED / KEY_HANDLED_STOP` and doesn't touch the event; `key_handle_key_press` is a thin GTK3 adapter, GTK4 gets one for GtkEventControllerKey. Checked by `tests/ux/keys.sh` (types over XTest, checks what reaches the server: send, Up history, Tab completion, Ctrl+K). Found on the way and fixed: on any macOS build, Ctrl shortcuts were registered as `<Meta>`, which on X11 matches the bare key (typing `q` quit); the modifier is now chosen at runtime (Command only on the native Quartz backend).
- [x] Hide the tray behind a small interface (`tray-backend.h`): GtkStatusIcon on GTK3 (`tray-backend-statusicon.c`, the only file allowed deprecated API), so phase 4 can swap in the new backends.
- [x] Hide window position and urgency behind small functions too (see section 9): `gtkutil_window_move / _get_position / _can_position / _set_urgent`. X11 and Windows behave as before (checked: saved position and size restored and saved again); on Wayland move is a no-op and the saved position is left alone.

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

- [ ] Rebuild the Windows (MSVC `win32/*.sln`, Inno Setup installer) and macOS (`osx/`, gtk-mac-bundler) packaging for GTK4. Phase 1 left their HexChat names alone because those scripts get rewritten here anyway.
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
| Font picker (Preferences > Text box > Browse) | GtkFontSelectionDialog is deprecated in GTK3 and gone in GTK4 | GtkFontChooserDialog: a searchable list with a preview instead of three columns. It returns the same Pango font string, so `text_font` is unchanged. |
| Colour picker (Preferences > Colors) | GtkColorSelectionDialog (the triangle) is deprecated and gone in GTK4 | GtkColorChooserDialog, opened straight in its editor (hue/saturation plane, hex entry). Same RGB out; `colors.conf` is byte-identical (`tests/ux/colors-roundtrip.sh`). |
| Striped rows in lists (DCC, ignore, keyboard shortcuts, …) | `gtk_tree_view_set_rules_hint` is deprecated with no replacement | Up to the theme. Adwaita doesn't stripe. |

## Rules for every PR

- One area per PR, and it builds and runs.
- Before/after screenshots for any change to something the user sees.
- No new features, no "while I'm here" redesigns, no default changes. Ideas go into a `later/` list for after 3.0.
- Never change `src/common` just to make the port easier, unless it's the `fe.h` boundary.
- Plugin API and config files stay compatible.
