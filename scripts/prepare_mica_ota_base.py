#!/usr/bin/env python3
"""Reproducibly prepare upgraded mica (e.g., 286.R2) dynamic partitions & Android inputs.

Pipeline:
1. Extract & SHA-256-verify the 6 base (16471258) _a EROFS slices from template.img
   (or an explicit --base-slices-dir).
2. Fetch/cache & SHA-256-verify the pinned official Google incremental OTA zips from
   configs/mica-ota-chain.json.
3. Fetch/cache AOSP 17 ota_extractor + its transitive shared libraries via HTTP Range
   from ci.android.com otatools.zip (or reuse --ota-extractor).
4. Apply each OTA hop sequentially and verify every partition's SHA-256 against the
   pinned Google DeltaArchiveManifest hashes, keeping untouched stock target images
   for future Hop N+1 chaining.
5. Trim Dell-only vendor payloads (etc/recovery/ota_recovery.img) via deterministic
   EROFS repacking (--tar=f) that preserves exact stock uid/gid/mode/symlinks and
   SELinux vendor_file_contexts without requiring root.
6. Export target 286.R2 desktop-services.jar, services.jar, and vendor/build.prop
   for full_image.py runtime adaptation.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CHAIN = ROOT / 'configs/mica-ota-chain.json'
DEFAULT_CACHE = ROOT / 'private/mica-ota-cache'
DEFAULT_TEMPLATE = ROOT / 'private/build-inputs/template.img'
CHUNK = 8 * 1024 * 1024
M = 1048576


def sha256_file(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        while True:
            buf = f.read(CHUNK)
            if not buf:
                break
            h.update(buf)
    return h.hexdigest()


def inspect_gpt_super(image_path):
    """Return (super_offset, super_bytes) from primary GPT of a raw disk image."""
    with Path(image_path).open('rb') as f:
        head = f.read(1024)
        if len(head) < 1024 or head[510:512] != b'\x55\xaa' or head[512:520] != b'EFI PART':
            raise ValueError(f'Not a GPT disk image: {image_path}')
        entries_lba, num_entries, entry_size = struct.unpack_from('<QII', head, 512 + 72)
        f.seek(entries_lba * 512)
        table = f.read(num_entries * entry_size)
    for i in range(num_entries):
        ent = table[i * entry_size:(i + 1) * entry_size]
        if not any(ent[:16]):
            continue
        name = ent[56:128].decode('utf-16le', errors='ignore').split('\0', 1)[0]
        if name == 'super':
            first_lba, last_lba = struct.unpack_from('<QQ', ent, 32)
            return first_lba * 512, (last_lba - first_lba + 1) * 512
    raise ValueError(f'GPT image has no super partition: {image_path}')


def extract_base_slices_from_template(template_path, chain, dest_dir):
    """Extract & verify the 6 base _a partitions from template.img super."""
    dest_dir = Path(dest_dir)
    dest_dir.mkdir(parents=True, exist_ok=True)
    super_off, super_bytes = inspect_gpt_super(template_path)
    base_parts = chain['base']['partitions']
    results = {}

    with Path(template_path).open('rb') as src:
        # Check if template super has LP metadata at +12288 for dynamic lookup.
        lp_offsets = {}
        src.seek(super_off + 12288)
        hdr = src.read(256)
        if len(hdr) == 256 and struct.unpack_from('<I', hdr, 0)[0] == 0x414c5030:
            hdr_sz = struct.unpack_from('<I', hdr, 8)[0]
            tables_sz = struct.unpack_from('<I', hdr, 44)[0]
            src.seek(super_off + 12288 + hdr_sz)
            tables = src.read(tables_sz)
            po, pn, ps = struct.unpack_from('<III', hdr, 80)
            eo, en, es = struct.unpack_from('<III', hdr, 92)
            extents = [struct.unpack_from('<QIQI', tables, eo + i * es) for i in range(en)]
            for i in range(pn):
                rec = tables[po + i * ps:po + (i + 1) * ps]
                pname = rec[:36].split(b'\0', 1)[0].decode('ascii', errors='ignore')
                first_ext, num_ext = struct.unpack_from('<II', rec, 40)
                if pname.endswith('_a') and num_ext == 1:
                    secs, kind, sec, _ = extents[first_ext]
                    if kind == 0:
                        lp_offsets[pname[:-2]] = (sec * 512, secs * 512)

        for name in chain['dynamic_partitions']:
            spec = base_parts[name]
            expected_bytes = int(spec['bytes'])
            expected_sha = spec['sha256'].lower()
            out_img = dest_dir / f'{name}.img'
            if out_img.is_file() and out_img.stat().st_size == expected_bytes:
                if sha256_file(out_img) == expected_sha:
                    results[name] = out_img
                    continue

            candidates = []
            if name in lp_offsets and lp_offsets[name][1] == expected_bytes:
                candidates.append(lp_offsets[name][0])
            for cand in spec.get('candidate_offsets', []):
                if int(cand) not in candidates:
                    candidates.append(int(cand))

            matched_off = None
            for cand_off in candidates:
                if cand_off + expected_bytes > super_bytes:
                    continue
                h = hashlib.sha256()
                src.seek(super_off + cand_off)
                rem = expected_bytes
                while rem > 0:
                    buf = src.read(min(rem, CHUNK))
                    if not buf:
                        break
                    h.update(buf)
                    rem -= len(buf)
                if rem == 0 and h.hexdigest() == expected_sha:
                    matched_off = cand_off
                    break
            if matched_off is None:
                raise ValueError(f'Base partition {name} ({expected_sha[:12]}) not found in {template_path}')

            tmp_img = out_img.with_suffix('.img.tmp')
            src.seek(super_off + matched_off)
            with tmp_img.open('wb') as dst:
                rem = expected_bytes
                while rem > 0:
                    buf = src.read(min(rem, CHUNK))
                    dst.write(buf)
                    rem -= len(buf)
            tmp_img.replace(out_img)
            results[name] = out_img
    return results


def verify_base_slices_dir(slices_dir, chain, dest_dir):
    slices_dir = Path(slices_dir)
    dest_dir = Path(dest_dir)
    dest_dir.mkdir(parents=True, exist_ok=True)
    results = {}
    for name in chain['dynamic_partitions']:
        spec = chain['base']['partitions'][name]
        cand = None
        for fname in (f'{name}.img', f'{name}_a.img'):
            p = slices_dir / fname
            if p.is_file():
                cand = p
                break
        if cand is None:
            raise ValueError(f'Missing base partition {name}.img in {slices_dir}')
        if cand.stat().st_size != int(spec['bytes']) or sha256_file(cand) != spec['sha256'].lower():
            raise ValueError(f'Base partition {name} SHA-256 mismatch: {cand}')
        dst = dest_dir / f'{name}.img'
        if dst.resolve() != cand.resolve():
            if dst.exists() or dst.is_symlink():
                dst.unlink()
            dst.symlink_to(cand.resolve())
        results[name] = dst
    return results


def download_verified(url, dest_path, expected_bytes, expected_sha256):
    dest_path = Path(dest_path)
    dest_path.parent.mkdir(parents=True, exist_ok=True)
    if dest_path.is_file() and dest_path.stat().st_size == expected_bytes:
        if sha256_file(dest_path) == expected_sha256.lower():
            return dest_path
    tmp = dest_path.with_suffix(dest_path.suffix + '.part')
    req = urllib.request.Request(url, headers={'User-Agent': 'spx-mica-ota-builder/1.0'})
    h = hashlib.sha256()
    got = 0
    with urllib.request.urlopen(req, timeout=60) as resp, tmp.open('wb') as out:
        while True:
            buf = resp.read(CHUNK)
            if not buf:
                break
            out.write(buf)
            h.update(buf)
            got += len(buf)
    if got != expected_bytes or h.hexdigest() != expected_sha256.lower():
        tmp.unlink(missing_ok=True)
        raise ValueError(f'Download verification failed for {url}: got {got} B / {h.hexdigest()}')
    tmp.replace(dest_path)
    return dest_path


def ensure_ota_extractor(chain, cache_dir, explicit_path=None):
    """Return path to x86_64 AOSP ota_extractor, fetching via HTTP Range if absent."""
    if explicit_path is not None:
        exe = Path(explicit_path).resolve()
        if not exe.is_file() or not os.access(exe, os.X_OK):
            raise ValueError(f'Invalid --ota-extractor: {exe}')
        return exe

    otatools_dir = Path(cache_dir) / f"otatools-{chain['otatools']['build']}"
    exe = otatools_dir / chain['otatools']['extractor_member']
    if exe.is_file() and os.access(exe, os.X_OK):
        return exe

    ci_url = chain['otatools']['url']
    html = urllib.request.urlopen(
        urllib.request.Request(ci_url, headers={'User-Agent': 'spx-mica-ota-builder/1.0'}),
        timeout=30,
    ).read().decode('utf-8', errors='replace')
    m = re.search(r'var JSVariables = (\{.*?\});', html)
    if not m:
        raise RuntimeError(f'Failed to resolve GCS artifactUrl from {ci_url}')
    gcs_url = json.loads(m.group(1))['artifactUrl']

    r0 = urllib.request.urlopen(urllib.request.Request(gcs_url, headers={'Range': 'bytes=0-0'}), timeout=30)
    total_sz = int(r0.headers['Content-Range'].split('/')[-1])
    tail_len = min(total_sz, 256 * 1024)
    tail = urllib.request.urlopen(
        urllib.request.Request(gcs_url, headers={'Range': f'bytes={total_sz - tail_len}-{total_sz - 1}'}),
        timeout=30,
    ).read()
    eocd = tail.rfind(b'PK\x05\x06')
    if eocd < 0:
        raise RuntimeError('otatools.zip EOCD not found')
    cd_size, cd_off = struct.unpack_from('<II', tail, eocd + 12)
    if cd_off >= total_sz - tail_len:
        start = cd_off - (total_sz - tail_len)
        cd = tail[start:start + cd_size]
    else:
        cd = urllib.request.urlopen(
            urllib.request.Request(gcs_url, headers={'Range': f'bytes={cd_off}-{cd_off + cd_size - 1}'}),
            timeout=30,
        ).read()

    entries = {}
    pos = 0
    while pos < len(cd):
        if cd[pos:pos + 4] != b'PK\x01\x02':
            break
        method, _, _, _, comp_sz, uncomp_sz, fn_len, ex_len, cm_len = struct.unpack_from('<HHHIIIHHH', cd, pos + 10)
        ext_attr, loc_off = struct.unpack_from('<II', cd, pos + 38)
        fn = cd[pos + 46:pos + 46 + fn_len].decode('utf-8')
        entries[fn] = (method, comp_sz, uncomp_sz, loc_off, ext_attr)
        pos += 46 + fn_len + ex_len + cm_len

    def fetch_member(fn):
        dst = otatools_dir / fn
        if dst.is_file():
            return dst
        method, comp_sz, _, loc_off, ext_attr = entries[fn]
        req = urllib.request.Request(
            gcs_url,
            headers={'Range': f'bytes={loc_off}-{loc_off + 30 + len(fn) + 1024 + comp_sz}'},
        )
        buf = urllib.request.urlopen(req, timeout=60).read()
        if buf[:4] != b'PK\x03\x04':
            raise RuntimeError(f'Bad ZIP local header for {fn}')
        l_fn, l_ex = struct.unpack_from('<HH', buf, 26)
        data = buf[30 + l_fn + l_ex:30 + l_fn + l_ex + comp_sz]
        if method == 8:
            data = zlib.decompress(data, -15)
        elif method != 0:
            raise RuntimeError(f'Unsupported ZIP compression method {method} for {fn}')
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(data)
        mode = (ext_attr >> 16) & 0o777
        dst.chmod(mode or 0o755)
        return dst

    exe = fetch_member(chain['otatools']['extractor_member'])
    fetched = set()
    queue = [exe]
    while queue:
        cur = queue.pop()
        out = subprocess.check_output(['readelf', '-d', str(cur)], text=True)
        for dep in re.finditer(r'\(NEEDED\)\s+Shared library:\s+\[([^\]]+)\]', out):
            lib_member = 'lib64/' + dep.group(1)
            if lib_member in entries and lib_member not in fetched:
                fetched.add(lib_member)
                queue.append(fetch_member(lib_member))
    return exe


def apply_ota_hop(ota_extractor, ota_zip, hop_spec, input_dir, output_dir, partitions):
    """Apply one official incremental OTA hop and verify every output partition SHA-256."""
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    # Fast-path if already verified in output_dir.
    all_ready = True
    for name in partitions:
        p = output_dir / f'{name}.img'
        spec = hop_spec['partitions'][name]
        if not (p.is_file() and p.stat().st_size == int(spec['bytes']) and sha256_file(p) == spec['sha256'].lower()):
            all_ready = False
            break
    if all_ready:
        return output_dir

    with tempfile.TemporaryDirectory(prefix=f"spx-{hop_spec['name']}-") as td:
        payload_bin = Path(td) / 'payload.bin'
        with zipfile.ZipFile(ota_zip) as zf, zf.open('payload.bin') as src, payload_bin.open('wb') as dst:
            shutil.copyfileobj(src, dst, CHUNK)
        cmd = [
            str(ota_extractor),
            f'--payload={payload_bin}',
            f'--input_dir={input_dir}',
            f'--output_dir={output_dir}',
            f"--partitions={','.join(partitions)}",
        ]
        subprocess.run(cmd, check=True)

    for name in partitions:
        p = output_dir / f'{name}.img'
        spec = hop_spec['partitions'][name]
        if not p.is_file():
            raise RuntimeError(f'ota_extractor did not produce {p}')
        got_sz = p.stat().st_size
        got_sha = sha256_file(p)
        if got_sz != int(spec['bytes']) or got_sha != spec['sha256'].lower():
            raise RuntimeError(
                f"Hop {hop_spec['name']} partition {name} verification mismatch: "
                f"got {got_sz}B/{got_sha}, expected {spec['bytes']}B/{spec['sha256']}"
            )
    return output_dir


EROFS_XATTR_PREFIXES = {
    0: '',
    1: 'user.',
    2: 'system.posix_acl_access',
    3: 'system.posix_acl_default',
    4: 'trusted.',
    5: 'lustre.',
    6: 'security.',
}


def read_erofs_metadata(img_path):
    """Read exact (uid, gid, mode, xattrs) for every path in a stock EROFS image without root."""
    with Path(img_path).open('rb') as f:
        f.seek(1024)
        sb = f.read(128)
        if len(sb) < 128 or struct.unpack_from('<I', sb, 0)[0] != 0xe0f5e1e2:
            raise ValueError(f'Not an EROFS image: {img_path}')
        blksz = 1 << sb[12]
        root_nid = struct.unpack_from('<H', sb, 14)[0]
        meta_blkaddr = struct.unpack_from('<I', sb, 40)[0]
        xattr_blkaddr = struct.unpack_from('<I', sb, 44)[0]

        def decode_xattr_at(file_off):
            f.seek(file_off)
            hdr = f.read(4)
            if len(hdr) < 4:
                return None, None, 0
            name_len, name_idx, val_size = struct.unpack_from('<BBH', hdr, 0)
            body = f.read(name_len + val_size)
            prefix = EROFS_XATTR_PREFIXES.get(name_idx, '')
            key = prefix + body[:name_len].decode('utf-8', errors='replace')
            val = body[name_len:name_len + val_size]
            step = (4 + name_len + val_size + 3) & ~3
            return key, val, step

        def read_inode(nid):
            off = meta_blkaddr * blksz + nid * 32
            f.seek(off)
            raw = f.read(64)
            fmt, xicount, mode = struct.unpack_from('<HHH', raw, 0)
            ver = fmt & 1
            layout = (fmt >> 1) & 7
            if ver == 0:
                size = struct.unpack_from('<I', raw, 8)[0]
                iu = struct.unpack_from('<I', raw, 16)[0]
                uid, gid = struct.unpack_from('<HH', raw, 24)
                isize = 32
            else:
                size = struct.unpack_from('<Q', raw, 8)[0]
                iu = struct.unpack_from('<I', raw, 16)[0]
                uid, gid = struct.unpack_from('<II', raw, 24)
                isize = 64
            xsize = 0 if xicount == 0 else (12 + (xicount - 1) * 4)
            xattrs = {}
            if xicount > 0:
                xoff = off + isize
                f.seek(xoff)
                xhdr = f.read(12)
                shared_cnt = xhdr[4]
                pos = xoff + 12
                for _ in range(shared_cnt):
                    f.seek(pos)
                    s_idx = struct.unpack_from('<I', f.read(4), 0)[0]
                    pos += 4
                    k, v, _ = decode_xattr_at(xattr_blkaddr * blksz + s_idx * 4)
                    if k:
                        xattrs[k] = v
                end = xoff + xsize
                while pos + 4 <= end:
                    k, v, step = decode_xattr_at(pos)
                    if not step:
                        break
                    if k:
                        xattrs[k] = v
                    pos += step
            return {
                'nid': nid, 'layout': layout, 'mode': mode, 'size': size,
                'iu': iu, 'uid': uid, 'gid': gid, 'isize': isize, 'xsize': xsize,
                'off': off, 'xattrs': xattrs,
            }

        def read_dir_bytes(ino):
            data = bytearray()
            full_blks = ino['size'] // blksz
            tail = ino['size'] % blksz
            if ino['layout'] == 0:
                f.seek(ino['iu'] * blksz)
                data.extend(f.read(ino['size']))
            elif ino['layout'] == 2:
                if full_blks:
                    f.seek(ino['iu'] * blksz)
                    data.extend(f.read(full_blks * blksz))
                if tail:
                    f.seek(ino['off'] + ino['isize'] + ino['xsize'])
                    data.extend(f.read(tail))
            else:
                raise ValueError(f"Unsupported EROFS directory layout {ino['layout']}")
            return bytes(data)

        meta = {}

        def walk(nid, rel):
            ino = read_inode(nid)
            meta[rel] = (ino['uid'], ino['gid'], ino['mode'], ino['xattrs'])
            if stat.S_ISDIR(ino['mode']):
                buf = read_dir_bytes(ino)
                for bstart in range(0, len(buf), blksz):
                    blk = buf[bstart:bstart + blksz]
                    if len(blk) < 12:
                        break
                    first_nameoff = struct.unpack_from('<H', blk, 8)[0]
                    ndirents = first_nameoff // 12
                    for i in range(ndirents):
                        cnid, noff, _ = struct.unpack_from('<QHB', blk, i * 12)
                        nend = struct.unpack_from('<H', blk, (i + 1) * 12 + 8)[0] if i + 1 < ndirents else len(blk)
                        name = blk[noff:nend].split(b'\x00', 1)[0].decode('utf-8', errors='surrogateescape')
                        if name in ('.', '..'):
                            continue
                        child = f'{rel}/{name}' if rel else name
                        walk(cnid, child)

        walk(root_nid, '')
    return meta


def _pax_xattr_headers(xattrs):
    headers = {}
    for k, v in sorted(xattrs.items()):
        headers[f'SCHILY.xattr.{k}'] = v.decode('utf-8', errors='surrogateescape')
    return headers


def trim_vendor_erofs(stock_vendor_img, output_vendor_img, trim_policy):
    """Repack vendor.img excluding Dell-only payloads while preserving uid/gid/mode/xattrs/SELinux."""
    stock_vendor_img = Path(stock_vendor_img).resolve()
    output_vendor_img = Path(output_vendor_img).resolve()
    exclude = {p.strip('/') for p in trim_policy.get('exclude_paths', [])}
    meta = read_erofs_metadata(stock_vendor_img)

    with tempfile.TemporaryDirectory(prefix='spx-vendor-trim-') as td:
        td = Path(td)
        extracted = td / 'vendor'
        subprocess.run(
            ['fsck.erofs', '--no-preserve', f'--extract={extracted}', str(stock_vendor_img)],
            check=True,
            capture_output=True,
        )
        fc_path = extracted / 'etc/selinux/vendor_file_contexts'
        if not fc_path.is_file():
            raise RuntimeError('Extracted vendor image missing etc/selinux/vendor_file_contexts')

        tar_path = td / 'vendor_trimmed.tar'
        removed_bytes = 0
        packed_count = 0
        ts = int(trim_policy.get('timestamp', 1230768000))

        with tarfile.open(tar_path, 'w', format=tarfile.PAX_FORMAT, encoding='utf-8', errors='surrogateescape') as tar:
            if '' in meta:
                r_uid, r_gid, r_mode, r_xattrs = meta['']
                r_ti = tarfile.TarInfo(name='.')
                r_ti.type = tarfile.DIRTYPE
                r_ti.uid = r_uid
                r_ti.gid = r_gid
                r_ti.uname = ''
                r_ti.gname = ''
                r_ti.mode = r_mode & 0o7777
                r_ti.mtime = ts
                if r_xattrs:
                    r_ti.pax_headers = _pax_xattr_headers(r_xattrs)
                tar.addfile(r_ti)

            for rel in sorted(meta.keys()):
                if rel == '' or rel in exclude or any(rel.startswith(ex + '/') for ex in exclude):
                    if rel in exclude:
                        ex_p = extracted / rel
                        if ex_p.is_file() and not ex_p.is_symlink():
                            removed_bytes += ex_p.stat().st_size
                    continue
                fs_path = extracted / rel
                if not fs_path.exists() and not fs_path.is_symlink():
                    continue
                uid, gid, mode, xattrs = meta[rel]
                ti = tarfile.TarInfo(name=rel)
                ti.uid = uid
                ti.gid = gid
                ti.uname = ''
                ti.gname = ''
                ti.mode = mode & 0o7777
                ti.mtime = ts
                if xattrs:
                    ti.pax_headers = _pax_xattr_headers(xattrs)
                if stat.S_ISDIR(mode):
                    ti.type = tarfile.DIRTYPE
                    tar.addfile(ti)
                elif stat.S_ISLNK(mode):
                    ti.type = tarfile.SYMTYPE
                    ti.linkname = os.readlink(fs_path)
                    tar.addfile(ti)
                elif stat.S_ISREG(mode):
                    ti.type = tarfile.REGTYPE
                    ti.size = fs_path.stat().st_size
                    with fs_path.open('rb') as fp:
                        tar.addfile(ti, fp)
                else:
                    continue
                packed_count += 1

        output_vendor_img.parent.mkdir(parents=True, exist_ok=True)
        tmp_img = output_vendor_img.with_suffix('.img.tmp')
        cmd = [
            'mkfs.erofs',
            '--tar=f',
            f"-z{trim_policy.get('compression', 'lz4hc,12')}",
            '-C', str(int(trim_policy.get('pcluster_bytes', 65536))),
            '-T', str(ts),
            '-U', str(trim_policy.get('uuid', 'f913b765-0109-5ff7-b64d-e3b1011804cc')),
            '--mount-point=/vendor',
            str(tmp_img),
            str(tar_path),
        ]
        subprocess.run(cmd, check=True, capture_output=True)
        tmp_img.replace(output_vendor_img)

    # Sanity-verify repacked EROFS metadata preserved Android ownership, SELinux xattrs, and capabilities.
    repacked_meta = read_erofs_metadata(output_vendor_img)
    for check_rel in ('', 'bin/hw', 'build.prop', 'bin/pd-mapper', 'bin/pm-service'):
        if check_rel in meta and check_rel in repacked_meta:
            got_uid, got_gid, got_mode, got_xattrs = repacked_meta[check_rel]
            exp_uid, exp_gid, exp_mode, exp_xattrs = meta[check_rel]
            if (got_uid, got_gid, got_mode & 0o7777) != (exp_uid, exp_gid, exp_mode & 0o7777):
                raise RuntimeError(f'Repacked vendor /{check_rel} metadata drift: got {(got_uid, got_gid, oct(got_mode))}')
            if got_xattrs != exp_xattrs:
                raise RuntimeError(f'Repacked vendor /{check_rel} xattr drift: got {got_xattrs!r} != {exp_xattrs!r}')

    return {
        'path': str(output_vendor_img),
        'bytes': output_vendor_img.stat().st_size,
        'sha256': sha256_file(output_vendor_img),
        'excluded_paths': sorted(exclude),
        'excluded_bytes': removed_bytes,
        'packed_entries': packed_count,
    }


def extract_android_inputs(system_img, vendor_img, out_dir):
    """Extract target desktop-services.jar, services.jar, vendor/build.prop, and vendor_file_contexts."""
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    exported = {}
    with tempfile.TemporaryDirectory(prefix='spx-android-inputs-') as td:
        td = Path(td)
        sys_root = td / 'system'
        vnd_root = td / 'vendor'
        subprocess.run(['fsck.erofs', '--no-preserve', f'--extract={sys_root}', str(system_img)],
                       check=True, capture_output=True)
        subprocess.run(['fsck.erofs', '--no-preserve', f'--extract={vnd_root}', str(vendor_img)],
                       check=True, capture_output=True)
        copies = (
            (sys_root / 'system/framework/desktop-services.jar', out_dir / 'desktop-services.jar'),
            (sys_root / 'system/framework/services.jar', out_dir / 'services.jar'),
            (sys_root / 'system/build.prop', out_dir / 'system_build.prop'),
            (vnd_root / 'build.prop', out_dir / 'vendor_build.prop'),
            (vnd_root / 'etc/selinux/vendor_file_contexts', out_dir / 'vendor_file_contexts'),
        )
        for src, dst in copies:
            if not src.is_file():
                raise RuntimeError(f'Expected Android input missing from extracted partition: {src}')
            shutil.copy2(src, dst)
            exported[dst.name] = {
                'path': str(dst),
                'bytes': dst.stat().st_size,
                'sha256': sha256_file(dst),
            }
    return exported


def prepare_mica_ota_base(
    output_dir,
    chain_path=DEFAULT_CHAIN,
    template=DEFAULT_TEMPLATE,
    base_slices_dir=None,
    cache_dir=DEFAULT_CACHE,
    ota_extractor=None,
    trim_vendor=True,
):
    chain = json.loads(Path(chain_path).read_text(encoding='utf-8'))
    output_dir = Path(output_dir).resolve()
    cache_dir = Path(cache_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    cache_dir.mkdir(parents=True, exist_ok=True)

    partitions = list(chain['dynamic_partitions'])
    base_dir = cache_dir / f"stock_{chain['base']['build']}"
    if base_slices_dir is not None:
        verify_base_slices_dir(base_slices_dir, chain, base_dir)
    else:
        extract_base_slices_from_template(template, chain, base_dir)

    exe = ensure_ota_extractor(chain, cache_dir, explicit_path=ota_extractor)

    cur_dir = base_dir
    for hop in chain['hops']:
        zip_path = cache_dir / 'zips' / f"{hop['name']}.zip"
        download_verified(hop['url'], zip_path, int(hop['zip_bytes']), hop['zip_sha256'])
        next_dir = cache_dir / f"stock_{hop['to_build']}"
        apply_ota_hop(exe, zip_path, hop, cur_dir, next_dir, partitions)
        cur_dir = next_dir

    target_hop = chain['hops'][-1]
    base_out = output_dir / 'base_slices'
    stock_out = output_dir / 'stock_slices'
    target_out = output_dir / 'target_slices'
    for d in (base_out, stock_out, target_out):
        d.mkdir(parents=True, exist_ok=True)

    for name in partitions:
        for src_p, dst_p in (
            (base_dir / f'{name}.img', base_out / f'{name}_a.img'),
            (cur_dir / f'{name}.img', stock_out / f'{name}_a.img'),
        ):
            if dst_p.exists() or dst_p.is_symlink():
                dst_p.unlink()
            dst_p.symlink_to(src_p.resolve())

    vendor_trim_report = None
    for name in partitions:
        dst_p = target_out / f'{name}_a.img'
        if name == 'vendor' and trim_vendor:
            vendor_trim_report = trim_vendor_erofs(
                cur_dir / 'vendor.img', dst_p, chain['vendor_trim_policy']
            )
        else:
            if dst_p.exists() or dst_p.is_symlink():
                dst_p.unlink()
            dst_p.symlink_to((cur_dir / f'{name}.img').resolve())

    android_inputs = extract_android_inputs(
        cur_dir / 'system.img', cur_dir / 'vendor.img', output_dir / 'android_inputs'
    )

    # Compute compacted 1 MiB-aligned target super size (matching disk_image.compact_super_slices).
    cursor = M
    target_slices_info = {}
    for name in partitions:
        p = target_out / f'{name}_a.img'
        sz = p.stat().st_size
        off = ((cursor + M - 1) // M) * M
        target_slices_info[f'{name}_a'] = {
            'path': str(p),
            'offset': off,
            'bytes': sz,
            'sha256': sha256_file(p),
        }
        cursor = off + sz
    compacted_super_bytes = ((cursor + M - 1) // M) * M

    report = {
        'schema': 1,
        'board': chain['board'],
        'base_build': chain['base']['build'],
        'base_build_id': chain['base']['build_id'],
        'target_build': target_hop['to_build'],
        'target_build_id': target_hop['to_build_id'],
        'target_fingerprint': target_hop['to_fingerprint'],
        'security_patch': target_hop['security_patch'],
        'vendor_trimmed': bool(trim_vendor),
        'vendor_trim': vendor_trim_report,
        'compacted_super_bytes': compacted_super_bytes,
        'fits_legacy_5part_3481mib': compacted_super_bytes <= 3650093056,
        'base_slices_dir': str(base_out),
        'stock_slices_dir': str(stock_out),
        'target_slices_dir': str(target_out),
        'android_inputs_dir': str(output_dir / 'android_inputs'),
        'target_slices': target_slices_info,
        'android_inputs': android_inputs,
    }
    (output_dir / 'MICA-OTA-BASE.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--chain', type=Path, default=DEFAULT_CHAIN)
    parser.add_argument('--template', type=Path, default=DEFAULT_TEMPLATE)
    parser.add_argument('--base-slices-dir', type=Path, help='Optional directory with already-extracted 16471258 slices')
    parser.add_argument('--cache-dir', type=Path, default=DEFAULT_CACHE)
    parser.add_argument('--ota-extractor', type=Path, help='Optional pre-extracted x86_64 ota_extractor binary')
    parser.add_argument('--no-trim-vendor', action='store_true', help='Keep full stock vendor.img (+119 MiB ota_recovery.img)')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)

    report = prepare_mica_ota_base(
        output_dir=args.output,
        chain_path=args.chain,
        template=args.template,
        base_slices_dir=args.base_slices_dir,
        cache_dir=args.cache_dir,
        ota_extractor=args.ota_extractor,
        trim_vendor=not args.no_trim_vendor,
    )
    print(json.dumps(report, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
