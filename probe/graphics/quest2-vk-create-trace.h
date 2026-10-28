/* Diagnostic only: compile into an explicitly selected private HAL variant.
 * Reads Vulkan creation arguments; does not change or filter them. Unknown
 * pNext payloads are never guessed. Logcat captures this in ordinary app
 * namespaces, where stderr may be discarded. No pointer values are logged.
 */
#pragma once
#include <android/log.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SQ2_VK_CREATE_LOG(...) __android_log_print(ANDROID_LOG_INFO,"SQ2VkCreate",__VA_ARGS__)
static void sq2_trace_feature_words(const char *name,const void *data,size_t bytes) {
    const unsigned char *p=(const unsigned char *)data;
    size_t words=bytes/sizeof(VkBool32);
    SQ2_VK_CREATE_LOG("features=%s words=%zu",name,words);
    /* Fixed-size known structs only; log zeros too, so replay is unambiguous. */
    for(size_t i=0;i<words&&i<64;i++) {
        VkBool32 value;memcpy(&value,p+i*sizeof(value),sizeof(value));
        SQ2_VK_CREATE_LOG("feature=%s index=%zu value=%u",name,i,value);
    }
}
static void sq2_trace_create_chain(const char *owner,const void *next) {
    const VkBaseInStructure *p=(const VkBaseInStructure *)next;
    for(unsigned index=0;p&&index<32;index++,p=p->pNext) {
        SQ2_VK_CREATE_LOG("chain=%s index=%u stype=%d",owner,index,(int)p->sType);
#define SQ2_FEATURE_CASE(tag,type,first,last) \
        case tag:sq2_trace_feature_words(#type,(const unsigned char *)p+offsetof(type,first), \
            offsetof(type,last)+sizeof(((type *)0)->last)-offsetof(type,first));break
        switch(p->sType) {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2:
            sq2_trace_feature_words("VkPhysicalDeviceFeatures2",&((const VkPhysicalDeviceFeatures2 *)p)->features,sizeof(VkPhysicalDeviceFeatures));break;
        SQ2_FEATURE_CASE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,VkPhysicalDeviceVulkan11Features,storageBuffer16BitAccess,shaderDrawParameters);
        SQ2_FEATURE_CASE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,VkPhysicalDeviceVulkan12Features,samplerMirrorClampToEdge,subgroupBroadcastDynamicId);
        SQ2_FEATURE_CASE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,VkPhysicalDeviceVulkan13Features,robustImageAccess,maintenance4);
        SQ2_FEATURE_CASE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES,VkPhysicalDeviceProtectedMemoryFeatures,protectedMemory,protectedMemory);
        SQ2_FEATURE_CASE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES,VkPhysicalDeviceSamplerYcbcrConversionFeatures,samplerYcbcrConversion,samplerYcbcrConversion);
        SQ2_FEATURE_CASE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,VkPhysicalDeviceTimelineSemaphoreFeatures,timelineSemaphore,timelineSemaphore);
        SQ2_FEATURE_CASE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,VkPhysicalDeviceSynchronization2Features,synchronization2,synchronization2);
        SQ2_FEATURE_CASE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,VkPhysicalDeviceDynamicRenderingFeatures,dynamicRendering,dynamicRendering);
        SQ2_FEATURE_CASE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,VkPhysicalDeviceShaderFloat16Int8Features,shaderFloat16,shaderInt8);
        SQ2_FEATURE_CASE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES,VkPhysicalDevice16BitStorageFeatures,storageBuffer16BitAccess,storageInputOutput16);
        SQ2_FEATURE_CASE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES,VkPhysicalDevice8BitStorageFeatures,storageBuffer8BitAccess,storagePushConstant8);
        SQ2_FEATURE_CASE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,VkPhysicalDeviceHostQueryResetFeatures,hostQueryReset,hostQueryReset);
        SQ2_FEATURE_CASE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES,VkPhysicalDeviceScalarBlockLayoutFeatures,scalarBlockLayout,scalarBlockLayout);
        SQ2_FEATURE_CASE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES,VkPhysicalDeviceUniformBufferStandardLayoutFeatures,uniformBufferStandardLayout,uniformBufferStandardLayout);
        SQ2_FEATURE_CASE(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES,VkPhysicalDeviceVulkanMemoryModelFeatures,vulkanMemoryModel,vulkanMemoryModelAvailabilityVisibilityChains);
        case VK_STRUCTURE_TYPE_DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR:
            SQ2_VK_CREATE_LOG("global_priority=%d",(int)((const VkDeviceQueueGlobalPriorityCreateInfoKHR *)p)->globalPriority);break;
        default:SQ2_VK_CREATE_LOG("chain=%s index=%u payload=uninspected",owner,index);break;
        }
#undef SQ2_FEATURE_CASE
    }
    if(p)SQ2_VK_CREATE_LOG("chain=%s truncated=1",owner);
}
static void sq2_trace_create_device(const VkDeviceCreateInfo *info,const VkAllocationCallbacks *allocator) {
    if(!info){SQ2_VK_CREATE_LOG("device_info=null");return;}
    SQ2_VK_CREATE_LOG("BEGIN flags=%u queue_infos=%u extensions=%u layers=%u allocator=%u",
        info->flags,info->queueCreateInfoCount,info->enabledExtensionCount,info->enabledLayerCount,allocator!=NULL);
    if(allocator)SQ2_VK_CREATE_LOG("allocator_callbacks allocate=%u reallocate=%u free=%u internal_allocate=%u internal_free=%u",
        allocator->pfnAllocation!=NULL,allocator->pfnReallocation!=NULL,allocator->pfnFree!=NULL,
        allocator->pfnInternalAllocation!=NULL,allocator->pfnInternalFree!=NULL);
    if(info->pQueueCreateInfos)for(uint32_t i=0;i<info->queueCreateInfoCount&&i<16;i++) {
        const VkDeviceQueueCreateInfo *q=&info->pQueueCreateInfos[i];
        SQ2_VK_CREATE_LOG("queue_info=%u stype=%d flags=%u family=%u count=%u",i,(int)q->sType,q->flags,q->queueFamilyIndex,q->queueCount);
        if(q->pQueuePriorities)for(uint32_t j=0;j<q->queueCount&&j<32;j++)
            SQ2_VK_CREATE_LOG("queue_info=%u priority_index=%u priority=%.9g",i,j,(double)q->pQueuePriorities[j]);
        sq2_trace_create_chain("queue",q->pNext);
    }
    if(info->ppEnabledExtensionNames)for(uint32_t i=0;i<info->enabledExtensionCount&&i<64;i++)
        SQ2_VK_CREATE_LOG("extension=%u name=%.255s",i,info->ppEnabledExtensionNames[i]?info->ppEnabledExtensionNames[i]:"(null)");
    if(info->queueCreateInfoCount>16||info->enabledExtensionCount>64)SQ2_VK_CREATE_LOG("counts_truncated=1");
    if(info->pEnabledFeatures)sq2_trace_feature_words("VkPhysicalDeviceFeatures",info->pEnabledFeatures,sizeof(*info->pEnabledFeatures));
    else SQ2_VK_CREATE_LOG("legacy_features=null");
    sq2_trace_create_chain("device",info->pNext);
    SQ2_VK_CREATE_LOG("END");
}
#undef SQ2_VK_CREATE_LOG
