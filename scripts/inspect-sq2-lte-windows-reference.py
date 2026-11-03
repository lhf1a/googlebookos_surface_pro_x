#!/usr/bin/env python3
"""Verify pinned Windows EFS GUID/call sites offline; never open a device."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import uuid

SHA = '1ad254e98e1e154f8382ab728fad8f23a7188b81c6db18736439e53cab067127'
SITES = {
    'modem_fs1': (0x20068, 0x23210, {0x4e6c: 0x9101a101, 0x4e74: 0x94006b62,
                                  0x4e80: 0xf00000e8, 0x4e84: 0x91084108}),
    'modem_fs2': (0x20078, 0x231f0, {0x4e8c: 0x9101e2c1, 0x4e94: 0x94006b5a,
                                  0x4e9c: 0xf00000e8, 0x4ea0: 0x9107c108}),
    'modem_fsg': (0x1fee8, 0x23200, {0x4e78: 0xf00000f3, 0x4ea8: 0xf00000c8,
                                  0x4eac: 0x913ba101, 0x4eb4: 0x94006b52,
                                  0x4ebc: 0x91080268}),
    'modem_fsc': (0x20088, 0x231e0, {0x4ec4: 0x900000e8, 0x4ec8: 0x91022101,
                                  0x4ed0: 0x94006b4b, 0x4ed8: 0xf00000e8,
                                  0x4edc: 0x91078108}),
}
GUIDS = ['ebbeadaf-22c9-e33b-8f5d-0e81686a68cb', '0a288b1f-22c9-e33b-8f5d-0e81686a68cb',
         '638ff8e2-22c9-e33b-8f5d-0e81686a68cb', '57b90a16-22c9-e33b-8f5d-0e81686a68cb']


def inspect(data):
    if hashlib.sha256(data).hexdigest() != SHA:
        raise ValueError('Unrecognized RemoteFS reference SHA256')
    pe = struct.unpack_from('<I', data, 60)[0]
    machine, count = struct.unpack_from('<HH', data, pe + 4)
    if data[pe:pe+4] != b'PE\0\0' or machine != 0xaa64 or count > 96:
        raise ValueError('Expected bounded ARM64 PE')
    optional = pe + 24
    size = struct.unpack_from('<H', data, pe + 20)[0]
    if struct.unpack_from('<H', data, optional)[0] != 0x20b:
        raise ValueError('Expected PE32+')
    sections = [struct.unpack_from('<IIII', data, optional + size + i*40 + 8)
                for i in range(count)]

    def at(rva, length):
        for _virtual_size, va, raw_size, raw in sections:
            if va <= rva and rva + length <= va + raw_size:
                start = raw + rva - va
                if start + length > len(data):
                    break
                return data[start:start+length]
        raise ValueError('Unmapped reference RVA ' + hex(rva))

    result = []
    for (name, (string_rva, guid_rva, words)), expected in zip(SITES.items(), GUIDS):
        text = ('/boot/' + name).encode() + b'\0'
        if at(string_rva, len(text)) != text:
            raise ValueError('Remote path mismatch: ' + name)
        for rva, word in words.items():
            if struct.unpack('<I', at(rva, 4))[0] != word:
                raise ValueError('Instruction mismatch at ' + hex(rva))
        guid = str(uuid.UUID(bytes_le=at(guid_rva, 16)))
        if guid != expected:
            raise ValueError('Type GUID mismatch: ' + name)
        result.append(dict(name=name, type_guid=guid, guid_rva=hex(guid_rva),
                           remote_path_rva=hex(string_rva), instruction_rvas=[hex(x) for x in words]))
    if struct.unpack('<I', at(0x4f1c, 4))[0] != 0x97fff48f:
        raise ValueError('Partition lookup call changed')
    return dict(status='verified-static-reference-not-runtime-proof', sha256=SHA,
                partitions=result, lookup_call_rva='0x4f1c', lookup_target_rva='0x2158',
                hardware_access=False, limits=[
                    'This maps one Windows disk-backed branch, not a proven firmware storage backend.',
                    'Known type GUID/size does not establish EFS contents, calibration, or unit ownership.',
                    'No SCM, QSPI, EFI BlockIo availability or LTE connectivity inferred.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--driver', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = inspect(args.driver.read_bytes())
    with args.output.open('x', encoding='utf-8') as output:
        json.dump(result, output, indent=2)
        output.write('\n')
    print('PASS: pinned ARM64 PE; 4 EFS GUID/path mappings; 19 instruction sites; no hardware access')


if __name__ == '__main__':
    main()
