/* Stock has one player wheel/surface controller. Its per-wheel events and
 * smoothed parameters must live as long as the vehicle sound banks. */
typedef __attribute__((sysv_abi)) void (*SplitFxLifetime)(void *);
typedef __attribute__((sysv_abi)) void (*SplitFxInit)(void *,int);
typedef __attribute__((sysv_abi)) void (*SplitFxUpdate)(void *,void *,void *,unsigned,int,float,float);
typedef __attribute__((sysv_abi)) void **(*SplitFxMaterials)(void *);
static SplitFxLifetime split_fx_destroy_original;
static SplitEnginePositionFn split_fx_original_position;
static _Alignas(16) unsigned char split_fx_wheels[0xc8];
static void *split_fx_stock;
static _Thread_local int split_fx_position;
static _Thread_local int split_fx_local_impact;
static int split_fx_qa;
#if defined(DC_FEEDBACK_QA) && defined(DC_NATIVE_MENU)
/* The isolated impact recording passes only native events submitted for P2. */
typedef __attribute__((sysv_abi)) int (*SplitFxStart)(void *);
static SplitFxStart split_fx_test_start;
static void *volatile split_fx_test_handles[16];
static unsigned split_fx_test_next;
static __attribute__((sysv_abi)) int split_fx_test_play(void *handle) {
    for(unsigned i=0;i<16;i++)
        if(InterlockedCompareExchangePointer(&split_fx_test_handles[i],NULL,handle)==handle) {
            int control=getenv("DC_QA_IMPACT_CONTROL")!=NULL;
            int result=control?0:split_fx_test_start(handle);
            printf("Split FX P2 isolated playback: ms=%llu result=%d muted=%d\n",GetTickCount64()-start,result,control);
            return result;
        }
    return 0;
}
#endif

static __attribute__((sysv_abi)) void split_fx_set_position(void *event,const float *pos,
                                                           const float *vel,const float *dir) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    if(split_fx_qa && caller==0x800cb142d && pos) {
        unsigned char *voice=event;
        printf("Split FX event %.128s state=%u relative=%u\n",voice+0x38,*(unsigned *)(voice+0x14),voice[0xdf]);
        printf("Split FX impact submit: ms=%llu nearP2=%d loaded=%d spatial=%u pos=%.2f %.2f %.2f\n",
               GetTickCount64()-start,split_fx_local_impact,*(int *)(voice+0x10),voice[0x10f],pos[0],pos[1],pos[2]);
#if defined(DC_FEEDBACK_QA) && defined(DC_NATIVE_MENU)
        if(split_fx_local_impact && split_fx_test_start)
            InterlockedExchangePointer(&split_fx_test_handles[split_fx_test_next++%16],voice+0xe0);
#endif
    }
    _Alignas(16) float mapped[4],velocity[4],direction[4];
    if(pos && split_fx_position &&
       split_audio_map_source(pos,vel,dir,mapped,velocity,direction,0)) {
        split_fx_original_position(event,mapped,vel?velocity:NULL,dir?direction:NULL);
        return;
    }
    split_fx_original_position(event,pos,vel,dir);
}

static void split_fx_report_tire(int seat,unsigned char *controller) {
    unsigned char *wheel=*(unsigned char **)(controller+8);
    unsigned char **surface=wheel?*(unsigned char ***)(wheel+0x60):NULL;
    void *event=surface?*surface:NULL;
    if(!event) return;
    typedef __attribute__((sysv_abi)) int (*PlaybackState)(void *);
    void **methods=*(void ***)event;
    printf("Split FX P%d tire: event=%p material=%u playback=%d stateFn=%p positionFn=%p\n",
        seat+1,event,*(unsigned *)(wheel+0x68),((PlaybackState)methods[0x68/8])(event),
        methods[0x68/8],methods[0x50/8]);
}

static __attribute__((sysv_abi)) void split_fx_destroy(void *self) {
    if(self==split_fx_stock) {
        split_fx_stock=NULL;
        split_fx_destroy_original(split_fx_wheels);
    }
    split_fx_destroy_original(self);
}

