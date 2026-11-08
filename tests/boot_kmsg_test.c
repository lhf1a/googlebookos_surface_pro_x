#define fstatvfs fake_fstatvfs
#define main recorder_main
#include "../probe/test2/boot-kmsg.c"
#undef main
#undef fstatvfs
#include <assert.h>
static unsigned long fragment = 4096;
static fsblkcnt_t available = MIN_FREE_BYTES / 4096;
static int stat_error;
int fake_fstatvfs(int fd, struct statvfs *s) {
    (void)fd;
    s->f_frsize = fragment;
    s->f_bavail = available;
    return stat_error;
}
int main(void) {
    assert(can_log(0, 0, 100));
    assert(can_log(0, MAX_LOG_BYTES - 100, 100));
    assert(!can_log(0, MAX_LOG_BYTES - 100, 101));
    assert(!can_log(0, UINT64_MAX, 1));
    available--; assert(!can_log(0, 0, 1)); available++;
    stat_error = -1; assert(!can_log(0, 0, 1)); stat_error = 0;
    fragment = 0; assert(!can_log(0, 0, 1)); fragment = 4096;
    int fd = open("/dev/full", O_WRONLY); assert(fd >= 0);
    assert(fullwrite(fd, "x", 1) == -1 && errno == ENOSPC); close(fd);
    return 0;
}
