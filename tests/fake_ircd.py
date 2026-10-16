#!/usr/bin/env python3
"""Tiny fake IRC server for smoke tests.

Accepts one client, does the registration dance, lets it join any channel,
echoes a canned reply to PRIVMSGs and logs every client line to --log.
Exits when the client sends QUIT or disconnects.
"""
import argparse
import socket
import sys

p = argparse.ArgumentParser()
p.add_argument("--port", type=int, default=0)
p.add_argument("--log", required=True)
p.add_argument("--portfile", required=True)
p.add_argument("--demo", action="store_true",
               help="fill joined channels with users and chatter (for screenshots)")
p.add_argument("--timeout", type=int, default=60)
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
buf = b""


def send(line):
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
                send(":fake CAP * LS :")
            elif cmd == "NICK":
                nick = parts[1]
            elif cmd == "USER":
                send(f":fake 001 {nick} :Welcome to the fake network {nick}")
                send(f":fake 005 {nick} CHANTYPES=# PREFIX=(qaohv)~&@%+ CHANMODES=b,k,l,imnpst NETWORK=Fake :are supported")
                send(f":fake 376 {nick} :End of MOTD")
            elif cmd == "PING":
                send(f":fake PONG fake {parts[1] if len(parts) > 1 else ''}")
            elif cmd == "JOIN":
                for chan in parts[1].split(","):
                    send(f":{nick}!u@h JOIN {chan}")
                    if a.demo:
                        send(f":fake 332 {nick} {chan} :Welcome to {chan} | be nice | https://example.org")
                        send(f":fake 333 {nick} {chan} alice 1700000000")
                        send(f":fake 353 {nick} = {chan} :{demo_names(nick)}")
                    else:
                        send(f":fake 332 {nick} {chan} :fake topic")
                        send(f":fake 353 {nick} = {chan} :@{nick} bob")
                    send(f":fake 366 {nick} {chan} :End of NAMES")
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
