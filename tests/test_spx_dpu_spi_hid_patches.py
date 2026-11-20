"""Verification suite for DPU CRTC kickoff, dispcc-sm8250, spi-hid robustness,
iptsd-sq2 MPP2 thresholds, and SQ2 ldoe14 XOB documentation.
"""
import configparser
import hashlib
import os
from pathlib import Path
import re
import shutil
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
import full_image_display as fid

EXTERNAL_TARBALL = ROOT / "source-dependencies/kernel-6.18/external-sources.tar.gz"
PINNED_EXTERNAL_SHA256 = "b080faec4c0229eebbeb0d53c4f1946f6df3c2fbb593b3909015d9caea14fa58"
BASELINE_PATCH = ROOT / "patches/kernel-6.18-surface-baseline.patch"

DPU_PATCH = ROOT / "patches/drm-618-dpu-crtc-kickoff/0001-drm-msm-dpu-count-pending-frame-before-kickoff.patch"
DISPCC_PATCH = ROOT / "patches/spx-portable-boot/0008-clk-qcom-dispcc-sm8250-no-init-park-mdp.patch"
SPI_HID_IRQ_PATCH = ROOT / "patches/spx-portable-boot/0004-spx-spi-hid-clear-pending-irq.patch"
SPI_HID_ROBUST_PATCH = ROOT / "patches/spx-portable-boot/0009-spx-spi-hid-cold-boot-robustness.patch"

_DEFAULT_SOURCES = Path.home() / "spx-full-image/kernel-20261110-v6-sources"
KSRC = Path(os.environ.get("SPX_KERNEL_SRC", str(_DEFAULT_SOURCES / "kernel")))


def extract_new_file_from_baseline(rel_path):
    """Extract a 'new file mode' file from patches/kernel-6.18-surface-baseline.patch."""
    text = BASELINE_PATCH.read_text()
    marker = f"diff --git a/{rel_path} b/{rel_path}\n"
    idx = text.index(marker)
    next_idx = text.find("\ndiff --git ", idx + len(marker))
    block = text[idx:] if next_idx == -1 else text[idx:next_idx]
    lines = [
        line[1:]
        for line in block.splitlines()
        if line.startswith("+") and not line.startswith("+++")
    ]
    return "\n".join(lines) + "\n"


def extract_c_function(source, name):
    match = re.search(
        r"^(?:static )?[^\n]*\b" + re.escape(name) + r"\([^;{]*?\n\{.*?\n\}\n",
        source,
        re.M | re.S,
    )
    if not match:
        raise ValueError(f"missing C function: {name}")
    return match.group(0)


def extract_spi_hid_hunk_from_0004():
    text = SPI_HID_IRQ_PATCH.read_text()
    marker = "--- a/drivers/hid/spi-hid/spi-hid-core.c\n"
    return text[text.index(marker):]


