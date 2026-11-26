/* The world-space tag draws a separate portrait quad even when the racer has
 * no avatar. Leave the native tag preferences and name rendering in place. */
typedef __attribute__((sysv_abi)) void *(*TagAvatarFn)(int);
typedef __attribute__((sysv_abi)) void *(*TagResourcesFn)(void);
typedef __attribute__((sysv_abi)) void *(*TagFindFn)(void *,const char *);

static __attribute__((sysv_abi)) void *tag_avatar(int actor) {
    void *avatar=((TagAvatarFn)0x8003d10a0)(actor);
    if(!avatar) return NULL;
#ifdef DC_NATIVE_MENU
    if(!InterlockedCompareExchange(&menu_race_active,0,0)) return avatar;
#endif
    void *resources=((TagResourcesFn)0x80025ec40)();
    void *fallback=((TagFindFn)0x80025f040)(resources,"avatar-default");
    return avatar==fallback?NULL:avatar;
}

static void player_tags_install(void) {
    static int installed;
    if(installed || camera_installed!=1) return;
    unsigned char *call=(unsigned char *)0x800bec599;
    static const unsigned char original[]={0xe8,0x02,0x4b,0x7e,0xff};
    if(memcmp(call,original,sizeof(original))) return;

    /* Route just the world-space tag's avatar lookup through a nearby stub.
     * The original lookup is still used by results and other menus. */
    unsigned char *stub=(unsigned char *)camera_code+camera_code_used;
    const intptr_t offset=(intptr_t)stub-(intptr_t)(call+sizeof(original));
    if(offset<INT32_MIN || offset>INT32_MAX) return;
    DWORD old_protection;
    if(!VirtualProtect(call,sizeof(original),PAGE_EXECUTE_READWRITE,&old_protection)) return;

    camera_code_used+=64;
    camera_jump(stub,(void *)tag_avatar);
    FlushInstructionCache(GetCurrentProcess(),stub,12);
    const int32_t displacement=(int32_t)offset;
    memcpy(call+1,&displacement,sizeof(displacement));
    DWORD ignored;
    VirtualProtect(call,sizeof(original),old_protection,&ignored);
    FlushInstructionCache(GetCurrentProcess(),call,sizeof(original));
    installed=1;
    printf("Split tags: suppressing default portraits above cars\n");
}
