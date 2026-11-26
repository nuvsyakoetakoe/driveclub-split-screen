/* FinishLineDisplayComponent shares course geometry. The retail update
 * chooses player zero. Keep its per-racer visibility and fade distance for
 * each main view, then submit the same native geometry in the correct view. */
typedef __attribute__((sysv_abi)) void (*FinishUpdateFn)(unsigned char *);
typedef __attribute__((sysv_abi)) unsigned char (*FinishDrawFn)(unsigned char *,const float *,void *);
typedef __attribute__((sysv_abi)) void *(*FinishSelectFn)(void *,int);

static FinishUpdateFn finish_original_update;
static FinishDrawFn finish_original_draw;
static FinishSelectFn finish_original_racer,finish_original_view;
static _Thread_local int finish_updating_second;
static _Thread_local int finish_drawing_seat=-1;
static SRWLOCK finish_lock=SRWLOCK_INIT;
static struct {
    const void *component;
    void *contexts[2];
    float distance;
    unsigned char visible,loop;
} finish_second;

/* The shared presentation state can advance before both racers finish.
 * Permit the native draw in that state for an identified main view. The
 * native update has already checked whether its racer has finished. */
static __attribute__((sysv_abi,used,noinline)) unsigned char finish_stage_allowed(void) {
    const unsigned stage=*(const unsigned *)0x80174db10;
    return (stage&~1u)==2 || (stage==4 && finish_drawing_seat>=0);
}

static __attribute__((used)) const uintptr_t finish_stage_draw=0x8000ccbd4;
static __attribute__((used)) const uintptr_t finish_stage_skip=0x8000ccc08;
static __attribute__((sysv_abi,naked)) void finish_stage_gate(void) {
    /* At this point the native draw keeps its live data in callee-saved
     * registers and its stack is aligned for the next call. */
    __asm__ volatile(
        "call finish_stage_allowed\n\t"
        "test %al, %al\n\t"
        "jz 1f\n\t"
        "jmp *finish_stage_draw(%rip)\n\t"
        "1: jmp *finish_stage_skip(%rip)\n\t");
}

static __attribute__((sysv_abi)) void *finish_racer(void *manager,int seat) {
    if(finish_updating_second && (uintptr_t)__builtin_return_address(0)==0x8000cd060)
        seat=1;
    return finish_original_racer(manager,seat);
}

static __attribute__((sysv_abi)) void *finish_view(void *manager,int seat) {
    const uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    if(finish_drawing_seat==1 && caller==0x8000ccd2d)
        seat=1;
#ifdef DC_SPLIT_ENGINE_AUDIO
    if(split_engine_p2 && caller==0x800cbe465 && ((void **)manager)[2])
        seat=1;
#endif
    return finish_original_view(manager,seat);
}

static __attribute__((sysv_abi)) void finish_update(unsigned char *component) {
    finish_original_update(component);
    if(!visibility_active()) return;

    _Alignas(16) unsigned char copy[0xb0];
    memcpy(copy,component,sizeof(copy));
    finish_updating_second=1;
    finish_original_update(copy);
    finish_updating_second=0;

    AcquireSRWLockExclusive(&finish_lock);
    if(finish_second.component!=component) {
        finish_second.component=component;
        finish_second.contexts[0]=finish_second.contexts[1]=NULL;
    }
    finish_second.visible=copy[0x10];
    finish_second.loop=copy[0x54];
    memcpy(&finish_second.distance,copy+0x1c,sizeof(float));
    ReleaseSRWLockExclusive(&finish_lock);
}

static int finish_matches_position(const void *view,const float *position) {
    if(!view) return 0;
    const float *eye=(const float *)((const unsigned char *)view+0x60);
    return fabsf(eye[0]-position[0])<0.02f && fabsf(eye[1]-position[1])<0.02f &&
           fabsf(eye[2]-position[2])<0.02f;
}

