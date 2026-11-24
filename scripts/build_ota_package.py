#!/usr/bin/env python3
"""Build a Surface Pro X Googlebook OS OTA update package (.tar.gz + manifest).

Automatically omits the super partition when super has not changed relative to
the previous baseline, packing only updated ESP files (content-deduplicated)
plus machine-readable update descriptors (ota-manifest.json, ota-manifest.kv,
ota-files.tsv). When super changes or --mode full is requested, packs super.img
alongside the ESP payload.
"""
import argparse
import datetime
import gzip
import hashlib
import io
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT / 'scripts') not in sys.path:
    sys.path.insert(0, str(ROOT / 'scripts'))

from kernel_baseline import policy

PRESERVED_ESP_PATHS = (
    'EFI/BOOT/grub.cfg',
    'SPX-USERDATA-GROWN.txt',
    'SPX-BOOT-SOURCE.txt',
)

PRESERVED_ESP_PREFIXES = (
    'SPX-OTA-',
    'EFI/SPX/LTE/',
)

CHUNK = 8 * 1024 * 1024


def is_preserved_esp_path(rel):
    if rel in PRESERVED_ESP_PATHS:
        return True
    return any(rel.startswith(prefix) for prefix in PRESERVED_ESP_PREFIXES)


def sha256_file(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        while True:
            buf = stream.read(CHUNK)
            if not buf:
                break
            h.update(buf)
    return h.hexdigest()


def sha256_region(path, offset, length):
    h = hashlib.sha256()
    left = length
    with Path(path).open('rb') as stream:
        stream.seek(offset)
        while left > 0:
            buf = stream.read(min(left, CHUNK))
            if not buf:
                raise ValueError(f'Short read while hashing {path} at {offset}+{length}')
            h.update(buf)
            left -= len(buf)
    return h.hexdigest()


def inspect_gpt_partitions(image_path):
    """Parse primary GPT from a raw disk image and return {name: {offset, bytes}}."""
    image_path = Path(image_path)
    with image_path.open('rb') as stream:
        head = stream.read(1024)
        if len(head) < 1024 or head[510:512] != b'\x55\xaa' or head[512:520] != b'EFI PART':
            raise ValueError(f'Invalid GPT image: {image_path}')
        hdr_size, hdr_crc = struct.unpack_from('<II', head, 512 + 12)
        if hdr_size < 92 or hdr_size > 512:
            raise ValueError('Invalid GPT header size')
        hdr_check = bytearray(head[512:512 + hdr_size])
        hdr_check[16:20] = b'\0\0\0\0'
        if (zlib.crc32(hdr_check) & 0xffffffff) != hdr_crc:
            raise ValueError('GPT header CRC mismatch')
        entries_lba, num_entries, entry_size, entries_crc = struct.unpack_from('<QIII', head, 512 + 72)
        stream.seek(entries_lba * 512)
        table = stream.read(num_entries * entry_size)
        if (zlib.crc32(table) & 0xffffffff) != entries_crc:
            raise ValueError('GPT entries CRC mismatch')
    parts = {}
    for i in range(num_entries):
        entry = table[i * entry_size:(i + 1) * entry_size]
        if not any(entry[:16]):
            continue
        first_lba, last_lba = struct.unpack_from('<QQ', entry, 32)
        if last_lba < first_lba:
            continue
        raw_name = entry[56:128].decode('utf-16le', errors='ignore').split('\0', 1)[0]
        parts[raw_name] = {
            'offset': first_lba * 512,
            'bytes': (last_lba - first_lba + 1) * 512,
        }
    return parts


def collect_esp_inventory(esp_tree):
    """Return sorted {relpath: {'path': Path, 'sha256': str, 'bytes': int}} for updatable ESP files."""
    esp_tree = Path(esp_tree).resolve()
    if not esp_tree.is_dir():
        raise ValueError(f'ESP tree not found: {esp_tree}')
    inventory = {}
    for path in sorted(esp_tree.rglob('*')):
        if not path.is_file() or path.is_symlink():
            continue
        rel = path.relative_to(esp_tree).as_posix()
        if is_preserved_esp_path(rel):
            continue
        inventory[rel] = {
            'path': path,
            'sha256': sha256_file(path),
            'bytes': path.stat().st_size,
        }
    if 'SPX-IMAGE.txt' not in inventory or 'EFI/SQ2Nat/Image' not in inventory or 'EFI/SQ2Nat/initramfs.cpio.gz' not in inventory:
        raise ValueError(f'Incomplete ESP tree (missing SPX-IMAGE.txt, Image, or initramfs.cpio.gz): {esp_tree}')
    return inventory


def load_previous_baseline(path):
    """Load previous super_sha256 and esp_files inventory from a manifest file or build directory."""
    if path is None:
        return None, None
    p = Path(path).resolve()
    if p.is_dir():
        for cand in (
            p / 'ota/OTA-MANIFEST.json',
            p / 'OTA-MANIFEST.json',
            p / 'FULL-IMAGE.json',
            p / 'disk/IMAGE-MANIFEST.json',
        ):
            if cand.is_file():
                p = cand
                break
        else:
            raise ValueError(f'No OTA-MANIFEST.json or IMAGE-MANIFEST.json found under {path}')
    data = json.loads(p.read_text(encoding='utf-8'))
    prev_super = (
        data.get('super_sha256')
        or (data.get('ota') or {}).get('super_sha256')
        or data.get('required_super_sha256')
        or ''
    )
    prev_esp = None
    if isinstance(data.get('esp_inventory'), dict):
        prev_esp = {k: v['sha256'] for k, v in data['esp_inventory'].items() if isinstance(v, dict) and 'sha256' in v}
    elif isinstance(data.get('efi_files'), dict):
        prev_esp = {
            k: v['sha256']
            for k, v in data['efi_files'].items()
            if isinstance(v, dict) and 'sha256' in v and not is_preserved_esp_path(k)
        }
    return (prev_super or None), prev_esp


def resolve_build_inputs(build_output=None, esp_tree=None, image=None, layout=None):
    """Resolve ESP tree, raw disk image, and super partition bounds from CLI/build paths."""
    if build_output is not None:
        bo = Path(build_output).resolve()
        if esp_tree is None:
            for cand in (bo / 'disk/work/esp-tree', bo / 'disk/esp', bo / 'work/esp-tree', bo / 'esp'):
                if cand.is_dir():
                    esp_tree = cand
                    break
        if image is None:
            for cand in (
                bo / 'disk/surface-pro-x-6.18-hybrid-generic.img',
                bo / 'disk/surface-pro-x-6.18-single-user-private.img',
                bo / 'surface-pro-x-6.18-hybrid-generic.img',
                bo / 'surface-pro-x-6.18-single-user-private.img',
            ):
                if cand.is_file():
                    image = cand
                    break
        if layout is None:
            for cand in (bo / 'disk/LAYOUT.json', bo / 'LAYOUT.json'):
                if cand.is_file():
                    layout = cand
                    break

    if esp_tree is None:
        raise ValueError('Either --esp-tree or --build-output containing esp-tree is required')

    super_info = None
    if image is not None and Path(image).is_file():
        image = Path(image).resolve()
        if layout is not None and Path(layout).is_file():
            parts_list = json.loads(Path(layout).read_text(encoding='utf-8')).get('partitions', [])
            for item in parts_list:
                if item.get('name') == 'super':
                    super_info = {'offset': int(item['offset']), 'bytes': int(item['bytes'])}
                    break
        if super_info is None:
            gpt = inspect_gpt_partitions(image)
            if 'super' in gpt:
                super_info = gpt['super']
    return Path(esp_tree).resolve(), (Path(image).resolve() if image else None), super_info


def detect_git_commit():
    try:
        return subprocess.check_output(
            ['git', 'rev-parse', '--short=12', 'HEAD'],
            cwd=ROOT, text=True, stderr=subprocess.DEVNULL,
        ).strip()
    except Exception:
        return ''


def detect_version(source_commit=None):
    try:
        desc = subprocess.check_output(
            ['git', 'describe', '--tags', '--always'],
            cwd=ROOT, text=True, stderr=subprocess.DEVNULL,
        ).strip()
        if desc:
            return desc
    except Exception:
        pass
    if source_commit:
        return '6.18-hybrid-' + str(source_commit)[:8]
    return '6.18-' + policy()['baseline'] + '-hybrid'


def _add_bytes_to_tar(tar, arcname, data):
    info = tarfile.TarInfo(name=arcname)
    info.size = len(data)
    info.mode = 0o644
    info.mtime = 0
    info.uid = 0
    info.gid = 0
    info.uname = 'root'
    info.gname = 'root'
    tar.addfile(info, io.BytesIO(data))


def _add_file_to_tar(tar, arcname, src_path):
    src_path = Path(src_path)
    info = tarfile.TarInfo(name=arcname)
    info.size = src_path.stat().st_size
    info.mode = 0o644
    info.mtime = 0
    info.uid = 0
    info.gid = 0
    info.uname = 'root'
    info.gname = 'root'
    with src_path.open('rb') as stream:
        tar.addfile(info, stream)


def _add_region_to_tar(tar, arcname, image_path, offset, length):
    info = tarfile.TarInfo(name=arcname)
    info.size = length
    info.mode = 0o644
    info.mtime = 0
    info.uid = 0
    info.gid = 0
    info.uname = 'root'
    info.gname = 'root'

    class _BoundedReader(io.RawIOBase):
        def __init__(self, f, limit):
            self._f = f
            self._left = limit

        def readable(self):
            return True

        def read(self, n=-1):
            if self._left <= 0:
                return b''
            want = self._left if (n is None or n < 0) else min(n, self._left)
            chunk = self._f.read(want)
            self._left -= len(chunk)
            return chunk

        def readinto(self, b):
            data = self.read(len(b))
            n = len(data)
            b[:n] = data
            return n

    with Path(image_path).open('rb') as stream:
        stream.seek(offset)
        tar.addfile(info, _BoundedReader(stream, length))


def _resolve_slice_file(slices_dir, slice_name):
    slices_dir = Path(slices_dir)
    for cand in (slices_dir / f'{slice_name}.img', slices_dir / f"{slice_name.removesuffix('_a')}.img"):
        if cand.is_file():
            return cand.resolve()
    raise ValueError(f'Missing slice image for {slice_name} in {slices_dir}')


def _default_base_candidate_offsets(base_slices_dir, slice_order, chain_path=None):
    """Build per-slice known source offset candidates (compacted 5-part + chain LP offsets)."""
    M = 1048576
    cands = {name: [] for name in slice_order}
    cursor = M
    for name in slice_order:
        p = _resolve_slice_file(base_slices_dir, name)
        off = ((cursor + M - 1) // M) * M
        cands[name].append(off)
        cursor = off + p.stat().st_size
    if chain_path is not None and Path(chain_path).is_file():
        chain = json.loads(Path(chain_path).read_text(encoding='utf-8'))
        base_parts = (chain.get('base') or {}).get('partitions') or {}
        for name in slice_order:
            short = name.removesuffix('_a')
            for off in (base_parts.get(short) or {}).get('candidate_offsets', []):
                off_i = int(off)
                if off_i not in cands[name]:
                    cands[name].append(off_i)
    return cands


def build_ota_package(
    output_dir,
    esp_tree=None,
    build_output=None,
    image=None,
    layout=None,
    previous_manifest=None,
    previous_super_sha256=None,
    mode='auto',
    super_unchanged_hint=None,
    version=None,
    source_commit=None,
    archive_name=None,
    compresslevel=1,
    base_slices_dir=None,
    target_slices_dir=None,
    chain_path=None,
):
    """Build an OTA .tar.gz package + OTA-MANIFEST.json + SHA256SUMS."""
    if mode not in ('auto', 'delta', 'esp', 'full', 'super-delta'):
        raise ValueError(f'Unsupported OTA mode: {mode}')

    esp_tree, image_path, super_info = resolve_build_inputs(
        build_output=build_output, esp_tree=esp_tree, image=image, layout=layout,
    )
    esp_inv = collect_esp_inventory(esp_tree)

    prev_super, prev_esp = load_previous_baseline(previous_manifest)
    if previous_super_sha256:
        prev_super = previous_super_sha256.strip().lower()

    super_sha256 = ''
    super_bytes = 0
    super_delta_entries = []
    super_delta_files = []  # (arcname, Path)
    super_delta_tsv_bytes = None
    super_delta_tmpdir = None
    super_mode = 'none'

    embedded_super_sha = ''
    if build_output is not None:
        bo = Path(build_output).resolve()
        for mf in (bo / 'FULL-IMAGE.json', bo / 'disk/IMAGE-MANIFEST.json'):
            if mf.is_file():
                try:
                    raw_sha = str(json.loads(mf.read_text(encoding='utf-8')).get('super_sha256') or '').strip().lower()
                    if len(raw_sha) == 64:
                        embedded_super_sha = raw_sha
                        break
                except Exception:
                    pass

    if mode == 'super-delta':
        import tempfile
        from disk_image import compact_super_slices_from_dir, compute_super_sha256_from_files
        from spx_super_delta import generate_slice_delta

        if base_slices_dir is None or target_slices_dir is None:
            raise ValueError('--mode super-delta requires both --base-slices-dir and --target-slices-dir')
        base_slices_dir = Path(base_slices_dir).resolve()
        target_slices_dir = Path(target_slices_dir).resolve()
        slice_order = ('system_a', 'vendor_a', 'product_a', 'system_ext_a', 'system_dlkm_a', 'vendor_dlkm_a')
        super_bytes, target_plan, _ = compact_super_slices_from_dir(target_slices_dir, slice_order=slice_order)
        super_sha256 = compute_super_sha256_from_files(super_bytes, target_plan)
        if chain_path is None and (ROOT / 'configs/mica-ota-chain.json').is_file():
            chain_path = ROOT / 'configs/mica-ota-chain.json'
        cand_map = _default_base_candidate_offsets(base_slices_dir, slice_order, chain_path=chain_path)

        super_delta_tmpdir = tempfile.TemporaryDirectory(prefix='spx-super-delta-pkg-')
        delta_work = Path(super_delta_tmpdir.name)
        tsv_delta_lines = []
        for slice_name, dst_path, dst_off, dst_bytes in target_plan:
            src_path = _resolve_slice_file(base_slices_dir, slice_name)
            delta_file = delta_work / f'{slice_name}.spxdlt'
            stats = generate_slice_delta(src_path, dst_path, delta_file)
            arcname = f'payload/super-delta/{slice_name}.spxdlt'
            cands_csv = ','.join(str(x) for x in cand_map.get(slice_name, []))
            tsv_delta_lines.append(
                f"{slice_name}\t{dst_off}\t{dst_bytes}\t{stats['src_bytes']}\t"
                f"{stats['src_sha256']}\t{stats['dst_sha256']}\t{arcname}\t{cands_csv}\n"
            )
            super_delta_entries.append({
                'slice': slice_name,
                'dst_offset': dst_off,
                'dst_bytes': dst_bytes,
                'src_bytes': stats['src_bytes'],
                'src_sha256': stats['src_sha256'],
                'dst_sha256': stats['dst_sha256'],
                'archive_path': arcname,
                'delta_bytes': stats['delta_bytes'],
                'copy_blocks': stats['copy_blocks'],
                'zero_blocks': stats['zero_blocks'],
                'literal_bytes': stats['literal_bytes'],
                'candidate_offsets': cand_map.get(slice_name, []),
            })
            super_delta_files.append((arcname, delta_file))
        super_delta_tsv_bytes = ''.join(tsv_delta_lines).encode('utf-8')
        super_included = True
        super_mode = 'delta'
    else:
        if mode in ('delta', 'esp'):
            super_included = False
            if embedded_super_sha:
                super_sha256 = embedded_super_sha
            elif image_path is not None and super_info is not None:
                super_bytes = int(super_info['bytes'])
                super_sha256 = sha256_region(image_path, int(super_info['offset']), super_bytes)
        else:
            if image_path is not None and super_info is not None:
                super_bytes = int(super_info['bytes'])
                super_sha256 = sha256_region(image_path, int(super_info['offset']), super_bytes)

            if mode == 'full':
                super_included = True
            else:
                # auto mode: compare against either embedded compacted super hash or full GPT hash
                compare_sha = embedded_super_sha or super_sha256
                if prev_super and compare_sha:
                    super_included = (compare_sha != prev_super)
                elif super_unchanged_hint is not None:
                    super_included = not bool(super_unchanged_hint)
                elif image_path is None or super_info is None:
                    super_included = False
                else:
                    super_included = True

            if not super_included and embedded_super_sha:
                super_sha256 = embedded_super_sha
                super_bytes = 0

        if super_included and (image_path is None or super_info is None):
            raise ValueError('Full OTA mode requires a raw disk image containing a super partition')
        super_mode = 'full' if super_included else 'none'

    try:
        # Determine changed/added and deleted ESP files.
        if prev_esp is not None and mode in ('auto', 'delta'):
            changed_rels = [
                rel for rel, info in esp_inv.items()
                if prev_esp.get(rel) != info['sha256']
            ]
            deleted_rels = sorted(rel for rel in prev_esp if rel not in esp_inv and not is_preserved_esp_path(rel))
            if not changed_rels and 'SPX-IMAGE.txt' in esp_inv:
                changed_rels = ['SPX-IMAGE.txt']
            esp_mode = 'delta'
        else:
            changed_rels = sorted(esp_inv.keys())
            deleted_rels = []
            esp_mode = 'sync'

        # Content-deduplicate identical files (e.g. EFI/SQ2Nat & EFI/SQ2Res share Image + initramfs).
        blob_by_sha = {}
        blobs_to_pack = []  # list of (arcname, Path)
        put_entries = []
        for rel in changed_rels:
            info = esp_inv[rel]
            digest = info['sha256']
            if digest not in blob_by_sha:
                arcname = f'payload/esp/{rel}'
                blob_by_sha[digest] = arcname
                blobs_to_pack.append((arcname, info['path']))
            put_entries.append({
                'action': 'PUT',
                'path': rel,
                'archive_path': blob_by_sha[digest],
                'sha256': digest,
                'bytes': info['bytes'],
            })

        del_entries = [{'action': 'DEL', 'path': rel} for rel in deleted_rels]

        commit = source_commit if source_commit is not None else detect_git_commit()
        ver = version or detect_version(commit)
        krel = policy()['kernel_release']
        created_at = datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')

        if super_mode == 'delta':
            # Sentinel path makes older v0.9.0-v0.9.3 runtimes fail cleanly before touching super/ESP.
            super_archive_path = 'payload/super-delta/UNSUPPORTED_UPGRADE_OTA_RUNTIME_FIRST'
            super_delta_tsv_name = 'ota-super-delta.tsv'
            package_type = 'super-delta'
        elif super_included:
            super_archive_path = 'payload/super.img'
            super_delta_tsv_name = ''
            package_type = 'full'
        else:
            super_archive_path = ''
            super_delta_tsv_name = ''
            package_type = 'delta' if esp_mode == 'delta' else 'esp'

        manifest = {
            'schema': 1,
            'package_type': package_type,
            'version': ver,
            'kernel_release': krel,
            'build_commit': commit,
            'created_at': created_at,
            'super_included': super_included,
            'super_mode': super_mode,
            'super_delta_tsv': super_delta_tsv_name,
            'super_archive_path': super_archive_path,
            'super_sha256': super_sha256,
            'super_bytes': super_bytes,
            'required_super_sha256': ('' if super_included else super_sha256),
            'esp_mode': esp_mode,
            'esp_preserve': list(PRESERVED_ESP_PATHS),
            'updates': put_entries + del_entries,
            'esp_inventory': {
                rel: {'sha256': info['sha256'], 'bytes': info['bytes']}
                for rel, info in esp_inv.items()
            },
            'deduplicated_blob_count': len(blobs_to_pack),
            'update_put_count': len(put_entries),
            'update_del_count': len(del_entries),
        }
        if super_mode == 'delta':
            manifest['super_delta_slices'] = super_delta_entries

        manifest_json_bytes = (json.dumps(manifest, indent=2) + '\n').encode('utf-8')
        kv_lines = [
            'SCHEMA=1',
            f'PACKAGE_TYPE={package_type}',
            f'VERSION={ver}',
            f'KERNEL_RELEASE={krel}',
            f'BUILD_COMMIT={commit}',
            f'SUPER_INCLUDED={1 if super_included else 0}',
            f'SUPER_MODE={super_mode}',
        ]
        if super_delta_tsv_name:
            kv_lines.append(f'SUPER_DELTA_TSV={super_delta_tsv_name}')
        kv_lines.extend([
            f'SUPER_ARCHIVE_PATH={super_archive_path}',
            f'SUPER_SHA256={super_sha256}',
            f'SUPER_BYTES={super_bytes}',
            f'REQUIRED_SUPER_SHA256={"" if super_included else super_sha256}',
            f'ESP_MODE={esp_mode}',
        ])
        manifest_kv_bytes = ('\n'.join(kv_lines) + '\n').encode('utf-8')

        tsv_lines = []
        for entry in put_entries:
            tsv_lines.append(
                f"PUT\t{entry['path']}\t{entry['archive_path']}\t{entry['sha256']}\t{entry['bytes']}\n"
            )
        for entry in del_entries:
            tsv_lines.append(f"DEL\t{entry['path']}\n")
        files_tsv_bytes = ''.join(tsv_lines).encode('utf-8')

        output_dir = Path(output_dir).resolve()
        output_dir.mkdir(parents=True, exist_ok=True)
        if not archive_name:
            suffix = 'super-delta' if super_mode == 'delta' else ('full' if super_included else ('delta' if esp_mode == 'delta' else 'esp'))
            archive_name = f'surface-pro-x-6.18-hybrid-ota-{suffix}.tar.gz'
        archive_path = output_dir / archive_name

        # Write tar.gz with metadata members FIRST so inspection reads only the beginning of the stream.
        with archive_path.open('wb') as raw_out:
            with gzip.GzipFile(filename='', mode='wb', fileobj=raw_out, compresslevel=compresslevel, mtime=0) as gz:
                with tarfile.open(fileobj=gz, mode='w', format=tarfile.USTAR_FORMAT) as tar:
                    _add_bytes_to_tar(tar, 'ota-manifest.json', manifest_json_bytes)
                    _add_bytes_to_tar(tar, 'ota-manifest.kv', manifest_kv_bytes)
                    _add_bytes_to_tar(tar, 'ota-files.tsv', files_tsv_bytes)
                    if super_delta_tsv_bytes is not None:
                        _add_bytes_to_tar(tar, 'ota-super-delta.tsv', super_delta_tsv_bytes)
                    dirs = ['payload', 'payload/esp']
                    if super_mode == 'delta':
                        dirs.append('payload/super-delta')
                    for dname in dirs:
                        dinfo = tarfile.TarInfo(name=dname)
                        dinfo.type = tarfile.DIRTYPE
                        dinfo.mode = 0o755
                        dinfo.mtime = 0
                        dinfo.uid = 0
                        dinfo.gid = 0
                        dinfo.uname = 'root'
                        dinfo.gname = 'root'
                        tar.addfile(dinfo)
                    for arcname, src_path in blobs_to_pack:
                        _add_file_to_tar(tar, arcname, src_path)
                    if super_mode == 'delta':
                        for arcname, delta_path in super_delta_files:
                            _add_file_to_tar(tar, arcname, delta_path)
                    elif super_included:
                        _add_region_to_tar(tar, super_archive_path, image_path, int(super_info['offset']), super_bytes)

        archive_sha = sha256_file(archive_path)
        archive_bytes = archive_path.stat().st_size

        manifest['archive'] = archive_name
        manifest['archive_sha256'] = archive_sha
        manifest['archive_bytes'] = archive_bytes

        manifest_out = output_dir / 'OTA-MANIFEST.json'
        manifest_out.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')

        sums_out = output_dir / 'SHA256SUMS'
        sums_out.write_text(f'{archive_sha}  {archive_name}\n', encoding='utf-8')

        return {
            'operation': 'ota-package',
            'package_type': package_type,
            'archive': str(archive_path),
            'archive_name': archive_name,
            'archive_bytes': archive_bytes,
            'archive_sha256': archive_sha,
            'manifest': str(manifest_out),
            'sha256sums': str(sums_out),
            'version': ver,
            'kernel_release': krel,
            'build_commit': commit,
            'super_included': super_included,
            'super_mode': super_mode,
            'super_sha256': super_sha256,
            'super_bytes': super_bytes,
            'esp_mode': esp_mode,
            'update_put_count': len(put_entries),
            'update_del_count': len(del_entries),
            'deduplicated_blob_count': len(blobs_to_pack),
        }
    finally:
        if super_delta_tmpdir is not None:
            super_delta_tmpdir.cleanup()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-output', type=Path,
                        help='Full-image build output directory (auto-discovers esp-tree, LAYOUT.json, and raw .img)')
    parser.add_argument('--esp-tree', type=Path, help='Explicit assembled ESP directory')
    parser.add_argument('--image', type=Path, help='Explicit raw GPT disk image (.img)')
    parser.add_argument('--layout', type=Path, help='Explicit LAYOUT.json matching --image')
    parser.add_argument('--previous-manifest', type=Path,
                        help='Previous OTA-MANIFEST.json, FULL-IMAGE.json, or build directory for delta/super comparison')
    parser.add_argument('--previous-super-sha256',
                        help='Explicit previous super SHA-256 to decide whether super changed')
    parser.add_argument('--mode', choices=('auto', 'delta', 'esp', 'full', 'super-delta'), default='auto',
                        help='auto: omit super when unchanged; delta/esp: ESP files + manifest only; full: include super.img; super-delta: per-slice SPXDLT01 incremental super update')
    parser.add_argument('--base-slices-dir', type=Path,
                        help='Base _a EROFS slices directory for --mode super-delta (e.g. 16471258)')
    parser.add_argument('--target-slices-dir', type=Path,
                        help='Target _a EROFS slices directory for --mode super-delta (e.g. 286.R2)')
    parser.add_argument('--chain', type=Path,
                        help='Optional mica-ota-chain.json to populate candidate source slice offsets')
    parser.add_argument('--super-unchanged', action='store_true',
                        help='Hint that super is unchanged from baseline when no --previous-manifest is supplied')
    parser.add_argument('--version', help='Explicit release version string')
    parser.add_argument('--archive-name', help='Explicit output archive filename')
    parser.add_argument('--output', type=Path, required=True, help='Output directory for OTA package & manifest')
    args = parser.parse_args(argv)

    report = build_ota_package(
        output_dir=args.output,
        esp_tree=args.esp_tree,
        build_output=args.build_output,
        image=args.image,
        layout=args.layout,
        previous_manifest=args.previous_manifest,
        previous_super_sha256=args.previous_super_sha256,
        mode=args.mode,
        super_unchanged_hint=(True if args.super_unchanged else None),
        version=args.version,
        archive_name=args.archive_name,
        base_slices_dir=args.base_slices_dir,
        target_slices_dir=args.target_slices_dir,
        chain_path=args.chain,
    )
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