#ifdef DC_NATIVE_MENU
static void split_fx_tunnel(unsigned char *controller,int seat) {
    /* Wheel controller +0x18 is the skid event initialized by e1f8b0.
     * Its stored Tunnel parameter belongs to this controller's own event. */
    unsigned char *skid=controller+0x18;
    if(!*skid || !*(void **)(skid+8)) return;
    void *parameter=*(void **)(skid+0x50);
    if(parameter) {
        float amount=0.001f*InterlockedCompareExchange(&env_amount[seat],0,0);
        ((EnvParameter)(*(void ***)parameter)[2])(parameter,amount,1);
    }
}
#endif

static void split_fx_surface(void *car,unsigned interior,float dt,float rpm) {
    unsigned char *stock=*(unsigned char **)0x8027fbd90;
    if(!split_fx_destroy_original || !stock || !*stock || !car) return;
    void *game=*(void **)0x801911100;
    void *mode=game?*(void **)((char *)game+0x970):NULL;
    void *service=mode?*(void **)((char *)mode+0x1d8):NULL;
    void *provider=service?*(void **)((char *)service+8):NULL;
    if(!provider) return;
    void **table=((SplitFxMaterials)(*(void ***)provider)[4])(provider);
    if(!table || !*table) return;
    if(!split_fx_stock) {
        const int wheels=*(int *)(stock+0x10);
        if(wheels<=0 || wheels>4) return;
        ((SplitFxLifetime)0x800e1dca0)(split_fx_wheels);
        ((SplitFxInit)0x800e1dd50)(split_fx_wheels,wheels);
        split_fx_stock=stock;
    }
    if(split_fx_stock!=stock) return;
    split_fx_position++;
    ((SplitFxUpdate)0x800e1e7c0)(split_fx_wheels,car,(char *)*table+0x40,interior,-1,dt,rpm);
    split_fx_position--;
#ifdef DC_NATIVE_MENU
    if(env_original_update && !env_stopped) {
        split_fx_tunnel(stock,0);
        split_fx_tunnel(split_fx_wheels,1);
    }
#endif
    if(split_fx_qa) {
        static ULONGLONG sampled;
        ULONGLONG now=GetTickCount64();
        if(now-sampled>=1000) {
            sampled=now;
            split_fx_report_tire(0,stock);
            split_fx_report_tire(1,split_fx_wheels);
            printf("Split FX P2 surfaces: wheels=%d ready=%u speedReady=%u interior=%u\n",
                   *(int *)(split_fx_wheels+0x10),split_fx_wheels[0x18],split_fx_wheels[0x70],interior);
        }
    }
}

/* The collision manager tests audibility before creating a sound. It then
 * computes pan and attenuation against listener zero from the contact point.
 * Supply the same listener-relative point at both stages, once per impact. */
typedef __attribute__((sysv_abi)) unsigned char (*SplitFxAudible)(void *,const float *,float *);
typedef __attribute__((sysv_abi)) void (*SplitFxImpact)(void *,void *,unsigned,unsigned,float);
typedef __attribute__((sysv_abi)) void (*SplitFxDebris)(void *,void *);
static SplitFxAudible split_fx_original_audible;
static SplitFxImpact split_fx_original_impact;
static SplitFxDebris split_fx_original_debris;
typedef __attribute__((sysv_abi)) void (*SplitFxScrapeBegin)(void *,unsigned,unsigned,const float *,float,float,float);
typedef __attribute__((sysv_abi)) void (*SplitFxScrapeUpdate)(void *,unsigned,const float *,float,float,float);
static SplitFxScrapeBegin split_fx_original_scrape_begin;
static SplitFxScrapeUpdate split_fx_original_scrape_update;

/* Stock samples camera zero when setting the scrape's interior parameter.
 * Route that parameter and the contact position together, including sustained
 * contact after the two cars have separated. */
static const float *split_fx_scrape(const float *position,float mapped[4],float *interior) {
    if(position && split_audio_map_source(position,NULL,NULL,mapped,NULL,NULL,1)) {
#ifdef DC_NATIVE_MENU
        *interior=InterlockedCompareExchange(&cabin_interior[1],0,0)?1.0f:0.0f;
#endif
        return mapped;
    }
    return position;
}

