#!/usr/bin/env python3
"""Add standard EFI tablet buttons to the pinned Surface GRUB console.

Source: linux-surface/grub 71402fac6df4af0c06fea69aec6cf4907a1234e0.
UEFI SCAN_VOLUME_UP/DOWN/SUSPEND: 0x80/0x81/0x102.
This patches a source checkout, never a firmware image or physical disk.
"""
from pathlib import Path
import hashlib
import sys

SOURCE_SHA = 'd6f77523a24b2093c3260b8f2c85396d12e8c77afe5d661135cb97f3a6ec223a'
ANCHOR = '  /* Some devices send enter with scan_code 0x0d (F3) and unicode_char 0x0d. */\n'
BLOCK = '''  /* SQ2_BUTTONS_BEGIN: standard non-printable EFI tablet keys. */
  else if (key.unicode_char == 0 && key.scan_code == 0x0080)
    return GRUB_TERM_KEY_UP;
  else if (key.unicode_char == 0 && key.scan_code == 0x0081)
    return GRUB_TERM_KEY_DOWN;
  else if (key.unicode_char == 0 && key.scan_code == 0x0102)
    return '\\r';
  /* SQ2_BUTTONS_END */
'''


def patch(source):
    if hashlib.sha256(source).hexdigest() != SOURCE_SHA:
        raise ValueError('Expected the exact unmodified Surface Fedora39 console')
    text = source.decode()
    if text.count(ANCHOR) != 1:
        raise ValueError('Ambiguous key translation anchor')
    result = text.replace(ANCHOR, BLOCK+ANCHOR).encode()
    assert result.replace(BLOCK.encode(), b'') == source
    return result


if __name__ == '__main__':
    path = Path(sys.argv[1])/'grub-core/term/efi/console.c'
    result = patch(path.read_bytes())
    path.write_bytes(result)
    print('Patched EFI volume up/down and power; SHA256', hashlib.sha256(result).hexdigest())
