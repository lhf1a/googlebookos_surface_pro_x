"""Host ABI/behavior tests for libsq2_sdm_backlight.so and build_sdm_backlight_shim.py."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))

import build_sdm_backlight_shim as builder


class SdmBacklightShimTest(unittest.TestCase):
    def test_reject_non_elf_sdmcore(self):
        with self.assertRaises(ValueError):
            builder.patch_sdmcore_elf(b'not-an-elf')

    def test_build_android_shim_and_patch_real_sdmcore_if_present(self):
        shim_bytes = builder.build_bytes()
        self.assertGreater(len(shim_bytes), 4096)
        self.assertEqual(shim_bytes[:6], b'\x7fELF\x02\x01')

        sdmcore = ROOT / (
            'private/build-inputs/full-image-runtime/files/etc/sq2-qti-system/root/'
            'lib64/sq2-qti-display/libsdmcore.so'
        )
        if not sdmcore.is_file():
            return
        payload = builder.build_payload(sdmcore.read_bytes())
        self.assertEqual(
            set(payload),
            {builder.SDMCORE_REL, builder.SHIM_REL},
        )
        with tempfile.TemporaryDirectory() as td:
            patched = Path(td) / 'libsdmcore.so'
            patched.write_bytes(payload[builder.SDMCORE_REL])
            needed = subprocess.check_output(['patchelf', '--print-needed', str(patched)], text=True)
            self.assertIn(builder.SHIM_NAME, needed.splitlines())
            # Second patch pass must be idempotent.
            patched2 = builder.patch_sdmcore_elf(payload[builder.SDMCORE_REL])
            self.assertEqual(patched2, payload[builder.SDMCORE_REL])

    def test_host_rewrite_and_sys_hook_behavior(self):
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            mock_sdmutils_c = d / 'mock_sdmutils.c'
            mock_sdmutils_c.write_text(
                r"""
#include <stdint.h>

extern void *fake_open;
extern void *fake_access;
extern void *fake_inotify;

int (*_ZN3sdm3Sys5open_E)(const char *, int, ...) = 0;
int (*_ZN3sdm3Sys7access_E)(const char *, int) = 0;
int (*_ZN3sdm3Sys18inotify_add_watch_E)(int, const char *, uint32_t) = 0;

void mock_sdm_reset(void *open_fn, void *access_fn, void *inotify_fn) {
    _ZN3sdm3Sys5open_E = (int (*)(const char *, int, ...))open_fn;
    _ZN3sdm3Sys7access_E = (int (*)(const char *, int))access_fn;
    _ZN3sdm3Sys18inotify_add_watch_E = (int (*)(int, const char *, uint32_t))inotify_fn;
}

int mock_call_open(const char *path, int flags, int mode) {
    return _ZN3sdm3Sys5open_E(path, flags, mode);
}

int mock_call_access(const char *path, int mode) {
    return _ZN3sdm3Sys7access_E(path, mode);
}

int mock_call_inotify(int fd, const char *path, uint32_t mask) {
    return _ZN3sdm3Sys18inotify_add_watch_E(fd, path, mask);
}
"""
            )
            harness_c = d / 'harness.c'
            harness_c.write_text(
                r"""
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern int sq2_rewrite_panel_backlight_path(const char *path, char *out, size_t out_cap);
extern void sq2_install_sdm_backlight_hooks(void);

extern void mock_sdm_reset(void *open_fn, void *access_fn, void *inotify_fn);
extern int mock_call_open(const char *path, int flags, int mode);
extern int mock_call_access(const char *path, int mode);
extern int mock_call_inotify(int fd, const char *path, uint32_t mask);

static int g_open_calls = 0;
static char g_last_open_path[2][256];
static int g_last_open_flags = 0;
static int g_last_open_mode = 0;
static int g_panel0_exists = 0;
static int g_panel0_errno = ENOENT;
static int g_fallback_errno = 0;

