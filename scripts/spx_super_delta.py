#!/usr/bin/env python3
"""SPXDLT01 4 KiB block-dedup delta generator and mica vendor build.prop adapter.

Binary format (matches probe/ota/spx-ota-tool.c `apply-delta`):
  Header (92 bytes):
    [0..8)   magic       = b'SPXDLT01'
    [8..12)  block_size  = 4096 (le32)
    [12..20) src_bytes   (le64)
    [20..52) src_sha256  (32 raw bytes)
    [52..60) dst_bytes   (le64)
    [60..92) dst_sha256  (32 raw bytes)
  Opcode stream:
    0x01 COPY:    src_block_idx (le32), block_count (le32)
    0x02 ZERO:    block_count (le32)
    0x03 LITERAL: byte_len (le32), raw_bytes[byte_len]
    0x00 EOF
"""
import hashlib
from pathlib import Path
import struct

DELTA_MAGIC = b'SPXDLT01'
BLOCK_SIZE = 4096
OP_EOF = 0x00
OP_COPY = 0x01
OP_ZERO = 0x02
OP_LITERAL = 0x03
MAX_LITERAL_CHUNK = 16 * 1024 * 1024  # Flush literal runs at 16 MiB to bound RAM

SPX_VENDOR_PROP_STRIP_KEYS = frozenset({
    'ro.hwui.use_vulkan',
    'ro.gfx.angle.supported',
    'ro.hardware.egl',
    'ro.vendor.hwc.drop_drm_master',
    'debug.renderengine.backend',
    'debug.renderengine.vulkan',
    'debug.renderengine.graphite_desktop_optin',
    'ro.boot.hypervisor.protected_vm.supported',
    'ro.surface_flinger.has_wide_color_display',
    'ro.surface_flinger.has_HDR_display',
    'ro.hardware.vulkan',
    'debug.hwui.renderer',
    'ro.vendor.hwcomposer.mode',
    'vendor.minigbm.debug',
})

SPX_VENDOR_PROP_FOOTER = (
    '\n'
    '# SQ2 graphics development: MSM DRM, Mesa GLES, minigbm.\n'
    'ro.boot.hypervisor.protected_vm.supported=0\n'
    'ro.hardware.egl=freedreno\n'
    'ro.hardware.vulkan=freedreno\n'
    'vendor.minigbm.debug=nocompression\n'
    'ro.hwui.use_vulkan=false\n'
    'debug.renderengine.backend=skiaglthreaded\n'
    'debug.renderengine.vulkan=false\n'
    'debug.renderengine.graphite_desktop_optin=false\n'
    'debug.hwui.renderer=skiagl\n'
    'ro.gfx.angle.supported=false\n'
    'ro.vendor.hwcomposer.mode=client\n'
    'ro.vendor.hwc.drop_drm_master=0\n'
    'ro.surface_flinger.has_wide_color_display=false\n'
    'ro.surface_flinger.has_HDR_display=false\n'
)


def adapt_vendor_build_prop(stock_vendor_prop_bytes):
    """Apply SPX hybrid overrides onto any stock mica vendor/build.prop."""
    text = stock_vendor_prop_bytes.decode('utf-8').replace('\r\n', '\n')
    out_lines = []
    for raw_line in text.splitlines():
        stripped = raw_line.strip()
        if stripped and not stripped.startswith('#') and '=' in stripped:
            key = stripped.split('=', 1)[0].strip()
            if key in SPX_VENDOR_PROP_STRIP_KEYS:
                continue
            if stripped == 'debug.stagefright.ccodec=4':
                out_lines.append('debug.stagefright.ccodec=1')
                continue
        out_lines.append(raw_line)
    body = '\n'.join(out_lines).rstrip('\n') + '\n' + SPX_VENDOR_PROP_FOOTER
    return body.encode('utf-8')


def _index_source_blocks(src_path, block_size=BLOCK_SIZE):
    """Index full block_size blocks of src_path by 128-bit blake2b digest; return (index, size, sha256_bytes)."""
    src_path = Path(src_path)
    sha = hashlib.sha256()
    index = {}  # digest16 -> first block_index (int)
    zero_block = b'\x00' * block_size
    total = 0
    blk_idx = 0
    with src_path.open('rb') as f:
        while True:
            chunk = f.read(block_size * 1024)  # 4 MiB read buffer
            if not chunk:
                break
            sha.update(chunk)
            total += len(chunk)
            full_cnt = len(chunk) // block_size
            mv = memoryview(chunk)
            for i in range(full_cnt):
                blk = mv[i * block_size:(i + 1) * block_size]
                if blk != zero_block:
                    dig = hashlib.blake2b(blk, digest_size=16).digest()
                    if dig not in index:
                        index[dig] = blk_idx
                blk_idx += 1
    return index, total, sha.digest()


def _hash_file_bytes(path):
    h = hashlib.sha256()
    total = 0
    with Path(path).open('rb') as f:
        while True:
            buf = f.read(8 * 1024 * 1024)
            if not buf:
                break
            h.update(buf)
            total += len(buf)
    return total, h.digest()


