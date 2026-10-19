# Coming from HexChat

nexchat is HexChat 2.16.2 moved to GTK4. Menus, dialogs, keys, text events
and config files are the same. This page is what you need to know when you
switch.

## Your settings

The first time nexchat starts, it copies your HexChat folder to its own:

| | HexChat | nexchat |
|---|---|---|
| Linux, BSD | `~/.config/hexchat` | `~/.config/nexchat` |
| Windows | `%APPDATA%\HexChat` | `%APPDATA%\nexchat` |
| macOS | `~/.config/hexchat` | `~/.config/nexchat` |

Everything comes along: networks, settings, colors, scripts in `addons`,
logs, scrollback, sounds. It's a copy, so HexChat keeps working and
the two don't affect each other. Symlinks in the folder are skipped.

The copy only happens when the nexchat folder doesn't exist yet. To copy
again, delete (or rename) `~/.config/nexchat` and start nexchat. Starting
with `-d <folder>` uses that folder and copies nothing.

## Plugins and scripts

The plugin API is HexChat's, unchanged. Compiled plugins built for HexChat
2.16 load as they are; put them in the `addons` folder.

- Python: `import hexchat` keeps working, and `import nexchat` is the same module.
- Perl: `HexChat::` and `Xchat::` keep working; `Nexchat::` is the same package.
- Lua: the `hexchat` table is there, and `nexchat` is the same table.
- C: include `hexchat-plugin.h` as before, or `nexchat-plugin.h`.
  `pkg-config nexchat-plugin` gives the flags; `hexchat-plugin` works too
  unless your package was built with `-Dinstall-hexchat-compat=false` (the
  Arch package is, so it can sit next to HexChat).

nexchat looks for its bundled plugins in `NEXCHAT_LIBDIR` (or
`HEXCHAT_LIBDIR`) when set.

## What's different

These come from GTK4 or the platform and couldn't be kept exactly:

- **Scrollbar arrows**: GTK4 scrollbars have none, so the chat view's has none.
- **Window position on Wayland**: the compositor places windows. Size is still
  remembered; position is remembered on X11, Windows and macOS.
- **Taskbar flashing on Wayland**: it asks the compositor to mark the window
  urgent. Whether that shows is up to the desktop.
- **Menus** are GTK4 popovers, styled to look like the old menus (square, 1px
  border, check boxes, mnemonics).
- **Font and color pickers** are GTK4's: a searchable font list with a preview,
  and a color editor with a hex field. They save the same values as before.
- **Striped rows** in lists like DCC or Ignore are up to the theme, and the
  default theme doesn't stripe.

## What's new

All of these are in Preferences. The ones that change how you look to others
are off until you turn them on.

- **Light and dark**: colors follow the system's light/dark mode. Colors you
  picked yourself stay as they are.
- **Color schemes** (Preferences > Colors): around 800, including Native (your
  system's text colors) and light/dark pairs like Solarized, Gruvbox and
  Catppuccin.
- **Cursor style**: line, thick bar, block, underline or hollow box, blinking
  or not.
- **Command list**: typing `/` shows the commands with what they do and an
  example.
- **Log search**: `/lastlog -l word` searches every saved log, and with a
  soju bouncer `/lastlog -s word` searches its history, including what was
  said while nexchat was closed.
- **Stay online** (Network List > Stay online...): stay in your channels while
  nexchat is closed. Point it at your soju or ZNC, or at a network that can
  keep you connected itself (Ergo), and it checks the login and sets the
  network up for you. The bouncer's networks show up in your Network List,
  `/bouncer` adds and removes them, and what you missed is shown between
  "While you were away" and "N messages you missed". The title says
  "(online 24/7)" when it's working.
- **Bouncer features** (soju, Ergo, ZNC): chat history on join and in
  private chats, read markers shared between your devices, netsplits shown
  as one line.
- **Deleted messages**: on servers that allow it, `/delete` takes back your
  last message, and when someone deletes one you see what it was.
- **Off until you turn them on**: who's typing, letting others see you type,
  showing what replies answer (`/reply`), and messages of several lines
  (Shift+Enter).
- **A paste of more than 5 lines asks first**, so a slip of the mouse can't
  flood a channel. Set the number, or 0 to turn it off, in Preferences > Input box.

## Running both

nexchat and HexChat can be installed and running at the same time. They use
different folders, different program names and a different D-Bus name
(`io.github.nexchat.service`), so `nexchat --existing` never talks to a
running HexChat.
