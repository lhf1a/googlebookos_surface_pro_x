"""Original-template full image, standalone private inputs, paired source kernel.

Import is a one-time conversion of private runtime dependencies, not a build-time
parent-package dependency. Full builds never deploy or open block devices.
"""
import argparse
import gzip
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile

from kernel_baseline import ROOT, policy
from disk_image import module, regular, require
from single_user_payload import ANCHOR, HOOK

DEFAULT_PRIVATE = ROOT / 'private/build-inputs'


def records_from_gzip(path):
    """Read effective newc records, preserving ownership/device metadata."""
    records, bodies = {}, {}
    with gzip.open(path, 'rb') as f:
        while True:
            first = f.read(1)
            if not first:
                break
            if first == b'\0':
                continue
            h = first + f.read(109)
            require(h[:6] in (b'070701', b'070702') and len(h) == 110, 'Invalid newc input')
            fields = [int(h[6+i*8:14+i*8], 16) for i in range(13)]
            size, length = fields[6], fields[11]
            require(0 < length < 65536, 'Invalid newc name')
            name = f.read(length)[:-1].decode().removeprefix('./')
            require(not name.startswith('/') and '..' not in Path(name).parts, 'Unsafe newc path')
            f.read(-(110+length) % 4)
            body = f.read(size)
            require(len(body) == size, 'Truncated newc input')
            f.read(-size % 4)
            if name == 'TRAILER!!!':
                continue
            require(fields[4] <= 1 or fields[1] & 0o170000 != 0o100000, 'Unresolved newc hardlink')
            records[name] = [h[:6].decode(), fields, '']
            bodies[name] = body
    return records, bodies


