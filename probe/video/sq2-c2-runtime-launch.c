#define _GNU_SOURCE
#include <sched.h>
#include <sys/mount.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Versioned private runtime; never bind the mapper in the caller's namespace.
#define BASE "/data/vendor/sq2-codec2/v1/"
int main(int argc, char **argv) {
    if (argc != 2 || (strcmp(argv[1], "--serve") && strcmp(argv[1], "--names-test"))) return 2;
    if (unshare(CLONE_NEWNS) || mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)) {
        perror("private namespace"); return 3;
    }
    if (mount(BASE "libminigbm_gralloc.so", "/vendor/lib64/libminigbm_gralloc.so", NULL, MS_BIND, NULL)) {
        perror("private mapper"); return 4;
    }
    setenv("LD_LIBRARY_PATH", "/system/lib64:/apex/com.android.media.swcodec/lib64:/vendor/lib64", 1);
    setenv("SQ2_VIDEO_MAP_CACHE", "1", 1);
    unsetenv("SQ2_P010_DIRECT");
    unsetenv("LD_PRELOAD");
    execl(BASE "service", "sq2-c2-service", argv[1], NULL);
    perror("runtime service"); return 5;
}
