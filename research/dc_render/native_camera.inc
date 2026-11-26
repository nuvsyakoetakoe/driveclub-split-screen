/* Per-player input for the retail vehicle camera. The game still computes all
 * camera transforms, smoothing and clipping. Addresses are for the checked 1.28 image. */
#include <stdint.h>
#include <math.h>
#include <stdbool.h>

typedef struct {
    uint32_t magic, sequence, active, keyboard_mask;
    int32_t actors[2];
    float look[2][2];
    uint32_t rear_mask,hud_hidden_mask;
    int32_t gamepad_ports[2];
} CameraPacket;
_Static_assert(sizeof(CameraPacket)==56,"camera input protocol");
static CameraPacket camera_packet;
static int32_t camera_race_actors[2];
static SRWLOCK camera_lock=SRWLOCK_INIT;
static ULONGLONG camera_fresh;
static _Thread_local int camera_seat=-1;
static int camera_installed;
static void *camera_code;
enum { CAMERA_CODE_CAPACITY=8192 };
static size_t camera_code_used;
typedef __attribute__((sysv_abi)) float (*CameraInputFn)(void*,const char*);
typedef __attribute__((sysv_abi)) void (*CameraUpdateFn)(void*,float,float);
typedef __attribute__((sysv_abi)) unsigned char (*CameraHasInputFn)(void*);
typedef __attribute__((sysv_abi)) int32_t (*CameraActorFn)(void*);
static CameraInputFn camera_original_input;
static CameraUpdateFn camera_original_update;
static CameraHasInputFn camera_original_has_input;
static void *camera_native_input[2];
typedef uint32_t (*CameraMouseFn)(float*,float*);
static CameraMouseFn camera_original_mouse;
static float camera_mouse_x,camera_mouse_y;
typedef bool (*CameraRumbleFn)(void *,uint16_t,uint16_t,uint32_t);
static CameraRumbleFn camera_original_rumble,camera_original_triggers;

