"""Tests for drm/msm asynchronous legacy GEM close patch."""
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "scripts"
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

import full_image as fi

PATCH_DIR = ROOT / "patches/drm-618-msm-async-close"
PATCH_FILE = PATCH_DIR / "0001-drm-msm-async-gem-close.patch"
SOURCE_ARCHIVE = ROOT / "private/build-inputs/kernel-6.18/source/source.tar.gz"
EXTERNAL_TARBALL = ROOT / "source-dependencies/kernel-6.18/external-sources.tar.gz"


class DrmMsmAsyncClosePatchTests(unittest.TestCase):
    def test_patch_wired_in_full_image_and_touches_only_msm(self):
        self.assertEqual(fi.DEFAULT_MSM_ASYNC_CLOSE_PATCHES, PATCH_DIR)
        self.assertTrue(PATCH_FILE.is_file())
        fi_source = (ROOT / "scripts/full_image.py").read_text()
        self.assertIn("DEFAULT_MSM_ASYNC_CLOSE_PATCHES", fi_source)

        _, blocks = fi.split_git_diffs(PATCH_FILE.read_text())
        touched = [name for name, _ in blocks]
        self.assertEqual(
            touched,
            [
                "drivers/gpu/drm/msm/msm_drv.h",
                "drivers/gpu/drm/msm/msm_drv.c",
                "drivers/gpu/drm/msm/msm_gem.h",
                "drivers/gpu/drm/msm/msm_gem.c",
                "drivers/gpu/drm/msm/adreno/adreno_device.c",
            ],
        )
        for path in touched:
            self.assertTrue(path.startswith("drivers/gpu/drm/msm/"))

    def test_patch_applies_cleanly_in_full_series_on_kernel_and_edp618(self):
        if not SOURCE_ARCHIVE.is_file():
            self.skipTest("private kernel source archive not present")
        with tempfile.TemporaryDirectory(prefix="msm-async-close-test-") as td:
            td = Path(td)
            kernel = td / "kernel"
            external = td / "external"
            out = td / "out"
            out.mkdir()

            with tarfile.open(SOURCE_ARCHIVE) as tf:
                members = [
                    m for m in tf.getmembers()
                    if m.name.startswith("kernel/drivers/gpu/drm/msm/")
                    or m.name.startswith("external/edp-618/drivers/gpu/drm/msm/")
                ]
                tf.extractall(td, members=members, filter="data")

            def run(cmd, cwd=None):
                res = subprocess.run(
                    [str(c) for c in cmd],
                    cwd=cwd,
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(res.returncode, 0, f"cmd={cmd}\n{res.stdout}\n{res.stderr}")
                return res

            extra = (
                fi.DEFAULT_A6XX_MMIO_FIX_PATCHES,
                fi.DEFAULT_DPU_CRTC_KICKOFF_PATCHES,
                ROOT / "patches/drm-618-msm-submit",
                ROOT / "patches/drm-618-msm-vmbind-resv",
                fi.DEFAULT_MSM_ASYNC_CLOSE_PATCHES,
            )
            report = fi.apply_kernel_patches(
                fi.DEFAULT_A6XX_PATCHES,
                kernel,
                out,
                run,
                external_source=external,
                extra_patch_dirs=extra,
            )
            self.assertIn(str(PATCH_DIR), report["extra_directories"])

            # Verify kernel and external/edp-618 match byte-for-byte on all patched files
            for rel in (
                "drivers/gpu/drm/msm/msm_drv.h",
                "drivers/gpu/drm/msm/msm_drv.c",
                "drivers/gpu/drm/msm/msm_gem.h",
                "drivers/gpu/drm/msm/msm_gem.c",
                "drivers/gpu/drm/msm/adreno/adreno_device.c",
            ):
                k_txt = (kernel / rel).read_text()
                e_txt = (external / "edp-618" / rel).read_text()
                self.assertEqual(k_txt, e_txt, f"diverged: {rel}")

            gem_c = (kernel / "drivers/gpu/drm/msm/msm_gem.c").read_text()
            self.assertIn("dma_resv_test_signaled(obj->resv, DMA_RESV_USAGE_BOOKKEEP)", gem_c)
            self.assertIn("dma_resv_get_singleton(obj->resv, DMA_RESV_USAGE_BOOKKEEP, &fence)", gem_c)
            self.assertIn("dma_fence_add_callback(fence, &close->cb, msm_gem_close_fence_cb)", gem_c)
            self.assertIn("msm_vma->close_pending = false;", gem_c)
            self.assertIn("msm_vma->close_seq == close->close_seq", gem_c)
            self.assertIn("msm_gem_close_sync(obj, vm);", gem_c)

            drv_c = (kernel / "drivers/gpu/drm/msm/msm_drv.c").read_text()
            self.assertIn('alloc_workqueue("msm-gem-close", WQ_UNBOUND, 0)', drv_c)
            self.assertIn("wait_event(priv->close_wait,", drv_c)
            self.assertIn("destroy_workqueue(priv->close_wq);", drv_c)

            adreno_c = (kernel / "drivers/gpu/drm/msm/adreno/adreno_device.c").read_text()
            self.assertIn("flush_workqueue(priv->close_wq);", adreno_c)


if __name__ == "__main__":
    unittest.main()
