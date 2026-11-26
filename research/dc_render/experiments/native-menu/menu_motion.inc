/* DRIVECLUB's late DLSS pass keeps the inputs of the last native TAA call.
 * In split screen, retain the complete pair and generate vectors per view. */
#include <stdbool.h>
typedef struct {
    VkDescriptorBufferInfo buffers[3];
    uint32_t offsets[3];
    float tan_half_fov_y;
} MenuTaaInputs;

typedef struct {
    float size[2],jitter[2];
    uint32_t offsets[4];
} MenuMotionPush;
_Static_assert(sizeof(MenuTaaInputs)==88 && sizeof(MenuMotionPush)==32,"DRIVECLUB TAA inputs");

static void (*menu_original_taa)(void *,uint32_t,uint32_t,uint32_t,uint32_t,const MenuTaaInputs *);
static void (*menu_original_reconstruct)(void *,uint32_t,uint32_t,uint32_t,uint64_t,bool);
static VkShaderModule menu_motion_shader;
static VkPipelineLayout menu_motion_layout,menu_bounded_layout;
static VkShaderModule menu_bounded_shader;
static PFN_vkCmdPushDescriptorSetKHR menu_original_motion_descriptors;
static PFN_vkCmdPushConstants menu_original_motion_constants;
static PFN_vkCmdDispatch menu_original_motion_dispatch;
static PFN_vkCmdDispatchBase menu_original_dispatch_base;
static PFN_vkCmdCopyImage menu_original_copy_image;
static uintptr_t menu_taa_copy_return;
#include "menu_composite.inc"
#ifdef DC_FEEDBACK_QA
static struct { volatile LONG active,taa,resolve,copies[2],sampled[2]; } menu_pass_counts;
static void menu_count_present(void) {
    static int enabled=-1;
    static unsigned frames;
    if(enabled<0) enabled=getenv("DC_QA_COUNT_TAA")!=NULL;
    if(!enabled || !menu_race_active || !visibility_active()) return;
    if(++frames==540) InterlockedExchange(&menu_pass_counts.active,1);
    if(frames==660) {
        InterlockedExchange(&menu_pass_counts.active,0);
        printf("Split passes over 120 presents: TAA=%ld resolves=%ld P1 copies=%ld P2 copies=%ld\n",
               menu_pass_counts.taa,menu_pass_counts.resolve,menu_pass_counts.copies[0],menu_pass_counts.copies[1]);
        printf("Split upscaled scene sampled: first composite=%ld second composite=%ld\n",
               menu_pass_counts.sampled[0],menu_pass_counts.sampled[1]);
    }
}
#endif
static _Thread_local struct {
    MenuTaaInputs inputs[2];
    uint32_t depth,gbuffer;
    unsigned count,copy_seat;
    int applied_mode;
    struct {
        MenuTaaInputs first;
        void *aa;
        uint64_t jitter;
        uint32_t color,depth,output,gbuffer;
        int pending,paired;
    } native;
    int compositing;
    VkCommandBuffer command;
    VkDescriptorImageInfo images[4];
    MenuMotionPush push;
} menu_motion;

static void (*menu_original_draw_record)(void *,void *,const void *,bool,uint32_t);
static volatile LONG menu_scene_generation;
static _Thread_local struct {
    void *aa;
    int waiting,in_draw,action;
} menu_late;
#include "menu_scene_draw.inc"
static void (*menu_original_redirect)(void*,const void*,void*,void*);
static void menu_redirect(void *aa,const void *mapping,void *view,void *layout) {
    menu_original_redirect(aa,mapping,view,layout);
    if(menu_late.in_draw && menu_late.action) {
        InterlockedExchangePointer(&menu_reconstructed_view,*(void**)view);
    }
}

