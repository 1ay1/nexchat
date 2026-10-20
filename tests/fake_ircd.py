#!/usr/bin/env python3
"""Tiny fake IRC server for smoke tests.

Accepts one client, does the registration dance, lets it join any channel,
echoes a canned reply to PRIVMSGs and logs every client line to --log.
Exits when the client sends QUIT or disconnects.
"""
import argparse
import datetime
import socket
import sys

p = argparse.ArgumentParser()
p.add_argument("--port", type=int, default=0)
p.add_argument("--log", required=True)
p.add_argument("--portfile", required=True)
p.add_argument("--demo", action="store_true",
               help="fill joined channels with users and chatter (for screenshots)")
p.add_argument("--showcase", action="store_true",
               help="a believable network for the README screenshot: "
                    "several channels with their own people and conversation")
p.add_argument("--timeout", type=int, default=60)
p.add_argument("--script",
               help="a file of raw lines sent after the first JOIN ({nick} is the client's nick)")
p.add_argument("--script-after",
               help="send --script after this channel's JOIN instead of the first")
p.add_argument("--caps", default="",
               help="capabilities to offer (echo-message makes it echo our PRIVMSGs back)")
a = p.parse_args()

DEMO_USERS = ["~founder", "&admin", "@alice", "@carol", "%helper", "+bob", "+dave",
              "erin", "frank", "grace", "heidi", "ivan", "judy", "mallory", "oscar"]


def demo_chatter(nick, chan):
    c = "\x03"
    b = "\x02"
    return [
        f":alice!a@example.org PRIVMSG {chan} :morning everyone",
        f":bob!b@example.org PRIVMSG {chan} :hey {nick}, did you try the new build?",
        f":carol!c@example.org PRIVMSG {chan} :\x01ACTION waves\x01",
        f":dave!d@example.org PRIVMSG {chan} :{c}04red{c} {c}03green{c} {c}12blue{c} and {b}bold{b} text",
        f":erin!e@example.org PRIVMSG {chan} :see https://hexchat.github.io for docs",
        f":newbie!n@example.org JOIN {chan}",
        f":frank!f@example.org PART {chan} :bye",
        f":alice!a@example.org MODE {chan} +v erin",
        f":grace!g@example.org PRIVMSG {chan} :a much longer line to show how wrapping works in the text box when the message is wider than the window, it should wrap and indent nicely under the nick column",
        f":bob!b@example.org NOTICE {nick} :private notice to you",
        f":heidi!h@example.org PRIVMSG {nick} :hi, this is a query",
    ]


def demo_names(nick):
    return " ".join(["@" + nick] + DEMO_USERS)


