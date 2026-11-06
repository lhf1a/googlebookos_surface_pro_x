#!/usr/bin/env python3
"""Add the paired EFI preboot ABI to a fresh SC8180X DTB, without deployment.

Do not copy an old board DTB/phandles or mark authentication as completed.
Requires python3-libfdt. Existing conflicting state is refused, not overwritten.
"""
import argparse
import importlib.util
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


def ensure_video_provider(data):
    """Give the native late-consumed VIDEOCC a collision-free DT phandle.

    Dtc omits phandles for unreferenced nodes. The 6.3 clock overlay supplied
    one, while 6.18's native node has no static consumer yet. HFI resolves the
    live provider and requires a valid phandle; do not pin a machine's value.
    """
    dt = libfdt.Fdt(data)
    path = '/soc@0/clock-controller@ab00000'
    compatible = get(dt, path, 'compatible')
    if compatible is None:
        return data
    if not {b'qcom,sc8180x-videocc', b'qcom,sm8150-videocc'} <= set(compatible.split(b'\0')):
        raise ValueError('unexpected native VIDEOCC binding')
    for name in ('#clock-cells', '#reset-cells', '#power-domain-cells'):
        if get(dt, path, name) != cells(1):
            raise ValueError('unexpected VIDEOCC resource cell count: '+name)
    existing = get(dt, path, 'phandle') or get(dt, path, 'linux,phandle')
    if existing is not None:
        if len(existing) != 4 or existing in (cells(0), cells(0xffffffff)):
            raise ValueError('invalid existing VIDEOCC phandle')
        return data
    spec = importlib.util.spec_from_file_location('dt_offsets', Path(__file__).with_name('sq2-overlay-phandles.py'))
    offsets = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(offsets)
    props = offsets.property_offsets(data)
    used = {struct.unpack('>I', value)[0] for (_, name), (_, value) in props.items()
            if name in ('phandle', 'linux,phandle') and len(value) == 4}
    value = max(used, default=0) + 1
    if value >= 0xffffffff:
        raise ValueError('no phandle available above existing assignments')
    dt.resize(len(data) + 128)
    dt.setprop(dt.path_offset(path), 'phandle', cells(value))
    dt.pack()
    return bytes(dt.as_bytearray())


def prepare(data, irq_backed=False):
    dt = libfdt.Fdt(data)
    compatible = get(dt, '/', 'compatible').split(b'\0')
    if not {b'microsoft,surface-pro-x', b'qcom,sc8180x'} <= set(compatible):
        raise ValueError('requires Surface Pro X / SC8180X hardware DT')
    properties = {
        ('/', 'microsoft,el2-kernel-fdt'): bytes(8),
        ('/', 'microsoft,el2-pas-trace'): b'\xff' * (3 * 4 * 5 * 8),
        ('/', 'microsoft,el2-venus-prepared'): cells(0, 9, 0x8c000000, 0x500000),
    }
    if irq_backed:
        path = '/soc@0/iommu@15000000'
        compat = (get(dt, path, 'compatible') or b'').split(b'\0')
        interrupts = get(dt, path, 'interrupts') or b''
        # Paired with sq2_el2_context_limit_allowed() in the migrated kernel.
        # Hardware IDR bank counts and EL2 remain checked by the kernel itself.
        if (b'qcom,sc8180x-smmu-500' not in compat
                or get(dt, path, 'reg') != cells(0, 0x15000000, 0, 0x100000)
                or get(dt, path, '#global-interrupts') != cells(1)
                or len(interrupts) != 107 * 3 * 4):
            raise ValueError('SMMU resource/IRQ layout does not match paired context-pool quirk')
        properties[path, 'microsoft,el2-irq-backed-context-pool'] = b''
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
    return ensure_video_provider(bytes(dt.as_bytearray()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--irq-backed-context-pool', action='store_true',
                        help='enable the paired SC8180X EL2 kernel CB allocation quirk')
    args = parser.parse_args()
    data = prepare(args.input.read_bytes(), args.irq_backed_context_pool)
    # Never truncate the input or an existing candidate, including hardlinks.
    with args.output.open('xb') as output:
        output.write(data)
    print(f'BUILD ONLY: {args.output.resolve()} ({len(data)} bytes); hardware untested')


if __name__ == '__main__':
    main()
