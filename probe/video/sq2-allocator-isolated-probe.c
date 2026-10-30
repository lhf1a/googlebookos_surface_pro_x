#define _GNU_SOURCE
#include <fcntl.h>
#include <grp.h>
#include <linux/capability.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

// Caller must verify the staged diagnostic ELF and launcher hashes first.
// The staged ELF has NO real service-registration import.
int main(void) {
    setbuf(stdout, NULL);
    alarm(15);
    if (getuid() != 0) return 2;
    if (unshare(CLONE_NEWNS) || mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)) {
        perror("private namespace"); return 3;
    }
    if (mount("/data/local/tmp/sq2-p010-gralloc/isolated-hw", "/vendor/bin/hw", NULL, MS_BIND, NULL) ||
        mount("/data/local/tmp/sq2-p010-gralloc", "/vendor/lib64/sq2-p010-allocator", NULL, MS_BIND, NULL)) {
        perror("private bind"); return 4;
    }
    gid_t group = 1026;
    if (prctl(PR_SET_KEEPCAPS, 1) || setgroups(1, &group) || setgid(1003) || setuid(1000)) {
        perror("identity"); return 5;
    }
    struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};
    struct __user_cap_data_struct caps[2] = {{0}};
    caps[0].effective = caps[0].permitted = caps[0].inheritable = 1U << CAP_SYS_NICE;
    if (syscall(SYS_capset, &header, caps) ||
        prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_SYS_NICE, 0, 0)) {
        perror("capabilities"); return 6;
    }
    puts("ISOLATED_LAUNCH uid1000/gid1003/group1026/SYS_NICE");
    execl("/vendor/bin/hw/android.hardware.graphics.allocator-service.minigbm",
          "/vendor/bin/hw/android.hardware.graphics.allocator-service.minigbm", NULL);
    perror("launcher exec"); return 7;
}
