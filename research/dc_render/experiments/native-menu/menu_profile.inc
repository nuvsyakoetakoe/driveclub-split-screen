/* Settings belong to this installation and are independent of launcher presets. */
static int menu_profile_path(wchar_t path[MAX_PATH]) {
#ifdef DC_FEEDBACK_QA
    if(getenv("DC_MENU_QA_KEYS")) {
        DWORD test=GetEnvironmentVariableW(L"DC_MENU_QA_PROFILE",path,MAX_PATH);
        if(test) return test<MAX_PATH;
    }
#endif
    DWORD n=GetEnvironmentVariableW(L"DC_NATIVE_ROOT",path,MAX_PATH);
    const wchar_t suffix[]=L"\\mods\\split-screen\\settings.ini";
    if(!n || n+wcslen(suffix)>=MAX_PATH) return 0;
    wcscat(path,suffix);
    return 1;
}

static int menu_profile_value(void *lua,const char *key,const char *value) {
    wchar_t file[MAX_PATH],field[32],wide[256];
    int ok=0;
    if(!key || (strcmp(key,"name1") && strcmp(key,"name2") && strcmp(key,"devices") &&
       strcmp(key,"layout") && strcmp(key,"keys1") && strcmp(key,"keys2"))) return 0;
    if(!menu_profile_path(file) || !MultiByteToWideChar(CP_UTF8,0,key,-1,field,32)) return 0;
    if(!value) {
        if(!GetPrivateProfileStringW(L"Split Screen",field,L"",wide,256,file)) return 0;
        char utf8[1024];
        if(!WideCharToMultiByte(CP_UTF8,0,wide,-1,utf8,sizeof(utf8),NULL,NULL)) return 0;
        menu_lua_push_string(lua,utf8);
    } else {
        if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value,-1,wide,256)) return 0;
        HANDLE h=CreateFileW(file,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        if(h!=INVALID_HANDLE_VALUE) {
            const WORD bom=0xfeff;
            DWORD written=0;
            ok=WriteFile(h,&bom,sizeof(bom),&written,NULL) && written==sizeof(bom);
            CloseHandle(h);
            if(!ok) { menu_lua_boolean(lua,0); return 1; }
        }
        ok=WritePrivateProfileStringW(L"Split Screen",field,wide,file)!=0;
        if(!ok) fprintf(stderr,"Split menu: settings write failed (%lu)\n",GetLastError());
        menu_lua_boolean(lua,ok);
    }
    return 1;
}
