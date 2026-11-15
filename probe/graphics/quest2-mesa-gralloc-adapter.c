/*
 * Lightweight HAL shim adapting Mesa's u_gralloc_cros_api (gralloc.default.so
 * / "CrOS Gralloc" perform opcodes 4 and 6) to QTI's gralloc.kona.so +
 * libgrallocutils.so + libqdMetaData.so closure.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef __ANDROID__
#include <android/log.h>
#define SQ2_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "sq2-mesa-gralloc", __VA_ARGS__)
#else
#include <stdio.h>
#define SQ2_LOGE(fmt, ...) fprintf(stderr, "sq2-mesa-gralloc: " fmt "\n", ##__VA_ARGS__)
#endif

/* ---- Binary ABI types matching Android hardware/hardware.h + gralloc.h ---- */

typedef struct sq2_native_handle {
    int version;
    int numFds;
    int numInts;
    int data[];
} sq2_native_handle_t;

typedef const sq2_native_handle_t *sq2_buffer_handle_t;

struct hw_module_t;
struct hw_device_t;

typedef struct sq2_hw_module_methods {
    int (*open)(const struct hw_module_t *module, const char *id, struct hw_device_t **device);
} sq2_hw_module_methods_t;

struct hw_device_t {
    uint32_t tag;
    uint32_t version;
    struct hw_module_t *module;
    uint64_t reserved[12];
    int (*close)(struct hw_device_t *device);
};

struct hw_module_t {
    uint32_t tag;
    uint16_t module_api_version;
    uint16_t hal_api_version;
    const char *id;
    const char *name;
    const char *author;
    sq2_hw_module_methods_t *methods;
    void *dso;
    uint64_t reserved[32 - 7];
};

typedef struct sq2_gralloc_module {
    struct hw_module_t common;
    int (*registerBuffer)(const struct sq2_gralloc_module *module, sq2_buffer_handle_t handle);
    int (*unregisterBuffer)(const struct sq2_gralloc_module *module, sq2_buffer_handle_t handle);
    int (*lock)(const struct sq2_gralloc_module *module, sq2_buffer_handle_t handle,
                int usage, int l, int t, int w, int h, void **vaddr);
    int (*unlock)(const struct sq2_gralloc_module *module, sq2_buffer_handle_t handle);
    int (*perform)(const struct sq2_gralloc_module *module, int operation, ...);
} sq2_gralloc_module_t;

_Static_assert(sizeof(void *) == 8, "Expected LP64");
_Static_assert(offsetof(struct hw_module_t, name) == 0x10, "hw_module_t.name offset mismatch");
_Static_assert(offsetof(struct hw_module_t, methods) == 0x20, "hw_module_t.methods offset mismatch");
_Static_assert(offsetof(struct hw_module_t, dso) == 0x28, "hw_module_t.dso offset mismatch");
_Static_assert(offsetof(struct sq2_gralloc_module, perform) == 0x118,
               "gralloc_module_t.perform offset must match Mesa u_gralloc_cros_api (0x118)");

/* ---- Binary ABI types matching Mesa u_gralloc_cros_api ---- */

enum {
    CROS_GRALLOC_DRM_GET_BUFFER_INFO = 4,
    CROS_GRALLOC_DRM_GET_USAGE = 5,
    CROS_GRALLOC_DRM_GET_YUV_INFO = 6,
};

struct cros_gralloc0_buffer_info {
    uint32_t drm_fourcc;
    int32_t num_fds;
    int32_t fds[4];
    uint64_t modifier;
    uint32_t offset[4];
    uint32_t stride[4];
};

_Static_assert(sizeof(struct cros_gralloc0_buffer_info) == 0x40, "buffer_info size");
_Static_assert(offsetof(struct cros_gralloc0_buffer_info, modifier) == 0x18, "modifier offset");
_Static_assert(offsetof(struct cros_gralloc0_buffer_info, offset) == 0x20, "offset offset");
_Static_assert(offsetof(struct cros_gralloc0_buffer_info, stride) == 0x30, "stride offset");

struct cros_gralloc0_yuv_info {
    uint32_t dataspace;
    uint32_t chroma_siting;
};

_Static_assert(sizeof(struct cros_gralloc0_yuv_info) == 0x08, "yuv_info size");

/* ---- Binary ABI types matching QTI gralloc1 / libgrallocutils / libqdMetaData ---- */