class SpxDpuCrtcKickoffTests(unittest.TestCase):
    def test_external_sources_tarball_sha256_untouched(self):
        actual = hashlib.sha256(EXTERNAL_TARBALL.read_bytes()).hexdigest()
        self.assertEqual(actual, PINNED_EXTERNAL_SHA256)

    def test_dpu_patch_wired_in_full_image(self):
        self.assertEqual(
            fi.DEFAULT_DPU_CRTC_KICKOFF_PATCHES,
            ROOT / "patches/drm-618-dpu-crtc-kickoff",
        )
        self.assertTrue(DPU_PATCH.is_file())
        source = (ROOT / "scripts/full_image.py").read_text()
        self.assertIn("DEFAULT_DPU_CRTC_KICKOFF_PATCHES", source)
        _, blocks = fi.split_git_diffs(DPU_PATCH.read_text())
        self.assertEqual([name for name, _ in blocks], ["drivers/gpu/drm/msm/disp/dpu1/dpu_crtc.c"])

    def test_dpu_patch_applies_to_raw_and_prepared_edp618_and_apply_kernel_patches(self):
        with tempfile.TemporaryDirectory(prefix="spx-dpu-test-") as td:
            td = Path(td)
            with tarfile.open(EXTERNAL_TARBALL) as tf:
                tf.extractall(td, filter="data")

            raw_edp = td / "external/edp-618"
            raw_dpu_crtc = raw_edp / "drivers/gpu/drm/msm/disp/dpu1/dpu_crtc.c"
            raw_bytes = raw_dpu_crtc.read_bytes()

            # Prepare a synthetic kernel tree matching raw dpu_crtc.c + prepared external/edp-618.
            kernel_src = td / "kernel"
            k_dpu_crtc = kernel_src / "drivers/gpu/drm/msm/disp/dpu1/dpu_crtc.c"
            k_dpu_crtc.parent.mkdir(parents=True, exist_ok=True)
            k_dpu_crtc.write_bytes(raw_bytes)

            # 1) Direct --fuzz=0 apply on raw extracted dpu_crtc.c
            raw_only = td / "raw-only"
            raw_only_file = raw_only / "drivers/gpu/drm/msm/disp/dpu1/dpu_crtc.c"
            raw_only_file.parent.mkdir(parents=True, exist_ok=True)
            raw_only_file.write_bytes(raw_bytes)
            r_raw = subprocess.run(
                ["patch", "-p1", "--batch", "--forward", "--fuzz=0", "-i", str(DPU_PATCH)],
                cwd=raw_only,
                capture_output=True,
                text=True,
            )
            self.assertEqual(r_raw.returncode, 0, r_raw.stdout + r_raw.stderr)

            # 2) Run full_image_display.prepare() first (real build order!), then apply_kernel_patches()
            fid.prepare(td / "external")
            self.assertIn("dpu_release_kickoff(crtc);", raw_dpu_crtc.read_text())

            # Create a minimal primary patch dir containing a dummy no-op? No: use DPU patch dir directly
            # as extra_patch_dirs alongside a minimal valid patch dir, OR as primary dir.
            out_dir = td / "out"
            out_dir.mkdir()

            def run_cmd(cmd, cwd=None):
                subprocess.run(cmd, cwd=cwd, check=True, capture_output=True, text=True)

            # Test apply_kernel_patches with DPU dir as extra_patch_dirs (mirroring full_image.py)
            # Use a single-file dummy patch dir for primary to verify extra_patch_dirs plumbing.
            primary_dir = td / "primary-patches"
            primary_dir.mkdir()
            (kernel_src / "dummy.txt").write_text("hello\n")
            (primary_dir / "0001-dummy.patch").write_text(
                "diff --git a/dummy.txt b/dummy.txt\n"
                "--- a/dummy.txt\n"
                "+++ b/dummy.txt\n"
                "@@ -1 +1 @@\n"
                "-hello\n"
                "+world\n"
            )
            report = fi.apply_kernel_patches(
                primary_dir,
                kernel_src,
                out_dir,
                run_cmd,
                external_source=td / "external",
                extra_patch_dirs=(fi.DEFAULT_DPU_CRTC_KICKOFF_PATCHES,),
            )
            self.assertTrue(report["external_msm_synced"])
            self.assertEqual(report["extra_directories"], [str(fi.DEFAULT_DPU_CRTC_KICKOFF_PATCHES)])
            self.assertEqual(report["patch_count"], 2)
            dpu_entry = report["patches"][1]
            self.assertIn("drivers/gpu/drm/msm/disp/dpu1/dpu_crtc.c", dpu_entry["files"])
            self.assertIn("drivers/gpu/drm/msm/disp/dpu1/dpu_crtc.c", dpu_entry["external_files"])

            # Verify ordering in both kernel and prepared edp-618 dpu_crtc.c
            for path in (k_dpu_crtc, raw_dpu_crtc, raw_only_file):
                text = path.read_text()
                clone_fn = extract_c_function(text, "dpu_crtc_kickoff_clone_mode")
                self.assertIn("dpu_crtc_count_pending_frame(crtc);", clone_fn)
                self.assertLess(
                    clone_fn.index("dpu_crtc_count_pending_frame(crtc);"),
                    clone_fn.index("dpu_encoder_kickoff(wb_encoder);"),
                )
                commit_fn = extract_c_function(text, "dpu_crtc_commit_kickoff")
                self.assertIn("dpu_crtc_count_pending_frame(crtc);", commit_fn)
                self.assertLess(
                    commit_fn.index("dpu_crtc_count_pending_frame(crtc);"),
                    commit_fn.index("dpu_encoder_kickoff(encoder);"),
                )
                self.assertNotIn("atomic_inc_return(&dpu_crtc->frame_pending)", commit_fn)

            # Verify full_image_display's release-fence hook survived in edp-618
            self.assertIn("dpu_release_kickoff(crtc);", raw_dpu_crtc.read_text())