static void menu_motion_device(void) {
    menu_scene_init();
    menu_original_copy_image=D(CmdCopyImage);
    menu_original_motion_descriptors=D(CmdPushDescriptorSetKHR);
    menu_original_motion_constants=D(CmdPushConstants);
    menu_original_motion_dispatch=D(CmdDispatch);
    menu_original_dispatch_base=D(CmdDispatchBase);
    if(!menu_original_dispatch_base) menu_original_dispatch_base=(PFN_vkCmdDispatchBase)D(CmdDispatchBaseKHR);
    menu_motion_shader=VK_NULL_HANDLE;
    menu_motion_layout=menu_bounded_layout=VK_NULL_HANDLE;
    menu_bounded_shader=VK_NULL_HANDLE;
}

static void menu_taa(void *aa,uint32_t color,uint32_t depth,uint32_t output,uint32_t gbuffer,
                     const MenuTaaInputs *inputs) {
    menu_display_pair.armed=0;
    const int split=menu_race_active && inputs && depth!=UINT32_MAX && gbuffer!=UINT32_MAX &&
        color!=UINT32_MAX && output!=UINT32_MAX && visibility_active();
    InterlockedExchange(&menu_composite_pair,split);
    if(!split) InterlockedExchangePointer(&menu_reconstructed_view,NULL);
    if(!split || menu_late.waiting) {
        menu_late.waiting=0;
        menu_motion.count=0;
    }
    if(split) {
#ifdef DC_FEEDBACK_QA
        if(menu_pass_counts.active) InterlockedIncrement(&menu_pass_counts.taa);
#endif
        int mode=*(const int*)((const unsigned char*)aa+0x1dc);
        if(menu_motion.applied_mode!=mode ||
           menu_motion.depth!=depth || menu_motion.gbuffer!=gbuffer) {
            menu_motion.count=0;
            menu_motion.native.pending=0;
            menu_motion.applied_mode=mode;
        }
        menu_motion.depth=depth;
        menu_motion.gbuffer=gbuffer;
        if(menu_motion.count<2) menu_motion.inputs[menu_motion.count]=*inputs;
        if(menu_motion.count<3) menu_motion.count++;
        /* The intercepted copy call exists only on DriveAA's late path.
         * Assign its region immediately, including the first frame after resize. */
        menu_motion.copy_seat=menu_motion.count;
    } else {
        menu_motion.count=0;
        menu_motion.native.pending=menu_motion.native.paired=0;
    }
    menu_original_taa(aa,color,depth,output,gbuffer,inputs);
    menu_motion.copy_seat=0;
}

static void VKAPI_CALL menu_taa_copy_image(VkCommandBuffer cmd,VkImage src,VkImageLayout sl,
        VkImage dst,VkImageLayout dl,uint32_t n,const VkImageCopy *regions) {
    VkImageCopy part;
    const unsigned seat=menu_motion.copy_seat;
    if((seat==1 || seat==2) && n==1 && (uintptr_t)__builtin_return_address(0)==menu_taa_copy_return &&
       regions->extent.depth==1 && regions->extent.height>=2 &&
       !regions->srcOffset.y && !regions->dstOffset.y) {
        part=*regions;
        uint32_t mid=part.extent.height/2;
        if(seat==2) part.srcOffset.y=part.dstOffset.y=(int32_t)mid;
        part.extent.height=seat==1?mid:part.extent.height-mid;
        regions=&part;
#ifdef DC_FEEDBACK_QA
        if(menu_pass_counts.active) InterlockedIncrement(&menu_pass_counts.copies[seat-1]);
        static LONG reported;
        LONG bit=1L<<seat;
        if(!(InterlockedOr(&reported,bit)&bit))
            printf("Split TAA copy: P%u y=%d size=%ux%u\n",seat,part.dstOffset.y,part.extent.width,part.extent.height);
#endif
    }
    menu_original_copy_image(cmd,src,sl,dst,dl,n,regions);
}

