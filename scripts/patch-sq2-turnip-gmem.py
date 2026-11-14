#!/usr/bin/env python3
"""Manage or revert SQ2/A680.1 Turnip sysmem workarounds in Mesa 26.2.4.

Default behavior (--revert) ensures src/freedreno/vulkan/tu_cmd_buffer.cc is the
pristine upstream hardware-GMEM source (SHA256 d61e9642...) without the legacy
3DMark or global sysmem workaround.
"""
import argparse
import hashlib
from pathlib import Path
import sys

ORIGINAL_HASH = 'd61e9642c7daadd6144fbbe20d9f1fe6d1b3f51db64d2c63da886a3182df4fe8'
TU_CMD_BUFFER_REL = Path('src/freedreno/vulkan/tu_cmd_buffer.cc')
INCLUDE_TARGET = '#include "tu_cmd_buffer.h"\n'
INCLUDE = '#include "util/u_process.h"\n'
ANCHOR = "   /* can't fit attachments into gmem */"

LEGACY_3DMARK_BLOCK = '''   /* SQ2_A680_1_3DMARK_SYSMEM: bounded compatibility workaround.
    * Wild Life misrenders in GMEM at both 514 and 661 MHz; sysmem produces
    * correct captured frames. Do not infer a hardware or UBWC root cause.
    * Keep other applications on their normal path while GMEM is investigated.
    */
   static const bool sq2_3dmark = []() {
      const char *name = util_get_process_name();
      return name && !strcmp(name, "com.futuremark.dmandroid.application");
   }();
   if (sq2_3dmark &&
       cmd->device->physical_device->dev_id.chip_id == 0x06080001 &&
       !TU_DEBUG(GMEM)) {
      cmd->state.rp.gmem_disable_reason = "SQ2 A680.1 3DMark compatibility";
      return true;
   }

'''

GLOBAL_SYSMEM_BLOCK = '''   /* SQ2_A680_1_GLOBAL_SYSMEM: avoid corrupt GMEM rendering across apps.
    * shortcut: use sysmem until A680.1 GMEM rendering is repaired.
    * Explicit TU_DEBUG=gmem retains the original path for research.
    */
   if (cmd->device->physical_device->dev_id.chip_id == 0x06080001 &&
       !TU_DEBUG(GMEM)) {
      cmd->state.rp.gmem_disable_reason = "A680.1 global sysmem compatibility";
      return true;
   }

'''


def resolve_target(path):
    """Resolve either a Mesa source root or direct tu_cmd_buffer.cc path."""
    target = Path(path)
    if target.is_file() or target.name == 'tu_cmd_buffer.cc':
        return target
    return target / TU_CMD_BUFFER_REL


def _strip_known_workarounds(source):
    """Strip known 3DMark or global sysmem hunks and return pristine source."""
    candidate = source
    if 'SQ2_A680_1_3DMARK_SYSMEM' in candidate:
        if candidate.count(LEGACY_3DMARK_BLOCK) != 1 or candidate.count(INCLUDE) != 1:
            raise ValueError('Unexpected modified legacy 3DMark sysmem workaround')
        candidate = candidate.replace(LEGACY_3DMARK_BLOCK, '', 1).replace(INCLUDE, '', 1)
    if 'SQ2_A680_1_GLOBAL_SYSMEM' in candidate:
        if candidate.count(GLOBAL_SYSMEM_BLOCK) != 1:
            raise ValueError('Unexpected modified global sysmem workaround')
        candidate = candidate.replace(GLOBAL_SYSMEM_BLOCK, '', 1)
        if candidate.count(INCLUDE) == 1 and hashlib.sha256(candidate.encode()).hexdigest() != ORIGINAL_HASH:
            candidate = candidate.replace(INCLUDE, '', 1)
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
    """Revert known Turnip sysmem workarounds so hardware GMEM stays intact."""
    target = resolve_target(path)
    source = target.read_text()
    clean = _strip_known_workarounds(source)
    _assert_clean_hash(clean)
    if clean != source:
        target.write_text(clean)
        return True
    return False


def apply_legacy_3dmark_sysmem(path):
    """Explicit opt-in for the historical 3DMark A680.1 sysmem workaround."""
    target = resolve_target(path)
    source = target.read_text()
    if 'SQ2_A680_1_3DMARK_SYSMEM' in source and 'SQ2_A680_1_GLOBAL_SYSMEM' not in source:
        clean = _strip_known_workarounds(source)
        _assert_clean_hash(clean)
        return False
    clean = _strip_known_workarounds(source)
    _assert_clean_hash(clean)
    if clean.count(INCLUDE_TARGET) != 1 or clean.count(ANCHOR) != 1:
        raise ValueError('Unexpected tu_cmd_buffer.cc insertion anchor')
    patched = clean.replace(INCLUDE_TARGET, INCLUDE_TARGET + INCLUDE, 1)
    patched = patched.replace(ANCHOR, LEGACY_3DMARK_BLOCK + ANCHOR, 1)
    target.write_text(patched)
    return True


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
    mode.add_argument(
        '--apply-legacy-3dmark-sysmem',
        action='store_true',
        help='Explicitly apply the legacy 3DMark sysmem workaround (opt-in only)',
    )
    parser.add_argument('mesa_src', type=Path, help='Mesa source root or tu_cmd_buffer.cc')
    args = parser.parse_args(argv)

    if args.verify_clean:
        verify_clean(args.mesa_src)
        print('SQ2 Turnip GMEM source verified clean (pristine upstream)')
        return 0
    if args.apply_legacy_3dmark_sysmem:
        changed = apply_legacy_3dmark_sysmem(args.mesa_src)
        print('SQ2 Turnip legacy 3DMark sysmem workaround ' + ('applied' if changed else 'already applied'))
        return 0

    changed = revert_file(args.mesa_src)
    print('SQ2 Turnip GMEM source ' + ('reverted to pristine hardware-GMEM' if changed else 'already pristine hardware-GMEM'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
