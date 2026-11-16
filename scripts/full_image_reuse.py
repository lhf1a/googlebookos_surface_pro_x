"""Explicit, hash-checked reuse of completed components; never a cache search."""
import argparse
import hashlib
import json
from pathlib import Path

MESA = {
    'libEGL_freedreno.so': 'etc/googlebook-security/lib64/egl/libEGL_freedreno.so',
    'libGLESv1_CM_freedreno.so': 'etc/googlebook-security/lib64/egl/libGLESv1_CM_freedreno.so',
    'libGLESv2_freedreno.so': 'etc/googlebook-security/lib64/egl/libGLESv2_freedreno.so',
    'libgallium_dri.so': 'etc/googlebook-security/lib64/libgallium_dri.so',
    'libdrm.so': 'etc/googlebook-security/lib64/libdrm.so',
}
GLUE = {
    'vulkan_router_bytes': 'etc/googlebook-security/lib64/hw/vulkan.freedreno.so',
    'gralloc_adapter_bytes': 'etc/sq2-qti-system/root/lib64/hw/gralloc.sq2_mesa.so',
}

ROOT = Path(__file__).resolve().parents[1]


def helper_sources():
    paths = [ROOT/n for n in ('scripts/build-touch-calibration.py',
             'scripts/build_mesa_qti_layout.py','scripts/build_sdm_backlight_shim.py',
             'scripts/build_mesa_qti_gralloc.py','probe/graphics/quest2-mesa-layout.c',
             'probe/graphics/sq2-sdm-backlight-shim.c','probe/storage/resize-userdata-partition.c',
             'probe/test2/sq2-touch-calibration.hpp')]
    paths += [p for p in (ROOT/'probe/touch-calibration').rglob('*') if p.is_file()]
    return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes().replace(b'\r\n',b'\n')).hexdigest() for p in paths}


def prepared_runtime(receipt):
    """Reuse only named compiled helpers, not the old init or personal state."""
    if not receipt: return {}, {}
    path = Path(receipt['path'])
    if digest(path) != receipt['sha256'] or helper_sources() != receipt['source_sha256']:
        raise ValueError('Prepared helper runtime or source changed')
    from full_image import records_from_gzip
    _, bodies = records_from_gzip(path)
    prefix = 'etc/sq2-qti-system/root/'
    graphics = {
        'layout_payload':{n:bodies[prefix+n] for n in (
            'lib64/libgrallocutils.so','lib64/sq2-qti-display/libgrallocutils.so','lib64/libsq2_grallocutils_vendor.so')},
        'backlight_payload':{n:bodies[prefix+n] for n in (
            'lib64/sq2-qti-display/libsdmcore.so','lib64/sq2-qti-display/libsq2_sdm_backlight.so')},
    }
    helpers = {n:bodies[n] for n in ('etc/sq2-touch-calibration/wizard.apk','bin/resize-userdata-partition')}
    return graphics, helpers


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def inventory(roots):
    result = {}
    for role, root in roots.items():
        if role == 'kernel':
            paths = [root/n for n in ('Image', 'kernel.config', 'Module.symvers', 'System.map', 'build-audit.json')]
            paths += sorted(p for p in (root/'modules-stage').rglob('*') if p.is_file())
        elif role == 'mesa':
            paths = [root/n for n in (*MESA, 'vulkan.turnip.so')]
        elif role == 'glue':
            paths = [root/n for n in GLUE.values()]
        else:
            paths = sorted(p for p in root.rglob('*') if p.is_file())
        result[role] = {}
        for p in paths:
            if p.is_symlink() or not p.is_file():
                raise ValueError('Not a regular reuse input: '+str(p))
            result[role][p.relative_to(root).as_posix()] = digest(p)
    return result


def load(path):
    data = json.loads(path.read_text())
    if data.get('schema') != 1 or set(data['roots']) != {'kernel','camera','sensors','mesa','glue'}:
        raise ValueError('Invalid component reuse manifest')
    roots = {k: Path(v).resolve() for k,v in data['roots'].items()}
    if inventory(roots) != data['sha256']:
        raise ValueError('Reused component hash/inventory mismatch')
    audit = json.loads((roots['kernel']/'build-audit.json').read_text())
    from full_image import policy
    release = policy()['kernel_release']
    if (not audit.get('source_compiled') or audit.get('core_only') or
            audit.get('kernel_release') != release or not audit.get('commands') or
            any(c['exit'] for c in audit['commands'])):
        raise ValueError('Kernel reuse requires a completed matching full source build')
    if not list((roots['kernel']/'modules-stage/lib/modules'/release).rglob('*.ko')):
        raise ValueError('Reused kernel has no staged modules')
    from full_image_camera import load as camera_load
    from full_image_sensors import load as sensors_load
    camera_load(roots['camera'])  # Complete bundle, not source-only schema 2.
    sensors_load(roots['sensors'])
    graphics = {k: (roots['glue']/n).read_bytes() for k,n in GLUE.items()}
    graphics['turnip_bytes'] = (roots['mesa']/'vulkan.turnip.so').read_bytes()
    replacements = {dest: (roots['mesa']/name).read_bytes() for name,dest in MESA.items()}
    return roots, replacements, graphics, data


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for role in ('kernel','camera','sensors','mesa','glue'):
        p.add_argument('--'+role, type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--assembled-runtime', type=Path, help='explicit prior initramfs for unchanged compiled helpers only')
    a = p.parse_args()
    roots = {k: getattr(a,k).resolve() for k in ('kernel','camera','sensors','mesa','glue')}
    data = {'schema':1, 'roots': {k:str(v) for k,v in roots.items()}, 'sha256':inventory(roots)}
    if a.assembled_runtime:
        data['prepared_runtime'] = {'path':str(a.assembled_runtime.resolve()),'sha256':digest(a.assembled_runtime),
                                    'source_sha256':helper_sources()}
        prepared_runtime(data['prepared_runtime'])
    with a.output.open('x') as f:
        json.dump(data, f, indent=2); f.write('\n')
    load(a.output)
    print(a.output.resolve())


if __name__ == '__main__':
    main()