def generate_slice_delta(src_path, dst_path, delta_out_path, block_size=BLOCK_SIZE):
    """Generate a verified SPXDLT01 delta stream from src_path to dst_path.

    Collision-safe: verifies exact source block bytes on hash match before emitting COPY.
    """
    src_path = Path(src_path)
    dst_path = Path(dst_path)
    delta_out_path = Path(delta_out_path)
    delta_out_path.parent.mkdir(parents=True, exist_ok=True)

    src_index, src_bytes, src_sha_raw = _index_source_blocks(src_path, block_size=block_size)
    dst_bytes, dst_sha_raw = _hash_file_bytes(dst_path)
    if dst_bytes <= 0:
        raise ValueError(f'Destination slice is empty: {dst_path}')

    zero_block = b'\x00' * block_size
    copy_blocks = 0
    zero_blocks = 0
    literal_bytes = 0
    op_count = 0

    with src_path.open('rb') as sf, dst_path.open('rb') as df, delta_out_path.open('wb') as out:
        out.write(struct.pack(
            '<8sIQ32sQ32s',
            DELTA_MAGIC, block_size, src_bytes, src_sha_raw, dst_bytes, dst_sha_raw,
        ))

        pending_copy_start = -1
        pending_copy_cnt = 0
        pending_zero_cnt = 0
        pending_lit = bytearray()

        def flush_copy():
            nonlocal pending_copy_start, pending_copy_cnt, copy_blocks, op_count
            if pending_copy_cnt > 0:
                out.write(struct.pack('<BII', OP_COPY, pending_copy_start, pending_copy_cnt))
                copy_blocks += pending_copy_cnt
                op_count += 1
                pending_copy_start = -1
                pending_copy_cnt = 0

        def flush_zero():
            nonlocal pending_zero_cnt, zero_blocks, op_count
            if pending_zero_cnt > 0:
                out.write(struct.pack('<BI', OP_ZERO, pending_zero_cnt))
                zero_blocks += pending_zero_cnt
                op_count += 1
                pending_zero_cnt = 0

        def flush_lit():
            nonlocal literal_bytes, op_count
            if pending_lit:
                out.write(struct.pack('<BI', OP_LITERAL, len(pending_lit)))
                out.write(pending_lit)
                literal_bytes += len(pending_lit)
                op_count += 1
                pending_lit.clear()

        def flush_all():
            flush_copy()
            flush_zero()
            flush_lit()

        # Maintain a small sequential cache of the source slice around the active COPY cursor
        # to verify matches without random pread syscalls on contiguous runs.
        src_cache_off = -1
        src_cache_buf = b''
        src_cache_size = 4 * 1024 * 1024

        def read_src_block(blk_idx):
            nonlocal src_cache_off, src_cache_buf
            off = blk_idx * block_size
            if src_cache_off <= off and off + block_size <= src_cache_off + len(src_cache_buf):
                rel = off - src_cache_off
                return src_cache_buf[rel:rel + block_size]
            sf.seek(off)
            src_cache_off = off
            src_cache_buf = sf.read(src_cache_size)
            return src_cache_buf[:block_size]

        while True:
            chunk = df.read(block_size * 1024)
            if not chunk:
                break
            pos = 0
            n = len(chunk)
            while pos < n:
                blk = chunk[pos:pos + block_size]
                pos += len(blk)
                if len(blk) == block_size:
                    if blk == zero_block:
                        flush_copy()
                        flush_lit()
                        pending_zero_cnt += 1
                        continue
                    # Fast path: check next sequential source block first so contiguous
                    # runs stay contiguous even when duplicate blocks exist earlier.
                    matched_idx = None
                    if pending_copy_cnt > 0:
                        next_idx = pending_copy_start + pending_copy_cnt
                        if (next_idx + 1) * block_size <= src_bytes:
                            if read_src_block(next_idx) == blk:
                                matched_idx = next_idx
                    if matched_idx is None:
                        dig = hashlib.blake2b(blk, digest_size=16).digest()
                        cand = src_index.get(dig)
                        if cand is not None and read_src_block(cand) == blk:
                            matched_idx = cand
                    if matched_idx is not None:
                        flush_zero()
                        flush_lit()
                        if pending_copy_cnt > 0 and matched_idx == pending_copy_start + pending_copy_cnt:
                            pending_copy_cnt += 1
                        else:
                            flush_copy()
                            pending_copy_start = matched_idx
                            pending_copy_cnt = 1
                        continue
                flush_copy()
                flush_zero()
                pending_lit.extend(blk)
                if len(pending_lit) >= MAX_LITERAL_CHUNK:
                    flush_lit()

        flush_all()
        out.write(bytes([OP_EOF]))

    return {
        'src_bytes': src_bytes,
        'src_sha256': src_sha_raw.hex(),
        'dst_bytes': dst_bytes,
        'dst_sha256': dst_sha_raw.hex(),
        'delta_bytes': delta_out_path.stat().st_size,
        'copy_blocks': copy_blocks,
        'zero_blocks': zero_blocks,
        'literal_bytes': literal_bytes,
        'op_count': op_count,
    }
