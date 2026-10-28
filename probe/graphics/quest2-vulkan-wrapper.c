/* Private v77 Vulkan HAL/AHB import adapter. No dispatchable handle rewriting.
 * Only run with the audited donor in a private vendor mount namespace.
 * Per-device PFNs come from that device's real GDPA, obtained from its owning
 * instance. Native Android WSI uses bounded, privately owned handle views.
 * Vulkan's normal external synchronization of instance/device destruction is
 * required; this table lock is not a substitute for application synchronization.
 */
#define _GNU_SOURCE
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <hardware/hwvulkan.h>
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/uio.h>
#include "quest2-v77-native.h"
#ifdef SQ2_VK_APP_DIAGNOSTICS
#include <android/log.h>
#include <stdarg.h>
#include <stdatomic.h>
static void app_diag(const char *format, ...) {
    static atomic_uint messages;
    if (atomic_fetch_add_explicit(&messages, 1, memory_order_relaxed) >= 96) return;
    va_list args; va_start(args,format);
    __android_log_vprint(ANDROID_LOG_INFO,"SQ2VkApp",format,args);
    va_end(args);
}
#else
#define app_diag(...) ((void)0)
#endif
#ifdef SQ2_VK_PIPELINE_DIAGNOSTICS
#include "quest2-vk-create-trace.h"
#endif

#define EXPORT __attribute__((visibility("default")))
#define DONOR "/vendor/lib64/egl/vulkan-v77-bridge.so"
#define MAX_INSTANCES 128
#define MAX_PHYSICAL 512
#define MAX_DEVICES 128
#define MAX_CHAIN 64
#define MAX_NATIVE_BATCH 256
/* Android's private native-buffer header is not part of the NDK. These are
 * extension 11's published structure IDs; the fixed v77 node is checked below.
 * Spec v8 permits NativeBufferANDROID in BindImageMemoryInfo's pNext as well. */
#define SQ2_NATIVE_BUFFER_ANDROID ((VkStructureType)1000010000)
#define SQ2_SWAPCHAIN_IMAGE_CREATE_ANDROID ((VkStructureType)1000010001)

extern int sq2_v77_ahb_prepare(const AHardwareBuffer *);
extern void sq2_v77_ahb_finish(const AHardwareBuffer *);
extern unsigned sq2_v77_ahb_stats(unsigned *, unsigned *, uint64_t *);

struct Hal { hwvulkan_device_t exposed; hwvulkan_device_t *real; };
struct Instance {
    unsigned state; /* 0 free, 1 reserved, 2 live; reserve before donor create. */
    VkInstance handle;
    PFN_vkGetInstanceProcAddr gipa;
    PFN_vkGetDeviceProcAddr gdpa;
    PFN_vkDestroyInstance destroy;
    PFN_vkEnumeratePhysicalDevices enumerate;
    PFN_vkEnumeratePhysicalDeviceGroups groups, groups_khr;
    PFN_vkCreateDevice create_device;
    PFN_vkDestroyDevice destroy_device;
};
struct Physical { VkPhysicalDevice handle; VkInstance instance; };
struct Device {
    unsigned state;
    VkDevice handle;
    VkInstance instance;
    PFN_vkGetDeviceProcAddr gdpa;
    PFN_vkDestroyDevice destroy;
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID properties;
    PFN_vkAllocateMemory allocate;
    PFN_vkFreeMemory free_memory;
    PFN_vkCreateImage create_image;
    PFN_vkDestroyImage destroy_image;
    PFN_vkBindImageMemory bind_image;
    PFN_vkBindImageMemory2 bind_image2, bind_image2_khr;
};
/* The current Android loader supplies 72 bytes (usage3 and AHB at +56/+64).
 * v77 shallow-copies only its compatible 56-byte prefix. Preserve the complete
 * caller node, including the newer fields, without modifying its tail chain. */
struct NativeNode {
    VkStructureType sType;
    const void *pNext;
    const void *handle;
    int32_t stride, format, usage;
    uint64_t consumer, producer;
    uint64_t usage3;
    const AHardwareBuffer *ahb;
};
_Static_assert(sizeof(void *) == 8 && sizeof(struct NativeNode) == 72, "current arm64 native ABI");
_Static_assert(offsetof(struct NativeNode, handle) == 16 &&
               offsetof(struct NativeNode, stride) == 24 &&
               offsetof(struct NativeNode, format) == 28 &&
               offsetof(struct NativeNode, consumer) == 40 &&
               offsetof(struct NativeNode, usage3) == 56 &&
               offsetof(struct NativeNode, ahb) == 64, "compatible native node offsets");
struct Image {
    struct Image *next;
    VkDevice device;
    VkImage handle;
    uint32_t width, height;
    unsigned eligible, binding, bound, native_attempt, native_succeeded;
    const void *view;
    void *lease;
};
static struct Image *images, *quarantined_images;
static unsigned native_failures;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static void *library; /* Kept loaded through process exit. */
static hw_module_t *donor_module;
static hwvulkan_device_t *persistent_device; /* v77 has no close callback. */
static struct Hal *hal;
static unsigned opening, hal_calls;
static struct Instance instances[MAX_INSTANCES];
static struct Physical physicals[MAX_PHYSICAL];
static struct Device devices[MAX_DEVICES];

EXPORT unsigned sq2_v77_wrapper_native_failures(void) {
    pthread_mutex_lock(&mutex);
    unsigned result = native_failures;
    pthread_mutex_unlock(&mutex);
    return result;
}