static int fake_open(const char *path, int flags, ...) {
    int mode = 0;
    if (flags & O_CREAT) {
        __builtin_va_list ap;
        __builtin_va_start(ap, flags);
        mode = __builtin_va_arg(ap, int);
        __builtin_va_end(ap);
    }
    if (g_open_calls < 2) {
        snprintf(g_last_open_path[g_open_calls], sizeof(g_last_open_path[0]), "%s", path);
    }
    g_open_calls++;
    g_last_open_flags = flags;
    g_last_open_mode = mode;

    if (strcmp(path, "/sys/class/backlight/panel0-backlight/max_brightness") == 0) {
        if (g_panel0_exists) {
            return 41;
        }
        errno = g_panel0_errno;
        return -1;
    }
    if (strcmp(path, "/sys/class/backlight/backlight/max_brightness") == 0) {
        if (g_fallback_errno != 0) {
            errno = g_fallback_errno;
            return -1;
        }
        return 42;
    }
    if (strcmp(path, "/sys/class/backlight/backlight/brightness") == 0) {
        return 43;
    }
    errno = ENOENT;
    return -1;
}

static int g_access_calls = 0;
static char g_last_access_path[256];

static int fake_access(const char *path, int mode) {
    (void)mode;
    g_access_calls++;
    snprintf(g_last_access_path, sizeof(g_last_access_path), "%s", path);
    if (strcmp(path, "/sys/class/backlight/backlight/brightness") == 0) {
        return 0;
    }
    errno = ENOENT;
    return -1;
}

static int g_inotify_calls = 0;
static char g_last_inotify_path[256];

static int fake_inotify(int fd, const char *path, uint32_t mask) {
    (void)fd;
    (void)mask;
    g_inotify_calls++;
    snprintf(g_last_inotify_path, sizeof(g_last_inotify_path), "%s", path);
    if (strcmp(path, "/sys/class/backlight/backlight/brightness") == 0) {
        return 7;
    }
    errno = ENOENT;
    return -1;
}

static void assert_rewrites(void) {
    char buf[256];
    char tiny[10];

    assert(sq2_rewrite_panel_backlight_path(
        "/sys/class/backlight/panel0-backlight/max_brightness", buf, sizeof(buf)) == 1);
    assert(strcmp(buf, "/sys/class/backlight/backlight/max_brightness") == 0);

    assert(sq2_rewrite_panel_backlight_path(
        "/sys/class/backlight/panel12-backlight/brightness", buf, sizeof(buf)) == 1);
    assert(strcmp(buf, "/sys/class/backlight/backlight/brightness") == 0);

    assert(sq2_rewrite_panel_backlight_path(
        "/sys/class/backlight/panel0-backlight/", buf, sizeof(buf)) == 1);
    assert(strcmp(buf, "/sys/class/backlight/backlight/") == 0);

    assert(sq2_rewrite_panel_backlight_path(
        "/sys/class/backlight/panel0-backlight", buf, sizeof(buf)) == 1);
    assert(strcmp(buf, "/sys/class/backlight/backlight") == 0);

    /* Exact boundary cap check (41 + 1 bytes for .../brightness) */
    assert(sq2_rewrite_panel_backlight_path(
        "/sys/class/backlight/panel0-backlight/brightness", buf, 41) == 0);
    assert(sq2_rewrite_panel_backlight_path(
        "/sys/class/backlight/panel0-backlight/brightness", buf, 42) == 1);

    /* Non-matching paths and invalid inputs */
    assert(sq2_rewrite_panel_backlight_path(NULL, buf, sizeof(buf)) == 0);
    assert(sq2_rewrite_panel_backlight_path("/sys/class/backlight/backlight/brightness", NULL, 100) == 0);
    assert(sq2_rewrite_panel_backlight_path("/sys/class/backlight/backlight/brightness", buf, 0) == 0);
    assert(sq2_rewrite_panel_backlight_path("/sys/class/backlight/backlight/brightness", buf, sizeof(buf)) == 0);
    assert(sq2_rewrite_panel_backlight_path("/sys/class/backlight/panel-backlight/brightness", buf, sizeof(buf)) == 0);
    assert(sq2_rewrite_panel_backlight_path("/sys/class/backlight/panela-backlight/brightness", buf, sizeof(buf)) == 0);
    assert(sq2_rewrite_panel_backlight_path("/sys/class/backlight/panel0-backlightx/brightness", buf, sizeof(buf)) == 0);
    assert(sq2_rewrite_panel_backlight_path("/dev/dri/card0", buf, sizeof(buf)) == 0);
    assert(sq2_rewrite_panel_backlight_path("/sys/class/backlight/panel0-backlight/brightness", tiny, sizeof(tiny)) == 0);
}

