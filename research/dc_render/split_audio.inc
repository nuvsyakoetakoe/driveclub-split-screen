/* The 1.28 executable has its own statically linked FMOD core. It initializes
 * one listener and updates it from P1's view. Add P2 to that same native mix. */
typedef __attribute__((sysv_abi)) int (*SplitAudioListenerFn)(void *,int,const float *,const float *,const float *,const float *);
typedef __attribute__((sysv_abi)) int (*SplitAudioCountFn)(void *,int);
static SplitAudioListenerFn split_audio_original;
static SRWLOCK split_audio_lock=SRWLOCK_INIT;
typedef struct {
    float position[3],velocity[3],forward[3],up[3];
} SplitAudioPose;
static struct {
    int32_t actor;
    const void *view;
    ULONGLONG fresh;
    LARGE_INTEGER sampled;
    SplitAudioPose pose;
} split_audio_pose;
static struct {
    ULONGLONG fresh;
    SplitAudioPose listener[2];
} split_audio_engine_mix;

static void split_audio_view(const unsigned char *view) {
    AcquireSRWLockShared(&camera_lock);
    const int32_t actor=camera_packet.actors[1];
    ReleaseSRWLockShared(&camera_lock);
    LARGE_INTEGER now,frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    float position[3],forward[3],up[3],velocity[3]={0};
    memcpy(position,view+0x60,sizeof(position));
    memcpy(forward,view+0x50,sizeof(forward));
    /* The render camera stores its backward Z axis. FMOD takes the
     * direction the listener is looking in. */
    for(int axis=0;axis<3;axis++) forward[axis]=-forward[axis];
    memcpy(up,view+0x40,sizeof(up));
    for(int axis=0;axis<3;axis++)
        if(!isfinite(position[axis]) || !isfinite(forward[axis]) || !isfinite(up[axis])) return;

    AcquireSRWLockExclusive(&split_audio_lock);
    const double dt=(double)(now.QuadPart-split_audio_pose.sampled.QuadPart)/frequency.QuadPart;
    if(split_audio_pose.actor==actor && split_audio_pose.view==view && dt>0.002 && dt<0.2) {
        float speed_squared=0;
        for(int axis=0;axis<3;axis++) {
            velocity[axis]=(float)((position[axis]-split_audio_pose.pose.position[axis])/dt);
            speed_squared+=velocity[axis]*velocity[axis];
        }
        if(speed_squared>140*140) memset(velocity,0,sizeof(velocity));
    }
    split_audio_pose.actor=actor;
    split_audio_pose.view=view;
    split_audio_pose.fresh=GetTickCount64();
    split_audio_pose.sampled=now;
    memcpy(split_audio_pose.pose.position,position,sizeof(position));
    memcpy(split_audio_pose.pose.velocity,velocity,sizeof(velocity));
    memcpy(split_audio_pose.pose.forward,forward,sizeof(forward));
    memcpy(split_audio_pose.pose.up,up,sizeof(up));
    ReleaseSRWLockExclusive(&split_audio_lock);
}

static __attribute__((sysv_abi)) int split_audio_listener(void *system,int index,const float *pos,const float *vel,
                                                          const float *forward,const float *up) {
    const int result=split_audio_original(system,index,pos,vel,forward,up);
    const uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    if(result || index!=0 || (caller!=0x80054e546 && caller!=0x800af09ba)) return result;

    static void *two_listeners;
    const SplitAudioCountFn count=(SplitAudioCountFn)0x800a41810;
    AcquireSRWLockShared(&camera_lock);
    const int32_t actor=camera_packet_has_actors()?camera_packet.actors[1]:0;
    ReleaseSRWLockShared(&camera_lock);
    SplitAudioPose second;
    AcquireSRWLockShared(&split_audio_lock);
    const int ready=actor>0 && split_audio_pose.actor==actor && GetTickCount64()-split_audio_pose.fresh<200;
    if(ready) second=split_audio_pose.pose;
    ReleaseSRWLockShared(&split_audio_lock);
    if(!ready) {
        if(two_listeners==system && !count(system,1)) two_listeners=NULL;
        return result;
    }
    if(two_listeners!=system) {
        if(count(system,2)) return result;
        two_listeners=system;
        printf("Split audio: enabled two native FMOD listeners\n");
    }
    const int second_result=split_audio_original(system,1,second.position,second.velocity,second.forward,second.up);
    if(second_result && !count(system,1)) two_listeners=NULL;
    if(!second_result && pos && forward && up) {
        SplitAudioPose first={0};
        memcpy(first.position,pos,sizeof(first.position));
        if(vel) memcpy(first.velocity,vel,sizeof(first.velocity));
        memcpy(first.forward,forward,sizeof(first.forward));
        memcpy(first.up,up,sizeof(first.up));
        AcquireSRWLockExclusive(&split_audio_lock);
        split_audio_engine_mix.listener[0]=first;
        split_audio_engine_mix.listener[1]=second;
        split_audio_engine_mix.fresh=GetTickCount64();
        ReleaseSRWLockExclusive(&split_audio_lock);
    }
#ifdef DC_FEEDBACK_QA
    static ULONGLONG sampled;
    const ULONGLONG now=GetTickCount64();
    if(pos && now-sampled>=1000) {
        sampled=now;
        const float dx=second.position[0]-pos[0],dy=second.position[1]-pos[1],dz=second.position[2]-pos[2];
        if(forward && getenv("DC_QA_BMW_IMPACT")) printf("Split FX camera alignment: ms=%llu dot=%.3f\n",now-start,
            forward[0]*second.forward[0]+forward[1]*second.forward[1]+forward[2]*second.forward[2]);
        printf("Split audio QA: ms=%llu P2 actor=%d result=%d pos=%.2f,%.2f,%.2f separation=%.1f\n",now-start,
            actor,second_result,second.position[0],second.position[1],second.position[2],sqrtf(dx*dx+dy*dy+dz*dz));
    }
#endif
    return result;
}

