/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Bounded adaptation of upstream hle_program0/1, revision 3ca419c.
 * Canonical LE VU memory -> native float shader inputs. Unknown layouts retain
 * the interpreter. Program identities were measured from SLUS-21678 uploads.
 */
static DrVuModelRenderer model_renderer;
static void *model_opaque;
static int model_dirty=1,model_kind,model_active,model_since_vm;
static uint32_t model_draws,model_fallbacks;
void dr_vu1_model_renderer(DrVuModelRenderer cb,void *opaque){
    model_renderer=cb;model_opaque=opaque;model_active=model_since_vm=0;
}
void dr_vu1_model_profile(uint32_t *draws,uint32_t *fallbacks){
    if(draws)*draws=model_draws;
    if(fallbacks)*fallbacks=model_fallbacks;
}
static int model_program(void){
    if(model_dirty){
        model_kind=0;
        if(sProgSize==127 || sProgSize==124 || sProgSize==99 || sProgSize==101){
            uint64_t h=14695981039346656037ull;
            for(unsigned i=0;i<sProgSize*8;i++)h=(h^vu.micro[i])*1099511628211ull;
            if(sProgSize==127 && h==0x931162449bb92b6full)model_kind=1;
            if(sProgSize==124 && h==0x1a6a4fafbcad4df0ull)model_kind=2;
            if(sProgSize==99 && h==0xc81f82b235544790ull)model_kind=3;
            if(sProgSize==101 && h==0x1a9bf60fb69a640full)model_kind=4;
        }
        model_dirty=0;
    }
    return model_kind;
}
/* Original programs 2a/2b: untextured flat silhouettes, same model chains.
 * Only the second primitive tag is consumed. Context comes from effective
 * PRMODE, selected by the renderer after applying PRIM. */
static int model_shadow_batch(int kind){
    unsigned top=vu.top&1023u;if(top>1018)return 0;
    const uint8_t *env=vu.mem+top*16;
    uint64_t head=stage_u64(env),tag=stage_u64(env+64),regs=stage_u64(env+72);
    unsigned n=tag&0x7fff,prim=(unsigned)(tag>>47)&0x7ff;
    if((head&0x7fff)!=1 || ((head>>58)&3) || (head>>60)!=1 || stage_u64(env+8)!=14 ||
       (stage_u64(env+24)!=6 && stage_u64(env+24)!=7) ||
       ((tag>>58)&3) || (tag>>60)!=3 || (regs!=0x412 && regs!=0x512) ||
       (prim&7)!=4 || (prim&(0x20|0x100)) || n<3 || n>80 || top+5+n*3>1024)return 0;
    float c[108]={0},v[80*12];
    for(unsigned i=0;i<40;i++)c[i]=stage_float(vu.mem+i*4);
    for(unsigned i=0;i<16;i++)c[56+i]=stage_float(vu.mem+(44+i)*4);
    for(unsigned i=0;i<4;i++)c[88+i]=stage_float(vu.mem+(40+i)*4);
    for(unsigned i=0;i<108;i++)if(!isfinite(c[i]))return 0;
    for(unsigned i=0;i<n;i++)for(unsigned j=0;j<12;j++){
        v[i*12+j]=(j>=4 && j<=7) || j==11?0:stage_float(env+80+(i*12+j)*4);
        if(!isfinite(v[i*12+j]))return 0;
    }
    int e=model_renderer(model_opaque,env,prim,v,n,c,2);
    if(e)return e<0?e:DR_VU_INVALID;
    model_draws++;model_since_vm=1;vu.pc=kind==3?97:99;return 1;
}
static int model_batch(int kind){
    if(kind>=3)return model_shadow_batch(kind);
    uint32_t top=vu.top&1023u;
    if(top>1018)return 0;
    const uint8_t *env=vu.mem+top*16;
    uint64_t header=stage_u64(env),tags[2];uint32_t prim[2];
    if((header&0x7fff)!=1 || ((header>>58)&3) || (header>>60)!=1 || stage_u64(env+8)!=14)return 0;
    for(unsigned l=0;l<2;l++){
        tags[l]=stage_u64(env+48+l*16);prim[l]=(uint32_t)(tags[l]>>47)&0x7ff;
        uint64_t regs=stage_u64(env+56+l*16);
        if(((tags[l]>>58)&3) || (tags[l]>>60)!=3 || (regs!=0x412 && regs!=0x512) ||
           (prim[l]&7)!=4 || (prim[l]&(0x20|0x100)))return 0;
        const uint8_t *tex=kind==2 && l==1?vu.mem+26*16:env+16+l*16;
        uint64_t addr=stage_u64(tex+8);
        if(addr!=6 && addr!=7)return 0;
    }
    uint32_t n=(uint32_t)tags[0]&0x7fff;
    if(n<3 || n>256 || ((uint32_t)tags[1]&0x7fff)!=n || top+5+n*3>1024)return 0;
    if(kind==2 && !(vu_le(env+108,4)&65535)){
        model_since_vm=1;vu.pc=15;return 1;
    }
    float vertices[256*12],constants[108];
    for(unsigned i=0;i<108;i++)constants[i]=stage_float(vu.mem+i*4);
    /* 24..26 can contain integer tags, never shader float operands. */
    for(unsigned i=0;i<96;i++)if(!isfinite(constants[i]))return 0;
    for(unsigned i=0;i<n*12;i++)vertices[i]=stage_float(env+80+i*4);
    for(unsigned i=0;i<n;i++)for(unsigned j=0;j<12;j++){
        if(j==7 || j==11){vertices[i*12+j]=0;continue;} /* unused marker / padding */
        if(!isfinite(vertices[i*12+j]))return 0;
    }
    /* Both layers are preflighted before the renderer can mutate GS state. */
    for(unsigned l=0;l<2;l++){
        uint8_t packet[32];memcpy(packet,env,16);
        memcpy(packet+16,kind==2 && l==1?vu.mem+26*16:env+16+l*16,16);
        int e=model_renderer(model_opaque,packet,prim[l],vertices,n,constants,kind==2 && l==1?5:l);
        if(e)return e<0?e:DR_VU_INVALID;
    }
    model_draws+=2;model_since_vm=1;vu.pc=kind==1?36:15;return 1;
}
