"""Exercise the real decoder with wire fixtures; no modem/socket required."""
from pathlib import Path
import os
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'probe/test2/sq2-qmi-readonly.c'


def tlv(kind, data):
    return struct.pack('<BH', kind, len(data)) + data


def reply(message, *items):
    body = b''.join(items)
    return struct.pack('<BHHH', 2, 1, message, len(body)) + body


OK = tlv(2, b'\0' * 4)
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    harness = tmp / 'decode.c'
    harness.write_text('''
#define main qmi_command_main
#include "''' + str(SOURCE) + '''"
#undef main
int main(int argc, char **argv) {
    uint8_t data[4096];
    if(argc!=4) return 2;
    size_t n=strlen(argv[3])/2;
    if(n>sizeof(data)) return 2;
    for(size_t i=0;i<n;i++) {
        unsigned value;
        if(sscanf(argv[3]+2*i,"%2x",&value)!=1) return 2;
        data[i]=(uint8_t)value;
    }
    return decode_response(data,n,(unsigned)strtoul(argv[1],0,0),argv[2]);
}
''')
    cc = os.environ.get('HOST_CC', 'cc')
    decoder, command = tmp / 'decode', tmp / 'probe'
    for source, output in [(harness, decoder), (SOURCE, command)]:
        subprocess.run([cc, '-Wall', '-Wextra', '-Werror', '-O2',
                        str(source), '-o', str(output)], check=True)

    def check(kind, message, data, expected=0, contains=(), absent=()):
        result = subprocess.run([str(decoder), hex(message), kind, data.hex()],
                                capture_output=True, text=True, timeout=5)
        assert result.returncode == expected, (kind, data.hex(), result)
        for value in contains:
            assert value in result.stdout, result.stdout
        for value in absent:
            assert value not in result.stdout, result.stdout

    for mode, name in enumerate(('online', 'low-power', 'factory-test', 'offline',
                                 'reset', 'shutting-down', 'persistent-low-power',
                                 'mode-only-low-power')):
        check('mode', 0x2d, reply(0x2d, OK, tlv(1, bytes([mode]))),
              contains=(f'operating_mode={name}\n',))
    check('mode', 0x2d, reply(0x2d, OK, tlv(1, b'\x02')),
          contains=('factory-test',), absent=('offline',))
    check('mode', 0x2d, reply(0x2d, OK, tlv(1, b'\xff')),
          contains=('operating_mode=unknown',))
    check('mode', 0x2d, reply(0x2d, OK, tlv(1, b'\x03'),
                             tlv(0x10, b'\x04\0'), tlv(0x11, b'\x01')),
          contains=('offline_reason_mask=0x0004', 'hardware_restricted=1'))
    nas = tlv(1, bytes([1, 2, 1, 2, 1, 8]))
    check('nas', 0x24, reply(0x24, OK, nas, tlv(0x12, b'PRIVATE_LOCATION')),
          contains=('registration=1', 'ps_attach=1', 'radio_interfaces=8'),
          absent=('PRIVATE_LOCATION', 'TLV 12'))
    for data in (b'', bytes([1, 2, 1, 2]), bytes([1, 2, 1, 2, 2, 8])):
        check('nas', 0x24, reply(0x24, OK, tlv(1, data)), expected=1)
    card = bytes(8) + bytes([1, 1, 0, 3, 10, 0, 1])
    check('uim', 0x2f, reply(0x2f, OK, tlv(0x10, card)),
          contains=('card_slots_reported=1', 'first_card_state=1', 'apps=1'))
    check('uim', 0x2f, reply(0x2f, OK, tlv(0x10, bytes(9))),
          contains=('card_slots_reported=0',))
    check('uim', 0x2f, reply(0x2f, OK, tlv(0x10, card[:14])), expected=1)
    check('revision', 0x23, reply(0x23, OK, tlv(1, b'test\x01')),
          contains=('revision=test?',))
    check('wds', 0x22, reply(0x22, OK, tlv(1, b'\x01')),
          contains=('status=1 (1=disconnected',))
    check('audio-domain', 0x21, reply(0x21, OK, tlv(0x10, b'avs/audio')),
          contains=('avs/audio',))
    valid = reply(0x2d, OK, tlv(1, b'\x02'))
    # Wrong transaction/message/type/length, truncation and bad TLVs fail closed.
    for offset, value in [(0, 4), (1, 2), (3, 0x2e), (5, 0)]:
        damaged = bytearray(valid)
        damaged[offset] = value
        check('mode', 0x2d, damaged, expected=1)
    for data in (b'', valid[:6], valid[:-1],
                 reply(0x2d, tlv(1, b'\x02')),
                 reply(0x2d, OK, OK, tlv(1, b'\x02')),
                 reply(0x2d, tlv(2, b'\0' * 3), tlv(1, b'\x02')),
                 reply(0x2d, tlv(2, b'\x01\0\x03\0'), tlv(1, b'\x02')),
                 reply(0x2d, OK), reply(0x2d, OK, b'\x10'),
                 reply(0x2d, OK, b'\x01\x02\0\x02'),
                 reply(0x2d, OK, tlv(1, b''))):
        check('mode', 0x2d, data, expected=1)
    for node, port, kind in [('bad', '1', 'mode'), ('-1', '1', 'mode'),
                             (' 1', '1', 'mode'), ('4294967296', '1', 'mode'),
                             ('0', '0', 'mode'), ('0', '4294967294', 'mode'),
                             ('0', '1x', 'mode'), ('0', '1', 'set-online')]:
        result = subprocess.run([str(command), node, port, kind], timeout=5)
        assert result.returncode == 2, (node, port, kind, result)

print('PASS: mode/NAS/UIM/WDS/revision/audio decode, privacy, malformed replies and CLI boundaries')
