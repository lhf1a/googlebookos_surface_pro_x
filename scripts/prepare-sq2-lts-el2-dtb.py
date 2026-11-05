#!/usr/bin/env python3
"""Add the paired EFI preboot ABI to a fresh SC8180X DTB, without deployment.

Do not copy an old board DTB/phandles or mark authentication as completed.
Requires python3-libfdt. Existing conflicting state is refused, not overwritten.
"""
import argparse
from pathlib import Path
import struct

import libfdt


def cells(*values):
    return struct.pack('>' + 'I' * len(values), *values)


def get(dt, path, name):
    try:
        return bytes(dt.getprop(dt.path_offset(path), name))
    except libfdt.FdtException as exc:
        if exc.err != -libfdt.NOTFOUND:
            raise
        return None


def prepare(data):
    dt = libfdt.Fdt(data)
    compatible = get(dt, '/', 'compatible').split(b'\0')
    if not {b'microsoft,surface-pro-x', b'qcom,sc8180x'} <= set(compatible):
        raise ValueError('requires Surface Pro X / SC8180X hardware DT')
    properties = {
        ('/', 'microsoft,el2-kernel-fdt'): bytes(8),
        ('/', 'microsoft,el2-pas-trace'): b'\xff' * (3 * 4 * 5 * 8),
        ('/', 'microsoft,el2-venus-prepared'): cells(0, 9, 0x8c000000, 0x500000),
    }
    rmtfs = '/reserved-memory/memory@85500000'
    if (get(dt, rmtfs, 'compatible') != b'qcom,rmtfs-mem\0'
            or get(dt, rmtfs, 'reg') != cells(0, 0x85500000, 0, 0x200000)
            or get(dt, rmtfs, 'qcom,vmid') != cells(15)
            or get(dt, rmtfs, 'qcom,client-id') != cells(1)):
        raise ValueError('RMTFS resource/ownership ABI mismatch')
    nodes = [rmtfs]
    properties[rmtfs, 'microsoft,el2-preboot-rmtfs'] = b''
    for address, dsp in [('17300000', 'adsp'), ('8300000', 'cdsp'), ('4080000', 'mpss')]:
        path = '/soc@0/remoteproc@' + address
        if get(dt, path, 'compatible') != f'qcom,sc8180x-{dsp}-pas\0'.encode():
            raise ValueError(f'{path}: incompatible DSP')
        if not get(dt, path, 'firmware-name') or not get(dt, path, 'memory-region'):
            raise ValueError(f'{path}: missing firmware/resource binding')
        properties[path, 'microsoft,el2-preboot'] = b''
        properties[path, 'qcom,broken-reset'] = b''
        nodes.append(path)
    for path in nodes:
        properties[path, 'microsoft,el2-preboot-complete'] = cells(0)
        properties[path, 'microsoft,el2-preboot-status'] = bytes(24)
    # Validate the complete transaction before changing even the in-memory DT.
    for (path, name), value in properties.items():
        old = get(dt, path, name)
        if old is not None and old != value:
            raise ValueError(f'{path}:{name}: conflicting/non-pristine handoff state')
    dt.resize(len(data) + 4096)
    for (path, name), value in properties.items():
        # Re-resolve offsets after every property insertion.
        dt.setprop(dt.path_offset(path), name, value)
    dt.pack()
    return bytes(dt.as_bytearray())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    data = prepare(args.input.read_bytes())
    # Never truncate the input or an existing candidate, including hardlinks.
    with args.output.open('xb') as output:
        output.write(data)
    print(f'BUILD ONLY: {args.output.resolve()} ({len(data)} bytes); hardware untested')


if __name__ == '__main__':
    main()
