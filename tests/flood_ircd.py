#!/usr/bin/env python3
"""IRC server that floods one client with fast-changing traffic.

For tests/stress.sh. Registers the client, puts it in several channels, then
for --seconds sends a random mix as fast as the socket takes it: messages
(long ones, mIRC colours, URLs, highlights, actions), joins and parts of a
pool of users, nick changes, quits, op/voice/ban modes, topic changes,
kicks, queries, notices, new queries opening, and channels the client is
forced into and out of (SAJOIN-ish JOIN/PART/KICK of the client itself).
Same seed, same traffic. Writes the line count sent to --log at the end.
"""
import argparse
import random
import socket
import time

p = argparse.ArgumentParser()
p.add_argument("--portfile", required=True)
p.add_argument("--log", required=True)
p.add_argument("--seconds", type=float, default=20)
p.add_argument("--rate", type=int, default=2000, help="lines per second, roughly")
p.add_argument("--seed", type=int, default=1)
p.add_argument("--timeout", type=int, default=60)
a = p.parse_args()
rnd = random.Random(a.seed)

srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", 0))
srv.listen(1)
srv.settimeout(a.timeout)
with open(a.portfile, "w") as f:
    f.write(str(srv.getsockname()[1]))
conn, _ = srv.accept()
conn.settimeout(a.timeout)

nick = "*"
buf = b""
sent = 0


def send(line):
    global sent
    conn.sendall((line + "\r\n").encode("utf-8", "replace"))
    sent += 1


def read_until_user():
    """registration: answer CAP, wait for NICK + USER"""
    global nick, buf
    got_user = False
    while not got_user:
        data = conn.recv(4096)
        if not data:
            raise SystemExit(1)
        buf += data
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            parts = raw.decode(errors="replace").rstrip("\r").split(" ")
            cmd = parts[0].upper()
            if cmd == "CAP" and len(parts) > 1 and parts[1] == "LS":
                send(":fake CAP * LS :")
            elif cmd == "NICK":
                nick = parts[1]
            elif cmd == "USER":
                got_user = True


read_until_user()
send(f":fake 001 {nick} :Welcome {nick}")
send(f":fake 005 {nick} CHANTYPES=# PREFIX=(qaohv)~&@%+ CHANMODES=b,k,l,imnpst NETWORK=Flood :are supported")
send(f":fake 376 {nick} :End of MOTD")

CHANS = ["#flood", "#busy", "#chaos", "#zzz"]
users = {f"u{i}" for i in range(200)}
members = {c: set() for c in CHANS}
in_chan = set()


def join_self(chan):
    in_chan.add(chan)
    members[chan] = set(rnd.sample(sorted(users), 120))
    send(f":{nick}!me@h JOIN {chan}")
    send(f":fake 332 {nick} {chan} :topic of {chan}")
    names = " ".join(rnd.choice(["", "", "", "+", "@", "%"]) + u for u in members[chan])
    # NAMES in chunks like real servers
    lst = names.split(" ")
    for i in range(0, len(lst), 40):
        send(f":fake 353 {nick} = {chan} :{' '.join(lst[i:i + 40])}")
    send(f":fake 366 {nick} {chan} :End of NAMES")


for c in CHANS:
    join_self(c)

WORDS = ("lorem ipsum dolor sit amet consectetur adipiscing elit sed do eiusmod "
         "tempor incididunt ut labore et dolore magna aliqua").split()


def text():
    k = rnd.random()
    if k < 0.05:
        return " ".join(rnd.choice(WORDS) for _ in range(rnd.randint(60, 120)))  # long, wraps
    if k < 0.15:
        return f"\x03{rnd.randint(0, 15)},{rnd.randint(0, 15)}colour\x0f \x02bold\x02 \x1ditalic\x1d \x1funder\x1f"
    if k < 0.22:
        return f"look https://example.org/{rnd.randint(0, 99999)} and #busy and {nick}"
    if k < 0.26:
        return f"{nick}: ping"  # highlight
    if k < 0.28:
        return "\x01ACTION dances\x01"
    if k < 0.29:
        return "ünïcödé 日本語 🎉 " * rnd.randint(1, 5)
    return " ".join(rnd.choice(WORDS) for _ in range(rnd.randint(1, 15)))


