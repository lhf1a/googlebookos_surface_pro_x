/* Exact RGBA8 glReadPixels matrix through system EGL in the private donor namespace.
 * Default/relay checks use public EGL/GLES entry points. An explicit donor-only
 * trace option instruments the audited CPU copy helper in this private process.
 * Never point CPU reads outside allocation.
 * Keep an external timeout: the private namespace does not isolate shared GPU hangs.
 */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/vfs.h>
#include <unistd.h>
#ifndef SQ2_READPIXELS_MESA_BASELINE
#include "quest2-readback-trace.h"
#endif

#define SENTINEL 0xa5
#define GUARD 256u
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

struct rectangle { GLint x, y; GLsizei w, h; };
struct read_case {
    const char *name;
    struct rectangle rect;
    GLint alignment, row_length, skip_pixels, skip_rows;
    size_t pointer_offset;
};
struct totals { unsigned cases, failed, gl_failures; size_t bad_bytes; };
static struct totals totals;
static void *(*map_range)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
static GLboolean (*unmap_buffer)(GLenum);
extern int sq2_readpixels_linear_ahb_info(const AHardwareBuffer *, unsigned, unsigned,
                                         uint64_t *, uint64_t *);

static void fatal(const char *message) {
    fprintf(stderr, "READPIXELS_FATAL %s EGL=0x%x GL=0x%x\n", message, eglGetError(), glGetError());
    exit(2);
}
static void require(int ok, const char *message) { if (!ok) fatal(message); }
static void require_gl(const char *message) { require(glGetError() == GL_NO_ERROR, message); }

static void private_environment(void) {
    char self[128], init[128];
    ssize_t a = readlink("/proc/self/ns/mnt", self, sizeof(self) - 1);
    ssize_t b = readlink("/proc/1/ns/mnt", init, sizeof(init) - 1);
    require(a > 0 && b > 0, "read namespace identity");
    self[a] = 0; init[b] = 0;
    require(geteuid() == 0, "root probe required");
#ifdef SQ2_READPIXELS_MESA_BASELINE
    require(!strcmp(self, init), "Mesa baseline requires original Android mount namespace");
#else
    struct statfs fs;
    require(strcmp(self, init), "private mount namespace required");
    require(!statfs("/vendor/lib64/egl", &fs) && fs.f_type == 0x01021994,
            "private EGL tmpfs required");
#endif
    FILE *guard = fopen("/sys/module/suspend/parameters/screen_only", "re");
    require(guard != NULL, "read screen-only guard");
    int enabled = fgetc(guard); fclose(guard);
    require(enabled == 'Y', "screen_only must remain Y");
    const char *preload = getenv("LD_PRELOAD"), *path = getenv("LD_LIBRARY_PATH");
    require((!preload || !*preload) && (!path || !*path), "unset LD_PRELOAD/LD_LIBRARY_PATH");
    printf("READPIXELS_NAMESPACE=%s INIT_NAMESPACE=%s\n", self, init);
}

static unsigned number(const char *value) {
    char *end = NULL;
    unsigned long n = strtoul(value, &end, 10);
    require(*value && end && !*end && n >= 1 && n <= 4096, "dimension must be 1..4096");
    return (unsigned)n;
}

static GLuint shader(GLenum type, const char *source) {
    GLuint result = glCreateShader(type);
    require(result != 0, "create shader");
    glShaderSource(result, 1, &source, NULL); glCompileShader(result);
    GLint ok = 0; glGetShaderiv(result, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048]; glGetShaderInfoLog(result, sizeof(log), NULL, log);
        fprintf(stderr, "READPIXELS_SHADER_LOG %s\n", log); fatal("compile shader");
    }
    return result;
}

static void expected_pixel(unsigned x, unsigned y, unsigned phase, uint8_t out[4]) {
    out[0] = (uint8_t)((x * 17u + y * 3u + phase * 13u) & 255u);
    out[1] = (uint8_t)((x * 7u + y * 29u + phase * 71u) & 255u);
    out[2] = (uint8_t)((x * 23u + y * 11u + phase * 47u) & 255u);
    out[3] = (uint8_t)(64u + (x * 5u + y * 11u + phase * 37u) % 192u);
}