static __attribute__((sysv_abi)) void split_fx_scrape_begin(void *controller,unsigned material,unsigned age,
        const float *position,float speed,float interior,float force) {
    _Alignas(16) float mapped[4];
    position=split_fx_scrape(position,mapped,&interior);
    split_fx_original_scrape_begin(controller,material,age,position,speed,interior,force);
}

static __attribute__((sysv_abi)) void split_fx_scrape_update(void *controller,unsigned age,const float *position,
        float speed,float interior,float force) {
    _Alignas(16) float mapped[4];
    if((uintptr_t)__builtin_return_address(0)==0x800cb099a)
        position=split_fx_scrape(position,mapped,&interior);
    split_fx_original_scrape_update(controller,age,position,speed,interior,force);
}
static _Thread_local float split_fx_contact_delta[3];
static _Thread_local int split_fx_p2_contact;

/* This lookup is only used by the collision queue's facing test. Translating
 * its temporary listener matrix projects this one contact from P2's view.
 * Keep the world-space point in the queue for matching later contacts. */
static __attribute__((sysv_abi)) const float *split_fx_contact_view(void *listener) {
    typedef __attribute__((sysv_abi)) const float *(*Matrix)(void *);
    const float *matrix=((Matrix)(*(void ***)listener)[3])(listener);
    if(!split_fx_p2_contact) return matrix;
    static _Thread_local _Alignas(16) float adjusted[28];
    memcpy(adjusted,matrix,sizeof(adjusted));
    for(int i=0;i<3;i++) adjusted[12+i]+=split_fx_contact_delta[i];
    split_fx_p2_contact=0;
    return adjusted;
}

typedef struct { unsigned char parameters[0x20]; _Alignas(16) float position[4]; } SplitFxContact;
static int split_fx_contact(const SplitFxContact *source,SplitFxContact *mapped) {
    if(!split_audio_map_source(source->position,NULL,NULL,mapped->position,NULL,NULL,1)) return 0;
    memcpy(mapped->parameters,source->parameters,sizeof(mapped->parameters));
    return 1;
}

static __attribute__((sysv_abi)) unsigned char split_fx_audible(void *manager,const float *pos,float *radius) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    _Alignas(16) float mapped[4];
    if(caller==0x800caf443) split_fx_p2_contact=0;
    if((caller==0x800caf443 || caller==0x800cb0048 || caller==0x800cb028d || caller==0x800cb0307) &&
       split_audio_map_source(pos,NULL,NULL,mapped,NULL,NULL,1)) {
        if(caller==0x800caf443) {
            for(int i=0;i<3;i++) split_fx_contact_delta[i]=pos[i]-mapped[i];
            split_fx_p2_contact=1;
        }
        pos=mapped;
        if(split_fx_qa && caller==0x800caf443) {
            static ULONGLONG last;
            ULONGLONG now=GetTickCount64();
            if(now-last>=250) {
                last=now;
                printf("Split FX P2 collision contact at %.1f %.1f %.1f\n",pos[0],pos[1],pos[2]);
            }
        }
    }
    return split_fx_original_audible(manager,pos,radius);
}

static __attribute__((sysv_abi)) void split_fx_impact(void *manager,void *contact,unsigned kind,unsigned interior,float strength) {
    SplitFxContact mapped;
    const int previous=split_fx_local_impact;
    split_fx_local_impact=split_fx_contact(contact,&mapped);
    if(split_fx_qa) printf("Split FX physical impact kind=%u material=%u impulse=%.4f speed=%.4f p2=%d\n",
        kind,*(unsigned *)((char*)contact+8),*(float *)((char*)contact+12),strength,split_fx_local_impact);
    if(split_fx_local_impact) {
        contact=&mapped;
#ifdef DC_NATIVE_MENU
        interior=InterlockedCompareExchange(&cabin_interior[1],0,0)!=0;
#endif
        if(split_fx_qa) printf("Split FX P2 impact: kind=%u strength=%.3f\n",kind,strength);
    }
    split_fx_original_impact(manager,contact,kind,interior,strength);
    split_fx_local_impact=previous;
}

