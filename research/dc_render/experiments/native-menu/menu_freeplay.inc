/* Keep the retail Free Play wizard and vehicle showroom. Only its final grid
 * gains another locally controlled vehicle when entered from the split tile. */
typedef __attribute__((sysv_abi)) uintptr_t (*MenuPagePush)(void *,const char *,void *,int);
typedef __attribute__((sysv_abi)) int (*MenuGarageAccept)(unsigned char *);
typedef __attribute__((sysv_abi)) int (*MenuAddDriver)(void *,int,const char *,int,void *,int,float,
        int,int64_t,int,int,const char *,uintptr_t,uintptr_t);
static MenuPagePush menu_push_page;
static MenuGarageAccept menu_garage_accept;
static MenuAddDriver menu_add_driver;
static int menu_flow_active,menu_garage_seat,menu_grid_armed,menu_second_model;
static int menu_return_to_split;
static unsigned char *menu_first_garage;

static unsigned char *menu_freeplay(void) {
    unsigned char *game=*(unsigned char **)0x801911100;
    unsigned char *mode=game?*(unsigned char **)(game+0x970):NULL;
    return mode?*(unsigned char **)(mode+0x8e8):NULL;
}

static __attribute__((sysv_abi)) uintptr_t menu_flow_push(void *selector,const char *key,void *args,int animate) {
    if(key && menu_return_to_split && !strcmp(key,"x_freeplay.main")) {
        menu_return_to_split=0;
        key="x_split_screen.setup";
    }
    if(key && menu_flow_active && !strcmp(key,"vehicleselect.freeplay")) key="x_split_screen.garage1";
    if(key && (!strcmp(key,"x_freeplay.main") || !strcmp(key,"x_drive.main") || !strcmp(key,"x_main.frontend"))) {
        menu_flow_active=0;
        menu_garage_seat=0;
    }
    return menu_push_page(selector,key,args,animate);
}

static __attribute__((sysv_abi)) int menu_choose_car(unsigned char *page) {
    if(menu_flow_active && menu_first_garage==page) menu_garage_seat=1;
    if(!menu_flow_active || !menu_garage_seat) return menu_garage_accept(page);
    unsigned char *record=*(unsigned char **)(page+0x330);
    unsigned char *freeplay=menu_freeplay();
    void *selector=*(void **)0x80189f8c8;
    if(!record || !freeplay || !selector) return 0;
    const char *name=*(size_t *)(record+0x20)<16?(char *)record+8:*(char **)(record+8);
#ifdef DC_FEEDBACK_QA
    int audio_pair=getenv("DC_QA_MERCEDES_FERRARI")!=NULL;
    if(audio_pair) name=menu_garage_seat==1?"me_sl_am_ca":"fe_599";
    int bmw_test=getenv("DC_QA_BMW_IMPACT")!=NULL;
    if(bmw_test) name=menu_garage_seat==1?"ren_dez":"bm_m2";
#endif
    printf("Split menu: P%d selected %s\n",menu_garage_seat,name);
    if(menu_garage_seat==1) {
        menu_first_garage=page;
        typedef __attribute__((sysv_abi)) void (*SetVehicle)(void *,const char *);
        ((SetVehicle)0x80004e3e0)(freeplay,name);
        menu_push_page(selector,"x_split_screen.garage2",NULL,1);
    } else {
        menu_second_model=*(int *)(record+0x50);
#ifdef DC_FEEDBACK_QA
        if(audio_pair) menu_second_model=24;
        if(bmw_test) menu_second_model=16;
#endif
        menu_push_page(selector,"x_split_screen.start",NULL,1);
    }
    menu_garage_seat=0;
    return 1;
}

static __attribute__((sysv_abi)) int menu_freeplay_driver(void *config,int group,const char *role,int model,
    void *appearance,int type,float speed,int active,int64_t profile,int opponent,int controller,
    const char *livery,uintptr_t a,uintptr_t b) {
    const uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    if(menu_grid_armed && caller==0x800191fd3) {
        // Twelve is the retail grid limit. Reserve two places for the local cars.
        const unsigned char *base=config;
        if((*(uintptr_t *)(base+0x10)-*(uintptr_t *)(base+8))/0x230>=10) return 0;
    }
    if(menu_grid_armed && caller==0x800192269) {
        menu_add_driver(config,group,role,menu_second_model,appearance,type,speed,active,-1,-1,0,NULL,0,0);
        controller=1;
        printf("Split menu: local grid models %d / %d\n",model,menu_second_model);
    }
    return menu_add_driver(config,group,role,model,appearance,type,speed,active,profile,opponent,controller,livery,a,b);
}

static int menu_flow_install(void) {
    static const unsigned char push[]={0x48,0x89,0xd0,0x48,0x89,0xf2,0x44,0x0f,0xb6,0xc1,0x31,0xf6};
    static const unsigned char accept[]={0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x83,0xec,0x60,0x48,0x89,0xfb};
    static const unsigned char add[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53};
    if(menu_push_page && menu_garage_accept && menu_add_driver) return 1;
    if(!camera_code || camera_code_used+192>CAMERA_CODE_CAPACITY || memcmp((void *)0x800283e50,push,sizeof(push)) ||
       memcmp((void *)0x8002fa050,accept,sizeof(accept)) || memcmp((void *)0x800003ff0,add,sizeof(add))) return 0;
    menu_push_page=(MenuPagePush)camera_detour(0x800283e50,(void *)menu_flow_push,sizeof(push),0);
    menu_garage_accept=(MenuGarageAccept)camera_detour(0x8002fa050,(void *)menu_choose_car,sizeof(accept),0);
    menu_add_driver=(MenuAddDriver)camera_detour(0x800003ff0,(void *)menu_freeplay_driver,sizeof(add),0);
    return menu_push_page && menu_garage_accept && menu_add_driver;
}
