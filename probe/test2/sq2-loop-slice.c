/* Read-only, size-bounded slices of the verified factory super partition. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/fs.h>
#include <linux/loop.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void die(const char *s) { perror(s); exit(1); }
static uint64_t number(const char *s) {
    char *end; errno = 0; unsigned long long n = strtoull(s, &end, 10);
    if (errno || !*s || *end || *s == '-') { fprintf(stderr, "Invalid number\n"); exit(2); }
    return n;
}
int main(int argc, char **argv) {
    if (argc != 4) return 2;
    uint64_t offset = number(argv[2]), length = number(argv[3]), size;
    int backing = open(argv[1], O_RDONLY | O_CLOEXEC);
    if (backing < 0 || ioctl(backing, BLKGETSIZE64, &size)) die("backing block device");
    if (!length || offset % 512 || length % 512 || offset > size || length > size-offset) return 2;
    int ctl = open("/dev/loop-control", O_RDWR | O_CLOEXEC);
    if (ctl < 0) die("loop-control");
    int num = ioctl(ctl, LOOP_CTL_GET_FREE);
    if (num < 0) die("free loop");
    char path[64]; snprintf(path, sizeof(path), "/dev/loop%d", num);
    int loop = open(path, O_RDWR | O_CLOEXEC);
    if (loop < 0) die("open loop");
    struct loop_config cfg = {0};
    cfg.fd = backing;
    cfg.info.lo_offset = offset;
    cfg.info.lo_sizelimit = length;
    cfg.info.lo_flags = LO_FLAGS_READ_ONLY;
    if (ioctl(loop, LOOP_CONFIGURE, &cfg)) die("configure read-only slice");
    close(loop); close(ctl); close(backing);
    puts(path);
    return 0;
}
