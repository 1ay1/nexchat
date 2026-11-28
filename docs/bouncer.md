# Staying online: the bouncer manual

IRC only delivers messages to people who are connected. Close nexchat (or
let your laptop sleep) and you leave your channels, and whatever is said
while you're gone is lost to you.

To stay in your channels while nexchat is closed, **something else has to
stay connected for you**. nexchat can use three such things. Pick one:

| You have... | Use | Section |
|---|---|---|
| Nothing yet, and you're on Linux, BSD or macOS | **your own soju**, run by nexchat on this computer | [1](#1-your-own-bouncer-the-easy-way) |
| A bouncer somewhere else (a server, a friend's, a paid service) | **your bouncer**, soju or ZNC | [2](#2-a-bouncer-you-already-have) |
| An account on a network running Ergo | **the network itself** keeps you on | [3](#3-a-network-that-keeps-you-online-ergo) |

All three end up the same: the title bar says **(online 24/7)**, and when you
come back, what you missed is shown between
`--- While you were away ---` and `--- N messages you missed, up to here ---`.

## Where everything is

There is one window for all of it, and one command:

- **Server > Bouncer...** in the main window, or the **Bouncer...** button in
  the Network List. The terminal client (`nexchat --tui`) has the same
  window under **Server > Bouncer…** and the same button in its Network List.
- **`/bouncer`** in any tab does the same things by typing. `/bouncer` alone
  says what the current tab is connected through.

Adding a bouncer elsewhere, or a network that keeps you on, goes through a
short step-by-step window called **Stay online**, opened from the Bouncer
window. Everything after that is in the Bouncer window itself.

---

## 1. Your own bouncer (the easy way)

nexchat can run [soju](https://soju.im) for you on this computer. You
install soju; nexchat does the rest: it sets up a private copy, keeps it
running and moves your networks onto it, with your nicks, logins and
channels. There's nothing to type.

**What it needs:** soju installed from your system's packages (the package
is called `soju`, e.g. `pacman -S soju`, `apt install soju`,
`brew install soju`). Not available on Windows: soju doesn't run there; use
[a bouncer elsewhere](#2-a-bouncer-you-already-have) instead.

### Set it up

1. Install soju.
2. Open **Server > Bouncer...**. It says **soju is installed**, not set up
   yet. Press **Set it up**. (The Stay online window offers the same as
   **Use soju on this computer**.)
3. Switch on the networks it should keep you on (the switch at the end of
   each row). Your favourite networks, the ones that auto-connect and the
   ones with channels are listed; to add another, open the Bouncer window
   from that network's tab.
4. Connect as you always do. Nothing else changes: same Network List
   entries, same tabs, same channels.

Or, in any tab:

```
/bouncer setup                 every network you use: favourites, auto-connect, or with channels
/bouncer setup Libera.Chat     just this one (more names: more networks)
```

### What it changed

- Each network you switched on now connects to `127.0.0.1` (your soju)
  instead of the network, logging in as `me/<network>`. soju connects to the
  real network, with your nick and NickServ/SASL login, and stays there.
- The network's original address and login are kept, so it can be put back
  exactly as it was.
- Your soju lives in its own folder: `~/.config/nexchat/bouncer/` (config,
  database `soju.db`, its log). It doesn't touch a system-wide soju, and
  nothing outside the folder (except the systemd unit below).
- Only this computer can reach it, unless you turn on
  [other devices](#use-it-from-your-phone-or-another-computer).

### Keeping it running

- **With systemd** (most Linux): it runs as a user service,
  `nexchat-bouncer.service` in `~/.config/systemd/user/`. It starts with
  the computer and keeps running when nexchat is closed. The window says
  *kept running by systemd*.
- **Without systemd** (macOS, BSD, some Linux): nexchat starts it when you
  open nexchat, and it keeps running after you close nexchat, until the
  computer restarts. The window says *started by nexchat*.

Change it in the **Access** tab: "Start it with the computer (systemd keeps
it running)". **Stop** and **Start** at the top of the window do what they
say; while stopped, the networks on it are offline.

> Your bouncer only keeps you online while **this computer is on**. For
> online-while-the-laptop-is-shut, it has to run on a computer that's always
> on: a home server or a VPS (see [section 2](#2-a-bouncer-you-already-have)).

### The Bouncer window, tab by tab

**Networks.** One row per network in your Network List:

- the switch: **on** means it goes through your bouncer and stays online;
  **off** means it connects directly, as before.
- its state: `in #a, #b` (online, in those channels), `joining...`,
  `can't connect: <why>`, `switched off`, `bouncer stopped`, or
  `connects directly`.
- **Nick and login...** — what the bouncer uses on that network: your nick
  (the same as everywhere, or its own), logging in as a guest or with your
  account (SASL; this is your NickServ account on the *network*, not the
  bouncer's), real name, user name, a server password, "Show me as away when
  no client is connected", "Stay connected" (off: kept, but offline),
  commands to send on connect, and a certificate login (**Make a
  certificate**, then add its fingerprint with `/msg NickServ CERT ADD`).

**Channels.** Pick a network: the channels the bouncer stays in, whether
nexchat is open or not. **Join** adds one. **Detached** keeps it in the
channel without showing you the tab until you `/join` it again (good for
busy channels you only want history from). **Leave** parts it for good.

**Access.**

- *Connecting to it*: the address (`127.0.0.1`, its port, no TLS), the user
  name (`me/<network>`, e.g. `me/Libera`) and the password. nexchat fills
  these in for itself; you only need them for another app. **Show**,
  **Change** (8 characters or more).
- *Other devices*: "Let my phone and other computers connect to it". See
  below.
- *Keeping it running*: systemd or nexchat, as above.

**Storage.**

- *History*: in the bouncer's database (the default; history search and
  missed messages work) or as plain text files, one per channel and day.
- *Its name*: shown to apps that connect to it.
- *Its log*: soju's own log, for when something's wrong. **Reload** refreshes
  it. `/bouncer log` prints the last lines in the chat.

### Use it from your phone or another computer

Tick **Access > Let my phone and other computers connect to it**. Your soju
then also listens on **port 6697, with TLS**, on every address of this
computer. In the other app:

- server: this computer's address, port `6697`, TLS on
- user name: `me/<network>` (e.g. `me/Libera`), or just `me` if the app
  understands soju's network list (Goguma, gamja, senpai, nexchat)
- password: the one in **Access > Connecting to it**

The certificate is self-signed; the app will ask whether to trust it.
Compare the fingerprint it shows with the one in the Access tab first. You
may need to open port 6697 in your firewall or router.

### Undo it

- **One network:** switch it off in the Networks tab. It connects directly
  again (next time you connect), with its old address and login.
- **All of it:** **Remove...** at the top of the window (press it twice: the
  second press says "Remove, sure?"), or `/bouncer remove`. soju stops, the
  systemd unit is removed, and every network connects directly again, as it
  did before. Your soju's folder (and its history) is left in
  `~/.config/nexchat/bouncer/`; delete it if you don't want it.

---

## 2. A bouncer you already have

A soju or ZNC on a server, run by you, a friend or a service.

**What you need:** its address, and your user name and password **on the
bouncer**.

### Add it

1. **Server > Bouncer...**, then **Use another bouncer...** (or
   **Add another bouncer...** if you already have one).
2. Choose **I have a bouncer**.
3. *Your bouncer*: fill in **Address** (`bnc.example.org`,
   `bnc.example.org:6697` or `irc://bnc.example.org:6667`), **User name** and
   **Password**. It's checked as you type, and the message under the fields
   tells you what it found:
   - *That's soju, and the password is right.* (or ZNC) — good, press **Next**.
   - a wrong password, or the address not answering.
   - *That's a plain IRC server, not a bouncer.* — check the address and
     port; you've probably typed the network's address, not the bouncer's.
   - *That's an IRC network, not a bouncer. But it can keep you online...* —
     it's an Ergo network: go back and use
     [section 3](#3-a-network-that-keeps-you-online-ergo) instead.
   - a certificate it can't check (common for self-hosted bouncers): tick
     **It's mine, trust this certificate** if it is.

   The address uses TLS unless you write `irc://` in front or use port 6667.
4. *Which networks should it stay on?* Tick the popular networks you want,
   or fill in **Another one** (an address) and **Its name**. Networks the
   bouncer already has are kept as they are.
5. **Connect.**

Already running a bouncer on this computer that isn't nexchat's own (a ZNC,
a system soju)? The first page of the setup offers it with the address
filled in.

### What it changed

- A new Network List entry, **My bouncer (bnc.example.org)**, logs in to the
  bouncer. Your existing network entries are left as they are.
- Each network the bouncer is on gets its own entry, named
  **`<bouncer entry>/<network>`**, e.g. `My bouncer (bnc.example.org)/Libera`.
  These are made and kept up to date by nexchat: add a network on the bouncer
  and its entry appears; remove it there and the entry goes. Connect to them
  like any other network. (Turn this off with `/set irc_bouncer_sync off`.)
- Use those entries, not your old direct ones, for the networks on the
  bouncer. When a network first shows up on the bouncer, an old entry that
  connects to it directly at startup stops doing so (the server tab says
  which), so you aren't on it twice.

### In the Bouncer window

Each bouncer you use is a card: **Connected (soju)**, **Connecting...**,
**Couldn't log in**, or **Not connected**, with:

- **Connect** / **Disconnect**
- **Edit...** — its address, user name and password (the setup window again)
- **Forget** (press twice) — takes it out of nexchat. Nothing changes on the
  bouncer itself.
- its networks, each *online*, *connecting* or *offline*, with **Open** (its
  tab), **Nick and login...** and **Remove** (the bouncer leaves that
  network)
- **Add a network...** — a name and an address; the bouncer connects to it
  and its tab opens.

The Channels, Access and Storage tabs are only for [your own
bouncer](#1-your-own-bouncer-the-easy-way); a bouncer elsewhere is managed
on the bouncer.

### By typing

In a tab connected through the bouncer:

```
/bouncer                                   what you're connected through, and its networks
/bouncer add Libera irc.libera.chat        have the bouncer join a network (TLS, port 6697)
/bouncer add work irc.example.org:6667     port 6667: no TLS
/bouncer add lan 10.0.0.5:6697 -notls mynick
/bouncer del Libera                        have it leave a network
```

soju gets everything (adding, removing, listing). ZNC is driven through its
`*controlpanel` module, which has to be loaded on the ZNC (`/msg *status
loadmod controlpanel`); its replies are kept out of the chat.

---

## 3. A network that keeps you online (Ergo)

Some networks, the ones running [Ergo](https://ergo.chat), can keep you
connected themselves: no bouncer at all, just a registered account.

1. **Server > Bouncer...**, **Use another bouncer...**, then **Keep me online
   on my network**.
2. Pick the **Network** from your Network List, and give **Your account** and
   its **Password** (your NickServ account). It checks whether that network
   can do it.
   No account yet? Connect and `/msg NickServ REGISTER <password> <email>`.
3. **Connect.** The entry is set to log in with your account (SASL), and
   once you're in nexchat asks the network to keep you on.

By typing, in that network's tab: `/bouncer on`, `/bouncer off`, and
`/bouncer` to check ("You stay online on this network even when nexchat is
closed.").

---

## What you get back

These work with any of the three, as far as the server supports them:

- **Missed messages.** On joining, nexchat asks for what was said since the
  last line you saw, and shows it between `--- While you were away ---` and
  `--- N messages you missed, up to here ---`. Scroll up past the start to
  load older history. Turn it off in **Preferences > Chatting > Advanced >
  "Get missed messages from the server on join"** (`/set irc_chathistory`).
  ZNC sends its buffer on connect instead (the *playback* module makes it
  send only what's new).
- **Search everything.** `/lastlog -s word` searches the bouncer's history
  of this channel, including while nexchat was closed; `/lastlog -s -a word`
  every channel (soju).
- **Read markers** are shared: read a channel on your phone and it isn't
  unread on your computer.
- **Status in the chat.** On soju, the server tab says when a network drops
  ("soju lost Libera: ... It keeps trying") and when it's back.

## Troubleshooting

- **"This is a plain IRC server"** from `/bouncer`: this tab connects
  straight to the network. Set something up as above.
- **The title doesn't say (online 24/7):** that tab isn't going through a
  bouncer. With a bouncer elsewhere, check you connected with the
  `.../<network>` entry, not the old direct one.
- **I'm in a channel twice / my nick has a _ on it:** you're connected
  directly *and* through the bouncer. Disconnect the direct entry (or turn
  off its auto-connect).
- **My own bouncer says "can't connect":** open **Storage > Its log**, or
  `/bouncer log`. On systemd: `systemctl --user status nexchat-bouncer`.
- **"The system's soju service is set to start but fails":** that's a
  separate, system-wide soju (needs root to fix). nexchat's own copy doesn't
  use it and doesn't need it.
- **Couldn't log in to a bouncer elsewhere:** the user name and password are
  the bouncer's, not the network's. For the bouncer's own entry give just
  the user name (`alice`, not `alice/libera`); nexchat adds the `/network`
  part for each network's entry itself.
- **ZNC's networks don't show up:** nexchat lists them through ZNC's
  `*status` and changes them through `*controlpanel`; load that module on
  the ZNC (`/msg *status loadmod controlpanel`).

## Reference

```
/bouncer                     what this tab goes through, and its networks (same as /bouncer list)
/bouncer setup [network...]  run your own soju and move networks onto it
/bouncer remove              stop your own soju; every network connects directly again (no name after it)
/bouncer add <name> <host>[:port] [-notls] [nick]
/bouncer del <name>          the bouncer leaves that network
/bouncer on | off            Ergo: stay online on this network, or not
/bouncer log                 the last lines of your own soju's log
/bouncer set                 your own soju's settings, listed
/bouncer set remote on|off             reachable from other devices
/bouncer set logs files|db             where history is kept
/bouncer set title <text>              its name
/bouncer set password <pw>             the password for other apps
/bouncer set systemd on|off            kept by systemd, or by nexchat
/bouncer set net <network> <key> <value>         realname, username, pass, auto-away, enabled
/bouncer set commands <network> <line>|<line>    raw IRC sent on connect
/bouncer set channel <network> <#chan> add|del|detached on|off
```

Settings (`/set`):

- `irc_bouncer_sync` (on) — list a bouncer's networks in the Network List as
  `<bouncer>/<network>` entries and keep them in step.
- `irc_chathistory` (on) — get missed messages on join.