def pick_member(chan):
    return rnd.choice(sorted(members[chan])) if members[chan] else None


def event():
    global users
    chans = sorted(in_chan)
    k = rnd.random()
    if not chans:
        join_self(rnd.choice(CHANS))
        return
    chan = rnd.choice(chans)
    u = pick_member(chan)
    if k < 0.55 and u:
        send(f":{u}!x@h PRIVMSG {chan} :{text()}")
    elif k < 0.62:
        new = f"u{rnd.randint(0, 400)}"
        users.add(new)
        if new not in members[chan]:
            members[chan].add(new)
            send(f":{new}!x@h JOIN {chan}")
    elif k < 0.68 and u:
        members[chan].discard(u)
        send(f":{u}!x@h PART {chan} :{text()}")
    elif k < 0.72 and u:
        new = f"n{rnd.randint(0, 100000)}"
        for c in members:
            if u in members[c]:
                members[c].discard(u)
                members[c].add(new)
        send(f":{u}!x@h NICK :{new}")
    elif k < 0.75 and u:
        for c in members:
            members[c].discard(u)
        send(f":{u}!x@h QUIT :{text()}")
    elif k < 0.82 and u:
        m = rnd.choice(["+o", "-o", "+v", "-v", "+h", "-h"])
        send(f":op!x@h MODE {chan} {m} {u}")
    elif k < 0.84:
        send(f":op!x@h MODE {chan} {rnd.choice(['+b', '-b'])} *!*@spam{rnd.randint(0, 50)}")
    elif k < 0.86:
        send(f":op!x@h TOPIC {chan} :{text()}")
    elif k < 0.88 and u:
        members[chan].discard(u)
        send(f":op!x@h KICK {chan} {u} :{text()}")
    elif k < 0.92:
        q = f"q{rnd.randint(0, 6)}"
        send(f":{q}!x@h PRIVMSG {nick} :{text()}")
    elif k < 0.95:
        send(f":srv!x@h NOTICE {nick} :{text()}")
    elif k < 0.97 and len(chans) > 1:
        # the client gets kicked from / parted out of a channel, then rejoined later
        if rnd.random() < 0.5:
            send(f":op!x@h KICK {chan} {nick} :stress")
        else:
            send(f":{nick}!me@h PART {chan}")
        in_chan.discard(chan)
    else:
        rest = [c for c in CHANS if c not in in_chan]
        if rest:
            join_self(rnd.choice(rest))


# drain whatever the client sends (PING replies, JOINs, WHO...) without
# blocking, and answer PINGs so it never times out
conn.setblocking(False)


def drain():
    global buf
    try:
        while True:
            data = conn.recv(65536)
            if not data:
                raise SystemExit(0)
            buf += data
    except BlockingIOError:
        pass
    while b"\n" in buf:
        raw, buf = buf.split(b"\n", 1)
        line = raw.decode(errors="replace").rstrip("\r")
        parts = line.split(" ")
        if parts[0].upper() == "PRIVMSG" and "latency-probe" in line:
            with open(a.log + ".probe", "a") as f:
                f.write(f"{time.time():.3f} {line}\n")
        if parts[0].upper() == "PING":
            send_blocking(f":fake PONG fake {parts[1] if len(parts) > 1 else ''}")
        elif parts[0].upper() == "QUIT":
            raise SystemExit(0)


def send_blocking(line):
    conn.setblocking(True)
    send(line)
    conn.setblocking(False)


end = time.time() + a.seconds
batch = max(1, a.rate // 50)
conn.setblocking(True)
try:
    while time.time() < end:
        t0 = time.time()
        for _ in range(batch):
            event()
        conn.setblocking(False)
        drain()
        conn.setblocking(True)
        dt = 0.02 - (time.time() - t0)
        if dt > 0:
            time.sleep(dt)
    # stay up, quiet, until the client quits
    conn.settimeout(a.timeout)
    while True:
        data = conn.recv(4096)
        if not data or b"QUIT" in data:
            break
except (socket.timeout, ConnectionError, BrokenPipeError):
    pass
finally:
    with open(a.log, "w") as f:
        f.write(f"{sent}\n")
    conn.close()
