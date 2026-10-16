#!/usr/bin/env python3

import os
import shutil
import sys
import subprocess

prefix = os.environ.get('MESON_INSTALL_PREFIX', '/usr/local')
datadir = os.path.join(prefix, 'share')
with_thememan = sys.argv[1] == 'true'


def run(cmd):
    # these are freedesktop cache updaters; skip quietly where a platform
    # (Windows/MSYS2, macOS) doesn't ship them instead of failing install
    if shutil.which(cmd[0]) is None:
        print('  (%s not found, skipped)' % cmd[0])
        return
    subprocess.call(cmd)


# Packaging tools define DESTDIR and this isn't needed for them
if 'DESTDIR' not in os.environ:
    print('Updating icon cache...')
    run(['gtk-update-icon-cache', '-qtf',
         os.path.join(datadir, 'icons', 'hicolor')])

    print('Updating desktop database...')
    run(['update-desktop-database', '-q',
         os.path.join(datadir, 'applications')])

    if with_thememan:
        print('Updating mime database...')
        run(['update-mime-database', os.path.join(datadir, 'mime')])
