/* DRIVECLUB recomp 0.4.2: keep SSR ray depth reads inside their viewport.
 * Native SSR converts ray coordinates through ViewCB's scale/bias, but its
 * texture sampler clamps against the whole image. With two packed views an
 * escaped ray can therefore intersect the other player's depth. Supply far
 * depth (reversed Z = 0) outside this view, preserving native SSR elsewhere.
 * Exact shader hashes guard all IDs below; unknown programs pass unchanged. */
static uint32_t *ssr_guard(const uint32_t *code,size_t words,size_t *patched_words) {
    uint64_t hash=14695981039346656037ULL;
    for(size_t i=0;i<words;i++) {hash^=code[i];hash*=1099511628211ULL;}
    unsigned viewport,expected_reads;
    if(hash==0xbc88a857c3fb3c11ULL) {
        viewport=198;
        expected_reads=4;
    } else if(hash==0x4c87874daf8f608cULL) {
        viewport=228;
        expected_reads=5;
    } else return NULL;
    if(words<5 || code[0]!=0x07230203 || code[3]>65536) return NULL;
    unsigned bound=code[3],next=bound,zero=0;
    unsigned *loaded=calloc(bound,sizeof(unsigned)),*sampled=calloc(bound,sizeof(unsigned));
    uint32_t *out=malloc((words+1024)*sizeof(uint32_t));
    if(!loaded || !sampled || !out) {free(loaded);free(sampled);free(out);return NULL;}
    for(size_t i=5;i<words;) {
        unsigned n=code[i]>>16,op=code[i]&65535;
        if(!n || i+n>words) goto invalid;
        const uint32_t *a=code+i+1;
        if(op==43 && n==4 && a[0]==8 && a[2]==0) zero=a[1];
        if(op==61 && n>=4 && a[1]<bound) loaded[a[1]]=a[2];
        if(op==86 && n==5 && a[1]<bound && a[2]<bound) sampled[a[1]]=loaded[a[2]];
        i+=n;
    }
    if(!zero) goto invalid;
    size_t pos=5;unsigned zero_vec=next++,negative_half=next++,added=0,reads=0;
    memcpy(out,code,5*sizeof(uint32_t));
#define SSR_SPV(op,...) do {const uint32_t v[]={__VA_ARGS__};size_t n=sizeof(v)/sizeof(v[0]);out[pos++]=(uint32_t)((n+1)<<16)|(op);memcpy(out+pos,v,sizeof(v));pos+=n;} while(0)
    for(size_t i=5;i<words;) {
        unsigned n=code[i]>>16,op=code[i]&65535;const uint32_t *a=code+i+1;
        if(op==54 && !added) {
            SSR_SPV(44,20,zero_vec,zero,zero,zero,zero); /* vec4(0) */
            SSR_SPV(43,8,negative_half,0xbf000000u);
            added=1;
        }
        /* Variables 70 / 72 are the full and coarse ray-depth textures. */
        if(op==88 && n==7 && a[2]<bound && (sampled[a[2]]==70 || sampled[a[2]]==72)) {
            unsigned f=next++;SSR_SPV(124,20,f,viewport);
            unsigned c[4],bounds[4],uv[2],test[4];
            for(unsigned k=0;k<4;k++) {c[k]=next++;SSR_SPV(81,8,c[k],f,k);}
            bounds[0]=next++;SSR_SPV(131,8,bounds[0],c[2],c[0]);
            bounds[1]=next++;SSR_SPV(129,8,bounds[1],c[3],c[1]);
            bounds[2]=next++;SSR_SPV(129,8,bounds[2],c[2],c[0]);
            bounds[3]=next++;SSR_SPV(131,8,bounds[3],c[3],c[1]);
            for(unsigned k=0;k<2;k++) {uv[k]=next++;SSR_SPV(81,8,uv[k],a[3],k);}
            test[0]=next++;SSR_SPV(190,3,test[0],uv[0],bounds[0]);
            test[1]=next++;SSR_SPV(190,3,test[1],uv[1],bounds[1]);
            test[2]=next++;SSR_SPV(184,3,test[2],uv[0],bounds[2]);
            test[3]=next++;SSR_SPV(184,3,test[3],uv[1],bounds[3]);
            unsigned inside=test[0];
            for(unsigned k=1;k<4;k++) {unsigned b=next++;SSR_SPV(167,3,b,inside,test[k]);inside=b;}
            /* Preserve the original full-screen menu / single-view shader. */
            unsigned full=next++,condition=next++,vector_condition=next++,raw=next++;
            SSR_SPV(188,3,full,c[1],negative_half);
            SSR_SPV(166,3,condition,inside,full);
            SSR_SPV(80,23,vector_condition,condition,condition,condition,condition);
            SSR_SPV(88,a[0],raw,a[2],a[3],a[4],a[5]);
            SSR_SPV(169,20,a[1],vector_condition,raw,zero_vec);
            reads++;
        } else {memcpy(out+pos,code+i,n*sizeof(uint32_t));pos+=n;}
        i+=n;
    }
#undef SSR_SPV
    if(!added || reads!=expected_reads) goto invalid;
    out[3]=next;*patched_words=pos;free(loaded);free(sampled);
    printf("Split SSR: shader=%016llx guarded_depth_reads=%u\n",(unsigned long long)hash,reads);
    return out;
invalid:
    free(loaded);free(sampled);free(out);return NULL;
}

static VkResult VKAPI_CALL ssr_shader(VkDevice device,const VkShaderModuleCreateInfo *ci,const VkAllocationCallbacks *a,VkShaderModule *module) {
    size_t words=0;uint32_t *code=ssr_guard(ci->pCode,ci->codeSize/4,&words);
    VkShaderModuleCreateInfo patched=*ci;
    if(code) {patched.pCode=code;patched.codeSize=words*sizeof(uint32_t);}
    VkResult result=D(CreateShaderModule)(device,&patched,a,module);
    free(code);return result;
}
