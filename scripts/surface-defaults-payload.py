"""Shared UI/KSU/swap userspace defaults. Does not replace kernel, modules or userdata."""
from pathlib import Path
import gzip
import zlib

FILES = ('ksud', 'manager-verified.apk')
HOOK = b'    ksu-framework-boot > /logs/ksu-framework-boot.txt 2>&1 &\n'
SWAP_HOOK = b'    surface-swap-boot > /logs/surface-swap-boot.txt 2>&1 &\n'
ANCHOR = b'    lockscreen-framework-boot > /logs/lockscreen-framework-boot.txt 2>&1 &\n'


def integrate(raw, root, bundle, c, dpi=300, ksu=True):
    if not isinstance(dpi, int) or not 72 <= dpi <= 1000:
        raise ValueError('DPI must be an integer between 72 and 1000')
    root, bundle = Path(root), Path(bundle)
    before = c.entries(gzip.decompress(raw))
    e = dict(before)
    helper = (root/'probe/test2/apply-device-info').read_bytes().replace(b'\r\n', b'\n')
    if helper.count(b'density=${2:-300}') != 1:
        raise ValueError('Unexpected density helper')
    helper = helper.replace(b'density=${2:-300}', ('density=${2:-%d}' % dpi).encode())
    e['bin/apply-device-info'] = (0o100755, helper)
    allowed = {'bin/apply-device-info', 'bin/surface-swap-boot', 'init'}
    e['bin/surface-swap-boot'] = (0o100755,
        (root/'probe/test2/surface-swap-boot').read_bytes().replace(b'\r\n', b'\n'))
    init = e['init'][1]
    if init.count(ANCHOR) != 1 or init.count(SWAP_HOOK) > 1:
        raise ValueError('Unexpected swap asynchronous boot anchor')
    if SWAP_HOOK not in init:
        init = init.replace(ANCHOR, ANCHOR+SWAP_HOOK)
    e['init'] = (e['init'][0], init)
    if ksu:
        for name in FILES:
            b = (bundle/name).read_bytes()
            if not b:
                raise ValueError('KSU input empty: '+name)
            e['etc/sq2-ksu/'+name] = (0o100644 if name.endswith('.apk') else 0o100755, b)
        e['etc/sq2-ksu'] = (0o40755, b'')
        e['bin/ksu-framework-boot'] = (0o100755,
            (root/'probe/test2/ksu-framework-boot').read_bytes().replace(b'\r\n', b'\n'))
        init = e['init'][1]
        if init.count(ANCHOR) != 1 or init.count(HOOK) > 1:
            raise ValueError('Unexpected KSU asynchronous boot anchor')
        if HOOK not in init:
            init = init.replace(ANCHOR, ANCHOR+HOOK)
        e['init'] = (e['init'][0], init)
        allowed |= {'init', 'etc/sq2-ksu', 'bin/ksu-framework-boot'} | {'etc/sq2-ksu/'+n for n in FILES}
    # Preserve the original private credential addon as the exact final member.
    rest, identity = raw, None
    while rest:
        d = zlib.decompressobj(31)
        entries = c.entries(d.decompress(rest))
        if not d.eof:
            raise ValueError('Incomplete gzip member')
        if 'etc/shadow' in entries and 'bin/dropbear' in entries:
            identity = rest[:len(rest)-len(d.unused_data)]
        rest = d.unused_data
    if identity is None:
        raise ValueError('Credential member absent')
    body = b''.join(c.entry(n, b, m) for n, (m, b) in
                    sorted(e.items(), key=lambda x: (x[0].count('/'), x[0])))
    body += c.entry('TRAILER!!!', b'', 0)
    body += b'\0' * (-len(body) % 512)
    packed = gzip.compress(body, compresslevel=3, mtime=0)+identity
    if c.entries(gzip.decompress(packed)) != e:
        raise ValueError('Defaults last-entry audit failed')
    changed = sorted(n for n in e if e[n] != before.get(n))
    if not set(changed) <= allowed:
        raise ValueError('Unrelated baseline change')
    return packed, dict(dpi=dpi, ksu_manager=ksu, swap_below_12g_gib=6, swap_threshold_kib=12582912, changed=changed,
                        credential_member_last=True, unrelated_entries_byte_identical=True,
                        kernel_replaced=False, userdata_overrides_preserved=True)
