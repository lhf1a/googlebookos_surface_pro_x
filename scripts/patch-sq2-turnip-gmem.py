#!/usr/bin/env python3
"""Verify or revert SQ2/A680.1 Turnip sysmem workarounds in Mesa 26.2.4.

Ensures src/freedreno/vulkan/tu_cmd_buffer.cc is the pristine upstream
hardware-GMEM source (SHA256 d61e9642...) with no 3DMark or global sysmem
workaround present.
"""
import argparse
import hashlib
from pathlib import Path
import re
import sys

ORIGINAL_HASH = 'd61e9642c7daadd6144fbbe20d9f1fe6d1b3f51db64d2c63da886a3182df4fe8'
TU_CMD_BUFFER_REL = Path('src/freedreno/vulkan/tu_cmd_buffer.cc')
INCLUDE_TARGET = '#include "tu_cmd_buffer.h"\n'
INCLUDE = '#include "util/u_process.h"\n'
ANCHOR = "   /* can't fit attachments into gmem */"
LEGACY_BLOCK_RE = re.compile(
    r"   /\* SQ2_A680_1_(?:3DMARK|GLOBAL)_SYSMEM:.*?return true;\n   \}\n\n(?=" + re.escape(ANCHOR) + ")",
    re.DOTALL,
)


def resolve_target(path):
    """Resolve either a Mesa source root or direct tu_cmd_buffer.cc path."""
    target = Path(path)
    if target.is_file() or target.name == 'tu_cmd_buffer.cc':
        return target
    return target / TU_CMD_BUFFER_REL


def _strip_known_workarounds(source):
    """Strip any legacy SQ2_A680_1 sysmem workaround hunks if present."""
    candidate = LEGACY_BLOCK_RE.sub('', source)
    if candidate != source and INCLUDE in candidate:
        without_include = candidate.replace(INCLUDE, '', 1)
        if hashlib.sha256(without_include.encode()).hexdigest() == ORIGINAL_HASH:
            candidate = without_include
    return candidate


def _assert_clean_hash(source):
    digest = hashlib.sha256(source.encode()).hexdigest()
    if digest != ORIGINAL_HASH:
        raise ValueError(f'Unexpected Mesa tu_cmd_buffer.cc SHA256: {digest} != {ORIGINAL_HASH}')


def verify_clean(path):
    """Verify tu_cmd_buffer.cc matches the pristine upstream hardware-GMEM SHA256."""
    target = resolve_target(path)
    _assert_clean_hash(target.read_text())
    return target


def revert_file(path):
    """Revert any legacy Turnip sysmem workarounds so hardware GMEM stays intact."""
    target = resolve_target(path)
    source = target.read_text()
    clean = _strip_known_workarounds(source)
    _assert_clean_hash(clean)
    if clean != source:
        target.write_text(clean)
        return True
    return False


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        '--revert',
        action='store_true',
        help='Ensure pristine upstream hardware-GMEM tu_cmd_buffer.cc (default)',
    )
    mode.add_argument(
        '--verify-clean',
        action='store_true',
        help='Verify tu_cmd_buffer.cc matches pristine upstream SHA256 without writing',
    )
    parser.add_argument('mesa_src', type=Path, help='Mesa source root or tu_cmd_buffer.cc')
    args = parser.parse_args(argv)

    if args.verify_clean:
        verify_clean(args.mesa_src)
        print('SQ2 Turnip GMEM source verified clean (pristine upstream)')
        return 0

    changed = revert_file(args.mesa_src)
    print('SQ2 Turnip GMEM source ' + ('reverted to pristine hardware-GMEM' if changed else 'already pristine hardware-GMEM'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
