"""Unit tests for Surface Pro X USB-DP / Type-C / QMP / DWC3 s2idle resume fixes."""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import full_image as full  # noqa: E402

PATCHES_DIR = ROOT / "patches"
OWNED_PATCHES = (
    "spx-qmp-lifecycle.patch",
    "spx-dp-s2idle.patch",
    "spx-dwc3-s2idle.patch",
    "spx-dwc3-ffs-ep0-dequeue.patch",
)


def _resolve_kernel_src():
    env = os.environ.get("SPX_KERNEL_SRC")
    if env:
        return Path(env)
    return Path.home() / "spx-full-image/kernel-20261110-v6-sources/kernel"


def _resolve_external_src():
    env = os.environ.get("SPX_EXTERNAL_SRC")
    if env:
        return Path(env)
    return Path.home() / "spx-full-image/kernel-20261110-v6-sources/external"


class DpS2idleFixes(unittest.TestCase):
    def test_patches_have_valid_git_diff_headers(self):
        for name in OWNED_PATCHES:
            text = (PATCHES_DIR / name).read_text()
            _, blocks = full.split_git_diffs(text)
            self.assertTrue(blocks, f"No git diff blocks found in {name}")

    def test_patches_dry_run_fuzz0_against_kernel_tree(self):
        ksrc = _resolve_kernel_src()
        if not ksrc.is_dir():
            self.skipTest("optional local kernel source tree not present")
        for name in OWNED_PATCHES:
            res = subprocess.run(
                [
                    "patch",
                    "--dry-run",
                    "--batch",
                    "--forward",
                    "--fuzz=0",
                    "-p1",
                    "-i",
                    str(PATCHES_DIR / name),
                ],
                cwd=ksrc,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                res.returncode,
                0,
                f"{name} failed --fuzz=0 dry-run:\n{res.stdout}\n{res.stderr}",
            )

    def test_dp_patch_dry_run_fuzz0_against_external_edp618(self):
        esrc = _resolve_external_src() / "edp-618"
        if not esrc.is_dir():
            self.skipTest("optional external/edp-618 source tree not present")
        res = subprocess.run(
            [
                "patch",
                "--dry-run",
                "--batch",
                "--forward",
                "--fuzz=0",
                "-p1",
                "-i",
                str(PATCHES_DIR / "spx-dp-s2idle.patch"),
            ],
            cwd=esrc,
            capture_output=True,
            text=True,
        )
        self.assertEqual(
            res.returncode,
            0,
            f"spx-dp-s2idle.patch failed external/edp-618 --fuzz=0:\n{res.stdout}\n{res.stderr}",
        )

    def test_dp_s2idle_patch_invariants(self):
        text = (PATCHES_DIR / "spx-dp-s2idle.patch").read_text()
        # dpu_kms.c fixes: restore SC8180X MDP_DP_PHY_INTF_SEL across MDSS_GDSC collapse
        for snippet in (
            "static void dpu_kms_setup_dp_phy_intf_sel(struct dpu_kms *dpu_kms)",
            "if (!dpu_kms || !dpu_kms->hw_mdp || !dpu_kms->hw_mdp->ops.dp_phy_intf_sel)",
            "dpu_kms_setup_dp_phy_intf_sel(dpu_kms);",
        ):
            self.assertIn(snippet, text)
        self.assertEqual(text.count("dpu_kms_setup_dp_phy_intf_sel("), 3)
        # dp_debug.c fix: initialize debug->connector to prevent debugfs NULL dereference
        self.assertIn("debug->connector = connector;", text)
        # dp_aux.c fixes: reftimer enable, AUX reset on init/reconfig, external-HPD link check
        for snippet in (
            "msm_dp_write_aux(aux, REG_DP_AUX_TRANS_CTRL, 0);",
            "reftimer |= DP_DP_HPD_REFTIMER_ENABLE;",
            "msm_dp_aux_reset(aux);",
            "if (!(msm_dp_read_aux(aux, REG_DP_DP_HPD_CTRL) & DP_DP_HPD_CTRL_HPD_EN))",
            "return aux->enable_xfers ? 1 : 0;",
            "mutex_lock(&aux->mutex);",
        ):
            self.assertIn(snippet, text)
        # dp_ctrl.c fixes: phy_power_on check, aux_reconfig after phy_init,
        # D0 wake + link_retrain failure check + full clock/phy unwind
        for snippet in (
            "ret = phy_power_on(phy);",
            "msm_dp_aux_reconfig(ctrl->aux);",
            "ret = msm_dp_link_psm_config(ctrl->link, &ctrl->panel->link_info, false);",
            "ret = msm_dp_ctrl_link_retrain(ctrl);",
            "goto err_link_train;",
            "if (stream_clks_enabled_here)",
            "if (link_clks_enabled_here)",
        ):
            self.assertIn(snippet, text)
        # dp_display.c fixes: ensure DPU PHY mux before external DP AUX,
        # ST_DISPLAY_OFF HPD plug recovery, atomic_enable error handling,
        # ST_MAINLINK_READY post_disable cleanup, preserve ST_DISCONNECTED, external IRQ_HPD dispatch
        for snippet in (
            "static void msm_dp_display_ensure_kms_phy_sel(struct msm_dp_display_private *dp)",
            "if (pm_runtime_resume_and_get(kms_dev) == 0)",
            "pm_runtime_put_sync(kms_dev);",
            "msm_dp_display_send_hpd_event(&dp->msm_dp_display);",
            "rc = msm_dp_display_enable(msm_dp_display, force_link_train);",
            "if (phy_initted)",
            "if (!dp->power_on && hpd_state == ST_MAINLINK_READY)",
            "if (hpd_state == ST_DISCONNECT_PENDING || hpd_state == ST_DISCONNECTED)",
            "if (READ_ONCE(dp->hpd_state) == ST_DISPLAY_OFF)",
            "msm_dp_add_event(dp, EV_IRQ_HPD_INT, 0, 0);",
        ):
            self.assertIn(snippet, text)
        self.assertEqual(text.count("msm_dp_display_ensure_kms_phy_sel("), 4)
        # Ensure host_phy_exit rollback only occurs in enable() failure, not post_enable()
        self.assertEqual(text.count("if (phy_initted)"), 1)

    def test_qmp_lifecycle_patch_invariants(self):
        text = (PATCHES_DIR / "spx-qmp-lifecycle.patch").read_text()
        for snippet in (
            "bool usb_powered_on;",
            "usleep_range(1000, 1100);",
            "pm_runtime_get_noresume(qmp->dev);",
            "if (!qmp->dp_init_count)",
            "pm_runtime_put(qmp->dev);",
            "ret = qmp_combo_dp_serdes_init(qmp);",
            "ret = cfg->configure_dp_phy(qmp);",
        ):
            self.assertIn(snippet, text)

    def test_spx_typec_non_freezable_pm_replay_and_irq_hpd(self):
        src = (ROOT / "probe/typec/spx-typec.c").read_text()
        self.assertNotIn("WQ_FREEZABLE", src)
        self.assertIn('alloc_ordered_workqueue("spx-typec",WQ_MEM_RECLAIM)', src)
        self.assertIn("SYSTEM_SLEEP_PM_OPS(spx_suspend,spx_resume)", src)
        self.assertIn(".complete=pm_sleep_ptr(spx_complete)", src)
        self.assertIn(".pm=pm_sleep_ptr(&spx_pm_ops)", src)
        self.assertIn("flush_workqueue(ctx->queue);", src)
        self.assertIn("if(!ctx->routing_ready || ctx->suspended)return 0;", src)
        # Preserve boot storage connector protection and IRQ_HPD forwarding
        self.assertIn("if(boot_storage_ports & BIT(b[4]))return 0;", src)
        self.assertIn("irq_hpd=!!b[10];", src)
        self.assertIn("if(hpd!=p->hpd_asserted || irq_hpd || force_hpd)", src)
        self.assertIn('MODULE_INFO(spx_boot_handoff,"1");', src)

    def test_spx_ucsi_transport_driver_pm_ops(self):
        src = (ROOT / "probe/typec/spx-ucsi-transport.c").read_text()
        self.assertIn("static struct platform_driver spx_ucsi_driver", src)
        self.assertIn(".pm = &surface_hid_pm_ops,", src)
        self.assertIn("ssam_client_bind(&pdev->dev)", src)
        self.assertIn("platform_set_drvdata(pdev, shid);", src)
        self.assertIn("surface_hid_device_add(shid)", src)
        self.assertIn("surface_hid_device_destroy(shid);", src)
        self.assertIn("platform_driver_register(&spx_ucsi_driver)", src)
        self.assertIn("if (!platform_get_drvdata(spx_ucsi_pdev))", src)
        self.assertIn("platform_driver_unregister(&spx_ucsi_driver);", src)

    def test_dwc3_and_ffs_patches_and_builder_wiring(self):
        dwc3_patch = (PATCHES_DIR / "spx-dwc3-s2idle.patch").read_text()
        self.assertEqual(dwc3_patch.count("if (dwc3_qcom_is_host(qcom) && wakeup)"), 2)
        self.assertEqual(dwc3_patch.count("+			wakeup-source;"), 3)

        ffs_patch = (PATCHES_DIR / "spx-dwc3-ffs-ep0-dequeue.patch").read_text()
        self.assertIn("#define FFS_FL_EP0REQ_QUEUED 2", ffs_patch)
        self.assertIn("set_bit(FFS_FL_EP0REQ_QUEUED, &ffs->flags);", ffs_patch)
        self.assertIn("clear_bit(FFS_FL_EP0REQ_QUEUED, &ffs->flags);", ffs_patch)
        self.assertEqual(
            ffs_patch.count("if (test_and_clear_bit(FFS_FL_EP0REQ_QUEUED, &ffs->flags))"),
            2,
        )
        # No ABI layout change to u_fs.h
        self.assertNotIn("u_fs.h", ffs_patch)

        builder = (ROOT / "scripts/build-spx-typec.py").read_text()
        self.assertIn("patches/spx-dwc3-ffs-ep0-dequeue.patch", builder)
        self.assertIn("patches/spx-qmp-lifecycle.patch", builder)

    def test_full_image_s2idle_patch_wiring(self):
        self.assertEqual(
            full.SPX_S2IDLE_PATCHES,
            (
                ROOT / "patches/spx-dp-s2idle.patch",
                ROOT / "patches/spx-dwc3-s2idle.patch",
            ),
        )
        # OOT Type-C patches must stay out of SPX_S2IDLE_PATCHES so build-spx-typec.py
        # can still patch fresh copies of phy-qcom-qmp-combo.c and f_fs.c from extracted/kernel.
        names = {p.name for p in full.SPX_S2IDLE_PATCHES}
        self.assertNotIn("spx-qmp-lifecycle.patch", names)
        self.assertNotIn("spx-dwc3-ffs-ep0-dequeue.patch", names)

        src = (ROOT / "scripts/full_image.py").read_text()
        call_idx = src.find("apply_spx_s2idle_patches(")
        self.assertGreater(call_idx, 0)
        self.assertIn(
            "apply_spx_s2idle_patches(\n            extracted/'kernel', run, external_source=extracted/'external')",
            src,
        )
        # Must run before build.py compile and outside the optional kernel_patches block.
        prepare_idx = src.index("display = prepare_display(extracted/'external')")
        optional_idx = src.index("if kernel_patches:")
        compile_idx = src.index("ROOT/'scripts/build.py','compile'")
        invoke_idx = src.index("s2idle_patches = apply_spx_s2idle_patches(")
        self.assertLess(prepare_idx, invoke_idx)
        self.assertLess(invoke_idx, optional_idx)
        self.assertLess(invoke_idx, compile_idx)

    def test_apply_spx_s2idle_patches_synthetic_idempotent_and_failure_propagation(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            kernel = root / "kernel"
            external = root / "external"
            edp = external / "edp-618"
            for tree in (kernel, edp):
                (tree / "drivers/gpu/drm/msm/dp").mkdir(parents=True)
            (kernel / "drivers/usb/dwc3").mkdir(parents=True)

            aux_before = "int aux_a = 1;\nint aux_b = 2;\nint aux_c = 3;\n"
            ctrl_k_before = "int ctrl_h = 0;\nint ctrl_a = 1;\nint ctrl_b = 2;\nint ctrl_c = 3;\n"
            ctrl_e_before = (
                "int sq2_edp_extra = 1;\n"
                "int ctrl_h = 0;\nint ctrl_a = 1;\nint ctrl_b = 2;\nint ctrl_c = 3;\n"
            )
            dwc_before = "int dwc_a = 1;\nint dwc_b = 2;\nint dwc_c = 3;\n"

            (kernel / "drivers/gpu/drm/msm/dp/dp_aux.c").write_text(aux_before)
            (edp / "drivers/gpu/drm/msm/dp/dp_aux.c").write_text(aux_before)
            (kernel / "drivers/gpu/drm/msm/dp/dp_ctrl.c").write_text(ctrl_k_before)
            (edp / "drivers/gpu/drm/msm/dp/dp_ctrl.c").write_text(ctrl_e_before)
            (kernel / "drivers/usb/dwc3/dwc3-qcom.c").write_text(dwc_before)

            dp_patch = root / "dp.patch"
            dp_patch.write_text(
                "diff --git a/drivers/gpu/drm/msm/dp/dp_aux.c b/drivers/gpu/drm/msm/dp/dp_aux.c\n"
                "--- a/drivers/gpu/drm/msm/dp/dp_aux.c\n"
                "+++ b/drivers/gpu/drm/msm/dp/dp_aux.c\n"
                "@@ -1,3 +1,3 @@\n"
                " int aux_a = 1;\n"
                "-int aux_b = 2;\n"
                "+int aux_b = 20;\n"
                " int aux_c = 3;\n"
                "diff --git a/drivers/gpu/drm/msm/dp/dp_ctrl.c b/drivers/gpu/drm/msm/dp/dp_ctrl.c\n"
                "--- a/drivers/gpu/drm/msm/dp/dp_ctrl.c\n"
                "+++ b/drivers/gpu/drm/msm/dp/dp_ctrl.c\n"
                "@@ -2,3 +2,3 @@\n"
                " int ctrl_a = 1;\n"
                "-int ctrl_b = 2;\n"
                "+int ctrl_b = 20;\n"
                " int ctrl_c = 3;\n"
            )
            dwc_patch = root / "dwc3.patch"
            dwc_patch.write_text(
                "diff --git a/drivers/usb/dwc3/dwc3-qcom.c b/drivers/usb/dwc3/dwc3-qcom.c\n"
                "--- a/drivers/usb/dwc3/dwc3-qcom.c\n"
                "+++ b/drivers/usb/dwc3/dwc3-qcom.c\n"
                "@@ -1,3 +1,3 @@\n"
                " int dwc_a = 1;\n"
                "-int dwc_b = 2;\n"
                "+int dwc_b = 20;\n"
                " int dwc_c = 3;\n"
            )

            commands = []

            def strict_run(command, cwd=None):
                commands.append((list(map(str, command)), Path(cwd) if cwd else None))
                return subprocess.run(
                    list(map(str, command)),
                    cwd=cwd,
                    check=True,
                    capture_output=True,
                    text=True,
                )

            # First run applies dp.patch to kernel + edp-618 and dwc3.patch to kernel.
            first = full.apply_spx_s2idle_patches(
                kernel, strict_run, external_source=external, patches=(dp_patch, dwc_patch)
            )
            self.assertEqual(first["patch_count"], 2)
            self.assertTrue(first["external_msm_synced"])
            self.assertTrue(first["patches"][0]["applied"])
            self.assertTrue(first["patches"][0]["external_applied"])
            self.assertTrue(first["patches"][1]["applied"])
            self.assertNotIn("external_files", first["patches"][1])
            self.assertEqual(len(commands), 3)
            self.assertIn("int aux_b = 20;", (kernel / "drivers/gpu/drm/msm/dp/dp_aux.c").read_text())
            self.assertEqual(
                (kernel / "drivers/gpu/drm/msm/dp/dp_aux.c").read_text(),
                (edp / "drivers/gpu/drm/msm/dp/dp_aux.c").read_text(),
            )
            self.assertIn("int sq2_edp_extra = 1;", (edp / "drivers/gpu/drm/msm/dp/dp_ctrl.c").read_text())
            self.assertIn("int ctrl_b = 20;", (edp / "drivers/gpu/drm/msm/dp/dp_ctrl.c").read_text())
            self.assertIn("int dwc_b = 20;", (kernel / "drivers/usb/dwc3/dwc3-qcom.c").read_text())

            # Second run is strictly idempotent with zero new patch invocations.
            commands.clear()
            second = full.apply_spx_s2idle_patches(
                kernel, strict_run, external_source=external, patches=(dp_patch, dwc_patch)
            )
            self.assertEqual(commands, [])
            self.assertFalse(second["patches"][0]["applied"])
            self.assertFalse(second["patches"][0]["external_applied"])
            self.assertFalse(second["patches"][1]["applied"])
            self.assertEqual(first["patches"][0]["files"], second["patches"][0]["files"])
            self.assertEqual(first["patches"][0]["external_files"], second["patches"][0]["external_files"])
            self.assertEqual(first["patches"][1]["files"], second["patches"][1]["files"])

            # Missing external MSM directory fails loudly.
            with self.assertRaisesRegex(ValueError, "External MSM override directory is missing"):
                full.apply_spx_s2idle_patches(
                    kernel, strict_run, external_source=root / "missing-ext", patches=(dp_patch,)
                )

            # Fuzz > 0 / corrupted context in kernel must propagate a real patch failure.
            (kernel / "drivers/usb/dwc3/dwc3-qcom.c").write_text(
                "int dwc_a = 999;\nint dwc_b = 2;\nint dwc_c = 3;\n"
            )
            with self.assertRaises(subprocess.CalledProcessError):
                full.apply_spx_s2idle_patches(
                    kernel, strict_run, external_source=external, patches=(dp_patch, dwc_patch)
                )
            (kernel / "drivers/usb/dwc3/dwc3-qcom.c").write_text(
                "int dwc_a = 1;\nint dwc_b = 20;\nint dwc_c = 3;\n"
            )

            # Corrupted context in external/edp-618 must propagate a real patch failure.
            (edp / "drivers/gpu/drm/msm/dp/dp_aux.c").write_text(
                "int aux_a = 999;\nint aux_b = 2;\nint aux_c = 3;\n"
            )
            with self.assertRaises(subprocess.CalledProcessError):
                full.apply_spx_s2idle_patches(
                    kernel, strict_run, external_source=external, patches=(dp_patch,)
                )

            # Half-applied multi-file patch (dp_aux patched, dp_ctrl unpatched) must fail loudly.
            (edp / "drivers/gpu/drm/msm/dp/dp_aux.c").write_text(
                "int aux_a = 1;\nint aux_b = 20;\nint aux_c = 3;\n"
            )
            (edp / "drivers/gpu/drm/msm/dp/dp_ctrl.c").write_text(ctrl_e_before)
            with self.assertRaises(subprocess.CalledProcessError):
                full.apply_spx_s2idle_patches(
                    kernel, strict_run, external_source=external, patches=(dp_patch,)
                )
            # Atomic dry-run failure check must leave dp_ctrl.c unmutated.
            self.assertEqual((edp / "drivers/gpu/drm/msm/dp/dp_ctrl.c").read_text(), ctrl_e_before)

    def test_apply_spx_s2idle_patches_against_real_kernel_and_edp618_trees(self):
        ksrc = _resolve_kernel_src()
        esrc = _resolve_external_src()
        if not ksrc.is_dir() or not (esrc / "edp-618").is_dir():
            self.skipTest("optional local kernel/external source trees not present")

        kernel_relpaths = (
            "drivers/gpu/drm/msm/disp/dpu1/dpu_kms.c",
            "drivers/gpu/drm/msm/dp/dp_aux.c",
            "drivers/gpu/drm/msm/dp/dp_ctrl.c",
            "drivers/gpu/drm/msm/dp/dp_debug.c",
            "drivers/gpu/drm/msm/dp/dp_display.c",
            "drivers/usb/dwc3/dwc3-qcom-legacy.c",
            "drivers/usb/dwc3/dwc3-qcom.c",
            "arch/arm64/boot/dts/qcom/sc8180x.dtsi",
            "drivers/phy/qualcomm/phy-qcom-qmp-combo.c",
            "drivers/usb/gadget/function/f_fs.c",
        )
        edp_relpaths = (
            "drivers/gpu/drm/msm/disp/dpu1/dpu_kms.c",
            "drivers/gpu/drm/msm/dp/dp_aux.c",
            "drivers/gpu/drm/msm/dp/dp_ctrl.c",
            "drivers/gpu/drm/msm/dp/dp_debug.c",
            "drivers/gpu/drm/msm/dp/dp_display.c",
        )
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            kt = root / "kernel"
            ext = root / "external"
            et = ext / "edp-618"
            for rel in kernel_relpaths:
                dst = kt / rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ksrc / rel, dst)
            for rel in edp_relpaths:
                dst = et / rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(esrc / "edp-618" / rel, dst)

            calls = []

            def strict_run(command, cwd=None):
                calls.append(list(map(str, command)))
                return subprocess.run(
                    list(map(str, command)),
                    cwd=cwd,
                    check=True,
                    capture_output=True,
                    text=True,
                )

            first = full.apply_spx_s2idle_patches(kt, strict_run, external_source=ext)
            self.assertEqual(first["patch_count"], 2)
            self.assertTrue(first["external_msm_synced"])
            self.assertEqual(len(calls), 3)
            self.assertTrue(first["patches"][0]["applied"])
            self.assertTrue(first["patches"][0]["external_applied"])
            self.assertTrue(first["patches"][1]["applied"])
            self.assertEqual(
                (kt / "drivers/gpu/drm/msm/dp/dp_aux.c").read_bytes(),
                (et / "drivers/gpu/drm/msm/dp/dp_aux.c").read_bytes(),
            )
            self.assertEqual(
                (kt / "drivers/gpu/drm/msm/dp/dp_debug.c").read_bytes(),
                (et / "drivers/gpu/drm/msm/dp/dp_debug.c").read_bytes(),
            )
            self.assertEqual(
                (kt / "drivers/gpu/drm/msm/dp/dp_display.c").read_bytes(),
                (et / "drivers/gpu/drm/msm/dp/dp_display.c").read_bytes(),
            )
            edp_kms = (et / "drivers/gpu/drm/msm/disp/dpu1/dpu_kms.c").read_text()
            self.assertIn("dpu_release_wait_result", edp_kms)
            self.assertEqual(edp_kms.count("dpu_kms_setup_dp_phy_intf_sel("), 3)
            edp_ctrl = (et / "drivers/gpu/drm/msm/dp/dp_ctrl.c").read_text()
            self.assertIn("sq2_edp_rate_index", edp_ctrl)
            self.assertIn("msm_dp_aux_reconfig(ctrl->aux);", edp_ctrl)
            self.assertIn("msm_dp_link_psm_config(ctrl->link, &ctrl->panel->link_info, false);", edp_ctrl)

            # Second invocation is strictly idempotent.
            calls.clear()
            second = full.apply_spx_s2idle_patches(kt, strict_run, external_source=ext)
            self.assertEqual(calls, [])
            self.assertFalse(second["patches"][0]["applied"])
            self.assertFalse(second["patches"][0]["external_applied"])
            self.assertFalse(second["patches"][1]["applied"])
            self.assertEqual(first["patches"][0]["files"], second["patches"][0]["files"])
            self.assertEqual(first["patches"][0]["external_files"], second["patches"][0]["external_files"])
            self.assertEqual(first["patches"][1]["files"], second["patches"][1]["files"])

            # OOT Type-C patches must still dry-run cleanly against the patched kernel tree.
            for oot_patch in ("spx-qmp-lifecycle.patch", "spx-dwc3-ffs-ep0-dequeue.patch"):
                res = subprocess.run(
                    [
                        "patch",
                        "--dry-run",
                        "--batch",
                        "--forward",
                        "--fuzz=0",
                        "-p1",
                        "-i",
                        str(PATCHES_DIR / oot_patch),
                    ],
                    cwd=kt,
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(res.returncode, 0, f"{oot_patch} conflicted after full_image wiring")

    def test_behavioral_state_machine_harness_asan_ubsan(self):
        gcc = shutil.which("gcc")
        if not gcc:
            self.skipTest("host gcc unavailable")
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 1. External-HPD AUX link-connected + reset-on-timeout model */
struct aux_hw {
    uint32_t hpd_ctrl;
    uint32_t hpd_status;
    uint32_t reftimer;
    uint32_t trans_ctrl;
    bool enable_xfers;
    int resets;
};

static uint32_t aux_is_link_connected(const struct aux_hw *a) {
    if (!(a->hpd_ctrl & 1u))
        return a->enable_xfers ? 1u : 0u;
    return (a->hpd_status >> 29) & 7u;
}

static void aux_reset(struct aux_hw *a) {
    a->trans_ctrl = 0;
    a->resets++;
}

static void aux_on_timeout(struct aux_hw *a) {
    if (aux_is_link_connected(a))
        aux_reset(a);
}

/* 2. MSM DP atomic_enable + ST_DISPLAY_OFF HPD recovery + post_disable model */
enum hpd_state { ST_DISCONNECTED = 0, ST_MAINLINK_READY = 1, ST_CONNECTED = 2, ST_DISPLAY_OFF = 3 };
struct dp_fsm {
    enum hpd_state state;
    bool link_ready;
    bool power_on;
    bool phy_initted;
    bool link_clks_on;
    bool stream_clks_on;
    int pm_refs;
    int hpd_events;
    int irq_hpd_events;
};

static int dp_on_stream(struct dp_fsm *d, bool force_train, int train_rc) {
    bool link_here = false, stream_here = false;
    if (!d->link_clks_on) {
        d->link_clks_on = true;
        link_here = true;
    }
    if (!d->stream_clks_on) {
        d->stream_clks_on = true;
        stream_here = true;
    }
    if (force_train && train_rc != 0) {
        if (stream_here) d->stream_clks_on = false;
        if (link_here) d->link_clks_on = false;
        return train_rc;
    }
    return 0;
}

static void dp_atomic_enable(struct dp_fsm *d, int train_rc) {
    bool phy_here = false, force = false;
    d->pm_refs++;
    if (d->state != ST_DISPLAY_OFF && d->state != ST_MAINLINK_READY)
        return;
    if (d->state == ST_DISPLAY_OFF) {
        d->phy_initted = true;
        phy_here = true;
        force = true;
    }
    int rc = dp_on_stream(d, force, train_rc);
    if (rc) {
        if (phy_here) d->phy_initted = false;
        return;
    }
    d->power_on = true;
    d->state = ST_CONNECTED;
}

static void dp_atomic_post_disable(struct dp_fsm *d) {
    if (!d->power_on && d->state == ST_MAINLINK_READY) {
        d->link_clks_on = false;
        d->phy_initted = false;
    } else if (d->power_on) {
        d->stream_clks_on = false;
        d->link_clks_on = false;
        d->phy_initted = false;
        d->power_on = false;
    }
    if (d->state == ST_DISCONNECTED)
        d->state = ST_DISCONNECTED;
    else
        d->state = ST_DISPLAY_OFF;
    d->pm_refs--;
}

static void dp_hpd_plug(struct dp_fsm *d, int train_rc) {
    if (d->state == ST_DISPLAY_OFF) {
        d->pm_refs++;
        d->phy_initted = true;
        if (train_rc == 0) {
            d->link_clks_on = true;
            d->state = ST_MAINLINK_READY;
            d->hpd_events++;
        } else {
            d->phy_initted = false;
        }
        d->pm_refs--;
        return;
    }
    if (d->state != ST_DISCONNECTED)
        return;
    d->pm_refs++;
    d->phy_initted = true;
    if (train_rc == 0) {
        d->link_clks_on = true;
        d->link_ready = true;
        d->state = ST_MAINLINK_READY;
        d->hpd_events++;
    } else {
        d->phy_initted = false;
        d->pm_refs--;
    }
}

static void dp_hpd_unplug(struct dp_fsm *d) {
    if (d->state == ST_MAINLINK_READY) {
        d->link_clks_on = false;
        d->phy_initted = false;
        d->state = ST_DISCONNECTED;
        d->link_ready = false;
        d->pm_refs--;
        return;
    }
    if (d->state == ST_DISPLAY_OFF) {
        d->state = ST_DISCONNECTED;
        d->link_ready = false;
        d->pm_refs--;
        return;
    }
}

static void dp_hpd_notify(struct dp_fsm *d, bool connected, int train_rc) {
    if (!d->link_ready && connected) {
        dp_hpd_plug(d, train_rc);
    } else if (d->link_ready && connected) {
        if (d->state == ST_DISPLAY_OFF)
            dp_hpd_plug(d, train_rc);
        else
            d->irq_hpd_events++;
    } else if (d->link_ready && !connected) {
        dp_hpd_unplug(d);
    }
}

/* 3. spx-typec suspend/cache/ACK + complete replay + boot_storage_ports model */
struct typec_ctx {
    bool routing_ready;
    bool suspended;
    unsigned boot_storage_ports;
    bool state_valid[2];
    uint8_t state[2][11];
    bool hpd_asserted[2];
    int applies[2];
    int hpd_notifies[2];
    int acks;
};

static int typec_apply(struct typec_ctx *c, const uint8_t *b, bool force_hpd) {
    unsigned port = b[4];
    if (c->boot_storage_ports & (1u << port))
        return 0;
    c->applies[port]++;
    bool hpd = !!b[9], irq = !!b[10];
    if (hpd != c->hpd_asserted[port] || irq || force_hpd) {
        c->hpd_notifies[port]++;
        c->hpd_asserted[port] = hpd;
    }
    return 0;
}

static void typec_process(struct typec_ctx *c, const uint8_t *b) {
    unsigned port = b[4];
    memcpy(c->state[port], b, 11);
    c->state_valid[port] = true;
    int rc = 0;
    if (c->routing_ready && !c->suspended) {
        rc = typec_apply(c, b, false);
        if (!rc) c->state[port][10] = 0;
    }
    if (rc == 0) c->acks++;
}

static void typec_complete(struct typec_ctx *c) {
    c->suspended = false;
    if (!c->routing_ready) return;
    for (int i = 0; i < 2; i++) {
        if (!c->state_valid[i]) continue;
        if (typec_apply(c, c->state[i], true) == 0)
            c->state[i][10] = 0;
    }
}

/* 4. f_fs ep0req dequeue tracking model */
struct ffs_sim {
    unsigned long flags;
    int dequeues;
};
#define EP0_QUEUED (1UL << 2)

static void ffs_queue(struct ffs_sim *f, bool complete_now) {
    f->flags |= EP0_QUEUED;
    if (complete_now)
        f->flags &= ~EP0_QUEUED;
}
static void ffs_unbind(struct ffs_sim *f) {
    if (f->flags & EP0_QUEUED) {
        f->flags &= ~EP0_QUEUED;
        f->dequeues++;
    }
}

/* 5. SC8180X MDSS_GDSC collapse + DPU MDP_DP_PHY_INTF_SEL (0x0ae01460) model */
#define SC8180X_DP_PHY_INTF_SEL_VAL 0x00000451u
struct mdss_sim {
    int gdsc_children;
    bool gdsc_on;
    uint32_t mdp_dp_phy_intf_sel;
    bool dpu_hw_mdp_ready;
    bool dpu_rpm_active;
    int dpu_resumes;
};

static void mdss_child_get(struct mdss_sim *m) {
    if (m->gdsc_children++ == 0)
        m->gdsc_on = true;
}

static void mdss_child_put(struct mdss_sim *m) {
    assert(m->gdsc_children > 0);
    if (--m->gdsc_children == 0) {
        m->gdsc_on = false;
        m->mdp_dp_phy_intf_sel = 0; /* GDSC power-off wipes DPU_TOP */
    }
}

static void mdss_s2idle_noirq_cycle(struct mdss_sim *m) {
    /* genpd_suspend_noirq collapses MDSS_GDSC and wipes DPU_TOP */
    m->gdsc_on = false;
    m->mdp_dp_phy_intf_sel = 0;
    if (m->gdsc_children > 0)
        m->gdsc_on = true;
}

static void dpu_setup_dp_phy_intf_sel(struct mdss_sim *m) {
    if (!m->dpu_hw_mdp_ready)
        return;
    assert(m->gdsc_on);
    m->mdp_dp_phy_intf_sel = SC8180X_DP_PHY_INTF_SEL_VAL;
}

static void dpu_rpm_get(struct mdss_sim *m) {
    if (!m->dpu_rpm_active) {
        mdss_child_get(m);
        m->dpu_rpm_active = true;
        m->dpu_resumes++;
        dpu_setup_dp_phy_intf_sel(m);
    }
}

static void dpu_rpm_put(struct mdss_sim *m) {
    if (m->dpu_rpm_active) {
        m->dpu_rpm_active = false;
        mdss_child_put(m);
    }
}

static void dp_ensure_kms_phy_sel(struct mdss_sim *m, bool is_edp) {
    if (is_edp)
        return;
    bool was_active = m->dpu_rpm_active;
    dpu_rpm_get(m);
    if (!was_active)
        dpu_rpm_put(m);
}

static int dp_aux_xfer_sim(const struct mdss_sim *m, bool is_edp) {
    if (!m->gdsc_on)
        return -110;
    if (!is_edp && m->mdp_dp_phy_intf_sel != SC8180X_DP_PHY_INTF_SEL_VAL)
        return -110;
    return 0;
}

int main(void) {
    /* Verify 1: External HPD AUX reset fires when connected, skips when unplugged */
    struct aux_hw aux = { .hpd_ctrl = 0, .hpd_status = 0, .trans_ctrl = 0x200, .enable_xfers = true };
    aux_on_timeout(&aux);
    assert(aux.resets == 1 && aux.trans_ctrl == 0);
    aux.enable_xfers = false;
    aux.trans_ctrl = 0x200;
    aux_on_timeout(&aux);
    assert(aux.resets == 1);
    /* Internal HPD eDP uses hardware register */
    struct aux_hw edp = { .hpd_ctrl = 1, .hpd_status = 0x40000000, .trans_ctrl = 0x200, .enable_xfers = false };
    aux_on_timeout(&edp);
    assert(edp.resets == 1);

    /* Verify 2: Failed s2idle atomic_enable stays ST_DISPLAY_OFF with zero leaks, then recovers */
    struct dp_fsm dp = { .state = ST_DISPLAY_OFF, .link_ready = true, .pm_refs = 1 };
    dp_atomic_enable(&dp, -110);
    assert(dp.state == ST_DISPLAY_OFF && !dp.power_on && !dp.phy_initted && !dp.link_clks_on && !dp.stream_clks_on);
    assert(dp.pm_refs == 2);
    /* spx_complete replays connected -> recovers to ST_MAINLINK_READY and emits HPD uevent */
    dp_hpd_notify(&dp, true, 0);
    assert(dp.state == ST_MAINLINK_READY && dp.phy_initted && dp.link_clks_on && dp.hpd_events == 1);
    /* If HWC disables before re-enabling, ST_MAINLINK_READY tears down cleanly */
    dp_atomic_post_disable(&dp);
    assert(dp.state == ST_DISPLAY_OFF && !dp.phy_initted && !dp.link_clks_on && dp.pm_refs == 1);
    /* Re-enable succeeds cleanly */
    dp_atomic_enable(&dp, 0);
    assert(dp.state == ST_CONNECTED && dp.power_on && dp.phy_initted && dp.link_clks_on && dp.stream_clks_on && dp.pm_refs == 2);
    /* Subsequent IRQ_HPD dispatches EV_IRQ_HPD_INT */
    dp_hpd_notify(&dp, true, 0);
    assert(dp.irq_hpd_events == 1);

    /* Verify 2b: Unplug after failed atomic_enable -> atomic_post_disable -> re-plug preserves PM refs */
    struct dp_fsm dp2 = { .state = ST_DISPLAY_OFF, .link_ready = true, .pm_refs = 1 };
    dp_atomic_enable(&dp2, -110);
    assert(dp2.state == ST_DISPLAY_OFF && !dp2.power_on && dp2.pm_refs == 2);
    /* Unplug before HWC disables CRTC */
    dp_hpd_notify(&dp2, false, 0);
    assert(dp2.state == ST_DISCONNECTED && !dp2.link_ready && dp2.pm_refs == 1);
    /* HWC disables CRTC: must preserve ST_DISCONNECTED (not overwrite with ST_DISPLAY_OFF) */
    dp_atomic_post_disable(&dp2);
    assert(dp2.state == ST_DISCONNECTED && dp2.pm_refs == 0);
    /* Next plug acquires normal plug PM ref (0 -> 1) and reaches ST_MAINLINK_READY */
    dp_hpd_notify(&dp2, true, 0);
    assert(dp2.state == ST_MAINLINK_READY && dp2.link_ready && dp2.pm_refs == 1);
    /* Unplug balances back to 0 without underflow */
    dp_hpd_notify(&dp2, false, 0);
    assert(dp2.state == ST_DISCONNECTED && dp2.pm_refs == 0);

    /* Verify 2c: Unplug after ST_MAINLINK_READY recovery -> atomic_post_disable -> re-plug */
    struct dp_fsm dp3 = { .state = ST_DISPLAY_OFF, .link_ready = true, .pm_refs = 1 };
    dp_atomic_enable(&dp3, -110);
    dp_hpd_notify(&dp3, true, 0);
    assert(dp3.state == ST_MAINLINK_READY && dp3.pm_refs == 2);
    dp_hpd_notify(&dp3, false, 0);
    assert(dp3.state == ST_DISCONNECTED && !dp3.phy_initted && !dp3.link_clks_on && dp3.pm_refs == 1);
    dp_atomic_post_disable(&dp3);
    assert(dp3.state == ST_DISCONNECTED && dp3.pm_refs == 0);
    dp_hpd_notify(&dp3, true, 0);
    assert(dp3.state == ST_MAINLINK_READY && dp3.pm_refs == 1);

    /* Verify 3: spx-typec caches+ACKs USC seq=29 during suspend, replays on complete, respects boot_storage_ports */
    struct typec_ctx tc = { .routing_ready = true, .suspended = true, .boot_storage_ports = 1u << 0 };
    uint8_t ev0[11] = {0,0,0,0, 0, 1, 3, 0, 10, 1, 1};
    uint8_t ev1[11] = {29,0,0,0, 1, 1, 3, 0, 10, 1, 1};
    typec_process(&tc, ev0);
    typec_process(&tc, ev1);
    assert(tc.acks == 2 && tc.applies[0] == 0 && tc.applies[1] == 0);
    assert(tc.state[1][10] == 1);
    typec_complete(&tc);
    assert(!tc.suspended && tc.applies[0] == 0 && tc.applies[1] == 1 && tc.hpd_notifies[1] == 1);
    assert(tc.state[1][10] == 0);
    /* Benign repeat with irq=0 does not re-notify; irq=1 does */
    ev1[10] = 0;
    typec_process(&tc, ev1);
    assert(tc.hpd_notifies[1] == 1);
    ev1[10] = 1;
    typec_process(&tc, ev1);
    assert(tc.hpd_notifies[1] == 2 && tc.state[1][10] == 0);

    /* Verify 4: f_fs only dequeues ep0req when queued */
    struct ffs_sim ffs = {0};
    ffs_unbind(&ffs);
    assert(ffs.dequeues == 0);
    ffs_queue(&ffs, true); /* completed/drained during suspend */
    ffs_unbind(&ffs);
    assert(ffs.dequeues == 0);
    ffs_queue(&ffs, false); /* still queued in-flight */
    ffs_unbind(&ffs);
    assert(ffs.dequeues == 1);
    ffs_unbind(&ffs);
    assert(ffs.dequeues == 1);

    /* Verify 5: SC8180X MDP_DP_PHY_INTF_SEL across cold boot, screen-on s2idle,
     * screen-off s2idle, and screen-off hotplug after idle MDSS_GDSC collapse */
    struct mdss_sim mdss = {0};
    /* 5a: Early dpu_kms_hw_init pm_runtime_resume_and_get before hw_mdp init */
    dpu_rpm_get(&mdss);
    assert(mdss.mdp_dp_phy_intf_sel == 0 && mdss.dpu_resumes == 1);
    mdss.dpu_hw_mdp_ready = true;
    dpu_setup_dp_phy_intf_sel(&mdss);
    assert(mdss.mdp_dp_phy_intf_sel == SC8180X_DP_PHY_INTF_SEL_VAL);
    /* External DP plugs in at boot (holds 1 MDSS child ref) */
    mdss_child_get(&mdss);
    assert(dp_aux_xfer_sim(&mdss, false) == 0);

    /* 5b: Screen-on s2idle -> prepare suspends DPU -> noirq wipes GDSC ->
     * complete enable_commit resumes DPU before atomic_enable AUX */
    dpu_rpm_put(&mdss); /* msm_kms_pm_prepare disables CRTCs */
    mdss_s2idle_noirq_cycle(&mdss);
    assert(mdss.mdp_dp_phy_intf_sel == 0);
    assert(dp_aux_xfer_sim(&mdss, false) == -110); /* broken without fix */
    dpu_rpm_get(&mdss); /* msm_atomic_commit_tail -> enable_commit */
    assert(mdss.mdp_dp_phy_intf_sel == SC8180X_DP_PHY_INTF_SEL_VAL);
    assert(dp_aux_xfer_sim(&mdss, false) == 0);

    /* 5c: Screen-off s2idle -> DPU already suspended -> noirq wipes GDSC ->
     * spx_complete HPD plug transiently wakes DPU while DP holds MDSS_GDSC */
    dpu_rpm_put(&mdss); /* screen blanked before s2idle */
    mdss_s2idle_noirq_cycle(&mdss);
    assert(!mdss.dpu_rpm_active && mdss.mdp_dp_phy_intf_sel == 0);
    dp_ensure_kms_phy_sel(&mdss, false);
    assert(!mdss.dpu_rpm_active && mdss.gdsc_on);
    assert(mdss.mdp_dp_phy_intf_sel == SC8180X_DP_PHY_INTF_SEL_VAL);
    assert(dp_aux_xfer_sim(&mdss, false) == 0);

    /* 5d: Cable unplugged + screen off -> MDSS_GDSC collapses -> cable hot-plugged
     * while screen stays off -> ensure_kms_phy_sel restores 0x451 */
    mdss_child_put(&mdss); /* unplug drops last MDSS child */
    assert(!mdss.gdsc_on && mdss.mdp_dp_phy_intf_sel == 0);
    mdss_child_get(&mdss); /* eDP check does not wake DPU */
    dp_ensure_kms_phy_sel(&mdss, true);
    assert(mdss.mdp_dp_phy_intf_sel == 0 && dp_aux_xfer_sim(&mdss, true) == 0);
    dp_ensure_kms_phy_sel(&mdss, false); /* external DP plug wakes+puts DPU */
    assert(!mdss.dpu_rpm_active && mdss.mdp_dp_phy_intf_sel == SC8180X_DP_PHY_INTF_SEL_VAL);
    assert(dp_aux_xfer_sim(&mdss, false) == 0);

    return 0;
}
"""
        with tempfile.TemporaryDirectory() as td:
            c_path = Path(td) / "harness.c"
            bin_path = Path(td) / "harness"
            c_path.write_text(harness)
            subprocess.run(
                [
                    gcc,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=address,undefined",
                    str(c_path),
                    "-o",
                    str(bin_path),
                ],
                check=True,
            )
            subprocess.run([str(bin_path)], check=True)

    def test_portability_no_workstation_paths_or_device_ids(self):
        home_re = re.compile(r"/home/[a-zA-Z0-9._-]+/")
        files = [
            *(PATCHES_DIR / p for p in OWNED_PATCHES),
            ROOT / "probe/typec/spx-typec.c",
            ROOT / "probe/typec/spx-ucsi-transport.c",
            ROOT / "scripts/build-spx-typec.py",
            ROOT / "scripts/full_image.py",
            ROOT / "scripts/full_image_typec.py",
            ROOT / "tests/test_full_image_typec.py",
            Path(__file__),
        ]
        for path in files:
            text = path.read_text()
            self.assertIsNone(
                home_re.search(text),
                f"Hardcoded workstation home path found in {path}",
            )


if __name__ == "__main__":
    unittest.main()
