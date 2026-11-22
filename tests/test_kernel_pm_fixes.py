"""Verification suite for SPX kernel/HAL s2idle & PM subsystem fixes."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
_DEFAULT_SOURCES = Path.home() / "spx-full-image/kernel-20261110-v6-sources"
KSRC = Path(os.environ.get("SPX_KERNEL_SRC", str(_DEFAULT_SOURCES / "kernel")))
EXT_SPI_HID = Path(os.environ.get("SPX_EXTERNAL_SRC", str(_DEFAULT_SOURCES / "external"))) / "spi-hid"

KERNEL_PM_PATCHES = (
    "patches/spx-portable-boot/0003-spx-slpi-fastrpc-freezable-invoke.patch",
    "patches/spx-portable-boot/0004-spx-spi-hid-clear-pending-irq.patch",
    "patches/spx-portable-boot/0005-spx-wcd934x-disable-port-int-on-stop.patch",
    "patches/spx-portable-boot/0006-spx-r8152-freezable-fw-work.patch",
    "patches/spx-portable-boot/0007-spx-timerfd-freezable-resume-work.patch",
    "patches/spx-portable-boot/0013-interconnect-qcom-sc8180x-mm0-keepalive.patch",
    "patches/spx-portable-boot/0014-clk-qcom-gcc-sc8180x-enable-rpm.patch",
    "patches/spx-portable-boot/0015-usb-typec-ucsi-cancel-resume-work-on-suspend.patch",
    "patches/spx-portable-boot/0016-arm64-dts-qcom-sc8180x-spmi-pdc-wake.patch",
)
CAMSS_UNWIND_PATCH = "patches/camera/0007-camss-balanced-s-stream-unwind.patch"
DPU_VSYNC_DEDUP_PATCH = "patches/sm8350-dpu-vsync-dedup.patch"


def load_script(name, rel):
    spec = importlib.util.spec_from_file_location(name, ROOT / rel)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def extract_c_function(source, name):
    match = re.search(
        r"^static [^\n]*\b" + re.escape(name) + r"\([^;{]*?\n\{.*?\n\}\n",
        source,
        re.M | re.S,
    )
    if not match:
        raise ValueError(f"missing C function: {name}")
    return match.group(0)


def apply_patch_to_temp_file(rel_kernel_path, patch_rel, flat=False):
    patch_text = (ROOT / patch_rel).read_text()
    with tempfile.TemporaryDirectory(prefix="spx-kpm-patch-") as td:
        root = Path(td)
        if flat:
            target = root / Path(rel_kernel_path).name
            shutil.copyfile(KSRC / rel_kernel_path, target)
        else:
            rels = re.findall(r"^--- a/(.+)$", patch_text, re.M)
            for rel in rels:
                dst = root / rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(KSRC / rel, dst)
            target = root / rel_kernel_path
        subprocess.run(
            ["patch", "--batch", "--fuzz=0", "-p1", "-i", str(ROOT / patch_rel)],
            cwd=root,
            check=True,
            capture_output=True,
            text=True,
        )
        return target.read_text()


class PatchIntegrityAndPortabilityTests(unittest.TestCase):
    def test_all_new_patches_exist_and_stay_portable(self):
        forbidden = (
            "192.168.",
            "10.0.",
            "1234567",
            "/dev/input/event",
        )
        home_path_re = re.compile(r"/home/[a-zA-Z0-9._-]+/")
        for rel in KERNEL_PM_PATCHES + (CAMSS_UNWIND_PATCH, DPU_VSYNC_DEDUP_PATCH):
            path = ROOT / rel
            self.assertTrue(path.is_file(), f"missing patch: {rel}")
            text = path.read_text()
            self.assertIn("--- a/", text)
            self.assertIn("+++ b/", text)
            self.assertIsNone(home_path_re.search(text), f"hardcoded home path in {rel}")
            for token in forbidden:
                self.assertNotIn(token, text, f"non-portable token {token!r} in {rel}")
        self.assertIsNone(home_path_re.search(Path(__file__).read_text()))

    def test_existing_sm8350_dpu_events_pin_preserved(self):
        cfg = json.loads((ROOT / "configs/sm8350-display-build.json").read_text())
        patch_bytes = (ROOT / cfg["dpu_events"]["patch"]).read_bytes()
        self.assertEqual(
            hashlib.sha256(patch_bytes).hexdigest(),
            cfg["dpu_events"]["patch_sha256"],
        )

    @unittest.skipUnless(KSRC.is_dir(), "requires local 6.18 reference kernel source")
    def test_all_kernel_patches_dry_run_cleanly_zero_fuzz(self):
        for rel in KERNEL_PM_PATCHES:
            res = subprocess.run(
                ["patch", "--dry-run", "--batch", "--fuzz=0", "-p1", "-i", str(ROOT / rel)],
                cwd=KSRC,
                capture_output=True,
                text=True,
            )
            self.assertEqual(res.returncode, 0, f"{rel} failed:\n{res.stdout}\n{res.stderr}")

    @unittest.skipUnless(KSRC.is_dir(), "requires local 6.18 reference kernel source")
    def test_camss_unwind_patch_applies_alongside_sc8180x_patch(self):
        with tempfile.TemporaryDirectory(prefix="spx-camss-dry-") as td:
            out = Path(td)
            for src in (KSRC / "drivers/media/platform/qcom/camss").glob("*.[ch]"):
                shutil.copyfile(src, out / src.name)
            for rel in ("patches/camera/0003-camss-sc8180x-initial.patch", CAMSS_UNWIND_PATCH):
                res = subprocess.run(
                    ["patch", "--batch", "--fuzz=0", "-p1", "-i", str(ROOT / rel)],
                    cwd=out,
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(res.returncode, 0, f"{rel} failed:\n{res.stdout}\n{res.stderr}")
        script_text = (ROOT / "scripts/build-spx-camss.sh").read_text()
        self.assertIn("0007-camss-balanced-s-stream-unwind.patch", script_text)


class SlpiFastrpcFreezableTests(unittest.TestCase):
    def test_build_spx_slpi_source_for_fastrpc_idempotent_and_fail_closed(self):
        slpi = load_script("build_spx_slpi_test", "scripts/build-spx-slpi.py")
        sample = "\tif (kernel) {\n\t} else {\n\t\terr = wait_for_completion_interruptible(&ctx->work);\n\t}\n"
        patched = slpi.source_for_fastrpc(sample)
        self.assertIn("wait_for_completion_state(&ctx->work,", patched)
        self.assertIn("TASK_INTERRUPTIBLE | TASK_FREEZABLE", patched)
        self.assertNotIn("wait_for_completion_interruptible(&ctx->work)", patched)
        self.assertEqual(slpi.source_for_fastrpc(patched), patched)
        with self.assertRaises(ValueError):
            slpi.source_for_fastrpc("unrelated source")

    @unittest.skipUnless(KSRC.is_dir(), "requires local 6.18 reference kernel source")
    def test_fastrpc_patch_and_script_transform_real_source(self):
        slpi = load_script("build_spx_slpi_test2", "scripts/build-spx-slpi.py")
        patched = apply_patch_to_temp_file(
            "drivers/misc/fastrpc.c",
            "patches/spx-portable-boot/0003-spx-slpi-fastrpc-freezable-invoke.patch",
        )
        self.assertIn("wait_for_completion_state(&ctx->work,", patched)
        self.assertIn("TASK_INTERRUPTIBLE | TASK_FREEZABLE", patched)
        self.assertEqual(slpi.source_for_fastrpc(patched), patched)
        raw = (KSRC / "drivers/misc/fastrpc.c").read_text()
        transformed = slpi.source_for_fastrpc(raw)
        self.assertIn("TASK_INTERRUPTIBLE | TASK_FREEZABLE", transformed)


class SpiHidResumeIrqTests(unittest.TestCase):
    @unittest.skipUnless(
        EXT_SPI_HID.is_dir() and KSRC.is_dir(),
        "requires local spi-hid sources",
    )
    def test_external_spi_hid_patch_and_c_harness(self):
        with tempfile.TemporaryDirectory(prefix="spx-spihid-test-") as td:
            root = Path(td)
            v1_copy = root / "v1"
            shutil.copytree(EXT_SPI_HID, v1_copy)
            # Upgrade V1 -> V3 and verify idempotence.
            r1 = subprocess.run(
                ["python3", str(ROOT / "scripts/patch-sq2-spi-pm.py"), str(v1_copy)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(r1.returncode, 0, r1.stderr)
            c_text = (v1_copy / "spi-hid-core.c").read_text()
            self.assertIn("SQ2_SPI_PM_INPUT_RECOVERY_V2", c_text)
            self.assertIn("SQ2_SPI_PM_CLEAR_PENDING_IRQ_V3", c_text)
            self.assertIn("irq_set_irqchip_state(spi->irq, IRQCHIP_STATE_PENDING, false);", c_text)
            self.assertIn("IRQF_NO_AUTOEN", c_text)
            first_bytes = (v1_copy / "spi-hid-core.c").read_bytes()
            r2 = subprocess.run(
                ["python3", str(ROOT / "scripts/patch-sq2-spi-pm.py"), str(v1_copy)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(r2.returncode, 0, r2.stderr)
            self.assertEqual((v1_copy / "spi-hid-core.c").read_bytes(), first_bytes)

            # Run scripts/check-spi-hid-resume.py harness against unpatched V1 vs patched V3!
            checker = load_script("check_spi_hid_resume", "scripts/check-spi-hid-resume.py")
            checker.check(EXT_SPI_HID, root, False)
            checker.check(v1_copy, root, True)

    @unittest.skipUnless(KSRC.is_dir(), "requires local 6.18 reference kernel source")
    def test_pinctrl_msm_set_irqchip_state_host_c_behavior(self):
        patched = apply_patch_to_temp_file(
            "drivers/pinctrl/qcom/pinctrl-msm.c",
            "patches/spx-portable-boot/0004-spx-spi-hid-clear-pending-irq.patch",
        )
        fn_code = extract_c_function(patched, "msm_gpio_irq_set_irqchip_state")
        harness = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum irqchip_irq_state { IRQCHIP_STATE_PENDING = 0, IRQCHIP_STATE_ACTIVE = 1 };
struct msm_pingroup { int id; };
struct msm_pinctrl_soc_data { const struct msm_pingroup *groups; };
struct msm_pinctrl {
    const struct msm_pinctrl_soc_data *soc;
    unsigned long skip_wake_irqs[2];
    int lock;
};
struct gpio_chip { struct msm_pinctrl *pctrl; };
struct irq_data { unsigned long hwirq; struct gpio_chip *gc; void *parent_data; };

static int parent_calls, ack_calls, last_acked_group;
static enum irqchip_irq_state last_parent_which;
static bool last_parent_state;

static struct gpio_chip *irq_data_get_irq_chip_data(struct irq_data *d) { return d->gc; }
static struct msm_pinctrl *gpiochip_get_data(struct gpio_chip *gc) { return gc->pctrl; }
static bool test_bit(unsigned long nr, const unsigned long *addr) {
    return (addr[nr / 64] >> (nr % 64)) & 1UL;
}
static int irq_chip_set_parent_state(struct irq_data *d, enum irqchip_irq_state which, bool state) {
    (void)d; parent_calls++; last_parent_which = which; last_parent_state = state; return 0;
}
#define raw_spin_lock_irqsave(lock, flags) do { (void)(lock); (flags) = 0; } while (0)
#define raw_spin_unlock_irqrestore(lock, flags) do { (void)(lock); (void)(flags); } while (0)
static void msm_ack_intr_status(struct msm_pinctrl *pctrl, const struct msm_pingroup *g) {
    (void)pctrl; ack_calls++; last_acked_group = g->id;
}
""" + fn_code + r"""
int main(void) {
    struct msm_pingroup groups[128];
    for (int i = 0; i < 128; i++) groups[i].id = i;
    struct msm_pinctrl_soc_data soc = { .groups = groups };
    struct msm_pinctrl pctrl = { .soc = &soc, .skip_wake_irqs = { 0, 1UL << (122 - 64) } };
    struct gpio_chip gc = { .pctrl = &pctrl };
    int dummy_parent = 1;

    /* PDC direct-connect GPIO 122 delegates to parent (PDC -> GICv3 GICD_ICPENDR). */
    struct irq_data d_pdc = { .hwirq = 122, .gc = &gc, .parent_data = &dummy_parent };
    assert(msm_gpio_irq_set_irqchip_state(&d_pdc, IRQCHIP_STATE_PENDING, false) == 0);
    assert(parent_calls == 1 && ack_calls == 0 && !last_parent_state);

    /* skip_wake_irqs without parent_data returns -EINVAL safely. */
    struct irq_data d_pdc_noparent = { .hwirq = 122, .gc = &gc, .parent_data = NULL };
    assert(msm_gpio_irq_set_irqchip_state(&d_pdc_noparent, IRQCHIP_STATE_PENDING, false) == -EINVAL);

    /* Non-PDC TLMM GPIO clears TLMM INTR_STATUS when clearing PENDING, rejects set/other. */
    struct irq_data d_tlmm = { .hwirq = 42, .gc = &gc, .parent_data = NULL };
    assert(msm_gpio_irq_set_irqchip_state(&d_tlmm, IRQCHIP_STATE_PENDING, false) == 0);
    assert(ack_calls == 1 && last_acked_group == 42);
    assert(msm_gpio_irq_set_irqchip_state(&d_tlmm, IRQCHIP_STATE_PENDING, true) == -EINVAL);
    assert(msm_gpio_irq_set_irqchip_state(&d_tlmm, IRQCHIP_STATE_ACTIVE, false) == -EINVAL);
    puts("pinctrl-msm irq_set_irqchip_state passed");
    return 0;
}
"""
        with tempfile.TemporaryDirectory(prefix="spx-pinctrl-test-") as td:
            cfile = Path(td) / "test.c"
            binary = Path(td) / "test"
            cfile.write_text(harness)
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                 str(cfile), "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True)


