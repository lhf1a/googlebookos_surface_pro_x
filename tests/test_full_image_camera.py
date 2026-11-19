"""Exercise camera payload assembly and input integrity without a device."""
import gzip
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"scripts"))
import full_image as image
import full_image_camera as camera


class CameraAssembly(unittest.TestCase):
    def test_source_only_bundle_requires_real_module_rebuild(self):
        elf = bytearray(64); elf[:6] = b'\x7fELF\x02\x01'; elf[18:20] = (183).to_bytes(2,'little')
        files = {n:bytes(elf)+n.encode() for n in camera.payload_paths() if not n.endswith('.ko')}
        with tempfile.TemporaryDirectory() as td:
            target = Path(td)/'camera'
            camera.write_inputs(files,target,source_only=True)
            with self.assertRaisesRegex(ValueError,'require source kernel'):
                camera.load(target)
            self.assertEqual(camera.load(target,source_build=True),files)
            manifest=json.loads((target/'CAMERA.json').read_text())
            manifest['modules_require_source_build']=False
            (target/'CAMERA.json').write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError,'require source kernel'):
                camera.load(target,source_build=True)

    def test_real_archive_has_hooks_profiles_and_preserves_identity(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            runtime = root/"full-image-runtime"
            bodies = {
                "init": b"#!/bin/sh\n"+camera.BOOT+b":\nfi\n"+camera.STAGE+image.ANCHOR,
                "etc/shadow": b"identity fixture", "bin/dropbear": b"ssh fixture",
                "bin/start-ssh": b"start fixture",
            }
            for relative in camera.GRAPHICS:
                bodies[camera.PREFIX+"root/"+relative] = b"old graphics"
            checksum = camera.PREFIX+"SHA256SUMS"
            bodies[checksum] = b"".join((camera.sha(b"old graphics")+"  root/"+n+"\n").encode()
                                        for n in camera.GRAPHICS)
            for name, data in bodies.items():
                p = runtime/"files"/name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(data)
            (runtime/"records.json").write_text(json.dumps({
                n: image.new_record(0o100644) for n in bodies}))
            files = {name: ("new "+name).encode() for name in camera.payload_paths()}
            loader = image.module
            def load(name, filename):
                if filename == "build-touch-calibration.py":
                    return SimpleNamespace(build=lambda root: ({
                        "etc/sq2-touch-calibration/wizard.apk": (0o100644, b"wizard")}, {}))
                return loader(name, filename)
            output = root/"runtime.gz"
            with patch.object(image, "module", side_effect=load):
                image.assemble_runtime(root, output, graphics=None, camera=files)
            records, result = image.records_from_gzip(output)
            self.assertEqual(result["etc/shadow"], bodies["etc/shadow"])
            self.assertEqual(result["bin/dropbear"], bodies["bin/dropbear"])
            for name, target in camera.payload_paths().items():
                self.assertEqual(result[target], files[name])
            init = result["init"]
            self.assertLess(init.index(camera.BOOT_HOOK), init.index(camera.BOOT))
            self.assertLess(init.index(camera.STAGE), init.index(camera.STAGE_HOOK))
            self.assertLess(init.index(camera.STAGE_HOOK), init.index(image.ANCHOR))
            self.assertIn(b'cameraId="1"', result["etc/spx-camera/userspace/media_profiles_spx.xml"])
            self.assertNotIn(b"old graphics", result[checksum])
            before = dict(result)
            camera.install(records, result, files, image.new_record)
            self.assertEqual(result, before)
            self.assertFalse(any("camera-id.ko" in n or "observe.ko" in n for n in result))

    def test_source_build_rejects_wrong_kernel_before_creating_output(self):
        builder = image.module("camera_bundle_test", "build-spx-camera-bundle.py")
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            source = root/"source"
            source.mkdir()
            (source/"Makefile").write_text("VERSION = 6\nPATCHLEVEL = 18\nSUBLEVEL = 60\n")
            build = root/"kernel-build"
            (build/"include/config").mkdir(parents=True)
            (build/"include/config/kernel.release").write_text("6.18.60-sq2-kgsl-dev1\n")
            with self.assertRaisesRegex(ValueError, "Unsupported module ABI"):
                builder.build({"kernel_source": source, "kernel_build": build}, root/"output")
            self.assertFalse((root/"output").exists())

    def test_source_bundle_roundtrip_excludes_unrelated_payloads(self):
        elf = bytearray(64)
        elf[:6] = b"\x7fELF\x02\x01"
        elf[18:20] = (183).to_bytes(2, "little")
        files = {name: bytes(elf)+name.encode() for name in camera.payload_paths()}
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            magic = camera.policy()["kernel_release"]+" SMP preempt mod_unload aarch64"
            with patch.object(camera.subprocess, "check_output", return_value=magic):
                camera.write_inputs(files, root/"camera")
                self.assertEqual(camera.load(root/"camera"), files)
            with self.assertRaisesRegex(ValueError, "Incomplete camera input inventory"):
                camera.write_inputs(files | {"etc/shadow": b"identity fixture"}, root/"bad")
            self.assertFalse((root/"bad").exists())

    def test_rejects_corrupt_private_input_before_use(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = {"schema": 1, "kernel_release": camera.policy()["kernel_release"],
                        "files": {n: camera.sha(b"expected") for n in camera.payload_paths()}}
            (root/"CAMERA.json").write_text(json.dumps(manifest))
            name = next(iter(manifest["files"]))
            elf = bytearray(64)
            elf[:6] = b"\x7fELF\x02\x01"
            elf[18:20] = (183).to_bytes(2, "little")
            (root/name).write_bytes(elf)
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                camera.load(root)


if __name__ == "__main__":
    unittest.main()
