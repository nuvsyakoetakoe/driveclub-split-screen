/* VehicleAudioComponentManager chooses the full player engine for the actor in
 * its selected view. Use P2's view while updating P2's vehicle and leave the
 * stock selection in place for the rest of the grid. */
#define DC_SPLIT_ENGINE_AUDIO 1
typedef __attribute__((sysv_abi)) void (*SplitEngineUpdateFn)(unsigned char *,float);
typedef __attribute__((sysv_abi)) void (*SplitEngineActorsFn)(void *,void *,int);
typedef __attribute__((sysv_abi)) void (*SplitEngineSwitchFn)(unsigned char *);
typedef __attribute__((sysv_abi)) void (*SplitEngineEventFn)(unsigned char *,void *,unsigned,unsigned,unsigned,float,float);
typedef __attribute__((sysv_abi)) void (*SplitEngineInteriorFn)(void *,int);
typedef __attribute__((sysv_abi)) void (*SplitEngineLoadFn)(unsigned char *);
typedef __attribute__((sysv_abi)) void (*SplitEnginePositionFn)(void *,const float *,const float *,const float *);

static SplitEngineUpdateFn split_engine_original_update;
static SplitEngineActorsFn split_engine_original_actors;
static SplitEngineSwitchFn split_engine_original_switch;
static SplitEngineEventFn split_engine_original_event;
static SplitEngineInteriorFn split_engine_original_interior;
static SplitEngineLoadFn split_engine_original_load;
static SplitEnginePositionFn split_engine_original_position;
static _Thread_local int split_engine_p2;
static _Thread_local int split_engine_p1;
static unsigned char split_engine_p2_transition;
static unsigned char *const split_engine_transition=(void *)0x8027f3d80;
static unsigned char *const split_engine_slot_free=(void *)0x8027fc7e0;
static void *volatile split_engine_p2_event;
static void *volatile split_engine_requests[2];
typedef __attribute__((sysv_abi)) void (*SplitBankPump)(unsigned char *);
static SplitBankPump split_bank_original_pump;

/* Only the bank manager owns the pending list. A showroom or second local
 * car can already occupy the single reserved player-bank slot. Allow the
 * manager to admit a queued local request through its normal shared pool. */
static __attribute__((sysv_abi)) void split_bank_pump(unsigned char *manager) {
    void **end=*(void ***)(manager+0x28);
    if(end && end[0]!=end && visibility_active() && *(int *)(manager+0x40)<*(int *)(manager+0x44)) {
        void *first=InterlockedCompareExchangePointer(&split_engine_requests[0],NULL,NULL);
        void *second=InterlockedCompareExchangePointer(&split_engine_requests[1],NULL,NULL);
        for(void **node=end[0];node!=end;node=node[0]) {
            unsigned char *request=node[2];
            if((request==first || request==second) && request[0x29] &&
               *(void **)(request+0x30)==manager+0x48 && *(int *)(manager+0x50)>=*(int *)(manager+0x54)) {
                *(void **)(request+0x30)=manager+0x38;
                printf("Split engine: admitted queued P%d player bank through shared pool\n",request==first?1:2);
            }
        }
    }
    split_bank_original_pump(manager);
}

#ifdef DC_NATIVE_MENU
#include "experiments/native-menu/menu_cabin_audio.inc"
#endif
#include "split_vehicle_fx.inc"

static __attribute__((sysv_abi)) void split_engine_position(void *event,const float *position,
                                                              const float *velocity,const float *direction) {
    _Alignas(16) float mapped_position[4],mapped_velocity[4],mapped_direction[4];
    if(split_engine_p2 && (split_fx_position || (uintptr_t)__builtin_return_address(0)==0x800d67430) && position &&
       split_audio_map_source(position,velocity,direction,mapped_position,mapped_velocity,mapped_direction,0)) {
        split_engine_original_position(event,mapped_position,velocity?mapped_velocity:NULL,direction?mapped_direction:NULL);
        return;
    }
    split_engine_original_position(event,position,velocity,direction);
}

