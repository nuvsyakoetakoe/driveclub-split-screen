typedef const unsigned char *(*MenuKeyboardFn)(int *);
static MenuKeyboardFn menu_original_keyboard;
typedef __attribute__((sysv_abi)) int (*MenuLuaPrintFn)(void *);
typedef __attribute__((sysv_abi)) const char *(*MenuLuaStringFn)(void *,int,size_t *);
#include "menu_settings.inc"
typedef __attribute__((sysv_abi)) void (*MenuLuaErrorFn)(
    void *,const char *,int,const char *,const char *,...);
static MenuLuaPrintFn menu_original_print;
static MenuLuaErrorFn menu_original_error;

static __attribute__((sysv_abi)) int menu_status_print(void *lua) {
    const MenuLuaStringFn get_string=(MenuLuaStringFn)0x8009ea050;
    const char *message=get_string(lua,1,NULL);
    if(message && !strncmp(message,"DCSplitMenu.",12))
        return menu_settings_command(lua,message+12,get_string);
    if(message && !strncmp(message,"SplitMenu: ",11)) {
#ifdef DC_NATIVE_MENU
        if(!strcmp(message+11,"menu hook ready")) InterlockedExchange(&menu_frontend_ready,1);
#endif
        puts(message);
        fflush(stdout);
        return 0;
    }
    return menu_original_print(lua);
}

static __attribute__((sysv_abi)) void menu_lua_error(
    void *report,const char *source,int line,const char *function,const char *format,uintptr_t detail) {
    if(!strcmp(format,"%s") && detail) {
        fprintf(stderr,"SplitMenu Lua error %s:%d (%s): %.600s\n",source,line,function,(const char *)detail);
    } else {
        fprintf(stderr,"SplitMenu Lua error %s:%d (%s): code %d\n",source,line,function,(int)detail);
    }
    menu_original_error(report,source,line,function,format,detail);
}

static void menu_status_install(void) {
    if(menu_original_print || !camera_code) return;
    const uintptr_t address=0x8007a6d30;
    const unsigned char prologue[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x50,0x49,0x89,0xff};
    if(memcmp((const void *)address,prologue,sizeof(prologue))) return;
    menu_original_print=(MenuLuaPrintFn)camera_detour(address,(void *)menu_status_print,sizeof(prologue),0);
    menu_settings_install();
    if(menu_original_print) puts("Menu QA: status receiver installed");
    const uintptr_t error_address=0x801075fd0;
    const unsigned char error_prologue[]={0x55,0x48,0x89,0xe5,0x53,0x48,0x81,0xec,0xd8,0x00,0x00,0x00};
    if(!memcmp((const void *)error_address,error_prologue,sizeof(error_prologue)))
        menu_original_error=(MenuLuaErrorFn)camera_detour(error_address,(void *)menu_lua_error,sizeof(error_prologue),0);
}
static volatile LONG menu_key;
static _Thread_local unsigned char menu_keys[1024];
static const unsigned char *menu_binding_keyboard(int *count) {
    int length=0;
    const unsigned char *keys=menu_original_keyboard(&length);
    if(count) *count=length;
    const LONG key=InterlockedCompareExchange(&menu_key,0,0);
    if(!keys || key<=0 || key>=length || length>(int)sizeof(menu_keys)) return keys;
    memcpy(menu_keys,keys,length);
    menu_keys[key]=1;
    return menu_keys;
}
static const unsigned char *menu_qa_keyboard(int *count) {
    int length=0;
    const unsigned char *keys=menu_binding_keyboard(&length);
    if(count) *count=length;
#ifdef DC_NATIVE_MENU
    static const unsigned char blank[1024]={0};
    if(InterlockedCompareExchange(&menu_capture_active,0,0)) return blank;
    /* Race actions are delivered by menu_controls. Stop retail shortcuts
     * from also consuming a bound key (for example T opens Photo Mode). */
    if(keys && length>0 && length<=(int)sizeof(menu_keys) &&
       InterlockedCompareExchange(&menu_driving_keys,0,0)) {
        if(keys!=menu_keys) memcpy(menu_keys,keys,length);
        /* E also drives the retail global camera when unassigned in the split profile. */
        if(length>8) menu_keys[8]=0;
        for(int i=0;i<menu_owned_count;i++)
            if(menu_owned_keys[i]<length) menu_keys[menu_owned_keys[i]]=0;
        return menu_keys;
    }
#endif
    return keys;
}
#ifdef DC_NATIVE_MENU
typedef SHORT (*MenuSdlAxisFn)(void *,int);
typedef bool (*MenuSdlButtonFn)(void *,int);
static MenuSdlAxisFn menu_original_axis;
static MenuSdlButtonFn menu_original_button;

