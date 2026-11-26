/* UI bridge for the native setup pages in the checked 1.28 guest image. */
typedef __attribute__((sysv_abi)) void (*MenuTextDrawFn)(unsigned char *,void *,void *,void *);
typedef __attribute__((sysv_abi)) void (*MenuTextSetFn)(void *,const char *);
typedef __attribute__((sysv_abi)) void (*MenuLuaBooleanFn)(void *,int);
typedef __attribute__((sysv_abi)) void (*MenuLuaPushStringFn)(void *,const char *);

static struct {
    const char *key;
    uint32_t lower_hash,upper_hash;
    char value[256];
} menu_fields[]={
    {"track",0xa85833f3,0x5ced9700,""},{"car1",0x07a2d5a5,0x5ddd575b,""},
    {"car2",0x9eab841f,0xc4d406e1,""},{"laps",0xf52587cd,0x943425fb,""},
    {"tod",0xb345e139,0xc91804c7,""},{"weather",0x87aa1ffc,0xc76a3aac,""},
    {"name1",0x145aa2a9,0xdb812692,""},{"name2",0x8d53f313,0x42887728,""},
    {"devices",0xda7d8208,0x9abda758,""},{"layout",0x21457552,0x3e6b4398,""},
    {"keyboard",0x9dbe0831,0xcdfbb928,""},
    {"binding0",0xa146d5f5,0xca6d4424,""},
    {"binding1",0xd641e563,0xbd6a74b2,""},
    {"binding2",0x4f48b4d9,0x24632508,""},
    {"binding3",0x384f844f,0x5364159e,""},
    {"binding4",0xa62b11ec,0xcd00803d,""},
    {"binding5",0xd12c217a,0xba07b0ab,""},
    {"binding6",0x482570c0,0x230ee111,""},
    {"binding7",0x3f224056,0x5409d187,""},
    {"binding8",0xaf9d5dc7,0xc4b6cc16,""},
    {"binding9",0xd89a6d51,0xb3b1fc80,""},
    {"binding10",0x20b6ae26,0xd10967f5,""},
    {"binding11",0x57b19eb0,0xa60e5763,""},
    {"binding12",0xceb8cf0a,0x3f0706d9,""},
    {"capture",0x40846a77,0x00444f27,""},
    {"keyswho1",0xbe2f3b9b,0xd504aa4a,""},
    {"keyswho2",0x27266a21,0x4c0dfbf0,""}
};
static SRWLOCK menu_fields_lock=SRWLOCK_INIT;
static MenuTextDrawFn menu_original_text_draw;
#ifdef DC_NATIVE_MENU
static MenuTextDrawFn menu_original_panel_draw;
static _Thread_local int menu_header_depth;
static int menu_wizard_page;
static int menu_language_ru(void) {
    char id[8];
    return GetEnvironmentVariableA("BB_LANGUAGE",id,sizeof(id))==1 && id[0]=='8';
}

static __attribute__((sysv_abi)) void menu_panel_draw(unsigned char *panel,void *a,void *b,void *c) {
    const uint32_t name=*(const uint32_t *)(panel+0x50);
    if(InterlockedCompareExchange(&menu_race_active,0,0) &&
       (name==0xd6c09326 || name==0xad1c0c7d || /* osd_speedometer */
        name==0x547b1147 || name==0x9d58bf4c || /* osd_wrong_way */
        name==0xbc5f048f || name==0xd1ebf165 || /* osd_percentage */
        name==0x333b43ee || name==0xf49737c5 || /* penalties */
        name==0xfc620e3f || name==0x91d6fbd5 || /* time gaps */
        name==0x296c2971 || name==0x47d12d72)) { /* osd_lap_count */
#ifdef DC_FEEDBACK_QA
        static LONG reported;
        if(!InterlockedExchange(&reported,1)) puts("Split HUD: suppressed shared race panels");
#endif
        return;
    }
    int header=name==0xb892038c || name==0x2a0c4faa; /* header_title */
    menu_header_depth+=header;
    menu_original_panel_draw(panel,a,b,c);
    menu_header_depth-=header;
}
#endif
static const MenuTextSetFn menu_set_text=(MenuTextSetFn)0x8002718d0;
static const MenuLuaBooleanFn menu_lua_boolean=(MenuLuaBooleanFn)0x8009eab30;
static const MenuLuaPushStringFn menu_lua_push_string=(MenuLuaPushStringFn)0x8009ea8d0;
#ifdef DC_NATIVE_MENU
#include "menu_input.inc"
#include "menu_freeplay.inc"
#include "menu_profile.inc"

