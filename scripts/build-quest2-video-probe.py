#!/usr/bin/env python3
"""Build OMX clients and process-local ABI tools, never a replacement codec.

Supply an Android NDK clang and the pinned AOSP OMX headers. No networking,
donor binary changes, device writes or system service installation is performed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc', type=Path, required=True, help='NDK clang, including Windows clang.exe under WSL')
    p.add_argument('--headers', type=Path, required=True, help='Pinned AOSP OMX_*.h directory')
    p.add_argument('--output', type=Path, required=True, help='Fresh directory')
    p.add_argument('--with-decode-client', action='store_true',
                   help='Also build the explicit OMX decode client; does not run it')
    p.add_argument('--with-vidc-bridge', action='store_true',
                   help='Also build the process-local VIDC bridge; does not enable it')
    p.add_argument('--native-extra-default', action='store_true',
                   help='Enable real firmware AVC VUI/HEVC HDR extradata in the built bridge by default')
    p.add_argument('--media-file-binder', type=Path,
                   help='Also build MediaCodec container probe, linking this actual platform libbinder_ndk.so')
    a = p.parse_args()
    if a.native_extra_default and not a.with_vidc_bridge:
        p.error('--native-extra-default requires --with-vidc-bridge')
    cfg = json.loads((ROOT/'configs/quest2-video-omx-headers.json').read_text())
    try:
        for name, expected in cfg['sha256'].items():
            if hashlib.sha256((a.headers/name).read_bytes()).hexdigest() != expected:
                raise ValueError('OMX header hash mismatch: '+name)
        if a.media_file_binder and not a.media_file_binder.is_file():
            raise ValueError('Platform libbinder_ndk.so is missing')
        if a.output.exists():
            raise ValueError('Output must be fresh')
        a.output.mkdir(parents=True)
        windows_cc = os.name != 'nt' and a.cc.suffix.lower() == '.exe'

        def path(value):
            value = str(Path(value).resolve())
            return subprocess.check_output(['wslpath', '-w', value], text=True).strip() if windows_cc else value

        products = [
            ('quest2-omx-probe.c', 'quest2-omx-probe', ['-I', path(a.headers)]),
        ]
        if a.with_decode_client:
            products.append(('quest2-omx-decode.c', 'quest2-omx-decode',
                             ['-I', path(a.headers), '-pthread']))
        if a.with_vidc_bridge:
            products.append(('quest2-vidc-bridge.c', 'libquest2-vidc-bridge.so',
                             ['-fPIC', '-shared', '-pthread', '-llog'] +
                             (['-DQUEST2_NATIVE_EXTRA_DEFAULT=1'] if a.native_extra_default else [])))
            products.append(('quest2-ion-probe.c', 'quest2-ion-probe', []))
        if a.media_file_binder:
            products.append(('sq2-mediacodec-file.c', 'sq2-mediacodec-file',
                             ['-lmediandk', path(a.media_file_binder), '-Wl,--allow-shlib-undefined']))
        commands = []
        for source, filename, extra in products:
            cmd = [str(a.cc.resolve()), '--target=aarch64-linux-android35', '-std=gnu11', '-O2',
                   '-Wall', '-Wextra', '-Werror', *extra, path(ROOT/'probe/video'/source),
                   '-ldl', '-o', path(a.output/filename)]
            run = subprocess.run(cmd, capture_output=True, text=True)
            record = dict(command=cmd, stdout=run.stdout, stderr=run.stderr, exit=run.returncode)
            commands.append(record)
            (a.output/'COMMANDS.json').write_text(json.dumps(commands, indent=2)+'\n')
            if run.returncode:
                raise RuntimeError('Compilation failed: '+source)
        report = dict(status='compiled-not-decoding-verified', commands=commands,
                      target='aarch64-linux-android35',
                      optional_features=dict(decode_client=a.with_decode_client,
                                             vidc_bridge=a.with_vidc_bridge,
                                             native_extra_default=a.native_extra_default,
                                             media_file=bool(a.media_file_binder)),
                      media_file_binder_sha256=hashlib.sha256(a.media_file_binder.read_bytes()).hexdigest() if a.media_file_binder else None,
                      header_sha256=cfg['sha256'],
                      memory_backend='dma-heap-system-uncached-v1' if a.with_vidc_bridge else None,
                      memory_backend_sha256=hashlib.sha256((ROOT/'probe/graphics/quest2-system-ion.h').read_bytes()).hexdigest() if a.with_vidc_bridge else None,
                      source_sha256={source: hashlib.sha256((ROOT/'probe/video'/source).read_bytes()).hexdigest()
                                     for source, _, _ in products},
                      sha256={name: hashlib.sha256((a.output/name).read_bytes()).hexdigest()
                              for _, name, _ in products})
        (a.output/'BUILD.json').write_text(json.dumps(report, indent=2)+'\n')
        print(json.dumps(report, indent=2))
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as e:
        p.exit(1, str(e)+'\n')


if __name__ == '__main__':
    main()
