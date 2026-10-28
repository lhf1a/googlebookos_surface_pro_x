/* Isolated Vulkan hardware probe: real shader dispatch and optimal-image readback.
 * Takes a driver or system loader path; requires SQ2's current A680 driver ID.
 * SQ2's marketed GPU is Adreno 690; the board DT identifies adreno-680.1.
 * No display surfaces, global properties, or boot configuration are changed.
 */
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <hardware/hwvulkan.h>
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include "vulkan-compute.h"
#include "vulkan-raster-vert.h"
#include "vulkan-raster-frag.h"

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { \
    fprintf(stderr, "%s: VkResult=%d\n", #x, r_); exit(1); } } while (0)
#define FUNCS(X) \
 X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) \
 X(GetPhysicalDeviceQueueFamilyProperties) X(GetPhysicalDeviceMemoryProperties) \
 X(EnumerateDeviceExtensionProperties) X(CreateDevice) X(DestroyDevice) X(GetDeviceQueue) \
 X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) \
 X(FreeMemory) X(BindBufferMemory) X(MapMemory) X(UnmapMemory) X(CreateShaderModule) \
 X(DestroyShaderModule) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) \
 X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateComputePipelines) X(DestroyPipeline) \
 X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
 X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(ResetCommandBuffer) \
 X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdBindPipeline) X(CmdBindDescriptorSets) \
 X(CmdPushConstants) X(CmdDispatch) X(CmdPipelineBarrier) X(QueueSubmit) \
 X(CreateFence) X(DestroyFence) X(ResetFences) X(WaitForFences) \
 X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(BindImageMemory) \
 X(CmdClearColorImage) X(CmdCopyImageToBuffer) \
 X(CreateRenderPass) X(DestroyRenderPass) X(CreateImageView) X(DestroyImageView) \
 X(CreateFramebuffer) X(DestroyFramebuffer) X(CreateGraphicsPipelines) \
 X(CmdBeginRenderPass) X(CmdEndRenderPass) X(CmdClearAttachments) X(CmdDraw)
