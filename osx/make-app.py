#!/usr/bin/env python3
"""Build a self-contained nexchat.app from a meson build directory.

    osx/make-app.py BUILDDIR [--out DIR] [--sign IDENTITY]

Copies the nexchat binary, the bundled plugins and translations into
nexchat.app, then every non-system library they load (recursively) into
Contents/Frameworks, rewriting the install names to @rpath / @loader_path
so the bundle runs on a Mac without Homebrew. GTK's runtime data goes in
Contents/Resources (GSettings schemas, the gdk-pixbuf loaders and their
cache, hicolor icons); the binary finds it through macos_bundle_setup()
in src/fe-gtk/platform-macos.m. Signed ad hoc unless --sign is given
(Apple Silicon refuses unsigned code).

A real Developer ID (see osx/SIGNING.md) signs every Mach-O inside out
with the hardened runtime and a secure timestamp, then the bundle.
Ad hoc stays `--sign -` with no runtime flag: check-app.sh depends on
DYLD_PRINT_LIBRARIES, which the hardened runtime strips.
"""
import argparse
import os
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ENTITLEMENTS = os.path.join(HERE, "nexchat.entitlements")

# Mach-O and fat Mach-O magics. CAFEBABE is also a Java class file; a fat
# header's arch count is tiny, a class file's version isn't.
_MACHO = (
    b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe",
    b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",
    b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca",
)


