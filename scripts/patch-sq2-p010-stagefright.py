#!/usr/bin/env python3
"""Attach the source-built P010 renderer without relocating any original code/data.

Only the pinned libstagefright is accepted. New ELF lookup tables are appended in
a read-only LOAD; three function pointers live in existing zeroed RELRO padding.
The original Android packed relocations, RELR, exports and CFI code stay intact.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
EH = struct.Struct("<16sHHIQQQIHHHHHH")
PH = struct.Struct("<IIQQQQQQ")
SH = struct.Struct("<IIQQQQIIQQ")
DYN = struct.Struct("<qQ")
RELA = struct.Struct("<QQq")
SYMBOL = struct.Struct("<IBBHQQ")


def align(value, alignment):
    return (value + alignment - 1) & -alignment


def patch(original, recipe):
    if hashlib.sha256(original).hexdigest() != recipe["target_sha256"]:
        raise ValueError("Unrecognized libstagefright; ABI audit is required")
    eh = list(EH.unpack_from(original))
    if eh[0][:6] != b"\x7fELF\x02\x01" or eh[1:3] != [3, 183]:
        raise ValueError("Expected ARM64 little-endian shared ELF")
    if eh[9] != PH.size or eh[11] != SH.size:
        raise ValueError("Unexpected ELF header sizes")
    phdrs = [list(PH.unpack_from(original, eh[5] + i * PH.size)) for i in range(eh[10])]
    shdrs = [list(SH.unpack_from(original, eh[6] + i * SH.size)) for i in range(eh[12])]
    strings = shdrs[eh[13]]
    names = original[strings[4]:strings[4] + strings[5]]
    sections = {names[s[0]:].split(b"\0", 1)[0].decode(): s for s in shdrs}

    def raw(name):
        sh = sections[name]
        return original[sh[4]:sh[4] + sh[5]]

    dyn = [list(DYN.unpack_from(raw(".dynamic"), i))
           for i in range(0, len(raw(".dynamic")), DYN.size)]
    dyn = dyn[:next(i for i, e in enumerate(dyn) if e[0] == 0)]
    tags = {tag: value for tag, value in dyn}
    if tags.get(2) != len(raw(".rela.plt")) or tags.get(20) != 7:
        raise ValueError("Expected complete RELA PLT table")
    if tags.get(23) != sections[".rela.plt"][3]:
        raise ValueError("Unexpected JMPREL address")
    if not (tags.get(30, 0) & 8 and tags.get(0x6ffffffb, 0) & 1):
        raise ValueError("BIND_NOW required before RELRO protection")
    padding = sections[".relro_padding"]
    relro = next(p for p in phdrs if p[0] == 0x6474e552)
    load = next(p for p in phdrs if p[0] == 1
                and p[3] <= padding[3] < p[3] + p[6])
    entries = recipe["entries"]
    if not (padding[1] == 8 and padding[3] % 8 == 0
            and padding[5] >= len(entries) * 8
            and padding[3] >= load[3] + load[5]
            and relro[3] <= padding[3]
            and padding[3] + len(entries) * 8 <= relro[3] + relro[6]):
        raise ValueError("Function pointers must be in zero-filled existing RELRO")
    symtab = bytearray(raw(".dynsym"))
    strtab = bytearray(raw(".dynstr"))
    versym = bytearray(raw(".gnu.version"))
    jmprel = bytearray(raw(".rela.plt"))
    if len(symtab) % SYMBOL.size or len(versym) != len(symtab) // SYMBOL.size * 2:
        raise ValueError("Symbol/version table mismatch")
    if recipe["library"].encode() + b"\0" in strtab:
        raise ValueError("Renderer dependency is already present")
    needed = len(strtab)
    strtab += recipe["library"].encode() + b"\0"
    output = bytearray(original)
    patches = []
    for i, entry in enumerate(entries):
        address = int(entry["entry"], 0)
        slot = padding[3] + i * 8
        seg = next(p for p in phdrs if p[0] == 1 and p[1] & 1
                   and p[3] <= address and address + 12 <= p[3] + p[5])
        offset = seg[2] + address - seg[3]
        if original[offset:offset + 12].hex() != entry["original_hex"]:
            raise ValueError("Unexpected entry instructions")
        page = ((slot & ~4095) - (address & ~4095)) >> 12
        if not -(1 << 20) <= page < (1 << 20):
            raise ValueError("ADRP target outside signed21 range")
        instructions = struct.pack("<III",
            0x90000010 | ((page & 3) << 29) | (((page >> 2) & 0x7ffff) << 5),
            0xf9400210 | (((slot & 4095) // 8) << 10), 0xd61f0200)
        output[offset:offset + 12] = instructions
        name_offset = len(strtab)
        strtab += entry["symbol"].encode() + b"\0"
        symbol_index = len(symtab) // SYMBOL.size
        symtab += SYMBOL.pack(name_offset, 0x12, 0, 0, 0, 0)  # global FUNC, undefined
        versym += struct.pack("<H", 1)
        jmprel += RELA.pack(slot, (symbol_index << 32) | 1026, 0)  # JUMP_SLOT
        patches.append(dict(entry=address, slot=slot, file_offset=offset,
                            symbol=entry["symbol"], patch_hex=instructions.hex()))

    # Relocate the PHDR table, not the existing LOADs: the old table has no slack.
    file_base = align(len(output), 0x4000)
    va_base = align(max(p[3] + p[6] for p in phdrs if p[0] == 1), 0x4000)
    phnum = len(phdrs) + 1
    payload = bytearray(phnum * PH.size)
    table_locations = {}

    def append(name, data):
        offset = align(len(payload), 8)
        payload.extend(b"\0" * (offset - len(payload)))
        payload.extend(data)
        table_locations[name] = (va_base + offset, file_base + offset, len(data))
        return va_base + offset

    replacement = {
        6: append(".dynsym", symtab),
        5: append(".dynstr", strtab),
        0x6ffffff0: append(".gnu.version", versym),
        23: append(".rela.plt", jmprel),
        10: len(strtab), 2: len(jmprel),
    }
    seen = set()
    for e in dyn:
        if e[0] in replacement:
            e[1] = replacement[e[0]]
            seen.add(e[0])
    if seen != replacement.keys():
        raise ValueError("Missing required dynamic tag")
    dyn += [[1, needed], [0, 0]]
    dynamic_va = append(".dynamic", b"".join(DYN.pack(*e) for e in dyn))
    for p in phdrs:
        if p[0] == 6:  # PHDR
            p[2:7] = [file_base, va_base, va_base, phnum * PH.size, phnum * PH.size]
        elif p[0] == 2:  # DYNAMIC
            p[1:7] = [4, table_locations[".dynamic"][1], dynamic_va, dynamic_va,
                      len(dyn) * DYN.size, len(dyn) * DYN.size]
    phdrs.append([1, 4, file_base, va_base, va_base, len(payload), len(payload), 0x4000])
    for i, p in enumerate(phdrs):
        PH.pack_into(payload, i * PH.size, *p)
    for name, (va, offset, size) in table_locations.items():
        sh = sections[name]
        sh[3:6] = [va, offset, size]
        if name == ".dynamic":
            sh[2] &= ~1  # new dynamic table is read-only
    for i, sh in enumerate(shdrs):
        SH.pack_into(output, eh[6] + i * SH.size, *sh)
    eh[5], eh[10] = file_base, phnum
    EH.pack_into(output, 0, *eh)
    output.extend(b"\0" * (file_base - len(output)))
    output.extend(payload)
    return bytes(output), dict(entries=patches, new_load=dict(
        file_offset=file_base, virtual_address=va_base, size=len(payload), flags="R"),
        retained_original_virtual_addresses=True, writable_executable_segments=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    recipe = json.loads((ROOT / "configs/stagefright-p010-elf.json").read_text())
    data, audit = patch(args.original.read_bytes(), recipe)
    with args.output.open("xb") as out:
        out.write(data)
    audit.update(input_sha256=recipe["target_sha256"],
                 output_sha256=hashlib.sha256(data).hexdigest(),
                 scope="candidate only; no automatic device installation")
    args.output.with_suffix(".audit.json").write_text(json.dumps(audit, indent=2) + "\n")
    print(json.dumps(audit, indent=2))


if __name__ == "__main__":
    main()
