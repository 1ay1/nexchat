#!/usr/bin/env python3
"""How long the client takes over N channel lines when it's in many
channels: a fake server that joins C channels, then sends N PRIVMSGs to
the last one as fast as the socket takes them, and reports when the
client's PING (sent after the burst) comes back.
usage: tests/bench-lines.py NEXCHAT-TEXT [CHANNELS] [LINES]"""
import os, socket, subprocess, sys, tempfile, time

binp = sys.argv[1]
nchan = int(sys.argv[2]) if len(sys.argv) > 2 else 300
nline = int(sys.argv[3]) if len(sys.argv) > 3 else 50000
srv = socket.socket(); srv.bind(("127.0.0.1", 0)); srv.listen(1)
port = srv.getsockname()[1]
cfg = tempfile.mkdtemp()
open(os.path.join(cfg, "hexchat.conf"), "w").write(
    "net_throttle = 0\nirc_nick1 = bench\ntext_max_lines = 500\naway_track = 0\nirc_who_join = 0\n")
p = subprocess.Popen([binp, "-n", "-d", cfg, f"irc://127.0.0.1:-{port}/"],
                     stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
c, _ = srv.accept(); f = c.makefile("rb")
def send(s): c.sendall(s.encode())
while True:
    l = f.readline().decode()
    if l.startswith("NICK"):
        break
send(":fake 001 bench :hi\r\n:fake 376 bench :end\r\n")
chans = [f"#c{i}" for i in range(nchan)]
for ch in chans:
    send(f":bench!u@h JOIN {ch}\r\n:fake 353 bench = {ch} :bench a b c\r\n:fake 366 bench {ch} :end\r\n")
time.sleep(2)
burst = "".join(f":a!u@h PRIVMSG {chans[-1]} :line {i} with some ordinary text in it\r\n" for i in range(nline))
t0 = time.time()
send(burst)
send(":fake PING :done\r\n")
while True:
    l = f.readline().decode()
    if not l:
        print("client went away"); break
    if l.startswith("PONG") and "done" in l:
        dt = time.time() - t0
        print(f"{nchan} channels, {nline} lines: {dt:.2f}s ({nline/dt:.0f} lines/s)")
        break
p.kill()
