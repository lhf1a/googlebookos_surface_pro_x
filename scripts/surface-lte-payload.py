"""Import only the audited LTE/Radio delta; never transplant storage or identity."""
import gzip
import hashlib
from pathlib import Path
import zlib

BASE_SHA = '957b5ee68d7a0d845638f7ebec9c0f624e0ae4184418bc8a12381628c720e452'
RADIO_SHA = '368d4f213d3f5bd6c8d6be159443c569cc82c846da2a88d111b5f416edcc4d05'
DT_SHA = '49228267f0f2fce8a54b87965f6bf83fa718dbe9bb68ea6da9a227b9e2f1210f'
LTE_COMMIT = '01004299d6b74d78eeb284a6b62b0e4184366ee2'
sha = lambda b: hashlib.sha256(b).hexdigest()
HOOKS = {
    'bin/wifi-services': [(b'done\nqrtr-ns -f ',
                          b'done\nlte-unit-seed || echo "LTE_UNIT_SEED_FAILED"\nqrtr-ns -f ')],
    'init': [
        (b'apply-device-info > /logs/device-info.txt 2>&1 || fail device-info\n',
         b'radio-stage > /logs/radio-stage.txt 2>&1 || fail radio-stage\n'
         b'apply-device-info > /logs/device-info.txt 2>&1 || fail device-info\n'),
        (b'    lockscreen-framework-boot > /logs/lockscreen-framework-boot.txt 2>&1 &\n',
         b'    lockscreen-framework-boot > /logs/lockscreen-framework-boot.txt 2>&1 &\n'
         b'    lte-framework-boot > /logs/lte-framework-boot.txt 2>&1 &\n')],
}
HELPERS = {'bin/lte-unit-seed', 'bin/lte-framework-boot', 'bin/radio-stage'}


def hooked(body, name):
    for old, new in HOOKS[name]:
        if body.count(old) != 1 or new in body:
            raise ValueError('Unexpected LTE hook site: ' + name)
        body = body.replace(old, new)
    return body


def prepare(base, root, bundle, c):
    """The private input is an exact copy of the reboot-verified Radio04, not a donor OS."""
    root, bundle = Path(root), Path(bundle)
    if sha(base) != BASE_SHA:
        raise ValueError('LTE requires the accepted complete 957b runtime')
    try:
        radio = (bundle / 'initramfs.cpio.gz').read_bytes()
        dt = (bundle / 'android-el2.dtb').read_bytes()
    except FileNotFoundError as e:
        raise ValueError('LTE defaults to on: supply private/lte-baseline with verified Radio04 '
                         'initramfs/DT, or explicitly select --lte off') from e
    if sha(radio) != RADIO_SHA or sha(dt) != DT_SHA:
        raise ValueError('Unverified LTE Radio04 payload/DT')
    before, after = c.entries(gzip.decompress(base)), c.entries(gzip.decompress(radio))
    changed = {n for n in before.keys() | after.keys() if before.get(n) != after.get(n)}
    allowed = lambda n: (n in HOOKS or n in HELPERS or n in ('etc/sq2-lte', 'etc/sq2-radio')
                         or n.startswith(('etc/sq2-lte/', 'etc/sq2-radio/')))
    if not changed or any(not allowed(n) or n not in after for n in changed):
        raise ValueError('LTE source changed an unrelated baseline file')
    for n in HOOKS:
        if after[n] != (before[n][0], hooked(before[n][1], n)):
            raise ValueError('LTE source has unrecognized init changes: ' + n)
    for n in HELPERS:
        source = (root / 'probe/lte' / n.split('/')[-1]).read_bytes().replace(b'\r\n', b'\n')
        if after[n][1] != source:
            raise ValueError('LTE helper source differs from verified Radio04: ' + n)
    delta = {n: after[n] for n in changed - HOOKS.keys()}
    guard = (root / 'probe/lte/lte-unit-guard').read_bytes().replace(b'\r\n', b'\n')
    delta['bin/lte-unit-guard'] = (0o100755, guard)
    # Gate before any telephony overlay/seed mutation, including on SQ1. The
    # validated helpers' own serial, ESP, EL2 and screen-only checks stay intact.
    for n in ('bin/radio-stage', 'bin/lte-unit-seed'):
        mode, body = delta[n]
        if body.count(b'set -eu\n') != 1:
            raise ValueError('Unexpected LTE guard insertion site')
        delta[n] = (mode, body.replace(b'set -eu\n', b'set -eu\nlte-unit-guard || exit 0\n'))
    return delta, dt, dict(source_commit=LTE_COMMIT, source_initramfs_sha256=RADIO_SHA,
                           source_dtb_sha256=DT_SHA, private_unit_fsg=True,
                           activation='SQ2 only; original unit serial and ESP marker must match',
                           sq1_lte_validated=False, deployed=False)


def integrate(raw, delta, c):
    before = c.entries(gzip.decompress(raw))
    if any(n in before for n in delta):
        raise ValueError('LTE overlay already present or collides with a baseline file')
    overlay = dict(delta)
    for n in HOOKS:
        overlay[n] = (before[n][0], hooked(before[n][1], n))
    expected = before | overlay
    # Last-member credentials are preserved verbatim; no SQ2 donor credentials
    # replace the SQ1/template's existing identity or storage profile.
    rest, credential = raw, None
    while rest:
        d = zlib.decompressobj(31)
        files = c.entries(d.decompress(rest))
        if not d.eof:
            raise ValueError('Truncated gzip member')
        member = rest[:len(rest) - len(d.unused_data)]
        if 'etc/shadow' in files and 'bin/dropbear' in files:
            credential = member
        rest = d.unused_data
    if credential is None:
        raise ValueError('Expected original credential member')
    archive = b''.join(c.entry(n, b, m) for n, (m, b) in sorted(
        overlay.items(), key=lambda x: (x[0].count('/'), x[0]))) + c.entry('TRAILER!!!', b'', 0)
    archive += b'\0' * (-len(archive) % 512)
    output = raw + gzip.compress(archive, mtime=0) + credential
    if c.entries(gzip.decompress(output)) != expected:
        raise ValueError('LTE last-entry-wins audit failed')
    return output, dict(changed=sorted(overlay), unrelated_entries_byte_identical=True,
                        original_credential_member_last=True, sha256=sha(output))