EXPORT unsigned sq2_v77_wrapper_stats(unsigned *leases, unsigned *owners, uint64_t *bytes) {
    return sq2_v77_ahb_stats(leases, owners, bytes);
}
static void stats(const char *operation, VkResult result) {
    if (result != VK_SUCCESS) app_diag("operation=%s result=%d",operation,(int)result);
    unsigned leases = 0, owners = 0;
    uint64_t bytes = 0;
    unsigned views = sq2_v77_ahb_stats(&leases, &owners, &bytes);
    uint64_t native_bytes = 0;
    unsigned native_views = sq2_v77_native_stats(&native_bytes);
    fprintf(stderr, "V77_AHB_WRAPPER operation=%s result=%d views=%u leases=%u owners=%u bytes=%llu native_views=%u native_bytes=%llu native_failures=%u\n",
            operation, (int)result, views, leases, owners, (unsigned long long)bytes,
            native_views, (unsigned long long)native_bytes, sq2_v77_wrapper_native_failures());
}
static VkResult prepare_error(int error) {
    return error == -ENOMEM || error == -ENOSPC ? VK_ERROR_OUT_OF_HOST_MEMORY :
                                                VK_ERROR_INVALID_EXTERNAL_HANDLE;
}
static hwvulkan_device_t *begin_hal(void) {
    pthread_mutex_lock(&mutex);
    hwvulkan_device_t *real = hal ? hal->real : NULL;
    if (real) ++hal_calls;
    pthread_mutex_unlock(&mutex);
    return real;
}
static void end_hal(void) {
    pthread_mutex_lock(&mutex); --hal_calls; pthread_mutex_unlock(&mutex);
}
static int instance_copy(VkInstance handle, struct Instance *out) {
    int found = 0;
    pthread_mutex_lock(&mutex);
    for (unsigned i = 0; i < MAX_INSTANCES; ++i)
        if (instances[i].state == 2 && instances[i].handle == handle) {
            *out = instances[i]; found = 1; break;
        }
    pthread_mutex_unlock(&mutex);
    return found;
}
static int device_copy(VkDevice handle, struct Device *out) {
    int found = 0;
    pthread_mutex_lock(&mutex);
    for (unsigned i = 0; i < MAX_DEVICES; ++i)
        if (devices[i].state == 2 && devices[i].handle == handle) {
            *out = devices[i]; found = 1; break;
        }
    pthread_mutex_unlock(&mutex);
    return found;
}
static VkResult remember_physical(VkInstance owner, uint32_t count, const VkPhysicalDevice *values) {
    VkResult result = VK_SUCCESS;
    pthread_mutex_lock(&mutex);
    unsigned free_count = 0, needed = 0;
    for (unsigned i = 0; i < MAX_PHYSICAL; ++i) free_count += !physicals[i].handle;
    for (uint32_t n = 0; n < count; ++n) {
        if (!values[n]) { result = VK_ERROR_INITIALIZATION_FAILED; goto done; }
        unsigned found = 0;
        for (unsigned i = 0; i < MAX_PHYSICAL; ++i) if (physicals[i].handle == values[n]) {
            if (physicals[i].instance != owner) { result = VK_ERROR_INITIALIZATION_FAILED; goto done; }
            found = 1; break;
        }
        for (uint32_t j = 0; j < n; ++j) if (values[j] == values[n]) found = 1;
        needed += !found;
    }
    if (needed > free_count) { result = VK_ERROR_OUT_OF_HOST_MEMORY; goto done; }
    for (uint32_t n = 0; n < count; ++n) {
        unsigned i, empty = MAX_PHYSICAL;
        for (i = 0; i < MAX_PHYSICAL; ++i) {
            if (physicals[i].handle == values[n]) break;
            if (!physicals[i].handle && empty == MAX_PHYSICAL) empty = i;
        }
        if (i == MAX_PHYSICAL) physicals[empty] = (struct Physical){values[n], owner};
    }
done:
    pthread_mutex_unlock(&mutex);
    return result;
}

