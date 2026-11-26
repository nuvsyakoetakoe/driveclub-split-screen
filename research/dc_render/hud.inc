/* Two small HUD cards over the engine's native viewports. Each swapchain image
 * owns its upload buffer, command buffer, semaphore and reuse fence. */
typedef struct {
    int speed, gear, position, lap, laps, finished, elapsed_ms;
} HudCar;
typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    void *mapped;
    VkCommandBuffer command;
    VkFence fence;
    VkSemaphore ready;
} HudFrame;
static HudFrame hud_frames[16];
static VkCommandPool hud_pool;
static HDC hud_dc;
static HBITMAP hud_bitmap, hud_old_bitmap;
static HFONT hud_small, hud_large;
static unsigned char *hud_pixels;
static uint32_t hud_width, hud_height;
static HudCar hud_cars[2];
static unsigned char *hud_cache;
static FILE *hud_log;
static char hud_line[2048];
static size_t hud_line_size;
static int hud_active, hud_dirty, hud_failed;

static void hud_reset(void) {
    if(hud_pool) D(DeviceWaitIdle)(dev);
    for(uint32_t i=0;i<16;i++) {
        HudFrame *f=&hud_frames[i];
        if(f->mapped) D(UnmapMemory)(dev,f->memory);
        if(f->buffer) D(DestroyBuffer)(dev,f->buffer,NULL);
        if(f->memory) D(FreeMemory)(dev,f->memory,NULL);
        if(f->fence) D(DestroyFence)(dev,f->fence,NULL);
        if(f->ready) D(DestroySemaphore)(dev,f->ready,NULL);
        memset(f,0,sizeof(*f));
    }
    if(hud_pool) D(DestroyCommandPool)(dev,hud_pool,NULL);
    hud_pool=0;
    if(hud_dc) {
        SelectObject(hud_dc,hud_old_bitmap);
        DeleteObject(hud_bitmap);
        DeleteDC(hud_dc);
    }
    if(hud_small) DeleteObject(hud_small);
    if(hud_large) DeleteObject(hud_large);
    hud_dc=NULL; hud_bitmap=NULL; hud_small=NULL; hud_large=NULL;
    free(hud_cache); hud_cache=NULL;
    if(hud_log) fclose(hud_log);
    hud_log=NULL; hud_line_size=0; hud_active=0; hud_failed=0;
}

static void hud_read(void) {
    if(!hud_log) hud_log=fopen("runtime.log","rb");
    if(!hud_log) return;
    clearerr(hud_log);
    char chunk[4096]; size_t n;
    while((n=fread(chunk,1,sizeof(chunk),hud_log))>0) {
        for(size_t i=0;i<n;i++) {
            char c=chunk[i];
            if(c=='\n') {
                hud_line[hud_line_size]=0;
                if(strstr(hud_line,"] split_hud_hide")) hud_active=0;
                char *p=strstr(hud_line,"] split_hud ");
                int seat; HudCar car;
                if(p && sscanf(p+12,"%d %d %d %d %d %d %d %d",&seat,&car.speed,&car.gear,
                    &car.position,&car.lap,&car.laps,&car.finished,&car.elapsed_ms)==8 && seat>=0 && seat<2) {
                    hud_cars[seat]=car; hud_active|=1<<seat; hud_dirty=1;
                }
                hud_line_size=0;
            } else if(hud_line_size+1<sizeof(hud_line)) {
                hud_line[hud_line_size++]=c;
            }
        }
    }
}

static void hud_text(int y, int large, const char *text) {
    HGDIOBJ previous=SelectObject(hud_dc,large?hud_large:hud_small);
    TextOutA(hud_dc,(int)hud_width/14,y,text,(int)strlen(text));
    SelectObject(hud_dc,previous);
}

