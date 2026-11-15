#!/usr/bin/env python3
"""Unified Linux 6.18 LTS entry, including original-template full-image builds.

full-image accepts --graphics baseline|mesa|hybrid. The mesa mode assembles the
full Mesa userspace chain (Mesa freedreno GLES/Vulkan, minigbm, drm HWC3 client
composition), while hybrid combines Mesa Freedreno GLES + Turnip Vulkan with QTI
gralloc + mapper5 + source QTI HWC/SDM + native-surface OMX, applying the repo
A6xx kernel patch series unless --kernel-patches overrides it. Both are
build-time selections only; no rendering or hardware validation is implied.

package is an exact private baseline copy, NOT a source compilation or disk
image. compile produces unvalidated source-build artifacts; it never splices a
new Image into an existing initramfs. single-user creates a separate fresh-data
initramfs candidate, not a migration. No command deploys, reboots or writes disks.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys

from kernel_baseline import ROOT, digest, policy, validate_build_tree, validate_kernel_source

DEFAULT_BUNDLE = ROOT / 'private/build-inputs/kernel-6.18' / policy()['bundle_directory']
REQUIRED = {'Image', 'android-el2.dtb', 'el2launch.efi', 'initramfs.cpio.gz',
            'kernel-options.txt', 'qebspilaa64.efi', 'start.efi', 'tcblaunch.exe'}
MSM_DISPLAY_PREFIXES = ('disp/', 'dp/', 'dsi/', 'hdmi/', 'registers/display/', 'generated/')
MSM_DISPLAY_FILES = {'Makefile', 'msm_kms.c', 'msm_kms.h', 'msm_atomic.c',
                     'msm_atomic_trace.h', 'msm_atomic_tracepoints.c',
                     'msm_fb.c', 'msm_fbdev.c'}


def validate_msm_gpu_sync(kernel_msm, external_msm):
    """Reject external msm.ko overrides whose non-display GPU/core files lag kernel source."""
    kernel_msm, external_msm = Path(kernel_msm), Path(external_msm)
    if not (kernel_msm / 'adreno').is_dir():
        return
    def gpu_files(root):
        result = {}
        for path in sorted(root.rglob('*')):
            if not path.is_file() or path.is_symlink():
                continue
            rel = path.relative_to(root).as_posix()
            if rel in MSM_DISPLAY_FILES or rel.startswith(MSM_DISPLAY_PREFIXES):
                continue
            result[rel] = path
        return result
    kern = gpu_files(kernel_msm)
    ext = gpu_files(external_msm)
    diverged = sorted((set(kern) ^ set(ext)) |
                      {rel for rel in (set(kern) & set(ext))
                       if kern[rel].read_bytes() != ext[rel].read_bytes()})
    if diverged:
        raise ValueError('External msm.ko GPU sources diverge from kernel source: '
                         + ', '.join(diverged[:8]))


def external_manifest(directory, selected):
    value = json.loads((ROOT / 'configs/kernel-external-modules.json').read_text())
    if value.get('schema') != 1 or value.get('kernel_release') != selected['kernel_release']:
        raise ValueError('External module manifest ABI mismatch')
    root = Path(directory).resolve()
    destinations = set()
    for group in value['groups']:
        source = (root / group['source']).resolve()
        if root not in source.parents or not source.is_dir():
            raise ValueError('Missing/escaping external source: ' + group['source'])
        if not any((source / name).is_file() for name in ('Makefile', 'Kbuild')):
            raise ValueError('Missing external Kbuild: ' + group['source'])
        for option in group.get('make_options', []):
            if not re.fullmatch(r'CONFIG_[A-Z0-9_]+=[ymn]', option):
                raise ValueError('Invalid external make option')
        for name, destination in group['modules'].items():
            relative = Path(destination)
            if Path(name).name != name or not name.endswith('.ko') or relative.is_absolute() or '..' in relative.parts or relative.name != name:
                raise ValueError('Invalid external module path')
            if destination in destinations:
                raise ValueError('Duplicate external module destination')
            destinations.add(destination)
    # Keep copied builds self-contained; an archive link must not reach private
    # files or an unrelated source tree outside the explicit external root.
    for path in root.rglob('*'):
        if path.is_symlink() and root not in path.resolve().parents:
            raise ValueError('External symlink escapes source root: ' + str(path))
    return value


def validate_bundle(directory, selected=None):
    selected = selected or policy()
    directory = Path(directory)
    files = selected['files']
    if set(files) != REQUIRED:
        raise ValueError('Baseline file list is incomplete or contains unexpected files')
    for name, expected in files.items():
        if not re.fullmatch(r'[0-9a-f]{64}', expected):
            raise ValueError('Invalid SHA256: ' + name)
        path = directory / name
        if path.is_symlink() or not path.is_file() or not stat.S_ISREG(path.stat().st_mode):
            raise ValueError('Expected a regular baseline file: ' + name)
        if digest(path) != expected:
            raise ValueError('Baseline checksum mismatch: ' + name)
    sums = {}
    for line in (directory / 'SHA256SUMS').read_text().splitlines():
        h, name = line.split(None, 1)
        name = name.lstrip('*')
        if name in sums:
            raise ValueError('Duplicate checksum entry: ' + name)
        sums[name] = h
    if sums != files:
        raise ValueError('SHA256SUMS differs from selected baseline')
    options = (directory / 'kernel-options.txt').read_text().split()
    for option in ('sq2.el2=1', 'nohibernate', 'suspend.screen_only=0',
                   'suspend.screen_only_autosuspend=0'):
        if options.count(option) != 1:
            raise ValueError('Missing/ambiguous accepted boot policy: ' + option)
    return dict(files)


def fresh(directory):
    directory = Path(directory)
    if directory.exists() or directory.is_symlink():
        raise ValueError('Output must be fresh: ' + str(directory))
    directory.mkdir(parents=True)


def package(directory, output, selected=None):
    selected = selected or policy()
    relative = Path(selected['bundle_directory'])
    if relative.is_absolute() or '..' in relative.parts or not relative.parts:
        raise ValueError('Invalid relative baseline bundle directory')
    hashes = validate_bundle(directory, selected)
    fresh(output)
    target = Path(output) / relative
    target.mkdir(parents=True)
    for name in sorted(REQUIRED | {'SHA256SUMS'}):
        shutil.copyfile(Path(directory) / name, target / name)
    validate_bundle(target, selected)
    report = {'operation': 'exact-baseline-package', 'kernel_release': selected['kernel_release'],
              'files': hashes, 'source_compiled': False, 'device_written': False,
              'private_identity_present': True, 'anonymous_public_image': False,
              'new_hardware_test_performed': False, 'bundle_directory': selected['bundle_directory'],
              'scope': selected.get('hardware_scope', 'SQ2 observed; SQ1 not revalidated'),
              'note': 'Scene user-data symlink is device state, not embedded into initramfs.'}
    (Path(output) / 'build-audit.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def compile_kernel(args, selected=None):
    selected = selected or policy()
    source, build, output = args.source.resolve(), args.build_dir.resolve(), args.output.resolve()
    validate_kernel_source(source, selected)
    if not args.core_only and args.external_sources is None:
        raise ValueError('Full baseline compilation requires --external-sources; use --core-only only for explicitly incomplete core outputs')
    external = external_manifest(args.external_sources, selected) if args.external_sources else None
    if external:
        external_root = args.external_sources.resolve()
        if external_root == output or external_root in output.parents or external_root == build or external_root in build.parents:
            raise ValueError('Build/export directories must be outside preserved external sources')
        for group in external['groups']:
            if 'msm.ko' in group['modules']:
                validate_msm_gpu_sync(source / 'drivers/gpu/drm/msm', external_root / group['source'])
    if not args.config.is_file():
        raise ValueError('An explicit accepted full .config is required')
    if source == build or source in build.parents:
        raise ValueError('Use a separate build directory outside the pristine source tree')
    if source == output or source in output.parents:
        raise ValueError('Export must stay outside the pristine source tree')
    if output == build or output in build.parents or build in output.parents:
        raise ValueError('Export and build directories must be separate')
    config = args.config.read_bytes()
    if b'CONFIG_LOCALVERSION="-sq2-android1"' not in config:
        raise ValueError('Config localversion does not match accepted module ABI')
    if args.resume_build:
        validate_build_tree(build, selected)
        if (build / '.config').read_bytes() != config:
            raise ValueError('Resume build .config differs from supplied config')
    else:
        fresh(build)
        (build / '.config').write_bytes(config)
    fresh(output)
    command = ['make', '-C', str(source), 'O=' + str(build), 'ARCH=arm64',
               'CROSS_COMPILE=' + args.cross_compile]
    records = []
    def run(label, extra, prefix=None):
        cmd = (prefix or command) + extra
        with (output / (label + '.stdout')).open('wb') as stdout, (output / (label + '.stderr')).open('wb') as stderr:
            result = subprocess.run(cmd, stdout=stdout, stderr=stderr)
        records.append({'command': cmd, 'exit': result.returncode,
                        'stdout': label + '.stdout', 'stderr': label + '.stderr'})
        (output / 'commands.json').write_text(json.dumps(records, indent=2) + '\n')
        if result.returncode:
            raise RuntimeError('Compilation failed; inspect ' + str(output / (label + '.stderr')))
    run('olddefconfig', ['olddefconfig'])
    run('kernel', ['-j' + str(args.jobs), 'Image', 'modules', 'dtbs'])
    validate_build_tree(build, selected)
    stage = output / 'modules-stage'
    run('modules-install', ['INSTALL_MOD_PATH=' + str(stage), 'modules_install'])
    external_results = []
    if external:
        work = output / 'external-build'
        shutil.copytree(args.external_sources, work)
        prefix = ['make', '-C', str(build), 'ARCH=arm64', 'CROSS_COMPILE=' + args.cross_compile]
        module_root = stage / 'lib/modules' / selected['kernel_release']
        for number, group in enumerate(external['groups']):
            module_dir = work / group['source']
            label = 'external-' + str(number)
            if 'spi-hid.ko' in group['modules']:
                # The copied 6.18 sources can retain V1 PM but lose V2 input
                # recovery. Reapply idempotently to the copy, never the input.
                run(label + '-resume-recovery', [str(module_dir)],
                    [sys.executable, str(ROOT / 'scripts/patch-sq2-spi-pm.py')])
            if 'venus-core.ko' in group['modules'] and (module_dir / 'venus-sq2-avc-export.h').is_file():
                # The preserved source archive copied raw probe/video/*.h over
                # venus-sq2-core.h and venus-sq2-system-pm.h, dropping the PM
                # core publish call and sq2_*_caps #include. Restore on copy.
                run(label + '-venus-hooks', ['--ensure-venus-hooks', str(module_dir)],
                    [sys.executable, str(ROOT / 'scripts/full_image_video.py')])
            options = ['M=' + str(module_dir)] + group.get('make_options', [])
            run(label + '-clean', options + ['clean'], prefix)
            run(label, options + ['-j' + str(args.jobs), 'modules'], prefix)
            for name, destination in group['modules'].items():
                compiled = module_dir / name
                body = compiled.read_bytes()
                magic = re.findall(rb'(?:^|\x00)vermagic=([^\x00]+)', body)
                if not body.startswith(b'\x7fELF') or not magic or any(m.split()[0].decode() != selected['kernel_release'] for m in magic):
                    raise ValueError('External module ABI mismatch: ' + name)
                target = module_root / destination
                target.parent.mkdir(parents=True, exist_ok=True)
                removed = []
                for old in module_root.rglob(name):
                    if old != target:
                        removed.append(old.relative_to(module_root).as_posix())
                        old.unlink()
                shutil.copyfile(compiled, target)
                external_results.append({'module': destination, 'sha256': None if getattr(args, 'build_only', False) else digest(target),
                                         'removed_duplicate_paths': removed})
        run('depmod', ['-b', str(stage), '-F', str(build / 'System.map'), selected['kernel_release']], ['depmod'])
    for src, dst in [('arch/arm64/boot/Image', 'Image'), ('System.map', 'System.map'),
                     ('.config', 'kernel.config'), ('Module.symvers', 'Module.symvers')]:
        shutil.copyfile(build / src, output / dst)
    dtbs = output / 'dtbs'
    dtbs.mkdir()
    count = 0
    for f in (build / 'arch/arm64/boot/dts').rglob('*.dtb'):
        relative = f.relative_to(build / 'arch/arm64/boot/dts')
        target = dtbs / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(f, target)
        count += 1
    if count == 0:
        raise ValueError('No DTBs exported')
    # modules_install can add absolute source/build links. They are not runtime
    # modules or useful export artifacts; remove links only, never their targets.
    for name in ('build', 'source'):
        link = stage / 'lib/modules' / selected['kernel_release'] / name
        if link.is_symlink():
            link.unlink()
    hashes = {} if getattr(args, 'build_only', False) else {p.relative_to(output).as_posix(): digest(p) for p in output.rglob('*')
              if p.is_file() and p.relative_to(output).parts[0] != 'external-build'
              and p.suffix not in ('.stdout', '.stderr') and p.name != 'commands.json'}
    report = {'operation': 'source-compilation', 'kernel_release': selected['kernel_release'],
              'source_compiled': True, 'device_written': False, 'hardware_tested': False,
              'accepted_baseline_replaced': False, 'external_runtime_overrides_integrated': bool(external),
              'external_modules': external_results, 'core_only': args.core_only,
              'input_config_sha256': None if getattr(args, 'build_only', False) else __import__('hashlib').sha256(config).hexdigest(),
              'files': hashes, 'commands': records,
              'note': 'This is not an accepted boot bundle. Generated Image, module stage and DTBs require explicit initramfs/helper/EFI pairing and runtime validation; package always preserves the separately pinned accepted bundle.'}
    (output / 'build-audit.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='operation', required=True)
    from full_image import DEFAULT_PRIVATE
    install = commands.add_parser('import-private', help='import standalone runtime/firmware/EFI dependencies into the fixed private directory')
    install.add_argument('--resources', type=Path, required=True)
    install.add_argument('--private-root', type=Path, default=DEFAULT_PRIVATE)
    full = commands.add_parser('full-image', help='build an original-template single-user whole disk; source kernel is the default')
    full.add_argument('--private-root', type=Path, default=DEFAULT_PRIVATE)
    full.add_argument('--template', type=Path, help='default: PRIVATE_ROOT/template.img; size/layout are retained exactly')
    full.add_argument('--kernel-mode', choices=('source','prebuilt'), default='source')
    full.add_argument('--graphics', choices=('baseline', 'mesa', 'hybrid'), default='baseline',
                      help='baseline: accepted mixed graphics stack; mesa: full Mesa/minigbm/drm-HWC3; hybrid: Mesa GLES/Turnip + QTI gralloc/mapper5/HWC')
    full.add_argument('--kernel-patches', type=Path,
                      help='apply every *.patch in this directory to the extracted kernel source (and external/edp-618) before compile; mesa and hybrid default to patches/drm-618-a6xx-upstream')
    full.add_argument('--esp-only', action='store_true',
                      help='build paired kernel/modules and native-SSD ESP payload only; skip the 20 GiB disk image')
    full.add_argument('--cross-compile', help='override the compiler; default extracts PRIVATE_ROOT/kernel-6.18/toolchain.tar.gz')
    full.add_argument('--build-dir', type=Path, help='fresh Linux filesystem kernel build directory')
    full.add_argument('--output', type=Path, required=True)
    full.add_argument('--jobs', type=int, default=8)
    verify = commands.add_parser('verify', help='verify the exact accepted private baseline')
    verify.add_argument('--baseline', type=Path, default=DEFAULT_BUNDLE)
    pack = commands.add_parser('package', help='copy baseline exactly; does NOT compile or build a disk image')
    pack.add_argument('--baseline', type=Path, default=DEFAULT_BUNDLE)
    pack.add_argument('--output', type=Path, required=True)
    single = commands.add_parser('single-user', help='prepare a fresh-userdata user-0 initramfs candidate; NOT a data migration or disk image')
    single.add_argument('--base', type=Path, required=True)
    single.add_argument('--expected-base-sha256', required=True)
    single.add_argument('--output', type=Path, required=True)
    disk = commands.add_parser('disk-image', help='assemble a fresh private 6.18 GPT disk image; never writes devices')
    disk.add_argument('--recovery-zip', type=Path, help='required only for an explicit custom recovery layout')
    disk.add_argument('--resources', type=Path, required=True)
    disk.add_argument('--single-user-initramfs', type=Path, required=True)
    disk.add_argument('--expected-single-user-sha256', help='legacy metadata option; universal disk builds no longer use hash gates')
    disk.add_argument('--output', type=Path, required=True)
    disk.add_argument('--size-gib', type=int, help='explicit custom recovery-layout capacity; no implicit 64 GiB default')
    disk.add_argument('--template', type=Path, help='preserve the original template layout and capacity')
    disk.add_argument('--jobs', type=int, default=8)
    disk.add_argument('--build-only', action='store_true', help='construct only; omit postbuild filesystem/partition/gzip readback checks and whole-image checksums')
    disk.add_argument('--resume-build', action='store_true', help='reuse this explicitly selected build directory and its completed pristine recovery extraction')
    build = commands.add_parser('compile', help='compile Image/modules/dtbs and export unvalidated outputs')
    build.add_argument('--source', type=Path, required=True)
    build.add_argument('--config', type=Path, default=ROOT / 'configs/kernel-6.18-baseline.config')
    build.add_argument('--build-dir', type=Path, required=True)
    build.add_argument('--output', type=Path, required=True)
    build.add_argument('--cross-compile', required=True, help='explicit compiler prefix, e.g. /tools/bin/aarch64-linux-gnu-')
    build.add_argument('--jobs', type=int, default=4)
    build.add_argument('--build-only', action='store_true', help='omit the final exported-file checksum inventory')
    build.add_argument('--resume-build', action='store_true', help='explicitly reuse an existing matching build directory/config')
    mode = build.add_mutually_exclusive_group()
    mode.add_argument('--core-only', action='store_true', help='explicit incomplete core compilation; excludes accepted external module overrides')
    mode.add_argument('--external-sources', type=Path, help='relocatable source root for the mandatory external module manifest')
    args = parser.parse_args(argv)
    try:
        if args.operation == 'import-private':
            from full_image import import_private
            report = import_private(args.resources, args.private_root)
        elif args.operation == 'full-image':
            from full_image import build as full_image_build
            report = full_image_build(args, compile_kernel)
        elif args.operation == 'verify':
            report = {'operation': 'verify', 'kernel_release': policy()['kernel_release'],
                      'files': validate_bundle(args.baseline), 'private_identity_present': True}
        elif args.operation == 'package':
            report = package(args.baseline, args.output)
        elif args.operation == 'single-user':
            from single_user_payload import build as single_user_build
            report = single_user_build(args.base, args.output, args.expected_base_sha256)
        elif args.operation == 'disk-image':
            from disk_image import build as disk_image_build
            if args.jobs < 1 or (not args.template and (args.size_gib is None or args.size_gib < 1)):
                parser.error('select --template or an explicit positive --size-gib; --jobs must be positive')
            if args.template and args.size_gib is not None:
                parser.error('--template preserves size; do not also set --size-gib')
            if not args.template and args.recovery_zip is None:
                parser.error('--recovery-zip is required without --template')
            report = disk_image_build(args)
        else:
            if args.jobs < 1:
                parser.error('--jobs must be positive')
            report = compile_kernel(args)
    except (ValueError, OSError, RuntimeError) as exc:
        parser.exit(1, str(exc) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
