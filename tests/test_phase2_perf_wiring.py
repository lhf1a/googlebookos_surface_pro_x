"""Verify Phase 2 Mesa perf, QTI SDM RGB overlay, and SF client cache wiring in full_image.py."""
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import full_image


class Phase2PerfWiringTest(unittest.TestCase):
    def test_hybrid_runtime_rewrites_sf_client_cache_prop(self):
        from test_full_image_hybrid import build_hybrid_fixture_tree

        records, bodies = build_hybrid_fixture_tree()
        prop_key = "etc/googlebook-security/build.prop"
        bodies[prop_key] += b"debug.sf.disable_client_composition_cache=1\n"

        report = full_image.hybrid_runtime(
            records,
            bodies,
            gralloc_adapter_bytes=b"adapter",
            layout_payload={
                "lib64/libgrallocutils.so": b"layout",
                "lib64/sq2-qti-display/libgrallocutils.so": b"layout",
                "lib64/libsq2_grallocutils_vendor.so": b"vendor",
            },
            backlight_payload={
                "lib64/sq2-qti-display/libsdmcore.so": b"sdmcore",
                "lib64/sq2-qti-display/libsq2_sdm_backlight.so": b"backlight",
            },
            vulkan_router_bytes=b"router",
            turnip_bytes=b"turnip",
            vulkan_policy_bytes=b"default=turnip\n",
        )
        props = bodies[prop_key].decode()
        self.assertIn("debug.sf.disable_client_composition_cache=0", props)
        self.assertNotIn("debug.sf.disable_client_composition_cache=1", props)
        self.assertFalse(report["mesa_perf_applied"])
        self.assertFalse(report["sdm_perf_applied"])

    def test_real_private_overlay_installs_phase2_mesa_sdm_and_cache_prop(self):
        runtime_dir = ROOT / "private/build-inputs/full-image-runtime/files"
        if not runtime_dir.is_dir():
            self.skipTest("private full-image-runtime not present")
        if not full_image.DEFAULT_MESA_PERF_DIR.is_dir() or not full_image.DEFAULT_SDM_PERF_PATH.is_file():
            self.skipTest("Phase 2 out/sq2-{mesa,sdm}-perf binaries not present")

        records, bodies = {}, {}
        for path in sorted(runtime_dir.rglob("*")):
            rel = path.relative_to(runtime_dir).as_posix()
            if path.is_dir():
                records[rel] = full_image.new_record(0o040755)
                bodies[rel] = b""
            else:
                records[rel] = full_image.new_record(0o100644)
                bodies[rel] = path.read_bytes()

        report = full_image.hybrid_runtime(records, bodies)
        self.assertTrue(report["mesa_perf_applied"])
        self.assertTrue(report["sdm_perf_applied"])

        # 1. Verify Mesa perf binaries and aligned libdrm.so installed
        self.assertEqual(
            bodies["etc/googlebook-security/lib64/egl/libEGL_freedreno.so"],
            (full_image.DEFAULT_MESA_PERF_DIR / "libEGL_freedreno.so").read_bytes(),
        )
        self.assertEqual(
            bodies["etc/googlebook-security/lib64/libgallium_dri.so"],
            (full_image.DEFAULT_MESA_PERF_DIR / "libgallium_dri.so").read_bytes(),
        )
        self.assertEqual(
            bodies["etc/googlebook-security/lib64/hw/vulkan.turnip.so"],
            (full_image.DEFAULT_MESA_PERF_DIR / "vulkan.turnip.so").read_bytes(),
        )
        self.assertEqual(
            bodies["etc/sq2-qti-system/root/lib64/sq2-qti-display/libdrm.so"],
            (full_image.DEFAULT_MESA_PERF_DIR / "libdrm.so").read_bytes(),
        )

        # 2. Verify patched libsdmcore.so still carries libsq2_sdm_backlight.so NEEDED
        sdm_bytes = bodies["etc/sq2-qti-system/root/lib64/sq2-qti-display/libsdmcore.so"]
        self.assertGreater(len(sdm_bytes), len(full_image.DEFAULT_SDM_PERF_PATH.read_bytes()))
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "libsdmcore.so"
            p.write_bytes(sdm_bytes)
            needed = subprocess.check_output(["patchelf", "--print-needed", str(p)], text=True).splitlines()
            self.assertIn("libsq2_sdm_backlight.so", needed)

        # 3. Verify SurfaceFlinger client composition cache enabled
        props = bodies["etc/googlebook-security/build.prop"].decode()
        self.assertIn("debug.sf.disable_client_composition_cache=0", props)
        self.assertNotIn("debug.sf.disable_client_composition_cache=1", props)

        # 4. Verify INSTALLED_SHA256SUMS matches every payload byte
        qti = "etc/sq2-qti-system"
        for line in bodies[f"{qti}/INSTALLED_SHA256SUMS"].decode().splitlines():
            digest, rel = line.split(None, 1)
            self.assertEqual(hashlib.sha256(bodies[f"{qti}/root/{rel}"]).hexdigest(), digest)


if __name__ == "__main__":
    unittest.main()
