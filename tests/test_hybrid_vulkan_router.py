#!/usr/bin/env python3
"""Tests for the SQ2Hyb Dual-Vulkan HAL router ELF and C policy/routing behavior."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / 'scripts'
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

import build_hybrid_vulkan_router  # noqa: E402
from build_mesa_qti_gralloc import ALLOWED_NEEDED  # noqa: E402

ROUTER_C = ROOT / 'probe/graphics/sq2-hybrid-vulkan-router.c'
POLICY_CONF = ROOT / 'configs/sq2-vulkan-policy.conf'

MOCK_BACKEND_C = r'''
#include <stdint.h>
#include <string.h>

#define MAKE_TAG(a, b, c, d) \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

typedef struct hw_module_t {
    uint32_t tag;
    uint16_t module_api_version;
    uint16_t hal_api_version;
    const char *id;
    const char *name;
    const char *author;
    void *methods;
    void *dso;
    uint64_t reserved[32 - 7];
} hw_module_t;

typedef struct hw_module_methods_t {
    int (*open)(const hw_module_t *module, const char *id, void **device);
} hw_module_methods_t;

typedef struct hw_device_t {
    uint32_t tag;
    uint32_t version;
    hw_module_t *module;
    uint64_t reserved[12];
    int (*close)(struct hw_device_t *device);
} hw_device_t;

typedef struct hwvulkan_device_t {
    hw_device_t common;
    void *EnumerateInstanceExtensionProperties;
    void *CreateInstance;
    void *GetInstanceProcAddr;
} hwvulkan_device_t;

static int dummy_fn(void) { return 0; }
static int mock_close(hw_device_t *dev) { (void)dev; return 0; }

extern hw_module_t HMI;

static hwvulkan_device_t g_dev = {
    .common = {
        .tag = MAKE_TAG('H', 'W', 'D', 'T'),
        .version = BACKEND_VERSION,
        .module = &HMI,
        .close = mock_close,
    },
    .EnumerateInstanceExtensionProperties = (void *)dummy_fn,
    .CreateInstance = (void *)dummy_fn,
    .GetInstanceProcAddr = (void *)dummy_fn,
};

static int mock_open(const hw_module_t *module, const char *id, void **device) {
    (void)module;
    if (!id || strcmp(id, "vk0") != 0 || !device) {
        return -22;
    }
    *device = &g_dev;
    return 0;
}

static hw_module_methods_t g_methods = { .open = mock_open };

__attribute__((visibility("default")))
hw_module_t HMI = {
    .tag = MAKE_TAG('H', 'W', 'M', 'T'),
    .module_api_version = 1,
    .hal_api_version = 0x0100,
    .id = MODULE_ID_STR,
    .name = "Mock Vulkan Backend",
    .author = "SQ2 Test",
    .methods = &g_methods,
};
'''

HOST_RUNNER_C = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

enum sq2_vk_backend {
    SQ2_VK_BACKEND_TURNIP = 0,
    SQ2_VK_BACKEND_QCOM = 1,
};

enum sq2_vk_app_type {
    SQ2_VK_TYPE_SYSTEM = 0,
    SQ2_VK_TYPE_SYSTEM_UI = 1,
    SQ2_VK_TYPE_BROWSER = 2,
    SQ2_VK_TYPE_EMULATOR_VK13 = 3,
    SQ2_VK_TYPE_BENCHMARK = 4,
    SQ2_VK_TYPE_UNREAL = 5,
    SQ2_VK_TYPE_UNITY = 6,
    SQ2_VK_TYPE_GENERIC = 7,
};

struct sq2_vk_eval_input {
    uint32_t uid;
    const char *cmdline_arg0;
    const char *const *loaded_sonames;
    size_t soname_count;
    const char *env_driver;
    const char *kernel_cmdline;
    const char *prop_debug_turnip_apps;
    const char *prop_debug_qcom_apps;
    const char *prop_persist_turnip_apps;
    const char *prop_persist_qcom_apps;
    const char *prop_type_override;
    const char *prop_driver;
    int prop_include_system;
    const char *policy_conf_text;
};

struct sq2_vk_decision {
    int backend;
    int app_type;
    int cacheable;
    char package_name[192];
    char reason[64];
};

struct hw_device_t;

typedef struct hw_module_t {
    uint32_t tag;
    uint16_t module_api_version;
    uint16_t hal_api_version;
    const char *id;
    const char *name;
    const char *author;
    struct {
        int (*open)(const struct hw_module_t *module, const char *id,
                    struct hw_device_t **device);
    } *methods;
    void *dso;
    uint64_t reserved[32 - 7];
} hw_module_t;

typedef struct hw_device_t {
    uint32_t tag;
    uint32_t version;
    hw_module_t *module;
    uint64_t reserved[12];
    int (*close)(struct hw_device_t *device);
} hw_device_t;

extern const char *sq2_vk_type_name(int type);
extern int sq2_vk_classify_process(uint32_t uid, const char *cmdline_arg0,
                                   const char *const *loaded_sonames,
                                   size_t soname_count);
extern int sq2_vk_evaluate_policy(const struct sq2_vk_eval_input *in,
                                  struct sq2_vk_decision *out);
extern hw_module_t HMI;

/* Mock Android system property storage supporting >92 char values via callback API. */
struct mock_prop {
    char key[96];
    char val[512];
};
static struct mock_prop g_props[16];

static void set_mock_prop(const char *key, const char *val) {
    for (size_t i = 0; i < sizeof(g_props) / sizeof(g_props[0]); ++i) {
        if (g_props[i].key[0] == '\0' || strcmp(g_props[i].key, key) == 0) {
            snprintf(g_props[i].key, sizeof(g_props[i].key), "%s", key);
            snprintf(g_props[i].val, sizeof(g_props[i].val), "%s", val ? val : "");
            return;
        }
    }
    abort();
}

__attribute__((visibility("default")))
const void *__system_property_find(const char *name) {
    if (!name) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(g_props) / sizeof(g_props[0]); ++i) {
        if (g_props[i].key[0] != '\0' && strcmp(g_props[i].key, name) == 0 &&
            g_props[i].val[0] != '\0') {
            return &g_props[i];
        }
    }
    return NULL;
}

typedef void (*prop_cb_t)(void *cookie, const char *name, const char *value, uint32_t serial);

__attribute__((visibility("default")))
void __system_property_read_callback(const void *pi, prop_cb_t cb, void *cookie) {
    const struct mock_prop *p = (const struct mock_prop *)pi;
    if (p && cb) {
        cb(cookie, p->key, p->val, 1u);
    }
}

static char *read_entire_file(const char *path) {
    FILE *f = fopen(path, "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    assert(sz >= 0);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    assert(buf != NULL);
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

static int wait_child_code(pid_t pid) {
    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status));
    return WEXITSTATUS(status);
}

int main(int argc, char **argv) {
    assert(argc == 5);
    const char *policy_path = argv[1];
    const char *turnip_so = argv[2];
    const char *qti_so = argv[3];
    const char *bad_so = argv[4];
    char *policy_text = read_entire_file(policy_path);

    /* 1. All 8 app types */
    assert(sq2_vk_classify_process(1000, "/system/bin/surfaceflinger", NULL, 0) == SQ2_VK_TYPE_SYSTEM);
    assert(strcmp(sq2_vk_type_name(SQ2_VK_TYPE_SYSTEM), "system") == 0);
    assert(sq2_vk_classify_process(10120, "com.android.systemui", NULL, 0) == SQ2_VK_TYPE_SYSTEM_UI);
    assert(strcmp(sq2_vk_type_name(SQ2_VK_TYPE_SYSTEM_UI), "system_ui") == 0);

    assert(sq2_vk_classify_process(10150, "com.android.chrome:privileged_process0", NULL, 0) == SQ2_VK_TYPE_BROWSER);
    const char *web_libs[] = {"/apex/com.android.art/lib64/libc.so", "/data/app/libwebviewchromium.so"};
    assert(sq2_vk_classify_process(10151, "com.custom.browserish", web_libs, 2) == SQ2_VK_TYPE_BROWSER);

    assert(sq2_vk_classify_process(10160, "com.winlator", NULL, 0) == SQ2_VK_TYPE_EMULATOR_VK13);
    const char *dxvk_libs[] = {"libdxvk_d3d11.so"};
    assert(sq2_vk_classify_process(10161, "com.foo.emu", dxvk_libs, 1) == SQ2_VK_TYPE_EMULATOR_VK13);

    assert(sq2_vk_classify_process(10170, "com.futuremark.dmandroid.application", NULL, 0) == SQ2_VK_TYPE_BENCHMARK);

    const char *ue_libs[] = {"/data/app/libUE4.so"};
    assert(sq2_vk_classify_process(10180, "com.kurogame.mingchao", ue_libs, 1) == SQ2_VK_TYPE_UNREAL);

    const char *unity_libs[] = {"/data/app/libunity.so"};
    assert(sq2_vk_classify_process(10190, "com.miHoYo.Yuanshen", unity_libs, 1) == SQ2_VK_TYPE_UNITY);

    assert(sq2_vk_classify_process(10200, "com.example.plain", NULL, 0) == SQ2_VK_TYPE_GENERIC);
    assert(strcmp(sq2_vk_type_name(SQ2_VK_TYPE_GENERIC), "generic") == 0);

    /* 2. Policy conf defaults and precedence rules */
    struct sq2_vk_decision dec;
    struct sq2_vk_eval_input base = {
        .uid = 10180,
        .cmdline_arg0 = "com.kurogame.mingchao",
        .loaded_sonames = ue_libs,
        .soname_count = 1,
        .kernel_cmdline = "console=ttyMSM0 sq2.vulkan=mesa androidboot.hardware=android-desktop",
        .policy_conf_text = policy_text,
    };
    /* Bootloader token sq2.vulkan=mesa must NOT override conf:app com.kurogame.* = qcom */
    assert(sq2_vk_evaluate_policy(&base, &dec) == 0);
    assert(dec.backend == SQ2_VK_BACKEND_QCOM);
    assert(dec.app_type == SQ2_VK_TYPE_UNREAL);
    assert(dec.cacheable == 1);
    assert(strcmp(dec.reason, "conf:app") == 0);

    /* sq2.rescue=1 beats sq2.vulkan.force=qcom regardless of token order */
    struct sq2_vk_eval_input rescue = base;
    rescue.kernel_cmdline = "sq2.vulkan.force=qcom sq2.rescue=1";
    assert(sq2_vk_evaluate_policy(&rescue, &dec) == 0);
    assert(dec.backend == SQ2_VK_BACKEND_TURNIP);
    assert(strcmp(dec.reason, "cmdline:sq2.rescue=1") == 0);

    /* 3DMark, Chrome, Winlator default to turnip */
    struct sq2_vk_eval_input bm = {
        .uid = 10170,
        .cmdline_arg0 = "com.futuremark.dmandroid.application",
        .policy_conf_text = policy_text,
    };
    assert(sq2_vk_evaluate_policy(&bm, &dec) == 0);
    assert(dec.backend == SQ2_VK_BACKEND_TURNIP);
    assert(strcmp(dec.reason, "conf:type") == 0);

    struct sq2_vk_eval_input chrome = {
        .uid = 10150,
        .cmdline_arg0 = "com.android.chrome:sandboxed_process0",
        .policy_conf_text = policy_text,
    };
    assert(sq2_vk_evaluate_policy(&chrome, &dec) == 0);
    assert(dec.backend == SQ2_VK_BACKEND_TURNIP);
    assert(strcmp(dec.reason, "conf:app") == 0);

    struct sq2_vk_eval_input winlator = {
        .uid = 10160,
        .cmdline_arg0 = "com.winlator",
        .policy_conf_text = policy_text,
    };
    assert(sq2_vk_evaluate_policy(&winlator, &dec) == 0);
    assert(dec.backend == SQ2_VK_BACKEND_TURNIP);
    assert(strcmp(dec.reason, "conf:type") == 0);

    /* debug.turnip_apps overrides conf:app qcom and ties over debug.qcom_apps */
    struct sq2_vk_eval_input dbg_over = base;
    dbg_over.prop_debug_turnip_apps = "com.foo.*,com.kurogame.*";
    dbg_over.prop_debug_qcom_apps = "com.kurogame.mingchao";
    dbg_over.prop_persist_qcom_apps = "com.kurogame.*";
    assert(sq2_vk_evaluate_policy(&dbg_over, &dec) == 0);
    assert(dec.backend == SQ2_VK_BACKEND_TURNIP);
    assert(strcmp(dec.reason, "prop:debug.turnip_apps") == 0);

    /* Per-type property override */
    struct sq2_vk_eval_input type_over = bm;
    type_over.prop_type_override = "qcom";
    assert(sq2_vk_evaluate_policy(&type_over, &dec) == 0);
    assert(dec.backend == SQ2_VK_BACKEND_QCOM);
    assert(strcmp(dec.reason, "prop:type.benchmark") == 0);

    /* Global property guard on system UID */
    struct sq2_vk_eval_input sys_guard = {
        .uid = 1000,
        .cmdline_arg0 = "/system/bin/surfaceflinger",
        .prop_driver = "qcom",
        .prop_include_system = 0,
        .policy_conf_text = policy_text,
    };
    assert(sq2_vk_evaluate_policy(&sys_guard, &dec) == 0);
    assert(dec.backend == SQ2_VK_BACKEND_TURNIP);
    assert(strcmp(dec.reason, "guard:system-on-global-qcom") == 0);

    sys_guard.prop_include_system = 1;
    assert(sq2_vk_evaluate_policy(&sys_guard, &dec) == 0);
    assert(dec.backend == SQ2_VK_BACKEND_QCOM);
    assert(strcmp(dec.reason, "prop:driver") == 0);

    /* Zygote/USAP guard */
    struct sq2_vk_eval_input zyg = {
        .uid = 0,
        .cmdline_arg0 = "zygote64",
        .prop_driver = "qcom",
        .prop_include_system = 1,
        .policy_conf_text = policy_text,
    };
    assert(sq2_vk_evaluate_policy(&zyg, &dec) == 0);
    assert(dec.backend == SQ2_VK_BACKEND_TURNIP);
    assert(dec.cacheable == 0);
    assert(strcmp(dec.reason, "guard:zygote") == 0);

    /* 3. Live HAL open(): >180-char callback property, fork() cache invalidation, -ELOOP, -ENODEV */
    assert(setenv("SQ2_TEST_TURNIP_SO", turnip_so, 1) == 0);
    assert(setenv("SQ2_TEST_QTI_SO", qti_so, 1) == 0);
    assert(setenv("SQ2_TEST_VK_POLICY_PATH", policy_path, 1) == 0);

    char long_list[384];
    snprintf(long_list, sizeof(long_list),
             "com.example.pad1,com.example.pad2,com.example.pad3,"
             "com.example.pad4,com.example.pad5,com.example.pad6,"
             "com.example.pad7,com.example.pad8,com.example.pad9,"
             "com.example.pad10,com.example.pad11,com.example.pad12,"
             "router_test_runner");
    assert(strlen(long_list) > 180);
    set_mock_prop("debug.sq2.vulkan.qcom_apps", long_list);

    hw_device_t *dev = NULL;
    assert(HMI.methods->open(&HMI, "vk0", &dev) == 0);
    assert(dev != NULL);
    assert(dev->version == 0x2222);
    if (dev->close) {
        dev->close(dev);
    }

    /* Fork child: parent PID cached qcom, child PID must re-evaluate to turnip */
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        set_mock_prop("debug.sq2.vulkan.qcom_apps", "");
        set_mock_prop("debug.sq2.vulkan.turnip_apps", "router_test_runner");
        hw_device_t *cdev = NULL;
        int rc = HMI.methods->open(&HMI, "vk0", &cdev);
        _exit((rc == 0 && cdev && cdev->version == 0x1111) ? 0 : 11);
    }
    assert(wait_child_code(pid) == 0);

    /* Fork child: self-recursion guard -> -ELOOP */
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        set_mock_prop("debug.sq2.vulkan.qcom_apps", "");
        set_mock_prop("debug.sq2.vulkan.turnip_apps", "router_test_runner");
        setenv("SQ2_TEST_TURNIP_SO", "librouter_host.so", 1);
        hw_device_t *cdev = NULL;
        int rc = HMI.methods->open(&HMI, "vk0", &cdev);
        _exit((rc == -ELOOP && cdev == NULL) ? 0 : 12);
    }
    assert(wait_child_code(pid) == 0);

    /* Fork child: missing library -> -ENODEV */
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        set_mock_prop("debug.sq2.vulkan.qcom_apps", "");
        set_mock_prop("debug.sq2.vulkan.turnip_apps", "router_test_runner");
        setenv("SQ2_TEST_TURNIP_SO", "/nonexistent/sq2_missing_vulkan.so", 1);
        hw_device_t *cdev = NULL;
        int rc = HMI.methods->open(&HMI, "vk0", &cdev);
        _exit((rc == -ENODEV && cdev == NULL) ? 0 : 13);
    }
    assert(wait_child_code(pid) == 0);

    /* Fork child: invalid HMI id -> -ENODEV */
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        set_mock_prop("debug.sq2.vulkan.qcom_apps", "");
        set_mock_prop("debug.sq2.vulkan.turnip_apps", "router_test_runner");
        setenv("SQ2_TEST_TURNIP_SO", bad_so, 1);
        hw_device_t *cdev = NULL;
        int rc = HMI.methods->open(&HMI, "vk0", &cdev);
        _exit((rc == -ENODEV && cdev == NULL) ? 0 : 14);
    }
    assert(wait_child_code(pid) == 0);

    free(policy_text);
    printf("SQ2_ROUTER_HOST_TESTS_OK\n");
    return 0;
}
'''


class HybridVulkanRouterTest(unittest.TestCase):
    def test_build_aarch64_router_elf(self):
        data = build_hybrid_vulkan_router.build_bytes()
        self.assertGreater(len(data), 4096)
        self.assertEqual(data[:6], b'\x7fELF\x02\x01')
        self.assertEqual(struct.unpack_from('<H', data, 18)[0], 183)

        cc = build_hybrid_vulkan_router.find_android_cc(None)
        readelf = cc.parent / 'llvm-readelf'
        with tempfile.TemporaryDirectory(prefix='sq2-router-elf-') as td:
            so_path = Path(td) / 'vulkan.freedreno.so'
            so_path.write_bytes(data)
            dyn = subprocess.check_output([str(readelf), '-d', str(so_path)], text=True)
            needed = set()
            sonames = set()
            for line in dyn.splitlines():
                if '(NEEDED)' in line and 'Shared library: [' in line:
                    needed.add(line.split('Shared library: [', 1)[1].split(']', 1)[0])
                elif '(SONAME)' in line and 'Library soname: [' in line:
                    sonames.add(line.split('Library soname: [', 1)[1].split(']', 1)[0])
            self.assertEqual(sonames, {'vulkan.freedreno.so'})
            self.assertLessEqual(needed, ALLOWED_NEEDED)
            syms = subprocess.check_output([str(readelf), '--dyn-syms', str(so_path)], text=True)
            for sym in build_hybrid_vulkan_router.REQUIRED_SYMBOLS:
                self.assertTrue(
                    any(line.rstrip().endswith(' ' + sym) for line in syms.splitlines()),
                    f'Missing exported symbol {sym}',
                )

    def test_host_asan_ubsan_policy_and_hal_routing(self):
        host_cc = shutil.which('clang') or shutil.which('gcc')
        self.assertIsNotNone(host_cc, 'Need host clang or gcc for ASan/UBSan tests')

        with tempfile.TemporaryDirectory(prefix='sq2-router-host-') as td:
            tmp = Path(td)
            mock_src = tmp / 'mock_backend.c'
            mock_src.write_text(MOCK_BACKEND_C, encoding='utf-8')
            runner_src = tmp / 'runner.c'
            runner_src.write_text(HOST_RUNNER_C, encoding='utf-8')

            router_so = tmp / 'librouter_host.so'
            turnip_so = tmp / 'mock_turnip.so'
            qti_so = tmp / 'mock_qti.so'
            bad_so = tmp / 'mock_bad.so'
            runner_bin = tmp / 'router_test_runner'

            san_flags = [
                '-std=c11',
                '-O1',
                '-g',
                '-fsanitize=address,undefined',
                '-fno-omit-frame-pointer',
                '-Wall',
                '-Wextra',
                '-Werror',
                '-pthread',
            ]
            subprocess.run(
                [
                    host_cc,
                    *san_flags,
                    '-fPIC',
                    '-shared',
                    '-Wl,-soname,librouter_host.so',
                    str(ROUTER_C),
                    '-ldl',
                    '-o',
                    str(router_so),
                ],
                check=True,
            )
            for out_so, mod_id, ver in (
                (turnip_so, '"vulkan"', '0x1111'),
                (qti_so, '"vulkan"', '0x2222'),
                (bad_so, '"not_vulkan"', '0x9999'),
            ):
                subprocess.run(
                    [
                        host_cc,
                        '-std=c11',
                        '-O1',
                        '-g',
                        '-Wall',
                        '-Wextra',
                        '-Werror',
                        '-fPIC',
                        '-shared',
                        f'-DMODULE_ID_STR={mod_id}',
                        f'-DBACKEND_VERSION={ver}',
                        str(mock_src),
                        '-o',
                        str(out_so),
                    ],
                    check=True,
                )
            subprocess.run(
                [
                    host_cc,
                    *san_flags,
                    '-rdynamic',
                    f'-Wl,-rpath,{tmp}',
                    str(runner_src),
                    str(router_so),
                    '-ldl',
                    '-o',
                    str(runner_bin),
                ],
                check=True,
            )
            env = dict(os.environ)
            env['SQ2_ROUTER_STDERR_LOG'] = '1'
            res = subprocess.run(
                [str(runner_bin), str(POLICY_CONF), str(turnip_so), str(qti_so), str(bad_so)],
                env=env,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                res.returncode,
                0,
                f'Host router test failed (rc={res.returncode})\nSTDOUT:\n{res.stdout}\nSTDERR:\n{res.stderr}',
            )
            self.assertIn('SQ2_ROUTER_HOST_TESTS_OK', res.stdout)


if __name__ == '__main__':
    unittest.main()
