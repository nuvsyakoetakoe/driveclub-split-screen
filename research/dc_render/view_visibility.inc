/* DRIVECLUB 1.28: keep shared vehicle parts available, then filter each
 * camera's final culling mask. The native renderer still builds every draw. */
typedef __attribute__((sysv_abi)) void (*VisibilityViewFn)(void*);
typedef __attribute__((sysv_abi)) void (*VisibilityCharacterFn)(void*,void*);
typedef __attribute__((sysv_abi)) void (*VisibilityPartFn)(void*,unsigned,unsigned,unsigned);
typedef __attribute__((sysv_abi)) void (*VisibilityModeFn)(void*,int);
typedef __attribute__((sysv_abi)) void (*VisibilityCullFn)(void*,void*,void*,void*,void*);
typedef __attribute__((sysv_abi)) void (*VisibilityMultiFn)(void*,void**,unsigned,void*,void*,void*,void*);
typedef __attribute__((sysv_abi)) void *(*VisibilityGetFn)(void*);
typedef __attribute__((sysv_abi)) int (*VisibilityActorFn)(void*);
typedef __attribute__((sysv_abi)) unsigned char (*VisibilityDisabledFn)(void*);
static VisibilityViewFn visibility_original_view;
static VisibilityCharacterFn visibility_original_character;
static VisibilityPartFn visibility_original_part;
static VisibilityModeFn visibility_original_mode;
static VisibilityCullFn visibility_original_cull;
static VisibilityMultiFn visibility_original_multi;
static int visibility_installed;
static int visibility_test;
static int visibility_test_ready[2];
static unsigned visibility_test_target[2]={2,2};
static _Thread_local int visibility_seat=-1;
static void *visibility_views[2],*visibility_character_models[2],*visibility_full_models[2];
/* Jobs may still drain after the scene starts releasing its objects. Store
 * values captured from live callbacks; workers only compare addresses. */
