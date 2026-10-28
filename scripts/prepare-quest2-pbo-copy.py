#!/usr/bin/env python3
"""Stage a private v77 PBO readback fallback experiment, with no device I/O.

Requires the exact SQ2 four-channel / bank-15 CPU converter candidate. The
original PBO direct-copy optimization rounds legal RGBA8 byte offset 259 down
to 256 in the observed output; all six failing precise-matrix byte counts match
that three-byte displacement. Its eligibility test checks pitch alignment but
does not check the byte offset. The final GPU address truncation instruction
has not been isolated, so this is a correctness workaround, not a root fix.

At VA 0x1bb758, replace CBZ W0,0x1bb7d8 with B 0x1bb7d8. The preceding
instruction already sends client-memory calls to that target. No PBO temporary
resource or reference has yet been acquired. Existing fallback maps the PBO,
adds the full byte offset at 0x1bc144 and unmaps it after the transfer.

This disables the PBO direct-copy optimization, preserving the original
fallback policy. With the unchanged UINT32_MAX/0 thresholds it uses the CPU
for the tested transfers. It may synchronize and block instead of using the
fast GPU path: this is NOT a performance improvement. Do not combine with the
zero-threshold experiment or deploy as the default without device regression.
"""

import hashlib
import json
from pathlib import Path
import re
import shutil
import struct

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "out/quest2-gpu/gles-linked-cpu-layout-bank15"
OUTPUT = ROOT / "out/quest2-gpu/gles-linked-cpu-layout-pbo"
LIB = "libGLESv2_adreno.so"
VA = 0x1BB758
TARGET = 0x1BB7D8
EXPECTED_OUTPUT = "677287b130ac3ac0dfd2c0a8bc1ac73b642466c64f67d39f662a2bb09d823ded"
EXPECTED_ORIGINAL = "99d4c18d35e69e8589f2550b242d5f6722031404852b9831e328df8d1778551f"
EXPECTED_LAYOUT_REPORT = "9d6c619f7f4fcb50d7b1b07e7afde9ad4217b35e45def73fa326553b09f68ccd"
EXPECTED_DONORS = {
    "libEGL_adreno.so": "026b25fe82d174fe9e4a327d4e6f9b05be800a59853b5cb729cae53a6b2892fc",
    "libGLESv1_CM_adreno.so": "396987f605b4f7aa1be5d10cacf0216bce400a3c13bb36f9cf29fac1d9bc5ace",
    LIB: "71eba2e1d10daaf842f8667654af711c63582dc9be3c42df14d6d4191faac668",
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
    require(path.is_file() and not path.is_symlink(), "Invalid input file: " + str(path))
    return hashlib.sha256(path.read_bytes()).hexdigest()


def manifest(directory, name, expected, check_files=True):
    result = {}
    path = directory / name
    sha(path)  # Reject missing files and symlinks.
    for line in path.read_text().splitlines():
        items = line.split()
        require(len(items) == 2, "Invalid manifest line")
        digest, filename = items
        require(bool(re.fullmatch(r"[0-9a-f]{64}", digest)), "Invalid manifest digest")
        require(bool(re.fullmatch(r"[A-Za-z0-9_.-]+", filename)) and
                filename not in (".", "..") and filename not in result, "Invalid manifest filename")
        if check_files:
            require(sha(directory / filename) == digest, "Hash mismatch: " + filename)
        result[filename] = digest
    require(result == expected, "Manifest differs from audited snapshot: " + str(path))
    return result


def text_offset(data, va, size):
    require(data[:6] == b"\x7fELF\x02\x01" and struct.unpack_from("<H", data, 18)[0] == 183,
            "Expected ELF64 little-endian AArch64")
    phoff = struct.unpack_from("<Q", data, 32)[0]
    phsize, phcount = struct.unpack_from("<HH", data, 54)
    require(phsize == 56 and 0 < phcount <= 64 and phoff + phsize * phcount <= len(data),
            "Unexpected program headers")
    matches = []
    for i in range(phcount):
        kind, flags, off, addr, _, length, _, _ = struct.unpack_from("<IIQQQQQQ", data, phoff + i * phsize)
        if kind == 1 and flags & 1 and addr <= va and va + size <= addr + length:
            require(off + length <= len(data), "Executable LOAD exceeds file")
            matches.append(off + va - addr)
    require(len(matches) == 1, "Instruction must map to exactly one executable LOAD")
    return matches[0]


def main():
    require(not OUTPUT.exists() and not OUTPUT.is_symlink(), "Refusing to overwrite an existing candidate")
    require(SOURCE.is_dir() and not SOURCE.is_symlink(), "Invalid source directory")
    donors = manifest(SOURCE, "SHA256SUMS", EXPECTED_DONORS)
    probes = manifest(SOURCE, "PROBE_SHA256SUMS", EXPECTED_PROBES)
    require(sha(SOURCE / "cpu-layout.json") == EXPECTED_LAYOUT_REPORT, "Unexpected CPU layout provenance")
    old_donors = dict(donors, **{LIB: EXPECTED_ORIGINAL})
    reference = SOURCE / "reference"
    require(reference.is_dir() and not reference.is_symlink(), "Invalid reference directory")
    manifest(reference, "SHA256SUMS", old_donors, check_files=False)
    manifest(reference, "PROBE_SHA256SUMS", probes, check_files=False)
    require(sha(reference / LIB) == EXPECTED_ORIGINAL, "Unexpected original linked reference")

    original = (SOURCE / LIB).read_bytes()
    data = bytearray(original)
    window = text_offset(data, VA - 24, 32)
    expected_window = bytes.fromhex("e11440b9514508941f000071ee1740f9fb079f1a2e0400b400040034e31b40f9")
    require(data[window:window + 32] == expected_window, "Unexpected PBO eligibility instructions")
    offset = text_offset(data, VA, 4)
    require(offset == window + 24, "Inconsistent instruction mapping")
    # Check preserved converter patches and the default threshold finalizer.
    for va, word in ((0x2A6100, 0x52800086), (0x2A61B0, 0x528001EA),
                     (0x1C2F9C, 0x2F00E5E0), (0x1C2FA0, 0x5284AF84), (0x1C2FA4, 0xFC246900)):
        require(struct.unpack_from("<I", data, text_offset(data, va, 4))[0] == word,
                "Unexpected preserved instruction at " + hex(va))
    branch = 0x14000000 | ((TARGET - VA) // 4)
    require(branch == 0x14000020, "Unexpected fallback branch encoding")
    struct.pack_into("<I", data, offset, branch)
    changed = [i for i, (a, b) in enumerate(zip(original, data)) if a != b]
    require(changed == [offset, offset + 1, offset + 3], "Unexpected changed bytes")
    require(data[:offset] == original[:offset] and data[offset + 4:] == original[offset + 4:],
            "Change escaped the single PBO eligibility instruction")
    require(hashlib.sha256(data).hexdigest() == EXPECTED_OUTPUT, "Unexpected candidate digest")

    provenance = [SOURCE / "SHA256SUMS", SOURCE / "PROBE_SHA256SUMS", SOURCE / "cpu-layout.json",
                  reference / LIB, reference / "SHA256SUMS", reference / "PROBE_SHA256SUMS"]
    provenance_hashes = {p: sha(p) for p in provenance}
    OUTPUT.mkdir()  # Exclusive, only after all input and patch checks.
    for name, digest in (donors | probes).items():
        shutil.copyfile(SOURCE / name, OUTPUT / name)
        require(sha(OUTPUT / name) == digest, "Copy mismatch: " + name)
    snapshot = OUTPUT / "reference" / "bank15"
    snapshot.mkdir(parents=True)
    original_snapshot = OUTPUT / "reference" / "original-linked"
    original_snapshot.mkdir()
    for name in (LIB, "SHA256SUMS", "PROBE_SHA256SUMS", "cpu-layout.json"):
        shutil.copyfile(SOURCE / name, snapshot / name)
        require(sha(snapshot / name) == sha(SOURCE / name), "Snapshot mismatch: " + name)
    for name in (LIB, "SHA256SUMS", "PROBE_SHA256SUMS"):
        shutil.copyfile(reference / name, original_snapshot / name)
        require(sha(original_snapshot / name) == sha(reference / name), "Original snapshot mismatch: " + name)
    shutil.copyfile(SOURCE / "cpu-layout.json", OUTPUT / "cpu-layout.json")
    (OUTPUT / LIB).write_bytes(data)
    (OUTPUT / "gles-readback").chmod(0o755)
    for filename, names in (("SHA256SUMS", donors), ("PROBE_SHA256SUMS", probes)):
        (OUTPUT / filename).write_text("".join(f"{sha(OUTPUT / n)}  {n}\n" for n in names))
    for name, digest in (donors | probes).items():
        require(sha(SOURCE / name) == digest, "Source changed during staging: " + name)
        require(sha(OUTPUT / name) == (EXPECTED_OUTPUT if name == LIB else digest),
                "Unexpected staged file change: " + name)
    for path, digest in provenance_hashes.items():
        require(sha(path) == digest, "Provenance changed during staging")
    report = {
        "reference_only": True, "system_deployed": False, "device_tested": False,
        "target": "SQ2 private PBO correctness experiment only",
        "input_sha256": donors[LIB], "output_sha256": EXPECTED_OUTPUT,
        "instruction_va": hex(VA), "file_offset": hex(offset), "branch_target_va": hex(TARGET),
        "before": "cbz w0,0x1bb7d8", "before_hex_le": "00040034",
        "after": "b 0x1bb7d8", "after_hex_le": "20000014",
        "changed_byte_offsets": [hex(i) for i in changed],
        "retained_cpu_layout": {"channel_mode": 4, "bank_bit": 15, "extra_bank": 0},
        "retained_readback_thresholds": {"byte_threshold": 4294967295, "percent_threshold": 0},
        "behavior": "Disable direct-to-PBO optimized copy; retain existing map/byte-offset/copy/unmap fallback",
        "performance": "CPU fallback can block and cost CPU time. This is not a performance repair.",
        "source_hashes": donors | probes,
        "unchanged": ["bank15 input directory", "client-memory readback path", "CPU4/bank15 instructions",
                      "default readback thresholds", "GPU identity and capabilities", "all other runtime files"],
        "limits": "Private untested candidate. Full PACK/PBO matrix, larger dimensions and YUV need device regression; final GPU truncation instruction not yet isolated.",
    }
    (OUTPUT / "pbo-copy.json").write_text(json.dumps(report, indent=2) + "\n")
    print("QUEST2_PBO_COPY_STAGED sha256=" + EXPECTED_OUTPUT)
    print(OUTPUT)


if __name__ == "__main__":
    main()
