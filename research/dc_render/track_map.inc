/* The retail track-map component can lay out multiple maps. Create the second
 * map before layout, then update each map from the corresponding local racer. */
typedef __attribute__((sysv_abi)) void (*TrackMapComponentFn)(void *);
typedef __attribute__((sysv_abi)) void *(*TrackMapRacerFn)(void *,int);
typedef __attribute__((sysv_abi)) int (*TrackMapActorIdFn)(void *);
typedef __attribute__((sysv_abi)) unsigned char (*TrackMapSuppressedFn)(void *,int);
typedef __attribute__((sysv_abi)) void (*TrackMapUpdateFn)(void *,int,unsigned char);

static TrackMapComponentFn track_map_original_load, track_map_original_update;
static int track_map_reported_actor[2];

static __attribute__((sysv_abi)) void track_map_load(void *component) {
    track_map_original_load(component);
#ifdef DC_NATIVE_MENU
    if(!InterlockedCompareExchange(&menu_race_active,0,0)) return;
#endif
    unsigned char *base=component;
    void **begin=*(void ***)(base+0x58);
    void **end=*(void ***)(base+0x60);
    if (begin && end==begin+1) {
        track_map_original_load(component);
        track_map_reported_actor[0]=track_map_reported_actor[1]=-1;
    }
}

static __attribute__((sysv_abi)) void track_map_update(void *component) {
    unsigned char *base=component;
    void **begin=*(void ***)(base+0x58);
    void **end=*(void ***)(base+0x60);
    if (!begin || end!=begin+2 || !base[0x70]) {
        track_map_original_update(component);
        return;
    }

    unsigned char *world=*(unsigned char **)0x801911100;
    unsigned char *race=*(unsigned char **)(world+0x968);
    void *racers=*(void **)(race+0x678);
    void *positions=*(void **)(race+0x5b8);
    for (int seat=0;seat<2;seat++) {
        unsigned char *racer=((TrackMapRacerFn)0x8004744a0)(racers,seat);
        if (!racer) continue;
        const int actor=((TrackMapActorIdFn)0x800d606e0)(racer);
        const int position=*(int *)(racer+0x29e4);
        const unsigned char suppressed=camera_hud_hidden(seat) || (positions && position>=0 &&
            ((TrackMapSuppressedFn)0x80002f930)(positions,position));
        ((TrackMapUpdateFn)0x80051b1d0)(begin[seat],actor,suppressed);
        if (track_map_reported_actor[seat]!=actor) {
            track_map_reported_actor[seat]=actor;
            printf("Split track map: P%d actor=%d\n",seat+1,actor);
        }
    }
}

static void track_map_install(void) {
    if (track_map_original_load || camera_installed!=1 || !getenv("DC_SPLIT_TRACK_MAP")) return;
    static const unsigned char prologue[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53};
    const uintptr_t load=0x8005175c0, update=0x800517040;
    if (memcmp((void *)load,prologue,sizeof(prologue)) ||
        memcmp((void *)update,prologue,sizeof(prologue))) return;
    track_map_original_update=(TrackMapComponentFn)camera_detour(update,(void *)track_map_update,sizeof(prologue),0);
    if (track_map_original_update)
        track_map_original_load=(TrackMapComponentFn)camera_detour(load,(void *)track_map_load,sizeof(prologue),0);
}
