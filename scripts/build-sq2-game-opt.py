#!/usr/bin/env python3
"""Build sq2-game-optd, zygisk/arm64-v8a.so, and package out/sq2-game-opt/sq2-game-opt.zip."""
import argparse
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = ROOT / 'probe' / 'sq2-game-opt'
MODULE_TMPL = SRC_DIR / 'module'
DEFAULT_OUT_ZIP = ROOT / 'out' / 'sq2-game-opt' / 'sq2-game-opt.zip'
WT_POLICY_CONF = ROOT / 'configs' / 'sq2-vulkan-policy.conf'
if not WT_POLICY_CONF.is_file():
    WT_POLICY_CONF = ROOT / 'out' / 'wt-hybrid-20261113' / 'configs' / 'sq2-vulkan-policy.conf'

ALLOWED_DAEMON_NEEDED = {'libc.so', 'libdl.so', 'libm.so'}
ALLOWED_ZYGISK_NEEDED = {'libc.so', 'libdl.so', 'liblog.so', 'libm.so'}
EXEC_FILES = {
    'customize.sh',
    'post-fs-data.sh',
    'service.sh',
    'uninstall.sh',
    'bin/sq2-game-optd',
    'META-INF/com/google/android/update-binary',
}


def find_android_cc(explicit=None):
    """Find aarch64-linux-android35-clang (or latest aarch64-linux-android*-clang)."""
    candidates = []
    if explicit:
        candidates.append(Path(explicit))
    for env_key in ('SQ2_ANDROID_CC', 'CC'):
        val = os.environ.get(env_key)
        if val:
            candidates.append(Path(val))
    which_cc = shutil.which('aarch64-linux-android35-clang')
    if which_cc:
        candidates.append(Path(which_cc))
    for ndk_env in ('ANDROID_NDK_HOME', 'ANDROID_NDK_ROOT'):
        base = os.environ.get(ndk_env)
        if base:
            candidates.append(
                Path(base) / 'toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang'
            )
    candidates.extend([
        Path('/mnt/spx-workspace/toolchains/sq2-graphics/android-ndk-r28c/'
             'toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang'),
        Path.home() / '.cache/ndk-r28c/android-ndk-r28c/'
                      'toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang',
        Path('/root/.cache/sq2-graphics/android-ndk-r28c/'
             'toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang'),
    ])
    for cand in candidates:
        try:
            if cand.is_file() and os.access(cand, os.X_OK):
                return cand
        except PermissionError:
            continue
    raise FileNotFoundError(
        'Android NDK aarch64-linux-android35-clang not found; set SQ2_ANDROID_CC or pass --cc'
    )


def find_llvm_tool(cc_path, tool_name):
    """Locate sibling llvm tool (readelf/strip) next to NDK clang."""
    sibling = cc_path.parent / tool_name
    if sibling.is_file() and os.access(sibling, os.X_OK):
        return sibling
    which_tool = shutil.which(tool_name)
    if which_tool:
        return Path(which_tool)
    return None


def check_elf_deps_and_exports(elf_path, readelf_bin, allowed_needed, required_dynsyms=()):
    """Verify ELF DT_NEEDED subset and required dynamic exports."""
    out = subprocess.check_output(
        [str(readelf_bin), '-d', str(elf_path)], text=True
    )
    needed = set()
    for line in out.splitlines():
        if '(NEEDED)' in line and '[' in line and ']' in line:
            lib = line.split('[', 1)[1].rsplit(']', 1)[0].strip()
            needed.add(lib)
    extra = needed - allowed_needed
    if extra:
        raise RuntimeError(f'{elf_path.name} has unexpected DT_NEEDED: {sorted(extra)}')

    if required_dynsyms:
        syms_out = subprocess.check_output(
            [str(readelf_bin), '--dyn-syms', '-W', str(elf_path)], text=True
        )
        for sym in required_dynsyms:
            if sym not in syms_out:
                raise RuntimeError(f'{elf_path.name} missing required exported symbol: {sym}')


