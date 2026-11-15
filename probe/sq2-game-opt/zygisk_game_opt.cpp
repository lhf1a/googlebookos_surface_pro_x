#include "common_policy.h"
#include "vk_swapchain_hook.h"

#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <jni.h>
#include <link.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <android/log.h>

#define LOG_TAG "SQ2GameOpt"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {

constexpr const char *kConfigPath = "/data/adb/sq2_game_opt/config.conf";
constexpr const char *kDataAppRoot = "/data/app";

/* Zygisk v4 ABI (mirrors zygisk-api raw/v4.rs + raw/v3.rs + raw/v1.rs) */
struct ZygiskAppSpecializeArgsV4 {
    jint *uid;
    jint *gid;
    jintArray *gids;
    jint *runtime_flags;
    jobjectArray *rlimits;
    jint *mount_external;
    jstring *se_info;
    jstring *nice_name;
    jstring *instruction_set;
    jstring *app_data_dir;
    jintArray *fds_to_ignore;
    jint *is_child_zygote;
    jint *is_top_app;
    jobjectArray *pkg_data_info_list;
    jobjectArray *whitelisted_data_info_list;
    jboolean *mount_data_dirs;
    jboolean *mount_storage_dirs;
};

struct ZygiskServerSpecializeArgsV4 {
    jint *uid;
    jint *gid;
    jintArray *gids;
    jint *runtime_flags;
    jlong *permitted_capabilities;
    jlong *effective_capabilities;
};

struct ZygiskModuleAbiV4;

struct ZygiskApiTableV4 {
    void *impl;
    bool (*registerModule)(const ZygiskApiTableV4 *table, ZygiskModuleAbiV4 *module);
    void (*hookJniNativeMethods)(JNIEnv *env, const char *className, JNINativeMethod *methods, int numMethods);
    void (*pltHookRegister)(dev_t dev, ino_t inode, const char *symbol, void *newFunc, void **oldFunc);
    bool (*exemptFd)(int fd);
    bool (*pltHookCommit)(void);
    int (*connectCompanion)(void *impl);
    void (*setOption)(void *impl, int option);
    int (*getModuleDir)(void *impl);
    uint32_t (*getFlags)(void *impl);
};

struct ZygiskModuleAbiV4 {
    long api_version;
    void *impl;
    void (*preAppSpecialize)(void *impl, ZygiskAppSpecializeArgsV4 *args);
    void (*postAppSpecialize)(void *impl, const ZygiskAppSpecializeArgsV4 *args);
    void (*preServerSpecialize)(void *impl, ZygiskServerSpecializeArgsV4 *args);
    void (*postServerSpecialize)(void *impl, const ZygiskServerSpecializeArgsV4 *args);
};

constexpr int ZYGISK_OPTION_DLCLOSE_MODULE_LIBRARY = 1;

struct ModuleState {
    const ZygiskApiTableV4 *api = nullptr;
    JNIEnv *env = nullptr;
    bool active = false;
    uint8_t engine = SQ2_ENGINE_NONE;
    bool mailbox_present_mode = true;
    bool egl_swap_interval_zero = true;
    char pkg[SQ2_MAX_PATTERN_LEN] = {0};
};

ModuleState g_state;
ZygiskModuleAbiV4 g_module_abi;
uintptr_t g_self_base = 0;

using FnLoaderDlopen = void *(*)(const char *, int, const void *);
using FnLoaderAndroidDlopenExt = void *(*)(const char *, int, const void *, const void *);
using FnLoaderDlsym = void *(*)(void *, const char *, const void *);
using FnEglSwapInterval = int (*)(void *, int);

pthread_mutex_t g_hook_lock = PTHREAD_MUTEX_INITIALIZER;
FnLoaderDlopen g_orig_loader_dlopen = nullptr;
FnLoaderAndroidDlopenExt g_orig_loader_android_dlopen_ext = nullptr;
FnLoaderDlsym g_orig_loader_dlsym = nullptr;
FnEglSwapInterval g_orig_egl_swap_interval = nullptr;
PFN_vkGetInstanceProcAddr g_orig_vk_get_instance_proc_addr = nullptr;
PFN_vkGetDeviceProcAddr g_orig_vk_get_device_proc_addr = nullptr;
PFN_vkCreateSwapchainKHR g_orig_vk_create_swapchain = nullptr;

