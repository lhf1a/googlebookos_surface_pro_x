#!/usr/bin/env python3
"""Host ASan/UBSan unit tests and KSU zip verification for sq2-game-opt."""
import importlib.util
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = ROOT / 'probe' / 'sq2-game-opt'
WT_POLICY_CONF = ROOT / 'configs' / 'sq2-vulkan-policy.conf'
if not WT_POLICY_CONF.is_file():
    WT_POLICY_CONF = ROOT / 'out' / 'wt-hybrid-20261113' / 'configs' / 'sq2-vulkan-policy.conf'

HOST_RUNNER_C = r'''
#include "common_policy.h"
#include "vk_swapchain_hook.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void write_text(const char *path, const char *text) {
    FILE *fp = fopen(path, "w");
    assert(fp != NULL);
    fputs(text, fp);
    fclose(fp);
}

static void read_text(const char *path, char *out, size_t out_sz) {
    FILE *fp = fopen(path, "r");
    assert(fp != NULL);
    size_t n = fread(out, 1, out_sz - 1, fp);
    out[n] = '\0';
    fclose(fp);
}

static VkPresentModeKHR g_seen_modes[4];
static int g_call_count = 0;
static int g_reject_mailbox = 0;

static VkResult fake_vkCreateSwapchainKHR(
    VkDevice device,
    const VkSwapchainCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkSwapchainKHR *pSwapchain) {
    (void)device;
    (void)pAllocator;
    assert(g_call_count < 4);
    g_seen_modes[g_call_count++] = pCreateInfo->presentMode;
    if (g_reject_mailbox && pCreateInfo->presentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pSwapchain = (VkSwapchainKHR)0x1234ULL;
    return VK_SUCCESS;
}

static int g_last_egl_interval = -99;
static int fake_eglSwapInterval(void *dpy, int interval) {
    (void)dpy;
    g_last_egl_interval = interval;
    return 1;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    const char *work = argv[1];
    char path[512];

    /* 1. Config parsing */
    struct sq2_opt_config cfg;
    sq2_opt_config_parse_text(
        "# comment\n"
        "enabled=1\n"
        "boost_cpu=true\n"
        "boost_gpu=false\n"
        "pin_threads=1\n"
        "mailbox_present_mode=1\n"
        "egl_swap_interval_zero=0\n"
        "poll_interval_ms=50\n"
        "extra_apps=com.kurogame.*, com.custom.game\n"
        "exclude_apps=com.example.bench\n",
        &cfg);
    assert(cfg.enabled == 1);
    assert(cfg.boost_cpu == 1);
    assert(cfg.boost_gpu == 0);
    assert(cfg.pin_threads == 1);
    assert(cfg.mailbox_present_mode == 1);
    assert(cfg.egl_swap_interval_zero == 0);
    assert(cfg.poll_interval_ms == 100); /* clamped >= 100 */
    assert(cfg.extra_count == 2);
    assert(strcmp(cfg.extra_apps[0], "com.kurogame.*") == 0);
    assert(strcmp(cfg.extra_apps[1], "com.custom.game") == 0);
    assert(cfg.exclude_count == 1);

    /* 2. Pattern & package name helpers */
    char base_pkg[64];
    sq2_base_package_name("com.kurogame.mingchao:UE4Service", base_pkg, sizeof(base_pkg));
    assert(strcmp(base_pkg, "com.kurogame.mingchao") == 0);
    assert(sq2_match_pattern("com.kurogame.*", "com.kurogame.mingchao") == 1);
    assert(sq2_match_pattern("com.kurogame.*", "com.other.app") == 0);
    assert(sq2_match_pattern("com.custom.game", "com.custom.game") == 1);
    assert(sq2_match_pattern("com.custom.game", "com.custom.game2") == 0);

    /* 3. Maps & soname engine detection */
    assert(sq2_classify_soname("/data/app/~~abc/com.ue-1/lib/arm64/libUE4.so") == SQ2_ENGINE_UNREAL);
    assert(sq2_classify_soname("/data/app/base.apk!lib/arm64-v8a/libUnreal.so") == SQ2_ENGINE_UNREAL);
    assert(sq2_classify_soname("libUE5.so") == SQ2_ENGINE_UNREAL);
    assert(sq2_classify_soname("/data/app/libunity.so") == SQ2_ENGINE_UNITY);
    assert(sq2_classify_soname("/data/app/libil2cpp.so") == SQ2_ENGINE_UNITY);
    assert(sq2_classify_soname("/system/lib64/libc.so") == SQ2_ENGINE_NONE);

    assert(sq2_detect_engine_from_maps_text(
        "7000-8000 r-xp 0000 00:00 0 /data/app/foo/lib/arm64/libUnreal.so\n") == SQ2_ENGINE_UNREAL);
    assert(sq2_detect_engine_from_maps_text(
        "7000-8000 r-xp 0000 00:00 0 /data/app/bar/base.apk!lib/arm64-v8a/libunity.so\n") == SQ2_ENGINE_UNITY);

    /* 4. Real ZIP APK central-directory scan & /data/app tree classification */
    snprintf(path, sizeof(path), "%s/ue.apk", work);
    assert(sq2_detect_engine_in_apk_file(path) == SQ2_ENGINE_UNREAL);
    snprintf(path, sizeof(path), "%s/unity.apk", work);
    assert(sq2_detect_engine_in_apk_file(path) == SQ2_ENGINE_UNITY);
    snprintf(path, sizeof(path), "%s/plain.apk", work);
    assert(sq2_detect_engine_in_apk_file(path) == SQ2_ENGINE_NONE);

    char data_app[512];
    snprintf(data_app, sizeof(data_app), "%s/data_app", work);
    assert(sq2_classify_app(&cfg, "com.test.uegame:main", NULL, NULL, data_app) == SQ2_ENGINE_UNREAL);
    assert(sq2_classify_app(&cfg, "com.test.unitygame", NULL, NULL, data_app) == SQ2_ENGINE_UNITY);
    assert(sq2_classify_app(&cfg, "com.kurogame.mingchao", NULL, NULL, data_app) == SQ2_ENGINE_EXTRA);
    assert(sq2_classify_app(&cfg, "com.example.bench", NULL, NULL, data_app) == SQ2_ENGINE_NONE);

    /* 5. Thread pinning classifier */
    assert(sq2_thread_should_pin_big(SQ2_ENGINE_UNREAL, "GameThread") == 1);
    assert(sq2_thread_should_pin_big(SQ2_ENGINE_UNREAL, "RenderThread 0") == 1);
    assert(sq2_thread_should_pin_big(SQ2_ENGINE_UNREAL, "RHIThread") == 1);
    assert(sq2_thread_should_pin_big(SQ2_ENGINE_UNREAL, "AudioThread") == 1);
    assert(sq2_thread_should_pin_big(SQ2_ENGINE_UNREAL, "AsyncLoadingTh") == 0);
    assert(sq2_thread_should_pin_big(SQ2_ENGINE_UNITY, "UnityMain") == 1);
    assert(sq2_thread_should_pin_big(SQ2_ENGINE_UNITY, "UnityGfxDeviceW") == 1);
    assert(sq2_thread_should_pin_big(SQ2_ENGINE_UNITY, "Job.Worker 0") == 0);
    assert(sq2_thread_should_pin_big(SQ2_ENGINE_EXTRA, "GameThread") == 1);
    assert(sq2_thread_should_pin_big(SQ2_ENGINE_EXTRA, "UnityMain") == 1);
    assert(sq2_thread_should_pin_big(SQ2_ENGINE_NONE, "GameThread") == 0);

    /* 6. Dynamic highest frequency & screen power state */
    char maxf[32];
    assert(sq2_find_highest_freq_str("661000000 525000000 700000000 305000000", maxf, sizeof(maxf)) == 0);
    assert(strcmp(maxf, "700000000") == 0);

    char sysfs[512];
    snprintf(sysfs, sizeof(sysfs), "%s/sys", work);
    assert(sq2_is_screen_on(sysfs) == 1);
    snprintf(path, sizeof(path), "%s/sys/class/backlight/panel0/brightness", work);
    write_text(path, "0\n");
    assert(sq2_is_screen_on(sysfs) == 0);
    write_text(path, "1200\n");
    assert(sq2_is_screen_on(sysfs) == 1);
    snprintf(path, sizeof(path), "%s/sys/class/backlight/panel0/bl_power", work);
    write_text(path, "4\n");
    assert(sq2_is_screen_on(sysfs) == 0);
    write_text(path, "0\n");
    assert(sq2_is_screen_on(sysfs) == 1);

    /* 7. DVFS boost engage and restore */
    struct sq2_dvfs_state dvfs;
    memset(&dvfs, 0, sizeof(dvfs));
    assert(sq2_dvfs_boost_engage(&dvfs, sysfs, 1, 1) == 0);
    char val[128];
    snprintf(path, sizeof(path), "%s/sys/devices/system/cpu/cpufreq/policy4/scaling_governor", work);
    read_text(path, val, sizeof(val));
    assert(strcmp(val, "performance") == 0);
    snprintf(path, sizeof(path), "%s/sys/devices/system/cpu/cpufreq/policy4/scaling_min_freq", work);
    read_text(path, val, sizeof(val));
    assert(strcmp(val, "3148800") == 0);
    snprintf(path, sizeof(path), "%s/sys/class/devfreq/2c00000.gpu/max_freq", work);
    read_text(path, val, sizeof(val));
    assert(strcmp(val, "700000000") == 0);
    snprintf(path, sizeof(path), "%s/sys/class/devfreq/2c00000.gpu/min_freq", work);
    read_text(path, val, sizeof(val));
    assert(strcmp(val, "700000000") == 0);
    snprintf(path, sizeof(path), "%s/sys/class/devfreq/2c00000.gpu/governor", work);
    read_text(path, val, sizeof(val));
    assert(strcmp(val, "performance") == 0);

    assert(sq2_dvfs_boost_restore(&dvfs) == 0);
    snprintf(path, sizeof(path), "%s/sys/devices/system/cpu/cpufreq/policy4/scaling_governor", work);
    read_text(path, val, sizeof(val));
    assert(strcmp(val, "schedutil") == 0);
    snprintf(path, sizeof(path), "%s/sys/devices/system/cpu/cpufreq/policy4/scaling_min_freq", work);
    read_text(path, val, sizeof(val));
    assert(strcmp(val, "691200") == 0);
    snprintf(path, sizeof(path), "%s/sys/class/devfreq/2c00000.gpu/max_freq", work);
    read_text(path, val, sizeof(val));
    assert(strcmp(val, "661000000") == 0);
    snprintf(path, sizeof(path), "%s/sys/class/devfreq/2c00000.gpu/min_freq", work);
    read_text(path, val, sizeof(val));
    assert(strcmp(val, "205000000") == 0);
    snprintf(path, sizeof(path), "%s/sys/class/devfreq/2c00000.gpu/governor", work);
    read_text(path, val, sizeof(val));
    assert(strcmp(val, "msm-adreno-tz") == 0);

    /* 8. Vulkan MAILBOX override + fallback on error + eglSwapInterval=0 */
    VkSwapchainCreateInfoKHR ci = {0};
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    VkSwapchainKHR sc = 0;
    int used_mb = 0, fell_back = 0;

    g_call_count = 0;
    g_reject_mailbox = 0;
    assert(sq2_apply_mailbox_create_swapchain(
        fake_vkCreateSwapchainKHR, 1, NULL, &ci, NULL, &sc, &used_mb, &fell_back) == VK_SUCCESS);
    assert(g_call_count == 1);
    assert(g_seen_modes[0] == VK_PRESENT_MODE_MAILBOX_KHR);
    assert(used_mb == 1 && fell_back == 0 && sc != 0);

    g_call_count = 0;
    g_reject_mailbox = 1;
    sc = 0;
    assert(sq2_apply_mailbox_create_swapchain(
        fake_vkCreateSwapchainKHR, 1, NULL, &ci, NULL, &sc, &used_mb, &fell_back) == VK_SUCCESS);
    assert(g_call_count == 2);
    assert(g_seen_modes[0] == VK_PRESENT_MODE_MAILBOX_KHR);
    assert(g_seen_modes[1] == VK_PRESENT_MODE_FIFO_KHR);
    assert(used_mb == 1 && fell_back == 1 && sc != 0);

    sq2_apply_egl_swap_interval(fake_eglSwapInterval, 1, NULL, 1);
    assert(g_last_egl_interval == 0);
    sq2_apply_egl_swap_interval(fake_eglSwapInterval, 0, NULL, 2);
    assert(g_last_egl_interval == 2);

    /* Adaptive idle/screen-off sleep interval */
    assert(sq2_compute_sleep_ms(&cfg, 1, 1) == 100);
    assert(sq2_compute_sleep_ms(&cfg, 1, 0) == 1500);
    assert(sq2_compute_sleep_ms(&cfg, 0, 0) == 5000);
    assert(sq2_compute_sleep_ms(&cfg, 0, 1) == 5000);
    struct sq2_opt_config cfg_def;
    sq2_opt_config_init_default(&cfg_def);
    assert(sq2_compute_sleep_ms(&cfg_def, 1, 1) == 250);
    assert(sq2_compute_sleep_ms(&cfg_def, 1, 0) == 1500);
    cfg_def.poll_interval_ms = 300;
    assert(sq2_compute_sleep_ms(&cfg_def, 1, 0) == 1800);
    cfg_def.enabled = 0;
    assert(sq2_compute_sleep_ms(&cfg_def, 1, 1) == 5000);

    printf("ALL_HOST_TESTS_OK\n");
    return 0;
}
'''


