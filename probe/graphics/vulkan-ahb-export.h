/* Dedicated Vulkan-created AHB allocation/export. Uses GPU -> staging readback
 * because a GPU-only exported AHB does not promise CPU lock usage. */
static int ahb_export_cycle(VkInstance inst, PFN_vkGetInstanceProcAddr gipa, VkPhysicalDevice gpu,
                            VkDevice dev, VkQueue queue, VkCommandBuffer cb, VkFence fence,
                            PFN_vkGetAndroidHardwareBufferPropertiesANDROID properties,
                            unsigned cycle, unsigned width, unsigned height) {
    int failed=1;
    VkImage image=VK_NULL_HANDLE;
    VkDeviceMemory allocation=VK_NULL_HANDLE, staging_memory=VK_NULL_HANDLE;
    VkBuffer staging=VK_NULL_HANDLE;
    AHardwareBuffer *exported[2]={NULL,NULL},*imported=NULL;
    void *mapped=NULL;
    VkFormat format=VK_FORMAT_R8G8B8A8_UNORM;
    uint32_t android_format=1,bytes=4;
    int import_mode=getenv("SQ2_VULKAN_COLOR_IMPORT")!=NULL;
    const char *format_option=getenv("SQ2_VULKAN_EXPORT_FORMAT");
    if(format_option) {
        if(!strcmp(format_option,"bgra8")){format=VK_FORMAT_B8G8R8A8_UNORM;android_format=5;}
        else if(!strcmp(format_option,"fp16")){format=VK_FORMAT_R16G16B16A16_SFLOAT;android_format=22;bytes=8;}
        else if(!strcmp(format_option,"rgbx8") && import_mode){android_format=2;}
        else if(strcmp(format_option,"rgba8")){fprintf(stderr,"Invalid SQ2_VULKAN_EXPORT_FORMAT\n");return 1;}
    }
    const VkDeviceSize length=(VkDeviceSize)width*height*bytes;
    PFN_vkGetMemoryAndroidHardwareBufferANDROID export_ahb=
        (PFN_vkGetMemoryAndroidHardwareBufferANDROID)gipa(inst,"vkGetMemoryAndroidHardwareBufferANDROID");
    PFN_vkGetPhysicalDeviceImageFormatProperties2 query=
        (PFN_vkGetPhysicalDeviceImageFormatProperties2)gipa(inst,"vkGetPhysicalDeviceImageFormatProperties2");
    if(!export_ahb || !query)return 1;
    const VkImageUsageFlags usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkPhysicalDeviceExternalImageFormatInfo external_query={
        .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
        .handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
    VkPhysicalDeviceImageFormatInfo2 format_query={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext=&external_query,.format=format,.type=VK_IMAGE_TYPE_2D,
        .tiling=VK_IMAGE_TILING_OPTIMAL,.usage=usage};
    VkExternalImageFormatProperties ep={.sType=VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
    VkImageFormatProperties2 ip={.sType=VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,.pNext=&ep};
    if(query(gpu,&format_query,&ip)!=VK_SUCCESS ||
       !(ep.externalMemoryProperties.externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) ||
       width>ip.imageFormatProperties.maxExtent.width || height>ip.imageFormatProperties.maxExtent.height)return 1;
    if(import_mode && (!(ep.externalMemoryProperties.externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) ||
        !(ep.externalMemoryProperties.exportFromImportedHandleTypes&VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID)))return 1;
    VkExternalMemoryImageCreateInfo external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
    VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.pNext=&external,
        .imageType=VK_IMAGE_TYPE_2D,.format=format,.extent={width,height,1},
        .mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=usage,.sharingMode=VK_SHARING_MODE_EXCLUSIVE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
    VkResult result=vkCreateImage(dev,&ci,NULL,&image);
    if(result!=VK_SUCCESS)goto cleanup;
    VkMemoryDedicatedAllocateInfo dedicated={.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,.image=image};
    VkExportMemoryAllocateInfo ei={.sType=VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,.pNext=&dedicated,
        .handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
    /* AHB dedicated image export requires allocationSize=0, not pre-bind
     * vkGetImageMemoryRequirements. The implementation selects backing memory. */
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=&ei,
        .allocationSize=0,.memoryTypeIndex=memory_type(UINT32_MAX,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    VkImportAndroidHardwareBufferInfoANDROID import_info={
        .sType=VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,.pNext=&ei};
    if(import_mode) {
        AHardwareBuffer_Desc desc={.width=width,.height=height,.layers=1,.format=android_format,
            .usage=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE|AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT};
        if(AHardwareBuffer_allocate(&desc,&imported) || !imported)goto cleanup;
        VkAndroidHardwareBufferFormatPropertiesANDROID fp={.sType=VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
        VkAndroidHardwareBufferPropertiesANDROID ap={.sType=VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,.pNext=&fp};
        result=properties(dev,imported,&ap);
        if(result!=VK_SUCCESS || fp.format!=format || ap.allocationSize<length || !ap.memoryTypeBits)goto cleanup;
        import_info.buffer=imported;ai.pNext=&import_info;ai.allocationSize=ap.allocationSize;
        ai.memoryTypeIndex=memory_type(ap.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }
    result=vkAllocateMemory(dev,&ai,NULL,&allocation);
    printf("AHB_NEW_ALLOCATE cycle=%u result=%d\n",cycle,result);
    if(result!=VK_SUCCESS)goto cleanup;
    result=vkBindImageMemory(dev,image,allocation,0);
    if(result!=VK_SUCCESS)goto cleanup;
    VkMemoryGetAndroidHardwareBufferInfoANDROID get={
        .sType=VK_STRUCTURE_TYPE_MEMORY_GET_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,.memory=allocation};
    for(unsigned i=0;i<2;i++) {
        result=export_ahb(dev,&get,&exported[i]);
        if(result!=VK_SUCCESS || !exported[i])goto cleanup;
        AHardwareBuffer_Desc desc={0};AHardwareBuffer_describe(exported[i],&desc);
        VkAndroidHardwareBufferFormatPropertiesANDROID fp={.sType=VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
        VkAndroidHardwareBufferPropertiesANDROID ap={.sType=VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,.pNext=&fp};
        result=properties(dev,exported[i],&ap);
        if(result!=VK_SUCCESS || desc.width!=width || desc.height!=height || desc.layers!=1 ||
           desc.format!=android_format || fp.format!=format ||
           ap.allocationSize<length || !ap.memoryTypeBits)goto cleanup;
        printf("AHB_NEW_EXPORT cycle=%u ref=%u size=%llu usage=%llx\n",cycle,i,
               (unsigned long long)ap.allocationSize,(unsigned long long)desc.usage);
    }
    VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=length,
        .usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    result=vkCreateBuffer(dev,&bi,NULL,&staging);if(result!=VK_SUCCESS)goto cleanup;
    VkMemoryRequirements mr;vkGetBufferMemoryRequirements(dev,staging,&mr);
    VkMemoryAllocateInfo sa={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=mr.size,
        .memoryTypeIndex=memory_type(mr.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    result=vkAllocateMemory(dev,&sa,NULL,&staging_memory);if(result!=VK_SUCCESS)goto cleanup;
    result=vkBindBufferMemory(dev,staging,staging_memory,0);if(result!=VK_SUCCESS)goto cleanup;
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkResetCommandBuffer(cb,0));CHECK(vkBeginCommandBuffer(cb,&begin));
    VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    VkImageMemoryBarrier ib={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.image=image,.subresourceRange=range};
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&ib);
    VkClearColorValue color={.float32={0,0,0,0}};
    unsigned code=(cycle-1u)&15u;
    for(unsigned c=0;c<4;c++)color.float32[c]=(code>>c)&1u;
    const float rich_values[4]={0.125f,-0.5f,2.0f,0.75f};
    const uint16_t rich_half[4]={0x3000,0xb800,0x4000,0x3a00};
    if(bytes==8 && cycle>16)
        for(unsigned c=0;c<4;c++)color.float32[c]=rich_values[(c+cycle-17)%4];
    vkCmdClearColorImage(cb,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&range);
    ib.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ib.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    ib.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;ib.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&ib);
    VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={width,height,1}};
    vkCmdCopyImageToBuffer(cb,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,staging,1,&copy);
    VkMemoryBarrier mb={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&mb,0,NULL,0,NULL);
    submit(dev,queue,cb,fence);
    result=vkMapMemory(dev,staging_memory,0,length,0,&mapped);if(result!=VK_SUCCESS)goto cleanup;
    unsigned bad=0;
    for(VkDeviceSize i=0;i<length;i++) {
        unsigned component=(i%bytes)/(bytes/4);
        if(android_format==5 && component<3)component=2-component;
        unsigned value=(code>>component)&1u;
        unsigned expected=bytes==8?((i&1)?value*0x3cu:0):value*255u;
        if(bytes==8 && cycle>16)
            expected=(rich_half[(component+cycle-17)%4]>>((i&1)*8))&255u;
        if(android_format!=2 || component!=3)
            if(((unsigned char*)mapped)[i]!=expected)bad++;
    }
    printf("AHB_NEW_READBACK format=%d cycle=%u pixels=%ux%u bad_bytes=%u\n",format,cycle,width,height,bad);
    if(bad)goto cleanup;
    if(import_mode) {
        AHardwareBuffer_Desc desc={0};AHardwareBuffer_describe(imported,&desc);
        void *cpu=NULL;
        if(desc.stride<width || AHardwareBuffer_lock(imported,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,-1,NULL,&cpu) || !cpu)goto cleanup;
        for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++)for(unsigned i=0;i<bytes;i++) {
            unsigned component=i/(bytes/4);
            if(android_format==5 && component<3)component=2-component;
            if(android_format==2 && component==3)continue; /* RGBX does not define alpha. */
            unsigned value=(code>>component)&1u;
            unsigned expected=bytes==8?((i&1)?value*0x3cu:0):value*255u;
            if(bytes==8 && cycle>16)expected=(rich_half[(component+cycle-17)%4]>>((i&1)*8))&255u;
            if(((unsigned char*)cpu)[((size_t)y*desc.stride+x)*bytes+i]!=expected)bad++;
        }
        int unlock=AHardwareBuffer_unlock(imported,NULL);
        printf("AHB_COLOR_IMPORT_CPU format=%u cycle=%u pixels=%ux%u bad_bytes=%u unlock=%d\n",android_format,cycle,width,height,bad,unlock);
        if(bad || unlock)goto cleanup;
    }
    failed=0;
cleanup:
    if(mapped)vkUnmapMemory(dev,staging_memory);
    if(staging)vkDestroyBuffer(dev,staging,NULL);
    if(staging_memory)vkFreeMemory(dev,staging_memory,NULL);
    if(image)vkDestroyImage(dev,image,NULL);
    if(allocation)vkFreeMemory(dev,allocation,NULL);
    if(imported)AHardwareBuffer_release(imported);
    for(unsigned i=0;i<2;i++)if(exported[i]) {
        VkAndroidHardwareBufferPropertiesANDROID ap={.sType=VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
        if(!failed && (properties(dev,exported[i],&ap)!=VK_SUCCESS || ap.allocationSize<length))failed=1;
        AHardwareBuffer_release(exported[i]);
    }
    printf("AHB_NEW_EXPORT_RESULT cycle=%u after_free_refs=%u result=%s last_vk_result=%d\n",
           cycle,(exported[0]!=NULL)+(exported[1]!=NULL),failed?"FAIL":"PASS",result);
    return failed;
}