def is_macho(path):
    if os.path.islink(path) or not os.path.isfile(path):
        return False
    try:
        with open(path, "rb") as f:
            magic = f.read(8)
    except OSError:
        return False
    if len(magic) < 4:
        return False
    head = magic[:4]
    if head in _MACHO:
        return True
    if head in (b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca") and len(magic) >= 8:
        order = "big" if head == b"\xca\xfe\xba\xbe" else "little"
        n = int.from_bytes(magic[4:8], order)
        return 1 <= n <= 16
    return False


def sign_argv(identity, path, entitlements=None, keychain=None):
    """codesign invocation. Ad hoc is exactly --force --sign - with nothing
    else, which is what the bundle has always used. A Developer ID gets the
    hardened runtime and a secure timestamp; entitlements go on executables
    and the bundle, not on libraries."""
    cmd = ["codesign", "--force"]
    if identity != "-":
        cmd += ["--options", "runtime", "--timestamp"]
        if entitlements:
            cmd += ["--entitlements", entitlements, "--generate-entitlement-der"]
        if keychain:
            cmd += ["--keychain", keychain]
    cmd += ["--sign", identity, path]
    return cmd


def _selftest():
    assert sign_argv("-", "a.dylib") == ["codesign", "--force", "--sign", "-", "a.dylib"]
    assert sign_argv("-", "a.dylib", ENTITLEMENTS, "/tmp/k") == [
        "codesign", "--force", "--sign", "-", "a.dylib"]
    ident = "Developer ID Application: Example (TEAMID)"
    lib = sign_argv(ident, "libgtk.dylib", keychain="/tmp/k")
    assert lib == [
        "codesign", "--force", "--options", "runtime", "--timestamp",
        "--keychain", "/tmp/k", "--sign", ident, "libgtk.dylib"]
    exe = sign_argv(ident, "nexchat.app", ENTITLEMENTS)
    assert "--entitlements" in exe and exe[exe.index("--entitlements") + 1] == ENTITLEMENTS
    assert "--generate-entitlement-der" in exe
    assert "--options" in exe and exe[exe.index("--options") + 1] == "runtime"
    assert "--timestamp" in exe
    # libraries don't get the entitlements plist
    assert "--entitlements" not in lib
    d = tempfile.mkdtemp()
    try:
        thin = os.path.join(d, "thin")
        with open(thin, "wb") as f:
            f.write(b"\xfe\xed\xfa\xcf" + b"\0" * 4)
        assert is_macho(thin)
        fat = os.path.join(d, "fat")
        with open(fat, "wb") as f:
            f.write(b"\xca\xfe\xba\xbe" + (2).to_bytes(4, "big"))
        assert is_macho(fat)
        java = os.path.join(d, "java")
        with open(java, "wb") as f:
            f.write(b"\xca\xfe\xba\xbe" + (61).to_bytes(4, "big"))
        assert not is_macho(java)
        assert not is_macho(os.path.join(d, "missing"))
        os.symlink(thin, os.path.join(d, "link"))
        assert not is_macho(os.path.join(d, "link"))
        with open(ENTITLEMENTS, "rb") as f:
            ent = plistlib.load(f)
        assert ent == {}
    finally:
        shutil.rmtree(d)
    print("sign argv ok")


if os.environ.get("NEXCHAT_SIGN_ARGV_TEST") == "1":
    _selftest()
    raise SystemExit(0)

ap = argparse.ArgumentParser()
ap.add_argument("builddir")
ap.add_argument("--out", default=None, help="where nexchat.app goes (default: BUILDDIR)")
ap.add_argument("--sign", default="-", help="codesign identity (default: ad hoc)")
a = ap.parse_args()

build = os.path.abspath(a.builddir)
out = os.path.abspath(a.out or build)
app = os.path.join(out, "nexchat.app")
contents = os.path.join(app, "Contents")
macos = os.path.join(contents, "MacOS")
fw = os.path.join(contents, "Frameworks")
res = os.path.join(contents, "Resources")


def run(*cmd, check=True):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if check and r.returncode:
        sys.exit(f"{' '.join(cmd)}: {r.stderr.strip()}")
    return r.stdout


def install_prefix(binary):
    """the package manager prefix the binary's GTK comes from: /opt/homebrew
    for /opt/homebrew/opt/gtk4/lib/libgtk-4.1.dylib, /opt/local for
    /opt/local/lib/libgtk-4.1.dylib -- where the shared loaders, schemas
    and tools of all its packages live, whichever is first in PATH"""
    for line in run("otool", "-L", binary).splitlines()[1:]:
        dep = line.strip().split(" (")[0]
        if os.path.basename(dep).startswith("libgtk-4"):
            m = re.match(r"(.*)/opt/[^/]+/lib/", dep)
            return m.group(1) if m else os.path.dirname(os.path.dirname(dep))
    sys.exit(f"{binary} doesn't link GTK 4")


def meson_value(name):
    """a value from the build's config.h (VERSION, HEXCHATLIBDIR, ...)"""
    with open(os.path.join(build, "config.h")) as f:
        for line in f:
            m = re.match(r'#define\s+%s\s+"(.*)"' % name, line)
            if m:
                return m.group(1)
    return None


def deps(path):
    """non-system libraries path links to, as (as-written, real file) pairs;
    @rpath/@loader_path ones resolved the way dyld would from path's
    original location (not its own install name, which otool -L prints
    first for a library)"""
    own = run("otool", "-D", path).splitlines()[1:]
    own = own[0].strip() if own else None
    here = os.path.dirname(os.path.realpath(path))
    lines = run("otool", "-L", path).splitlines()[1:]
    out = []
    for line in lines:
        dep = line.strip().split(" (")[0]
        if dep == own or dep.startswith(("/usr/lib/", "/System/")):
            continue
        real = None
        if dep.startswith("@rpath/"):
            for r in rpaths(path):
                r = r.replace("@loader_path", here).replace("@executable_path", here)
                cand = os.path.join(r, dep[len("@rpath/"):])
                if os.path.exists(cand):
                    real = cand
                    break
            if real is None:
                # already in the bundle (a copy we made) or a sibling
                cand = os.path.join(here, dep[len("@rpath/"):])
                real = cand if os.path.exists(cand) else None
        elif dep.startswith(("@loader_path/", "@executable_path/")):
            real = os.path.normpath(os.path.join(here, dep.split("/", 1)[1]))
        else:
            real = dep
        if real is None:
            continue
        out.append((dep, real))
    return out


def rpaths(path):
    out, lines = [], run("otool", "-l", path).splitlines()
    for i, line in enumerate(lines):
        if "cmd LC_RPATH" in line:
            out.append(lines[i + 2].strip().split(" ")[1])
    return out


def fix(path, to_fw):
    """point path's library references into Frameworks; to_fw is the
    relative path from path's directory to Contents/Frameworks"""
    os.chmod(path, 0o755)
    for r in rpaths(path):
        run("install_name_tool", "-delete_rpath", r, path, check=False)
    run("install_name_tool", "-add_rpath", "@loader_path/" + to_fw, path)
    for dep, _ in deps(path):
        if not dep.startswith("@rpath/"):
            run("install_name_tool", "-change", dep, "@rpath/" + os.path.basename(dep), path)
    if path.startswith(fw):
        run("install_name_tool", "-id", "@rpath/" + os.path.basename(path), path)
    elif path.endswith((".dylib", ".so")):
        # a loadable module: its own name, not the build machine's path
        run("install_name_tool", "-id", os.path.basename(path), path)


copied = set()


def bundle_libs(path, src=None):
    """copy every library path needs (recursively) into Frameworks; src is
    where path came from, to resolve its @rpath entries against"""
    for dep, real in deps(src or path):
        name = os.path.basename(dep)
        if name in copied:
            continue
        real = os.path.realpath(real)
        if not os.path.exists(real):
            sys.exit(f"{path} needs {dep}, which doesn't exist")
        copied.add(name)
        dst = os.path.join(fw, name)
        shutil.copy2(real, dst)
        bundle_libs(dst, real)
        fix(dst, ".")


shutil.rmtree(app, ignore_errors=True)
for d in (macos, fw, res):
    os.makedirs(d)

# the program, and what it links
exe = os.path.join(macos, "nexchat")
exe_src = os.path.join(build, "src", "fe-gtk", "nexchat")
shutil.copy2(exe_src, exe)
bundle_libs(exe, exe_src)
fix(exe, "../Frameworks")

# the terminal UI (`nexchat --tui` runs it from beside nexchat), when built
tui_src = os.path.join(build, "src", "fe-tui", "nexchat-tui")
if os.path.exists(tui_src):
    tui = os.path.join(macos, "nexchat-tui")
    shutil.copy2(tui_src, tui)
    bundle_libs(tui, tui_src)
    fix(tui, "../Frameworks")

# plugins (fishlim, sysinfo, checksum, ...): Resources/lib/nexchat/plugins
plugdir = os.path.join(res, "lib", "nexchat", "plugins")
os.makedirs(plugdir)
for top, _, files in os.walk(os.path.join(build, "plugins")):
    if ".p" in top.split(os.sep)[-1] or "tests" in top:
        continue
    for f in files:
        if f.endswith(".dylib"):
            dst = os.path.join(plugdir, f)
            shutil.copy2(os.path.join(top, f), dst)
            bundle_libs(dst, os.path.join(top, f))
            fix(dst, "../../../../Frameworks")

# OpenSSL's provider modules: FiSH needs "legacy" for Blowfish, loaded
# from OPENSSL_MODULES (macos_bundle_setup points it here)
ossl = [d for d in copied if d.startswith("libcrypto")]
if ossl:
    crypto = os.path.realpath(os.path.join(fw, ossl[0]))
    src_mods = None
    for line in run("otool", "-L", exe_src).splitlines()[1:]:
        dep = line.strip().split(" (")[0]
        if os.path.basename(dep).startswith("libcrypto"):
            src_mods = os.path.join(os.path.dirname(os.path.realpath(dep)), "ossl-modules")
    if src_mods and os.path.isdir(src_mods):
        mdst = os.path.join(res, "lib", "ossl-modules")
        os.makedirs(mdst)
        for f in os.listdir(src_mods):
            if f.endswith(".dylib"):
                shutil.copy2(os.path.join(src_mods, f), os.path.join(mdst, f))
                bundle_libs(os.path.join(mdst, f), os.path.join(src_mods, f))
                fix(os.path.join(mdst, f), "../../../Frameworks")

# translations: Resources/share/locale/LANG/LC_MESSAGES/nexchat.mo
for top, _, files in os.walk(os.path.join(build, "po")):
    for f in files:
        if f.endswith(".mo"):
            rel = os.path.relpath(os.path.join(top, f), os.path.join(build, "po"))
            dst = os.path.join(res, "share", "locale", rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(os.path.join(top, f), dst)

# GTK runtime data: the gdk-pixbuf loaders GTK uses for PNG/SVG images
# (GTK4 has PNG/JPEG/TIFF built in; SVG icons need librsvg's loader)
prefix = install_prefix(os.path.join(build, "src", "fe-gtk", "nexchat"))
# GTK in a prefix of its own (osx/build-gtk.sh): gdk-pixbuf, librsvg and
# GLib's schemas and tools are still where Homebrew put them
dprefix = prefix if os.path.isdir(os.path.join(prefix, "lib", "gdk-pixbuf-2.0")) else \
    os.path.dirname(os.path.dirname(shutil.which("glib-compile-schemas") or "/opt/homebrew/bin/x"))
pixbuf = os.path.join(dprefix, "lib", "gdk-pixbuf-2.0", "2.10.0", "loaders")
ldir = os.path.join(res, "lib", "gdk-pixbuf-2.0", "2.10.0", "loaders")
os.makedirs(ldir)
if os.path.isdir(pixbuf):
    for f in sorted(os.listdir(pixbuf)):
        if not f.endswith((".so", ".dylib")) or (f.endswith(".so") and os.path.exists(os.path.join(pixbuf, f[:-3] + ".dylib"))):
            continue
        src = os.path.realpath(os.path.join(pixbuf, f))
        dst = os.path.join(ldir, f)
        shutil.copy2(src, dst)
        bundle_libs(dst, src)
        fix(dst, "../../../../../Frameworks")
# loaders.cache: gdk-pixbuf resolves a relative module path against the
# loaders directory, so the cache stays valid wherever the bundle goes
query = os.path.join(dprefix, "bin", "gdk-pixbuf-query-loaders")
if os.access(query, os.X_OK):
    text = run(query, *[os.path.join(ldir, f) for f in sorted(os.listdir(ldir))])
    text = text.replace(ldir + "/", "")
    with open(os.path.join(res, "lib", "gdk-pixbuf-2.0", "2.10.0", "loaders.cache"), "w") as f:
        f.write(text)
else:
    print("warning: no gdk-pixbuf-query-loaders, SVG icons won't load")

# GSettings schemas GTK reads (file chooser, colour chooser)
schemas = os.path.join(prefix, "share", "glib-2.0", "schemas")
sdst = os.path.join(res, "share", "glib-2.0", "schemas")
os.makedirs(sdst)
for f in os.listdir(schemas):
    if f.startswith("org.gtk.") and f.endswith(".xml"):
        shutil.copy2(os.path.join(schemas, f), sdst)
run(shutil.which("glib-compile-schemas") or os.path.join(prefix, "bin", "glib-compile-schemas"), sdst)

# icons
shutil.copy2(os.path.join(HERE, "nexchat.icns"), os.path.join(res, "nexchat.icns"))

version = meson_value("PACKAGE_VERSION") or "0"
with open(os.path.join(contents, "Info.plist"), "wb") as f:
    plistlib.dump({
        "CFBundleDevelopmentRegion": "en",
        "CFBundleExecutable": "nexchat",
        "CFBundleIconFile": "nexchat",
        "CFBundleIdentifier": "io.github.nexchat",
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": "nexchat",
        "CFBundleDisplayName": "nexchat",
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": version,
        "CFBundleVersion": version,
        "LSMinimumSystemVersion": "11.0",
        "LSApplicationCategoryType": "public.app-category.social-networking",
        "NSHighResolutionCapable": True,
        "NSSupportsAutomaticGraphicsSwitching": True,
        "CFBundleURLTypes": [{
            "CFBundleURLName": "IRC URL",
            "CFBundleURLSchemes": ["irc", "ircs"],
        }],
    }, f)

# Apple Silicon only runs signed code; install_name_tool broke the
# original signatures, so sign everything again (inside out).
# Ad hoc keeps the flags this bundle has always used. A Developer ID
# signs every Mach-O (hardened runtime, timestamp), executables in
# Contents/MacOS get osx/nexchat.entitlements, and the bundle is last
# so its seal covers the signatures inside it.
if a.sign in ("-", ""):
    for top, _, files in os.walk(contents):
        for f in files:
            p = os.path.join(top, f)
            if f.endswith((".dylib", ".so")):
                run("codesign", "--force", "--sign", "-", p)
    run("codesign", "--force", "--sign", "-", exe)
    if os.path.exists(os.path.join(macos, "nexchat-tui")):
        run("codesign", "--force", "--sign", "-", os.path.join(macos, "nexchat-tui"))
    run("codesign", "--force", "--sign", "-", app)
else:
    with open(ENTITLEMENTS, "rb") as f:
        plistlib.load(f)
    # Homebrew libraries sometimes carry Finder info that codesign rejects
    # once the signature has a timestamp ("detritus not allowed").
    subprocess.run(["xattr", "-cr", app], capture_output=True, text=True)
    keychain = os.environ.get("CODESIGN_KEYCHAIN") or None
    machos = []
    for top, _, files in os.walk(app):
        for f in files:
            p = os.path.join(top, f)
            if is_macho(p):
                machos.append(p)
    machos.sort(key=lambda p: (-p.count(os.sep), p))
    for p in machos:
        ent = ENTITLEMENTS if os.path.dirname(p) == macos else None
        print(f"sign {'executable' if ent else 'library'} {os.path.relpath(p, app)}")
        run(*sign_argv(a.sign, p, ent, keychain))
    print(f"sign bundle {os.path.basename(app)}")
    run(*sign_argv(a.sign, app, ENTITLEMENTS, keychain))
    r = subprocess.run(["codesign", "--verify", "--deep", "--strict", "--verbose=2", app])
    if r.returncode:
        sys.exit(f"codesign --verify --deep --strict {app} failed ({r.returncode})")
    info = subprocess.run(["codesign", "-d", "--verbose=4", app], capture_output=True, text=True)
    sys.stdout.write(info.stderr)
    if info.returncode or "(runtime)" not in info.stderr or "Developer ID Application" not in info.stderr:
        sys.exit(f"{app} is not a hardened-runtime Developer ID signature")

size = run("du", "-sh", app).split()[0]
print(f"{app}: {len(copied)} libraries, {size}")