static int menu_adjust_viewports(int split) {
    static const struct {
        uintptr_t address;
        size_t length;
        unsigned char original[4], local[4];
    } patches[]={
        /* Use the compositor's no-heat-distortion path for split races.
         * Its full-frame distortion texture otherwise warps the lower view. */
        {0x8007024e1,2,{0x73,0x20},{0xeb,0x20}},
        {0x8013669a4,4,{0x9a,0x99,0x19,0x3e},{0,0,0,0}},
        {0x800df55b8,4,{0x50,0x13,0x57,0},{0x4c,0x13,0x57,0}},
        {0x800df5659,4,{0xaf,0x12,0x57,0},{0xab,0x12,0x57,0}},
        {0x800476b50,3,{0x48,0x8d,0x05},{0x31,0xc0,0xc3}}, // HUD shake
        /* This widget animates Collision Penalty and Corner Penalty independently
         * of OSDPage.Hide. Its update only changes presentation. */
        {0x8004a43b0,1,{0x55},{0xc3}},
        /* The engine command above these instructions is per vehicle. The
         * listener gain and exciter immediately following it are shared. */
        {0x800d67b7b,4,{0x61,0x91,0x5f,0},{0x71,0x91,0x5f,0}},
        {0x800d67bae,3,{0x0f,0xb6,0xfb},{0x31,0xff,0x90}},
        {0x800ca550d,2,{0xc5,0xfa},{0xeb,0x11}}, // Mix_Racing.FrontCamera
        /* Sample once per frame on view zero, including two-view races. */
        {0x800ca53b0,4,{0x41,0x83,0xff,0x01},{0x41,0x83,0xfc,0}},
    };
    const size_t count=sizeof(patches)/sizeof(*patches);
    DWORD protections[sizeof(patches)/sizeof(*patches)];
    for(size_t i=0;i<count;i++) {
        const void *address=(const void *)patches[i].address;
        const unsigned char *expected=split?patches[i].original:patches[i].local;
        if(memcmp(address,expected,patches[i].length)) return 0;
    }
    size_t writable=0;
    for(;writable<count;writable++) {
        if(!VirtualProtect((void *)patches[writable].address,patches[writable].length,
                           PAGE_EXECUTE_READWRITE,&protections[writable])) break;
    }
    if(writable==count) for(size_t i=0;i<count;i++) {
        void *address=(void *)patches[i].address;
        memcpy(address,split?patches[i].local:patches[i].original,patches[i].length);
        FlushInstructionCache(GetCurrentProcess(),address,patches[i].length);
    }
    const int success=writable==count;
    while(writable) {
        const size_t i=--writable;
        DWORD ignored;
        VirtualProtect((void *)patches[i].address,patches[i].length,protections[i],&ignored);
    }
    return success;
}
#endif

static __attribute__((sysv_abi)) void menu_text_draw(unsigned char *text,void *a,void *b,void *c) {
    const uint32_t hash=*(const uint32_t *)(text+0x50);
#ifdef DC_NATIVE_MENU
    if(menu_flow_active && (hash==0x2b36786b || hash==0xdc62badb || hash==0x5dbde457 || hash==0xcf23a871)) {
        const char *current=*(const size_t *)(text+0x98)<16?(const char *)text+0x80:*(const char *const *)(text+0x80);
        if(current && (!strcmp(current,"DC_GARAGE_1") || !strcmp(current,"DC_GARAGE_2"))) {
            char title[256];
            AcquireSRWLockShared(&menu_fields_lock);
            snprintf(title,sizeof(title),menu_language_ru()?"Выбор автомобиля — %s":"Select car — %s",menu_fields[current[10]=='1'?6:7].value);
            ReleaseSRWLockShared(&menu_fields_lock);
            menu_set_text(text,title);
        } else if(current && menu_wizard_page && strcmp(current,"Split Screen")) {
            wchar_t localized[64];
            if(menu_header_depth || (MultiByteToWideChar(CP_UTF8,0,current,-1,localized,64) &&
               (CompareStringOrdinal(localized,-1,L"Отдельный заезд",-1,TRUE)==CSTR_EQUAL ||
                !_stricmp(current,"Free Play") || !_stricmp(current,"Single Event"))))
                menu_set_text(text,"Split Screen");
        }
    }
#endif
    for(size_t i=0;i<sizeof(menu_fields)/sizeof(*menu_fields);i++) {
        if(hash!=menu_fields[i].lower_hash && hash!=menu_fields[i].upper_hash) continue;
        char value[sizeof(menu_fields[i].value)];
        AcquireSRWLockShared(&menu_fields_lock);
        memcpy(value,menu_fields[i].value,sizeof(value));
        ReleaseSRWLockShared(&menu_fields_lock);
        /* Read the inline or allocated string used by the guest text widget. */
        const char *current=*(const size_t *)(text+0x98)<16?
            (const char *)text+0x80:*(const char *const *)(text+0x80);
        if(value[0] && strcmp(value,current)) menu_set_text(text,value);
        break;
    }
    menu_original_text_draw(text,a,b,c);
}

