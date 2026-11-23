#!/usr/bin/env python3
"""Verify/import a Windows dependency package on Linux; never write a device.

Raw downloads and OEM assets are NOT an already prepared full-image input tree.
The report lists the distinction; --build checks the real builder inputs first.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path, algorithm='sha256'):
    h = hashlib.new(algorithm)
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(8 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def safe_name(name):
    if '\\' in name or ':' in name or '\x00' in name:
        raise ValueError('Unsafe member name: ' + repr(name))
    p = PurePosixPath(name)
    if p.is_absolute() or '..' in p.parts:
        raise ValueError('Unsafe member name: ' + repr(name))
    return str(p)


def import_bundle(archive, output, expected_sha256):
    if output.exists():
        raise ValueError('Import output must be fresh')
    if digest(archive) != expected_sha256.lower():
        raise ValueError('Package SHA256 mismatch')
    with tarfile.open(archive, 'r:*') as tar:
        files = {}
        names = set()
        for member in tar.getmembers():
            name = safe_name(member.name)
            if member.isdir():
                continue
            if not member.isfile():
                raise ValueError('Only regular files/directories allowed: ' + name)
            if name.casefold() in names:
                raise ValueError('Duplicate member: ' + name)
            names.add(name.casefold())
            files[name] = member
        manifest_member = files.get('MANIFEST.json')
        if not manifest_member or manifest_member.size > 16 << 20:
            raise ValueError('Missing or oversized MANIFEST.json')
        report = json.load(tar.extractfile(manifest_member))
        if report.get('schema') != 1 or report.get('product') != 'hybrid-sq1-sq2-6.18':
            raise ValueError('Unsupported dependency package')
        expected = {}
        for row in report['files']:
            name = safe_name(row['path'])
            if name in expected or name == 'MANIFEST.json':
                raise ValueError('Duplicate/self-referencing manifest entry')
            expected[name] = row
        if set(files) != set(expected) | {'MANIFEST.json'}:
            raise ValueError('Manifest inventory mismatch (extra or missing files)')
        # No extractall: ignore archive ownership, modes, links and devices.
        # Verify all members before creating the destination.
        for name, row in expected.items():
            member = files[name]
            if member.size != row['bytes']:
                raise ValueError('Size mismatch: ' + name)
            h = hashlib.sha256()
            with tar.extractfile(member) as stream:
                for block in iter(lambda: stream.read(8 << 20), b''):
                    h.update(block)
            if h.hexdigest() != row['sha256'].lower():
                raise ValueError('File hash mismatch: ' + name)
        # Also compare downloaded inputs with this checkout's pinned values,
        # not just a self-consistent manifest supplied with the archive.
        spec = json.loads((ROOT/'configs/hybrid-dependencies.json').read_text())
        for item in spec['downloads']:
            name = 'downloads/' + item['file']
            if name in files:
                h = hashlib.new(item['algorithm'].lower())
                with tar.extractfile(files[name]) as stream:
                    for block in iter(lambda: stream.read(8 << 20), b''):
                        h.update(block)
                if h.hexdigest() != item['hash']:
                    raise ValueError('Pinned download hash mismatch: ' + name)
        output.mkdir(parents=True)
        for name, member in files.items():
            target = output/name
            target.parent.mkdir(parents=True, exist_ok=True)
            with tar.extractfile(member) as source, target.open('xb') as dest:
                shutil.copyfileobj(source, dest, 8 << 20)
            target.chmod(0o600)
    return report


def prepare_surface(output):
    """Normalize unique OEM firmware; conflicting installed versions need selection."""
    source = output/'surface/windows/System32/DriverStore/FileRepository'
    dest = output/'surface-assets'
    result = {'files': {}, 'missing': [], 'conflicts': [], 'build_ready': False}
    # Same hardware firmware used for SQ1/SQ2, not a device-specific identity.
    mapping = {n: 'firmware/qcom/msft/surface/pro-x/'+n for n in (
        'qcadsp8180.mbn', 'qccdsp8180.mbn', 'qcslpi8180.mbn', 'qcmpss8180.mbn',
        'qcmpss8180_nm.mbn', 'qcdxkmsuc8180.mbn', 'qcvss8180.mbn', 'wlanmdsp.mbn')}
    mapping.update({'SurfaceLightSensor.sys': 'sensors/SurfaceLightSensor.sys'})
    candidates = list(source.rglob('*')) if source.exists() else []
    def choose(name):
        found = [p for p in candidates if p.is_file() and p.name.lower() == name.lower()]
        if not found:
            result['missing'].append(name)
            return None
        if len({digest(p) for p in found}) != 1:
            result['conflicts'].append(name)
            return None
        return found[0]
    def copy(src, relative):
        target = dest/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, target)
        result['files'][relative] = {'source': str(src.relative_to(output)), 'sha256': digest(target)}
    for name, relative in mapping.items():
        src = choose(name)
        if src:
            copy(src, relative)
    # Configs are read only from the factory sensorsconfig package, never persist.
    configs = [p for p in candidates if p.is_file() and
               any(part.lower().startswith('surfaceprox_sensorsconfig.inf_') for part in p.parts)
               and (p.suffix.lower() == '.json' or p.name == 'config_list.txt')]
    for name in sorted({p.name for p in configs}):
        matches = [p for p in configs if p.name == name]
        if len({digest(p) for p in matches}) != 1:
            result['conflicts'].append('config/'+name)
        else:
            copy(matches[0], 'sensors/config/'+name)
    expected_configs = {Path(n).name for n in json.loads((ROOT/'configs/sensors-baseline.json').read_text())['files'] if n.startswith('config/')}
    result['missing'].extend('config/'+n for n in sorted(expected_configs - {p.name for p in configs}))
    driver = dest/'sensors/SurfaceLightSensor.sys'
    if driver.exists():
        cmd = [sys.executable, str(ROOT/'scripts/extract-spx-light-calibration.py'),
               str(driver), str(dest/'sensors/spx-tcs3430-golden.bin')]
        run = subprocess.run(cmd, capture_output=True, text=True)
        result['calibration'] = {'command': cmd, 'exit': run.returncode,
                                 'stdout': run.stdout, 'stderr': run.stderr}
        if run.returncode:
            result['missing'].append('valid model-default calibration')
        else:
            result['files']['sensors/spx-tcs3430-golden.bin'] = {
                'sha256': digest(dest/'sensors/spx-tcs3430-golden.bin')}
    required_sensor = {'firmware/qcom/msft/surface/pro-x/qcslpi8180.mbn', 'sensors/spx-tcs3430-golden.bin'}
    required_sensor.update('sensors/config/'+n for n in expected_configs)
    result['sensor_oem_ready'] = required_sensor <= result['files'].keys() and not any(
        n.startswith('config/') or n in ('qcslpi8180.mbn','SurfaceLightSensor.sys') for n in result['conflicts'])
    # Vendor WLAN/BT blobs remain raw: board-2.bin/TLV patching must follow the
    # pinned aarch64-firmware recipe, not rename an arbitrary board file.
    result['remaining'] = ['WLAN board/TLV and Venus firmware conversion',
                           'source-built SLPI EFI, sensor HAL/RPC/overlay/modules',
                           'prepared Android/QTI runtime, kernel source and camera bundle']
    (output/'SURFACE-PREPARATION.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


def check_build_inputs(private):
    required = ('template.img', 'kernel-6.18/source/SOURCE.json', 'kernel-6.18/source/source.tar.gz',
                'kernel-6.18/toolchain.tar.gz', 'kernel-6.18/board.dts',
                'kernel-6.18/bootloader/BOOTAA64.EFI', 'full-image-runtime/records.json',
                'camera/CAMERA.json', 'sensors/sensors-bundle.json')
    required += tuple('kernel-6.18/EFI/SQ2Nat/'+n for n in
                      ('start.efi', 'el2launch.efi', 'qebspilaa64.efi', 'tcblaunch.exe', 'kernel-options.txt'))
    missing = [n for n in required if not (private/n).is_file() or not (private/n).stat().st_size]
    errors = []
    for name in ('camera', 'sensors'):
        filename = 'CAMERA.json' if name == 'camera' else 'sensors-bundle.json'
        if (private/name/filename).is_file():
            try:
                spec = importlib.util.spec_from_file_location(name, ROOT/'scripts'/('full_image_'+name+'.py'))
                module = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(module)
                module.load(private/name)
            except Exception as exc:
                errors.append(name+': '+str(exc))
    # This is input presence/component validation, not a dry run of compilation.
    return {'private_root': str(private), 'missing': missing, 'errors': errors,
            'input_preflight_passed': not missing and not errors, 'build_verified': False}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--archive', type=Path)
    p.add_argument('--sha256')
    p.add_argument('--output', type=Path)
    p.add_argument('--private-root', type=Path)
    p.add_argument('--build', action='store_true')
    p.add_argument('--build-output', type=Path)
    p.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) - 1))
    p.add_argument('--bootstrap-work', type=Path, help='fresh Linux work directory for offline source/extraction/build stages')
    p.add_argument('--bootstrap-stage', choices=('sources','toolchains','kernel','quest','android','platform','all'), default='all')
    a = p.parse_args()
    if a.archive:
        if not a.sha256 or not a.output:
            p.error('--archive requires --sha256 and fresh --output')
        result = import_bundle(a.archive, a.output.resolve(), a.sha256)
        surface = prepare_surface(a.output.resolve())
        print(json.dumps({'imported': str(a.output.resolve()), 'verified_files': len(result['files']),
                          'build_ready': False, 'unresolved': result['unresolved'],
                          'surface_missing': surface['missing'], 'surface_conflicts': surface['conflicts']}, indent=2))
    if a.private_root:
        result = check_build_inputs(a.private_root.resolve())
        print(json.dumps(result, indent=2))
        if not result['input_preflight_passed']:
            return 2
    if a.bootstrap_work:
        if not a.archive or not a.output:
            p.error('--bootstrap-work requires an archive imported in this invocation')
        result = subprocess.run([sys.executable, str(ROOT/'scripts/hybrid_bootstrap.py'),
                                 '--imported', str(a.output.resolve()), '--work', str(a.bootstrap_work.resolve()),
                                 '--stage', a.bootstrap_stage])
        if result.returncode:
            return result.returncode
    if a.build:
        if not a.private_root or not a.build_output or a.jobs < 1:
            p.error('--build requires --private-root, fresh --build-output and positive --jobs')
        return subprocess.run([sys.executable, str(ROOT/'scripts/build.py'), 'full-image',
                               '--private-root', str(a.private_root.resolve()), '--graphics', 'hybrid',
                               '--output', str(a.build_output.resolve()), '--jobs', str(a.jobs)]).returncode
    if not a.archive and not a.private_root:
        p.error('Specify --archive or --private-root')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, OSError, KeyError, tarfile.TarError) as exc:
        print('ERROR: '+str(exc), file=sys.stderr)
        sys.exit(1)