class Wcd934xPortInterruptTests(unittest.TestCase):
    @unittest.skipUnless(KSRC.is_dir(), "requires local 6.18 reference kernel source")
    def test_wcd934x_trigger_and_dapm_mask_and_clear_port_irqs(self):
        patched = apply_patch_to_temp_file(
            "sound/soc/codecs/wcd934x.c",
            "patches/spx-portable-boot/0005-spx-wcd934x-disable-port-int-on-stop.patch",
        )
        fns = "\n".join(
            extract_c_function(patched, name)
            for name in (
                "wcd934x_codec_enable_int_port",
                "wcd934x_codec_disable_int_port",
                "wcd934x_codec_enable_slim",
                "wcd934x_trigger",
            )
        )
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BIT(nr) (1U << (nr))
#define WCD934X_RX_START 16
#define WCD934X_SLIM_PGD_PORT_INT_EN0      0x30
#define WCD934X_SLIM_PGD_PORT_INT_TX_EN0   0x32
#define WCD934X_SLIM_PGD_PORT_INT_CLR_RX_0 0x38
#define WCD934X_SLIM_PGD_PORT_INT_CLR_TX_0 0x3A
#define SNDRV_PCM_TRIGGER_STOP          0
#define SNDRV_PCM_TRIGGER_START         1
#define SNDRV_PCM_TRIGGER_PAUSE_PUSH    3
#define SNDRV_PCM_TRIGGER_PAUSE_RELEASE 4
#define SNDRV_PCM_TRIGGER_SUSPEND       5
#define SNDRV_PCM_TRIGGER_RESUME        6
#define SND_SOC_DAPM_POST_PMU 1
#define SND_SOC_DAPM_POST_PMD 2

