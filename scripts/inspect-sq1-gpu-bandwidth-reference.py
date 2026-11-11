#!/usr/bin/env python3
"""Inspect pinned SQ1 graphics power tables offline; never emit target DT votes.

ACPICA evaluates the original AML with a read-only query wrapper. The Windows
instruction checks concern a bandwidth query, not a proven GMU voting path.
Expanded firmware, logs and driver bytes belong in ignored out/ directories.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

DSDT_SHA = '454e7cf5c4b1e9a7f8dd2ff428959ce4b1990948030cf6e5111670cb460f0639'
DRIVER_SHA = '14369cb4acec038f94e76bc42cb65db6bc63031c18fc0d632d7d77e1c5b2b112'
QUERY = r'''DefinitionBlock ("", "SSDT", 2, "SPX", "BWQUERY", 1)
{
    External (\_SB.GPU0.PMCL, MethodObj)
    Method (BWQR, 0, Serialized)
    {
        Local0 = \_SB.GPU0.PMCL ()
        Local1 = DerefOf (Local0 [4])
        Local2 = Package (2) { Zero, Zero }
        Local2 [0] = DerefOf (Local1 [12])
        Local2 [1] = DerefOf (Local1 [13])
        Return (Local2)
    }
}
'''


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def check_acpi(data):
    require(len(data) >= 36 and data[:4] == b'DSDT', 'Expected DSDT')
    require(struct.unpack_from('<I', data, 4)[0] == len(data), 'DSDT length mismatch')
    require(sum(data) % 256 == 0, 'DSDT checksum mismatch')


def strip_comments(text):
    return re.sub(r'"(?:\\.|[^"\\])*"|/\*.*?\*/|//[^\n]*',
                  lambda m: m[0] if m[0].startswith('"') else ' ', text, flags=re.S)


def block(text, marker):
    matches = list(re.finditer(marker, text))
    require(len(matches) == 1, 'Expected one block: ' + marker)
    start = text.index('{', matches[0].end())
    depth = 0
    for m in re.finditer(r'"(?:\\.|[^"\\])*"|[{}]', text[start:]):
        if m[0] == '{':
            depth += 1
        elif m[0] == '}':
            depth -= 1
            if depth == 0:
                return text[start:start + m.end()]
    raise ValueError('Unclosed block')


def parse_package(text):
    text = strip_comments(text)
    token = re.compile(r'Package|Zero|Ones?\b|0x[0-9A-Fa-f]+|[0-9]+|"(?:\\.|[^"\\])*"|[(){},]')
    tokens, end = [], 0
    for m in token.finditer(text):
        require(not text[end:m.start()].strip(), 'Unsupported package syntax')
        tokens.append(m[0])
        end = m.end()
    require(not text[end:].strip(), 'Trailing package syntax')
    pos = 0

    def take(expected=None):
        nonlocal pos
        require(pos < len(tokens), 'Truncated package')
        value = tokens[pos]
        pos += 1
        require(expected is None or value == expected, 'Unexpected package token')
        return value

    def value(depth=0):
        require(depth <= 32, 'Package nesting limit')
        t = take()
        if t == 'Package':
            take('(')
            count = value(depth + 1)
            require(isinstance(count, int) and 0 <= count <= 65535, 'Invalid package count')
            take(')')
            take('{')
            items = []
            for i in range(count):
                if i:
                    take(',')
                items.append(value(depth + 1))
            if pos < len(tokens) and tokens[pos] == ',':
                take(',')
            take('}')
            return items
        if t.startswith('"'):
            return json.loads(t)
        if t in ('Zero', 'One', 'Ones'):
            return {'Zero': 0, 'One': 1, 'Ones': (1 << 64) - 1}[t]
        require(re.fullmatch(r'0x[0-9a-fA-F]+|[0-9]+', t), 'Expected constant')
        n = int(t, 16 if t.startswith('0x') else 10)
        require(0 <= n < 1 << 64, 'ACPI integer overflow')
        return n

    result = value()
    require(isinstance(result, list) and pos == len(tokens), 'Trailing package tokens')
    return result


def source_tables(dsl):
    text = strip_comments(dsl)
    gpu = block(text, r'\bDevice\s*\(GPU0\)')
    pmcl = block(gpu, r'\bMethod\s*\(PMCL,\s*0,\s*NotSerialized\)')
    tables = {}
    for number in range(1, 8):
        name = f'P{number:03d}'
        matches = list(re.finditer(r'\bName\s*\(' + name + r',\s*(Package\s*\([^)]*\))', pmcl))
        require(len(matches) == 1, 'Missing/duplicate table ' + name)
        literal = matches[0][1] + block(pmcl, r'\bName\s*\(' + name + ',')
        table = parse_package(literal)
        expected = 'GRAPHICS_FREQ_CONTROL' if number <= 5 else 'GRAPHICS_BW_CONTROL'
        require(table[:1] == ['ENGINE_PSTATE_SET'] and table[2] == expected, 'Wrong engine table')
        require(table[4] == len(table) - 9, 'PSTATE count mismatch')
        for i, row in enumerate(table[9:]):
            require(row[:2] == ['PSTATE', i], 'PSTATE index mismatch')
            require(len(row) == (6 if number <= 5 else 5), 'PSTATE shape mismatch')
        tables[name] = table
    return tables


def parse_evaluation(log):
    marker = 'Evaluation of \\BWQR returned object '
    require(log.count(marker) == 1, 'Missing/ambiguous successful BWQR evaluation')
    body = log.split(marker, 1)[1].splitlines()[1:]
    lines = []
    for line in body:
        if not line.strip():
            break
        lines.append(line)
    pos = 0

    def read(indent):
        nonlocal pos
        require(pos < len(lines), 'Truncated evaluated object')
        line = lines[pos]
        pos += 1
        require(len(line) - len(line.lstrip()) == indent, 'Invalid object indentation')
        line = line.strip()
        m = re.fullmatch(r'\[Package\] Contains (\d+) Elements:', line)
        if m:
            count = int(m[1])
            require(count <= 65535 and indent <= 66, 'Evaluated package limit')
            return [read(indent + 2) for _ in range(count)]
        m = re.fullmatch(r'\[Integer\] = ([0-9A-Fa-f]{16})', line)
        if m:
            return int(m[1], 16)
        m = re.fullmatch(r'\[String\] Length ([0-9A-Fa-f]+) = (".*")', line)
        require(m is not None, 'Unsupported evaluated object')
        value = json.loads(m[2])
        # ACPICA reports the string bytes, excluding the terminating NUL.
        require(int(m[1], 16) == len(value.encode('ascii')), 'String length mismatch')
        return value

    result = read(2)
    require(pos == len(lines) and isinstance(result, list) and len(result) == 2,
            'Unexpected evaluated root/trailing object')
    return result


def driver_evidence(data):
    require(digest(data) == DRIVER_SHA, 'Unrecognized driver hash')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    require(data[:2] == b'MZ' and data[pe:pe + 4] == b'PE\0\0', 'Invalid PE')
    machine, count = struct.unpack_from('<HH', data, pe + 4)
    optional = struct.unpack_from('<H', data, pe + 20)[0]
    require(machine == 0xaa64 and 0 < count <= 32, 'Expected ARM64 PE')
    require(struct.unpack_from('<H', data, pe + 24)[0] == 0x20b, 'Expected PE32+')
    base = struct.unpack_from('<Q', data, pe + 48)[0]
    sections = []
    for i in range(count):
        off = pe + 24 + optional + 40 * i
        _, rva, size, raw = struct.unpack_from('<4I', data, off + 8)
        require(raw + size <= len(data), 'PE section outside file')
        sections.append((base + rva, raw, size))

    def checked(va, words):
        for start, raw, size in sections:
            if start <= va and va + 4 * len(words) <= start + size:
                actual = struct.unpack_from('<' + 'I' * len(words), data, raw + va - start)
                require(list(actual) == words, 'Instruction mismatch at ' + hex(va))
                return {'va': hex(va), 'words': [f'{w:08x}' for w in words]}
        raise ValueError('Unbacked instruction address')

    return {
        'query_selects_graphics_bw_control': checked(0x140283ea0, [0xf00005fb, 0xd2801c18,
            0xd3407e68, 0x9b182909, 0x91002134, 0xb9407688, 0x350000e8, 0xd2800502,
            0x912c8361, 0xaa1403e0, 0x97fab50a]),
        'pstate_raw_value_to_entry_plus_8': checked(0x140280914, [0x91002281, 0xaa1503e0, 0x97f6085d]),
        'integer_reader_identity_u64': checked(0x140002ae0, [0xf8404128, 0x14000006, 0xb9400528,
            0x14000004, 0x79400928, 0x14000002, 0x39401128, 0xf9000028]),
        'query_max_formula': checked(0x140283f1c, [0xd288d88c, 0x34000137, 0xf9406a89,
            0xb86c6ac8, 0xf940052a, 0xd34e490b, 0x9b0a7d68, 0xd37ff90b, 0xf900030b]),
        'query_current_formula': checked(0x140283f44, [0xb9403a89, 0xd2800e0a, 0xf9406a88,
            0x9b0a212b, 0xb86c6ac9, 0xf9400568, 0xd34e492a, 0x9b0a7d0a, 0xd37ff949, 0xf9000309]),
        'initial_configuration': checked(0x140293b34, [0xd288d88a, 0x52880209, 0xb82a6a89]),
        'configuration_update': checked(0x14029ee78, [0xb8796a68, 0x4a092d09, 0x2a0803ea,
            0x12151d2b, 0x4a0a0168, 0xb8396a68]),
        'separate_vote_diagnostic_divisor_1000000': checked(0x1400c0fc0, [1000000, 0]),
        'interpretation': 'Query output = raw PSTATE value * ((device_u32_at_0x46c4 >> 14) & 31) * 2. '
            'Initial field is 0x4010; another path updates bits 11..18. The runtime factor and its '
            'physical meaning are not established. AB/IB diagnostic divisor belongs to another data path.',
    }


def inspect(dsdt, driver, output):
    data = dsdt.read_bytes()
    require(digest(data) == DSDT_SHA, 'Unrecognized DSDT hash')
    check_acpi(data)
    instructions = driver_evidence(driver.read_bytes())
    output.mkdir(parents=True, exist_ok=False)
    (output / 'input.dat').write_bytes(data)
    (output / 'query.asl').write_text(QUERY)

    def run(args, name):
        result = subprocess.run(args, cwd=output, capture_output=True, text=True, timeout=30)
        log = result.stdout + result.stderr
        (output / name).write_text(log)
        require(result.returncode == 0, name + ' failed')
        return log

    run(['iasl', '-d', 'input.dat'], 'decompile.log')
    dsl = (output / 'input.dsl').read_text()
    tables = source_tables(dsl)
    identity = {}
    for name in ['SOID', 'SIDV', 'SIDT']:
        matches = re.findall(r'Name\s*\(' + name + r',\s*(0x[0-9A-Fa-f]+)\)', dsl)
        require(len(matches) == 1, 'Missing/duplicate identity field ' + name)
        identity[name] = int(matches[0], 16)
    run(['iasl', 'query.asl'], 'compile.log')
    log = run(['acpiexec', '-di', '-b', 'evaluate BWQR', 'input.dat', 'query.aml'], 'evaluation.log')
    selected = parse_evaluation(log)
    names = []
    for table in selected:
        matches = [name for name, value in tables.items() if value == table]
        require(len(matches) == 1, 'Evaluated table did not match one source package')
        names.append(matches[0])
    require(names == ['P004', 'P007'], 'Unexpected selection from pinned SQ1 DSDT')
    diagnostics = [line for line in log.splitlines() if 'Error' in line]
    require(len(diagnostics) == 3 and all('[BAUD]' in line for line in diagnostics),
            'Unexpected ACPICA diagnostics; inspect evaluation log')
    result = {
        'status': 'offline-reference-not-target-calibration',
        'inputs': {'dsdt_sha256': DSDT_SHA, 'driver_sha256': DRIVER_SHA},
        'static_acpi_identity': identity,
        'selected_packages': names,
        'tables': tables,
        'selected_frequencies_hz': [row[2] for row in selected[0][9:]],
        'selected_bandwidth_raw': [row[2] for row in selected[1][9:]],
        'driver': instructions,
        'acpica_diagnostics': diagnostics,
        'limits': [
            'Original AML PMCL evaluated offline with STA/INI disabled, not on hardware. '
            'Pinned DSDT has unrelated BAUD buffer-field initialization errors, retained above.',
            'SQ1 archive selection only; no claim about current SQ2 or firmware revisions.',
            'Bandwidth units, runtime factor, GPU-frequency-to-bandwidth policy and CNOC ON/OFF votes remain unproved.',
            '9 frequency states and 11 bandwidth states must not be zipped into an OPP table.',
            'No DT, driver source, device, or deployment was changed by this tool.',
        ],
        'artifacts': {f.name: digest(f.read_bytes()) for f in output.iterdir() if f.is_file()},
        'script_sha256': digest(Path(__file__).read_bytes()),
    }
    (output / 'RESULT.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dsdt', type=Path, required=True)
    parser.add_argument('--driver', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = inspect(args.dsdt.resolve(), args.driver.resolve(), args.output.resolve())
    print(json.dumps({k: result[k] for k in ['status', 'selected_packages', 'selected_frequencies_hz',
                                            'selected_bandwidth_raw']}, indent=2))


if __name__ == '__main__':
    main()
