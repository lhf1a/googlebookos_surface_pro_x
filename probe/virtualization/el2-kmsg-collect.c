/* SPDX-License-Identifier: MIT */
/* Foreground, bounded lab collector; caller backgrounds it before Android init. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
static int64_t now_ms(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return -1;
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    int k = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (k < 0) { perror("kmsg"); return 3; }
    int f = open(argv[1], O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (f < 0) { perror("log output"); close(k); return 3; }
    int64_t start = now_ms();
    if (start < 0) { close(k); close(f); return 4; }
    size_t left = 8u * 1024 * 1024;
    char buf[16384];
    int rc = 0;
    while (left) {
        int64_t elapsed = now_ms() - start;
        if (elapsed < 0 || elapsed >= 180000) break;
        struct pollfd p = {.fd = k, .events = POLLIN};
        int ready = poll(&p, 1, (int)(180000 - elapsed));
        if (ready < 0) { if (errno == EINTR) continue; rc = 5; break; }
        if (!ready) break;
        ssize_t n = read(k, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EPIPE) continue;
            rc = 6; break;
        }
        if (!n) break;
        size_t count = (size_t)n < left ? (size_t)n : left, off = 0;
        while (off < count) {
            ssize_t w = write(f, buf + off, count - off);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) { rc = 7; goto done; }
            off += (size_t)w;
        }
        left -= count;
    }
done:
    if (fsync(f) && !rc) rc = 8;
    close(f); close(k); return rc;
}