typedef struct sq2_gralloc1_device {
    struct hw_device_t common;
    void (*getCapabilities)(struct sq2_gralloc1_device *device, uint32_t *outCount, int32_t *outCaps);
    void *(*getFunction)(struct sq2_gralloc1_device *device, int32_t descriptor);
} sq2_gralloc1_device_t;

_Static_assert(offsetof(sq2_gralloc1_device_t, getCapabilities) == 0x78, "gralloc1 getCapabilities");
_Static_assert(offsetof(sq2_gralloc1_device_t, getFunction) == 0x80, "gralloc1 getFunction");

enum {
    SQ2_GRALLOC1_FUNCTION_GET_DIMENSIONS = 10,
    SQ2_GRALLOC1_FUNCTION_GET_FORMAT = 11,
    SQ2_GRALLOC1_FUNCTION_GET_PRODUCER_USAGE = 12,
    SQ2_GRALLOC1_FUNCTION_GET_STRIDE = 13,
    SQ2_GRALLOC1_FUNCTION_GET_LAYER_COUNT = 22,
};

struct sq2_qti_buffer_info {
    int width;
    int height;
    int format;
    int layer_count;
    uint64_t usage;
};

_Static_assert(sizeof(struct sq2_qti_buffer_info) == 24, "qti BufferInfo size");
_Static_assert(offsetof(struct sq2_qti_buffer_info, usage) == 16, "qti BufferInfo.usage offset");

struct sq2_qti_plane_layout {
    uint32_t component;
    uint32_t h_subsampling;
    uint32_t v_subsampling;
    uint32_t offset;
    int32_t step;
    int32_t stride;
    int32_t stride_bytes;
    int32_t scanlines;
    uint32_t size;
};

_Static_assert(sizeof(struct sq2_qti_plane_layout) == 36, "qti PlaneLayoutInfo size");

/* ColorMetaData layout pinned in probe/graphics/quest2-color-metadata.h and verified in mapper.quest2.so */
enum {
    SQ2_QTI_METADATA_GET_COLOR_METADATA = 2,
    SQ2_QTI_METADATA_GET_LINEAR_FORMAT = 0x1000,
    SQ2_QTI_COLOR_METADATA_SIZE = 2804,
};

struct sq2_color_metadata_prefix {
    uint32_t colorPrimaries;
    uint32_t range;
    uint32_t transfer;
    uint32_t matrixCoefficients;
};

/* Constants matching QTI private_handle_t + DRM fourccs + HAL formats */
#define SQ2_HW_MODULE_TAG 0x48574d54u /* 'HWMT' */
#define SQ2_QTI_HANDLE_MAGIC 0x676d736du /* 'msmg' */
#define SQ2_DRM_FORMAT_MOD_QCOM_COMPRESSED 0x0500000000000001ULL

#define SQ2_FOURCC(a, b, c, d) \
    ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

#define SQ2_DRM_FORMAT_R8 SQ2_FOURCC('R', '8', ' ', ' ')
#define SQ2_DRM_FORMAT_RGB565 SQ2_FOURCC('R', 'G', '1', '6')
#define SQ2_DRM_FORMAT_BGR888 SQ2_FOURCC('B', 'G', '2', '4')
#define SQ2_DRM_FORMAT_ARGB8888 SQ2_FOURCC('A', 'R', '2', '4')
#define SQ2_DRM_FORMAT_ABGR8888 SQ2_FOURCC('A', 'B', '2', '4')
#define SQ2_DRM_FORMAT_XBGR8888 SQ2_FOURCC('X', 'B', '2', '4')
#define SQ2_DRM_FORMAT_ABGR2101010 SQ2_FOURCC('A', 'B', '3', '0')
#define SQ2_DRM_FORMAT_ABGR16161616F SQ2_FOURCC('A', 'B', '4', 'H')
#define SQ2_DRM_FORMAT_NV12 SQ2_FOURCC('N', 'V', '1', '2')
#define SQ2_DRM_FORMAT_NV21 SQ2_FOURCC('N', 'V', '2', '1')
#define SQ2_DRM_FORMAT_YVU420 SQ2_FOURCC('Y', 'V', '1', '2')
#define SQ2_DRM_FORMAT_P010 SQ2_FOURCC('P', '0', '1', '0')

