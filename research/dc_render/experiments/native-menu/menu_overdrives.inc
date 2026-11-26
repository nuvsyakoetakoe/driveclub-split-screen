/* Overdrives queues the driving award before the HUD and progression consume it. */
typedef __attribute__((sysv_abi)) void (*MenuQueueAwardFn)(void *,int,int,int,int);
static MenuQueueAwardFn menu_original_queue_award;

static __attribute__((sysv_abi)) void menu_queue_award(void *component,int event,int points,int separate,int variant) {
    if(InterlockedCompareExchange(&menu_race_active,0,0)) {
        static LONG reported;
        if(points && !InterlockedExchange(&reported,1))
            printf("Split scoring: skipped driving award event=%d points=%d\n",event,points);
        return;
    }
    menu_original_queue_award(component,event,points,separate,variant);
}

static void menu_overdrives_install(void) {
    if(menu_original_queue_award || camera_installed!=1) return;
    const uintptr_t address=0x8011a0950;
    static const unsigned char head[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53};
    if(!memcmp((const void *)address,head,sizeof(head)))
        menu_original_queue_award=(MenuQueueAwardFn)camera_detour(address,(void *)menu_queue_award,sizeof(head),0);
}
