#!/usr/bin/env python3
"""Verify the pinned SQ1 secure PIL/CSRT reference offline; never access hardware."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

PIL_SHA = 'da09677b8bc3f35c4217e768f73a1ccc19e112cedf724bd14d393e1545511e22'
CSRT_SHA = '4620734b5ec83582882a965070952d95fa2bc2cdae5af0a4d14c147dc32f94ee'


def require(ok, message):
    if not ok:
        raise ValueError(message)


def records(data, start, count, end):
    result = []
    for _ in range(count):
        require(start + 4 <= end, 'Truncated record')
        size = struct.unpack_from('<I', data, start)[0]
        require(size >= 4 and start + size <= end, 'Invalid record length')
        result.append(data[start:start + size])
        start += size
    require(start == end, 'Record boundary mismatch')
    return result


def csrt_layout(data):
    require(len(data) >= 36 and data[:4] == b'CSRT', 'Expected CSRT')
    require(struct.unpack_from('<I', data, 4)[0] == len(data) and sum(data) % 256 == 0,
            'CSRT length/checksum failure')
    groups = records(data, 36, 2, len(data))
    group = groups[1]
    require(group[4:8] == b'QCOM' and struct.unpack_from('<H', group, 12)[0] == 0x100c,
            'Expected Qualcomm secure resource group')
    # These offsets are relative to the 24-byte group header, as used by QcSkExt.
    n_smmu, smmu_off, n_domain, domain_off, n_rule, rule_off = struct.unpack_from('<6I', group, 40)
    require((n_smmu, smmu_off, n_domain, domain_off, n_rule, rule_off) ==
            (2, 0x40, 40, 0x1ab4, 76, 0x1ce4), 'Unexpected secure table layout')
    smmus = records(group, 24 + smmu_off, n_smmu, 24 + domain_off)
    domains = records(group, 24 + domain_off, n_domain, 24 + rule_off)
    rules = records(group, 24 + rule_off, n_rule, 24 + 0x28bb)
    streams = []
    for smmu in smmus:
        require(len(smmu) >= 96, 'Truncated SMMU resource')
        base = struct.unpack_from('<Q', smmu, 12)[0]
        count, stride, _field88, offset = struct.unpack_from('<4I', smmu, 80)
        require(stride == 10 and offset == 96 and offset + count * stride == len(smmu),
                'Invalid packed stream table')
        for index in range(count):
            raw = smmu[offset + index * stride:offset + (index + 1) * stride]
            sid = struct.unpack_from('<H', raw)[0]
            if base == 0x15000000 and 0x2300 <= sid < 0x2380:
                streams.append({'sid': hex(sid), 'domain_byte': raw[6], 'raw': raw.hex()})
    require([s for s in streams if s['domain_byte'] == 12] ==
            [{'sid': '0x2342', 'domain_byte': 12, 'raw': '4223010000000c010000'}],
            'Video firmware stream candidate changed')
    require(not any(s['sid'] == '0x2302' for s in streams), 'Unexpected 2302 stream')
    require(domains[11].hex() == '0e000000020000000c0000202401', 'Domain12 changed')
    selected_rules = []
    for index in (45, 46, 47, 61, 66, 67, 68, 72):
        rule = rules[index]
        require(len(rule) == 39, 'Unexpected rule size')
        ns, src_stride, src_off, nd, dst_stride, dst_off = struct.unpack_from('<6I', rule, 8)
        require((ns, src_stride, src_off, nd, dst_stride, dst_off) == (1, 1, 36, 1, 2, 37),
                'Unexpected ownership rule layout')
        selected_rules.append([rule[36], rule[37], rule[38]])
    require(selected_rules == [[3, 4, 7], [4, 3, 7], [3, 14, 7], [4, 14, 7],
                               [14, 12, 7], [12, 14, 7], [14, 3, 7], [14, 4, 7]],
            'Ownership rules changed')
    policy = group[24 + struct.unpack_from('<I', group, 76)[0]:]
    require(len(policy) >= 72, 'Truncated extension policy')
    length = struct.unpack_from('<I', policy)[0]
    require(72 <= length <= len(policy), 'Invalid policy length')
    policy = policy[:length]
    count, stride, offset = struct.unpack_from('<3I', policy, 40)
    require((count, stride, offset) == (45, 16, 0xa4) and offset + count * stride <= length,
            'Invalid secure MMIO list')
    mmio = [struct.unpack_from('<QQ', policy, offset + i * stride) for i in range(count)]
    for base in (0xaae0000, 0xaae1000, 0xaae2000, 0xaae3000):
        require((base, 4096) in mmio, 'Video MMIO missing from extension policy')
    return {'streams': streams, 'ownership_rules': selected_rules,
            'secure_extension_mmio': [hex(b) for b, size in mmio if 0xaa00000 <= b < 0xac00000]}


def inspect(pil, csrt):
    require(hashlib.sha256(pil).hexdigest() == PIL_SHA, 'Unrecognized secure extension hash')
    require(hashlib.sha256(csrt).hexdigest() == CSRT_SHA, 'Unrecognized CSRT hash')
    # shortcut: fixed raw/text mapping for this hash only; parse PE sections for another build.
    checks = {
        'map_secure_io': (0x3b0c, [0xf0000068, 0xf9402908, 0xd2800004,
                                  0x52800242, 0xaa1303e1, 0xd63f0100]),
        'pre_auth_memory_stage': (0xaf9c, [0x52800021, 0xaa1403e0, 0x97fffde5]),
        'tz_auth': (0xafc4, [0xb940b280, 0x940007a0]),
        'post_auth_memory_stage': (0xafd4, [0x52800088, 0x52800041, 0xb900ea88,
                                           0xaa1403e0, 0x97fffdd5]),
        'video_destination12': (0x97a8, [0x52800188, 0xb90007e8]),
        'video_source14': (0x97d0, [0x528001c9, 0xb90003e9]),
        'segment_assignment': (0x9b80, [0x910163e4, 0xb9000e68, 0x52800023,
                                       0x910003e2, 0x52800021, 0xaa1303e0, 0x97ffef2c]),
        'soc_domain_api': (0x61a4, [0x90000068, 0xf9404108, 0xd63f0100]),
        'start_sequence': (0xa260, [0x97ffff8c, 0x940004a5, 0x940005b2]),
        'delay_frequency': (0x3d2c, [0xf0000068, 0xf9402d08]),
        'delay_microsecond_divisor': (0x3db8, [1000000, 0]),
    }
    for name, (rva, words) in checks.items():
        require(list(struct.unpack_from('<' + 'I' * len(words), pil, rva - 0xc00)) == words,
                'Instruction mismatch: ' + name)
    for slot, name in [(0x12050, b'MapSecureIo'), (0x12080, b'AssignMemoryToSocDomain'),
                       (0x12058, b'QueryPerformanceFrequency')]:
        name_rva = struct.unpack_from('<Q', pil, slot - 0xc00)[0]
        raw = name_rva - 0xc00 + 2
        require(pil[raw:raw + len(name) + 1] == name + b'\0', 'Unexpected API import')
    require(struct.unpack_from('<I', pil, 0x1a2b0)[0] == 9, 'Expected PAS9 descriptor')
    require(struct.unpack_from('<Q', pil, 0x1a2c8)[0] == 0x14000a280 and
            struct.unpack_from('<Q', pil, 0x1a2e0)[0] == 0x140009760, 'Video callbacks changed')
    result = csrt_layout(csrt)
    # Runtime startup candidate consumes these exact HWIO table entries.
    hwio = {94: 0xaae3084, 95: 0xaae3050, 96: 0xaae3054, 97: 0xaae3058,
            98: 0xaae3070, 99: 0xaae306c, 100: 0xaae3078, 101: 0xaae3088,
            102: 0xaae308c, 103: 0xaae304c, 104: 0xaae0000, 105: 0xaae3000,
            106: 0xaae1028, 107: 0xaae102c, 108: 0xaae1020, 109: 0xaae1024,
            110: 0xaae1030, 111: 0xaae1034, 129: 0xaae2010, 130: 0xaae2000,
            176: 0xaae1084}
    for index, address in hwio.items():
        entry = struct.unpack_from('<8I', pil, 0x1a8a0 + index * 32)
        require(entry == (index, 0, address & ~4095, 0, 4096, address & 4095, 0, 0),
                'HWIO entry changed: ' + str(index))
    result['startup_hwio'] = {str(i): hex(addr) for i, addr in hwio.items()}
    result.update(status='pinned-static-reference-not-runtime-proof', pil_sha256=PIL_SHA,
                  csrt_sha256=CSRT_SHA, instruction_checks=list(checks), hardware_access=False,
                  limits=['2342/domain12 association is a firmware-stream candidate, not a live SQ2 observation',
                          'MapSecureIo policy does not grant Linux EL2 MMIO access',
                          'Windows ownership handles are not Linux SCM assignment arguments',
                          'No firmware execution, PAS error interpretation or decoded frames'])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--extension', type=Path, required=True)
    parser.add_argument('--csrt', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = inspect(args.extension.read_bytes(), args.csrt.read_bytes())
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print('WINDOWS_PIL_REFERENCE_OK candidate_sid=2342 domain=12 NO_DEVICE_ACCESS')


if __name__ == '__main__':
    main()