enum {
    SQ2_HAL_PIXEL_FORMAT_RGBA_8888 = 1,
    SQ2_HAL_PIXEL_FORMAT_RGBX_8888 = 2,
    SQ2_HAL_PIXEL_FORMAT_RGB_888 = 3,
    SQ2_HAL_PIXEL_FORMAT_RGB_565 = 4,
    SQ2_HAL_PIXEL_FORMAT_BGRA_8888 = 5,
    SQ2_HAL_PIXEL_FORMAT_YCRCB_420_SP = 0x11,
    SQ2_HAL_PIXEL_FORMAT_RGBA_FP16 = 0x16,
    SQ2_HAL_PIXEL_FORMAT_BLOB = 0x21,
    SQ2_HAL_PIXEL_FORMAT_YCBCR_420_888 = 0x23,
    SQ2_HAL_PIXEL_FORMAT_RGBA_1010102 = 0x2b,
    SQ2_HAL_PIXEL_FORMAT_YCBCR_P010 = 0x36,
    SQ2_HAL_PIXEL_FORMAT_R8 = 0x38,
    SQ2_HAL_PIXEL_FORMAT_YV12 = 0x32315659,
    SQ2_HAL_PIXEL_FORMAT_Y8 = 0x20203859,
    SQ2_HAL_PIXEL_FORMAT_YCBCR_420_SP_VENUS = 0x7fa30c04,
    SQ2_HAL_PIXEL_FORMAT_YCBCR_420_P010_VENUS = 0x7fa30c0a,
};

#define SQ2_USAGE_PROTECTED 0x4000ULL
#define SQ2_USAGE_CPU_MASK 0xffULL

/* ---- Backend State ---- */

typedef int32_t (*pfn_get_dimensions_t)(sq2_gralloc1_device_t *, sq2_buffer_handle_t, uint32_t *, uint32_t *);
typedef int32_t (*pfn_get_format_t)(sq2_gralloc1_device_t *, sq2_buffer_handle_t, int32_t *);
typedef int32_t (*pfn_get_layer_count_t)(sq2_gralloc1_device_t *, sq2_buffer_handle_t, uint32_t *);
typedef int32_t (*pfn_get_producer_usage_t)(sq2_gralloc1_device_t *, sq2_buffer_handle_t, uint64_t *);
typedef int32_t (*pfn_get_stride_t)(sq2_gralloc1_device_t *, sq2_buffer_handle_t, uint32_t *);

typedef bool (*pfn_is_ubwc_enabled_t)(int format, uint64_t usage);
typedef uint32_t (*pfn_get_rgb_meta_size_t)(int format, uint32_t width, uint32_t height, uint64_t usage);
typedef int (*pfn_get_buf_size_dims_t)(const struct sq2_qti_buffer_info *, unsigned *, unsigned *, unsigned *);
typedef int (*pfn_get_yuv_plane_info_t)(const struct sq2_qti_buffer_info *, int32_t, int32_t, int32_t, int32_t,
                                        int *, struct sq2_qti_plane_layout *);
typedef int (*pfn_get_metadata_t)(void *handle, int param_type, void *param);

struct sq2_backend {
    pthread_once_t once;
    bool ready;
    const struct hw_module_t *kona_module;
    sq2_gralloc1_device_t *device;
    pfn_get_dimensions_t get_dimensions;
    pfn_get_format_t get_format;
    pfn_get_layer_count_t get_layer_count;
    pfn_get_producer_usage_t get_producer_usage;
    pfn_get_stride_t get_stride;
    pfn_is_ubwc_enabled_t is_ubwc_enabled;
    pfn_get_rgb_meta_size_t get_rgb_meta_size;
    pfn_get_buf_size_dims_t get_buf_size_dims;
    pfn_get_yuv_plane_info_t get_yuv_plane_info;
    pfn_get_metadata_t get_metadata;
};

static struct sq2_backend g_backend = {
    .once = PTHREAD_ONCE_INIT,
};

static const char *resolve_lib_path(const char *env_name, const char *fallback) {
#ifdef SQ2_HOST_TEST
    const char *override = env_name ? getenv(env_name) : NULL;
    if (override && override[0]) {
        return override;
    }
#else
    (void)env_name;
#endif
    return fallback;
}

