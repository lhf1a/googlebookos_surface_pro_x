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
    import importlib.util
    root, bundle = Path(root), Path(bundle)
    spec=importlib.util.spec_from_file_location('lte_source',root/'scripts/build-lte-source.py')
    compiler=importlib.util.module_from_spec(spec);spec.loader.exec_module(compiler)
    out, config=compiler.build(root,bundle)
    fsg=(bundle/'modem_fsg.bin').read_bytes()
    if len(fsg)!=2097152 or not any(fsg):raise ValueError('FSG must be a nonempty 2MiB own-device collection')
    delta={}
    def put(name,body,mode=0o100600):
        for parent in reversed(Path(name).parents):
            key=parent.as_posix()
            if key.startswith('etc/sq2-'):delta.setdefault(key,(0o40700,b''))
        delta[name]=(mode,body)
    for name in ('qmi-data','qmi-mode','rmnet-create'):
        put('etc/sq2-lte/sq2-'+name,(out/('sq2-'+name)).read_bytes(),0o100700)
    put('etc/sq2-lte/sq2-lte.jar',(out/'sq2-lte.jar').read_bytes())
    for name in ('ipa_sq2_lab.ko','rmnet.ko','modem_fsg.bin'):put('etc/sq2-lte/'+name,(bundle/name).read_bytes())
    put('etc/sq2-lte/firmware/qcom/sc8180x/sq2/ipa_fws.elf',(bundle/'ipa_fws.elf').read_bytes())
    put('etc/sq2-lte/unit-serial',(config.get('unit_serial','')+'\n').encode())
    put('etc/sq2-lte/apn',(config.get('apn','')+'\n').encode())
    put('etc/sq2-lte/FSG.sha256',(sha(fsg)+'  modem_fsg.bin\n').encode())
    runtime=['ipa_sq2_lab.ko','rmnet.ko','firmware/qcom/sc8180x/sq2/ipa_fws.elf','sq2-qmi-data','sq2-qmi-mode','sq2-rmnet-create','sq2-lte.jar']
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
    return delta,(bundle/'android-el2.dtb').read_bytes(),dict(source_built=True,private_unit_fsg=True,unit_configured=bool(config.get('unit_serial')),hardware_validated=False,activation='SQ1/SQ2 shared LTE; no NVMe serial/ESP binding; operator-supplied FSG staged only in RAM')


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
