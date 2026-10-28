/* System-loader device creation probe. No queues are submitted, no resources
 * or surfaces are created. Driver initialization may still access the GPU.
 */
#define _GNU_SOURCE
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    const char *choice=getenv("SQ2_VK_DEVICE_QUEUES");
    unsigned wanted=1;
    if(choice) {
        if(!strcmp(choice,"2"))wanted=2;
        else if(strcmp(choice,"1"))return 2;
    }
    void *loader=dlopen("/system/lib64/libvulkan.so",RTLD_NOW|RTLD_LOCAL);
    if(!loader){fprintf(stderr,"DEVICE_LOADER_FAIL %s\n",dlerror());return 1;}
    PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(loader,"vkGetInstanceProcAddr");
    if(!gipa){dlclose(loader);return 1;}
    PFN_vkCreateInstance create_instance=(PFN_vkCreateInstance)gipa(VK_NULL_HANDLE,"vkCreateInstance");
    if(!create_instance){dlclose(loader);return 1;}
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="SQ2 device-create-only",.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo ic={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    VkInstance instance=VK_NULL_HANDLE;VkResult result=create_instance(&ic,NULL,&instance);
    printf("DEVICE_INSTANCE result=%d api=1.1 extensions=0\n",result);
    if(result!=VK_SUCCESS||!instance){dlclose(loader);return 1;}
#define LOAD(type,var,name) type var=(type)gipa(instance,name)
    LOAD(PFN_vkDestroyInstance,destroy_instance,"vkDestroyInstance");
    LOAD(PFN_vkEnumeratePhysicalDevices,enumerate,"vkEnumeratePhysicalDevices");
    LOAD(PFN_vkGetPhysicalDeviceProperties,properties,"vkGetPhysicalDeviceProperties");
    LOAD(PFN_vkGetPhysicalDeviceQueueFamilyProperties,families,"vkGetPhysicalDeviceQueueFamilyProperties");
    LOAD(PFN_vkGetPhysicalDeviceFeatures,features,"vkGetPhysicalDeviceFeatures");
    LOAD(PFN_vkCreateDevice,create_device,"vkCreateDevice");
    LOAD(PFN_vkDestroyDevice,destroy_device,"vkDestroyDevice");
    LOAD(PFN_vkGetDeviceProcAddr,gdpa,"vkGetDeviceProcAddr");
#undef LOAD
    int exit_code=1;
    if(!destroy_instance||!enumerate||!properties||!families||!features||!create_device||!destroy_device||!gdpa)goto done;
    uint32_t count=0;
    result=enumerate(instance,&count,NULL);
    if(result!=VK_SUCCESS||!count||count>16)goto done;
    VkPhysicalDevice physicals[16];uint32_t capacity=count;
    result=enumerate(instance,&count,physicals);
    if(result!=VK_SUCCESS||!count||count>capacity)goto done;
    VkPhysicalDevice selected=VK_NULL_HANDLE;
    for(uint32_t i=0;i<count;i++) {
        VkPhysicalDeviceProperties props;properties(physicals[i],&props);
        printf("DEVICE_PHYSICAL index=%u vendor=%x device=%x api=%x driver=%x name=%.255s\n",
            i,props.vendorID,props.deviceID,props.apiVersion,props.driverVersion,props.deviceName);
        if(strstr(props.deviceName,"Turnip")||strstr(props.deviceName,"turnip"))continue;
        if(props.vendorID==0x5143&&strstr(props.deviceName,"Adreno"))selected=physicals[i];
    }
    if(!selected){printf("DEVICE_FAIL reason=closed-adreno-not-found\n");goto done;}
    uint32_t family_count=0;families(selected,&family_count,NULL);
    if(!family_count||family_count>64)goto done;
    VkQueueFamilyProperties qp[64];capacity=family_count;families(selected,&family_count,qp);
    if(family_count>capacity)goto done;
    for(uint32_t i=0;i<family_count;i++)printf("DEVICE_FAMILY index=%u count=%u flags=%x timestamp_bits=%u\n",i,qp[i].queueCount,qp[i].queueFlags,qp[i].timestampValidBits);
    VkPhysicalDeviceFeatures supported;features(selected,&supported);
    printf("DEVICE_FEATURES enabled=none supported_robustBufferAccess=%u supported_samplerAnisotropy=%u\n",supported.robustBufferAccess,supported.samplerAnisotropy);
    float priorities[2]={0.5f,0.5f};VkDeviceQueueCreateInfo queue_info[2];memset(queue_info,0,sizeof(queue_info));
    uint32_t requested=0,info_count=0;
    for(uint32_t i=0;i<family_count&&requested<wanted;i++) {
        /* Prefer useful advertised families; a video/protected-only family is
         * not a valid substitute for the graphics/compute Chrome comparison. */
        if(!qp[i].queueCount||!(qp[i].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT|VK_QUEUE_TRANSFER_BIT)))continue;
        uint32_t n=qp[i].queueCount<wanted-requested?qp[i].queueCount:wanted-requested;
        queue_info[info_count]=(VkDeviceQueueCreateInfo){.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex=i,.queueCount=n,.pQueuePriorities=priorities};
        printf("DEVICE_REQUEST info=%u family=%u count=%u flags=0 priority=0.5\n",info_count,i,n);
        requested+=n;info_count++;
    }
    if(requested!=wanted){printf("DEVICE_UNSUPPORTED wanted=%u available=%u\n",wanted,requested);exit_code=77;goto done;}
    VkDeviceCreateInfo dc={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=info_count,.pQueueCreateInfos=queue_info};
    VkDevice device=VK_NULL_HANDLE;
    printf("DEVICE_CREATE_BEGIN queues=%u infos=%u flags=0 extensions=0 legacy_features=null pnext=null allocator=null\n",requested,info_count);
    result=create_device(selected,&dc,NULL,&device);
    printf("DEVICE_CREATE_END result=%d device_present=%u\n",result,device!=VK_NULL_HANDLE);
    if(result!=VK_SUCCESS||!device)goto done;
    PFN_vkDestroyDevice destroy=(PFN_vkDestroyDevice)gdpa(device,"vkDestroyDevice");
    if(!destroy){destroy_device(device,NULL);goto done;}
    destroy(device,NULL);
    printf("DEVICE_DESTROY_DONE queues=%u submits=0 surfaces=0 images=0 status=PASS\n",wanted);
    exit_code=0;
done:
    if(destroy_instance)destroy_instance(instance,NULL);
    dlclose(loader);
    printf("DEVICE_CREATE_ONLY_RESULT queues=%u exit=%d\n",wanted,exit_code);
    return exit_code;
}
