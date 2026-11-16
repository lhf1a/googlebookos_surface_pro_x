"""Offline source recipes used by hybrid_bootstrap; no device or cached-input lookup."""
import json
import os
from pathlib import Path
import shutil
import sys
import tarfile


def kernel(b):
    from hybrid_bootstrap import ROOT, digest
    recipe = json.loads((ROOT/'configs/hybrid-kernel-source.json').read_text())
    archive = b.input('linux-6.18.60')
    if digest(archive) != recipe['upstream']['sha256']:
        raise ValueError('Kernel upstream pin mismatch')
    for name in ('patch', 'external'):
        if digest(ROOT/recipe[name]) != recipe[name+'_sha256']:
            raise ValueError('Kernel project source pin mismatch: '+name)
    source = b.work/'kernel-source'; source.mkdir()
    with tarfile.open(archive) as tar:
        tar.extractall(source, filter='data')
    (source/'linux-6.18.60').rename(source/'kernel')
    b.run('kernel-project-delta', ['patch', '--batch', '--fuzz=0', '-p1', '-d', source/'kernel',
                                 '-i', ROOT/recipe['patch']])
    with tarfile.open(ROOT/recipe['external']) as tar:
        tar.extractall(source, filter='data')
    target = b.work/'build-inputs/kernel-6.18/source'; target.mkdir(parents=True)
    with tarfile.open(target/'source.tar.gz', 'w:gz', compresslevel=1) as tar:
        for name in ('kernel','external'): tar.add(source/name, arcname=name)
    result = dict(schema=1, current_release_patches=True, archive='source.tar.gz',
                  archive_sha256=digest(target/'source.tar.gz'), recipe=recipe,
                  kernel_source_compiled=False, hardware_tested=False)
    (target/'SOURCE.json').write_text(json.dumps(result, indent=2)+'\n')
    b.report['kernel_source'] = result; b.save()


def materialize_hidl(b, out):
    from gitiles_archive import archive_tree
    out.mkdir(parents=True)
    for item in b.spec['downloads']:
        material = item.get('materialize', {})
        if material.get('group') != 'hidl': continue
        source = b.input(item['id']); target = out/material['destination']
        if material['archive']:
            target.mkdir(parents=True, exist_ok=True)
            with tarfile.open(source) as tar:
                if item.get('git_tree') and archive_tree(tar) != item['git_tree']:
                    raise ValueError('HIDL Git tree mismatch: '+item['id'])
                tar.extractall(target, filter='data')
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
    (out/'hidl-gen').chmod(0o755)
    env = dict(os.environ, LD_LIBRARY_PATH=str(out))
    for api in ('android.hidl.base@1.0', 'android.hidl.manager@1.0',
                'android.hidl.manager@1.1', 'android.hidl.manager@1.2'):
        b.run('generate-'+api, [out/'hidl-gen', '-o', out/'generated', '-Lc++-headers',
              '-randroid.hardware:'+str(out/'hal/android/hardware'),
              '-randroid.hidl:'+str(out/'hal/android/hidl'), api], env)


def platform(b, sources=None, toolchains=None, files=None):
    from hybrid_bootstrap import ROOT, digest
    sources = Path(sources or b.work/'sources').resolve()
    toolchains = Path(toolchains or b.work/'toolchains').resolve()
    files = Path(files or b.work/'android/mica-files').resolve()
    out = b.work/'platform'; out.mkdir()
    # Explicit inputs are recorded; these override flags are for isolated tests,
    # not a search for another developer's cache or an accepted clean-room build.
    b.report['platform_inputs'] = dict(sources=str(sources), toolchains=str(toolchains), files=str(files))
    staged = out/'toolchains'; staged.mkdir()
    for name, target in (
        ('sq2-graphics', toolchains/'sq2-graphics'),
        ('sq2-c2-native-reference', sources/'aosp-frameworks-native')):
        if not target.is_dir(): raise ValueError('Missing explicit source/tools: '+str(target))
        (staged/name).symlink_to(target)
    dependencies = out/'camera-deps'; dependencies.mkdir()
    for id, name in (('camera-system-media','media'), ('camera-jpeg','jpeg'), ('camera-core','core')):
        target = dependencies/name; target.mkdir()
        with tarfile.open(b.input(id)) as tar: tar.extractall(target, filter='data')
    core = staged/'sq2-c2-core-reference/libcutils'; core.mkdir(parents=True)
    (core/'include').symlink_to(dependencies/'core')
    (dependencies/'system-media').symlink_to(dependencies/'media/camera/include/system')
    (dependencies/'native-headers').symlink_to(sources/'aosp-frameworks-native')
    interfaces = sources/'aosp-camera-sensor-interfaces'
    headers = out/'headers'; headers.mkdir()
    for source in (interfaces/'graphics/mapper/stable-c/include',
                   interfaces/'graphics/mapper/stable-c/implutils/include', dependencies/'core'):
        shutil.copytree(source, headers, dirs_exist_ok=True)
    libraries = out/'lib64'; libraries.mkdir()
    # Prefer framework libraries, then vendor-only HIDL services. No whole
    # filesystem is copied into runtime; builders consume named ELF inputs.
    for group in ('system_a/system','system_ext_a','product_a','vendor_a'):
        for path in sorted((files/group/'lib64').glob('*.so')):
            if not (libraries/path.name).exists(): (libraries/path.name).symlink_to(path)
    ndk = staged/'sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64'
    aidl = staged/'sq2-graphics/sdk/android-15/aidl'
    b.run('camera-aidl', [sys.executable, ROOT/'scripts/build-spx-camera-aidl.py', interfaces,
          aidl, ndk, out/'camera-aidl', '--binder-headers', sources/'aosp-frameworks-native/libs/binder/ndk'])
    b.run('camera-provider', [sys.executable, ROOT/'scripts/build-spx-camera-provider.py', dependencies,
          out/'camera-aidl', libraries, staged, out/'camera-provider', '--platform-headers', headers,
          '--binder-library', libraries/'libbinder_ndk.so'])
    materialize_hidl(b, staged/'sq2-quest2-hidl')
    b.run('sensor-hal', [sys.executable, ROOT/'scripts/build-spx-sensor-hal.py', interfaces,
                        staged, libraries, out/'sensor-hal'])
    b.run('sensor-overlay', [sys.executable, ROOT/'scripts/build-spx-sensor-overlay.py', staged,
                            out/'sensor-overlay', '--keystore', out/'overlay.keystore'])
    outputs = ('camera-provider','sensor-hal/sensors.spx.so','sensor-hal/subhal-test',
               'sensor-overlay/spx-sensors-overlay.apk')
    b.report['platform_outputs'] = {n: digest(out/n) for n in outputs}
    b.report['platform_hardware_tested'] = False
    b.save()


