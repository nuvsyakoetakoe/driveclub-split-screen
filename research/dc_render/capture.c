/* Copyright (C) 2026 nuvsyakoetakoe. SPDX-License-Identifier: GPL-2.0-only */
/* Opt-in Vulkan layer for one SDR DRIVECLUB device/swapchain. Samples final
 * game frames into PPM files in the test run directory. */
#define VK_NO_PROTOTYPES
#include <vulkan/vk_layer.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static ULONGLONG start,last;
#ifdef DC_NATIVE_MENU
static LONG menu_race_active;
static LONG menu_primary_pad_triggers;
static LONG menu_frontend_ready;
#include "experiments/native-menu/menu_graphics.inc"
#endif
#include "native_camera.inc"
#include "split_rumble.inc"
static void split_audio_view(const unsigned char *view);
#include "view_visibility.inc"
#include "split_audio.inc"
#include "split_engine_audio.inc"
#if defined(DC_NATIVE_MENU) && defined(DC_FEEDBACK_QA)
#include "experiments/engine-mix-qa/engine_mix_qa.inc"
#endif
#include "view_stencil.inc"
#include "mirror.inc"
#include "track_map.inc"
#include "race_results.inc"
#include "finish_line.inc"
#include "player_tags.inc"
#ifdef DC_EXPOSURE_QA
#include "experiments/exposure_qa.inc"
#else
#include "view_exposure.inc"
#endif
#ifdef DC_STOCK_HUD_QA
#include "experiments/stock_hud_qa.inc"
#endif

static PFN_vkGetInstanceProcAddr next_i;
static PFN_vkGetDeviceProcAddr next_d;
static PFN_vkSetDeviceLoaderData set_dispatch;
static VkInstance instance;
static VkDevice dev;
static VkPhysicalDevice gpu;
static uint32_t family;
static VkSwapchainKHR chain;
static VkExtent2D extent;
static VkFormat fmt;
static VkImage images[16];
static uint32_t image_count;
#define I(n) ((PFN_vk##n)next_i(instance,"vk" #n))
#define D(n) ((PFN_vk##n)next_d(dev,"vk" #n))
#include "hud.inc"
#include "dual_view.inc"
#include "ssr.inc"
#ifdef DC_NATIVE_MENU
#ifdef DC_FEEDBACK_QA
static void menu_report_viewports(uint32_t count,const VkViewport *views);
#endif
#include "experiments/native-menu/menu_motion.inc"
#endif
#ifdef DC_EXPOSURE_QA
#include "experiments/exposure_shaders_qa.inc"
#endif