class SpxDispccNoInitParkTests(unittest.TestCase):
    def test_dispcc_patch_structure(self):
        self.assertTrue(DISPCC_PATCH.is_file())
        text = DISPCC_PATCH.read_text()
        self.assertIn("diff --git a/drivers/clk/qcom/dispcc-sm8250.c b/drivers/clk/qcom/dispcc-sm8250.c", text)
        self.assertIn("-\t\t.ops = &clk_rcg2_shared_ops,", text)
        self.assertIn("+\t\t.ops = &clk_rcg2_shared_no_init_park_ops,", text)

    @unittest.skipUnless(KSRC.is_dir(), "requires local 6.18 reference kernel source")
    def test_dispcc_patch_applies_and_symbol_exported_in_618(self):
        rcg_h = (KSRC / "drivers/clk/qcom/clk-rcg.h").read_text()
        rcg2_c = (KSRC / "drivers/clk/qcom/clk-rcg2.c").read_text()
        self.assertIn("clk_rcg2_shared_no_init_park_ops;", rcg_h)
        self.assertIn("EXPORT_SYMBOL_GPL(clk_rcg2_shared_no_init_park_ops);", rcg2_c)

        with tempfile.TemporaryDirectory(prefix="spx-dispcc-test-") as td:
            td = Path(td)
            dst = td / "drivers/clk/qcom/dispcc-sm8250.c"
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(KSRC / "drivers/clk/qcom/dispcc-sm8250.c", dst)
            r = subprocess.run(
                ["patch", "-p1", "--batch", "--forward", "--fuzz=0", "-i", str(DISPCC_PATCH)],
                cwd=td,
                capture_output=True,
                text=True,
            )
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            patched = dst.read_text()
            m = re.search(r"static struct clk_rcg2 disp_cc_mdss_mdp_clk_src = \{.*?\n\};", patched, re.S)
            self.assertIsNotNone(m)
            self.assertIn(".ops = &clk_rcg2_shared_no_init_park_ops,", m.group(0))


class SpxSpiHidRobustnessTests(unittest.TestCase):
    def _apply_spi_hid_series_to_baseline(self):
        src = extract_new_file_from_baseline("drivers/hid/spi-hid/spi-hid-core.c")
        with tempfile.TemporaryDirectory(prefix="spx-spihid-test-") as td:
            td = Path(td)
            target = td / "drivers/hid/spi-hid/spi-hid-core.c"
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(src)

            p0004 = td / "0004-spi-hid-only.patch"
            p0004.write_text(extract_spi_hid_hunk_from_0004())
            r4 = subprocess.run(
                ["patch", "-p1", "--batch", "--forward", "--fuzz=0", "-i", str(p0004)],
                cwd=td,
                capture_output=True,
                text=True,
            )
            self.assertEqual(r4.returncode, 0, r4.stdout + r4.stderr)

            r9 = subprocess.run(
                ["patch", "-p1", "--batch", "--forward", "--fuzz=0", "-i", str(SPI_HID_ROBUST_PATCH)],
                cwd=td,
                capture_output=True,
                text=True,
            )
            self.assertEqual(r9.returncode, 0, r9.stdout + r9.stderr)
            return target.read_text()

    def test_spi_hid_robustness_applies_and_preserves_d3_and_s2idle(self):
        patched = self._apply_spi_hid_series_to_baseline()

        # Verify all 3 robustness improvements are present
        self.assertIn("#define SPI_HID_MAX_RESET_ATTEMPTS 5", patched)
        self.assertIn("msecs_to_jiffies(2000)", patched)
        self.assertNotIn("msecs_to_jiffies(1000)", patched)

        irq_fn = extract_c_function(patched, "spi_hid_dev_irq")
        self.assertIn("if (!shid->powered || shid->power_state == SPI_HID_POWER_MODE_OFF)", irq_fn)
        self.assertIn("READ_ONCE(shid->pm_suspended)", irq_fn)

        hdr_fn = extract_c_function(patched, "spi_hid_input_header_complete")
        self.assertIn("if (--shid->input_transfer_pending)", hdr_fn)
        self.assertIn("else if (!shid->ready)", hdr_fn)
        self.assertIn("schedule_work(&shid->error_work);", hdr_fn)

        # STRICT D3 / s2idle non-regression checks
        close_fn = extract_c_function(patched, "spi_hid_ll_close")
        self.assertIn("spi_hid_power_down(shid)", close_fn)
        self.assertIn("SPI_HID_POWER_MODE_OFF", close_fn)

        create_fn = extract_c_function(patched, "spi_hid_create_device_work")
        self.assertIn("spi_hid_power_down(shid)", create_fn)
        self.assertIn("SPI_HID_POWER_MODE_OFF", create_fn)

        self.assertIn("static int spi_hid_pm_suspend(struct device *dev)", patched)
        self.assertIn("static int spi_hid_pm_resume(struct device *dev)", patched)

    def test_spi_hid_host_c_irq_and_header_recovery_behavior(self):
        if shutil.which("gcc") is None:
            self.skipTest("gcc not available")
        patched = self._apply_spi_hid_series_to_baseline()
        hdr_fn = extract_c_function(patched, "spi_hid_input_header_complete")
        irq_fn = extract_c_function(patched, "spi_hid_dev_irq")

        harness = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int irqreturn_t;
