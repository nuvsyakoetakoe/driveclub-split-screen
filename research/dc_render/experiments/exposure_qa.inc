/* Optional trace for the native view post-processing pass in DRIVECLUB 1.28. */
typedef __attribute__((sysv_abi)) void (*ExposureQAPassFn)(unsigned char *,void *,const float *,unsigned,void *);
static ExposureQAPassFn exposure_qa_original;
typedef __attribute__((sysv_abi)) void (*ExposureQAUploadFn)(void *,unsigned,unsigned,unsigned,void *,const void *);
static ExposureQAUploadFn exposure_qa_upload_original;
static _Thread_local int exposure_qa_seat;
static _Thread_local unsigned char *exposure_qa_context;
static _Thread_local unsigned char *exposure_qa_state;

typedef __attribute__((sysv_abi)) void (*ExposureQADrawFn)(void *,void *,unsigned);
static ExposureQADrawFn exposure_qa_draw_original;

static int exposure_qa_readable(uintptr_t address,size_t length) {
    MEMORY_BASIC_INFORMATION info;
    if(!address || !VirtualQuery((const void *)address,&info,sizeof(info)) || info.State!=MEM_COMMIT
       || (info.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return 0;
    return length<=info.RegionSize-(address-(uintptr_t)info.BaseAddress);
}

static __attribute__((sysv_abi)) void exposure_qa_draw(void *command,void *draw,unsigned flags) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    if(exposure_qa_seat && caller==0x8007028af && getenv("DC_QA_METER_DIRECT")) {
        typedef __attribute__((sysv_abi)) void (*BindTexture)(void *,unsigned,unsigned,unsigned,void *);
        BindTexture bind=(BindTexture)0x800751450;
        unsigned char *scene=*(unsigned char **)exposure_qa_state;
        void *source=*(void **)(scene+0x2d0);
        if(source) bind(exposure_qa_context,0x60,1,0,source);
    }
    if(exposure_qa_seat && caller==0x8007028af && getenv("DC_QA_METER_INSET")) {
        typedef __attribute__((sysv_abi)) void (*BindConstants)(void *,unsigned,void *,const void *);
        BindConstants bind=(BindConstants)0x800750920;
        unsigned char *state=exposure_qa_state;
        unsigned char *scene=*(unsigned char **)state;
        unsigned char *texture=*(unsigned char **)(scene+0x340);
        unsigned height=*(unsigned *)(texture+0x30);
        unsigned lod=(unsigned)*(float *)(state+0x4c0);
        void *layout=state+0x4a0;
        static unsigned reported;
        unsigned bit=1u<<(exposure_qa_seat-1);
        if(!(reported&bit))
            printf("Exposure QA: candidate seat=%d source h=%u lod=%u bytes=%u cpu_rect=%.6f,%.6f,%.6f,%.6f\n",
                   exposure_qa_seat,height,lod,*(unsigned *)(state+0x4a4),((float *)(state+0x280))[0],
                   ((float *)(state+0x280))[1],((float *)(state+0x280))[2],((float *)(state+0x280))[3]);
        if(*(unsigned *)(state+0x4a4)==0x2c0) {
            _Alignas(16) unsigned char data[0x2c0];
            memcpy(data,state+0x1e0,sizeof(data));
            float *rect=(float *)(data+0xa0);
            float margin=height && lod<=8 ? (float)(1u<<lod)/(float)height : 0;
            memcpy(rect,state+0x570,16);
            if(margin>0 && margin<0.20f) {
                float low=exposure_qa_seat==2?0.5f+margin:rect[1];
                float high=exposure_qa_seat==1?0.5f-margin:rect[1]+rect[3];
                if(low>=rect[1] && high<=rect[1]+rect[3] && high>low) {
                    rect[1]=low;
                    rect[3]=high-low;
                    if(!(reported&bit)) {
                        reported|=bit;
                        printf("Exposure QA: inset seat=%d source h=%u lod=%u range=%.6f..%.6f\n",
                               exposure_qa_seat,height,lod,low,high);
                    }
                    bind(exposure_qa_context,12,layout,data);
                    exposure_qa_draw_original(command,draw,flags);
                    bind(exposure_qa_context,12,layout,state+0x1e0);
                    return;
                }
            }
        }
        reported|=bit;
    }
    exposure_qa_draw_original(command,draw,flags);
    if(!exposure_qa_seat || caller!=0x8007028af) return;
    static unsigned logged;
    unsigned bit=1u<<(exposure_qa_seat-1);
    if(logged&bit) return;
    logged|=bit;
    unsigned char *cmd=*(unsigned char **)(exposure_qa_context+0x1d00);
    uintptr_t binds=*(uintptr_t *)(cmd+0x2d0);
    for(unsigned stage=0;stage<7;stage++) for(unsigned slot=0;slot<16;slot++) {
        uintptr_t ptr=binds+(0x540+0x642*stage+4*slot)*4u;
        if(!exposure_qa_readable(ptr,16)) continue;
        const unsigned *d=(const unsigned *)ptr;
        uintptr_t data=d[0]|((uintptr_t)(d[1]&0xfff)<<32);
        if(d[2]<11 || !exposure_qa_readable(data,176)) continue;
        const float *v=(const float *)data;
        printf("Exposure QA: draw seat=%d stage=%u cb=%u size=%u uv=%g,%g,%g,%g\n",exposure_qa_seat,stage,slot,
               d[2]*16u,v[40],v[41],v[42],v[43]);
    }
}

static __attribute__((sysv_abi)) void exposure_qa_upload(void *context,unsigned slot,unsigned stage,unsigned bank,
                                                        void *layout,const void *data) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    if(exposure_qa_seat && (caller==0x800702874 || caller==0x8007025eb)) {
        static unsigned logged[2];
        unsigned index=caller==0x800702874;
        unsigned bit=1u<<index;
        if(!(logged[exposure_qa_seat-1]&bit)) {
            logged[exposure_qa_seat-1]|=bit;
            const float *f=data;
            unsigned size=*(unsigned *)((unsigned char *)layout+4);
            printf("Exposure QA: upload seat=%d caller=%llx slot=%u stage=%u bytes=%u",exposure_qa_seat,
                   (unsigned long long)caller,slot,stage,size);
            for(unsigned i=0;i<size/4 && i<32;i++) printf(" %.6f",f[i]);
            puts("");
        }
    }
    if(exposure_qa_seat && caller==0x800702874 && getenv("DC_QA_METER_DIRECT")
       && *(unsigned *)((unsigned char *)layout+4)==32) {
        float constants[8];
        memcpy(constants,data,sizeof(constants));
        constants[4]=0.0f;
        exposure_qa_upload_original(context,slot,stage,bank,layout,constants);
        return;
    }
    exposure_qa_upload_original(context,slot,stage,bank,layout,data);
}