# --showcase: each channel has a topic, people and a conversation. Each
# line carries a server-time tag (IRCv3) for its HH:MM, so the timestamps
# read like a real afternoon instead of all being the same minute.
SHOWCASE = {
    "#nexchat": {
        "topic": "HexChat, continued \u2014 now on GTK4 | Be kind, be patient",
        "setter": "maya",
        "users": ["~maya", "@rin", "@tobias", "%kestrel", "+lena", "+dmitri", "ana", "felix",
                  "guest_4821", "jonas", "kai", "marguerite", "noor", "priya", "sam", "theo",
                  "ugo", "wren", "yusuf", "zoe"],
        "lines": [
            ("13:57", ":wren!wren@home.example PRIVMSG #nexchat :morning all"),
            ("13:58", ":kai!kai@tokyo.example PRIVMSG #nexchat :evening from tokyo \U0001f319"),
            ("14:02", ":lena!lena@berlin.example PRIVMSG #nexchat :just built it on my mac, the global menu bar is a nice touch"),
            ("14:02", ":tobias!tobias@tobias.dev PRIVMSG #nexchat :Cmd-, opens preferences too \u2014 feels native"),
            ("14:03", ":dmitri!dmitri@spb.example PRIVMSG #nexchat :does it still read my old \x02colors.conf\x02?"),
            ("14:03", ":rin!rin@staff.nexchat PRIVMSG #nexchat :dmitri: yes, HexChat configs are copied over on first start, and the palette follows dark mode unless you picked your own"),
            ("14:04", ":ana!ana@lisbon.example JOIN #nexchat"),
            ("14:04", ":kestrel!kestrel@moderator PRIVMSG #nexchat :welcome ana \x0303o/\x03"),
            ("14:05", ":ana!ana@lisbon.example PRIVMSG #nexchat :hi! is fishlim still bundled?"),
            ("14:05", ":maya!maya@staff.nexchat PRIVMSG #nexchat :\x01ACTION nods \u2014 FiSHLiM, Sysinfo and Checksum ship in the app bundle\x01"),
            ("14:06", ":felix!felix@cafe.example PRIVMSG #nexchat :zooming with ctrl+scroll keeps the nick column straight now \U0001f44c"),
            ("14:07", ":theo!theo@mobile.example QUIT :Ping timeout: 240 seconds"),
            ("14:07", ":lena!lena@berlin.example PRIVMSG #nexchat :startup is quick too, and \x0310NEXCHAT_TIMING=1\x03 shows where the time goes"),
            ("14:08", ":tobias!tobias@tobias.dev PRIVMSG #nexchat :\x0304\x02heads up:\x02\x03 please test the GTK4 build and file issues on github"),
            ("14:08", ":noor!noor@uni.example PRIVMSG #nexchat :scripts too? my old perl stuff uses HexChat:: everywhere"),
            ("14:09", ":rin!rin@staff.nexchat PRIVMSG #nexchat :noor: both HexChat:: and Nexchat:: work, same for python and lua"),
            ("14:09", ":rin!rin@staff.nexchat MODE #nexchat +v ana"),
            ("14:10", ":marguerite!m@paris.example PRIVMSG #nexchat :the tree on the left + user list on the right, exactly how I like it"),
            ("14:10", ":jonas!jonas@oslo.example PRIVMSG #nexchat :\x0312\x1fhttps://github.com/1ay1/nexchat/issues\x1f\x03 is where bugs go, right?"),
            ("14:11", ":kestrel!kestrel@moderator PRIVMSG #nexchat :jonas: yep"),
            ("14:11", ":priya!priya@sf.example PRIVMSG #nexchat :{nick}: how does it follow dark mode, polling?"),
        ],
    },
    "#gtk": {
        "topic": "GTK development | https://docs.gtk.org | No support for GTK 2",
        "setter": "ebassi",
        "users": ["@ebassi", "@mclasen", "+otte", "alice", "bilal", "cora", "gwen"],
        "lines": [
            ("13:51", ":otte!otte@gnome PRIVMSG #gtk :the new renderer handles that path fine"),
            ("13:52", ":bilal!bilal@gnome PRIVMSG #gtk :anyone seen popovers misplaced on multi-monitor macOS?"),
        ],
    },
    "#linux": {
        "topic": "Linux support | Ask, don't ask to ask | No distro wars",
        "setter": "op",
        "users": ["@op", "+helper", "user1", "user2", "user3"],
        "lines": [
            ("13:40", ":helper!h@help PRIVMSG #linux :try journalctl -b -1 to see the previous boot"),
        ],
    },
}
SHOWCASE_QUERY = [
    ("13:58", ":rin!rin@staff.nexchat PRIVMSG {nick} :hey, can you look at the release checklist when you get a sec?"),
    ("13:59", ":rin!rin@staff.nexchat PRIVMSG {nick} :no rush"),
]


# the connection-time lines a client prints itself ("Now talking on",
# the topic) carry no tag; stamp them at the start of the scene
SHOWCASE_JOIN_TIME = "13:56"
# 333's topic time: a fixed date, so the README screenshot doesn't carry
# the day it was taken
SHOWCASE_TOPIC_SET = 1767225600   # 2026-01-01


def showcase_join(chan):
    """the channel a showcase JOIN gets: (topic, setter, names, lines)"""
    c = SHOWCASE.get(chan.lower())
    if not c:
        return (f"Welcome to {chan}", "op", [], [])
    return (c["topic"], c["setter"], c["users"], c["lines"])


def tagged(when, line):
    """a server-time tag (IRCv3) for HH:MM today, so the client stamps
    the line with that time instead of now"""
    h, m = map(int, when.split(":"))
    t = datetime.datetime.now(datetime.timezone.utc).astimezone()
    t = t.replace(hour=h, minute=m, second=0, microsecond=0).astimezone(datetime.timezone.utc)
    return "@time=" + t.strftime("%Y-%m-%dT%H:%M:%S.000Z") + " " + line

srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", a.port))
srv.listen(1)
srv.settimeout(a.timeout)
with open(a.portfile, "w") as f:
    f.write(str(srv.getsockname()[1]))

conn, _ = srv.accept()
conn.settimeout(a.timeout)
log = open(a.log, "w", buffering=1)
nick = "*"
echo = False
buf = b""


def send(line):
    # bytes go out as they are (tests send broken UTF-8 on purpose)
    if isinstance(line, bytes):
        conn.sendall(line + b"\r\n")
    else:
        conn.sendall((line + "\r\n").encode())