static void menu_motion_resolve(void *aa,uint32_t color,uint32_t depth,uint32_t output,uint64_t jitter,bool upscale) {
    /* Both guest TAA dispatches have already written their half of the output.
     * In DLAA the port otherwise evaluates the entire same target twice with
     * the same jitter, advancing the same temporal history for each camera. */
    if(!upscale && menu_motion.count==1 && menu_motion.depth==depth && menu_original_dispatch_base &&
       menu_motion_layout && extent.height && (menu_bounded_layout==menu_motion_layout || extent.height%16==0)) {
        const int same_targets=menu_motion.native.aa==aa && menu_motion.native.color==color &&
            menu_motion.native.depth==depth && menu_motion.native.output==output &&
            menu_motion.native.gbuffer==menu_motion.gbuffer;
        if(same_targets && menu_motion.native.pending && menu_motion.native.jitter==jitter) {
            menu_motion.inputs[1]=menu_motion.inputs[0];
            menu_motion.inputs[0]=menu_motion.native.first;
            menu_motion.count=2;
            menu_motion.native.pending=0;
            menu_motion.native.paired=1;
        } else {
            if(!same_targets || menu_motion.native.pending) menu_motion.native.paired=0;
            menu_motion.native.aa=aa;
            menu_motion.native.color=color;
            menu_motion.native.depth=depth;
            menu_motion.native.output=output;
            menu_motion.native.gbuffer=menu_motion.gbuffer;
            menu_motion.native.jitter=jitter;
            menu_motion.native.first=menu_motion.inputs[0];
            menu_motion.native.pending=1;
            if(menu_motion.native.paired) {
                menu_motion.count=0;
                return;
            }
        }
    } else menu_motion.native.pending=menu_motion.native.paired=0;
    menu_motion.compositing=menu_motion.count==2 && menu_motion.depth==depth &&
        InterlockedCompareExchange(&menu_race_active,0,0);
#ifdef DC_FEEDBACK_QA
    if(menu_pass_counts.active) InterlockedIncrement(&menu_pass_counts.resolve);
#endif
    menu_original_reconstruct(aa,color,depth,output,jitter,upscale);
    if(upscale && menu_motion.compositing && *((const unsigned char*)aa+0x1a8)) {
        menu_display_pair.aa=aa;
        menu_display_pair.color=color;
        menu_display_pair.epoch=menu_display_epoch;
        menu_display_pair.armed=1;
    }
    menu_motion.compositing=0;
    menu_motion.command=VK_NULL_HANDLE;
    menu_motion.count=0;
}

/* The lower camera finishes its post effects between the two display draws.
 * Keep the first native draw as a fallback and resolve from this frame when
 * the second draw binds the complete scene. */
static void menu_motion_reconstruct(void *aa,uint32_t color,uint32_t depth,uint32_t output,uint64_t jitter,bool upscale) {
    if(!upscale || !menu_original_redirect || !menu_late.in_draw || menu_motion.count!=2 || menu_motion.depth!=depth || !menu_race_active) {
        menu_late.waiting=0;
        menu_motion_resolve(aa,color,depth,output,jitter,upscale);
        return;
    }
    menu_late.aa=aa;
    menu_late.action=menu_late.waiting?2:1;
    if(menu_late.action==1) {
        ((unsigned char*)aa)[0x1a8]=0;
        *(uint32_t*)((unsigned char*)aa+0x1ac)=color;
        return;
    }
    menu_late.waiting=0;
    menu_motion_resolve(aa,color,depth,output,jitter,true);
}
static void menu_draw_record(void *raster,void *pipeline,const void *draw,bool indexed,uint32_t offset) {
    menu_late.in_draw=1;
    menu_late.action=0;
    menu_original_draw_record(raster,pipeline,draw,indexed,offset);
    menu_late.in_draw=0;
    if(!menu_late.action) return;
    unsigned char *state=menu_late.aa;
    state[0x1a8]=0;
    menu_display_pair.armed=0;
    if(menu_late.action==1) {
        menu_late.waiting=1;
        state[0x1b0]=1;
    }
}