static __attribute__((sysv_abi)) unsigned char finish_draw(unsigned char *component,const float *camera,void *context) {
    if(!visibility_active() || (uintptr_t)__builtin_return_address(0)!=0x8005defe6)
        return finish_original_draw(component,camera,context);

    const int first=finish_matches_position(visibility_views[0],camera+12);
    const int second=finish_matches_position(visibility_views[1],camera+12);
    AcquireSRWLockExclusive(&finish_lock);
    const int current=finish_second.component==component;
    int seat=-1;
    if(current && first!=second) {
        seat=second;
        finish_second.contexts[seat]=context;
    } else if(current && finish_second.contexts[0]!=finish_second.contexts[1]) {
        if(finish_second.contexts[0]==context) seat=0;
        if(finish_second.contexts[1]==context) seat=1;
    }
    const unsigned char visible=finish_second.visible,loop=finish_second.loop;
    const float distance=finish_second.distance;
    ReleaseSRWLockExclusive(&finish_lock);
    if(seat<0) return finish_original_draw(component,camera,context);

    _Alignas(16) unsigned char copy[0xb0];
    if(seat==1) {
        if(!visible) return 0;
        memcpy(copy,component,sizeof(copy));
        copy[0x10]=visible;
        copy[0x54]=loop;
        memcpy(copy+0x1c,&distance,sizeof(distance));
        component=copy;
    }
    const unsigned stage=*(const unsigned *)0x80174db10;
    const int saved_seat=finish_drawing_seat;
    finish_drawing_seat=seat;
    unsigned char drawn=finish_original_draw(component,camera,context);
    finish_drawing_seat=saved_seat;
    static LONG reported[2][2];
    if(drawn && !InterlockedExchange(&reported[seat][stage==4],1))
        printf("Split finish line: P%d native draw at %llu ms (stage %u)\n",seat+1,GetTickCount64()-start,stage);
    return drawn;
}

static void finish_line_install(void) {
    if(finish_original_update || visibility_installed!=1) return;
    const uintptr_t racer=0x8004744a0, view=0x80014db30, update=0x8000ccfc0, draw=0x8000ccb70, stage=0x8000ccbc3;
    static const unsigned char racer_head[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x83,0xec,0x30};
    static const unsigned char view_head[]={0x89,0xf0,0x48,0x8b,0x44,0xc7,0x08,0x48,0x8b,0x40,0x10,0xc3,0x0f,0x1f,0x40,0x00};
    static const unsigned char update_head[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x49,0x89,0xfe};
    static const unsigned char draw_head[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53};
    static const unsigned char stage_head[]={0x48,0x8d,0x05,0x46,0x0f,0x68,0x01,0x8b,0x00,
        0x83,0xe0,0xfe,0x83,0xf8,0x02,0x75,0x34};
    if(memcmp((const void *)racer,racer_head,sizeof(racer_head)) ||
       memcmp((const void *)view,view_head,sizeof(view_head)) ||
       memcmp((const void *)update,update_head,sizeof(update_head)) ||
       memcmp((const void *)draw,draw_head,sizeof(draw_head)) ||
       memcmp((const void *)stage,stage_head,sizeof(stage_head))) return;
    finish_original_racer=(FinishSelectFn)camera_detour(racer,(void *)finish_racer,sizeof(racer_head),0);
    finish_original_view=(FinishSelectFn)camera_detour(view,(void *)finish_view,sizeof(view_head),0);
    if(finish_original_racer && finish_original_view) {
        finish_original_draw=(FinishDrawFn)camera_detour(draw,(void *)finish_draw,sizeof(draw_head),0);
        if(finish_original_draw)
            finish_original_update=(FinishUpdateFn)camera_detour(update,(void *)finish_update,sizeof(update_head),0);
        if(finish_original_update)
            camera_detour(stage,(void *)finish_stage_gate,sizeof(stage_head),0);
    }
}
