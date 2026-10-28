#!/usr/bin/env python3
"""Stage a private, hash-pinned v77 PBO empty-extent validation correction.

No device I/O. Requires the separately prepared CPU4/bank15/PBO-fallback
candidate, Linux AArch64 cross GCC, and qemu-aarch64. It preserves all existing
validation before and after the PBO bounds calculation. An actual instruction
test under QEMU must pass before any candidate is staged. No section is enlarged;
only the audited RX LOAD's file/memory extents grow into zero-filled padding.
"""

import argparse
import hashlib
import json
import lzma
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "out/quest2-gpu/gles-linked-cpu-layout-pbo"
OUTPUT = ROOT / "out/quest2-gpu/gles-linked-zero-area"
LIB = "libGLESv2_adreno.so"
INPUT_SHA = "677287b130ac3ac0dfd2c0a8bc1ac73b642466c64f67d39f662a2bb09d823ded"
OUTPUT_SHA = "c47968472a01e3d2a69dad87d3e000fe7914a0f4e0198c6c70e389ba43219476"
ENTRY, NORMAL, EMPTY, STUB = 0x1ED580, 0x1ED584, 0x1ED61C, 0x3EA2A0
STUB_SIZE, NEXT_PAGE = 20, 0x3EB000
EXPECTED_DONORS = {
    "libEGL_adreno.so": "026b25fe82d174fe9e4a327d4e6f9b05be800a59853b5cb729cae53a6b2892fc",
    "libGLESv1_CM_adreno.so": "396987f605b4f7aa1be5d10cacf0216bce400a3c13bb36f9cf29fac1d9bc5ace",
    LIB: INPUT_SHA,
    "eglSubDriverAndroid.so": "eaa30f3fa6ed9dcf79ad12c9c214aab9d11aebd2cf6d8b3388e8d0075ef17012",
    "libgsl.so": "1013c8e7d17c86a787275b98b9dd05151d024b6aeee2aa3411745cb8520feb57",
    "libadreno_utils.so": "dd85dbc6b3bb6cbc4ca7d79b84ae19dd953275f13f7b6cccdf12ac72242985c0",
    "libllvm-glnext.so": "14e5e1db811300f60c7916bf53d3ad33131a74b0e08495d15b0094c4812e6cac",
    "libllvm-qgl.so": "8b595cffb7336ab920782e40c47bbcd8a11760a317b61f98438358a110348975",
}
EXPECTED_PROBES = {
    "gles-readback": "52b4ca94ea2ed43a383ce9e64b0a22b78565d69f93a20bb9d62fbe4beffe7a34",
    "libqti-mapper3-bridge.so": "a1a0b1c720b47acc0536036f71fd9184ab504ab319f1e545ef7dce02bc593f97",
    "libquest2-ion-dmaheap.so": "c92989678b00ffb8562887ddb281c04b98eb0050edea8b081b4541bb9e1ec496",
    "libquest2-egl-wrapper.so": "68712421fe8b68de52d39381d678eac2abf4324328fc7d8127894b74cedab389",
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def sha(path):
    return digest(path.read_bytes())


def overlap(start, size, other, length):
    return size > 0 and length > 0 and start < other + length and other < start + size


def manifest(filename, expected):
    path = SOURCE / filename
    require(path.is_file() and not path.is_symlink(), "Invalid manifest " + filename)
    found = {}
    for line in path.read_text().splitlines():
        parts = line.split()
        require(len(parts) == 2, "Invalid manifest line")
        value, name = parts
        require(re.fullmatch(r"[0-9a-f]{64}", value), "Invalid hash")
        require(re.fullmatch(r"[A-Za-z0-9_.-]+", name) and name not in (".", "..")
                and name not in found, "Invalid manifest filename")
        src = SOURCE / name
        require(src.is_file() and not src.is_symlink() and sha(src) == value,
                "Source hash mismatch: " + name)
        found[name] = value
    require(found == expected, "Inputs differ from audited snapshot: " + filename)
    return found


def branch(pc, target, register=None):
    delta = target - pc
    require(delta % 4 == 0, "Unaligned branch")
    bits = 26 if register is None else 19
    imm = delta // 4
    require(-(1 << (bits - 1)) <= imm < (1 << (bits - 1)), "Branch out of range")
    if register is None:
        return 0x14000000 | (imm & ((1 << 26) - 1))
    require(0 <= register < 31, "Invalid CBZ register")
    return 0x34000000 | ((imm & ((1 << 19) - 1)) << 5) | register


def packed_relocations(blob):
    """Decode APS2 fields, following the documented Bionic group format.

    Format reference: android.googlesource.com/platform/bionic/+/refs/heads/main/
    linker/linker_reloc_iterators.h . This parser only audits relocation targets;
    it never applies relocations or invokes the donor library.
    """
    require(blob[:4] == b"APS2", "Unknown Android packed-relocation encoding")
    position = 4
    def signed_leb():
        nonlocal position
        value, shift = 0, 0
        while True:
            require(position < len(blob) and shift < 70, "Invalid packed-relocation LEB128")
            byte = blob[position]
            position += 1
            value |= (byte & 127) << shift
            shift += 7
            if not byte & 128:
                return value - (1 << shift) if byte & 64 else value
    total, offset = signed_leb(), signed_leb()
    require(0 <= total <= 1000000, "Invalid packed relocation count")
    result, info, addend = [], 0, 0
    while len(result) < total:
        count, flags = signed_leb(), signed_leb()
        require(0 < count <= total - len(result) and 0 <= flags <= 15,
                "Invalid packed relocation group")
        delta = signed_leb() if flags & 2 else None
        if flags & 1:
            info = signed_leb()
        if (flags & 12) == 12:
            addend += signed_leb()
        elif not flags & 8:
            addend = 0
        for _ in range(count):
            offset += delta if delta is not None else signed_leb()
            if not flags & 1:
                info = signed_leb()
            if (flags & 12) == 8:
                addend += signed_leb()
            require(0 <= offset < (1 << 64) and info >= 0, "Invalid packed relocation address")
            result.append((offset, info, addend))
    require(position == len(blob), "Trailing packed-relocation bytes")
    return result


def inspect_and_patch(original):
    require(digest(original) == INPUT_SHA, "Requires exact PBO fallback candidate")
    data = bytearray(original)
    require(data[:6] == b"\x7fELF\x02\x01", "Expected little-endian ELF64")
    require(struct.unpack_from("<HHI", data, 16) == (3, 183, 1), "Expected AArch64 ET_DYN")
    phoff, shoff = struct.unpack_from("<QQ", data, 32)
    ehsize, phsize, phcount, shsize, shcount, shstr = struct.unpack_from("<6H", data, 52)
    require((ehsize, phsize, phcount, shsize) == (64, 56, 12, 64), "Unexpected ELF tables")
    require(0 < shcount < 256 and shstr < shcount, "Unsupported section numbering")
    require(phoff + phsize * phcount <= len(data) and shoff + shsize * shcount <= len(data),
            "Truncated ELF tables")
    ph = [struct.unpack_from("<IIQQQQQQ", data, phoff + i * phsize) for i in range(phcount)]
    sh = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shsize) for i in range(shcount)]
    string_table = sh[shstr]
    names = data[string_table[4]:string_table[4] + string_table[5]]
    def section_name(section):
        start = section[0]
        require(start < len(names), "Section name outside string table")
        end = names.find(b"\0", start)
        require(end >= 0, "Unterminated section name")
        return bytes(names[start:end]).decode("ascii")

    rx = [i for i, p in enumerate(ph) if p == (1, 5, 0xE8000, 0xE8000, 0xE8000,
                                              0x3022A0, 0x3022A0, 0x1000)]
    require(len(rx) == 1, "RX LOAD differs from audited layout")
    rx_index = rx[0]
    require(STUB + STUB_SIZE <= NEXT_PAGE and (STUB - 1) // 4096 == (STUB + STUB_SIZE - 1) // 4096,
            "Extension crosses the existing final RX page")
    require(len(data) >= NEXT_PAGE and not any(data[STUB:NEXT_PAGE]), "RX tail padding is not zero")
    for i, p in enumerate(ph):
        kind, flags, off, va, _, filesz, memsz, _ = p
        require(off + filesz <= len(data), "Segment extends beyond file")
        require(not overlap(off, filesz, STUB, STUB_SIZE), "Stub overlaps a program-header file range")
        if kind == 1:
            require(not overlap(va, memsz, STUB, STUB_SIZE), "Stub overlaps an existing LOAD")
    for start, length in ((0, ehsize), (phoff, phsize * phcount), (shoff, shsize * shcount)):
        require(not overlap(start, length, STUB, STUB_SIZE), "Stub overlaps ELF tables")

    symbol_count = relocation_count = debug_symbol_count = 0
    section_ranges = []
    for s in sh:
        _, kind, flags, va, off, size, _, _, _, entsize = s
        name = section_name(s)
        if kind != 8:  # SHT_NOBITS has no file range.
            require(off + size <= len(data), "Section extends beyond file: " + name)
            require(not overlap(off, size, STUB, STUB_SIZE), "Stub overlaps section file bytes: " + name)
        if flags & 2:  # SHF_ALLOC
            require(not overlap(va, size, STUB, STUB_SIZE), "Stub overlaps ALLOC section: " + name)
            section_ranges.append({"name": name, "va": hex(va), "size": hex(size)})
        if kind in (2, 11):  # SYMTAB / DYNSYM
            require(entsize == 24 and size % 24 == 0, "Unexpected symbol table")
            for pos in range(off, off + size, 24):
                _, _, _, index, value, length = struct.unpack_from("<IBBHQQ", data, pos)
                symbol_count += 1
                require(index == 0 or not overlap(value, max(1, length), STUB, STUB_SIZE),
                        "Defined symbol covers stub")
        if kind in (4, 9):  # RELA / REL
            expected = 24 if kind == 4 else 16
            require(entsize == expected and size % expected == 0, "Unexpected relocation table")
            for pos in range(off, off + size, expected):
                target = struct.unpack_from("<Q", data, pos)[0]
                relocation_count += 1
                require(not overlap(target, 8, STUB, STUB_SIZE), "Relocation target covers stub")
                if kind == 4:
                    addend = struct.unpack_from("<q", data, pos + 16)[0]
                    require(not STUB <= addend < STUB + STUB_SIZE, "Relocation addend names stub")
        if kind in (19, 0x6FFFFF00):  # SHT_RELR / Android's original SHT_RELR
            require(entsize == 8 and size % 8 == 0, "Unexpected RELR table")
            base = None
            for pos in range(off, off + size, 8):
                entry = struct.unpack_from("<Q", data, pos)[0]
                if entry & 1:
                    require(base is not None, "RELR bitmap without base")
                    targets = [base + (bit - 1) * 8 for bit in range(1, 64) if entry & (1 << bit)]
                    base += 63 * 8
                else:
                    targets = [entry]
                    base = entry + 8
                for target in targets:
                    relocation_count += 1
                    require(not overlap(target, 8, STUB, STUB_SIZE), "RELR target covers stub")
        if kind in (0x60000001, 0x60000002):
            require(kind == 0x60000002 and name == ".rela.dyn", "Unexpected packed relocation table")
            for target, _, addend in packed_relocations(data[off:off + size]):
                relocation_count += 1
                require(not overlap(target, 8, STUB, STUB_SIZE), "Packed relocation target covers stub")
                require(not STUB <= addend < STUB + STUB_SIZE, "Packed relocation addend names stub")
        if name == ".gnu_debugdata":
            mini = lzma.decompress(data[off:off + size])
            require(mini[:6] == b"\x7fELF\x02\x01", "Unexpected mini debug ELF")
            mini_shoff = struct.unpack_from("<Q", mini, 40)[0]
            mini_shsize, mini_shcount = struct.unpack_from("<HH", mini, 58)
            require(mini_shsize == 64 and 0 < mini_shcount < 256 and
                    mini_shoff + mini_shsize * mini_shcount <= len(mini), "Invalid mini debug sections")
            for j in range(mini_shcount):
                ms = struct.unpack_from("<IIQQQQIIQQ", mini, mini_shoff + j * mini_shsize)
                if ms[1] not in (2, 11):
                    continue
                require(ms[9] == 24 and ms[5] % 24 == 0 and ms[4] + ms[5] <= len(mini),
                        "Invalid mini debug symbol table")
                for pos in range(ms[4], ms[4] + ms[5], 24):
                    _, _, _, index, value, length = struct.unpack_from("<IBBHQQ", mini, pos)
                    debug_symbol_count += 1
                    require(index == 0 or not overlap(value, max(1, length), STUB, STUB_SIZE),
                            "Mini debug symbol covers stub")
    plt = [s for s in sh if section_name(s) == ".plt"]
    require(len(plt) == 1 and plt[0][3] + plt[0][5] == STUB and plt[0][4] + plt[0][5] == STUB,
            "Expected .plt to end exactly before zero padding")
    dynamic = [p for p in ph if p[0] == 2]
    require(len(dynamic) == 1 and dynamic[0][5] % 16 == 0, "Unexpected DYNAMIC segment")
    tags = {}
    for pos in range(dynamic[0][2], dynamic[0][2] + dynamic[0][5], 16):
        tag, value = struct.unpack_from("<QQ", data, pos)
        if tag == 0:
            break
        tags.setdefault(tag, []).append(value)
    for tag, value in {0x60000011: 0x8F48, 0x60000012: 0x166,
                       0x6FFFE000: 0x90B0, 0x6FFFE001: 0x3E8, 0x6FFFE003: 8,
                       23: 0x9498, 2: 0xFD8, 6: 0x420038, 11: 24}.items():
        require(tags.get(tag) == [value], "Dynamic relocation/symbol tables differ from sections")

    expected_bounds = (0x51000523, 0xF9401B04, 0x1B1A7D89, 0x1B0D7C65,
                       0x8B050108, 0x8B09010A, 0xEB04015F, 0x54000409)
    require(struct.unpack_from("<8I", data, ENTRY) == expected_bounds, "Unexpected PBO bounds sequence")
    for va, expected in ((0x1ED560, (0xF94033E8, 0xB9404FED, 0x294827EC, 0x340000B7,
                                    0x2A1703F6, 0x9AD60906, 0x9B16A0C7, 0xB5000127)),
                         (EMPTY, (0x2A1F03FB, 0xF9400BF0, 0x35FFED3B, 0xF9419268)),
                         (0x2A6100, (0x52800086,)), (0x2A61B0, (0x528001EA,)),
                         (0x1BB758, (0x14000020,)), (0x1C2F9C, (0x2F00E5E0,))):
        require(struct.unpack_from("<" + "I" * len(expected), data, va) == expected,
                "Unexpected predecessor/continuation or preserved patch: " + hex(va))
    # CBZ has only +/-1 MiB reach; both conditionals target a local B trampoline.
    stub = (branch(STUB, STUB + 16, 12), branch(STUB + 4, STUB + 16, 9),
            0x51000523, branch(STUB + 12, NORMAL), branch(STUB + 16, EMPTY))
    jump = branch(ENTRY, STUB)
    struct.pack_into("<I", data, ENTRY, jump)
    struct.pack_into("<5I", data, STUB, *stub)
    header = phoff + rx_index * phsize
    struct.pack_into("<QQ", data, header + 32, 0x3022A0 + STUB_SIZE, 0x3022A0 + STUB_SIZE)
    allowed = set(range(ENTRY, ENTRY + 4)) | set(range(STUB, STUB + STUB_SIZE)) | set(range(header + 32, header + 48))
    changed = [i for i, (a, b) in enumerate(zip(original, data)) if a != b]
    require(len(original) == len(data) and set(changed) <= allowed, "Unexpected changed bytes")
    require(data[shoff:shoff + shsize * shcount] == original[shoff:shoff + shsize * shcount],
            "Section headers changed")
    require(digest(data) == OUTPUT_SHA, "Unexpected candidate digest")
    report = {
        "input_sha256": INPUT_SHA, "output_sha256": digest(data),
        "entry_va": hex(ENTRY), "entry_before_le": original[ENTRY:ENTRY + 4].hex(),
        "entry_after_le": data[ENTRY:ENTRY + 4].hex(), "stub_va": hex(STUB),
        "stub_size": STUB_SIZE, "stub_hex_le": data[STUB:STUB + STUB_SIZE].hex(),
        "normal_continuation": hex(NORMAL), "empty_continuation": hex(EMPTY),
        "rx_program_header_index": rx_index, "rx_old_size": hex(0x3022A0),
        "rx_new_size": hex(0x3022A0 + STUB_SIZE), "next_page": hex(NEXT_PAGE),
        "checked_zero_padding_bytes": NEXT_PAGE - STUB, "checked_symbols": symbol_count,
        "checked_debug_symbols": debug_symbol_count,
        "checked_relocation_targets": relocation_count, "alloc_sections": section_ranges,
        "changed_byte_offsets": [hex(i) for i in changed],
    }
    return data, report, jump, stub


