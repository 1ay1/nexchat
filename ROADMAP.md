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
- [→ phase 4] Menus from GMenuModel + GAction. **Moved to phase 4.** A probe on GTK3 (`tests/ux/gmenuprobe.c`) showed that `gtk_menu_bar_new_from_model` on GTK3 makes every item a GtkCheckMenuItem internally, ignores the model's `accel` attribute without a GtkApplication (every shortcut would need wiring by hand), and hits `gtk_menu_tracker_remove_items` assertions on the runtime add/remove that `/MENU` and plugins do. GtkMenu is not deprecated in GTK3, so the menus already build with `GTK_DISABLE_DEPRECATED`; porting them twice (GTK3 models, then GTK4 popovers) buys nothing. The requirements carry over to phase 4 unchanged:
  - main menu, popups, user list buttons, `/MENU` and plugin menu entries, toggles and radio groups (`menu_entry`: `group`, `state`, `ucmd`), icons, markup labels, enabling and disabling at runtime;
  - mnemonics (`_Server`, `_Window`…) and accelerators must stay exactly the same;
  - `fe_menu_add/del/update` keep the same behaviour for plugins.
  - `tests/ux/menus.sh` (baseline `tests/ux/menus.expected`, recorded from the GtkMenu code) must pass unchanged.

**Phase 3 is done.**
- [x] `GtkUIManager`/`GtkAction`, if any: none used.
- [x] Dialogs: stop using `gtk_dialog_run`. The quit dialog is async now (same buttons, order, modality; checked by driving Escape/Cancel/Quit on GTK2 and GTK3). The two message boxes that must block (startup error before the main loop, `FE_MSG_WAIT`) use `gtkutil_dialog_run_blocking()`, a nested main loop with the same semantics.
- [x] Key handling in `fkeys.c`: `key_handle (wid, keyval, state, sess)` returns `KEY_NOT_HANDLED / KEY_HANDLED / KEY_HANDLED_STOP` and doesn't touch the event; `key_handle_key_press` is a thin GTK3 adapter, GTK4 gets one for GtkEventControllerKey. Checked by `tests/ux/keys.sh` (types over XTest, checks what reaches the server: send, Up history, Tab completion, Ctrl+K). Found on the way and fixed: on any macOS build, Ctrl shortcuts were registered as `<Meta>`, which on X11 matches the bare key (typing `q` quit); the modifier is now chosen at runtime (Command only on the native Quartz backend).
- [x] Hide the tray behind a small interface (`tray-backend.h`): GtkStatusIcon on GTK3 (`tray-backend-statusicon.c`, the only file allowed deprecated API), so phase 4 can swap in the new backends.
- [x] Hide window position and urgency behind small functions too (see section 9): `gtkutil_window_move / _get_position / _can_position / _set_urgent`. X11 and Windows behave as before (checked: saved position and size restored and saved again); on Wayland move is a no-op and the saved position is left alone.

## Phase 4: GTK4

**Approach (decided):** move code to GTK4-compatible spellings while still building and testing on GTK3, then switch. Every GTK3/GTK4 difference that is only spelling goes through `src/fe-gtk/gtkcompat.h` (GTK3 branch = the old call, GTK4 branch compile- and behaviour-checked by `tests/gtkcompat-test.c` against real GTK4, in CI). Behaviour differences (drawing, events, menus, DnD) get their own files. Measured size: ~1,350 compile errors / 254 distinct GTK3-only symbols on a trial GTK4 build.