def build_package(out_zip=DEFAULT_OUT_ZIP, cc=None):
    """Compile native components and package the KernelSU/Magisk zip."""
    cc_path = find_android_cc(cc)
    cxx_path = cc_path.parent / (cc_path.name + '++')
    if not cxx_path.is_file():
        raise FileNotFoundError(f'Missing companion C++ driver: {cxx_path}')
    readelf_bin = find_llvm_tool(cc_path, 'llvm-readelf') or find_llvm_tool(cc_path, 'readelf')
    strip_bin = find_llvm_tool(cc_path, 'llvm-strip')
    if not readelf_bin:
        raise FileNotFoundError('Missing llvm-readelf / readelf')

    if WT_POLICY_CONF.is_file():
        wt_text = WT_POLICY_CONF.read_text(encoding='utf-8').strip()
        mod_policy = MODULE_TMPL / 'system/vendor/etc/sq2-vulkan-policy.conf'
        mod_text = mod_policy.read_text(encoding='utf-8').strip()
        if wt_text != mod_text:
            raise RuntimeError(
                f'Out-of-sync policy files: {WT_POLICY_CONF} != {mod_policy}'
            )

    out_zip = Path(out_zip)
    out_zip.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix='sq2-game-opt-build-') as td:
        work = Path(td)
        stage = work / 'stage'
        shutil.copytree(MODULE_TMPL, stage)
        (stage / 'bin').mkdir(parents=True, exist_ok=True)
        (stage / 'zygisk').mkdir(parents=True, exist_ok=True)

        common_o = work / 'common_policy.o'
        daemon_o = work / 'sq2_game_optd.o'
        vk_hook_o = work / 'vk_swapchain_hook.o'
        zygisk_o = work / 'zygisk_game_opt.o'

        cflags = [
            '-O2', '-fPIC', '-std=c11', '-Wall', '-Wextra', '-Werror',
            f'-I{SRC_DIR}',
        ]
        cxxflags = [
            '-O2', '-fPIC', '-std=c++20', '-fno-exceptions', '-fno-rtti',
            '-fvisibility=hidden', '-Wall', '-Wextra', '-Werror',
            f'-I{SRC_DIR}',
        ]

        subprocess.check_call([
            str(cc_path), *cflags, '-c', str(SRC_DIR / 'common_policy.c'),
            '-o', str(common_o),
        ])
        subprocess.check_call([
            str(cc_path), *cflags, '-c', str(SRC_DIR / 'sq2_game_optd.c'),
            '-o', str(daemon_o),
        ])
        subprocess.check_call([
            str(cc_path), *cflags, '-c', str(SRC_DIR / 'vk_swapchain_hook.c'),
            '-o', str(vk_hook_o),
        ])
        subprocess.check_call([
            str(cxx_path), *cxxflags, '-c', str(SRC_DIR / 'zygisk_game_opt.cpp'),
            '-o', str(zygisk_o),
        ])

        daemon_out = stage / 'bin' / 'sq2-game-optd'
        subprocess.check_call([
            str(cc_path), '-fPIE', '-pie', '-Wl,-z,noexecstack', '-Wl,-z,relro,-z,now',
            str(common_o), str(daemon_o), '-o', str(daemon_out),
        ])

        zygisk_out = stage / 'zygisk' / 'arm64-v8a.so'
        subprocess.check_call([
            str(cc_path), '-shared', '-Wl,-z,noexecstack', '-Wl,-z,relro,-z,now',
            str(zygisk_o), str(common_o), str(vk_hook_o),
            '-llog', '-ldl', '-o', str(zygisk_out),
        ])

        if strip_bin:
            subprocess.check_call([str(strip_bin), '--strip-unneeded', str(daemon_out)])
            subprocess.check_call([str(strip_bin), '--strip-unneeded', str(zygisk_out)])

        check_elf_deps_and_exports(daemon_out, readelf_bin, ALLOWED_DAEMON_NEEDED)
        check_elf_deps_and_exports(
            zygisk_out,
            readelf_bin,
            ALLOWED_ZYGISK_NEEDED,
            required_dynsyms=('zygisk_module_entry', 'zygisk_companion_entry'),
        )

        # Normalize text files to LF and write deterministic KSU zip
        files_to_pack = sorted(
            p for p in stage.rglob('*') if p.is_file()
        )
        with zipfile.ZipFile(out_zip, 'w', compression=zipfile.ZIP_DEFLATED) as zf:
            for src_file in files_to_pack:
                rel = src_file.relative_to(stage).as_posix()
                data = src_file.read_bytes()
                if rel.endswith(('.sh', '.conf', '.prop')) or rel.endswith(('update-binary', 'updater-script')):
                    data = data.replace(b'\r\n', b'\n')
                zi = zipfile.ZipInfo(rel, date_time=(2026, 11, 15, 0, 0, 0))
                zi.compress_type = zipfile.ZIP_DEFLATED
                mode = 0o755 if rel in EXEC_FILES else 0o644
                zi.external_attr = (stat.S_IFREG | mode) << 16
                zf.writestr(zi, data)

    return out_zip


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', default=str(DEFAULT_OUT_ZIP), help='Output KSU zip path')
    parser.add_argument('--cc', default=None, help='Path to aarch64-linux-android35-clang')
    args = parser.parse_args()
    built = build_package(out_zip=Path(args.out), cc=args.cc)
    print(f'Built {built} ({built.stat().st_size} bytes)')


if __name__ == '__main__':
    main()
