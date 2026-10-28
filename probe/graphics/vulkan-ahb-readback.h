/* Included by vulkan-readback.c after its Vulkan entrypoints are loaded.
 * Stage-only modes never create command buffers or submit GPU work. Imported
 * AHB memory must acquire its own reference on successful vkAllocateMemory;
 * see VkImportAndroidHardwareBufferInfoANDROID. Our extra application reference
 * remains alive until after image destruction and vkFreeMemory.
 */
enum ahb_probe_mode { AHB_FULL, AHB_PROPERTIES_ONLY, AHB_IMPORT_ONLY };

enum { AHB_GRID_SIDE=4 };
static void ahb_grid_edges(unsigned size, uint32_t edges[AHB_GRID_SIDE+1]) {
    edges[0]=0;edges[AHB_GRID_SIDE]=size;
    for(unsigned i=1;i<AHB_GRID_SIDE;i++) {
        uint32_t edge=(uint64_t)size*i/AHB_GRID_SIDE;
        /* Odd internal edges exercise unaligned rectangles. Repeated edges
         * for small images are allowed; their empty rectangles are skipped. */
        if(edge && edge<size && !(edge&1u)) edge++;
        edges[i]=edge;
    }
}
static unsigned ahb_grid_cell(unsigned position, const uint32_t edges[AHB_GRID_SIDE+1]) {
    unsigned cell=0;
    while(cell+1<AHB_GRID_SIDE && position>=edges[cell+1]) cell++;
    return cell;
}
static unsigned ahb_grid_code(unsigned column, unsigned row, unsigned cycle) {
    /* All sixteen RGBA bit combinations occur once in a full-sized grid.
     * Vary each new allocation's pattern to expose stale-buffer contents. */
    return (row*AHB_GRID_SIDE+column)^((cycle-1u)&15u);
}

static const char *ahb_description_error(const AHardwareBuffer_Desc *actual,
                                        unsigned width, unsigned height) {
    const uint64_t usage=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
        AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    if(actual->width!=width || actual->height!=height || actual->layers!=1 ||
       actual->format!=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM || actual->stride<width ||
       (actual->usage & usage)!=usage || (actual->usage & AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT))
        return "AHB description does not match the requested unprotected RGBA allocation";
    return NULL;
}

static const char *ahb_metadata_error(const AHardwareBuffer_Desc *actual,
                                     unsigned width, unsigned height,
                                     const VkAndroidHardwareBufferPropertiesANDROID *ap,
                                     const VkAndroidHardwareBufferFormatPropertiesANDROID *fp,
                                     uint32_t *type) {
    const char *description_error=ahb_description_error(actual,width,height);
    if(description_error) return description_error;
    if(fp->format!=VK_FORMAT_R8G8B8A8_UNORM)
        return "AHB does not report VK_FORMAT_R8G8B8A8_UNORM";
    /* describe() has no allocation-size field. This is a lower-bound check,
     * not a substitute for exact fd/mapper metadata validation in the bridge. */
    if(!ap->allocationSize || ap->allocationSize<(uint64_t)width*height*4)
        return "AHB allocationSize is zero or smaller than its RGBA pixel data";
    if(!memory.memoryTypeCount || memory.memoryTypeCount>VK_MAX_MEMORY_TYPES ||
       !memory.memoryHeapCount || memory.memoryHeapCount>VK_MAX_MEMORY_HEAPS)
        return "Invalid physical-device memory properties";
    uint32_t valid=memory.memoryTypeCount==32 ? UINT32_MAX : (1u<<memory.memoryTypeCount)-1u;
    if(!ap->memoryTypeBits || (ap->memoryTypeBits & ~valid))
        return "AHB memoryTypeBits is empty or names a nonexistent memory type";
    for(uint32_t i=0;i<memory.memoryTypeCount;i++) if(ap->memoryTypeBits & (1u<<i)) {
        uint32_t heap=memory.memoryTypes[i].heapIndex;
        if(heap>=memory.memoryHeapCount) return "AHB memory type names an invalid heap";
    }
    for(uint32_t i=0;i<memory.memoryTypeCount;i++) if(ap->memoryTypeBits & (1u<<i)) {
        if(ap->allocationSize<=memory.memoryHeaps[memory.memoryTypes[i].heapIndex].size) {
            *type=i;
            return NULL;
        }
    }
    return "AHB allocationSize exceeds every compatible memory heap";
}