static VKAPI_ATTR VkResult VKAPI_CALL wrapped_create_instance(
        const VkInstanceCreateInfo *info, const VkAllocationCallbacks *allocator, VkInstance *out) {
    if (!info || !out) {
        fprintf(stderr, "V77_WRAPPER_INIT stage=create-instance-invalid-arguments info=%d out=%d\n",
                !!info, !!out);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *out = VK_NULL_HANDLE;
    hwvulkan_device_t *real = begin_hal();
    if (!real) {
        fprintf(stderr, "V77_WRAPPER_INIT stage=create-instance-no-open-hal\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    unsigned slot;
    pthread_mutex_lock(&mutex);
    for (slot = 0; slot < MAX_INSTANCES && instances[slot].state; ++slot) {}
    if (slot < MAX_INSTANCES) instances[slot].state = 1;
    pthread_mutex_unlock(&mutex);
    if (slot == MAX_INSTANCES) {
        fprintf(stderr, "V77_WRAPPER_INIT stage=create-instance-table-full\n");
        end_hal(); return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    VkInstance value = VK_NULL_HANDLE;
    fprintf(stderr, "V77_WRAPPER_INIT stage=donor-create-instance-begin slot=%u api=%u extensions=%u layers=%u\n",
            slot, info->pApplicationInfo ? info->pApplicationInfo->apiVersion : 0,
            info->enabledExtensionCount, info->enabledLayerCount);
    VkResult result = real->CreateInstance(info, allocator, &value);
    fprintf(stderr, "V77_WRAPPER_INIT stage=donor-create-instance-end slot=%u result=%d instance=%d\n",
            slot, (int)result, value != VK_NULL_HANDLE);
    struct Instance saved = {0};
    if (result == VK_SUCCESS) {
        saved.state = 2; saved.handle = value; saved.gipa = real->GetInstanceProcAddr;
#define IP(field, type, name) saved.field = (type)saved.gipa(value, name)
        IP(destroy, PFN_vkDestroyInstance, "vkDestroyInstance");
        IP(gdpa, PFN_vkGetDeviceProcAddr, "vkGetDeviceProcAddr");
        IP(enumerate, PFN_vkEnumeratePhysicalDevices, "vkEnumeratePhysicalDevices");
        IP(groups, PFN_vkEnumeratePhysicalDeviceGroups, "vkEnumeratePhysicalDeviceGroups");
        IP(groups_khr, PFN_vkEnumeratePhysicalDeviceGroups, "vkEnumeratePhysicalDeviceGroupsKHR");
        IP(create_device, PFN_vkCreateDevice, "vkCreateDevice");
        IP(destroy_device, PFN_vkDestroyDevice, "vkDestroyDevice");
#undef IP
        fprintf(stderr, "V77_WRAPPER_INIT stage=instance-dispatch slot=%u destroy=%d gdpa=%d enumerate=%d groups=%d groups_khr=%d create_device=%d destroy_device=%d\n",
                slot, !!saved.destroy, !!saved.gdpa, !!saved.enumerate, !!saved.groups,
                !!saved.groups_khr, !!saved.create_device, !!saved.destroy_device);
        if (!value || !saved.destroy) {
            /* A successful ICD create without a valid destroy function breaks
             * the HAL contract. Do not drop an uncollectable live instance. */
            fprintf(stderr, "V77_WRAPPER_FATAL invalid instance dispatch\n"); abort();
        }
        if (!saved.gdpa || !saved.enumerate || !saved.create_device || !saved.destroy_device) {
            fprintf(stderr, "V77_WRAPPER_INIT stage=instance-dispatch-rejected slot=%u cleanup=destroy-instance\n", slot);
            saved.destroy(value, allocator); saved = (struct Instance){0};
            result = VK_ERROR_INITIALIZATION_FAILED;
        } else *out = value;
    }
    pthread_mutex_lock(&mutex); instances[slot] = saved; pthread_mutex_unlock(&mutex);
    end_hal();
    fprintf(stderr, "V77_WRAPPER_INIT stage=create-instance-return slot=%u result=%d instance=%d\n",
            slot, (int)result, *out != VK_NULL_HANDLE);
    return result;
}
static VKAPI_ATTR void VKAPI_CALL wrapped_destroy_instance(VkInstance value, const VkAllocationCallbacks *allocator) {
    if (!value) return;
    struct Instance saved;
    if (!instance_copy(value, &saved)) return;
    saved.destroy(value, allocator);
    pthread_mutex_lock(&mutex);
    for (unsigned i = 0; i < MAX_PHYSICAL; ++i)
        if (physicals[i].instance == value) physicals[i] = (struct Physical){0};
    for (unsigned i = 0; i < MAX_INSTANCES; ++i)
        if (instances[i].handle == value) instances[i] = (struct Instance){0};
    pthread_mutex_unlock(&mutex);
}
static VKAPI_ATTR VkResult VKAPI_CALL wrapped_enumerate_physical(VkInstance value, uint32_t *count, VkPhysicalDevice *out) {
    struct Instance saved;
    if (!count || !instance_copy(value, &saved)) return VK_ERROR_INITIALIZATION_FAILED;
    uint32_t capacity = out ? *count : 0;
    VkResult result = saved.enumerate(value, count, out);
    if (out && (result == VK_SUCCESS || result == VK_INCOMPLETE)) {
        if (*count > capacity || *count > MAX_PHYSICAL) return VK_ERROR_OUT_OF_HOST_MEMORY;
        VkResult tracked = remember_physical(value, *count, out);
        if (tracked != VK_SUCCESS) return tracked;
    }
    return result;
}
static VkResult enumerate_groups(VkInstance value, uint32_t *count, VkPhysicalDeviceGroupProperties *out, int khr) {
    struct Instance saved;
    if (!count || !instance_copy(value, &saved)) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkEnumeratePhysicalDeviceGroups fn = khr ? saved.groups_khr : saved.groups;
    if (!fn) return VK_ERROR_EXTENSION_NOT_PRESENT;
    uint32_t capacity = out ? *count : 0;
    VkResult result = fn(value, count, out);
    if (out && (result == VK_SUCCESS || result == VK_INCOMPLETE)) {
        if (*count > capacity || *count > MAX_PHYSICAL) return VK_ERROR_OUT_OF_HOST_MEMORY;
        VkPhysicalDevice values[MAX_PHYSICAL]; unsigned used = 0;
        for (uint32_t i = 0; i < *count; ++i) {
            if (out[i].physicalDeviceCount > VK_MAX_DEVICE_GROUP_SIZE ||
                out[i].physicalDeviceCount > MAX_PHYSICAL - used) return VK_ERROR_OUT_OF_HOST_MEMORY;
            for (uint32_t j = 0; j < out[i].physicalDeviceCount; ++j) values[used++] = out[i].physicalDevices[j];
        }
        VkResult tracked = remember_physical(value, used, values);
        if (tracked != VK_SUCCESS) return tracked;
    }
    return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL wrapped_groups(VkInstance i, uint32_t *n, VkPhysicalDeviceGroupProperties *p) {
    return enumerate_groups(i, n, p, 0);
}
static VKAPI_ATTR VkResult VKAPI_CALL wrapped_groups_khr(VkInstance i, uint32_t *n, VkPhysicalDeviceGroupProperties *p) {
    return enumerate_groups(i, n, p, 1);
}
static VKAPI_ATTR VkResult VKAPI_CALL wrapped_create_device(VkPhysicalDevice physical,
        const VkDeviceCreateInfo *info, const VkAllocationCallbacks *allocator, VkDevice *out) {
    if (!info || !out) return VK_ERROR_INITIALIZATION_FAILED;
#ifdef SQ2_VK_PIPELINE_DIAGNOSTICS
    static atomic_uint traces;
    if (atomic_fetch_add(&traces,1) < 2) sq2_trace_create_device(info,allocator);
#endif
    *out = VK_NULL_HANDLE;
    struct Instance parent = {0}; unsigned slot;
    pthread_mutex_lock(&mutex);
    for (unsigned p = 0; p < MAX_PHYSICAL; ++p) if (physicals[p].handle == physical && physical) {
        for (unsigned i = 0; i < MAX_INSTANCES; ++i)
            if (instances[i].state == 2 && instances[i].handle == physicals[p].instance) parent = instances[i];
        break;
    }
    for (slot = 0; slot < MAX_DEVICES && devices[slot].state; ++slot) {}
    if (parent.state && slot < MAX_DEVICES) devices[slot].state = 1;
    pthread_mutex_unlock(&mutex);
    if (!parent.state) return VK_ERROR_INITIALIZATION_FAILED;
    if (slot == MAX_DEVICES) return VK_ERROR_OUT_OF_HOST_MEMORY;
    VkDevice value = VK_NULL_HANDLE;
    VkResult result = parent.create_device(physical, info, allocator, &value);
    struct Device saved = {0};
    if (result == VK_SUCCESS) {
        saved.state = 2; saved.handle = value; saved.instance = parent.handle; saved.gdpa = parent.gdpa;
#define DP(field, type, name) saved.field = (type)saved.gdpa(value, name)
        DP(destroy, PFN_vkDestroyDevice, "vkDestroyDevice");
        DP(properties, PFN_vkGetAndroidHardwareBufferPropertiesANDROID, "vkGetAndroidHardwareBufferPropertiesANDROID");
        DP(allocate, PFN_vkAllocateMemory, "vkAllocateMemory");
        DP(free_memory, PFN_vkFreeMemory, "vkFreeMemory");
        DP(create_image, PFN_vkCreateImage, "vkCreateImage");
        DP(destroy_image, PFN_vkDestroyImage, "vkDestroyImage");
        DP(bind_image, PFN_vkBindImageMemory, "vkBindImageMemory");
        DP(bind_image2, PFN_vkBindImageMemory2, "vkBindImageMemory2");
        DP(bind_image2_khr, PFN_vkBindImageMemory2, "vkBindImageMemory2KHR");
#undef DP
        if (!value || !saved.destroy || !saved.allocate || !saved.free_memory ||
            !saved.create_image || !saved.destroy_image || !saved.bind_image) {
            if (value) parent.destroy_device(value, allocator);
            saved = (struct Device){0}; result = VK_ERROR_INITIALIZATION_FAILED;
        } else *out = value;
    }
    pthread_mutex_lock(&mutex); devices[slot] = saved; pthread_mutex_unlock(&mutex);
    return result;
}
static VKAPI_ATTR void VKAPI_CALL wrapped_destroy_device(VkDevice value, const VkAllocationCallbacks *allocator) {
    if (!value) return;
    struct Device saved;
    if (!device_copy(value, &saved)) return;
    saved.destroy(value, allocator);
    pthread_mutex_lock(&mutex);
    for (unsigned i = 0; i < MAX_DEVICES; ++i)
        if (devices[i].handle == value) devices[i] = (struct Device){0};
    pthread_mutex_unlock(&mutex);
    stats("DestroyDevice", VK_SUCCESS);
}

static VKAPI_ATTR VkResult VKAPI_CALL wrapped_properties(VkDevice device, const AHardwareBuffer *buffer,
        VkAndroidHardwareBufferPropertiesANDROID *properties) {
    struct Device saved;
    if (!buffer || !properties) return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    if (!device_copy(device, &saved)) return VK_ERROR_INITIALIZATION_FAILED;
    if (!saved.properties) return VK_ERROR_EXTENSION_NOT_PRESENT;
    int error = sq2_v77_ahb_prepare(buffer);
    if (error) { VkResult result = prepare_error(error); stats("Properties-rejected", result); return result; }
    VkResult result = saved.properties(device, buffer, properties);
    sq2_v77_ahb_finish(buffer);
    stats("Properties", result);
    return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL wrapped_allocate(VkDevice device, const VkMemoryAllocateInfo *info,
        const VkAllocationCallbacks *allocator, VkDeviceMemory *out) {
    struct Device saved;
    if (!info || !out) return VK_ERROR_INITIALIZATION_FAILED;
    *out = VK_NULL_HANDLE;
    if (!device_copy(device, &saved)) return VK_ERROR_INITIALIZATION_FAILED;
    const AHardwareBuffer *buffer = NULL;
    unsigned count = 0, imports = 0;
    for (const VkBaseInStructure *p = info->pNext; p; p = p->pNext) {
        if (++count > MAX_CHAIN) return VK_ERROR_INVALID_EXTERNAL_HANDLE;
        if (p->sType == VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO &&
            ((const VkExportMemoryAllocateInfo *)p)->handleTypes) {
            stats("Allocate-export-unsupported", VK_ERROR_INVALID_EXTERNAL_HANDLE);
            return VK_ERROR_INVALID_EXTERNAL_HANDLE;
        }
        if (p->sType == VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_NV &&
            ((const VkExportMemoryAllocateInfoNV *)p)->handleTypes) return VK_ERROR_INVALID_EXTERNAL_HANDLE;
        if (p->sType == VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID) {
            if (++imports > 1) return VK_ERROR_INVALID_EXTERNAL_HANDLE;
            buffer = ((const VkImportAndroidHardwareBufferInfoANDROID *)p)->buffer;
        }
    }
    /* This donor routes a present-but-null import structure toward its own AHB
     * allocation hook. Export/allocation of donor-created AHBs is unsupported;
     * reject here rather than enter that unadapted path without a lease. */
    if (imports && !buffer) {
        stats("Allocate-null-AHB-unsupported", VK_ERROR_INVALID_EXTERNAL_HANDLE);
        return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    }
    int error = buffer ? sq2_v77_ahb_prepare(buffer) : 0;
    if (error) { VkResult result = prepare_error(error); stats("Allocate-rejected", result); return result; }
    /* Pass the original complete chain and caller's allocator untouched. */
    VkResult result = saved.allocate(device, info, allocator, out);
    if (buffer) sq2_v77_ahb_finish(buffer);
    stats("AllocateMemory", result);
    return result;
}
static VKAPI_ATTR void VKAPI_CALL wrapped_free_memory(VkDevice device, VkDeviceMemory memory,
        const VkAllocationCallbacks *allocator) {
    struct Device saved;
    if (!device_copy(device, &saved)) return;
    saved.free_memory(device, memory, allocator); stats("FreeMemory", VK_SUCCESS);
}
static VKAPI_ATTR VkResult VKAPI_CALL wrapped_export(VkDevice device,
        const VkMemoryGetAndroidHardwareBufferInfoANDROID *info, AHardwareBuffer **out) {
    (void)device; (void)info;
    if (out) *out = NULL;
    stats("GetMemoryAHB-export-unsupported", VK_ERROR_INVALID_EXTERNAL_HANDLE);
    return VK_ERROR_INVALID_EXTERNAL_HANDLE;
}
/* A native node behind an unknown-size prefix cannot be replaced without
 * rewriting caller memory. Reject that path, while preserving every tail byte.
 * The marker-only create used by Android's deferred bind remains supported. */
static int native_chain(const void *chain, int create, const struct NativeNode **native) {
    unsigned count = 0, markers = 0, bind_aliases = 0;
    *native = NULL;
    for (const VkBaseInStructure *p = chain; p; p = p->pNext) {
        if (++count > MAX_CHAIN) return 0;
        if (p->sType == SQ2_NATIVE_BUFFER_ANDROID) {
            if ((const void *)p != chain || *native) return 0;
            *native = (const struct NativeNode *)p;
        }
        if (p->sType == SQ2_SWAPCHAIN_IMAGE_CREATE_ANDROID && (!create || ++markers > 1)) return 0;
        if (p->sType == VK_STRUCTURE_TYPE_IMAGE_SWAPCHAIN_CREATE_INFO_KHR) return 0;
        /* Current Android libvulkan prepends NativeBufferANDROID to this
         * original bind tail. v77 skips its unknown 1000060009 sType. Bare KHR
         * alias binding remains unsupported; do not invent its raw view owner. */
        if (p->sType == VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_SWAPCHAIN_INFO_KHR &&
            (create || !*native || ++bind_aliases > 1)) return 0;
        /* The plane-binding branch reuses the donor descriptor's native-node
         * slot. It is not a legal part of our non-disjoint, single-plane path. */
        if (*native && p->sType == VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO) return 0;
    }
    return 1;
}
static struct Image *image_find_locked(VkDevice device, VkImage handle) {
    for (struct Image *p = images; p; p = p->next)
        if (p->device == device && p->handle == handle) return p;
    return NULL;
}
static uintptr_t untag(uintptr_t address) {
    /* Bionic heap TBI tags were observed in this fixed donor. Do not mask any
     * other address bits; the resulting complete range must still be readable. */
    return address & UINT64_C(0x00ffffffffffffff);
}
static int read_private(uintptr_t address, void *out, size_t size) {
    address = untag(address);
    if (!address || !size || size > 4096 || address > UINTPTR_MAX - size) return 0;
    FILE *maps = fopen("/proc/self/maps", "re");
    if (!maps) { app_diag("read_private stage=maps-open errno=%d",errno); return 0; }
    char line[1024], permission[5]; unsigned long long low, high;
    int readable = 0;
    while (fgets(line, sizeof(line), maps)) {
        if (sscanf(line, "%llx-%llx %4s", &low, &high, permission) == 3 &&
            permission[0] == 'r' && address >= low && address + size <= high) {
            readable = 1; break;
        }
    }
    fclose(maps);
    if (!readable) { app_diag("read_private stage=range-unreadable size=%zu",size); return 0; }
#ifdef SQ2_VK_PROCESS_READ
    /* Read our own mm without opening proc mem, which is root-owned when the
     * application is nondumpable. Kernel copy returns a short read/error for an
     * invalidated range; do not turn failed validation into unchecked memcpy. */
    struct iovec local = {out,size}, remote = {(void *)address,size};
    ssize_t got;
    do { got = process_vm_readv(getpid(),&local,1,&remote,1,0); } while (got < 0 && errno == EINTR);
    if (got != (ssize_t)size) app_diag("read_private stage=process-read size=%zu got=%zd errno=%d",size,got,errno);
    return got == (ssize_t)size;
#else
    int fd = open("/proc/self/mem", O_RDONLY | O_CLOEXEC);
    if (fd < 0) { app_diag("read_private stage=mem-open errno=%d",errno); return 0; }
    ssize_t got;
    do { got = pread(fd, out, size, (off_t)address); } while (got < 0 && errno == EINTR);
    if (got != (ssize_t)size) app_diag("read_private stage=mem-read size=%zu got=%zd errno=%d",size,got,errno);
    close(fd);
    return got == (ssize_t)size;
#endif
}
struct NativeNoteCheck { uintptr_t base; unsigned found; };
static int native_note(struct dl_phdr_info *module, size_t size, void *opaque) {
    (void)size;
    struct NativeNoteCheck *check = opaque;
    if ((uintptr_t)module->dlpi_addr != check->base) return 0;
    static const unsigned char build_id[16] = {
        0x17,0x9c,0x7a,0x33,0x81,0x68,0x3d,0x68,0x00,0x46,0xce,0x54,0x4b,0x76,0xef,0x30
    };
    for (ElfW(Half) i = 0; i < module->dlpi_phnum; ++i) {
        const ElfW(Phdr) *p = &module->dlpi_phdr[i];
        if (p->p_type != PT_NOTE || p->p_filesz < sizeof(ElfW(Nhdr)) || p->p_filesz > 4096 ||
            p->p_vaddr > UINTPTR_MAX - check->base) continue;
        unsigned char data[4096];
        if (!read_private(check->base + p->p_vaddr, data, p->p_filesz)) continue;
        size_t pos = 0, length = (size_t)p->p_filesz;
        while (pos <= length - sizeof(ElfW(Nhdr))) {
            ElfW(Nhdr) note;
            memcpy(&note, data + pos, sizeof(note)); pos += sizeof(note);
            if (note.n_namesz > length - pos) break;
            size_t name = pos;
            pos += ((size_t)note.n_namesz + 3) & ~(size_t)3;
            if (pos > length || note.n_descsz > length - pos) break;
            if (note.n_type == NT_GNU_BUILD_ID && note.n_namesz == 4 && note.n_descsz == 16 &&
                !memcmp(data + name, "GNU", 4) && !memcmp(data + pos, build_id, 16)) check->found = 1;
            pos += ((size_t)note.n_descsz + 3) & ~(size_t)3;
        }
    }
    return 1;
}
static uintptr_t native_donor_base(const struct Device *device) {
    Dl_info info;
    if (!dladdr((const void *)device->create_image, &info) || !info.dli_fbase || !info.dli_fname)
        return 0;
    const char *name = strrchr(info.dli_fname, '/');
    if (strcmp(name ? name + 1 : info.dli_fname, "vulkan-v77-bridge.so")) return 0;
    uintptr_t base = (uintptr_t)info.dli_fbase;
    if ((uintptr_t)device->create_image != base + 0xd76b0 ||
        (uintptr_t)device->destroy_image != base + 0xdb128) return 0;
    /* patchelf relocates PT_NOTE in the private candidate: use loaded program
     * headers, never the original file offset of its BuildID descriptor. */
    struct NativeNoteCheck note = {base, 0};
    dl_iterate_phdr(native_note, &note);
    if (!note.found) { app_diag("native_donor_base stage=build-id-unavailable"); return 0; }
    /* Original 0f316d03... BuildID and four independent control-flow ranges.
     * Private import-symbol/RUNPATH changes preserve these original bytes. */
    static const struct { uintptr_t offset; unsigned char bytes[16]; } checks[] = {
        {0xd76b0, {0xff,0x03,0x07,0xd1,0xfd,0x7b,0x18,0xa9,0xfd,0x03,0x06,0x91,0xfc,0xcb,0x00,0xf9}},
        {0xdb128, {0x81,0x03,0x00,0xb4,0xff,0x43,0x01,0xd1,0xfd,0x7b,0x04,0xa9,0xfd,0x03,0x01,0x91}},
        {0x147164, {0x6b,0x62,0x41,0xb9,0xeb,0x01,0x00,0x34,0xf5,0x03,0x1f,0xaa,0x76,0xa2,0x05,0x91}},
        {0x1f5824, {0x68,0x00,0x90,0x36,0xe0,0x01,0x40,0xf9,0xa0,0x02,0x00,0xb5,0x03,0xe4,0x00,0x6f}}
    };
    for (unsigned i = 0; i < sizeof(checks) / sizeof(checks[0]); ++i) {
        unsigned char actual[16];
        if (base > UINTPTR_MAX - checks[i].offset ||
            !read_private(base + checks[i].offset, actual, sizeof(actual)) ||
            memcmp(actual, checks[i].bytes, sizeof(actual))) return 0;
    }
    return base;
}
static uint32_t value32(const unsigned char *p, size_t offset) {
    uint32_t value; memcpy(&value, p + offset, sizeof(value)); return value;
}
static uintptr_t valueptr(const unsigned char *p, size_t offset) {
    uintptr_t value; memcpy(&value, p + offset, sizeof(value)); return untag(value);
}
static int native_last_owner(const struct Device *device, const struct Image *image) {
    uintptr_t base = native_donor_base(device), iv = 0, mv = 0, memory = 0, raw = 0;
    uint32_t refs = 0, memory_refs = 0, planes = UINT32_MAX, kind = UINT32_MAX;
    unsigned char object[368], mem[344];
    const char *reason = "donor-layout-unavailable";
    int safe = 0;
    if (!base || !read_private((uintptr_t)image->handle, object, sizeof(object))) goto report;
    iv = valueptr(object, 0); refs = value32(object, 12);
    planes = value32(object, 352); kind = value32(object, 288);
    memory = valueptr(object, 360);
    reason = "image-layout-or-reference";
    if (iv != base + 0x2e0d00 || refs != 1 || planes > 1) goto report;
    /* A failed/never-reached bind may not have created any internal memory.
     * The unique image can be destroyed while its still-private lease survives. */
    if (!memory) {
        reason = "empty-native-memory";
        safe = image->native_attempt && !image->native_succeeded;
        goto report;
    }
    reason = "memory-layout-unavailable";
    if (kind != 3 || planes != 1 || !read_private(memory, mem, sizeof(mem))) goto report;
    mv = valueptr(mem, 0); memory_refs = value32(mem, 12); raw = valueptr(mem, 336);
    reason = "memory-layout-reference-or-view";
    if (mv != base + 0x2e17f0 || memory_refs != 1 || raw != untag((uintptr_t)image->view) ||
        valueptr(mem, 312) || value32(mem, 320) != (uint32_t)SQ2_NATIVE_BUFFER_ANDROID ||
        valueptr(mem, 104) != valueptr(object, 104)) goto report;
    reason = "unique-native-owner"; safe = 1;
report:
    fprintf(stderr, "V77_WSI_LIFETIME status=%s reason=%s image=%p image_vtable=%p image_refs=%u kind=%u planes=%u memory=%p memory_vtable=%p memory_refs=%u raw=%p view=%p\n",
            safe ? "READY" : "FAILED", reason, (void *)(uintptr_t)image->handle, (void *)iv,
            refs, kind, planes, (void *)memory, (void *)mv, memory_refs, (void *)raw, image->view);
    return safe;
}
static int prepare_native(const struct Device *device, const struct Image *image,
        const struct NativeNode *node, struct NativeNode *copy, const void **view, void **lease) {
    *view = NULL; *lease = NULL;
    if (!image->eligible || !node->handle || node->format != 1 || node->stride <= 0) {
        app_diag("native_prepare stage=fields eligible=%u handle=%u format=%d stride=%d width=%u height=%u",
                 image->eligible,node->handle!=NULL,node->format,node->stride,image->width,image->height);
        return -EINVAL;
    }
    if (!native_donor_base(device)) { app_diag("native_prepare stage=donor-layout"); return -EINVAL; }
    struct Sq2V77NativeInfo actual = {0};
    int error = sq2_v77_native_prepare(node->handle, view, lease, &actual);
    if (error) { app_diag("native_prepare stage=mapper error=%d",error); return error; }
    if (!*view || !*lease || actual.width != image->width || actual.height != image->height ||
        actual.layers != 1 || actual.format != 1 || actual.stride != (uint32_t)node->stride ||
        actual.stride < actual.width || !actual.size) {
        app_diag("native_prepare stage=metadata width=%u/%u height=%u/%u format=%u stride=%u/%d size=%llu",
                 actual.width,image->width,actual.height,image->height,actual.format,actual.stride,node->stride,
                 (unsigned long long)actual.size);
        if (*lease) sq2_v77_native_finish(*lease);
        *view = NULL; *lease = NULL;
        return -EINVAL;
    }
    memcpy(copy, node, sizeof(*copy)); copy->handle = *view;
    return 0;
}
static VKAPI_ATTR VkResult VKAPI_CALL wrapped_create_image(VkDevice device, const VkImageCreateInfo *info,
        const VkAllocationCallbacks *allocator, VkImage *out) {
    struct Device saved;
    if (!info || !out) return VK_ERROR_INITIALIZATION_FAILED;
    *out = VK_NULL_HANDLE;
    if (!device_copy(device, &saved)) return VK_ERROR_INITIALIZATION_FAILED;
    const struct NativeNode *native;
    if (!native_chain(info->pNext, 1, &native)) {
        stats("CreateImage-raw-WSI-unsupported", VK_ERROR_INVALID_EXTERNAL_HANDLE);
        return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    }
    struct Image *record = calloc(1, sizeof(*record));
    if (!record) return VK_ERROR_OUT_OF_HOST_MEMORY;
    record->device = device; record->width = info->extent.width; record->height = info->extent.height;
    record->eligible = info->imageType == VK_IMAGE_TYPE_2D &&
        info->format == VK_FORMAT_R8G8B8A8_UNORM && info->arrayLayers == 1 && info->mipLevels == 1 &&
        info->samples == VK_SAMPLE_COUNT_1_BIT && info->extent.depth == 1 &&
        info->extent.width && info->extent.height &&
        !(info->flags & (VK_IMAGE_CREATE_SPARSE_BINDING_BIT | VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT |
                        VK_IMAGE_CREATE_SPARSE_ALIASED_BIT | VK_IMAGE_CREATE_PROTECTED_BIT |
                        VK_IMAGE_CREATE_DISJOINT_BIT));
    VkImageCreateInfo changed = *info;
    struct NativeNode node;
    if (native) {
        int error = prepare_native(&saved, record, native, &node, &record->view, &record->lease);
        if (error) { free(record); stats("CreateImage-native-rejected", prepare_error(error)); return prepare_error(error); }
        record->native_attempt = 1;
        changed.pNext = &node;
    }
    VkResult result = saved.create_image(device, &changed, allocator, out);
    if (result == VK_SUCCESS && *out) {
        record->handle = *out;
        record->native_succeeded = native != NULL;
        pthread_mutex_lock(&mutex); record->next = images; images = record; pthread_mutex_unlock(&mutex);
    } else if (result == VK_SUCCESS && record->lease) {
        /* A broken success-without-handle contract has no collectible owner.
         * Do not release a view that the donor may nevertheless have stored. */
        pthread_mutex_lock(&mutex);
        ++native_failures; record->next = quarantined_images; quarantined_images = record;
        pthread_mutex_unlock(&mutex);
        fprintf(stderr, "V77_WSI_FAILED operation=CreateImage reason=null-success action=retain-lease\n");
        result = VK_ERROR_INITIALIZATION_FAILED;
    } else {
        /* The audited failed constructor synchronously destroys its partial
         * image before returning, while this temporary view remains alive. */
        if (record->lease) sq2_v77_native_finish(record->lease);
        free(record);
        if (result == VK_SUCCESS) result = VK_ERROR_INITIALIZATION_FAILED;
    }
    if (native) stats("CreateImage-native", result);
    return result;
}
static VKAPI_ATTR void VKAPI_CALL wrapped_destroy_image(VkDevice device, VkImage image,
        const VkAllocationCallbacks *allocator) {
    struct Device saved;
    if (!device_copy(device, &saved)) return;
    struct Image *record = NULL;
    pthread_mutex_lock(&mutex);
    struct Image **slot = &images;
    while (*slot && !((*slot)->device == device && (*slot)->handle == image)) slot = &(*slot)->next;
    if (*slot) { record = *slot; *slot = record->next; }
    pthread_mutex_unlock(&mutex);
    int safe = !record || !record->lease || (!record->binding && native_last_owner(&saved, record));
    saved.destroy_image(device, image, allocator);
    if (record) {
        if (safe) {
            if (record->lease) sq2_v77_native_finish(record->lease);
            free(record);
        } else {
            /* The void Vulkan destroy API cannot report this safety failure.
             * Keep the lease, expose a sticky failure counter, and fail probe
             * verification rather than risking a deferred donor use-after-free. */
            pthread_mutex_lock(&mutex);
            ++native_failures; record->next = quarantined_images; quarantined_images = record;
            pthread_mutex_unlock(&mutex);
            fprintf(stderr, "V77_WSI_FAILED operation=DestroyImage action=retain-lease\n");
        }
    }
    stats("DestroyImage", VK_SUCCESS);
}
static VKAPI_ATTR VkResult VKAPI_CALL wrapped_bind_image(VkDevice device, VkImage image,
        VkDeviceMemory memory, VkDeviceSize offset) {
    struct Device saved;
    if (!device_copy(device, &saved)) return VK_ERROR_INITIALIZATION_FAILED;
    pthread_mutex_lock(&mutex);
    struct Image *record = image_find_locked(device, image);
    int native = record && (record->lease || record->binding);
    pthread_mutex_unlock(&mutex);
    if (native) return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    VkResult result = saved.bind_image(device, image, memory, offset);
    if (record && result == VK_SUCCESS) {
        pthread_mutex_lock(&mutex); record->bound = 1; pthread_mutex_unlock(&mutex);
    }
    return result;
}
static VkResult bind_image2(VkDevice device, uint32_t count, const VkBindImageMemoryInfo *infos, int khr) {
    struct Device saved;
    if ((count && !infos) || !device_copy(device, &saved)) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkBindImageMemory2 fn = khr ? saved.bind_image2_khr : saved.bind_image2;
    if (!fn) return VK_ERROR_EXTENSION_NOT_PRESENT;
    unsigned natives = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const struct NativeNode *node;
        if (!native_chain(infos[i].pNext, 0, &node)) return VK_ERROR_INVALID_EXTERNAL_HANDLE;
        pthread_mutex_lock(&mutex);
        struct Image *record = image_find_locked(device, infos[i].image);
        int already_native = record && (record->lease || record->binding);
        pthread_mutex_unlock(&mutex);
        if (already_native) return VK_ERROR_INVALID_EXTERNAL_HANDLE;
        if (node) {
            if (!record || !record->eligible || record->bound || infos[i].memory || infos[i].memoryOffset)
                return VK_ERROR_INVALID_EXTERNAL_HANDLE;
            ++natives;
        }
    }
    if (!natives) {
        VkResult result = fn(device, count, infos);
        /* A failed batch may still bind its earlier entries. Conservatively
         * prevent later native rebinding of every submitted ordinary image. */
        pthread_mutex_lock(&mutex);
        for (uint32_t i = 0; i < count; ++i) {
            struct Image *record = image_find_locked(device, infos[i].image);
            if (record) record->bound = 1;
        }
        pthread_mutex_unlock(&mutex);
        return result;
    }
    if (count > MAX_NATIVE_BATCH) return VK_ERROR_OUT_OF_HOST_MEMORY;
    for (uint32_t i = 0; i < count; ++i)
        for (uint32_t j = 0; j < i; ++j)
            if (infos[i].image == infos[j].image) return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    struct Attempt {
        struct Image *record;
        struct NativeNode node;
        const void *view;
        void *lease;
    };
    struct Attempt *attempts = calloc(count, sizeof(*attempts));
    VkBindImageMemoryInfo *copies = calloc(count, sizeof(*copies));
    if (!attempts || !copies) { free(attempts); free(copies); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    memcpy(copies, infos, count * sizeof(*copies));
    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < count; ++i) {
        const struct NativeNode *node;
        if (!native_chain(infos[i].pNext, 0, &node)) { result = VK_ERROR_INVALID_EXTERNAL_HANDLE; goto cancel; }
        if (!node) continue;
        pthread_mutex_lock(&mutex);
        struct Image *record = image_find_locked(device, infos[i].image);
        if (record && !record->lease && !record->binding && !record->bound) {
            record->binding = 1; attempts[i].record = record;
        }
        pthread_mutex_unlock(&mutex);
        if (!attempts[i].record) { result = VK_ERROR_INVALID_EXTERNAL_HANDLE; goto cancel; }
        int error = prepare_native(&saved, record, node, &attempts[i].node, &attempts[i].view, &attempts[i].lease);
        if (error) { result = prepare_error(error); goto cancel; }
        copies[i].pNext = &attempts[i].node;
    }
    /* Commit every prepared lease before donor entry. The batch may return on
     * its first error after storing raw pointers in earlier/partial images. */
    pthread_mutex_lock(&mutex);
    for (uint32_t i = 0; i < count; ++i) if (attempts[i].record) {
        attempts[i].record->view = attempts[i].view;
        attempts[i].record->lease = attempts[i].lease;
        attempts[i].record->native_attempt = 1;
    }
    pthread_mutex_unlock(&mutex);
    result = fn(device, count, copies);
    pthread_mutex_lock(&mutex);
    for (uint32_t i = 0; i < count; ++i) {
        struct Image *record = image_find_locked(device, infos[i].image);
        if (!record) continue;
        record->binding = 0; record->bound = 1;
        if (attempts[i].record) record->native_succeeded = result == VK_SUCCESS;
    }
    pthread_mutex_unlock(&mutex);
    free(copies); free(attempts);
    stats("BindImageMemory2-native", result);
    return result;
cancel:
    for (uint32_t i = 0; i < count; ++i) {
        if (attempts[i].lease) sq2_v77_native_finish(attempts[i].lease);
        if (attempts[i].record) {
            pthread_mutex_lock(&mutex); attempts[i].record->binding = 0; pthread_mutex_unlock(&mutex);
        }
    }
    free(copies); free(attempts);
    stats("BindImageMemory2-native-prepare-cancel", result);
    return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL wrapped_bind_image2(VkDevice d, uint32_t n, const VkBindImageMemoryInfo *p) {
    return bind_image2(d, n, p, 0);
}
static VKAPI_ATTR VkResult VKAPI_CALL wrapped_bind_image2_khr(VkDevice d, uint32_t n, const VkBindImageMemoryInfo *p) {
    return bind_image2(d, n, p, 1);
}

/* Internal callback addresses must not be ELF-preempted by libvulkan's public
 * entrypoints. Exported functions below only forward to these static targets. */
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL wrapper_gipa(VkInstance, const char *);
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL wrapper_gdpa(VkDevice, const char *);
#ifdef SQ2_VK_PIPELINE_DIAGNOSTICS
#include "quest2-vk-pipeline-trace.h"
#endif
static PFN_vkVoidFunction intercept(const char *name) {
#define MATCH(api, function) if (!strcmp(name, api)) return (PFN_vkVoidFunction)function
    MATCH("vkGetInstanceProcAddr", wrapper_gipa);
    MATCH("vkGetDeviceProcAddr", wrapper_gdpa);
    MATCH("vkCreateInstance", wrapped_create_instance);
    MATCH("vkDestroyInstance", wrapped_destroy_instance);
    MATCH("vkEnumeratePhysicalDevices", wrapped_enumerate_physical);
    MATCH("vkEnumeratePhysicalDeviceGroups", wrapped_groups);
    MATCH("vkEnumeratePhysicalDeviceGroupsKHR", wrapped_groups_khr);
    MATCH("vkCreateDevice", wrapped_create_device);
#ifdef SQ2_VK_PIPELINE_DIAGNOSTICS
    MATCH("vkCreateShaderModule", traced_create_shader);
    MATCH("vkCreateGraphicsPipelines", traced_create_graphics);
#endif
    MATCH("vkDestroyDevice", wrapped_destroy_device);
    MATCH("vkGetAndroidHardwareBufferPropertiesANDROID", wrapped_properties);
    MATCH("vkAllocateMemory", wrapped_allocate);
    MATCH("vkFreeMemory", wrapped_free_memory);
    MATCH("vkGetMemoryAndroidHardwareBufferANDROID", wrapped_export);
    MATCH("vkCreateImage", wrapped_create_image);
    MATCH("vkDestroyImage", wrapped_destroy_image);
    MATCH("vkBindImageMemory", wrapped_bind_image);
    MATCH("vkBindImageMemory2", wrapped_bind_image2);
    MATCH("vkBindImageMemory2KHR", wrapped_bind_image2_khr);
#undef MATCH
    return NULL;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL wrapper_gipa(VkInstance instance, const char *name) {
    if (!name) return NULL;
    hwvulkan_device_t *real = begin_hal();
    if (!real) return NULL;
    PFN_vkVoidFunction original = real->GetInstanceProcAddr(instance, name);
    PFN_vkVoidFunction wrapped = original ? intercept(name) : NULL;
    end_hal();
    return wrapped ? wrapped : original;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL wrapper_gdpa(VkDevice device, const char *name) {
    struct Device saved;
    if (!name || !device_copy(device, &saved)) return NULL;
    PFN_vkVoidFunction original = saved.gdpa(device, name);
    PFN_vkVoidFunction wrapped = original ? intercept(name) : NULL;
    return wrapped ? wrapped : original;
}
EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *name) {
    return wrapper_gipa(instance, name);
}
EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char *name) {
    return wrapper_gdpa(device, name);
}
static VKAPI_ATTR VkResult VKAPI_CALL wrapped_instance_extensions(const char *layer, uint32_t *count,
        VkExtensionProperties *out) {
    hwvulkan_device_t *real = begin_hal();
    if (!real) {
        fprintf(stderr, "V77_WRAPPER_INIT stage=enumerate-instance-extensions-no-open-hal\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult result = real->EnumerateInstanceExtensionProperties(layer, count, out);
    fprintf(stderr, "V77_WRAPPER_INIT stage=enumerate-instance-extensions result=%d layer=%d output=%d count=%u\n",
            (int)result, !!layer, !!out,
            count && (result == VK_SUCCESS || result == VK_INCOMPLETE) ? *count : 0);
    end_hal(); return result;
}
static void load_driver(void) {
    fprintf(stderr, "V77_WRAPPER_INIT stage=load-donor-begin path=%s\n", DONOR);
    library = dlopen(DONOR, RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "V77_WRAPPER dlopen failed: %s\n", dlerror()); return; }
    hw_module_t *module = dlsym(library, HAL_MODULE_INFO_SYM_AS_STR);
    if (!module || module->tag != HARDWARE_MODULE_TAG || !module->id ||
        strcmp(module->id, HWVULKAN_HARDWARE_MODULE_ID) || !module->methods || !module->methods->open) {
        fprintf(stderr, "V77_WRAPPER_INIT stage=load-donor-invalid-hmi symbol=%d\n", !!module);
        return;
    }
    donor_module = module;
    fprintf(stderr, "V77_WRAPPER_INIT stage=load-donor-ready module_api=%u hal_api=%u\n",
            (unsigned)module->module_api_version, (unsigned)module->hal_api_version);
}
static int close_driver(hw_device_t *common) {
    struct Hal *closing = (struct Hal *)common;
    pthread_mutex_lock(&mutex);
    if (!hal || closing != hal) { pthread_mutex_unlock(&mutex); return -EINVAL; }
    unsigned busy = hal_calls;
    for (unsigned i = 0; i < MAX_INSTANCES; ++i) busy |= instances[i].state;
    for (unsigned i = 0; i < MAX_DEVICES; ++i) busy |= devices[i].state;
    if (busy) { pthread_mutex_unlock(&mutex); return -EBUSY; }
    hal = NULL; opening = 1;
    pthread_mutex_unlock(&mutex);
    // Matched v77 allocates its HAL device but provides no close callback.
    // Keep that one donor device for reuse until process exit; only our own
    // wrapper is freed here. Do not guess the donor allocator's destruction ABI.
    int result = closing->real->common.close ?
        closing->real->common.close(&closing->real->common) : 0;
    free(closing);
    pthread_mutex_lock(&mutex); opening = 0; pthread_mutex_unlock(&mutex);
    return result;
}
static int open_driver(const hw_module_t *module, const char *id, hw_device_t **out) {
    fprintf(stderr, "V77_WRAPPER_INIT stage=hal-open-begin module=%d id_vk0=%d output=%d\n",
            !!module, id && !strcmp(id, HWVULKAN_DEVICE_0), !!out);
    if (!module || !out || !id || strcmp(id, HWVULKAN_DEVICE_0)) return -EINVAL;
    *out = NULL;
    pthread_mutex_lock(&mutex);
    if (hal || opening) {
        pthread_mutex_unlock(&mutex);
        fprintf(stderr, "V77_WRAPPER_INIT stage=hal-open-busy\n");
        return -EBUSY;
    }
    opening = 1;
    pthread_mutex_unlock(&mutex);
    pthread_once(&once, load_driver);
    int result = -ENODEV;
    struct Hal *created = NULL;
    hw_device_t *device = NULL;
    if (!donor_module) {
        fprintf(stderr, "V77_WRAPPER_INIT stage=hal-open-no-donor-module\n");
        goto done;
    }
    created = calloc(1, sizeof(*created));
    if (!created) { result = -ENOMEM; goto done; }
    if (persistent_device) {
        device = &persistent_device->common; result = 0;
    } else result = donor_module->methods->open(donor_module, id, &device);
    fprintf(stderr, "V77_WRAPPER_INIT stage=donor-hal-open-end result=%d device=%d\n", result, !!device);
    if (result || !device) { if (!result) result = -ENODEV; goto done; }
    hwvulkan_device_t *real = (hwvulkan_device_t *)device;
    fprintf(stderr, "V77_WRAPPER_INIT stage=donor-hal-dispatch tag_ok=%d close=%d create_instance=%d gipa=%d enumerate_extensions=%d recursive_gipa=%d recursive_create=%d\n",
            real->common.tag == HARDWARE_DEVICE_TAG, !!real->common.close,
            !!real->CreateInstance, !!real->GetInstanceProcAddr, !!real->EnumerateInstanceExtensionProperties,
            real->GetInstanceProcAddr == wrapper_gipa, real->CreateInstance == wrapped_create_instance);
    if (real->common.tag != HARDWARE_DEVICE_TAG ||
        !real->CreateInstance || !real->GetInstanceProcAddr || !real->EnumerateInstanceExtensionProperties ||
        real->GetInstanceProcAddr == wrapper_gipa || real->CreateInstance == wrapped_create_instance) {
        result = -ENODEV; goto done;
    }
    created->real = real;
    if (!real->common.close) persistent_device = real;
    created->exposed.common = real->common;
    /* Use the module the loader actually opened; no preemptible HMI reference. */
    created->exposed.common.module = (hw_module_t *)module;
    created->exposed.common.close = close_driver;
    created->exposed.CreateInstance = wrapped_create_instance;
    created->exposed.GetInstanceProcAddr = wrapper_gipa;
    created->exposed.EnumerateInstanceExtensionProperties = wrapped_instance_extensions;
    pthread_mutex_lock(&mutex); hal = created; pthread_mutex_unlock(&mutex);
    *out = &created->exposed.common;
    result = 0;
done:
    if (result) {
        if (device && device->close) device->close(device);
        free(created);
    }
    pthread_mutex_lock(&mutex); opening = 0; pthread_mutex_unlock(&mutex);
    fprintf(stderr, "V77_WRAPPER_INIT stage=hal-open-return result=%d device=%d\n", result, !!*out);
    return result;
}
static hw_module_methods_t methods = {.open = open_driver};
EXPORT hwvulkan_module_t HAL_MODULE_INFO_SYM = {
    .common = {.tag = HARDWARE_MODULE_TAG,
        .module_api_version = HWVULKAN_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = HWVULKAN_HARDWARE_MODULE_ID,
        .name = "SQ2 private v77 AHB import wrapper", .author = "SQ2 lab port", .methods = &methods}
};