static __attribute__((sysv_abi)) void split_fx_debris(void *manager,void *contact) {
    SplitFxContact mapped;
    if(split_fx_contact(contact,&mapped)) {
        contact=&mapped;
        if(split_fx_qa) printf("Split FX P2 impact debris\n");
    }
    split_fx_original_debris(manager,contact);
}

static void split_fx_install(void) {
    static const unsigned char head[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53};
    static const unsigned char destroy_head[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x49,0x89,0xff};
    static const unsigned char position_head[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x83,0xec,0x20};
    static const unsigned char view_call[]={0x48,0x8b,0x08,0x48,0x89,0xc7,0xff,0x51,0x18};
    unsigned char *site=(void *)0x800caf46b;
    if(camera_code_used+8*64>CAMERA_CODE_CAPACITY || memcmp(site,view_call,sizeof(view_call)) ||
       memcmp((void *)0x800e1e5b0,destroy_head,sizeof(destroy_head)) ||
       memcmp((void *)0x800554780,position_head,sizeof(position_head)) ||
       memcmp((void *)0x800551510,head,sizeof(head)) || memcmp((void *)0x800cb0ba0,head,sizeof(head)) ||
       memcmp((void *)0x800cb14b0,head,sizeof(head)) || memcmp((void *)0x800cb7840,head,sizeof(head)) ||
       memcmp((void *)0x800cb7a20,head,sizeof(head))) return;
    unsigned char *stub=(unsigned char *)camera_code+camera_code_used;
    intptr_t distance=(intptr_t)stub-(intptr_t)(site+8);
    DWORD previous;
    if(distance<INT32_MIN || distance>INT32_MAX ||
       !VirtualProtect(site,sizeof(view_call),PAGE_EXECUTE_READWRITE,&previous)) return;
    camera_code_used+=64;
    camera_jump(stub,split_fx_contact_view);
    FlushInstructionCache(GetCurrentProcess(),stub,12);
    /* mov rdi,rax / call stub / nop */
    unsigned char replacement[]={0x48,0x89,0xc7,0xe8,0,0,0,0,0x90};
    int32_t offset=(int32_t)distance;
    memcpy(replacement+4,&offset,sizeof(offset));
    memcpy(site,replacement,sizeof(replacement));
    DWORD ignored;
    VirtualProtect(site,sizeof(view_call),previous,&ignored);
    FlushInstructionCache(GetCurrentProcess(),site,sizeof(view_call));
    split_fx_qa=getenv("DC_QA_SURFACE_TEST")!=NULL;
#if defined(DC_FEEDBACK_QA) && defined(DC_NATIVE_MENU)
    static const unsigned char play_head[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x89,0xfb};
    if(cabin_test_impact_only && camera_code_used+9*64<=CAMERA_CODE_CAPACITY && !memcmp((void *)0x800af1a00,play_head,sizeof(play_head)))
        split_fx_test_start=(SplitFxStart)camera_detour(0x800af1a00,(void *)split_fx_test_play,sizeof(play_head),0);
#endif
    split_fx_original_position=(SplitEnginePositionFn)camera_detour(0x800554780,(void *)split_fx_set_position,sizeof(position_head),0);
    split_fx_destroy_original=(SplitFxLifetime)camera_detour(0x800e1e5b0,(void *)split_fx_destroy,sizeof(destroy_head),0);
    split_fx_original_audible=(SplitFxAudible)camera_detour(0x800551510,(void *)split_fx_audible,sizeof(head),0);
    split_fx_original_impact=(SplitFxImpact)camera_detour(0x800cb0ba0,(void *)split_fx_impact,sizeof(head),0);
    split_fx_original_debris=(SplitFxDebris)camera_detour(0x800cb14b0,(void *)split_fx_debris,sizeof(head),0);
    split_fx_original_scrape_begin=(SplitFxScrapeBegin)camera_detour(0x800cb7840,(void *)split_fx_scrape_begin,sizeof(head),0);
    split_fx_original_scrape_update=(SplitFxScrapeUpdate)camera_detour(0x800cb7a20,(void *)split_fx_scrape_update,sizeof(head),0);
}
