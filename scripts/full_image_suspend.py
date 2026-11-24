"""Default s2idle fix: build-time DEX patch and matched Venus module installation.

Android/private binaries stay outside Git. Source builds use freshly compiled
Venus; prebuilt builds require an explicitly Image-bound fixed module input.
No boot-time experiment, tracing, test mode or wakelock bypass is installed.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[1]
JAR = 'etc/s2idle-desktop-services.jar'
HOOK = (b'# Surface s2idle: bounded AC screen-off grace; app wakelocks unchanged.\n'
        b'mount --bind /etc/s2idle-desktop-services.jar /newroot/system/framework/desktop-services.jar || fail s2idle-framework-bind\n'
        b'/bin/surface-sleep-defaults > /logs/surface-sleep-defaults.txt 2>&1 &\n')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def dex_tool():
    spec = importlib.util.spec_from_file_location('screenoff', ROOT/'scripts/patch-surface-desktop-screenoff.py')
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


def validate_module(data):
    if data[:6] != b'\x7fELF\x02\x01' or int.from_bytes(data[18:20], 'little') != 183:
        raise ValueError('Suspend module must be an AArch64 ELF')
    for symbol in (b'venus_hfi_prepare_system_stop\0', b'stop:prepare-pc\0',
                   b'sq2_pm_stream_reuses\0', b'vermagic=6.18.60-sq2-android1 '):
        if symbol not in data:
            raise ValueError('Venus module lacks accepted sleep fix/ABI: '+repr(symbol))


def load(inputs, image, compiled=None, desktop_services_jar=None):
    inputs = Path(inputs)
    jar_src = Path(desktop_services_jar) if desktop_services_jar is not None else (inputs/'desktop-services.jar')
    with tempfile.TemporaryDirectory() as tmp:
        output = Path(tmp)/'desktop-services.jar'
        dex_tool().patch(jar_src, output)
        jar = output.read_bytes()
    if compiled is not None:
        candidates = list((Path(compiled)/'modules-stage/lib/modules').rglob('venus-core.ko'))
        if len(candidates) != 1:
            raise ValueError('One freshly compiled Venus core is required')
        module = candidates[0].read_bytes()
    else:
        manifest = json.loads((inputs/'prebuilt.json').read_text())
        if manifest['kernel_image_sha256'] != sha(Path(image).read_bytes()):
            raise ValueError('Prebuilt sleep module is not paired with this Image; use source build')
        module = (inputs/'venus-core.ko').read_bytes()
        if manifest['module_sha256'] != sha(module):
            raise ValueError('Prebuilt sleep module checksum mismatch')
    validate_module(module)
    return {'jar': jar, 'module': module}


def install(records, bodies, files, new_record):
    validate_module(files['module'])
    names = [n for n in bodies if n.endswith('/venus-core.ko')]
    if not names:
        raise ValueError('Runtime Venus core missing')
    for name in names:
        bodies[name] = files['module']
    bodies[JAR] = files['jar']
    records[JAR] = new_record(0o100644)
    helper = 'bin/surface-sleep-defaults'
    bodies[helper] = (ROOT/'probe/test2/surface-sleep-defaults').read_bytes().replace(b'\r\n', b'\n')
    records[helper] = new_record(0o100755)
    init = bodies['init']
    anchor = b'exec native-handoff /newroot\n'
    if init.count(anchor) != 1:
        raise ValueError('Unknown Android handoff for suspend framework bind')
    if HOOK not in init:
        if b's2idle-desktop-services.jar' in init:
            raise ValueError('Remove experimental sleep hooks before preparing default runtime')
        init = init.replace(anchor, HOOK+anchor)
    bodies['init'] = init
    return {'enabled': True, 'module_sha256': sha(files['module']),
            'framework_sha256': sha(files['jar']), 'ac_screen_off_grace_ms': 1000,
            'module_paths': sorted(names)}


def main():
    p = argparse.ArgumentParser(description='Stage reusable private sleep inputs (never deploys)')
    p.add_argument('--desktop-services', type=Path, required=True,
                   help='Original desktop-services.jar from the Android system image')
    p.add_argument('--venus-core', type=Path, help='Optional already-fixed prebuilt module')
    p.add_argument('--image', type=Path, help='Exact paired kernel Image for --venus-core')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if bool(a.venus_core) != bool(a.image):
        p.error('--venus-core and --image must be supplied together')
    dex_tool().inspect(a.desktop_services)
    if a.venus_core:
        validate_module(a.venus_core.read_bytes())
    a.output.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(a.desktop_services, a.output/'desktop-services.jar')
    if a.venus_core:
        shutil.copyfile(a.venus_core, a.output/'venus-core.ko')
        (a.output/'prebuilt.json').write_text(json.dumps({
            'kernel_image_sha256': sha(a.image.read_bytes()),
            'module_sha256': sha(a.venus_core.read_bytes())}, indent=2)+'\n')


if __name__ == '__main__':
    main()
