/*
 * Dual-Vulkan HAL router for SQ2Hyb (Mesa Turnip + QTI v77 native policy).
 *
 * Installed at /vendor/lib64/hw/vulkan.freedreno.so (ro.hardware.vulkan=freedreno).
 * Routes per-process in hw_module_methods_t::open() to either:
 *   - /vendor/lib64/hw/vulkan.turnip.so (unpatched hardware-GMEM Mesa Turnip 1.3)
 *   - /vendor/lib64/hw/vulkan.qti.so    (QTI v77 vulkan.qti-policy.so -> vulkan.qti-native.so)
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <link.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/log.h>
#define SQ2_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SQ2VkRouter", __VA_ARGS__)
#define SQ2_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SQ2VkRouter", __VA_ARGS__)
#else
static void sq2_host_log(const char *level, const char *fmt, ...) {
    const char *verbose = getenv("SQ2_ROUTER_STDERR_LOG");
    if (!verbose || strcmp(verbose, "1") != 0) {
        return;
    }
    va_list ap;
    fprintf(stderr, "SQ2VkRouter[%s]: ", level);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
#define SQ2_LOGI(...) sq2_host_log("I", __VA_ARGS__)
#define SQ2_LOGE(...) sq2_host_log("E", __VA_ARGS__)
#endif

#define EXPORT __attribute__((visibility("default")))

/* ---- Binary ABI types matching Android hardware/hardware.h + hwvulkan.h ---- */

#define SQ2_MAKE_TAG(a, b, c, d) \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define SQ2_HARDWARE_MODULE_TAG SQ2_MAKE_TAG('H', 'W', 'M', 'T')
#define SQ2_HARDWARE_DEVICE_TAG SQ2_MAKE_TAG('H', 'W', 'D', 'T')
#define SQ2_HARDWARE_HAL_API_VERSION 0x0100u
#define SQ2_HWVULKAN_MODULE_API_VERSION_0_1 0x0001u
#define SQ2_HWVULKAN_HARDWARE_MODULE_ID "vulkan"
#define SQ2_HWVULKAN_DEVICE_0 "vk0"
#define HAL_MODULE_INFO_SYM HMI

struct sq2_hw_module_t;
struct sq2_hw_device_t;

typedef struct sq2_hw_module_methods_t {
    int (*open)(const struct sq2_hw_module_t *module, const char *id,
                struct sq2_hw_device_t **device);
} sq2_hw_module_methods_t;

typedef struct sq2_hw_module_t {
    uint32_t tag;
    uint16_t module_api_version;
    uint16_t hal_api_version;
    const char *id;
    const char *name;
    const char *author;
    sq2_hw_module_methods_t *methods;
    void *dso;
    uint64_t reserved[32 - 7];
} sq2_hw_module_t;

typedef struct sq2_hw_device_t {
    uint32_t tag;
    uint32_t version;
    struct sq2_hw_module_t *module;
    uint64_t reserved[12];
    int (*close)(struct sq2_hw_device_t *device);
} sq2_hw_device_t;

typedef struct sq2_hwvulkan_module_t {
    sq2_hw_module_t common;
} sq2_hwvulkan_module_t;

typedef struct sq2_hwvulkan_device_t {
    sq2_hw_device_t common;
    void *EnumerateInstanceExtensionProperties;
    void *CreateInstance;
    void *GetInstanceProcAddr;
} sq2_hwvulkan_device_t;

_Static_assert(sizeof(void *) == 8, "Expected LP64");
_Static_assert(sizeof(sq2_hw_module_t) == 248, "hw_module_t size mismatch");
_Static_assert(offsetof(sq2_hw_module_t, id) == 0x08, "hw_module_t.id offset mismatch");
_Static_assert(offsetof(sq2_hw_module_t, methods) == 0x20, "hw_module_t.methods offset mismatch");
_Static_assert(offsetof(sq2_hw_module_t, dso) == 0x28, "hw_module_t.dso offset mismatch");
_Static_assert(sizeof(sq2_hw_device_t) == 120, "hw_device_t size mismatch");
_Static_assert(offsetof(sq2_hw_device_t, close) == 0x70, "hw_device_t.close offset mismatch");
_Static_assert(sizeof(sq2_hwvulkan_device_t) == 144, "hwvulkan_device_t size mismatch");
_Static_assert(offsetof(sq2_hwvulkan_device_t, EnumerateInstanceExtensionProperties) == 120,
               "hwvulkan_device_t.EnumerateInstanceExtensionProperties offset mismatch");
_Static_assert(offsetof(sq2_hwvulkan_device_t, CreateInstance) == 128,
               "hwvulkan_device_t.CreateInstance offset mismatch");
