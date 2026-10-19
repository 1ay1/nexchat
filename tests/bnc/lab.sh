#!/bin/sh
# Start real bouncers for tests: Ergo (the IRC network, with always-on),
# soju and ZNC in front of it, all on loopback, in a private dir.
# usage: . tests/bnc/lab.sh; bnc_start DIR; ...; bnc_stop
# Sets ERGO_PORT, SOJU_PORT, SOJU_TLS_PORT (self-signed), ZNC_PORT. The user is "ayush", password
# "secret123", on soju and ZNC (ZNC's network to Ergo is "ergo").
# The binaries come from tests/bnc/fetch.sh (into ~/.cache/nexchat-ref/bnc).

BNC_HOME=${BNC_HOME:-$HOME/.cache/nexchat-ref/bnc}
ERGO=$BNC_HOME/ergo-2.19.1-linux-x86_64/ergo
SOJU=$BNC_HOME/usr/bin/soju
SOJUDB=$BNC_HOME/usr/bin/sojudb
ZNC=$BNC_HOME/usr/bin/znc

bnc_have() { [ -x "$ERGO" ] && [ -x "$SOJU" ] && [ -x "$ZNC" ]; }

# a free port on loopback
bnc_port() { python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])'; }

bnc_wait_port() {
	python3 - "$1" <<'EOF'
import socket, sys, time
for _ in range(100):
    try:
        socket.create_connection(("127.0.0.1", int(sys.argv[1])), 0.2).close(); sys.exit(0)
    except OSError:
        time.sleep(0.1)
sys.exit(1)
EOF
}

bnc_start() {
	BNC_DIR=$1
	mkdir -p "$BNC_DIR/ergo" "$BNC_DIR/soju" "$BNC_DIR/znc/configs" "$BNC_DIR/znc/modules"
	ERGO_PORT=$(bnc_port); SOJU_PORT=$(bnc_port); ZNC_PORT=$(bnc_port)

	# Ergo: its default config, plaintext on loopback, no STS, always-on opt-in
	python3 - "$BNC_HOME/ergo-2.19.1-linux-x86_64/default.yaml" "$BNC_DIR/ergo/ircd.yaml" "$ERGO_PORT" "$BNC_DIR/ergo" <<'EOF'
import re, sys
s = open(sys.argv[1]).read()
s = re.sub(r'    listeners:\n(.*?)\n    # sets the permissions',
           '    listeners:\n        "127.0.0.1:%s":\n\n    # sets the permissions' % sys.argv[3], s, flags=re.S)
s = s.replace('require-tls: true', 'require-tls: false')
s = s.replace('path: ircd.db', 'path: %s/ircd.db' % sys.argv[4])
s = re.sub(r'(    sts:\n(?:.*\n)*?\s*)enabled: true', r'\1enabled: false', s, count=1)
# registering an account needs no email in tests
s = s.replace('enabled: true # verification', 'enabled: false # verification')
# deleting your own messages (and ops theirs), for REDACT
s = s.replace('allow-individual-delete: false', 'allow-individual-delete: true')
open(sys.argv[2], 'w').write(s)
EOF
	cp -r "$BNC_HOME/ergo-2.19.1-linux-x86_64/languages" "$BNC_DIR/ergo/"
	(cd "$BNC_DIR/ergo" && "$ERGO" initdb --conf ircd.yaml >/dev/null 2>&1)
	(cd "$BNC_DIR/ergo" && exec "$ERGO" run --conf ircd.yaml >ergo.log 2>&1) &
	BNC_PIDS=$!

	# soju (also on TLS, with a self-signed certificate)
	SOJU_TLS_PORT=$(bnc_port)
	openssl req -x509 -newkey rsa:2048 -nodes -days 2 -subj "/CN=localhost" \
		-keyout "$BNC_DIR/soju/key.pem" -out "$BNC_DIR/soju/cert.pem" >/dev/null 2>&1
	cat > "$BNC_DIR/soju/config" <<EOF
listen irc+insecure://127.0.0.1:$SOJU_PORT
listen ircs://127.0.0.1:$SOJU_TLS_PORT
tls $BNC_DIR/soju/cert.pem $BNC_DIR/soju/key.pem
hostname localhost
db sqlite3 $BNC_DIR/soju/soju.db
message-store db
EOF
	printf 'secret123\n' | "$SOJUDB" -config "$BNC_DIR/soju/config" create-user ayush -admin >/dev/null 2>&1
	"$SOJU" -config "$BNC_DIR/soju/config" >"$BNC_DIR/soju/soju.log" 2>&1 &
	BNC_PIDS="$BNC_PIDS $!"

	# ZNC: the modules it needs linked in, one user with a network to Ergo
	for m in chansaver controlpanel simple_away; do ln -sf "$BNC_HOME/usr/lib/znc/$m.so" "$BNC_DIR/znc/modules/"; done
	cat > "$BNC_DIR/znc/configs/znc.conf" <<EOF
Version = 1.10.3
<Listener l>
	Port = $ZNC_PORT
	IPv4 = true
	IPv6 = false
	SSL = false
	Host = 127.0.0.1
</Listener>
<User ayush>
	Admin = true
	Nick = ayush
	AltNick = ayush_
	Ident = ayush
	RealName = Ayush
	LoadModule = chansaver
	LoadModule = controlpanel
	<Pass password>
		Method = Plain
		Hash = secret123
	</Pass>
	<Network ergo>
		Server = 127.0.0.1 $ERGO_PORT
	</Network>
</User>
EOF
	LD_LIBRARY_PATH=$BNC_HOME/usr/lib "$ZNC" -d "$BNC_DIR/znc" --foreground >"$BNC_DIR/znc/znc.log" 2>&1 &
	BNC_PIDS="$BNC_PIDS $!"

	bnc_wait_port "$ERGO_PORT" && bnc_wait_port "$SOJU_PORT" && bnc_wait_port "$ZNC_PORT"
}

bnc_stop() {
	if [ -n "${BNC_PIDS:-}" ]; then
		kill $BNC_PIDS 2>/dev/null || true
		wait $BNC_PIDS 2>/dev/null || true
	fi
	return 0
}

# say lines on the network as someone else: bnc_say NICK CHANNEL TEXT...
bnc_say() {
	python3 - "$ERGO_PORT" "$@" <<'EOF'
import socket, sys, time
port, nick, chan = int(sys.argv[1]), sys.argv[2], sys.argv[3]
s = socket.create_connection(("127.0.0.1", port)); f = s.makefile("rwb")
def w(l): f.write((l + "\r\n").encode()); f.flush()
w("NICK " + nick); w("USER %s 0 * :%s" % (nick, nick))
s.settimeout(5); buf = b""
while b" 001 " not in buf:
    buf += s.recv(4096)
w("JOIN " + chan); time.sleep(0.3)
for t in sys.argv[4:]:
    w("PRIVMSG %s :%s" % (chan, t))
time.sleep(0.5); w("QUIT"); time.sleep(0.2)
EOF
}
