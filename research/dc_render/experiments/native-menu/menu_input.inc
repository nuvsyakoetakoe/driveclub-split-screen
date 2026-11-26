#include <xinput.h>

typedef DWORD (WINAPI *MenuXInputGetStateFn)(DWORD,XINPUT_STATE *);
typedef __attribute__((sysv_abi)) void (*MenuLuaNumberFn)(void *,double);
static const MenuLuaNumberFn menu_lua_number=(MenuLuaNumberFn)0x8009ea830;
static MenuXInputGetStateFn menu_input_get_state;
static int menu_ports[2]={-1,-1};
static int menu_device_choice,menu_keyboard_layout;
static unsigned menu_input_sequence,menu_hud_down;

typedef struct {
    float gas,brake,steer,look_x,look_y;
    int handbrake,cycle,rear,hud;
} MenuControls;

#include "menu_bindings.inc"

static void menu_input_begin(void) {
    if(!menu_original_keyboard) menu_qa_keyboard_install();
    if(!menu_input_get_state) {
        HMODULE module=GetModuleHandleW(L"xinput1_4.dll");
        if(!module) module=LoadLibraryExW(L"xinput1_4.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(module) menu_input_get_state=(MenuXInputGetStateFn)GetProcAddress(module,"XInputGetState");
    }
    int ports[2]={-1,-1},count=0;
    for(DWORD port=0;port<4 && count<2 && menu_input_get_state;port++) {
        XINPUT_STATE state;
        if(menu_input_get_state(port,&state)==ERROR_SUCCESS) ports[count++]=(int)port;
    }
    menu_ports[0]=count==2?ports[0]:-1;
    menu_ports[1]=count?ports[count-1]:-1;
    const int first=count?ports[0]:0;
    switch(menu_device_choice) {
    case 1: menu_ports[0]=-1; menu_ports[1]=first; break;
    case 2: menu_ports[0]=first; menu_ports[1]=-1; break;
    case 3: menu_ports[0]=first; menu_ports[1]=count==2?ports[1]:(first+1)%4; break;
    case 4: menu_ports[0]=menu_ports[1]=-1; break;
    }
    InterlockedExchange(&menu_primary_pad_triggers,count && menu_ports[0]==ports[0]);
    menu_hud_down=0;
    menu_collect_owned_keys();
    printf("Split input: native ports P1=%d P2=%d (-1 keyboard)\n",menu_ports[0],menu_ports[1]);
}

static float menu_axis(SHORT value,int deadzone) {
    const float magnitude=(float)(abs(value)-deadzone)/(32768-deadzone);
    return magnitude<=0?0:value<0?-magnitude:magnitude;
}

static MenuControls menu_sample_controls(int seat) {
    if(menu_ports[seat]>=0) {
        XINPUT_STATE state;
        if(!menu_input_get_state || menu_input_get_state((DWORD)menu_ports[seat],&state)!=ERROR_SUCCESS) return (MenuControls){0};
        const XINPUT_GAMEPAD *pad=&state.Gamepad;
        return (MenuControls){
            .gas=fmaxf(0,pad->bRightTrigger-XINPUT_GAMEPAD_TRIGGER_THRESHOLD)/225,
            .brake=fmaxf(0,pad->bLeftTrigger-XINPUT_GAMEPAD_TRIGGER_THRESHOLD)/225,
            .steer=menu_axis(pad->sThumbLX,XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE),
            .look_x=menu_axis(pad->sThumbRX,XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE),
            .look_y=-menu_axis(pad->sThumbRY,XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE),
            .handbrake=(pad->wButtons&XINPUT_GAMEPAD_A)!=0,
            .cycle=(pad->wButtons&(XINPUT_GAMEPAD_Y|XINPUT_GAMEPAD_RIGHT_SHOULDER))!=0,
            .rear=(pad->wButtons&XINPUT_GAMEPAD_RIGHT_THUMB)!=0,
            .hud=(pad->wButtons&XINPUT_GAMEPAD_DPAD_RIGHT)!=0,
        };
    }
    return menu_sample_keyboard(seat);
}

static int menu_controls(void *lua) {
    if(!InterlockedCompareExchange(&menu_race_active,0,0)) return 0;
    DWORD foreground=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    MenuControls controls[2]={0};
    if(foreground==GetCurrentProcessId()) {
        for(int seat=0;seat<2;seat++) controls[seat]=menu_sample_controls(seat);
    }
#ifdef DC_FEEDBACK_QA
    static int hud_test=-1;
    if(hud_test<0) hud_test=getenv("DC_QA_ROUTE_HUD")!=NULL;
    if(hud_test) {
        const unsigned frame=menu_input_sequence;
        for(unsigned seat=0;seat<2;seat++) {
            const unsigned begin=240+seat*1860;
            controls[seat]=(MenuControls){.gas=frame>=begin && frame<begin+600,
                .brake=frame>=begin+600 && frame<begin+1680};
        }
    }
    static int exercise_views=-1;
    static int surface_test=-1;
    if(surface_test<0) surface_test=getenv("DC_QA_SURFACE_TEST")!=NULL;
    if(surface_test) {
        unsigned frame=menu_input_sequence;
        for(unsigned seat=0;seat<2;seat++) {
            unsigned begin=240+(1-seat)*1050;
            unsigned t=frame-begin;
            controls[seat]=(MenuControls){.gas=t<900,.steer=t>=170 && t<420?0.8f:0,
                .handbrake=t>=205 && t<260,.cycle=t>=600 && t<843 && (t-600)%80<3};
        }
    }
    if(exercise_views<0) exercise_views=getenv("DC_QA_VIEW_TEST")!=NULL;
    if(exercise_views) {
        const unsigned frame=menu_input_sequence;
        controls[0].cycle=frame>=180 && frame<=780 && (frame-180)%120<3;
        controls[1].cycle=frame>=900 && frame<=1500 && (frame-900)%120<3;
        if(frame>=260 && frame<780) controls[1].look_x=0.85f*sinf((float)frame*0.034f);
        if(frame>=850 && frame<1550) controls[1].gas=0.7f;
    }
    static int stress_cameras=-1;
    if(stress_cameras<0) stress_cameras=getenv("DC_QA_CAMERA_STRESS")!=NULL;
    if(stress_cameras) {
        unsigned frame=menu_input_sequence;
        controls[0].gas=frame>=120 && frame<3900?0.8f:0.0f;
        controls[0].cycle=frame>=180 && frame<3600 && frame%60<3;
        controls[1].cycle=frame>=1800 && frame<3000 && frame%120<3;
    }
    static int check_lighting=-1;
    if(check_lighting<0) check_lighting=getenv("DC_QA_LIGHT_DIR")!=NULL;
    if(check_lighting) {
        unsigned frame=menu_input_sequence;
        unsigned phase=frame<810?0:(frame-810)/150;
        controls[0]=(MenuControls){.look_x=(phase==5 || phase==7)?1.0f:0.0f};
        controls[1]=(MenuControls){.look_x=(phase==1 || phase==3)?1.0f:0.0f};
        static int drive_pair=-1;
        if(drive_pair<0) drive_pair=getenv("DC_QA_DRIVE_PAIR")!=NULL;
        if(drive_pair) {
            for(unsigned seat=0;seat<2;seat++) {
                controls[seat].gas=controls[seat].look_x;
                controls[seat].handbrake=controls[seat].gas==0;
            }
        }
    }
#endif
    menu_lua_number(lua,++menu_input_sequence);
    AcquireSRWLockExclusive(&camera_lock);
    camera_packet.active=1;
    camera_packet.keyboard_mask=0;
    camera_packet.rear_mask=0;
    for(int seat=0;seat<2;seat++) {
        const MenuControls *c=&controls[seat];
        const unsigned bit=1u<<seat;
        if(c->hud && !(menu_hud_down&bit)) camera_packet.hud_hidden_mask^=bit;
        if(c->hud) menu_hud_down|=bit; else menu_hud_down&=~bit;
        if(c->rear) camera_packet.rear_mask|=bit;
        if(menu_ports[seat]<0) camera_packet.keyboard_mask|=bit;
        camera_packet.gamepad_ports[seat]=menu_ports[seat];
        camera_packet.look[seat][0]=c->look_x;
        camera_packet.look[seat][1]=c->look_y;
        menu_lua_number(lua,c->gas);
        menu_lua_number(lua,c->brake);
        menu_lua_number(lua,c->steer);
        menu_lua_number(lua,c->handbrake);
        menu_lua_number(lua,c->cycle);
        menu_lua_number(lua,(camera_packet.hud_hidden_mask&bit)!=0);
    }
    ReleaseSRWLockExclusive(&camera_lock);
    return 13;
}

static void menu_set_actors(int first,int second) {
    if(first<=0 || second<=0 || first==second) return;
    InterlockedExchange(&env_stopped,0);
    AcquireSRWLockExclusive(&camera_lock);
    results_event_actors[0]=first;
    results_event_actors[1]=second;
    camera_packet.active=1;
    camera_packet.actors[0]=camera_race_actors[0]=first;
    camera_packet.actors[1]=camera_race_actors[1]=second;
    camera_fresh=GetTickCount64();
    ReleaseSRWLockExclusive(&camera_lock);
}
