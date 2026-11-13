import ctypes
import errno
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))

import build_mesa_qti_gralloc
import full_image
import full_image_video

TEST_DIR = Path('/home/kanoyami/spx_googlebookos/chromebookOS-mesa-a6xx')


def make_aarch64_elf(extra=b''):
    hdr = bytearray(64)
    hdr[0:4] = b'\x7fELF'
    hdr[4] = 2  # ELF64
    hdr[5] = 1  # little endian
    hdr[6] = 1
    struct.pack_into('<H', hdr, 18, 183)  # EM_AARCH64
    return bytes(hdr) + extra


def make_native_omx_elf():
    size = max(
        full_image_video.OMX_INDEX_CHECK_OFFSET + len(full_image_video.OMX_INDEX_CHECK_BYTES),
        full_image_video.OMX_SWRENDER_OFFSET + len(full_image_video.OMX_SWRENDER_BEFORE),
    )
    buf = bytearray(make_aarch64_elf(b'\x00' * size))
    idx_off = full_image_video.OMX_INDEX_CHECK_OFFSET
    buf[idx_off:idx_off + len(full_image_video.OMX_INDEX_CHECK_BYTES)] = (
        full_image_video.OMX_INDEX_CHECK_BYTES
    )
    sw_off = full_image_video.OMX_SWRENDER_OFFSET
    buf[sw_off:sw_off + len(full_image_video.OMX_SWRENDER_BEFORE)] = (
        full_image_video.OMX_SWRENDER_BEFORE
    )
    return bytes(buf)


def build_hybrid_fixture_tree():
    records = {}
    bodies = {}

    def add_dir(name):
        records[name] = full_image.new_record(0o040755)
        bodies[name] = b''

    def add_file(name, data):
        records[name] = full_image.new_record(0o100644)
        bodies[name] = data

    omx_elf = make_native_omx_elf()
    mesa_libdrm = b'MESA_LIBDRM_2_4_133'
    add_dir('etc/googlebook-security')
    add_file('etc/googlebook-security/lib64/egl/libEGL_freedreno.so', b'egl')
    add_file('etc/googlebook-security/lib64/egl/libGLESv1_CM_freedreno.so', b'gles1')
    add_file('etc/googlebook-security/lib64/egl/libGLESv2_freedreno.so', b'gles2')
    add_file('etc/googlebook-security/lib64/libgallium_dri.so', b'dri')
    add_file('etc/googlebook-security/lib64/libdrm.so', mesa_libdrm)
    add_file('etc/googlebook-security/lib64/hw/vulkan.freedreno.so', b'turnip')
    add_file(
        'etc/googlebook-security/build.prop',
        (
            'ro.hardware.egl=freedreno\n'
            'ro.hardware.vulkan=freedreno\n'
            'ro.vendor.hwcomposer.mode=client\n'
            'vendor.minigbm.debug=nocompression\n'
            'vendor.gralloc.disable_ubwc=0\n'
            'ro.surface_flinger.force_hole_punch=true\n'
        ).encode(),
    )

    add_dir('etc/sq2-graphics-full')
    add_file('etc/sq2-graphics-full/payload.bin', b'closed-full')
    add_dir('etc/sq2-vulkan-closed')
    add_file('etc/sq2-vulkan-closed/vk.so', b'closed-vk')
    add_file('bin/qualcomm-full-start', b'#!/bin/sh\n')
    add_file('bin/closed-vulkan-start', b'#!/bin/sh\n')
    add_file('bin/qti-system-display-stage', b'#!/bin/sh\n')

    qti_prefix = 'etc/sq2-qti-system'
    add_dir(qti_prefix)
    add_file(
        f'{qti_prefix}/root/bin/hw/vendor.qti.hardware.display.composer-service',
        b'composer',
    )
    add_file(
        f'{qti_prefix}/root/bin/hw/vendor.qti.hardware.display.allocator-service',
        b'allocator',
    )
    add_file(f'{qti_prefix}/root/lib64/hw/mapper.quest2.so', b'mapper5')
    add_file(f'{qti_prefix}/root/lib64/hw/gralloc.kona.so', b'kona')
    add_file(f'{qti_prefix}/root/lib64/libgrallocutils.so', b'grallocutils')
    add_file(f'{qti_prefix}/root/lib64/libqdMetaData.so', b'qdmeta')
    add_file(f'{qti_prefix}/root/lib64/sq2-qti-display/libdrm.so', b'OLD_QTI_LIBDRM')
    for closed_rel in full_image.HYBRID_CLOSED_QTI_ROOT_FILES:
        add_file(f'{qti_prefix}/root/{closed_rel}', b'closed-' + closed_rel.encode())

    symlinks = [
        'lib64/hw/gralloc.default.so gralloc.kona.so',
        'lib64/hw/mapper.qti.so mapper.quest2.so',
    ]
    for closed_link in sorted(full_image.HYBRID_CLOSED_QTI_SYMLINKS):
        symlinks.append(f'{closed_link} libEGL_adreno.so')
    add_file(f'{qti_prefix}/SYMLINKS', ('\n'.join(symlinks) + '\n').encode())

    root_prefix = f'{qti_prefix}/root/'
    install_rel = sorted(n.removeprefix(root_prefix) for n in bodies if n.startswith(root_prefix))
    add_file(f'{qti_prefix}/INSTALL_FILES', ('\n'.join(install_rel) + '\n').encode())
    add_file(
        f'{qti_prefix}/INSTALLED_SHA256SUMS',
        ('\n'.join(f"{hashlib.sha256(bodies[root_prefix + r]).hexdigest()}  {r}" for r in install_rel) + '\n').encode(),
    )
    add_file(
        f'{qti_prefix}/SHA256SUMS',
        ('\n'.join(f"{hashlib.sha256(bodies[root_prefix + r]).hexdigest()}  root/{r}" for r in install_rel) + '\n').encode(),
    )

    add_file(full_image_video.OMX_SERVICE, omx_elf)
    add_file(
        'init',
        (
            b'#!/bin/sh\n'
            b'qualcomm-full-start > /logs/qualcomm-full.txt 2>&1 || fail qualcomm-full-graphics\n'
            b'qti-system-display-stage > /logs/qti-system-display.txt 2>&1 || fail qti-system-display\n'
            b'if [ -d /etc/googlebook-security ]; then\n'
            b'    echo ok\n'
            b'fi\n'
        ),
    )
    return records, bodies