static void menu_motion_install(void) {
    if(menu_original_taa || camera_installed!=1) return;
    /* DriveAA::OnTaa and DriveAA::Reconstruct in the checked Windows 0.4.2 executable. */
    const uintptr_t base=(uintptr_t)GetModuleHandleW(NULL);
    static const unsigned char copy_call[]={0x41,0xb8,1,0,0,0,0xff,0xd0};
    if(!memcmp((void*)(base+0x25a2be),copy_call,sizeof(copy_call)))
        menu_taa_copy_return=base+0x25a2c6;
    static const unsigned char taa_head[]={0x55,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x53,
        0x48,0x81,0xec,0x28,0x01,0x00,0x00};
    static const unsigned char reconstruct_head[]={0x55,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x53,
        0x48,0x81,0xec,0x08,0x09,0x00,0x00};
    if(memcmp((const void *)(base+0x259fc0),taa_head,sizeof(taa_head)) ||
       memcmp((const void *)(base+0x163d00),reconstruct_head,sizeof(reconstruct_head))) return;
    menu_original_reconstruct=(void (*)(void *,uint32_t,uint32_t,uint32_t,uint64_t,bool))camera_detour(base+0x163d00,
        (void *)menu_motion_reconstruct,sizeof(reconstruct_head),0);
    if(menu_original_reconstruct)
        menu_original_taa=(void (*)(void *,uint32_t,uint32_t,uint32_t,uint32_t,const MenuTaaInputs *))camera_detour(
            base+0x259fc0,(void *)menu_taa,sizeof(taa_head),0);
    if(menu_original_taa) {
        menu_composite_install(base);
        static const unsigned char draw_head[]={0x55,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x53,
            0x48,0x81,0xec,0xc8,0x02,0,0};
        if(!memcmp((void*)(base+0x1eb80),draw_head,sizeof(draw_head)))
            menu_original_draw_record=(void (*)(void *,void *,const void *,bool,uint32_t))camera_detour(
                base+0x1eb80,(void*)menu_draw_record,sizeof(draw_head),0);
        unsigned char redirect_head[sizeof(draw_head)];
        memcpy(redirect_head,draw_head,sizeof(draw_head));
        redirect_head[16]=0;
        if(!memcmp((void*)(base+0x415920),redirect_head,sizeof(redirect_head)))
            menu_original_redirect=(void (*)(void*,const void*,void*,void*))camera_detour(
                base+0x415920,(void*)menu_redirect,sizeof(draw_head),0);
    }
}

static int menu_motion_spirv_name(const uint32_t *code,size_t size,const char *name) {
    const size_t words=size/sizeof(*code),length=strlen(name)+1;
    for(size_t i=5;i<words;) {
        const size_t count=code[i]>>16;
        if(!count || count>words-i) return 0;
        const uint32_t op=code[i]&0xffff;
        const size_t skip=op==5?2:3; /* OpName or OpMemberName. */
        if((op==5 || op==6) && count>skip && (count-skip)*4>=length &&
           !memcmp(code+i+skip,name,length)) return 1;
        i+=count;
    }
    return 0;
}