typedef struct {
    int32_t user;
    uint32_t type;
    uint64_t languages;
    uint32_t enter_label,input_method;
    void *filter;
    uint32_t option,max_length;
    uint16_t *buffer;
    float x,y;
    uint32_t halign,valign;
    const uint16_t *placeholder,*title;
    int8_t reserved[16];
} MenuImeParam;
typedef __attribute__((sysv_abi)) int32_t (*MenuImeInitFn)(const MenuImeParam *,const void *);
typedef __attribute__((sysv_abi)) int32_t (*MenuImeStateFn)(void);
typedef __attribute__((sysv_abi)) int32_t (*MenuImeResultFn)(uint32_t *);
static uint16_t menu_ime_buffer[97];
static int menu_ime_open;

static int menu_ime_begin(const char *title,const char *initial) {
    const MenuImeInitFn init=*(MenuImeInitFn const *)0x802e740a0;
    if(!init || menu_ime_open) return 0;
    uint16_t wide_title[128];
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,title,-1,(wchar_t *)wide_title,128) ||
       !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,initial,-1,(wchar_t *)menu_ime_buffer,97)) return 0;
    const MenuImeParam param={.max_length=96,.buffer=menu_ime_buffer,.title=wide_title};
    menu_ime_open=init(&param,NULL)==0;
    return menu_ime_open;
}

static int menu_ime_poll(void *lua) {
    const MenuImeStateFn status=*(MenuImeStateFn const *)0x802e74088;
    const MenuImeResultFn result=*(MenuImeResultFn const *)0x802e740a8;
    const MenuImeStateFn term=*(MenuImeStateFn const *)0x802e740b0;
    if(!menu_ime_open || !status || !result || !term) return 0;
    const int state=status();
    if(state==1) return 0;
    uint32_t outcome=1;
    const int accepted=state==2 && !result(&outcome) && !outcome;
    term();
    menu_ime_open=0;
    char value[385];
    if(accepted && WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,(wchar_t *)menu_ime_buffer,-1,
                                       value,sizeof(value),NULL,NULL)) menu_lua_push_string(lua,value);
    else menu_lua_boolean(lua,0);
    return 1;
}

static int menu_set_rain(const char *preset) {
    if(strcmp(preset,"Light Precipitation") && strcmp(preset,"Mid Precipitation") &&
       strcmp(preset,"Heavy Precipitation")) return 0;
    const unsigned char *game=*(const unsigned char *const *)0x801911100;
    const unsigned char *mode=game?*(const unsigned char *const *)(game+0x970):NULL;
    unsigned char *base=mode?*(unsigned char *const *)(mode+0xf8):NULL;
    if(!base) return 0;
    unsigned char *config=base+(base[0x780]==0?0x3b8:8);
    typedef __attribute__((sysv_abi)) void *(*MenuAssignFn)(void *,const char *);
    ((MenuAssignFn)0x800e978b0)(config+0x230,preset);
    return 1;
}

static void menu_clear_scene(int leaving_event) {
#ifdef DC_NATIVE_MENU
    InterlockedIncrement(&menu_scene_generation);
    InterlockedExchangePointer(&menu_reconstructed_view,NULL);
#endif
    /* The out-of-race preview can outlive a gameplay world and reuse its
     * allocations. Drop the non-owning camera and model indices on exit. */
    AcquireSRWLockExclusive(&camera_lock);
    memset(&camera_packet,0,sizeof(camera_packet));
    if(leaving_event) memset(results_event_actors,0,sizeof(results_event_actors));
    memset(camera_race_actors,0,sizeof(camera_race_actors));
    memset(camera_native_input,0,sizeof(camera_native_input));
    ReleaseSRWLockExclusive(&camera_lock);
    AcquireSRWLockExclusive(&visibility_snapshot_lock);
    memset(visibility_ranges,0,sizeof(visibility_ranges));
    memset(visibility_matrices,0,sizeof(visibility_matrices));
    ReleaseSRWLockExclusive(&visibility_snapshot_lock);
    AcquireSRWLockExclusive(&stencil_lock);
    memset(stencil_collectors,0,sizeof(stencil_collectors));
    ReleaseSRWLockExclusive(&stencil_lock);
    memset(visibility_cabin,0,sizeof(visibility_cabin));
    memset(visibility_views,0,sizeof(visibility_views));
    memset(visibility_character_models,0,sizeof(visibility_character_models));
    memset(visibility_full_models,0,sizeof(visibility_full_models));
    memset(visibility_character_hidden,0,sizeof(visibility_character_hidden));
    memset(visibility_parts,0,sizeof(visibility_parts));
    memset(visibility_queries,0,sizeof(visibility_queries));
    memset(visibility_frusta,0,sizeof(visibility_frusta));
    AcquireSRWLockExclusive(&split_audio_lock);
    memset(&split_audio_pose,0,sizeof(split_audio_pose));
    memset(&split_audio_engine_mix,0,sizeof(split_audio_engine_mix));
    ReleaseSRWLockExclusive(&split_audio_lock);
    InterlockedExchangePointer(&split_engine_p2_event,NULL);
    for(int seat=0;seat<2;seat++) InterlockedExchangePointer(&split_engine_requests[seat],NULL);
#ifdef DC_NATIVE_MENU
    cabin_clear();
#endif
}