static void sq2_init_backend(void) {
    const char *kona_path = resolve_lib_path("SQ2_TEST_KONA_SO", "gralloc.kona.so");
    const char *utils_path = resolve_lib_path("SQ2_TEST_GRALLOCUTILS_SO", "libgrallocutils.so");
    const char *meta_path = resolve_lib_path("SQ2_TEST_QDMETADATA_SO", "libqdMetaData.so");

    void *kona_dl = dlopen(kona_path, RTLD_NOW | RTLD_LOCAL);
    if (!kona_dl) {
        SQ2_LOGE("dlopen(%s) failed: %s", kona_path, dlerror());
        return;
    }
    const struct hw_module_t *kona_mod = (const struct hw_module_t *)dlsym(kona_dl, "HMI");
    if (!kona_mod || kona_mod->tag != SQ2_HW_MODULE_TAG || !kona_mod->methods || !kona_mod->methods->open) {
        SQ2_LOGE("invalid HMI in %s", kona_path);
        return;
    }
    struct hw_device_t *raw_dev = NULL;
    if (kona_mod->methods->open(kona_mod, "gralloc", &raw_dev) != 0 || !raw_dev) {
        SQ2_LOGE("gralloc1 open on %s failed", kona_path);
        return;
    }
    sq2_gralloc1_device_t *dev = (sq2_gralloc1_device_t *)raw_dev;
    if (!dev->getFunction) {
        SQ2_LOGE("gralloc1 getFunction missing");
        return;
    }

    pfn_get_dimensions_t get_dims =
        (pfn_get_dimensions_t)dev->getFunction(dev, SQ2_GRALLOC1_FUNCTION_GET_DIMENSIONS);
    pfn_get_format_t get_fmt =
        (pfn_get_format_t)dev->getFunction(dev, SQ2_GRALLOC1_FUNCTION_GET_FORMAT);
    pfn_get_layer_count_t get_layers =
        (pfn_get_layer_count_t)dev->getFunction(dev, SQ2_GRALLOC1_FUNCTION_GET_LAYER_COUNT);
    pfn_get_producer_usage_t get_usage =
        (pfn_get_producer_usage_t)dev->getFunction(dev, SQ2_GRALLOC1_FUNCTION_GET_PRODUCER_USAGE);
    pfn_get_stride_t get_stride =
        (pfn_get_stride_t)dev->getFunction(dev, SQ2_GRALLOC1_FUNCTION_GET_STRIDE);

    if (!get_dims || !get_fmt || !get_layers || !get_usage || !get_stride) {
        SQ2_LOGE("missing required gralloc1 getters");
        return;
    }

    void *utils_dl = dlopen(utils_path, RTLD_NOW | RTLD_LOCAL);
    if (!utils_dl) {
        SQ2_LOGE("dlopen(%s) failed: %s", utils_path, dlerror());
        return;
    }
    pfn_is_ubwc_enabled_t is_ubwc =
        (pfn_is_ubwc_enabled_t)dlsym(utils_dl, "_ZN7gralloc13IsUBwcEnabledEim");
    pfn_get_rgb_meta_size_t rgb_meta =
        (pfn_get_rgb_meta_size_t)dlsym(utils_dl, "_ZN7gralloc14GetRgbMetaSizeEijjm");
    pfn_get_buf_size_dims_t buf_size_dims =
        (pfn_get_buf_size_dims_t)dlsym(utils_dl, "_ZN7gralloc26GetBufferSizeAndDimensionsERKNS_10BufferInfoEPjS3_S3_");
    pfn_get_yuv_plane_info_t yuv_planes =
        (pfn_get_yuv_plane_info_t)dlsym(utils_dl, "_ZN7gralloc15GetYUVPlaneInfoERKNS_10BufferInfoEiiiiPiPNS_15PlaneLayoutInfoE");

    if (!is_ubwc || !rgb_meta || !buf_size_dims || !yuv_planes) {
        SQ2_LOGE("missing required symbols in %s", utils_path);
        return;
    }

    void *meta_dl = dlopen(meta_path, RTLD_NOW | RTLD_LOCAL);
    pfn_get_metadata_t get_meta = NULL;
    if (meta_dl) {
        get_meta = (pfn_get_metadata_t)dlsym(meta_dl, "getMetaData");
    }
    if (!get_meta) {
        SQ2_LOGE("missing getMetaData in %s", meta_path);
        return;
    }

    g_backend.kona_module = kona_mod;
    g_backend.device = dev;
    g_backend.get_dimensions = get_dims;
    g_backend.get_format = get_fmt;
    g_backend.get_layer_count = get_layers;
    g_backend.get_producer_usage = get_usage;
    g_backend.get_stride = get_stride;
    g_backend.is_ubwc_enabled = is_ubwc;
    g_backend.get_rgb_meta_size = rgb_meta;
    g_backend.get_buf_size_dims = buf_size_dims;
    g_backend.get_yuv_plane_info = yuv_planes;
    g_backend.get_metadata = get_meta;
    g_backend.ready = true;
}

