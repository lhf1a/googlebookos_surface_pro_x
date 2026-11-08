"""ELF preservation tests; run with the actual pinned private system library."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("patch_p010", ROOT / "scripts/patch-sq2-p010-stagefright.py")
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


def headers(data):
    phoff, shoff = struct.unpack_from("<QQ", data, 32)
    phsize, phcount, shsize, shcount, shstr = struct.unpack_from("<HHHHH", data, 54)
    ph = [struct.unpack_from("<IIQQQQQQ", data, phoff + i * phsize) for i in range(phcount)]
    sh = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shsize) for i in range(shcount)]
    strings = data[sh[shstr][4]:sh[shstr][4] + sh[shstr][5]]
    sections = {strings[s[0]:].split(b"\0", 1)[0].decode(): s for s in sh}
    return ph, sections


def section(data, table, name):
    s = table[name]
    return data[s[4]:s[4] + s[5]]


class P010ElfTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(os.environ.get("SQ2_STAGEFRIGHT_INPUT", str(
            ROOT / "out/quest2-video-618-20261107/all-codecs/libstagefright-current.so")))
        if not path.is_file():
            raise unittest.SkipTest("Actual private libstagefright fixture is unavailable")
        cls.original = path.read_bytes()
        cls.recipe = json.loads((ROOT / "configs/stagefright-p010-elf.json").read_text())
        cls.patched, cls.audit = patcher.patch(cls.original, cls.recipe)

    def test_reject_unknown_input(self):
        with self.assertRaisesRegex(ValueError, "Unrecognized"):
            patcher.patch(self.original + b"\0", self.recipe)

    def test_original_loads_and_code_addresses_preserved(self):
        old_ph, _ = headers(self.original)
        new_ph, _ = headers(self.patched)
        for p in old_ph:
            if p[0] == 1:
                self.assertIn(p, new_ph)
        expected = bytearray(self.original)
        expected[:64] = self.patched[:64]
        for e in self.audit["entries"]:
            start = e["file_offset"]
            expected[start:start + 12] = bytes.fromhex(e["patch_hex"])
        for p in old_ph:
            if p[0] == 1:
                start, end = p[2], p[2] + p[5]
                self.assertEqual(expected[start:end], self.patched[start:end])
        # e_phoff and e_phnum are the only changed ELF header fields.
        old_header = bytearray(self.original[:64])
        old_header[32:40] = self.patched[32:40]
        old_header[56:58] = self.patched[56:58]
        self.assertEqual(old_header, self.patched[:64])

    def test_lookup_tables_keep_original_indices_and_relocations(self):
        _, old = headers(self.original)
        _, new = headers(self.patched)
        for name in (".dynsym", ".dynstr", ".gnu.version", ".rela.plt"):
            before = section(self.original, old, name)
            after = section(self.patched, new, name)
            self.assertEqual(before, after[:len(before)], name)
        for name in (".gnu.hash", ".gnu.version_r", ".rela.dyn", ".relr.dyn",
                     ".plt", ".rodata", ".eh_frame", ".data", ".data.rel.ro"):
            self.assertEqual(old[name][3], new[name][3], name)
            self.assertEqual(section(self.original, old, name),
                             section(self.patched, new, name), name)
        self.assertEqual(new[".dynsym"][5] - old[".dynsym"][5], 3 * 24)
        self.assertEqual(new[".rela.plt"][5] - old[".rela.plt"][5], 3 * 24)

    def test_branch_slots_are_readonly_after_binding(self):
        ph, table = headers(self.patched)
        relro = next(p for p in ph if p[0] == 0x6474e552)
        self.assertFalse(any(p[0] == 1 and p[1] & 3 == 3 for p in ph))
        for entry in self.audit["entries"]:
            a, load, branch = struct.unpack_from("<III", self.patched, entry["file_offset"])
            imm = ((a >> 29) & 3) | (((a >> 5) & 0x7ffff) << 2)
            if imm & (1 << 20):
                imm -= 1 << 21
            slot = (entry["entry"] & ~4095) + imm * 4096 + ((load >> 10) & 4095) * 8
            self.assertEqual(slot, entry["slot"])
            self.assertEqual(branch, 0xd61f0200)
            self.assertTrue(relro[3] <= slot and slot + 8 <= relro[3] + relro[6])
        load = next(p for p in ph if p[0] == 1 and p[2] == self.audit["new_load"]["file_offset"])
        self.assertEqual(load[1], 4)

    def test_reject_non_relro_slots(self):
        data = bytearray(self.original)
        phoff = struct.unpack_from("<Q", data, 32)[0]
        ph, _ = headers(data)
        index = next(i for i, p in enumerate(ph) if p[0] == 0x6474e552)
        struct.pack_into("<Q", data, phoff + index * 56 + 40, 0)
        recipe = dict(self.recipe, target_sha256=hashlib.sha256(data).hexdigest())
        with self.assertRaisesRegex(ValueError, "RELRO"):
            patcher.patch(bytes(data), recipe)


if __name__ == "__main__":
    unittest.main()
