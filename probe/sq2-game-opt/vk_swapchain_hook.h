#ifndef SQ2_VK_SWAPCHAIN_HOOK_H
#define SQ2_VK_SWAPCHAIN_HOOK_H

#include <stdint.h>

#if defined(__ANDROID__) || __has_include(<vulkan/vulkan.h>)
#include <vulkan/vulkan.h>
#else
/* Minimal frozen Vulkan KHR swapchain ABI types for host ASan/UBSan tests */
typedef int32_t VkResult;
typedef uint32_t VkBool32;
typedef uint32_t VkFlags;
typedef int32_t VkStructureType;
typedef int32_t VkFormat;
typedef int32_t VkColorSpaceKHR;
typedef int32_t VkSharingMode;
typedef int32_t VkSurfaceTransformFlagBitsKHR;
typedef int32_t VkCompositeAlphaFlagBitsKHR;
typedef int32_t VkPresentModeKHR;
typedef VkFlags VkSwapchainCreateFlagsKHR;
typedef VkFlags VkImageUsageFlags;

typedef struct VkExtent2D {
    uint32_t width;
    uint32_t height;
} VkExtent2D;

typedef struct VkDevice_T *VkDevice;
typedef struct VkInstance_T *VkInstance;
typedef uint64_t VkSurfaceKHR;
typedef uint64_t VkSwapchainKHR;
typedef struct VkAllocationCallbacks VkAllocationCallbacks;
typedef void (*PFN_vkVoidFunction)(void);

typedef struct VkSwapchainCreateInfoKHR {
    VkStructureType sType;
    const void *pNext;
    VkSwapchainCreateFlagsKHR flags;
    VkSurfaceKHR surface;
    uint32_t minImageCount;
    VkFormat imageFormat;
    VkColorSpaceKHR imageColorSpace;
    VkExtent2D imageExtent;
    uint32_t imageArrayLayers;
    VkImageUsageFlags imageUsage;
    VkSharingMode imageSharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t *pQueueFamilyIndices;
    VkSurfaceTransformFlagBitsKHR preTransform;
    VkCompositeAlphaFlagBitsKHR compositeAlpha;
    VkPresentModeKHR presentMode;
    VkBool32 clipped;
    VkSwapchainKHR oldSwapchain;
} VkSwapchainCreateInfoKHR;

typedef VkResult (*PFN_vkCreateSwapchainKHR)(
    VkDevice device,
    const VkSwapchainCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkSwapchainKHR *pSwapchain);

typedef PFN_vkVoidFunction (*PFN_vkGetDeviceProcAddr)(
    VkDevice device,
    const char *pName);

typedef PFN_vkVoidFunction (*PFN_vkGetInstanceProcAddr)(
    VkInstance instance,
    const char *pName);

#define VK_SUCCESS 0
#define VK_ERROR_INITIALIZATION_FAILED (-3)
#define VK_PRESENT_MODE_IMMEDIATE_KHR 0
#define VK_PRESENT_MODE_MAILBOX_KHR 1
#define VK_PRESENT_MODE_FIFO_KHR 2
#define VK_PRESENT_MODE_FIFO_RELAXED_KHR 3
#endif

#ifdef __cplusplus
extern "C" {
#endif

VkResult sq2_apply_mailbox_create_swapchain(
    PFN_vkCreateSwapchainKHR real_fn,
    int force_mailbox,
    VkDevice device,
    const VkSwapchainCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkSwapchainKHR *pSwapchain,
    int *out_used_mailbox,
    int *out_fell_back);

int sq2_apply_egl_swap_interval(
    int (*real_fn)(void *, int),
    int force_zero,
    void *dpy,
    int interval);

#ifdef __cplusplus
}
#endif

#endif /* SQ2_VK_SWAPCHAIN_HOOK_H */
