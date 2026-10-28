/* Private diagnostic only. Bounded capture of original SPIR-V and pipeline
 * metadata; all API arguments/results remain unchanged. Captures stay private.
 * Included after wrapper Device/device_copy definitions, before intercept(). */
#pragma once
static struct { VkDevice device; VkShaderModule module; unsigned id; } shader_notes[48];
static atomic_uint shader_note_count, pipeline_note_count;
static VKAPI_ATTR VkResult VKAPI_CALL traced_create_shader(VkDevice d,
        const VkShaderModuleCreateInfo *info,const VkAllocationCallbacks *allocator,VkShaderModule *out) {
    struct Device saved;
    if(!device_copy(d,&saved))return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkCreateShaderModule fn=(PFN_vkCreateShaderModule)saved.gdpa(d,"vkCreateShaderModule");
    if(!fn)return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result=fn(d,info,allocator,out);
    unsigned id=atomic_fetch_add(&shader_note_count,1);
    if(id<48 && info && info->pCode && info->codeSize>=20 && info->codeSize<=262144 && !(info->codeSize%4)) {
        char path[160];
        snprintf(path,sizeof(path),"/data/local/tmp/sq2-vk-capture/shader.%d.%u.spv",getpid(),id);
        int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        size_t written=0;
        if(fd>=0){
            while(written<info->codeSize){
                ssize_t n=write(fd,(const char *)info->pCode+written,info->codeSize-written);
                if(n<0&&errno==EINTR)continue;
                if(n<=0)break;
                written+=(size_t)n;
            }
            close(fd);
        }
        if(result==VK_SUCCESS&&out&&*out){
            pthread_mutex_lock(&mutex);
            shader_notes[id].device=d;shader_notes[id].module=*out;shader_notes[id].id=id;
            pthread_mutex_unlock(&mutex);
        }
        __android_log_print(ANDROID_LOG_INFO,"SQ2VkPipeline","shader=%u result=%d bytes=%zu written=%zu version=%x generator=%x bound=%u",
            id,result,info->codeSize,written,info->pCode[1],info->pCode[2],info->pCode[3]);
    }
    return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL traced_create_graphics(VkDevice d,VkPipelineCache cache,
        uint32_t count,const VkGraphicsPipelineCreateInfo *infos,const VkAllocationCallbacks *allocator,VkPipeline *out) {
    struct Device saved;
    if(!device_copy(d,&saved))return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkCreateGraphicsPipelines fn=(PFN_vkCreateGraphicsPipelines)saved.gdpa(d,"vkCreateGraphicsPipelines");
    if(!fn)return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result=fn(d,cache,count,infos,allocator,out);
    unsigned serial=atomic_fetch_add(&pipeline_note_count,1);
    if(serial>=8||!infos)return result;
#define PIPE_LOG(...) __android_log_print(ANDROID_LOG_INFO,"SQ2VkPipeline",__VA_ARGS__)
    PIPE_LOG("pipeline_call=%u count=%u result=%d cache=%u",serial,count,result,cache!=VK_NULL_HANDLE);
    for(uint32_t i=0;i<count&&i<8;i++){
        const VkGraphicsPipelineCreateInfo *p=&infos[i];
        PIPE_LOG("pipeline=%u:%u flags=%x stages=%u subpass=%u base_index=%d next=%d",serial,i,p->flags,p->stageCount,p->subpass,p->basePipelineIndex,p->pNext?((const VkBaseInStructure *)p->pNext)->sType:-1);
        for(uint32_t j=0;j<p->stageCount&&j<6&&p->pStages;j++){
            const VkPipelineShaderStageCreateInfo *s=&p->pStages[j];unsigned id=UINT32_MAX;
            pthread_mutex_lock(&mutex);
            for(unsigned k=0;k<48;k++)if(shader_notes[k].device==d&&shader_notes[k].module==s->module)id=shader_notes[k].id;
            pthread_mutex_unlock(&mutex);
            PIPE_LOG("stage=%u:%u:%u bit=%x flags=%x shader=%u entry=%.64s spec=%u next=%d",serial,i,j,s->stage,s->flags,id,s->pName?s->pName:"(null)",s->pSpecializationInfo!=NULL,s->pNext?((const VkBaseInStructure *)s->pNext)->sType:-1);
        }
        if(p->pVertexInputState)PIPE_LOG("vertex=%u:%u stype=%d flags=%x bindings=%u attributes=%u",serial,i,p->pVertexInputState->sType,p->pVertexInputState->flags,p->pVertexInputState->vertexBindingDescriptionCount,p->pVertexInputState->vertexAttributeDescriptionCount);
        if(p->pInputAssemblyState)PIPE_LOG("assembly=%u:%u stype=%d topology=%d restart=%u",serial,i,p->pInputAssemblyState->sType,p->pInputAssemblyState->topology,p->pInputAssemblyState->primitiveRestartEnable);
        if(p->pTessellationState)PIPE_LOG("tess=%u:%u stype=%d patch=%u",serial,i,p->pTessellationState->sType,p->pTessellationState->patchControlPoints);
        if(p->pRasterizationState)PIPE_LOG("raster=%u:%u stype=%d discard=%u polygon=%d cull=%x front=%d",serial,i,p->pRasterizationState->sType,p->pRasterizationState->rasterizerDiscardEnable,p->pRasterizationState->polygonMode,p->pRasterizationState->cullMode,p->pRasterizationState->frontFace);
        if(p->pMultisampleState)PIPE_LOG("sample=%u:%u stype=%d count=%u sample_shading=%u",serial,i,p->pMultisampleState->sType,p->pMultisampleState->rasterizationSamples,p->pMultisampleState->sampleShadingEnable);
        if(p->pColorBlendState)PIPE_LOG("blend=%u:%u stype=%d attachments=%u logic=%u",serial,i,p->pColorBlendState->sType,p->pColorBlendState->attachmentCount,p->pColorBlendState->logicOpEnable);
        if(p->pDynamicState)for(uint32_t j=0;j<p->pDynamicState->dynamicStateCount&&j<32&&p->pDynamicState->pDynamicStates;j++)PIPE_LOG("dynamic=%u:%u index=%u state=%d",serial,i,j,p->pDynamicState->pDynamicStates[j]);
    }
#undef PIPE_LOG
    return result;
}