static bool ensure_backend(void) {
    pthread_once(&g_backend.once, sq2_init_backend);
    return g_backend.ready;
}

/* ---- Helpers ---- */

struct sq2_rgb_desc {
    uint32_t fourcc;
    uint32_t cpp;
    bool ubwc_capable;
};

static bool lookup_rgb_format(int32_t hal_format, struct sq2_rgb_desc *desc) {
    switch (hal_format) {
        case SQ2_HAL_PIXEL_FORMAT_RGBA_8888:
            *desc = (struct sq2_rgb_desc){SQ2_DRM_FORMAT_ABGR8888, 4, true};
            return true;
        case SQ2_HAL_PIXEL_FORMAT_RGBX_8888:
            *desc = (struct sq2_rgb_desc){SQ2_DRM_FORMAT_XBGR8888, 4, true};
            return true;
        case SQ2_HAL_PIXEL_FORMAT_RGB_888:
            *desc = (struct sq2_rgb_desc){SQ2_DRM_FORMAT_BGR888, 3, false};
            return true;
        case SQ2_HAL_PIXEL_FORMAT_RGB_565:
            *desc = (struct sq2_rgb_desc){SQ2_DRM_FORMAT_RGB565, 2, true};
            return true;
        case SQ2_HAL_PIXEL_FORMAT_BGRA_8888:
            *desc = (struct sq2_rgb_desc){SQ2_DRM_FORMAT_ARGB8888, 4, false};
            return true;
        case SQ2_HAL_PIXEL_FORMAT_RGBA_FP16:
            *desc = (struct sq2_rgb_desc){SQ2_DRM_FORMAT_ABGR16161616F, 8, true};
            return true;
        case SQ2_HAL_PIXEL_FORMAT_RGBA_1010102:
            *desc = (struct sq2_rgb_desc){SQ2_DRM_FORMAT_ABGR2101010, 4, true};
            return true;
        case SQ2_HAL_PIXEL_FORMAT_BLOB:
        case SQ2_HAL_PIXEL_FORMAT_R8:
        case SQ2_HAL_PIXEL_FORMAT_Y8:
            *desc = (struct sq2_rgb_desc){SQ2_DRM_FORMAT_R8, 1, false};
            return true;
        default:
            return false;
    }
}

static bool is_yuv_format(int32_t format) {
    return format == SQ2_HAL_PIXEL_FORMAT_YCBCR_420_SP_VENUS ||
           format == SQ2_HAL_PIXEL_FORMAT_YCBCR_420_888 ||
           format == SQ2_HAL_PIXEL_FORMAT_YCRCB_420_SP ||
           format == SQ2_HAL_PIXEL_FORMAT_YCBCR_P010 ||
           format == SQ2_HAL_PIXEL_FORMAT_YCBCR_420_P010_VENUS ||
           format == SQ2_HAL_PIXEL_FORMAT_YV12;
}