class CrosGralloc0BufferInfo(ctypes.Structure):
    _fields_ = [
        ('drm_fourcc', ctypes.c_uint32),
        ('num_fds', ctypes.c_int32),
        ('fds', ctypes.c_int32 * 4),
        ('modifier', ctypes.c_uint64),
        ('offset', ctypes.c_uint32 * 4),
        ('stride', ctypes.c_uint32 * 4),
    ]


class CrosGralloc0YuvInfo(ctypes.Structure):
    _fields_ = [
        ('dataspace', ctypes.c_uint32),
        ('chroma_siting', ctypes.c_uint32),
    ]


class NativeHandleHeader(ctypes.Structure):
    _fields_ = [
        ('version', ctypes.c_int),
        ('numFds', ctypes.c_int),
        ('numInts', ctypes.c_int),
        ('data', ctypes.c_int * 28),
    ]


def make_fourcc(a, b, c, d):
    return ord(a) | (ord(b) << 8) | (ord(c) << 16) | (ord(d) << 24)


MOCK_QTI_C = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int version;
    int numFds;
    int numInts;
    int data[28];
} mock_handle_t;

struct hw_module_t;
struct hw_device_t;

typedef struct {
    int (*open)(const struct hw_module_t *module, const char *id, struct hw_device_t **device);
} hw_module_methods_t;

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
    hw_module_methods_t *methods;
    void *dso;
    uint64_t reserved[32 - 7];
};

typedef struct {
    struct hw_device_t common;
    void (*getCapabilities)(void *dev, uint32_t *outCount, int32_t *outCaps);
    void *(*getFunction)(void *dev, int32_t descriptor);
} gralloc1_device_t;

/* Encoded in mock handle ints:
 * data[3]=width, data[4]=height, data[5]=stride, data[6]=format,
 * data[7]=layers, data[8]=usage_lo, data[9]=usage_hi, data[10]=linear_fmt,
 * data[11]=primaries, data[12]=range, data[13]=transfer, data[14]=matrix,
 * data[15]=has_color_meta
 */
