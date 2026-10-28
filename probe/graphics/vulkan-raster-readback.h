/* Included by vulkan-readback.c after dynamic Vulkan entrypoints and helpers.
 * Check actual vertex/fragment execution, not only a transfer clear. Boundary
 * pixels are excluded to avoid treating rasterization edge rules as failures. */
static void raster_readback(VkDevice dev,VkQueue queue,VkCommandBuffer cb,
                            VkFence fence,VkBuffer buffer,uint32_t *mapped) {
    VkImageCreateInfo ici={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
        .format=VK_FORMAT_R8G8B8A8_UNORM,.extent={64,64,1},.mipLevels=1,.arrayLayers=1,
        .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
    VkImage im; CHECK(vkCreateImage(dev,&ici,NULL,&im));
    VkMemoryRequirements req; vkGetImageMemoryRequirements(dev,im,&req);
    VkMemoryAllocateInfo mai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,
        .memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    VkDeviceMemory mem; CHECK(vkAllocateMemory(dev,&mai,NULL,&mem)); CHECK(vkBindImageMemory(dev,im,mem,0));
    VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=im,
        .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=ici.format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    VkImageView view; CHECK(vkCreateImageView(dev,&vi,NULL,&view));
    VkAttachmentDescription ad={.format=ici.format,.samples=VK_SAMPLE_COUNT_1_BIT,
        .loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    VkAttachmentReference ar={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&ar};
    VkSubpassDependency deps[2]={
        {.srcSubpass=VK_SUBPASS_EXTERNAL,.dstSubpass=0,.srcStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT,
         .dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
        {.srcSubpass=0,.dstSubpass=VK_SUBPASS_EXTERNAL,.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         .dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT}};
    VkRenderPassCreateInfo rpci={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,
        .pAttachments=&ad,.subpassCount=1,.pSubpasses=&sub,.dependencyCount=2,.pDependencies=deps};
    VkRenderPass pass; CHECK(vkCreateRenderPass(dev,&rpci,NULL,&pass));
    VkFramebufferCreateInfo fbi={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,
        .attachmentCount=1,.pAttachments=&view,.width=64,.height=64,.layers=1};
    VkFramebuffer fb; CHECK(vkCreateFramebuffer(dev,&fbi,NULL,&fb));
    VkShaderModule vert,frag;
    VkShaderModuleCreateInfo sm={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=sizeof(sq2_raster_vert),.pCode=sq2_raster_vert};
    CHECK(vkCreateShaderModule(dev,&sm,NULL,&vert));
    sm.codeSize=sizeof(sq2_raster_frag);sm.pCode=sq2_raster_frag;CHECK(vkCreateShaderModule(dev,&sm,NULL,&frag));
    VkPipelineShaderStageCreateInfo stages[2]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vert,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=frag,.pName="main"}};
    VkPipelineVertexInputStateCreateInfo vertex={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport viewport={0,0,64,64,0,1}; VkRect2D scissor={{0,0},{64,64}};
    VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};
    VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState ba={.colorWriteMask=15};
    VkPipelineColorBlendStateCreateInfo blend={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&ba};
    VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkPipelineLayout layout; CHECK(vkCreatePipelineLayout(dev,&pli,NULL,&layout));
    VkGraphicsPipelineCreateInfo gp={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,
        .pStages=stages,.pVertexInputState=&vertex,.pInputAssemblyState=&assembly,.pViewportState=&vp,
        .pRasterizationState=&raster,.pMultisampleState=&ms,.pColorBlendState=&blend,.layout=layout,.renderPass=pass};
    VkPipeline pipeline; CHECK(vkCreateGraphicsPipelines(dev,VK_NULL_HANDLE,1,&gp,NULL,&pipeline));
    for(unsigned frame=0;frame<8;frame++) {
        memset(mapped,0,64*64*4);
        CHECK(vkResetCommandBuffer(cb,0));
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; CHECK(vkBeginCommandBuffer(cb,&begin));
        VkClearValue clear={.color={.float32={frame%2 ? 1:0,0,frame%2 ? 0:1,1}}};
        VkRenderPassBeginInfo rb={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,
            .framebuffer=fb,.renderArea={{0,0},{64,64}},.clearValueCount=1,.pClearValues=&clear};
        vkCmdBeginRenderPass(cb,&rb,VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline); vkCmdDraw(cb,3,1,0,0); vkCmdEndRenderPass(cb);
        VkBufferImageCopy region={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={64,64,1}};
        vkCmdCopyImageToBuffer(cb,im,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&region);
        VkMemoryBarrier mb={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&mb,0,NULL,0,NULL);
        submit(dev,queue,cb,fence);
        unsigned inside=0,outside=0;
        for(unsigned y=0;y<64;y++) for(unsigned x=0;x<64;x++) {
            float px=x+0.5f,py=y+0.5f;
            float e0=py-8,e1=px-8-(py-8)*0.5f,e2=56-(py-8)*0.5f-px;
            int in=e0>1 && e1>1 && e2>1;
            int out=e0< -1 || e1< -1 || e2< -1;
            if(!in && !out)continue;
            uint32_t expected=in ? 0xff00ff00u : (frame%2 ? 0xff0000ffu : 0xffff0000u);
            if(mapped[y*64+x]!=expected) {
                fprintf(stderr,"Raster mismatch frame=%u x=%u y=%u got=%08x expected=%08x\n",frame,x,y,mapped[y*64+x],expected);exit(1);
            }
            inside+=in;outside+=out;
        }
        if(inside<500 || outside<2000){fprintf(stderr,"Invalid raster test coverage\n");exit(1);}
    }
    puts("RASTER_OK 8 frames 64x64 vertex/fragment triangle plus alternating clear, interior/exterior readback");
    vkDestroyPipeline(dev,pipeline,NULL);vkDestroyPipelineLayout(dev,layout,NULL);
    vkDestroyShaderModule(dev,vert,NULL);vkDestroyShaderModule(dev,frag,NULL);
    vkDestroyFramebuffer(dev,fb,NULL);vkDestroyRenderPass(dev,pass,NULL);
    vkDestroyImageView(dev,view,NULL);vkDestroyImage(dev,im,NULL);vkFreeMemory(dev,mem,NULL);
}