int g_logged_vk_mailbox = 0;
int g_logged_vk_fallback = 0;
int g_logged_egl_interval = 0;

bool read_exact(int fd, void *buf, size_t len) {
    uint8_t *p = static_cast<uint8_t *>(buf);
    size_t got = 0;
    while (got < len) {
        ssize_t r = read(fd, p + got, len - got);
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (r == 0) return false;
        got += static_cast<size_t>(r);
    }
    return true;
}

bool write_exact(int fd, const void *buf, size_t len) {
    const uint8_t *p = static_cast<const uint8_t *>(buf);
    size_t sent = 0;
    while (sent < len) {
        ssize_t w = write(fd, p + sent, len - sent);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        sent += static_cast<size_t>(w);
    }
    return true;
}

VkResult hook_vkCreateSwapchainKHR(
    VkDevice device,
    const VkSwapchainCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkSwapchainKHR *pSwapchain) {
    PFN_vkCreateSwapchainKHR real_fn = g_orig_vk_create_swapchain;
    if (!real_fn && g_orig_vk_get_device_proc_addr && device) {
        PFN_vkVoidFunction resolved = g_orig_vk_get_device_proc_addr(device, "vkCreateSwapchainKHR");
        if (resolved && reinterpret_cast<void *>(resolved) != reinterpret_cast<void *>(hook_vkCreateSwapchainKHR)) {
            real_fn = reinterpret_cast<PFN_vkCreateSwapchainKHR>(resolved);
            g_orig_vk_create_swapchain = real_fn;
        }
    }
    int used_mailbox = 0;
    int fell_back = 0;
    VkResult res = sq2_apply_mailbox_create_swapchain(
        real_fn,
        g_state.mailbox_present_mode ? 1 : 0,
        device,
        pCreateInfo,
        pAllocator,
        pSwapchain,
        &used_mailbox,
        &fell_back);
    if (used_mailbox && !fell_back && !g_logged_vk_mailbox) {
        g_logged_vk_mailbox = 1;
        LOGI("pkg=%s forced vkCreateSwapchainKHR presentMode=VK_PRESENT_MODE_MAILBOX_KHR", g_state.pkg);
    } else if (fell_back && !g_logged_vk_fallback) {
        g_logged_vk_fallback = 1;
        LOGI("pkg=%s MAILBOX rejected by driver/surface; fell back to original presentMode", g_state.pkg);
    }
    return res;
}

PFN_vkVoidFunction hook_vkGetDeviceProcAddr(VkDevice device, const char *pName) {
    PFN_vkGetDeviceProcAddr real_gda = g_orig_vk_get_device_proc_addr;
    if (!real_gda) {
        return nullptr;
    }
    PFN_vkVoidFunction fn = real_gda(device, pName);
    if (!fn || !pName) {
        return fn;
    }
    if (strcmp(pName, "vkCreateSwapchainKHR") == 0) {
        if (reinterpret_cast<void *>(fn) != reinterpret_cast<void *>(hook_vkCreateSwapchainKHR)) {
            g_orig_vk_create_swapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR>(fn);
        }
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCreateSwapchainKHR);
    }
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        if (reinterpret_cast<void *>(fn) != reinterpret_cast<void *>(hook_vkGetDeviceProcAddr)) {
            g_orig_vk_get_device_proc_addr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(fn);
        }
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetDeviceProcAddr);
    }
    return fn;
}

