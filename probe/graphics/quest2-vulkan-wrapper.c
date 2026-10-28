/* Private v77 Vulkan HAL/AHB import adapter. No dispatchable handle rewriting.
 * Only run with the audited donor in a private vendor mount namespace.
 * Per-device PFNs come from that device's real GDPA, obtained from its owning
 * instance. Unsupported export/raw Android WSI paths fail before donor access.
 * Vulkan's normal external synchronization of instance/device destruction is
 * required; this table lock is not a substitute for application synchronization.
 */
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <hardware/hwvulkan.h>
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPORT __attribute__((visibility("default")))
#define DONOR "/vendor/lib64/egl/vulkan-v77-bridge.so"
#define MAX_INSTANCES 128
#define MAX_PHYSICAL 512
#define MAX_DEVICES 128
#define MAX_CHAIN 64
/* Android's private native-buffer header is not part of the NDK. These are
 * extension 11's published structure IDs; no private structure is dereferenced.
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

EXPORT unsigned sq2_v77_wrapper_stats(unsigned *leases, unsigned *owners, uint64_t *bytes) {
    return sq2_v77_ahb_stats(leases, owners, bytes);
}
static void stats(const char *operation, VkResult result) {
    unsigned leases = 0, owners = 0;
    uint64_t bytes = 0;
    unsigned views = sq2_v77_ahb_stats(&leases, &owners, &bytes);
    fprintf(stderr, "V77_AHB_WRAPPER operation=%s result=%d views=%u leases=%u owners=%u bytes=%llu\n",
            operation, (int)result, views, leases, owners, (unsigned long long)bytes);
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
static int raw_android_chain(const void *chain) {
    unsigned count = 0;
    for (const VkBaseInStructure *p = chain; p; p = p->pNext) {
        if (++count > MAX_CHAIN) return 1;
        if (p->sType == SQ2_NATIVE_BUFFER_ANDROID || p->sType == SQ2_SWAPCHAIN_IMAGE_CREATE_ANDROID ||
            p->sType == VK_STRUCTURE_TYPE_IMAGE_SWAPCHAIN_CREATE_INFO_KHR ||
            p->sType == VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_SWAPCHAIN_INFO_KHR) return 1;
    }
    return 0;
}
static VKAPI_ATTR VkResult VKAPI_CALL wrapped_create_image(VkDevice device, const VkImageCreateInfo *info,
        const VkAllocationCallbacks *allocator, VkImage *out) {
    struct Device saved;
    if (!info || !out) return VK_ERROR_INITIALIZATION_FAILED;
    *out = VK_NULL_HANDLE;
    if (!device_copy(device, &saved)) return VK_ERROR_INITIALIZATION_FAILED;
    if (raw_android_chain(info->pNext)) {
        stats("CreateImage-raw-WSI-unsupported", VK_ERROR_INVALID_EXTERNAL_HANDLE);
        return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    }
    return saved.create_image(device, info, allocator, out);
}
static VKAPI_ATTR void VKAPI_CALL wrapped_destroy_image(VkDevice device, VkImage image,
        const VkAllocationCallbacks *allocator) {
    struct Device saved;
    if (!device_copy(device, &saved)) return;
    saved.destroy_image(device, image, allocator); stats("DestroyImage", VK_SUCCESS);
}
static VKAPI_ATTR VkResult VKAPI_CALL wrapped_bind_image(VkDevice device, VkImage image,
        VkDeviceMemory memory, VkDeviceSize offset) {
    struct Device saved;
    if (!device_copy(device, &saved)) return VK_ERROR_INITIALIZATION_FAILED;
    return saved.bind_image(device, image, memory, offset);
}
static VkResult bind_image2(VkDevice device, uint32_t count, const VkBindImageMemoryInfo *infos, int khr) {
    struct Device saved;
    if ((count && !infos) || !device_copy(device, &saved)) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkBindImageMemory2 fn = khr ? saved.bind_image2_khr : saved.bind_image2;
    if (!fn) return VK_ERROR_EXTENSION_NOT_PRESENT;
    for (uint32_t i = 0; i < count; ++i) if (raw_android_chain(infos[i].pNext)) {
        stats("BindImageMemory2-raw-WSI-unsupported", VK_ERROR_INVALID_EXTERNAL_HANDLE);
        return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    }
    return fn(device, count, infos);
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
