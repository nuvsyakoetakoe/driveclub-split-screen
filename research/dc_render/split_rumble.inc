/* The native manager calculates motor strengths per racer. Route its output
 * to the controller assigned to that racer for driving. */
typedef __attribute__((sysv_abi)) int (*SplitRumbleTypeFn)(void *,int);
typedef __attribute__((sysv_abi)) void (*SplitRumbleSendFn)(void *,int,float,float);
typedef struct { WORD low,high; } SplitRumbleMotors;
typedef DWORD (WINAPI *SplitXInputSetFn)(DWORD,const SplitRumbleMotors *);
static SplitRumbleTypeFn split_rumble_original_type;
static SplitRumbleSendFn split_rumble_original_send;
static SplitXInputSetFn split_rumble_xinput;
static SRWLOCK split_rumble_lock=SRWLOCK_INIT;
static ULONGLONG split_rumble_last_send[4];

/* -2 hands control back to single-player, -1 discards a non-pad effect. */
static int split_rumble_port(int seat) {
#ifdef DC_NATIVE_MENU
    if(!InterlockedCompareExchange(&menu_race_active,0,0)) return -2;
    if(seat<0 || seat>1) return -1;
    AcquireSRWLockShared(&camera_lock);
    const int port=camera_packet.active && !(camera_packet.keyboard_mask&(1u<<seat))
        ? camera_packet.gamepad_ports[seat] : -1;
    ReleaseSRWLockShared(&camera_lock);
    return port>=0 && port<4?port:-1;
#else
    if(seat!=1) return -2;
    AcquireSRWLockShared(&camera_lock);
    const int port=camera_packet.active && camera_packet.keyboard_mask==1 && camera_packet_has_actors()
        ? camera_packet.gamepad_ports[1] : -1;
    ReleaseSRWLockShared(&camera_lock);
    return port>=0 && port<4?port:-2;
#endif
}

static __attribute__((sysv_abi)) int split_rumble_type(void *manager,int seat) {
    const uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    if(caller>=0x800db53b0 && caller<0x800db6c00 && split_rumble_port(seat)>=0) return 3;
    return split_rumble_original_type(manager,seat);
}

static WORD split_rumble_strength(float value) {
    return isfinite(value)?(WORD)(fmaxf(0,fminf(1,value))*65535.0f+0.5f):0;
}

static __attribute__((sysv_abi)) void split_rumble_send(void *manager,int seat,float high,float low) {
    const uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    const int port=split_rumble_port(seat);
    const int racer_effect=caller>=0x800db53b0 && caller<0x800db6c00;
    if(port==-2
#ifndef DC_NATIVE_MENU
       || !racer_effect
#endif
    ) {
        split_rumble_original_send(manager,seat,high,low);
        return;
    }
    if(port<0 || !racer_effect) return;
    const SplitRumbleMotors motors={split_rumble_strength(low),split_rumble_strength(high)};
    const int vibrating=motors.low || motors.high;
    AcquireSRWLockExclusive(&split_rumble_lock);
    if(vibrating || split_rumble_last_send[port]) {
        const DWORD status=split_rumble_xinput((DWORD)port,&motors);
        split_rumble_last_send[port]=status==ERROR_SUCCESS && vibrating ? GetTickCount64() : 0;
        static LONG reported;
        if(vibrating && status==ERROR_SUCCESS && !InterlockedExchange(&reported,1))
            printf("Split rumble: native P%d motors sent to XInput %d\n",seat+1,port);
#ifdef DC_FEEDBACK_QA
        static ULONGLONG sampled;
        const ULONGLONG now=GetTickCount64();
        if(vibrating && now-sampled>=500) {
            sampled=now;
            printf("Split rumble QA: ms=%llu P%d port=%d low=%u high=%u status=%lu\n",
                now-start,seat+1,port,motors.low,motors.high,(unsigned long)status);
        }
#endif
    }
    ReleaseSRWLockExclusive(&split_rumble_lock);
}

static void split_rumble_poll(void) {
    if(camera_installed!=1) return;
    if(!split_rumble_original_send) {
        /* Wait until race setup has finished writing guest code. */
        AcquireSRWLockShared(&camera_lock);
        const int ready=camera_packet_has_actors();
        ReleaseSRWLockShared(&camera_lock);
        if(!ready) return;
        static int tried;
        if(tried) return;
        tried=1;
        static const unsigned char type_head[]={0x89,0xf0,0x48,0x8d,0x04,0x40,0x8b,0x84,0x87,0xb4,0,0,0,0xc3};
        static const unsigned char send_head[]={0x55,0x48,0x89,0xe5,0x53,0x50,0xc5,0xfa,0x11,0x4d,0xf4,0xc5,0xfa,0x11,0x45,0xf0};
        const uintptr_t type=0x800d917b0,send=0x800d917c0;
        if(memcmp((const void *)type,type_head,sizeof(type_head)) || memcmp((const void *)send,send_head,sizeof(send_head))) return;
        HMODULE module=LoadLibraryExW(L"xinput1_4.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module) return;
        split_rumble_xinput=(SplitXInputSetFn)GetProcAddress(module,"XInputSetState");
        if(!split_rumble_xinput) return;
        split_rumble_original_send=(SplitRumbleSendFn)camera_detour(send,(void *)split_rumble_send,sizeof(send_head),0);
        if(split_rumble_original_send)
            split_rumble_original_type=(SplitRumbleTypeFn)camera_detour(type,(void *)split_rumble_type,sizeof(type_head),0);
    }
    const int first=split_rumble_port(0),second=split_rumble_port(1);
    AcquireSRWLockExclusive(&split_rumble_lock);
    for(int port=0;port<4;port++) if(split_rumble_last_send[port] &&
       ((port!=first && port!=second) || GetTickCount64()-split_rumble_last_send[port]>=150)) {
        const SplitRumbleMotors stop={0,0};
        split_rumble_xinput((DWORD)port,&stop);
        split_rumble_last_send[port]=0;
    }
    ReleaseSRWLockExclusive(&split_rumble_lock);
}
