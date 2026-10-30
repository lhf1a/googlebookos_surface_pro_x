#!/usr/bin/env python3
"""Pinned Pro-X display profiles; software EDID only, never EEPROM or disk I/O.

Keep the common initramfs stock. Only the 65Hz entry appends the tested module
and EDID, followed by the original credential member (last-entry-wins).
"""
import gzip
import hashlib
import re
import zlib

PROFILES = ('5.4-65', '2.7-60')
DEFAULT = '5.4-65'
STOCK_SHA = 'f69c91e754e2a0d66cd195549d970973ed81b4c6f1bbfcbc3723f80a26414633'
MODULE_SHA = '080cceff71f5b6168bfc89ed372a48e4ac783cf09fa9500f573411651a54f263'
EDID_SHA = '83101509b096d6ddfc83e96bd08917d4989f78b80e2549489bd12f8d14e5a014'
MODULE = 'lib/modules/6.3.8-sq2-android1/kernel/drivers/gpu/drm/msm/msm.ko'
EDID = 'lib/firmware/edid/sq2-native65.bin'
BASE = 'native-combined-initramfs.cpio.gz'
ADDON = 'display-edp65-addon.cpio.gz'
BOOT = '/boot/sq2-android1/'
OVERRIDE = 'drm.edid_firmware=eDP-1:edid/sq2-native65.bin'
sha = lambda b: hashlib.sha256(b).hexdigest()


def require_hash(blob, expected, label):
    if sha(blob) != expected:
        raise ValueError('Unverified '+label)


def build_addon(base, cpio, module, edid):
    before = cpio.entries(gzip.decompress(base))
    require_hash(before[MODULE][1], STOCK_SHA, 'stock msm (60Hz fallback)')
    require_hash(module, MODULE_SHA, 'cached-only V6 msm')
    require_hash(edid, EDID_SHA, 'native65 EDID')
    if EDID in before:
        raise ValueError('Common initramfs must not contain the 65Hz override')
    rest, credential = base, None
    while rest:
        dec = zlib.decompressobj(31)
        files = cpio.entries(dec.decompress(rest))
        if not dec.eof:
            raise ValueError('Truncated gzip member')
        if 'etc/shadow' in files and 'bin/dropbear' in files:
            credential = rest[:len(rest)-len(dec.unused_data)]
        rest = dec.unused_data
    if credential is None:
        raise ValueError('Missing final lab SSH credential member')
    delta = {}
    def put(name, data, mode=0o100644):
        parent = name.rpartition('/')[0]
        if parent and parent not in before and parent not in delta:
            put(parent, b'', 0o40755)
        delta[name] = (mode, data)
    put(MODULE, module, before[MODULE][0])
    put(EDID, edid)
    archive = b''.join(cpio.entry(n, b, m) for n, (m, b) in delta.items())
    archive += cpio.entry('TRAILER!!!', b'', 0)
    archive += b'\0'*(-len(archive) % 512)
    addon = gzip.compress(archive, mtime=0)+credential
    after = cpio.entries(gzip.decompress(base+addon))
    expected = {**before, **delta}
    if after != expected or not addon.endswith(credential):
        raise ValueError('Display delta or credential ordering mismatch')
    return addon


def blocks(text):
    # Deliberately accept only the builder's simple flat menuentry grammar.
    return list(re.finditer(r'^menuentry [^\n]*\{[^{}]*\}', text, re.M))