#include "menu_sun.inc"
static VkResult VKAPI_CALL menu_motion_create_shader(VkDevice device,const VkShaderModuleCreateInfo *info,
                                                       const VkAllocationCallbacks *alloc,VkShaderModule *shader) {
    size_t sun_words=0;
    uint32_t *sun=menu_sun_shader(info->pCode,info->codeSize/4,&sun_words);
    if(sun) {
        VkShaderModuleCreateInfo edited=*info;
        edited.pCode=sun;
        edited.codeSize=sun_words*sizeof(*sun);
        VkResult status=D(CreateShaderModule)(device,&edited,alloc,shader);
        free(sun);
        if(status==VK_SUCCESS) {
            puts("Split sun: sampling occlusion in the active viewport");
            return status;
        }
    }
    /* Use the spare fourth offset as a signed scanline boundary. Extending
     * the existing pixel bounds check lets the shared 8-row workgroup at the
     * seam be dispatched twice without either camera writing the other's rows.
     * Ordinary dispatches set the spare word to zero. */
    if(info->codeSize==2715*sizeof(uint32_t)) {
        uint64_t hash=14695981039346656037ULL;
        for(size_t i=0;i<2715;i++) hash=(hash^info->pCode[i])*1099511628211ULL;
        if(hash==0x2c9e48aa06c08f62ULL) {
            uint32_t code[2800],id=info->pCode[3];
            memcpy(code,info->pCode,1235*sizeof(*code));
            uint32_t ops[]={
                6u<<16|65,41,id,38,39,95,       /* offsets.w */
                4u<<16|61,6,id+1,id,
                4u<<16|124,32,id+2,id+1,
                4u<<16|126,32,id+3,id+2,        /* -boundary */
                5u<<16|173,114,id+4,id+2,33,    /* bottom && y < boundary */
                5u<<16|177,114,id+5,127,id+2,
                5u<<16|167,114,id+6,id+4,id+5,
                5u<<16|177,114,id+7,id+2,33,    /* top && y >= -boundary */
                5u<<16|175,114,id+8,127,id+3,
                5u<<16|167,114,id+9,id+7,id+8,
                5u<<16|166,114,id+10,id+6,id+9,
                5u<<16|166,114,id+11,131,id+10,
            };
            size_t added=sizeof(ops)/sizeof(*ops);
            memcpy(code+1235,ops,sizeof(ops));
            memcpy(code+1235+added,info->pCode+1235,(2715-1235)*sizeof(*code));
            code[1244+added]=id+11; /* y bounds input to the existing OpPhi */
            code[3]=id+12;
            VkShaderModuleCreateInfo bounded=*info;
            bounded.pCode=code;
            bounded.codeSize=info->codeSize+sizeof(ops);
            if(D(CreateShaderModule)(device,&bounded,alloc,shader)==VK_SUCCESS) {
                menu_bounded_shader=menu_motion_shader=*shader;
                return VK_SUCCESS;
            }
        }
    }
    const VkResult result=getenv("DC_SPLIT_CAMERA") && !getenv("DC_LEGACY_SSR")?ssr_shader(device,info,alloc,shader):
        D(CreateShaderModule)(device,info,alloc,shader);
    if(result==VK_SUCCESS && menu_motion_spirv_name(info->pCode,info->codeSize,"depth_copy_img") &&
       menu_motion_spirv_name(info->pCode,info->codeSize,"object_data")) menu_motion_shader=*shader;
    return result;
}

static VkResult VKAPI_CALL menu_motion_create_pipeline(VkDevice device,VkPipelineCache cache,uint32_t count,
        const VkComputePipelineCreateInfo *infos,const VkAllocationCallbacks *alloc,VkPipeline *pipelines) {
    if(count!=1 || !menu_motion_shader || !menu_original_dispatch_base || infos[0].stage.module!=menu_motion_shader)
        return D(CreateComputePipelines)(device,cache,count,infos,alloc,pipelines);
    VkComputePipelineCreateInfo local=infos[0];
    local.flags|=VK_PIPELINE_CREATE_DISPATCH_BASE_BIT;
    const VkResult result=D(CreateComputePipelines)(device,cache,1,&local,alloc,pipelines);
    if(result==VK_SUCCESS) {
        menu_motion_layout=local.layout;
        menu_bounded_layout=menu_motion_shader==menu_bounded_shader?local.layout:VK_NULL_HANDLE;
    }
    menu_bounded_shader=VK_NULL_HANDLE;
    menu_motion_shader=VK_NULL_HANDLE;
    return result==VK_SUCCESS?result:D(CreateComputePipelines)(device,cache,count,infos,alloc,pipelines);
}

