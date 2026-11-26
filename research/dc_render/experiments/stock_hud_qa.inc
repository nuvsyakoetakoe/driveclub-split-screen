/* Trace the native OSD layout loader and its per-panel objects. */
typedef __attribute__((sysv_abi)) void *(*HudQAFactory)(const char *,const char *,const char *);
static HudQAFactory hud_qa_factory_original;
typedef __attribute__((sysv_abi)) void (*MapQALoad)(void *);
typedef __attribute__((sysv_abi)) void (*MapQARect)(void *,float,float,float,float);
typedef __attribute__((sysv_abi)) void *(*MapQAActor)(void *,int);
typedef __attribute__((sysv_abi)) int (*MapQAActorId)(void *);
typedef __attribute__((sysv_abi)) unsigned char (*MapQAPosition)(void *,int);
typedef __attribute__((sysv_abi)) void (*MapQADraw)(void *,int,unsigned char);
static MapQALoad map_qa_load_original, map_qa_update_original;
static MapQARect map_qa_rect_original;
static int map_qa_rect_events;

static __attribute__((sysv_abi)) void map_qa_load(void *owner) {
    map_qa_load_original(owner);
    unsigned char *base = owner;
    void **begin = *(void ***)(base + 0x58);
    void **end = *(void ***)(base + 0x60);
    if (begin && end == begin + 1 && getenv("DC_MAP_PAIR")) {
        map_qa_load_original(owner);
        begin = *(void ***)(base + 0x58);
        end = *(void ***)(base + 0x60);
    }
    printf("TrackMap QA: load owner=%p count=%lld enabled=%u\n",
           owner, (long long)(begin ? end - begin : 0), base[0x70]);
}

static __attribute__((sysv_abi)) void map_qa_rect(void *map, float x,float y,float width,float height) {
    if (map_qa_rect_events++ < 24)
        printf("TrackMap QA: rect %p %.3f %.3f %.3f %.3f\n",map,x,y,width,height);
    map_qa_rect_original(map,x,y,width,height);
}

static __attribute__((sysv_abi)) void map_qa_update(void *owner) {
    unsigned char *base = owner;
    void **begin = *(void ***)(base + 0x58);
    void **end = *(void ***)(base + 0x60);
    if (!begin || end == begin || !base[0x70]) {
        map_qa_update_original(owner);
        return;
    }
    unsigned char *world = *(unsigned char **)0x801911100;
    unsigned char *state = *(unsigned char **)(world + 0x968);
    void *manager = *(void **)(state + 0x678);
    void *position = *(void **)(state + 0x5b8);
    static int first = 1;
    if (end == begin + 2) {
        for (int seat=0; seat<2; seat++) {
            unsigned char *actor = ((MapQAActor)0x8004744a0)(manager,seat);
            if (!actor) continue;
            int actor_id = ((MapQAActorId)0x800d606e0)(actor);
            int position_index = *(int *)(actor + 0x29e4);
            unsigned char suppressed = position && position_index >= 0 ?
                ((MapQAPosition)0x80002f930)(position,position_index) : 0;
            if (first)
                printf("TrackMap QA: update seat=%d map=%p actor=%d suppressed=%u\n",
                       seat,begin[seat],actor_id,suppressed);
            ((MapQADraw)0x80051b1d0)(begin[seat],actor_id,suppressed);
        }
        first = 0;
        return;
    }
    if (first) {
        for (int seat=0; seat<2; seat++) {
            void *actor = ((MapQAActor)0x8004744a0)(manager,seat);
            printf("TrackMap QA: stock seat=%d map=%p actor=%d\n",seat,begin[0],
                   actor ? ((MapQAActorId)0x800d606e0)(actor) : -1);
        }
        first = 0;
    }
    map_qa_update_original(owner);
}

static void map_qa_install(void) {
    if (!getenv("DC_MAP_QA") || map_qa_load_original) return;
    static const unsigned char head[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53};
    static const unsigned char rect[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x48,0x83,0xec,0x28};
    if (memcmp((void *)0x8005175c0,head,sizeof(head)) ||
        memcmp((void *)0x800517040,head,sizeof(head)) ||
        memcmp((void *)0x80051cf00,rect,sizeof(rect))) return;
    map_qa_load_original=(MapQALoad)camera_detour(0x8005175c0,(void *)map_qa_load,sizeof(head),0);
    map_qa_update_original=(MapQALoad)camera_detour(0x800517040,(void *)map_qa_update,sizeof(head),0);
    map_qa_rect_original=(MapQARect)camera_detour(0x80051cf00,(void *)map_qa_rect,sizeof(rect),0);
}

static __attribute__((sysv_abi)) void *hud_qa_factory(const char *type,const char *name,const char *asset) {
    void *panel=hud_qa_factory_original(type,name,asset);
    printf("Stock HUD QA: type=%s name=%s asset=%s controller=%p panel=%p\n",
           type,name,asset,panel,panel?*(void **)((unsigned char *)panel+0x58):NULL);
    return panel;
}

static void hud_qa_install(void) {
    if(hud_qa_factory_original || visibility_installed!=1) return;
    map_qa_install();
    static const unsigned char head[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x50,0x49,0x89,0xd7};
    if(memcmp((const void *)0x800480580,head,sizeof(head))) return;
    hud_qa_factory_original=(HudQAFactory)camera_detour(0x800480580,(void *)hud_qa_factory,sizeof(head),0);
}