static void hud_rasterize(void) {
    const size_t bytes=(size_t)hud_width*hud_height*4;
    for(int seat=0;seat<2;seat++) {
        for(size_t p=0;p<bytes;p+=4) {
            hud_pixels[p]=19; hud_pixels[p+1]=19; hud_pixels[p+2]=19; hud_pixels[p+3]=255;
        }
        SetBkMode(hud_dc,TRANSPARENT);
        SetTextColor(hud_dc,RGB(220,220,220));
        char text[100];
        const char *name=getenv(seat?"DC_HUD_P2":"DC_HUD_P1");
        snprintf(text,sizeof(text),"P%d   %s",seat+1,name?name:(seat?"MASERATI":"AUDI R8"));
        HGDIOBJ previous=SelectObject(hud_dc,hud_small);
        RECT name_rect={(LONG)hud_width/14,(LONG)hud_height/14,
            (LONG)hud_width-(LONG)hud_width/14,(LONG)hud_height*24/100};
        DrawTextA(hud_dc,text,-1,&name_rect,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        SelectObject(hud_dc,previous);
        HudCar car=hud_cars[seat];
        SetTextColor(hud_dc,RGB(255,255,255));
        char gear[16];
        if(car.gear<0) strcpy(gear,"R");
        else if(car.gear==0) strcpy(gear,"N");
        else snprintf(gear,sizeof(gear),"%d",car.gear);
        snprintf(text,sizeof(text),"%d km/h   %s",car.speed,gear);
        hud_text((int)hud_height*25/100,1,text);
        snprintf(text,sizeof(text),"POS %d/8     LAP %d/%d",car.position,car.lap,car.laps);
        hud_text((int)hud_height*63/100,0,text);
        int tenths=car.elapsed_ms/100;
        snprintf(text,sizeof(text),"%s%d:%02d.%d",car.finished?"FINISH  ":"",
            tenths/600,(tenths/10)%60,tenths%10);
        hud_text((int)hud_height*81/100,0,text);
        GdiFlush();
        unsigned char *dst=hud_cache+seat*bytes;
        memcpy(dst,hud_pixels,bytes);
        if(fmt==VK_FORMAT_R8G8B8A8_UNORM || fmt==VK_FORMAT_R8G8B8A8_SRGB) {
            for(size_t p=0;p<bytes;p+=4) {
                unsigned char b=dst[p]; dst[p]=dst[p+2]; dst[p+2]=b;
            }
        }
    }
    hud_dirty=0;
}

static int hud_prepare(void) {
    hud_width=extent.height*240/1080;
    hud_height=extent.height*140/1080;
    if(hud_width<120 || hud_height<70) return 0;
    hud_dc=CreateCompatibleDC(NULL);
    BITMAPINFO bi={0};
    bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=(LONG)hud_width;
    bi.bmiHeader.biHeight=-(LONG)hud_height;
    bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32;
    hud_bitmap=CreateDIBSection(hud_dc,&bi,DIB_RGB_COLORS,(void**)&hud_pixels,NULL,0);
    if(!hud_dc || !hud_bitmap) return 0;
    hud_old_bitmap=SelectObject(hud_dc,hud_bitmap);
    hud_small=CreateFontA(-(int)hud_height*15/140,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,"Arial");
    hud_large=CreateFontA(-(int)hud_height*31/140,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,
        ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,"Arial");
    const VkDeviceSize bytes=(VkDeviceSize)hud_width*hud_height*8;
    hud_cache=malloc((size_t)bytes);
    if(!hud_cache || !hud_small || !hud_large) return 0;
    VkCommandPoolCreateInfo pc={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex=family,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
    if(D(CreateCommandPool)(dev,&pc,NULL,&hud_pool)!=VK_SUCCESS) return 0;
    VkPhysicalDeviceMemoryProperties props; I(GetPhysicalDeviceMemoryProperties)(gpu,&props);
    for(uint32_t i=0;i<image_count;i++) {
        HudFrame *f=&hud_frames[i];
        VkBufferCreateInfo bc={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
        if(D(CreateBuffer)(dev,&bc,NULL,&f->buffer)!=VK_SUCCESS) return 0;
        VkMemoryRequirements req; D(GetBufferMemoryRequirements)(dev,f->buffer,&req);
        uint32_t type=0, flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for(;type<props.memoryTypeCount;type++)
            if((req.memoryTypeBits&(1u<<type)) && (props.memoryTypes[type].propertyFlags&flags)==flags) break;
        if(type==props.memoryTypeCount) return 0;
        VkMemoryAllocateInfo ma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};
        if(D(AllocateMemory)(dev,&ma,NULL,&f->memory)!=VK_SUCCESS ||
           D(BindBufferMemory)(dev,f->buffer,f->memory,0)!=VK_SUCCESS ||
           D(MapMemory)(dev,f->memory,0,bytes,0,&f->mapped)!=VK_SUCCESS) return 0;
        VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool=hud_pool,.commandBufferCount=1};
        if(D(AllocateCommandBuffers)(dev,&ca,&f->command)!=VK_SUCCESS) return 0;
        if(set_dispatch && set_dispatch(dev,f->command)!=VK_SUCCESS) return 0;
        VkFenceCreateInfo fc={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,.flags=VK_FENCE_CREATE_SIGNALED_BIT};
        VkSemaphoreCreateInfo sc={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if(D(CreateFence)(dev,&fc,NULL,&f->fence)!=VK_SUCCESS ||
           D(CreateSemaphore)(dev,&sc,NULL,&f->ready)!=VK_SUCCESS) return 0;
    }
    hud_dirty=1;
    return 1;
}

static int hud_draw(VkQueue q,VkPresentInfoKHR *pi,uint32_t index) {
    if(hud_failed) return 0;
    hud_read();
    if(hud_active!=3) return 0;
    VkQueue graphics_queue; D(GetDeviceQueue)(dev,family,0,&graphics_queue);
    if(q!=graphics_queue) return 0;
    if(!hud_pool && !hud_prepare()) {hud_reset();hud_failed=1;return 0;}
    HudFrame *f=&hud_frames[index];
    if(D(WaitForFences)(dev,1,&f->fence,VK_TRUE,UINT64_MAX)!=VK_SUCCESS) return 0;
    if(hud_dirty) hud_rasterize();
    memcpy(f->mapped,hud_cache,(size_t)hud_width*hud_height*8);
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if(D(ResetCommandBuffer)(f->command,0)!=VK_SUCCESS || D(BeginCommandBuffer)(f->command,&begin)!=VK_SUCCESS) return 0;
    VkImageMemoryBarrier bar={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=images[index],.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    D(CmdPipelineBarrier)(f->command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&bar);
    const uint32_t margin=extent.height*20/1080;
    VkBufferImageCopy regions[2]={0};
    for(uint32_t seat=0;seat<2;seat++) {
        regions[seat].bufferOffset=(VkDeviceSize)seat*hud_width*hud_height*4;
        regions[seat].imageSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        regions[seat].imageOffset=(VkOffset3D){(int)(extent.width-hud_width-margin),
            (int)(extent.height*(seat+1)/2-hud_height-margin),0};
        regions[seat].imageExtent=(VkExtent3D){hud_width,hud_height,1};
    }
    D(CmdCopyBufferToImage)(f->command,f->buffer,images[index],bar.newLayout,2,regions);
    bar.oldLayout=bar.newLayout; bar.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    bar.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; bar.dstAccessMask=0;
    D(CmdPipelineBarrier)(f->command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,NULL,0,NULL,1,&bar);
    if(D(EndCommandBuffer)(f->command)!=VK_SUCCESS) return 0;
    VkPipelineStageFlags stages[16];
    for(uint32_t i=0;i<pi->waitSemaphoreCount;i++) stages[i]=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo sub={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=pi->waitSemaphoreCount,
        .pWaitSemaphores=pi->pWaitSemaphores,.pWaitDstStageMask=stages,
        .commandBufferCount=1,.pCommandBuffers=&f->command,.signalSemaphoreCount=1,.pSignalSemaphores=&f->ready};
    if(D(ResetFences)(dev,1,&f->fence)!=VK_SUCCESS || D(QueueSubmit)(q,1,&sub,f->fence)!=VK_SUCCESS) {
        /* A failed submission leaves the reuse fence unsignalled. */
        hud_failed=1;
        fprintf(stderr,"Split screen HUD submission failed\n");
        return 0;
    }
    pi->waitSemaphoreCount=1; pi->pWaitSemaphores=&f->ready;
    return 1;
}
