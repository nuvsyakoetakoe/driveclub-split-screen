typedef __attribute__((sysv_abi)) int (*EngineMixPcmFn)(int,const float *);
static EngineMixPcmFn engine_mix_original_pcm;
typedef bool (*EngineMixPut)(void *,const void *,int);
static EngineMixPut engine_mix_put_original;
static _Thread_local int engine_mix_main_port;
static bool engine_mix_put(void *stream,const void *samples,int bytes) {
    if(engine_mix_main_port && bytes==256*2*(int)sizeof(float)) {
        static FILE *out;
        static unsigned long long frames;
        static ULONGLONG last;
        if(!out) out=fopen("impact-output-stereo.f32","wb");
        if(out && fwrite(samples,1,bytes,out)==(size_t)bytes) {
            frames+=256;
            ULONGLONG now=GetTickCount64();
            if(now-last>=250) {
                printf("Impact output QA: ms=%llu frames=%llu\n",now-start,frames);
                last=now;
            }
        }
    }
    return engine_mix_put_original(stream,samples,bytes);
}
static __attribute__((sysv_abi)) int engine_mix_pcm(int handle,const float *samples) {
    static double power;
    static unsigned count;
    static ULONGLONG from;
    static FILE *recording;
    if(handle==0x20000000 && samples && visibility_active()) {
        const ULONGLONG now=GetTickCount64();
        if(!from) {
            from=now;
            recording=fopen("engine-mix-stereo.f32","wb");
        }
        for(int i=0;i<8*256;i++) power+=(double)samples[i]*samples[i];
        count+=8*256;
        if(recording) {
            float stereo[512];
            for(int i=0;i<256;i++) {
                stereo[2*i]=samples[8*i];
                stereo[2*i+1]=samples[8*i+1];
            }
            fwrite(stereo,sizeof(*stereo),512,recording);
        }
        if(now-from>=900) {
            printf("Engine mix QA: ms=%llu rms=%.8f n=%u\n",now-start,sqrt(power/count),count);
            from=now;
            power=0;
            count=0;
        }
    }
    engine_mix_main_port=handle==0x20000000 && samples;
    int result=engine_mix_original_pcm(handle,samples);
    engine_mix_main_port=0;
    return result;
}
static void engine_mix_install(void) {
    if(engine_mix_original_pcm || !visibility_active()) return;
    void *volatile *slot=(void *volatile *)0x80166f7b0;
    DWORD previous,ignored;
    if(!*slot || !VirtualProtect((void *)slot,sizeof(*slot),PAGE_READWRITE,&previous)) return;
    if(getenv("DC_QA_BMW_IMPACT"))
        camera_hook_sdl("SDL_PutAudioStreamData",engine_mix_put,&engine_mix_put_original);
    engine_mix_original_pcm=(EngineMixPcmFn)*slot;
    InterlockedExchangePointer(slot,(void *)engine_mix_pcm);
    VirtualProtect((void *)slot,sizeof(*slot),previous,&ignored);
}