static int ahb_same_properties(const VkAndroidHardwareBufferPropertiesANDROID *a,
                               const VkAndroidHardwareBufferFormatPropertiesANDROID *af,
                               const VkAndroidHardwareBufferPropertiesANDROID *b,
                               const VkAndroidHardwareBufferFormatPropertiesANDROID *bf) {
    return a->allocationSize==b->allocationSize && a->memoryTypeBits==b->memoryTypeBits &&
        af->format==bf->format && af->externalFormat==bf->externalFormat &&
        af->formatFeatures==bf->formatFeatures;
}

static int ahb_query_again(VkDevice dev, AHardwareBuffer *ahb,
                           PFN_vkGetAndroidHardwareBufferPropertiesANDROID properties,
                           unsigned width, unsigned height,
                           const VkAndroidHardwareBufferPropertiesANDROID *initial,
                           const VkAndroidHardwareBufferFormatPropertiesANDROID *initial_format) {
    AHardwareBuffer_Desc actual={0};
    AHardwareBuffer_describe(ahb,&actual);
    const char *description_error=ahb_description_error(&actual,width,height);
    if(description_error) { fprintf(stderr,"AHB repeat description rejected: %s\n",description_error);return 1; }
    VkAndroidHardwareBufferFormatPropertiesANDROID fp={.sType=VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
    VkAndroidHardwareBufferPropertiesANDROID ap={.sType=VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,.pNext=&fp};
    VkResult result=properties(dev,ahb,&ap);
    if(result!=VK_SUCCESS) { fprintf(stderr,"AHB repeat properties: VkResult=%d\n",result);return 1; }
    uint32_t type=UINT32_MAX;
    const char *error=ahb_metadata_error(&actual,width,height,&ap,&fp,&type);
    if(error || !ahb_same_properties(initial,initial_format,&ap,&fp)) {
        fprintf(stderr,"AHB repeat properties invalid/unstable: %s\n",error?error:"fields changed");return 1;
    }
    return 0;
}

static int ahb_guard_checks(const AHardwareBuffer_Desc *actual, unsigned width, unsigned height,
                            const VkAndroidHardwareBufferPropertiesANDROID *ap,
                            const VkAndroidHardwareBufferFormatPropertiesANDROID *fp) {
    uint32_t type=UINT32_MAX;
    AHardwareBuffer_Desc d=*actual;
    VkAndroidHardwareBufferPropertiesANDROID p=*ap;
    VkAndroidHardwareBufferFormatPropertiesANDROID f=*fp;
    unsigned rejected=0;
    d.stride=width-1;
    rejected+=ahb_metadata_error(&d,width,height,ap,fp,&type)!=NULL;
    d=*actual;d.layers=2;
    rejected+=ahb_metadata_error(&d,width,height,ap,fp,&type)!=NULL;
    p.allocationSize=0;
    rejected+=ahb_metadata_error(actual,width,height,&p,fp,&type)!=NULL;
    p=*ap;p.memoryTypeBits=0;
    rejected+=ahb_metadata_error(actual,width,height,&p,fp,&type)!=NULL;
    f.format=VK_FORMAT_UNDEFINED;
    rejected+=ahb_metadata_error(actual,width,height,ap,&f,&type)!=NULL;
    if(rejected!=5) { fprintf(stderr,"AHB local metadata guard self-check failed\n");return 1; }
    puts("AHB_GUARD_CHECKS_OK 5 invalid metadata cases rejected locally; no invalid Vulkan/AHB calls");
    return 0;
}

static int ahb_image_supported(VkInstance inst, PFN_vkGetInstanceProcAddr gipa,
                               VkPhysicalDevice gpu, unsigned width, unsigned height) {
    PFN_vkGetPhysicalDeviceImageFormatProperties2 query=
        (PFN_vkGetPhysicalDeviceImageFormatProperties2)gipa(inst,"vkGetPhysicalDeviceImageFormatProperties2");
    if(!query) { fprintf(stderr,"Missing vkGetPhysicalDeviceImageFormatProperties2\n");return 1; }
    VkPhysicalDeviceExternalImageFormatInfo external={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
        .handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
    VkPhysicalDeviceImageFormatInfo2 info={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,.pNext=&external,
        .format=VK_FORMAT_R8G8B8A8_UNORM,.type=VK_IMAGE_TYPE_2D,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    VkExternalImageFormatProperties ep={.sType=VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
    VkImageFormatProperties2 properties={.sType=VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,.pNext=&ep};
    VkResult result=query(gpu,&info,&properties);
    if(result!=VK_SUCCESS) { fprintf(stderr,"AHB external image support: VkResult=%d\n",result);return 1; }
    VkExternalMemoryProperties *mp=&ep.externalMemoryProperties;
    VkImageFormatProperties *ip=&properties.imageFormatProperties;
    printf("AHB_IMAGE_SUPPORT features=%x compatible=%x max=%ux%u\n",mp->externalMemoryFeatures,
           mp->compatibleHandleTypes,ip->maxExtent.width,ip->maxExtent.height);
    if(!(mp->externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) ||
       !(mp->compatibleHandleTypes & VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID) ||
       width>ip->maxExtent.width || height>ip->maxExtent.height || !ip->maxArrayLayers || !ip->maxMipLevels ||
       !(ip->sampleCounts & VK_SAMPLE_COUNT_1_BIT)) {
        fprintf(stderr,"AHB RGBA image is not importable with the requested dimensions/usage\n");return 1;
    }
    return 0;
}

static int ahb_probe_cycle(VkInstance inst, PFN_vkGetInstanceProcAddr gipa, VkPhysicalDevice gpu,
                           VkDevice dev, VkQueue queue, VkCommandBuffer cb, VkFence fence,
                           PFN_vkGetAndroidHardwareBufferPropertiesANDROID properties,
                           enum ahb_probe_mode mode, unsigned cycle, unsigned width, unsigned height) {
    AHardwareBuffer_Desc requested={.width=width,.height=height,.layers=1,
        .format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
        .usage=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT|AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE};
    AHardwareBuffer_Desc actual={0};
    AHardwareBuffer *ahb=NULL;
    VkImage image=VK_NULL_HANDLE;
    VkDeviceMemory allocation=VK_NULL_HANDLE;
    VkImageView view=VK_NULL_HANDLE;
    VkRenderPass pass=VK_NULL_HANDLE;
    VkFramebuffer framebuffer=VK_NULL_HANDLE;
    int references=0,created=0,allocated=0,locked=0,failed=1;
    int view_created=0,pass_created=0,framebuffer_created=0;
    unsigned submits_before=queue_submissions;
    VkAndroidHardwareBufferFormatPropertiesANDROID fp={.sType=VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
    VkAndroidHardwareBufferPropertiesANDROID ap={.sType=VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,.pNext=&fp};
    printf("AHB_STAGE allocate cycle=%u\n",cycle);
    if(AHardwareBuffer_allocate(&requested,&ahb) || !ahb) {
        fprintf(stderr,"AHB allocation failed\n");return 1;
    }
    references=1;
    AHardwareBuffer_acquire(ahb);references++;
    AHardwareBuffer_describe(ahb,&actual);
    printf("AHB_DESC cycle=%u width=%u height=%u layers=%u format=%u stride=%u usage=%llx app_refs=%d\n",
           cycle,actual.width,actual.height,actual.layers,actual.format,actual.stride,
           (unsigned long long)actual.usage,references);
    const char *description_error=ahb_description_error(&actual,width,height);
    if(description_error) { fprintf(stderr,"AHB description rejected: %s\n",description_error);goto cleanup; }
    puts("AHB_STAGE driver properties");
    VkResult result=properties(dev,ahb,&ap);
    if(result!=VK_SUCCESS) { fprintf(stderr,"AHB properties: VkResult=%d\n",result);goto cleanup; }
    printf("AHB_PROPERTIES format=%d external=%llu size=%llu types=%x features=%x cycle=%u\n",
           fp.format,(unsigned long long)fp.externalFormat,(unsigned long long)ap.allocationSize,
           ap.memoryTypeBits,fp.formatFeatures,cycle);
    uint32_t type=UINT32_MAX;
    const char *error=ahb_metadata_error(&actual,width,height,&ap,&fp,&type);
    if(error) { fprintf(stderr,"AHB metadata rejected: %s\n",error);goto cleanup; }
    if(cycle==1 && ahb_guard_checks(&actual,width,height,&ap,&fp)) goto cleanup;
    printf("AHB_METADATA_OK memory_type=%u allocation_size_reported=%llu minimum_rgba_bytes=%llu exact_backing_size=bridge_responsibility\n",
           type,(unsigned long long)ap.allocationSize,(unsigned long long)width*height*4);
    if(mode!=AHB_PROPERTIES_ONLY) {
        if(cycle==1 && ahb_image_supported(inst,gipa,gpu,width,height)) goto cleanup;
        const VkFormatFeatureFlags needed=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if((fp.formatFeatures & needed)!=needed) {
            fprintf(stderr,"AHB format lacks sampled/color-attachment features\n");goto cleanup;
        }
        puts("AHB_STAGE create/import/bind");
        VkExternalMemoryImageCreateInfo external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
            .handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
        VkImageCreateInfo image_info={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.pNext=&external,
            .imageType=VK_IMAGE_TYPE_2D,.format=fp.format,.extent={width,height,1},.mipLevels=1,.arrayLayers=1,
            .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
            .usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .sharingMode=VK_SHARING_MODE_EXCLUSIVE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
        result=vkCreateImage(dev,&image_info,NULL,&image);
        if(result!=VK_SUCCESS) { fprintf(stderr,"AHB create image: VkResult=%d\n",result);goto cleanup; }
        created=1;
        VkMemoryDedicatedAllocateInfo dedicated={.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,.image=image};
        VkImportAndroidHardwareBufferInfoANDROID import={.sType=VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
            .pNext=&dedicated,.buffer=ahb};
        VkMemoryAllocateInfo memory_info={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=&import,
            .allocationSize=ap.allocationSize,.memoryTypeIndex=type};
        result=vkAllocateMemory(dev,&memory_info,NULL,&allocation);
        if(result!=VK_SUCCESS) { fprintf(stderr,"AHB import memory: VkResult=%d\n",result);goto cleanup; }
        allocated=1;
        result=vkBindImageMemory(dev,image,allocation,0);
        if(result!=VK_SUCCESS) { fprintf(stderr,"AHB bind memory: VkResult=%d\n",result);goto cleanup; }
        printf("AHB_IMPORT_OK cycle=%u images=1 imported_memories=1 gpu_submits=0\n",cycle);
    }
    /* Drop the original owner, retain the explicit extra application reference.
     * Successful Vulkan imports independently own an implementation reference. */
    AHardwareBuffer_release(ahb);references--;
    if(ahb_query_again(dev,ahb,properties,width,height,&ap,&fp)) goto cleanup;
    printf("AHB_REFERENCE_OK cycle=%u original_owner_released=1 retained_app_refs=%d\n",cycle,references);
    if(mode==AHB_FULL) {
        uint32_t x_edges[AHB_GRID_SIDE+1],y_edges[AHB_GRID_SIDE+1];
        ahb_grid_edges(width,x_edges);ahb_grid_edges(height,y_edges);
        printf("AHB_GRID cycle=%u side=%u code_xor=%u rgba_bits=0,1,2,3 x_edges=%u,%u,%u,%u,%u y_edges=%u,%u,%u,%u,%u\n",
               cycle,AHB_GRID_SIDE,(cycle-1u)&15u,x_edges[0],x_edges[1],x_edges[2],x_edges[3],x_edges[4],
               y_edges[0],y_edges[1],y_edges[2],y_edges[3],y_edges[4]);
        VkImageViewCreateInfo view_info={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image,
            .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=fp.format,
            .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        result=vkCreateImageView(dev,&view_info,NULL,&view);
        if(result!=VK_SUCCESS) { fprintf(stderr,"AHB create image view: VkResult=%d\n",result);goto cleanup; }
        view_created=1;
        VkAttachmentDescription attachment={.format=fp.format,.samples=VK_SAMPLE_COUNT_1_BIT,
            .loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference reference={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount=1,.pColorAttachments=&reference};
        VkSubpassDependency dependency={.srcSubpass=VK_SUBPASS_EXTERNAL,.dstSubpass=0,
            .srcStageMask=VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
        VkRenderPassCreateInfo pass_info={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount=1,.pAttachments=&attachment,.subpassCount=1,.pSubpasses=&subpass,
            .dependencyCount=1,.pDependencies=&dependency};
        result=vkCreateRenderPass(dev,&pass_info,NULL,&pass);
        if(result!=VK_SUCCESS) { fprintf(stderr,"AHB create render pass: VkResult=%d\n",result);goto cleanup; }
        pass_created=1;
        VkFramebufferCreateInfo framebuffer_info={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass=pass,.attachmentCount=1,.pAttachments=&view,.width=width,.height=height,.layers=1};
        result=vkCreateFramebuffer(dev,&framebuffer_info,NULL,&framebuffer);
        if(result!=VK_SUCCESS) { fprintf(stderr,"AHB create framebuffer: VkResult=%d\n",result);goto cleanup; }
        framebuffer_created=1;
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CHECK(vkResetCommandBuffer(cb,0));CHECK(vkBeginCommandBuffer(cb,&begin));
        VkClearValue background={.color={.float32={0,0,0,0}}};
        VkRenderPassBeginInfo render={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,
            .framebuffer=framebuffer,.renderArea={{0,0},{width,height}},.clearValueCount=1,.pClearValues=&background};
        vkCmdBeginRenderPass(cb,&render,VK_SUBPASS_CONTENTS_INLINE);
        unsigned rectangles=0;
        for(unsigned row=0;row<AHB_GRID_SIDE;row++) for(unsigned column=0;column<AHB_GRID_SIDE;column++) {
            uint32_t rect_width=x_edges[column+1]-x_edges[column],rect_height=y_edges[row+1]-y_edges[row];
            if(!rect_width || !rect_height) continue;
            unsigned code=ahb_grid_code(column,row,cycle);
            VkClearAttachment clear={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.colorAttachment=0};
            for(unsigned channel=0;channel<4;channel++) clear.clearValue.color.float32[channel]=(code>>channel)&1u;
            VkClearRect rect={.rect={{(int32_t)x_edges[column],(int32_t)y_edges[row]},
                                     {rect_width,rect_height}},.baseArrayLayer=0,.layerCount=1};
            vkCmdClearAttachments(cb,1,&clear,1,&rect);rectangles++;
        }
        vkCmdEndRenderPass(cb);
        VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
            .oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .image=image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_HOST_BIT,
                             0,0,NULL,0,NULL,1,&barrier);
        /* A submit/wait failure uses the existing fatal CHECK path; never free
         * potentially in-flight image/memory as though completion had occurred. */
        submit(dev,queue,cb,fence);
        void *pixels=NULL;
        puts("AHB_STAGE CPU lock");
        if(AHardwareBuffer_lock(ahb,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,-1,NULL,&pixels)) {
            fprintf(stderr,"AHB CPU lock failed\n");goto cleanup;
        }
        locked=1;
        if(!pixels || (uint64_t)actual.stride*height*4>ap.allocationSize) {
            fprintf(stderr,"AHB CPU address/stride exceeds reported allocation\n");goto cleanup;
        }
        uint32_t bad=0;
        for(uint32_t y=0;y<height;y++) for(uint32_t x=0;x<width;x++) {
            unsigned column=ahb_grid_cell(x,x_edges),row=ahb_grid_cell(y,y_edges);
            unsigned code=ahb_grid_code(column,row,cycle);
            const uint8_t *pixel=(const uint8_t*)pixels+((size_t)y*actual.stride+x)*4;
            uint8_t expected[4];int mismatch=0;
            for(unsigned channel=0;channel<4;channel++) {
                expected[channel]=((code>>channel)&1u)*255u;
                mismatch|=pixel[channel]!=expected[channel];
            }
            if(mismatch) {
                if(bad<8) fprintf(stderr,"AHB_GRID_MISMATCH cycle=%u x=%u y=%u cell=%u,%u got=%02x,%02x,%02x,%02x expected=%02x,%02x,%02x,%02x\n",
                    cycle,x,y,column,row,pixel[0],pixel[1],pixel[2],pixel[3],
                    expected[0],expected[1],expected[2],expected[3]);
                bad++;
            }
        }
        int unlocked=AHardwareBuffer_unlock(ahb,NULL);locked=0;
        if(unlocked || bad) { fprintf(stderr,"AHB readback failed unlock=%d bad_pixels=%u\n",unlocked,bad);goto cleanup; }
        printf("AHB_OK %ux%u stride=%u mapper5 import/GPU attachment-grid/CPU exact-RGBA readback cycle=%u rectangles=%u checked_pixels=%llu bad_pixels=0\n",
               width,height,actual.stride,cycle,rectangles,(unsigned long long)width*height);
    } else if(queue_submissions!=submits_before) {
        fprintf(stderr,"Stage-only AHB probe unexpectedly submitted GPU work\n");goto cleanup;
    }
    failed=0;
cleanup:
    if(locked && AHardwareBuffer_unlock(ahb,NULL)) { fprintf(stderr,"AHB cleanup unlock failed\n");failed=1; }
    if(framebuffer_created) vkDestroyFramebuffer(dev,framebuffer,NULL);
    if(pass_created) vkDestroyRenderPass(dev,pass,NULL);
    if(view_created) vkDestroyImageView(dev,view,NULL);
    if(created) vkDestroyImage(dev,image,NULL);
    if(allocated) vkFreeMemory(dev,allocation,NULL);
    /* The retained application reference must survive freeing the import. */
    if(!failed && allocated && ahb_query_again(dev,ahb,properties,width,height,&ap,&fp)) failed=1;
    while(references) { AHardwareBuffer_release(ahb);references--; }
    printf("AHB_CYCLE_CLEANUP cycle=%u app_refs=0 images=0 imported_memories=0 gpu_submits=%u result=%s\n",
           cycle,queue_submissions-submits_before,failed?"FAIL":"PASS");
    return failed;
}

static int ahb_probe(VkInstance inst, PFN_vkGetInstanceProcAddr gipa, VkPhysicalDevice gpu,
                     VkDevice dev, VkQueue queue, VkCommandBuffer cb, VkFence fence,
                     enum ahb_probe_mode mode, unsigned cycles, unsigned width, unsigned height) {
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID properties=
        (PFN_vkGetAndroidHardwareBufferPropertiesANDROID)gipa(inst,"vkGetAndroidHardwareBufferPropertiesANDROID");
    if(!properties) { fprintf(stderr,"Missing vkGetAndroidHardwareBufferPropertiesANDROID\n");return 1; }
    unsigned before=queue_submissions;
    for(unsigned cycle=1;cycle<=cycles;cycle++) {
        if(ahb_probe_cycle(inst,gipa,gpu,dev,queue,cb,fence,properties,mode,cycle,width,height)) {
            printf("AHB_SUMMARY mode=%s cycles_passed=%u cycles_requested=%u gpu_submits=%u result=FAIL\n",
                   mode==AHB_PROPERTIES_ONLY?"properties":mode==AHB_IMPORT_ONLY?"import":"readback",
                   cycle-1,cycles,queue_submissions-before);return 1;
        }
    }
    printf("AHB_SUMMARY mode=%s cycles_passed=%u cycles_requested=%u gpu_submits=%u result=PASS\n",
           mode==AHB_PROPERTIES_ONLY?"properties":mode==AHB_IMPORT_ONLY?"import":"readback",
           cycles,cycles,queue_submissions-before);
    return 0;
}
