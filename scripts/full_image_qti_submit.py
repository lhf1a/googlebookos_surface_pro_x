"""Build the QCOM submit path from source; keep the GSL donor private."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile

from build_mesa_qti_gralloc import find_android_cc

ROOT = Path(__file__).resolve().parents[1]
HEADERS = ('drm.h', 'drm_mode.h', 'drm_fourcc.h', 'msm_drm.h')


def build_payload(private, out, uapi=None):
    out = Path(out).resolve()
    out.mkdir(parents=True)
    hdr = out/'include/drm'
    hdr.mkdir(parents=True)
    if uapi is not None:
        for name in HEADERS:
            shutil.copyfile(Path(uapi)/'drm'/name, hdr/name)
    else:
        src = Path(private)/'kernel-6.18/source'
        recipe = json.loads((src/'SOURCE.json').read_text())
        wanted = {'kernel/include/uapi/drm/'+name: name for name in HEADERS}
        with tarfile.open(src/recipe['archive'], 'r|*') as tar:
            for member in tar:
                name = wanted.pop(member.name.removeprefix('./'), None)
                if name is not None:
                    if not member.isfile():
                        raise ValueError(f'Expected regular UAPI header: {member.name}')
                    with tar.extractfile(member) as data, (hdr/name).open('wb') as target:
                        shutil.copyfileobj(data, target)
                if not wanted:
                    break
        if wanted:
            raise ValueError(f'Missing paired kernel headers: {sorted(wanted)}')

    g = ROOT/'probe/graphics'
    cc = find_android_cc()
    common = [str(cc), '--target=aarch64-linux-android35', '-O2', '-std=gnu11',
              '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC', '-Wl,--no-undefined']
    bridge = 'libqti-msm-vmbind-bridge.so'
    helper = 'libquest2-native-vulkan-memory.so'
    policy = 'vulkan.qti-policy.so'
    def compile_so(name, sources, flags):
        subprocess.run(common + [str(g/s) for s in sources] + list(map(str, flags)) +
                       ['-Wl,-soname,'+name, '-Wl,-rpath,/vendor/lib64:/vendor/lib64/egl',
                        '-o', str(out/name)], check=True)
    compile_so(bridge, ['qti-msm-vmbind'+s+'.c' for s in ('', '-registry', '-sync', '-bridge')],
               ['-I', hdr.parent, '-pthread', '-llog'])
    donor = Path(private)/'full-image-runtime/files/etc/sq2-qti-system/root/lib64/libgsl.so'
    # The prepared donor and linked helper share the same Android namespace.
    patched = out/'gsl'
    subprocess.run([sys.executable, str(ROOT/'scripts/prepare-qti-msm-vmbind.py'),
                    '--gsl', str(donor), '--bridge', str(out/bridge), '--output', str(patched)], check=True)
    compile_so(helper, ['quest2-native-vulkan-memory.c'], ['-L', patched, '-lgsl'])
    compile_so(policy, ['quest2-native-vulkan-policy.c'],
               ['-I', g/'android-hal-headers', '-pthread',
                '-DSQ2_NATIVE_VULKAN_DONOR="/vendor/lib64/vulkan.qti-native.so"',
                '-L', out, '-lquest2-native-vulkan-memory', '-ldl', '-Wl,-Bsymbolic'])
    return {'lib64/'+bridge: (out/bridge).read_bytes(), 'lib64/libgsl.so': (patched/'libgsl.so').read_bytes(),
            'lib64/'+helper: (out/helper).read_bytes(), 'lib64/hw/vulkan.qti.so': (out/policy).read_bytes(),
            'etc/init/sq2-qti-vmbind.rc': b'on post-fs-data\n'
                b'    setprop debug.sq2.vmbind.app com.hottagames.yh.laohu\n'
                b'    setprop debug.sq2.vmbind.noshare_mode exclude\n'}
