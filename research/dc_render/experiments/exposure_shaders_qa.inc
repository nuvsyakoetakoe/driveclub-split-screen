/* Match the native luminance fragment program to its vertex program. */
static SRWLOCK exposure_qa_shader_lock=SRWLOCK_INIT;
static struct { VkShaderModule module; uint64_t hash; } exposure_qa_modules[4096];
static unsigned exposure_qa_module_count;

static VkResult VKAPI_CALL exposure_qa_shader(VkDevice device,const VkShaderModuleCreateInfo *ci,
                                             const VkAllocationCallbacks *a,VkShaderModule *out) {
    unsigned model=~0u;
    for(size_t i=5;i<ci->codeSize/4;) {
        unsigned op=ci->pCode[i]&0xffff,n=ci->pCode[i]>>16;
        if(!n || i+n>ci->codeSize/4) break;
        if(op==15 && n>=3) { model=ci->pCode[i+1]; break; }
        i+=n;
    }
    VkResult result=ssr_shader(device,ci,a,out);
    if(result!=VK_SUCCESS || (model!=0 && model!=4)) return result;
    uint64_t hash=14695981039346656037ULL;
    for(size_t i=0;i<ci->codeSize/4;i++) {hash^=ci->pCode[i];hash*=1099511628211ULL;}
    if(model==0) {
        char name[64];
        snprintf(name,sizeof(name),"meter-vertex-%016llx.spv",(unsigned long long)hash);
        FILE *file=fopen(name,"wb");
        if(file) {fwrite(ci->pCode,1,ci->codeSize,file);fclose(file);}
    }
    AcquireSRWLockExclusive(&exposure_qa_shader_lock);
    unsigned at=exposure_qa_module_count++%4096u;
    exposure_qa_modules[at].module=*out;
    exposure_qa_modules[at].hash=hash;
    ReleaseSRWLockExclusive(&exposure_qa_shader_lock);
    return result;
}

static uint64_t exposure_qa_shader_hash(VkShaderModule module) {
    unsigned count=exposure_qa_module_count<4096?exposure_qa_module_count:4096;
    for(unsigned i=1;i<=count;i++) {
        unsigned at=(exposure_qa_module_count-i)%4096;
        if(exposure_qa_modules[at].module==module) return exposure_qa_modules[at].hash;
    }
    return 0;
}

static VkResult VKAPI_CALL exposure_qa_graphics(VkDevice device,VkPipelineCache cache,uint32_t count,
                                              const VkGraphicsPipelineCreateInfo *ci,
                                              const VkAllocationCallbacks *a,VkPipeline *pipelines) {
    AcquireSRWLockShared(&exposure_qa_shader_lock);
    for(uint32_t i=0;i<count;i++) {
        uint64_t vertex=0,fragment=0;
        for(uint32_t j=0;j<ci[i].stageCount;j++) {
            const VkPipelineShaderStageCreateInfo *stage=ci[i].pStages+j;
            if(stage->stage==VK_SHADER_STAGE_VERTEX_BIT) vertex=exposure_qa_shader_hash(stage->module);
            if(stage->stage==VK_SHADER_STAGE_FRAGMENT_BIT) fragment=exposure_qa_shader_hash(stage->module);
        }
        if(fragment==0xc5b49163bcbc275dULL)
            printf("Exposure QA: meter pipeline vertex=%016llx\n",(unsigned long long)vertex);
    }
    ReleaseSRWLockShared(&exposure_qa_shader_lock);
    return D(CreateGraphicsPipelines)(device,cache,count,ci,a,pipelines);
}