struct list_head { struct list_head *next, *prev; };
#define list_for_each_entry(pos, head, member) \
    for (pos = (typeof(pos))((head)->next); &(pos)->member != (head); pos = (typeof(pos))((pos)->member.next))

struct wcd934x_slim_ch { struct list_head list; int port; };
struct slim_stream_config { int dummy; };
struct slim_stream_runtime { int active; };
struct wcd_slim_codec_dai_data {
    struct list_head slim_ch_list;
    struct slim_stream_config sconfig;
    struct slim_stream_runtime *sruntime;
};
struct regmap { unsigned int regs[128]; unsigned int clr_count[128]; };
struct wcd934x_codec { struct regmap *if_regmap; struct wcd_slim_codec_dai_data dai[4]; };
struct device { struct wcd934x_codec *drvdata; };
struct snd_soc_component { struct device *dev; };
struct snd_soc_dapm_context { struct snd_soc_component *comp; };
struct snd_soc_dapm_widget { struct snd_soc_dapm_context *dapm; int shift; };
struct snd_kcontrol { int dummy; };
struct snd_pcm_substream { int dummy; };
struct snd_soc_dai { int id; struct snd_soc_component *component; };

static struct wcd934x_codec *dev_get_drvdata(struct device *dev) { return dev->drvdata; }
static struct wcd934x_codec *snd_soc_component_get_drvdata(struct snd_soc_component *c) { return c->dev->drvdata; }
static struct snd_soc_component *snd_soc_dapm_to_component(struct snd_soc_dapm_context *d) { return d->comp; }