_Static_assert(offsetof(sq2_hwvulkan_device_t, GetInstanceProcAddr) == 136,
               "hwvulkan_device_t.GetInstanceProcAddr offset mismatch");

/* ---- Policy Types & Pure Evaluation API ---- */

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

EXPORT const char *sq2_vk_type_name(int type) {
    switch (type) {
    case SQ2_VK_TYPE_SYSTEM:
        return "system";
    case SQ2_VK_TYPE_SYSTEM_UI:
        return "system_ui";
    case SQ2_VK_TYPE_BROWSER:
        return "browser";
    case SQ2_VK_TYPE_EMULATOR_VK13:
        return "emulator_vk13";
    case SQ2_VK_TYPE_BENCHMARK:
        return "benchmark";
    case SQ2_VK_TYPE_UNREAL:
        return "unreal";
    case SQ2_VK_TYPE_UNITY:
        return "unity";
    case SQ2_VK_TYPE_GENERIC:
    default:
        return "generic";
    }
}

static const char *backend_name(int backend) {
    return backend == SQ2_VK_BACKEND_QCOM ? "qcom" : "turnip";
}

static void extract_proc_and_pkg(const char *cmdline_arg0,
                                 char *proc_out, size_t proc_sz,
                                 char *pkg_out, size_t pkg_sz) {
    const char *raw = cmdline_arg0 ? cmdline_arg0 : "";
    const char *slash = strrchr(raw, '/');
    const char *base = slash ? slash + 1 : raw;

    snprintf(proc_out, proc_sz, "%s", base);
    const char *colon = strchr(base, ':');
    size_t pkg_len = colon ? (size_t)(colon - base) : strlen(base);
    if (pkg_len >= pkg_sz) {
        pkg_len = pkg_sz - 1;
    }
    memcpy(pkg_out, base, pkg_len);
    pkg_out[pkg_len] = '\0';
}

static int ci_starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) {
        return 0;
    }
    while (*prefix) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) {
            return 0;
        }
        ++s;
        ++prefix;
    }
    return 1;
}

static int ci_contains(const char *haystack, const char *needle) {
    if (!haystack || !needle || !*needle) {
        return 0;
    }
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; ++p) {
        size_t i = 0;
        while (i < nlen && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            ++i;
        }
        if (i == nlen) {
            return 1;
        }
    }
    return 0;
}