PFN_vkVoidFunction hook_vkGetInstanceProcAddr(VkInstance instance, const char *pName) {
    PFN_vkGetInstanceProcAddr real_gipa = g_orig_vk_get_instance_proc_addr;
    if (!real_gipa) {
        return nullptr;
    }
    PFN_vkVoidFunction fn = real_gipa(instance, pName);
    if (!fn || !pName) {
        return fn;
    }
    if (strcmp(pName, "vkCreateSwapchainKHR") == 0) {
        if (reinterpret_cast<void *>(fn) != reinterpret_cast<void *>(hook_vkCreateSwapchainKHR)) {
            g_orig_vk_create_swapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR>(fn);
        }
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkCreateSwapchainKHR);
    }
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        if (reinterpret_cast<void *>(fn) != reinterpret_cast<void *>(hook_vkGetDeviceProcAddr)) {
            g_orig_vk_get_device_proc_addr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(fn);
        }
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetDeviceProcAddr);
    }
    if (strcmp(pName, "vkGetInstanceProcAddr") == 0) {
        if (reinterpret_cast<void *>(fn) != reinterpret_cast<void *>(hook_vkGetInstanceProcAddr)) {
            g_orig_vk_get_instance_proc_addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(fn);
        }
        return reinterpret_cast<PFN_vkVoidFunction>(hook_vkGetInstanceProcAddr);
    }
    return fn;
}

int hook_eglSwapInterval(void *dpy, int interval) {
    if (g_state.egl_swap_interval_zero && !g_logged_egl_interval) {
        g_logged_egl_interval = 1;
        LOGI("pkg=%s forced eglSwapInterval=0 (requested=%d)", g_state.pkg, interval);
    }
    return sq2_apply_egl_swap_interval(
        g_orig_egl_swap_interval,
        g_state.egl_swap_interval_zero ? 1 : 0,
        dpy,
        interval);
}

static inline uintptr_t rebase_dyn_ptr(uintptr_t base, uintptr_t val) {
    return (val >= base) ? val : (base + val);
}

int get_page_prot(uintptr_t addr) {
    FILE *fp = fopen("/proc/self/maps", "re");
    if (!fp) {
        return PROT_READ;
    }
    char line[512];
    int prot = -1;
    while (fgets(line, sizeof(line), fp)) {
        uintptr_t start = 0;
        uintptr_t end = 0;
        char perms[8] = {0};
        if (sscanf(line, "%lx-%lx %7s", &start, &end, perms) == 3) {
            if (addr >= start && addr < end) {
                prot = 0;
                if (perms[0] == 'r') prot |= PROT_READ;
                if (perms[1] == 'w') prot |= PROT_WRITE;
                if (perms[2] == 'x') prot |= PROT_EXEC;
                break;
            }
        }
    }
    fclose(fp);
    return prot;
}

bool patch_got_slot(uintptr_t slot_addr, void *new_fn, void **saved_orig) {
    uintptr_t current_ptr = *reinterpret_cast<uintptr_t *>(slot_addr);
    if (current_ptr == reinterpret_cast<uintptr_t>(new_fn)) {
        return false;
    }
    long page_sz = sysconf(_SC_PAGESIZE);
    if (page_sz <= 0) page_sz = 4096;
    uintptr_t page = slot_addr & ~(static_cast<uintptr_t>(page_sz) - 1);
    int orig_prot = get_page_prot(slot_addr);
    if (orig_prot < 0) {
        return false;
    }
    if ((orig_prot & PROT_WRITE) == 0) {
        if (mprotect(reinterpret_cast<void *>(page), static_cast<size_t>(page_sz),
                     orig_prot | PROT_READ | PROT_WRITE) != 0) {
            return false;
        }
    }
    if (saved_orig && *saved_orig == nullptr && current_ptr != 0) {
        *saved_orig = reinterpret_cast<void *>(current_ptr);
    }
    *reinterpret_cast<uintptr_t *>(slot_addr) = reinterpret_cast<uintptr_t>(new_fn);
    if ((orig_prot & PROT_WRITE) == 0) {
        (void)mprotect(reinterpret_cast<void *>(page), static_cast<size_t>(page_sz), orig_prot);
    }
    return true;
}

void *hook_loader_dlsym(void *handle, const char *symbol, const void *caller_addr);
void *hook_loader_dlopen(const char *filename, int flags, const void *caller_addr);
void *hook_loader_android_dlopen_ext(const char *filename, int flags, const void *extinfo, const void *caller_addr);

struct HookTarget {
    const char *symbol;
    void *new_fn;
    void **saved_orig;
};

