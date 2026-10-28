#!/usr/bin/env python3
"""Prepare a hash-pinned, private SQ2 CPU tiled-copy experiment; no device I/O.

Original v77 GPU rendering and exact shader sampling pass, but its CPU converter
chooses eight-channel macrotiles while captured SQ2 bytes decode exactly with
four-channel macrotiles at 640x360, 264x136 and 640x384. Change only the converter
initialization's local destination value; do not alter its source capability
word, GPU identity, hardware programming or readback thresholds. The default
keeps the original bank selection. Explicit --bank-bit 15 additionally changes
only the CPU bank-16 case's final assignment to 15: the 1024x128 and 2048x128
RGBA captures decode exactly with mode 4 / bank 15 / extra 0.

The converter object is separately allocated at 0x1693f4..0x16942c (464 bytes),
with vtable VA 0x3f2670. It is initialized by 0x2a5b38; observed slots +824/+832
resolve to CPU linear/tiled routines 0x2bca38/0x2be398. At 0x2a6100, W6 is used
only for the store to converter+312 at 0x2a6108, then redefined before later use.
This is a converter-wide CPU copy change, not merely one glReadPixels call; both
copy directions and other formats require device regression before promotion.
"""

import hashlib
import json
import argparse
from pathlib import Path
import re
import shutil
import struct

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "out/quest2-gpu/gles-linked"
OUTPUT = ROOT / "out/quest2-gpu/gles-linked-cpu-layout"
OUTPUT_BANK15 = ROOT / "out/quest2-gpu/gles-linked-cpu-layout-bank15"
LIB = "libGLESv2_adreno.so"
VA = 0x2A6100
BANK_VA = 0x2A61B0
EXPECTED_OUTPUTS = {
    16: "14dca59473e08dc2e797a7eb9d77ee623b28fe1c2592da654ac1bd3e821b51d5",
    15: "71eba2e1d10daaf842f8667654af711c63582dc9be3c42df14d6d4191faac668",
}
EXPECTED_DONORS = {
    "libEGL_adreno.so": "026b25fe82d174fe9e4a327d4e6f9b05be800a59853b5cb729cae53a6b2892fc",
    "libGLESv1_CM_adreno.so": "396987f605b4f7aa1be5d10cacf0216bce400a3c13bb36f9cf29fac1d9bc5ace",
    LIB: "99d4c18d35e69e8589f2550b242d5f6722031404852b9831e328df8d1778551f",
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


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def manifest(name, expected):
    result = {}
    path = SOURCE / name
    require(path.is_file() and not path.is_symlink(), "Invalid manifest " + name)
    for line in path.read_text().splitlines():
        items = line.split()
        require(len(items) == 2, "Invalid manifest line")
        digest, filename = items
        require(bool(re.fullmatch(r"[0-9a-f]{64}", digest)), "Invalid digest")
        require(bool(re.fullmatch(r"[A-Za-z0-9_.-]+", filename)) and
                filename not in (".", "..") and filename not in result, "Invalid filename")
        require((SOURCE / filename).is_file() and not (SOURCE / filename).is_symlink(),
                "Invalid source file " + filename)
        require(sha(SOURCE / filename) == digest, "Source hash mismatch " + filename)
        result[filename] = digest
    require(result == expected, "Inputs differ from audited snapshot: " + name)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bank-bit", type=int, choices=(15, 16), default=16,
                        help="16 preserves bank selection (default); 15 changes only the audited CPU bank-16 case")
    args = parser.parse_args()
    output = OUTPUT_BANK15 if args.bank_bit == 15 else OUTPUT
    expected_output = EXPECTED_OUTPUTS[args.bank_bit]
    require(not output.exists() and not output.is_symlink(), "Refusing to overwrite an existing candidate")
    donors = manifest("SHA256SUMS", EXPECTED_DONORS)
    probes = manifest("PROBE_SHA256SUMS", EXPECTED_PROBES)
    original = (SOURCE / LIB).read_bytes()
    data = bytearray(original)
    require(data[:6] == b"\x7fELF\x02\x01" and struct.unpack_from("<H", data, 18)[0] == 183,
            "Expected ELF64 little-endian AArch64")
    phoff = struct.unpack_from("<Q", data, 32)[0]
    phsize, phcount = struct.unpack_from("<HH", data, 54)
    require(phsize == 56 and 0 < phcount <= 64 and phoff + phsize * phcount <= len(data),
            "Unexpected program headers")
    def file_offset(va, length, executable=True):
        matches = []
        for i in range(phcount):
            kind, flags, off, addr, _, size, _, _ = struct.unpack_from("<IIQQQQQQ", data, phoff + i * phsize)
            if (kind == 1 and (not executable or flags & 1) and
                    addr <= va and va + length <= addr + size and off + size <= len(data)):
                matches.append(off + va - addr)
        require(len(matches) == 1, "Address must map to one file-backed LOAD: " + hex(va))
        return matches[0]

    offset = file_offset(VA - 16, 32) + 16
    before = (0x528001E4, 0xF9009A60, 0x7100015F, 0x1A9F07E7,
              0x530A3506, 0xB9013E64, 0xB9013A66, 0xB9014267)
    require(struct.unpack_from("<8I", data, offset - 16) == before,
            "Unexpected converter initialization instructions")
    # The initial bank value at 0x2a6104 is not final: capability+72 != 0
    # dispatches on (capability+76 & 127) through this four-entry jump table.
    # Captures show 1 / 16, selecting the bank-16 block at 0x2a61b0.
    # Patch its MOV rather than the initial/default store, a capability word,
    # or the shared successor (which also updates unrelated converter flags).
    bank_offset = file_offset(BANK_VA, 20)
    table_offset = file_offset(0x22B0C, 4, executable=False)
    require(data[table_offset:table_offset + 4] == bytes((0, 5, 10, 28)),
            "Unexpected CPU bank jump table")
    require(struct.unpack_from("<12I", data, file_offset(0x2A6110, 48)) == (
        0x34000305, 0xB9404E6C, 0x1200198A, 0x5100354A,
        0x71000D5F, 0x54000268, 0x90FFEBE1, 0x912C3021,
        0x1000008B, 0x386A682C, 0x8B0C096B, 0xD61F0160),
        "Unexpected CPU bank dispatch instructions")
    require(struct.unpack_from("<5I", data, bank_offset) == (
        0x5280020A, 0xB9013E6A, 0xF940B66B, 0x366FFDEB, 0x2A1F03EA),
        "Unexpected final CPU bank assignment/successors")
    require(struct.unpack_from("<I", data, file_offset(0x2A6178, 4))[0] == 0x5280002A,
            "Unexpected W10 redefinition on taken successor")
    # UBFX W6,W8,#10,#4 -> MOV W6,#4. No flags changed. Source W8 untouched.
    struct.pack_into("<I", data, offset, 0x52800086)
    allowed_changed = list(range(offset, offset + 4))
    if args.bank_bit == 15:
        # MOV W10,#16 -> MOV W10,#15. The next instruction stores +316.
        # Both successors redefine W10 (to 1 at 0x2a6178 or 0 at 0x2a61c0)
        # before any further use, and there is no later bank-field assignment
        # in this hash-pinned initializer. MOV does not change NZCV.
        struct.pack_into("<I", data, bank_offset, 0x528001EA)
        allowed_changed += [bank_offset, bank_offset + 1]
    changed = [i for i, (a, b) in enumerate(zip(original, data)) if a != b]
    require(changed == allowed_changed, "Unexpected changed bytes")
    require(hashlib.sha256(data).hexdigest() == expected_output, "Unexpected output digest")

    output.mkdir()  # Exclusive creation after all source validation.
    for name in donors | probes:
        shutil.copyfile(SOURCE / name, output / name)
        require(sha(output / name) == (donors | probes)[name], "Copy mismatch " + name)
    # Retain the replaced library and exact original manifests below a directory
    # so runtime '*.so' globs never load the reference copy.
    reference = output / "reference"
    reference.mkdir()
    for name in (LIB, "SHA256SUMS", "PROBE_SHA256SUMS"):
        shutil.copyfile(SOURCE / name, reference / name)
    (output / LIB).write_bytes(data)
    (output / "gles-readback").chmod(0o755)
    for filename, names in (("SHA256SUMS", donors), ("PROBE_SHA256SUMS", probes)):
        (output / filename).write_text("".join(f"{sha(output / n)}  {n}\n" for n in names))
    for name, digest in (donors | probes).items():
        require(sha(SOURCE / name) == digest, "Source changed during staging " + name)
        require(sha(output / name) == (expected_output if name == LIB else digest),
                "Unexpected staged change " + name)
    require(sha(reference / LIB) == EXPECTED_DONORS[LIB], "Reference copy mismatch")
    report = {
        "reference_only": True, "system_deployed": False, "device_tested": False,
        "target": "SQ2 private experiment only", "input_sha256": EXPECTED_DONORS[LIB],
        "output_sha256": expected_output, "instruction_va": hex(VA), "file_offset": hex(offset),
        "before": "ubfx w6,w8,#10,#4", "before_hex_le": original[offset:offset + 4].hex(),
        "after": "mov w6,#4", "after_hex_le": data[offset:offset + 4].hex(),
        "changed_byte_offsets": [hex(i) for i in changed],
        "destination": "CPU copy converter uint32 +312",
        "bank_bit_option": args.bank_bit,
        "unchanged": ["source capability word +80", "GPU identity and programming",
                      "source capability words +72/+76", "extra bank field +320", "readback thresholds",
                      "all other library instructions and all other files"],
        "source_hashes": donors | probes,
        "evidence": "Three exact RGBA source captures decode with four-channel mode; original scalar address function independently checked under QEMU",
        "limits": "CPU converter-wide: scalar/NEON, upload/readback and other pixel formats need regression. Not a default/system driver.",
    }
    if args.bank_bit == 15:
        report["bank_assignment"] = {
            "instruction_va": hex(BANK_VA), "file_offset": hex(bank_offset),
            "before": "mov w10,#16", "before_hex_le": original[bank_offset:bank_offset + 4].hex(),
            "after": "mov w10,#15", "after_hex_le": data[bank_offset:bank_offset + 4].hex(),
            "destination": "CPU copy converter uint32 +316 via str w10,[x19,#316] at 0x2a61b4",
            "scope": "Only original selector-16 case; selector-13/14/15 cases unchanged",
            "observed_capability_72": 1, "observed_capability_76_low7": 16,
            "jump_table_va": "0x22b0c", "jump_table_hex": "00050a1c",
            "w10_next_definitions": ["0x2a6178: mov w10,#1", "0x2a61c0: mov w10,wzr"],
            "control_flow_audit": "Final bank store for selector 16; no later +316 store in initializer through ret at 0x2a72d0",
        }
        report["evidence"] += "; 1024x128/pitch4096 and 2048x128/pitch8192 captures pass only mode4/bank15/extra0 among bank13..16/extra0..1"
    else:
        report["unchanged"].append("bank field +316")
    (output / "cpu-layout.json").write_text(json.dumps(report, indent=2) + "\n")
    print("QUEST2_CPU_LAYOUT_STAGED bank_bit=" + str(args.bank_bit) + " sha256=" + expected_output)
    print(output)


if __name__ == "__main__":
    main()
