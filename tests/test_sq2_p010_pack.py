"""Renderer packaging must preserve unrelated runtime and reject mismatched inputs."""
import gzip
import importlib.util
from pathlib import Path
import tempfile
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("rendererpack", ROOT / "scripts/pack-sq2-p010-renderer.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class RendererPack(unittest.TestCase):
    def base(self):
        files = {
            "init": (0o100755, b"#!/bin/sh\nquest2-hidl-stage > /logs/hidl-stage.txt 2>&1 || fail hidl-stage\nmount --bind /newroot/debug_ramdisk/keystore2.rc /newroot/system/etc/init/keystore2.rc || fail keystore-debug-overlay\n"),
            "bin/quest2-hidl-stage": (0o100755, b"existing HIDL"),
            "etc/sq2-hidl/service": (0o100755, b"AVC-only"),
            "lib/modules/6.18.60-sq2-android1/venus-core.ko": (0o100644, b"paired kernel"),
            "bin/dropbear": (0o100755, b"original SSH"),
            "bin/start-ssh": (0o100755, b"original script"),
            "etc/shadow": (0o100600, b"fixture credential"),
        }
        raw = b"".join(m.pack.cpio.entry(n, b, mode) for n, (mode, b) in files.items())
        return gzip.compress(raw + m.pack.cpio.entry("TRAILER!!!", b"", 0)), files

    def test_only_renderer_and_init_change(self):
        base, before = self.base()
        output, changes = m.build(base, {"libstagefright.so": b"candidate", "libsq2-p010-renderer.so": b"adapter"})
        after = m.pack.cpio.entries(gzip.decompress(output))
        self.assertEqual(after, before | changes)
        self.assertTrue(output.startswith(base))
        self.assertTrue(output.endswith(m.pack.credential_tail(base)))
        self.assertIn(b"sq2-p010-renderer-stage", after["init"][1])
        with self.assertRaises(ValueError):
            m.build(output, {})

    def test_stage_rejects_wrong_system_before_mount(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            source, target, stage = root / "source", root / "target", root / "stage"
            source.mkdir()
            target.mkdir()
            (target / "libstagefright.so").write_bytes(b"unexpected system")
            baseline = (m.sha(b"expected system") + "  libstagefright.so\n").encode()
            (source / "BASELINE.sha256").write_bytes(baseline)
            (source / "SHA256SUMS").write_text(m.sha(baseline) + "  BASELINE.sha256\n")
            helper = (ROOT / "probe/test2/sq2-p010-renderer-stage").read_text()
            helper = helper.replace("$(cat /proc/cmdline)", "sq2.video.c2=experimental")
            helper = helper.replace("source=/etc/sq2-p010-renderer", "source=" + str(source))
            helper = helper.replace("target=/newroot/system/lib64", "target=" + str(target))
            helper = helper.replace("stage=/newroot/debug_ramdisk/sq2-p010-renderer", "stage=" + str(stage))
            result = subprocess.run(["sh"], input=helper, text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("libstagefright.so: FAILED", result.stdout)
            self.assertFalse(stage.exists())
            self.assertEqual((target / "libstagefright.so").read_bytes(), b"unexpected system")

    def test_reject_unverified_pair_and_path(self):
        with tempfile.TemporaryDirectory() as td:
            directory = Path(td)
            recipe = {"target_sha256": "0"*64, "deployment_sha256": {}}
            for name in ("libstagefright.so", "libsq2-p010-renderer.so"):
                (directory / name).write_bytes(name.encode())
                recipe["deployment_sha256"][name] = m.sha(name.encode())
            result = m.bundle(directory, recipe)
            self.assertIn(b"libstagefright.so", result["BASELINE.sha256"])
            (directory / "libsq2-p010-renderer.so").write_bytes(b"unhardened")
            with self.assertRaises(ValueError):
                m.bundle(directory, recipe)
        base, _ = self.base()
        with self.assertRaises(ValueError):
            m.build(base, {"../unexpected": b"bad"})


if __name__ == "__main__":
    unittest.main()