/* The port maps its first physical SDL pad to the primary retail driver.
 * In the keyboard/P2-pad layout XInput already supplies these actions to P2. */
static int menu_pad_owned_by_second(void) {
    return menu_race_active && menu_driving_keys && menu_ports[0]<0 && menu_ports[1]>=0;
}
static SHORT menu_retail_axis(void *pad,int axis) {
    if(menu_pad_owned_by_second()) return 0;
    return menu_original_axis(pad,axis);
}
static bool menu_retail_button(void *pad,int button) {
    /* SDL gamepad South is the split driver's handbrake. */
    if(button==0 && menu_pad_owned_by_second()) return false;
    return menu_original_button(pad,button);
}
#endif
static int menu_qa_keyboard_install(void) {
    unsigned char *base=(void *)GetModuleHandleW(NULL);
    const IMAGE_DOS_HEADER *dos=(void *)base;
    const IMAGE_NT_HEADERS *nt=(void *)(base+dos->e_lfanew);
    const DWORD offset=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if(!offset) return 0;
    for(const IMAGE_IMPORT_DESCRIPTOR *dll=(void *)(base+offset);dll->Name;dll++) {
        if(_stricmp((char *)base+dll->Name,"SDL3.dll") || !dll->OriginalFirstThunk) continue;
        const IMAGE_THUNK_DATA *lookup=(void *)(base+dll->OriginalFirstThunk);
        IMAGE_THUNK_DATA *entry=(void *)(base+dll->FirstThunk);
        for(;lookup->u1.AddressOfData;entry++,lookup++) {
            if(IMAGE_SNAP_BY_ORDINAL(lookup->u1.Ordinal)) continue;
            const IMAGE_IMPORT_BY_NAME *symbol=(void *)(base+lookup->u1.AddressOfData);
            const char *name=(const char *)symbol->Name;
            void **original=NULL,*replacement=NULL;
            if(!strcmp(name,"SDL_GetKeyboardState")) {
                original=(void **)&menu_original_keyboard;
                replacement=(void *)menu_qa_keyboard;
            }
#ifdef DC_NATIVE_MENU
            else if(!strcmp(name,"SDL_GetGamepadAxis")) {
                original=(void **)&menu_original_axis;
                replacement=(void *)menu_retail_axis;
            } else if(!strcmp(name,"SDL_GetGamepadButton")) {
                original=(void **)&menu_original_button;
                replacement=(void *)menu_retail_button;
            }
#endif
            if(!original || *original) continue;
            DWORD old,ignored;
            if(!VirtualProtect(entry,sizeof(*entry),PAGE_READWRITE,&old)) return 0;
            *original=(void *)entry->u1.Function;
            InterlockedExchangePointer((void *volatile *)&entry->u1.Function,replacement);
            VirtualProtect(entry,sizeof(*entry),old,&ignored);
        }
    }
    return menu_original_keyboard!=NULL;
}
static void menu_qa_keyboard_poll(void) {
    static int installed;
    static unsigned last_sequence;
    static ULONGLONG until,next;
    const ULONGLONG now=GetTickCount64();
    if(!installed) {
        installed=menu_qa_keyboard_install()?1:-1;
        printf("Menu QA: keyboard adapter %d\n",installed);
    }
    if(installed!=1) return;
    if(until && now>=until) {
        InterlockedExchange(&menu_key,0);
        until=0;
    }
    if(now<next || until) return;
    next=now+50;
    FILE *stream=fopen("menu-key.txt","r");
    if(!stream) return;
    unsigned sequence=0,scancode=0;
    const int ok=fscanf(stream,"%u %u",&sequence,&scancode);
    fclose(stream);
    if(ok!=2 || sequence<=last_sequence || scancode==0 || scancode>=256) return;
    last_sequence=sequence;
    until=now+170;
    InterlockedExchange(&menu_key,scancode);
    printf("Menu QA: key %u sequence %u\n",scancode,sequence);
}

static BOOL CALLBACK menu_test_window(HWND window,LPARAM param) {
    DWORD pid=0;
    GetWindowThreadProcessId(window,&pid);
    if(pid!=GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window,GW_OWNER)) return TRUE;
    RECT area;
    GetClientRect(window,&area);
    if(area.right<640 || area.bottom<360) return TRUE;
    SetWindowTextW(window,L"DRIVECLUB - Split Screen menu test");
    if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
    SetForegroundWindow(window);
    *(int *)param=1;
    return FALSE;
}

static void menu_qa_poll(void) {
    menu_status_install();
#ifdef DC_NATIVE_MENU
    if(!getenv("DC_MENU_QA_KEYS")) return;
#endif
    static int named_window;
    if(!named_window) EnumWindows(menu_test_window,(LPARAM)&named_window);
    menu_qa_keyboard_poll();
}