def camera_graphics(b, platform_work=None, interfaces=None, quest_vendor=None):
    from hybrid_bootstrap import ROOT, load_module, digest
    from full_image_camera import write_inputs
    out = Path(platform_work or b.work/'platform').resolve()
    interfaces = Path(interfaces or b.work/'sources/aosp-camera-sensor-interfaces').resolve()
    quest = Path(quest_vendor or b.work/'quest2/reference/vendor').resolve()
    refs = out/'mapper-references'; refs.mkdir()
    for item in b.spec['downloads']:
        material = item.get('materialize', {})
        if material.get('group') != 'mapper': continue
        target = refs/material['destination']; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(b.input(item['id']), target)
    shutil.copytree(out/'camera-deps/core', refs/'include', dirs_exist_ok=True)
    with tarfile.open(b.input('camera-libhardware')) as tar: tar.extractall(refs/'include', filter='data')
    roots = [refs/'allocator-aidl', interfaces/'common/aidl/aidl_api/android.hardware.common/2',
             interfaces/'graphics/common/aidl/aidl_api/android.hardware.graphics.common/5']
    frozen_hash = (refs/'allocator-v2.hash').read_text().strip().splitlines()[-1]
    for n, source in enumerate(sorted((refs/'allocator-aidl').rglob('*.aidl'))):
        b.run('allocator-aidl-'+str(n), [out/'toolchains/sq2-graphics/sdk/android-15/aidl',
              '--lang=ndk','--structured','--stability=vintf','--version=2','--hash='+frozen_hash,
              '--out='+str(out/'allocator/src'),'--header_out='+str(out/'allocator/include')]+
              ['-I'+str(p) for p in roots]+[source])
    common = out/'toolchains/sq2-c2-aidl-generated'; common.mkdir()
    (common/'include').symlink_to(out/'camera-aidl/include')
    inputs = dict(toolchains=out/'toolchains', platform_headers=refs/'include',
                  layout_headers=out/'camera-aidl/include', hidl_headers=out/'toolchains/sq2-quest2-hidl/include',
                  allocator_headers=out/'allocator/include', graphics_library_dirs=[quest/'lib64',out/'lib64'])
    builder = load_module('camera_bundle', 'build-spx-camera-bundle.py')
    recipe = out/'camera-graphics-commands.json'
    recipe.write_text(json.dumps({'commands':builder.graphics_commands(inputs)},indent=2)+'\n')
    b.run('camera-graphics', [sys.executable, ROOT/'scripts/build-spx-camera-graphics.py',recipe,
                             ROOT/'probe/graphics', out/'camera-graphics'])
    files = {'provider':(out/'camera-provider').read_bytes()}
    files.update({name:(out/'camera-graphics'/name).read_bytes()
                  for name in ('mapper.quest2.so','quest2-allocator5-service')})
    bundle = b.work/'build-inputs/camera'
    write_inputs(files, bundle, source_only=True)
    b.report['camera_source_bundle'] = {'path':str(bundle), 'manifest_sha256':digest(bundle/'CAMERA.json'),
                                      'modules_require_source_build':True}
    b.save()
