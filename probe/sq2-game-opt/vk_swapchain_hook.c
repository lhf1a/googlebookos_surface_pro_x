#include "vk_swapchain_hook.h"

#include <stddef.h>

VkResult sq2_apply_mailbox_create_swapchain(
    PFN_vkCreateSwapchainKHR real_fn,
    int force_mailbox,
    VkDevice device,
    const VkSwapchainCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkSwapchainKHR *pSwapchain,
    int *out_used_mailbox,
    int *out_fell_back) {
    if (out_used_mailbox) *out_used_mailbox = 0;
    if (out_fell_back) *out_fell_back = 0;

    if (!real_fn) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!force_mailbox || !pCreateInfo ||
        pCreateInfo->presentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
        return real_fn(device, pCreateInfo, pAllocator, pSwapchain);
    }

    VkSwapchainCreateInfoKHR modified = *pCreateInfo;
    modified.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    if (out_used_mailbox) *out_used_mailbox = 1;

    VkResult res = real_fn(device, &modified, pAllocator, pSwapchain);
    if (res == VK_SUCCESS) {
        return res;
    }

    /* Fallback to the app's original presentMode if driver/surface rejects MAILBOX */
    if (out_fell_back) *out_fell_back = 1;
    return real_fn(device, pCreateInfo, pAllocator, pSwapchain);
}

int sq2_apply_egl_swap_interval(
    int (*real_fn)(void *, int),
    int force_zero,
    void *dpy,
    int interval) {
    if (!real_fn) {
        return 0;
    }
    return real_fn(dpy, force_zero ? 0 : interval);
}
