#!/usr/bin/env python3
"""Append the full-Mesa hardware-video fix to an existing full-Mesa initramfs.

For an already built/deployed full-Mesa initramfs (older full_image output):
installs freshly source-compiled paired Venus modules (with ``sq2_*_caps`` and
``venus_sq2_pm_publish`` restored) and switches the OMX service to the
ByteBuffer/SoftwareRenderer variant with its private QTI utility library.
Only one new gzip member is inserted before the final credential member;
every original member stays byte-identical.  Never touches a device.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import full_image as fi  # noqa: E402
import full_image_video as video  # noqa: E402
from disk_image import module, require  # noqa: E402
from kernel_baseline import policy  # noqa: E402


def build(base, venus_modules, runtime, output):
    require(not output.exists(), 'Output must be fresh')
    data = base.read_bytes()
    records, bodies = fi.records_from_gzip(base)
    require(bodies.get('etc/sq2-graphics-mode') == b'full-mesa\n',
            'Input is not a full-Mesa initramfs')
    release = policy()['kernel_release']
    built = {}
    for mod in video.VENUS_MODULES:
        path = Path(venus_modules) / mod
        require(path.is_file(), 'Missing freshly compiled Venus module: ' + str(path))
        body = path.read_bytes()
        magic = re.findall(rb'(?:^|\x00)vermagic=([^\x00]+)', body)
        require(body.startswith(b'\x7fELF') and magic and
                all(m.split()[0].decode() == release for m in magic),
                'Venus module vermagic mismatch: ' + mod)
        built[mod] = body
    video.validate_venus_core_module(built['venus-core.ko'])
    names = [n for n in bodies if n.rsplit('/', 1)[-1] in video.VENUS_MODULES]
    require(names, 'No Venus modules in input')
    delta = {}
    for name in names:
        replacement = built[name.rsplit('/', 1)[-1]]
        delta[name] = (records[name][1][1], replacement)
    files = Path(runtime) / 'files'
    scratch_b = {k: bodies[k] for k in (video.OMX_SERVICE, video.OMX_SUMS)}
    scratch_b[video.OMX_GRALLOC_UTILS_SOURCE] = (files / video.OMX_GRALLOC_UTILS_SOURCE).read_bytes()
    scratch_r = {}
    omx = video.mesa_omx(scratch_r, scratch_b, fi.new_record)
    delta[video.OMX_SERVICE] = (records[video.OMX_SERVICE][1][1], scratch_b[video.OMX_SERVICE])
    delta[video.OMX_SUMS] = (records[video.OMX_SUMS][1][1], scratch_b[video.OMX_SUMS])
    delta[video.OMX_GRALLOC_UTILS] = (0o100644, scratch_b[video.OMX_GRALLOC_UTILS])
    # Update etc/sq2-video-native/SHA256SUMS metadata when present.
    native_sums = 'etc/sq2-video-native/SHA256SUMS'
    if native_sums in bodies:
        sums_body = video._rewrite_sums(bodies[native_sums], {
            'venus-core.ko': built['venus-core.ko'],
            'venus-dec.ko': built['venus-dec.ko'],
        })
        delta[native_sums] = (records[native_sums][1][1], sums_body)
    cpio = module('pack_vulkan_cpio', 'pack-sq2-vulkan.py')
    pack = module('qti_pack_tail', 'pack-sq2-qti-system-display.py')
    raw = b''.join(cpio.entry(n, b, m) for n, (m, b) in delta.items()) + cpio.entry('TRAILER!!!', b'', 0)
    raw += b'\0' * (-len(raw) % 512)
    member = gzip.compress(raw, mtime=0)
    tail = pack.credential_tail(data)
    require(tail and data.endswith(tail), 'Credential member must be last')
    result = data[:len(data) - len(tail)] + member + tail
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('xb') as stream:
        stream.write(result)
    _, after = fi.records_from_gzip(output)
    expected = dict(bodies, **{n: b for n, (_, b) in delta.items()})
    require(after == expected, 'Unexpected effective archive content')
    summary = {
        'base_sha256': hashlib.sha256(data).hexdigest(),
        'output_sha256': hashlib.sha256(result).hexdigest(),
        'bytes': len(result),
        'changed': sorted(delta),
        'venus_source_compiled_sha256': {k: video.sha(v) for k, v in sorted(built.items())},
        'omx': omx,
        'original_members_byte_identical': True,
        'credential_member_last': True,
        'device_written': False,
        'hardware_validated': False,
    }
    output.with_suffix('.json').write_text(json.dumps(summary, indent=2) + '\n')
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--venus-modules', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, default=ROOT / 'private/build-inputs/full-image-runtime')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(build(args.base, args.venus_modules, args.runtime, args.output), indent=2))


if __name__ == '__main__':
    main()