static int menu_settings_command(void *lua,const char *command,MenuLuaStringFn get_string) {
#ifdef DC_NATIVE_MENU
    if(!strcmp(command,"russian")) {
        menu_lua_boolean(lua,menu_language_ru());
        return 1;
    }
    if(!strcmp(command,"freeplay")) {
        unsigned char *flow=menu_freeplay();
        menu_flow_active=flow && menu_flow_install();
        menu_garage_seat=menu_grid_armed=menu_return_to_split=0;
        menu_first_garage=NULL;
        if(menu_flow_active) {
            typedef __attribute__((sysv_abi)) void (*SetEvent)(void *,int);
            ((SetEvent)0x800190d60)(flow,0);
        }
        menu_lua_boolean(lua,menu_flow_active);
        return 1;
    }
    if(!strcmp(command,"return_route")) {
        menu_lua_boolean(lua,menu_return_to_split);
        menu_return_to_split=0;
        return 1;
    }
    if(!strcmp(command,"wizard_heading")) {
        const char *flag=get_string(lua,2,NULL);
        menu_wizard_page=flag && !strcmp(flag,"1");
        const char *page=get_string(lua,3,NULL);
        InterlockedExchange(&menu_driving_keys,menu_race_active && page && (!strcmp(page,"osd.main") || !strcmp(page,"x_prerace.countdown")));
        return 0;
    }
    if(!strcmp(command,"garage")) {
        const char *seat=get_string(lua,2,NULL);
        menu_garage_seat=seat?atoi(seat):0;
        return 0;
    }
    if(!strcmp(command,"launch_event")) {
        unsigned char *flow=menu_freeplay();
        int ready=menu_grid_armed && flow;
        if(ready) {
            typedef __attribute__((sysv_abi)) void (*LaunchEvent)(void *);
            menu_return_to_split=1;
            ((LaunchEvent)0x8001920e0)(flow);
        }
        menu_lua_boolean(lua,ready);
        return 1;
    }
    if(!strcmp(command,"exit_setup")) {
        menu_flow_active=menu_garage_seat=0;
        return 0;
    }
#endif
    if(!strcmp(command,"poll")) return menu_ime_poll(lua);
    if(!strcmp(command,"drop_actors")) {
#ifdef DC_NATIVE_MENU
        if(menu_race_active && visibility_active()) env_stop_race();
#endif
        menu_clear_scene(0);
        return 0;
    }
    if(!strcmp(command,"clear_scene")) {
#ifdef DC_NATIVE_MENU
        if(menu_race_active && visibility_active()) env_stop_race();
        menu_grid_armed=0;
        if(InterlockedExchange(&menu_race_active,0) && !menu_adjust_viewports(0))
            fputs("Split menu: could not restore the single-player viewports\n",stderr);
#endif
        menu_clear_scene(1);
        return 0;
    }
    if(!strcmp(command,"start_mode")) {
        int ready=1;
#ifdef DC_NATIVE_MENU
        ready=camera_installed==1 &&
            (InterlockedCompareExchange(&menu_race_active,0,0) || menu_adjust_viewports(1));
        if(ready) {
            InterlockedExchange(&env_stopped,0);
            menu_grid_armed=menu_flow_active;
            menu_input_begin();
            InterlockedExchange(&menu_race_active,1);
        }
#endif
        menu_lua_boolean(lua,ready);
        return 1;
    }
    if(!strcmp(command,"native_input")) {
#ifdef DC_NATIVE_MENU
        menu_lua_boolean(lua,1);
#else
        menu_lua_boolean(lua,0);
#endif
        return 1;
    }
#ifdef DC_NATIVE_MENU
#ifdef DC_FEEDBACK_QA
    if(!strcmp(command,"ai_test")) {
        menu_lua_boolean(lua,getenv("DC_QA_FINISH_RACE")!=NULL);
        return 1;
    }
#endif
    if(!strcmp(command,"controls")) return menu_controls(lua);
#endif
    const char *arg=get_string(lua,2,NULL);
    const char *value=get_string(lua,3,NULL);
    if(!strcmp(command,"rain")) {
        menu_lua_boolean(lua,arg && menu_set_rain(arg));
        return 1;
    }
#ifdef DC_NATIVE_MENU
    if(!strcmp(command,"read")) return menu_profile_value(lua,arg,NULL);
    if(!strcmp(command,"save") && value) return menu_profile_value(lua,arg,value);
    if(!strcmp(command,"capture_begin")) {
        menu_pending_key=menu_capture_ready=0;
        int ok=menu_qa_keyboard_install();
        InterlockedExchange(&menu_capture_active,ok);
        menu_lua_boolean(lua,ok);
        return 1;
    }
    if(!strcmp(command,"capture_end")) {
        InterlockedExchange(&menu_capture_active,0);
        return 0;
    }
    if(!strcmp(command,"capture_poll")) {
        if(!menu_capture_active) return 0;
        int key=menu_capture_poll();
        if(!key) return 0;
        menu_lua_number(lua,key);
        return 1;
    }
    if(arg && value && (!strcmp(command,"key_label") || !strcmp(command,"effective_key") || !strcmp(command,"key_set"))) {
        int seat=atoi(arg)-1,action=atoi(value);
        if(seat<0 || seat>1 || action<0 || action>=MenuActionCount) return 0;
        if(!strcmp(command,"key_set")) {
            const char *v=get_string(lua,4,NULL);
            int key=v?atoi(v):0;
            if(key>=-1 && key<512) menu_custom[seat][action]=key;
            return 0;
        }
        if(!strcmp(command,"key_label")) menu_lua_push_string(lua,menu_key_name(menu_binding(seat,action)));
        else menu_lua_number(lua,menu_binding(seat,action));
        return 1;
    }
#endif
    if(!arg || !value) return 0;
#ifdef DC_NATIVE_MENU
    if(!strcmp(command,"bindings")) {
        int devices=atoi(arg),layout=atoi(value);
        if(devices>=0 && devices<=4 && layout>=0 && layout<=1) {
            menu_device_choice=devices;
            menu_keyboard_layout=layout;
            menu_input_begin();
        }
        return 0;
    }
    if(!strcmp(command,"actors")) {
        menu_set_actors(atoi(arg),atoi(value));
        return 0;
    }
#endif
    if(!strcmp(command,"open")) {
        menu_lua_boolean(lua,menu_ime_begin(arg,value));
        return 1;
    }
    if(!strcmp(command,"label")) {
        for(size_t i=0;i<sizeof(menu_fields)/sizeof(*menu_fields);i++) {
            if(strcmp(arg,menu_fields[i].key)) continue;
            AcquireSRWLockExclusive(&menu_fields_lock);
            snprintf(menu_fields[i].value,sizeof(menu_fields[i].value),"%s",value);
            ReleaseSRWLockExclusive(&menu_fields_lock);
            break;
        }
    } else if(!strcmp(command,"name")) {
        const wchar_t *variable=!strcmp(arg,"name1")?L"DC_PLAYER_NAME_1":
            !strcmp(arg,"name2")?L"DC_PLAYER_NAME_2":NULL;
        wchar_t name[64];
        if(variable && MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value,-1,name,64))
            SetEnvironmentVariableW(variable,name);
    }
    return 0;
}

static void menu_settings_install(void) {
    if(menu_original_text_draw) return;
    const uintptr_t address=0x800272440;
    const unsigned char head[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53};
    if(memcmp((const void *)address,head,sizeof(head))) return;
    menu_original_text_draw=(MenuTextDrawFn)camera_detour(address,(void *)menu_text_draw,sizeof(head),0);
#ifdef DC_NATIVE_MENU
    const uintptr_t panel_draw=0x80026ef60;
    if(!memcmp((const void *)panel_draw,head,sizeof(head)))
        menu_original_panel_draw=(MenuTextDrawFn)camera_detour(panel_draw,(void *)menu_panel_draw,sizeof(head),0);
#endif
}