static int32_t mock_get_dimensions(gralloc1_device_t *d, const mock_handle_t *h, uint32_t *w, uint32_t *ht) {
    (void)d;
    *w = (uint32_t)h->data[3];
    *ht = (uint32_t)h->data[4];
    return 0;
}
static int32_t mock_get_format(gralloc1_device_t *d, const mock_handle_t *h, int32_t *f) {
    (void)d;
    *f = h->data[6];
    return 0;
}
static int32_t mock_get_layer_count(gralloc1_device_t *d, const mock_handle_t *h, uint32_t *l) {
    (void)d;
    *l = (uint32_t)h->data[7];
    return 0;
}
static int32_t mock_get_producer_usage(gralloc1_device_t *d, const mock_handle_t *h, uint64_t *u) {
    (void)d;
    *u = ((uint64_t)(uint32_t)h->data[9] << 32) | (uint32_t)h->data[8];
    return 0;
}
static int32_t mock_get_stride(gralloc1_device_t *d, const mock_handle_t *h, uint32_t *s) {
    (void)d;
    *s = (uint32_t)h->data[5];
    return 0;
}

static void mock_get_caps(void *dev, uint32_t *outCount, int32_t *outCaps) {
    (void)dev; (void)outCaps;
    *outCount = 0;
}

static void *mock_get_function(void *dev, int32_t desc) {
    (void)dev;
    switch (desc) {
        case 10: return (void *)mock_get_dimensions;
        case 11: return (void *)mock_get_format;
        case 12: return (void *)mock_get_producer_usage;
        case 13: return (void *)mock_get_stride;
        case 22: return (void *)mock_get_layer_count;
        default: return NULL;
    }
}

static gralloc1_device_t g_dev = {
    .common = { .tag = 0x48574454u, .version = 1 },
    .getCapabilities = mock_get_caps,
    .getFunction = mock_get_function,
};

static int mock_open(const struct hw_module_t *mod, const char *id, struct hw_device_t **dev) {
    (void)mod;
    /* QTI gralloc_device_open uses GRALLOC_HARDWARE_MODULE_ID, not an API-version name. */
    if (strcmp(id, "gralloc") != 0) return -1;
    *dev = &g_dev.common;
    return 0;
}

static hw_module_methods_t g_methods = { .open = mock_open };

struct hw_module_t HMI = {
    .tag = 0x48574d54u,
    .module_api_version = 0x0100,
    .id = "gralloc",
    .name = "Mock QTI Gralloc",
    .methods = &g_methods,
};

struct buf_info {
    int width;
    int height;
    int format;
    int layer_count;
    uint64_t usage;
};