static int check_error(GLenum expected, const char *where) {
    GLenum actual = glGetError(), extra = glGetError();
    int ok = actual == expected && extra == GL_NO_ERROR;
    if (!ok) {
        printf("READPIXELS_GL_ERROR at=%s actual=0x%x expected=0x%x extra=0x%x\n",
               where, actual, expected, extra);
        totals.gl_failures++;
        for (unsigned i = 0; i < 16 && glGetError() != GL_NO_ERROR; i++) {}
    }
    return ok;
}

static size_t compare_bytes(const uint8_t *actual, const uint8_t *expected, size_t length,
                            const char *name, const char *path) {
    size_t bad = 0;
    for (size_t i = 0; i < length; i++) {
        if (actual[i] == expected[i]) continue;
        if (bad < 8) printf("READPIXELS_BYTE case=%s path=%s offset=%zu actual=%u expected=%u\n",
                            name, path, i, actual[i], expected[i]);
        bad++;
    }
    totals.bad_bytes += bad;
    return bad;
}

static void pack_state(const struct read_case *test) {
    glPixelStorei(GL_PACK_ALIGNMENT, test->alignment);
    glPixelStorei(GL_PACK_ROW_LENGTH, test->row_length);
    glPixelStorei(GL_PACK_SKIP_PIXELS, test->skip_pixels);
    glPixelStorei(GL_PACK_SKIP_ROWS, test->skip_rows);
    require_gl("valid PACK setup");
}

static void reset_pack(void) {
    const struct read_case reset = {.alignment = 4};
    pack_state(&reset);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    require_gl("reset PACK buffer");
}

static void read_case(const struct read_case *test, unsigned phase, int pbo,
                      unsigned image_width, unsigned image_height) {
    const char *path = pbo ? "pbo" : "client";
    const struct rectangle *r = &test->rect;
    size_t row_pixels = test->row_length ? (size_t)test->row_length : (size_t)r->w;
    size_t stride = (row_pixels * 4u + (size_t)test->alignment - 1u) &
                    ~((size_t)test->alignment - 1u);
    size_t base = GUARD + test->pointer_offset;
    size_t length = base + stride * ((size_t)test->skip_rows + (size_t)r->h + 1u) + GUARD;
    require(stride >= ((size_t)test->skip_pixels + (size_t)r->w) * 4u,
            "non-overlapping valid PACK rows");
    uint8_t *actual = malloc(length), *expected = malloc(length);
    require(actual && expected, "readback allocations");
    memset(actual, SENTINEL, length); memset(expected, SENTINEL, length);
    for (unsigned y = 0; y < (unsigned)r->h; y++) {
        for (unsigned x = 0; x < (unsigned)r->w; x++) {
            size_t offset = base + ((size_t)test->skip_rows + y) * stride +
                            ((size_t)test->skip_pixels + x) * 4u;
            expected_pixel((unsigned)r->x + x, (unsigned)r->y + y, phase, expected + offset);
        }
    }
    pack_state(test);
    GLuint buffer = 0;
    if (pbo) {
        glGenBuffers(1, &buffer); glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
        glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)length, actual, GL_STREAM_READ);
        require_gl("allocate initialized PACK buffer");
    } else {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0); require_gl("unbind PACK buffer");
    }
    printf("READPIXELS_BEGIN image=%ux%u phase=%u case=%s path=%s rect=%d,%d,%d,%d "
           "align=%d row_length=%d skip_pixels=%d skip_rows=%d base=%zu stride=%zu bytes=%zu\n",
           image_width, image_height, phase, test->name, path, r->x, r->y, r->w, r->h,
           test->alignment, test->row_length, test->skip_pixels, test->skip_rows, base, stride, length);
    glReadPixels(r->x, r->y, r->w, r->h, GL_RGBA, GL_UNSIGNED_BYTE,
                 pbo ? (void *)(uintptr_t)base : actual + base);
    int ok = check_error(GL_NO_ERROR, test->name);
    if (pbo) {
        void *mapped = map_range(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)length, GL_MAP_READ_BIT);
        if (!check_error(GL_NO_ERROR, "map PACK buffer") || !mapped) {
            ok = 0;
            printf("READPIXELS_MAP_FAILED case=%s path=%s\n", test->name, path);
        }
        if (mapped) {
            memcpy(actual, mapped, length);
            if (!unmap_buffer(GL_PIXEL_PACK_BUFFER)) ok = 0;
            if (!check_error(GL_NO_ERROR, "unmap PACK buffer")) ok = 0;
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0); glDeleteBuffers(1, &buffer);
        require_gl("delete PACK buffer");
    }
    size_t bad = compare_bytes(actual, expected, length, test->name, path);
    totals.cases++;
    if (!ok || bad) totals.failed++;
    printf("READPIXELS_RESULT image=%ux%u phase=%u case=%s path=%s bad_bytes=%zu status=%s\n",
           image_width, image_height, phase, test->name, path, bad, ok && !bad ? "PASS" : "FAIL");
    free(expected); free(actual);
}