static int regmap_read(struct regmap *rm, unsigned int reg, unsigned int *val) {
    *val = rm->regs[reg];
    return 0;
}
static int regmap_write(struct regmap *rm, unsigned int reg, unsigned int val) {
    if (reg >= WCD934X_SLIM_PGD_PORT_INT_CLR_RX_0 && reg <= WCD934X_SLIM_PGD_PORT_INT_CLR_TX_0 + 1)
        rm->clr_count[reg] += __builtin_popcount(val);
    rm->regs[reg] = val;
    return 0;
}

static bool port_closed_irq_seen = false;
static int slim_stream_prepare(struct slim_stream_runtime *rt, struct slim_stream_config *cfg) {
    (void)rt; (void)cfg; return 0;
}
static int slim_stream_enable(struct slim_stream_runtime *rt) { rt->active = 1; return 0; }
static int slim_stream_unprepare(struct slim_stream_runtime *rt) { (void)rt; return 0; }
static struct regmap *g_rm;
static int slim_stream_disable(struct slim_stream_runtime *rt) {
    rt->active = 0;
    /* Hardware raises PORT_CLOSED when disconnecting ports while INT_EN is still set! */
    if (g_rm->regs[WCD934X_SLIM_PGD_PORT_INT_EN0] & 0x3)
        port_closed_irq_seen = true;
    return 0;
}
""" + fns + r"""
int main(void) {
    struct regmap rm;
    memset(&rm, 0, sizeof(rm));
    g_rm = &rm;
    struct slim_stream_runtime rt = { 0 };
    struct wcd934x_codec wcd;
    memset(&wcd, 0, sizeof(wcd));
    wcd.if_regmap = &rm;
    wcd.dai[0].sruntime = &rt;
    struct wcd934x_slim_ch rx0 = { .port = WCD934X_RX_START + 0 };
    struct wcd934x_slim_ch rx1 = { .port = WCD934X_RX_START + 1 };
    wcd.dai[0].slim_ch_list.next = &rx0.list;
    rx0.list.next = &rx1.list;
    rx1.list.next = &wcd.dai[0].slim_ch_list;

    struct device dev = { .drvdata = &wcd };
    struct snd_soc_component comp = { .dev = &dev };
    struct snd_soc_dapm_context dapm = { .comp = &comp };
    struct snd_soc_dapm_widget w = { .dapm = &dapm, .shift = 0 };
    struct snd_soc_dai dai = { .id = 0, .component = &comp };
    struct snd_pcm_substream sub = { 0 };

    /* Start playback: POST_PMU + TRIGGER_START enables RX port 0/1 interrupts. */
    wcd934x_codec_enable_slim(&w, NULL, SND_SOC_DAPM_POST_PMU);
    assert(rm.regs[WCD934X_SLIM_PGD_PORT_INT_EN0] == 0x3);
    wcd934x_trigger(&sub, SNDRV_PCM_TRIGGER_START, &dai);
    assert(rt.active == 1 && rm.regs[WCD934X_SLIM_PGD_PORT_INT_EN0] == 0x3);

    /* Stop/Suspend: interrupts MUST be masked before slim_stream_disable() so no PORT_CLOSED fires. */
    wcd934x_trigger(&sub, SNDRV_PCM_TRIGGER_STOP, &dai);
    assert(!port_closed_irq_seen);
    assert(rm.regs[WCD934X_SLIM_PGD_PORT_INT_EN0] == 0x0);
    assert(rm.clr_count[WCD934X_SLIM_PGD_PORT_INT_CLR_RX_0] >= 4);

    /* Resume re-enables monitoring; DAPM POST_PMD masks again. */
    wcd934x_trigger(&sub, SNDRV_PCM_TRIGGER_RESUME, &dai);
    assert(rm.regs[WCD934X_SLIM_PGD_PORT_INT_EN0] == 0x3);
    wcd934x_codec_enable_slim(&w, NULL, SND_SOC_DAPM_POST_PMD);
    assert(rm.regs[WCD934X_SLIM_PGD_PORT_INT_EN0] == 0x0);
    puts("wcd934x port-int PM lifecycle passed");
    return 0;
}
"""
        with tempfile.TemporaryDirectory(prefix="spx-wcd-test-") as td:
            cfile = Path(td) / "test.c"
            binary = Path(td) / "test"
            cfile.write_text(harness)
            subprocess.run(
                ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                 "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                 str(cfile), "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True)


class R8152AndTimerfdFreezableTests(unittest.TestCase):
    @unittest.skipUnless(KSRC.is_dir(), "requires local 6.18 reference kernel source")
    def test_r8152_uses_freezable_wq_for_all_hw_phy_work(self):
        patched = apply_patch_to_temp_file(
            "drivers/net/usb/r8152.c",
            "patches/spx-portable-boot/0006-spx-r8152-freezable-fw-work.patch",
        )
        self.assertEqual(patched.count("queue_delayed_work(system_long_wq, &tp->hw_phy_work,"), 0)
        self.assertEqual(patched.count("queue_delayed_work(system_freezable_wq, &tp->hw_phy_work,"), 3)

    @unittest.skipUnless(KSRC.is_dir(), "requires local 6.18 reference kernel source")
    def test_timerfd_resume_uses_freezable_wq(self):
        patched = apply_patch_to_temp_file(
            "fs/timerfd.c",
            "patches/spx-portable-boot/0007-spx-timerfd-freezable-resume-work.patch",
        )
        self.assertNotIn("schedule_work(&timerfd_work);", patched)
        self.assertIn("queue_work(system_freezable_wq, &timerfd_work);", patched)


class ZramConfigTests(unittest.TestCase):
    def test_hybrid_kernel_config_enables_zram_for_mmd(self):
        text = (ROOT / "configs/kernel-6.18-hybrid.config").read_text()
        self.assertNotIn("# CONFIG_ZRAM is not set\n", text)
        for opt in (
            'CONFIG_LOCALVERSION="-sq2-android1"',
            "CONFIG_ZSMALLOC=y",
            "CONFIG_ZRAM=y",
            "CONFIG_ZRAM_BACKEND_LZ4=y",
            "CONFIG_ZRAM_BACKEND_LZO=y",
            "CONFIG_ZRAM_DEF_COMP_LZ4=y",
            'CONFIG_ZRAM_DEF_COMP="lz4"',
            "CONFIG_ZRAM_WRITEBACK=y",
            "CONFIG_ZRAM_TRACK_ENTRY_ACTIME=y",
            "CONFIG_ZRAM_MULTI_COMP=y",
        ):
            self.assertIn(opt + "\n", text)


class Sc8180xS2idleStabilityFixTests(unittest.TestCase):
    @unittest.skipUnless(KSRC.is_dir(), "requires local 6.18 reference kernel source")
    def test_sc8180x_mm0_keepalive_patch(self):
        patched = apply_patch_to_temp_file(
            "drivers/interconnect/qcom/sc8180x.c",
            "patches/spx-portable-boot/0013-interconnect-qcom-sc8180x-mm0-keepalive.patch",
        )
        m = re.search(r"static struct qcom_icc_bcm bcm_mm0 = \{.*?\n\};", patched, re.S)
        self.assertIsNotNone(m)
        self.assertIn(".keepalive = true,", m.group(0))

    @unittest.skipUnless(KSRC.is_dir(), "requires local 6.18 reference kernel source")
    def test_gcc_sc8180x_enable_rpm_patch(self):
        patched = apply_patch_to_temp_file(
            "drivers/clk/qcom/gcc-sc8180x.c",
            "patches/spx-portable-boot/0014-clk-qcom-gcc-sc8180x-enable-rpm.patch",
        )
        m = re.search(r"static const struct qcom_cc_desc gcc_sc8180x_desc = \{.*?\n\};", patched, re.S)
        self.assertIsNotNone(m)
        self.assertIn(".use_rpm = true,", m.group(0))

    @unittest.skipUnless(KSRC.is_dir(), "requires local 6.18 reference kernel source")
    def test_ucsi_suspend_cancels_resume_work_before_connectors(self):
        patched = apply_patch_to_temp_file(
            "drivers/usb/typec/ucsi/ucsi.c",
            "patches/spx-portable-boot/0015-usb-typec-ucsi-cancel-resume-work-on-suspend.patch",
        )
        m = re.search(r"int ucsi_suspend\(struct ucsi \*ucsi\).*?\n\}", patched, re.S)
        self.assertIsNotNone(m)
        body = m.group(0)
        idx_delay = body.index("cancel_delayed_work_sync(&ucsi->work);")
        idx_resume = body.index("cancel_work_sync(&ucsi->resume_work);")
        idx_conn = body.index("if (!ucsi->connector)")
        self.assertLess(idx_delay, idx_resume)
        self.assertLess(idx_resume, idx_conn)

    def test_ucsi_surface_hid_cancels_notify_work_before_and_after_ucsi_suspend(self):
        src = (ROOT / "probe/typec/ucsi_surface_hid.c").read_text()
        fn = extract_c_function(src, "surface_ucsi_hid_suspend")
        first_cancel = fn.index("cancel_work_sync(&ush->notify_work);")
        call_suspend = fn.index("ucsi_suspend(ush->ucsi);")
        second_cancel = fn.index("cancel_work_sync(&ush->notify_work);", call_suspend)
        self.assertLess(first_cancel, call_suspend)
        self.assertLess(call_suspend, second_cancel)

    @unittest.skipUnless(KSRC.is_dir(), "requires local 6.18 reference kernel source")
    def test_sc8180x_dtsi_routes_spmi_periph_irq_via_pdc(self):
        patched = apply_patch_to_temp_file(
            "arch/arm64/boot/dts/qcom/sc8180x.dtsi",
            "patches/spx-portable-boot/0016-arm64-dts-qcom-sc8180x-spmi-pdc-wake.patch",
        )
        m = re.search(r"spmi_bus:\s*spmi@c440000\s*\{.*?\n\t\t\};", patched, re.S)
        self.assertIsNotNone(m)
        node = m.group(0)
        self.assertIn("interrupts-extended = <&pdc 1 IRQ_TYPE_LEVEL_HIGH>;", node)
        self.assertNotIn("interrupts = <GIC_SPI 481", node)

    def test_hybrid_kernel_config_enables_qcom_stats(self):
        text = (ROOT / "configs/kernel-6.18-hybrid.config").read_text()
        self.assertNotIn("# CONFIG_QCOM_STATS is not set\n", text)
        self.assertIn("CONFIG_QCOM_STATS=y\n", text)


class CamssBalancedStreamUnwindTests(unittest.TestCase):
    @unittest.skipUnless(KSRC.is_dir(), "requires local 6.18 reference kernel source")
    def test_camss_video_start_failure_unwinds_and_stop_completes_all_subdevs(self):
        patched = apply_patch_to_temp_file(
            "drivers/media/platform/qcom/camss/camss-video.c",
            CAMSS_UNWIND_PATCH,
            flat=True,
        )
        fns = "\n".join(
            extract_c_function(patched, name)
            for name in (
                "video_stop_subdevs_until",
                "video_start_streaming",
                "video_stop_streaming",
            )
        )
        harness = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>

#define ENOIOCTLCMD 515
#define MEDIA_PAD_FL_SINK 1
#define VB2_BUF_STATE_QUEUED 1
#define VB2_BUF_STATE_ERROR  2

struct media_entity;
struct media_pad { unsigned long flags; struct media_entity *entity; struct media_pad *remote; };
struct media_entity { struct media_pad pads[1]; bool is_subdev; };
struct v4l2_subdev {
    const char *name;
    struct media_entity entity;
    bool s_stream_enabled;
    int fail_start_ret;
    int fail_stop_ret;
    int start_calls;
    int stop_calls;
};
struct camss_video;
struct camss_video_ops { void (*flush_buffers)(struct camss_video *video, int state); };
struct camss_device { int dev; };
struct video_device { struct media_entity entity; };
struct camss_video {
    struct camss_device *camss;
    struct video_device vdev;
    const struct camss_video_ops *ops;
};
struct vb2_queue { struct camss_video *drv_priv; };

static int pipeline_started = 0;
static int pipeline_stop_calls = 0;
static int flush_calls = 0;
static int last_flush_state = 0;
static int s_stream_warn_count = 0;

#define dev_err(dev, ...) ((void)(dev))
static struct camss_video *vb2_get_drv_priv(struct vb2_queue *q) { return q->drv_priv; }
static int video_device_pipeline_alloc_start(struct video_device *vdev) { (void)vdev; pipeline_started++; return 0; }
static void video_device_pipeline_stop(struct video_device *vdev) { (void)vdev; pipeline_started--; pipeline_stop_calls++; }
static int video_check_format(struct camss_video *video) { (void)video; return 0; }
static struct media_pad *media_pad_remote_pad_first(struct media_pad *pad) { return pad->remote; }
static bool is_media_entity_v4l2_subdev(struct media_entity *e) { return e->is_subdev; }
static struct v4l2_subdev *media_entity_to_v4l2_subdev(struct media_entity *e) {
    return (struct v4l2_subdev *)((char *)e - __builtin_offsetof(struct v4l2_subdev, entity));
}

/* Exact Linux 6.18 drivers/media/v4l2-core/v4l2-subdev.c:call_s_stream() check. */
static int call_s_stream(struct v4l2_subdev *sd, int enable) {
    if (sd->s_stream_enabled == !!enable) {
        s_stream_warn_count++;
        return 0;
    }
    int ret = enable ? sd->fail_start_ret : sd->fail_stop_ret;
    if (enable) sd->start_calls++; else sd->stop_calls++;
    if (!enable && ret < 0) {
        ret = 0; /* matches v4l2-subdev.c:491 warning-only disable */
    }
    if (!ret) sd->s_stream_enabled = !!enable;
    return ret;
}
#define v4l2_subdev_call(sd, o, f, arg) call_s_stream((sd), (arg))

static void mock_flush(struct camss_video *v, int state) {
    (void)v; flush_calls++; last_flush_state = state;
}
""" + fns + r"""
int main(void) {
    struct camss_device camss = { 0 };
    struct camss_video_ops ops = { .flush_buffers = mock_flush };
    struct camss_video video = { .camss = &camss, .ops = &ops };
    struct vb2_queue q = { .drv_priv = &video };

    /* Pipeline: video -> vfe -> csid -> csiphy -> sensor */
    struct v4l2_subdev vfe    = { .name = "msm_vfe0_rdi0", .entity = { .is_subdev = true } };
    struct v4l2_subdev csid   = { .name = "msm_csid0",     .entity = { .is_subdev = true } };
    struct v4l2_subdev csiphy = { .name = "msm_csiphy0",   .entity = { .is_subdev = true } };
    struct v4l2_subdev sensor = { .name = "ov5693",        .entity = { .is_subdev = true } };

    video.vdev.entity.pads[0] = (struct media_pad){ MEDIA_PAD_FL_SINK, &video.vdev.entity, &vfe.entity.pads[0] };
    vfe.entity.pads[0]    = (struct media_pad){ MEDIA_PAD_FL_SINK, &vfe.entity,    &csid.entity.pads[0] };
    csid.entity.pads[0]   = (struct media_pad){ MEDIA_PAD_FL_SINK, &csid.entity,   &csiphy.entity.pads[0] };
    csiphy.entity.pads[0] = (struct media_pad){ MEDIA_PAD_FL_SINK, &csiphy.entity, &sensor.entity.pads[0] };
    sensor.entity.pads[0] = (struct media_pad){ 0,                 &sensor.entity, NULL };

    /* 1. Sensor fails s_stream(1) (e.g., I2C/PM resume failure): vfe/csid/csiphy MUST unwind! */
    sensor.fail_start_ret = -EIO;
    assert(video_start_streaming(&q, 1) == -EIO);
    assert(s_stream_warn_count == 0);
    assert(!vfe.s_stream_enabled && !csid.s_stream_enabled && !csiphy.s_stream_enabled && !sensor.s_stream_enabled);
    assert(vfe.stop_calls == 1 && csid.stop_calls == 1 && csiphy.stop_calls == 1 && sensor.stop_calls == 0);
    assert(pipeline_started == 0 && flush_calls == 1 && last_flush_state == VB2_BUF_STATE_QUEUED);

    /* 2. Repeated retries while sensor fails produce ZERO call_s_stream warnings! */
    for (int i = 0; i < 10; i++) {
        assert(video_start_streaming(&q, 1) == -EIO);
    }
    assert(s_stream_warn_count == 0);

    /* 3. Sensor recovers -> clean start -> clean stop even if mid-pipeline subdev errors on stop. */
    sensor.fail_start_ret = 0;
    assert(video_start_streaming(&q, 1) == 0);
    assert(s_stream_warn_count == 0);
    assert(vfe.s_stream_enabled && csid.s_stream_enabled && csiphy.s_stream_enabled && sensor.s_stream_enabled);

    csid.fail_stop_ret = -ETIMEDOUT;
    video_stop_streaming(&q);
    assert(s_stream_warn_count == 0);
    assert(!vfe.s_stream_enabled && !csid.s_stream_enabled && !csiphy.s_stream_enabled && !sensor.s_stream_enabled);
    assert(pipeline_started == 0 && last_flush_state == VB2_BUF_STATE_ERROR);
    puts("camss balanced s_stream unwind passed");
    return 0;
}
"""
        with tempfile.TemporaryDirectory(prefix="spx-camss-unit-") as td:
            cfile = Path(td) / "test.c"
            binary = Path(td) / "test"
            cfile.write_text(harness)
            subprocess.run(
                ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                 "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                 str(cfile), "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True)


