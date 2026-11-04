"""Import only the audited LTE/Radio delta; never transplant storage or identity."""
import gzip
import hashlib
from pathlib import Path
import zlib

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
RETIRED_RUNTIME = {'bin/sq2-ipa-version-read', 'bin/lte-fs12-seed', 'bin/sq2-radio-query', 'bin/sq2-qmi-readonly', 'bin/sq2-mfs-readonly-lab', 'bin/sq2-pdc-own-profile-lab', 'bin/sq2-ipa-smmu-snapshot', 'bin/lte-fsg-seed', 'bin/sq2-ipa-iort-read', 'bin/sq2-lte-online-test', 'bin/test-lte-readonly', 'bin/sq2-ipa-gsi-state-read', 'bin/sq2-pdc-readonly'}
HELPERS = {'bin/lte-unit-seed', 'bin/lte-framework-boot', 'bin/radio-stage'}


def hooked(body, name):
    for old, new in HOOKS[name]:
        if body.count(old) != 1 or new in body:
            raise ValueError('Unexpected LTE hook site: ' + name)
        body = body.replace(old, new)
    if name == 'bin/wifi-services':
        body = body.replace(b'rmtfs -r -o /run/rmtfs -s -v ', b'rmtfs -r -o /run/rmtfs -s ')
        body = body.replace(b'rmtfs -r -o /run/rmtfs -v ', b'rmtfs -r -o /run/rmtfs ')
    return body


def prepare(base, root, bundle, c):
    import importlib.util
    root, bundle = Path(root), Path(bundle)
    spec=importlib.util.spec_from_file_location('lte_source',root/'scripts/build-lte-source.py')
    compiler=importlib.util.module_from_spec(spec);spec.loader.exec_module(compiler)
    out, config=compiler.build(root,bundle)
    delta={}
    def put(name,body,mode=0o100600):
        for parent in reversed(Path(name).parents):
            key=parent.as_posix()
            if key.startswith('etc/sq2-'):delta.setdefault(key,(0o40700,b''))
        delta[name]=(mode,body)
    for name in ('rmnet-create',):
        put('etc/sq2-lte/sq2-'+name,(out/('sq2-'+name)).read_bytes(),0o100700)
    for name in ('ipa_sq2_lab.ko','rmnet.ko'):put('etc/sq2-lte/'+name,(bundle/name).read_bytes())
    put('bin/sq2-fsg-cache',(out/'sq2-fsg-cache').read_bytes(),0o100755)
    put('etc/sq2-lte/firmware/qcom/sc8180x/sq2/ipa_fws.elf',(bundle/'ipa_fws.elf').read_bytes())
    runtime=['ipa_sq2_lab.ko','rmnet.ko','firmware/qcom/sc8180x/sq2/ipa_fws.elf','sq2-rmnet-create']
    put('etc/sq2-lte/runtime.sha256',''.join(sha(delta['etc/sq2-lte/'+n][1])+'  '+n+'\n' for n in runtime).encode())
    for name in ('sq2-radio','sq2-radio-overlay.apk'):put('etc/sq2-radio/'+name,(out/name).read_bytes())
    for name in ('sq2-qmi-data','sq2-qmi-mode'):put('etc/sq2-radio/'+name,(out/name).read_bytes())
    for name in ('sq2-radio.rc','sq2-radio-features.xml','sq2-radio-manifest.xml'):put('etc/sq2-radio/'+name,(root/'configs'/name).read_bytes().replace(b'\r\n',b'\n'))
    radiofiles={n.split('/')[-1]:body for n,(mode,body) in delta.items() if n.startswith('etc/sq2-radio/')}
    put('etc/sq2-radio/SHA256SUMS',''.join(sha(b)+'  '+n+'\n' for n,b in radiofiles.items()).encode())
    for name in ('lte-unit-seed','lte-framework-boot','radio-stage','lte-unit-guard'):
        body=(root/'probe/lte'/name).read_bytes().replace(b'\r\n',b'\n')
        if name in ('lte-unit-seed','radio-stage'):body=body.replace(b'set -eu\n',b'set -eu\nlte-unit-guard || exit 0\n',1)
        put('bin/'+name,body,0o100755)
    return delta,(bundle/'android-el2.dtb').read_bytes(),dict(source_built=True,private_unit_fsg=False,hardware_validated=False,activation='Generic runtime; EFI captures own-device FSG into a UUID-scoped local cache, seed consumes it in RAM. No embedded per-unit calibration.')


def integrate(raw, delta, c):
    before = c.entries(gzip.decompress(raw))
    if any(n in before for n in delta):
        raise ValueError('LTE overlay already present or collides with a baseline file')
    overlay = dict(delta)
    for n in HOOKS:
        overlay[n] = (before[n][0], hooked(before[n][1], n))
    # EL2 uses a separate RMTFS launcher; disable verbose there as well.
    if 'bin/el2-rmtfs-start' in before:
        mode, body = before['bin/el2-rmtfs-start']
        body = body.replace(b'rmtfs -r -o /run/rmtfs -s -v', b'rmtfs -r -o /run/rmtfs -s')
        body = body.replace(b'rmtfs -r -o /run/rmtfs -v', b'rmtfs -r -o /run/rmtfs')
        overlay['bin/el2-rmtfs-start'] = (mode, body)
    expected = {n: v for n, v in before.items() if n not in RETIRED_RUNTIME} | overlay
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
        expected.items(), key=lambda x: (x[0].count('/'), x[0]))) + c.entry('TRAILER!!!', b'', 0)
    archive += b'\0' * (-len(archive) % 512)
    output = gzip.compress(archive, mtime=0) + credential
    if c.entries(gzip.decompress(output)) != expected:
        raise ValueError('LTE last-entry-wins audit failed')
    return output, dict(changed=sorted(overlay), removed=sorted(set(before) & RETIRED_RUNTIME), unrelated_entries_byte_identical=True,
                        original_credential_member_last=True, sha256=sha(output))
