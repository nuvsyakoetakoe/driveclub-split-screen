/* Meter each local view against the scene color at full resolution. The native
 * meter reduces its own crop to a small luminance target. Feeding that pass a
 * coarse mip of the shared post-processing atlas introduces pixels from the
 * neighboring view at the split. The output histogram and adaptation are
 * already separate and stay in the original render pipeline. */
typedef __attribute__((sysv_abi)) void (*ExposurePassFn)(unsigned char *,void *);
typedef __attribute__((sysv_abi)) void (*ExposureUploadFn)(void *,unsigned,unsigned,unsigned,void *,const void *);
typedef __attribute__((sysv_abi)) void (*ExposureTextureFn)(void *,unsigned,unsigned,unsigned,void *);

static ExposurePassFn exposure_original_pass;
static ExposureUploadFn exposure_original_upload;
static int exposure_installed;

typedef struct {
    void *context;
    void *scene_color;
    void *meter_mips;
    unsigned seat;
    int rebound;
} ExposureScope;
static _Thread_local ExposureScope *exposure_scope;

static void exposure_bind_meter(void *context,void *texture) {
    const ExposureTextureFn bind=(ExposureTextureFn)0x800751450;
    bind(context,0x60,1,0,texture);
}

static __attribute__((sysv_abi)) void exposure_upload(void *context,unsigned slot,unsigned stage,unsigned bank,
                                                     void *layout,const void *data) {
    ExposureScope *scope=exposure_scope;
    if(scope && scope->context==context && (uintptr_t)__builtin_return_address(0)==0x800702874
       && slot==9 && *(unsigned *)((unsigned char *)layout+4)==32) {
        float constants[8];
        memcpy(constants,data,sizeof(constants));
        constants[4]=0.0f; /* Full-resolution mip of the same scene used by the native copy. */
        exposure_bind_meter(context,scope->scene_color);
        scope->rebound=1;
        static LONG bound_seats;
        LONG bit=1L<<scope->seat;
        if(!(InterlockedOr(&bound_seats,bit)&bit))
            printf("Split exposure: P%u meter reads scene color\n",scope->seat+1);
        exposure_original_upload(context,slot,stage,bank,layout,constants);
        return;
    }
    exposure_original_upload(context,slot,stage,bank,layout,data);
}

static __attribute__((sysv_abi)) void exposure_pass(unsigned char *state,void *context) {
    const float *viewport=(const float *)(state+0x560);
    ExposureScope *previous=exposure_scope;
    ExposureScope scope={0};
    exposure_scope=NULL;
    if(exposure_installed==1 && visibility_active() && viewport[0]==0.0f && viewport[2]>0.99f
       && viewport[3]>0.49f && viewport[3]<0.51f && (viewport[1]==0.0f || viewport[1]==0.5f)) {
        const unsigned char *scene=*(void **)state;
        if(scene) {
            scope.context=context;
            scope.seat=viewport[1]>0.0f;
            scope.scene_color=*(void **)(scene+0x2d0);
            scope.meter_mips=*(void **)(scene+0x340);
            if(scope.scene_color && scope.meter_mips) exposure_scope=&scope;
        }
    }
    exposure_original_pass(state,context);
    if(scope.rebound) exposure_bind_meter(context,scope.meter_mips);
    exposure_scope=previous;
}

static void exposure_install(void) {
    if(exposure_installed || visibility_installed!=1) return;
    const uintptr_t pass=0x800702750,upload=0x800750700;
    static const unsigned char expected[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,
        0x41,0x54,0x53,0x48,0x83,0xec,0x38};
    if(memcmp((const void *)pass,expected,sizeof(expected)) || memcmp((const void *)upload,expected,sizeof(expected))) {
        exposure_installed=-1;
        return;
    }
    exposure_original_upload=(ExposureUploadFn)camera_detour(upload,(void *)exposure_upload,sizeof(expected),0);
    if(exposure_original_upload)
        exposure_original_pass=(ExposurePassFn)camera_detour(pass,(void *)exposure_pass,sizeof(expected),0);
    exposure_installed=exposure_original_pass ? 1 : -1;
    if(exposure_installed==1) printf("Split exposure: meter using per-view scene color\n");
}
