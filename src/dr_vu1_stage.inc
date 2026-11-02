/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Bounded native bridge for upstream hle_program4. Code size + FNV64 is a
 * renderer classifier, not authentication; bounds checks are independent.
 */
static DrVuStageRenderer stage_renderer;
static void *stage_opaque;
static int stage_active,stage_header,stage_dirty=1,stage_match,stage_since_vm;
static uint32_t stage_draws,stage_fallbacks;
void dr_vu1_stage_renderer(DrVuStageRenderer cb,void *opaque){
    stage_renderer=cb;stage_opaque=opaque;stage_active=stage_header=stage_since_vm=0;
}
void dr_vu1_profile(uint32_t *instructions,uint32_t *draws,uint32_t *fallbacks){
    if(instructions)*instructions=sStatInstr;
    if(draws)*draws=stage_draws;
    if(fallbacks)*fallbacks=stage_fallbacks;
}
static int stage_program(void){
    if(stage_dirty){uint64_t h=14695981039346656037ull;
        for(unsigned i=0;i<3192;i++)h=(h^vu.micro[i])*1099511628211ull;
        stage_match=sProgSize==399 && h==0xbaf79ad006a2a128ull;stage_dirty=0;
    }return stage_match;
}
static float stage_float(const uint8_t *p){uint32_t u=vu_le(p,4);float f;memcpy(&f,&u,4);return f;}
static uint64_t stage_u64(const uint8_t *p){return vu_le(p,4)|((uint64_t)vu_le(p+4,4)<<32);}
static int stage_batch(void){
    uint32_t top=vu.top&1023u;if(top>1021)return 0;
    const uint8_t *env=vu.mem+top*16,*p=env+32;
    uint64_t tag=stage_u64(p),envtag=stage_u64(env);
    uint32_t n=(uint32_t)tag&0x7fff,prim=(uint32_t)(tag>>47)&0x7ff;
    unsigned descriptor=vu_le(env+8,4)&15,address=vu_le(env+24,4),regs=vu_le(p+8,4);
    if(n<3 || n>256 || top+3+n*3>1024 || (envtag&0x7fff)!=1 ||
       ((envtag>>58)&3) || (envtag>>60)!=1 || (descriptor!=14 && descriptor!=15) ||
       (descriptor==14 && ((address!=6 && address!=7 && address!=0x3f) || vu_le(env+28,4))) ||
       ((tag>>58)&3) || (tag>>60)!=3 || (regs!=0x412 && regs!=0x512) || vu_le(p+12,4) ||
       (prim&7)!=4 || (prim&(0x20|0x100)))return 0;
    float vertices[256*12],matrix[16];
    for(unsigned i=0;i<16;i++){matrix[i]=stage_float(vu.mem+i*4);if(!isfinite(matrix[i]))return 0;}
    for(unsigned i=0;i<n*12;i++)vertices[i]=stage_float(env+48+i*4);
    for(unsigned i=0;i<n;i++)for(unsigned j=0;j<10;j++)if(!isfinite(vertices[i*12+j]))return 0;
    int e=stage_renderer(stage_opaque,env,prim,vertices,n,matrix);
    if(e)return e<0?e:DR_VU_INVALID;
    stage_draws++;stage_since_vm=1;vu.pc=21;return 1;
}