static const char *soname_base(const char *path) {
    if (!path) {
        return "";
    }
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int match_pattern(const char *pattern, size_t pat_len,
                         const char *pkg, const char *proc) {
    if (!pattern || pat_len == 0) {
        return 0;
    }
    if (pattern[pat_len - 1] == '*') {
        size_t prefix_len = pat_len - 1;
        return (pkg && strncmp(pkg, pattern, prefix_len) == 0) ||
               (proc && strncmp(proc, pattern, prefix_len) == 0);
    }
    return (pkg && strlen(pkg) == pat_len && memcmp(pkg, pattern, pat_len) == 0) ||
           (proc && strlen(proc) == pat_len && memcmp(proc, pattern, pat_len) == 0);
}

static int match_pattern_list(const char *list, const char *pkg, const char *proc) {
    if (!list || !*list) {
        return 0;
    }
    const char *p = list;
    while (*p) {
        while (*p == ',' || *p == ';' || isspace((unsigned char)*p)) {
            ++p;
        }
        if (!*p) {
            break;
        }
        const char *start = p;
        while (*p && *p != ',' && *p != ';' && !isspace((unsigned char)*p)) {
            ++p;
        }
        if (match_pattern(start, (size_t)(p - start), pkg, proc)) {
            return 1;
        }
    }
    return 0;
}

static int parse_backend_token(const char *val, size_t len, int *out_backend) {
    if (!val || len == 0) {
        return 0;
    }
    char buf[32];
    if (len >= sizeof(buf)) {
        return 0;
    }
    for (size_t i = 0; i < len; ++i) {
        buf[i] = (char)tolower((unsigned char)val[i]);
    }
    buf[len] = '\0';
    if (strcmp(buf, "turnip") == 0 || strcmp(buf, "mesa") == 0) {
        *out_backend = SQ2_VK_BACKEND_TURNIP;
        return 1;
    }
    if (strcmp(buf, "qcom") == 0 || strcmp(buf, "qti") == 0 ||
        strcmp(buf, "adreno") == 0 || strcmp(buf, "closed") == 0) {
        *out_backend = SQ2_VK_BACKEND_QCOM;
        return 1;
    }
    return 0;
}

static int parse_backend_str(const char *val, int *out_backend) {
    if (!val) {
        return 0;
    }
    while (isspace((unsigned char)*val)) {
        ++val;
    }
    size_t len = strlen(val);
    while (len > 0 && isspace((unsigned char)val[len - 1])) {
        --len;
    }
    return parse_backend_token(val, len, out_backend);
}

static int is_zygote_process(const char *pkg, const char *proc) {
    static const char *const zygote_names[] = {
        "zygote", "zygote64", "webview_zygote", "usap32", "usap64",
    };
    for (size_t i = 0; i < sizeof(zygote_names) / sizeof(zygote_names[0]); ++i) {
        if ((pkg && strcmp(pkg, zygote_names[i]) == 0) ||
            (proc && strcmp(proc, zygote_names[i]) == 0)) {
            return 1;
        }
    }
    return 0;
}

EXPORT int sq2_vk_classify_process(uint32_t uid, const char *cmdline_arg0,
                                   const char *const *loaded_sonames,
                                   size_t soname_count) {
    char proc[256];
    char pkg[192];
    extract_proc_and_pkg(cmdline_arg0, proc, sizeof(proc), pkg, sizeof(pkg));

    if (strcmp(pkg, "com.android.systemui") == 0) {
        return SQ2_VK_TYPE_SYSTEM_UI;
    }

    uint32_t appid = uid % 100000u;
    if (appid < 10000u) {
        return SQ2_VK_TYPE_SYSTEM;
    }

    static const char *const browser_patterns[] = {
        "com.android.chrome", "com.chrome.*", "org.chromium.*",
        "com.google.android.webview", "com.android.webview",
        "com.sec.android.app.sbrowser*", "org.mozilla.firefox*",
        "com.microsoft.emmx*", "com.brave.browser*", "com.opera.*",
    };
    for (size_t i = 0; i < sizeof(browser_patterns) / sizeof(browser_patterns[0]); ++i) {
        if (match_pattern(browser_patterns[i], strlen(browser_patterns[i]), pkg, proc)) {
            return SQ2_VK_TYPE_BROWSER;
        }
    }
    if (ci_contains(proc, ":sandboxed_process") ||
        ci_contains(proc, ":privileged_process") ||
        ci_contains(pkg, "webview")) {
        return SQ2_VK_TYPE_BROWSER;
    }
    if (loaded_sonames) {
        for (size_t i = 0; i < soname_count; ++i) {
            const char *base = soname_base(loaded_sonames[i]);
            if (ci_starts_with(base, "libwebviewchromium") ||
                ci_starts_with(base, "libmonochrome") ||
                ci_starts_with(base, "libchrome")) {
                return SQ2_VK_TYPE_BROWSER;
            }
        }
    }

    static const char *const emu_patterns[] = {
        "com.winlator*", "org.yuzu.*", "org.dolphinemu.*", "org.ppsspp.*",
        "app.melon*", "xyz.aethersx2.*", "skyline.*",
    };
    for (size_t i = 0; i < sizeof(emu_patterns) / sizeof(emu_patterns[0]); ++i) {
        if (match_pattern(emu_patterns[i], strlen(emu_patterns[i]), pkg, proc)) {
            return SQ2_VK_TYPE_EMULATOR_VK13;
        }
    }
    if (loaded_sonames) {
        static const char *const emu_libs[] = {
            "libdxvk", "libvkd3d", "libyuzu", "libskyline", "libcitra",
            "libsuyu", "libdolphin", "libppsspp", "libaethersx2", "libnethersx2",
            "libbox64", "libfex", "libvirgl",
        };
        for (size_t i = 0; i < soname_count; ++i) {
            const char *base = soname_base(loaded_sonames[i]);
            for (size_t j = 0; j < sizeof(emu_libs) / sizeof(emu_libs[0]); ++j) {
                if (ci_starts_with(base, emu_libs[j])) {
                    return SQ2_VK_TYPE_EMULATOR_VK13;
                }
            }
        }
    }

    static const char *const bench_patterns[] = {
        "com.futuremark.*", "com.primatelabs.geekbench*", "com.antutu.*",
        "com.glbenchmark.*", "com.kishonti.gfxbench*", "com.maxon.*",
        "com.basemark.*", "com.drawelements.deqp*",
    };
    for (size_t i = 0; i < sizeof(bench_patterns) / sizeof(bench_patterns[0]); ++i) {
        if (match_pattern(bench_patterns[i], strlen(bench_patterns[i]), pkg, proc)) {
            return SQ2_VK_TYPE_BENCHMARK;
        }
    }

    if (loaded_sonames) {
        for (size_t i = 0; i < soname_count; ++i) {
            const char *base = soname_base(loaded_sonames[i]);
            if (ci_starts_with(base, "libUE4") ||
                ci_starts_with(base, "libUnreal") ||
                ci_starts_with(base, "libUE5")) {
                return SQ2_VK_TYPE_UNREAL;
            }
        }
        for (size_t i = 0; i < soname_count; ++i) {
            const char *base = soname_base(loaded_sonames[i]);
            if (ci_starts_with(base, "libunity")) {
                return SQ2_VK_TYPE_UNITY;
            }
        }
    }

    return SQ2_VK_TYPE_GENERIC;
}

static int eval_kernel_cmdline(const char *cmdline, int *out_backend, char *reason, size_t rsz) {
    if (!cmdline || !*cmdline) {
        return 0;
    }
    int rescue = 0;
    int have_force = 0;
    int force_backend = SQ2_VK_BACKEND_TURNIP;
    const char *p = cmdline;
    while (*p) {
        while (isspace((unsigned char)*p)) {
            ++p;
        }
        if (!*p) {
            break;
        }
        const char *start = p;
        while (*p && !isspace((unsigned char)*p)) {
            ++p;
        }
        size_t len = (size_t)(p - start);
        if (len == strlen("sq2.rescue=1") && memcmp(start, "sq2.rescue=1", len) == 0) {
            rescue = 1;
            continue;
        }
        const char *prefix = "sq2.vulkan.force=";
        size_t plen = strlen(prefix);
        if (len > plen && memcmp(start, prefix, plen) == 0) {
            int parsed = SQ2_VK_BACKEND_TURNIP;
            if (parse_backend_token(start + plen, len - plen, &parsed)) {
                have_force = 1;
                force_backend = parsed;
            }
        }
    }
    if (rescue) {
        *out_backend = SQ2_VK_BACKEND_TURNIP;
        snprintf(reason, rsz, "cmdline:sq2.rescue=1");
        return 1;
    }
    if (have_force) {
        *out_backend = force_backend;
        snprintf(reason, rsz, "cmdline:sq2.vulkan.force");
        return 1;
    }
    return 0;
}

static int eval_policy_conf(const char *conf, const char *pkg, const char *proc,
                            const char *type_str, int *out_backend,
                            char *reason, size_t rsz) {
    if (!conf || !*conf) {
        return 0;
    }
    int have_type = 0, type_backend = SQ2_VK_BACKEND_TURNIP;
    int have_default = 0, default_backend = SQ2_VK_BACKEND_TURNIP;

    const char *line = conf;
    while (*line) {
        const char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        const char *hash = memchr(line, '#', len);
        if (hash) {
            len = (size_t)(hash - line);
        }
        while (len > 0 && isspace((unsigned char)line[0])) {
            ++line;
            --len;
        }
        while (len > 0 && isspace((unsigned char)line[len - 1])) {
            --len;
        }
        if (len > 0) {
            const char *eq = memchr(line, '=', len);
            if (eq) {
                const char *lhs = line;
                size_t lhs_len = (size_t)(eq - line);
                while (lhs_len > 0 && isspace((unsigned char)lhs[lhs_len - 1])) {
                    --lhs_len;
                }
                const char *rhs = eq + 1;
                size_t rhs_len = (size_t)((line + len) - rhs);
                while (rhs_len > 0 && isspace((unsigned char)rhs[0])) {
                    ++rhs;
                    --rhs_len;
                }
                int b = SQ2_VK_BACKEND_TURNIP;
                if (parse_backend_token(rhs, rhs_len, &b)) {
                    if (lhs_len > 4 && memcmp(lhs, "app", 3) == 0 &&
                        isspace((unsigned char)lhs[3])) {
                        const char *pat = lhs + 4;
                        size_t pat_len = lhs_len - 4;
                        while (pat_len > 0 && isspace((unsigned char)*pat)) {
                            ++pat;
                            --pat_len;
                        }
                        if (match_pattern(pat, pat_len, pkg, proc)) {
                            *out_backend = b;
                            snprintf(reason, rsz, "conf:app");
                            return 1;
                        }
                    } else if (lhs_len > 5 && memcmp(lhs, "type", 4) == 0 &&
                               isspace((unsigned char)lhs[4])) {
                        const char *tname = lhs + 5;
                        size_t tlen = lhs_len - 5;
                        while (tlen > 0 && isspace((unsigned char)*tname)) {
                            ++tname;
                            --tlen;
                        }
                        if (!have_type && type_str && strlen(type_str) == tlen &&
                            memcmp(type_str, tname, tlen) == 0) {
                            have_type = 1;
                            type_backend = b;
                        }
                    } else if (lhs_len == 7 && memcmp(lhs, "default", 7) == 0) {
                        have_default = 1;
                        default_backend = b;
                    }
                }
            }
        }
        if (!nl) {
            break;
        }
        line = nl + 1;
    }

    if (have_type) {
        *out_backend = type_backend;
        snprintf(reason, rsz, "conf:type");
        return 1;
    }
    if (have_default) {
        *out_backend = default_backend;
        snprintf(reason, rsz, "conf:default");
        return 1;
    }
    return 0;
}

EXPORT int sq2_vk_evaluate_policy(const struct sq2_vk_eval_input *in,
                                  struct sq2_vk_decision *out) {
    if (!in || !out) {
        return -EINVAL;
    }
    memset(out, 0, sizeof(*out));

    char proc[256];
    extract_proc_and_pkg(in->cmdline_arg0, proc, sizeof(proc),
                         out->package_name, sizeof(out->package_name));
    out->app_type = sq2_vk_classify_process(in->uid, in->cmdline_arg0,
                                            in->loaded_sonames, in->soname_count);
    int is_zygote = is_zygote_process(out->package_name, proc);
    out->cacheable = is_zygote ? 0 : 1;

    int b = SQ2_VK_BACKEND_TURNIP;
    /* Priority 1: non-secure env SQ2_VULKAN_DRIVER */
    if (parse_backend_str(in->env_driver, &b)) {
        out->backend = b;
        snprintf(out->reason, sizeof(out->reason), "env:SQ2_VULKAN_DRIVER");
        return 0;
    }
    /* Priority 2: kernel cmdline sq2.rescue=1 or sq2.vulkan.force=... */
    if (eval_kernel_cmdline(in->kernel_cmdline, &b, out->reason, sizeof(out->reason))) {
        out->backend = b;
        return 0;
    }
    /* Priority 3: Zygote / USAP guard */
    if (is_zygote) {
        out->backend = SQ2_VK_BACKEND_TURNIP;
        out->cacheable = 0;
        snprintf(out->reason, sizeof(out->reason), "guard:zygote");
        return 0;
    }
    /* Priority 4: per-app property lists (debug > persist; turnip > qcom tie-break) */
    if (match_pattern_list(in->prop_debug_turnip_apps, out->package_name, proc)) {
        out->backend = SQ2_VK_BACKEND_TURNIP;
        snprintf(out->reason, sizeof(out->reason), "prop:debug.turnip_apps");
        return 0;
    }
    if (match_pattern_list(in->prop_debug_qcom_apps, out->package_name, proc)) {
        out->backend = SQ2_VK_BACKEND_QCOM;
        snprintf(out->reason, sizeof(out->reason), "prop:debug.qcom_apps");
        return 0;
    }
    if (match_pattern_list(in->prop_persist_turnip_apps, out->package_name, proc)) {
        out->backend = SQ2_VK_BACKEND_TURNIP;
        snprintf(out->reason, sizeof(out->reason), "prop:persist.turnip_apps");
        return 0;
    }
    if (match_pattern_list(in->prop_persist_qcom_apps, out->package_name, proc)) {
        out->backend = SQ2_VK_BACKEND_QCOM;
        snprintf(out->reason, sizeof(out->reason), "prop:persist.qcom_apps");
        return 0;
    }
    /* Priority 5: per-type property override */
    const char *type_str = sq2_vk_type_name(out->app_type);
    if (parse_backend_str(in->prop_type_override, &b)) {
        out->backend = b;
        snprintf(out->reason, sizeof(out->reason), "prop:type.%s", type_str);
        return 0;
    }
    /* Priority 6: global driver property */
    if (parse_backend_str(in->prop_driver, &b)) {
        if (b == SQ2_VK_BACKEND_QCOM && out->app_type == SQ2_VK_TYPE_SYSTEM &&
            !in->prop_include_system) {
            out->backend = SQ2_VK_BACKEND_TURNIP;
            snprintf(out->reason, sizeof(out->reason), "guard:system-on-global-qcom");
        } else {
            out->backend = b;
            snprintf(out->reason, sizeof(out->reason), "prop:driver");
        }
        return 0;
    }
    /* Priority 7: /vendor/etc/sq2-vulkan-policy.conf */
    if (eval_policy_conf(in->policy_conf_text, out->package_name, proc, type_str,
                         &b, out->reason, sizeof(out->reason))) {
        out->backend = b;
        return 0;
    }
    /* Priority 8: safe default */
    out->backend = SQ2_VK_BACKEND_TURNIP;
    snprintf(out->reason, sizeof(out->reason), "default:turnip");
    return 0;
}

/* ---- Runtime Helpers (Process Introspection & Android Property Callback) ---- */

static char *read_text_file(const char *path, size_t max_bytes, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    char *buf = malloc(max_bytes + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, max_bytes, f);
    fclose(f);
    buf[n] = '\0';
    if (out_len) {
        *out_len = n;
    }
    return buf;
}

struct prop_info_opaque;
typedef void (*sq2_prop_cb_t)(void *cookie, const char *name, const char *value, uint32_t serial);
typedef const struct prop_info_opaque *(*sq2_prop_find_fn_t)(const char *name);
typedef void (*sq2_prop_read_cb_fn_t)(const struct prop_info_opaque *pi,
                                      sq2_prop_cb_t cb, void *cookie);
typedef int (*sq2_prop_get_fn_t)(const char *name, char *value);

static void copy_prop_cb(void *cookie, const char *name, const char *value, uint32_t serial) {
    (void)name;
    (void)serial;
    char **out = (char **)cookie;
    if (value && *value) {
        *out = strdup(value);
    }
}

static char *read_android_prop(const char *name) {
    sq2_prop_find_fn_t find_fn = NULL;
    sq2_prop_read_cb_fn_t read_cb_fn = NULL;
    sq2_prop_get_fn_t get_fn = NULL;

    if (!name) {
        return NULL;
    }
    /* Resolve dynamically so host Linux tests can also interpose via RTLD_DEFAULT. */
    void *h_find = dlsym(RTLD_DEFAULT, "__system_property_find");
    void *h_cb = dlsym(RTLD_DEFAULT, "__system_property_read_callback");
    void *h_get = dlsym(RTLD_DEFAULT, "__system_property_get");
    memcpy(&find_fn, &h_find, sizeof(find_fn));
    memcpy(&read_cb_fn, &h_cb, sizeof(read_cb_fn));
    memcpy(&get_fn, &h_get, sizeof(get_fn));

    if (find_fn && read_cb_fn) {
        const struct prop_info_opaque *pi = find_fn(name);
        if (!pi) {
            return NULL;
        }
        char *result = NULL;
        read_cb_fn(pi, copy_prop_cb, &result);
        return result;
    }
    if (get_fn) {
        char buf[256];
        buf[0] = '\0';
        int n = get_fn(name, buf);
        if (n > 0) {
            buf[sizeof(buf) - 1] = '\0';
            return strdup(buf);
        }
    }
    return NULL;
}

struct soname_collector {
    char **items;
    size_t count;
    size_t cap;
};

static int collect_phdr_cb(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    struct soname_collector *col = (struct soname_collector *)data;
    if (!info || !info->dlpi_name || !info->dlpi_name[0]) {
        return 0;
    }
    const char *base = soname_base(info->dlpi_name);
    if (!*base || col->count >= 256) {
        return 0;
    }
    for (size_t i = 0; i < col->count; ++i) {
        if (strcmp(col->items[i], base) == 0) {
            return 0;
        }
    }
    if (col->count == col->cap) {
        size_t next_cap = col->cap ? col->cap * 2 : 32;
        if (next_cap > 256) {
            next_cap = 256;
        }
        char **next = realloc(col->items, next_cap * sizeof(*next));
        if (!next) {
            return 0;
        }
        col->items = next;
        col->cap = next_cap;
    }
    char *copy = strdup(base);
    if (copy) {
        col->items[col->count++] = copy;
    }
    return 0;
}

static void free_sonames(struct soname_collector *col) {
    for (size_t i = 0; i < col->count; ++i) {
        free(col->items[i]);
    }
    free(col->items);
    col->items = NULL;
    col->count = 0;
    col->cap = 0;
}

static int parse_bool_prop(const char *val) {
    if (!val) {
        return 0;
    }
    return strcmp(val, "1") == 0 || strcasecmp(val, "true") == 0 || strcasecmp(val, "yes") == 0;
}

/* ---- HAL Entry Point & Fork-Safe Caching ---- */

extern sq2_hwvulkan_module_t HAL_MODULE_INFO_SYM;

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pid_t g_cached_pid = -1;
static void *g_cached_dl = NULL;
static const sq2_hw_module_t *g_cached_module = NULL;
static const char *g_cached_path = NULL;
static struct sq2_vk_decision g_cached_decision;

static int router_open(const sq2_hw_module_t *module, const char *id,
                       sq2_hw_device_t **device);

static const char *backend_library_path(int backend) {
#ifndef __ANDROID__
    if (backend == SQ2_VK_BACKEND_QCOM) {
        const char *override = getenv("SQ2_TEST_QTI_SO");
        if (override && *override) {
            return override;
        }
    } else {
        const char *override = getenv("SQ2_TEST_TURNIP_SO");
        if (override && *override) {
            return override;
        }
    }
#endif
    return backend == SQ2_VK_BACKEND_QCOM
               ? "/vendor/lib64/hw/vulkan.qti.so"
               : "/vendor/lib64/hw/vulkan.turnip.so";
}

static const char *policy_file_path(void) {
#ifndef __ANDROID__
    const char *override = getenv("SQ2_TEST_VK_POLICY_PATH");
    if (override && *override) {
        return override;
    }
#endif
    return "/vendor/etc/sq2-vulkan-policy.conf";
}

static int validate_target_module(const sq2_hw_module_t *target, const char *path) {
    if (!target) {
        SQ2_LOGE("FAIL missing HMI symbol in %s", path);
        return -ENODEV;
    }
    if (target == &HAL_MODULE_INFO_SYM.common) {
        SQ2_LOGE("FAIL self-recursion HMI in %s", path);
        return -ELOOP;
    }
    if (target->tag != SQ2_HARDWARE_MODULE_TAG || !target->id ||
        strcmp(target->id, SQ2_HWVULKAN_HARDWARE_MODULE_ID) != 0 ||
        !target->methods || !target->methods->open) {
        SQ2_LOGE("FAIL invalid Vulkan HMI in %s", path);
        return -ENODEV;
    }
    if (target->methods->open == router_open) {
        SQ2_LOGE("FAIL self-recursion open() in %s", path);
        return -ELOOP;
    }
    return 0;
}

static int evaluate_current_process(struct sq2_vk_decision *out,
                                    char *proc_out, size_t proc_sz) {
    size_t cmd0_len = 0;
    char *self_cmd = read_text_file("/proc/self/cmdline", 512, &cmd0_len);
    const char *arg0 = (self_cmd && cmd0_len > 0) ? self_cmd : "";
    char pkg_tmp[192];
    extract_proc_and_pkg(arg0, proc_out, proc_sz, pkg_tmp, sizeof(pkg_tmp));

    struct soname_collector sonames = {0};
    dl_iterate_phdr(collect_phdr_cb, &sonames);

    uint32_t uid = (uint32_t)getuid();
    int app_type = sq2_vk_classify_process(uid, arg0,
                                           (const char *const *)sonames.items,
                                           sonames.count);
    const char *type_str = sq2_vk_type_name(app_type);

    char type_key_dbg[64];
    char type_key_pst[64];
    snprintf(type_key_dbg, sizeof(type_key_dbg), "debug.sq2.vulkan.type.%s", type_str);
    snprintf(type_key_pst, sizeof(type_key_pst), "persist.sq2.vulkan.type.%s", type_str);

    const char *env_drv = NULL;
    if (getuid() == geteuid() && getgid() == getegid()) {
        env_drv = getenv("SQ2_VULKAN_DRIVER");
    }

    char *kernel_cmd = read_text_file("/proc/cmdline", 8192, NULL);
    char *dbg_turnip = read_android_prop("debug.sq2.vulkan.turnip_apps");
    char *dbg_qcom = read_android_prop("debug.sq2.vulkan.qcom_apps");
    char *pst_turnip = read_android_prop("persist.sq2.vulkan.turnip_apps");
    char *pst_qcom = read_android_prop("persist.sq2.vulkan.qcom_apps");
    char *prop_type = read_android_prop(type_key_dbg);
    if (!prop_type) {
        prop_type = read_android_prop(type_key_pst);
    }
    char *prop_drv = read_android_prop("debug.sq2.vulkan.driver");
    if (!prop_drv) {
        prop_drv = read_android_prop("persist.sq2.vulkan.driver");
    }
    char *prop_inc_sys = read_android_prop("debug.sq2.vulkan.include_system");
    char *policy_conf = read_text_file(policy_file_path(), 32768, NULL);

    struct sq2_vk_eval_input input = {
        .uid = uid,
        .cmdline_arg0 = arg0,
        .loaded_sonames = (const char *const *)sonames.items,
        .soname_count = sonames.count,
        .env_driver = env_drv,
        .kernel_cmdline = kernel_cmd,
        .prop_debug_turnip_apps = dbg_turnip,
        .prop_debug_qcom_apps = dbg_qcom,
        .prop_persist_turnip_apps = pst_turnip,
        .prop_persist_qcom_apps = pst_qcom,
        .prop_type_override = prop_type,
        .prop_driver = prop_drv,
        .prop_include_system = parse_bool_prop(prop_inc_sys),
        .policy_conf_text = policy_conf,
    };
    int rc = sq2_vk_evaluate_policy(&input, out);

    free(policy_conf);
    free(prop_inc_sys);
    free(prop_drv);
    free(prop_type);
    free(pst_qcom);
    free(pst_turnip);
    free(dbg_qcom);
    free(dbg_turnip);
    free(kernel_cmd);
    free_sonames(&sonames);
    free(self_cmd);
    return rc;
}

static int router_open(const sq2_hw_module_t *module, const char *id,
                       sq2_hw_device_t **device) {
    if (!module || !device || !id || strcmp(id, SQ2_HWVULKAN_DEVICE_0) != 0) {
        return -EINVAL;
    }
    *device = NULL;

    pid_t pid = getpid();
    const sq2_hw_module_t *target = NULL;
    const char *target_path = NULL;
    struct sq2_vk_decision decision;
    char proc[256] = {0};

    pthread_mutex_lock(&g_mutex);
    if (g_cached_module != NULL && g_cached_pid == pid) {
        target = g_cached_module;
        target_path = g_cached_path;
        decision = g_cached_decision;
        pthread_mutex_unlock(&g_mutex);
    } else {
        pthread_mutex_unlock(&g_mutex);
        if (evaluate_current_process(&decision, proc, sizeof(proc)) != 0) {
            return -ENODEV;
        }
        target_path = backend_library_path(decision.backend);
        SQ2_LOGI("SELECT pkg=%s proc=%s uid=%u type=%s backend=%s path=%s reason=%s",
                 decision.package_name[0] ? decision.package_name : "(empty)",
                 proc[0] ? proc : "(empty)",
                 (unsigned)getuid(),
                 sq2_vk_type_name(decision.app_type),
                 backend_name(decision.backend),
                 target_path,
                 decision.reason);

        void *handle = dlopen(target_path, RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            const char *err = dlerror();
            SQ2_LOGE("FAIL dlopen %s: %s", target_path, err ? err : "(unknown)");
            return -ENODEV;
        }
        const sq2_hw_module_t *loaded =
            (const sq2_hw_module_t *)dlsym(handle, "HMI");
        int valid = validate_target_module(loaded, target_path);
        if (valid != 0) {
            dlclose(handle);
            return valid;
        }

        if (decision.cacheable) {
            pthread_mutex_lock(&g_mutex);
            if (g_cached_module != NULL && g_cached_pid == pid) {
                target = g_cached_module;
                target_path = g_cached_path;
                decision = g_cached_decision;
            } else {
                /* Do not dlclose stale pre-fork handle if any device survived. */
                g_cached_dl = handle;
                (void)g_cached_dl;
                g_cached_module = loaded;
                g_cached_path = target_path;
                g_cached_decision = decision;
                g_cached_pid = pid;
                target = loaded;
            }
            pthread_mutex_unlock(&g_mutex);
        } else {
            /* Zygote/USAP: leave uncached so forked children re-evaluate freely. */
            target = loaded;
        }
    }

    int rc = target->methods->open(target, id, device);
    if (rc != 0 || !*device) {
        SQ2_LOGE("FAIL delegate open path=%s result=%d", target_path, rc);
        *device = NULL;
        return rc != 0 ? rc : -ENODEV;
    }

    sq2_hwvulkan_device_t *vk = (sq2_hwvulkan_device_t *)*device;
    if (vk->common.tag != SQ2_HARDWARE_DEVICE_TAG ||
        !vk->EnumerateInstanceExtensionProperties ||
        !vk->CreateInstance || !vk->GetInstanceProcAddr) {
        SQ2_LOGE("FAIL invalid delegate hwvulkan_device_t at %s", target_path);
        if (vk->common.close) {
            vk->common.close(&vk->common);
        }
        *device = NULL;
        return -ENODEV;
    }

    SQ2_LOGI("OPEN_OK backend=%s path=%s", backend_name(decision.backend), target_path);
    return 0;
}

static sq2_hw_module_methods_t g_methods = {
    .open = router_open,
};

EXPORT sq2_hwvulkan_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = SQ2_HARDWARE_MODULE_TAG,
        .module_api_version = SQ2_HWVULKAN_MODULE_API_VERSION_0_1,
        .hal_api_version = SQ2_HARDWARE_HAL_API_VERSION,
        .id = SQ2_HWVULKAN_HARDWARE_MODULE_ID,
        .name = "SQ2Hyb Dual-Vulkan HAL Router",
        .author = "SQ2 hybrid graphics port",
        .methods = &g_methods,
    },
};