static void *info(const void *p, VkStructureType s, VkLayerFunction f) {
    for (const VkLayerDeviceCreateInfo *it=p;it;it=it->pNext)
        if (it->sType==s && it->function==f) return (void *)it;
    return NULL;
}
static VkResult VKAPI_CALL make_instance(const VkInstanceCreateInfo *ci,const VkAllocationCallbacks *a,VkInstance *out) {
    VkLayerInstanceCreateInfo *li=info(ci->pNext,VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO,VK_LAYER_LINK_INFO);
    if(!li) return VK_ERROR_INITIALIZATION_FAILED;
#ifdef DC_NATIVE_MENU
    menu_prepare_graphics();
#endif
    next_i=li->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    li->u.pLayerInfo=li->u.pLayerInfo->pNext;
    VkResult r=((PFN_vkCreateInstance)next_i(NULL,"vkCreateInstance"))(ci,a,out);
    if(r==VK_SUCCESS) { instance=*out; start=GetTickCount64(); }
    return r;
}
static VkResult VKAPI_CALL make_device(VkPhysicalDevice phys,const VkDeviceCreateInfo *ci,const VkAllocationCallbacks *a,VkDevice *out) {
    VkLayerDeviceCreateInfo *li=info(ci->pNext,VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO,VK_LAYER_LINK_INFO);
    if(!li) return VK_ERROR_INITIALIZATION_FAILED;
    VkLayerDeviceCreateInfo *cb=info(ci->pNext,VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO,VK_LOADER_DATA_CALLBACK);
    set_dispatch=cb?cb->u.pfnSetDeviceLoaderData:NULL;
    next_d=li->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    PFN_vkCreateDevice make=(PFN_vkCreateDevice)li->u.pLayerInfo->pfnNextGetInstanceProcAddr(instance,"vkCreateDevice");
    li->u.pLayerInfo=li->u.pLayerInfo->pNext;
    VkResult r=make(phys,ci,a,out);
    if(r==VK_SUCCESS) {
        dev=*out; gpu=phys; family=ci->pQueueCreateInfos[0].queueFamilyIndex;
#ifdef DC_NATIVE_MENU
        menu_motion_device();
#endif
    }
    return r;
}
static VkResult VKAPI_CALL make_chain(VkDevice device,const VkSwapchainCreateInfoKHR *src,const VkAllocationCallbacks *a,VkSwapchainKHR *out) {
    VkSwapchainCreateInfoKHR ci=*src;
    VkSurfaceCapabilitiesKHR caps;
    if (I(GetPhysicalDeviceSurfaceCapabilitiesKHR)(gpu,ci.surface,&caps)==VK_SUCCESS)
        ci.imageUsage |= caps.supportedUsageFlags & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            ((getenv("DC_DUAL_VIEW") || getenv("DC_NATIVE_HUD"))?VK_IMAGE_USAGE_TRANSFER_DST_BIT:0));
    VkResult r=D(CreateSwapchainKHR)(device,&ci,a,out);
    if(r==VK_SUCCESS) {
        hud_reset();
        reset_atlas();
        dual_enabled &= !!(ci.imageUsage & VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        extent=ci.imageExtent; fmt=ci.imageFormat; chain=*out; image_count=0;
        if(ci.imageUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
            uint32_t n=16;
            if(D(GetSwapchainImagesKHR)(dev,chain,&n,images)==VK_SUCCESS) image_count=n;
        }
    }
    return r;
}
static void ppm(const unsigned char *src, ULONGLONG ms) {
    char name[MAX_PATH]; snprintf(name,sizeof(name),"frame-%08llu.ppm",ms);
    unsigned step=2;
#if defined(DC_NATIVE_MENU) && defined(DC_FEEDBACK_QA)
    const char *output=getenv("DC_QA_LIGHT_DIR");
    if(getenv("DC_QA_FOLIAGE_DIR")) { output=getenv("DC_QA_FOLIAGE_DIR"); step=1; }
    if(output) snprintf(name,sizeof(name),"%s/frame-%08llu.ppm",output,ms);
    const char *menuFrame=getenv("DC_QA_MENU_FRAME");
    if(menuFrame) snprintf(name,sizeof(name),"%s",menuFrame);
#endif
    FILE *f=fopen(name,"wb"); if(!f) return;
    const int rgb=fmt==VK_FORMAT_R8G8B8A8_UNORM||fmt==VK_FORMAT_R8G8B8A8_SRGB;
    uint32_t w=extent.width/step,h=extent.height/step;
    unsigned char *row=malloc(3*w);
    if(row) {
        fprintf(f,"P6\n%u %u\n255\n",w,h);
        for(uint32_t y=0;y<h;y++) {
            for(uint32_t x=0;x<w;x++) {
                const unsigned char *p=src+4*((size_t)y*step*extent.width+x*step);
                row[3*x]=p[rgb?0:2];row[3*x+1]=p[1];row[3*x+2]=p[rgb?2:0];
            }
            fwrite(row,3,w,f);
        }
        free(row);
    }
    fclose(f);
}
/* Return true only after consuming the present waits on this queue. */
static int sample(VkQueue q,const VkPresentInfoKHR *pi,VkImage img,ULONGLONG ms) {
    VkCommandPool pool=0; VkBuffer buf=0; VkDeviceMemory mem=0;
    VkDeviceSize bytes=(VkDeviceSize)extent.width*extent.height*4;
    VkCommandPoolCreateInfo pc={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};
    VkBufferCreateInfo bc={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    int consumed=0;
    if(D(CreateCommandPool)(dev,&pc,NULL,&pool)!=VK_SUCCESS || D(CreateBuffer)(dev,&bc,NULL,&buf)!=VK_SUCCESS) goto done;
    VkMemoryRequirements req; D(GetBufferMemoryRequirements)(dev,buf,&req);
    VkPhysicalDeviceMemoryProperties props; I(GetPhysicalDeviceMemoryProperties)(gpu,&props);
    uint32_t i=0,flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for(int cached=1;cached>=0;cached--) {
        uint32_t want=flags|(cached?VK_MEMORY_PROPERTY_HOST_CACHED_BIT:0);
        for(i=0;i<props.memoryTypeCount;i++) if((req.memoryTypeBits&(1u<<i)) && (props.memoryTypes[i].propertyFlags&want)==want) break;
        if(i<props.memoryTypeCount) break;
    }
    if(i==props.memoryTypeCount) goto done;
    VkMemoryAllocateInfo ma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=i};
    if(D(AllocateMemory)(dev,&ma,NULL,&mem)!=VK_SUCCESS || D(BindBufferMemory)(dev,buf,mem,0)!=VK_SUCCESS) goto done;
    VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.commandBufferCount=1};
    VkCommandBuffer cmd; if(D(AllocateCommandBuffers)(dev,&ca,&cmd)!=VK_SUCCESS) goto done;
    if(set_dispatch && set_dispatch(dev,cmd)!=VK_SUCCESS) goto done;
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if(D(BeginCommandBuffer)(cmd,&begin)!=VK_SUCCESS) goto done;
    VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.oldLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=img,
        .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    D(CmdPipelineBarrier)(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&b);
    VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={extent.width,extent.height,1}};
    D(CmdCopyImageToBuffer)(cmd,img,b.newLayout,buf,1,&copy);
    b.oldLayout=b.newLayout;b.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;b.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;b.dstAccessMask=0;
    VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    D(CmdPipelineBarrier)(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,1,&host,0,NULL,1,&b);
    if(D(EndCommandBuffer)(cmd)!=VK_SUCCESS) goto done;
    VkPipelineStageFlags stages[16];
    for(i=0;i<pi->waitSemaphoreCount;i++) stages[i]=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo sub={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd,
        .waitSemaphoreCount=pi->waitSemaphoreCount,.pWaitSemaphores=pi->pWaitSemaphores,.pWaitDstStageMask=stages};
    if(D(QueueSubmit)(q,1,&sub,VK_NULL_HANDLE)!=VK_SUCCESS) goto done;
    consumed=1;
    if(D(QueueWaitIdle)(q)==VK_SUCCESS) {
        void *mapped;
        if(D(MapMemory)(dev,mem,0,bytes,0,&mapped)==VK_SUCCESS) {
            unsigned char *cached=malloc((size_t)bytes);
            if(cached) {memcpy(cached,mapped,(size_t)bytes);ppm(cached,ms);free(cached);}
            D(UnmapMemory)(dev,mem);
        }
    }
