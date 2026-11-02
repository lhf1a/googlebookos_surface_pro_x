"""Default-build USB network integration from the pinned, device-tested bundle.

No device access. Changing the kernel/module set requires a new export audit and
explicit pins, not a same-vermagic assumption. Helpers are refreshed from source.
"""
import hashlib
import importlib.util
import json
import re
from pathlib import Path

PREFIX = 'lib/modules/6.3.8-sq2-android1/'
HELPERS = ('usb-input-watch', 'usb-network-watch')
sha = lambda b: hashlib.sha256(b).hexdigest()


def pins(root):
    return json.loads((root/'configs/sq2-usb-network.json').read_text())


def semantic_digest(entries):
    """Only the existing explicit lab-user argument may vary in the parent."""
    h = hashlib.sha256()
    for n,(mode,body) in sorted(entries.items()):
        if n == 'init':
            body, count = re.subn(
                rb'(^[ \t]*el2-terminal-autostart)(?: [0-9]+)?( > /logs/el2-terminal-autostart.txt 2>&1 &\n)',
                rb'\1 @USER@\2', body, flags=re.M)
            if count != 1:
                raise ValueError('Unknown normal boot user hook')
        h.update(json.dumps([n,mode,sha(body)],separators=(',',':')).encode()+b'\n')
    return h.hexdigest()


def validate_bundle(root, bundle, kernel_sha256):
    policy = pins(root)
    if policy['schema'] != 1 or kernel_sha256 != policy['kernel_image_sha256']:
        raise ValueError('USB network bundle requires its tested kernel Image')
    audit = json.loads((bundle/'module-audit.json').read_text())
    actual = {n:v['sha256'] for n,v in audit['new_modules'].items()}
    if (audit['missing'] or actual != policy['modules']
            or audit['base_sha256'] != policy['baseline_payload_sha256']
            or audit['reference_vermagic'] != policy['reference_vermagic']):
        raise ValueError('USB network module/export audit differs from tested pins')
    stage = bundle/'module-stage'/PREFIX
    for n,digest in policy['modules'].items():
        if sha((stage/n).read_bytes()) != digest:
            raise ValueError('USB network module differs from tested pins: '+n)
    for n,digest in policy['metadata'].items():
        if sha((stage/n).read_bytes()) != digest:
            raise ValueError('USB network index differs from tested pins: '+n)
    return policy


def verify(entries, root):
    policy = pins(root)
    for n,digest in policy['modules'].items():
        if sha(entries[PREFIX+n][1]) != digest:
            raise ValueError('Default archive USB module mismatch: '+n)
    for n,digest in policy['metadata'].items():
        if sha(entries[PREFIX+n][1]) != digest:
            raise ValueError('Default archive USB index mismatch: '+n)
    if sha(entries['etc/sq2-usb-network.aliases'][1]) != policy['aliases_sha256']:
        raise ValueError('Default archive USB whitelist mismatch')
    source_hashes = {}
    init = entries['init'][1]
    for n in HELPERS:
        source = (root/'probe/test2'/n).read_bytes().replace(b'\r\n',b'\n')
        if entries['bin/'+n] != (0o100755, source):
            raise ValueError('Default archive USB helper mismatch: '+n)
        if init.count((n+' > /logs/'+n+'.txt').encode()) != 1:
            raise ValueError('Default archive USB startup hook mismatch: '+n)
        source_hashes[n] = sha(source)
    return source_hashes


def integrate(root, raw, bundle, kernel_sha256):
    policy = validate_bundle(root, bundle, kernel_sha256)
    spec = importlib.util.spec_from_file_location('usb_network_pack', root/'scripts/pack-sq2-usb-network.py')
    pack = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pack)
    before = pack.c.entries(pack.gzip.decompress(raw))
    if semantic_digest(before) != policy['baseline_semantic_sha256']:
        raise ValueError('Default runtime changed: re-audit USB against the new baseline')
    result, report = pack.build(raw, bundle, sha(raw),
                                audit_base_sha256=policy['baseline_payload_sha256'])
    entries = pack.c.entries(pack.gzip.decompress(result))
    report.update(enabled=True, source_sha256=verify(entries, root),
                  kernel_image_sha256=kernel_sha256)
    return result, report