static bool validate_handle_and_geometry(sq2_buffer_handle_t h, uint32_t *out_w, uint32_t *out_h,
                                         uint32_t *out_stride, int32_t *out_fmt, uint64_t *out_usage,
                                         unsigned *out_size, unsigned *out_aligned_w,
                                         unsigned *out_aligned_h) {
    if (!h || h->version != 12 || h->numFds != 2 || h->numInts != 26 || h->data[0] < 0) {
        return false;
    }
    if ((uint32_t)h->data[2] != SQ2_QTI_HANDLE_MAGIC) {
        return false;
    }

    uint32_t width = 0, height = 0, stride = 0, layers = 0;
    int32_t format = 0;
    uint64_t usage = 0;
    if (g_backend.get_dimensions(g_backend.device, h, &width, &height) ||
        g_backend.get_stride(g_backend.device, h, &stride) ||
        g_backend.get_format(g_backend.device, h, &format) ||
        g_backend.get_layer_count(g_backend.device, h, &layers) ||
        g_backend.get_producer_usage(g_backend.device, h, &usage)) {
        return false;
    }
    if (!width || !height || !stride || width > INT32_MAX || height > INT32_MAX ||
        stride > INT32_MAX || layers != 1 || (usage & SQ2_USAGE_PROTECTED)) {
        return false;
    }

    struct sq2_qti_buffer_info info = {
        .width = (int)width,
        .height = (int)height,
        .format = format,
        .layer_count = 1,
        .usage = usage,
    };
    unsigned alloc_size = 0, aligned_w = 0, aligned_h = 0;
    if (g_backend.get_buf_size_dims(&info, &alloc_size, &aligned_w, &aligned_h) ||
        !alloc_size || !aligned_w || aligned_h < height || aligned_w > INT32_MAX ||
        aligned_h > INT32_MAX || aligned_w != stride) {
        return false;
    }

    struct stat st;
    if (fstat(h->data[0], &st) != 0 || st.st_size < 0 || (uint64_t)st.st_size < alloc_size) {
        return false;
    }

    *out_w = width;
    *out_h = height;
    *out_stride = stride;
    *out_fmt = format;
    *out_usage = usage;
    *out_size = alloc_size;
    *out_aligned_w = aligned_w;
    *out_aligned_h = aligned_h;
    return true;
}

static uint32_t color_metadata_to_dataspace(const struct sq2_color_metadata_prefix *cm) {
    uint32_t s = 0, t = 0, r = 0;
    switch (cm->colorPrimaries) {
        case 1:
            s = 1;
            break; /* BT709 */
        case 4:
            s = 8;
            break; /* BT470_6M */
        case 5:
            s = (cm->matrixCoefficients == 11) ? 3u : 2u;
            break; /* BT601_625 */
        case 6:
            s = (cm->matrixCoefficients == 7) ? 5u : 4u;
            break; /* BT601_525 */
        case 8:
            s = 9;
            break; /* GenericFilm */
        case 9:
            s = (cm->matrixCoefficients == 10) ? 7u : 6u;
            break; /* BT2020 */
        case 11:
            s = 11;
            break; /* AdobeRGB */
        case 12:
            s = 10;
            break; /* DCIP3 */
        default:
            return 0;
    }
    switch (cm->transfer) {
        case 8:
            t = 1;
            break; /* Linear */
        case 13:
            t = 2;
            break; /* sRGB */
        case 1:
        case 6:
        case 14:
        case 15:
            t = 3;
            break; /* SMPTE_170M */
        case 4:
            t = 4;
            break; /* Gamma2_2 */
        case 5:
            t = 6;
            break; /* Gamma2_8 */
        case 16:
            t = 7;
            break; /* ST2084 */
        case 18:
            t = 8;
            break; /* HLG */
        default:
            return 0;
    }
    switch (cm->range) {
        case 0:
            r = 2;
            break; /* Range_Limited */
        case 1:
            r = 1;
            break; /* Range_Full */
        case 2:
            r = 3;
            break; /* Range_Extended */
        default:
            return 0;
    }
    return (s << 16) | (t << 22) | (r << 27);
}

