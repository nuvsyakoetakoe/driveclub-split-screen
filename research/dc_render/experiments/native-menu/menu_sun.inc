/* The sun-source compute shader projects the flare with the current camera,
 * but samples its visibility at the position stored by the last camera.
 * Move the visibility read after projection so we can use this view's UV. */
static uint32_t *menu_sun_shader(const uint32_t *code,size_t words,size_t *out_words) {
    if(words!=4126) return NULL;
    uint64_t hash=14695981039346656037ULL;
    for(size_t i=0;i<words;i++) {hash^=code[i];hash*=1099511628211ULL;}
    if(hash!=0x37003b3f3e7336b7ULL) return NULL;
    uint32_t *out=malloc((words+320)*sizeof(*out));
    if(!out) return NULL;
    uint32_t next=code[3];
    size_t pos=5;
    memcpy(out,code,pos*sizeof(*out));
#define SUN_OP(op,...) do {const uint32_t operands[]={__VA_ARGS__};size_t count=sizeof(operands)/sizeof(*operands); \
    out[pos++]=(uint32_t)((count+1)<<16)|(op);memcpy(out+pos,operands,sizeof(operands));pos+=count;} while(0)
    for(size_t i=5;i<words;) {
        unsigned n=code[i]>>16;
        if(i>=2432 && i<=2452) { i+=n; continue; } /* sampled image, Dref read and result */
        if(i==3525) {
            /* CB 3 begins with the per-view sun sampling rectangle (x,y,w,h).
             * 539/541 are the sun's clip-space x/w and y/w for this camera. */
            uint32_t rect[4];
            const uint32_t indices[]={30,29,93,243};
            for(unsigned k=0;k<4;k++) {
                uint32_t idx=next++,ptr=next++,value=next++;
                rect[k]=next++;
                SUN_OP(128,10,idx,108,indices[k]);
                SUN_OP(65,50,ptr,54,30,idx);
                SUN_OP(61,10,value,ptr);
                SUN_OP(124,8,rect[k],value);
            }
            uint32_t edge=next++,top=next++,bottom=next++,split=next++,half=next++,use_local=next++;
            SUN_OP(129,8,edge,rect[1],rect[3]);
            SUN_OP(180,3,top,edge,579);      /* upper viewport ends at 0.5 */
            SUN_OP(180,3,bottom,rect[1],579); /* lower viewport begins at 0.5 */
            SUN_OP(166,3,split,top,bottom);
            SUN_OP(188,3,half,rect[3],579);
            SUN_OP(167,3,use_local,split,half);
            uint32_t depth_size=next++,uv[2];
            SUN_OP(103,14,depth_size,320,30); /* OpImageQuerySizeLod, mip 0 */
            for(unsigned k=0;k<2;k++) {
                uint32_t ndc=next++,scaled=next++,unit=next++,offset=next++,local=next++;
                uint32_t pixels=next++,size=next++,inset=next++,lo=next++,far_edge=next++,hi=next++,safe=next++;
                uv[k]=next++;
                SUN_OP(133,8,ndc,k?541:539,k?577:579);
                SUN_OP(129,8,scaled,ndc,579);
                SUN_OP(12,8,unit,317,43,scaled,31,465); /* clamp to this view */
                SUN_OP(133,8,offset,rect[2+k],unit);
                SUN_OP(129,8,local,rect[k],offset);
                /* Keep even a filtered depth compare inside this player's pixels. */
                SUN_OP(81,10,pixels,depth_size,k);
                SUN_OP(112,8,size,pixels);
                SUN_OP(136,8,inset,579,size);
                SUN_OP(129,8,lo,rect[k],inset);
                SUN_OP(129,8,far_edge,rect[k],rect[2+k]);
                SUN_OP(131,8,hi,far_edge,inset);
                SUN_OP(12,8,safe,317,43,local,lo,hi);
                SUN_OP(169,8,uv[k],use_local,safe,k?314:310);
            }
            uint32_t coords=next++;
            SUN_OP(80,12,coords,uv[0],uv[1]);
            SUN_OP(86,60,322,320,321);
            SUN_OP(90,8,323,322,coords,318,2,31);
            SUN_OP(80,20,324,323,31,31,31);
            SUN_OP(81,8,325,324,0);
        }
        memcpy(out+pos,code+i,n*sizeof(*out)); pos+=n; i+=n;
    }
#undef SUN_OP
    out[3]=next;
    *out_words=pos;
    return out;
}