static void expected_error_read(const char *name, GLsizei width, GLsizei height, int pbo,
                                int out_of_bounds, int mapped_test, GLenum error) {
    const size_t length = 512;
    uint8_t actual[512], expected[512];
    memset(actual, SENTINEL, sizeof(actual)); memset(expected, SENTINEL, sizeof(expected));
    reset_pack();
    GLuint buffer = 0;
    void *mapped = NULL;
    size_t base = GUARD;
    if (pbo) {
        glGenBuffers(1, &buffer); glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
        glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)length, actual, GL_STREAM_READ);
        require_gl("error-case PBO allocation");
        if (out_of_bounds) base = length - (size_t)width * (size_t)height * 4u + 1u;
        if (mapped_test) {
            mapped = map_range(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)length, GL_MAP_READ_BIT);
            require(mapped != NULL, "error-case PBO map"); require_gl("error-case PBO map error");
        }
    }
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                 pbo ? (void *)(uintptr_t)base : actual + base);
    int ok = check_error(error, name);
    if (pbo) {
        if (!mapped) {
            mapped = map_range(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)length, GL_MAP_READ_BIT);
            if (!check_error(GL_NO_ERROR, "map error-case result") || !mapped) ok = 0;
        }
        if (mapped) {
            memcpy(actual, mapped, length);
            if (!unmap_buffer(GL_PIXEL_PACK_BUFFER)) ok = 0;
            if (!check_error(GL_NO_ERROR, "unmap error-case result")) ok = 0;
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0); glDeleteBuffers(1, &buffer);
        require_gl("delete error-case PBO");
    }
    const char *path = pbo ? "pbo" : "client";
    size_t bad = compare_bytes(actual, expected, length, name, path);
    totals.cases++;
    if (!ok || bad) totals.failed++;
    printf("READPIXELS_ERROR_CASE case=%s path=%s expected_error=0x%x bad_bytes=%zu status=%s\n",
           name, path, error, bad, ok && !bad ? "PASS" : "FAIL");
}

