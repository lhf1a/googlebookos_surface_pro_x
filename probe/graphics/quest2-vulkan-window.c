/* System Vulkan -> Android surface/swapchain -> real ImageReader consumer.
 * Private lab namespace only. No SurfaceFlinger or installed-driver changes.
 * --create-only creates and destroys a swapchain with zero queue submissions.
 */
#define _GNU_SOURCE
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define INSTANCE_FUNCTIONS(X) \
 X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) \
 X(GetPhysicalDeviceQueueFamilyProperties) X(EnumerateDeviceExtensionProperties) \
 X(CreateAndroidSurfaceKHR) X(DestroySurfaceKHR) X(GetPhysicalDeviceSurfaceSupportKHR) \
 X(GetPhysicalDeviceSurfaceCapabilitiesKHR) X(GetPhysicalDeviceSurfaceFormatsKHR) \
 X(GetPhysicalDeviceSurfacePresentModesKHR) X(CreateDevice) X(GetDeviceProcAddr)
#define DEVICE_FUNCTIONS(X) \
 X(DestroyDevice) X(GetDeviceQueue) X(DeviceWaitIdle) X(CreateSwapchainKHR) X(DestroySwapchainKHR) \
 X(GetSwapchainImagesKHR) X(AcquireNextImageKHR) X(QueuePresentKHR) X(QueueSubmit) \
 X(CreateImageView) X(DestroyImageView) X(CreateRenderPass) X(DestroyRenderPass) \
 X(CreateFramebuffer) X(DestroyFramebuffer) X(CreateCommandPool) X(DestroyCommandPool) \
 X(AllocateCommandBuffers) X(ResetCommandBuffer) X(BeginCommandBuffer) X(EndCommandBuffer) \
 X(CmdBeginRenderPass) X(CmdClearAttachments) X(CmdEndRenderPass) \
 X(CreateSemaphore) X(DestroySemaphore) X(CreateFence) X(DestroyFence) X(WaitForFences) X(ResetFences)
#define DECLARE(n) static PFN_vk##n vk##n;
INSTANCE_FUNCTIONS(DECLARE)
DEVICE_FUNCTIONS(DECLARE)
#undef DECLARE
static unsigned submissions, presents, consumed, native_fences;
typedef unsigned (*native_stats_fn)(uint64_t *);
static native_stats_fn native_stats;

#define REQUIRE(condition, message) do { if(!(condition)) { \
    fprintf(stderr,"VULKAN_WINDOW_FAIL %s\n",message); goto cleanup; } } while(0)
