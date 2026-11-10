"""Fresh-image user-0 policy; preserve original initramfs members and identities."""
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[1]
ANCHOR = b'exec native-handoff /newroot\n'
HOOK = b'apply-single-user /newroot || fail single-user-policy\n'


def cpio_module():
    spec = importlib.util.spec_from_file_location('single_user_cpio', ROOT / 'scripts/pack-sq2-vulkan.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def integrate(base, expected):
    digest = lambda data: hashlib.sha256(data).hexdigest()
    if digest(base) != expected:
        raise ValueError('Parent SHA256 mismatch')
    cpio = cpio_module()
    before = cpio.entries(gzip.decompress(base))
    mode, init = before['init']
    if init.count(ANCHOR) != 1 or init.count(HOOK) > 1:
        raise ValueError('Expected one native Android init handoff')
    # Retire the lab's forced user selection in this fresh-image variant only.
    # No switch-user 0 on existing data: Android initializes full user 0 itself.
    init = re.sub(rb'(?m)^(\s*el2-terminal-autostart)[ \t]+[0-9]+(?=[ \t]*(?:>|&|\n|$))', rb'\1', init)
    for line in init.splitlines():
        if b'el2-terminal-autostart' in line and not line.lstrip().startswith(b'#'):
            if not re.fullmatch(rb'[ \t]*el2-terminal-autostart[ \t]*(?:>[^\n]*|&)?', line):
                raise ValueError('Unknown Terminal user selection; refusing partial single-user policy')
    if HOOK not in init:
        init = init.replace(ANCHOR, HOOK + ANCHOR)
    helper = (ROOT / 'probe/test2/apply-single-user').read_bytes().replace(b'\r\n', b'\n')
    delta = {'init': (mode, init), 'bin/apply-single-user': (0o100755, helper)}
    for _, body in delta.values():
        subprocess.run(['sh', '-n'], input=body, check=True)
    # Insert before the final private identity member, if present. Keep all
    # original compressed members byte-exact; anonymous inputs work as well.
    rest, offset, insert = base, 0, len(base)
    identity_last = False
    while rest:
        decoder = zlib.decompressobj(31)
        payload = decoder.decompress(rest)
        if not decoder.eof:
            raise ValueError('Truncated gzip member')
        size = len(rest) - len(decoder.unused_data)
        if not decoder.unused_data:
            last = cpio.entries(payload)
            identity_last = {'etc/shadow', 'bin/dropbear'} <= last.keys()
            if identity_last:
                if last.keys() & delta.keys():
                    raise ValueError('Identity member overrides user policy')
                insert = offset
        offset += size
        rest = decoder.unused_data
    archive = b''.join(cpio.entry(n, b, m) for n, (m, b) in delta.items())
    archive += cpio.entry('TRAILER!!!', b'', 0)
    archive += b'\0' * (-len(archive) % 512)
    result = base[:insert] + gzip.compress(archive, mtime=0) + base[insert:]
    after = cpio.entries(gzip.decompress(result))
    expected_entries = dict(before, **delta)
    if after != expected_entries:
        raise ValueError('Unexpected effective initramfs changes')
    return result, {
        'operation': 'fresh-image-single-user-candidate',
        'parent_sha256': expected, 'sha256': digest(result),
        'changed': sorted(n for n in after if before.get(n) != after[n]),
        'android_user_id': 0, 'headless_system_user': False, 'max_users': 1,
        'app_data': ['/data/user/0/<PACKAGE>', '/data/data/<PACKAGE>'],
        'existing_userdata_migrated': False, 'userdata_written': False,
        'original_members_byte_identical': True, 'private_identity_present': identity_last,
        'credential_member_last': identity_last, 'anonymous_public_image': False,
        'device_written': False, 'hardware_boot_tested': False,
        'scope': 'Fresh userdata only; not a migration or a complete disk image. App sandbox permissions remain unchanged.',
    }


def build(base, output, expected):
    base, output = Path(base), Path(output)
    audit = output.with_suffix('.audit.json')
    if output.exists() or output.is_symlink() or audit.exists() or audit.is_symlink() or base.resolve() == output.resolve():
        raise ValueError('Candidate and audit outputs must be fresh, separate from parent')
    result, report = integrate(base.read_bytes(), expected)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('xb') as stream:
        stream.write(result)
    if output.read_bytes() != result:
        raise ValueError('Candidate readback mismatch')
    with audit.open('x') as stream:
        stream.write(json.dumps(report, indent=2) + '\n')
    return report
