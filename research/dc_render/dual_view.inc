/* Store a crop of each camera in one persistent image, then present that image.
 * Serialized submissions keep this proof of concept straightforward. */
static int dual_enabled, phase=-1, age, valid, initialized;
static VkImage atlas;
static VkDeviceMemory backing;
static VkCommandPool cmd_pool;
static VkCommandBuffer commands;
static VkQueue graphics;
static FILE *markers;
static char log_line[4096];
static size_t used;

static void reset_atlas(void) {
    if(atlas || cmd_pool) D(DeviceWaitIdle)(dev);
    if(cmd_pool) D(DestroyCommandPool)(dev,cmd_pool,NULL);
    if(atlas) D(DestroyImage)(dev,atlas,NULL);
    if(backing) D(FreeMemory)(dev,backing,NULL);
    atlas=0;cmd_pool=0;backing=0;valid=0;initialized=0;
    dual_enabled=getenv("DC_DUAL_VIEW")!=NULL;
}
static void VKAPI_CALL destroy_device(VkDevice d,const VkAllocationCallbacks *a) {
    hud_reset();
    reset_atlas();
    if(markers) {fclose(markers);markers=NULL;}
    D(DestroyDevice)(d,a);
}
static void read_phase(void) {
    if(!markers) markers=fopen("runtime.log","rb");
    if(!markers) return;
    clearerr(markers);
    char data[8192]; size_t n;
    while((n=fread(data,1,sizeof data,markers))!=0) {
        for(size_t i=0;i<n;i++) {
            if(data[i]=='\n') {
                log_line[used]=0;
                char *m=strstr(log_line,"] dcview ");
                if(m && (m[9]=='a'||m[9]=='b')) {
                    int target=m[9]=='b';
                    if(phase!=target) {phase=target;age=0;}
                }
                used=0;
            } else if(used+1<sizeof log_line) log_line[used++]=data[i];
        }
    }
}
static int create_atlas(void) {
    VkImageCreateInfo ic={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
        .format=fmt,.extent={extent.width,extent.height,1},.mipLevels=1,.arrayLayers=1,
        .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    if(D(CreateImage)(dev,&ic,NULL,&atlas)!=VK_SUCCESS) return 0;
    VkMemoryRequirements req; D(GetImageMemoryRequirements)(dev,atlas,&req);
    VkPhysicalDeviceMemoryProperties props; I(GetPhysicalDeviceMemoryProperties)(gpu,&props);
    uint32_t i=0;
    for(;i<props.memoryTypeCount;i++) if((req.memoryTypeBits&(1u<<i)) && (props.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) break;
    if(i==props.memoryTypeCount) return 0;
    VkMemoryAllocateInfo ma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=i};
    if(D(AllocateMemory)(dev,&ma,NULL,&backing)!=VK_SUCCESS || D(BindImageMemory)(dev,atlas,backing,0)!=VK_SUCCESS) return 0;
    VkCommandPoolCreateInfo pc={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family,
        .flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
    if(D(CreateCommandPool)(dev,&pc,NULL,&cmd_pool)!=VK_SUCCESS) return 0;
    VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=cmd_pool,.commandBufferCount=1};
    if(D(AllocateCommandBuffers)(dev,&ca,&commands)!=VK_SUCCESS) return 0;
    return !set_dispatch || set_dispatch(dev,commands)==VK_SUCCESS;
}
static void transition(VkImage img,VkImageLayout from,VkImageLayout to,VkAccessFlags src,VkAccessFlags dst) {
    VkImageMemoryBarrier bar={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask=src,.dstAccessMask=dst,.oldLayout=from,.newLayout=to,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=img,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    D(CmdPipelineBarrier)(commands,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0,0,NULL,0,NULL,1,&bar);
}
static int compose(VkQueue q,const VkPresentInfoKHR *pi,VkImage img) {
    read_phase(); if(phase<0) return 0;
    if(!graphics) D(GetDeviceQueue)(dev,family,0,&graphics);
    if(graphics!=q) return 0;
    if(!atlas && !create_atlas()) {reset_atlas();dual_enabled=0;return 0;}
    /* Let already queued frames from the previous camera drain. */
    int update=++age==3;
    if(!update && valid!=3) return 0;
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if(D(ResetCommandBuffer)(commands,0)!=VK_SUCCESS || D(BeginCommandBuffer)(commands,&begin)!=VK_SUCCESS) return 0;
    const VkImageLayout src=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,dst=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,presented=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkImageLayout active=presented;
    VkImageSubresourceLayers layer={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
    if(update) {
        transition(atlas,initialized?src:VK_IMAGE_LAYOUT_UNDEFINED,dst,initialized?VK_ACCESS_TRANSFER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);
        transition(img,presented,src,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        uint32_t half=extent.width/2;
        /* With four guest frames per camera, the third presentation still
         * contains the prior requested camera on the tested 0.4.2 renderer. */
        int right=!phase;
        VkImageCopy crop={.srcSubresource=layer,.dstSubresource=layer,.srcOffset={(int)(extent.width/4),0,0},
            .dstOffset={right?(int)half:0,0,0},.extent={right?extent.width-half:half,extent.height,1}};
        D(CmdCopyImage)(commands,img,src,atlas,dst,1,&crop);
        transition(atlas,dst,src,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        active=src;
    }
    int next_valid=update?(valid|(1<<phase)):valid;
    if(next_valid==3) {
        transition(img,active,dst,active==src?VK_ACCESS_TRANSFER_READ_BIT:VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
        VkImageCopy full={.srcSubresource=layer,.dstSubresource=layer,.extent={extent.width,extent.height,1}};
        D(CmdCopyImage)(commands,atlas,src,img,dst,1,&full);
        active=dst;
    }
    transition(img,active,presented,active==dst?VK_ACCESS_TRANSFER_WRITE_BIT:VK_ACCESS_TRANSFER_READ_BIT,0);
    if(D(EndCommandBuffer)(commands)!=VK_SUCCESS) return 0;
    VkPipelineStageFlags stages[16]; for(uint32_t i=0;i<pi->waitSemaphoreCount;i++) stages[i]=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo sub={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.pWaitSemaphores=pi->pWaitSemaphores,
        .waitSemaphoreCount=pi->waitSemaphoreCount,.pWaitDstStageMask=stages,.commandBufferCount=1,.pCommandBuffers=&commands};
    if(D(QueueSubmit)(q,1,&sub,0)!=VK_SUCCESS) return 0;
    D(QueueWaitIdle)(q);valid=next_valid;initialized=1;return 1;
}
