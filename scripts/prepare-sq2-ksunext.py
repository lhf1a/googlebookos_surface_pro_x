#!/usr/bin/env python3
"""Prepare an isolated KernelSU Next experiment; never deploy or alter boot media."""
from pathlib import Path
import hashlib
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
WORK = Path('/root/.cache/sq2-ksunext')
KERNEL = WORK / 'kernel'
ARCHIVE = ROOT / 'downloads/surface-kernel-b86f786.tar.gz'
PIN = '9b08e88862000d5c50fb2e43a5b75123cf472e54'

def run(*args):
    subprocess.run(args, check=True)

assert hashlib.sha256(ARCHIVE.read_bytes()).hexdigest() == '275f05c5608c5906a1fe43dbc001a63c06296bee54e6fd29cb2ff440b488cdb5'
repo = WORK / 'next-v3.2'
if not repo.exists():
    WORK.mkdir(parents=True, exist_ok=True)
    run('git', 'clone', '--branch', 'v3.2.0-legacy', '--single-branch',
        'https://github.com/KernelSU-Next/KernelSU-Next.git', str(repo))
assert subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip() == PIN
if KERNEL.exists():
    raise SystemExit('Refusing to overwrite an existing experiment tree')
KERNEL.mkdir(parents=True)
run('tar', '-xf', str(ARCHIVE), '--strip-components=1', '-C', str(KERNEL))
# Reuse the existing, reviewed port patches but redirect their fixed historical
# cache path. Their repository-relative inputs continue to resolve normally.
for name in ('prepare-memfd-compat.py', 'patch-sq2-bluetooth.py', 'patch-sq2-audio.py'):
    path = ROOT / 'scripts' / name
    source = path.read_text().replace('/root/.cache/sq2-probe-build/kernel', str(KERNEL))
    source = source.replace("root/'out/connectivity/bluetooth-kernel-patch.json'", "Path('/root/.cache/sq2-ksunext/bluetooth-kernel-patch.json')")
    exec(compile(source, str(path), 'exec'), {'__file__': str(path), '__name__': '__main__'})
run('python3', str(ROOT / 'scripts/patch-sq2-screen-only.py'), str(KERNEL))
shutil.copytree(repo / 'kernel', KERNEL / 'drivers/kernelsu', ignore=shutil.ignore_patterns('*.o', '*.ko', '*.cmd', '*.mod', '*.mod.c', 'Module.symvers', 'modules.order'))
run('python3', str(ROOT / 'scripts/patch-sq2-ksunext-63.py'), str(KERNEL / 'drivers/kernelsu'))
with (KERNEL / 'drivers/Makefile').open('a') as f:
    f.write('\nobj-$(CONFIG_KSU) += kernelsu/\n')
with (KERNEL / 'drivers/Kconfig').open('a') as f:
    f.write('\nsource "drivers/kernelsu/Kconfig"\n')
build = WORK / 'builtin-build'
build.mkdir()
shutil.copyfile(ROOT / 'configs/sq2-internal-kernel.config', build / '.config')
run(str(KERNEL / 'scripts/config'), '--file', str(build / '.config'), '--enable', 'KSU', '--enable', 'KSU_KPROBES_HOOK')
run('make', '-s', '-C', str(KERNEL), 'O='+str(build), 'ARCH=arm64',
    'CROSS_COMPILE=aarch64-linux-gnu-', 'olddefconfig')
print(f'Prepared {KERNEL}; pinned Next {PIN}; compile-only, not deployment-ready')
