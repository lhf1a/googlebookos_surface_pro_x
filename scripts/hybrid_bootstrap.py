"""Offline, explicit-workspace dependency materialization and source build stages."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def load_module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT/'scripts'/filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


deps = load_module('hybrid_import', 'import-hybrid-dependencies.py')
digest = deps.digest


def copy_range(source, target, offset, size):
    if offset < 0 or size <= 0 or offset+size > source.stat().st_size:
        raise ValueError('Invalid source range')
    target.parent.mkdir(parents=True, exist_ok=True)
    with source.open('rb') as src, target.open('xb') as dst:
        src.seek(offset)
        while size:
            block = src.read(min(size, 8 << 20))
            if not block:
                raise ValueError('Truncated range')
            dst.write(block)
            size -= len(block)


def unzip(archive, target):
    """Preserve executable bits and in-tree SDK links; reject traversal/duplicates."""
    target.mkdir(parents=True, exist_ok=False)
    with zipfile.ZipFile(archive) as z:
        names = set()
        links = []
        for info in z.infolist():
            name = deps.safe_name(info.filename)
            if name in names:
                raise ValueError('Duplicate ZIP member')
            names.add(name)
            path = target/name
            mode = info.external_attr >> 16
            if info.is_dir():
                path.mkdir(parents=True, exist_ok=True)
            elif stat.S_ISLNK(mode):
                link = z.read(info).decode()
                if Path(link).is_absolute() or not (path.parent/link).resolve().is_relative_to(target.resolve()):
                    raise ValueError('External SDK link')
                links.append((path, link))
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                with z.open(info) as src, path.open('xb') as dst:
                    shutil.copyfileobj(src, dst, 8 << 20)
                path.chmod((mode & 0o777) or 0o644)
        for path, link in links:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.symlink_to(link)


def unsparse(source, target):
    with source.open('rb') as src, target.open('xb') as dst:
        header = src.read(28)
        if len(header) != 28:
            raise ValueError('Truncated sparse header')
        magic, major, minor, fh, ch, block, blocks, chunks, checksum = struct.unpack('<I4H4I', header)
        if magic != 0xed26ff3a or major != 1 or fh < 28 or ch < 12 or block != 4096 or checksum:
            raise ValueError('Unsupported sparse format/checksum')
        src.read(fh-28)
        written = 0
        for _ in range(chunks):
            h = src.read(ch)
            if len(h) != ch:
                raise ValueError('Truncated sparse chunk')
            kind, reserved, count, size = struct.unpack_from('<2H2I', h)
            length = count*block
            if written+length > blocks*block or size < ch:
                raise ValueError('Sparse extent outside image')
            if kind == 0xcac1 and size-ch == length:
                left = length
                while left:
                    b = src.read(min(left, 8 << 20))
                    if not b:
                        raise ValueError('Short sparse RAW chunk')
                    dst.write(b); left -= len(b)
            elif kind == 0xcac2 and size-ch == 4:
                fill = src.read(4)
                if len(fill) != 4:
                    raise ValueError('Short sparse FILL chunk')
                batch = fill*(min(length, 1 << 20)//4)
                left = length
                while left:
                    b = batch[:left]; dst.write(b); left -= len(b)
            elif kind == 0xcac3 and size == ch:
                dst.seek(length, 1)
            else:
                raise ValueError('Unsupported sparse chunk')
            written += length
        if written != blocks*block or src.read(1):
            raise ValueError('Sparse length/trailing bytes mismatch')
        dst.truncate(written)


def logical_partitions(source, output, base=0, total=None):
    """Extract all nonempty slot-A LP partitions after geometry/header/table hashes."""
    total = source.stat().st_size-base if total is None else total
    output.mkdir(parents=True, exist_ok=False)
    with source.open('rb') as src:
        def read(offset, size):
            if offset < 0 or size < 0 or offset+size > total:
                raise ValueError('LP read outside super')
            src.seek(base+offset); data = src.read(size)
            if len(data) != size:
                raise ValueError('Short LP read')
            return data
        u32 = lambda data, off: struct.unpack_from('<I', data, off)[0]
        geometry = read(4096, 4096)
        gs = u32(geometry, 4)
        if u32(geometry, 0) != 0x616c4467 or not 52 <= gs <= 4096:
            raise ValueError('Invalid LP geometry')
        checked = bytearray(geometry[:gs]); checked[8:40] = bytes(32)
        if hashlib.sha256(checked).digest() != geometry[8:40]:
            raise ValueError('LP geometry hash')
        metadata = read(12288, u32(geometry, 40)); hs = u32(metadata, 8)
        if u32(metadata, 0) != 0x414c5030 or not 128 <= hs <= len(metadata):
            raise ValueError('Invalid LP metadata')
        checked = bytearray(metadata[:hs]); checked[12:44] = bytes(32)
        if hashlib.sha256(checked).digest() != metadata[12:44]:
            raise ValueError('LP header hash')
        table = metadata[hs:hs+u32(metadata, 44)]
        if len(table) != u32(metadata, 44) or hashlib.sha256(table).digest() != metadata[48:80]:
            raise ValueError('LP table hash')
        po,pn,ps = struct.unpack_from('<III', metadata, 80)
        eo,en,es = struct.unpack_from('<III', metadata, 92)
        if ps < 52 or es < 24 or po+pn*ps > len(table) or eo+en*es > len(table):
            raise ValueError('Invalid LP table bounds')
        results = {}
        for i in range(pn):
            p = table[po+i*ps:po+(i+1)*ps]
            name = p[:36].split(b'\0')[0].decode('ascii')
            if not name or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_' for c in name):
                raise ValueError('Invalid partition name')
            first,count = struct.unpack_from('<II', p, 40)
            if first+count > en:
                raise ValueError('Invalid LP extent index')
            if not name.endswith('_a') or not count:
                continue
            dest = output/(name+'.img')
            with dest.open('xb') as dst:
                for j in range(first, first+count):
                    sectors,kind,sector,device = struct.unpack_from('<QIQI', table, eo+j*es)
                    length = sectors*512
                    if kind not in (0,1) or device != 0 or length > total:
                        raise ValueError('Unsupported LP extent')
                    if kind == 1:
                        dst.seek(length, 1)
                    else:
                        if sector*512+length > total:
                            raise ValueError('LP extent outside super')
                        src.seek(base+sector*512)
                        while length:
                            b = src.read(min(length, 8 << 20))
                            if not b: raise ValueError('Short LP extent')
                            dst.write(b); length -= len(b)
                dst.truncate(dst.tell())
            results[name] = {'path': str(dest), 'bytes': dest.stat().st_size, 'sha256': digest(dest)}
        (output/'PARTITIONS.json').write_text(json.dumps(results, indent=2)+'\n')
        return results


class Bootstrap:
    def __init__(self, imported, work):
        self.imported = Path(imported).resolve() if imported else None
        self.work = Path(work).resolve()
        self.work.mkdir(parents=True, exist_ok=False)
        self.spec = json.loads((ROOT/'configs/hybrid-dependencies.json').read_text())
        self.report = {'schema': 1, 'steps': [], 'image_built': False, 'commands': []}
        self.save()

    def save(self):
        (self.work/'BOOTSTRAP.json').write_text(json.dumps(self.report, indent=2)+'\n')

    def run(self, label, command, env=None):
        log = self.work/(label+'.log')
        with log.open('wb') as stream:
            result = subprocess.run(list(map(str, command)), stdout=stream, stderr=subprocess.STDOUT, env=env)
        self.report['commands'].append({'label': label, 'command': list(map(str, command)),
                                       'exit': result.returncode, 'log': str(log), 'sha256': digest(log)})
        self.save()
        if result.returncode:
            raise RuntimeError(f'{label} failed (exit {result.returncode}): {log}')

    def input(self, id):
        item = next(x for x in self.spec['downloads'] if x['id'] == id)
        path = self.imported/'downloads'/item['file']
        if digest(path, item['algorithm'].lower()) != item['hash']:
            raise ValueError('Pinned input mismatch: '+id)
        return path

    def sources(self):
        out = self.work/'sources'; out.mkdir()
        missing = []
        env = {k:v for k,v in os.environ.items() if not k.startswith('GIT_')}
        env.update(GIT_CONFIG_NOSYSTEM='1', GIT_CONFIG_GLOBAL='/dev/null', GIT_TERMINAL_PROMPT='0')
        for item in self.spec['repositories']:
            bundle = self.imported/'repositories'/(item['id']+'.bundle')
            if not bundle.exists():
                missing.append(item['id']); continue
            dest = out/item['id']
            self.run('clone-'+item['id'], ['git','-c','core.autocrlf=false','clone','--no-checkout',bundle,dest], env)
            self.run('checkout-'+item['id'], ['git','-C',dest,'checkout','--detach',item['commit']], env)
            actual = subprocess.check_output(['git','-C',str(dest),'rev-parse','HEAD'], env=env, text=True).strip()
            if actual != item['commit']: raise ValueError('Source commit mismatch')
        self.report['missing_sources'] = missing; self.save()
        if missing: raise ValueError('Missing Git bundles: '+', '.join(missing))

    def toolchains(self):
        tools = self.work/'toolchains/sq2-graphics'; tools.mkdir(parents=True)
        unpack = self.work/'unpacked-ndk'; unzip(self.input('ndk'), unpack)
        shutil.move(unpack/'android-ndk-r28c', tools/'android-ndk-r28c')
        sdk = tools/'sdk'; sdk.mkdir()
        for id in ('sdk-platform','sdk-build-tools'):
            dest = self.work/('unpacked-'+id); unzip(self.input(id), dest)
            for child in dest.iterdir(): shutil.move(child, sdk/child.name)
        src = tools/'src'; src.mkdir()
        for id in ('mesa','libdrm'):
            with tarfile.open(self.input(id)) as archive:
                archive.extractall(src, filter='data')
        return tools/'android-ndk-r28c'

    def quest(self):
        cfg = json.loads((ROOT/'configs/quest2-full-donor-candidate.json').read_text())
        out = self.work/'quest2'; out.mkdir()
        start,end = cfg['vendor']['archive_range_inclusive']
        full = self.imported/'downloads/q2_51584880050400150.zip'
        archive_sha = None
        if full.exists():
            archive = self.input('quest2-v77-donor')
            if archive.stat().st_size != cfg['archive_bytes']: raise ValueError('Wrong OTA size')
            copy_range(archive,out/'prefix.bin',0,cfg['payload_data_absolute_offset'])
            copy_range(archive,out/'vendor-range.bin',start,end-start+1)
            archive_sha = digest(archive)
        else:
            shutil.copyfile(self.input('quest2-v77-prefix'),out/'prefix.bin')
            shutil.copyfile(self.input('quest2-v77-vendor-range'),out/'vendor-range.bin')
        self.run('quest-rebuild',[sys.executable,ROOT/'scripts/rebuild-quest2-v77-vendor.py',
                 out/'prefix.bin',out/'vendor-range.bin',out/'vendor.img','--work-root',self.work])
        self.run('quest-extract',[sys.executable,ROOT/'scripts/extract-quest2-v77-reference.py',
                 out/'vendor.img',out/'reference','--work-root',self.work])
        self.report['quest'] = {'archive_sha256': archive_sha, 'vendor_sha256': digest(out/'vendor.img'),
                                'prefix_sha256': digest(out/'prefix.bin'), 'range_sha256': digest(out/'vendor-range.bin'),
                                'oem_signature_verified': False}
        self.save()

    def android(self):
        out = self.work/'android'; out.mkdir()
        raw = out/'mica-recovery.raw'
        with zipfile.ZipFile(self.input('googlebook')) as z, z.open('android-desktop_signed_recovery_image.bin') as src, raw.open('xb') as dst:
            shutil.copyfileobj(src, dst, 8 << 20)
        pin = json.loads((ROOT/'configs/sources.lock.json').read_text())['googlebook']['raw_sha256']
        if digest(raw) != pin: raise ValueError('Recovery RAW hash mismatch')
        layout = load_module('recovery_inspect','inspect-recovery.py').inspect(raw)
        superpart = next(p for p in layout['partitions'] if p['name']=='super')
        logical_partitions(raw,out/'mica',superpart['offset'],superpart['bytes'])
        sparse = out/'cuttlefish-super.sparse'
        with zipfile.ZipFile(self.input('cuttlefish')) as z, z.open('super.img') as src, sparse.open('xb') as dst:
            shutil.copyfileobj(src,dst,8 << 20)
        unsparse(sparse,out/'cuttlefish-super.raw')
        logical_partitions(out/'cuttlefish-super.raw',out/'cuttlefish')
        # Export read-only filesystems for platform libraries. Never mount them.
        for group in ('mica','cuttlefish'):
            for image in sorted((out/group).glob('*.img')):
                dest = out/(group+'-files')/image.stem
                dest.parent.mkdir(parents=True,exist_ok=True)
                self.run('extract-'+group+'-'+image.stem,
                         ['fsck.erofs','--no-preserve','--extract='+str(dest),image])

    def glue(self, ndk):
        ndk = Path(ndk).resolve()
        cc = ndk/'toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang'
        # Do not permit legacy builder fallback to another host's cached NDK.
        if not cc.is_file() or not os.access(cc, os.X_OK): raise ValueError('Explicit staged NDK compiler missing')
        out = self.work/'runtime-overlay'
        outputs = {}
        for label, script, rel in (
            ('vulkan-router','build_hybrid_vulkan_router.py','etc/googlebook-security/lib64/hw/vulkan.freedreno.so'),
            ('gralloc-adapter','build_mesa_qti_gralloc.py','etc/sq2-qti-system/root/lib64/hw/gralloc.sq2_mesa.so')):
            target = out/rel
            self.run(label,[sys.executable,ROOT/'scripts'/script,'--cc',cc,'--output',target])
            outputs[str(target.relative_to(self.work/'runtime-overlay'))] = digest(target)
        self.report['compiled_overlay'] = outputs
        self.report['compiler_sha256'] = digest(cc.resolve())
        self.save()


def main():
    import argparse
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--imported', type=Path, help='directory verified by import-hybrid-dependencies.py')
    p.add_argument('--work', type=Path, required=True, help='fresh Linux filesystem directory')
    p.add_argument('--stage', choices=('sources','toolchains','kernel','quest','android','platform','mesa','glue','assemble','all'), default='all')
    p.add_argument('--reuse-components', type=Path, help='completed components manifest for assemble stage')
    p.add_argument('--private-root', type=Path, help='explicit prepared runtime/EFI/firmware inputs for assemble stage')
    p.add_argument('--template', type=Path, help='optional explicit disk template for assemble stage')
    p.add_argument('--ndk', type=Path, help='explicit NDK for isolated glue-stage testing')
    p.add_argument('--sources-root', type=Path, help='explicit source checkout root for isolated platform test')
    p.add_argument('--toolchains-root', type=Path, help='explicit toolchains root for isolated platform test')
    p.add_argument('--platform-files', type=Path, help='explicit extracted platform root for isolated test')
    p.add_argument('--quest-vendor', type=Path, help='explicit verified Quest vendor root for isolated platform test')
    p.add_argument('--jobs', type=int, default=8)
    a = p.parse_args()
    if a.stage not in ('glue','assemble') and not a.imported: p.error('--imported required')
    if a.stage == 'assemble' and not (a.reuse_components and a.private_root):
        p.error('--stage assemble requires --reuse-components and --private-root')
    if a.stage in ('glue','mesa') and not a.ndk: p.error('--ndk required for isolated glue/mesa stage')
    b = Bootstrap(a.imported,a.work)
    try:
        if a.stage == 'assemble':
            command = [sys.executable,ROOT/'scripts/build.py','--reuse-components',a.reuse_components,
                       '--private-root',a.private_root,'--output',b.work/'image','--jobs',a.jobs]
            if a.template: command += ['--template',a.template]
            b.run('assemble',command)
            b.report['image_built'] = True
            b.report['image_manifest'] = str(b.work/'image/FULL-IMAGE.json')
            b.report['steps'].append('assemble'); b.save()
            print(json.dumps(b.report,indent=2)); return 0
        ndk = a.ndk
        for step in ('sources','toolchains','kernel','quest','android','platform','mesa','glue'):
            if a.stage not in ('all',step): continue
            if step == 'glue': b.glue(ndk)
            elif step == 'toolchains': ndk = b.toolchains()
            elif step == 'kernel':
                from hybrid_bootstrap_platform import kernel
                kernel(b)
            elif step == 'platform':
                from hybrid_bootstrap_platform import platform, camera_graphics
                platform(b, a.sources_root, a.toolchains_root, a.platform_files)
                camera_graphics(b, interfaces=(a.sources_root/'aosp-camera-sensor-interfaces') if a.sources_root else None,
                                quest_vendor=a.quest_vendor)
            elif step == 'mesa':
                from hybrid_bootstrap_mesa import mesa
                mesa(b, ndk, a.jobs)
            else: getattr(b,step)()
            b.report['steps'].append(step); b.save()
        if a.stage == 'all':
            raise RuntimeError('Source preparation completed, not a full image. Use build-spx-sensor-runtime.py for Windows OEM/RPC/EFI, then --stage assemble with an explicit completed-component manifest and prepared runtime. Clean-room HWC/OMX/runtime preparation is not yet covered by --stage all.')
        print(json.dumps(b.report,indent=2))
        return 0
    except Exception as exc:
        b.report['failure'] = str(exc); b.save(); print(str(exc),file=sys.stderr); return 1


if __name__ == '__main__':
    sys.exit(main())
