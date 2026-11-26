/* The retail results row has an actor ID but resolves both local seats to the
 * active profile name. Use the seat names assigned when the racers were spawned. */
typedef __attribute__((sysv_abi)) void (*ResultsRowFn)(unsigned char *);
typedef __attribute__((sysv_abi)) void *(*ResultsPanelFn)(void *);
typedef __attribute__((sysv_abi)) void *(*ResultsFindFn)(void *,const char *,int,int);
typedef __attribute__((sysv_abi)) void (*ResultsTextFn)(void *,const char *);
static ResultsRowFn results_original_row;
#ifdef DC_NATIVE_MENU
/* Results rows outlive the render scene. Only keep integer IDs here. */
static int results_event_actors[2];
#else
#define results_event_actors camera_race_actors
#endif

static __attribute__((sysv_abi)) void results_row(unsigned char *row) {
    results_original_row(row);
    const int actor=*(const int *)(row+0x238);
    if(actor<=0) return;

    int seat=-1;
    AcquireSRWLockShared(&camera_lock);
    if(results_event_actors[0]==actor) seat=0;
    if(results_event_actors[1]==actor) seat=1;
    ReleaseSRWLockShared(&camera_lock);
    if(seat<0) return;

    void *panel=((ResultsPanelFn)(*(uintptr_t **)row)[0x28/8])(row);
    void *text=((ResultsFindFn)0x80026e720)(panel,"text_driver_name",0,1);
    if(text) {
        const wchar_t *variable=seat==0?L"DC_PLAYER_NAME_1":L"DC_PLAYER_NAME_2";
        wchar_t wide_name[64];
        char name[256];
        const DWORD length=GetEnvironmentVariableW(variable,wide_name,sizeof(wide_name)/sizeof(wide_name[0]));
        if(!length || length>=sizeof(wide_name)/sizeof(wide_name[0]) ||
           !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide_name,-1,name,sizeof(name),NULL,NULL))
            snprintf(name,sizeof(name),"Player %d",seat+1);
        ((ResultsTextFn)0x8002718d0)(text,name);
        static LONG reported;
        if(!(InterlockedOr(&reported,1L<<seat)&(1L<<seat)))
            printf("Split results: %s actor=%d\n",name,actor);
    }
}

static void results_install(void) {
    if(results_original_row || camera_installed!=1) return;
    const uintptr_t address=0x800f90120;
    static const unsigned char head[]={0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x89,0xfb,0x83,0xbb,0x38,0x02,0,0,0};
    if(!memcmp((const void *)address,head,sizeof(head)))
        results_original_row=(ResultsRowFn)camera_detour(address,(void *)results_row,sizeof(head),0);
}