static void error_cases(int paths, unsigned width, unsigned height) {
    for (int pbo = 0; pbo <= 1; pbo++) {
        if (!(paths & (pbo ? 2 : 1))) continue;
        expected_error_read("negative-width", -1, 1, pbo, 0, 0, GL_INVALID_VALUE);
        expected_error_read("negative-height", 1, -1, pbo, 0, 0, GL_INVALID_VALUE);
        expected_error_read("zero-width", 0, 1, pbo, 0, 0, GL_NO_ERROR);
        expected_error_read("zero-height", 1, 0, pbo, 0, 0, GL_NO_ERROR);
    }
    if (paths & 2) {
        GLsizei w = (GLsizei)(width < 4 ? width : 4), h = (GLsizei)(height < 4 ? height : 4);
        expected_error_read("pbo-one-byte-overrun", w, h, 1, 1, 0, GL_INVALID_OPERATION);
        expected_error_read("pbo-mapped", w, h, 1, 0, 1, GL_INVALID_OPERATION);
    }
    const GLenum states[] = {GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_PIXELS, GL_PACK_SKIP_ROWS};
    const char *names[] = {"invalid-alignment", "negative-row-length", "negative-skip-pixels", "negative-skip-rows"};
    reset_pack();
    for (size_t i = 0; i < ARRAY_SIZE(states); i++) {
        GLint before = 0, after = 0;
        glGetIntegerv(states[i], &before); require_gl("query valid PACK state");
        glPixelStorei(states[i], i == 0 ? 3 : -1);
        int ok = check_error(GL_INVALID_VALUE, names[i]);
        glGetIntegerv(states[i], &after);
        if (!check_error(GL_NO_ERROR, "query rejected PACK state") || after != before) ok = 0;
        totals.cases++;
        if (!ok) totals.failed++;
        printf("READPIXELS_ERROR_CASE case=%s before=%d after=%d status=%s\n",
               names[i], before, after, ok ? "PASS" : "FAIL");
        reset_pack();
    }
}

static void run_size(unsigned width, unsigned height, GLuint program, int paths, int run_errors) {
    GLuint texture = 0, framebuffer = 0;
    glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)width, (GLsizei)height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &framebuffer); glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "RGBA8 framebuffer");
    require_gl("texture/FBO allocation");
    glViewport(0, 0, (GLsizei)width, (GLsizei)height);
    glDisable(GL_DITHER); glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glUseProgram(program);
    GLint phase_location = glGetUniformLocation(program, "phase");
    require(phase_location >= 0, "phase uniform");
    struct rectangle full = {0, 0, (GLsizei)width, (GLsizei)height};
    struct rectangle center = {(GLint)(width / 4u), (GLint)(height / 3u),
        (GLsizei)(width > 1 ? width / 2u : 1), (GLsizei)(height > 1 ? height / 2u : 1)};
    struct rectangle edge = {(GLint)width - 1, (GLint)height - 1, 1, 1};
    struct rectangle inner = {width > 2 ? 1 : 0, height > 2 ? 1 : 0,
        (GLsizei)(width > 2 ? width - 2 : width), (GLsizei)(height > 2 ? height - 2 : height)};
    const struct read_case tests[] = {
        {"full-align1", full, 1, 0, 0, 0, 0},
        {"full-align2", full, 2, 0, 0, 0, 0},
        {"full-align4", full, 4, 0, 0, 0, 0},
        {"full-align8", full, 8, 0, 0, 0, 0},
        {"full-offset3", full, 1, 0, 0, 0, 3},
        {"full-padded-skips", full, 8, (GLint)width + 5, 2, 1, 0},
        {"full-offset-skips", full, 4, (GLint)width + 7, 3, 2, 3},
        {"center-align8", center, 8, 0, 0, 0, 0},
        {"last-pixel", edge, 1, 0, 0, 0, 0},
        {"inner-padded-skips", inner, 8, inner.w + 9, 3, 2, 3}
    };
    for (unsigned phase = 0; phase < 2; phase++) {
        glUniform1i(phase_location, (GLint)phase); glDrawArrays(GL_TRIANGLES, 0, 3);
        require_gl("draw exact RGBA8 coordinate pattern");
        for (size_t i = 0; i < ARRAY_SIZE(tests); i++) {
            if (paths & 1) read_case(&tests[i], phase, 0, width, height);
            if (paths & 2) read_case(&tests[i], phase, 1, width, height);
        }
    }
    if (run_errors) error_cases(paths, width, height);
    reset_pack();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &framebuffer); glDeleteTextures(1, &texture);
    require_gl("size teardown");
}