def make_test_apk(path, entries):
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as zf:
        for name, data in entries.items():
            zf.writestr(name, data)


class Sq2GameOptTests(unittest.TestCase):
    def test_host_asan_ubsan_policy_dvfs_apk_and_swapchain(self):
        host_cc = shutil.which('clang') or shutil.which('gcc')
        self.assertIsNotNone(host_cc, 'Need host clang or gcc')

        with tempfile.TemporaryDirectory(prefix='sq2-opt-test-') as td:
            work = Path(td)

            # Build test APKs and /data/app hierarchy
            make_test_apk(work / 'ue.apk', {
                'AndroidManifest.xml': b'manifest',
                'lib/arm64-v8a/libUE4.so': b'\x7fELF',
            })
            make_test_apk(work / 'unity.apk', {
                'AndroidManifest.xml': b'manifest',
                'lib/arm64-v8a/libunity.so': b'\x7fELF',
            })
            make_test_apk(work / 'plain.apk', {
                'AndroidManifest.xml': b'manifest',
                'lib/arm64-v8a/libapp.so': b'\x7fELF',
            })

            ue_pkg = work / 'data_app' / '~~rand1' / 'com.test.uegame-hash1'
            ue_pkg.mkdir(parents=True)
            shutil.copy2(work / 'ue.apk', ue_pkg / 'base.apk')

            unity_lib = work / 'data_app' / '~~rand2' / 'com.test.unitygame-hash2' / 'lib' / 'arm64'
            unity_lib.mkdir(parents=True)
            (unity_lib / 'libil2cpp.so').write_bytes(b'\x7fELF')

            # Build mocked sysfs
            policy4 = work / 'sys' / 'devices/system/cpu/cpufreq/policy4'
            policy4.mkdir(parents=True)
            (policy4 / 'scaling_governor').write_text('schedutil\n')
            (policy4 / 'scaling_min_freq').write_text('691200\n')
            (policy4 / 'scaling_max_freq').write_text('3148800\n')
            (policy4 / 'scaling_available_frequencies').write_text('691200 2016000 3148800\n')

            gpu = work / 'sys' / 'class/devfreq/2c00000.gpu'
            gpu.mkdir(parents=True)
            (gpu / 'governor').write_text('msm-adreno-tz\n')
            (gpu / 'min_freq').write_text('205000000\n')
            (gpu / 'max_freq').write_text('661000000\n')
            (gpu / 'available_frequencies').write_text('205000000 490000000 661000000 700000000\n')

            bl = work / 'sys' / 'class/backlight/panel0'
            bl.mkdir(parents=True)
            (bl / 'brightness').write_text('1024\n')
            (bl / 'bl_power').write_text('0\n')

            runner_c = work / 'runner.c'
            runner_c.write_text(HOST_RUNNER_C, encoding='utf-8')
            runner_bin = work / 'runner'

            subprocess.check_call([
                host_cc,
                '-std=c11',
                '-O1',
                '-g',
                '-fsanitize=address,undefined',
                '-fno-omit-frame-pointer',
                '-Wall',
                '-Wextra',
                '-Werror',
                f'-I{SRC_DIR}',
                str(SRC_DIR / 'common_policy.c'),
                str(SRC_DIR / 'vk_swapchain_hook.c'),
                str(runner_c),
                '-o',
                str(runner_bin),
            ])

            out = subprocess.check_output([str(runner_bin), str(work)], text=True)
            self.assertIn('ALL_HOST_TESTS_OK', out)

    def test_build_ksu_zip_and_policy_sync(self):
        spec = importlib.util.spec_from_file_location(
            'build_sq2_game_opt', ROOT / 'scripts' / 'build-sq2-game-opt.py'
        )
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)

        with tempfile.TemporaryDirectory(prefix='sq2-opt-zip-') as td:
            out_zip = Path(td) / 'sq2-game-opt.zip'
            built = mod.build_package(out_zip=out_zip)
            self.assertTrue(built.is_file())

            with zipfile.ZipFile(built, 'r') as zf:
                names = set(zf.namelist())
                expected = {
                    'module.prop',
                    'config.conf',
                    'customize.sh',
                    'post-fs-data.sh',
                    'service.sh',
                    'uninstall.sh',
                    'bin/sq2-game-optd',
                    'zygisk/arm64-v8a.so',
                    'system/vendor/etc/sq2-vulkan-policy.conf',
                    'META-INF/com/google/android/update-binary',
                    'META-INF/com/google/android/updater-script',
                }
                self.assertTrue(expected.issubset(names), f'Missing entries: {expected - names}')

                # ELF64 AArch64 sanity checks
                for elf_name in ('bin/sq2-game-optd', 'zygisk/arm64-v8a.so'):
                    raw = zf.read(elf_name)
                    self.assertEqual(raw[:6], b'\x7fELF\x02\x01', f'{elf_name} not ELF64 LE')
                    machine = struct.unpack_from('<H', raw, 18)[0]
                    self.assertEqual(machine, 183, f'{elf_name} not EM_AARCH64')

                # Unix line endings and permissions
                for sh_name in ('customize.sh', 'post-fs-data.sh', 'service.sh', 'uninstall.sh', 'bin/sq2-game-optd'):
                    mode = (zf.getinfo(sh_name).external_attr >> 16) & 0o777
                    self.assertEqual(mode, 0o755, f'{sh_name} mode={oct(mode)}')

                for text_name in ('customize.sh', 'post-fs-data.sh', 'service.sh', 'uninstall.sh', 'module.prop', 'config.conf'):
                    self.assertNotIn(b'\r\n', zf.read(text_name), f'{text_name} has CRLF')

                # Policy conf checks & sync with Dual-Vulkan worktree
                policy_text = zf.read('system/vendor/etc/sq2-vulkan-policy.conf').decode('utf-8')
                self.assertIn('type unity = qcom', policy_text)
                self.assertIn('type unreal = qcom', policy_text)
                self.assertIn('app com.kurogame.* = qcom', policy_text)
                self.assertIn('default = turnip', policy_text)
                if WT_POLICY_CONF.is_file():
                    self.assertEqual(
                        policy_text.strip(),
                        WT_POLICY_CONF.read_text(encoding='utf-8').strip(),
                    )


if __name__ == '__main__':
    unittest.main()
