/* The retail display pass is a triangle with one vertex stream, push constants
 * and push descriptors. Reissue the upper triangle with the current resolved
 * color immediately before the lower triangle, preserving each view's inputs. */
enum { MENU_SCENE_BINDINGS=20, MENU_SCENE_CONSTANTS=128 };
typedef struct {
    VkCommandBuffer cmd;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkBuffer vertex;
    VkDeviceSize vertex_offset,vertex_size,vertex_stride;
    int sized_vertex,has_size,has_stride;
    VkViewport viewport;
    VkRect2D scissor;
    VkShaderStageFlags stages;
    uint32_t constant_size,count;
    unsigned char constants[MENU_SCENE_CONSTANTS];
    VkWriteDescriptorSet writes[MENU_SCENE_BINDINGS];
    VkDescriptorBufferInfo buffers[MENU_SCENE_BINDINGS];
    VkDescriptorImageInfo images[MENU_SCENE_BINDINGS];
    VkImageView source,attachment;
    int source_slot;
    LONG generation, epoch;
} MenuSceneDraw;
static void *volatile menu_reconstructed_view;
typedef struct {
    VkCommandBuffer key;
    MenuSceneDraw draw;
    int armed,counted_view,counted_scissor;
} MenuSceneState;
enum { MENU_COMMAND_SLOTS=1024 };
static MenuSceneState menu_draw_states[MENU_COMMAND_SLOTS];
static void *volatile menu_display_layout;
static volatile LONG menu_color_slot;
/* The port hands its command buffers between recording workers. Cache their
 * dynamic state by Vulkan handle. Each buffer has only one writer at a time. */
static MenuSceneState *menu_scene_state(VkCommandBuffer cmd) {
    static _Thread_local MenuSceneState *last;
    static _Thread_local MenuSceneState overflow;
    if(last && last->key==cmd) return last;
    uintptr_t bits=(uintptr_t)cmd;
    unsigned slot=(unsigned)((bits>>11)^(bits>>17)^(bits>>23));
    for(unsigned probe=0;probe<MENU_COMMAND_SLOTS;probe++) {
        MenuSceneState *state=&menu_draw_states[(slot+probe)%MENU_COMMAND_SLOTS];
        if(!state->key)
            InterlockedCompareExchangePointer((void *volatile *)&state->key,cmd,NULL);
        if(state->key==cmd) return last=state;
    }
    if(overflow.key!=cmd) {
        memset(&overflow,0,sizeof(overflow));
        overflow.key=cmd;
    }
    return &overflow;
}
static int menu_scene_active(void) {
    return menu_race_active && menu_composite_pair && menu_reconstructed_view;
}
#define menu_scene (*menu_scene_state(cmd))
static SRWLOCK menu_upper_lock=SRWLOCK_INIT;
static MenuSceneDraw menu_upper;
static PFN_vkCmdDraw menu_vk_draw;
static PFN_vkCmdBindVertexBuffers menu_vk_vertex;
static PFN_vkCmdBindVertexBuffers2EXT menu_vk_vertex2;
static PFN_vkCmdBeginRenderingKHR menu_vk_begin_rendering;
static PFN_vkCmdBindPipeline menu_vk_pipeline;
static PFN_vkCmdSetViewport menu_vk_viewport;
static PFN_vkCmdSetScissor menu_vk_scissor;
static PFN_vkCmdSetViewportWithCountEXT menu_vk_viewport_count;
static PFN_vkCmdSetScissorWithCountEXT menu_vk_scissor_count;