- [x] Step 1, mechanical: `gtk_box_pack_start/end` (exact GTK3 order and expand/fill/padding semantics on GTK4, tested), `gtk_container_set_border_width`, `gtk_widget_show_all`, `gtk_entry_get/set_text`, `gtk_widget_destroy` → `nx_*`. ~250 call sites. GTK3 suite (10 tests incl. GUI) and screenshots unchanged.
- [x] Step 2, containers: `gtkutil_container_add` has a GTK4 branch (window/frame/scrolled window/viewport/button/expander/paned setters); `gtk_container_get_children` / `gtk_bin_get_child` → `nx_widget_get_children` / `nx_bin_get_child` outside menus (menu.c's go with the menu rewrite).
- [x] Step 3, small spellings: GtkButtonBox → `nx_button_box_new (orientation, layout)` (GTK4: homogeneous GtkBox + halign; SPREAD/END/START/EDGE all screenshot-checked), scrolled window new/shadow, window position hints (no-op on GTK4, the WM places windows), default button, `gtk_widget_get_toplevel`.
- [x] Step 4, input outside xtext: `gtkevents.h` — `nx_on_click/key/scroll/crossing/focus_in` with plain-value handlers (button, click count, x/y, state, keyval, scroll direction). GTK3: the old `*-event` signals; GTK4: GtkGestureClick / EventControllerKey/Scroll/Motion/Focus in the capture phase. Converted the input box, user list, channel tree and tabs, ban/channel/URL lists, char chart, Esc-to-close, server list, key dialog, joind, window focus. `nx_current_event()` hands the GdkEvent to the few callees that position popups from it. Tests: `gui-keys` and new `gui-mouse` (double-click a nick, wheel over the channel tree switches tab, right-click nick menu). Left: xtext (its own drawing/input rewrite), GtkMenu's own enter/leave (menu rewrite), drag and drop.
- [x] Step 5, xtext input: press/release/motion/scroll/leave go through `gtkevents.h` (new `nx_on_release` — a GtkGestureDrag on GTK4 so a release after a drag-select isn't lost — and `nx_on_motion`). The pointer is no longer polled with `gdk_window_get_device_position`: motion records `pointer_y` for the drag-scroll timers. `word_click` hands a `GtkXTextClick` (button, click count, state) instead of a GdkEventButton. Cursors go by name through `nx_widget_set_cursor_name` (GTK3 keeps GDK_HAND1 / GDK_LEFT_SIDE). The GTK3 pointer grab during selection stays GTK3-only. `tests/ux/mouse.sh` now also drag-selects and double-clicks a word in the chat view and checks the copied text at the ircd.

- [x] Step 6, xtext widget plumbing for GTK4 (in `#if` branches, GTK3 untouched): no own GdkWindow (overflow hidden), `measure` / `size_allocate(w,h,baseline)` sharing one `gtk_xtext_resized`, `snapshot` wraps the existing cairo `draw` via `gtk_snapshot_append_cairo`, `dispose` instead of `destroy`, a scale-aware image backing surface. Clipboard: CLIPBOARD via `gdk_clipboard_set_text`; PRIMARY gets a content provider and a "changed" handler that unselects when someone else takes it (GTK3's selection-clear-event). Background image: own pixbuf→cairo conversion, checked bit-identical against `gdk_cairo_surface_create_from_pixbuf` (with and without alpha) and screenshot-identical with a tiled translucent background. fkeys: dead `key_handle_key_press` removed, actions take `gpointer` instead of an always-NULL GdkEventKey, `STATE_ALT` is GDK_ALT_MASK on GTK4. xtext.c and fkeys.c compile against GTK4 apart from shared headers (menu.h, maingui.h, gtkutil.h), which go with the menu and DnD steps.

- [x] Step 7, drag and drop through `gtkdnd.h`: layout drags (channel tree ⇄ user list ⇄ chat scrollbar, same-app only) and file drops (DCC, `text/uri-list`) with plain-value callbacks (`y` + which kind / the uri-list string). GTK3: the same targets, flags and info numbers on one target list per widget, drag icon from the widget snapshot as before. GTK4: GtkDragSource with private mime types + GtkDropTargetAsync for layout drags (no data transfer, the drop is the message), GtkDropTarget(GDK_TYPE_FILE_LIST) turned back into a uri-list for files. The drop-half outline painter is still GTK3 `::draw` (TODO with the GTK4 drawing of the panes). Tests: `gtkdnd` checks every widget's target lists against the old literal tables (and fails if one changes); `gui-mouse` drags the user list to left-bottom and the channel tree onto the chat scrollbar and checks the saved `gui_ulist_pos` / `gui_tab_pos`.

- [x] Step 8, shared headers: popup menus take `GdkEvent *` (`nx_menu_popup_at_pointer`), the chanview context-menu callback gets button + click count, `gtkutil_copy_to_clipboard` loses its always-NULL selection argument (GTK4 branch added). `gui-mouse`: the URL menu's Copy and middle-click tab close.
- [x] Step 9a, `nxmenu.h`: menus without GtkMenu. Items, checks, radio groups, separators, submenus, insert at a position, remove/clear, sensitivity, quiet state changes, shown text, per-item data, "submenu shown" hook, popup at the event. GTK3: exactly the GtkMenu calls nexchat made (an `nx_menu` *is* the GtkMenu, so code converts piecemeal). GTK4: GMenu (separators become sections) + GSimpleActionGroup (one string-state action per radio group), shown in a nested GtkPopoverMenu over the widget the click came to (`nx_current_event_widget`). `nxmenu` test runs one script on both GTKs (in CI against GTK4) including GTK3's radio callback order (off, then on; re-choosing runs it again).
  - Known GTK4 differences: empty sections draw nothing (a doubled/leading separator vanishes); a submenu's "shown" hook runs when the popup opens, not when the submenu does (the nick info /WHOIS is sent a little earlier); submenu entries can't be greyed (only /MENU -d does that).
- [x] Step 9b, popups on nxmenu: nick (incl. the WHOIS info submenu), URL, channel, tab, input-box colours, ban list, channel list, the tray's items and the /MENU plugin API. `tests/ux/shot-popups.sh` screenshots all six right-click menus: pixel-identical to before. Fixed on the way: "Unknown" info lines copied a stack buffer, the ban list menu leaked.
- [x] Step 9c, the main menu bar on nxmenu: `nx_menu_bar_new` (GTK3 GtkMenuBar, GTK4 GtkPopoverMenuBar), shortcuts via `nx_menu_set_accel_window` / `nx_item_set_accel` (GTK3: a GtkAccelGroup with the old "works while sensitive, even with the bar hidden" rule; GTK4: a managed-scope GtkShortcutController firing the item's action, so a disabled item's shortcut does nothing), pixbuf icons, icon-column submenus, lazy model rebuilds. `gui->menu_item[]` are `nx_item`s, `gui->main_menu` the bar. `menu_debug_dump` walks the nx_menu tree, so `gui-menus` checks the same thing on either GTK; baseline unchanged. `shot-popups.sh` also shoots the bar (View open) and the middle-click main menu: identical to before. menu.c, maingui.c, chanlist.c and banlist.c have no GtkMenu calls left; what's left is the tray (GtkStatusIcon, GTK3 only) and the input box's own popup (moves with the spell entry).
- [x] Step 10, the long tail through `gtkcompat.h` / `gtkevents.h`: flat buttons, frame/viewport shadows, paned pack1/pack2, box reorder, radio buttons (GTK4 check-button groups), window role/type hints (no-ops on GTK4), label wrap, window size, icon sizes (`nx_icon_size`: GTK3's enum values, GTK4 pixel sizes), button images, grid child cells, paned handle size, removing a child, event boxes, image pixbufs, minimize, window state / configure / close-request hooks, the main loop (a GMainLoop until the GtkApplication step), composited check, CSS loading, forwarding a key to the input box, entry width. gtkutil: GTK4 file requests on GtkFileDialog (same callback contract), themed icons as pixbufs, no window placement or urgency (TODO), window icon by name. Every frontend file now compiles against GTK4 except the spell entry, the tray and sexy-iso-codes (build defines only); CI checks that with a list that can only shrink. `tests/ux/compare-builds.sh` screenshots the main window and 8 dialogs with two builds side by side: all identical (the only differing pixels are the input box's blinking cursor).
- [x] Step 11, the spell entry: its menus are nx_menus (suggestions, "More…", per-language submenus, Add to Dictionary, Ignore All; icon items keep plain labels via `NX_ITEM_PLAIN_ICON`). The input box's colour-code submenu comes in through `sexy_spell_entry_set_menu_hook` instead of the entry's populate-popup. GTK4 branches: the underline/colour attributes go through `gtk_entry_set_attributes` (GtkText leaves them off during IM preedit itself), word breaks from a private PangoLayout, and the right-click menu is built whole (spelling, then GtkEntry's own Cut/Copy/Paste/Delete/Select All/Insert Emoji with gtk30's translations, then the hook), the clicked character found by a binary search over `gtk_text_compute_cursor_extents`. Fixed: macOS never loaded enchant 2 (`libenchant-2.dylib`), so spell checking was off on every Homebrew/MacPorts install. New `gui-spell` test types "wrold", right-clicks, picks the first suggestion and checks the text became "world"; its menu and submenu screenshots are pixel-identical to before. CI installs hunspell en_US so it runs there too.
- [x] Step 12, the tray menu: plugin-tray.c builds an nx_menu and hands it to the backend (`tray_backend_show_menu`), which owns showing and freeing it; the GtkStatusIcon backend keeps the GtkMenu popup and Windows' close-after-2s-outside timer. A GTK4 backend (StatusNotifierItem + DBusMenu over GDBus on Linux, the native APIs elsewhere) is still to write; until then plugin-tray.c compiles for GTK4 and only the GTK3-only GtkStatusIcon backend doesn't. New `gui-tray` test: with `NEXCHAT_TRAY_MENU_DUMP` set the tray menu is built as a right-click would and written out (items, Blink-on states, Away/Back sensitivity, a pref change showing up); its baseline was checked against the previous build, which gives the same menu.
- [x] Step 13, **the GTK4 build runs**: `meson setup -Dgtk=4` (default stays 3). The tray uses `tray-backend-none.c` on GTK4 until the StatusNotifierItem backend exists. First run fixes: check buttons aren't toggle buttons in GTK4 (`nx_toggle_get/set_active`), no `gtk-key-theme-name`, xtext's adjustment upper < page size (GTK4 asserts; clamped on GTK4 only). It starts with `G_DEBUG=fatal-criticals`, connects, joins and draws the main window, chat text, topic and user list on native macOS (Quartz). `NEXCHAT_SHOT=path[:secs]` makes either build write its main window as a PNG (for backends where a screen grab isn't possible). New `smoke-gui` test (backend-independent: connect, join, render, no criticals) runs on both builds; CI has a GTK4 job that builds everything and runs smoke-gui, nxmenu and gtkcompat on Ubuntu 24.04 (GTK 4.14).

- [x] meson: `gtk4` behind `-Dgtk=4` (step 13). No libadwaita.
### What's left (measured after step 13, on Arch / GTK 4.24 / X11; steps 14-16 and 18 done since)

State: the GTK4 build needed 3 Linux fixes to compile (`gdk/gdkx.h` is GTK3 only, `gtk_show_uri` lost an argument); upstream CI only builds on GTK 4.14. With those, 8 of 13 tests pass. Under `GDK_BACKEND=x11` gui-keys, gui-tray and gui-colors-roundtrip pass too; what still fails is gui-mouse (3 chat-view selection checks) and gui-menus (accel spelling only). The build still makes 1026 deprecation warnings: ~900 are GtkTreeView/GtkListStore/cell renderers (phase 5), the rest are `gtk_widget_show/hide` (204), GtkDialog/GtkMessageDialog, GtkComboBoxText, the font/colour chooser dialogs, allocation getters and `gtk_show_uri`.

Order matters: first make the GTK4 build trustworthy (tests and CI), then make it look the same, then fill the platform gaps, then drop the deprecated API, then switch the default.

- [x] Step 14, Linux GTK4 builds and tests. The 3 compile fixes (`gdk/gdkx.h` is GTK3 only, `gtk_show_uri` lost an argument, a `GtkBox *` passed as a widget). GUI tests source `tests/ux/isolate.sh`: `GDK_BACKEND=x11` and no `WAYLAND_DISPLAY` (from a Wayland session GTK4 opened on the real compositor and every XTest key went nowhere), a private `XDG_CONFIG_HOME` with Adwaita, and a fontconfig file that makes Monospace/Sans DejaVu like on Ubuntu (the user's theme and fonts moved every click target). `start_xvfb` waits for the display instead of sleeping 1.5s (a busy machine lost that race). Every GUI test runs with `G_DEBUG=fatal-criticals`. `gui-mouse` writes its own known chat lines (`/clear` + `/echo`) instead of clicking the demo chatter, and checks the URL menu too; `gui-colors-roundtrip` waits for Preferences to map. The GTK4 CI job runs the whole suite. Both builds pass everything here (GTK4 13/13, GTK3 16/16). Not done: a second CI job on a newer GTK than Ubuntu's.
- [x] Step 15, criticals: About dialog (logo as a texture; GTK4's has no `response`, it closes on `close-request`); popups on a tree view (the user list) hang off its parent, since a popover added after a tree view's header buttons trips `gtk_css_node_insert_after`; Preferences' tree `cursor-changed` fires once more during destroy with no selection. `gtk_window_set_icon` was dead code on GTK4 already (icon by name).
- [x] Step 16, chat view text metrics. Confirmed by measuring: GTK4 gave a 17px line, 12/3 ascent/descent and 8px cells for Monospace 9 where GTK3 has 18, 13/4 and 7. xtext now draws with its own pango context (hinted metrics, slight hinting, rounded glyph positions), which matches GTK3 exactly: same gaps, line height and wrapping.
- [x] Popup menus on GTK4 opened with the first item focused, so the first Down skipped it (GTK3 starts with nothing selected): the first Down on a fresh popup is eaten. Popups use their own action prefix (`nxp.`): with `nx.`, the main menu's shortcut labels showed up on popup items ("world  Ctrl+S") and a popup's Return could reach the wrong item.
- [ ] Step 17, look. Done so far, checked with `tests/ux/compare-builds.sh` (now opens 14 windows and menus through the menu keys; the old slash commands didn't exist, so it used to shoot the main window 9 times): popup menus look like GTK3 menus (square, 1px border, 25px rows, check/radio boxes, item icons shown next to labels, submenu arrows at the right edge); GTK3's stock-only icons (gtk-ok, gtk-cancel, gtk-connect, ...) ship in our resources for the GTK4 build, since GTK4 has none; square frames, GTK3's 4px corners on entries and buttons, no blue focus outline on lists; tree-view lists get their frame; `nx_set_border_width` on a window pads its child (GTK4 windows ignore margins); button boxes give buttons GTK3's equal 85px+ widths; radio groups start with the first one on; the network list selects and scrolls to the current network after it maps. Left: the chat view's scrollbar has no stepper arrows (GTK4 has none, add to section 9), the input box is a few px taller, the Preferences category tree rows are 20px instead of 23px, the about dialog is GTK4's own layout.
- [x] Step 18, `menu_debug_dump` writes accels itself (`<Primary><Shift><Alt>key`) instead of `gtk_accelerator_name`, which spells them differently on GTK3 and GTK4; gui-menus passes on both with the existing baseline.
- [x] Step 19, D-Bus and GtkApplication. The remote API (`dbus-plugin.c`) is rewritten on GDBus: same bus name, object paths, interfaces, methods, signals and per-client hooks/lists, so remote scripts keep working, and dbus-glib + dbus-binding-tool are gone from the build, CI and Flatpak. New `dbus-remote` test (private bus via dbus-run-session) drives every method, the hook signals, Unhook, a client that exits without Disconnect, and `nexchat -e -c`; it passes the same on the old dbus-glib build and the new one. `-e/--existing` stays on that remote API (it was already GDBus) rather than GApplication's single-instance, which would change behaviour: a plain second `nexchat` still opens a second instance like HexChat. The GTK4 build runs a registered, non-unique GtkApplication `io.github.nexchat` and every window joins it, so Wayland docks match windows to the desktop file; startup works without a session bus. The C example client is ported to GDBus.
- [x] Step 20, window manager gaps on X11 (GTK4). Window position: the saved `gui_win_left/top` is applied with Xlib at realize (USPosition hint + XMoveWindow) and read back with XTranslateCoordinates; moves are seen through the X11 display's `xevent` ConfigureNotify, as GTK3's configure-event did. Urgency: `XSetWMHints` urgency hint, cleared when the window becomes active (`is-active`, which needs a WM; GTK4's focus controllers don't see a window being re-activated). The drop-half outline over the channel tree / user list is drawn by a GtkDrawingArea in a GtkOverlay around them, with the same cairo code GTK3 ran in `::draw`. New `gui-wm` test (position restored, move saved, and under openbox: urgency set while another window has focus, cleared on activation) passes on both builds. Not done: Windows (`FlashWindowEx`/`SetWindowPos`) and Wayland xdg-activation, which need those platforms to test on; the chat scrollbar's drop outline (it's not in an overlay).
- [ ] Step 21, tray: StatusNotifierItem + com.canonical.dbusmenu over GDBus, fed by the existing nx_menu (`tray_backend_show_menu`); blinking via icon swaps; Windows `Shell_NotifyIcon`. gui-tray also checks the exported dbusmenu layout against `tray.expected`.
- [ ] Step 22, the rest of the deprecated non-tree API, through `gtkcompat.h` so GTK3 stays as is: `gtk_widget_show/hide` → `gtk_widget_set_visible`; the ~15 GtkDialog/GtkMessageDialog uses → a small `nx_dialog` (GtkWindow + content box + button row, same response ids, same layout, so they don't turn into GtkAlertDialog's different look); GtkComboBoxText → GtkDropDown on a GtkStringList; font/colour pickers → GtkFontDialog/GtkColorDialog (colors-roundtrip must stay byte-identical); `gtk_show_uri` → GtkUriLauncher; allocation getters → `gtk_widget_get_width/height`; pixbufs → GdkTexture where GTK4 takes a paintable (window icon, about logo, menu icons, images). Exit: the GTK4 build has no deprecation warnings outside the tree-view files.
- [ ] Step 23, other platforms: run smoke-gui and the GUI tests under a headless Wayland compositor (weston `--backend=headless`, or mutter `--headless`) with tests that don't need XTest; build and smoke on Windows (MSYS2 GTK4) and macOS. Fill the section 9 table with what's actually different on each.
- [ ] Step 24, switch: `-Dgtk=4` becomes the default once the parity checklist and all tests pass on GTK4. Keep `-Dgtk=3` for one release as the fallback, then delete the GTK3 branches and fold `gtkcompat.h`/`gtkevents.h`/`gtkdnd.h` into plain GTK4 calls.

Tree views stay on GtkTreeView through phase 4 (they work and look the same on GTK4); that's phase 5.

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