struct plane_layout {
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

bool _ZN7gralloc13IsUBwcEnabledEim(int format, uint64_t usage) {
    if (usage & 0xffULL) return false;
    if (format == 1 || format == 2 || format == 4 || format == 0x16 || format == 0x2b) {
        return true;
    }
    return false;
}

uint32_t _ZN7gralloc14GetRgbMetaSizeEijjm(int format, uint32_t w, uint32_t h, uint64_t usage) {
    (void)format; (void)w; (void)h; (void)usage;
    return 4096u;
}

int _ZN7gralloc26GetBufferSizeAndDimensionsERKNS_10BufferInfoEPjS3_S3_(
        const struct buf_info *info, unsigned *size, unsigned *aw, unsigned *ah) {
    unsigned align_w = (unsigned)info->width;
    unsigned align_h = (unsigned)info->height;
    *aw = align_w;
    *ah = align_h;
    if (info->format == 0x36 || info->format == 0x7fa30c0a) {
        *size = align_w * 2 * align_h * 3 / 2;
    } else if (info->format == 0x7fa30c04 || info->format == 0x32315659) {
        *size = align_w * align_h * 3 / 2;
    } else {
        *size = align_w * align_h * 4 + 4096u;
    }
    return 0;
}

int _ZN7gralloc15GetYUVPlaneInfoERKNS_10BufferInfoEiiiiPiPNS_15PlaneLayoutInfoE(
        const struct buf_info *info, int32_t fmt, int32_t aw, int32_t ah, int32_t flags,
        int *count, struct plane_layout *planes) {
    (void)info; (void)flags;
    if (fmt == 0x32315659) {
        /* Order matches donor utility: Y, Cb, Cr. */
        *count = 3;
        uint32_t y_size = (uint32_t)(aw * ah);
        uint32_t c_stride = (uint32_t)(aw / 2);
        uint32_t c_size = c_stride * (uint32_t)(ah / 2);
        planes[0] = (struct plane_layout){ .offset = 0, .stride_bytes = aw, .size = y_size };
        planes[1] = (struct plane_layout){ .offset = y_size, .stride_bytes = (int32_t)c_stride, .size = c_size };
        planes[2] = (struct plane_layout){ .offset = y_size + c_size, .stride_bytes = (int32_t)c_stride, .size = c_size };
        return 0;
    }
    *count = 2;
    if (fmt == 0x36 || fmt == 0x7fa30c0a) {
        int32_t y_stride = aw * 2;
        uint32_t y_size = (uint32_t)(y_stride * ah);
        planes[0] = (struct plane_layout){ .offset = 0, .stride_bytes = y_stride, .size = y_size };
        planes[1] = (struct plane_layout){ .offset = y_size, .stride_bytes = y_stride, .size = y_size / 2 };
    } else {
        uint32_t y_size = (uint32_t)(aw * ah);
        planes[0] = (struct plane_layout){ .offset = 0, .stride_bytes = aw, .size = y_size };
        planes[1] = (struct plane_layout){ .offset = y_size, .stride_bytes = aw, .size = y_size / 2 };
    }
    return 0;
}

int getMetaData(void *handle, int param_type, void *param) {
    const mock_handle_t *h = (const mock_handle_t *)handle;
    if (param_type == 0x1000) {
        *(int32_t *)param = h->data[10];
        return 0;
    }
    if (param_type == 2) {
        if (!h->data[15]) return -1;
        uint32_t *u = (uint32_t *)param;
        u[0] = (uint32_t)h->data[11];
        u[1] = (uint32_t)h->data[12];
        u[2] = (uint32_t)h->data[13];
        u[3] = (uint32_t)h->data[14];
        return 0;
    }
    return -1;
}
'''


class HybridGraphicsTest(unittest.TestCase):
    def test_host_adapter_abi_and_formats(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            mock_c = tmp_path / 'mock_qti.c'
            mock_so = tmp_path / 'libmock_qti.so'
            adapter_so = tmp_path / 'gralloc.sq2_mesa.host.so'
            mock_c.write_text(MOCK_QTI_C)
            subprocess.run(
                ['cc', '-std=c11', '-O2', '-fPIC', '-shared', '-Wall', '-Wextra', '-Werror',
                 str(mock_c), '-o', str(mock_so)],
                check=True,
            )
            subprocess.run(
                ['cc', '-DSQ2_HOST_TEST=1', '-std=c11', '-O2', '-fPIC', '-shared',
                 '-Wall', '-Wextra', '-Werror',
                 str(ROOT / 'probe/graphics/quest2-mesa-gralloc-adapter.c'),
                 '-ldl', '-pthread', '-o', str(adapter_so)],
                check=True,
            )

            script = (
                'import ctypes, errno, os, tempfile\n'
                'class BufInfo(ctypes.Structure):\n'
                '    _fields_ = [("drm_fourcc", ctypes.c_uint32), ("num_fds", ctypes.c_int32),\n'
                '                ("fds", ctypes.c_int32 * 4), ("modifier", ctypes.c_uint64),\n'
                '                ("offset", ctypes.c_uint32 * 4), ("stride", ctypes.c_uint32 * 4)]\n'
                'class YuvInfo(ctypes.Structure):\n'
                '    _fields_ = [("dataspace", ctypes.c_uint32), ("chroma_siting", ctypes.c_uint32)]\n'
                'class Handle(ctypes.Structure):\n'
                '    _fields_ = [("version", ctypes.c_int), ("numFds", ctypes.c_int),\n'
                '                ("numInts", ctypes.c_int), ("data", ctypes.c_int * 28)]\n'
                'fourcc = lambda a, b, c, d: ord(a) | (ord(b) << 8) | (ord(c) << 16) | (ord(d) << 24)\n'
                f'lib = ctypes.CDLL({str(adapter_so)!r})\n'
                'hmi_addr = ctypes.cast(ctypes.c_void_p.in_dll(lib, "HMI"), ctypes.c_void_p)\n'
                'hmi_base = ctypes.addressof(ctypes.c_char.in_dll(lib, "HMI"))\n'
                'name_ptr = ctypes.c_char_p.from_address(hmi_base + 0x10).value\n'
                'assert name_ptr == b"CrOS Gralloc", name_ptr\n'
                'perform_addr = ctypes.c_uint64.from_address(hmi_base + 0x118).value\n'
                'perform_fn = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int)(perform_addr)\n'
                'assert perform_fn(hmi_base, 5) == -errno.ENOTSUP\n'
                'perform_u = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int, ctypes.c_uint64)(perform_addr)\n'
                'assert perform_u(hmi_base, 5, 1) == -errno.ENOTSUP\n'
                'perform_p = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(Handle), ctypes.c_void_p)(perform_addr)\n'
                'with tempfile.TemporaryFile() as bf, tempfile.TemporaryFile() as mf:\n'
                '    bf.truncate(64 * 1024 * 1024)\n'
                '    def mk(w, h, stride, fmt, usage=0, linear=0, cm=None):\n'
                '        obj = Handle(version=12, numFds=2, numInts=26)\n'
                '        obj.data[0] = bf.fileno()\n'
                '        obj.data[1] = mf.fileno()\n'
                '        obj.data[2] = 0x676d736d\n'
                '        obj.data[3], obj.data[4], obj.data[5], obj.data[6], obj.data[7] = w, h, stride, fmt, 1\n'
                '        obj.data[8] = usage & 0xffffffff\n'
                '        obj.data[9] = (usage >> 32) & 0xffffffff\n'
                '        obj.data[10] = linear\n'
                '        if cm:\n'
                '            obj.data[11], obj.data[12], obj.data[13], obj.data[14] = cm\n'
                '            obj.data[15] = 1\n'
                '        return obj\n'
                '    bi = BufInfo()\n'
                '    h_ubwc = mk(2880, 1920, 2880, 1)\n'
                '    assert perform_p(hmi_base, 4, ctypes.byref(h_ubwc), ctypes.byref(bi)) == 0\n'
                '    assert bi.drm_fourcc == fourcc("A", "B", "2", "4")\n'
                '    assert bi.num_fds == 1 and bi.fds[0] == bf.fileno()\n'
                '    assert bi.modifier == 0x0500000000000001\n'
                '    assert bi.offset[0] == 0 and bi.stride[0] == 2880 * 4\n'
                '    h_lin = mk(2880, 1920, 2880, 1, linear=1)\n'
                '    assert perform_p(hmi_base, 4, ctypes.byref(h_lin), ctypes.byref(bi)) == 0\n'
                '    assert bi.modifier == 0 and bi.offset[0] == 0\n'
                '    h_nv12 = mk(1920, 1080, 1920, 0x7fa30c04)\n'
                '    assert perform_p(hmi_base, 4, ctypes.byref(h_nv12), ctypes.byref(bi)) == 0\n'
                '    assert bi.drm_fourcc == fourcc("N", "V", "1", "2") and bi.num_fds == 2\n'
                '    assert bi.fds[0] == bf.fileno() and bi.fds[1] == bf.fileno()\n'
                '    assert bi.offset[0] == 0 and bi.offset[1] == 1920 * 1080\n'
                '    for pfmt in (0x36, 0x7fa30c0a):\n'
                '        hp = mk(3840, 2160, 3840, pfmt)\n'
                '        assert perform_p(hmi_base, 4, ctypes.byref(hp), ctypes.byref(bi)) == 0\n'
                '        assert bi.drm_fourcc == fourcc("P", "0", "1", "0") and bi.num_fds == 2\n'
                '        assert bi.fds[0] == bf.fileno() and bi.fds[1] == bf.fileno()\n'
                '        assert bi.stride[0] == 7680 and bi.offset[1] == 7680 * 2160\n'
                '    h_yv12 = mk(1280, 720, 1280, 0x32315659)\n'
                '    assert perform_p(hmi_base, 4, ctypes.byref(h_yv12), ctypes.byref(bi)) == 0\n'
                '    assert bi.drm_fourcc == fourcc("Y", "V", "1", "2") and bi.num_fds == 3\n'
                '    assert bi.fds[0] == bf.fileno() and bi.fds[1] == bf.fileno() and bi.fds[2] == bf.fileno()\n'
                '    y_sz = 1280 * 720\n'
                '    c_sz = 640 * 360\n'
                '    assert bi.offset[0] == 0 and bi.offset[1] == y_sz + c_sz and bi.offset[2] == y_sz\n'
                '    h_bad = mk(1280, 720, 1280, 1)\n'
                '    h_bad.data[2] = 0x12345678\n'
                '    assert perform_p(hmi_base, 4, ctypes.byref(h_bad), ctypes.byref(bi)) == -errno.EINVAL\n'
                '    h_prot = mk(1280, 720, 1280, 1, usage=0x4000)\n'
                '    assert perform_p(hmi_base, 4, ctypes.byref(h_prot), ctypes.byref(bi)) == -errno.EINVAL\n'
                '    yi = YuvInfo()\n'
                '    h_hdr = mk(3840, 2160, 3840, 0x36, cm=(9, 0, 16, 9))\n'
                '    assert perform_p(hmi_base, 6, ctypes.byref(h_hdr), ctypes.byref(yi)) == 0\n'
                '    assert yi.dataspace == ((6 << 16) | (7 << 22) | (2 << 27)), hex(yi.dataspace)\n'
                '    assert perform_p(hmi_base, 6, ctypes.byref(h_nv12), ctypes.byref(yi)) == 0\n'
                '    assert yi.dataspace == ((1 << 16) | (3 << 22) | (2 << 27)), hex(yi.dataspace)\n'
                '    h_sd = mk(720, 480, 720, 0x7fa30c04)\n'
                '    assert perform_p(hmi_base, 6, ctypes.byref(h_sd), ctypes.byref(yi)) == 0\n'
                '    assert yi.dataspace == ((2 << 16) | (3 << 22) | (2 << 27)), hex(yi.dataspace)\n'
            )
            env = dict(os.environ)
            env['SQ2_TEST_KONA_SO'] = str(mock_so)
            env['SQ2_TEST_GRALLOCUTILS_SO'] = str(mock_so)
            env['SQ2_TEST_QDMETADATA_SO'] = str(mock_so)
            subprocess.run([sys.executable, '-c', script], env=env, check=True)

    def test_build_mesa_qti_gralloc_elf(self):
        data = build_mesa_qti_gralloc.build_bytes()
        self.assertGreater(len(data), 4096)
        self.assertEqual(data[:6], b'\x7fELF\x02\x01')
        self.assertEqual(struct.unpack_from('<H', data, 18)[0], 183)

    def test_hybrid_runtime_fixture_and_omx_gate(self):
        records, bodies = build_hybrid_fixture_tree()
        fake_adapter = make_aarch64_elf(b'ADAPTER')
        report = full_image.hybrid_runtime(records, bodies, gralloc_adapter_bytes=fake_adapter, layout_payload={
                'lib64/libgrallocutils.so': b'fixture-layout',
                'lib64/sq2-qti-display/libgrallocutils.so': b'fixture-layout',
                'lib64/libsq2_grallocutils_vendor.so': b'fixture-vendor'})

        self.assertEqual(report['graphics'], 'hybrid')
        self.assertEqual(bodies['etc/sq2-graphics-mode'], b'hybrid-mesa-qti\n')
        self.assertEqual(bodies['etc/sq2-qti-system/root/lib64/hw/gralloc.sq2_mesa.so'], fake_adapter)
        self.assertEqual(
            bodies['etc/sq2-qti-system/root/lib64/sq2-qti-display/libdrm.so'],
            bodies['etc/googlebook-security/lib64/libdrm.so'],
        )
        self.assertNotIn('etc/sq2-graphics-full', bodies)
        self.assertNotIn('etc/sq2-vulkan-closed', bodies)
        self.assertNotIn('bin/qualcomm-full-start', bodies)
        self.assertIn('bin/qti-system-display-stage', bodies)
        self.assertIn('qti-system-display-stage', bodies['init'].decode())
        self.assertNotIn('qualcomm-full-start', bodies['init'].decode())

        for rel in full_image.HYBRID_CLOSED_QTI_ROOT_FILES:
            self.assertNotIn(f'etc/sq2-qti-system/root/{rel}', bodies)

        symlinks = bodies['etc/sq2-qti-system/SYMLINKS'].decode().splitlines()
        self.assertIn('lib64/hw/gralloc.default.so gralloc.sq2_mesa.so', symlinks)
        for closed_link in full_image.HYBRID_CLOSED_QTI_SYMLINKS:
            self.assertFalse(any(line.startswith(closed_link + ' ') for line in symlinks))

        props = bodies['etc/googlebook-security/build.prop'].decode()
        self.assertNotIn('ro.vendor.hwcomposer.mode=client', props)
        self.assertNotIn('vendor.minigbm.debug=nocompression', props)
        self.assertIn('ro.hardware.egl=freedreno', props)
        self.assertIn('ro.surface_flinger.force_hole_punch=true', props)

        install_files = bodies['etc/sq2-qti-system/INSTALL_FILES'].decode().splitlines()
        self.assertIn('lib64/hw/gralloc.sq2_mesa.so', install_files)
        self.assertIn('lib64/hw/mapper.quest2.so', install_files)
        self.assertEqual(
            len(install_files),
            len(bodies['etc/sq2-qti-system/INSTALLED_SHA256SUMS'].decode().splitlines()),
        )

        # Verify tampering RequiresSwRenderer=0 -> 1 is rejected.
        bad_records, bad_bodies = build_hybrid_fixture_tree()
        omx = bytearray(bad_bodies[full_image_video.OMX_SERVICE])
        off = full_image_video.OMX_SWRENDER_OFFSET
        omx[off:off + len(full_image_video.OMX_SWRENDER_AFTER)] = full_image_video.OMX_SWRENDER_AFTER
        bad_bodies[full_image_video.OMX_SERVICE] = bytes(omx)
        with self.assertRaises(ValueError):
            full_image.hybrid_runtime(bad_records, bad_bodies, gralloc_adapter_bytes=fake_adapter, layout_payload={
                'lib64/libgrallocutils.so': b'fixture-layout',
                'lib64/sq2-qti-display/libgrallocutils.so': b'fixture-layout',
                'lib64/libsq2_grallocutils_vendor.so': b'fixture-vendor'})

    def test_hybrid_runtime_real_private_overlay_if_present(self):
        runtime_dir = ROOT / 'private/build-inputs/full-image-runtime'
        if not (runtime_dir / 'records.json').is_file():
            self.skipTest('private full-image-runtime overlay not present')
        records = json.loads((runtime_dir / 'records.json').read_text())
        bodies = {
            name: b'' if rec[1][1] & 0o170000 == 0o040000 else (runtime_dir / 'files' / name).read_bytes()
            for name, rec in records.items()
        }
        adapter_bytes = build_mesa_qti_gralloc.build_bytes()
        report = full_image.hybrid_runtime(records, bodies, gralloc_adapter_bytes=adapter_bytes)
        self.assertEqual(report['graphics'], 'hybrid')
        self.assertEqual(report['video']['omx_output'], 'native-anw-surface')
        qti = 'etc/sq2-qti-system'
        for line in bodies[f'{qti}/INSTALLED_SHA256SUMS'].decode().splitlines():
            digest, rel = line.split(None, 1)
            self.assertEqual(hashlib.sha256(bodies[f'{qti}/root/{rel}']).hexdigest(), digest)
        for line in bodies[f'{qti}/SHA256SUMS'].decode().splitlines():
            digest, rel = line.split(None, 1)
            self.assertEqual(hashlib.sha256(bodies[f'{qti}/{rel}']).hexdigest(), digest)

    def test_venus_omx_header_sync_and_dec_validation(self):
        with tempfile.TemporaryDirectory() as tmp:
            vdir = Path(tmp)
            (vdir / 'venus-sq2-core.h').write_text(' if (ret) goto fail;\n return 0;\nfail:')
            (vdir / 'venus-sq2-system-pm.h').write_text('static const char *sq2_pm_stage = "x";')
            (vdir / 'venus-sq2-avc-export.h').write_text('/* export */\n')
            (vdir / 'venus-sq2-omx.h').write_text('/* stale */\n')
            res = full_image_video.ensure_venus_source_hooks(vdir)
            self.assertIn('venus-sq2-omx.h', res['updated_headers'])
            self.assertEqual(
                (vdir / 'venus-sq2-omx.h').read_bytes(),
                (ROOT / 'probe/video/venus-sq2-omx.h').read_bytes(),
            )
            res2 = full_image_video.ensure_venus_source_hooks(vdir)
            self.assertEqual(res2['updated_headers'], [])

        good_ko = make_aarch64_elf(b'foo\0venus_helper_queue_dpb_bufs\0bar')
        full_image_video.validate_venus_dec_module(good_ko)
        with self.assertRaises(ValueError):
            full_image_video.validate_venus_dec_module(make_aarch64_elf(b'no_dpb'))

    def test_build_cli_accepts_hybrid(self):
        res = subprocess.run(
            [sys.executable, str(ROOT / 'scripts/build-lts.py'), '--help'],
            capture_output=True,
            text=True,
            check=True,
        )
        self.assertIn('hybrid', res.stdout)


if __name__ == '__main__':
    unittest.main()
