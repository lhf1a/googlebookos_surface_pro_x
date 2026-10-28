/* Private system-EGL RGBA composition probe; never installs a system driver.
 * Two unshared contexts, two GPU-produced AHB inputs and one AHB output.
 * Producer flush/native fence -> consumer server wait -> release fence ->
 * mapper5 CPU lock. No glFinish occurs on the successful comparison path.
 * All pixels are checked. Only blended RGB at alpha=128 permits +/-1 UNORM8;
 * opaque/transparent pixels, crop boundaries and alpha remain exact.
 * Run under the existing private namespace with an external GPU-hang timeout.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/sync_file.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/vfs.h>
#include <unistd.h>
#include "quest2-mapper5.h"
#include "quest2-readback-settings.h"

extern "C" const native_handle_t *AHardwareBuffer_getNativeHandle(const AHardwareBuffer *);

static bool fail(const char *message) {
    fprintf(stderr, "COMPOSITION_FAIL %s EGL=0x%x GL=0x%x errno=%d\n",
            message, eglGetError(), glGetError(), errno);
    return false;
}
#define CHECK(condition, message) do { if (!(condition)) return fail(message); } while (0)

struct Fd {
    int value = -1;
    ~Fd() { reset(); }
    void reset(int fd = -1) { if (value >= 0) close(value); value = fd; }
    int take() { int fd = value; value = -1; return fd; }
};
static int fd_count() {
    DIR *directory = opendir("/proc/self/fd");
    if (!directory) return -1;
    int count = 0;
    while (dirent *entry = readdir(directory)) if (entry->d_name[0] != '.') ++count;
    closedir(directory);
    return count - 1; // Do not count our directory FD.
}
static int compare_fd(const void *a, const void *b) {
    int x = *static_cast<const int *>(a), y = *static_cast<const int *>(b);
    return (x > y) - (x < y);
}
static bool fd_snapshot(const char *stage, unsigned phase) {
    // Only inspect our own descriptor metadata. No file contents, command
    // lines, environment or arbitrary target paths enter the diagnostic log.
    int numbers[4096]; size_t count = 0;
    DIR *directory = opendir("/proc/self/fd");
    CHECK(directory, "FD snapshot directory");
    int own = dirfd(directory); bool overflow = false;
    while (dirent *entry = readdir(directory)) {
        if (entry->d_name[0] == '.') continue;
        char *end = nullptr;
        long number = strtol(entry->d_name, &end, 10);
        if (!*entry->d_name || *end || number < 0 || number > INT_MAX || number == own) continue;
        if (count == sizeof(numbers) / sizeof(numbers[0])) { overflow = true; break; }
        numbers[count++] = static_cast<int>(number);
    }
    closedir(directory);
    CHECK(!overflow, "FD snapshot capacity exceeded");
    qsort(numbers, count, sizeof(numbers[0]), compare_fd);
    printf("COMPOSITION_FD_SNAPSHOT stage=%s phase=%u count=%zu\n", stage, phase, count);
    for (size_t i = 0; i < count; ++i) {
        char path[64], target[512];
        snprintf(path, sizeof(path), "/proc/self/fd/%d", numbers[i]);
        ssize_t length = readlink(path, target, sizeof(target) - 1);
        if (length < 0) {
            printf("COMPOSITION_FD stage=%s phase=%u fd=%d target=vanished errno=%d\n",
                   stage, phase, numbers[i], errno);
            continue;
        }
        target[length] = 0;
        // Allow only kernel descriptor kinds and graphics/device nodes.
        bool visible = !strncmp(target, "anon_inode:", 11) || !strncmp(target, "socket:[", 8) ||
                       !strncmp(target, "pipe:[", 6) || !strncmp(target, "/dev/dri/", 9) ||
                       !strncmp(target, "/dev/dma_heap/", 14) || !strcmp(target, "/dev/ion") ||
                       !strcmp(target, "/dev/null") || !strcmp(target, "/dev/kgsl-3d0");
        for (ssize_t n = 0; n < length; ++n)
            if (target[n] < 32 || target[n] > 126) visible = false;
        const char *kind = visible ? target :
            (strstr(target, "dmabuf") ? "dma-buf-name-redacted" :
             (strstr(target, "memfd:") ? "memfd-name-redacted" : "path-redacted"));
        printf("COMPOSITION_FD stage=%s phase=%u fd=%d target=%s\n", stage, phase, numbers[i], kind);
        if (visible && strstr(target, "sync_file")) {
            sync_file_info sync{};
            if (!ioctl(numbers[i], SYNC_IOC_FILE_INFO, &sync))
                printf("COMPOSITION_FD_SYNC stage=%s phase=%u fd=%d status=%d fences=%u\n",
                       stage, phase, numbers[i], sync.status, sync.num_fences);
        }
        snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", numbers[i]);
        FILE *info = fopen(path, "re");
        if (!info) continue;
        char line[256];
        while (fgets(line, sizeof(line), info)) {
            const char *keys[] = {"pos:", "flags:", "mnt_id:", "ino:", "size:", "count:"};
            bool allowed = false;
            for (const char *key : keys) if (!strncmp(line, key, strlen(key))) allowed = true;
            if (!allowed) continue;
            char *value = strchr(line, ':') + 1;
            for (const char *p = value; *p; ++p)
                if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') ||
                      (*p >= 'A' && *p <= 'F') || *p == 'x' || *p == 'X' ||
                      *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '-')) allowed = false;
            if (allowed) printf("COMPOSITION_FDINFO stage=%s phase=%u fd=%d %s", stage, phase, numbers[i], line);
        }
        fclose(info);
    }
    return true;
}
static bool extension(const char *list, const char *name) {
    if (!list) return false;
    size_t n = strlen(name);
    for (const char *p = list; (p = strstr(p, name)); p += n)
        if ((p == list || p[-1] == ' ') && (p[n] == 0 || p[n] == ' ')) return true;
    return false;
}
static bool environment() {
    char own[128], init[128];
    ssize_t a = readlink("/proc/self/ns/mnt", own, sizeof(own) - 1);
    ssize_t b = readlink("/proc/1/ns/mnt", init, sizeof(init) - 1);
    CHECK(a > 0 && b > 0 && geteuid() == 0, "root/namespace identity");
    own[a] = 0; init[b] = 0;
#ifdef SQ2_COMPOSITION_MESA_BASELINE
    CHECK(!strcmp(own, init), "Mesa baseline needs original Android namespace");
#else
    struct statfs fs;
    CHECK(strcmp(own, init) && !statfs("/vendor/lib64/egl", &fs) && fs.f_type == 0x01021994,
          "private EGL tmpfs/mount namespace required");
#endif
    FILE *guard = fopen("/sys/module/suspend/parameters/screen_only", "re");
    CHECK(guard, "read screen-only guard");
    int enabled = fgetc(guard); fclose(guard);
    CHECK(enabled == 'Y', "screen_only must remain Y");
    const char *preload = getenv("LD_PRELOAD"), *path = getenv("LD_LIBRARY_PATH");
    CHECK((!preload || !*preload) && (!path || !*path), "unset LD_PRELOAD/LD_LIBRARY_PATH");
    printf("COMPOSITION_NAMESPACE=%s INIT_NAMESPACE=%s\n", own, init);
    return true;
}
static bool parameter(const char *name, unsigned fallback, unsigned max, unsigned *out) {
    const char *value = getenv(name);
    if (!value) { *out = fallback; return true; }
    char *end = nullptr; errno = 0;
    unsigned long number = strtoul(value, &end, 10);
    CHECK(*value && !errno && end && !*end && number >= 1 && number <= max,
          "invalid bounded COMPOSITION environment parameter");
    *out = static_cast<unsigned>(number);
    return true;
}
static bool flag(const char *name, bool *out) {
    const char *value = getenv(name);
    CHECK(!value || !strcmp(value, "0") || !strcmp(value, "1"), "boolean diagnostic environment must be 0 or 1");
    *out = value && !strcmp(value, "1");
    return true;
}
static bool origin(const void *symbol, const char *expected, const char *name) {
    Dl_info info{};
    CHECK(symbol && dladdr(symbol, &info) && info.dli_fname && !strcmp(info.dli_fname, expected),
          "unexpected system dispatch entry origin");
    printf("COMPOSITION_ENTRY name=%s origin=%s\n", name, info.dli_fname);
    return true;
}
static void maps() {
    FILE *file = fopen("/proc/self/maps", "re");
    if (!file) return;
    char line[4096];
    while (fgets(line, sizeof(line), file))
        if (strstr(line, "libEGL") || strstr(line, "libGLES") || strstr(line, "adreno") ||
            strstr(line, "mapper") || strstr(line, "libgsl")) printf("COMPOSITION_MAP %s", line);
    fclose(file);
}

static const char vertex_source[] =
    "#version 300 es\n"
    "void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
    "gl_Position=vec4(p*2.-1.,0.,1.);}";
static const char pattern_source[] =
    "#version 300 es\nprecision highp float;precision highp int;"
    "uniform int phase;uniform int layer;out vec4 color;"
    "void main(){uvec2 p=uvec2(gl_FragCoord.xy);uint x=p.x,y=p.y,ph=uint(phase);uvec4 v;"
    "if(layer==0)v=uvec4((x*17u+y*3u+ph*13u)&255u,"
    "(x*7u+y*29u+ph*71u)&255u,(x*23u+y*11u+ph*47u)&255u,255u);"
    "else{uint k=(x+2u*y+ph)%3u;uint a=k==0u?0u:(k==1u?128u:255u);"
    "v=uvec4((x*11u+y*7u+ph*19u)%(a+1u),"
    "(x*5u+y*31u+ph*23u)%(a+1u),(x*29u+y*13u+ph*17u)%(a+1u),a);}"
    "color=vec4(v)/255.;}";
static const char compose_source[] =
    "#version 300 es\nprecision highp float;precision highp int;"
    "uniform highp sampler2D src;uniform ivec2 offset;out vec4 color;"
    "void main(){ivec2 p=ivec2(gl_FragCoord.xy)-offset;ivec2 s=textureSize(src,0);"
    "if(any(lessThan(p,ivec2(0)))||any(greaterThanEqual(p,s)))discard;"
    "color=texelFetch(src,p,0);}";
static GLuint compile(GLenum type, const char *text) {
    GLuint shader = glCreateShader(type);
    if (!shader) return 0;
    glShaderSource(shader, 1, &text, nullptr); glCompileShader(shader);
    GLint ok = 0; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048]; glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        fprintf(stderr, "COMPOSITION_SHADER %s\n", log); glDeleteShader(shader); return 0;
    }
    return shader;
}
static GLuint program(const char *fragment_source) {
    GLuint vertex = compile(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment = compile(GL_FRAGMENT_SHADER, fragment_source);
    GLuint result = vertex && fragment ? glCreateProgram() : 0;
    if (result) {
        glAttachShader(result, vertex); glAttachShader(result, fragment); glLinkProgram(result);
        GLint ok = 0; glGetProgramiv(result, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048]; glGetProgramInfoLog(result, sizeof(log), nullptr, log);
            fprintf(stderr, "COMPOSITION_LINK %s\n", log); glDeleteProgram(result); result = 0;
        }
    }
    if (vertex) glDeleteShader(vertex);
    if (fragment) glDeleteShader(fragment);
    return result;
}

struct Harness {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext contexts[2] = {EGL_NO_CONTEXT, EGL_NO_CONTEXT};
    EGLSurface surfaces[2] = {EGL_NO_SURFACE, EGL_NO_SURFACE};
    GLuint programs[2]{};
    bool initialized = false, cleaned = false;
    void *gles1 = nullptr, *bridge = nullptr;
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC client = nullptr;
    PFNEGLCREATEIMAGEKHRPROC create_image = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC destroy_image = nullptr;
    PFNEGLCREATESYNCKHRPROC create_sync = nullptr;
    PFNEGLDESTROYSYNCKHRPROC destroy_sync = nullptr;
    PFNEGLWAITSYNCKHRPROC wait_sync = nullptr;
    PFNEGLDUPNATIVEFENCEFDANDROIDPROC dup_fence = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target = nullptr;
    void (*blend_func)(GLenum, GLenum) = nullptr;
    unsigned (*refs)() = nullptr;
    unsigned exported = 0, waited = 0;
    ~Harness() { cleanup(); }
    bool current(unsigned index) {
        CHECK(eglMakeCurrent(display, surfaces[index], surfaces[index], contexts[index]), "make context current");
        return true;
    }
    bool start() {
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint major = 0, minor = 0;
        CHECK(display != EGL_NO_DISPLAY && eglInitialize(display, &major, &minor), "initialize EGL");
        initialized = true;
        CHECK(eglBindAPI(EGL_OPENGL_ES_API), "bind ES API");
        const char *extensions = eglQueryString(display, EGL_EXTENSIONS);
        printf("COMPOSITION_EGL=%d.%d EXTENSIONS=%s\n", major, minor, extensions ? extensions : "(null)");
        CHECK(extension(extensions, "EGL_ANDROID_native_fence_sync") &&
              extension(extensions, "EGL_KHR_wait_sync") && extension(extensions, "EGL_KHR_image_base") &&
              extension(extensions, "EGL_ANDROID_image_native_buffer") &&
              extension(extensions, "EGL_ANDROID_get_native_client_buffer"), "required actual EGL extensions");
#define LOAD(field, type, name) field = reinterpret_cast<type>(eglGetProcAddress(name))
        LOAD(client, PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC, "eglGetNativeClientBufferANDROID");
        LOAD(create_image, PFNEGLCREATEIMAGEKHRPROC, "eglCreateImageKHR");
        LOAD(destroy_image, PFNEGLDESTROYIMAGEKHRPROC, "eglDestroyImageKHR");
        LOAD(create_sync, PFNEGLCREATESYNCKHRPROC, "eglCreateSyncKHR");
        LOAD(destroy_sync, PFNEGLDESTROYSYNCKHRPROC, "eglDestroySyncKHR");
        LOAD(wait_sync, PFNEGLWAITSYNCKHRPROC, "eglWaitSyncKHR");
        LOAD(dup_fence, PFNEGLDUPNATIVEFENCEFDANDROIDPROC, "eglDupNativeFenceFDANDROID");
#undef LOAD
        CHECK(client && create_image && destroy_image && create_sync && destroy_sync && wait_sync && dup_fence,
              "required EGL entry points");
        gles1 = dlopen("/system/lib64/libGLESv1_CM.so", RTLD_NOW | RTLD_LOCAL);
        CHECK(gles1, "open system libGLESv1_CM");
        image_target = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(dlsym(gles1, "glEGLImageTargetTexture2DOES"));
        blend_func = reinterpret_cast<void (*)(GLenum, GLenum)>(dlsym(gles1, "glBlendFunc"));
        CHECK(origin(reinterpret_cast<void *>(image_target), "/system/lib64/libGLESv1_CM.so", "glEGLImageTargetTexture2DOES") &&
              origin(reinterpret_cast<void *>(blend_func), "/system/lib64/libGLESv1_CM.so", "glBlendFunc") &&
              origin(reinterpret_cast<void *>(eglCreateContext), "/system/lib64/libEGL.so", "eglCreateContext"), "system dispatcher origins");
        const EGLint wanted[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
        EGLConfig config; EGLint count = 0;
        CHECK(eglChooseConfig(display, wanted, &config, 1, &count) && count == 1, "RGBA8 ES3 config");
        const EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        const EGLint sa[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        for (unsigned i = 0; i < 2; ++i) {
            contexts[i] = eglCreateContext(display, config, EGL_NO_CONTEXT, ca);
            surfaces[i] = eglCreatePbufferSurface(display, config, sa);
            CHECK(contexts[i] != EGL_NO_CONTEXT && surfaces[i] != EGL_NO_SURFACE && current(i), "unshared context/surface");
            programs[i] = program(i ? compose_source : pattern_source);
            CHECK(programs[i] && glGetError() == GL_NO_ERROR, "build ES3 program");
            glDisable(GL_DITHER); glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
            glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_CULL_FACE);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }
        const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
        const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
        printf("COMPOSITION_RENDERER=%s VERSION=%s\n", renderer ? renderer : "(null)", version ? version : "(null)");
#ifdef SQ2_COMPOSITION_MESA_BASELINE
        CHECK(version && strstr(version, "Mesa"), "expected Mesa baseline");
#else
        CHECK(renderer && strstr(renderer, "Adreno (TM) 680") && version && strstr(version, "V@0819.0") &&
              !strstr(version, "Mesa"), "expected Quest2 v77 donor");
        bridge = dlopen("/vendor/lib64/egl/libqti-mapper3-bridge.so", RTLD_NOW | RTLD_NOLOAD);
        if (bridge) refs = reinterpret_cast<unsigned (*)()>(dlsym(bridge, "sq2_qti_mapper3_address_refs"));
        // Never load a second registry merely to obtain a misleading zero.
        // Android's sphal namespace may hide the live instance from this probe.
        if (refs) {
            CHECK(refs() == 0, "initial mapper references must be zero");
            Dl_info entry{}; CHECK(dladdr(reinterpret_cast<void *>(refs), &entry), "mapper stats origin");
            printf("COMPOSITION_STATS available=1 module=%s base=%p\n", entry.dli_fname, entry.dli_fbase);
        } else printf("COMPOSITION_STATS available=0 verification=QTI_ADDRESS_RELEASE_wrapper_log_required\n");
#endif
        maps();
        return true;
    }
    bool export_fence(Fd &fd, const char *name) {
        EGLSyncKHR sync = create_sync(display, EGL_SYNC_NATIVE_FENCE_ANDROID, nullptr);
        CHECK(sync != EGL_NO_SYNC_KHR, "create native producer fence");
        glFlush(); // Native fence is materialized by Flush, not a CPU wait.
        GLenum error = glGetError();
        fd.reset(dup_fence(display, sync));
        EGLBoolean destroyed = destroy_sync(display, sync);
        CHECK(error == GL_NO_ERROR && destroyed && fd.value >= 0, "flush/dup/destroy native fence");
        sync_file_info info{};
        CHECK(fcntl(fd.value, F_GETFD) >= 0 && !ioctl(fd.value, SYNC_IOC_FILE_INFO, &info) &&
              info.num_fences > 0 && info.status >= 0, "real sync_file survives EGLSync destruction");
        ++exported;
        printf("COMPOSITION_FENCE stage=%s sync_file=1 fences=%u status=%d export=%u\n",
               name, info.num_fences, info.status, exported);
        return true;
    }
    bool server_wait(Fd &fd) {
        CHECK(fd.value >= 0, "server wait requires a real fence");
        const char *trace = getenv("SQ2_COMPOSITION_FD_DIAGNOSTIC");
        bool tracing = trace && !strcmp(trace, "1");
        const char *control = getenv("SQ2_COMPOSITION_FD_CPU_CONTROL");
        const char *import_control = getenv("SQ2_COMPOSITION_FD_IMPORT_CONTROL");
        bool import_only = tracing && import_control && !strcmp(import_control, "1");
        if (tracing && ((control && !strcmp(control, "1")) || import_only)) {
            struct pollfd pending{fd.value, POLLIN, 0}; int result;
            do { result = poll(&pending, 1, 5000); } while (result < 0 && errno == EINTR);
            CHECK(result == 1 && (pending.revents & POLLIN) && !(pending.revents & (POLLERR | POLLNVAL)), "diagnostic CPU control wait");
            printf("COMPOSITION_DIAGNOSTIC_CPU_CONTROL fd=%d no_server_wait=1\n", fd.value);
            if (!import_only) { fd.reset(); return true; }
        }
        if (tracing) printf("COMPOSITION_DIAGNOSTIC_IMPORT fd=%d wait=%u\n", fd.value, waited);
        if (tracing) CHECK(fd_snapshot("server-before-import", waited), "server fence snapshot");
        // ANDROID_native_fence_sync transfers ownership on eglCreateSyncKHR.
        // Do not close or reuse the submitted integer, even when create fails.
        const EGLint attributes[] = {EGL_SYNC_NATIVE_FENCE_FD_ANDROID, fd.take(), EGL_NONE};
        EGLSyncKHR sync = create_sync(display, EGL_SYNC_NATIVE_FENCE_ANDROID, attributes);
        CHECK(sync != EGL_NO_SYNC_KHR, "import native fence");
        if (tracing) CHECK(fd_snapshot("server-after-import", waited), "server fence snapshot");
        EGLBoolean ok = import_only ? EGL_TRUE : wait_sync(display, sync, 0);
        if (tracing) CHECK(fd_snapshot("server-after-wait", waited), "server fence snapshot");
        if (tracing) quest2_log_readback_settings();
        EGLBoolean destroyed = destroy_sync(display, sync);
        if (tracing) CHECK(fd_snapshot("server-after-destroy", waited), "server fence snapshot");
        CHECK(ok && destroyed, "GPU server wait/destroy imported fence");
        if (!import_only) ++waited;
        return true;
    }
    bool cleanup() {
        if (cleaned) return true;
        cleaned = true; bool ok = true;
        for (unsigned i = 0; i < 2; ++i) if (contexts[i] != EGL_NO_CONTEXT) {
            if (surfaces[i] != EGL_NO_SURFACE && current(i)) {
                if (programs[i]) glDeleteProgram(programs[i]);
                if (glGetError() != GL_NO_ERROR) ok = false;
            } else ok = false;
        }
        if (initialized) {
            if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) ok = false;
            for (unsigned i = 0; i < 2; ++i) {
                if (surfaces[i] != EGL_NO_SURFACE && !eglDestroySurface(display, surfaces[i])) ok = false;
                if (contexts[i] != EGL_NO_CONTEXT && !eglDestroyContext(display, contexts[i])) ok = false;
            }
            if (!eglTerminate(display) || !eglReleaseThread()) ok = false;
        }
        int remaining = refs ? static_cast<int>(refs()) : -1;
        printf("COMPOSITION_FINAL mapper_refs=%d stats_available=%d exports=%u server_waits=%u cleanup=%s\n",
               remaining, !!refs, exported, waited, ok && remaining <= 0 ? "PASS" : "FAIL");
        if (bridge) dlclose(bridge);
        if (gles1) dlclose(gles1);
        return ok && remaining <= 0;
    }
};

static void pixel(unsigned x, unsigned y, unsigned phase, unsigned layer, unsigned out[4]) {
    if (!layer) {
        out[0] = (x * 17u + y * 3u + phase * 13u) & 255u;
        out[1] = (x * 7u + y * 29u + phase * 71u) & 255u;
        out[2] = (x * 23u + y * 11u + phase * 47u) & 255u;
        out[3] = 255;
    } else {
        unsigned kind = (x + 2u * y + phase) % 3u;
        unsigned a = kind == 0 ? 0 : (kind == 1 ? 128 : 255);
        out[0] = (x * 11u + y * 7u + phase * 19u) % (a + 1u);
        out[1] = (x * 5u + y * 31u + phase * 23u) % (a + 1u);
        out[2] = (x * 29u + y * 13u + phase * 17u) % (a + 1u);
        out[3] = a;
    }
}

struct Cycle {
    Harness &h;
    AHardwareBuffer *buffers[3]{};
    sq2::BufferInfo layouts[3]{};
    GLuint textures[2][3]{}, fbos[2][3]{};
    EGLImageKHR images[2][3]{};
    Fd fences[3];
    AIMapper *mapper = nullptr;
    buffer_handle_t cpu_handle = nullptr;
    bool locked = false, complete = false, cleaned = false;
    explicit Cycle(Harness &harness) : h(harness) {}
    ~Cycle() { cleanup(); }
    bool allocate(unsigned w, unsigned height) {
        AHardwareBuffer_Desc desc{};
        desc.width = w; desc.height = height; desc.layers = 1;
        desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
        desc.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
                     AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
        for (unsigned i = 0; i < 3; ++i) {
            CHECK(!AHardwareBuffer_allocate(&desc, &buffers[i]) && buffers[i], "allocate RGBA AHB");
            AHardwareBuffer_Desc actual{}; AHardwareBuffer_describe(buffers[i], &actual);
            auto &b = layouts[i];
            CHECK(sq2::color_info(AHardwareBuffer_getNativeHandle(buffers[i]), b) && b.width == w && b.height == height &&
                  b.layers == 1 && b.format == 1 && b.fourcc == 0x34324241u && b.sample_bits == 32 &&
                  b.offset == 0 && b.modifier == 0 && !(b.usage & AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT) &&
                  b.stride >= uint64_t(w) * 4 && b.stride == uint64_t(actual.stride) * 4 &&
                  height <= b.plane_size / b.stride, "real unprotected linear RGBA metadata");
            printf("COMPOSITION_LAYOUT buffer=%u width=%u height=%u stride=%llu size=%llu modifier=0\n",
                   i, w, height, static_cast<unsigned long long>(b.stride), static_cast<unsigned long long>(b.plane_size));
        }
        return true;
    }
    bool import(unsigned context, unsigned index, bool target) {
        EGLClientBuffer client = h.client(buffers[index]);
        CHECK(client, "AHB native client buffer");
        const EGLint attributes[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
        images[context][index] = h.create_image(h.display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, client, attributes);
        CHECK(images[context][index] != EGL_NO_IMAGE_KHR, "AHB EGL image import");
        glGenTextures(1, &textures[context][index]); glBindTexture(GL_TEXTURE_2D, textures[context][index]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        h.image_target(GL_TEXTURE_2D, images[context][index]); // Exact SF GLESv1 export.
        CHECK(glGetError() == GL_NO_ERROR, "system GLESv1 image-target on ES3 context");
        if (target) {
            glGenFramebuffers(1, &fbos[context][index]); glBindFramebuffer(GL_FRAMEBUFFER, fbos[context][index]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[context][index], 0);
            CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "AHB framebuffer complete");
        }
        // Each input is imported once in each context. Destroy images early;
        // both independent texture siblings must retain the same real AHB.
        CHECK(h.destroy_image(h.display, images[context][index]), "early EGLImage destruction");
        images[context][index] = EGL_NO_IMAGE_KHR;
        return true;
    }
    bool run(unsigned w, unsigned height, unsigned phase) {
        CHECK(allocate(w, height) && h.current(0), "allocate/producer context");
        glViewport(0, 0, w, height); glUseProgram(h.programs[0]);
        GLint phase_location = glGetUniformLocation(h.programs[0], "phase");
        GLint layer_location = glGetUniformLocation(h.programs[0], "layer");
        CHECK(phase_location >= 0 && layer_location >= 0, "pattern uniforms");
        glUniform1i(phase_location, static_cast<GLint>(phase));
        for (unsigned i = 0; i < 2; ++i) {
            CHECK(import(0, i, true), "import producer target");
            glBindTexture(GL_TEXTURE_2D, 0); glUniform1i(layer_location, i);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            CHECK(glGetError() == GL_NO_ERROR && h.export_fence(fences[i], i ? "producer-top" : "producer-base"), "producer draw/export");
        }
        CHECK(h.current(1), "consumer context");
        for (unsigned i = 0; i < 2; ++i) CHECK(import(1, i, false), "import consumer source");
        CHECK(import(1, 2, true), "import consumer output");
        for (unsigned i = 0; i < 2; ++i) CHECK(h.server_wait(fences[i]), "producer-to-consumer fence");
        glViewport(0, 0, w, height); glUseProgram(h.programs[1]); glActiveTexture(GL_TEXTURE0);
        GLint source_location = glGetUniformLocation(h.programs[1], "src");
        GLint offset_location = glGetUniformLocation(h.programs[1], "offset");
        CHECK(source_location >= 0 && offset_location >= 0, "composition uniforms");
        glUniform1i(source_location, 0); glUniform2i(offset_location, 0, 0);
        glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
        glBindTexture(GL_TEXTURE_2D, textures[1][0]); glDrawArrays(GL_TRIANGLES, 0, 3);
        const int tx = phase & 1 ? -2 : 3, ty = phase & 1 ? 1 : -2;
        const unsigned sx = w / 7, sy = height / 9;
        const unsigned sw = w - sx - w / 11, sh = height - sy - height / 13;
        glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
        h.blend_func(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // Exact SF GLESv1 export.
        GLint sf = 0, df = 0;
        glGetIntegerv(GL_BLEND_SRC_RGB, &sf); glGetIntegerv(GL_BLEND_DST_RGB, &df);
        CHECK(sf == GL_ONE && df == GL_ONE_MINUS_SRC_ALPHA, "GLESv1 blend dispatch state in ES3");
        glEnable(GL_SCISSOR_TEST); glScissor(sx, sy, sw, sh);
        glUniform2i(offset_location, tx, ty); glBindTexture(GL_TEXTURE_2D, textures[1][1]);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND);
        CHECK(glGetError() == GL_NO_ERROR && h.export_fence(fences[2], "consumer-release"), "composition draw/export");
        mapper = sq2::mapper5();
        CHECK(mapper && mapper->v5.importBuffer(AHardwareBuffer_getNativeHandle(buffers[2]), &cpu_handle) == AIMAPPER_ERROR_NONE,
              "CPU mapper import output AHB");
        void *address = nullptr;
        ARect region{0, 0, static_cast<int32_t>(w), static_cast<int32_t>(height)};
        // AIMapper v5 explicitly owns acquireFence on success AND error. The
        // original dup survives destroyed EGLSync; no poll/client wait/finish.
        CHECK(mapper->v5.lock(cpu_handle, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, region,
                             fences[2].take(), &address) == AIMAPPER_ERROR_NONE, "output release fence -> CPU lock");
        locked = true; complete = true;
        CHECK(address, "CPU output address");
        size_t bad = 0, rounded = 0, mixed = 0; unsigned max_error = 0;
        for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < w; ++x) {
            unsigned expected[4], top[4]; pixel(x, y, phase, 0, expected);
            int px = static_cast<int>(x) - tx, py = static_cast<int>(y) - ty;
            bool inside = x >= sx && x < sx + sw && y >= sy && y < sy + sh &&
                          px >= 0 && py >= 0 && px < static_cast<int>(w) && py < static_cast<int>(height);
            bool half = false;
            if (inside) {
                pixel(static_cast<unsigned>(px), static_cast<unsigned>(py), phase, 1, top);
                half = top[3] == 128;
                for (unsigned c = 0; c < 4; ++c)
                    expected[c] = top[c] + (expected[c] * (255u - top[3]) + 127u) / 255u;
            }
            if (half) ++mixed;
            const uint8_t *actual = static_cast<const uint8_t *>(address) + size_t(y) * layouts[2].stride + size_t(x) * 4;
            for (unsigned c = 0; c < 4; ++c) {
                unsigned error = actual[c] > expected[c] ? actual[c] - expected[c] : expected[c] - actual[c];
                if (error > max_error) max_error = error;
                unsigned tolerance = half && c < 3 ? 1 : 0;
                if (error && error <= tolerance) ++rounded;
                if (error > tolerance) {
                    if (bad < 8) printf("COMPOSITION_BYTE phase=%u x=%u y=%u channel=%u actual=%u expected=%u tolerance=%u\n",
                                        phase, x, y, c, actual[c], expected[c], tolerance);
                    ++bad;
                }
            }
        }
        int release = -1;
        AIMapper_Error unlocked = mapper->v5.unlock(cpu_handle, &release); locked = false;
        Fd unlock_fd; unlock_fd.reset(release);
        CHECK(unlocked == AIMAPPER_ERROR_NONE, "CPU unlock output AHB");
        if (unlock_fd.value >= 0) {
            struct pollfd poller{unlock_fd.value, POLLIN, 0}; int result;
            do { result = poll(&poller, 1, 5000); } while (result < 0 && errno == EINTR);
            CHECK(result == 1 && (poller.revents & POLLIN) && !(poller.revents & (POLLERR | POLLNVAL)), "CPU unlock release fence");
        }
        printf("COMPOSITION_RESULT phase=%u width=%u height=%u translate=%d,%d scissor=%u,%u,%u,%u "
               "checked_bytes=%zu half_alpha_pixels=%zu rounded_bytes=%zu max_error=%u bad_bytes=%zu status=%s\n",
               phase, w, height, tx, ty, sx, sy, sw, sh, size_t(w) * height * 4,
               mixed, rounded, max_error, bad, bad ? "FAIL" : "PASS");
        return !bad;
    }
    bool cleanup() {
        if (cleaned) return true;
        cleaned = true; bool ok = true;
        if (locked && mapper && cpu_handle) {
            int fd = -1;
            if (mapper->v5.unlock(cpu_handle, &fd)) ok = false; else locked = false;
            if (fd >= 0) close(fd);
        }
        if (cpu_handle && mapper && !locked) {
            if (mapper->v5.freeBuffer(cpu_handle)) ok = false; else cpu_handle = nullptr;
        }
        for (unsigned c = 0; c < 2; ++c) {
            if (h.contexts[c] == EGL_NO_CONTEXT || !h.current(c)) { ok = false; continue; }
            // Failure-only draining is cleanup, never successful test evidence.
            if (!complete) { glFinish(); printf("COMPOSITION_FAILURE_DRAIN context=%u\n", c); }
            glBindFramebuffer(GL_FRAMEBUFFER, 0); glBindTexture(GL_TEXTURE_2D, 0);
            for (unsigned i = 0; i < 3; ++i) {
                if (fbos[c][i]) glDeleteFramebuffers(1, &fbos[c][i]);
                if (textures[c][i]) glDeleteTextures(1, &textures[c][i]);
                if (images[c][i] != EGL_NO_IMAGE_KHR) {
                    if (!h.destroy_image(h.display, images[c][i])) ok = false;
                    else images[c][i] = EGL_NO_IMAGE_KHR;
                }
                fbos[c][i] = textures[c][i] = 0;
            }
            if (glGetError() != GL_NO_ERROR) ok = false;
        }
        for (unsigned i = 0; i < 3; ++i) {
            fences[i].reset();
            if (buffers[i]) AHardwareBuffer_release(buffers[i]);
            buffers[i] = nullptr;
        }
        int remaining = h.refs ? static_cast<int>(h.refs()) : -1;
        unsigned image_count = 0, texture_count = 0, framebuffer_count = 0;
        for (unsigned c = 0; c < 2; ++c) for (unsigned i = 0; i < 3; ++i) {
            image_count += images[c][i] != EGL_NO_IMAGE_KHR;
            texture_count += textures[c][i] != 0;
            framebuffer_count += fbos[c][i] != 0;
        }
        printf("COMPOSITION_RELEASE mapper_refs=%d stats_available=%d own_ahbs=0 own_images=%u own_textures=%u "
               "own_framebuffers=%u own_fences=0 own_cpu_imports=%u cleanup=%s\n",
               remaining, !!h.refs, image_count, texture_count, framebuffer_count, cpu_handle ? 1u : 0u,
               ok && remaining <= 0 ? "PASS" : "FAIL");
        return ok && remaining <= 0;
    }
};

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    unsigned width = 64, height = 64, cycles = 3;
    bool snapshots = false, diagnostic = false;
    if (!environment() || !parameter("SQ2_COMPOSITION_WIDTH", 64, 4096, &width) ||
        !parameter("SQ2_COMPOSITION_HEIGHT", 64, 4096, &height) ||
        !parameter("SQ2_COMPOSITION_CYCLES", 3, 150, &cycles) ||
        !flag("SQ2_COMPOSITION_FD_SNAPSHOT", &snapshots) ||
        !flag("SQ2_COMPOSITION_FD_DIAGNOSTIC", &diagnostic)) return 2;
    if (diagnostic && (cycles < 3 || cycles > 5)) {
        fprintf(stderr, "COMPOSITION_FAIL FD diagnostic requires 3..5 cycles\n"); return 2;
    }
    const char *cpu_control = getenv("SQ2_COMPOSITION_FD_CPU_CONTROL");
    if (cpu_control && (!diagnostic || strcmp(cpu_control, "1"))) {
        fprintf(stderr, "COMPOSITION_FAIL CPU control requires FD diagnostic mode\n"); return 2;
    }
    const char *import_control = getenv("SQ2_COMPOSITION_FD_IMPORT_CONTROL");
    if (import_control && (!diagnostic || strcmp(import_control, "1") || cpu_control)) {
        fprintf(stderr, "COMPOSITION_FAIL import control requires exclusive FD diagnostic mode\n"); return 2;
    }
    snapshots = snapshots || diagnostic;
    if (diagnostic) printf("COMPOSITION_DIAGNOSTIC_ONLY fd_growth_continuation=1 maximum_cycles=5 no_validation_pass=1\n");
    Harness h;
    if (!h.start()) return 2;
    if (snapshots && !fd_snapshot("initialized", 0)) return 2;
    int fd_start = fd_count(), fd_baseline = -1;
    bool ok = fd_start >= 0; unsigned passed = 0, completed = 0, fd_growth_events = 0;
    for (unsigned phase = 0; ok && phase < cycles; ++phase) {
        Cycle cycle(h);
        bool pixels = cycle.run(width, height, phase);
        bool released = cycle.cleanup();
        int fds = fd_count();
        if (phase == 0) fd_baseline = fds; // First use may initialize permanent allocator/driver FDs.
        bool stable = fds >= 0 && fds == fd_baseline;
        if (!stable) ++fd_growth_events;
        printf("COMPOSITION_FDS phase=%u before_first=%d after_first=%d current=%d delta_from_first=%d status=%s\n",
               phase, fd_start, fd_baseline, fds, fds - fd_baseline, stable ? "PASS" : "FAIL");
        bool snapshot = !snapshots || fd_snapshot("cycle-released", phase);
        ok = pixels && released && snapshot && fds >= 0 && (stable || diagnostic);
        if (pixels && released && snapshot && stable) ++passed;
        ++completed;
    }
    bool cleanup = h.cleanup();
    if (snapshots && !fd_snapshot("egl-terminated", completed)) cleanup = false;
    if (diagnostic) {
        printf("COMPOSITION_SUMMARY status=DIAGNOSTIC_ONLY completed=%u requested=%u strict_passed=%u "
               "fd_growth_events=%u pixels_and_cleanup=%s validation_pass=0\n",
               completed, cycles, passed, fd_growth_events,
               ok && cleanup && completed == cycles ? "COMPLETE" : "FAILED");
        return 3; // A diagnostic run can never be mistaken for a passing test.
    }
    printf("COMPOSITION_SUMMARY passed=%u requested=%u pixel_tolerance=RGB_HALF_ALPHA_ONLY_1 "
           "fd_growth_checks=%u status=%s\n", passed, cycles, passed ? passed - 1 : 0,
           ok && cleanup && passed == cycles ? "PASS" : "FAIL");
    return ok && cleanup && passed == cycles ? 0 : 1;
}
