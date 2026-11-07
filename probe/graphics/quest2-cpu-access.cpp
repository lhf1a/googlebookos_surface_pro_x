/* Private Qualcomm donor compatibility, not a replacement system mapper.
 * Only the old mapper3 DSO's explicit mapper path loads this library. Ordinary operations
 * forward to the real mapper5. GPU-only, verified linear RGB DMA-BUFs may use
 * real synchronized CPU access when mapper5 explicitly rejects CPU usage.
 * As in the platform mapper contract, callers must retain their imported
 * handle and must not race freeBuffer with another operation on that handle.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/dma-buf.h>
#include <linux/sync_file.h>
#include "quest2-mapper5.h"

namespace {
using Load = AIMapper_Error (*)(AIMapper **);
static AIMapper *real_mapper;
static AIMapper proxy;
static void *real_library;
static AIMapper_Error load_error = AIMAPPER_ERROR_UNSUPPORTED;
static pthread_once_t load_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
struct Access {
    buffer_handle_t handle;
    dev_t dev;
    ino_t ino;
    size_t size;
    int fd;
    void *base;
    uint64_t direction;
    bool busy;
    Access *next;
};
static Access *accesses;

static bool cpu_flags(uint64_t usage, uint64_t &direction, int &protection) {
    uint64_t r = usage & 0xf, w = usage & 0xf0;
    if ((usage & ~UINT64_C(0xff)) || (!r && !w) ||
        (r && r != 2 && r != 3) || (w && w != 0x20 && w != 0x30)) return false;
    direction = (r ? DMA_BUF_SYNC_READ : 0) | (w ? DMA_BUF_SYNC_WRITE : 0);
    protection = (r ? PROT_READ : 0) | (w ? PROT_WRITE : 0);
    return true;
}
static bool region_valid(const ARect &r, const sq2::BufferInfo &b) {
    if (!r.left && !r.top && !r.right && !r.bottom) return true;
    return r.left >= 0 && r.top >= 0 && r.right > r.left && r.bottom > r.top &&
           static_cast<uint64_t>(r.right) <= b.width && static_cast<uint64_t>(r.bottom) <= b.height;
}
static bool sync_dma(int fd, uint64_t flags) {
    dma_buf_sync sync{flags}; int result;
    do { result = ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync); } while (result < 0 && (errno == EINTR || errno == EAGAIN));
    return result == 0;
}
static bool wait_fence(int fd) {
    if (fd < 0) return true;
    sync_file_info information{};
    if (ioctl(fd, SYNC_IOC_FILE_INFO, &information) || information.status < 0) return false;
    pollfd p{fd, POLLIN, 0}; int result;
    do { result = poll(&p, 1, -1); } while (result < 0 && errno == EINTR);
    if (result != 1 || !(p.revents & POLLIN) || (p.revents & (POLLERR | POLLNVAL))) return false;
    information = {};
    return !ioctl(fd, SYNC_IOC_FILE_INFO, &information) && information.status > 0;
}
static void remove_access(Access *entry) {
    pthread_mutex_lock(&mutex);
    Access **p = &accesses;
    while (*p && *p != entry) p = &(*p)->next;
    if (*p) *p = entry->next;
    pthread_mutex_unlock(&mutex);
}
static AIMapper_Error private_free(buffer_handle_t handle) {
    pthread_mutex_lock(&mutex);
    bool active = false;
    for (Access *p = accesses; p; p = p->next) if (p->handle == handle) active = true;
    pthread_mutex_unlock(&mutex);
    if (active) {
        std::fprintf(stderr, "QTI_CPU_ACCESS free refused: active or unconfirmed CPU access\n");
        return AIMAPPER_ERROR_NO_RESOURCES;
    }
    return real_mapper->v5.freeBuffer(handle);
}
static AIMapper_Error private_unlock(buffer_handle_t handle, int *release_fence) {
    if (!release_fence) return AIMAPPER_ERROR_BAD_VALUE;
    *release_fence = -1;
    pthread_mutex_lock(&mutex);
    Access *entry = accesses;
    while (entry && entry->handle != handle) entry = entry->next;
    bool busy = entry && entry->busy;
    if (entry && !busy) entry->busy = true;
    pthread_mutex_unlock(&mutex);
    if (!entry) return real_mapper->v5.unlock(handle, release_fence);
    if (busy) return AIMAPPER_ERROR_NO_RESOURCES;
    // END failure leaves the import, FD and mapping alive. freeBuffer refuses
    // them. The old mapper3 forgets its slot on error, so no automatic retry or
    // recovery is promised: this may remain quarantined until process exit.
    if (!sync_dma(entry->fd, DMA_BUF_SYNC_END | entry->direction)) {
        std::fprintf(stderr, "QTI_CPU_ACCESS END failed errno=%d; mapping retained\n", errno);
        pthread_mutex_lock(&mutex); entry->busy = false; pthread_mutex_unlock(&mutex);
        return AIMAPPER_ERROR_NO_RESOURCES;
    }
    if (munmap(entry->base, entry->size)) {
        // END succeeded but lifetime cleanup failed: retain all ownership.
        std::fprintf(stderr, "QTI_CPU_ACCESS unmap failed errno=%d; import retained\n", errno);
        return AIMAPPER_ERROR_NO_RESOURCES;
    }
    close(entry->fd); remove_access(entry); std::free(entry);
    std::fprintf(stderr, "QTI_CPU_ACCESS END complete release_fence=-1\n");
    return AIMAPPER_ERROR_NONE;
}
static AIMapper_Error private_lock(buffer_handle_t handle, uint64_t usage, ARect region,
                                   int acquire_fence, void **out_data) {
    if (!out_data) { if (acquire_fence >= 0) close(acquire_fence); return AIMAPPER_ERROR_BAD_VALUE; }
    *out_data = nullptr;
    // Preserve our owned fence because the real mapper consumes its duplicate
    // on success AND failure. No borrowed HIDL descriptor is closed here.
    int forwarded_fence = -1;
    if (acquire_fence >= 0) {
        forwarded_fence = fcntl(acquire_fence, F_DUPFD_CLOEXEC, 0);
        if (forwarded_fence < 0) { close(acquire_fence); return AIMAPPER_ERROR_NO_RESOURCES; }
    }
    AIMapper_Error original_error = real_mapper->v5.lock(handle, usage, region, forwarded_fence, out_data);
    if (original_error != AIMAPPER_ERROR_BAD_VALUE) {
        if (acquire_fence >= 0) close(acquire_fence);
        return original_error;
    }
    *out_data = nullptr;
    uint64_t direction = 0; int protection = 0; sq2::BufferInfo b;
    bool eligible = cpu_flags(usage, direction, protection) &&
                    sq2::color_info(handle, b) && sq2::linear_rgba_layout(b) &&
                    !(b.usage & UINT64_C(0xff)) && (b.usage & UINT64_C(0x300)) && b.fd_size <= SIZE_MAX &&
                    b.plane_size % b.stride == 0 && region_valid(region, b);
    if (!eligible || !wait_fence(acquire_fence)) {
        if (acquire_fence >= 0) close(acquire_fence);
        return original_error;
    }
    if (acquire_fence >= 0) close(acquire_fence);
    struct stat original_stat{}, st{};
    if (fstat(b.fd, &original_stat)) return AIMAPPER_ERROR_NO_RESOURCES;
    int fd = fcntl(b.fd, F_DUPFD_CLOEXEC, 0);
    if (fd < 0 || fstat(fd, &st) || st.st_dev != original_stat.st_dev || st.st_ino != original_stat.st_ino ||
        st.st_size <= 0 || static_cast<uint64_t>(st.st_size) != b.fd_size) {
        if (fd >= 0) close(fd); return AIMAPPER_ERROR_NO_RESOURCES;
    }
    auto *entry = static_cast<Access *>(std::calloc(1, sizeof(Access)));
    if (!entry) { close(fd); return AIMAPPER_ERROR_NO_RESOURCES; }
    *entry = {handle, st.st_dev, st.st_ino, static_cast<size_t>(b.fd_size), fd, nullptr, direction, true, nullptr};
    // Reserve before potentially blocking operations. No registry lock is held
    // during a fence wait, mapper call, mmap or DMA-BUF synchronization.
    pthread_mutex_lock(&mutex);
    bool conflict = false;
    for (Access *p = accesses; p; p = p->next)
        if (p->handle == handle || (p->dev == st.st_dev && p->ino == st.st_ino)) conflict = true;
    if (!conflict) { entry->next = accesses; accesses = entry; }
    pthread_mutex_unlock(&mutex);
    if (conflict) { close(fd); std::free(entry); return AIMAPPER_ERROR_NO_RESOURCES; }
    void *base = mmap(nullptr, entry->size, protection, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED || !sync_dma(fd, DMA_BUF_SYNC_START | direction)) {
        if (base != MAP_FAILED) munmap(base, entry->size);
        close(fd); remove_access(entry); std::free(entry);
        return AIMAPPER_ERROR_NO_RESOURCES;
    }
    pthread_mutex_lock(&mutex); entry->base = base; entry->busy = false; pthread_mutex_unlock(&mutex);
    *out_data = static_cast<char *>(base) + b.offset;
    std::fprintf(stderr, "QTI_CPU_ACCESS START actual_usage=0x%llx requested=0x%llx stride=%llu bytes=%llu\n",
                 static_cast<unsigned long long>(b.usage), static_cast<unsigned long long>(usage),
                 static_cast<unsigned long long>(b.stride), static_cast<unsigned long long>(b.fd_size));
    return AIMAPPER_ERROR_NONE;
}
static void initialize() {
    real_library = dlopen("/vendor/lib64/hw/mapper.minigbm.so", RTLD_NOW | RTLD_LOCAL);
    if (!real_library) return;
    auto real_load = reinterpret_cast<Load>(dlsym(real_library, "AIMapper_loadIMapper"));
    if (!real_load) return;
    load_error = real_load(&real_mapper);
    if (load_error != AIMAPPER_ERROR_NONE || !real_mapper || real_mapper->version < 5) {
        real_mapper = nullptr; load_error = AIMAPPER_ERROR_UNSUPPORTED; return;
    }
    // Never copy unknown future fields or mutate the platform table.
    proxy.version = static_cast<AIMapper_Version>(5);
    proxy.v5 = real_mapper->v5;
    proxy.v5.lock = private_lock; proxy.v5.unlock = private_unlock; proxy.v5.freeBuffer = private_free;
}
static AIMapper_Error private_load(AIMapper **out) {
    if (!out) return AIMAPPER_ERROR_BAD_VALUE;
    pthread_once(&load_once, initialize);
    *out = load_error == AIMAPPER_ERROR_NONE ? &proxy : nullptr;
    return load_error;
}
}

extern "C" __attribute__((visibility("default")))
AIMapper_Error AIMapper_loadIMapper(AIMapper **out) {
    return private_load(out);
}
