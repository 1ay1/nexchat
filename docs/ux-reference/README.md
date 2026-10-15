# UX reference

This is what HexChat 2.16.2 (GTK2) looks like and how it behaves. nexchat has to match it. Every port step gets checked against it.

- `gtk2/`: screenshots of the unchanged GTK2 build: main window (tree and tabs), every menu, every dialog, every preferences page, and right-click popups. Captured on Xvfb with Openbox, using the default GTK2 theme (Raleigh) at 96 dpi.
- `surface/`: text dumps of the parts you can't see: default keybindings, the menu tree with mnemonics and accelerators, every `/set` pref, every built-in `/command`, and the preferences pages with their widgets.

## Regenerating

```sh
# screenshots (needs Xvfb, xdotool, imagemagick; openbox optional)
tests/ux/capture.py --bin <path to hexchat/nexchat binary> --out <dir> [--ld-library-path ...]

# text dumps (parses the source, works for any toolkit)
tests/ux/dump_surface.py --out <dir>

# side by side, reference on the left
tests/ux/compare.py docs/ux-reference/gtk2 <dir> /tmp/compare
```

The GTK2 reference build links against a private GTK 2.24.33 build, because Arch no longer ships gtk2. See `tests/ux/build-gtk2-reference.sh`.

## Parity checklist

Each port phase has to keep all of these true. A difference only goes in if GTK4 or Wayland makes matching impossible, and then it's listed in ROADMAP.md section 9.

Layout
- [ ] Main window: channel tree on the left, topic bar on top, text box in the middle, user list on the right with the "N ops, M total" header, nick button + input box at the bottom.
- [ ] Tabs layout (View > Channel Switcher > Tabs) puts tabs at the bottom, like `main-tabs.png`.
- [ ] Both panes can be resized with the dotted grip handles, and the sizes are saved (`gui_pane_*`).
- [ ] User list icons for ~ & @ % + (`ulist_*.png`), and their order.
- [ ] Text box: timestamp column, right-aligned nicks, separator line, wrapped lines indented under the text column, mIRC colours, bold, actions, join/part/mode colours.
- [ ] The search bar sits inline above the input box, with Find, arrows, Highlight all, Match case, Regex.

Menus
- [ ] Menu bar order and labels: HexChat, View, Server, Settings, Window, Help (plus Usermenu if enabled).
- [ ] Every item, separator, submenu, toggle and radio in `surface/menus.txt`, with the same mnemonics and accelerators.
- [ ] Menu bar hidden with Ctrl+F9; right-clicking the text box then offers the menu.
- [ ] Popups: nick (userlist), text box, channel tree / tab, input box (spelling), URL, channel name.
- [ ] `/MENU` add/del/update from users and plugins still works, including toggles, radio groups and icons.

Keys
- [ ] Every binding in `surface/keybindings.txt` does the same thing.
- [ ] Tab completion (nick, command, channel), Up/Down input history, Ctrl+Up/Down.
- [ ] Alt+1..9 switches tabs, Ctrl+PgUp/PgDn cycles, Ctrl+W closes, Ctrl+F searches.
- [ ] The Keyboard Shortcuts editor reads and writes the same `keybindings.conf`.

Dialogs
- [ ] Each dialog in `gtk2/` has the same widgets, labels, order, buttons and default button: network list and edit, preferences (all 16 pages), join, channel list, ban list, DCC, friends, ignore, plugins, raw log, URL grabber, char chart, the editors (auto replace, CTCP, dialog buttons, text events, URL handlers, user commands, userlist buttons/popup), about, file choosers.
- [ ] The network list opens at first start, and "Skip network list on startup" works.

Behaviour
- [ ] Tray icon, its menu, and blinking on highlight / PM / file offer.
- [ ] Window title format, urgency/flash on highlight.
- [ ] Detach/attach tabs into separate windows.
- [ ] Marker line, URL hover and click, nick click, double-click actions.
- [ ] Drag a tab to reorder it; drop a file on a nick for DCC.
- [ ] Spell checking in the input box with right-click suggestions.
- [ ] Theme files (.hct) and `colors.conf` give the same colours.