try:
    while True:
        data = conn.recv(4096)
        if not data:
            break
        buf += data
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = raw.decode(errors="replace").rstrip("\r")
            log.write(line + "\n")
            parts = line.split(" ")
            cmd = parts[0].upper()
            if cmd == "CAP" and len(parts) > 1 and parts[1] == "LS":
                caps = (a.caps + " server-time").strip() if a.showcase else a.caps
                send(f":fake CAP * LS :{caps}")
            elif cmd == "CAP" and len(parts) > 2 and parts[1] == "REQ":
                send(f":fake CAP * ACK {' '.join(parts[2:])}")
                if "echo-message" in line:
                    echo = True
            elif cmd == "PRIVMSG" and echo:
                send(f":{nick}!u@h {line}")
            elif cmd == "CHATHISTORY" and len(parts) >= 3:
                # three old lines in a batch, the middle one naming us
                target = parts[2]
                send(f":fake BATCH +h1 chathistory {target}")
                peer = "old1" if target.startswith("#") else target
                for t, who, msg in [("2026-01-01T10:00:00.000Z", peer, "history-line-one"),
                                    ("2026-01-01T10:01:00.000Z", "old2" if target.startswith("#") else peer, f"history mention {nick}"),
                                    ("2026-01-01T10:02:00.000Z", "old3" if target.startswith("#") else peer, "history-line-three")]:
                    send(f"@batch=h1;time={t} :{who}!u@h PRIVMSG {target if target.startswith('#') else nick} :{msg}")
                send(":fake BATCH -h1")
            elif cmd == "NICK":
                nick = parts[1]
            elif cmd == "USER":
                send(f":fake 001 {nick} :Welcome to the fake network {nick}")
                net = "Libera.Chat" if a.showcase else "Fake"
                send(f":fake 005 {nick} CHANTYPES=# PREFIX=(qaohv)~&@%+ CHANMODES=b,k,l,imnpst NETWORK={net} :are supported")
                send(f":fake 376 {nick} :End of MOTD")
                if a.showcase:
                    for when, l in SHOWCASE_QUERY:
                        send(tagged(when, l.format(nick=nick)))
            elif cmd == "PING":
                send(f":fake PONG fake {parts[1] if len(parts) > 1 else ''}")
            elif cmd == "AWAY":
                if len(parts) > 1 and parts[1].lstrip(":"):
                    send(f":fake 306 {nick} :You have been marked as being away")
                else:
                    send(f":fake 305 {nick} :You are no longer marked as being away")
            elif cmd == "JOIN":
                for chan in parts[1].split(","):
                    if a.showcase:
                        topic, setter, users, lines = showcase_join(chan)
                        at = SHOWCASE_JOIN_TIME
                        set_at = SHOWCASE_TOPIC_SET
                        send(tagged(at, f":{nick}!u@h JOIN {chan}"))
                        send(tagged(at, f":fake 332 {nick} {chan} :{topic}"))
                        send(tagged(at, f":fake 333 {nick} {chan} {setter} {set_at}"))
                        send(f":fake 353 {nick} = {chan} :{' '.join(['@' + nick] + users)}")
                        send(f":fake 366 {nick} {chan} :End of NAMES")
                        for when, l in lines:
                            send(tagged(when, l.format(nick=nick)))
                        continue
                    send(f":{nick}!u@h JOIN {chan}")
                    if a.demo:
                        send(f":fake 332 {nick} {chan} :Welcome to {chan} | be nice | https://example.org")
                        send(f":fake 333 {nick} {chan} alice 1700000000")
                        send(f":fake 353 {nick} = {chan} :{demo_names(nick)}")
                    else:
                        send(f":fake 332 {nick} {chan} :fake topic")
                        send(f":fake 353 {nick} = {chan} :@{nick} bob")
                    send(f":fake 366 {nick} {chan} :End of NAMES")
                    if a.script and (not a.script_after or a.script_after == chan):
                        with open(a.script, "rb") as f:
                            for l in f.read().splitlines():
                                # {nick} filled in; everything else byte for byte
                                send(l.replace(b"{nick}", nick.encode()))
                        a.script = None
                    if a.demo:
                        for l in demo_chatter(nick, chan):
                            send(l)
            elif cmd == "MODE" and len(parts) == 2 and parts[1].startswith("#"):
                send(f":fake 324 {nick} {parts[1]} +nt")
            elif cmd == "MODE" and len(parts) == 3 and parts[2] in ("+b", "b"):
                if a.demo:
                    send(f":fake 367 {nick} {parts[1]} *!*@spam.example alice 1700000000")
                    send(f":fake 367 {nick} {parts[1]} troll!*@* bob 1710000000")
                send(f":fake 368 {nick} {parts[1]} :End of Channel Ban List")
            elif cmd == "LIST":
                send(f":fake 321 {nick} Channel :Users  Name")
                if a.demo:
                    for chan, users, topic in [("#nexchat", 16, "Welcome to #nexchat | be nice"),
                                               ("#help", 42, "Ask your question, then wait"),
                                               ("#linux", 1203, "Linux support | no distro wars"),
                                               ("#offtopic", 7, "")]:
                        send(f":fake 322 {nick} {chan} {users} :{topic}")
                send(f":fake 323 {nick} :End of /LIST")
            elif cmd == "PRIVMSG" and not a.demo:
                target = parts[1]
                send(f":bob!b@h PRIVMSG {target} :pong-reply")
            elif cmd == "QUIT":
                send(":fake ERROR :Closing link")
                raise SystemExit(0)
except (socket.timeout, ConnectionError):
    pass
finally:
    conn.close()
    log.close()
