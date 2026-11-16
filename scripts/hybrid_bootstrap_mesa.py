"""Build clean MSM Freedreno/Turnip from pinned archives, without Meson downloads."""
import os
from pathlib import Path
import shutil
import sys
import tarfile


def mesa(b, ndk=None, jobs=8):
    from hybrid_bootstrap import ROOT, digest, unzip
    ndk = Path(ndk or b.work/'toolchains/sq2-graphics/android-ndk-r28c').resolve()
    llvm = ndk/'toolchains/llvm/prebuilt/linux-x86_64'
    if not (llvm/'bin/aarch64-linux-android35-clang').is_file():
        raise ValueError('Explicit Mesa NDK missing')
    out = b.work/'mesa'; out.mkdir()
    for id in ('mesa','libdrm','expat'):
        with tarfile.open(b.input(id)) as tar: tar.extractall(out, filter='data')
    src = out/'mesa-26.2.4'
    for name in ('libdrm-2.4.133','expat-2.5.0'): shutil.move(out/name, src/'subprojects'/name)
    unzip(b.input('expat-meson'),out/'expat-meson')
    shutil.copytree(out/'expat-meson', src/'subprojects', dirs_exist_ok=True)
    # The pinned reference omits its final newline; preserve the input and
    # normalize a working copy just as the existing GLES builder does.
    mapper = out/'mapper5.patch'
    mapper.write_text(b.input('mesa-mapper5').read_text().rstrip()+'\n')
    b.run('mesa-mapper5', ['patch','--batch','--fuzz=0','-p1','-d',src,'-i',mapper])
    b.run('mesa-multiplane', [sys.executable, ROOT/'scripts/patch-mesa-sq2-multiplane.py',
                             src/'src/util/u_gralloc/u_gralloc_vm_mapper5.cpp'])
    host = out/'host-env'
    wheels = [b.input(x['id']) for x in b.spec['downloads'] if x['id'].startswith('host-python-')]
    b.run('mesa-host-venv', [sys.executable,'-m','venv',host])
    python = host/'bin/python'
    b.run('mesa-host-python', [python,'-m','pip','install','--no-index','--no-deps']+wheels)
    env = dict(os.environ, PATH=str(host/'bin')+os.pathsep+os.environ['PATH'])
    cross = out/'android.cross'
    cross.write_text(f"""[binaries]
c = '{llvm}/bin/aarch64-linux-android35-clang'
cpp = ['{llvm}/bin/aarch64-linux-android35-clang++', '-fno-exceptions', '--start-no-unused-arguments', '-static-libstdc++', '--end-no-unused-arguments']
ar = '{llvm}/bin/llvm-ar'
strip = '{llvm}/bin/llvm-strip'
pkg-config = '/usr/bin/pkg-config'
[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'armv8'
endian = 'little'
[properties]
needs_exe_wrapper = true
pkg_config_libdir = '{out}/pkgconfig'
""")
    (out/'pkgconfig').mkdir()
    options = ['--buildtype=release','--wrap-mode=nodownload','-Dforce_fallback_for=libdrm,expat',
               '-Dallow-fallback-for=libdrm',
               '-Dplatforms=android','-Dplatform-sdk-version=35','-Dandroid-stub=true',
               '-Dandroid-libbacktrace=disabled','-Dllvm=disabled','-Dglx=disabled','-Dgbm=disabled',
               '-Dvideo-codecs=','-Dgallium-va=disabled','-Dzstd=disabled',
               '-Dgallium-drivers=freedreno','-Dfreedreno-kmds=msm','-Dvulkan-drivers=freedreno',
               '-Degl=enabled','-Dgles1=enabled','-Dgles2=enabled','-Dopengl=true',
               '-Degl-lib-suffix=_freedreno','-Dgles-lib-suffix=_freedreno']
    b.run('mesa-configure', [python,'-m','mesonbuild.mesonmain','setup',out/'build',src,
                            '--cross-file',cross]+options,env)
    b.run('mesa-compile', ['ninja','-j',str(jobs),'-C',out/'build'],env)
    runtime = out/'runtime'; runtime.mkdir()
    outputs = {
        'libEGL_freedreno.so':'src/egl/libEGL_freedreno.so',
        'libGLESv1_CM_freedreno.so':'src/mesa/glapi/es1api/libGLESv1_CM_freedreno.so',
        'libGLESv2_freedreno.so':'src/mesa/glapi/es2api/libGLESv2_freedreno.so',
        'libgallium_dri.so':'src/gallium/targets/dri/libgallium_dri.so',
        'vulkan.turnip.so':'src/freedreno/vulkan/libvulkan_freedreno.so',
        'libdrm.so':'subprojects/libdrm-2.4.133/libdrm.so',
    }
    for name, rel in outputs.items():
        b.run('strip-'+name,[llvm/'bin/llvm-strip','--strip-unneeded','-o',runtime/name,out/'build'/rel])
        data=(runtime/name).read_bytes()
        if data[:6]!=b'\x7fELF\x02\x01' or int.from_bytes(data[18:20],'little')!=183:
            raise ValueError('Not ARM64 ELF: '+name)
        if name == 'vulkan.turnip.so' and any(marker in data for marker in
                (b'com.futuremark.dmandroid.application', b'SQ2 A680.1')):
            raise ValueError('Legacy benchmark-specific sysmem override in clean Turnip')
    b.report['mesa_outputs'] = {name:digest(runtime/name) for name in outputs}
    b.report['mesa_hardware_tested'] = False
    b.save()
