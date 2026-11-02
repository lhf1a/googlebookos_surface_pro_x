#!/usr/bin/env python3
"""Add an audited USB network module bundle and hotplug helpers to a lab image.

No disk access. Preserve every unrelated effective entry and the final private
SSH member. Module audit must be made against the exact running kernel exports.
"""
import argparse, gzip, hashlib, importlib.util, json, subprocess, zlib
from pathlib import Path

R = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('cpio', R/'scripts/pack-sq2-vulkan.py')
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)
sha = lambda b: hashlib.sha256(b).hexdigest()


def build(base, bundle, expected, *, audit_base_sha256=None):
    if sha(base) != expected:
        raise ValueError('Base hash mismatch')
    before = c.entries(gzip.decompress(base))
    audit = json.loads((bundle/'module-audit.json').read_text())
    if audit.get('base_sha256') != (audit_base_sha256 or expected):
        raise ValueError('Module audit belongs to a different baseline')
    if audit['missing'] or not audit['new_modules']:
        raise ValueError('Missing runtime exports / empty module set')
    prefix = 'lib/modules/6.3.8-sq2-android1/'
    stage = bundle/'module-stage'
    delta, aliases = {}, []
    for n, info in audit['new_modules'].items():
        path = prefix+n
        if path in before:
            raise ValueError('Refusing to replace an existing module: '+path)
        b = (stage/path).read_bytes()
        if sha(b) != info['sha256']:
            raise ValueError('Module hash mismatch: '+n)
        delta[path] = (0o100644, b)
        for alias in subprocess.check_output(['modinfo', '-F', 'alias', str(stage/path)], text=True).splitlines():
            if alias.startswith('usb:'):
                aliases.append(alias+' '+Path(n).stem)
    for p in (stage/prefix).glob('modules.*'):
        if p.name not in ('modules.builtin', 'modules.builtin.modinfo', 'modules.order', 'modules.dep.bb'):
            delta[prefix+p.name] = (0o100644, p.read_bytes())
    delta['etc/sq2-usb-network.aliases'] = (0o100644, ('\n'.join(sorted(set(aliases)))+'\n').encode())
    init = before['init'][1]
    anchor = b'    lab-input-posture > /logs/lab-input-posture.txt 2>&1 &'
    if init.count(anchor) != 1 or b'\n        usb-input-watch >' in init:
        raise ValueError('Unknown/already patched init')
    block = b''
    for name in ('usb-input-watch', 'usb-network-watch'):
        body = (R/'probe/test2'/name).read_bytes().replace(b'\r\n', b'\n')
        subprocess.run(['sh', '-n'], input=body, check=True)
        delta['bin/'+name] = (0o100755, body)
        block += f'    if [ -x /bin/{name} ]; then\n        {name} > /logs/{name}.txt 2>&1 &\n    fi\n'.encode()
    init = init.replace(anchor, block+anchor)
    # BusyBox small modprobe caches are specific to the old modules.dep.
    anchor = b'modprobe af_key'
    if init.count(anchor) != 1:
        raise ValueError('Unknown first module hook')
    init = init.replace(anchor, b'rm -f /lib/modules/"$(uname -r)"/modules.dep.bb\n'+anchor)
    subprocess.run(['sh', '-n'], input=init, check=True)
    delta['init'] = (0o100755, init)
    # Explicit directory entries: never rely on cpio creating absent parents.
    for n in list(delta):
        for parent in Path(n).parents:
            p = parent.as_posix()
            if p != '.' and p not in before and p not in delta:
                delta[p] = (0o40755, b'')
    rest, last, creds = base, None, None
    while rest:
        dec = zlib.decompressobj(31)
        files = c.entries(dec.decompress(rest))
        if not dec.eof:
            raise ValueError('Truncated gzip member')
        last = rest[:len(rest)-len(dec.unused_data)]
        if 'etc/shadow' in files and 'bin/dropbear' in files:
            creds = last
        rest = dec.unused_data
    if creds is None or last != creds:
        raise ValueError('Expected original private SSH member last')
    archive = b''.join(c.entry(n, b, mode) for n,(mode,b) in sorted(delta.items(), key=lambda x:(x[0].count('/'),x[0])))
    archive += c.entry('TRAILER!!!', b'', 0)
    archive += b'\0' * (-len(archive) % 512)
    result = base+gzip.compress(archive, mtime=0)+creds
    after = c.entries(gzip.decompress(result))
    changed = {n for n in before.keys()|after.keys() if before.get(n) != after.get(n)}
    if changed != {n for n in delta if before.get(n) != delta[n]}:
        raise ValueError('Unexpected effective delta')
    return result, dict(base_sha256=sha(base), output_sha256=sha(result),
                        module_count=len(audit['new_modules']), usb_alias_count=len(set(aliases)),
                        changed=sorted(changed), password_member_last=True,
                        reference_vermagic=audit['reference_vermagic'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('base', type=Path)
    p.add_argument('bundle', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--expected-base-sha256', required=True)
    a = p.parse_args()
    data, audit = build(a.base.read_bytes(), a.bundle, a.expected_base_sha256)
    with a.output.open('xb') as f:
        f.write(data)
    if a.output.read_bytes() != data:
        raise ValueError('Write/readback mismatch')
    a.output.with_suffix('.audit.json').write_text(json.dumps(audit,indent=2)+'\n')
    print(json.dumps(audit,indent=2))


if __name__ == '__main__':
    main()
