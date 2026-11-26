/* One native pair of Havok probes is available. Consume its previous result
 * before moving it, and keep the smoothing and sound for each seat separate. */
typedef __attribute__((sysv_abi)) void (*EnvUpdate)(unsigned char *,const float *,const float *,float);
typedef __attribute__((sysv_abi)) void (*EnvJob)(void);
typedef __attribute__((sysv_abi)) void (*EnvParameter)(void *,float,float);
static EnvUpdate env_original_update;
static SRWLOCK env_job_lock=SRWLOCK_INIT;
static LONG env_stopped,env_amount[2];
static unsigned env_frame;
static float env_sides[2][2];

/* Each seat gets a child of the same mix bus the native player selected.
 * The effect lives on that group for the race, independently of individual
 * engine layers starting, fading and retiring on the mixer thread. */
typedef __attribute__((sysv_abi)) int (*EnvPlay)(void *,void *,void *,int,void **);
typedef __attribute__((sysv_abi)) void (*EnvStart)(void *,void *);
typedef __attribute__((sysv_abi)) int (*EnvWave)(unsigned char *,void *,void *);
typedef __attribute__((sysv_abi)) int (*EnvGroupCreate)(void *,const char *,void **);
typedef __attribute__((sysv_abi)) int (*EnvGroupAdd)(void *,void *,int,void *);
typedef __attribute__((sysv_abi)) int (*EnvCreateEffect)(void *,int,void **);
typedef __attribute__((sysv_abi)) int (*EnvAddEffect)(void *,int,void *,void *);
typedef __attribute__((sysv_abi)) int (*EnvRemoveEffect)(void *,void *);
typedef __attribute__((sysv_abi)) int (*EnvRelease)(void *);
typedef __attribute__((sysv_abi)) int (*EnvSetting)(void *,int,float);
static EnvWave env_original_wave;
static EnvStart env_original_start;
static _Thread_local int env_start_seat=-1;
static SRWLOCK env_voice_lock=SRWLOCK_INIT;
typedef struct { void *source; int seat; ULONGLONG seen; } EnvOwner;
static EnvOwner env_owners[256];
typedef struct { void *parent,*group,*effect; int seat,amount; } EnvBus;
static EnvBus env_buses[16];