int main(void) {
    assert_rewrites();

    mock_sdm_reset((void *)&fake_open, (void *)&fake_access, (void *)&fake_inotify);
    sq2_install_sdm_backlight_hooks();
    /* Calling constructor/installer twice must remain idempotent and not lose real_sys_*. */
    sq2_install_sdm_backlight_hooks();

    /* Case 1: panel0-backlight missing (ENOENT) -> falls back to backlight/max_brightness */
    g_open_calls = 0;
    g_panel0_exists = 0;
    g_panel0_errno = ENOENT;
    g_fallback_errno = 0;
    assert(mock_call_open("/sys/class/backlight/panel0-backlight/max_brightness", O_RDONLY, 0) == 42);
    assert(g_open_calls == 2);
    assert(strcmp(g_last_open_path[0], "/sys/class/backlight/panel0-backlight/max_brightness") == 0);
    assert(strcmp(g_last_open_path[1], "/sys/class/backlight/backlight/max_brightness") == 0);

    /* Case 2: O_CREAT forwards mode to fallback open */
    g_open_calls = 0;
    assert(mock_call_open("/sys/class/backlight/panel3-backlight/brightness", O_RDWR | O_CREAT, 0640) == 43);
    assert(g_open_calls == 2);
    assert(g_last_open_flags == (O_RDWR | O_CREAT));
    assert(g_last_open_mode == 0640);

    /* Case 3: panel0-backlight exists -> never redirects to backlight/ */
    g_open_calls = 0;
    g_panel0_exists = 1;
    assert(mock_call_open("/sys/class/backlight/panel0-backlight/max_brightness", O_RDONLY, 0) == 41);
    assert(g_open_calls == 1);

    /* Case 4: Non-ENOENT error on primary node is NOT swallowed by fallback */
    g_open_calls = 0;
    g_panel0_exists = 0;
    g_panel0_errno = EACCES;
    errno = 0;
    assert(mock_call_open("/sys/class/backlight/panel0-backlight/max_brightness", O_RDONLY, 0) == -1);
    assert(errno == EACCES);
    assert(g_open_calls == 1);

    /* Case 5: access() and inotify_add_watch() fallback on ENOENT */
    g_access_calls = 0;
    assert(mock_call_access("/sys/class/backlight/panel0-backlight/brightness", R_OK | W_OK) == 0);
    assert(g_access_calls == 2);
    assert(strcmp(g_last_access_path, "/sys/class/backlight/backlight/brightness") == 0);

    g_inotify_calls = 0;
    assert(mock_call_inotify(9, "/sys/class/backlight/panel0-backlight/brightness", 2) == 7);
    assert(g_inotify_calls == 2);
    assert(strcmp(g_last_inotify_path, "/sys/class/backlight/backlight/brightness") == 0);

    puts("SDM_BACKLIGHT_HOST_TESTS_OK");
    return 0;
}
"""
            )
            mock_so = d / 'libsdmutils.so'
            shim_so = d / 'libsq2_sdm_backlight.so'
            harness_bin = d / 'harness'

            subprocess.run(
                ['cc', '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror', str(mock_sdmutils_c), '-o', str(mock_so)],
                check=True,
            )
            subprocess.run(
                [
                    'cc',
                    '-std=c11',
                    '-shared',
                    '-fPIC',
                    '-Wall',
                    '-Wextra',
                    '-Werror',
                    str(builder.SOURCE),
                    '-ldl',
                    '-o',
                    str(shim_so),
                ],
                check=True,
            )
            subprocess.run(
                [
                    'cc',
                    '-std=c11',
                    '-Wall',
                    '-Wextra',
                    '-Werror',
                    str(harness_c),
                    str(shim_so),
                    str(mock_so),
                    f'-Wl,-rpath,{td}',
                    '-o',
                    str(harness_bin),
                ],
                check=True,
            )
            out = subprocess.check_output([str(harness_bin)], text=True)
            self.assertIn('SDM_BACKLIGHT_HOST_TESTS_OK', out)


if __name__ == '__main__':
    unittest.main()