static uint32_t camera_mouse_read(float *x,float *y) {
    float dx=0,dy=0;
    uint32_t buttons=camera_original_mouse(&dx,&dy);
    AcquireSRWLockExclusive(&camera_lock);
    camera_mouse_x+=dx; camera_mouse_y+=dy;
    int active=camera_packet.active && GetTickCount64()-camera_fresh<500;
    ReleaseSRWLockExclusive(&camera_lock);
    if(x) *x=active?0:dx;
    if(y) *y=active?0:dy;
    return buttons;
}
static void camera_hook_sdl(const char *name,void *replacement,void *storage) {
    const FARPROC original=GetProcAddress(GetModuleHandleW(L"SDL3.dll"),name);
    if(!original) return;
    unsigned char *base=(unsigned char*)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER *dos=(IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS64 *nt=(IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);
    DWORD imports=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if(!imports) return;
    for(IMAGE_IMPORT_DESCRIPTOR *desc=(IMAGE_IMPORT_DESCRIPTOR*)(base+imports);desc->Name;desc++) {
        if(_stricmp((const char*)base+desc->Name,"SDL3.dll")) continue;
        IMAGE_THUNK_DATA64 *thunk=(IMAGE_THUNK_DATA64*)(base+desc->FirstThunk);
        for(;thunk->u1.Function;thunk++) if(thunk->u1.Function==(uintptr_t)original) {
            DWORD previous;
            if(!VirtualProtect(&thunk->u1.Function,sizeof(uintptr_t),PAGE_READWRITE,&previous)) return;
            memcpy(storage,&original,sizeof(original));
            thunk->u1.Function=(uintptr_t)replacement;
            DWORD ignored;
            VirtualProtect(&thunk->u1.Function,sizeof(uintptr_t),previous,&ignored);
            return;
        }
    }
}

static int camera_wrong_pad_rumble(void) {
#ifdef DC_NATIVE_MENU
    return InterlockedCompareExchange(&menu_race_active,0,0)!=0;
#else
    AcquireSRWLockShared(&camera_lock);
    const int suppress=camera_packet.active && camera_packet.keyboard_mask==1 &&
        GetTickCount64()-camera_fresh<500;
    ReleaseSRWLockShared(&camera_lock);
    return suppress;
#endif
}

static bool camera_rumble(void *pad,uint16_t low,uint16_t high,uint32_t duration) {
    if(!camera_wrong_pad_rumble()) return camera_original_rumble(pad,low,high,duration);
    static LONG reported;
    if((low || high) && !InterlockedExchange(&reported,1))
        printf("Split input: primary-profile rumble blocked on P2's pad\n");
    /* Clearing the SDL motors here would also erase P2's XInput output. */
    return true;
}

static bool camera_triggers(void *pad,uint16_t left,uint16_t right,uint32_t duration) {
    if(camera_wrong_pad_rumble()
#ifdef DC_NATIVE_MENU
       && !InterlockedCompareExchange(&menu_primary_pad_triggers,0,0)
#endif
    ) return true;
    return camera_original_triggers(pad,left,right,duration);
}

/* Rendering needs both actor identities during the countdown. The active
 * field only authorizes input overrides once the drivers can take control. */
static int camera_packet_has_actors(void) {
    return camera_packet.actors[0]>0 && camera_packet.actors[1]>0 &&
        camera_packet.actors[0]!=camera_packet.actors[1] && GetTickCount64()-camera_fresh<500;
}

static int camera_hud_hidden(int seat) {
    AcquireSRWLockShared(&camera_lock);
    const int hidden=camera_packet.active && camera_packet_has_actors() &&
        (camera_packet.hud_hidden_mask & (1u<<seat));
    ReleaseSRWLockShared(&camera_lock);
    return hidden;
}
static int camera_actor_seat(void *camera) {
    int32_t actor=((CameraActorFn)(uintptr_t)0x8004f2110)(camera);
    int seat=-1;
    AcquireSRWLockShared(&camera_lock);
    if(camera_packet_has_actors()) {
        for(int i=0;i<2;i++) if(camera_packet.actors[i]==actor) seat=i;
    }
    ReleaseSRWLockShared(&camera_lock);
    return seat;
}
static __attribute__((sysv_abi)) float camera_input(void *input,const char *name) {
    const int rear=!strcmp(name,"LookBack");
    const int global=!strcmp(name,"CameraChange") || !strcmp(name,"ToggleOSDPad") || !strcmp(name,"ToggleOSDStick");
    if(rear || global || !strcmp(name,"LookX") || !strcmp(name,"LookZ")) {
        AcquireSRWLockExclusive(&camera_lock);
        int seat=camera_seat;
        if(!rear && !global && seat>=0 && !camera_native_input[seat]) {
            camera_native_input[seat]=input;
            printf("Split camera: P%d native input=%p\n",seat+1,input);
        }
        if(seat<0 && camera_native_input[0]!=camera_native_input[1])
            for(int i=0;i<2;i++) if(camera_native_input[i]==input) seat=i;
        if(!camera_packet.active || !camera_packet_has_actors()) {
            ReleaseSRWLockExclusive(&camera_lock);
            return camera_original_input(input,name);
        }
        if(global || rear) {
            const int pressed=rear && seat>=0 && (camera_packet.rear_mask & (1u<<seat));
            static unsigned rear_reported;
            if(pressed && !(rear_reported & (1u<<seat))) {
                rear_reported|=1u<<seat;
                printf("Split camera: P%d native LookBack\n",seat+1);
            }
            ReleaseSRWLockExclusive(&camera_lock);
            return pressed?1.0f:0.0f;
        }
        if(seat<0) {
            ReleaseSRWLockExclusive(&camera_lock);
            return camera_original_input(input,name);
        }
        float value=camera_packet.look[seat][name[4]=='Z'];
        static int reported[2];
        if(!reported[seat] && fabsf(value)>0.1f) {
            reported[seat]=1;
            printf("Split camera: P%d native %s %.3f\n",seat+1,name,value);
        }
        ReleaseSRWLockExclusive(&camera_lock);
        return value;
    }
    return camera_original_input(input,name);
}
static __attribute__((sysv_abi)) void camera_update(void *camera,float a,float b) {
    int previous=camera_seat;
    camera_seat=camera_actor_seat(camera);
    camera_original_update(camera,a,b);
    static int reported[2];
    if(camera_seat>=0 && !reported[camera_seat]) {
        AcquireSRWLockShared(&camera_lock);
        float x=camera_packet.look[camera_seat][0];
        ReleaseSRWLockShared(&camera_lock);
        if(fabsf(x)>0.1f && fabsf(*(const float*)((const unsigned char*)camera+0x200))>10) {
            reported[camera_seat]=1;
            const float *limits=(const float*)((const unsigned char*)camera+0x70);
            const float *look=(const float*)((const unsigned char*)camera+0x200);
            printf("Split camera: P%d limits %.3f %.3f %.3f %.3f result %.3f %.3f dt %.3f %.3f\n",
                   camera_seat+1,limits[0],limits[1],limits[2],limits[3],look[0],look[1],a,b);
        }
    }
    camera_seat=previous;
}
static __attribute__((sysv_abi)) unsigned char camera_has_input(void *camera) {
    int seat=camera_actor_seat(camera);
    if(seat<0) return camera_original_has_input(camera);
    AcquireSRWLockShared(&camera_lock);
    int active=camera_packet.active;
    float x=camera_packet.look[seat][0],z=camera_packet.look[seat][1];
    ReleaseSRWLockShared(&camera_lock);
    return active ? (fabsf(x)>0.001f || fabsf(z)>0.001f) : camera_original_has_input(camera);
}

static void camera_jump(unsigned char *code,void *target) {
    uintptr_t address=(uintptr_t)target;
    code[0]=0x48; code[1]=0xb8; memcpy(code+2,&address,8); code[10]=0xff; code[11]=0xe0;
}
static void *camera_detour(uintptr_t address,void *target,size_t length,int relocate_lea) {
    if(!camera_code || camera_code_used+64>CAMERA_CODE_CAPACITY) return NULL;
    unsigned char *original=(unsigned char*)address;
    unsigned char *gateway=(unsigned char*)camera_code+camera_code_used;
    camera_code_used+=64;
    memcpy(gateway,original,length);
    if(relocate_lea) {
        int32_t old_delta; memcpy(&old_delta,original+10,4);
        intptr_t delta=(intptr_t)(original+14+old_delta)-(intptr_t)(gateway+14);
        if(delta<INT32_MIN || delta>INT32_MAX) return NULL;
        int32_t new_delta=(int32_t)delta; memcpy(gateway+10,&new_delta,4);
    }
    /* An indirect jump preserves registers loaded by the copied instructions. */
    uintptr_t resume=address+length;
    gateway[length]=0xff; gateway[length+1]=0x25; memset(gateway+length+2,0,4);
    memcpy(gateway+length+6,&resume,8);
    DWORD previous;
    if(!VirtualProtect(original,length,PAGE_EXECUTE_READWRITE,&previous)) return NULL;
    camera_jump(original,target);
    memset(original+12,0x90,length-12);
    DWORD ignored;
    VirtualProtect(original,length,previous,&ignored);
    FlushInstructionCache(GetCurrentProcess(),original,length);
    return gateway;
}
static void *camera_port_update(const unsigned char *entry) {
    /* The port's high-FPS camera fix can already own this entry. Keep that
     * exact bridge as the original update when adding per-player routing. */
    static const unsigned char jump[]={0xff,0x25,0,0,0,0};
    static const unsigned char padding[]={0x90,0x90,0x90};
    if(memcmp(entry,jump,sizeof(jump)) || memcmp(entry+14,padding,sizeof(padding))) return NULL;
    void *target;
    memcpy(&target,entry+sizeof(jump),sizeof(target));
    MEMORY_BASIC_INFORMATION memory;
    if(!VirtualQuery(target,&memory,sizeof(memory)) || memory.AllocationBase!=GetModuleHandleW(NULL) ||
       !(memory.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE))) return NULL;
    return target;
}
static void camera_install(void) {
    if(camera_installed || !getenv("DC_SPLIT_CAMERA")) return;
#ifdef DC_NATIVE_MENU
    if(!InterlockedCompareExchange(&menu_frontend_ready,0,0)) return;
#endif
    const unsigned char prologue[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53};
    const unsigned char has_prologue[]={0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x8d,0x05,0x12,0xd4,0xd7,0x00};
    const uintptr_t addresses[]={0x800d8ecf0,0x800b92a40,0x800b93ce0};
    void *port_update=NULL;
    for(int i=0;i<3;i++) {
        MEMORY_BASIC_INFORMATION memory;
        if(!VirtualQuery((void*)addresses[i],&memory,sizeof(memory)) || memory.State!=MEM_COMMIT ||
           (memory.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return;
        if(memcmp((void*)addresses[i],i==2?has_prologue:prologue,i==2?14:13)) {
            if(i!=1 || !(port_update=camera_port_update((const unsigned char *)addresses[i]))) return;
        }
    }
    /* A nearby gateway keeps the one RIP-relative lea within signed 32-bit range. */
    for(uintptr_t at=0x810000000;at<0x820000000 && !camera_code;at+=0x10000)
        camera_code=VirtualAlloc((void*)at,CAMERA_CODE_CAPACITY,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
    if(!camera_code) { camera_installed=-1; fprintf(stderr,"Split camera: no gateway memory\n"); return; }
    camera_original_input=(CameraInputFn)camera_detour(addresses[0],(void*)camera_input,13,0);
    if(port_update) {
        camera_original_update=(CameraUpdateFn)port_update;
        if(!camera_detour(addresses[1],(void*)camera_update,17,0)) abort();
        puts("Split camera: preserving the port's high-FPS camera update");
    } else camera_original_update=(CameraUpdateFn)camera_detour(addresses[1],(void*)camera_update,13,0);
    camera_original_has_input=(CameraHasInputFn)camera_detour(addresses[2],(void*)camera_has_input,14,1);
    if(!camera_original_input || !camera_original_update || !camera_original_has_input) abort();
    camera_installed=1;
    printf("Split camera: native per-player look installed\n");
}
static void camera_poll(void) {
    if(!getenv("DC_SPLIT_CAMERA")) return;
    if(!camera_original_mouse) {
        camera_hook_sdl("SDL_GetRelativeMouseState",(void *)camera_mouse_read,&camera_original_mouse);
        if(camera_original_mouse) printf("Split camera: relative mouse routed from SDL\n");
    }
    if(!camera_original_rumble)
        camera_hook_sdl("SDL_RumbleGamepad",(void *)camera_rumble,&camera_original_rumble);
    if(!camera_original_triggers)
        camera_hook_sdl("SDL_RumbleGamepadTriggers",(void *)camera_triggers,&camera_original_triggers);
    camera_install();
#ifdef DC_NATIVE_MENU
    AcquireSRWLockExclusive(&camera_lock);
    camera_fresh=GetTickCount64();
    ReleaseSRWLockExclusive(&camera_lock);
#else
    CameraPacket next;
    FILE *file=fopen("mods/splitscreen_camera.dat","rb");
    if(file) {
        int valid=fread(&next,1,sizeof(next),file)==sizeof(next) && next.magic==0x44434341;
        fclose(file);
        AcquireSRWLockExclusive(&camera_lock);
        if(valid && next.sequence!=camera_packet.sequence) {
            if(memcmp(next.actors,camera_packet.actors,sizeof(next.actors)))
                memset(camera_native_input,0,sizeof(camera_native_input));
            camera_packet=next;
            camera_fresh=GetTickCount64();
            if(camera_packet_has_actors())
                memcpy(camera_race_actors,camera_packet.actors,sizeof(camera_race_actors));
        }
        ReleaseSRWLockExclusive(&camera_lock);
    }
#endif
    if(camera_original_mouse) {
        AcquireSRWLockExclusive(&camera_lock);
        float x=camera_mouse_x,y=camera_mouse_y;
        camera_mouse_x=0; camera_mouse_y=0;
        if(camera_packet.active) for(int seat=0;seat<2;seat++) if(camera_packet.keyboard_mask&(1u<<seat)) {
            static int reported;
            if(!reported && (x!=0 || y!=0)) {
                reported=1;
                printf("Split camera: mouse delta %.1f %.1f routed to P%d\n",x,y,seat+1);
            }
            camera_packet.look[seat][0]=fmaxf(-1,fminf(1,camera_packet.look[seat][0]+x*0.025f));
            camera_packet.look[seat][1]=fmaxf(-1,fminf(1,camera_packet.look[seat][1]+y*0.025f));
            break;
        }
        ReleaseSRWLockExclusive(&camera_lock);
    }
}