/* This independent path never calls glReadPixels or maps a GL pack buffer.
 * texelFetch preserves the integer pixel coordinate and all four RGBA bytes;
 * no normalized UV, filtering tolerance, vertical flip, or alpha blending. */
static void relay_size(EGLDisplay display, unsigned width, unsigned height,
                       GLuint pattern_program, GLuint relay_program, int upload) {
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC client =
        (void *)eglGetProcAddress("eglGetNativeClientBufferANDROID");
    PFNEGLCREATEIMAGEKHRPROC create = (void *)eglGetProcAddress("eglCreateImageKHR");
    PFNEGLDESTROYIMAGEKHRPROC destroy = (void *)eglGetProcAddress("eglDestroyImageKHR");
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target =
        (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    require(client && create && destroy && image_target, "AHB relay EGL image entry points");
    GLuint source = 0, source_fbo = 0;
    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &source); glBindTexture(GL_TEXTURE_2D, source);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)width, (GLsizei)height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &source_fbo); glBindFramebuffer(GL_FRAMEBUFFER, source_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, source, 0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
            "relay internal RGBA8 framebuffer");
    glDisable(GL_DITHER); glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, (GLsizei)width, (GLsizei)height);
    GLint phase_location = glGetUniformLocation(pattern_program, "phase");
    GLint source_location = glGetUniformLocation(relay_program, "src");
    require(phase_location >= 0 && source_location >= 0, "relay shader uniforms");
    require_gl("relay source setup");
    for (unsigned phase = 0; phase < 2; phase++) {
        printf("READPIXELS_RELAY_BEGIN image=%ux%u phase=%u path=%s_texelFetch_AHB_CPU\n",
               width, height, phase, upload ? "CPU_upload" : "internal");
        glBindFramebuffer(GL_FRAMEBUFFER, source_fbo);
        glBindTexture(GL_TEXTURE_2D, 0);  /* Source is an attachment while rendering. */
        if (upload) {
            size_t size = (size_t)width * height * 4;
            uint8_t *pixels = malloc(size);
            require(pixels != NULL, "relay CPU upload allocation");
            for (unsigned y = 0; y < height; y++) for (unsigned x = 0; x < width; x++)
                expected_pixel(x, y, phase, pixels + ((size_t)y * width + x) * 4);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0); glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
            glBindTexture(GL_TEXTURE_2D, source);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)width, (GLsizei)height,
                            GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            free(pixels);
        } else {
            glUseProgram(pattern_program); glUniform1i(phase_location, (GLint)phase);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        glFinish();
        require_gl("relay prepare internal exact pattern");

        AHardwareBuffer_Desc wanted = {.width = width, .height = height, .layers = 1,
            .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
            .usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
                     AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT};
        AHardwareBuffer *buffer = NULL;
        require(!AHardwareBuffer_allocate(&wanted, &buffer) && buffer, "relay AHB allocate");
        AHardwareBuffer_Desc actual = {0}; AHardwareBuffer_describe(buffer, &actual);
        uint64_t row_stride = 0, plane_size = 0;
        require(actual.width == width && actual.height == height && actual.layers == 1 &&
                actual.format == wanted.format && actual.stride >= width &&
                sq2_readpixels_linear_ahb_info(buffer, width, height, &row_stride, &plane_size) &&
                (uint64_t)actual.stride * 4 == row_stride, "relay real linear RGBA metadata");
        printf("READPIXELS_RELAY_LAYOUT image=%ux%u phase=%u modifier=0 row_stride=%llu "
               "plane_size=%llu pixel_stride=4\n", width, height, phase,
               (unsigned long long)row_stride, (unsigned long long)plane_size);
        AHardwareBuffer_Planes planes = {0};
        require(!AHardwareBuffer_lockPlanes(buffer, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
                                            -1, NULL, &planes), "relay poison CPU lock");
        require(planes.planeCount == 1 && planes.planes[0].data &&
                planes.planes[0].pixelStride == 4 && planes.planes[0].rowStride == row_stride,
                "relay poison lock layout");
        for (unsigned y = 0; y < height; y++)
            memset((uint8_t *)planes.planes[0].data + (size_t)y * (size_t)row_stride,
                   SENTINEL, (size_t)width * 4);
        require(!AHardwareBuffer_unlock(buffer, NULL), "relay poison CPU unlock");

        EGLClientBuffer native = client(buffer); require(native != NULL, "relay native client buffer");
        const EGLint image_attributes[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
        EGLImageKHR image = create(display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
                                    native, image_attributes);
        require(image != EGL_NO_IMAGE_KHR, "relay AHB EGL image");
        GLuint destination = 0, destination_fbo = 0;
        glGenTextures(1, &destination); glBindTexture(GL_TEXTURE_2D, destination);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        image_target(GL_TEXTURE_2D, image);
        glGenFramebuffers(1, &destination_fbo); glBindFramebuffer(GL_FRAMEBUFFER, destination_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, destination, 0);
        require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
                "relay linear AHB framebuffer");
        require_gl("relay AHB import");
        glBindTexture(GL_TEXTURE_2D, source);
        glUseProgram(relay_program); glUniform1i(source_location, 0);
        glDrawArrays(GL_TRIANGLES, 0, 3); glFinish();
        require_gl("relay texelFetch draw and finish");

        memset(&planes, 0, sizeof(planes));
        require(!AHardwareBuffer_lockPlanes(buffer, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
                                            -1, NULL, &planes), "relay CPU read lock");
        require(planes.planeCount == 1 && planes.planes[0].data &&
                planes.planes[0].pixelStride == 4 && planes.planes[0].rowStride == row_stride,
                "relay read lock layout");
        size_t bad = 0;
        for (unsigned y = 0; y < height; y++) for (unsigned x = 0; x < width; x++) {
            uint8_t expected[4]; expected_pixel(x, y, phase, expected);
            const uint8_t *pixel = (const uint8_t *)planes.planes[0].data +
                                    (size_t)y * (size_t)row_stride + (size_t)x * 4;
            for (unsigned c = 0; c < 4; c++) if (pixel[c] != expected[c]) {
                if (bad < 8) printf("READPIXELS_RELAY_BYTE image=%ux%u phase=%u x=%u y=%u "
                                    "channel=%u actual=%u expected=%u\n",
                                    width, height, phase, x, y, c, pixel[c], expected[c]);
                bad++;
            }
        }
        require(!AHardwareBuffer_unlock(buffer, NULL), "relay CPU read unlock");
        totals.cases++; totals.bad_bytes += bad;
        if (bad) totals.failed++;
        printf("READPIXELS_RELAY_RESULT image=%ux%u phase=%u checked_bytes=%zu bad_bytes=%zu "
               "status=%s\n", width, height, phase, (size_t)width * height * 4, bad,
               bad ? "FAIL" : "PASS");

        /* GPU finished and CPU unlocked before releasing image siblings. Keep
         * the real AHB alive until the FBO, texture and EGLImage are destroyed. */
        glBindTexture(GL_TEXTURE_2D, 0); glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &destination_fbo); glDeleteTextures(1, &destination);
        require_gl("relay destination teardown");
        require(destroy(display, image), "relay EGL image destroy");
        AHardwareBuffer_release(buffer);
        printf("READPIXELS_RELAY_RELEASE image=%ux%u phase=%u completed=1\n",
               width, height, phase);
    }
    glDeleteFramebuffers(1, &source_fbo); glDeleteTextures(1, &source);
    require_gl("relay source teardown");
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    unsigned custom_width = 0, custom_height = 0;
    int paths = 3, run_errors = 1, relay = 0, upload = 0, explicit_matrix_option = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--size") && i + 2 < argc) {
            custom_width = number(argv[++i]); custom_height = number(argv[++i]);
        } else if (!strcmp(argv[i], "--path") && i + 1 < argc) {
            explicit_matrix_option = 1;
            const char *path = argv[++i];
            if (!strcmp(path, "client")) paths = 1;
            else if (!strcmp(path, "pbo")) paths = 2;
            else if (!strcmp(path, "both")) paths = 3;
            else fatal("--path requires client|pbo|both");
        } else if (!strcmp(argv[i], "--no-errors")) { run_errors = 0; explicit_matrix_option = 1; }
        else if (!strcmp(argv[i], "--relay-to-ahb")) relay = 1;
        else if (!strcmp(argv[i], "--upload-relay")) { relay = 1; upload = 1; }
        else {
            fprintf(stderr, "usage: %s [--size WIDTH HEIGHT] [--path client|pbo|both] [--no-errors]\n"
                    "       %s --relay-to-ahb|--upload-relay [--size WIDTH HEIGHT]\n", argv[0], argv[0]);
            return 2;
        }
    }
    require(!relay || !explicit_matrix_option, "relay mode is separate from --path/--no-errors matrix options");
    const char *trace_value = getenv("SQ2_READPIXELS_TRACE_COPY");
    int trace_copy = trace_value && !strcmp(trace_value, "1");