#define DECL(n) static PFN_vk##n vk##n;
FUNCS(DECL)
static VkPhysicalDeviceMemoryProperties memory;
static unsigned queue_submissions;
static uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
    for (uint32_t i=0; i<memory.memoryTypeCount; ++i)
        if ((bits & (1u<<i)) && (memory.memoryTypes[i].propertyFlags & flags)==flags) return i;
    fprintf(stderr,"No suitable memory type\n"); exit(1);
}
static void submit(VkDevice dev, VkQueue queue, VkCommandBuffer cb, VkFence fence) {
    CHECK(vkEndCommandBuffer(cb));
    VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cb};
    queue_submissions++;
    CHECK(vkQueueSubmit(queue,1,&si,fence));
    CHECK(vkWaitForFences(dev,1,&fence,VK_TRUE,10000000000ull));
    CHECK(vkResetFences(dev,1,&fence));
}
#include "vulkan-raster-readback.h"
#include "vulkan-ahb-readback.h"
int main(int argc,char **argv) {
    int offscreen_only=0,mode_set=0,cycles_set=0,result=0;
    enum ahb_probe_mode ahb_mode=AHB_FULL;
    unsigned ahb_cycles=1;
    if(argc<2) goto usage;
    for(int arg=2;arg<argc;arg++) {
        if(!strcmp(argv[arg],"--offscreen-only")) {
            if(mode_set) goto usage;
            offscreen_only=1;mode_set=1;
        } else if(!strcmp(argv[arg],"--ahb-properties-only") || !strcmp(argv[arg],"--ahb-import-only")) {
            if(mode_set) goto usage;
            ahb_mode=!strcmp(argv[arg],"--ahb-properties-only")?AHB_PROPERTIES_ONLY:AHB_IMPORT_ONLY;
            mode_set=1;
        } else if(!strcmp(argv[arg],"--ahb-cycles")) {
            if(cycles_set || ++arg>=argc || !argv[arg][0]) goto usage;
            unsigned value=0;
            for(const char *p=argv[arg];*p;p++) {
                if(*p<'0' || *p>'9' || value>150) goto usage;
                value=value*10+(unsigned)(*p-'0');
            }
            if(!value || value>150) goto usage;
            ahb_cycles=value;cycles_set=1;
        } else goto usage;
    }
    if(offscreen_only && cycles_set) goto usage;
    goto parsed;
usage:
    fprintf(stderr,"usage: vulkan-readback DRIVER_OR_LOADER.so [--offscreen-only | --ahb-properties-only | --ahb-import-only] [--ahb-cycles 1..150]\n");
    return 2;
parsed:
    setvbuf(stdout,NULL,_IOLBF,0);
    /* Optional bounded ALU workload for frequency validation. Defaults retain
     * the original small probe. CPU expectation uses a composed affine map. */
    const char *seconds_env=getenv("SQ2_VULKAN_SECONDS");
    const char *iterations_env=getenv("SQ2_VULKAN_ITERATIONS");
    unsigned seconds=seconds_env ? (unsigned)atoi(seconds_env) : 0;
    unsigned iterations=iterations_env ? (unsigned)atoi(iterations_env) : 0;
    if(seconds>60 || iterations>65536) { fprintf(stderr,"Workload out of bounds\n");return 2; }
    const char *aw=getenv("SQ2_VULKAN_AHB_WIDTH"),*ah=getenv("SQ2_VULKAN_AHB_HEIGHT");
    unsigned ahb_width=aw?(unsigned)atoi(aw):64,ahb_height=ah?(unsigned)atoi(ah):64;
    if(!ahb_width || !ahb_height || ahb_width>4096 || ahb_height>4096) {
        fprintf(stderr,"AHB dimensions out of bounds\n");return 2;
    }
    uint32_t multiplier=1,addition=0;
    uint32_t words=seconds ? 4096 : 256;
    for(unsigned j=0;j<iterations;j++){multiplier*=1664525u;addition=addition*1664525u+1013904223u;}
    const char *tools_dir=getenv("SQ2_ADRENOTOOLS_DIR");
    void *lib=NULL;
    if(tools_dir) {
        char path[PATH_MAX],driver_dir[PATH_MAX];
        if(snprintf(path,sizeof(path),"%s/libadrenotools.so",tools_dir)>=(int)sizeof(path) || strlen(argv[1])>=sizeof(driver_dir))return 2;
        strcpy(driver_dir,argv[1]);char *slash=strrchr(driver_dir,'/');if(!slash)return 2;
        const char *driver_name=argv[1]+(slash-driver_dir)+1;slash[1]=0;
        void *tools=dlopen(path,RTLD_NOW|RTLD_LOCAL);
        typedef void *(*open_t)(int,int,const char*,const char*,const char*,const char*,const char*,void**);
        open_t open_custom=tools?(open_t)dlsym(tools,"adrenotools_open_libvulkan"):NULL;
        if(!open_custom){fprintf(stderr,"Adrenotools load failed: %s\n",dlerror());return 1;}
        // Only DRIVER_CUSTOM; never KGSL mapping import, turbo or file redirection.
        lib=open_custom(RTLD_NOW|RTLD_LOCAL,1,NULL,tools_dir,driver_dir,driver_name,NULL,NULL);
        puts("ADRENOTOOLS isolated system loader requested");
    } else lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!lib) { fprintf(stderr,"dlopen: %s\n",dlerror()); return 1; }
    PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr");
    if(!gipa) gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vkGetInstanceProcAddr");
    hw_device_t *hal_device=NULL;
    if(!gipa) {
        hw_module_t *module=(hw_module_t*)dlsym(lib,HAL_MODULE_INFO_SYM_AS_STR);
        if(module && module->methods && module->methods->open &&
           !module->methods->open(module,HWVULKAN_DEVICE_0,&hal_device))
            gipa=((hwvulkan_device_t*)hal_device)->GetInstanceProcAddr;
    }
    if(!gipa) { fprintf(stderr,"No Vulkan entrypoint\n"); return 1; }
    /* Android HAL exposes global creation directly; a vendor GIPA may not
     * return vkCreateInstance for a null instance. */
    PFN_vkCreateInstance create=hal_device ? ((hwvulkan_device_t*)hal_device)->CreateInstance :
        (PFN_vkCreateInstance)gipa(VK_NULL_HANDLE,"vkCreateInstance");
    if(!create) { fprintf(stderr,"No vkCreateInstance entrypoint\n"); return 1; }
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="SQ2 hardware readback",.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    VkInstance inst; CHECK(create(&ici,NULL,&inst));
#define LOAD(n) vk##n=(PFN_vk##n)gipa(inst,"vk" #n); if(!vk##n) { fprintf(stderr,"Missing vk%s\n",#n); return 1; }
    FUNCS(LOAD)
    uint32_t count=0; CHECK(vkEnumeratePhysicalDevices(inst,&count,NULL));
    if(!count || count>16) { fprintf(stderr,"No usable physical device (count=%u)\n",count); return 1; }
    VkPhysicalDevice devices[16]; CHECK(vkEnumeratePhysicalDevices(inst,&count,devices));
    VkPhysicalDevice gpu=VK_NULL_HANDLE;
    for(uint32_t i=0;i<count;i++) {
        VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(devices[i],&p);
        printf("GPU name=%s vendor=%x device=%x type=%d Vulkan=%u.%u.%u driver=%u\n",p.deviceName,p.vendorID,p.deviceID,p.deviceType,VK_VERSION_MAJOR(p.apiVersion),VK_VERSION_MINOR(p.apiVersion),VK_VERSION_PATCH(p.apiVersion),p.driverVersion);
        if(tools_dir && (p.driverVersion!=2150838273u || strstr(p.deviceName,"Turnip"))) {
            fprintf(stderr,"Custom v819 driver not active; refusing fallback\n");return 1;
        }
        if(getenv("SQ2_VULKAN_REJECT_TURNIP") && strstr(p.deviceName,"Turnip")) {
            fprintf(stderr,"Quest2 probe unexpectedly loaded Turnip\n");return 1;
        }
        if(p.vendorID==0x5143 && p.deviceType==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU && strstr(p.deviceName,"680")) gpu=devices[i];
    }
    if(getenv("SQ2_VULKAN_REJECT_TURNIP")) {
        FILE *maps=fopen("/proc/self/maps","r");
        if(!maps){perror("Vulkan runtime maps");return 1;}
        char line[2048];
        while(fgets(line,sizeof(line),maps))
            if(strstr(line,"r-xp") && (strstr(line,"vulkan") || strstr(line,"libgsl") ||
               strstr(line,"mapper") || strstr(line,"quest2")))printf("QUEST2_RUNTIME_MAP %s",line);
        fclose(maps);
    }
    if(!gpu) { fprintf(stderr,"Expected hardware Adreno 680, refusing fallback\n"); return 1; }
    vkGetPhysicalDeviceMemoryProperties(gpu,&memory);
    uint32_t nq=0; vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,NULL);
    VkQueueFamilyProperties *qp=calloc(nq,sizeof(*qp)); if(!qp) return 1;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,qp);
    uint32_t family=UINT32_MAX;
    for(uint32_t i=0;i<nq;i++) if((qp[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))==(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)) { family=i; break; }
    free(qp); if(family==UINT32_MAX) return 1;
    uint32_t ne=0; CHECK(vkEnumerateDeviceExtensionProperties(gpu,NULL,&ne,NULL));
    VkExtensionProperties *ext=calloc(ne,sizeof(*ext)); if(!ext) return 1;
    CHECK(vkEnumerateDeviceExtensionProperties(gpu,NULL,&ne,ext));
    int has_ahb=0;
    for(uint32_t i=0;i<ne;i++) {
        if(!strcmp(ext[i].extensionName,VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME)) has_ahb=1;
        if(getenv("SQ2_VULKAN_ALL_EXTENSIONS") || strstr(ext[i].extensionName,"android") || strstr(ext[i].extensionName,"swapchain")) printf("EXT %s\n",ext[i].extensionName);
    }
    free(ext);
    if(!offscreen_only && !has_ahb) {
        fprintf(stderr,"AHB extension is not advertised\n");
        vkDestroyInstance(inst,NULL);
        if(hal_device && hal_device->close) hal_device->close(hal_device);
        dlclose(lib);return 1;
    }
    float priority=1.0;
    VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    const char *device_ext=VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME;
    VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qci,.enabledExtensionCount=offscreen_only?0:1,.ppEnabledExtensionNames=&device_ext};
    VkDevice dev; CHECK(vkCreateDevice(gpu,&dci,NULL,&dev));
    if(ahb_mode!=AHB_FULL) {
        printf("AHB_STAGE_ONLY mode=%s cycles=%u no_compute_or_raster=1\n",
               ahb_mode==AHB_PROPERTIES_ONLY?"properties":"import",ahb_cycles);
        result=ahb_probe(inst,gipa,gpu,dev,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,
                         ahb_mode,ahb_cycles,ahb_width,ahb_height);
        if(queue_submissions) { fprintf(stderr,"Stage-only probe submitted GPU work\n");result=1; }
        goto cleanup_device;
    }
    VkQueue queue; vkGetDeviceQueue(dev,family,0,&queue);
    VkBufferCreateInfo bci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=64*64*4,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buffer; CHECK(vkCreateBuffer(dev,&bci,NULL,&buffer));
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(dev,buffer,&req);
    VkMemoryAllocateInfo mai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    VkDeviceMemory mem; CHECK(vkAllocateMemory(dev,&mai,NULL,&mem)); CHECK(vkBindBufferMemory(dev,buffer,mem,0));
    uint32_t *mapped; CHECK(vkMapMemory(dev,mem,0,VK_WHOLE_SIZE,0,(void**)&mapped));
    VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
    VkDescriptorSetLayoutCreateInfo slci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};
    VkDescriptorSetLayout sl; CHECK(vkCreateDescriptorSetLayout(dev,&slci,NULL,&sl));
    VkPushConstantRange push={VK_SHADER_STAGE_COMPUTE_BIT,0,8};
    VkPipelineLayoutCreateInfo plci={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&sl,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
    VkPipelineLayout pl; CHECK(vkCreatePipelineLayout(dev,&plci,NULL,&pl));
    VkShaderModuleCreateInfo smci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizeof(sq2_compute),.pCode=sq2_compute};
    VkShaderModule shader; CHECK(vkCreateShaderModule(dev,&smci,NULL,&shader));
    VkComputePipelineCreateInfo cpci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=shader,.pName="main"},.layout=pl};
    VkPipeline pipeline; CHECK(vkCreateComputePipelines(dev,VK_NULL_HANDLE,1,&cpci,NULL,&pipeline));
    VkDescriptorPoolSize size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};
    VkDescriptorPoolCreateInfo dpci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&size};
    VkDescriptorPool pool; CHECK(vkCreateDescriptorPool(dev,&dpci,NULL,&pool));
    VkDescriptorSetAllocateInfo dsai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&sl};
    VkDescriptorSet ds; CHECK(vkAllocateDescriptorSets(dev,&dsai,&ds));
    VkDescriptorBufferInfo bi={buffer,0,bci.size};
    VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=ds,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&bi};
    vkUpdateDescriptorSets(dev,1,&write,0,NULL);
    VkCommandPoolCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};
    VkCommandPool cmdpool; CHECK(vkCreateCommandPool(dev,&cpi,NULL,&cmdpool));
    VkCommandBufferAllocateInfo cbai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=cmdpool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer cb; CHECK(vkAllocateCommandBuffers(dev,&cbai,&cb));
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VkFenceCreateInfo fci={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence; CHECK(vkCreateFence(dev,&fci,NULL,&fence));
    time_t started=time(NULL);
    uint32_t rounds=0;
    for(uint32_t round=0;round<1000000 && (round<16 || (seconds && time(NULL)-started<(time_t)seconds));round++) {
        memset(mapped,0,bci.size);
        CHECK(vkResetCommandBuffer(cb,0)); CHECK(vkBeginCommandBuffer(cb,&begin));
        vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
        vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pl,0,1,&ds,0,NULL);
        uint32_t seed=round*7919u+12345u,params[2]={seed,iterations};
        vkCmdPushConstants(cb,pl,VK_SHADER_STAGE_COMPUTE_BIT,0,8,params);
        vkCmdDispatch(cb,words/64,1,1);
        VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
        submit(dev,queue,cb,fence);
        for(uint32_t i=0;i<words;i++) if(mapped[i]!=(((i*1664525u+seed)*multiplier+addition)^0x9e3779b9u)) { fprintf(stderr,"Compute mismatch round=%u word=%u\n",round,i); return 1; }
        rounds++;
    }
    printf("COMPUTE_OK %u rounds x %u checked words, ALU iterations=%u, elapsed=%lld seconds\n",rounds,words,iterations,(long long)(time(NULL)-started));
    VkImageCreateInfo imci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.extent={64,64,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
    VkImage im; CHECK(vkCreateImage(dev,&imci,NULL,&im)); vkGetImageMemoryRequirements(dev,im,&req);
    mai.allocationSize=req.size; mai.memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory immem; CHECK(vkAllocateMemory(dev,&mai,NULL,&immem)); CHECK(vkBindImageMemory(dev,im,immem,0));
    CHECK(vkResetCommandBuffer(cb,0)); CHECK(vkBeginCommandBuffer(cb,&begin));
    VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    VkImageMemoryBarrier ib={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=im,.subresourceRange=range};
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&ib);
    VkClearColorValue color={.float32={1,0,0,1}};
    vkCmdClearColorImage(cb,im,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&range);
    ib.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; ib.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    ib.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; ib.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&ib);
    VkBufferImageCopy region={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={64,64,1}};
    vkCmdCopyImageToBuffer(cb,im,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&region);
    VkMemoryBarrier mb={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&mb,0,NULL,0,NULL);
    submit(dev,queue,cb,fence);
    for(uint32_t i=0;i<4096;i++) if(mapped[i]!=0xff0000ffu) { fprintf(stderr,"Image mismatch pixel=%u value=%08x\n",i,mapped[i]); return 1; }
    puts("IMAGE_OK 64x64 optimal-tiled RGBA clear/copy/readback");
    vkDestroyImage(dev,im,NULL); vkFreeMemory(dev,immem,NULL);
    raster_readback(dev,queue,cb,fence,buffer,mapped);
    if(offscreen_only) goto cleanup;
    result=ahb_probe(inst,gipa,gpu,dev,queue,cb,fence,ahb_mode,ahb_cycles,ahb_width,ahb_height);