#define IRQ_HANDLED 1
#define KERN_ERR ""
#define DUMP_PREFIX_NONE 0
#define SPI_HID_POWER_MODE_ACTIVE 0
#define SPI_HID_POWER_MODE_OFF 1
#define SPI_HID_INPUT_STAGE_BODY 2
#define SPI_HID_REPORT_TYPE_COMMAND_RESP 1
#define SPI_HID_REPORT_TYPE_GET_FEATURE_RESP 2
#define SPI_HID_REPORT_TYPE_REPORT_DESC 3

struct device { int dummy; };
struct spi_device { struct device dev; };
struct spi_transfer { void *tx_buf; void *rx_buf; size_t len; };
struct spi_message { int status; };
struct spi_hid_input_header { int report_type; size_t report_length; };
struct spi_hid_input_buf { uint8_t header[4]; uint8_t body[64]; };
struct spi_hid {
    struct spi_device *spi;
    int input_lock;
    bool powered;
    bool pm_suspended;
    bool ready;
    uint8_t power_state;
    int input_stage;
    int input_transfer_pending;
    int bus_error_count;
    int bus_last_error;
    struct spi_transfer input_transfer[2];
    struct spi_message input_message;
    struct spi_hid_input_buf input;
    struct spi_hid_input_buf response;
    int error_work;
};

static int irq_report_calls;
static int error_work_calls;
static int async_calls;
static int async_ret;
static int validate_ret;
static void (*last_async_cb)(void *);

#define READ_ONCE(x) (x)
#define spin_lock(l) ((void)(l))
#define spin_unlock(l) ((void)(l))
#define spin_lock_irqsave(l, f) do { (void)(l); (f) = 0; } while (0)
#define spin_unlock_irqrestore(l, f) do { (void)(l); (void)(f); } while (0)
#define dev_err(d, ...) ((void)(d))
#define dev_warn(d, ...) ((void)(d))
#define print_hex_dump(...) ((void)0)
#define trace_spi_hid_dev_irq(s, i) do { (void)(s); (void)(i); } while (0)
#define trace_spi_hid_input_header_complete(s, a, b, c, d, e) do { \
    (void)(s); (void)(a); (void)(b); (void)(c); (void)(d); (void)(e); \
} while (0)