def import_private(resources, private):
    """Never replace existing dependencies. Secrets stay below ignored private/."""
    resources, private = resources.resolve(), private.resolve()
    source = resources / 'private/build-inputs/kernel-6.18'
    dest = private / 'kernel-6.18'
    runtime = private / 'full-image-runtime'
    require(not runtime.exists(), 'Private runtime already imported; select an explicit new input root to update')
    # Existing historical source archive is retained; new release subdirectories
    # must be fresh so an import cannot silently overwrite an accepted bundle.
    for name in ('EFI/SQ2Nat', 'firmware', 'bootloader'):
        require(not (dest/name).exists(), 'Refusing to overwrite private dependency: '+name)
    for name in ('EFI/SQ2Nat', 'firmware', 'bootloader'):
        shutil.copytree(source/name, dest/name)
    shutil.copyfile(resources/'RESOURCE-MANIFEST.json', private/'RESOURCE-MANIFEST.json')
    records, bodies = records_from_gzip(source/'EFI/SQ2Nat/initramfs.cpio.gz')
    runtime.mkdir(parents=True)
    for name, body in bodies.items():
        # Store even symlink bodies as ordinary blobs; metadata controls the
        # eventual archive. No host ownership, device nodes or secret links.
        target = runtime/'files'/name
        if records[name][1][1] & 0o170000 == 0o040000:
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(body)
    (runtime/'records.json').write_text(json.dumps(records, indent=2)+'\n')
    # Preserve accepted board adaptations as explicit DTS input, not an opaque
    # parent DTB selected by a source-build mode.
    from board_dts import from_dtb
    (dest/'board.dts').write_text(from_dtb((source/'EFI/SQ2Nat/android-el2.dtb').read_bytes()))
    report = {'operation':'import-standalone-private-dependencies',
              'private_root':str(private),'runtime_entries':len(records),
              'source_baseline':policy()['baseline'],'source_commit':
              subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
              'credentials_present':True,'parent_needed_for_future_source_build':False}
    (runtime/'INPUTS.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


def new_record(mode):
    return ['070701',[0,mode,0,0,1,0,0,0,0,0,0,0,0],'']


def install_default_wallpaper(private, records, bodies):
    # Keep the original drawable names and Android wallpaper service. Only the
    # existing read-only resource APK is replaced; never touch /data wallpaper.
    wallpaper = private/'wallpaper/DefaultWallpaperOverlayMica.apk'
    if wallpaper.is_file():
        name = 'etc/DefaultWallpaperOverlayMica.apk'
        bodies[name] = wallpaper.read_bytes()
        records[name] = new_record(0o100644)
        hook = b'mount --bind /etc/DefaultWallpaperOverlayMica.apk /newroot/system_ext/overlay/DefaultWallpaperOverlayMica.apk || fail default-wallpaper\n'
        if hook not in bodies['init']:
            require(bodies['init'].count(ANCHOR) == 1, 'Unknown wallpaper handoff')
            bodies['init'] = bodies['init'].replace(ANCHOR, hook + ANCHOR)


def install_userdata_growth(records, bodies):
    import tempfile
    with tempfile.TemporaryDirectory(prefix='spx-grow-build-') as tmp:
        binary=Path(tmp)/'resize-userdata-partition'
        subprocess.run(['aarch64-linux-gnu-gcc','-static','-Os','-Wall','-Wextra',
                        str(ROOT/'probe/storage/resize-userdata-partition.c'),'-o',str(binary)],check=True)
        bodies['bin/resize-userdata-partition']=binary.read_bytes()
    for name in ('grow-image-userdata','surface-swap-boot'):
        bodies['bin/'+name]=(ROOT/'probe/test2'/name).read_bytes().replace(b'\r\n',b'\n')
    for name in ('resize-userdata-partition','grow-image-userdata','surface-swap-boot'):
        records['bin/'+name]=new_record(0o100755)

def assemble_runtime(private, output, compiled=None):
    runtime = private/'full-image-runtime'
    records = json.loads((runtime/'records.json').read_text())
    bodies = {name: b'' if rec[1][1] & 0o170000 == 0o040000 else (runtime/'files'/name).read_bytes()
              for name,rec in records.items()}
    if compiled:
        release = policy()['kernel_release']
        stage = compiled/'modules-stage'
        modules = stage/'lib/modules'/release
        by_name = {}
        for path in modules.rglob('*.ko'):
            require(path.name not in by_name, 'Ambiguous freshly built module: '+path.name)
            by_name[path.name] = path
        # Replace ALL effective module copies, including private overlay copies,
        # never silently retain parent modules with the same version string.
        for name in list(bodies):
            if name.endswith('.ko'):
                require(Path(name).name in by_name, 'Required runtime module not built: '+name)
                bodies[name] = by_name[Path(name).name].read_bytes()
        for path in modules.rglob('*'):
            if not path.is_file() or path.is_symlink():
                continue
            name = path.relative_to(stage).as_posix()
            bodies[name] = path.read_bytes()
            records[name] = new_record(0o100644)
    init = bodies['init']
    require(init.count(ANCHOR) == 1, 'Unknown Android handoff')
    init = re.sub(rb'(?m)^(\s*el2-terminal-autostart)[ \t]+[0-9]+(?=[ \t]*(?:>|&|\n|$))',rb'\1',init)
    if HOOK not in init:
        init = init.replace(ANCHOR,HOOK+ANCHOR)
    bodies['init'] = init
    bodies['bin/apply-single-user'] = (ROOT/'probe/test2/apply-single-user').read_bytes().replace(b'\r\n',b'\n')
    records['bin/apply-single-user'] = new_record(0o100755)
    # Take the current committed helper, while the accepted cumulative IPTSd
    # binary remains the fixed private runtime input (not the old touch cache).
    bodies['bin/touch-calibration-boot'] = (ROOT/'probe/test2/touch-calibration-boot').read_bytes().replace(b'\r\n',b'\n')
    # Universal builds do not rewrite reference hashes or gate boot on content
    # hashes. Preserve imported manifests as metadata; no startup consumer uses
    # them for validation after this policy is applied.
    from runtime_boot_policy import apply
    apply(bodies)
    from full_image_home import apply as apply_home
    apply_home(records, bodies, new_record)
    install_default_wallpaper(private, records, bodies)
    install_userdata_growth(records, bodies)
    # Add parent directory records for every newly installed module/index.
    for name in list(bodies):
        for parent in Path(name).parents:
            key=parent.as_posix()
            if key!='.' and key not in records:
                records[key]=new_record(0o040755);bodies[key]=b''
    encode=module('full_image_encode','pack-sq2-release.py').encode
    trailer=new_record(0)
    identity={'bin/dropbear','bin/start-ssh','etc/shadow'}
    require(identity <= bodies.keys(),'Incomplete private SSH identity')
    def archive(names):
        raw=b''.join(encode(n,bodies[n],records[n]) for n in names)
        raw+=encode('TRAILER!!!',b'',trailer)
        return gzip.compress(raw+b'\0'*(-len(raw)%512),mtime=0)
    # Directories before children; separate final credential member is retained.
    output.write_bytes(archive(sorted(bodies,key=lambda n:(n.count('/'),n)))+archive(sorted(identity)))


def build(args, compile_kernel):
    private=args.private_root.resolve()
    source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    template=regular(args.template or private/'template.img')
    require(args.jobs>0,'--jobs must be positive')
    require(not args.output.exists(),'Full build output must be fresh')
    args.output.mkdir(parents=True)
    records=[]
    def run(command):
        label='step-'+str(len(records))
        with (args.output/(label+'.stdout')).open('w') as out,(args.output/(label+'.stderr')).open('w') as err:
            result=subprocess.run(list(map(str,command)),stdout=out,stderr=err)
        records.append({'command':list(map(str,command)),'exit':result.returncode,
                        'stdout':label+'.stdout','stderr':label+'.stderr'})
        (args.output/'BUILD-COMMANDS.json').write_text(json.dumps(records,indent=2)+'\n')
        require(result.returncode==0,'Build failed: '+str(args.output/(label+'.stderr')))
    compiled=None
    if args.kernel_mode=='source':
        source=private/'kernel-6.18/source'
        recipe=json.loads((source/'SOURCE.json').read_text())
        require(recipe.get('current_release_patches') is True,'Source inputs are not prepared for current release')
        compiled=args.output/'kernel-export'
        build_dir=args.build_dir or args.output/'kernel-build'
        extracted=build_dir.parent/(build_dir.name+'-sources')
        require(not extracted.exists(),'Source extraction directory must be fresh: '+str(extracted))
        extracted.mkdir(parents=True)
        with tarfile.open(source/recipe['archive']) as archive:
            archive.extractall(extracted,filter='data')
        # The preserved archive predates the accepted QTI HWC kernel contract.
        # A source build must carry those adaptations, not replace the runtime
        # with an older MSM merely because its release string matches.
        from full_image_display import prepare as prepare_display
        display = prepare_display(extracted/'external')
        (args.output/'DISPLAY-SOURCE.json').write_text(json.dumps(display,indent=2)+'\n')
        # The preserved source-only archive omitted Git's include-prefix
        # symlinks. Recreate the standard local DTC include namespace from
        # existing source directories, never from an old host absolute path.
        prefixes=extracted/'kernel/scripts/dtc/include-prefixes'
        prefixes.mkdir(exist_ok=True)
        include_targets={'dt-bindings':extracted/'kernel/include/dt-bindings'}
        include_targets.update({p.name:p/'boot/dts' for p in (extracted/'kernel/arch').iterdir()
                                if (p/'boot/dts').is_dir()})
        for name,target in include_targets.items():
            link=prefixes/name
            if not link.exists() and not link.is_symlink():
                link.symlink_to(os.path.relpath(target,prefixes),target_is_directory=True)
        cross=args.cross_compile
        if cross is None:
            toolchain=build_dir.parent/(build_dir.name+'-toolchain')
            require(not toolchain.exists(),'Toolchain extraction directory must be fresh')
            toolchain.mkdir(parents=True)
            with tarfile.open(private/'kernel-6.18/toolchain.tar.gz') as archive:
                archive.extractall(toolchain,filter='data')
            cross=str(toolchain/'usr/bin/aarch64-linux-gnu-')
        run([sys.executable,ROOT/'scripts/build.py','compile','--source',extracted/'kernel',
             '--external-sources',extracted/'external','--config',ROOT/'configs/kernel-6.18-baseline.config',
             '--build-dir',build_dir,'--output',compiled,'--cross-compile',cross,
             '--jobs',str(args.jobs),'--build-only'])
    bundle=args.output/'paired/EFI/SQ2Nat'
    bundle.mkdir(parents=True)
    original=private/'kernel-6.18/EFI/SQ2Nat'
    for name in ('start.efi','el2launch.efi','qebspilaa64.efi','tcblaunch.exe','kernel-options.txt'):
        shutil.copyfile(original/name,bundle/name)
    if compiled:
        shutil.copyfile(compiled/'Image',bundle/'Image')
        run(['dtc','-q','-I','dts','-O','dtb','-o',bundle/'android-el2.dtb',private/'kernel-6.18/board.dts'])
    else:
        for name in ('Image','android-el2.dtb'):
            shutil.copyfile(original/name,bundle/name)
    assemble_runtime(private,bundle/'initramfs.cpio.gz',compiled)
    from disk_image import build as disk_build
    disk_args=argparse.Namespace(output=args.output/'disk',template=template,recovery_zip=None,
        resources=private,private_root=private,prepared_bundle=bundle,
        single_user_initramfs=bundle/'initramfs.cpio.gz',
        expected_single_user_sha256=None,
        size_gib=None,jobs=args.jobs,build_only=True,resume_build=False,kernel_mode=args.kernel_mode)
    report=disk_build(disk_args)
    report.update(source_compiled=bool(compiled),private_root=str(private),
        source_commit=source_commit,
        build_time_parent_initramfs_used=False,postbuild_tests_performed=False)
    (args.output/'FULL-IMAGE.json').write_text(json.dumps(report,indent=2)+'\n')
    return report
