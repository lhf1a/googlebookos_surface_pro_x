/* JNI producer -> SurfaceControl BLAST BufferQueue -> existing Mesa SurfaceFlinger.
 * Requires the private donor EGL mount namespace; it never changes global mounts.
 * Successful submissions are not proof of correctly displayed pixels.
 */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window_jni.h>
#include <dlfcn.h>
#include <errno.h>
#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>

#define DURATION_NS INT64_C(5000000000)
#define FRAME_NS INT64_C(16666667)
#define TMPFS_MAGIC 0x01021994

static int64_t monotonic_ns(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value)) return -1;
    return (int64_t)value.tv_sec * INT64_C(1000000000) + value.tv_nsec;
}

static void dump_graphics_maps(void) {
    FILE *maps = fopen("/proc/self/maps", "re");
    if (!maps) {
        fprintf(stderr, "VISIBLE_MAPS_UNAVAILABLE errno=%d\n", errno);
        return;
    }
    char line[4096];
    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, "libEGL") || strstr(line, "libGLES") || strstr(line, "adreno") ||
            strstr(line, "mapper") || strstr(line, "libgsl") || strstr(line, "libquest2"))
            printf("VISIBLE_MAP %s", line);
    }
    fclose(maps);
}

static GLuint compile_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    if (!shader) return 0;
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "VISIBLE_SHADER_FAIL %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

