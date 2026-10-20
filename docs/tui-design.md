# nexchat TUI — design

`nexchat --tui` runs nexchat in the terminal: the same core (src/common:
servers, sessions, text events, logging, plugins, bouncers, config), drawn
with maya instead of GTK. It is the GUI's equal, not a stripped-down
`nexchat-text`: everything the main window does, with the keyboard AND the
mouse.

Status: the widget kit (src/fe-tui/widgets) and step 1 of §9 are done:
`nexchat --tui` / `nexchat-tui` is a working IRC client with the mouse
(src/fe-tui: fe-tui.c, ui.cpp, bridge.h). The rest of §9 follows.

### Done so far

* One binary per frontend; `nexchat --tui` execs `nexchat-tui` before
  GTK starts (next to the GTK binary, then libexecdir/nexchat, then
  $PATH). Installed to bindir with a libexecdir/nexchat symlink.
* fe-tui.c implements every fe_* in fe.h; the TUI runs in GLib's loop
  with coalesced redraws.
* Window: menu bar, topic, tree, chat, user list (the GUI's
  gui_ulist_sort order), status, input with the nick as prompt.
* Keys: §5's set, the GUI's formatting keys, Alt-1…9, F1/F2/F3/F10, Ctrl-G
  switcher, Tab completion through the core's own lists.
* Mouse: menus (click, hover, submenus), tree (click, ▾/▸, middle-click
  closes, right-click menu), user list (click, double-click =
  gui_ulist_doubleclick, right-click = popup.conf nick menu), chat (wheel,
  drag to select + autocopy over OSC 52, double/triple-click word/line,
  click a URL / Ctrl-click a channel, right-click nick/URL/channel/text
  menus, middle-click paste), dividers (drag to resize, double-click to
  hide), status (click "↓ N more", click to toggle away), input (click to
  place the caret, right-click menu, middle-click paste, wheel history).