static EnvOwner *env_owner(void *source,int allocate) {
    if(!source) return NULL;
    unsigned start=((uintptr_t)source>>5)%256;
    EnvOwner *old=NULL;
    for(unsigned i=0;i<256;i++) {
        EnvOwner *v=&env_owners[(start+i)%256];
        if(v->source==source) return v;
        if(!v->source) return allocate?v:NULL;
        if(!old || v->seen<old->seen) old=v;
    }
    return allocate?old:NULL;
}
static __attribute__((sysv_abi)) int env_wave(unsigned char *layer,void *sample,void *state) {
    if(menu_race_active && !env_stopped) {
        void *source=*(void **)(layer+0x38);
        AcquireSRWLockExclusive(&env_voice_lock);
        EnvOwner *owner=env_owner(source,cabin_seat>=0);
        if(owner) {
            owner->source=source;
            owner->seat=cabin_seat;
            owner->seen=GetTickCount64();
        }
        ReleaseSRWLockExclusive(&env_voice_lock);
    }
    return env_original_wave(layer,sample,state);
}
static __attribute__((sysv_abi)) void env_start(void *manager,void *source) {
    int previous=env_start_seat;
    env_start_seat=cabin_seat;
    if(env_start_seat<0 && menu_race_active && !env_stopped) {
        AcquireSRWLockShared(&env_voice_lock);
        EnvOwner *owner=env_owner(source,0);
        if(owner && GetTickCount64()-owner->seen<500) env_start_seat=owner->seat;
        ReleaseSRWLockShared(&env_voice_lock);
    }
    env_original_start(manager,source);
    env_start_seat=previous;
}
static void env_release_bus(EnvBus *bus) {
    if(bus->effect) {
        ((EnvRemoveEffect)0x800a170c0)(bus->group,bus->effect);
        ((EnvRelease)0x800a20510)(bus->effect);
    }
    if(bus->group) ((EnvRelease)0x800a1a460)(bus->group);
    memset(bus,0,sizeof(*bus));
}
static void *env_group(void *system,void *parent,int seat) {
    EnvBus *bus=NULL;
    for(unsigned i=0;i<16;i++) {
        EnvBus *entry=&env_buses[i];
        if(entry->group && entry->parent==parent && entry->seat==seat) return entry->group;
        if(!bus && !entry->group) bus=entry;
    }
    if(!bus || ((EnvGroupCreate)0x800a41ba0)(system,seat?"Split P2":"Split P1",&bus->group)) return parent;
    EnvSetting set=(EnvSetting)0x800a20940;
    void *effect=NULL;
    if((parent && ((EnvGroupAdd)0x800a1a4b0)(parent,bus->group,1,NULL)) ||
       ((EnvCreateEffect)0x800a41b40)(system,6,&effect) || !effect ||
       set(effect,0,58) || set(effect,1,24) || set(effect,2,0) || set(effect,3,-80) ||
       ((EnvAddEffect)0x800a17050)(bus->group,0,effect,NULL)) {
        if(effect) ((EnvRelease)0x800a20510)(effect);
        env_release_bus(bus);
        return parent;
    }
    bus->effect=effect;
    bus->parent=parent;
    bus->seat=seat;
    bus->amount=-100;
    return bus->group;
}
static __attribute__((sysv_abi)) int env_play(void *system,void *sound,void *parent,int paused,void **channel) {
    if(env_start_seat<0 || !menu_race_active || env_stopped)
        return ((EnvPlay)0x800a41c20)(system,sound,parent,paused,channel);
    AcquireSRWLockExclusive(&env_voice_lock);
    void *group=env_stopped?parent:env_group(system,parent,env_start_seat);
    int result=((EnvPlay)0x800a41c20)(system,sound,group,paused,channel);
#ifdef DC_FEEDBACK_QA
    static LONG reported;
    if(cabin_meter_enabled && !result && group!=parent && !(InterlockedOr(&reported,1L<<env_start_seat)&(1L<<env_start_seat)))
        printf("Split room playback: P%d parent=%p bus=%p channel=%p\n",env_start_seat+1,parent,group,*channel);
#endif
    if(result && group!=parent) result=((EnvPlay)0x800a41c20)(system,sound,parent,paused,channel);
    ReleaseSRWLockExclusive(&env_voice_lock);
    return result;
}
static void env_mix(int seat,int amount) {
    AcquireSRWLockExclusive(&env_voice_lock);
    for(unsigned i=0;i<16;i++) {
        EnvBus *bus=&env_buses[i];
        if(!bus->effect || bus->seat!=seat || (abs(bus->amount-amount)<5 && (amount || !bus->amount))) continue;
        float wet=amount?fmaxf(-80,20*log10f(0.00055f*amount)):-80;
        if(!((EnvSetting)0x800a20940)(bus->effect,3,wet)) {
            bus->amount=amount;
#ifdef DC_FEEDBACK_QA
            if(cabin_meter_enabled && env_frame%128<16)
                printf("Split room bus P%d: coverage=%d wet=%.1f dB\n",seat+1,amount,wet);
#endif
        }
    }
    ReleaseSRWLockExclusive(&env_voice_lock);
}

static void env_finish_queries(void) {
    if(!*(void **)0x8027e12a0) return;
    void *game=*(void **)0x801911100;
    void *mode=game?*(void **)((char *)game+0x970):NULL;
    void *physics=mode?*(void **)((char *)mode+0x80):NULL;
    if(physics) {
        typedef __attribute__((sysv_abi)) void *(*Queries)(void *);
        typedef __attribute__((sysv_abi)) void (*Finish)(void *);
        void *jobs=((Queries)(*(void ***)physics)[0x1d8/8])(physics);
        if(jobs) ((Finish)0x8004c8080)(jobs);
    }
}

static void env_stop_race(void) {
    if(!env_original_update) return;
    AcquireSRWLockExclusive(&env_job_lock);
    if(!InterlockedExchange(&env_stopped,1)) {
        env_finish_queries();
        AcquireSRWLockExclusive(&env_voice_lock);
        for(unsigned i=0;i<16;i++) env_release_bus(&env_buses[i]);
        memset(env_owners,0,sizeof(env_owners));
        ReleaseSRWLockExclusive(&env_voice_lock);
        for(int seat=0;seat<2;seat++) InterlockedExchange(&env_amount[seat],0);
        env_frame=0;
        memset(env_sides,0,sizeof(env_sides));
    }
    ReleaseSRWLockExclusive(&env_job_lock);
}

static __attribute__((sysv_abi)) void env_submit(void) {
    AcquireSRWLockShared(&env_job_lock);
    if(!env_stopped || !menu_race_active) ((EnvJob)0x800cb5170)();
    ReleaseSRWLockShared(&env_job_lock);
}

