/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Native USA common 3. Unknown entries are never exposed as native.
 * Original scene indexes TABLE WORDS, so word 1 is pack entry 0. */
static uint32_t common3_word(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
#include "xb_common3_formats.inc"
static const unsigned c3sets[]={0xc,0x20,0x25,0x4c,0x66,0x73,0x88,0x91,0xa4,0xb9,0xf2,0x11a,0x137,0x158,0x192,0x1aa,0x1c0,0x1c1,0x1cb,0x1e6,0x1fc,0x217,0x23f,0x268};
static int c3entry(const void *pack,size_t n,unsigned index,const uint8_t **bytes,size_t *length){
 size_t off;unsigned flags;int e=dr_pack_memory_entry(pack,n,index-1,&off,length,&flags);
 if(e)return e;
 if(flags)return -890;
 *bytes=(const uint8_t *)pack+off;return 0;
}
int XbBattleRuntime_AttachCommon3(XbBattleRuntime *r,const void *pack,size_t n,XbCommon3Info *info){
 if(info)memset(info,0,sizeof(*info));
 if(!r || active!=r || r->managers || r->common3 || !pack || n<12 || n>16*1024*1024)return -891;
 const uint8_t *p=pack;unsigned count=common3_word(p);
 if(count!=637)return -890;
 void **entries=calloc((size_t)count+1,sizeof(*entries));if(!entries)return -851;
 uint8_t type[638]={0},kind[638]={0},second[638]={0};
 unsigned textures=0,images=0,prepared=0,diagnostic_id=0;int error=0;
 for(unsigned set=0;set<sizeof(c3sets)/sizeof(*c3sets);set++){
  unsigned id=c3sets[set];diagnostic_id=id;const uint8_t *s;size_t bytes;
  error=c3entry(pack,n,id,&s,&bytes);if(error)goto fail;
  if(bytes<64){error=-890;goto fail;}
  unsigned groups=s[4],parts=s[5],sum=0,res=0,pairs=0,next=id+1;uint32_t mask=0;
  if(groups>19 || parts>40 || ((64+32*groups+64*parts+63)&~63u)!=bytes){error=-890;goto fail;}
  type[id]=2;
  for(unsigned g=0;g<groups;g++){
   const uint8_t *q=s+64+g*32;unsigned k=q[0],np=q[1],nr=q[2];
   if(k>=19 || mask&(1u<<k) || sum+np>parts || (k && (!nr || res+nr>32 || pairs+np>40))){error=-890;goto fail;}
   mask|=1u<<k;sum+=np;if(!k)continue;
   res+=nr;pairs+=np;
   for(unsigned j=0;j<nr;j++){if(next>count || type[next]){error=-890;goto fail;}type[next++]=1;}
   for(unsigned j=0;j<np;j++)for(unsigned which=0;which<(k==2 || k==14?1u:2u);which++){
    if(next>count || type[next]){error=-890;goto fail;}
    type[next]=k==14?4:3;kind[next]=(uint8_t)k;second[next]=(uint8_t)which;next++;
   }
   unsigned first=sum-np;
   for(unsigned j=0;j<np;j++)if(s[64+groups*32+(first+j)*64+1]>=nr){error=-890;goto fail;}
  }
  if(mask!=c3u(s)){error=-890;goto fail;}
 }
 for(unsigned id=1;id<=count;id++){
  diagnostic_id=id;const uint8_t *s;size_t bytes;error=c3entry(pack,n,id,&s,&bytes);if(error)goto fail;
  if(!type[id]){
   if(id==0x239 || id==0x23b)type[id]=5;
   else if(id==1 || id==4 || id==7 || id==0x23d || id==0x23e || (id>=0x24e && id<=0x25b) || id==0x25d || id==0x25f || id==0x260 || (id>=0x261 && id<=0x267)){
    if(id!=0x259)type[id]=6;
   }
  }
  if(!type[id] && bytes>=32 && c3u(s+4)==8 && c3u(s)>0 && c3u(s)<=256)type[id]=1;
  if(type[id]==1){
   error=XbNativeTexture_Prepare(s,bytes,&entries[id]);if(error)goto fail;
   textures++;images+=c3u(s);prepared++;continue;
  }
  uint8_t *d=malloc(bytes);if(!d){error=-851;goto fail;}memcpy(d,s,bytes);
  if(type[id]==2){
   error=c3fields(d,s,bytes,0,4,4,0);
   if(!error)error=c3fields(d,s,bytes,8,32,4,1);
   if(!error)error=c3fields(d,s,bytes,40,64,4,1);
   for(unsigned j=0;!error && j<s[5];j++)error=c3fields(d,s,bytes,64+s[4]*32+j*64+16,64+s[4]*32+j*64+52,4,1);
  }else if(type[id]==3 || type[id]==4)error=c3param(d,s,bytes,kind[id],second[id]);
  else {error=c3standalone(d,s,bytes,id);if(!error)type[id]=(id==0x239 || id==0x23b)?5:6;}
  if(error){free(d);if(type[id])goto fail;error=0;continue;}
  entries[id]=d;prepared++;
 }
 r->common3=entries;r->common3_count=count;r->common3_textures=textures;r->common3_images=images;r->common3_prepared=prepared;memcpy(r->common3_type,type,sizeof(type));
 if(info)*info=(XbCommon3Info){.entries=count,.textures=textures,.images=images,.unprepared=count-prepared,.prepared=prepared};
 return 0;
fail:
 extern void xsdk_log_write(const char *,...);xsdk_log_write("common3 preparation refused: id=%u type=%u error=%d",diagnostic_id,type[diagnostic_id],error);
 for(unsigned i=1;i<=count;i++)XbNativeTexture_Free(entries[i]);
 free(entries);return error;
}
int XbBattleRuntime_Common3Type(int index){return active && index>0 && index<=637?active->common3_type[index]:0;}
int XbBattleRuntime_TryCommon3Entry(int index,void **out){
 if(!out || !XbBattleRuntime_HasResources() || !active->common3)return 0;
 *out=NULL;if(index>0 && (unsigned)index<=active->common3_count)*out=active->common3[index];return 1;
}