done:
    if(pool) D(DestroyCommandPool)(dev,pool,NULL);
    if(buf) D(DestroyBuffer)(dev,buf,NULL);
    if(mem) D(FreeMemory)(dev,mem,NULL);
    return consumed;
}
#if defined(DC_NATIVE_MENU) || defined(DC_NATIVE_MENU_QA)
#include "experiments/native-menu/menu_keyboard_qa.inc"
#endif
#ifdef DC_NATIVE_MENU
#include "experiments/native-menu/menu_overdrives.inc"
#endif
static VkResult VKAPI_CALL present(VkQueue q,const VkPresentInfoKHR *p) {
#if defined(DC_NATIVE_MENU) || defined(DC_NATIVE_MENU_QA)
    menu_qa_poll();
#endif
    camera_poll();
#ifdef DC_NATIVE_MENU
    menu_motion_install();
    InterlockedIncrement(&menu_display_epoch);
#ifdef DC_FEEDBACK_QA
    menu_count_present();
#endif
    menu_overdrives_install();
#endif
    split_rumble_poll();
    mirror_install();
    visibility_install();
    split_audio_install();
    stencil_install();
    track_map_install();
    results_install();
    finish_line_install();
    if(finish_original_view) split_engine_install();
#if defined(DC_NATIVE_MENU) && defined(DC_FEEDBACK_QA)
    if(getenv("DC_QA_ENGINE_MIX")) engine_mix_install();
#endif
    player_tags_install();
#ifdef DC_EXPOSURE_QA
    exposure_qa_install();
#else
    exposure_install();
#endif
#ifdef DC_STOCK_HUD_QA
    hud_qa_install();
#endif
    VkPresentInfoKHR ci=*p; ULONGLONG ms=GetTickCount64()-start;
    int color=fmt==VK_FORMAT_B8G8R8A8_UNORM || fmt==VK_FORMAT_B8G8R8A8_SRGB || fmt==VK_FORMAT_R8G8B8A8_UNORM || fmt==VK_FORMAT_R8G8B8A8_SRGB;
    if(color && dual_enabled && ci.swapchainCount==1 && ci.waitSemaphoreCount<=16 &&
       ci.pSwapchains[0]==chain && ci.pImageIndices[0]<image_count) {
        if(compose(q,&ci,images[ci.pImageIndices[0]])) ci.waitSemaphoreCount=0;
    }
    if(color && getenv("DC_NATIVE_HUD") && ci.swapchainCount==1 && ci.waitSemaphoreCount<=16 &&
       ci.pSwapchains[0]==chain && ci.pImageIndices[0]<image_count) {
        hud_draw(q,&ci,ci.pImageIndices[0]);
    }
    int record=getenv("DC_RECORD")!=NULL;
    int capture=getenv("DC_CAPTURE") && !dual_enabled && ms>9000 && ms-last>=1800;
#if defined(DC_NATIVE_MENU) && defined(DC_FEEDBACK_QA)
    if(getenv("DC_QA_FOLIAGE_DIR") && menu_race_active) {
        static unsigned captured;
        if(menu_input_sequence>=780 && captured<12) { capture=1; captured++; }
    }
    if(getenv("DC_QA_LIGHT_DIR") && menu_race_active) {
        static unsigned previous;
        const unsigned frame=menu_input_sequence, slot=frame/30;
        const int transition=frame>=960 && frame<2010 && (frame-960)%150<9;
        if(frame>=840 && frame<2130 && frame!=previous && (slot!=previous/30 || transition)) {
            previous=frame;
            capture=1;
            printf("Split light sample: frame=%u ms=%llu\n",frame,ms);
        }
    }
#endif
    if(record && dual_enabled) capture=valid==3 && ms<23000 && ms-last>=65;
    if(record && !dual_enabled) {
        static ULONGLONG from,to;
        if(!to) {
            const char *start=getenv("DC_RECORD_FROM_MS"),*end=getenv("DC_RECORD_TO_MS");
            from=start?strtoull(start,NULL,10):15000;
            to=end?strtoull(end,NULL,10):23000;
        }
        capture|=ms>=from && ms<to && ms-last>=32;
    }
    if(capture && color && ci.swapchainCount==1 && ci.waitSemaphoreCount<=16 &&
       ci.pSwapchains[0]==chain && ci.pImageIndices[0]<image_count) {
        VkQueue first; D(GetDeviceQueue)(dev,family,0,&first);
        last=ms;
        if(first==q && sample(q,&ci,images[ci.pImageIndices[0]],ms)) ci.waitSemaphoreCount=0;
    }
    return D(QueuePresentKHR)(q,&ci);
}
#if defined(DC_NATIVE_MENU) && defined(DC_FEEDBACK_QA)
static void menu_report_viewports(uint32_t count,const VkViewport *views) {
    static _Thread_local struct { int x,y,w,h; } seen[32];
    static _Thread_local unsigned used;
    if(!menu_race_active || used==32) return;
    for(uint32_t i=0;i<count && used<32;i++) {
        const int x=(int)views[i].x,y=(int)views[i].y,w=(int)views[i].width,h=(int)views[i].height;
        if(w<800 || abs(h)<240 || w<2*abs(h)) continue;
        unsigned j=0;
        while(j<used && (seen[j].x!=x || seen[j].y!=y || seen[j].w!=w || seen[j].h!=h)) j++;
        if(j==used) {
            seen[used++]=(typeof(*seen)){x,y,w,h};
            printf("Split viewport QA: origin=(%d,%d) extent=(%d,%d)\n",x,y,w,h);
        }
    }
}
static void VKAPI_CALL menu_viewports(VkCommandBuffer cmd,uint32_t first,uint32_t count,const VkViewport *views) {
    menu_report_viewports(count,views);
    D(CmdSetViewport)(cmd,first,count,views);
}
static void VKAPI_CALL menu_counted_viewports(VkCommandBuffer cmd,uint32_t count,const VkViewport *views) {
    menu_report_viewports(count,views);
    typedef void (VKAPI_PTR *CountedFn)(VkCommandBuffer,uint32_t,const VkViewport *);
    CountedFn next=(CountedFn)next_d(dev,"vkCmdSetViewportWithCount");
    if(!next) next=(CountedFn)next_d(dev,"vkCmdSetViewportWithCountEXT");
    next(cmd,count,views);
}
#endif
static PFN_vkVoidFunction VKAPI_CALL device_proc(VkDevice d,const char *n) {
#ifdef DC_NATIVE_MENU
    PFN_vkVoidFunction scene=menu_scene_proc(n);
    if(scene) return scene;
#endif
#if defined(DC_NATIVE_MENU) && defined(DC_FEEDBACK_QA)
    if(!strcmp(n,"vkCmdSetViewport")) return (PFN_vkVoidFunction)menu_viewports;
    if(!strcmp(n,"vkCmdSetViewportWithCount") || !strcmp(n,"vkCmdSetViewportWithCountEXT"))
        return (PFN_vkVoidFunction)menu_counted_viewports;
#endif
    if(!strcmp(n,"vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)device_proc;
    if(!strcmp(n,"vkDestroyDevice")) return (PFN_vkVoidFunction)destroy_device;
    if(!strcmp(n,"vkQueuePresentKHR")) return (PFN_vkVoidFunction)present;
    if(!strcmp(n,"vkCreateSwapchainKHR")) return (PFN_vkVoidFunction)make_chain;
#ifdef DC_NATIVE_MENU
    if(!strcmp(n,"vkCmdCopyImage") && menu_original_copy_image) return (PFN_vkVoidFunction)menu_taa_copy_image;
    if(!strcmp(n,"vkCreateShaderModule")) return (PFN_vkVoidFunction)menu_motion_create_shader;
    if(!strcmp(n,"vkCreateComputePipelines")) return (PFN_vkVoidFunction)menu_motion_create_pipeline;
    if(!strcmp(n,"vkCmdPushDescriptorSetKHR") && menu_original_motion_descriptors)
        return (PFN_vkVoidFunction)menu_motion_descriptors;
    if(!strcmp(n,"vkCmdPushConstants")) return (PFN_vkVoidFunction)menu_motion_constants;
    if(!strcmp(n,"vkCmdDispatch")) return (PFN_vkVoidFunction)menu_motion_dispatch;
#endif
#ifdef DC_EXPOSURE_QA
    if(getenv("DC_QA_EXPOSURE_SHADERS")) {
        if(!strcmp(n,"vkCreateShaderModule")) return (PFN_vkVoidFunction)exposure_qa_shader;
        if(!strcmp(n,"vkCreateGraphicsPipelines")) return (PFN_vkVoidFunction)exposure_qa_graphics;
    }
#endif
    if(!strcmp(n,"vkCreateShaderModule") && getenv("DC_SPLIT_CAMERA") && !getenv("DC_LEGACY_SSR")) return (PFN_vkVoidFunction)ssr_shader;
    return next_d(d,n);
}
static PFN_vkVoidFunction VKAPI_CALL instance_proc(VkInstance i,const char *n) {
#ifdef DC_NATIVE_MENU
    PFN_vkVoidFunction scene=menu_scene_proc(n);
    if(scene) return scene;
#endif
#if defined(DC_NATIVE_MENU) && defined(DC_FEEDBACK_QA)
    if(!strncmp(n,"vkCmdSetViewport",16) && (!n[16] || !strcmp(n+16,"WithCount") || !strcmp(n+16,"WithCountEXT")))
        return device_proc(dev,n);
#endif
    if(!strcmp(n,"vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)instance_proc;
    if(!strcmp(n,"vkCreateInstance")) return (PFN_vkVoidFunction)make_instance;
    if(!strcmp(n,"vkCreateDevice")) return (PFN_vkVoidFunction)make_device;
#ifdef DC_NATIVE_MENU
    if(!strcmp(n,"vkCreateShaderModule") || !strcmp(n,"vkCreateComputePipelines") ||
       !strcmp(n,"vkCmdPushDescriptorSetKHR") || !strcmp(n,"vkCmdPushConstants") ||
       !strcmp(n,"vkCmdCopyImage") || !strcmp(n,"vkCmdDispatch")) return device_proc(dev,n);
#endif
#ifdef DC_EXPOSURE_QA
    if(getenv("DC_QA_EXPOSURE_SHADERS") && (!strcmp(n,"vkCreateShaderModule") || !strcmp(n,"vkCreateGraphicsPipelines")))
        return device_proc(dev,n);
#endif
    if(!strcmp(n,"vkCreateShaderModule") && getenv("DC_SPLIT_CAMERA") && !getenv("DC_LEGACY_SSR")) return device_proc(dev,n);
    if(!strcmp(n,"vkQueuePresentKHR")||!strcmp(n,"vkCreateSwapchainKHR")||!strcmp(n,"vkGetDeviceProcAddr")||!strcmp(n,"vkDestroyDevice")) return device_proc(dev,n);
    return next_i(i,n);
}
__declspec(dllexport) VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *v) {
    if(v->loaderLayerInterfaceVersion<2) return VK_ERROR_INITIALIZATION_FAILED;
    v->loaderLayerInterfaceVersion=2;v->pfnGetInstanceProcAddr=instance_proc;v->pfnGetDeviceProcAddr=device_proc;
    v->pfnGetPhysicalDeviceProcAddr=NULL;
    return VK_SUCCESS;
}