static __attribute__((sysv_abi)) void env_update(unsigned char *probe,const float *view,const float *listener,float speed) {
    if(!menu_race_active) {
        env_original_update(probe,view,listener,speed);
        return;
    }
    if(env_stopped || !visibility_active()) return;
    AcquireSRWLockExclusive(&env_job_lock);
    if(env_stopped) { ReleaseSRWLockExclusive(&env_job_lock); return; }
    SplitAudioPose pose[2];
    AcquireSRWLockShared(&split_audio_lock);
    int ready=split_audio_engine_mix.fresh && GetTickCount64()-split_audio_engine_mix.fresh<250;
    memcpy(pose,split_audio_engine_mix.listener,sizeof(pose));
    ReleaseSRWLockShared(&split_audio_lock);
    if(!ready) {
        env_finish_queries();
        ReleaseSRWLockExclusive(&env_job_lock);
        return;
    }
    /* The guest writes its room estimate into one pair of master-bus
     * parameters. Read the estimate locally and leave the master bus dry. */
    if(env_frame==0) for(int side=0;side<2;side++) {
        void *parameter=*(void **)(probe+0x78+8*side);
        if(parameter) ((EnvParameter)(*(void ***)parameter)[2])(parameter,0,1);
    }
    int seat=(env_frame/8)%2;
    _Alignas(16) unsigned char local[0x90];
    _Alignas(16) float matrix[16];
    memcpy(local,probe,0x88);
    memset(local+0x78,0,2*sizeof(void *));
    memcpy(local+8,env_sides[seat],sizeof(env_sides[seat]));
    memcpy(matrix,view,sizeof(matrix));
    split_audio_right(&pose[seat],matrix);
    memcpy(matrix+4,pose[seat].up,3*sizeof(float));
    for(int i=0;i<3;i++) matrix[8+i]=-pose[seat].forward[i];
    memcpy(matrix+12,pose[seat].position,3*sizeof(float));
    const float *vel=pose[seat].velocity;
    env_original_update(local,matrix,matrix,sqrtf(vel[0]*vel[0]+vel[1]*vel[1]+vel[2]*vel[2]));
    /* The first result in each block still belongs to the previous position. */
    if(env_frame%8>=2) {
        memcpy(env_sides[seat],local+8,sizeof(env_sides[seat]));
        float value=(env_sides[seat][0]+env_sides[seat][1])*0.5f;
        if(isfinite(value)) {
            int amount=(int)(1000*fminf(1,fmaxf(0,value)));
            InterlockedExchange(&env_amount[seat],amount);
            env_mix(seat,amount);
        }
    }
#ifdef DC_FEEDBACK_QA
    if((env_frame%128)==15 && cabin_meter_enabled)
        printf("Split room: P1=%ld P2=%ld separation=%.1f\n",env_amount[0],env_amount[1],fabsf(pose[1].position[2]-pose[0].position[2]));
#endif
    env_frame++;
    ReleaseSRWLockExclusive(&env_job_lock);
}

static int env_redirect_call(unsigned char *site,void *target) {
    unsigned char *stub=(unsigned char *)camera_code+camera_code_used;
    intptr_t distance=stub-(site+5);
    DWORD old,ignored;
    if(distance<INT32_MIN || distance>INT32_MAX || !VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&old)) return 0;
    camera_code_used+=64;
    camera_jump(stub,target);
    FlushInstructionCache(GetCurrentProcess(),stub,12);
    int32_t rel=(int32_t)distance;
    memcpy(site+1,&rel,sizeof(rel));
    FlushInstructionCache(GetCurrentProcess(),site,5);
    VirtualProtect(site,5,old,&ignored);
    return 1;
}
static int env_install(void) {
    static const unsigned char head[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53};
    const unsigned char submit[]={0xe8,0x71,0x34,0xec,0xff}, play[]={0xe8,0xf5,0x40,0x4f,0x00};
    if(camera_code_used+5*64>CAMERA_CODE_CAPACITY || memcmp((void *)0x800df1cfa,submit,5) ||
       memcmp((void *)0x80054db26,play,5) || memcmp((void *)0x800cb58a0,head,sizeof(head)) ||
       memcmp((void *)0x8007d2cb0,head,sizeof(head)) || memcmp((void *)0x80054da60,head,sizeof(head))) return 0;
    env_original_wave=(EnvWave)camera_detour(0x8007d2cb0,env_wave,sizeof(head),0);
    env_original_start=(EnvStart)camera_detour(0x80054da60,env_start,sizeof(head),0);
    if(!env_original_wave || !env_original_start || !env_redirect_call((void *)0x80054db26,env_play) ||
       !env_redirect_call((void *)0x800df1cfa,env_submit)) return 0;
    env_original_update=(EnvUpdate)camera_detour(0x800cb58a0,env_update,sizeof(head),0);
    return env_original_update!=NULL;
}
