#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/log.h>
#endif

#define SQ2_PANEL_PREFIX "/sys/class/backlight/panel"
#define SQ2_PANEL_INFIX "-backlight"
#define SQ2_TARGET_BASE "/sys/class/backlight/backlight"

typedef int (*sq2_open_fn)(const char *, int, ...);
typedef int (*sq2_access_fn)(const char *, int);
typedef int (*sq2_inotify_add_watch_fn)(int, const char *, uint32_t);

static sq2_open_fn real_sys_open = NULL;
static sq2_access_fn real_sys_access = NULL;
static sq2_inotify_add_watch_fn real_sys_inotify_add_watch = NULL;

int sq2_rewrite_panel_backlight_path(const char *path, char *out, size_t out_cap) {
    const size_t prefix_len = sizeof(SQ2_PANEL_PREFIX) - 1;
    const size_t infix_len = sizeof(SQ2_PANEL_INFIX) - 1;
    const size_t target_len = sizeof(SQ2_TARGET_BASE) - 1;
    const char *cursor;
    const char *suffix;
    size_t suffix_len;

    if (path == NULL || out == NULL || out_cap == 0) {
        return 0;
    }
    if (strncmp(path, SQ2_PANEL_PREFIX, prefix_len) != 0) {
        return 0;
    }

    cursor = path + prefix_len;
    if (*cursor < '0' || *cursor > '9') {
        return 0;
    }
    while (*cursor >= '0' && *cursor <= '9') {
        cursor++;
    }
    if (strncmp(cursor, SQ2_PANEL_INFIX, infix_len) != 0) {
        return 0;
    }

    suffix = cursor + infix_len;
    if (*suffix != '\0' && *suffix != '/') {
        return 0;
    }

    suffix_len = strlen(suffix);
    if (suffix_len > out_cap - 1 || target_len > out_cap - 1 - suffix_len) {
        return 0;
    }

    memcpy(out, SQ2_TARGET_BASE, target_len);
    memcpy(out + target_len, suffix, suffix_len + 1);
    return 1;
}

static void sq2_log_redirect_once(const char *from, const char *to) {
#ifdef __ANDROID__
    static int logged = 0;
    if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED)) {
        __android_log_print(
            ANDROID_LOG_INFO,
            "SQ2SdmBacklight",
            "Redirected missing sysfs backlight path %s -> %s",
            from,
            to
        );
    }
#else
    (void)from;
    (void)to;
#endif
}

static void *sq2_load_real(void **cache_slot, const char *libc_name) {
    void *fn = __atomic_load_n(cache_slot, __ATOMIC_ACQUIRE);
    if (fn != NULL) {
        return fn;
    }
    fn = dlsym(RTLD_NEXT, libc_name);
    if (fn != NULL) {
        __atomic_store_n(cache_slot, fn, __ATOMIC_RELEASE);
    }
    return fn;
}

static int sq2_call_open(sq2_open_fn fn, const char *path, int flags, int needs_mode, mode_t mode) {
    return needs_mode ? fn(path, flags, mode) : fn(path, flags);
}

static int sq2_sdm_open(const char *path, int flags, ...) {
    char alt[512];
    int needs_mode = (flags & O_CREAT) != 0;
    mode_t mode = 0;
    sq2_open_fn fn;
    int fd;

#ifdef O_TMPFILE
    if ((flags & O_TMPFILE) == O_TMPFILE) {
        needs_mode = 1;
    }
#endif
    if (needs_mode) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, unsigned int);
        va_end(ap);
    }

    fn = (sq2_open_fn)sq2_load_real((void **)&real_sys_open, "open");
    if (fn == NULL) {
        errno = ENOSYS;
        return -1;
    }

    fd = sq2_call_open(fn, path, flags, needs_mode, mode);
    if (fd < 0 && errno == ENOENT && sq2_rewrite_panel_backlight_path(path, alt, sizeof(alt))) {
        fd = sq2_call_open(fn, alt, flags, needs_mode, mode);
        if (fd >= 0) {
            sq2_log_redirect_once(path, alt);
        }
    }
    return fd;
}

static int sq2_sdm_access(const char *path, int mode) {
    char alt[512];
    sq2_access_fn fn = (sq2_access_fn)sq2_load_real((void **)&real_sys_access, "access");
    int rc;

    if (fn == NULL) {
        errno = ENOSYS;
        return -1;
    }

    rc = fn(path, mode);
    if (rc < 0 && errno == ENOENT && sq2_rewrite_panel_backlight_path(path, alt, sizeof(alt))) {
        rc = fn(alt, mode);
        if (rc >= 0) {
            sq2_log_redirect_once(path, alt);
        }
    }
    return rc;
}

static int sq2_sdm_inotify_add_watch(int inotify_fd, const char *path, uint32_t mask) {
    char alt[512];
    sq2_inotify_add_watch_fn fn = (sq2_inotify_add_watch_fn)sq2_load_real(
        (void **)&real_sys_inotify_add_watch,
        "inotify_add_watch"
    );
    int wd;

    if (fn == NULL) {
        errno = ENOSYS;
        return -1;
    }

    wd = fn(inotify_fd, path, mask);
    if (wd < 0 && errno == ENOENT && sq2_rewrite_panel_backlight_path(path, alt, sizeof(alt))) {
        wd = fn(inotify_fd, alt, mask);
        if (wd >= 0) {
            sq2_log_redirect_once(path, alt);
        }
    }
    return wd;
}

struct sq2_sys_hook {
    const char *symbol;
    void **real_slot;
    void *wrapper;
};

static void *sq2_find_sys_slot(void *sdmutils_handle, const char *symbol) {
    void *addr = NULL;
    if (sdmutils_handle != NULL) {
        addr = dlsym(sdmutils_handle, symbol);
    }
    if (addr == NULL) {
        addr = dlsym(RTLD_DEFAULT, symbol);
    }
    return addr;
}

__attribute__((constructor)) void sq2_install_sdm_backlight_hooks(void) {
    const struct sq2_sys_hook hooks[] = {
        {"_ZN3sdm3Sys5open_E", (void **)&real_sys_open, (void *)&sq2_sdm_open},
        {"_ZN3sdm3Sys7access_E", (void **)&real_sys_access, (void *)&sq2_sdm_access},
        {
            "_ZN3sdm3Sys18inotify_add_watch_E",
            (void **)&real_sys_inotify_add_watch,
            (void *)&sq2_sdm_inotify_add_watch,
        },
    };
    void *sdmutils = dlopen("libsdmutils.so", RTLD_NOLOAD | RTLD_NOW);
    size_t i;

    (void)dlerror();
    for (i = 0; i < sizeof(hooks) / sizeof(hooks[0]); i++) {
        void **slot = (void **)sq2_find_sys_slot(sdmutils, hooks[i].symbol);
        void *orig;
        if (slot == NULL) {
            continue;
        }
        orig = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
        if (orig != NULL && orig != hooks[i].wrapper) {
            __atomic_store_n(hooks[i].real_slot, orig, __ATOMIC_RELEASE);
            __atomic_store_n(slot, hooks[i].wrapper, __ATOMIC_RELEASE);
        }
    }
    if (sdmutils != NULL) {
        dlclose(sdmutils);
    }
}