void patch_single_phdr(struct dl_phdr_info *info) {
    if (!info || info->dlpi_addr == 0 || info->dlpi_addr == g_self_base) {
        return;
    }
    if (info->dlpi_name &&
        (strstr(info->dlpi_name, "linker64") || strstr(info->dlpi_name, "[vdso]"))) {
        return;
    }

    const Elf64_Dyn *dyn = nullptr;
    for (Elf64_Half i = 0; i < info->dlpi_phnum; i++) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
            dyn = reinterpret_cast<const Elf64_Dyn *>(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
            break;
        }
    }
    if (!dyn) {
        return;
    }

    uintptr_t base = info->dlpi_addr;
    const char *strtab = nullptr;
    const Elf64_Sym *symtab = nullptr;
    const Elf64_Rela *jmprel = nullptr;
    size_t pltrelsz = 0;
    const Elf64_Rela *rela = nullptr;
    size_t relasz = 0;
    Elf64_Sxword pltrel_type = 0;

    for (const Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
        case DT_STRTAB:
            strtab = reinterpret_cast<const char *>(rebase_dyn_ptr(base, d->d_un.d_ptr));
            break;
        case DT_SYMTAB:
            symtab = reinterpret_cast<const Elf64_Sym *>(rebase_dyn_ptr(base, d->d_un.d_ptr));
            break;
        case DT_JMPREL:
            jmprel = reinterpret_cast<const Elf64_Rela *>(rebase_dyn_ptr(base, d->d_un.d_ptr));
            break;
        case DT_PLTRELSZ:
            pltrelsz = static_cast<size_t>(d->d_un.d_val);
            break;
        case DT_PLTREL:
            pltrel_type = d->d_un.d_val;
            break;
        case DT_RELA:
            rela = reinterpret_cast<const Elf64_Rela *>(rebase_dyn_ptr(base, d->d_un.d_ptr));
            break;
        case DT_RELASZ:
            relasz = static_cast<size_t>(d->d_un.d_val);
            break;
        default:
            break;
        }
    }

    if (!strtab || !symtab) {
        return;
    }

    const HookTarget targets[] = {
        {"__loader_dlopen", reinterpret_cast<void *>(hook_loader_dlopen),
         reinterpret_cast<void **>(&g_orig_loader_dlopen)},
        {"__loader_android_dlopen_ext", reinterpret_cast<void *>(hook_loader_android_dlopen_ext),
         reinterpret_cast<void **>(&g_orig_loader_android_dlopen_ext)},
        {"__loader_dlsym", reinterpret_cast<void *>(hook_loader_dlsym),
         reinterpret_cast<void **>(&g_orig_loader_dlsym)},
        {"vkGetInstanceProcAddr", reinterpret_cast<void *>(hook_vkGetInstanceProcAddr),
         reinterpret_cast<void **>(&g_orig_vk_get_instance_proc_addr)},
        {"vkGetDeviceProcAddr", reinterpret_cast<void *>(hook_vkGetDeviceProcAddr),
         reinterpret_cast<void **>(&g_orig_vk_get_device_proc_addr)},
        {"vkCreateSwapchainKHR", reinterpret_cast<void *>(hook_vkCreateSwapchainKHR),
         reinterpret_cast<void **>(&g_orig_vk_create_swapchain)},
        {"eglSwapInterval", reinterpret_cast<void *>(hook_eglSwapInterval),
         reinterpret_cast<void **>(&g_orig_egl_swap_interval)},
    };

    auto apply_table = [&](const Elf64_Rela *tbl, size_t count) {
        if (!tbl || count == 0) return;
        for (size_t i = 0; i < count; i++) {
            uint32_t type = ELF64_R_TYPE(tbl[i].r_info);
            if (type != R_AARCH64_JUMP_SLOT && type != R_AARCH64_GLOB_DAT) continue;
            uint32_t sym_idx = ELF64_R_SYM(tbl[i].r_info);
            if (sym_idx == 0) continue;
            const char *sym_name = strtab + symtab[sym_idx].st_name;
            for (const auto &t : targets) {
                if (strcmp(sym_name, t.symbol) == 0) {
                    patch_got_slot(base + tbl[i].r_offset, t.new_fn, t.saved_orig);
                    break;
                }
            }
        }
    };

    if (jmprel && pltrel_type == DT_RELA) {
        apply_table(jmprel, pltrelsz / sizeof(Elf64_Rela));
    }
    if (rela) {
        apply_table(rela, relasz / sizeof(Elf64_Rela));
    }
}

