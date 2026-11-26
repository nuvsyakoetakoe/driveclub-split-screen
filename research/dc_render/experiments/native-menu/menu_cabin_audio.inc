/* Both local engines pass their freshly computed wave gain through the game's
 * normal channel update. Keep the cockpit mix local to that call. A wave or
 * its mixer channel can be retired asynchronously by the audio thread. */
typedef __attribute__((sysv_abi)) void (*CabinCueFn)(void *);
typedef __attribute__((sysv_abi)) unsigned char (*CabinLevelsFn)(void *,void *,float *,float *);
static CabinCueFn cabin_original_cue;
static CabinLevelsFn cabin_original_levels;
static void *volatile cabin_cues[2];
static LONG cabin_interior[2];
static _Thread_local int cabin_seat=-1;
static _Thread_local int cabin_request=-1;
#ifdef DC_FEEDBACK_QA
static int cabin_meter_enabled;
static int cabin_test_impact_only;
#endif

#include "menu_environment_audio.inc"

static void cabin_publish(int seat,const unsigned char *engine) {
    void *cue=engine && *(const uint32_t *)(engine+0x10)==3?*(void *const *)(engine+0xe8):NULL;
    InterlockedExchangePointer(&cabin_cues[seat],cue);
    if(cabin_request>=0) InterlockedExchange(&cabin_interior[seat],cabin_request!=0);
}

static __attribute__((sysv_abi)) unsigned char cabin_levels(void *wave,void *sample,float *gain,float *pitch) {
    unsigned char active=cabin_original_levels(wave,sample,gain,pitch);
#ifdef DC_FEEDBACK_QA
    if(cabin_test_impact_only && (uintptr_t)__builtin_return_address(0)==0x8007d2d14) *gain=0;
#endif
    if(cabin_seat>=0 && (uintptr_t)__builtin_return_address(0)==0x8007d2d14 &&
       InterlockedCompareExchange(&cabin_interior[cabin_seat],0,0) && *gain>0) {
        *gain*=0.60f;
#ifdef DC_FEEDBACK_QA
        static LONG seen[2];
        if(!InterlockedExchange(&seen[cabin_seat],1)) printf("Split cabin: engine wave gain applied to P%d\n",cabin_seat+1);
#endif
    }
#ifdef DC_FEEDBACK_QA
    if(cabin_meter_enabled && cabin_seat>=0 && active && *gain>0) {
        static ULONGLONG reported[2];
        ULONGLONG now=GetTickCount64();
        if(now-reported[cabin_seat]>=1000) {
            reported[cabin_seat]=now;
            printf("Split local engine P%d wave gain=%.5f pitch=%.3f\n",cabin_seat+1,*gain,*pitch);
        }
    }
#endif
    return active;
}

static __attribute__((sysv_abi)) void cabin_cue(void *cue) {
    const int previous=cabin_seat;
    cabin_seat=cue && cue==InterlockedCompareExchangePointer(&cabin_cues[0],NULL,NULL)?0:
        cue && cue==InterlockedCompareExchangePointer(&cabin_cues[1],NULL,NULL)?1:-1;
    cabin_original_cue(cue);
    cabin_seat=previous;
}

static void cabin_clear(void) {
    for(int seat=0;seat<2;seat++) {
        InterlockedExchangePointer(&cabin_cues[seat],NULL);
        InterlockedExchange(&cabin_interior[seat],0);
    }
}

static void cabin_install(void) {
#ifdef DC_FEEDBACK_QA
    cabin_meter_enabled=getenv("DC_QA_ENGINE_MIX")!=NULL;
    cabin_test_impact_only=getenv("DC_QA_IMPACT_ONLY")!=NULL;
#endif
    if(cabin_original_cue) return;
    static const unsigned char head[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53};
    if(memcmp((const void *)0x8007d0a60,head,sizeof(head)) || memcmp((const void *)0x8007cf970,head,sizeof(head))) return;
    cabin_original_levels=(CabinLevelsFn)camera_detour(0x8007cf970,(void *)cabin_levels,sizeof(head),0);
    if(cabin_original_levels)
        cabin_original_cue=(CabinCueFn)camera_detour(0x8007d0a60,(void *)cabin_cue,sizeof(head),0);
}
