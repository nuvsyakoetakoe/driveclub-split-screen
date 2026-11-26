/* StartGame has already generated its graphics pack when Vulkan starts.
 * Make each split viewport use the corresponding launcher-tuned race profile. */
static char *menu_find(char *begin,char *end,const char *value) {
    const size_t length=strlen(value);
    for(char *at=begin;end-at>=(ptrdiff_t)length;at++)
        if(*at==*value && !memcmp(at,value,length)) return at;
    return NULL;
}

static int menu_apply_viewport_quality(char *data,size_t size) {
    char *end=data+size;
    char *viewport=menu_find(data,end,"<ViewportQuality>");
    if(!viewport) return 0;
    char *close=menu_find(viewport,end,"</ViewportQuality>");
    if(!close) return 0;
    static const struct { const char *name,*parent; } views[]={
        {"TwoPlayer2D","OnePlayer2D"},
        {"TwoPlayer2DInCar","OnePlayer2DInCar"},
        {"TwoPlayer2DHQ","OnePlayer2DHQ"},
    };
    for(size_t i=0;i<sizeof(views)/sizeof(*views);i++) {
        char marker[96],profile_xml[280];
        snprintf(marker,sizeof(marker),"<Configuration name=\"%s\"",views[i].name);
        char *profile=menu_find(viewport,close,marker);
        if(!profile) return 0;
        static const char closing[]="</Configuration>";
        char *profile_end=menu_find(profile,close,closing);
        if(!profile_end) return 0;
        const size_t available=(size_t)(profile_end-profile)+sizeof(closing)-1;
        const int length=snprintf(profile_xml,sizeof(profile_xml),
            "<Configuration name=\"%s\" parent=\"%s\">"
            "<General fieldOfViewScale=\"1.050000\" enableRearViewMirrorTextures=\"true\"/>"
            "<ScreenRush Rain2Enabled=\"false\"/></Configuration>",views[i].name,views[i].parent);
        if(length<0 || (size_t)length>available || length>=sizeof(profile_xml)) return 0;
        memcpy(profile,profile_xml,(size_t)length);
        memset(profile+length,' ',available-(size_t)length);
    }
    return 1;
}

static void menu_prepare_graphics(void) {
    wchar_t exe[MAX_PATH],root[MAX_PATH],source[MAX_PATH],target[MAX_PATH],temporary[MAX_PATH],user[MAX_PATH];
    if(!GetModuleFileNameW(NULL,exe,MAX_PATH)) return;
    const wchar_t *basename=wcsrchr(exe,L'\\');
    if(!basename || _wcsicmp(basename+1,L"DriveClubGame.exe")) return;
    const DWORD length=GetEnvironmentVariableW(L"DC_NATIVE_ROOT",root,MAX_PATH);
    if(!length || length>=MAX_PATH) return;
    if(swprintf(target,MAX_PATH,L"%ls\\mods\\global.rpk",root)<0 ||
       swprintf(temporary,MAX_PATH,L"%ls\\mods\\split-screen\\global-%lu.tmp",root,
                (unsigned long)GetCurrentProcessId())<0) return;
    const DWORD user_length=GetEnvironmentVariableW(L"BB_GPU_USER_DIR",user,MAX_PATH);
    FILE *stream=NULL;
    if(user_length && user_length<MAX_PATH) {
        wchar_t *last=wcsrchr(user,L'\\');
        if(last) {
            *last=0;
            if(swprintf(source,MAX_PATH,L"%ls\\graphics\\global.rpk",user)>=0) stream=_wfopen(source,L"rb");
        }
    }
    const int configured=stream!=NULL;
    if(!stream) {
        if(swprintf(source,MAX_PATH,L"%ls\\mods\\split-screen\\default-mirrors.rpk",root)<0) return;
        stream=_wfopen(source,L"rb");
    }
    if(!stream) return;
    const long size=fseek(stream,0,SEEK_END)?-1:ftell(stream);
    char *data=size>0 && !fseek(stream,0,SEEK_SET)?malloc((size_t)size):NULL;
    const int loaded=data && fread(data,1,(size_t)size,stream)==(size_t)size;
    fclose(stream);
    int success=0;
    if(loaded && menu_apply_viewport_quality(data,(size_t)size)) {
        stream=_wfopen(temporary,L"wb");
        if(stream) {
            int written=fwrite(data,1,(size_t)size,stream)==(size_t)size;
            if(fclose(stream)) written=0;
            success=written && MoveFileExW(temporary,target,MOVEFILE_REPLACE_EXISTING);
            if(!success) DeleteFileW(temporary);
        }
    }
    free(data);
    if(success) printf("Split graphics: matched race viewport profiles with %s graphics\n",configured?"launcher":"default");
    else {
        DeleteFileW(target); /* The installer reserves this generated path. */
        fputs("Split graphics: could not build the viewport overlay\n",stderr);
    }
}
