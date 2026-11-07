/* Run a fresh native probe against full Qualcomm libraries in this process's
 * private mount namespace only. No SurfaceFlinger/app restart or disk writes.
 * The caller must verify the runtime SHA256SUMS before invoking this helper.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *libraries[] = {
    "libadreno_utils.so", "libgsl.so", "libq2v77glnext.so", "libq2v77qgl.so",
    "libqti-mapper3-bridge.so", "libquest2-ion-dmaheap.so", "libquest2-v77-ahb.so",
    "libquest2-vulkan-wrapper.so", "vulkan-v77-bridge.so", "libEGL_adreno.so",
    "libGLESv1_CM_adreno.so", "libGLESv2_adreno.so", "eglSubDriverAndroid.so",
    "libquest2-egl-wrapper.so"
};
static int copy_file(int source, const char *name, const char *target) {
    int in = openat(source, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    struct stat st;
    if (in < 0 || fstat(in, &st) || !S_ISREG(st.st_mode)) {
        if (in >= 0) close(in);
        return -1;
    }
    int out = open(target, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (out < 0) { close(in); return -1; }
    char buffer[65536]; ssize_t count; off_t total = 0; int result = 0;
    while ((count = read(in, buffer, sizeof(buffer))) != 0) {
        if (count < 0) { if (errno == EINTR) continue; result = -1; break; }
        ssize_t done = 0;
        while (done < count) {
            ssize_t written = write(out, buffer + done, (size_t)(count - done));
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { result = -1; break; }
            done += written;
        }
        if (result) break;
        total += count;
    }
    if (total != st.st_size) result = -1;
    if (close(out)) result = -1;
    close(in); return result;
}
int main(int argc, char **argv) {
    if (argc < 3 || argv[1][0] != '/' || argv[2][0] != '/' || geteuid() != 0) {
        fprintf(stderr, "usage: qualcomm-full-probe-namespace ABS_RUNTIME ABS_PROBE [args...]\n");
        return 2;
    }
    if ((getenv("LD_PRELOAD") && *getenv("LD_PRELOAD")) ||
        (getenv("LD_LIBRARY_PATH") && *getenv("LD_LIBRARY_PATH"))) return 2;
    int source = open(argv[1], O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (source < 0) { perror("runtime directory"); return 1; }
    struct stat before, after, config_dir;
    /* The v8 GSL reads this renamed path, not /data/vendor/gpu. Refuse a
     * missing mountpoint; creating device directories belongs to the caller's
     * explicit staging operation, never to this private namespace helper. */
    if (lstat("/data/sq2vk-v77t", &config_dir) || !S_ISDIR(config_dir.st_mode)) {
        fprintf(stderr, "PRIVATE_CONFIG_MOUNTPOINT_MISSING /data/sq2vk-v77t\n");
        close(source); return 1;
    }
    if (stat("/proc/self/ns/mnt", &before) || unshare(CLONE_NEWNS) ||
        mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) ||
        stat("/proc/self/ns/mnt", &after) || before.st_ino == after.st_ino ||
        mount("tmpfs", "/vendor/lib64/egl", "tmpfs", MS_NOSUID | MS_NODEV, "mode=755") ||
        mount("tmpfs", "/data/vendor", "tmpfs", MS_NOSUID | MS_NODEV, "mode=755") ||
        mount("tmpfs", "/data/sq2vk-v77t", "tmpfs", MS_NOSUID | MS_NODEV, "mode=755")) {
        perror("private graphics namespace"); close(source); return 1;
    }
    for (unsigned i = 0; i < sizeof(libraries) / sizeof(libraries[0]); ++i) {
        char target[256]; snprintf(target, sizeof(target), "/vendor/lib64/egl/%s", libraries[i]);
        if (copy_file(source, libraries[i], target)) { perror(libraries[i]); close(source); return 1; }
    }
    /* The CPU-access candidate adds one explicitly named private mapper shim.
     * Original 14-library runtimes remain valid; callers verify each supplied
     * runtime's manifest before invoking this helper. Never follow a symlink. */
    struct stat shim;
    unsigned library_count = 14;
    if (!fstatat(source, "libsq2map.so", &shim, AT_SYMLINK_NOFOLLOW)) {
        if (!S_ISREG(shim.st_mode) ||
            copy_file(source, "libsq2map.so", "/vendor/lib64/egl/libsq2map.so")) {
            fprintf(stderr, "invalid private mapper shim\n"); close(source); return 1;
        }
        library_count++;
    } else if (errno != ENOENT) {
        perror("private mapper shim inspection"); close(source); return 1;
    }
    if (mkdir("/data/vendor/gpu", 0700) ||
        copy_file(source, "qgl_config.txt", "/data/sq2vk-v77t/qgl_config.txt") ||
        symlink("libquest2-egl-wrapper.so", "/vendor/lib64/egl/libEGL_freedreno.so") ||
        symlink("libGLESv1_CM_adreno.so", "/vendor/lib64/egl/libGLESv1_CM_freedreno.so") ||
        symlink("libGLESv2_adreno.so", "/vendor/lib64/egl/libGLESv2_freedreno.so") ||
        mount("/vendor/lib64/egl/libquest2-vulkan-wrapper.so",
              "/vendor/lib64/hw/vulkan.freedreno.so", NULL, MS_BIND, NULL)) {
        perror("private graphics files"); close(source); return 1;
    }
    close(source);
    setenv("XDG_CACHE_HOME", "/data/vendor/gpu", 1);
    setenv("HOME", "/data/vendor/gpu", 1);
    printf("QUALCOMM_PROBE_PRIVATE_NAMESPACE prior=%llu current=%llu libraries=%u cache=fresh-private-tmpfs qgl_config=/data/sq2vk-v77t/qgl_config.txt global_driver_changed=0\n",
           (unsigned long long)before.st_ino, (unsigned long long)after.st_ino, library_count);
    fflush(stdout);
    execv(argv[2], argv + 2);
    perror("probe exec"); return 1;
}
