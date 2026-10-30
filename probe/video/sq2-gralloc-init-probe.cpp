// Exercise the donor's public initialization entry without registering Binder services.
#include <cstdio>
#include <dlfcn.h>
#include <memory>
#include <cstring>
#include <grp.h>
#include <unistd.h>

class cros_gralloc_driver;
int main(int argc, char **argv) {
    if (argc != 2 && !(argc == 3 && !strcmp(argv[2], "--system"))) return 2;
    setbuf(stdout, nullptr);
    alarm(15);
    if (argc == 3) {
        // Match the observed allocator IDs. No capabilities are retained.
        const gid_t groups[] = {1026};
        if (setgroups(1, groups) || setgid(1003) || setuid(1000)) {
            perror("drop allocator IDs"); return 6;
        }
    }
    printf("PROBE_UID=%u GID=%u\n", getuid(), getgid());
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { puts(dlerror()); return 3; }
    using GetInstance = std::shared_ptr<cros_gralloc_driver> (*)();
    auto get = reinterpret_cast<GetInstance>(dlsym(lib, "_ZN19cros_gralloc_driver12get_instanceEv"));
    if (!get) { puts(dlerror()); dlclose(lib); return 4; }
    puts("GRALLOC_INIT_BEGIN");
    auto driver = get();
    const bool ok = static_cast<bool>(driver);
    printf("GRALLOC_INIT_OK=%d\n", ok);
    driver.reset();
    // The donor owns a process-lifetime singleton; retain its DSO until exit.
    puts("CLIENT_REFERENCE_RELEASED");
    return ok ? 0 : 5;
}
