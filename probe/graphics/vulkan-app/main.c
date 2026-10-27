/* Bounded, ordinary-app Vulkan WSI smoke test. No permissions or driver overrides.
 * Draws 120 frames through Android's system loader then retains the last frame.
 * A680 below is the current DT/Mesa ID; SQ2's marketed GPU is Adreno 690.
 */
#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "triangle-vert.h"
#include "triangle-frag.h"
#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"SQ2Vulkan",__VA_ARGS__)
#define VK(x) do { VkResult r=(x); if(r!=VK_SUCCESS){LOG("FAIL %s = %d",#x,r); return false;} }while(0)
typedef struct {
    VkInstance instance; VkPhysicalDevice gpu; VkDevice device; VkQueue queue;
    VkSurfaceKHR surface; VkSwapchainKHR swapchain; VkExtent2D extent;
    VkRenderPass pass; VkPipelineLayout layout; VkPipeline pipeline;
    VkCommandPool pool; VkCommandBuffer command; VkFence fence; VkSemaphore acquire;
    VkImage images[8]; VkImageView views[8]; VkFramebuffer frames[8]; VkSemaphore done[8];
    uint32_t count, family, rendered; bool ready;
} Engine;
static Engine e;
static bool initialize(ANativeWindow *window) {
    const char *ix[]={VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="SQ2 Vulkan Test",.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app,.enabledExtensionCount=2,.ppEnabledExtensionNames=ix};
    VK(vkCreateInstance(&ici,NULL,&e.instance));
    uint32_t n=8; VkPhysicalDevice devices[8]; VK(vkEnumeratePhysicalDevices(e.instance,&n,devices));
    for(uint32_t i=0;i<n;i++) {
        VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(devices[i],&p);
        LOG("DEVICE %s api=%u.%u.%u",p.deviceName,VK_VERSION_MAJOR(p.apiVersion),VK_VERSION_MINOR(p.apiVersion),VK_VERSION_PATCH(p.apiVersion));
        if(p.vendorID==0x5143 && p.deviceType==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU && strstr(p.deviceName,"680")) e.gpu=devices[i];
    }
    if(!e.gpu){LOG("FAIL expected hardware A680");return false;}
    VkAndroidSurfaceCreateInfoKHR sci={.sType=VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,.window=window};
    VK(vkCreateAndroidSurfaceKHR(e.instance,&sci,NULL,&e.surface));
    uint32_t nq=0; vkGetPhysicalDeviceQueueFamilyProperties(e.gpu,&nq,NULL);
    VkQueueFamilyProperties *q=calloc(nq,sizeof(*q)); if(!q)return false;
    vkGetPhysicalDeviceQueueFamilyProperties(e.gpu,&nq,q); e.family=UINT32_MAX;
    for(uint32_t i=0;i<nq;i++) { VkBool32 present=0; VkResult r=vkGetPhysicalDeviceSurfaceSupportKHR(e.gpu,i,e.surface,&present);
        if(r==VK_SUCCESS && present && (q[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)){e.family=i;break;} }
    free(q); if(e.family==UINT32_MAX)return false;
    float priority=1; VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=e.family,.queueCount=1,.pQueuePriorities=&priority};
    const char *dx[]={VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi,.enabledExtensionCount=1,.ppEnabledExtensionNames=dx};
    VK(vkCreateDevice(e.gpu,&di,NULL,&e.device)); vkGetDeviceQueue(e.device,e.family,0,&e.queue);
    VkSurfaceCapabilitiesKHR cap; VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(e.gpu,e.surface,&cap));
    uint32_t nf=0; VK(vkGetPhysicalDeviceSurfaceFormatsKHR(e.gpu,e.surface,&nf,NULL));
    if(!nf)return false;
    VkSurfaceFormatKHR *formats=calloc(nf,sizeof(*formats)); if(!formats)return false;
    VK(vkGetPhysicalDeviceSurfaceFormatsKHR(e.gpu,e.surface,&nf,formats));
    VkSurfaceFormatKHR fmt=formats[0];
    for(uint32_t i=0;i<nf;i++) if(formats[i].format==VK_FORMAT_R8G8B8A8_UNORM){fmt=formats[i];break;}
    free(formats); e.extent=cap.currentExtent;
    if(e.extent.width==UINT32_MAX) e.extent=(VkExtent2D){ANativeWindow_getWidth(window),ANativeWindow_getHeight(window)};
    uint32_t count=cap.minImageCount+1; if(cap.maxImageCount && count>cap.maxImageCount)count=cap.maxImageCount;
    VkCompositeAlphaFlagBitsKHR alpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if(!(cap.supportedCompositeAlpha&alpha)) alpha=VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    VkSwapchainCreateInfoKHR sw={.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,.surface=e.surface,.minImageCount=count,.imageFormat=fmt.format,.imageColorSpace=fmt.colorSpace,.imageExtent=e.extent,.imageArrayLayers=1,.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE,.preTransform=cap.currentTransform,.compositeAlpha=alpha,.presentMode=VK_PRESENT_MODE_FIFO_KHR,.clipped=VK_TRUE};
    VK(vkCreateSwapchainKHR(e.device,&sw,NULL,&e.swapchain));
    VK(vkGetSwapchainImagesKHR(e.device,e.swapchain,&e.count,NULL)); if(e.count>8)return false;
    VK(vkGetSwapchainImagesKHR(e.device,e.swapchain,&e.count,e.images));
    VkAttachmentDescription ad={.format=fmt.format,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    VkAttachmentReference ar={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&ar};
    VkSubpassDependency dep={.srcSubpass=VK_SUBPASS_EXTERNAL,.dstSubpass=0,.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    VkRenderPassCreateInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&ad,.subpassCount=1,.pSubpasses=&sub,.dependencyCount=1,.pDependencies=&dep};
    VK(vkCreateRenderPass(e.device,&rp,NULL,&e.pass));
    VkSemaphoreCreateInfo se={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK(vkCreateSemaphore(e.device,&se,NULL,&e.acquire));
    for(uint32_t i=0;i<e.count;i++) {
        VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=e.images[i],.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=fmt.format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        VK(vkCreateImageView(e.device,&vi,NULL,&e.views[i]));
        VkFramebufferCreateInfo fb={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=e.pass,.attachmentCount=1,.pAttachments=&e.views[i],.width=e.extent.width,.height=e.extent.height,.layers=1};
        VK(vkCreateFramebuffer(e.device,&fb,NULL,&e.frames[i])); VK(vkCreateSemaphore(e.device,&se,NULL,&e.done[i]));
    }
    VkShaderModule vert,frag;
    VkShaderModuleCreateInfo sm={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizeof(triangle_vert),.pCode=triangle_vert};
    VK(vkCreateShaderModule(e.device,&sm,NULL,&vert)); sm.codeSize=sizeof(triangle_frag);sm.pCode=triangle_frag; VK(vkCreateShaderModule(e.device,&sm,NULL,&frag));
    VkPipelineShaderStageCreateInfo stages[2]={{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vert,.pName="main"},{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=frag,.pName="main"}};
    VkPipelineVertexInputStateCreateInfo vertex={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport viewport={0,0,(float)e.extent.width,(float)e.extent.height,0,1}; VkRect2D scissor={{0,0},e.extent};
    VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};
    VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState ba={.colorWriteMask=15};
    VkPipelineColorBlendStateCreateInfo blend={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&ba};
    VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; VK(vkCreatePipelineLayout(e.device,&pli,NULL,&e.layout));
    VkGraphicsPipelineCreateInfo gp={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&vertex,.pInputAssemblyState=&assembly,.pViewportState=&vp,.pRasterizationState=&raster,.pMultisampleState=&ms,.pColorBlendState=&blend,.layout=e.layout,.renderPass=e.pass};
    VK(vkCreateGraphicsPipelines(e.device,VK_NULL_HANDLE,1,&gp,NULL,&e.pipeline));
    vkDestroyShaderModule(e.device,vert,NULL);vkDestroyShaderModule(e.device,frag,NULL);
    VkCommandPoolCreateInfo pc={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=e.family};VK(vkCreateCommandPool(e.device,&pc,NULL,&e.pool));
    VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=e.pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VK(vkAllocateCommandBuffers(e.device,&ca,&e.command));
    VkFenceCreateInfo fc={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,.flags=VK_FENCE_CREATE_SIGNALED_BIT};VK(vkCreateFence(e.device,&fc,NULL,&e.fence));
    LOG("READY surface=%ux%u images=%u",e.extent.width,e.extent.height,e.count);return true;
}
static bool draw(void) {
    VK(vkWaitForFences(e.device,1,&e.fence,VK_TRUE,3000000000ull));
    uint32_t i; VkResult r=vkAcquireNextImageKHR(e.device,e.swapchain,3000000000ull,e.acquire,VK_NULL_HANDLE,&i);
    if(r!=VK_SUCCESS && r!=VK_SUBOPTIMAL_KHR){LOG("FAIL acquire=%d",r);return false;}
    VK(vkResetFences(e.device,1,&e.fence)); VK(vkResetCommandBuffer(e.command,0));
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};VK(vkBeginCommandBuffer(e.command,&begin));
    VkClearValue clear={.color={.float32={0.025f,0.045f,0.08f,1}}};
    VkRenderPassBeginInfo rb={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=e.pass,.framebuffer=e.frames[i],.renderArea={{0,0},e.extent},.clearValueCount=1,.pClearValues=&clear};
    vkCmdBeginRenderPass(e.command,&rb,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(e.command,VK_PIPELINE_BIND_POINT_GRAPHICS,e.pipeline);vkCmdDraw(e.command,3,1,0,0);vkCmdEndRenderPass(e.command);VK(vkEndCommandBuffer(e.command));
    VkPipelineStageFlags wait=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=1,.pWaitSemaphores=&e.acquire,.pWaitDstStageMask=&wait,.commandBufferCount=1,.pCommandBuffers=&e.command,.signalSemaphoreCount=1,.pSignalSemaphores=&e.done[i]};VK(vkQueueSubmit(e.queue,1,&si,e.fence));
    VkPresentInfoKHR pi={.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,.waitSemaphoreCount=1,.pWaitSemaphores=&e.done[i],.swapchainCount=1,.pSwapchains=&e.swapchain,.pImageIndices=&i};
    r=vkQueuePresentKHR(e.queue,&pi); if(r!=VK_SUCCESS&&r!=VK_SUBOPTIMAL_KHR){LOG("FAIL present=%d",r);return false;}
    if(++e.rendered==120)LOG("SQ2_VULKAN_WSI_OK frames=120 hardware=A680");return true;
}
static void destroy(void) {
    if(e.device){
        vkDeviceWaitIdle(e.device);
        if(e.fence)vkDestroyFence(e.device,e.fence,NULL);if(e.pool)vkDestroyCommandPool(e.device,e.pool,NULL);
        if(e.pipeline)vkDestroyPipeline(e.device,e.pipeline,NULL);if(e.layout)vkDestroyPipelineLayout(e.device,e.layout,NULL);
        for(uint32_t i=0;i<e.count && i<8;i++){if(e.frames[i])vkDestroyFramebuffer(e.device,e.frames[i],NULL);if(e.views[i])vkDestroyImageView(e.device,e.views[i],NULL);if(e.done[i])vkDestroySemaphore(e.device,e.done[i],NULL);}
        if(e.acquire)vkDestroySemaphore(e.device,e.acquire,NULL);if(e.pass)vkDestroyRenderPass(e.device,e.pass,NULL);
        if(e.swapchain)vkDestroySwapchainKHR(e.device,e.swapchain,NULL);vkDestroyDevice(e.device,NULL);
    }
    if(e.surface)vkDestroySurfaceKHR(e.instance,e.surface,NULL);if(e.instance)vkDestroyInstance(e.instance,NULL);
    memset(&e,0,sizeof(e));
}
static void command(struct android_app *app,int32_t cmd){
    if(cmd==APP_CMD_INIT_WINDOW && app->window){destroy();e.ready=initialize(app->window);}
    if(cmd==APP_CMD_TERM_WINDOW)destroy();
}
void android_main(struct android_app *app){
    app->onAppCmd=command;
    while(!app->destroyRequested){
        int events;struct android_poll_source *source=NULL;
        int id=ALooper_pollOnce(e.ready&&e.rendered<120?0:-1,NULL,&events,(void**)&source);
        if(id>=0&&source)source->process(app,source);
        if(e.ready&&e.rendered<120)e.ready=draw();
    }
    destroy();
}
