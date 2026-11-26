/* DriveAA clears its scene redirect at each group of display draws. The two
 * player composites can be separated by offscreen draws within one present. */
static volatile LONG menu_display_epoch;
static volatile LONG __attribute__((used)) menu_composite_pair;
static _Thread_local struct {
    void *aa;
    uint32_t color;
    LONG epoch;
    int armed;
} menu_display_pair;
static void (*menu_original_next_frame)(void*);

static void menu_display_next_frame(void *aa) {
    unsigned char *state=aa;
    const int second=menu_display_pair.armed && menu_display_pair.aa==aa &&
        menu_display_pair.epoch==menu_display_epoch && menu_race_active &&
        state[0x1a8] && !state[0x1e8] && *(uint32_t*)(state+0x1ac)==menu_display_pair.color;
    int mode=*(int*)(state+0x1dc);
    menu_original_next_frame(aa);
    if(second && mode==*(int*)(state+0x1dc)) {
        state[0x1a8]=1;
        /* Keep the resolved image available through every display group in this present. */
    }
}

/* Native AA normally resolves too soon: the top post effects have already
 * sampled the guest TAA by the time the second player's geometry is ready.
 * Select the existing late composite for a matched-size split scene too.
 * The DLSS feature still selects DLAA for equal input and output sizes. */
static uintptr_t __attribute__((used)) menu_scale_setting,menu_early_taa,menu_late_taa,menu_bind_continue;
static __attribute__((naked)) void menu_composite_timing(void) {
    __asm__ volatile(
        "mov menu_scale_setting(%rip),%r8\n mov (%r8),%r8d\n"
        "test %r8d,%r8d\n jnz 1f\n"
        "cmpl $0,menu_composite_pair(%rip)\n je 2f\n"
        "cmp %r12d,%ecx\n jne 2f\n cmp %r15d,%eax\n jne 2f\n jmp 3f\n"
        "1: cmp %r12d,%ecx\n jbe 2f\n cmp %r15d,%eax\n jbe 2f\n"
        "3: jmp *menu_late_taa(%rip)\n"
        "2: jmp *menu_early_taa(%rip)\n");
}
static __attribute__((naked)) void menu_composite_bind(void) {
    __asm__ volatile(
        "mov menu_scale_setting(%rip),%rax\n mov (%rax),%eax\n test %eax,%eax\n jnz 1f\n"
        "cmpb $0,0x1d85bc4(%r14)\n je 2f\n"
        "cmpl $0,menu_composite_pair(%rip)\n je 2f\n"
        "cmpl $0,menu_race_active(%rip)\n je 2f\n"
        "1: mov $1,%eax\n"
        "2: mov %eax,0x8a0(%rbp)\n jmp *menu_bind_continue(%rip)\n");
}

static int menu_composite_jump(void *entry,size_t size,uintptr_t target) {
    unsigned char jump[14]={0xff,0x25,0,0,0,0};
    DWORD old,ignored;
    memcpy(jump+6,&target,8);
    if(!VirtualProtect(entry,size,PAGE_EXECUTE_READWRITE,&old)) return 0;
    memcpy(entry,jump,sizeof(jump));
    VirtualProtect(entry,size,old,&ignored);
    FlushInstructionCache(GetCurrentProcess(),entry,sizeof(jump));
    return 1;
}
static void menu_composite_install(uintptr_t base) {
    const unsigned char next[]={0x55,0x56,0x57,0x53,0x48,0x83,0xec,0x48,0x48,0x8d,0x6c,0x24,0x40};
    const unsigned char timing[]={0x44,0x8b,5,0xc0,0x20,0xb5,0,0x44,0x39,0xe1,0x0f,0x86,0x42,2,0,0,
        0x45,0x85,0xc0,0x0f,0x84,0x39,2,0,0,0x44,0x39,0xf8,0x0f,0x86,0x30,2,0,0};
    const unsigned char binding[]={0x8b,5,0x4a,0x42,0xd5,0,0x85,0xc0,0x0f,0x95,0xc0,0x89,0x85,0xa0,8,0,0};
    if(memcmp((void*)(base+0x259c10),next,sizeof(next)) ||
       memcmp((void*)(base+0x25a19d),timing,sizeof(timing)) ||
       memcmp((void*)(base+0x58014),binding,sizeof(binding))) return;
    menu_original_next_frame=(void (*)(void*))camera_detour(base+0x259c10,(void*)menu_display_next_frame,sizeof(next),0);
    if(!menu_original_next_frame) return;
    menu_scale_setting=base+0x25a1a4+0xb520c0;
    menu_early_taa=base+0x25a3ef;
    menu_late_taa=base+0x25a1bf;
    menu_bind_continue=base+0x58025;
    if(menu_composite_jump((void*)(base+0x58014),sizeof(binding),(uintptr_t)menu_composite_bind))
        menu_composite_jump((void*)(base+0x25a19d),sizeof(timing),(uintptr_t)menu_composite_timing);
}