static __attribute__((sysv_abi)) void split_engine_update(unsigned char *component,float dt) {
    if(*(const uint32_t *)(component+0x78)==0x12345678) {
        split_engine_original_update(component,dt);
        return;
    }
    const int actor=((CameraActorFn)0x8004f2110)(component);
    const unsigned char *voice=*(const unsigned char **)(component+0x68);
    void *engine=voice && voice[0x59]==0?*(void *const *)(voice+0x70):NULL;
    const int already_local=engine && engine==InterlockedCompareExchangePointer(&split_engine_p2_event,NULL,NULL);
    AcquireSRWLockShared(&camera_lock);
    const int second=(camera_packet.active && camera_packet_has_actors() && actor==camera_packet.actors[1]) ||
        (already_local && actor==camera_race_actors[1]);
    const int first=camera_packet.active && camera_packet_has_actors() && actor==camera_packet.actors[0];
#ifdef DC_NATIVE_MENU
    const int local_seat=camera_packet.active && camera_packet_has_actors() && actor==camera_packet.actors[0]?0:
        second?1:-1;
    cabin_request=-1;
#endif
    ReleaseSRWLockShared(&camera_lock);

    const int previous=split_engine_p2;
    const int previous_first=split_engine_p1;
    split_engine_p1=first;
    split_engine_p2=second;
    split_engine_original_update(component,dt);
    split_engine_p1=previous_first;
    split_engine_p2=previous;
#ifdef DC_NATIVE_MENU
    if(local_seat>=0 && cabin_original_cue) {
        voice=*(const unsigned char **)(component+0x68);
        engine=voice && voice[0x59]==0?*(void *const *)(voice+0x70):NULL;
        InterlockedExchangePointer(&split_engine_requests[local_seat],engine?*(void **)((char *)engine+0xe0):NULL);
        cabin_publish(local_seat,engine);
#ifdef DC_FEEDBACK_QA
        static ULONGLONG sampled_voice[2];
        ULONGLONG now=GetTickCount64();
        if(cabin_meter_enabled && now-sampled_voice[local_seat]>2000) {
            sampled_voice[local_seat]=now;
            printf("Local engine P%d actor=%d kind=%d state=%d cue=%p\n",local_seat+1,actor,
                   voice?voice[0x59]:-1,engine?*(int*)((char*)engine+0x10):-1,cabin_cues[local_seat]);
            if(engine) {
                void *bank=*(void **)((char *)engine+0xe0);
                unsigned char *pool=bank?*(unsigned char **)((char *)bank+0x30):NULL;
                printf("Local bank P%d %.128s request=%p ready=%d queued=%d pool=%d/%d\n",local_seat+1,
                       (char *)engine+0x38,bank,bank?((unsigned char *)bank)[0x28]:-1,
                       bank?((unsigned char *)bank)[0x29]:-1,pool?*(int *)(pool+8):0,pool?*(int *)(pool+12):0);
            }
        }
#endif
    }
#endif
    if(!second) return;

    voice=*(const unsigned char **)(component+0x68);
    engine=voice && voice[0x59]==0?*(void *const *)(voice+0x70):NULL;
    InterlockedExchangePointer(&split_engine_p2_event,engine);
#ifdef DC_FEEDBACK_QA
    static ULONGLONG sampled;
    const ULONGLONG now=GetTickCount64();
    if(engine && now-sampled>=1000) {
        sampled=now;
        const unsigned char *event=engine;
        printf("Split engine QA: ms=%llu actor=%d state=%u source=%p\n",now-start,actor,
            *(const uint32_t *)(event+0x10),*(void *const *)(event+0xe8));
    }
#endif
}

static __attribute__((sysv_abi)) void split_engine_actors(void *manager,void *actors,int seat) {
    if((split_engine_p1 || split_engine_p2) && (uintptr_t)__builtin_return_address(0)==0x800cbe81f && ((void **)manager)[2])
        seat=split_engine_p2?1:0;
    split_engine_original_actors(manager,actors,seat);
}

static __attribute__((sysv_abi)) void split_engine_switch(unsigned char *voice) {
    if(!split_engine_p2) {
        split_engine_original_switch(voice);
        return;
    }
    if(voice[0x59]==0) return;
    /* The switch uses global ownership and transition flags. Borrow them for the
     * synchronous P2 call, then restore P1's state before updating any other car. */
    const unsigned char transition=*split_engine_transition, free_slot=*split_engine_slot_free;
    *split_engine_transition=0;
    *split_engine_slot_free=1;
    split_engine_original_switch(voice);
    split_engine_p2_transition=*split_engine_transition;
    *split_engine_transition=transition;
    *split_engine_slot_free=free_slot;
}

static __attribute__((sysv_abi)) void split_engine_event(unsigned char *voice,void *car,unsigned primary,
                                                        unsigned interior,unsigned first,float dt,float rpm) {
    if(!split_engine_p2) {
        split_engine_original_event(voice,car,primary,interior,first,dt,rpm);
        return;
    }
    const unsigned char transition=*split_engine_transition;
    *split_engine_transition=split_engine_p2_transition;
    /* Keep the per-car engine parameters and P1's global surfaces controller. */
    split_engine_original_event(voice,car,0,interior,first,dt,rpm);
    split_engine_p2_transition=*split_engine_transition;
    *split_engine_transition=transition;
    unsigned char *engine=voice[0x59]==0?*(unsigned char **)(voice+0x70):NULL;
    if(engine) {
        /* Stock sends this command only to the selected player's live engine.
         * Its other primary-only calls update the shared exciter and surfaces. */
        if(interior && *(int *)(engine+0x10)==3) {
            typedef __attribute__((sysv_abi)) unsigned (*SoundHash)(const char *);
            typedef __attribute__((sysv_abi)) unsigned (*CommandIndex)(unsigned);
            typedef __attribute__((sysv_abi)) void (*EngineCommand)(void *,unsigned);
            static unsigned command;
            static int initialized;
            if(!initialized) {
                command=((CommandIndex)0x8007d3680)(((SoundHash)0x800557b10)("internal camera"));
                initialized=1;
            }
            ((EngineCommand)(*(void ***)engine)[0x48/8])(engine,command);
        }
        split_fx_surface(car,interior,dt,rpm);
    }
}

