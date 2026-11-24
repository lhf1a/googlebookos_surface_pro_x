"""Host tests for Mesa 26.2.4 Freedreno MSAA Z/S GMEM fix and Android EGL UBWC patch."""
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MESA_TAR = ROOT / "private/dependency-import/downloads/mesa-26.2.4.tar.xz"
GMEM_PATCH = ROOT / "patches/mesa-26.2.4-freedreno-msaa-zsbuf-gmem.patch"
UBWC_PATCH = ROOT / "patches/mesa-26.2.4-android-egl-ubwc.patch"
BLOB_GET_PATCH = ROOT / "patches/mesa-26.2.4-disk-cache-blob-get-two-step.patch"


def extract_fn(source, start_re):
    m = re.search(start_re, source, re.M)
    if not m:
        raise AssertionError(f"Function not found: {start_re}")
    left = source.index("{", m.start())
    depth, end = 1, left + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[m.start():end]


class MesaFreedrenoPerfPatchesTest(unittest.TestCase):
    def test_bootstrap_wires_patches(self):
        bootstrap = (ROOT / "scripts/hybrid_bootstrap_mesa.py").read_text()
        self.assertIn("patches/mesa-26.2.4-freedreno-msaa-zsbuf-gmem.patch", bootstrap)
        self.assertIn("patches/mesa-26.2.4-android-egl-ubwc.patch", bootstrap)

    def test_patches_apply_cleanly_and_behavior(self):
        self.assertTrue(MESA_TAR.is_file(), "Mesa 26.2.4 archive missing")
        targets = {
            "mesa-26.2.4/src/gallium/drivers/freedreno/freedreno_gmem.c",
            "mesa-26.2.4/src/gallium/drivers/freedreno/a6xx/fd6_resource.cc",
            "mesa-26.2.4/src/egl/drivers/dri2/platform_android.c",
        }
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            with tarfile.open(MESA_TAR) as tar:
                members = [m for m in tar.getmembers() if m.name in targets]
                self.assertEqual(len(members), len(targets))
                tar.extractall(d, members=members)
            src = d / "mesa-26.2.4"
            for patch in (GMEM_PATCH, UBWC_PATCH):
                subprocess.run(
                    ["patch", "--batch", "--fuzz=0", "-p1", "-d", str(src), "-i", str(patch)],
                    check=True,
                    capture_output=True,
                    text=True,
                )

            gmem_src = (src / "src/gallium/drivers/freedreno/freedreno_gmem.c").read_text()
            fd6_src = (src / "src/gallium/drivers/freedreno/a6xx/fd6_resource.cc").read_text()
            egl_src = (src / "src/egl/drivers/dri2/platform_android.c").read_text()

            # Verify force_ubwc on explicit QCOM_COMPRESSED import in fd6_resource.cc
            self.assertIn("params.force_ubwc = true;", fd6_src)

            # Compile and run real extracted layout_gmem + calc_nbins + zsbuf_cpp formula + droid_wants_qti_ubwc
            layout_fn = extract_fn(gmem_src, r"^static bool\nlayout_gmem\(")
            calc_fn = extract_fn(gmem_src, r"^static void\ncalc_nbins\(")
            div_fn = extract_fn(gmem_src, r"^static unsigned\ndiv_align\(")
            fmt_fn = extract_fn(egl_src, r"^static bool\ndroid_format_supports_qti_ubwc\(")
            wants_fn = extract_fn(egl_src, r"^static bool\ndroid_wants_qti_ubwc\(")

            # Verify gmem_key_init expression matches exact formula
            self.assertIn(
                "(rsc->layout.cpp / fd_resource_nr_samples(&rsc->b.b)) * pfb->samples",
                gmem_src,
            )
            self.assertIn(
                "(rsc->stencil->layout.cpp / fd_resource_nr_samples(&rsc->stencil->b.b)) *",
                gmem_src,
            )

            harness = d / "test_mesa_perf.c"
            harness.write_text(f"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_RENDER_TARGETS 8
#define DIV_ROUND_UP(A, B) (((A) + (B) - 1) / (B))
#define FD_DBG(x) 0
#define debug_printf(...) ((void)0)
#define PROP_VALUE_MAX 92
#define HAL_PIXEL_FORMAT_RGBA_8888 1
#define HAL_PIXEL_FORMAT_RGBX_8888 2
#define HAL_PIXEL_FORMAT_RGB_565 4
#define HAL_PIXEL_FORMAT_RGBA_FP16 22
#define HAL_PIXEL_FORMAT_RGBA_1010102 43
#define HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED 0x22
#define HAL_PIXEL_FORMAT_YCbCr_420_888 0x23

static unsigned util_align_npot(unsigned v, unsigned a) {{
    return ((v + a - 1) / a) * a;
}}

struct fd_dev_info {{
    uint32_t tile_align_w, tile_align_h, tile_max_w, tile_max_h, num_ccu;
}};
struct fd_screen {{
    const struct fd_dev_info *info;
    uint32_t gmemsize_bytes;
}};
struct gmem_key {{
    uint32_t width, height, gmem_page_align, nr_cbufs;
    uint8_t cbuf_cpp[MAX_RENDER_TARGETS];
    uint8_t zsbuf_cpp[2];
}};
struct fd_gmem_stateobj {{
    struct fd_screen *screen;
    uint32_t bin_w, bin_h, nbins_x, nbins_y;
    uint32_t cbuf_base[MAX_RENDER_TARGETS];
    uint32_t zsbuf_base[2];
}};

{div_fn}
{layout_fn}
{calc_fn}

struct dri2_egl_display {{
    const char *driver_name;
}};
static const char *g_disable_ubwc = "0";
static int __system_property_get(const char *name, char *val) {{
    if (strcmp(name, "vendor.gralloc.disable_ubwc") == 0) {{
        strcpy(val, g_disable_ubwc);
        return (int)strlen(val);
    }}
    val[0] = 0;
    return 0;
}}

{fmt_fn}
{wants_fn}

static uint8_t compute_zs_cpp(uint32_t single_bpp, uint32_t rsc_samples, uint32_t pfb_samples) {{
    uint32_t nr = rsc_samples > 0 ? rsc_samples : 1;
    uint32_t layout_cpp = single_bpp * nr;
    return (uint8_t)((layout_cpp / nr) * pfb_samples);
}}

int main(void) {{
    /* A680 hardware parameters */
    struct fd_dev_info a680 = {{ 64, 32, 1024, 1024, 4 }};
    struct fd_screen scr = {{ &a680, 2 * 1024 * 1024 }};

    /* 1. Verify zsbuf_cpp across 1x, 4x, 8x MSAA, MSRTT, and separate stencil */
    assert(compute_zs_cpp(4, 1, 1) == 4);
    assert(compute_zs_cpp(4, 4, 4) == 16);
    assert(compute_zs_cpp(4, 8, 8) == 32); /* old bug overflowed uint8_t (256 -> 0)! */
    assert(compute_zs_cpp(4, 1, 4) == 16); /* MSRTT */
    assert(compute_zs_cpp(1, 4, 4) == 4);  /* separate S8 stencil */

    /* 2. Windowed BaiduMap 2073x1307 4x MSAA RGBA8 + Z24S8: 121 -> 49 tiles */
    struct gmem_key k1 = {{ .width = 2073, .height = 1307, .gmem_page_align = 4, .nr_cbufs = 1 }};
    k1.cbuf_cpp[0] = 16;
    k1.zsbuf_cpp[0] = compute_zs_cpp(4, 4, 4);
    struct fd_gmem_stateobj g1 = {{ .screen = &scr }};
    calc_nbins(&k1, &g1);
    assert(g1.nbins_x == 7 && g1.nbins_y == 7);
    assert(g1.bin_w == 320 && g1.bin_h == 192);
    assert(g1.cbuf_base[0] == 0 && g1.zsbuf_base[0] == 320 * 192 * 16);
    assert(g1.zsbuf_base[0] + 320 * 192 * 16 <= scr.gmemsize_bytes);

    /* 3. Fullscreen BaiduMap 2880x1672 4x MSAA RGBA8 + Z24S8: 210 -> 81 tiles */
    struct gmem_key k2 = {{ .width = 2880, .height = 1672, .gmem_page_align = 4, .nr_cbufs = 1 }};
    k2.cbuf_cpp[0] = 16;
    k2.zsbuf_cpp[0] = compute_zs_cpp(4, 4, 4);
    struct fd_gmem_stateobj g2 = {{ .screen = &scr }};
    calc_nbins(&k2, &g2);
    assert(g2.nbins_x == 9 && g2.nbins_y == 9);
    assert(g2.bin_w == 320 && g2.bin_h == 192);

    /* 4. Android EGL UBWC usage gating */
    struct dri2_egl_display dpy_msm = {{ "msm" }};
    struct dri2_egl_display dpy_sw = {{ "swrast" }};
    g_disable_ubwc = "0";
    assert(droid_wants_qti_ubwc(&dpy_msm, HAL_PIXEL_FORMAT_RGBA_8888));
    assert(droid_wants_qti_ubwc(&dpy_msm, HAL_PIXEL_FORMAT_RGBX_8888));
    assert(droid_wants_qti_ubwc(&dpy_msm, HAL_PIXEL_FORMAT_RGBA_FP16));
    assert(droid_wants_qti_ubwc(&dpy_msm, HAL_PIXEL_FORMAT_RGBA_1010102));
    assert(!droid_wants_qti_ubwc(&dpy_msm, HAL_PIXEL_FORMAT_RGB_565));
    assert(!droid_wants_qti_ubwc(&dpy_msm, HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED));
    assert(!droid_wants_qti_ubwc(&dpy_msm, HAL_PIXEL_FORMAT_YCbCr_420_888));
    assert(!droid_wants_qti_ubwc(&dpy_msm, 0x7fa30c04));
    assert(!droid_wants_qti_ubwc(&dpy_sw, HAL_PIXEL_FORMAT_RGBA_8888));
    g_disable_ubwc = "1";
    assert(!droid_wants_qti_ubwc(&dpy_msm, HAL_PIXEL_FORMAT_RGBA_8888));

    printf("MESA_PERF_UNIT_TESTS_OK bins_win=%ux%u(%ux%u) bins_full=%ux%u(%ux%u)\\n",
           g1.nbins_x, g1.nbins_y, g1.bin_w, g1.bin_h,
           g2.nbins_x, g2.nbins_y, g2.bin_w, g2.bin_h);
    return 0;
}}
""")
            binary = d / "test_mesa_perf"
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(harness), "-o", str(binary)],
                check=True,
            )
            res = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn("bins_win=7x7(320x192)", res.stdout)
            self.assertIn("bins_full=9x9(320x192)", res.stdout)


    def test_disk_cache_blob_get_two_step_patch(self):
        self.assertTrue(MESA_TAR.is_file(), "Mesa 26.2.4 archive missing")
        target = "mesa-26.2.4/src/util/disk_cache.c"
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            with tarfile.open(MESA_TAR) as tar:
                member = tar.getmember(target)
                tar.extractall(d, members=[member], filter="data")
            src = d / "mesa-26.2.4"
            subprocess.run(
                ["patch", "--batch", "--fuzz=0", "-p1", "-d", str(src), "-i", str(BLOB_GET_PATCH)],
                check=True,
                capture_output=True,
                text=True,
            )
            dc_src = (src / "src/util/disk_cache.c").read_text()
            blob_get_fn = extract_fn(dc_src, r"^static void \*\nblob_get_compressed\(")

            harness = d / "test_blob_get.c"
            harness.write_text(f"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_KEY_SIZE 32
#define MESA_TRACE_FUNC() ((void)0)
#define MESA_TRACE_SCOPE(name) ((void)0)

typedef uint8_t cache_key[CACHE_KEY_SIZE];
typedef signed long (*disk_cache_get_cb)(const void *key, signed long key_size,
                                         void *value, signed long value_size);

struct disk_cache {{
    disk_cache_get_cb blob_get_cb;
}};

struct blob_cache_entry {{
    uint32_t uncompressed_size;
    uint8_t compressed_data[];
}};

static bool g_fail_inflate = false;
static bool util_compress_inflate(const uint8_t *in_data, size_t in_data_size,
                                  uint8_t *out_data, size_t out_data_size) {{
    if (g_fail_inflate || in_data_size != out_data_size)
        return false;
    for (size_t i = 0; i < out_data_size; ++i)
        out_data[i] = (uint8_t)(in_data[i] ^ 0x5Au);
    return true;
}}

{blob_get_fn}

struct mock_entry {{
    bool present;
    uint8_t key[CACHE_KEY_SIZE];
    uint8_t *raw;
    signed long raw_size;
}};

static struct mock_entry g_entry;
static int g_size_queries = 0;
static int g_data_reads = 0;
static int g_mismatches = 0;
static int g_evict_after_size_query = 0;
static signed long g_override_size_ret = -999;
static signed long g_override_read_ret = -999;

/* Faithful Android 15 libEGL MultifileBlobCache::get() mock:
 *  - valueSize < cached -> returns cached size without reading disk / touching value
 *  - valueSize >= cached -> requires exact header->valueSize == valueSize,
 *    otherwise logs mismatch, evicts entry, and returns 0!
 */
static signed long mock_multifile_get(const void *key, signed long key_size,
                                      void *value, signed long value_size) {{
    assert(key_size == CACHE_KEY_SIZE);
    if (value_size == 0) {{
        g_size_queries++;
        if (g_override_size_ret != -999)
            return g_override_size_ret;
    }} else {{
        g_data_reads++;
        if (g_override_read_ret != -999)
            return g_override_read_ret;
    }}
    if (!g_entry.present || memcmp(key, g_entry.key, CACHE_KEY_SIZE) != 0)
        return 0;
    if (g_entry.raw_size > value_size) {{
        if (g_evict_after_size_query) {{
            g_entry.present = false;
            g_evict_after_size_query = 0;
        }}
        return g_entry.raw_size;
    }}
    if (g_entry.raw_size != value_size) {{
        g_mismatches++;
        g_entry.present = false; /* AOSP evicts on valueSize mismatch */
        return 0;
    }}
    assert(value != NULL);
    memcpy(value, g_entry.raw, (size_t)g_entry.raw_size);
    return g_entry.raw_size;
}}

/* Faithful single-file BlobCache::get() mock */
static signed long mock_singlefile_get(const void *key, signed long key_size,
                                       void *value, signed long value_size) {{
    assert(key_size == CACHE_KEY_SIZE);
    if (!g_entry.present || memcmp(key, g_entry.key, CACHE_KEY_SIZE) != 0)
        return 0;
    if (g_entry.raw_size > value_size)
        return g_entry.raw_size;
    assert(value != NULL);
    memcpy(value, g_entry.raw, (size_t)g_entry.raw_size);
    return g_entry.raw_size;
}}

static void seed_entry(const cache_key key, uint32_t payload_size, uint32_t hdr_uncomp_size) {{
    free(g_entry.raw);
    signed long total = (signed long)(sizeof(struct blob_cache_entry) + payload_size);
    struct blob_cache_entry *e = malloc((size_t)total);
    assert(e != NULL);
    e->uncompressed_size = hdr_uncomp_size;
    for (uint32_t i = 0; i < payload_size; ++i)
        e->compressed_data[i] = (uint8_t)(((i * 31u) & 0xFFu) ^ 0x5Au);
    memcpy(g_entry.key, key, CACHE_KEY_SIZE);
    g_entry.raw = (uint8_t *)e;
    g_entry.raw_size = total;
    g_entry.present = true;
}}

int main(void) {{
    cache_key key;
    for (int i = 0; i < CACHE_KEY_SIZE; ++i) key[i] = (uint8_t)(0xA0 + i);
    struct disk_cache cache = {{ .blob_get_cb = mock_multifile_get }};

    /* 1. Typical 309-byte MultifileBlobCache entry (previously evicted on 65536 != 309) */
    seed_entry(key, 305, 305);
    size_t out_sz = 0;
    uint8_t *got = blob_get_compressed(&cache, key, &out_sz);
    assert(got != NULL && out_sz == 305);
    assert(g_mismatches == 0 && g_entry.present);
    for (size_t i = 0; i < out_sz; ++i)
        assert(got[i] == (uint8_t)((i * 31u) & 0xFFu));
    free(got);

    /* Repeat lookup must still hit (not evicted) and handle size == NULL */
    got = blob_get_compressed(&cache, key, NULL);
    assert(got != NULL && g_mismatches == 0 && g_entry.present);
    free(got);

    /* 2. Large compressed shader > 64 KiB (96 KiB) */
    seed_entry(key, 96 * 1024, 96 * 1024);
    out_sz = 0;
    got = blob_get_compressed(&cache, key, &out_sz);
    assert(got != NULL && out_sz == 96 * 1024 && g_mismatches == 0);
    assert(got[0] == 0 && got[96 * 1024 - 1] == (uint8_t)(((96 * 1024 - 1) * 31u) & 0xFFu));
    free(got);

    /* 3. Cache miss: only size query called, zero data reads */
    g_entry.present = false;
    int reads_before = g_data_reads;
    got = blob_get_compressed(&cache, key, &out_sz);
    assert(got == NULL && g_data_reads == reads_before);

    /* 4. Corrupt/tiny sizes (0, -1, 4 bytes) and > 64 MiB rejected before malloc/read */
    signed long bad_sizes[] = {{ -1, 0, 1, 4, (64L * 1024L * 1024L) + 1L }};
    for (size_t i = 0; i < sizeof(bad_sizes) / sizeof(bad_sizes[0]); ++i) {{
        g_override_size_ret = bad_sizes[i];
        reads_before = g_data_reads;
        got = blob_get_compressed(&cache, key, &out_sz);
        assert(got == NULL && g_data_reads == reads_before);
    }}
    g_override_size_ret = -999;

    /* 5. Concurrent eviction between size query and data read */
    seed_entry(key, 512, 512);
    g_evict_after_size_query = 1;
    got = blob_get_compressed(&cache, key, &out_sz);
    assert(got == NULL && g_mismatches == 0);

    /* 6. Concurrent size change between step 1 and step 2 */
    seed_entry(key, 512, 512);
    g_override_read_ret = 256;
    got = blob_get_compressed(&cache, key, &out_sz);
    assert(got == NULL);
    g_override_read_ret = 1024;
    got = blob_get_compressed(&cache, key, &out_sz);
    assert(got == NULL);
    g_override_read_ret = -999;

    /* 7. Corrupt header with uncompressed_size == 0 */
    seed_entry(key, 64, 0);
    got = blob_get_compressed(&cache, key, &out_sz);
    assert(got == NULL);

    /* 8. Inflate failure */
    seed_entry(key, 64, 64);
    g_fail_inflate = true;
    got = blob_get_compressed(&cache, key, &out_sz);
    assert(got == NULL);
    g_fail_inflate = false;

    /* 9. Single-file BlobCache fallback works for both small and > 64 KiB */
    cache.blob_get_cb = mock_singlefile_get;
    seed_entry(key, 80 * 1024, 80 * 1024);
    out_sz = 0;
    got = blob_get_compressed(&cache, key, &out_sz);
    assert(got != NULL && out_sz == 80 * 1024);
    free(got);

    free(g_entry.raw);
    printf("MESA_BLOB_GET_TWO_STEP_OK size_queries=%d data_reads=%d mismatches=%d\\n",
           g_size_queries, g_data_reads, g_mismatches);
    return 0;
}}
""")
            binary = d / "test_blob_get"
            cmd = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", str(harness), "-o", str(binary)]
            build = subprocess.run(cmd, capture_output=True, text=True)
            if build.returncode != 0:
                subprocess.run(
                    ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(harness), "-o", str(binary)],
                    check=True,
                )
            res = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn("MESA_BLOB_GET_TWO_STEP_OK", res.stdout)
            self.assertIn("mismatches=0", res.stdout)

if __name__ == "__main__":
    unittest.main()
