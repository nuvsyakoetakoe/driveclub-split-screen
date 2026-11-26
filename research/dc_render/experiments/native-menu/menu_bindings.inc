/* SDL scancodes keep a binding on the same physical key across Windows layouts. */
enum MenuAction { Gas, Brake, Left, Right, Handbrake, LookLeft, LookRight,
    LookUp, LookDown, Camera, Rear, Hud, AltCamera, MenuActionCount };
static const int menu_presets[2][MenuActionCount]={
    {26,22,4,7,44,13,15,12,14,6,25,79,8},
    {82,81,80,79,228,92,94,96,90,98,99,95,0}
};
static int menu_custom[2][MenuActionCount];
static volatile LONG menu_capture_active;
static volatile LONG menu_driving_keys;
static int menu_owned_keys[2 * MenuActionCount],menu_owned_count;
static int menu_capture_ready,menu_pending_key;
static int menu_qa_keyboard_install(void);
static const unsigned char *menu_binding_keyboard(int *length);

static int menu_binding(int seat,int action) {
    int shared=menu_ports[0]<0 && menu_ports[1]<0;
    int arrows=menu_keyboard_layout^(shared && seat);
    int key=menu_custom[seat][action];
    if(key) return key>0?key:0;
    if(!arrows && shared && action==Hud) return 11; /* H */
    return menu_presets[arrows][action];
}

static const char *menu_key_name(int key) {
    typedef const char *(*Name)(int);
    static Name name;
    if(!name) name=(Name)GetProcAddress(GetModuleHandleW(L"SDL3.dll"),"SDL_GetScancodeName");
    if(!key) return "—";
    const char *s=name?name(key):NULL;
    return s && *s?s:"?";
}

static void menu_collect_owned_keys(void) {
    menu_owned_count=0;
    for(int seat=0;seat<2;seat++) if(menu_ports[seat]<0) {
        for(int action=0;action<MenuActionCount;action++) {
            int code=menu_binding(seat,action);
            if(code>0) menu_owned_keys[menu_owned_count++]=code;
        }
    }
}

static int menu_capture_poll(void) {
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    if(pid!=GetCurrentProcessId()) return 0;
    int length=0,pressed=0;
    const unsigned char *keys=menu_binding_keyboard(&length);
    if(!keys) return 0;
    for(int k=4;k<length && k<512;k++) if(keys[k]) { pressed=k; break; }
    if(!pressed) {
        if(menu_pending_key) {
            int result=menu_pending_key;
            menu_pending_key=0;
            InterlockedExchange(&menu_capture_active,0);
            return result==41?-1:result;
        }
        menu_capture_ready=1;
    } else if(menu_capture_ready && !menu_pending_key) menu_pending_key=pressed;
    return 0;
}

static MenuControls menu_sample_keyboard(int seat) {
    int length=0;
    const unsigned char *keys=menu_original_keyboard?menu_binding_keyboard(&length):NULL;
    int down[MenuActionCount]={0};
    if(keys) for(int i=0;i<MenuActionCount;i++) {
        int code=menu_binding(seat,i);
        down[i]=code>0 && code<length && keys[code];
    }
    return (MenuControls){.gas=down[Gas],.brake=down[Brake],.steer=down[Right]-down[Left],
        .handbrake=down[Handbrake],.look_x=down[LookRight]-down[LookLeft],
        .look_y=down[LookDown]-down[LookUp],.cycle=down[Camera] || down[AltCamera],
        .rear=down[Rear],.hud=down[Hud]};
}