static void menu_scene_init(void) {
    memset(menu_draw_states,0,sizeof(menu_draw_states));
    menu_display_layout=menu_reconstructed_view=NULL;
    menu_vk_draw=D(CmdDraw);
    menu_vk_vertex=D(CmdBindVertexBuffers);
    menu_vk_vertex2=(PFN_vkCmdBindVertexBuffers2EXT)next_d(dev,"vkCmdBindVertexBuffers2");
    if(!menu_vk_vertex2) menu_vk_vertex2=D(CmdBindVertexBuffers2EXT);
    menu_vk_begin_rendering=(PFN_vkCmdBeginRenderingKHR)next_d(dev,"vkCmdBeginRendering");
    if(!menu_vk_begin_rendering) menu_vk_begin_rendering=D(CmdBeginRenderingKHR);
    menu_vk_pipeline=D(CmdBindPipeline);
    menu_vk_viewport=D(CmdSetViewport);
    menu_vk_scissor=D(CmdSetScissor);
    menu_vk_viewport_count=(PFN_vkCmdSetViewportWithCountEXT)next_d(dev,"vkCmdSetViewportWithCount");
    menu_vk_scissor_count=(PFN_vkCmdSetScissorWithCountEXT)next_d(dev,"vkCmdSetScissorWithCount");
    if(!menu_vk_viewport_count) menu_vk_viewport_count=D(CmdSetViewportWithCountEXT);
    if(!menu_vk_scissor_count) menu_vk_scissor_count=D(CmdSetScissorWithCountEXT);
}
static void VKAPI_CALL menu_scene_vertex(VkCommandBuffer cmd,uint32_t first,uint32_t count,
        const VkBuffer *buffers,const VkDeviceSize *offsets) {
    if(menu_scene_active()) {
        menu_scene.draw.sized_vertex=0;
        menu_scene.draw.vertex=!first && count==1?buffers[0]:VK_NULL_HANDLE;
        if(menu_scene.draw.vertex) menu_scene.draw.vertex_offset=offsets[0];
    }
    menu_vk_vertex(cmd,first,count,buffers,offsets);
}
static void VKAPI_CALL menu_scene_vertex2(VkCommandBuffer cmd,uint32_t first,uint32_t count,
        const VkBuffer *buffers,const VkDeviceSize *offsets,const VkDeviceSize *sizes,const VkDeviceSize *strides) {
    if(menu_scene_active()) {
        MenuSceneDraw *d=&menu_scene.draw;
        d->vertex=!first && count==1?buffers[0]:VK_NULL_HANDLE;
        if(d->vertex) {
            d->sized_vertex=1;
            d->vertex_offset=offsets[0];
            d->has_size=sizes!=NULL;
            d->has_stride=strides!=NULL;
            if(sizes) d->vertex_size=sizes[0];
            if(strides) d->vertex_stride=strides[0];
        }
    }
    menu_vk_vertex2(cmd,first,count,buffers,offsets,sizes,strides);
}
static void VKAPI_CALL menu_scene_begin_rendering(VkCommandBuffer cmd,const VkRenderingInfoKHR *info) {
    if(menu_scene_active())
        menu_scene.draw.attachment=info->colorAttachmentCount==1?info->pColorAttachments[0].imageView:VK_NULL_HANDLE;
    menu_vk_begin_rendering(cmd,info);
}
static void VKAPI_CALL menu_scene_pipeline(VkCommandBuffer cmd,VkPipelineBindPoint point,VkPipeline pipeline) {
    if(point==VK_PIPELINE_BIND_POINT_GRAPHICS && menu_scene_active()) menu_scene.draw.pipeline=pipeline;
    menu_vk_pipeline(cmd,point,pipeline);
}
static void VKAPI_CALL menu_scene_viewport(VkCommandBuffer cmd,uint32_t first,uint32_t n,const VkViewport *v) {
#ifdef DC_FEEDBACK_QA
    menu_report_viewports(n,v);
#endif
    if(menu_scene_active()) {
        menu_scene.counted_view=0;
        menu_scene.draw.viewport=!first && n==1?v[0]:(VkViewport){0};
    }
    menu_vk_viewport(cmd,first,n,v);
}
static void VKAPI_CALL menu_scene_counted_viewport(VkCommandBuffer cmd,uint32_t n,const VkViewport *v) {
#ifdef DC_FEEDBACK_QA
    menu_report_viewports(n,v);
#endif
    if(menu_scene_active()) {
        menu_scene.counted_view=1;
        menu_scene.draw.viewport=n==1?v[0]:(VkViewport){0};
    }
    menu_vk_viewport_count(cmd,n,v);
}
static void VKAPI_CALL menu_scene_scissor(VkCommandBuffer cmd,uint32_t first,uint32_t n,const VkRect2D *v) {
    if(menu_scene_active()) {
        menu_scene.counted_scissor=0;
        menu_scene.draw.scissor=!first && n==1?v[0]:(VkRect2D){0};
    }
    menu_vk_scissor(cmd,first,n,v);
}
static void VKAPI_CALL menu_scene_counted_scissor(VkCommandBuffer cmd,uint32_t n,const VkRect2D *v) {
    if(menu_scene_active()) {
        menu_scene.counted_scissor=1;
        menu_scene.draw.scissor=n==1?v[0]:(VkRect2D){0};
    }
    menu_vk_scissor_count(cmd,n,v);
}
static void menu_scene_constants(VkCommandBuffer cmd,VkPipelineLayout layout,VkShaderStageFlags stages,
                                 uint32_t offset,uint32_t size,const void *data) {
    if(!(stages&VK_SHADER_STAGE_VERTEX_BIT) || !menu_scene_active()) return;
    menu_scene.draw.cmd=cmd;
    menu_scene.draw.layout=layout;
    menu_scene.draw.constant_size=0;
    if(layout==(VkPipelineLayout)menu_display_layout && !offset && size<=MENU_SCENE_CONSTANTS) {
        memcpy(menu_scene.draw.constants,data,size);
        menu_scene.draw.stages=stages;
        menu_scene.draw.constant_size=size;
    }
}
static void menu_scene_descriptors(VkCommandBuffer cmd,VkPipelineLayout layout,uint32_t set,
                                   uint32_t n,const VkWriteDescriptorSet *writes) {
    if(!menu_scene_active()) return;
    MenuSceneDraw *d=&menu_scene.draw;
    menu_scene.armed=0;
    if(set || n>MENU_SCENE_BINDINGS) return;
    if(layout!=(VkPipelineLayout)menu_display_layout) {
        for(uint32_t i=0;i<n;i++) if(writes[i].descriptorType==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE &&
           writes[i].pImageInfo && (void*)writes[i].pImageInfo->imageView==menu_reconstructed_view) {
            menu_color_slot=(int)i;
            InterlockedExchangePointer(&menu_display_layout,(void*)layout);
            break;
        }
    }
    if(layout!=(VkPipelineLayout)menu_display_layout || (uint32_t)menu_color_slot>=n ||
       d->cmd!=cmd || d->layout!=layout || !d->constant_size) return;
    if(writes[menu_color_slot].descriptorType!=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) return;
    d->count=n;
    d->source_slot=menu_color_slot;
    for(uint32_t i=0;i<n;i++) {
        const VkWriteDescriptorSet *w=&writes[i];
        if(w->pNext || w->descriptorCount!=1 || w->dstArrayElement) return;
        d->writes[i]=*w;
        switch(w->descriptorType) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            d->buffers[i]=*w->pBufferInfo; break;
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: case VK_DESCRIPTOR_TYPE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            d->images[i]=*w->pImageInfo; break;
        default:
            return;
        }
    }
    d->source=d->images[d->source_slot].imageView;
    d->generation=menu_scene_generation;
    d->epoch=menu_display_epoch;
    menu_scene.armed=1;
}
static void menu_scene_bind(VkCommandBuffer cmd,MenuSceneDraw *d) {
    for(uint32_t i=0;i<d->count;i++) {
        VkWriteDescriptorSet *w=&d->writes[i];
        w->pBufferInfo=NULL;
        w->pImageInfo=NULL;
        w->pTexelBufferView=NULL;
        if(w->descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || w->descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            w->pBufferInfo=&d->buffers[i];
        else w->pImageInfo=&d->images[i];
    }
    if(d->sized_vertex)
        menu_vk_vertex2(cmd,0,1,&d->vertex,&d->vertex_offset,d->has_size?&d->vertex_size:NULL,
                        d->has_stride?&d->vertex_stride:NULL);
    else menu_vk_vertex(cmd,0,1,&d->vertex,&d->vertex_offset);
    menu_original_motion_constants(cmd,d->layout,d->stages,0,d->constant_size,d->constants);
    menu_original_motion_descriptors(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,d->layout,0,d->count,d->writes);
    if(menu_scene.counted_view) menu_vk_viewport_count(cmd,1,&d->viewport);
    else menu_vk_viewport(cmd,0,1,&d->viewport);
    if(menu_scene.counted_scissor) menu_vk_scissor_count(cmd,1,&d->scissor);
    else menu_vk_scissor(cmd,0,1,&d->scissor);
}
static void VKAPI_CALL menu_scene_draw(VkCommandBuffer cmd,uint32_t vertices,uint32_t instances,
                                       uint32_t first,uint32_t base) {
    if(!menu_scene_active()) {
        menu_vk_draw(cmd,vertices,instances,first,base);
        return;
    }
    MenuSceneDraw *d=&menu_scene.draw;
    if(menu_scene.armed && menu_race_active && vertices==3 && instances==1 && !first && !base &&
       d->cmd==cmd && d->vertex && d->viewport.x==0 && d->viewport.width==extent.width &&
       d->scissor.extent.width==extent.width && d->viewport.height==-(float)d->scissor.extent.height) {
        if(!d->scissor.offset.y && d->scissor.extent.height==extent.height/2 && d->viewport.y==extent.height/2) {
            AcquireSRWLockExclusive(&menu_upper_lock);
            menu_upper=*d;
            ReleaseSRWLockExclusive(&menu_upper_lock);
        } else if(d->scissor.offset.y==(int32_t)(extent.height/2) && d->viewport.y==extent.height &&
                  d->source==(VkImageView)menu_reconstructed_view) {
            MenuSceneDraw upper;
            AcquireSRWLockExclusive(&menu_upper_lock);
            upper=menu_upper;
            menu_upper.cmd=VK_NULL_HANDLE;
            ReleaseSRWLockExclusive(&menu_upper_lock);
            /* A scheduler flush may split one display pass across two command
             * buffers. Its two triangles still use the same display pipeline
             * and the same frame's vertex upload stream. */
            const uint32_t elapsed=(uint32_t)d->epoch-(uint32_t)upper.epoch;
            const int same_target=upper.attachment && upper.attachment==d->attachment;
#ifdef DC_FEEDBACK_QA
            if(menu_pass_counts.active) InterlockedIncrement(&menu_pass_counts.sampled[1]);
#endif
            if(upper.cmd && upper.vertex==d->vertex && ((same_target && elapsed<=1) ||
               (!upper.attachment && !d->attachment && !elapsed && upper.cmd==cmd)) &&
               upper.pipeline==d->pipeline && upper.layout==d->layout &&
               upper.generation==d->generation && upper.source_slot==d->source_slot) {
                upper.images[upper.source_slot]=d->images[d->source_slot];
                menu_scene_bind(cmd,&upper);
                menu_vk_draw(cmd,vertices,instances,first,base);
                menu_scene_bind(cmd,d);
#ifdef DC_FEEDBACK_QA
                if(menu_pass_counts.active) InterlockedIncrement(&menu_pass_counts.sampled[0]);
#endif
            }
        }
    }
    menu_scene.armed=0;
    menu_vk_draw(cmd,vertices,instances,first,base);
}
static PFN_vkVoidFunction menu_scene_proc(const char *n) {
#define MENU_SCENE_PROC(name,fn,next) if(!strcmp(n,"vkCmd" name) && next) return (PFN_vkVoidFunction)fn
    MENU_SCENE_PROC("BeginRendering",menu_scene_begin_rendering,menu_vk_begin_rendering);
    MENU_SCENE_PROC("BeginRenderingKHR",menu_scene_begin_rendering,menu_vk_begin_rendering);
    MENU_SCENE_PROC("BindVertexBuffers2",menu_scene_vertex2,menu_vk_vertex2);
    MENU_SCENE_PROC("BindVertexBuffers2EXT",menu_scene_vertex2,menu_vk_vertex2);
    MENU_SCENE_PROC("Draw",menu_scene_draw,menu_vk_draw);
    MENU_SCENE_PROC("BindVertexBuffers",menu_scene_vertex,menu_vk_vertex);
    MENU_SCENE_PROC("BindPipeline",menu_scene_pipeline,menu_vk_pipeline);
    MENU_SCENE_PROC("SetViewport",menu_scene_viewport,menu_vk_viewport);
    MENU_SCENE_PROC("SetScissor",menu_scene_scissor,menu_vk_scissor);
    MENU_SCENE_PROC("SetViewportWithCount",menu_scene_counted_viewport,menu_vk_viewport_count);
    MENU_SCENE_PROC("SetViewportWithCountEXT",menu_scene_counted_viewport,menu_vk_viewport_count);
    MENU_SCENE_PROC("SetScissorWithCount",menu_scene_counted_scissor,menu_vk_scissor_count);
    MENU_SCENE_PROC("SetScissorWithCountEXT",menu_scene_counted_scissor,menu_vk_scissor_count);
#undef MENU_SCENE_PROC
    return NULL;
}

#undef menu_scene