static void schedule_work(int *w) { (void)w; error_work_calls++; }
static void spi_hid_populate_input_header(uint8_t *raw, struct spi_hid_input_header *h) {
    (void)raw; h->report_type = 0; h->report_length = 8;
}
static int spi_hid_bus_validate_header(struct spi_hid *s, struct spi_hid_input_header *h) {
    (void)s; (void)h; return validate_ret;
}
static int spi_hid_input_async(struct spi_hid *s, void *buf, size_t len, void (*cb)(void *)) {
    (void)s; (void)buf; (void)len;
    async_calls++;
    last_async_cb = cb;
    return async_ret;
}
static void spi_hid_input_body_complete(void *_shid) { (void)_shid; }
static int spi_hid_bus_input_report(struct spi_hid *s) { (void)s; irq_report_calls++; return 0; }
""" + hdr_fn + "\n" + irq_fn + r"""
int main(void) {
    struct spi_device spi = {0};
    struct spi_hid shid;

    /* 1. Unpowered IRQ must be ignored */
    memset(&shid, 0, sizeof(shid));
    shid.spi = &spi;
    shid.powered = false;
    shid.power_state = SPI_HID_POWER_MODE_ACTIVE;
    assert(spi_hid_dev_irq(1, &shid) == IRQ_HANDLED);
    assert(irq_report_calls == 0);

    /* 2. Power-OFF IRQ must be ignored even if powered flag not yet cleared */
    shid.powered = true;
    shid.power_state = SPI_HID_POWER_MODE_OFF;
    assert(spi_hid_dev_irq(1, &shid) == IRQ_HANDLED);
    assert(irq_report_calls == 0);

    /* 3. Suspended IRQ must be ignored */
    shid.power_state = SPI_HID_POWER_MODE_ACTIVE;
    shid.pm_suspended = true;
    assert(spi_hid_dev_irq(1, &shid) == IRQ_HANDLED);
    assert(irq_report_calls == 0);

    /* 4. Powered, active, resumed IRQ processes input report */
    shid.pm_suspended = false;
    assert(spi_hid_dev_irq(1, &shid) == IRQ_HANDLED);
    assert(irq_report_calls == 1);

    /* 5. Invalid header with queued pending=2 decrements and restarts header read */
    validate_ret = -EINVAL;
    async_ret = 0;
    async_calls = 0;
    error_work_calls = 0;
    shid.ready = false;
    shid.input_transfer_pending = 2;
    spi_hid_input_header_complete(&shid);
    assert(shid.input_transfer_pending == 1);
    assert(async_calls == 1);
    assert(last_async_cb == spi_hid_input_header_complete);
    assert(error_work_calls == 0);

    /* 6. Invalid header during cold boot (!ready) with pending=1 schedules error_work */
    async_calls = 0;
    error_work_calls = 0;
    shid.ready = false;
    shid.input_transfer_pending = 1;
    spi_hid_input_header_complete(&shid);
    assert(shid.input_transfer_pending == 0);
    assert(async_calls == 0);
    assert(error_work_calls == 1);

    /* 7. Invalid header after ready with pending=1 clears pending without reset storm */
    async_calls = 0;
    error_work_calls = 0;
    shid.ready = true;
    shid.input_transfer_pending = 1;
    spi_hid_input_header_complete(&shid);
    assert(shid.input_transfer_pending == 0);
    assert(async_calls == 0);
    assert(error_work_calls == 0);

    /* 8. Restart failure resets input_transfer_pending to 0 */
    async_ret = -EIO;
    async_calls = 0;
    shid.ready = false;
    shid.input_transfer_pending = 2;
    spi_hid_input_header_complete(&shid);
    assert(async_calls == 1);
    assert(shid.input_transfer_pending == 0);

    return 0;
}
"""
        with tempfile.TemporaryDirectory(prefix="spx-spihid-c-") as td:
            c_file = Path(td) / "test.c"
            bin_file = Path(td) / "test"
            c_file.write_text(harness)
            subprocess.run(
                ["gcc", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", str(c_file), "-o", str(bin_file)],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(bin_file)], check=True, capture_output=True, text=True)


class SpxIptsdAndSpeakerDocsTests(unittest.TestCase):
    def test_iptsd_sq2_conf_mpp2_thresholds_and_compat(self):
        conf_text = (ROOT / "configs/iptsd-sq2.conf").read_text()
        cal_text = (ROOT / "configs/iptsd-sq2-calibration.conf").read_text()

        # Standalone parse
        cfg = configparser.ConfigParser()
        cfg.read_string(conf_text)
        self.assertEqual(cfg["Touchscreen"]["DisableOnStylus"], "true")
        self.assertEqual(cfg["DFT"]["PositionMinAmp"], "15")
        self.assertEqual(cfg["DFT"]["PositionMinMag"], "400")
        self.assertEqual(cfg["DFT"]["TiltMinMag"], "2000")
        self.assertEqual(cfg["DFT"]["ButtonMinMag"], "4000")
        self.assertEqual(cfg["DFT"]["Mpp2ButtonMinMag"], "15000")
        self.assertEqual(cfg["DFT"]["Mpp2ContactMinMag"], "8000")
        self.assertEqual(cfg["Contacts"]["AspectMax"], "2.0")
        self.assertEqual(cfg["Contacts"]["SizeMax"], "2.0")

        # Combined parse (matching scripts/build-test3-iptsd.py + inih section merge)
        merged = configparser.ConfigParser(strict=False)
        merged.read_string(conf_text + "\n" + cal_text)
        self.assertEqual(merged["Touchscreen"]["DisableOnStylus"], "true")
        self.assertIn("CalibrationX", merged["Touchscreen"])
        self.assertEqual(merged["DFT"]["Mpp2ContactMinMag"], "8000")
        self.assertEqual(merged["Contacts"]["SizeMax"], "2.0")

    def test_speaker_loudness_documents_ldoe14_xob_without_mutating_dts(self):
        doc = (ROOT / "docs/SPEAKER-LOUDNESS.md").read_text()
        for token in ("ldoe14", "0x43900", "XOB", "+0x4", "vreg_l14e_1p8", "TCS"):
            self.assertIn(token, doc)

        baseline = BASELINE_PATCH.read_text()
        self.assertIn("vreg_l14e_1p8: ldo14 {", baseline)
        self.assertIn("vdda-supply = <&vreg_l14e_1p8>;", baseline)


if __name__ == "__main__":
    unittest.main()
