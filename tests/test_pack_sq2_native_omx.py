"""Exercise the real append packer with small synthetic archives and ELF fixtures."""
import gzip
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("native_omx_pack", ROOT / "scripts/pack-sq2-native-omx.py")
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)
pack = native.pack
cpio = pack.load_module("pack-sq2-vulkan")


def member(files):
    data = b""
    for name, body, mode in files:
        entry = bytearray(cpio.entry(name, body, mode))
        # Non-default uid/gid/mtime catch metadata damage in unrelated members.
        for index, value in ((2, 2001), (3, 3002), (5, 123456)):
            entry[6 + index * 8:14 + index * 8] = f"{value:08x}".encode()
        data += entry
    return gzip.compress(data + cpio.entry("TRAILER!!!", b"", 0), mtime=0)


class NativeOmxPack(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.elfs = {}
        for role, symbols in (("service", native.SERVICE_EXPORTS), ("bridge", native.BRIDGE_EXPORTS)):
            path = Path(cls.directory.name) / (role + ".so")
            source = "\n".join("int " + name + "(void) { return 0; }" for name in sorted(symbols))
            subprocess.run(["gcc", "-x", "c", "-shared", "-fPIC", "-o", str(path), "-"], input=source, text=True, check=True)
            data = bytearray(path.read_bytes())
            # Structural ELF fixture only: tests never execute this synthetic ARM64-tagged binary.
            data[18:20] = (183).to_bytes(2, "little")
            cls.elfs[role] = bytes(data)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_v2_bridge_requires_matching_service_while_legacy_remains_valid(self):
        with tempfile.TemporaryDirectory() as temp:
            d=Path(temp);base,service,bridge,_,_=self.fixture(d)
            bridge.write_bytes(bridge.read_bytes()+native.LAYOUT_V2.encode()+b"\0")
            output=d/"mismatch"
            with self.assertRaisesRegex(ValueError, "Missing defined native API exports"):
                native.build(base,service,bridge,output,pack.file_hash(base))
            self.assertFalse(output.exists())
            symbols=native.SERVICE_EXPORTS|{native.LAYOUT_V2}
            source="\n".join("int "+name+"(void){return 0;}" for name in symbols)
            subprocess.run(["gcc","-x","c","-shared","-fPIC","-o",str(service),"-"],
                           input=source,text=True,check=True)
            data=bytearray(service.read_bytes());data[18:20]=(183).to_bytes(2,"little")
            service.write_bytes(data)
            report=native.build(base,service,bridge,d/"matched",pack.file_hash(base))
            self.assertIn(native.LAYOUT_V2,report["required_exports"][native.SERVICE])

    def test_dual_abi_bridge_accepts_original_service(self):
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            base, service, bridge, output, _ = self.fixture(d)
            bridge.write_bytes(bridge.read_bytes() + native.LAYOUT_V2.encode() + b"\0" +
                               b"quest2_omx_capture_layout_for_usage\0")
            report = native.build(base, service, bridge, output, pack.file_hash(base))
            self.assertNotIn(native.LAYOUT_V2, report["required_exports"][native.SERVICE])

    def fixture(self, temp):
        files = {native.SERVICE: b"old service", native.BRIDGE: b"old bridge",
                 native.PREFIX + "service.rc": b"old rc and environment", native.PREFIX + "lib64/unchanged.so": b"old unchanged library"}
        sums = "".join(pack.digest(data) + "  " + name.removeprefix(native.PREFIX) + "\n" for name, data in files.items()).encode()
        initial = member([(name, data, 0o100755 if name == native.SERVICE else 0o100640) for name, data in files.items()] +
                         [(native.SUMS, sums, 0o100644), ("init", b"original\n" + pack.HOOK + pack.NEW_HOOK, 0o100755),
                          ("kernel", b"unchanged kernel", 0o100600), ("bin/quest2-hidl-stage", b"same stage", 0o100755),
                          ("etc/sq2-qti-system/root/lib64/hw/mapper.quest2.so", b"same mapper", 0o100644)])
        private = member([("etc/shadow", b"synthetic credential sentinel", 0o100600),
                          ("bin/dropbear", b"synthetic SSH sentinel", 0o100755)])
        base = temp / "base.gz"
        base.write_bytes(initial + private)
        service, bridge = temp / "service", temp / "bridge"
        service.write_bytes(self.elfs["service"])
        bridge.write_bytes(self.elfs["bridge"])
        return base, service, bridge, temp / "candidate", private

    def test_exact_three_changes_metadata_and_credential_tail(self):
        with tempfile.TemporaryDirectory() as directory:
            base, service, bridge, output, private = self.fixture(Path(directory))
            original = base.read_bytes()
            before, _ = pack.scan(base)
            report = native.build(base, service, bridge, output, pack.digest(original))
            image = output / "initramfs.cpio.gz"
            after, bodies = pack.scan(image, {native.SERVICE, native.BRIDGE, native.SUMS})
            self.assertEqual(set(report["changed_effective_paths"]), {native.SERVICE, native.BRIDGE, native.SUMS})
            for name, record in before.items():
                if name not in report["changed_effective_paths"]:
                    self.assertEqual(after[name], record, name)
                else:
                    self.assertEqual(after[name][1][:6], record[1][:6], name)
            self.assertEqual(bodies[native.SERVICE], service.read_bytes())
            self.assertEqual(bodies[native.BRIDGE], bridge.read_bytes())
            self.assertEqual(pack.credential_tail(image.read_bytes()), private)
            self.assertTrue(image.read_bytes().endswith(private))
            self.assertTrue(image.read_bytes().startswith(original))
            self.assertEqual(base.read_bytes(), original)
            self.assertTrue(report["normal_boot_pending"])
            self.assertFalse(report["full_stack_complete"])
            self.assertFalse(report["deployed"])
            self.assertEqual(report["verified_hidl_checksum_entries"], 4)

    def test_bad_hash_and_corrupt_unrelated_manifest_entry_fail_before_output(self):
        with tempfile.TemporaryDirectory() as directory:
            base, service, bridge, output, _ = self.fixture(Path(directory))
            with self.assertRaisesRegex(ValueError, "Base artifact hash differs"):
                native.build(base, service, bridge, output, "0" * 64)
            self.assertFalse(output.exists())
            base.write_bytes(base.read_bytes() + member([(native.PREFIX + "lib64/unchanged.so", b"corrupt", 0o100640)]))
            with self.assertRaisesRegex(ValueError, "HIDL checksum mismatch"):
                native.build(base, service, bridge, output, pack.file_hash(base))
            self.assertFalse(output.exists())

    def test_checksum_paths_duplicates_and_missing_entries_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            base, _, _, _, _ = self.fixture(Path(directory))
            records, bodies = pack.scan(base, {native.SUMS})
            original = bodies[native.SUMS]
            for data in (original + original.splitlines(keepends=True)[0],
                         pack.digest(b"x").encode() + b"  ../outside\n",
                         pack.digest(b"x").encode() + b"  missing\n", original.splitlines(keepends=True)[0]):
                with self.subTest(data=data), self.assertRaises(ValueError):
                    native.checked_sums(data, records)

    def test_arm64_exports_and_stage_order_required(self):
        with tempfile.TemporaryDirectory() as directory:
            base, service, bridge, output, _ = self.fixture(Path(directory))
            service.write_bytes(self.elfs["bridge"])
            with self.assertRaisesRegex(ValueError, "Missing defined native API exports"):
                native.build(base, service, bridge, output, pack.file_hash(base))
            data = bytearray(self.elfs["service"])
            data[18:20] = (62).to_bytes(2, "little")
            service.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "Expected ARM64 ELF"):
                native.build(base, service, bridge, output, pack.file_hash(base))
            service.write_bytes(self.elfs["service"])
            base.write_bytes(base.read_bytes() + member([("init", pack.NEW_HOOK + pack.HOOK, 0o100755)]))
            with self.assertRaisesRegex(ValueError, "HIDL stage before QTI"):
                native.build(base, service, bridge, output, pack.file_hash(base))
            self.assertFalse(output.exists())

    def test_existing_output_is_never_replaced(self):
        with tempfile.TemporaryDirectory() as directory:
            base, service, bridge, output, _ = self.fixture(Path(directory))
            output.mkdir()
            (output / "sentinel").write_bytes(b"preserve")
            with self.assertRaisesRegex(ValueError, "fresh private output"):
                native.build(base, service, bridge, output, pack.file_hash(base))
            self.assertEqual((output / "sentinel").read_bytes(), b"preserve")


if __name__ == "__main__":
    unittest.main()