* Dialogs: fe_get_str/int/bool/file, fe_confirm, error messages; themes
  pickable from View → Theme (all of maya's schemes, saved in tui.conf).
* Windows (src/fe-tui/*.cpp, each a widgets/Panel over the bridge):
  - Network List (netlist.cpp + networks.c): the networks and the global
    user info; servers / autojoin / connect commands as editable lists,
    login method, TLS and the other flags; Connect, Add, Remove. Opens at
    startup unless gui_slist_skip, and on Ctrl-S.
  - Preferences (prefs.cpp): setup.c's pages minus what a terminal owns
    (fonts, zoom, opacity, tray, spell check, icons); 85 settings by their
    hexchat.conf names, applied live, saved on close.
  - Channel List (chanlist.cpp): /list as a table, type to filter, click
    a heading to sort, Enter / double-click to join.
  - Ignore List, Friends List (lists.cpp): entries, per-type ignore
    toggles, networks for a friend, Add / Remove.
  - File Transfers (transfers.cpp): progress bars, speed, ETA; Accept,
    Resume, Abort, Clear; opens itself on offers (gui_autoopen_*).
  - Raw Log: a buffer of its own under the network (Window → Raw Log).
  - Window → URL Grabber, Mentions, Plugins and Scripts, Ban List
    (tools.cpp over tools.c, the GUI's urlgrab.c, mentions.c,
    plugingui.c, banlist.c): a table each, the GUI's buttons. The Ban
    List lists the kinds the server has (+b/+e/+I/+q), Space marks rows,
    Remove / Crop / Clear -mode them.
* Tests: tui-widgets (the kit); in a pty against fake_ircd, keys and mouse:
  tui-smoke, tui-netlist, tui-prefs, tui-chanlist, tui-lists,
  tui-transfers (a real DCC download, byte for byte), tui-find (the find
  bar: steps, wraps, painting, case/regex), tui-keys (keybindings.conf
  and replace.conf), tui-tabs (the switcher as tabs, top / bottom, and
  the tree on the right), tui-tools (the four
  above, a real plugin loaded and unloaded), tui-app-theme (every
  window under native, a light and a dark scheme), tui-bouncer (the
  Bouncer windows against real soju and Ergo, the GUI's scenarios); and
  tests/ux/tui-theme-check.py for the gallery.

### Still to do

* Packaging: the .app bundles nexchat-tui; Windows doesn't build it yet.

---

## 1. Shape of the program

```
            ┌──────────────── one process, one thread ───────────────┐
 argv ──►  main (src/fe-tui/main.c) ── --tui? ──► the TUI frontend
            │                          else      ──► the GTK frontend
            │
            │  GLib main loop  (the core's: sockets, timers, DNS, …)
            │     ├─ tty readable   → maya::Screen::read → events → Ui
            │     ├─ tty writable   → Screen::flush (only while backpressured)
            │     ├─ SIGWINCH       → Screen::on_resize
            │     ├─ redraw idle    → Screen::present(Ui::view())   ← coalesced
            │     └─ escape timeout → Screen::resolve_pending_input
            │
            │  src/common  ──fe_*()──►  fe-tui glue (C ABI, C++ inside)
            │                              │ owns Ui state, never draws
            └──────────────────────────────┴───────────────────────────
```

* **One binary, two frontends.** The core calls `fe_*` functions, and
  fe-gtk and fe-tui both define every one of them, so they can't simply
  link into one executable. The plan:
  - `nexchat` (the GTK build) gains `--tui`. When it's given, `main`
    `execv`s the TUI binary installed next to it (`nexchat-tui`, in
    libexec on Linux and in `Contents/MacOS` in the .app; on Windows it's
    `nexchat-tui.exe` beside `nexchat.exe`). The GTK binary never touches
    a display in that case: the check runs before `gtk_init`.
  - `nexchat-tui` is also installed and works on its own, which is what a
    headless server (no GTK at all, built with `-Dgtk-frontend=false`)
    gets.
  - Tried and rejected: one binary with a frontend vtable. That means 95
    `fe_*` functions rewritten as indirect calls in fe-gtk and the core,
    a large risky diff to the GUI for no user-visible gain.
* **No second event loop.** maya's `Screen` is a device with no loop
  (screen.hpp). The core lives in GLib's loop, so the TUI is driven from
  it, the same way the tui-gallery's hand-written loop drives it. jaal is
  not linked (`MAYA_BUILD_APP=OFF`).
* **Redraws are coalesced.** A `fe_*` callback only changes state and
  calls `ui_dirty()`, which schedules one idle callback. A burst of 500
  lines of scrollback replay costs one frame. maya's frame acks come back
  as empty `read()`s and must never cause a redraw (learned in the
  gallery).
* **Fullscreen** (alt screen), mouse on, bracketed paste, focus events,
  title via OSC 0, clipboard via OSC 52 (`Screen::write_clipboard`).

## 2. Layering

```
src/fe-tui/
  widgets/        the kit: knows maya, not nexchat (done)
  ui/             the app: knows the kit, not the C core
     model.hpp      Buffer, Network, the window state, all plain C++
     ui.hpp/.cpp    Ui: the state, key/mouse routing, view()
     menus.cpp      menu bar, context menus, the command palette
     dialogs.cpp    prompts, confirm, the dialogs (§6)
  glue/           the C side: fe-tui.c implements fe.h, talks to Ui
     fe-tui.c       every fe_* (C), calls nx_ui_* (C ABI to ui/)
     bridge.cpp     nx_ui_* → Ui, and Ui → core (handle_command, …)
  main.c          --tui dispatch (GTK binary) / plain main (TUI binary)
```

The rule: **ui/ never includes a core header, glue/ never includes
maya.** The core's structs (`session`, `server`, `struct User`) cross the
bridge as opaque keys plus the values the UI shows, the same way the
widgets already take `ChanEntry` / `UserEntry`. So the UI is testable
without a server (unit tests drive `Ui` with fake events), and core
changes don't ripple into drawing code.

## 3. The screen

```
┌ menu bar ─ File  Edit  View  Server  Window  Help ──────────── 12:04 ┐
│ #nexchat [+nt] │ nexchat 0.1.0 is out \o/ | https://… | be nice      │  topic (click: edit)
├──────────────────────────────────────────────────────────────────────┤
│▾ Libera     ‖ 15:02   <ayush> │ the TUI is drawn with maya   ‖@ayush │
│  #nexchat   ‖ 15:02     <bob> │ nice!                        ‖+bob   │
│  #gtk     4 ‖ ─────────────────────────────────────────────  ‖ carol │
│  bob    1 ● ‖ 15:03   <frank> │ alice: you there?            ‖ dave  │
├──────────────────────────────────────────────────────────────────────┤
│ alice(+Ziw) · Libera · lag 0.1s · ↓ 3 more     bob is typing…        │  status
│ [alice] hello wor▏                                         [ @ ][ ☺ ]│  input + buttons
└──────────────────────────────────────────────────────────────────────┘
```

* Built from MainLayout, plus a menu bar row (View → Menu Bar hides it,
  as in the GUI, and F10 still opens it), the input's nick button (click:
  change nick, like fe-gtk's), and the reply bar above the input when
  replying.
* `‖` marks the pane dividers. They're draggable: drag to resize the tree
  or the user list, double-click to collapse. Sizes persist in
  `hex_gui_pane_left_size` / `_right_size`, the GUI's own prefs.
* Responsive as the widgets are: under 100 columns the user list hides
  (toggled with Ctrl-L / View → User List), under 60 the tree hides too.
  The switcher, Alt-←/→ and Alt-1…9 still reach every buffer.
* Tabs vs tree: the GUI's gui_tab_layout and gui_tab_pos are honoured
  (Preferences → Channel switcher has both). Tabs is one row of tabs
  (ChanTree::strip, the same ChanEntry input) at the top (pos 5) or the
  bottom (everything else, as the GUI's left/right tabs make no sense in
  one row); ‹ › when they don't fit, › coloured by the activity past the
  edge; click, wheel, right-click menu, middle-click close. The tree goes
  left (pos 1/2) or right (3/4); Hidden (7) hides it.

## 4. Mouse — everything the GUI does with a pointer

Hover (mode 1003, on by default; `hover=0` in tui.conf turns it off for a
terminal that handles it badly): what a click would act on is underlined
while the pointer is over it, and only then — a link, nick or channel in
the chat, a tab in the tree or the tab strip, a nick in the user list, a
menu-bar title, the status bar's network / "↓ N more below" / key hints
(all clickable), a panel's list rows, buttons and form labels, the
Bouncer window's controls, dialog buttons. The ✕ turns red, a pane
divider lights up under the pointer and while dragged, and the pointer
shape follows (OSC 22: a hand, ↔ on a divider, an I-beam on text; reset
on exit). Motion: a spinner while connecting and walking dots when
someone types, at ~8 frames/s only while they're on screen.

Mouse reporting is SGR 1006, with any-motion (1003) for hover. Every hit is decided against the last frame's
layout: Ui keeps a `HitMap` (rect → target) filled in `view()`.

| Where | Left | Double | Right | Middle | Wheel |
|---|---|---|---|---|---|
| tree row | switch to it | — | context menu (close, detach, channel list, auto-join…) | close (like a middle-clicked tab) | scroll tree |
| tree ▾/▸ | collapse/expand network | | | | |
| chat: nick | select the nick in the user list | open query (`hex_gui_ulist_doubleclick`) | nick menu (`popup_list`) | | scroll chat |
| chat: URL | open (`fe_open_url`) when `hex_gui_url_mod` is satisfied, else select | | URL menu (open, copy) | | |
| chat: #channel | join | | channel menu (join, part…) | | |
| chat: text | start selection (drag) | select word | text menu (copy, clear, search) | paste selection | |
| user list row | select (Ctrl: toggle, Shift: range) | `hex_gui_ulist_doubleclick` command | nick menu (multi-select aware) | | scroll list |
| topic | edit topic (inline editor) | | copy / edit | | |
| status: lag / away | lag: `/ping`; away: toggle away | | | | |
| status: "↓ N more" | scroll to the bottom | | | | |
| input | place caret, drag to select | select word | cut/copy/paste/select-all menu | paste | history |
| divider | drag to resize | collapse | | | |
| menu bar | open menu | | | | |
| dialog / menu | the obvious | | | | scroll |

* **Selection and copy.** The chat view owns selection, as xtext does:
  it's drawn in the Selection role and copied on release
  (`hex_text_autocopy_text`) to the system clipboard through OSC 52, and
  kept for middle-click paste. Shift+drag hands selection back to the
  terminal for users who want native selection.
* **Word types** come from the core (`url_check_word`: URL, channel,
  nick, host, email), the same classification xtext's clicks use. The
  chat view keeps, per drawn row, the byte span of each word, so a click
  maps to (line, word).
* **Hover:** with a menu open, the item under the pointer is highlighted.
  URLs underline on hover only while 1003 is on (menu open); otherwise a
  URL is drawn underlined all the time, as xtext does.

## 5. Keyboard — the GUI's keys, plus a terminal's

The GUI's defaults (fe-gtk/fkeys.c `default_kb_cfg`) apply verbatim where
a terminal can deliver the key:

* Alt-1…9 / Alt-` (auto) change buffer; Ctrl-PgUp/PgDn previous/next;
  Alt-←/→ previous/next; Alt-A next active.
* PgUp/PgDn scroll; Ctrl-Home/End top/bottom; Shift-PgUp/PgDn change the
  selected nick.
* Formatting into the input: Ctrl-B bold, Ctrl-K colour (opens the
  colour picker), Ctrl-I italic, Ctrl-U underline, Ctrl-R reverse,
  Ctrl-O reset. **Conflict:** readline's Ctrl-K/Ctrl-U (kill line) lose
  to the GUI's formatting meanings, because a GUI user's muscle memory
  for IRC is the reason this exists. Kill-to-end moves to Alt-K,
  kill-to-start to Alt-U (InputLine changes accordingly).
* Tab nick/channel/command completion through the core's own rules
  (`hex_completion_*`); Ctrl-F the find bar (the GUI's search bar: over
  the status row, every match painted, Up/Enter older, Down newer, both
  wrap; Alt-C/R/A toggle the GUI's text_search_* prefs; chat.cpp's
  ChatView::set_find / find_step);
  the GUI's keybindings.conf (keys.c reads it as fkeys.c does; with a
  file its bindings come before the TUI's own keys, with none the TUI's
  keys are the GUI's defaults): Run Command, Change Page (n, Relative,
  auto), Insert in Buffer, Set Buffer, Scroll Page, Last / Next Command,
  Complete, Change Selected Nick, Check For Replace (replace.conf, on
  space and Enter by default), Push input line into history. Moving tabs
  does nothing: the tree keeps the core's order;
  Esc closes whatever is open, then cancels a reply.
* F10 / Alt-F etc. open the menus; Ctrl-G the switcher; Ctrl-P the
  command palette (every menu action as "Menu › Item", every tab, every
  /command, fuzzy, the matched letters lit).
* Typing "/" opens the command popup over the input (the GUI's
  cmdpopup.c, cmds.c over common/cmdinfo.c): commands grouped (Most used
  first) with what to type after them and what they do, the selected one's
  details and example; once the command is in, its usage with the argument
  under the caret stressed and values for it (people by who spoke last,
  channels, settings, networks, options). ↑↓ pick, Tab fills in, Enter
  fills in a partly typed command or sends a whole one, Esc closes it for
  that command, a click fills in. gui_input_cmdhelp turns it off.
* keybindings.conf: the user's own bindings from the GUI's file are
  loaded too (same parser, fe-gtk/fkeys.c's actions), where they map to
  keys a terminal sends.

## 6. Everything else the GUI has

Each is a TUI dialog (one Modal widget: a centred card over the screen,
with its own focus, Esc to close) or a special buffer:

| GUI | TUI |
|---|---|
| Network List (servlistgui.c) | dialog: networks list + edit pane (servers, nick, login, auto-join), connect |
| Channel List (chanlist.c) | special buffer: sortable table, filter, double-click to join |
| Preferences (setup.c) | dialog: the same sections, live-applied like the GUI's; plus **TUI: theme** (a maya scheme name or native, picked with live preview) |
| Ban List, Ignore List, Friends List | dialogs over the core's lists |
| File Transfers (dccgui.c) | dialog: transfers table, accept / abort / open; `fe_confirm` for incoming offers |
| Raw Log | special buffer per server (`fe_add_rawlog`) |
| URL Grabber, Mentions | panels (tables): Open / Copy / Clear / Save As…; Go to / Clear |
| Plugins and Scripts | panel: list, Load… (a path prompt in addons/), Unload, Reload |
| Search (Ctrl-F) | find bar over the chat |
| `fe_get_str/int/bool/file` | prompt dialogs (file: a path entry with completion) |
| `fe_message` | a toast; FE_MSG_ERROR/WAIT as a modal message box |
| tray / notifications | terminal bell + OSC 9/777 notifications where supported; title activity mark |
| Bouncer (bncwin/bncsetup) | Server → Bouncer…: the same window and Stay online setup (bouncerwin.cpp over bridge-bnc.h); detection and probing shared via common/bncprobe.c |

Not in the TUI: the character chart (a terminal has its own input
method), image previews, the spell checker.

## 7. Theming

As built and tested in the kit (theme.hpp): **maya themes are the only
colour source.** `native` is the default; View → Theme stores a maya
scheme name in the TUI's own file (`tui.conf` in the config dir), never
the GUI's colors.conf. A theme switch is `maya::app_set_theme`.

## 8. Build, install, test

* `-Dtui=auto|enabled|disabled` (done) builds the kit and, from this
  plan, `nexchat-tui`; the GTK binary gets `--tui` only when the TUI is
  built.
* Packaging: the .app bundles `nexchat-tui` beside `nexchat`
  (osx/make-app.py), the Windows packages ship `nexchat-tui.exe`, Linux
  installs it to libexecdir plus a `nexchat-tui` symlink in bindir.
* Tests:
  - `tui-widgets` (done): the kit.
  - `tui-ui`: Ui driven by fake events (keys, clicks at coordinates),
    asserting state and rendered rows.
  - `tui-smoke`: nexchat-tui in a pty against tests/fake_ircd.py,
    connect, join, type, click a nick, open a menu, quit (pyte, like
    tui-shot.py).
  - `tui-theme-check.py` (done) extended to the real app.

## 9. Order of work

1. glue + bridge + loop: connect, buffers, chat, input, tree, user list,
   status, topic. **Usable IRC client.**
2. Mouse: hit map, clicks on tree/user list/chat/status, wheel, selection
   + copy, pane drag.
3. Menus: menu bar, context menus from popup_list etc., command palette.
4. Dialogs: prompts/confirm/messages, then Network List, Preferences
   (with the theme picker), Channel List, transfers, lists.
5. `--tui` dispatch in the GTK binary, packaging, smoke tests.
