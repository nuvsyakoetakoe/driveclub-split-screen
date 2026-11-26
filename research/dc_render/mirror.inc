/* DRIVECLUB 1.28: separate native rear-view cameras and surfaces per local car.
 * The original constructor, camera builder, renderer and compositor remain in use. */
typedef __attribute__((sysv_abi)) void (*MirrorModelFn)(void*,float);
typedef __attribute__((sysv_abi)) void (*MirrorUpdateFn)(void*,const float*,unsigned);
static MirrorModelFn mirror_original_model;
static MirrorUpdateFn mirror_original_update;
static _Thread_local unsigned char *mirror_model;
static int mirror_installed;
static unsigned char *mirror_roots[2];
static void *mirror_configs[2];
typedef __attribute__((sysv_abi)) void (*MirrorInitFn)(void*,void*,unsigned,unsigned);
typedef __attribute__((sysv_abi)) void (*MirrorBuildFn)(void*,unsigned,void*);
static unsigned char *mirror_root(void *config) {
    for(int seat=0;seat<2;seat++) if(config==mirror_configs[seat] && mirror_roots[seat]) return mirror_roots[seat];
    return *(unsigned char**)(uintptr_t)0x801902b90;
}
static __attribute__((sysv_abi)) unsigned char mirror_enabled(void *config,unsigned index) {
    if(!*((unsigned char*)config+0x19) || index>=3) return 0;
    unsigned char *root=mirror_root(config);
    if(!root) return 0;
    void *view=*(void**)(root+0x140+8*index);
    typedef __attribute__((sysv_abi)) unsigned (*StateFn)(void*);
    return ((StateFn)(*(uintptr_t**)view)[0x60/8])(view)!=0;
}
static __attribute__((sysv_abi)) void *mirror_texture(void *config,unsigned index) {
    unsigned char *root=mirror_root(config);
    return *(void**)(*(unsigned char**)(root+0x158)+0x538);
}
static __attribute__((sysv_abi)) void mirror_release(void *config) {
    if(InterlockedDecrement((volatile LONG*)(uintptr_t)0x801902b98)) return;
    for(int seat=0;seat<2;seat++) if(mirror_roots[seat]) {
        ((VisibilityViewFn)(uintptr_t)0x800fbd940)(mirror_roots[seat]);
        VirtualFree(mirror_roots[seat],0,MEM_RELEASE);
        mirror_roots[seat]=NULL;
        mirror_configs[seat]=NULL;
        for(int i=0;i<3;i++) visibility_save_matrix(2+3*seat+i,NULL);
        printf("Split mirrors: P%d native resources released\n",seat+1);
    }
    void **global=(void**)(uintptr_t)0x801902b90;
    if(*global) {
        ((VisibilityViewFn)(uintptr_t)0x800fbd940)(*global);
        void *allocator=((VisibilityGetFn)(uintptr_t)0x800092dc0)(NULL);
        typedef __attribute__((sysv_abi)) void (*FreeFn)(void*,void*);
        ((FreeFn)(*(uintptr_t**)allocator)[0x30/8])(allocator,*global);
        *global=NULL;
    }
}
static void mirror_replace(uintptr_t at,void *target) {
    unsigned char *code=(unsigned char*)at;
    DWORD previous;
    if(!VirtualProtect(code,14,PAGE_EXECUTE_READWRITE,&previous)) abort();
    camera_jump(code,target);
    code[12]=code[13]=0x90;
    DWORD ignored;VirtualProtect(code,14,previous,&ignored);FlushInstructionCache(GetCurrentProcess(),code,14);
}
static __attribute__((sysv_abi)) void mirror_update(void *config,const float *matrix,unsigned index) {
    static void *seen[24];
    static unsigned count;
    unsigned i;
    for(i=0;i<count;i++) if(seen[i]==config) break;
    if(i==count && count<24) {
        seen[count++]=config;
        printf("Mirror source: model=%p handle=%d config=%p enabled=%u position=%.3f,%.3f,%.3f mask=%08x\n",
            mirror_model,mirror_model?*(int*)(mirror_model+0x1660):-1,config,
            *((unsigned char*)config+0x19),matrix[12],matrix[13],matrix[14],*(unsigned*)config);
    }
    if(getenv("DC_SPLIT_MIRRORS") && mirror_model &&
       (visibility_active() || mirror_roots[0] || mirror_roots[1])) {
        int seat=-1;
        void *full=*(void**)(mirror_model+0x1658);
        for(int i=0;i<2;i++) if(visibility_full_models[i]==full) seat=i;
        if(seat>=0 && index<3) {
            unsigned char *original=*(unsigned char**)(uintptr_t)0x801902b90;
            if(!mirror_roots[seat]) {
                mirror_roots[seat]=VirtualAlloc(NULL,0x180,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
                if(!mirror_roots[seat]) abort();
                ((MirrorInitFn)(uintptr_t)0x800fbd240)(mirror_roots[seat],*(void**)(original+0x120),0,original[0x170]);
                for(int i=0;i<3;i++) {
                    void *old=*(void**)(original+0x140+8*i);
                    typedef __attribute__((sysv_abi)) void (*EnableFn)(void*,unsigned);
                    ((EnableFn)(*(uintptr_t**)old)[0x20/8])(old,0);
                }
                printf("Split mirrors: P%d root=%p native=%p\n",seat+1,mirror_roots[seat],original);
            }
            unsigned char *root=mirror_roots[seat],*entry=root+index*0x60;
            mirror_configs[seat]=config;
            memcpy(entry,matrix,64);
            *(unsigned*)(entry+0x40)=*(unsigned*)config;
            *(unsigned*)(entry+0x44)=*(unsigned*)(uintptr_t)0x802749f58;
            entry[0x48]=*((unsigned char*)config+0x10);
            memcpy(entry+0x4c,(unsigned char*)config+0x14,4);
            entry[0x50]=*((unsigned char*)config+0x18);
            void *view=*(void**)(root+0x140+index*8);
            ((VisibilityViewFn)(*(uintptr_t**)view)[0x100/8])(view);
            ((MirrorBuildFn)(uintptr_t)0x8004d9630)(root,index,NULL);
            visibility_full_models[seat]=*(void**)(mirror_model+0x1658);
            visibility_save_matrix(2+3*seat+index,view);
            visibility_driver_queries(view,seat+2);
            return;
        }
    }
    mirror_original_update(config,matrix,index);
}
static __attribute__((sysv_abi)) void mirror_update_model(void *model,float dt) {
    unsigned char *previous=mirror_model;
    mirror_model=model;
    mirror_original_model(model,dt);
    mirror_model=previous;
}
static void mirror_install(void) {
    if(mirror_installed || (!getenv("DC_MIRROR_TRACE") && !getenv("DC_SPLIT_MIRRORS"))) return;
    const unsigned char prologue[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53};
    const uintptr_t addresses[]={0x800d7d660,0x8004d9dc0};
    for(int i=0;i<2;i++) {
        MEMORY_BASIC_INFORMATION memory;
        if(!VirtualQuery((void*)addresses[i],&memory,sizeof(memory)) || memory.State!=MEM_COMMIT ||
           (memory.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return;
        if(memcmp((void*)addresses[i],prologue,13)) return;
    }
    for(uintptr_t at=0x810000000;at<0x820000000 && !camera_code;at+=0x10000)
        camera_code=VirtualAlloc((void*)at,CAMERA_CODE_CAPACITY,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
    if(!camera_code) { mirror_installed=-1; return; }
    mirror_original_model=(MirrorModelFn)camera_detour(addresses[0],(void*)mirror_update_model,13,0);
    mirror_original_update=(MirrorUpdateFn)camera_detour(addresses[1],(void*)mirror_update,13,0);
    /* The texture accessor has a RIP-relative load, so implement its gateway
     * directly rather than relocating the original leaf function. */
    unsigned char *texture=(unsigned char*)(uintptr_t)0x8004d9f40;
    const unsigned char texture_head[]={0x48,0x8d,0x05,0x49,0x8c,0x42,0x01};
    if(memcmp(texture,texture_head,7)) abort();
    if(getenv("DC_SPLIT_MIRRORS")) {
        mirror_replace(0x8004d9f40,(void*)mirror_texture);
        mirror_replace(0x8004d9d90,(void*)mirror_enabled);
        mirror_replace(0x8004d9f60,(void*)mirror_release);
    }
    if(!mirror_original_model || !mirror_original_update) abort();
    mirror_installed=1;
    printf("Split mirrors: native per-car rendering installed\n");
}