typedef struct { uintptr_t metadata; void *records; unsigned first,count; } VisibilityRange;
typedef struct { void *address; unsigned char value[128]; } VisibilityMatrix;
static VisibilityRange visibility_ranges[4];
static VisibilityMatrix visibility_matrices[8];
static int visibility_cabin[2];
static SRWLOCK visibility_snapshot_lock=SRWLOCK_INIT;
static void visibility_save_model(int slot,unsigned char *model) {
    VisibilityRange range={0};
    if(model && *(void**)model) {
        range.metadata=(uintptr_t)*(void**)(model+0x40);
        range.count=*(unsigned*)(*(unsigned char**)model+0x20);
        unsigned char *manager=*(unsigned char**)(model+0x48);
        unsigned instance=*(unsigned*)(model+0x50);
        if(manager && instance!=~0u && *(void**)(manager+0x90)) {
            range.records=*(void**)(manager+0x50);
            range.first=(*(unsigned**)(manager+0x90))[instance];
        }
    }
    AcquireSRWLockExclusive(&visibility_snapshot_lock);
    visibility_ranges[slot]=range;
    ReleaseSRWLockExclusive(&visibility_snapshot_lock);
}
static void visibility_save_matrix(int slot,void *driver) {
    void *matrix=driver?((VisibilityGetFn)(*(uintptr_t**)driver)[0x50/8])(driver):NULL;
    AcquireSRWLockExclusive(&visibility_snapshot_lock);
    visibility_matrices[slot].address=matrix;
    if(matrix) memcpy(visibility_matrices[slot].value,matrix,128);
    ReleaseSRWLockExclusive(&visibility_snapshot_lock);
}
static int visibility_character_hidden[2];
typedef struct { unsigned char *address; unsigned char mask[2],hidden[2]; } VisibilityFlag;
static VisibilityFlag visibility_parts[16384];
typedef struct { void *object; int seat; } VisibilityQuery;
static VisibilityQuery visibility_queries[256];
typedef struct { void *params; unsigned count; int seats[6]; } VisibilityFrusta;
static VisibilityFrusta visibility_frusta[256];
static int visibility_active(void) {
    AcquireSRWLockShared(&camera_lock);
    int active=camera_packet_has_actors();
    ReleaseSRWLockShared(&camera_lock);
    return active;
}
static int visibility_view_seat(void *view) {
    void *list=((VisibilityGetFn)(*(uintptr_t**)view)[0x1c0/8])(view);
    void **begin=*(void***)((unsigned char*)list+8),**end=*(void***)((unsigned char*)list+0x10);
    for(;begin!=end;begin++) {
        int actor=((VisibilityActorFn)(uintptr_t)0x800ccd4a0)(*begin);
        AcquireSRWLockShared(&camera_lock);
        int seat=camera_packet.actors[0]==actor?0:(camera_packet.actors[1]==actor?1:-1);
        ReleaseSRWLockShared(&camera_lock);
        if(seat>=0) {
            if(visibility_views[seat]!=view) printf("Split visibility: P%d logical=%p driver=%p\n",seat+1,view,*(void**)((unsigned char*)view+0x10));
            visibility_views[seat]=view;
            return seat;
        }
    }
    return -1;
}
static VisibilityFlag *visibility_find_part(unsigned char *address,int create) {
    size_t slot=((uintptr_t)address>>5)&16383;
    for(unsigned probe=0;probe<16384;probe++,slot=(slot+1)&16383) {
        VisibilityFlag *flag=&visibility_parts[slot];
        if(flag->address==address) return flag;
        if(!flag->address) {
            if(!create) return NULL;
            flag->address=address;
            return flag;
        }
    }
    return NULL;
}
static unsigned visibility_record(unsigned char *address,unsigned reason,unsigned enabled,int seat,int owner) {
    VisibilityFlag *flag=visibility_find_part(address,1);
    if(!flag) return enabled;
    if(owner>=0) {
        for(int i=0;i<2;i++) {
            flag->mask[i]|=reason;
            if(i==owner && !enabled) flag->hidden[i]|=reason; else flag->hidden[i]&=~reason;
        }
    } else {
        flag->mask[seat]|=reason;
        if(enabled) flag->hidden[seat]&=~reason; else flag->hidden[seat]|=reason;
    }
    for(int i=0;i<2;i++) if(!(flag->mask[i]&reason) || !(flag->hidden[i]&reason)) return 1;
    return 0;
}
static __attribute__((sysv_abi)) void visibility_part(void *object,unsigned part,unsigned enabled,unsigned reason) {
    /* 0x10 selects camera-dependent groups; 0x40 hides the complete vehicle.
     * Capture both the unbound model and its current render-manager record. */
    int owner=-1,seat=visibility_seat;
    if(reason==0x40 && visibility_active())
        for(int i=0;i<2;i++) if(visibility_full_models[i]==object) owner=i;
    if((reason==0x10 && seat>=0) || owner>=0) {
        unsigned char *model=object,*local=*(unsigned char**)(model+0x40);
        unsigned result=enabled;
        if(local) result=visibility_record(local+(size_t)part*32+0x1a,reason,enabled,seat,owner);
        unsigned char *manager=*(unsigned char**)(model+0x48);
        unsigned instance=*(unsigned*)(model+0x50);
        if(manager && instance!=~0u) {
            unsigned start=(*(unsigned**)(manager+0x90))[instance];
            unsigned char *records=*(unsigned char**)(manager+0x50);
            result=visibility_record(records+((size_t)start+part)*64+0x38,reason,enabled,seat,owner);
        }
        enabled=result;
    }
    visibility_original_part(object,part,enabled,reason);
}
static __attribute__((sysv_abi)) void visibility_mode(void *model,int mode) {
    if(visibility_seat>=0) {
        int owner=visibility_seat;
        visibility_full_models[owner]=*(void**)((unsigned char*)model+0x1658);
        visibility_save_model(owner,visibility_full_models[owner]);
        visibility_seat=1-owner;
        *(int*)((unsigned char*)model+0x1a58)=-1;
        visibility_original_mode(model,2); /* Exterior for the other local camera. */
        visibility_seat=owner;
        *(int*)((unsigned char*)model+0x1a58)=-1;
    }
    visibility_original_mode(model,mode);
}
static __attribute__((sysv_abi)) void visibility_character(void *object,void *view) {
    int previous=visibility_seat;
    visibility_seat=-1;
    if(visibility_active()) {
        int owner=camera_actor_seat(object);
        if(owner>=0) {
            visibility_seat=visibility_view_seat(view);
            unsigned char *visual=*(unsigned char**)((unsigned char*)object+0x40);
            if(visual) {
                visibility_character_models[owner]=*(void**)(visual+8);
                visibility_save_model(owner+2,visibility_character_models[owner]);
                memset(visual+0x30,2,4); /* Re-evaluate each view's cached groups. */
            }
        }
    }
    visibility_original_character(object,view);
    visibility_seat=previous;
}
static void visibility_driver_queries(void *object,int seat) {
    unsigned char *driver=object;
    unsigned count=*(unsigned*)(driver+0x30);
    unsigned char *entries=*(unsigned char**)(driver+0x38);
    if(count>512) return;
    for(unsigned i=0;i<count;i++) {
        void *query=*(void**)(entries+i*0x28);
        size_t slot=((uintptr_t)query>>4)&255;
        for(unsigned n=0;n<256;n++,slot=(slot+1)&255) {
            if(!visibility_queries[slot].object || visibility_queries[slot].object==query) {
                visibility_queries[slot]=(VisibilityQuery){query,seat}; break;
            }
        }
    }
}
static void visibility_update_queries(void *view,int seat) {
    visibility_driver_queries(*(void**)((unsigned char*)view+0x10),seat);
}
static __attribute__((sysv_abi)) void visibility_view(void *view) {
    int previous=visibility_seat;
    visibility_seat=-1;
    if(visibility_active()) visibility_seat=visibility_view_seat(view);
    /* Visual regressions use far chase or an explicit diagnostic target,
     * reached through ordinary native camera cycles. */
    if(visibility_test && visibility_seat>=0 && !visibility_test_ready[visibility_seat]) {
        unsigned char *logic=((VisibilityGetFn)(*(uintptr_t**)view)[0x180/8])(view);
        if(logic && *(int*)(logic+0x10)==0) {
            if(*(unsigned*)(logic+0x228)==visibility_test_target[visibility_seat]) visibility_test_ready[visibility_seat]=1;
            else if(!*(unsigned*)(logic+0x234)) ((VisibilityViewFn)(uintptr_t)0x800cd6210)(logic);
        }
    }
    visibility_original_view(view);
    if(visibility_seat>=0) {
        void *logic=((VisibilityGetFn)(*(uintptr_t**)view)[0x180/8])(view);
        if(visibility_seat==1) split_audio_view(view);
        visibility_character_hidden[visibility_seat]=logic && *(int*)((unsigned char*)logic+0x10)==0 &&
            ((VisibilityDisabledFn)(uintptr_t)0x800cd3970)(logic);
        visibility_save_matrix(visibility_seat,*(void**)((unsigned char*)view+0x10));
        visibility_cabin[visibility_seat]=*(unsigned*)((unsigned char*)view+0x4c8)!=1;
        visibility_update_queries(view,visibility_seat);
        static unsigned last[2]={~0u,~0u};
        unsigned mode=logic?*(unsigned*)((unsigned char*)logic+0x228):~0u;
        if(mode!=last[visibility_seat] && (!visibility_test || visibility_test_ready[visibility_seat])) {
            last[visibility_seat]=mode;
            printf("Split visibility: P%d native camera=%u hide_character=%d\n",visibility_seat+1,mode,visibility_character_hidden[visibility_seat]);
        }
    }
    visibility_seat=previous;
}
static int visibility_query_seat(void *object) {
    size_t slot=((uintptr_t)object>>4)&255;
    for(unsigned n=0;n<256;n++,slot=(slot+1)&255) {
        if(!visibility_queries[slot].object) return -1;
        if(visibility_queries[slot].object==object) return visibility_queries[slot].seat;
    }
    return -1;
}
static void visibility_register(void *params,unsigned count,const int *seats) {
    size_t slot=((uintptr_t)params>>4)&255;
    for(unsigned n=0;n<256;n++,slot=(slot+1)&255) {
        VisibilityFrusta *frusta=&visibility_frusta[slot];
        if(!frusta->params || frusta->params==params) {
            frusta->params=params;frusta->count=count;
            memcpy(frusta->seats,seats,count*sizeof(int));return;
        }
    }
}
static __attribute__((sysv_abi)) void visibility_cull(void *object,void *a,void *transform,void *quality,void *out) {
    if(visibility_active()) {
        int seat=visibility_query_seat(object);
        if(seat>=0) visibility_register((unsigned char*)object+0x70,1,&seat);
    }
    visibility_original_cull(object,a,transform,quality,out);
}
static __attribute__((sysv_abi)) void visibility_multi(void *object,void **matrices,unsigned count,void *extra,void *transform,void *quality,void *out) {
    if(visibility_active() && count<=6) {
        int seats[6];
        int owner=visibility_query_seat(object);
        for(unsigned bit=0;bit<count;bit++) {
            seats[bit]=-1;
            if(count==1) seats[bit]=owner;
            else if(matrices[bit]) {
                AcquireSRWLockShared(&visibility_snapshot_lock);
                for(int i=0;i<8;i++) {
                    const VisibilityMatrix *m=&visibility_matrices[i];
                    if(m->address && (m->address==matrices[bit] || !memcmp(m->value,matrices[bit],128)))
                        seats[bit]=i<2?i:2+(i-2)/3;
                }
                ReleaseSRWLockShared(&visibility_snapshot_lock);
            }
        }
        if(owner>=0) visibility_register((unsigned char*)object+0x70,count,seats);
    }
    visibility_original_multi(object,matrices,count,extra,transform,quality,out);
}
static __attribute__((used)) uintptr_t visibility_resume=0x80071c122;
static __attribute__((sysv_abi,used,noinline)) unsigned char visibility_mask_impl(unsigned char *frame,unsigned char result) {
    void *context=*(void**)(frame-0x198),*params=*(void**)context;
    size_t slot=((uintptr_t)params>>4)&255;
    VisibilityFrusta *frusta=NULL;
    for(unsigned n=0;n<256;n++,slot=(slot+1)&255) {
        if(!visibility_frusta[slot].params) return result;
        if(visibility_frusta[slot].params==params) { frusta=&visibility_frusta[slot]; break; }
    }
    if(!frusta || !visibility_active()) return result;
    unsigned char *records=*(unsigned char**)(frame-0x1d0);
    size_t part=*(size_t*)(frame-0x1b0);
    if(part>=*(unsigned*)((unsigned char*)params+0x1358)) return result;
    VisibilityFlag *flag=visibility_find_part(records+part*64+0x38,0);
    for(unsigned bit=0;bit<frusta->count;bit++) {
        int seat=frusta->seats[bit];
        if(seat<0) continue;
        int mirror=seat>=2;
        int hidden=!mirror && flag && flag->hidden[seat];
        if(mirror || visibility_character_hidden[seat]) {
            AcquireSRWLockShared(&visibility_snapshot_lock);
            VisibilityRange range=visibility_ranges[mirror?seat-2:seat+2];
            ReleaseSRWLockShared(&visibility_snapshot_lock);
            if(records && records==range.records && part>=range.first && part-range.first<range.count) hidden=1;
        }
        if(hidden) result&=~(1u<<bit);
    }
    return result;
}
static __attribute__((naked,sysv_abi)) void visibility_mask_gate(void) {
    __asm__ volatile(
        "pushfq\n\t"
        "push %rax\n\t"
        "push %rbx\n\t"
        "push %rcx\n\t"
        "push %rdx\n\t"
        "push %rsi\n\t"
        "push %rdi\n\t"
        "push %r8\n\t"
        "push %r9\n\t"
        "push %r10\n\t"
        "push %r11\n\t"
        "push %r12\n\t"
        "push %r13\n\t"
        "push %r14\n\t"
        "push %r15\n\t"
        "sub $0x108, %rsp\n\t"
        "vmovdqu %xmm0, 0x0(%rsp)\n\t"
        "vmovdqu %xmm1, 0x10(%rsp)\n\t"
        "vmovdqu %xmm2, 0x20(%rsp)\n\t"
        "vmovdqu %xmm3, 0x30(%rsp)\n\t"
        "vmovdqu %xmm4, 0x40(%rsp)\n\t"
        "vmovdqu %xmm5, 0x50(%rsp)\n\t"
        "vmovdqu %xmm6, 0x60(%rsp)\n\t"
        "vmovdqu %xmm7, 0x70(%rsp)\n\t"
        "vmovdqu %xmm8, 0x80(%rsp)\n\t"
        "vmovdqu %xmm9, 0x90(%rsp)\n\t"
        "vmovdqu %xmm10, 0xa0(%rsp)\n\t"
        "vmovdqu %xmm11, 0xb0(%rsp)\n\t"
        "vmovdqu %xmm12, 0xc0(%rsp)\n\t"
        "vmovdqu %xmm13, 0xd0(%rsp)\n\t"
        "vmovdqu %xmm14, 0xe0(%rsp)\n\t"
        "vmovdqu %xmm15, 0xf0(%rsp)\n\t"
        "mov %rbp, %rdi\n\t"
        "mov %rax, %rsi\n\t"
        "call visibility_mask_impl\n\t"
        "mov %al, 0x170(%rsp)\n\t"
        "vmovdqu 0x0(%rsp), %xmm0\n\t"
        "vmovdqu 0x10(%rsp), %xmm1\n\t"
        "vmovdqu 0x20(%rsp), %xmm2\n\t"
        "vmovdqu 0x30(%rsp), %xmm3\n\t"
        "vmovdqu 0x40(%rsp), %xmm4\n\t"
        "vmovdqu 0x50(%rsp), %xmm5\n\t"
        "vmovdqu 0x60(%rsp), %xmm6\n\t"
        "vmovdqu 0x70(%rsp), %xmm7\n\t"
        "vmovdqu 0x80(%rsp), %xmm8\n\t"
        "vmovdqu 0x90(%rsp), %xmm9\n\t"
        "vmovdqu 0xa0(%rsp), %xmm10\n\t"
        "vmovdqu 0xb0(%rsp), %xmm11\n\t"
        "vmovdqu 0xc0(%rsp), %xmm12\n\t"
        "vmovdqu 0xd0(%rsp), %xmm13\n\t"
        "vmovdqu 0xe0(%rsp), %xmm14\n\t"
        "vmovdqu 0xf0(%rsp), %xmm15\n\t"
        "add $0x108, %rsp\n\t"
        "pop %r15\n\t"
        "pop %r14\n\t"
        "pop %r13\n\t"
        "pop %r12\n\t"
        "pop %r11\n\t"
        "pop %r10\n\t"
        "pop %r9\n\t"
        "pop %r8\n\t"
        "pop %rdi\n\t"
        "pop %rsi\n\t"
        "pop %rdx\n\t"
        "pop %rcx\n\t"
        "pop %rbx\n\t"
        "pop %rax\n\t"
        "popfq\n\t"
        "mov -0x198(%rbp), %rcx\n\t"
        "mov (%rcx), %r8\n\t"
        "mov 0x24(%r8), %edx\n\t"
        "jmp *visibility_resume(%rip)\n\t"
    );
}
#if defined(DC_NATIVE_MENU) && defined(DC_FEEDBACK_QA)
typedef __attribute__((sysv_abi)) uintptr_t (*VisibilityPoseFn)(void*,const void*,void*);
static VisibilityPoseFn visibility_original_pose;
static __attribute__((sysv_abi)) uintptr_t visibility_matched_pose(void *dest,const void *pose,void *projection) {
    if(visibility_seat==1 && visibility_views[0] && (uintptr_t)__builtin_return_address(0)==0x8004e47f6)
        pose=(const unsigned char*)visibility_views[0]+0x30;
    return visibility_original_pose(dest,pose,projection);
}
#endif
static void visibility_install(void) {
    if(visibility_installed || !getenv("DC_VIEW_VISIBILITY") || camera_installed!=1) return;
    const unsigned char prologue[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53};
    const unsigned char mode[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x50,0x41,0x89,0xf6};
    const unsigned char part[]={0x41,0x89,0xf1,0x48,0x8b,0x47,0x48,0x48,0x85,0xc0,0x74,0x1a,0x8b,0x77,0x50};
    const unsigned char filter_head[]={0x48,0x8b,0x8d,0x68,0xfe,0xff,0xff,0x4c,0x8b,0x01,0x41,0x8b,0x50,0x24};
    const uintptr_t addresses[]={0x8004e3b70,0x800d36430,0x8005f3020,0x8005f3630};
    for(int i=0;i<4;i++) if(memcmp((void*)addresses[i],prologue,13)) return;
    if(memcmp((void*)(uintptr_t)0x800d6e8e0,mode,13) || memcmp((void*)(uintptr_t)0x800682450,part,15) ||
       memcmp((void*)(uintptr_t)0x80071c114,filter_head,14)) return;
    visibility_original_part=(VisibilityPartFn)camera_detour(0x800682450,(void*)visibility_part,15,0);
    /* Preserve the short branch to the unbound-model path in this prologue. */
    unsigned char *island=(unsigned char*)visibility_original_part+38;
    island[0]=0xff;island[1]=0x25;memset(island+2,0,4);
    uintptr_t unbound=0x800682476;memcpy(island+6,&unbound,8);
    visibility_original_mode=(VisibilityModeFn)camera_detour(0x800d6e8e0,(void*)visibility_mode,13,0);
    visibility_original_character=(VisibilityCharacterFn)camera_detour(0x800d36430,(void*)visibility_character,13,0);
    visibility_original_view=(VisibilityViewFn)camera_detour(0x8004e3b70,(void*)visibility_view,13,0);
    visibility_original_cull=(VisibilityCullFn)camera_detour(0x8005f3020,(void*)visibility_cull,13,0);
    visibility_original_multi=(VisibilityMultiFn)camera_detour(0x8005f3630,(void*)visibility_multi,13,0);
    if(!visibility_original_part || !visibility_original_mode || !visibility_original_character ||
       !visibility_original_view || !visibility_original_cull || !visibility_original_multi) abort();
    unsigned char *filter=(unsigned char*)(uintptr_t)0x80071c114;
    DWORD previous;
    if(!VirtualProtect(filter,14,PAGE_EXECUTE_READWRITE,&previous)) abort();
    filter[0]=0xff;filter[1]=0x25;memset(filter+2,0,4);
    uintptr_t target=(uintptr_t)visibility_mask_gate;memcpy(filter+6,&target,8);
    DWORD ignored;VirtualProtect(filter,14,previous,&ignored);FlushInstructionCache(GetCurrentProcess(),filter,14);
    visibility_installed=1;
#if defined(DC_NATIVE_MENU) && defined(DC_FEEDBACK_QA)
    if(getenv("DC_QA_MATCH_CAMERA")) {
        const uintptr_t pose=0x80072d9e0;
        const unsigned char head[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x50,0x49,0x89,0xd6};
        if(!memcmp((const void*)pose,head,sizeof(head)))
            visibility_original_pose=(VisibilityPoseFn)camera_detour(pose,(void*)visibility_matched_pose,sizeof(head),0);
    }
#endif
    visibility_test=getenv("DC_VISIBILITY_TEST")!=NULL;
    if(visibility_test) for(int seat=0;seat<2;seat++) {
        const char *target=getenv(seat?"DC_TEST_CAMERA_P2":"DC_TEST_CAMERA_P1");
        if(target && target[0]>='0' && target[0]<='5' && !target[1]) visibility_test_target[seat]=target[0]-'0';
    }
    printf("Split visibility: native per-camera vehicle filtering installed\n");
}