cleanup:
    puts("CLEANUP_STAGE resources");
    vkDestroyFence(dev,fence,NULL);
    vkDestroyCommandPool(dev,cmdpool,NULL); vkDestroyDescriptorPool(dev,pool,NULL);
    vkDestroyPipeline(dev,pipeline,NULL); vkDestroyShaderModule(dev,shader,NULL);
    vkDestroyPipelineLayout(dev,pl,NULL); vkDestroyDescriptorSetLayout(dev,sl,NULL);
    vkUnmapMemory(dev,mem); vkDestroyBuffer(dev,buffer,NULL); vkFreeMemory(dev,mem,NULL);
cleanup_device:
    puts("CLEANUP_STAGE device");
    vkDestroyDevice(dev,NULL);
    puts("CLEANUP_STAGE instance");
    vkDestroyInstance(inst,NULL);
    puts("CLEANUP_STAGE HAL close");
    if(hal_device && hal_device->close) hal_device->close(hal_device);
    puts("CLEANUP_STAGE dlclose");
    dlclose(lib);
    if(result) { puts("SQ2_VULKAN_PROBE_FAILED");return result; }
    puts(offscreen_only ? "SQ2_VULKAN_OFFSCREEN_OK (AHB/WSI not tested)" :
         ahb_mode==AHB_PROPERTIES_ONLY ? "SQ2_VULKAN_AHB_PROPERTIES_OK (no GPU submissions; import/WSI not tested)" :
         ahb_mode==AHB_IMPORT_ONLY ? "SQ2_VULKAN_AHB_IMPORT_OK (no GPU submissions; rendering/WSI not tested)" :
         "SQ2_VULKAN_READBACK_OK");return 0;
}