static int handle_get_buffer_info(sq2_buffer_handle_t h, struct cros_gralloc0_buffer_info *out) {
    if (!out) {
        return -EINVAL;
    }

    uint32_t width = 0, height = 0, stride = 0;
    int32_t format = 0;
    uint64_t usage = 0;
    unsigned alloc_size = 0, aligned_w = 0, aligned_h = 0;
    if (!validate_handle_and_geometry(h, &width, &height, &stride, &format, &usage,
                                      &alloc_size, &aligned_w, &aligned_h)) {
        return -EINVAL;
    }

    int32_t linear_fmt = 0;
    bool force_linear = (g_backend.get_metadata((void *)h, SQ2_QTI_METADATA_GET_LINEAR_FORMAT,
                                              &linear_fmt) == 0 &&
                         linear_fmt != 0);

    memset(out, 0, sizeof(*out));

    if (!is_yuv_format(format)) {
        struct sq2_rgb_desc rgb;
        if (!lookup_rgb_format(format, &rgb)) {
            return -EINVAL;
        }
        bool ubwc = rgb.ubwc_capable && !force_linear && !(usage & SQ2_USAGE_CPU_MASK) &&
                    g_backend.is_ubwc_enabled(format, usage);
        if (ubwc && g_backend.get_rgb_meta_size(format, aligned_w, aligned_h, usage) == 0) {
            return -EINVAL;
        }
        uint64_t byte_stride = (uint64_t)aligned_w * rgb.cpp;
        if (!byte_stride || byte_stride > UINT32_MAX) {
            return -EINVAL;
        }
        out->drm_fourcc = rgb.fourcc;
        out->num_fds = 1;
        out->fds[0] = h->data[0];
        out->modifier = ubwc ? SQ2_DRM_FORMAT_MOD_QCOM_COMPRESSED : 0ULL;
        /* Both Linux DPU and Mesa fdl6_layout take offset=0 for UBWC meta base. */
        out->offset[0] = 0;
        out->stride[0] = (uint32_t)byte_stride;
        return 0;
    }

    if ((width & 1u) || (height & 1u) || (aligned_h & 1u) ||
        g_backend.is_ubwc_enabled(format, usage)) {
        return -EINVAL;
    }

    struct sq2_qti_buffer_info info = {
        .width = (int)width,
        .height = (int)height,
        .format = format,
        .layer_count = 1,
        .usage = usage,
    };
    struct sq2_qti_plane_layout planes[8];
    memset(planes, 0, sizeof(planes));
    int count = 0;
    if (g_backend.get_yuv_plane_info(&info, format, (int32_t)aligned_w, (int32_t)aligned_h, 0,
                                     &count, planes) != 0) {
        return -EINVAL;
    }

    if (format == SQ2_HAL_PIXEL_FORMAT_YV12) {
        if (count != 3 || planes[0].stride_bytes <= 0 || planes[1].stride_bytes <= 0 ||
            planes[2].stride_bytes <= 0) {
            return -EINVAL;
        }
        if ((uint64_t)planes[0].offset + planes[0].size > alloc_size ||
            (uint64_t)planes[1].offset + planes[1].size > alloc_size ||
            (uint64_t)planes[2].offset + planes[2].size > alloc_size) {
            return -EINVAL;
        }
        /* Donor utility order is Y/Cb/Cr; DRM_FORMAT_YVU420 expects Y/Cr/Cb. */
        out->drm_fourcc = SQ2_DRM_FORMAT_YVU420;
        out->num_fds = 3;
        out->modifier = 0ULL;
        out->fds[0] = h->data[0];
        out->fds[1] = h->data[0];
        out->fds[2] = h->data[0];
        out->offset[0] = planes[0].offset;
        out->stride[0] = (uint32_t)planes[0].stride_bytes;
        out->offset[1] = planes[2].offset;
        out->stride[1] = (uint32_t)planes[2].stride_bytes;
        out->offset[2] = planes[1].offset;
        out->stride[2] = (uint32_t)planes[1].stride_bytes;
        return 0;
    }

    if (count != 2 || planes[0].stride_bytes <= 0 || planes[1].stride_bytes <= 0) {
        return -EINVAL;
    }
    if ((uint64_t)planes[0].offset + planes[0].size > alloc_size ||
        (uint64_t)planes[1].offset >= alloc_size) {
        return -EINVAL;
    }

    uint32_t fourcc = SQ2_DRM_FORMAT_NV12;
    if (format == SQ2_HAL_PIXEL_FORMAT_YCRCB_420_SP) {
        fourcc = SQ2_DRM_FORMAT_NV21;
    } else if (format == SQ2_HAL_PIXEL_FORMAT_YCBCR_P010 ||
               format == SQ2_HAL_PIXEL_FORMAT_YCBCR_420_P010_VENUS) {
        fourcc = SQ2_DRM_FORMAT_P010;
    }

    /* All video/YUV planes reside in data[0]; data[1] is the qdMetaData ashmem fd. */
    out->drm_fourcc = fourcc;
    out->num_fds = 2;
    out->modifier = 0ULL;
    out->fds[0] = h->data[0];
    out->fds[1] = h->data[0];
    out->offset[0] = planes[0].offset;
    out->stride[0] = (uint32_t)planes[0].stride_bytes;
    out->offset[1] = planes[1].offset;
    out->stride[1] = (uint32_t)planes[1].stride_bytes;
    return 0;
}