def default_menu(text, profile=DEFAULT):
    if profile not in PROFILES:
        raise ValueError('Unknown display profile')
    if any(x in text for x in ('sq2.display=', 'drm.edid_firmware=', ADDON)):
        raise ValueError('Input must be a pristine common/stock menu')
    found = blocks(text)
    if not found:
        raise ValueError('Missing flat menu entries')
    normal = next((m for m in found if re.search(r'^\s*linux ', m[0], re.M)
                   and 'sq2.rescue=1' not in m[0]), None)
    if normal is None:
        raise ValueError('Missing normal entry')
    match = re.search(r'--id ([\w-]+)', normal[0])
    if not match:
        raise ValueError('Normal entry requires stable id')
    stock_id = match[1]
    clone = normal[0].splitlines()
    clone[0] = "menuentry 'SQ2 eDP 5.4 Gbps x4 / 65 Hz (experimental)' --id sq2-display-65 {"
    clone = '\n'.join(clone)
    clone = re.sub(r'(?m)^(\s*linux .+)$', r'\1 sq2.display=5.4-65 '+OVERRIDE, clone)
    clone = re.sub(r'(?m)^(\s*initrd .+)$', r'\1 '+BOOT+ADDON, clone)
    lines = []
    for line in text.splitlines():
        if line.lstrip().startswith('linux '):
            line += ' sq2.display=2.7-60'
        lines.append(line)
    text = '\n'.join(lines)+'\n'
    text = text.replace(normal[0].splitlines()[0],
                        normal[0].splitlines()[0].replace("' --id", " [eDP stock 2.7 Gbps x4 / 60 Hz]' --id"), 1)
    selected = 'sq2-display-65' if profile == '5.4-65' else stock_id
    text, count = re.subn(r'(?m)^set default=\S+$', 'set default='+selected, text)
    if count != 1:
        raise ValueError('Exactly one static default is required')
    text += clone+'\n'
    validate_menu(text.encode(), profile)
    return text


def validate_menu(menu, profile):
    if profile not in PROFILES:
        raise ValueError('Unknown display profile')
    text = menu.decode('utf-8')
    found = blocks(text)
    all_linux = re.findall(r'^\s*linux .+$', text, re.M)
    parsed_linux = []
    ids, modes, rescue, normal_ids = {}, set(), 0, set()
    for item in found:
        body = item[0]
        linux = re.findall(r'^\s*linux .+$', body, re.M)
        if not linux:
            continue
        parsed_linux += linux
        ident = re.search(r'--id ([\w-]+)', body)
        initrd = re.findall(r'^\s*initrd (.+)$', body, re.M)
        if len(linux) != 1 or not ident or ident[1] in ids or len(initrd) != 1:
            raise ValueError('Ambiguous boot entry')
        tokens = linux[0].split()
        if not {'suspend.screen_only=1', 'nohibernate'}.issubset(tokens):
            raise ValueError('Screen-only safety guard missing')
        flags = [t for t in tokens if t.startswith('sq2.display=')]
        if len(flags) != 1 or flags[0][12:] not in PROFILES:
            raise ValueError('Display selection missing/duplicated')
        mode = flags[0][12:]
        overrides = [t for t in tokens if t.startswith('drm.edid_firmware=')]
        paths = initrd[0].split()
        if mode == '5.4-65':
            if overrides != [OVERRIDE] or paths != [BOOT+BASE, BOOT+ADDON] or 'sq2.rescue=1' in tokens:
                raise ValueError('65Hz needs isolated addon/EDID; never rescue')
        elif overrides or paths != [BOOT+BASE]:
            raise ValueError('Stock/rescue must use stock module and physical EDID')
        if 'sq2.rescue=1' in tokens:
            rescue += 1
        else:
            modes.add(mode)
            normal_ids.add(ident[1])
        ids[ident[1]] = mode
    default = re.findall(r'^set default=(\S+)$', text, re.M)
    if (len(default) != 1 or ids.get(default[0]) != profile or default[0] not in normal_ids
            or rescue < 1 or modes != set(PROFILES)):
        raise ValueError('Both profiles, stock rescue and selected default required')
    if parsed_linux != all_linux:
        raise ValueError('Unparsed linux command')


def policy(menu, base, addon, profile):
    validate_menu(menu, profile)
    return dict(schema=1, default=profile, menu_sha256=sha(menu),
                stock_module_sha256=STOCK_SHA, edp65_module_sha256=MODULE_SHA,
                edp65_edid_sha256=EDID_SHA, password_addon_last=True,
                boot_payload_sha256={BASE: sha(base), ADDON: sha(addon)},
                hardware_boot_tested=False)