#define VK_REQUIRE(expression) do { VkResult checked=(expression); if(checked!=VK_SUCCESS) { \
    fprintf(stderr,"VULKAN_WINDOW_FAIL %s result=%d\n",#expression,checked); goto cleanup; } } while(0)
#define VK_CREATE(expression, output) do { VkResult checked=(expression); if(checked!=VK_SUCCESS) { \
    (output)=VK_NULL_HANDLE; \
    fprintf(stderr,"VULKAN_WINDOW_FAIL %s result=%d\n",#expression,checked); goto cleanup; } } while(0)

static unsigned option(const char *name,unsigned fallback,unsigned maximum) {
    const char *text=getenv(name);if(!text)return fallback;
    char *end=NULL;errno=0;unsigned long value=strtoul(text,&end,10);
    if(errno||!*text||*end||*text=='-'||value<1||value>maximum) {
        fprintf(stderr,"Invalid %s (expected 1..%u)\n",name,maximum);exit(2);
    }
    return (unsigned)value;
}
static int extension(const VkExtensionProperties *values,uint32_t count,const char *name) {
    for(uint32_t i=0;i<count;i++)if(!strcmp(values[i].extensionName,name))return 1;
    return 0;
}
static void edges(unsigned size,uint32_t out[5]) {
    out[0]=0;out[4]=size;
    for(unsigned i=1;i<4;i++) {
        uint32_t value=(uint64_t)size*i/4;
        if(value&&value<size&&!(value&1u))value++;
        out[i]=value;
    }
}
static unsigned cell(unsigned position,const uint32_t bounds[5]) {
    unsigned n=0;while(n<3&&position>=bounds[n+1])n++;return n;
}
static unsigned code(unsigned column,unsigned row,unsigned frame,unsigned cycle) {
    return (row*4+column)^((frame+cycle*7u)&15u);
}
static int wait_native_fence(int fd) {
    if(fd<0)return 0;
    struct pollfd pending={.fd=fd,.events=POLLIN};int result;
    do {result=poll(&pending,1,3000);} while(result<0&&errno==EINTR);
    close(fd);
    if(result!=1||!(pending.revents&POLLIN))return 1;
    native_fences++;return 0;
}
static int consume(AImageReader *reader,unsigned width,unsigned height,unsigned cycle,unsigned frame) {
    AImage *image=NULL;int fence=-1,result=1;
    media_status_t acquired=AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE;
    for(unsigned attempt=0;attempt<300&&acquired==AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE;attempt++) {
        acquired=AImageReader_acquireNextImageAsync(reader,&image,&fence);
        if(acquired==AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE)usleep(10000);
    }
    REQUIRE(acquired==AMEDIA_OK&&image,"acquire ImageReader image");
    {int fd=fence;fence=-1;if(wait_native_fence(fd)) {
        fputs("VULKAN_WINDOW_INFLIGHT_FAIL producer fence; no unsafe image release\n",stderr);_Exit(1);
    }}
    int32_t w=0,h=0,planes=0,row_stride=0,pixel_stride=0,length=0,format=0;
    uint8_t *data=NULL;AHardwareBuffer *buffer=NULL;uint64_t id=0;
    REQUIRE(AImage_getWidth(image,&w)==AMEDIA_OK&&AImage_getHeight(image,&h)==AMEDIA_OK&&
        AImage_getFormat(image,&format)==AMEDIA_OK&&AImage_getNumberOfPlanes(image,&planes)==AMEDIA_OK&&
        AImage_getPlaneRowStride(image,0,&row_stride)==AMEDIA_OK&&
        AImage_getPlanePixelStride(image,0,&pixel_stride)==AMEDIA_OK&&
        AImage_getPlaneData(image,0,&data,&length)==AMEDIA_OK&&
        AImage_getHardwareBuffer(image,&buffer)==AMEDIA_OK&&buffer&&
        AHardwareBuffer_getId(buffer,&id)==0,"ImageReader metadata");
    REQUIRE(w==(int32_t)width&&h==(int32_t)height&&format==AIMAGE_FORMAT_RGBA_8888&&
        planes==1&&pixel_stride==4&&row_stride>0&&(unsigned)row_stride>=width*4&&data&&length>0&&
        (uint64_t)(height-1)*(unsigned)row_stride+(uint64_t)width*4<=(uint64_t)length,
        "ImageReader RGBA layout/bounds");
    AHardwareBuffer_Desc description={0};AHardwareBuffer_describe(buffer,&description);
    REQUIRE(description.width==width&&description.height==height&&description.layers==1&&
        description.format==AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM&&description.stride>=width&&
        (uint64_t)description.stride*4==(uint64_t)row_stride,"AHB/consumer stride agreement");
    uint32_t xb[5],yb[5];edges(width,xb);edges(height,yb);unsigned bad=0;
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++) {
        unsigned expected_code=code(cell(x,xb),cell(y,yb),frame,cycle);
        const uint8_t *pixel=data+(size_t)y*(unsigned)row_stride+(size_t)x*4;
        uint8_t expected[4];int mismatch=0;
        for(unsigned c=0;c<4;c++){
            unsigned on=(expected_code>>c)&1u;
            expected[c]=on*((getenv("SQ2_VULKAN_WINDOW_SRGB")&&c<3)?137u:255u);
            int delta=(int)pixel[c]-expected[c];
            mismatch|=getenv("SQ2_VULKAN_WINDOW_SRGB")&&c<3 ? (delta < -1 || delta > 1) : delta!=0;
        }
        if(mismatch) {
            if(bad<8)fprintf(stderr,"VULKAN_WINDOW_MISMATCH cycle=%u frame=%u x=%u y=%u got=%02x,%02x,%02x,%02x expected=%02x,%02x,%02x,%02x\n",
                cycle,frame,x,y,pixel[0],pixel[1],pixel[2],pixel[3],expected[0],expected[1],expected[2],expected[3]);
            bad++;
        }
    }
    printf("VULKAN_WINDOW_CONSUMER cycle=%u frame=%u buffer=%llu row_stride=%d pixel_stride=%d checked=%llu bad=%u\n",
        cycle,frame,(unsigned long long)id,row_stride,pixel_stride,(unsigned long long)width*height,bad);
    REQUIRE(!bad,"consumer exact RGBA grid");
    consumed++;result=0;
cleanup:
    if(fence>=0)close(fence);
    if(image)AImage_delete(image);
    return result;
}

static int run_cycle(VkInstance instance,VkPhysicalDevice physical,VkDevice device,VkQueue queue,
                      uint32_t family,unsigned width,unsigned height,unsigned frames,unsigned cycle,int create_only) {
    AImageReader *reader=NULL;ANativeWindow *window=NULL;
    VkSurfaceKHR surface=VK_NULL_HANDLE;VkSwapchainKHR swapchain=VK_NULL_HANDLE;
    VkRenderPass pass=VK_NULL_HANDLE;VkCommandPool pool=VK_NULL_HANDLE;
    VkCommandBuffer command=VK_NULL_HANDLE;VkSemaphore acquired=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;
    VkImage *images=NULL;VkImageView *views=NULL;VkFramebuffer *framebuffers=NULL;VkSemaphore *rendered=NULL;
    VkSurfaceFormatKHR *formats=NULL;VkPresentModeKHR *modes=NULL;
    uint32_t image_count=0,resource_count=0;int result=1,queue_touched=0,acquire_pending=0;
    unsigned before_submissions=submissions,before_presents=presents;
    uint64_t usage=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;
    REQUIRE(AImageReader_newWithUsage((int32_t)width,(int32_t)height,AIMAGE_FORMAT_RGBA_8888,usage,4,&reader)==AMEDIA_OK&&
        reader&&AImageReader_getWindow(reader,&window)==AMEDIA_OK&&window,"ImageReader window creation");
    VkAndroidSurfaceCreateInfoKHR surface_info={.sType=VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,.window=window};
    VK_CREATE(vkCreateAndroidSurfaceKHR(instance,&surface_info,NULL,&surface),surface);
    VkBool32 supported=VK_FALSE;
    VK_REQUIRE(vkGetPhysicalDeviceSurfaceSupportKHR(physical,family,surface,&supported));
    REQUIRE(supported,"selected graphics queue does not support this Android surface");
    VkSurfaceCapabilitiesKHR capabilities;
    VK_REQUIRE(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&capabilities));
    REQUIRE((capabilities.supportedUsageFlags&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)&&
        (capabilities.supportedTransforms&VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)&&
        width>=capabilities.minImageExtent.width&&width<=capabilities.maxImageExtent.width&&
        height>=capabilities.minImageExtent.height&&height<=capabilities.maxImageExtent.height&&
        (capabilities.currentExtent.width==UINT32_MAX||
         (capabilities.currentExtent.width==width&&capabilities.currentExtent.height==height)),
        "surface color attachment, identity transform or requested extent unsupported");
    uint32_t count=0;
    VK_REQUIRE(vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&count,NULL));
    REQUIRE(count&&count<=256,"surface format count");
    formats=calloc(count,sizeof(*formats));REQUIRE(formats,"allocate surface formats");
    VK_REQUIRE(vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&count,formats));
    VkSurfaceFormatKHR format={VK_FORMAT_UNDEFINED,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    VkFormat requested_format=getenv("SQ2_VULKAN_WINDOW_SRGB")?VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_R8G8B8A8_UNORM;
    for(uint32_t i=0;i<count;i++)if((formats[i].format==requested_format||
        (count==1&&formats[i].format==VK_FORMAT_UNDEFINED))&&formats[i].colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        format=(VkSurfaceFormatKHR){requested_format,formats[i].colorSpace};
    free(formats);formats=NULL;REQUIRE(format.format!=VK_FORMAT_UNDEFINED,"requested RGBA8 / SRGB nonlinear surface format");
    fprintf(stderr,"VULKAN_WINDOW_FORMAT=%d\n",requested_format);
    count=0;VK_REQUIRE(vkGetPhysicalDeviceSurfacePresentModesKHR(physical,surface,&count,NULL));
    REQUIRE(count&&count<=256,"present mode count");
    modes=calloc(count,sizeof(*modes));REQUIRE(modes,"allocate present modes");
    VK_REQUIRE(vkGetPhysicalDeviceSurfacePresentModesKHR(physical,surface,&count,modes));
    int fifo=0;for(uint32_t i=0;i<count;i++)fifo|=modes[i]==VK_PRESENT_MODE_FIFO_KHR;
    free(modes);modes=NULL;REQUIRE(fifo,"FIFO present mode");
    VkCompositeAlphaFlagBitsKHR alpha=0;
    const VkCompositeAlphaFlagBitsKHR alpha_modes[]={VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR};
    for(unsigned i=0;i<4&&!alpha;i++)if(capabilities.supportedCompositeAlpha&alpha_modes[i])alpha=alpha_modes[i];
    REQUIRE(alpha,"surface composite alpha");
    uint32_t minimum=capabilities.minImageCount;
    REQUIRE(minimum&&minimum<64,"surface minimum image count");
    minimum++;if(capabilities.maxImageCount&&minimum>capabilities.maxImageCount)minimum=capabilities.maxImageCount;
    VkSwapchainCreateInfoKHR swapchain_info={.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,.surface=surface,
        .minImageCount=minimum,.imageFormat=format.format,.imageColorSpace=format.colorSpace,.imageExtent={width,height},
        .imageArrayLayers=1,.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE,
        .preTransform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,.compositeAlpha=alpha,.presentMode=VK_PRESENT_MODE_FIFO_KHR,
        .clipped=VK_TRUE};
    printf("VULKAN_WINDOW_CREATE cycle=%u size=%ux%u minimum_images=%u alpha=%u create_only=%d\n",
        cycle,width,height,minimum,(unsigned)alpha,create_only);
    VK_CREATE(vkCreateSwapchainKHR(device,&swapchain_info,NULL,&swapchain),swapchain);
    VK_REQUIRE(vkGetSwapchainImagesKHR(device,swapchain,&image_count,NULL));
    REQUIRE(image_count&&image_count<=64,"swapchain image count");
    images=calloc(image_count,sizeof(*images));REQUIRE(images,"allocate swapchain image list");
    {uint32_t capacity=image_count;VK_REQUIRE(vkGetSwapchainImagesKHR(device,swapchain,&image_count,images));
     REQUIRE(image_count&&image_count<=capacity,"stable swapchain image count");}
    resource_count=image_count;
    printf("VULKAN_WINDOW_SWAPCHAIN_OK cycle=%u images=%u\n",cycle,image_count);
    if(create_only){result=0;goto cleanup;}
    views=calloc(image_count,sizeof(*views));framebuffers=calloc(image_count,sizeof(*framebuffers));
    rendered=calloc(image_count,sizeof(*rendered));REQUIRE(views&&framebuffers&&rendered,"allocate per-image resources");
    VkAttachmentDescription attachment={.format=format.format,.samples=VK_SAMPLE_COUNT_1_BIT,
        .loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    VkAttachmentReference reference={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount=1,.pColorAttachments=&reference};
    VkSubpassDependency dependencies[2]={
        {.srcSubpass=VK_SUBPASS_EXTERNAL,.dstSubpass=0,.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         .dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
        {.srcSubpass=0,.dstSubpass=VK_SUBPASS_EXTERNAL,.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         .dstStageMask=VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT}};
    VkRenderPassCreateInfo pass_info={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,
        .pAttachments=&attachment,.subpassCount=1,.pSubpasses=&subpass,.dependencyCount=2,.pDependencies=dependencies};
    VK_CREATE(vkCreateRenderPass(device,&pass_info,NULL,&pass),pass);
    VkSemaphoreCreateInfo semaphore_info={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for(uint32_t i=0;i<image_count;i++) {
        VkImageViewCreateInfo view_info={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=images[i],
            .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format.format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        VK_CREATE(vkCreateImageView(device,&view_info,NULL,&views[i]),views[i]);
        VkFramebufferCreateInfo framebuffer_info={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,
            .attachmentCount=1,.pAttachments=&views[i],.width=width,.height=height,.layers=1};
        VK_CREATE(vkCreateFramebuffer(device,&framebuffer_info,NULL,&framebuffers[i]),framebuffers[i]);
        VK_CREATE(vkCreateSemaphore(device,&semaphore_info,NULL,&rendered[i]),rendered[i]);
    }
    VK_CREATE(vkCreateSemaphore(device,&semaphore_info,NULL,&acquired),acquired);
    VkFenceCreateInfo fence_info={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CREATE(vkCreateFence(device,&fence_info,NULL,&fence),fence);
    VkCommandPoolCreateInfo pool_info={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};
    VK_CREATE(vkCreateCommandPool(device,&pool_info,NULL,&pool),pool);
    VkCommandBufferAllocateInfo command_info={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VK_REQUIRE(vkAllocateCommandBuffers(device,&command_info,&command));
    uint32_t xb[5],yb[5];edges(width,xb);edges(height,yb);
    printf("VULKAN_WINDOW_GRID x=%u,%u,%u,%u,%u y=%u,%u,%u,%u,%u rgba_bits=0,1,2,3\n",
        xb[0],xb[1],xb[2],xb[3],xb[4],yb[0],yb[1],yb[2],yb[3],yb[4]);
    for(unsigned frame=0;frame<frames;frame++) {
        uint32_t index=UINT32_MAX;
        VkResult status=vkAcquireNextImageKHR(device,swapchain,10000000000ull,acquired,VK_NULL_HANDLE,&index);
        if(status==VK_SUCCESS||status==VK_SUBOPTIMAL_KHR)acquire_pending=1;
        REQUIRE(acquire_pending&&index<image_count,"acquire swapchain image");
        queue_touched=1; /* acquire may signal its semaphore asynchronously. */
        VK_REQUIRE(vkResetCommandBuffer(command,0));
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK_REQUIRE(vkBeginCommandBuffer(command,&begin));
        VkClearValue background={.color={.float32={0,0,0,0}}};
        VkRenderPassBeginInfo render={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,
            .framebuffer=framebuffers[index],.renderArea={{0,0},{width,height}},.clearValueCount=1,.pClearValues=&background};
        vkCmdBeginRenderPass(command,&render,VK_SUBPASS_CONTENTS_INLINE);
        for(unsigned row=0;row<4;row++)for(unsigned column=0;column<4;column++) {
            unsigned w=xb[column+1]-xb[column],h=yb[row+1]-yb[row];if(!w||!h)continue;
            unsigned color=code(column,row,frame,cycle);
            VkClearAttachment clear={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.colorAttachment=0};
            for(unsigned c=0;c<4;c++)clear.clearValue.color.float32[c]=((color>>c)&1u)*
                ((getenv("SQ2_VULKAN_WINDOW_SRGB")&&c<3)?0.25f:1.0f);
            VkClearRect rect={.rect={{(int32_t)xb[column],(int32_t)yb[row]},{w,h}},.baseArrayLayer=0,.layerCount=1};
            vkCmdClearAttachments(command,1,&clear,1,&rect);
        }
        vkCmdEndRenderPass(command);VK_REQUIRE(vkEndCommandBuffer(command));
        VkPipelineStageFlags wait_stage=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=1,.pWaitSemaphores=&acquired,
            .pWaitDstStageMask=&wait_stage,.commandBufferCount=1,.pCommandBuffers=&command,
            .signalSemaphoreCount=1,.pSignalSemaphores=&rendered[index]};
        status=vkQueueSubmit(queue,1,&submit,fence);submissions++;
        if(status!=VK_SUCCESS){fprintf(stderr,"VULKAN_WINDOW_INFLIGHT_FAIL submit=%d; no unsafe teardown\n",status);_Exit(1);}
        acquire_pending=0; /* The queue now owns the semaphore wait. */
        VkResult per_swapchain=VK_SUCCESS;
        VkPresentInfoKHR present={.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,.waitSemaphoreCount=1,
            .pWaitSemaphores=&rendered[index],.swapchainCount=1,.pSwapchains=&swapchain,
            .pImageIndices=&index,.pResults=&per_swapchain};
        status=vkQueuePresentKHR(queue,&present);presents++;
        REQUIRE((status==VK_SUCCESS||status==VK_SUBOPTIMAL_KHR)&&
            (per_swapchain==VK_SUCCESS||per_swapchain==VK_SUBOPTIMAL_KHR),"present swapchain image");
        status=vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull);
        if(status!=VK_SUCCESS){fprintf(stderr,"VULKAN_WINDOW_INFLIGHT_FAIL fence=%d; no unsafe teardown\n",status);_Exit(1);}
        REQUIRE(!consume(reader,width,height,cycle,frame),"consume presented frame");
        VK_REQUIRE(vkResetFences(device,1,&fence));
        printf("VULKAN_WINDOW_FRAME_OK cycle=%u frame=%u swapchain_image=%u\n",cycle,frame,index);
    }
    result=0;
cleanup:
    if(acquire_pending) {
        /* A successful WSI acquire may still be signaling its semaphore;
         * DeviceWaitIdle only covers queues. Do not destroy that semaphore
         * after an intervening command-recording failure without a real wait. */
        fputs("VULKAN_WINDOW_INFLIGHT_FAIL unconsumed acquire; no unsafe teardown\n",stderr);_Exit(1);
    }
    if(queue_touched) {
        VkResult idle=vkDeviceWaitIdle(device);
        if(idle!=VK_SUCCESS){fprintf(stderr,"VULKAN_WINDOW_INFLIGHT_FAIL idle=%d; no unsafe teardown\n",idle);_Exit(1);}
    }
    if(pool)vkDestroyCommandPool(device,pool,NULL);
    if(fence)vkDestroyFence(device,fence,NULL);
    if(acquired)vkDestroySemaphore(device,acquired,NULL);
    for(uint32_t i=resource_count;i>0;i--) {
        if(rendered&&rendered[i-1])vkDestroySemaphore(device,rendered[i-1],NULL);
        if(framebuffers&&framebuffers[i-1])vkDestroyFramebuffer(device,framebuffers[i-1],NULL);
        if(views&&views[i-1])vkDestroyImageView(device,views[i-1],NULL);
    }
    if(pass)vkDestroyRenderPass(device,pass,NULL);
    if(swapchain)vkDestroySwapchainKHR(device,swapchain,NULL); /* Owns VkImages. */
    if(native_stats) {
        uint64_t bytes=0;unsigned live=native_stats(&bytes);
        printf("VULKAN_WINDOW_NATIVE_STATS cycle=%u views=%u bytes=%llu\n",cycle,live,(unsigned long long)bytes);
        if(live||bytes){fprintf(stderr,"VULKAN_WINDOW_FAIL native view leak after swapchain teardown\n");result=1;}
    } else printf("VULKAN_WINDOW_NATIVE_STATS cycle=%u visibility=unavailable verification=wrapper-log-required\n",cycle);
    if(surface)vkDestroySurfaceKHR(instance,surface,NULL);
    if(reader)AImageReader_delete(reader); /* getWindow returns a borrowed ref. */
    free(rendered);free(framebuffers);free(views);free(images);free(formats);free(modes);
    if(create_only&&(submissions!=before_submissions||presents!=before_presents))result=1;
    printf("VULKAN_WINDOW_CYCLE_%s cycle=%u submits=%u presents=%u create_only=%d\n",result?"FAIL":"OK",
        cycle,submissions-before_submissions,presents-before_presents,create_only);
    return result;
}

#ifdef SQ2_COMBINED_HARNESS
extern int quest2_composition_c_entry(void);
extern int quest2_combined_audit(void);
#endif
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IOLBF,0);
    int create_only=argc==2&&!strcmp(argv[1],"--create-only");
    if(argc!=1&&!create_only){fprintf(stderr,"usage: quest2-vulkan-window [--create-only]\n");return 2;}
    unsigned width=option("SQ2_VULKAN_WINDOW_WIDTH",64,4096),height=option("SQ2_VULKAN_WINDOW_HEIGHT",64,4096);
    unsigned frames=option("SQ2_VULKAN_WINDOW_FRAMES",12,300),cycles=option("SQ2_VULKAN_WINDOW_CYCLES",1,150);
    void *library=NULL,*stats_library=NULL;VkInstance instance=VK_NULL_HANDLE;VkDevice device=VK_NULL_HANDLE;
    VkExtensionProperties *extensions=NULL;VkQueueFamilyProperties *families=NULL;
    VkPhysicalDevice *physical_devices=NULL;int result=1;
    library=dlopen("/system/lib64/libvulkan.so",RTLD_NOW|RTLD_LOCAL);REQUIRE(library,"system libvulkan load");
    PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(library,"vkGetInstanceProcAddr");
    REQUIRE(gipa,"system vkGetInstanceProcAddr");
    Dl_info origin={0};REQUIRE(dladdr((void*)gipa,&origin)&&origin.dli_fname&&
        !strcmp(origin.dli_fname,"/system/lib64/libvulkan.so"),"system Vulkan entry provenance");
    printf("VULKAN_WINDOW_LOADER=%s\n",origin.dli_fname);
    PFN_vkEnumerateInstanceExtensionProperties enumerate_extensions=
        (PFN_vkEnumerateInstanceExtensionProperties)gipa(VK_NULL_HANDLE,"vkEnumerateInstanceExtensionProperties");
    PFN_vkCreateInstance create_instance=(PFN_vkCreateInstance)gipa(VK_NULL_HANDLE,"vkCreateInstance");
    REQUIRE(enumerate_extensions&&create_instance,"global Vulkan entrypoints");
    uint32_t count=0;VK_REQUIRE(enumerate_extensions(NULL,&count,NULL));
    REQUIRE(count&&count<=1024,"instance extension count");extensions=calloc(count,sizeof(*extensions));REQUIRE(extensions,"extension allocation");
    VK_REQUIRE(enumerate_extensions(NULL,&count,extensions));
    REQUIRE(extension(extensions,count,VK_KHR_SURFACE_EXTENSION_NAME)&&
        extension(extensions,count,VK_KHR_ANDROID_SURFACE_EXTENSION_NAME),"Android surface extensions");
    free(extensions);extensions=NULL;
    const char *instance_extensions[]={VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    VkApplicationInfo application={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="SQ2 Vulkan window probe",.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo instance_info={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&application,
        .enabledExtensionCount=2,.ppEnabledExtensionNames=instance_extensions};
    VK_CREATE(create_instance(&instance_info,NULL,&instance),instance);
#define LOAD_INSTANCE(n) vk##n=(PFN_vk##n)gipa(instance,"vk" #n);REQUIRE(vk##n,"missing vk" #n);
    INSTANCE_FUNCTIONS(LOAD_INSTANCE)
#undef LOAD_INSTANCE
    count=0;VK_REQUIRE(vkEnumeratePhysicalDevices(instance,&count,NULL));
    REQUIRE(count&&count<=32,"physical device count");physical_devices=calloc(count,sizeof(*physical_devices));REQUIRE(physical_devices,"physical list");
    VK_REQUIRE(vkEnumeratePhysicalDevices(instance,&count,physical_devices));
    VkPhysicalDevice physical=VK_NULL_HANDLE;
    for(uint32_t i=0;i<count;i++) {
        VkPhysicalDeviceProperties p;vkGetPhysicalDeviceProperties(physical_devices[i],&p);
        printf("VULKAN_WINDOW_GPU name=%s vendor=%x device=%x driver=%u api=%u\n",p.deviceName,p.vendorID,p.deviceID,p.driverVersion,p.apiVersion);
        if(p.vendorID==0x5143&&p.deviceType==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU&&strstr(p.deviceName,"680")) {
            REQUIRE(!getenv("SQ2_VULKAN_REJECT_TURNIP")||!strstr(p.deviceName,"Turnip"),"unexpected Mesa/Turnip fallback");physical=physical_devices[i];break;
        }
    }
    free(physical_devices);physical_devices=NULL;REQUIRE(physical,"SQ2 hardware Adreno 680");
    count=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,NULL);REQUIRE(count&&count<=256,"queue family count");
    families=calloc(count,sizeof(*families));REQUIRE(families,"queue family allocation");
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families);uint32_t family=UINT32_MAX;
    for(uint32_t i=0;i<count;i++)if(families[i].queueCount&&(families[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)){family=i;break;}
    free(families);families=NULL;REQUIRE(family!=UINT32_MAX,"graphics queue");
    count=0;VK_REQUIRE(vkEnumerateDeviceExtensionProperties(physical,NULL,&count,NULL));
    REQUIRE(count&&count<=1024,"device extension count");extensions=calloc(count,sizeof(*extensions));REQUIRE(extensions,"device extensions");
    VK_REQUIRE(vkEnumerateDeviceExtensionProperties(physical,NULL,&count,extensions));
    REQUIRE(extension(extensions,count,VK_KHR_SWAPCHAIN_EXTENSION_NAME),"swapchain extension");free(extensions);extensions=NULL;
    float priority=1.f;VkDeviceQueueCreateInfo queue_info={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    const char *device_extension=VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo device_info={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,
        .pQueueCreateInfos=&queue_info,.enabledExtensionCount=1,.ppEnabledExtensionNames=&device_extension};
    VK_CREATE(vkCreateDevice(physical,&device_info,NULL,&device),device);
#define LOAD_DEVICE(n) vk##n=(PFN_vk##n)vkGetDeviceProcAddr(device,"vk" #n);REQUIRE(vk##n,"missing vk" #n);
    DEVICE_FUNCTIONS(LOAD_DEVICE)
#undef LOAD_DEVICE
    /* NOLOAD avoids silently constructing a second registry in another linker
     * namespace. Missing visibility is a failed diagnostic, never a fake zero. */
    stats_library=dlopen("/vendor/lib64/egl/libquest2-v77-ahb.so",RTLD_NOW|RTLD_LOCAL|RTLD_NOLOAD);
    if(stats_library)native_stats=(native_stats_fn)dlsym(stats_library,"sq2_v77_native_stats");
    if(native_stats) {
        REQUIRE(dladdr((void*)native_stats,&origin),"live native-view stats entry provenance");
        printf("VULKAN_WINDOW_STATS visibility=probe-visible module=%s base=%p\n",origin.dli_fname,origin.dli_fbase);
        uint64_t bytes=0;REQUIRE(!native_stats(&bytes)&&!bytes,"native-view registry not empty before test");
    } else puts("VULKAN_WINDOW_STATS visibility=unavailable verification=wrapper-log-required (no second adapter loaded)");
    VkQueue queue=VK_NULL_HANDLE;vkGetDeviceQueue(device,family,0,&queue);REQUIRE(queue,"device queue");
#ifdef SQ2_COMBINED_HARNESS
    if(!strcmp(getenv("SQ2_COMBINED_ORDER"),"egl-first")) REQUIRE(!quest2_combined_audit(),"combined binding audit");
    if(!strcmp(getenv("SQ2_COMBINED_ORDER"),"vulkan-first")) {
        puts("COMBINED_OUTER_LIVE api=Vulkan nested=EGL");
        REQUIRE(!quest2_composition_c_entry(),"nested GLES composition");
        puts("COMBINED_OUTER_RESUME api=Vulkan");
    }
#endif
    for(unsigned cycle=0;cycle<cycles;cycle++)if(run_cycle(instance,physical,device,queue,family,width,height,frames,cycle,create_only))goto cleanup;
    result=0;
cleanup:
    if(device&&vkDestroyDevice)vkDestroyDevice(device,NULL);
    if(instance&&vkDestroyInstance)vkDestroyInstance(instance,NULL);
    if(stats_library)dlclose(stats_library);
    if(library)dlclose(library);
    free(extensions);free(families);free(physical_devices);
    printf("VULKAN_WINDOW_%s mode=%s cycles=%u frames_consumed=%u submits=%u presents=%u native_fences=%u native_stats=%s\n",
        result?"FAIL":"PASS",create_only?"create-only":"render-present-readback",cycles,consumed,submissions,presents,native_fences,
        native_stats?"probe-visible":"wrapper-log-required");
    return result;
}