static void split_audio_right(const SplitAudioPose *pose,float right[3]) {
    right[0]=pose->up[1]*pose->forward[2]-pose->up[2]*pose->forward[1];
    right[1]=pose->up[2]*pose->forward[0]-pose->up[0]*pose->forward[2];
    right[2]=pose->up[0]*pose->forward[1]-pose->up[1]*pose->forward[0];
}

static void split_audio_rotate(const float vector[3],const float *const from[3],
                               const float *const to[3],float output[3]) {
    for(int axis=0;axis<3;axis++) output[axis]=0;
    for(int basis=0;basis<3;basis++) {
        const float projection=vector[0]*from[basis][0]+vector[1]*from[basis][1]+vector[2]*from[basis][2];
        for(int axis=0;axis<3;axis++) output[axis]+=projection*to[basis][axis];
    }
}

/* The local engine event still uses P1's listener for attenuation. Express the
 * P2 engine in that listener's space while preserving its P2-relative pose. */
static int split_audio_map_source(const float *pos,const float *vel,const float *dir,
                                  float *mapped_pos,float *mapped_vel,float *mapped_dir,int nearest) {
    SplitAudioPose first,second;
    AcquireSRWLockShared(&split_audio_lock);
    const int ready=split_audio_engine_mix.fresh && GetTickCount64()-split_audio_engine_mix.fresh<250;
    if(ready) {
        first=split_audio_engine_mix.listener[0];
        second=split_audio_engine_mix.listener[1];
    }
    ReleaseSRWLockShared(&split_audio_lock);
    if(!ready) return 0;

    float first_right[3],second_right[3],relative_pos[3],relative_vel[3];
    split_audio_right(&first,first_right);
    split_audio_right(&second,second_right);
    const float *from[]={second_right,second.up,second.forward};
    const float *to[]={first_right,first.up,first.forward};
    float distance1=0,distance2=0;
    for(int axis=0;axis<3;axis++) {
        relative_pos[axis]=pos[axis]-second.position[axis];
        relative_vel[axis]=vel?vel[axis]-second.velocity[axis]:0;
        const float delta=pos[axis]-first.position[axis];
        distance1+=delta*delta;
        distance2+=relative_pos[axis]*relative_pos[axis];
    }
    if(nearest && distance2>=distance1) return 0;
    split_audio_rotate(relative_pos,from,to,mapped_pos);
    if(vel) split_audio_rotate(relative_vel,from,to,mapped_vel);
    if(dir) split_audio_rotate(dir,from,to,mapped_dir);
    for(int axis=0;axis<3;axis++) {
        mapped_pos[axis]+=first.position[axis];
        if(vel) mapped_vel[axis]+=first.velocity[axis];
        if(!isfinite(mapped_pos[axis]) || (vel && !isfinite(mapped_vel[axis])) || (dir && !isfinite(mapped_dir[axis]))) return 0;
    }
    mapped_pos[3]=pos[3];
    if(vel) mapped_vel[3]=vel[3];
    if(dir) mapped_dir[3]=dir[3];
    return 1;
}

static void split_audio_install(void) {
    if(split_audio_original || visibility_installed!=1) return;
    const uintptr_t listener=0x800a41870;
    static const unsigned char head[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53};
    if(!memcmp((const void *)listener,head,sizeof(head)))
        split_audio_original=(SplitAudioListenerFn)camera_detour(listener,(void *)split_audio_listener,sizeof(head),0);
}
