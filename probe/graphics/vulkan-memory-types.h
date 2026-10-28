/* Exercise every advertised buffer-compatible memory type independently.
 * Valid host accesses honor HOST_VISIBLE and explicit cache maintenance. */
static int memory_types_probe(VkDevice dev,VkQueue queue,VkCommandBuffer cb,VkFence fence) {
    unsigned failures=0,passed=0;
    const char *cycle_option=getenv("SQ2_VULKAN_MEMORY_CYCLES");
    unsigned cycles=cycle_option?(unsigned)atoi(cycle_option):1;
    if(!cycles || cycles>150)return 1;
    const char *size_option=getenv("SQ2_VULKAN_MEMORY_BYTES");
    char *end=NULL;
    unsigned long size=size_option?strtoul(size_option,&end,10):4096;
    if((size_option && (!*size_option || *end)) || size<4096 || size>16*1024*1024 || size%4096)return 1;
    unsigned words=(unsigned)(size/4);
    VkBufferCreateInfo staging_info={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,
        .usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer readback;CHECK(vkCreateBuffer(dev,&staging_info,NULL,&readback));
    VkMemoryRequirements staging_req;vkGetBufferMemoryRequirements(dev,readback,&staging_req);
    VkMemoryAllocateInfo staging_alloc={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize=staging_req.size,.memoryTypeIndex=memory_type(staging_req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    VkDeviceMemory staging_memory;CHECK(vkAllocateMemory(dev,&staging_alloc,NULL,&staging_memory));
    CHECK(vkBindBufferMemory(dev,readback,staging_memory,0));
    uint32_t *readback_words;CHECK(vkMapMemory(dev,staging_memory,0,VK_WHOLE_SIZE,0,(void**)&readback_words));
    printf("MEMORY_TYPES_PARAMETERS bytes=%lu cycles=%u\n",size,cycles);
    for(unsigned tiling=0;tiling<2;tiling++) {
        VkImage image=VK_NULL_HANDLE;
        VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
            .format=VK_FORMAT_R8G8B8A8_UNORM,.extent={32,32,1},.mipLevels=1,.arrayLayers=1,
            .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=(VkImageTiling)tiling,
            .usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .sharingMode=VK_SHARING_MODE_EXCLUSIVE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
        VkResult r=vkCreateImage(dev,&ci,NULL,&image);
        if(r==VK_SUCCESS) {
            VkMemoryRequirements req;vkGetImageMemoryRequirements(dev,image,&req);
            printf("MEMORY_IMAGE_TYPES tiling=%u types=%x size=%llu\n",tiling,req.memoryTypeBits,(unsigned long long)req.size);
            vkDestroyImage(dev,image,NULL);
        } else printf("MEMORY_IMAGE_TYPES tiling=%u create_result=%d\n",tiling,r);
    }
    for(uint32_t type=0;type<memory.memoryTypeCount;type++) {
        VkMemoryPropertyFlags flags=memory.memoryTypes[type].propertyFlags;
        printf("MEMORY_TYPE index=%u flags=%x heap=%u\n",type,flags,memory.memoryTypes[type].heapIndex);
        if(flags&VK_MEMORY_PROPERTY_PROTECTED_BIT){puts("MEMORY_TYPE_SKIP protected device not enabled");continue;}
        VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory allocation=VK_NULL_HANDLE;void *mapped=NULL;
        int failed=1;
        VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,
            .usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode=VK_SHARING_MODE_EXCLUSIVE};
        VkResult result=vkCreateBuffer(dev,&bi,NULL,&buffer);if(result!=VK_SUCCESS)goto cleanup_type;
        VkMemoryRequirements req;vkGetBufferMemoryRequirements(dev,buffer,&req);
        if(!(req.memoryTypeBits&(1u<<type))) {
            /* A type excluded for this buffer can still be allocated without
             * binding. Do not bind it illegally or count this as GPU coverage. */
            VkMemoryAllocateInfo only={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .allocationSize=size,.memoryTypeIndex=type};
            result=vkAllocateMemory(dev,&only,NULL,&allocation);
            printf("MEMORY_TYPE_ALLOCATION_ONLY index=%u flags=%x result=%d gpu_coverage=0\n",type,flags,result);
            failed=result!=VK_SUCCESS;
            if(!failed && (flags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                result=vkMapMemory(dev,allocation,0,VK_WHOLE_SIZE,0,&mapped);
                if(result!=VK_SUCCESS){failed=1;goto cleanup_type;}
                for(unsigned i=0;i<words;i++)((uint32_t*)mapped)[i]=i^0x5a5a0000u;
                VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=allocation,.size=VK_WHOLE_SIZE};
                result=vkFlushMappedMemoryRanges(dev,1,&range);
                if(result==VK_SUCCESS)result=vkInvalidateMappedMemoryRanges(dev,1,&range);
                if(result!=VK_SUCCESS){failed=1;goto cleanup_type;}
                for(unsigned i=0;i<words;i++)if(((uint32_t*)mapped)[i]!=(i^0x5a5a0000u))failed=1;
                printf("MEMORY_TYPE_HOST_ONLY index=%u cache_api_result=%d gpu_coverage=0\n",type,result);
            }
            goto cleanup_type;
        }
        VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};
        result=vkAllocateMemory(dev,&ai,NULL,&allocation);
        printf("MEMORY_TYPE_ALLOC index=%u flags=%x bytes=%llu result=%d\n",type,flags,(unsigned long long)req.size,result);
        if(result!=VK_SUCCESS)goto cleanup_type;
        result=vkBindBufferMemory(dev,buffer,allocation,0);if(result!=VK_SUCCESS)goto cleanup_type;
        if(flags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            result=vkMapMemory(dev,allocation,0,VK_WHOLE_SIZE,0,&mapped);if(result!=VK_SUCCESS)goto cleanup_type;
        }
        for(unsigned round=0;round<cycles;round++) {
        if(mapped) {
            for(unsigned i=0;i<words;i++)((uint32_t*)mapped)[i]=0x7a000000u^(type<<16)^(round<<10)^i;
            if(!(flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=allocation,.size=VK_WHOLE_SIZE};
                result=vkFlushMappedMemoryRanges(dev,1,&range);if(result!=VK_SUCCESS)goto cleanup_type;
            }
        }
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CHECK(vkResetCommandBuffer(cb,0));CHECK(vkBeginCommandBuffer(cb,&begin));
        VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask=mapped?VK_ACCESS_HOST_WRITE_BIT:VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};
        uint32_t fill=0xf5000000u|(round<<8)|type;
        if(!mapped)vkCmdFillBuffer(cb,buffer,0,size,fill);
        vkCmdPipelineBarrier(cb,mapped?VK_PIPELINE_STAGE_HOST_BIT:VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,NULL,0,NULL);
        VkBufferCopy copy={.size=size};vkCmdCopyBuffer(cb,buffer,readback,1,&copy);
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
        submit(dev,queue,cb,fence);
        unsigned bad=0;
        for(unsigned i=0;i<words;i++)if(readback_words[i]!=(mapped?(0x7a000000u^(type<<16)^(round<<10)^i):fill))bad++;
        if(bad){printf("MEMORY_TYPE_READBACK index=%u bad_words=%u\n",type,bad);goto cleanup_type;}
        if(mapped) {
            CHECK(vkResetCommandBuffer(cb,0));CHECK(vkBeginCommandBuffer(cb,&begin));
            vkCmdFillBuffer(cb,buffer,0,size,fill);
            vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
            submit(dev,queue,cb,fence);
            if(!(flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=allocation,.size=VK_WHOLE_SIZE};
                result=vkInvalidateMappedMemoryRanges(dev,1,&range);if(result!=VK_SUCCESS)goto cleanup_type;
            }
            for(unsigned i=0;i<words;i++)if(((uint32_t*)mapped)[i]!=fill)bad++;
            if(bad){printf("MEMORY_TYPE_HOST_READ index=%u bad_words=%u\n",type,bad);goto cleanup_type;}
        }
        }
        printf("MEMORY_TYPE_GPU_CYCLES index=%u cycles=%u host_write_gpu_read=%u gpu_write_host_read=%u\n",
            type,cycles,mapped!=NULL,mapped!=NULL);
        failed=0;
cleanup_type:
        if(mapped)vkUnmapMemory(dev,allocation);
        if(buffer)vkDestroyBuffer(dev,buffer,NULL);
        if(allocation)vkFreeMemory(dev,allocation,NULL);
        printf("MEMORY_TYPE_RESULT index=%u flags=%x result=%s last_vk_result=%d\n",type,flags,failed?"FAIL":"PASS",result);
        failures+=failed;passed+=!failed;
    }
    printf("MEMORY_TYPES_SUMMARY passed=%u failed=%u\n",passed,failures);
    vkUnmapMemory(dev,staging_memory);
    vkDestroyBuffer(dev,readback,NULL);vkFreeMemory(dev,staging_memory,NULL);
    return failures!=0;
}
