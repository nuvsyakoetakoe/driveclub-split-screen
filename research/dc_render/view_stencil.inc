/* DRIVECLUB 1.28: stencil bit 0x80 protects first-person vehicle/driver geometry.
 * Native model metadata shares this bit across views. Only the owner's cabin
 * view may retain it; exterior views clear it even for their own car/driver.
 * Give each draw a private metadata copy when its protection bit differs,
 * including parts without callbacks.
 * Keep native materials, callbacks, geometry and GPU buffers unchanged. */
typedef __attribute__((sysv_abi)) void (*StencilQueryFn)(void*,void*,void*,void*,unsigned,float);
typedef __attribute__((sysv_abi)) void (*StencilJobFn)(void*,void*);
typedef __attribute__((sysv_abi)) void *(*StencilAllocFn)(unsigned);
static StencilQueryFn stencil_original_query;
static StencilJobFn stencil_original_job;
static _Thread_local int stencil_seat=-1;
static _Thread_local int stencil_protect;
static _Thread_local VisibilityRange stencil_ranges[4];
static int stencil_installed;
static int stencil_trace;
static int stencil_all_own;
typedef struct { void *collector; int seat,protect; } StencilCollector;
static StencilCollector stencil_collectors[1024];
static SRWLOCK stencil_lock=SRWLOCK_INIT;
static int stencil_query_seat(void *query,unsigned index) {
    void *params=(unsigned char*)query+0x70;
    size_t slot=((uintptr_t)params>>4)&255;
    for(unsigned n=0;n<256;n++,slot=(slot+1)&255) {
        VisibilityFrusta *frusta=&visibility_frusta[slot];
        if(!frusta->params) break;
        if(frusta->params==params && index<frusta->count) return frusta->seats[index];
    }
    return index==0?visibility_query_seat(query):-1;
}
static __attribute__((sysv_abi)) void stencil_query(void *query,void *transform,void *renderer,void *requests,unsigned index,float value) {
    if(index<6) {
        void *collector=(unsigned char*)query+0x13f0+index*0x160;
        size_t slot=((uintptr_t)collector>>4)&1023;
        int seat=stencil_query_seat(query,index);
        /* Native class 1 is exterior; 0/2 use the near/cabin path. Snapshot
         * this with the collector so queued jobs cannot inherit another view. */
        int protect=seat>=0 && seat<2 && (stencil_all_own || visibility_cabin[seat]);
        AcquireSRWLockExclusive(&stencil_lock);
        for(unsigned n=0;n<1024;n++,slot=(slot+1)&1023) {
            if(!stencil_collectors[slot].collector || stencil_collectors[slot].collector==collector) {
                stencil_collectors[slot]=(StencilCollector){collector,seat,protect};break;
            }
        }
        ReleaseSRWLockExclusive(&stencil_lock);
    }
    stencil_original_query(query,transform,renderer,requests,index,value);
}
static __attribute__((sysv_abi)) void stencil_job(void *job,void *range) {
    int previous=stencil_seat;
    int previous_protect=stencil_protect;
    stencil_seat=-1;
    stencil_protect=0;
    void *collector=*(void**)((unsigned char*)job+0x60);
    size_t slot=((uintptr_t)collector>>4)&1023;
    AcquireSRWLockShared(&stencil_lock);
    for(unsigned n=0;n<1024;n++,slot=(slot+1)&1023) {
        if(!stencil_collectors[slot].collector) break;
        if(stencil_collectors[slot].collector==collector) {
            stencil_seat=stencil_collectors[slot].seat;
            stencil_protect=stencil_collectors[slot].protect;break;
        }
    }
    ReleaseSRWLockShared(&stencil_lock);
    if(!visibility_active()) stencil_seat=-1;
    VisibilityRange saved[4];
    if(previous>=0) memcpy(saved,stencil_ranges,sizeof(saved));
    if(stencil_seat>=0) {
        AcquireSRWLockShared(&visibility_snapshot_lock);
        memcpy(stencil_ranges,visibility_ranges,sizeof(stencil_ranges));
        ReleaseSRWLockShared(&visibility_snapshot_lock);
    }
    stencil_original_job(job,range);
    if(previous>=0) memcpy(stencil_ranges,saved,sizeof(saved));
    stencil_seat=previous;
    stencil_protect=previous_protect;
}
typedef struct {void *model;int seat,car;} StencilOwner;
static __attribute__((sysv_abi)) void stencil_car(StencilOwner *owner,void *renderer) {
    unsigned type=*(unsigned*)((unsigned char*)renderer+0x6c50);
    int car=owner->car,mode=-1;
    if(visibility_views[car]) {
        void *logic=((VisibilityGetFn)(*(uintptr_t**)visibility_views[car])[0x180/8])(visibility_views[car]);
        if(logic) mode=*(unsigned*)((unsigned char*)logic+0x228);
    }
    static LONG seen[2][4][16][6];
    if(car>=0 && owner->seat>=0 && owner->seat<4 && type<16 && mode>=0 && mode<6 && !InterlockedExchange(&seen[car][owner->seat][type][mode],1)) {
        unsigned char *r=renderer;
        printf("Car draw: car=%d view=%d camera=%d pass=%u ctx=%p stencil=%08x,%08x,%08x,%08x ref=%02x,%02x,%02x,%02x\n",car,owner->seat,mode,type,renderer,*(unsigned*)(r+0x638c),*(unsigned*)(r+0x6390),*(unsigned*)(r+0x6394),*(unsigned*)(r+0x6398),r[0x63a8],r[0x63a9],r[0x63aa],r[0x63ab]);
    }
    ((StencilJobFn)(uintptr_t)0x800d7eb70)(owner->model,renderer);
}
static __attribute__((sysv_abi,used,noinline)) void *stencil_shader(unsigned kind,unsigned char *frame) {
    if(stencil_seat>=0) {
        unsigned char **slot=(unsigned char**)(frame-0x48),*metadata=*slot;
        int car=-1,character=0;
        for(int i=0;i<4;i++) {
            VisibilityRange range=stencil_ranges[i];
            uintptr_t offset=(uintptr_t)metadata-range.metadata;
            if(range.metadata && offset<(size_t)range.count*32 && !(offset&31)) {car=i%2;character=i>=2;break;}
        }
        if(car>=0) {
            unsigned char flags=(metadata[0x19]&0x7f)|(car==stencil_seat && stencil_protect?0x80:0);
            static LONG reported[2][2][4][2];
            if(stencil_seat<4 && !InterlockedExchange(&reported[character][car][stencil_seat][stencil_protect],1))
                printf("%s P%d view=%d flags=%02x->%02x protect=%d\n",character?"Split stencil driver:":"Split stencil:",car+1,stencil_seat+1,metadata[0x19],flags,stencil_protect);
            int trace=stencil_trace && kind==5 && !character;
            if(flags==metadata[0x19] && !trace) return kind?((void**)(uintptr_t)0x802724f20)[kind]:NULL;
            unsigned char *memory=((StencilAllocFn)(uintptr_t)0x8007148e0)(trace?80:32);
            memcpy(memory,metadata,32);
            memory[0x19]=flags;
            if(trace) {
                void **original=*(void***)(metadata+0x10);
                void **pair=(void**)(memory+32);StencilOwner *owner=(StencilOwner*)(memory+48);
                *owner=(StencilOwner){original[0],stencil_seat,car};pair[0]=owner;pair[1]=(void*)stencil_car;
                *(void**)(memory+0x10)=pair;
            }
            *slot=memory;
        }
    }
    return kind?((void**)(uintptr_t)0x802724f20)[kind]:NULL;
}
static __attribute__((naked,sysv_abi)) void stencil_shader_gate(void) {
    __asm__ volatile("mov %rbp, %rsi\n\t" "jmp stencil_shader\n\t");
}
static void stencil_install(void) {
    if(stencil_installed || visibility_installed!=1 || !getenv("DC_VIEW_STENCIL")) return;
    const unsigned char prologue[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53};
    const unsigned char shader[]={0x89,0xf8,0x48,0x8d,0x0d,0xe7,0x6b,0x0a,0x02,0x48,0x8b,0x04,0xc1,0xc3};
    if(memcmp((void*)(uintptr_t)0x8005f41d0,prologue,13) ||
       memcmp((void*)(uintptr_t)0x800624890,prologue,13) ||
       memcmp((void*)(uintptr_t)0x80067e330,shader,14) ||
       memcmp((void*)(uintptr_t)0x800624c14,"\x74\x05",2)) return;
    stencil_all_own=getenv("DC_STENCIL_ALL_OWN")!=NULL; /* Prior behavior, for A/B captures only. */
    stencil_original_query=(StencilQueryFn)camera_detour(0x8005f41d0,(void*)stencil_query,13,0);
    stencil_original_job=(StencilJobFn)camera_detour(0x800624890,(void*)stencil_job,13,0);
    if(!stencil_original_query || !stencil_original_job) abort();
    unsigned char *code=(unsigned char*)(uintptr_t)0x80067e330;
    DWORD previous;
    if(!VirtualProtect(code,14,PAGE_EXECUTE_READWRITE,&previous)) abort();
    camera_jump(code,(void*)stencil_shader_gate);code[12]=code[13]=0x90;
    DWORD ignored;VirtualProtect(code,14,previous,&ignored);FlushInstructionCache(GetCurrentProcess(),code,14);
    /* Visit metadata with no shader callback too; it carries the stencil tag. */
    unsigned char *skip=(unsigned char*)(uintptr_t)0x800624c14;
    if(!VirtualProtect(skip,2,PAGE_EXECUTE_READWRITE,&previous)) abort();
    skip[0]=skip[1]=0x90;VirtualProtect(skip,2,previous,&ignored);FlushInstructionCache(GetCurrentProcess(),skip,2);
    stencil_installed=1;
    stencil_trace=getenv("DC_STENCIL_TRACE")!=NULL;
    printf("Split stencil: native per-view vehicle ownership installed\n");
}
