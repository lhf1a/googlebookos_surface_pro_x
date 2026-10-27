#!/usr/bin/env python3
"""Extract only software security donors, without mounting or modifying images."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zipfile
import argparse

ROOT = Path(__file__).resolve().parents[1]
spec = json.loads((ROOT / 'configs/sources.lock.json').read_text())['cuttlefish_security_donor']
archive = ROOT / 'downloads' / ('cuttlefish-' + spec['build'] + '.zip')
args = argparse.ArgumentParser()
args.add_argument('--graphics', action='store_true', help='Extract graphics HAL donors instead of security components')
args.add_argument('--audio', action='store_true', help='Extract the official virtual audio HAL and matching configuration')
args.add_argument('--connectivity', action='store_true', help='Extract Bluetooth serial HAL and Wi-Fi donors for hardware adaptation')
options = args.parse_args()
assert sum((options.graphics,options.audio,options.connectivity))<=1
out = ROOT / ('out/cuttlefish-connectivity' if options.connectivity else 'out/cuttlefish-audio' if options.audio else 'out/cuttlefish-graphics' if options.graphics else 'out/cuttlefish-security')
cache = Path('/root/.cache/sq2-probe-build')
with archive.open('rb') as f:
    assert hashlib.file_digest(f, 'sha256').hexdigest() == spec['zip_sha256']
loader = importlib.util.spec_from_file_location('donor_unpack', ROOT / 'upstream/gbos-vm/tools/unpack_cuttlefish.py')
donor = importlib.util.module_from_spec(loader)
loader.loader.exec_module(donor)
u32 = lambda data, off: struct.unpack_from('<I', data, off)[0]
out.mkdir(parents=True, exist_ok=True)
results = {}
with tempfile.TemporaryDirectory(prefix='security-donor.', dir=cache) as name:
    tmp = Path(name)
    with zipfile.ZipFile(archive) as z:
        with z.open('super.img') as src, (tmp / 'super.img').open('xb') as dst:
            shutil.copyfileobj(src, dst, 8 << 20)
    raw = tmp / 'super.raw'
    donor.unsparse(tmp / 'super.img', raw)
    with raw.open('rb') as f:
        f.seek(4096)
        geometry = f.read(4096)
        assert u32(geometry, 0) == 0x616c4467
        g = bytearray(geometry[:u32(geometry, 4)])
        g[8:40] = bytes(32)
        assert hashlib.sha256(g).digest() == geometry[8:40]
        f.seek(12288)
        metadata = f.read(u32(geometry, 40))
        assert u32(metadata, 0) == 0x414c5030
        size = u32(metadata, 8)
        header = bytearray(metadata[:size])
        header[12:44] = bytes(32)
        assert hashlib.sha256(header).digest() == metadata[12:44]
        tables = metadata[size:size + u32(metadata, 44)]
        assert hashlib.sha256(tables).digest() == metadata[48:80]
        po, pn, ps = struct.unpack_from('<III', metadata, 80)
        eo, en, es = struct.unpack_from('<III', metadata, 92)
        matches = [tables[po + i * ps:po + (i + 1) * ps] for i in range(pn)
                   if tables[po + i * ps:po + i * ps + 36].split(b'\0')[0] == b'vendor_a']
        assert len(matches) == 1
        first, count = struct.unpack_from('<II', matches[0], 40)
        assert first + count <= en and count
        with (tmp / 'vendor.img').open('xb') as dst:
            for i in range(first, first + count):
                sectors, kind, sector, device = struct.unpack_from('<QIQI', tables, eo + i * es)
                assert kind == 0 and device == 0
                assert (sector + sectors) * 512 <= raw.stat().st_size
                f.seek(sector * 512)
                left = sectors * 512
                while left:
                    chunk = f.read(min(left, 8 << 20))
                    assert chunk
                    dst.write(chunk)
                    left -= len(chunk)
    subprocess.run(['fsck.erofs', '--no-preserve', '--extract=' + str(tmp / 'vendor'), str(tmp / 'vendor.img')], check=True)
    if options.connectivity:
        manifest={'source':spec,'files':{},'inventory':[]}
        vendor=tmp/'vendor'
        for p in sorted(vendor.rglob('*')):
            if not p.is_file(): continue
            rel=str(p.relative_to(vendor))
            if not any(word in p.name.lower() for word in ('wifi','bluetooth','supplicant','cf.bt')): continue
            manifest['inventory'].append(rel)
            dest=out/rel;dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(p,dest)
            manifest['files'][rel]={'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
        apex=vendor/'apex/com.google.cf.bt.apex'
        with zipfile.ZipFile(apex) as z: payload=z.read('apex_payload.img')
        image=tmp/'bt.img';image.write_bytes(payload)
        name='bin/hw/android.hardware.bluetooth-service.cuttlefish'
        result=subprocess.run(['debugfs','-R','cat /'+name,str(image)],capture_output=True,check=True)
        assert result.stdout.startswith(b'\x7fELF')
        dest=out/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(result.stdout)
        manifest['files'][name]={'bytes':len(result.stdout),'sha256':hashlib.sha256(result.stdout).hexdigest()}
        (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        print('\n'.join(manifest['inventory']))
        print('Extracted connectivity donors; real hardware compatibility still requires adaptation')
        raise SystemExit(0)
    if options.audio:
        paths = ['apex/com.android.hardware.audio.apex']
        paths += ['etc/' + n for n in (
            'audio_effects.xml', 'audio_effects_config.xml', 'audio_policy_configuration.xml',
            'audio_policy_volumes.xml', 'default_volume_tables.xml',
            'bluetooth_with_le_audio_policy_configuration_7_0.xml', 'primary_audio_policy_configuration.xml',
            'r_submix_audio_policy_configuration.xml', 'surround_sound_configuration_5_0.xml',
            'usb_audio_policy_configuration.xml')]
        manifest = {'source': spec, 'files': {}}
        for rel in paths:
            content = (tmp / 'vendor' / rel).read_bytes()
            dest = out / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(content)
            manifest['files'][rel] = {'bytes': len(content), 'sha256': hashlib.sha256(content).hexdigest()}
        (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(f'Extracted {len(paths)} virtual audio components; physical SQ2 audio is NOT implemented')
        raise SystemExit(0)
    if options.graphics:
        paths = [
            'lib64/libminigbm_gralloc.so', 'lib64/libminigbm_gralloc4_utils.so',
            'lib64/android.hardware.graphics.allocator-V3-ndk.so',
            'lib64/android.hardware.graphics.common-V7-ndk.so',
            'lib64/hw/mapper.minigbm.so', 'lib64/hw/gralloc.default.so',
            'bin/hw/android.hardware.graphics.allocator-service.minigbm',
            'bin/hw/android.hardware.composer.hwc3-service.drm',
            'lib64/drm_hwcomposer_atom_reporter.so',
        ]
        manifest = {'source': spec, 'files': {}}
        payloads = {}
        for rel in paths:
            donor_entry = next(v for k, v in donor.FILES.items()
                               if k.startswith('graphics/') and Path(k).name == Path(rel).name)
            container, inner = donor_entry
            if container == 'vendor':
                content = (tmp / 'vendor' / inner.lstrip('/')).read_bytes()
            else:
                apex_name = container.split(':', 1)[1]
                if apex_name not in payloads:
                    with zipfile.ZipFile(tmp / 'vendor/apex' / apex_name) as z:
                        payload = z.read('apex_payload.img')
                    payload_path = tmp / (apex_name + '.img')
                    payload_path.write_bytes(payload)
                    assert payload[1080:1082] == b'\x53\xef', 'Expected ext4 graphics APEX'
                    payloads[apex_name] = payload_path
                result = subprocess.run(['debugfs', '-R', 'cat ' + inner, str(payloads[apex_name])], capture_output=True, check=True)
                assert result.stdout and b'File not found' not in result.stderr
                content = result.stdout
            dest = out / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(content)
            manifest['files'][rel] = {'bytes': len(content), 'sha256': hashlib.sha256(content).hexdigest()}
        (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(f'Extracted {len(paths)} graphics donors to {out}; backend compatibility is NOT yet verified')
        raise SystemExit(0)
    apex = tmp / 'vendor/apex'
    with zipfile.ZipFile(apex / 'com.android.hardware.boot.apex') as z:
        boot_payload = z.read('apex_payload.img')
    boot_image = tmp / 'boot.img'
    boot_image.write_bytes(boot_payload)
    result = subprocess.run(['debugfs', '-R', 'cat /bin/hw/android.hardware.boot-service.default', str(boot_image)], capture_output=True, check=True)
    assert result.stdout.startswith(b'\x7fELF') and b'File not found' not in result.stderr
    results['boot-service.default'] = result.stdout
    keymint = apex / 'com.android.hardware.keymint.rust_nonsecure.apex'
    with zipfile.ZipFile(keymint) as z:
        payload = z.read('apex_payload.img')
    image = tmp / 'keymint.img'
    image.write_bytes(payload)
    assert payload[1080:1082] == b'\x53\xef', 'Expected ext4 KeyMint payload'
    paths = {
        'android.hardware.security.keymint-service': '/bin/hw/android.hardware.security.keymint-service.nonsecure',
        'libcrypto.so': '/lib64/libcrypto.so',
    }
    for component in ('keymint', 'secureclock', 'sharedsecret'):
        name = f'android.hardware.security.{component}-service.xml'
        paths[name] = '/etc/vintf/' + name
    for target, path in paths.items():
        result = subprocess.run(['debugfs', '-R', 'cat ' + path, str(image)], capture_output=True, check=True)
        assert result.stdout and b'File not found' not in result.stderr
        results[target] = result.stdout
    results['com.android.hardware.gatekeeper.nonsecure.apex'] = (apex / 'com.android.hardware.gatekeeper.nonsecure.apex').read_bytes()
    results['keymint-source.apex'] = keymint.read_bytes()
manifest = {'source': spec, 'files': {}}
for name, content in results.items():
    (out / name).write_bytes(content)
    manifest['files'][name] = {'bytes': len(content), 'sha256': hashlib.sha256(content).hexdigest()}
(out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(f'Extracted {len(results)} verified security components to {out}')