static __attribute__((sysv_abi)) void split_engine_interior(void *manager,int interior) {
#ifdef DC_NATIVE_MENU
    cabin_request=interior;
#endif
#ifdef DC_FEEDBACK_QA
    if(split_engine_p2) {
        static int last=-1;
        if(last!=interior) {
            last=interior;
            printf("Split audio P2 cockpit QA: ms=%llu requested=%d\n",GetTickCount64()-start,interior);
        }
    }
#endif
    if(split_engine_p2) return;
    /* Mix_InternalCamera is an FMOD snapshot for the whole output. Applying
     * it for P1 would also filter P2's engine, even after spatial routing. */
    const int shared=visibility_active();
    split_engine_original_interior(manager,shared?0:interior);
#ifdef DC_FEEDBACK_QA
    static int last_request=-1, last_applied=-1;
    const int applied=shared?0:interior;
    if(last_request!=interior || last_applied!=applied) {
        printf("Split audio snapshot QA: ms=%llu requested=%d applied=%d split=%d\n",
            GetTickCount64()-start,interior,applied,shared);
        last_request=interior;
        last_applied=applied;
    }
#endif
}

static __attribute__((sysv_abi)) void split_engine_load(unsigned char *engine) {
    uint32_t *kind=(uint32_t *)(engine+0xc);
    const int second=split_engine_p2 || engine==InterlockedCompareExchangePointer(&split_engine_p2_event,NULL,NULL);
    if(*kind!=1 || !second) {
        split_engine_original_load(engine);
        return;
    }
    AcquireSRWLockShared(&camera_lock);
    const int active=camera_packet.active && camera_packet_has_actors();
    ReleaseSRWLockShared(&camera_lock);
    if(!active) {
        split_engine_original_load(engine);
        return;
    }
    /* The player bank request uses the single reserved slot when kind is 1.
     * Load P2's player bank through the shared pool and keep its playback kind. */
    *kind=2;
    split_engine_original_load(engine);
    *kind=1;
    printf("Split engine: P%d player bank requested from the shared pool\n",second?2:1);
}

static void split_engine_install(void) {
    static int attempted;
    if(attempted || !visibility_active()) return;
    attempted=1;
    static const unsigned char head[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53};
    static const unsigned char switch_head[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x83,0xec,0x50};
    static const unsigned char load_head[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x83,0xec,0x30};
    static const unsigned char interior_head[]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb,0x48,0x8b,0xbb,0x90,0,0,0};
    if(memcmp((void *)0x800cbe410,head,sizeof(head)) ||
       memcmp((void *)0x80014dd80,head,sizeof(head)) ||
       memcmp((void *)0x800d66e70,switch_head,sizeof(switch_head)) ||
       memcmp((void *)0x800d67360,head,sizeof(head)) ||
       memcmp((void *)0x800ca8ae0,interior_head,sizeof(interior_head)) ||
       memcmp((void *)0x800552cc0,load_head,sizeof(load_head)) ||
       memcmp((void *)0x800552b20,head,sizeof(head))) return;

    split_engine_original_actors=(SplitEngineActorsFn)camera_detour(0x80014dd80,(void *)split_engine_actors,sizeof(head),0);
    split_engine_original_switch=(SplitEngineSwitchFn)camera_detour(0x800d66e70,(void *)split_engine_switch,sizeof(switch_head),0);
    split_engine_original_event=(SplitEngineEventFn)camera_detour(0x800d67360,(void *)split_engine_event,sizeof(head),0);
    split_engine_original_interior=(SplitEngineInteriorFn)camera_detour(0x800ca8ae0,(void *)split_engine_interior,sizeof(interior_head),0);
    split_engine_original_load=(SplitEngineLoadFn)camera_detour(0x800552cc0,(void *)split_engine_load,sizeof(load_head),0);
    split_engine_original_position=(SplitEnginePositionFn)camera_detour(0x800552b20,(void *)split_engine_position,sizeof(head),0);
    if(split_engine_original_actors && split_engine_original_switch && split_engine_original_event &&
       split_engine_original_interior && split_engine_original_load && split_engine_original_position)
        split_engine_original_update=(SplitEngineUpdateFn)camera_detour(0x800cbe410,(void *)split_engine_update,sizeof(head),0);
#ifdef DC_NATIVE_MENU
    if(split_engine_original_update) cabin_install();
#endif
    if(split_engine_original_update) split_fx_install();
    if(split_engine_original_update && camera_code_used+64<=CAMERA_CODE_CAPACITY && !memcmp((void *)0x800559280,head,sizeof(head)))
        split_bank_original_pump=(SplitBankPump)camera_detour(0x800559280,(void *)split_bank_pump,sizeof(head),0);
#ifdef DC_NATIVE_MENU
    if(split_engine_original_update && cabin_original_cue && !env_install())
        fputs("Split room: unsupported native sampler or mixer layout\n",stderr);
#endif
}