JNIEXPORT jint JNICALL Java_Sq2Quest2Visible_draw(JNIEnv *env, jclass type,
                                                 jobject java_surface, jint width, jint height) {
    (void)type;
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *failure = NULL;
    ANativeWindow *window = NULL;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    GLuint vertex = 0, fragment = 0, program = 0;
    int initialized = 0, current = 0, frames = 0;
    struct statfs fs;
    char own[128], init[128];
    ssize_t own_length = readlink("/proc/self/ns/mnt", own, sizeof(own) - 1);
    ssize_t init_length = readlink("/proc/1/ns/mnt", init, sizeof(init) - 1);
#define REQUIRE(condition, message) do { if (!(condition)) { failure = (message); goto cleanup; } } while (0)
    REQUIRE(own_length > 0 && init_length > 0, "read mount namespaces");
    own[own_length] = '\0';
    init[init_length] = '\0';
    REQUIRE(strcmp(own, init), "private mount namespace required");
    REQUIRE(!statfs("/vendor/lib64/egl", &fs) && fs.f_type == TMPFS_MAGIC,
            "private EGL tmpfs required");
    REQUIRE(geteuid() == 0 && width >= 64 && width <= 640 && height >= 64 && height <= 360,
            "root and bounded window dimensions required");
    window = ANativeWindow_fromSurface(env, java_surface);
    REQUIRE(window, "ANativeWindow_fromSurface");
    REQUIRE(!ANativeWindow_setBuffersGeometry(window, width, height, WINDOW_FORMAT_RGBA_8888),
            "RGBA window geometry");
    printf("VISIBLE_NATIVE_WINDOW width=%d height=%d format=%d\n",
           ANativeWindow_getWidth(window), ANativeWindow_getHeight(window),
           ANativeWindow_getFormat(window));
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    REQUIRE(display != EGL_NO_DISPLAY && eglInitialize(display, &major, &minor), "EGL initialize");
    initialized = 1;
    REQUIRE(eglBindAPI(EGL_OPENGL_ES_API), "EGL bind GLES");
    const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config;
    EGLint count = 0, format = 0;
    REQUIRE(eglChooseConfig(display, attributes, &config, 1, &count) && count == 1 &&
            eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format) &&
            format == WINDOW_FORMAT_RGBA_8888, "RGBA window config");
    const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
    REQUIRE(context != EGL_NO_CONTEXT, "EGL context");
    surface = eglCreateWindowSurface(display, config, window, NULL);
    REQUIRE(surface != EGL_NO_SURFACE && eglMakeCurrent(display, surface, surface, context),
            "EGL window surface/current");
    current = 1;
    REQUIRE(eglSwapInterval(display, 1), "EGL swap interval");
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    const char *version = (const char *)glGetString(GL_VERSION);
    printf("VISIBLE_EGL=%d.%d RENDERER=%s VERSION=%s\n", major, minor,
           renderer ? renderer : "(null)", version ? version : "(null)");
    dump_graphics_maps();
    REQUIRE(renderer && strstr(renderer, "Adreno (TM) 680") && version &&
            strstr(version, "V@0819.0") && !strstr(version, "Mesa"), "expected Quest2 v77 GLES");
    Dl_info origin = {0};
    REQUIRE(dladdr((void *)eglSwapBuffers, &origin) && origin.dli_fname &&
            strstr(origin.dli_fname, "/system/lib64/libEGL.so"), "system EGL dispatch");
    printf("VISIBLE_SWAP_ENTRY=%s\n", origin.dli_fname);
    vertex = compile_shader(GL_VERTEX_SHADER,
            "attribute vec2 p;void main(){gl_Position=vec4(p,0.,1.);}");
    fragment = compile_shader(GL_FRAGMENT_SHADER,
            "precision highp float;uniform vec2 halfSize;uniform float phase;"
            "void main(){gl_FragColor=vec4(step(halfSize.x,gl_FragCoord.x),"
            "step(halfSize.y,gl_FragCoord.y),phase,1.);}");
    REQUIRE(vertex && fragment, "compile shaders");
    program = glCreateProgram();
    REQUIRE(program, "create program");
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glBindAttribLocation(program, 0, "p");
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    REQUIRE(linked, "link program");
    glUseProgram(program);
    const GLfloat vertices[] = {-1, -1, 3, -1, -1, 3};
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(0);
    glUniform2f(glGetUniformLocation(program, "halfSize"), width * 0.5f, height * 0.5f);
    GLint phase_location = glGetUniformLocation(program, "phase");
    REQUIRE(phase_location >= 0, "phase uniform");
    glViewport(0, 0, width, height);
    glDisable(GL_DITHER);
    int64_t start = monotonic_ns();
    REQUIRE(start >= 0, "monotonic clock");
    int previous_phase = -1;
    while (frames < 400) {
        int64_t now = monotonic_ns();
        REQUIRE(now >= start, "monotonic frame clock");
        if (now - start >= DURATION_NS) break;
        int phase = (int)((now - start) / INT64_C(1000000000)) & 1;
        glUniform1f(phase_location, (float)phase);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        REQUIRE(glGetError() == GL_NO_ERROR && eglSwapBuffers(display, surface), "draw/swap");
        frames++;
        if (phase != previous_phase) {
            printf("VISIBLE_FRAME frame=%d elapsed_ms=%lld blue_phase=%d\n", frames,
                   (long long)((now - start) / INT64_C(1000000)), phase);
            previous_phase = phase;
        }
        // Cap submissions even when this driver does not block on swap interval.
        int64_t target = start + (int64_t)frames * FRAME_NS;
        if (target > start + DURATION_NS) target = start + DURATION_NS;
        now = monotonic_ns();
        REQUIRE(now >= start, "monotonic pacing clock");
        if (target > now) {
            struct timespec delay = {0, (long)(target - now)};
            while (nanosleep(&delay, &delay) && errno == EINTR) {}
        }
    }
    REQUIRE(frames > 0, "no submitted frames");
    glFinish();
    REQUIRE(glGetError() == GL_NO_ERROR, "finish");
    printf("VISIBLE_DRAW_COMPLETE frames=%d visual_confirmation_required=1\n", frames);
cleanup:
    if (failure) fprintf(stderr, "VISIBLE_NATIVE_FAIL %s EGL=0x%x\n", failure, eglGetError());
    if (current) {
        if (program) glDeleteProgram(program);
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT) && !failure)
            failure = "release current EGL context";
    }
    if (surface != EGL_NO_SURFACE && !eglDestroySurface(display, surface) && !failure)
        failure = "destroy EGL surface";
    if (context != EGL_NO_CONTEXT && !eglDestroyContext(display, context) && !failure)
        failure = "destroy EGL context";
    if (initialized && !eglTerminate(display) && !failure) failure = "terminate EGL display";
    if (initialized && !eglReleaseThread() && !failure) failure = "release EGL thread";
    if (window) ANativeWindow_release(window);
    printf("VISIBLE_NATIVE_RELEASED frames=%d status=%s\n", frames, failure ? "failure" : "submitted");
    if (failure && !(*env)->ExceptionCheck(env)) {
        jclass exception = (*env)->FindClass(env, "java/lang/IllegalStateException");
        if (exception) (*env)->ThrowNew(env, exception, failure);
    }
    return failure ? -1 : frames;
#undef REQUIRE
}