def qemu_verify(directory, jump, stub):
    """Run the exact encoded instructions at their original relative distances."""
    gcc, qemu = shutil.which("aarch64-linux-gnu-gcc"), shutil.which("qemu-aarch64")
    require(gcc and qemu, "Requires AArch64 cross GCC and qemu-aarch64")
    source = r'''#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
struct state { uint64_t r[31], nzcv, route; };
extern void audit_run(const struct state *, struct state *);
int main(void) {
  const uint32_t cases[][2] = {{0,0},{0,1},{1,0},{1,1},{640,360},{264,136},
    {0,0x7fffffff},{0x7fffffff,0},{0x7fffffff,0x7fffffff},{1,0xffffffff},{0xffffffff,1}};
  for (unsigned c=0;c<sizeof(cases)/sizeof(cases[0]);c++) for(unsigned f=0;f<16;f++) {
    struct state in={0}, out={0};
    for (unsigned r=0;r<31;r++) in.r[r]=UINT64_C(0x1234000056780000)+(uint64_t)r*0x10001+c;
    in.r[12]=cases[c][0]; in.r[9]=cases[c][1]; in.nzcv=(uint64_t)f<<28;
    audit_run(&in,&out);
    if(fwrite(&in,sizeof(in),1,stdout)!=1 || fwrite(&out,sizeof(out),1,stdout)!=1) return 2;
  }
  return fflush(stdout)!=0;
}
'''
    save = "\n".join(f"  stp x{i},x{i+1},[sp,#{8*i}]" for i in range(0, 30, 2)) + "\n  str x30,[sp,#240]"
    capture = []
    for name, route in (("normal", 1), ("empty", 0)):
        capture.append(f"""capture_{name}:
  sub sp,sp,#272
{save}
  mrs x9,nzcv
  str x9,[sp,#248]
  mov x9,#{route}
  str x9,[sp,#256]
  ldr x0,[sp,#368]
  mov x1,sp
  mov x2,#264
  bl memcpy
  add sp,sp,#272
  ldp x19,x20,[sp,#16]
  ldp x21,x22,[sp,#32]
  ldp x23,x24,[sp,#48]
  ldp x25,x26,[sp,#64]
  ldp x27,x28,[sp,#80]
  ldp x29,x30,[sp],#112
  ret
""")
    loads = "\n".join(f"  ldp x{i},x{i+1},[x30,#{8*i}]" for i in range(0, 30, 2))
    assembly = f""".text
.p2align 2
.global audit_run
.type audit_run,%function
audit_run:
  stp x29,x30,[sp,#-112]!
  stp x19,x20,[sp,#16]
  stp x21,x22,[sp,#32]
  stp x23,x24,[sp,#48]
  stp x25,x26,[sp,#64]
  stp x27,x28,[sp,#80]
  str x1,[sp,#96]
  mov x30,x0
  ldr x9,[x30,#248]
  msr nzcv,x9
{loads}
  ldr x30,[x30,#240]
  b audited_entry
{''.join(capture)}
.org 0x400
audited_entry:
  .word 0x{jump:08x}
  b capture_normal
.org 0x{0x400 + EMPTY - ENTRY:x}
  b capture_empty
.org 0x{0x400 + STUB - ENTRY:x}
{''.join(f'  .word 0x{w:08x}' + chr(10) for w in stub)}
.section .note.GNU-stack,"",%progbits
"""
    (directory / "audit.c").write_text(source)
    (directory / "audit.S").write_text(assembly)
    (directory / "stub.bin").write_bytes(struct.pack("<5I", *stub))
    (directory / "entry.bin").write_bytes(struct.pack("<I", jump))
    binary = directory / "audit-arm64"
    command = [gcc, "-static", "-O2", "-Wall", "-Wextra", "-Werror", str(directory / "audit.c"),
               str(directory / "audit.S"), "-o", str(binary)]
    subprocess.run(command, check=True, capture_output=True, timeout=60)
    result = subprocess.run([qemu, str(binary)], check=True, capture_output=True, timeout=30)
    require(not result.stderr, "Unexpected QEMU diagnostics")
    record_size, cases = 264 * 2, 11 * 16
    require(len(result.stdout) == cases * record_size, "Unexpected QEMU record count")
    routes = {"empty": 0, "normal": 0}
    for case in range(cases):
        a = struct.unpack_from("<33Q", result.stdout, case * record_size)
        b = struct.unpack_from("<33Q", result.stdout, case * record_size + 264)
        expected = list(a)
        empty = (a[12] & 0xFFFFFFFF) == 0 or (a[9] & 0xFFFFFFFF) == 0
        expected[32] = 0 if empty else 1
        if not empty:
            expected[3] = ((a[9] & 0xFFFFFFFF) - 1) & 0xFFFFFFFF
        require(list(b) == expected, "QEMU register/flags/control-flow mismatch: " + str(case))
        routes["empty" if empty else "normal"] += 1
    (directory / "registers.bin").write_bytes(result.stdout)
    return {"cases": cases, "all_gpr_and_nzcv_checked": True, "routes": routes,
            "binary_sha256": sha(binary), "register_output_sha256": digest(result.stdout),
            "scope": "Actual branch/stub encodings only; not the full donor library or GPU execution"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--audit-only", action="store_true", help="Validate and run QEMU without staging a candidate")
    args = parser.parse_args()
    require(args.audit_only or (not OUTPUT.exists() and not OUTPUT.is_symlink()), "Refusing existing candidate")
    donors, probes = manifest("SHA256SUMS", EXPECTED_DONORS), manifest("PROBE_SHA256SUMS", EXPECTED_PROBES)
    original = (SOURCE / LIB).read_bytes()
    data, report, jump, stub = inspect_and_patch(original)
    with tempfile.TemporaryDirectory(prefix="quest2-zero-area-") as temporary:
        audit = Path(temporary)
        report["qemu"] = qemu_verify(audit, jump, stub)
        report.update({"device_tested": False, "system_deployed": False, "private_candidate": True,
                       "source_hashes": donors | probes,
                       "preserved": ["negative-size, format/type, mapped-buffer and offset alignment validation",
                                     "later framebuffer/sample validation", "all non-empty extent arithmetic",
                                     "CPU4/bank15/PBO fallback patches", "normal readback thresholds"],
                       "limits": "No API/device regression performed by this script. Keep the original 414 expectations."})
        if not args.audit_only:
            OUTPUT.mkdir()
            for name, value in (donors | probes).items():
                shutil.copyfile(SOURCE / name, OUTPUT / name)
                require(sha(OUTPUT / name) == value, "Copy mismatch " + name)
            reference = OUTPUT / "reference"
            reference.mkdir()
            for name in (LIB, "SHA256SUMS", "PROBE_SHA256SUMS", "pbo-copy.json", "cpu-layout.json"):
                require((SOURCE / name).is_file() and not (SOURCE / name).is_symlink(), "Invalid reference " + name)
                shutil.copyfile(SOURCE / name, reference / name)
            (OUTPUT / LIB).write_bytes(data)
            (OUTPUT / "gles-readback").chmod(0o755)
            for name, files in (("SHA256SUMS", donors), ("PROBE_SHA256SUMS", probes)):
                (OUTPUT / name).write_text("".join(f"{sha(OUTPUT / n)}  {n}\n" for n in files))
            shutil.copytree(audit, OUTPUT / "audit")
            (OUTPUT / "zero-area.json").write_text(json.dumps(report, indent=2) + "\n")
            for name, value in (donors | probes).items():
                require(sha(SOURCE / name) == value, "Source changed while staging " + name)
                require(sha(OUTPUT / name) == (report["output_sha256"] if name == LIB else value),
                        "Unexpected staged change " + name)
            require(sha(reference / LIB) == INPUT_SHA, "Reference mismatch")
    print(json.dumps(report, indent=2))
    if not args.audit_only:
        print("QUEST2_ZERO_AREA_STAGED " + str(OUTPUT))


if __name__ == "__main__":
    main()
