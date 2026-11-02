/* SPDX-License-Identifier: AGPL-3.0-or-later
 * EPC K animation triples: TexFile, variable-size Vu1Node list, Vu1Track list.
 * Native headers/tracks, bounded canonical VIF payloads. No GPU readiness claim. */
static int stage_animation_vif(uint8_t *p,size_t bytes,uint32_t bits0,uint32_t bits1,int write){
 for(size_t at=0;at+4<=bytes;at+=4){
  uint32_t word=rd(p+at);if(word==0x70000000)return 0;
  if(word==0x6c038000){
   if(bytes-at<28)return DR_STAGE_BOUNDS;
   if(write){uint32_t values[2]={rd(p+at+20)|bits0,rd(p+at+24)|(bits1<<5)};
    for(unsigned j=0;j<2;j++)for(unsigned k=0;k<4;k++)p[at+20+j*4+k]=(uint8_t)(values[j]>>(8*k));
   }
  }
 }
 return DR_STAGE_FORMAT;
}
static int geom_animations(XbBattleStageCollision *c,const void *pack,size_t n){
 size_t off,bytes;unsigned flags;int e=dr_pack_memory_entry(pack,n,8,&off,&bytes,&flags);
 if(e==DR_NOT_FOUND)return 0;
 if(e || flags || bytes<16)return DR_STAGE_FORMAT;
 const uint8_t *s=(const uint8_t *)pack+off;uint8_t *d=c->render_pack+off;
 uint32_t count=rd(s+8);if(rd(s)!=0x4b435045 || rd(s+12) || count%3 || count>192 || count>(bytes-16)/4)return DR_STAGE_FORMAT;
 uint32_t previous=16+count*4;
 for(unsigned i=0;i<count;i++){
  uint32_t at=rd(s+16+i*4);if(at<previous || at>=bytes || (at&15))return DR_STAGE_BOUNDS;previous=at;
 }
 c->animation_textures=calloc(count/3?count/3:1,sizeof(void *));if(!c->animation_textures)return XB_STAGE_MEMORY;
 words(d,s,16+count*4);uint32_t relocated=1;memcpy(d+12,&relocated,4);
 for(unsigned triple=0;triple<count;triple+=3){
  size_t tex=rd(s+16+triple*4),node=rd(s+20+triple*4),track=rd(s+24+triple*4),end=triple+3<count?rd(s+28+triple*4):bytes;
  if(tex>=node || node>=track || track>=end || end>bytes)return DR_STAGE_BOUNDS;
  e=XbNativeTexture_Prepare(s+tex,node-tex,&c->animation_textures[c->animation_texture_count]);if(e)return e;
  void *texture=c->animation_textures[c->animation_texture_count++];memcpy(d+16+triple*4,&texture,4);
  if(track-node<16 || rd(s+node)<16 || rd(s+node)>track-node || (rd(s+node)&15))return DR_STAGE_BOUNDS;
  words(d+node,s+node,16);
  size_t at=node+rd(s+node),cursor=track;unsigned nodes=0,depth=0;
  for(;;){
   if(at>track || track-at<240 || cursor>end || end-cursor<16)return DR_STAGE_BOUNDS;
   const uint8_t *q=s+at;uint32_t size=rd(q),pop=rd(q+4),last=rd(q+8),has_mesh=rd(q+12);
   if(size<240 || (size&15) || size>track-at || pop>++depth || depth>64 || param_finite(q+16,56))return DR_STAGE_FORMAT;
   depth-=pop;if(++nodes>1024)return DR_STAGE_BOUNDS;
   words(d+at,q,240);
   if(has_mesh){e=stage_animation_vif(d+at+240,size-240,0,0,0);if(e)return e;}
   unsigned keys=geom_half(s+cursor+10);if((nodes==1 && keys && !geom_half(s+cursor+12)) || keys>(end-cursor-16)/32)return DR_STAGE_BOUNDS;
   /* +0xC is read as the low u16 length by StgModel_ResetAnims;
    * treating this mixed header as four u32s would zero that length on PPC. */
   param_halves(d+cursor+10,s+cursor+10,6);
   uint32_t previous_frame=0;
   for(unsigned k=0;k<keys;k++){
    size_t key=cursor+16+k*32;uint32_t frame=rd(s+key+12);
    if((k && frame<previous_frame) || param_finite(s+key,3) || param_finite(s+key+16,4))return DR_STAGE_NUMBER;
    previous_frame=frame;words(d+key,s+key,32);
   }
   cursor+=16+keys*32;
   if(last)break;
   at+=size;
  }
  void *native_node=d+node,*native_track=d+track;
  memcpy(d+20+triple*4,&native_node,4);memcpy(d+24+triple*4,&native_track,4);
  c->animation_nodes+=nodes;
 }
 StgDataA *params=(StgDataA *)c->parameters;
 for(int i=0;i<params->objCount;i++)if(params->objDefs[i].unk8>0 && (unsigned)params->objDefs[i].unk8>count/3)return DR_STAGE_BOUNDS;
 c->animation_models=count/3;return 0;
}
int XbStage_TryAnimationNodeFlags(void *address,uint32_t bits0,uint32_t bits1){
 if(!active || !active->render_live || !active->animation_models)return 0;
 uintptr_t base=(uintptr_t)active->render_pack,at=(uintptr_t)address;
 if(at<base || at-base>=active->render_bytes)return 0;
 uint8_t *node=address;size_t available=active->render_bytes-(size_t)(at-base);
 /* Every node and terminator was checked before original initialization. */
 while(available>=240){
  uint32_t size,last,has_mesh;memcpy(&size,node,4);memcpy(&last,node+8,4);memcpy(&has_mesh,node+12,4);
  if(size<240 || size>available)break;
  if(has_mesh && stage_animation_vif(node+240,size-240,bits0,bits1,1))break;
  if(last)return 1;
  node+=size;available-=size;
 }
 active->render_error=DR_STAGE_FORMAT;
 return 1; /* Never fall through to an unbounded native-u32 scan of LE VIF. */
}