#ifdef SQ2_READPIXELS_MESA_BASELINE
    require(!trace_copy, "SQ2_READPIXELS_TRACE_COPY=1 is forbidden for the Mesa baseline");
#endif
    private_environment();
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    require(display != EGL_NO_DISPLAY && eglInitialize(display, NULL, NULL), "EGL initialize");
    require(eglBindAPI(EGL_OPENGL_ES_API), "EGL bind GLES");
    const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config; EGLint count = 0;
    require(eglChooseConfig(display, attributes, &config, 1, &count) && count == 1, "GLES3 config");
    const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    const EGLint surface_attributes[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
    EGLSurface surface = eglCreatePbufferSurface(display, config, surface_attributes);
    require(context != EGL_NO_CONTEXT && surface != EGL_NO_SURFACE &&
            eglMakeCurrent(display, surface, surface, context), "GLES3 current");
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    const char *version = (const char *)glGetString(GL_VERSION);
    printf("READPIXELS_RENDERER=%s VERSION=%s\n", renderer ? renderer : "(null)", version ? version : "(null)");
#ifdef SQ2_READPIXELS_MESA_BASELINE
    require(renderer && version && strstr(version, "Mesa"), "expected original Mesa baseline");
#else
    require(renderer && strstr(renderer, "Adreno (TM) 680") && version &&
            strstr(version, "V@0819.0") && !strstr(version, "Mesa"), "expected Quest2 v77 driver");
    if (trace_copy) {
        require(!atexit(quest2_report_readback_trace), "register CPU copy trace summary");
        quest2_install_readback_trace(1);
    }
#endif
    GLint major = 0; glGetIntegerv(GL_MAJOR_VERSION, &major);
    require(major >= 3, "actual GLES3 context"); require_gl("GLES3 version");
    Dl_info egl_origin = {0}, gl_origin = {0};
    require(dladdr((void *)eglInitialize, &egl_origin) && egl_origin.dli_fname &&
            strstr(egl_origin.dli_fname, "/system/lib64/libEGL.so") &&
            dladdr((void *)glReadPixels, &gl_origin) && gl_origin.dli_fname &&
            strstr(gl_origin.dli_fname, "/system/lib64/libGLESv2.so"), "system EGL/GLES dispatch");
    printf("READPIXELS_EGL_ENTRY=%s GLES_ENTRY=%s\n", egl_origin.dli_fname, gl_origin.dli_fname);
    FILE *maps = fopen("/proc/self/maps", "re"); require(maps != NULL, "read process maps");
    char line[2048];
    while (fgets(line, sizeof(line), maps))
        if (strstr(line, "/egl/") && strstr(line, "r-xp")) printf("READPIXELS_MAP %s", line);
    fclose(maps);
    map_range = (void *)eglGetProcAddress("glMapBufferRange");
    unmap_buffer = (void *)eglGetProcAddress("glUnmapBuffer");
    require(map_range && unmap_buffer, "public GLES3 PBO entry points");
    GLuint vertex = shader(GL_VERTEX_SHADER,
        "#version 300 es\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
        "gl_Position=vec4(p*2.-1.,0.,1.);}");
    GLuint fragment = shader(GL_FRAGMENT_SHADER,
        "#version 300 es\nprecision highp float;precision highp int;uniform int phase;"
        "layout(location=0) out vec4 color;void main(){uvec2 p=uvec2(gl_FragCoord.xy);"
        "uint x=p.x,y=p.y,f=uint(phase);uvec4 c=uvec4((x*17u+y*3u+f*13u)&255u,"
        "(x*7u+y*29u+f*71u)&255u,(x*23u+y*11u+f*47u)&255u,"
        "64u+(x*5u+y*11u+f*37u)%192u);color=vec4(c)/255.;}");
    GLuint program = glCreateProgram(); glAttachShader(program, vertex); glAttachShader(program, fragment);
    glLinkProgram(program); GLint linked = 0; glGetProgramiv(program, GL_LINK_STATUS, &linked);
    require(linked, "link exact RGBA shader"); require_gl("shader setup");
    const unsigned sizes[][2] = {{1,1}, {3,5}, {13,7}, {63,65}, {64,64}, {65,63},
        {257,129}, {640,360}, {640,384}, {641,359}};
    if (relay) {
        GLuint relay_fragment = shader(GL_FRAGMENT_SHADER,
            "#version 300 es\nprecision highp float;precision highp int;"
            "uniform highp sampler2D src;layout(location=0) out vec4 color;"
            "void main(){color=texelFetch(src,ivec2(gl_FragCoord.xy),0);}");
        GLuint relay_program = glCreateProgram();
        glAttachShader(relay_program, vertex); glAttachShader(relay_program, relay_fragment);
        glLinkProgram(relay_program); glGetProgramiv(relay_program, GL_LINK_STATUS, &linked);
        require(linked, "link exact texelFetch relay shader"); require_gl("relay shader setup");
        const unsigned relay_sizes[][2] = {{640,360}, {264,136}, {640,384}, {1024,128}, {2048,128}};
        printf("READPIXELS_RELAY_MATRIX phases=2 size_count=%zu oracle=exact_RGBA8_all_pixels "
               "readback=AHB_CPU glReadPixels=unused\n",
               custom_width ? (size_t)1 : ARRAY_SIZE(relay_sizes));
        if (custom_width) relay_size(display, custom_width, custom_height, program, relay_program, upload);
        else for (size_t i = 0; i < ARRAY_SIZE(relay_sizes); i++)
            relay_size(display, relay_sizes[i][0], relay_sizes[i][1], program, relay_program, upload);
        glUseProgram(0); glDeleteProgram(relay_program); glDeleteShader(relay_fragment);
        require_gl("relay program teardown");
    } else {
        printf("READPIXELS_MATRIX paths=%d phases=2 size_count=%zu api_errors=%d oracle=exact_RGBA8_all_bytes\n",
               paths, custom_width ? (size_t)1 : ARRAY_SIZE(sizes), run_errors);
        if (custom_width) run_size(custom_width, custom_height, program, paths, run_errors);
        else for (size_t i = 0; i < ARRAY_SIZE(sizes); i++)
            run_size(sizes[i][0], sizes[i][1], program, paths, run_errors && i + 1 == ARRAY_SIZE(sizes));
    }
    glUseProgram(0); glDeleteProgram(program); glDeleteShader(vertex); glDeleteShader(fragment);
    require_gl("program teardown");
    require(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT), "release current");
    require(eglDestroySurface(display, surface) && eglDestroyContext(display, context) &&
            eglTerminate(display) && eglReleaseThread(), "EGL teardown");
    printf("READPIXELS_SUMMARY cases=%u failed=%u gl_failures=%u bad_bytes=%zu status=%s\n",
           totals.cases, totals.failed, totals.gl_failures, totals.bad_bytes, totals.failed ? "FAIL" : "PASS");
    return totals.failed ? 1 : 0;
}