void install_got_hooks() {
    pthread_mutex_lock(&g_hook_lock);
    dl_iterate_phdr([](struct dl_phdr_info *info, size_t, void *) -> int {
        patch_single_phdr(info);
        return 0;
    }, nullptr);
    pthread_mutex_unlock(&g_hook_lock);
}

void *hook_loader_dlsym(void *handle, const char *symbol, const void *caller_addr) {
    if (!g_orig_loader_dlsym) {
        return nullptr;
    }
    void *res = g_orig_loader_dlsym(handle, symbol, caller_addr);
    if (!res || !symbol) {
        return res;
    }
    if (strcmp(symbol, "vkGetInstanceProcAddr") == 0) {
        if (res != reinterpret_cast<void *>(hook_vkGetInstanceProcAddr)) {
            g_orig_vk_get_instance_proc_addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(res);
        }
        return reinterpret_cast<void *>(hook_vkGetInstanceProcAddr);
    }
    if (strcmp(symbol, "vkGetDeviceProcAddr") == 0) {
        if (res != reinterpret_cast<void *>(hook_vkGetDeviceProcAddr)) {
            g_orig_vk_get_device_proc_addr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(res);
        }
        return reinterpret_cast<void *>(hook_vkGetDeviceProcAddr);
    }
    if (strcmp(symbol, "vkCreateSwapchainKHR") == 0) {
        if (res != reinterpret_cast<void *>(hook_vkCreateSwapchainKHR)) {
            g_orig_vk_create_swapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR>(res);
        }
        return reinterpret_cast<void *>(hook_vkCreateSwapchainKHR);
    }
    if (strcmp(symbol, "eglSwapInterval") == 0) {
        if (res != reinterpret_cast<void *>(hook_eglSwapInterval)) {
            g_orig_egl_swap_interval = reinterpret_cast<FnEglSwapInterval>(res);
        }
        return reinterpret_cast<void *>(hook_eglSwapInterval);
    }
    return res;
}

void *hook_loader_dlopen(const char *filename, int flags, const void *caller_addr) {
    if (!g_orig_loader_dlopen) {
        return nullptr;
    }
    void *handle = g_orig_loader_dlopen(filename, flags, caller_addr);
    if (handle) {
        install_got_hooks();
    }
    return handle;
}

void *hook_loader_android_dlopen_ext(
    const char *filename,
    int flags,
    const void *extinfo,
    const void *caller_addr) {
    if (!g_orig_loader_android_dlopen_ext) {
        return nullptr;
    }
    void *handle = g_orig_loader_android_dlopen_ext(filename, flags, extinfo, caller_addr);
    if (handle) {
        install_got_hooks();
    }
    return handle;
}

void pre_app_specialize(void *impl, ZygiskAppSpecializeArgsV4 *args) {
    auto *st = static_cast<ModuleState *>(impl);
    if (!st || !st->api || !st->env || !args || !args->uid || *args->uid < 10000 ||
        !args->nice_name || !*args->nice_name) {
        if (st && st->api) {
            st->api->setOption(st->api->impl, ZYGISK_OPTION_DLCLOSE_MODULE_LIBRARY);
        }
        return;
    }

    const char *raw_name = st->env->GetStringUTFChars(*args->nice_name, nullptr);
    if (!raw_name) {
        st->api->setOption(st->api->impl, ZYGISK_OPTION_DLCLOSE_MODULE_LIBRARY);
        return;
    }
    sq2_base_package_name(raw_name, st->pkg, sizeof(st->pkg));
    st->env->ReleaseStringUTFChars(*args->nice_name, raw_name);

    int fd = st->api->connectCompanion(st->api->impl);
    if (fd < 0) {
        st->api->setOption(st->api->impl, ZYGISK_OPTION_DLCLOSE_MODULE_LIBRARY);
        return;
    }

    uint16_t len = static_cast<uint16_t>(strnlen(st->pkg, SQ2_MAX_PATTERN_LEN - 1));
    struct sq2_companion_reply reply = {};
    if (write_exact(fd, &len, sizeof(len)) && write_exact(fd, st->pkg, len)) {
        (void)read_exact(fd, &reply, sizeof(reply));
    }
    close(fd);

    if (reply.magic == SQ2_COMPANION_MAGIC && reply.active) {
        st->active = true;
        st->engine = reply.engine;
        st->mailbox_present_mode = (reply.mailbox_present_mode != 0);
        st->egl_swap_interval_zero = (reply.egl_swap_interval_zero != 0);
    } else {
        st->active = false;
        st->api->setOption(st->api->impl, ZYGISK_OPTION_DLCLOSE_MODULE_LIBRARY);
    }
}

