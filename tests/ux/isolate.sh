# Sourced by the GUI tests. The app must open on our Xvfb, not on the
# session's Wayland compositor, and must not pick up the user's GTK theme or
# fonts: the tests click at fixed coordinates. DejaVu is what CI has.
unset WAYLAND_DISPLAY
export GDK_BACKEND=x11
# no accessibility bus in a private test session (GTK4 asserts without it)
export GTK_A11Y=none NO_AT_BRIDGE=1
XDG_CONFIG_HOME=$(mktemp -d)
export XDG_CONFIG_HOME
unset GTK_THEME
for v in gtk-3.0 gtk-4.0; do
	mkdir -p "$XDG_CONFIG_HOME/$v"
	printf '[Settings]\ngtk-theme-name=Adwaita\ngtk-icon-theme-name=Adwaita\ngtk-font-name=DejaVu Sans 10\ngtk-application-prefer-dark-theme=0\n' \
		> "$XDG_CONFIG_HOME/$v/settings.ini"
done
# "Monospace 9" (the chat font) is DejaVu Sans Mono on Ubuntu, not everywhere
mkdir -p "$XDG_CONFIG_HOME/fontconfig"
cat > "$XDG_CONFIG_HOME/fontconfig/fonts.conf" <<'EOF'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <include ignore_missing="yes">/etc/fonts/fonts.conf</include>
  <match><test name="family"><string>monospace</string></test>
    <edit name="family" mode="prepend" binding="strong"><string>DejaVu Sans Mono</string></edit></match>
  <match><test name="family"><string>sans-serif</string></test>
    <edit name="family" mode="prepend" binding="strong"><string>DejaVu Sans</string></edit></match>
</fontconfig>
EOF
export FONTCONFIG_FILE="$XDG_CONFIG_HOME/fontconfig/fonts.conf"

# start_xvfb XVFB DISPLAY [W H]: start Xvfb and wait until it takes
# connections (a fixed sleep wasn't enough on a busy machine). Sets $xv.
start_xvfb() {
	"$1" "$2" -screen 0 "${3:-1100}x${4:-720}x24" -nolisten tcp >/dev/null 2>&1 &
	xv=$!
	DISPLAY=$2 python3 "${NX_UX_DIR:-$(dirname "$0")}/xkey.py" --wait-display
}