static float exposure_qa_float(const unsigned char *state,size_t offset) {
    float value;
    memcpy(&value,state+offset,sizeof(value));
    return value;
}

static __attribute__((sysv_abi)) void exposure_qa_pass(unsigned char *state,void *context,
                                                      const float *rect,unsigned mode,void *options) {
    float viewport[4];
    memcpy(viewport,rect,sizeof(viewport));
    int split=visibility_active() && viewport[2]>=0.99f && viewport[3]>=0.49f && viewport[3]<=0.51f;
    unsigned seat=viewport[1]>0.25f;
    ULONGLONG now=GetTickCount64()-start;
    static ULONGLONG began;
    static unsigned char *frozen_state;
    static float frozen_value;
    float previous_limits[2];
    int freeze=split && seat==1 && !mode && getenv("DC_QA_FREEZE_EXPOSURE");
    if(freeze && frozen_state!=state) {
        frozen_state=state;
        frozen_value=0;
        began=now;
    }
    if(freeze && now-began>=3500 && frozen_value==0) {
        frozen_value=exposure_qa_float(state,0x68);
        printf("Exposure QA: freezing P2 at %.6f\n",frozen_value);
    }
    freeze=freeze && frozen_value>0;
    if(freeze) {
        memcpy(previous_limits,state+0x150,sizeof(previous_limits));
        memcpy(state+0x150,&frozen_value,sizeof(frozen_value));
        memcpy(state+0x154,&frozen_value,sizeof(frozen_value));
    }
    int previous_seat=exposure_qa_seat;
    unsigned char *previous_context=exposure_qa_context;
    unsigned char *previous_state=exposure_qa_state;
    exposure_qa_seat=split ? (int)seat+1 : 0;
    exposure_qa_context=context;
    exposure_qa_state=state;
    exposure_qa_original(state,context,rect,mode,options);
    exposure_qa_seat=previous_seat;
    exposure_qa_context=previous_context;
    exposure_qa_state=previous_state;
    if(freeze) memcpy(state+0x150,previous_limits,sizeof(previous_limits));
    if(!split) return;
    static const void *logged_state[2];
    if(logged_state[seat]!=state) {
        logged_state[seat]=state;
        const unsigned char *scene=*(void **)state;
        printf("Exposure QA: seat=%u luminance=%p source=%p histograms=%p/%p\n",seat+1,
               *(void **)(state+0x28),*(void **)(scene+0x340),*(void **)(state+0x18),*(void **)(state+0x20));
        for(unsigned offset=0x560;offset<=0x570;offset+=0x10) {
            printf("Exposure QA: seat=%u rect=%x %.6f %.6f %.6f %.6f\n",seat+1,offset,
                   exposure_qa_float(state,offset),exposure_qa_float(state,offset+4),
                   exposure_qa_float(state,offset+8),exposure_qa_float(state,offset+12));
        }
    }
    static ULONGLONG last_sample[2];
    if(now-last_sample[seat]<250) return;
    last_sample[seat]=now;
    printf("Exposure QA: ms=%llu seat=%u state=%p scene=%p mode=%u auto=%u measured=%.6f adapted=%.6f output=%.6f\n",
           now,seat+1,state,*(void **)state,mode,(unsigned)state[0x140],
           exposure_qa_float(state,0x6c),exposure_qa_float(state,0x68),exposure_qa_float(state,0x74));
}

static void exposure_qa_install(void) {
    if(exposure_qa_original || visibility_installed!=1) return;
    static const unsigned char expected[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,
        0x41,0x54,0x53,0x48,0x81,0xec,0x88,0,0,0};
    const uintptr_t address=0x800701ee0;
    if(memcmp((const void *)address,expected,sizeof(expected))) return;
    exposure_qa_original=(ExposureQAPassFn)camera_detour(address,(void *)exposure_qa_pass,sizeof(expected),0);
    if(exposure_qa_original) printf("Exposure QA: native pass trace installed\n");
    static const unsigned char upload_head[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,
        0x41,0x54,0x53,0x48,0x83,0xec,0x38};
    if(exposure_qa_original && !memcmp((const void *)0x800750700,upload_head,sizeof(upload_head)))
        exposure_qa_upload_original=(ExposureQAUploadFn)camera_detour(0x800750700,(void *)exposure_qa_upload,sizeof(upload_head),0);
    static const unsigned char draw_head[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50};
    if(exposure_qa_original && !memcmp((const void *)0x800735490,draw_head,sizeof(draw_head)))
        exposure_qa_draw_original=(ExposureQADrawFn)camera_detour(0x800735490,(void *)exposure_qa_draw,sizeof(draw_head),0);
}