class DpuVsyncDedupStateMachineTests(unittest.TestCase):
    def test_vsync_double_registration_reproduced_before_and_eliminated_after_fix(self):
        # Verify patch contains all three required fixes.
        patch_text = (ROOT / DPU_VSYNC_DEDUP_PATCH).read_text()
        self.assertIn("if (!kernel_managed_dpu_ && vsync_handler_count_ > 1)", patch_text)
        self.assertIn("vsync_cookie_ = next_vsync_cookie_.fetch_add(1", patch_text)
        self.assertIn("timestamp <= handler->last_vsync_timestamp_", patch_text)

        cpp_harness = r"""
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <vector>

struct QueuedEvent { unsigned long cookie; };

struct HWEventsModel {
    bool kernel_managed_dpu_ = true;
    bool fixed_ = false;
    bool vsync_enabled_ = false;
    bool vsync_registered_ = false;
    uint32_t vsync_handler_count_ = 0;
    unsigned long vsync_cookie_ = 1;
    int64_t last_vsync_timestamp_ = -1;
    std::atomic<unsigned long> next_cookie_{2};
    std::mutex vsync_mutex_;
    std::vector<QueuedEvent> drm_queue_;
    std::vector<int64_t> delivered_timestamps_;
    int duplicate_count_ = 0;

    void RegisterVSync() { drm_queue_.push_back({vsync_cookie_}); }

    void SetEventState(bool enable) {
        std::lock_guard<std::mutex> lock(vsync_mutex_);
        vsync_enabled_ = enable;
        if (vsync_enabled_ && !vsync_registered_) {
            RegisterVSync();
            vsync_registered_ = true;
        } else if (!vsync_enabled_) {
            if (fixed_ && kernel_managed_dpu_) {
                vsync_cookie_ = next_cookie_.fetch_add(1, std::memory_order_relaxed);
            }
            vsync_registered_ = false;
        }
    }

    void Deliver(int64_t ts) {
        if (!delivered_timestamps_.empty() && delivered_timestamps_.back() == ts)
            duplicate_count_++;
        delivered_timestamps_.push_back(ts);
        vsync_handler_count_++;
    }

    void FireNextVBlank(int64_t ts) {
        auto batch = drm_queue_;
        drm_queue_.clear();
        if (batch.empty()) return;

        vsync_handler_count_ = 0;
        {
            std::lock_guard<std::mutex> lock(vsync_mutex_);
            vsync_registered_ = false;
            if (vsync_enabled_) {
                RegisterVSync();
                vsync_registered_ = true;
            }
        }
        for (const auto &ev : batch) {
            if (fixed_) {
                std::lock_guard<std::mutex> lock(vsync_mutex_);
                if (ev.cookie != vsync_cookie_) continue;
                if (last_vsync_timestamp_ >= 0 && ts <= last_vsync_timestamp_) continue;
                last_vsync_timestamp_ = ts;
            } else {
                if (ev.cookie != vsync_cookie_) continue;
            }
            Deliver(ts);
        }
        if ((!fixed_ || !kernel_managed_dpu_) && vsync_handler_count_ > 1) {
            std::lock_guard<std::mutex> lock(vsync_mutex_);
            vsync_registered_ = false;
            if (vsync_enabled_) {
                RegisterVSync();
                vsync_registered_ = true;
            }
        }
    }
};

int main() {
    /* Before fix: disabling + re-enabling VSYNC within one 16.67ms frame self-perpetuates duplicates forever. */
    HWEventsModel broken;
    broken.fixed_ = false;
    broken.SetEventState(true);
    broken.SetEventState(false);
    broken.SetEventState(true); /* queues 2nd event before 1st fires */
    for (int f = 1; f <= 10; f++)
        broken.FireNextVBlank(f * 16666667LL);
    assert(broken.duplicate_count_ == 10);
    assert(broken.drm_queue_.size() == 2);

    /* After fix: stale cookie dropped, single RegisterVSync per vblank, zero duplicates. */
    HWEventsModel fixed;
    fixed.fixed_ = true;
    fixed.SetEventState(true);
    fixed.SetEventState(false);
    fixed.SetEventState(true);
    for (int f = 1; f <= 10; f++)
        fixed.FireNextVBlank(f * 16666667LL);
    assert(fixed.duplicate_count_ == 0);
    assert(fixed.delivered_timestamps_.size() == 10);
    assert(fixed.drm_queue_.size() == 1);
    std::puts("HWEventsDRM duplicate-VSYNC self-perpetuation fix passed");
    return 0;
}
"""
        with tempfile.TemporaryDirectory(prefix="spx-vsync-test-") as td:
            cpp = Path(td) / "test.cpp"
            binary = Path(td) / "test"
            cpp.write_text(cpp_harness)
            subprocess.run(
                ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                 str(cpp), "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
