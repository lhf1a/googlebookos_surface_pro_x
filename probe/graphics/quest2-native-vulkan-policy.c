/* Native QTI Vulkan HAL adapter: preserve the verified HOST_CACHED allocation
 * policy only. Handles, AHBs, images, pNext chains and WSI remain donor-native.
 * Lifecycle dispatch is shared in design with the already exercised v77 HAL;
 * no synthetic handle registry, mapper implementation, or image translation.
 */
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_ANDROID_KHR
#define SQ2_VK_CACHED_MEMORY
#include <vulkan/vulkan.h>
#include <hardware/hwvulkan.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quest2-native-vulkan-memory.h"
#ifndef SQ2_NATIVE_VULKAN_DONOR
#define SQ2_NATIVE_VULKAN_DONOR "/odm/lib64/vulkan.qti-native.so"
#endif
#define DONOR SQ2_NATIVE_VULKAN_DONOR
#define EXPORT __attribute__((visibility("default")))
#define MAX_INSTANCES 16
#define MAX_PHYSICAL 64
#define MAX_DEVICES 64
struct Hal { hwvulkan_device_t exposed; hwvulkan_device_t *real; };
struct Instance {
    unsigned state;
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
    PFN_vkAllocateMemory allocate;
    uint32_t cached_types, memory_flags[VK_MAX_MEMORY_TYPES];
};
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static void *library;
static hw_module_t *donor_module;
static hwvulkan_device_t *persistent_device;
static struct Hal *hal;
static unsigned opening, hal_calls;
static struct Instance instances[MAX_INSTANCES];
static struct Physical physicals[MAX_PHYSICAL];
static struct Device devices[MAX_DEVICES];
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
        fprintf(stderr, "QTI_VULKAN_POLICY_INIT stage=create-instance-invalid-arguments info=%d out=%d\n",
                !!info, !!out);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *out = VK_NULL_HANDLE;
    hwvulkan_device_t *real = begin_hal();
    if (!real) {
        fprintf(stderr, "QTI_VULKAN_POLICY_INIT stage=create-instance-no-open-hal\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    unsigned slot;
    pthread_mutex_lock(&mutex);
    for (slot = 0; slot < MAX_INSTANCES && instances[slot].state; ++slot) {}
    if (slot < MAX_INSTANCES) instances[slot].state = 1;
    pthread_mutex_unlock(&mutex);
    if (slot == MAX_INSTANCES) {
        fprintf(stderr, "QTI_VULKAN_POLICY_INIT stage=create-instance-table-full\n");
        end_hal(); return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    VkInstance value = VK_NULL_HANDLE;
    fprintf(stderr, "QTI_VULKAN_POLICY_INIT stage=donor-create-instance-begin slot=%u api=%u extensions=%u layers=%u\n",
            slot, info->pApplicationInfo ? info->pApplicationInfo->apiVersion : 0,
            info->enabledExtensionCount, info->enabledLayerCount);
    VkResult result = real->CreateInstance(info, allocator, &value);
    fprintf(stderr, "QTI_VULKAN_POLICY_INIT stage=donor-create-instance-end slot=%u result=%d instance=%d\n",
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
        fprintf(stderr, "QTI_VULKAN_POLICY_INIT stage=instance-dispatch slot=%u destroy=%d gdpa=%d enumerate=%d groups=%d groups_khr=%d create_device=%d destroy_device=%d\n",
                slot, !!saved.destroy, !!saved.gdpa, !!saved.enumerate, !!saved.groups,
                !!saved.groups_khr, !!saved.create_device, !!saved.destroy_device);
        if (!value || !saved.destroy) {
            /* A successful ICD create without a valid destroy function breaks
             * the HAL contract. Do not drop an uncollectable live instance. */
            fprintf(stderr, "QTI_VULKAN_POLICY_FATAL invalid instance dispatch\n"); abort();
        }
        if (!saved.gdpa || !saved.enumerate || !saved.create_device || !saved.destroy_device) {
            fprintf(stderr, "QTI_VULKAN_POLICY_INIT stage=instance-dispatch-rejected slot=%u cleanup=destroy-instance\n", slot);
            saved.destroy(value, allocator); saved = (struct Instance){0};
            result = VK_ERROR_INITIALIZATION_FAILED;
        } else *out = value;
    }
    pthread_mutex_lock(&mutex); instances[slot] = saved; pthread_mutex_unlock(&mutex);
    end_hal();
    fprintf(stderr, "QTI_VULKAN_POLICY_INIT stage=create-instance-return slot=%u result=%d instance=%d\n",
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
        DP(allocate, PFN_vkAllocateMemory, "vkAllocateMemory");
#undef DP
#ifdef SQ2_VK_CACHED_MEMORY
        PFN_vkGetPhysicalDeviceMemoryProperties query=
            (PFN_vkGetPhysicalDeviceMemoryProperties)parent.gipa(parent.handle,"vkGetPhysicalDeviceMemoryProperties");
        VkPhysicalDeviceMemoryProperties mp={0};
        if(query)query(physical,&mp);
        if(!query || mp.memoryTypeCount>32) saved.allocate=NULL;
        else for(unsigned i=0;i<mp.memoryTypeCount;i++) {
            saved.memory_flags[i]=mp.memoryTypes[i].propertyFlags;
            if((mp.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT))==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) saved.cached_types|=1u<<i;
            if(getenv("SQ2_VULKAN_MEMORY_TRACE"))
                fprintf(stderr,"QTI_VULKAN_MEMORY_TYPE index=%u flags=%x cached_scope=%u\n",
                        i,mp.memoryTypes[i].propertyFlags,!!(saved.cached_types&(1u<<i)));
        }
#endif
        if (!value || !saved.destroy || !saved.allocate) {
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
}

static VKAPI_ATTR VkResult VKAPI_CALL wrapped_allocate(VkDevice device,
        const VkMemoryAllocateInfo *info, const VkAllocationCallbacks *allocator, VkDeviceMemory *out) {
    struct Device saved;
    if (!info || !out || !device_copy(device, &saved)) return VK_ERROR_INITIALIZATION_FAILED;
    unsigned external=0, depth=0;
    for (const VkBaseInStructure *p=info->pNext; p; p=p->pNext) {
        if (++depth>64) return VK_ERROR_INITIALIZATION_FAILED;
        if (p->sType==VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID ||
            p->sType==VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR ||
            p->sType==VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT) external=1;
        if (p->sType==VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO &&
            ((const VkExportMemoryAllocateInfo *)p)->handleTypes) external=1;
    }
    static atomic_uint_fast64_t serial;
    struct Sq2VulkanMemoryScope scope={
        .enabled=!external && info->memoryTypeIndex<32 &&
                 !!(saved.cached_types&(1u<<info->memoryTypeIndex)),
        .memory_type=info->memoryTypeIndex,
        .memory_flags=info->memoryTypeIndex<32?saved.memory_flags[info->memoryTypeIndex]:0,
        .serial=atomic_fetch_add(&serial,1)};
    struct Sq2VulkanMemoryScope previous=sq2_native_vulkan_memory_scope(scope);
    VkResult result=saved.allocate(device,info,allocator,out);
    sq2_native_vulkan_memory_scope(previous);
    if(getenv("SQ2_VULKAN_MEMORY_TRACE"))
        fprintf(stderr,"QTI_VULKAN_ALLOC serial=%llu type=%u properties=%x cached_scope=%u external=%u bytes=%llu result=%d\n",
                (unsigned long long)scope.serial,scope.memory_type,scope.memory_flags,scope.enabled,external,
                (unsigned long long)info->allocationSize,result);
    return result;
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
    MATCH("vkAllocateMemory", wrapped_allocate);
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
        fprintf(stderr, "QTI_VULKAN_POLICY_INIT stage=enumerate-instance-extensions-no-open-hal\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult result = real->EnumerateInstanceExtensionProperties(layer, count, out);
    fprintf(stderr, "QTI_VULKAN_POLICY_INIT stage=enumerate-instance-extensions result=%d layer=%d output=%d count=%u\n",
            (int)result, !!layer, !!out,
            count && (result == VK_SUCCESS || result == VK_INCOMPLETE) ? *count : 0);
    end_hal(); return result;
}
static void load_driver(void) {
    fprintf(stderr, "QTI_VULKAN_POLICY_INIT stage=load-donor-begin path=%s\n", DONOR);
    library = dlopen(DONOR, RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "QTI_VULKAN_POLICY dlopen failed: %s\n", dlerror()); return; }
    hw_module_t *module = dlsym(library, HAL_MODULE_INFO_SYM_AS_STR);
    if (!module || module->tag != HARDWARE_MODULE_TAG || !module->id ||
        strcmp(module->id, HWVULKAN_HARDWARE_MODULE_ID) || !module->methods || !module->methods->open) {
        fprintf(stderr, "QTI_VULKAN_POLICY_INIT stage=load-donor-invalid-hmi symbol=%d\n", !!module);
        return;
    }
    donor_module = module;
    fprintf(stderr, "QTI_VULKAN_POLICY_INIT stage=load-donor-ready module_api=%u hal_api=%u\n",
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
    fprintf(stderr, "QTI_VULKAN_POLICY_INIT stage=hal-open-begin module=%d id_vk0=%d output=%d\n",
            !!module, id && !strcmp(id, HWVULKAN_DEVICE_0), !!out);
    if (!module || !out || !id || strcmp(id, HWVULKAN_DEVICE_0)) return -EINVAL;
    *out = NULL;
    pthread_mutex_lock(&mutex);
    if (hal || opening) {
        pthread_mutex_unlock(&mutex);
        fprintf(stderr, "QTI_VULKAN_POLICY_INIT stage=hal-open-busy\n");
        return -EBUSY;
    }
    opening = 1;
    pthread_mutex_unlock(&mutex);
    pthread_once(&once, load_driver);
    int result = -ENODEV;
    struct Hal *created = NULL;
    hw_device_t *device = NULL;
    if (!donor_module) {
        fprintf(stderr, "QTI_VULKAN_POLICY_INIT stage=hal-open-no-donor-module\n");
        goto done;
    }
    created = calloc(1, sizeof(*created));
    if (!created) { result = -ENOMEM; goto done; }
    if (persistent_device) {
        device = &persistent_device->common; result = 0;
    } else result = donor_module->methods->open(donor_module, id, &device);
    fprintf(stderr, "QTI_VULKAN_POLICY_INIT stage=donor-hal-open-end result=%d device=%d\n", result, !!device);
    if (result || !device) { if (!result) result = -ENODEV; goto done; }
    hwvulkan_device_t *real = (hwvulkan_device_t *)device;
    fprintf(stderr, "QTI_VULKAN_POLICY_INIT stage=donor-hal-dispatch tag_ok=%d close=%d create_instance=%d gipa=%d enumerate_extensions=%d recursive_gipa=%d recursive_create=%d\n",
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
    fprintf(stderr, "QTI_VULKAN_POLICY_INIT stage=hal-open-return result=%d device=%d\n", result, !!*out);
    return result;
}
static hw_module_methods_t methods = {.open = open_driver};
EXPORT hwvulkan_module_t HAL_MODULE_INFO_SYM = {
    .common = {.tag = HARDWARE_MODULE_TAG,
        .module_api_version = HWVULKAN_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = HWVULKAN_HARDWARE_MODULE_ID,
        .name = "SQ2 native QTI Vulkan memory policy", .author = "SQ2 lab port", .methods = &methods}
};
