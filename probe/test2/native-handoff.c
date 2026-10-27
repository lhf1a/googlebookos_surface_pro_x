/* Real PID 1 handoff. Keep the initramfs pinned for diagnostic processes;
 * no PID, mount or network namespace is created. */
#define _GNU_SOURCE
#include <sys/mount.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    if (argc != 2 || getpid() != 1) { fprintf(stderr, "native-handoff requires PID 1 and a mounted root\n"); return 2; }
    if (chdir(argv[1]) || mount(".", "/", NULL, MS_MOVE, NULL) || chroot(".") || chdir("/")) { perror("root handoff"); return 3; }
    setenv("PATH", "/system/bin:/system/xbin", 1);
    setenv("INIT_STARTED_AT", "0", 1);
    setenv("INIT_SELINUX_STARTED_AT", "0", 1);
    setenv("INIT_AVB_VERSION", "1.3", 1);
    execl("/system/bin/init", "/system/bin/init", "selinux_setup", NULL);
    perror("original Android init"); return 4;
}
