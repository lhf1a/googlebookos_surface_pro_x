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
                image.assemble_runtime(root, output, camera=files)
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
