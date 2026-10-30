/* Used only by a private ELF whose registration import was renamed.
 * Never call or forward to the real service manager. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int SQ2_allocator_probe_registration(void *binder, const char *name, int flags) {
    const int ok = binder && name &&
        !strcmp(name, "android.hardware.graphics.allocator.IAllocator/default");
    dprintf(STDOUT_FILENO, "REGISTRATION_INTERCEPTED valid=%d flags=%d uid=%u\n",
            ok, flags, getuid());
    _exit(ok ? 0 : 7);
}