static void VKAPI_CALL menu_motion_descriptors(VkCommandBuffer cmd,VkPipelineBindPoint point,VkPipelineLayout layout,
        uint32_t set,uint32_t count,const VkWriteDescriptorSet *writes) {
    if(menu_motion.compositing && point==VK_PIPELINE_BIND_POINT_COMPUTE && layout==menu_motion_layout &&
       set==0 && count==7) {
        int matches=1;
        for(uint32_t i=0;i<7;i++) {
            if(writes[i].dstBinding!=i || writes[i].descriptorCount!=1) matches=0;
            if(i<3) {
                const VkDescriptorBufferInfo *expected=&menu_motion.inputs[1].buffers[i];
                const VkDescriptorBufferInfo *buffer=writes[i].pBufferInfo;
                if(!buffer || buffer->buffer!=expected->buffer || buffer->offset!=expected->offset ||
                   buffer->range!=expected->range) matches=0;
            } else if(!writes[i].pImageInfo) matches=0;
            else menu_motion.images[i-3]=*writes[i].pImageInfo;
        }
        if(matches) menu_motion.command=cmd;
    }
    if(point==VK_PIPELINE_BIND_POINT_GRAPHICS) menu_scene_descriptors(cmd,layout,set,count,writes);
    menu_original_motion_descriptors(cmd,point,layout,set,count,writes);
}

static void VKAPI_CALL menu_motion_constants(VkCommandBuffer cmd,VkPipelineLayout layout,
        VkShaderStageFlags stages,uint32_t offset,uint32_t size,const void *data) {
    if(menu_motion.compositing && layout==menu_motion_layout && stages==VK_SHADER_STAGE_COMPUTE_BIT &&
       !offset && size==sizeof(MenuMotionPush)) memcpy(&menu_motion.push,data,sizeof(MenuMotionPush));
    menu_scene_constants(cmd,layout,stages,offset,size,data);
    menu_original_motion_constants(cmd,layout,stages,offset,size,data);
}

static void VKAPI_CALL menu_motion_dispatch(VkCommandBuffer cmd,uint32_t x,uint32_t y,uint32_t z) {
    if(menu_motion.command==cmd && menu_motion.compositing) {
        menu_motion.command=VK_NULL_HANDLE;
        const uint32_t height=(uint32_t)menu_motion.push.size[1];
        const uint32_t middle=height/2,lower=middle/8,upper=(middle+7)/8;
        if(menu_original_dispatch_base && middle && (height%16==0 || menu_bounded_layout==menu_motion_layout) && y==(height+7)/8 && z==1 &&
           !memcmp(menu_motion.inputs[1].offsets,menu_motion.push.offsets,3*sizeof(uint32_t))) {
            VkWriteDescriptorSet writes[7]={0};
            for(int i=0;i<7;i++) writes[i]=(VkWriteDescriptorSet){
                .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstBinding=(uint32_t)i,.descriptorCount=1,
                .descriptorType=i<3?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:i<5?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .pBufferInfo=i<3?&menu_motion.inputs[0].buffers[i]:NULL,
                .pImageInfo=i>=3?&menu_motion.images[i-3]:NULL,
            };
            if(middle%8) {
                menu_motion.push.offsets[3]=middle;
                menu_original_motion_constants(cmd,menu_motion_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(MenuMotionPush),&menu_motion.push);
                menu_motion.push.offsets[3]=(uint32_t)-(int32_t)middle;
            }
            menu_original_dispatch_base(cmd,0,lower,0,x,y-lower,z);
            menu_original_motion_descriptors(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,menu_motion_layout,0,7,writes);
            memcpy(menu_motion.push.offsets,menu_motion.inputs[0].offsets,3*sizeof(uint32_t));
            menu_original_motion_constants(cmd,menu_motion_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(MenuMotionPush),&menu_motion.push);
            menu_original_dispatch_base(cmd,0,0,0,x,upper,z);
            static LONG reported;
            if(!InterlockedExchange(&reported,1)) puts("Split graphics: routed each native TAA view into its half of motion buffer");
            return;
        }
    }
    menu_original_motion_dispatch(cmd,x,y,z);
}
