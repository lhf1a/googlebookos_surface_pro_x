/* Open endpoints before init handoff; bound persistent diagnostic logging. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <stdint.h>
#define MAX_LOG_BYTES (8ULL * 1024 * 1024)
#define MIN_FREE_BYTES (32ULL * 1024 * 1024)
static int can_log(int fd, uint64_t used, size_t incoming) {
    struct statvfs space;
    if (used > MAX_LOG_BYTES || incoming > MAX_LOG_BYTES - used) return 0;
    if (fstatvfs(fd, &space) || !space.f_frsize) return 0;
    uint64_t reserve = MIN_FREE_BYTES / space.f_frsize + !!(MIN_FREE_BYTES % space.f_frsize);
    return space.f_bavail >= reserve;
}
static int fullwrite(int fd, const char *b, size_t n) {
    while (n) {
        ssize_t r = write(fd, b, n);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (!r) { errno = EIO; return -1; }
        b += r; n -= r;
    }
    return 0;
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    int k = open("/dev/kmsg", O_RDONLY), f = open(argv[1], O_WRONLY|O_CREAT|O_APPEND|O_DSYNC, 0600);
    if (k < 0 || f < 0) { perror("boot-kmsg open"); return 3; }
    struct stat st;
    if (fstat(f, &st) || st.st_size < 0 || !can_log(f, st.st_size, 0)) {
        fputs("boot-kmsg: persistent logging unavailable; keep boot logs in RAM\n", stderr);
        close(k); close(f); return 7;
    }
    pid_t p = fork(); if (p < 0) return 4; if (p > 0) return 0;
    setsid(); signal(SIGHUP, SIG_IGN); signal(SIGTERM, SIG_IGN);
    uint64_t used = st.st_size;
    char b[16384];
    for (;;) {
        ssize_t n = read(k, b, sizeof b);
        if (n > 0) {
            if (!can_log(f, used, n)) { close(k); close(f); return 0; }
            if (fullwrite(f, b, n)) return 5;
            used += n;
        } else if (n < 0 && errno != EINTR && errno != EPIPE) {
            perror("boot-kmsg read"); return 6;
        }
    }
}
