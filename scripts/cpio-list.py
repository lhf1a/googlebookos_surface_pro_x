#!/usr/bin/env python3
"""Produce gen_init_cpio input with root-owned files and a console device."""
from pathlib import Path
import stat
import sys

root = Path(sys.argv[1]).resolve()
if any(c.isspace() for c in str(root)):
    raise SystemExit('Use a Linux build directory without whitespace.')
for path in sorted(root.rglob('*')):
    name = '/' + path.relative_to(root).as_posix()
    mode = stat.S_IMODE(path.lstat().st_mode)
    if path.is_symlink():
        print(f'slink {name} {path.readlink()} {mode:o} 0 0')
    elif path.is_dir():
        print(f'dir {name} {mode:o} 0 0')
    elif path.is_file():
        print(f'file {name} {path} {mode:o} 0 0')
print('nod /dev/console 600 0 0 c 5 1')
print('nod /dev/null 666 0 0 c 1 3')