static int handle_get_yuv_info(sq2_buffer_handle_t h, struct cros_gralloc0_yuv_info *out) {
    if (!out || !h || h->version != 12 || h->numFds != 2 || h->numInts != 26 || h->data[0] < 0 ||
        (uint32_t)h->data[2] != SQ2_QTI_HANDLE_MAGIC) {
        return -EINVAL;
    }

    uint32_t width = 0, height = 0;
    if (g_backend.get_dimensions(g_backend.device, h, &width, &height) != 0 || !width || !height) {
        return -EINVAL;
    }

    uint8_t cm_buf[SQ2_QTI_COLOR_METADATA_SIZE];
    memset(cm_buf, 0, sizeof(cm_buf));
    uint32_t ds = 0;
    if (g_backend.get_metadata((void *)h, SQ2_QTI_METADATA_GET_COLOR_METADATA, cm_buf) == 0) {
        struct sq2_color_metadata_prefix prefix;
        memcpy(&prefix, cm_buf, sizeof(prefix));
        ds = color_metadata_to_dataspace(&prefix);
    }

    if (ds == 0) {
        /* Unset metadata fallback: BT.709 limited for >=720p, BT.601_625 limited otherwise. */
        ds = (width >= 1280 || height >= 720) ? ((1u << 16) | (3u << 22) | (2u << 27))
                                              : ((2u << 16) | (3u << 22) | (2u << 27));
    }

    out->dataspace = ds;
    out->chroma_siting = 0;
    return 0;
}

static int sq2_device_open(const struct hw_module_t *module, const char *id,
                           struct hw_device_t **device) {
    (void)module;
    if (!ensure_backend() || !g_backend.kona_module) {
        return -ENODEV;
    }
    return g_backend.kona_module->methods->open(g_backend.kona_module, id, device);
}

static int sq2_register_buffer(const struct sq2_gralloc_module *module, sq2_buffer_handle_t handle) {
    (void)module;
    (void)handle;
    return -ENOTSUP;
}

static int sq2_unregister_buffer(const struct sq2_gralloc_module *module,
                                 sq2_buffer_handle_t handle) {
    (void)module;
    (void)handle;
    return -ENOTSUP;
}

static int sq2_lock(const struct sq2_gralloc_module *module, sq2_buffer_handle_t handle,
                    int usage, int l, int t, int w, int h, void **vaddr) {
    (void)module;
    (void)handle;
    (void)usage;
    (void)l;
    (void)t;
    (void)w;
    (void)h;
    (void)vaddr;
    return -ENOTSUP;
}

static int sq2_unlock(const struct sq2_gralloc_module *module, sq2_buffer_handle_t handle) {
    (void)module;
    (void)handle;
    return -ENOTSUP;
}

static int sq2_perform(const struct sq2_gralloc_module *module, int operation, ...) {
    (void)module;
    if (operation == CROS_GRALLOC_DRM_GET_USAGE) {
        return -ENOTSUP;
    }
    if (!ensure_backend()) {
        return -ENODEV;
    }

    va_list args;
    va_start(args, operation);
    int rc = -ENOTSUP;
    if (operation == CROS_GRALLOC_DRM_GET_BUFFER_INFO) {
        sq2_buffer_handle_t handle = va_arg(args, sq2_buffer_handle_t);
        struct cros_gralloc0_buffer_info *out = va_arg(args, struct cros_gralloc0_buffer_info *);
        rc = handle_get_buffer_info(handle, out);
    } else if (operation == CROS_GRALLOC_DRM_GET_YUV_INFO) {
        sq2_buffer_handle_t handle = va_arg(args, sq2_buffer_handle_t);
        struct cros_gralloc0_yuv_info *out = va_arg(args, struct cros_gralloc0_yuv_info *);
        rc = handle_get_yuv_info(handle, out);
    }
    va_end(args);
    return rc;
}

static sq2_hw_module_methods_t g_sq2_module_methods = {
    .open = sq2_device_open,
};

__attribute__((visibility("default")))
struct sq2_gralloc_module HMI = {
    .common = {
        .tag = SQ2_HW_MODULE_TAG,
        .module_api_version = 0x0100,
        .hal_api_version = 0,
        .id = "gralloc",
        .name = "CrOS Gralloc",
        .author = "SQ2 Hybrid Mesa/QTI",
        .methods = &g_sq2_module_methods,
        .dso = NULL,
        .reserved = {0},
    },
    .registerBuffer = sq2_register_buffer,
    .unregisterBuffer = sq2_unregister_buffer,
    .lock = sq2_lock,
    .unlock = sq2_unlock,
    .perform = sq2_perform,
};
