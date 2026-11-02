/* SPDX-License-Identifier: AGPL-3.0-or-later
 * USA common effect formats, derived from the original typed consumers.
 * Byte fields/padding stay opaque. Scalar arrays are converted individually.
 * IDs are TABLE WORD indices, not zero-based pack entry indices. */
#include <math.h>
static uint32_t c3u(const uint8_t *p){return common3_word(p);}
static unsigned c3h(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
static int c3fields(uint8_t *d,const uint8_t *s,size_t n,size_t at,size_t end,unsigned width,int floating){
 if(end>n || at>end || (end-at)%width)return -890;
 for(size_t j=at;j<end;j+=width){
  if(width==2){uint16_t v=(uint16_t)c3h(s+j);memcpy(d+j,&v,2);}
  else {uint32_t v=c3u(s+j);if(floating){float f;memcpy(&f,&v,4);if(!isfinite(f))return -890;}memcpy(d+j,&v,4);}
 }
 return 0;
}
#define C3W(a,b) do{int error=c3fields(d,s,n,(a),(b),4,0);if(error)return error;}while(0)
#define C3F(a,b) do{int error=c3fields(d,s,n,(a),(b),4,1);if(error)return error;}while(0)
#define C3H(a,b) do{int error=c3fields(d,s,n,(a),(b),2,0);if(error)return error;}while(0)
/* EftSprPack/EftSprLayerDef/EftSprKey, eft_ae.h. */
static int c3sprite(uint8_t *d,const uint8_t *s,size_t n){
 if(n<32 || c3u(s)!=0x30303056 || !s[4] || s[4]>16)return -890;
 size_t keys=c3u(s+8),next=keys;
 if(keys<32+(size_t)s[4]*32 || keys>n || (keys&15))return -890;
 C3W(0,4);C3W(8,12);
 for(unsigned i=0;i<s[4];i++){
  size_t at=32+i*32;unsigned count=s[at+4];
  if(!count || s[at+5]>=count || s[at+6]>=count || count>(n-next)/64)return -890;
  C3H(at+8,at+12);C3W(at+16,at+28);C3H(at+28,at+32);
  uint32_t previous=0;
  for(unsigned k=0;k<count;k++){
   size_t key=next+k*64;uint32_t frame=c3u(s+key+52);
   if(k && frame<previous)return -890;
   previous=frame;C3F(key,key+48);C3W(key+52,key+64);
  }
  next+=(size_t)count*64;
 }
 return 0;
}
/* EftMeshData, EftMeshGroup/Tri/Vtx, eft_ad.h. Offsets are bytes. */
static int c3mesh(uint8_t *d,const uint8_t *s,size_t n){
 if(n<16 || c3u(s)!=0x53454244)return -890;
 size_t groups=c3u(s+4),tri=c3u(s+8),vtx=c3u(s+12);
 if(!groups || groups>(n-16)/16 || tri<16+groups*16 || vtx<tri || vtx>n || (tri&15) || (vtx&15))return -890;
 size_t triangles=(vtx-tri)/16,vertices=(n-vtx)/48;
 C3W(0,16+groups*16);
 for(size_t i=0;i<groups;i++){
  size_t at=16+i*16,count=c3u(s+at),first=c3u(s+at+8);
  if(first>triangles || count>triangles-first)return -890;
  for(size_t j=first;j<first+count;j++){
   for(unsigned k=0;k<3;k++)if(c3u(s+tri+j*16+k*4)>=vertices)return -890;
   C3W(tri+j*16,tri+j*16+16);
  }
 }
 C3F(vtx,vtx+vertices*48);return 0;
}
/* Emitters dispatched by eft_h.c/eft_i.c. Kind comes from the owning set. */
static int c3param(uint8_t *d,const uint8_t *s,size_t n,unsigned kind,unsigned second){
 switch(kind){
 case 5: /* EftUPtclDef/Def2, eft_u.h (also eft_v.h's runtime view). */
  if(second){C3F(0,0x114);}else{C3W(0,4);C3F(0x10,0x1c);C3H(0x1c,0x28);C3F(0x28,0x2b0);}break;
 case 9: /* EftQuadDef/Def2, eft_y.h. */
  if(second){C3F(0,0x12c);}else{C3F(0,0x224);C3W(0x234,0x238);}break;
 case 10: /* EftPart10Def/Def2, eft_x.h. */
  if(second){C3F(0,0x12c);}else{C3F(0,0x1f4);C3W(0x1f4,0x1f8);C3F(0x1fc,0x20c);}break;
 case 16: /* EftBillDef + EftLineDef/Def2, eft_z.h/eft_y.h. */
  if(second){C3F(0,0x84);}else{C3W(0,4);C3F(8,0x90);}break;
 case 17: /* EftRbnPrm/EftRbnAnim: EftRibbon_LoadKey reads these as floats. */
  if(second){C3F(0,0xb4);}else{C3W(0,4);C3F(8,0x60);}break;
 case 18: /* EftArcParam/Keys, eft_s.h. */
  if(second){C3F(0,0x114);}else{C3W(0,4);C3F(0xc,0x1e8);}break;
 case 2: /* EftRaysDef: ten 0x78-byte rays with byte RGBA keys. */
  if(second || n<0x4b8 || c3u(s+0x4b0)>10)return -890;
  for(unsigned i=0;i<10;i++){
   size_t at=i*0x78;
   for(unsigned k=0;k<3;k++)C3F(at+k*16+4,at+k*16+16);
   C3F(at+0x30,at+0x70);C3H(at+0x70,at+0x72);
  }
  C3W(0x4b0,0x4b8);break;
 case 14:if(second)return -890;return c3sprite(d,s,n);
 default:return -890;
 }
 return 0;
}
/* Standalone consumers in eft_m/n/p/q/t/aa/ab. Unreferenced 0x23D/E
 * are deliberately not guessed. No mapped set depends on those entries. */
static int c3standalone(uint8_t *d,const uint8_t *s,size_t n,unsigned id){
 /* These two 64-byte records sit between the disc meshes and the 0x23F
  * emitter set. No decompiled consumer names the records; the known USA
  * variant has finite scalar words and one integer flag at word 14. */
 if(id==0x23d){if(n!=64 || c3u(s+56)>1)return -890;C3F(0,56);C3W(56,64);return 0;}
 if(id==0x23e){if(n!=64)return -890;C3F(0,64);return 0;}
 if(id==1){C3F(0,0x410);C3W(0x410,0x500);C3F(0x510,0x1070);return 0;}
 if(id==4){C3W(0,8*0x14);return 0;} /* EftBoltPair[8]. */
 if(id==7){C3F(0,0x490);C3W(0x490,0x580);C3F(0x580,0x620);C3F(0x620,0xa30);return 0;}
 if(id==0x239 || id==0x23b)return c3mesh(d,s,n);
 if(id==0x24e){C3F(0,7*16);return 0;}
 if(id>=0x24f && id<=0x258){C3F(0,0x1c);C3F(0x20,0x40);C3W(0x40,0x48);return 0;}
 if(id==0x25a){C3F(0,0x7c);return 0;}
 if(id==0x25b){C3F(0,0x74);return 0;}
 if(id==0x25d || id==0x25f || id==0x260)return c3param(d,s,n,5,0);
 if(id>=0x261 && id<=0x267)return c3param(d,s,n,5,1);
 return -890;
}
#undef C3W
#undef C3F
#undef C3H
