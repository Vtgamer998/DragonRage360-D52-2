/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_effect_pack.h"
#include "xb_character_effects.h"
#include "xb_resconv.h"
#include "dr_container.h"
#include "dr_texfile.h"
#include <stdint.h>
#include <float.h>
#ifdef malloc
#undef malloc
#endif
#include <stdlib.h>
#include <string.h>
static uint32_t common3_word(const uint8_t*p){return XbRes_Le32(p);}
#include "xb_common3_formats.inc"
typedef struct FlatFx {uint8_t*root,*technique[5];XbCharacterEffectsInfo info;} FlatFx;
static int charfx_entry(const uint8_t *p,size_t n,unsigned id,size_t *off,size_t *bytes){
 unsigned flags;int e=dr_pack_memory_entry(p,n,id-1,off,bytes,&flags);return e?e:flags?-896:0;
}
static int charfx_table(uint8_t *d,const uint8_t *s,size_t n,unsigned expected){
 if(n<12 || c3u(s)!=expected)return -896;
 size_t off,bytes;for(unsigned i=1;i<=expected;i++){int e=charfx_entry(s,n,i,&off,&bytes);if(e && e!=DR_NOT_FOUND)return e;}
 return c3fields(d,s,n,0,(expected+2)*4,4,0);
}
static int charfx_texture(uint8_t*d,const uint8_t*s,size_t n,XbCharacterEffectsInfo*info){
 (void)s;int e=dr_texfile_prepare(d,n,__BYTE_ORDER__==__ORDER_BIG_ENDIAN__);if(!e)info->textures++;return e;
}
static int charfx_emitter(uint8_t *d,const uint8_t *s,size_t n,unsigned first,XbCharacterEffectsInfo *info){
 unsigned count=c3u(s);if(!count || count>256)return -896;
 int e=charfx_table(d,s,n,count);if(e)return e;
 uint8_t type[257]={0},kind[257]={0},second[257]={0};size_t off,bytes;
 if(first<1 || first>count || charfx_entry(s,n,first,&off,&bytes) || bytes<64)return -896;
 const uint8_t *head=s+off;unsigned groups=head[4],parts=head[5],sum=0,res=0,pairs=0,next=first+1;uint32_t mask=0;
 if(groups>19 || parts>40 || ((64+32*groups+64*parts+63)&~63u)!=bytes)return -896;
 type[first]=2;
 for(unsigned g=0;g<groups;g++){
  const uint8_t *q=head+64+g*32;unsigned k=q[0],np=q[1],nr=q[2];
  if(k>=19 || mask&(1u<<k) || sum+np>parts || (k && (!nr || res+nr>32 || pairs+np>40)))return -896;
  mask|=1u<<k;sum+=np;if(!k)continue;res+=nr;pairs+=np;
  for(unsigned j=0;j<nr;j++){if(next>count)return -896;type[next++]=1;}
  for(unsigned j=0;j<np;j++)for(unsigned which=0;which<(k==2 || k==14?1u:2u);which++){
   if(next>count)return -896;
   type[next]=3;kind[next]=(uint8_t)k;second[next]=(uint8_t)which;next++;
  }
  for(unsigned j=sum-np;j<sum;j++)if(head[64+groups*32+j*64+1]>=nr)return -896;
 }
 if(mask!=c3u(head) || next!=count+1)return -896;
 for(unsigned id=first;id<=count;id++){
  e=charfx_entry(s,n,id,&off,&bytes);if(e)return e;uint8_t *out=d+off;const uint8_t *in=s+off;
  if(type[id]==1)e=charfx_texture(out,in,bytes,info);
  else if(type[id]==2){
   e=c3fields(out,in,bytes,0,4,4,0);
   if(!e)e=c3fields(out,in,bytes,8,32,4,1);
   if(!e)e=c3fields(out,in,bytes,40,64,4,1);
   for(unsigned j=0;!e && j<parts;j++)e=c3fields(out,in,bytes,64+groups*32+j*64+16,64+groups*32+j*64+52,4,1);
  }else if(type[id]==3){e=c3param(out,in,bytes,kind[id],second[id]);if(!e && kind[id]==14)info->sprite_packs++;}
  else return -896;
  if(e)return e;
 }
 info->emitter_sets++;return 0;
}
static int charfx_camera(uint8_t *d,const uint8_t *s,size_t n){
 if(n<36 || c3u(s)!=0x6e414d43 || !c3u(s+4))return -896;
 unsigned channels=c3u(s+12);size_t table=c3u(s+8),defaults=c3u(s+24),optional=c3u(s+28);
 if(channels>8 || table<36 || (table&3) || table>n || channels>(n-table)/16 || defaults<36 || (defaults&3) || defaults>n || n-defaults<32 || (optional && optional>=n))return -896;
 int e=c3fields(d,s,n,0,36,4,0);if(!e)e=c3fields(d,s,n,20,24,4,1);
 if(!e)e=c3fields(d,s,n,defaults,defaults+24,4,1);
 if(!e)e=c3fields(d,s,n,defaults+24,defaults+28,4,0);
 if(!e)e=c3fields(d,s,n,defaults+28,defaults+32,4,1);
 unsigned seen=0;
 for(unsigned i=0;!e && i<channels;i++){
  size_t row=table+i*16,keys=c3u(s+row+4);unsigned index=c3u(s+row),count=c3u(s+row+8);
  if(index>=8 || seen&(1u<<index) || !count || keys<36 || (keys&3) || keys>n || count>(n-keys)/32)return -896;
  seen|=1u<<index;e=c3fields(d,s,n,row,row+16,4,0);float previous=-FLT_MAX;
  for(unsigned j=0;!e && j<count;j++){
   size_t key=keys+j*32;uint32_t bits=c3u(s+key+4);float time;memcpy(&time,&bits,4);
   if(time<previous)return -896;
   previous=time;e=c3fields(d,s,n,key,key+4,4,0);
   if(!e)e=c3fields(d,s,n,key+4,key+12,4,1);
  }
 }
 return e;
}
static int flat(uint8_t*destination,const void*pack,size_t n,XbCharacterEffectsInfo*info){
 (void)c3standalone;
 size_t off,bytes;int e=charfx_table(destination,pack,n,6);if(e)return e;
 e=charfx_entry(pack,n,6,&off,&bytes);if(e)return e;
 const uint8_t*s=(const uint8_t*)pack+off;FlatFx storage={0},*fx=&storage;fx->root=destination+off;
 e=charfx_table(fx->root,s,bytes,11);if(e)goto fail;
 {size_t at,len;e=charfx_entry(s,bytes,1,&at,&len);if(e || len<8){e=-896;goto fail;}
  unsigned flags=c3u(s+at);static const unsigned bits[]={0,1,2,4,5},members[]={2,3,4,6,7};
  for(unsigned i=0;i<5;i++)if((flags&(1u<<bits[i])) && charfx_entry(s,bytes,members[i],&at,&len)){e=-896;goto fail;}
 }
 for(unsigned id=1;id<=11;id++){
  size_t at,len;e=charfx_entry(s,bytes,id,&at,&len);if(e==DR_NOT_FOUND){e=0;continue;}if(e)goto fail;
  uint8_t *d=fx->root+at;const uint8_t *q=s+at;
  if(id==1){
   /* The original flag accessor adds word 1 to this flag block itself. */
   if(len<8 || c3u(q+4) || c3u(q)&~0x7fu){e=-896;goto fail;}
   e=c3fields(d,q,len,0,8,4,0);
  }else if(id==2 || id==3 || id==4 || id==7)e=charfx_emitter(d,q,len,1,&fx->info);
  else if(id==6){
   e=charfx_table(d,q,len,2);size_t a,z;
   if(!e)e=charfx_entry(q,len,1,&a,&z);
   if(!e)e=c3sprite(d+a,q+a,z);
   if(!e)fx->info.sprite_packs++;
   if(!e)e=charfx_entry(q,len,2,&a,&z);
   if(!e)e=charfx_texture(d+a,q+a,z,&fx->info);
  }else if(id==8){
   e=charfx_table(d,q,len,4);
   for(unsigned j=1;!e && j<=4;j++){size_t a,z;e=charfx_entry(q,len,j,&a,&z);if(e==DR_NOT_FOUND){e=0;continue;}if(!e)e=charfx_camera(d+a,q+a,z);if(!e)fx->info.cameras++;}
  }else e=-896;
  if(e)goto fail;
  fx->info.manager_entries++;
 }
 /* Entries 1 and 2 are absent in the tested fighters. Entry 3 of three
  * technique packs is a fixed 704-float array (0 or -160 in these files).
  * Keep it in the owned arena, in native scalar order. Unknown variants stay
  * unbound rather than exposing unconverted little-endian values. */
 for(unsigned slot=0;slot<5;slot++){
  size_t at,len;e=charfx_entry(pack,n,slot+1,&at,&len);if(e)goto fail;
  const uint8_t *raw=(const uint8_t *)pack+at;
  size_t extra_at=0,extra_len=0;int compatible=1;
  for(unsigned id=1;id<=2;id++){
   size_t x,y;int present=charfx_entry(raw,len,id,&x,&y);
   if(present!=DR_NOT_FOUND){if(present){e=present;goto fail;}compatible=0;}
  }
  int extra=charfx_entry(raw,len,3,&extra_at,&extra_len);
  if(extra && extra!=DR_NOT_FOUND){e=extra;goto fail;}
  if(!extra && extra_len!=2816)compatible=0;
  if(!compatible){e=-896;goto fail;}
  uint8_t *native=destination+at;
  memcpy(native,raw,len);
  e=charfx_emitter(native,raw,len,4,&fx->info);
  if(!e && !extra){
   for(size_t j=0;j<extra_len;j+=4){
    uint32_t bits=c3u(raw+extra_at+j);
    if(bits!=0 && bits!=0xc3200000u){e=-896;break;}
   }
   if(!e)e=c3fields(native+extra_at,raw+extra_at,extra_len,0,extra_len,4,1);
  }
  if(e)goto fail;
  fx->technique[slot]=native;fx->info.prepared_technique_packs++;
 }
 *info=fx->info;return 0;
fail:return e;
}
int XbEffectPack_Prepare(void*p,size_t n,XbCharacterEffectsInfo*info){
 if(info)memset(info,0,sizeof(*info));
 if(!p||n<32||n>4*1024*1024||XbRes_Le32(p)!=6)return -896;
 uint8_t*d=malloc(n);if(!d)return -851;memcpy(d,p,n);XbCharacterEffectsInfo stats={0};int e=flat(d,p,n,&stats);
 if(!e){memcpy(p,d,n);if(info)*info=stats;}free(d);return e;
}
static int prepare(void*p,size_t n,int id){if(id!=0x599&&id!=0x5a3)return -896;return XbEffectPack_Prepare(p,n,NULL);}
int XbEffectPack_RegisterInitial(void){static const XbResCheckedRange ranges[]={{0x599,0x599,prepare,0},{0x5a3,0x5a3,prepare,0}};return XbRes_RegisterCheckedBatch(ranges,2);}