void post_app_specialize(void *impl, const ZygiskAppSpecializeArgsV4 *) {
    auto *st = static_cast<ModuleState *>(impl);
    if (!st || !st->active) {
        return;
    }
    install_got_hooks();
    LOGI("armed Zygisk hooks for pkg=%s engine=%u mailbox=%d egl0=%d",
         st->pkg, static_cast<unsigned>(st->engine),
         st->mailbox_present_mode ? 1 : 0, st->egl_swap_interval_zero ? 1 : 0);
}

void pre_server_specialize(void *impl, ZygiskServerSpecializeArgsV4 *) {
    auto *st = static_cast<ModuleState *>(impl);
    if (st && st->api) {
        st->api->setOption(st->api->impl, ZYGISK_OPTION_DLCLOSE_MODULE_LIBRARY);
    }
}

void post_server_specialize(void *, const ZygiskServerSpecializeArgsV4 *) {}

} /* namespace */

extern "C" __attribute__((visibility("default")))
void zygisk_module_entry(const ZygiskApiTableV4 *api_table, JNIEnv *env) {
    if (!api_table || !api_table->registerModule) {
        return;
    }
    Dl_info dli = {};
    if (dladdr(reinterpret_cast<void *>(&zygisk_module_entry), &dli) != 0) {
        g_self_base = reinterpret_cast<uintptr_t>(dli.dli_fbase);
    }
    g_state = {};
    g_state.api = api_table;
    g_state.env = env;

    g_module_abi.api_version = 4;
    g_module_abi.impl = &g_state;
    g_module_abi.preAppSpecialize = pre_app_specialize;
    g_module_abi.postAppSpecialize = post_app_specialize;
    g_module_abi.preServerSpecialize = pre_server_specialize;
    g_module_abi.postServerSpecialize = post_server_specialize;

    (void)api_table->registerModule(api_table, &g_module_abi);
}

extern "C" __attribute__((visibility("default")))
void zygisk_companion_entry(int sock_fd) {
    if (sock_fd < 0) {
        return;
    }
    uint16_t len = 0;
    char pkg[SQ2_MAX_PATTERN_LEN] = {0};
    struct sq2_companion_reply reply = {};
    reply.magic = SQ2_COMPANION_MAGIC;

    if (read_exact(sock_fd, &len, sizeof(len)) && len > 0 && len < sizeof(pkg)) {
        if (read_exact(sock_fd, pkg, len)) {
            pkg[len] = '\0';
            struct sq2_opt_config cfg;
            sq2_opt_config_init_default(&cfg);
            (void)sq2_opt_config_load_file(kConfigPath, &cfg);

            enum sq2_engine_type eng = sq2_classify_app(&cfg, pkg, nullptr, nullptr, kDataAppRoot);
            if (cfg.enabled && eng != SQ2_ENGINE_NONE &&
                (cfg.mailbox_present_mode || cfg.egl_swap_interval_zero)) {
                reply.active = 1;
                reply.engine = static_cast<uint8_t>(eng);
                reply.mailbox_present_mode = cfg.mailbox_present_mode ? 1 : 0;
                reply.egl_swap_interval_zero = cfg.egl_swap_interval_zero ? 1 : 0;
            }
        }
    }

    (void)write_exact(sock_fd, &reply, sizeof(reply));
    close(sock_fd);
}
