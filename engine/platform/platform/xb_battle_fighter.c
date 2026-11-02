/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "port_compat.h"
#undef malloc
#include "xb_battle_fighter.h"
#include "xb_fighter_tables.h"
#include "xb_model_native.h"
#include "dr_body_model.h"
#include "dr_container.h"
#include "dr_texfile.h"
#include "dr_face_vif.h"
#include "battle/bobj_a.h"
/* The two pinned partial views give different documentary NODE_MAX values.
 * Each header must parse with its own value; this module's storage uses the
 * explicit reviewed 162-entry object pool, never either macro. */
#undef BOBJ_NODE_MAX
#include "battle/bobj_b.h"
#undef BOBJ_NODE_MAX
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
typedef struct XbNativeNode {BobjPart part;Vec4 previous;} XbNativeNode;
_Static_assert(sizeof(XbNativeNode)==0xe0,"native object node pool stride");
typedef struct XbCachedImage {unsigned family,index;DrTexFileImage image;void *rgba;} XbCachedImage;
struct XbBattleFighter {
 XbCharacterEffects *effects;
 BobjObj template_obj;XbNativeModel *native_model;int model_lifecycle,model_side;size_t motions_size;
 BobjObj owned_obj;BobjObj *obj;int registered_id;BobjBone *bones;DrBodyPart *parts;
 uint8_t *model_pack,*motions_pack;size_t model_size,mesh_offset;
 XbCachedImage *images;unsigned image_count;int pose_valid,matrices_valid;
 void *decoded[BOBJ_ANIM_MAX];size_t decoded_size[BOBJ_ANIM_MAX];
 XbNativeNode node[162] __attribute__((aligned(16)));unsigned char present[162];
 BobjBodyData *body_data;unsigned body_count;
 BobjModelHdr model_header;void *tables[XB_FIGHTER_TABLES];size_t table_bytes[XB_FIGHTER_TABLES];
 float common_reference[213];int common_reference_ready;XbFighterPrepared stats;
};
#if UINTPTR_MAX==UINT32_MAX
_Static_assert(sizeof(BobjWork)==0x1a0c0 && sizeof(BobjBodyPart)==0x1d0,"fighter work/body ABI");
_Static_assert(sizeof(BobjObj)==0x1670,"BobjObj ABI");
_Static_assert(sizeof(BobjBone)==64,"BobjBone ABI");
_Static_assert(sizeof(BobjAnimLayer)==0x98,"BobjAnimLayer ABI");
#endif
extern void BtlObjAnim_PlayModel(BobjObj *,s32,s32);
extern void BtlObjAnim_Step(BobjObj *);
static XbBattleFighter *owners[16];
int XbFighter_ChainSpan(const void *ptr,size_t *bytes){
 if(bytes)*bytes=0;
 if(!ptr || !bytes)return 0;
 for(unsigned i=0;i<16;i++)if(owners[i] && owners[i]->model_lifecycle && owners[i]->registered_id>=0 &&
    XbNativeModel_ChainSpan(owners[i]->native_model,ptr,bytes))return 1;
 return 0;
}
static XbBattleFighter *find_owner(void *p){for(unsigned i=0;i<16;i++)if(owners[i] && owners[i]->obj==p)return owners[i];return NULL;}
int XbFighter_ResolveNode(void *obj,int id,void **out){XbBattleFighter *f=find_owner(obj);if(!f)return 0;if(f->model_lifecycle){*out=NULL;if(id>=0 && id<162)memcpy(out,(unsigned char *)obj+0xd6c+(size_t)id*4,4);return 1;}*out=(id>=0 && id<162 && f->present[id])?&f->node[id].part:NULL;return 1;}
int XbFighter_LoadDecoded(void *obj,void *buf,int id,int *size,void **out){
 XbBattleFighter *f=find_owner(obj);if(!f)return 0;*out=NULL;*size=0;
 if(id>=BOBJ_ANIM_MAX && id<BOBJ_ANIM_MAX+8){*out=f->obj->mdl.modelAnims[id-BOBJ_ANIM_MAX];*size=*out?1:0;return 1;}
 if(id<0 || id>=BOBJ_ANIM_MAX || !buf || !f->decoded[id])return 1;
 memcpy(buf,f->decoded[id],f->decoded_size[id]);*size=(int)f->decoded_size[id];*out=buf;return 1;
}
static int motion(const void *pack,size_t size,unsigned index,int compressed,void **out,int *empty,size_t *bytes){
 size_t off,n,written=0;*out=NULL;*empty=0;if(bytes)*bytes=0;
 int e=dr_pack_memory_entry(pack,size,index,&off,&n,NULL);
 if(e==DR_NOT_FOUND){*empty=1;return 0;}if(e)return e;
 void *p=malloc(0xc000);if(!p)return -851;
#if __BYTE_ORDER__==__ORDER_BIG_ENDIAN__
 const int be=1;
#else
 const int be=0;
#endif
 e=dr_motion_from_memory((const uint8_t *)pack+off,n,compressed,be,p,0xc000,&written);
 if(e==DR_LOAD_EMPTY){free(p);*empty=1;return 0;}
 if(e){free(p);return e;}
 if(written<0x94){free(p);return DR_LOAD_ANIMATION;}
 void *small=realloc(p,written);*out=small?small:p;if(bytes)*bytes=written;return 0;
}
void XbBattleFighter_FreeModelAnimation(XbBattleFighter *f){
 if(!f)return;
 if(f->registered_id>=0 && XbBattleFighter_UnregisterObject(f))return;
 for(unsigned i=0;i<16;i++)if(owners[i]==f)owners[i]=NULL;
 for(unsigned i=0;i<f->image_count;i++)free(f->images[i].rgba);
 for(unsigned i=0;i<XB_FIGHTER_TABLES;i++)free(f->tables[i]);
 XbCharacterEffects_Free(f->effects);XbNativeModel_Free(f->native_model);free(f->images);free(f->model_pack);free(f->motions_pack);
 free(f->obj->work);free(f->body_data);
 for(unsigned i=0;i<BOBJ_ANIM_MAX;i++)free(f->decoded[i]);
 for(unsigned i=0;i<8;i++)free(f->obj->mdl.modelAnims[i]);
 free(f->bones);free(f->parts);free(f);
}
int XbBattleFighter_AttachEffectManagers(XbBattleFighter *f,const void *pack,size_t bytes,XbCharacterEffectsInfo *info){
 if(!f || f->effects || f->registered_id>=0)return -896;
 return XbCharacterEffects_Prepare(pack,bytes,&f->effects,info);
}
int XbBattleFighter_PrepareModelAnimation(const void *model,size_t model_size,const void *motions,size_t motions_size,XbBattleFighter **out,XbFighterPrepared *stats){
 if(!out || !stats)return DR_MODEL_INVALID;
 *out=NULL;memset(stats,0,sizeof(*stats));
 if(!model || !motions || model_size<24 || motions_size<24 || model_size>16*1024*1024 || motions_size>16*1024*1024 || dr_resource_le32(motions)<BOBJ_ANIM_MAX || dr_resource_le32(model)<36)return DR_MODEL_INVALID;
 size_t off,n,bad;int e=dr_pack_memory_entry(model,model_size,2,&off,&n,NULL);if(e)return e;
 XbBattleFighter *f=calloc(1,sizeof(*f));if(!f)return -851;f->obj=&f->owned_obj;f->registered_id=-1;
 f->parts=calloc(256,sizeof(*f->parts));if(!f->parts){e=-851;goto fail;}
 DrBodyModel body;e=dr_body_model_read((const uint8_t *)model+off,n,f->parts,256,&body,&bad);if(e)goto fail;
 f->obj->work=calloc(1,sizeof(*f->obj->work));if(!f->obj->work){e=-851;goto fail;}
 f->bones=calloc(body.parts,sizeof(*f->bones));if(!f->bones){e=-851;goto fail;}
 for(unsigned i=0;i<body.parts;i++){
  DrBodyPart *p=&f->parts[i];BobjBone *b=&f->bones[i];
  if(p->node>=162 || f->present[p->node]){e=DR_MODEL_INVALID;goto fail;}
  b->next=sizeof(*b);b->pop=p->pop;b->last=p->last;b->id=p->node;b->unk08=p->enabled;b->unk0C=(s32)dr_resource_le32((const uint8_t *)model+off+p->record_offset+12);
  f->present[p->node]=1;f->node[p->node].part.id=p->node;f->node[p->node].part.rot.w=1.0f;f->node[p->node].part.fromRot.w=1.0f;
  memcpy(&f->node[p->node].part.pos,p->rest,16);memcpy(&f->node[p->node].part.fromPos,p->rest,16);
  memcpy(&b->origin,p->pivot_a,16);memcpy(&b->unk20,p->pivot_b,16);memcpy(&b->rest,p->rest,16);
 }
 f->model_pack=malloc(model_size);if(!f->model_pack){e=-851;goto fail;}memcpy(f->model_pack,model,model_size);f->model_size=model_size;f->mesh_offset=off;
 f->stats.bones=body.parts;f->obj->mdl.nodes=f->bones;
 f->motions_size=motions_size;f->motions_pack=malloc(motions_size);if(!f->motions_pack){e=-851;goto fail;}memcpy(f->motions_pack,motions,motions_size);
 for(unsigned i=0;i<BOBJ_ANIM_MAX;i++){
  int empty;e=motion(motions,motions_size,i,1,&f->decoded[i],&empty,&f->decoded_size[i]);if(e)goto fail;
  if(empty)f->stats.empty_motions++;else {size_t at,bytes;e=dr_pack_memory_entry(f->motions_pack,motions_size,i,&at,&bytes,NULL);if(e)goto fail;
   f->obj->mdl.anims[i]=f->motions_pack+at; /* actual compressed source, never a decoded buffer */
   f->stats.motions++;}
 }
 /* Original BtlObj_BindTables: file0 entries i+0x1D, ONE-based. */
 for(unsigned i=0;i<8;i++){
  int empty;void *p;e=motion(model,model_size,0x1c+i,0,&p,&empty,NULL);if(e)goto fail;
  f->obj->mdl.modelAnims[i]=p;if(empty)f->stats.empty_model_motions++;else f->stats.model_motions++;
 }
 f->model_header.flags=dr_resource_le32((const uint8_t *)model+off+12);
 for(unsigned i=0;i<4;i++){uint32_t bits=dr_resource_le32((const uint8_t *)model+off+24+i*4);memcpy(&f->model_header.size[i],&bits,4);}
 static const unsigned table_entries[]={17,18,19,20,23,24,26,27};
 for(unsigned i=0;i<XB_FIGHTER_TABLES;i++){
  size_t po,pn;e=dr_pack_memory_entry(model,model_size,table_entries[i],&po,&pn,NULL);if(e)goto fail;
  f->tables[i]=malloc(pn);if(!f->tables[i]){e=-851;goto fail;}
#if __BYTE_ORDER__==__ORDER_BIG_ENDIAN__
  const int table_be=1;
#else
  const int table_be=0;
#endif
  e=XbFighterTable_Convert(i,(const uint8_t *)model+po,pn,table_be,f->tables[i],pn,&f->table_bytes[i]);if(e)goto fail;
 }
 BobjParam *param=f->tables[XB_FIGHTER_PARAM];
 if(!isfinite(param->height) || param->height<=0 || !isfinite(param->radius)){e=DR_MODEL_INVALID;goto fail;}
 /* These three native reader pointers occupy mdl+908,+90C,+910 on PPC32.
  * The 64-bit host's partial views do not share the original layout. */
#if UINTPTR_MAX==UINT32_MAX
 /* Original object +920..938: attack, ki, movement, supers, skills, AI,
  * camera. These are separate native views; source pack remains canonical. */
 for(unsigned i=1;i<XB_FIGHTER_TABLES;i++)memcpy(f->obj->mdl.unk908+(i-1)*4,&f->tables[i],4);
#endif
 size_t bo,bn;e=dr_pack_memory_entry(model,model_size,1,&bo,&bn,NULL);if(e || bn<16){e=DR_MODEL_INVALID;goto fail;}
 unsigned count=0;for(;;){size_t at=16+(size_t)count*0x1d0;if(at>bn || bn-at<0x1d0 || count>=162){e=DR_MODEL_INVALID;goto fail;}uint32_t flags=dr_resource_le32((const uint8_t *)model+bo+at);count++;if(flags&1)break;}
 f->body_data=calloc(1,offsetof(BobjBodyData,part)+(size_t)count*sizeof(BobjBodyPart));if(!f->body_data){e=-851;goto fail;}f->body_count=count;
 f->body_data->unk00=(s32)dr_resource_le32((const uint8_t *)model+bo);
 uint32_t bits=dr_resource_le32((const uint8_t *)model+bo+4);memcpy(&f->body_data->unk04,&bits,4);
 if(!isfinite(f->body_data->unk04)){e=DR_MODEL_INVALID;goto fail;}
 for(unsigned i=0;i<count;i++){
  const uint8_t *src=(const uint8_t *)model+bo+16+(size_t)i*0x1d0;BobjBodyPart *dst=&f->body_data->part[i];
  dst->flags=dr_resource_le32(src);dst->node=(s32)dr_resource_le32(src+4);
  if(dst->node<0 || dst->node>=162 || !f->present[dst->node]){e=DR_MODEL_INVALID;goto fail;}
  for(unsigned j=0;j<4;j++){uint32_t v=dr_resource_le32(src+16+j*4);memcpy((uint8_t *)&dst->offset+j*4,&v,4);}
  for(unsigned j=0;j<32;j++){uint32_t v=dr_resource_le32(src+0xd0+j*4);memcpy((uint8_t *)dst->box.local+j*4,&v,4);}
  const float *local=(const float *)dst->box.local;for(unsigned j=0;j<32;j++)if(!isfinite(local[j])){e=DR_MODEL_INVALID;goto fail;}
  const float *pt=(const float *)&dst->offset;for(unsigned j=0;j<4;j++)if(!isfinite(pt[j])){e=DR_MODEL_INVALID;goto fail;}
 }
 f->obj->mdl.body=f->body_data;
 f->obj->mdl.model=&f->model_header;f->obj->mdl.param=f->tables[XB_FIGHTER_PARAM];
 f->obj->type=0;f->obj->xf.scale=1.0f;f->obj->hit.scale=1.0f;
 f->obj->look.node2.w=1.0f;f->obj->look.node2E.w=1.0f;f->obj->look.node2F.w=1.0f;
 f->obj->anim.step=1.0f; /* diagnostic: one motion frame per Step call */
 unsigned slot=0;while(slot<16 && owners[slot])slot++;if(slot==16){e=-852;goto fail;}owners[slot]=f;
 memcpy(&f->template_obj,&f->owned_obj,sizeof(f->template_obj));*stats=f->stats;*out=f;return 0;
fail:XbBattleFighter_FreeModelAnimation(f);return e;
}
int XbBattleFighter_PlayModel(XbBattleFighter *f,unsigned index,unsigned mode){
 if(!f || index>=8 || mode>2)return DR_MODEL_INVALID;
 if(!f->obj->mdl.modelAnims[index])return DR_LOAD_EMPTY;
 BtlObjAnim_PlayModel(f->obj,(s32)index,(s32)mode);f->pose_valid=f->matrices_valid=0;return 0;
}
int XbBattleFighter_StepModel(XbBattleFighter *f,uint32_t *frame,uint32_t *length){
 if(!f || !frame || !length || !f->obj->anim.layer[0].data)return DR_MODEL_INVALID;
 f->pose_valid=f->matrices_valid=0;BtlObjAnim_Step(f->obj);*frame=(uint32_t)f->obj->anim.frame;*length=(uint32_t)f->obj->anim.layer[0].length;return 0;
}

extern void BtlObjAnim_Play(BobjObj *,s32,s32,s32);
extern void BtlObjAnim_SamplePose(BobjObj *);
extern void BtlObjPose_CalcMatrices(BobjObj *);
extern void BtlObjAnim_PlayFrom(BobjObj *,BobjObj *,s32,s32);
int XbBattleFighter_PlayMotion(XbBattleFighter *f,unsigned id,unsigned mode){
 if(!f || id>=BOBJ_ANIM_MAX || mode>2)return DR_MODEL_INVALID;
 if(!f->decoded[id])return DR_LOAD_EMPTY;
 BtlObjAnim_Play(f->obj,0,(s32)id,1);f->obj->anim.mode=mode;f->pose_valid=f->matrices_valid=0;return 0;
}
int XbBattleFighter_PlayRetargeted(XbBattleFighter *f,XbBattleFighter *other,unsigned id){
 if(!f || !other || id>=BOBJ_ANIM_MAX)return DR_MODEL_INVALID;
 if(!other->decoded[id])return DR_LOAD_EMPTY;
 BtlObjAnim_PlayFrom(f->obj,other->obj,(s32)id,0);f->obj->anim.mode=2;f->pose_valid=f->matrices_valid=0;return 0;
}
int XbBattleFighter_SamplePose(XbBattleFighter *f){
 if(!f || !f->obj->anim.layer[0].data)return DR_MODEL_INVALID;
 BtlObjAnim_SamplePose(f->obj);f->pose_valid=1;f->matrices_valid=0;return 0;
}
int XbBattleFighter_GetNodePose(XbBattleFighter *f,unsigned id,float pos[4],float rot[4]){
 if(!f || !f->pose_valid || id>=162 || !f->present[id] || !pos || !rot)return DR_MODEL_INVALID;
 void *node=NULL;XbFighter_ResolveNode(f->obj,(int)id,&node);if(!node)return DR_MODEL_INVALID;BobjPart *part=node;memcpy(pos,&part->pos,16);memcpy(rot,&part->rot,16);return 0;
}
extern void Vu0_InitAxisRegs(void);
extern void BtlObjXf_Update(BobjObj *);
int XbBattleFighter_CalcMatrices(XbBattleFighter *f){
 if(!f || !f->pose_valid || !f->obj->anim.layer[0].data)return DR_MODEL_INVALID;
 Vu0_InitAxisRegs();BtlObjXf_Update(f->obj);BtlObjPose_CalcMatrices(f->obj);f->matrices_valid=1;return 0;
}
int XbBattleFighter_GetNodeMatrix(XbBattleFighter *f,unsigned id,float matrix[16]){
 if(!f || !f->matrices_valid || id>=162 || !f->present[id] || !matrix)return DR_MODEL_INVALID;
 void *node=NULL;XbFighter_ResolveNode(f->obj,(int)id,&node);if(!node)return DR_MODEL_INVALID;memcpy(matrix,&((BobjPart *)node)->mtx,64);return 0;
}

extern void BtlObjBody_Init(BobjObj *);
extern s32 BtlObjBody_Update(BobjObj *,s32);
extern void BtlObjAnim_UpdateEvents(BobjObj *);
extern void BtlObjHit_BuildVolumes(BobjObj *);
int XbBattleFighter_UpdateVolumes(XbBattleFighter *f,unsigned *parts,unsigned *sphere,unsigned *boxes){
 if(!f || !f->matrices_valid || !f->obj->anim.layer[0].data || !parts || !sphere || !boxes)return DR_MODEL_INVALID;
 if(!f->obj->body.parts)BtlObjBody_Init(f->obj);
 BtlObjBody_Update(f->obj,0);BtlObjAnim_UpdateEvents(f->obj);BtlObjHit_BuildVolumes(f->obj);
 *parts=f->body_count;*sphere=(unsigned)f->obj->work->atkSphereCount;*boxes=(unsigned)f->obj->work->atkObbCount;return 0;
}

int XbBattleFighter_FindAttackMotion(XbBattleFighter *f,unsigned *motion){
 if(!f || !motion)return DR_MODEL_INVALID;
 for(unsigned i=0;i<BOBJ_ANIM_MAX;i++)if(f->decoded[i]){
  BobjAnim *a=f->decoded[i];for(unsigned j=0;j<a->eventCount;j++)if((a->event[j].attrLo&1) && a->event[j].arg){*motion=i;return 0;}
 }
 return DR_LOAD_EMPTY;
}

int XbBattleFighter_GetMesh(XbBattleFighter *f,unsigned index,XbFighterMesh *out){
 if(!f || !f->matrices_valid || !out || index>=f->stats.bones)return DR_MODEL_INVALID;
 DrBodyPart *p=&f->parts[index];memset(out,0,sizeof(*out));out->node=p->node;
 out->vif=f->model_pack+f->mesh_offset+p->chain_offset;out->vif_bytes=p->chain_bytes;
 if(f->model_lifecycle){int rc=XbNativeModel_Mesh(f->native_model,index,&out->vif,&out->vif_bytes);if(rc)return rc;}
 int rc=XbBattleFighter_GetNodeMatrix(f,p->node,out->world);if(rc)return rc;memcpy(out->pivot_a,p->pivot_a,16);memcpy(out->pivot_b,p->pivot_b,16);return 0;
}
int XbBattleFighter_GetFaceGeometry(XbBattleFighter *f,unsigned shape,const void **bytes,size_t *size){
 if(!f || !bytes || !size || shape>=8)return DR_MODEL_INVALID;
 *bytes=NULL;*size=0;size_t off,n,bad;int e=dr_pack_memory_entry(f->model_pack,f->model_size,3+shape,&off,&n,NULL);if(e)return e;
 e=dr_face_vif_parse(f->model_pack+off,n,NULL,NULL,NULL,&bad);if(e)return e;
 *bytes=f->model_pack+off;*size=n;return 0;
}
int XbBattleFighter_PrepareTextures(XbBattleFighter *f,unsigned *count){
 if(!f || !count)return DR_MODEL_INVALID;
 *count=0;if(f->images){*count=f->image_count;return 0;}
 static const unsigned entries[]={0,11,12,13,14,15,16};unsigned total=0;
 for(unsigned g=0;g<7;g++){size_t off,n;int e=dr_pack_memory_entry(f->model_pack,f->model_size,entries[g],&off,&n,NULL);if(e==DR_NOT_FOUND)continue;if(e || n<20)return DR_MODEL_INVALID;uint32_t c=dr_resource_le32(f->model_pack+off);if(c>256 || total>512-c)return DR_MODEL_INVALID;total+=c;}
 XbCachedImage *images=calloc(total?total:1,sizeof(*images));void *scratch=malloc(4*1024*1024);if(!images || !scratch){free(images);free(scratch);return -851;}
 unsigned used=0;size_t budget=0;int e=0;
 for(unsigned g=0;g<7 && !e;g++){
  size_t off,n;e=dr_pack_memory_entry(f->model_pack,f->model_size,entries[g],&off,&n,NULL);if(e==DR_NOT_FOUND){e=0;continue;}if(e)break;
  uint32_t c=dr_resource_le32(f->model_pack+off);
  for(unsigned i=0;i<c;i++){
   DrTexFileImage image;e=dr_texfile_decode(f->model_pack+off,n,i,scratch,4*1024*1024,&image);if(e)break;
   if(image.rgba_bytes>32*1024*1024-budget){e=DR_MODEL_CAPACITY;break;}
   void *pixels=malloc(image.rgba_bytes);if(!pixels){e=-851;break;}memcpy(pixels,scratch,image.rgba_bytes);
   images[used++]=(XbCachedImage){entries[g],i,image,pixels};budget+=image.rgba_bytes;
  }
 }
 free(scratch);if(e){for(unsigned i=0;i<used;i++)free(images[i].rgba);free(images);return e;}
 f->images=images;f->image_count=used;*count=used;return 0;
}
int XbBattleFighter_GetTexture(XbBattleFighter *f,unsigned index,XbFighterTexture *out){
 if(!f || !out || index>=f->image_count)return DR_MODEL_INVALID;
 XbCachedImage *v=&f->images[index];*out=(XbFighterTexture){v->family,v->index,v->image.width,v->image.height,v->image.rgba_bytes,v->rgba};return 0;
}

int XbBattleFighter_GetNativeTable(XbBattleFighter *f,unsigned kind,const void **data,size_t *bytes){
 if(!f || kind>=XB_FIGHTER_TABLES || !data || !bytes)return DR_MODEL_INVALID;
 *data=f->tables[kind];*bytes=f->table_bytes[kind];return 0;
}
int XbBattleFighter_BindCommonReference(XbBattleFighter *f,const float *xyz,size_t count){
 if(!f || !xyz || count!=213)return DR_MODEL_INVALID;
 for(unsigned i=0;i<213;i++)if(!isfinite(xyz[i]))return DR_MODEL_INVALID;
 memcpy(f->common_reference,xyz,sizeof(f->common_reference));f->common_reference_ready=1;return 0;
}
const float *XbFighter_CommonReference(void *obj,const float *fallback){
 XbBattleFighter *f=find_owner(obj);return f? (f->common_reference_ready?f->common_reference:NULL):fallback;
}
int XbBattleFighter_PlayCommonRetargeted(XbBattleFighter *f,XbBattleFighter *other,unsigned id){
 if(!f || !other || id>=BOBJ_ANIM_MAX || !f->common_reference_ready)return DR_MODEL_INVALID;
 if(!other->decoded[id])return DR_LOAD_EMPTY;
 BtlObjAnim_PlayFrom(f->obj,other->obj,(s32)id,1);f->obj->anim.mode=2;f->pose_valid=f->matrices_valid=0;return 0;
}

extern int XbFighterRegistry_Begin(void);
extern int XbFighterRegistry_Add(const void *,size_t,void **,int *);
extern int XbFighterRegistry_Remove(int,void *,size_t);
extern int XbFighterRegistry_End(void);
int XbBattleFighter_BeginObjectDiagnostic(void){return XbFighterRegistry_Begin();}
int XbBattleFighter_EndObjectDiagnostic(void){return XbFighterRegistry_End();}
int XbBattleFighter_RegisterObject(XbBattleFighter *f,int *id){
 if(!f || !id || f->registered_id>=0)return -865;
 void *live=NULL;int n=-1;int e=XbFighterRegistry_Add(f->obj,sizeof(*f->obj),&live,&n);if(e)return e;
 f->obj=live;f->registered_id=n;*id=n;return 0;
}
int XbBattleFighter_UnregisterObject(XbBattleFighter *f){
 if(!f || f->registered_id<0)return -865;
 if(f->model_lifecycle){int de=XbBattleFighter_DestroyModelObject(f);if(de)return de;}
 int e=XbFighterRegistry_Remove(f->registered_id,&f->owned_obj,sizeof(f->owned_obj));if(e)return e;
 f->obj=&f->owned_obj;f->registered_id=-1;return 0;
}

extern int XbFighterRegistry_ModelCapacity(unsigned);
extern int XbFighterRegistry_ModelSlot(int,const void *,size_t,const void *,size_t,void **);
extern void XbFighterRegistry_ReleaseModelSlot(int);
extern int XbFighterRegistry_Setup(int,void *);
extern void XbFighterRegistry_DestroyModel(int);
int XbFighter_BindOwnedTables(void *object){
 XbBattleFighter *f=find_owner(object);if(!f || !f->model_lifecycle)return 0;
 XbNativeModel_BindTables(f->native_model,object,&f->template_obj.mdl,sizeof(f->template_obj.mdl));
 if(f->effects){
  BObjMdl *mdl=&((BObj *)object)->mdl;
  mdl->unk40=XbCharacterEffects_Root(f->effects);
  for(unsigned slot=0;slot<5;slot++)mdl->unk84[slot]=XbCharacterEffects_TechniqueRoot(f->effects,slot);
 }
 return 1;
}
int XbFighter_BindOwnedFiles(void *object){
 XbBattleFighter *f=find_owner(object);if(!f || !f->model_lifecycle)return 0;
 XbNativeModel_BindFiles(f->native_model,object);return 1;
}
int XbBattleFighter_SetupModelObject(XbBattleFighter *f,unsigned side){
 if(!f || sizeof(void *)!=4 || f->registered_id<0 || f->registered_id>=2 || side>1 || f->model_lifecycle)return -870;
 int e=XbFighterRegistry_ModelCapacity(f->stats.bones);if(e)return e;
 XbNativeModel *model=NULL;e=XbNativeModel_Prepare(f->model_pack,f->model_size,&model);if(e)return e;
 void *slot=NULL;e=XbFighterRegistry_ModelSlot((int)side,f->model_pack,f->model_size,f->motions_pack,f->motions_size,&slot);if(e){XbNativeModel_Free(model);return e;}
 f->native_model=model;f->model_lifecycle=1;f->model_side=(int)side;
 e=XbFighterRegistry_Setup(f->registered_id,slot);if(e){f->model_lifecycle=0;XbFighterRegistry_ReleaseModelSlot((int)side);XbNativeModel_Free(model);f->native_model=NULL;return e;}
 f->pose_valid=f->matrices_valid=0;return 0;
}
int XbBattleFighter_DestroyModelObject(XbBattleFighter *f){
 if(!f || !f->model_lifecycle || f->registered_id<0)return -870;
 XbFighterRegistry_DestroyModel(f->registered_id);XbFighterRegistry_ReleaseModelSlot(f->model_side);
 f->model_lifecycle=0;memcpy(f->obj,&f->template_obj,sizeof(*f->obj));f->obj->body.parts=NULL;
 XbNativeModel_Free(f->native_model);f->native_model=NULL;f->pose_valid=f->matrices_valid=0;return 0;
}
extern int XbFighterRegistry_NextModel(void **,int *);
extern int XbFighterRegistry_CreateModel(int);
int XbBattleFighter_CreateModelObject(XbBattleFighter *f,unsigned side,int *id){
 if(!f || !id || sizeof(void *)!=4 || side>1 || f->registered_id>=0 || f->model_lifecycle)return -870;
 *id=-1;int e=XbFighterRegistry_ModelCapacity(f->stats.bones);if(e)return e;
 void *next=NULL;int expected=-1;e=XbFighterRegistry_NextModel(&next,&expected);if(e)return e;
 XbNativeModel *model=NULL;e=XbNativeModel_Prepare(f->model_pack,f->model_size,&model);if(e)return e;
 void *slot=NULL;e=XbFighterRegistry_ModelSlot((int)side,f->model_pack,f->model_size,f->motions_pack,f->motions_size,&slot);
 if(e){XbNativeModel_Free(model);return e;}
 f->native_model=model;f->model_lifecycle=1;f->model_side=(int)side;f->obj=next;
 int actual=XbFighterRegistry_CreateModel((int)side);
 if(actual<0){f->obj=&f->owned_obj;f->model_lifecycle=0;XbFighterRegistry_ReleaseModelSlot((int)side);XbNativeModel_Free(model);f->native_model=NULL;return actual;}
 f->registered_id=actual;f->pose_valid=f->matrices_valid=0;*id=actual;return 0;
}
static BObj *facial_object(XbBattleFighter *f){return f && sizeof(void *)==4 && f->model_lifecycle && f->registered_id>=0?(BObj *)(void *)f->obj:NULL;}
int XbBattleFighter_SetFace(XbBattleFighter *f,unsigned mode,unsigned track){
 BObj *obj=facial_object(f);
 if(!obj || !obj->face || !(obj->face->flags&BOBJ_FACE_MOUTH) || mode>13)return -870;
 if(mode==BOBJ_MOUTH_LIP && (track>=100 || !obj->mdl.lip[track]))return DR_NOT_FOUND;
 /* The original intentionally refuses transitions out of held shapes except
  * through rest/another held shape. Report that instead of silent success. */
 if(obj->face->mode>=9 && obj->face->mode<=12 && mode && (mode<9 || mode>12))return -870;
 BtlObj_SetSubState(obj,(s32)mode,(s32)track);f->matrices_valid=0;return 0;
}
int XbBattleFighter_SetEye(XbBattleFighter *f,unsigned frame){
 BObj *obj=facial_object(f);
 if(!obj || !obj->face || !(obj->face->flags&BOBJ_FACE_EYES) || !obj->mdl.eyes || (frame!=9 && frame>obj->mdl.eyes->count))return -870;
 BtlObj_SetEyeFrame(obj,frame);return 0;
}
int XbBattleFighter_StepFace(XbBattleFighter *f,XbFighterFaceState *out){
 BObj *obj=facial_object(f);if(!obj || !obj->face || !out)return -870;
 BtlObj_UpdateFace(obj);BObjFace *face=obj->face;
 out->eye=face->eye;out->mode=face->mode;out->shape=face->shape;
 out->lip_index=face->lipIndex;out->lip_count=face->lipCount;out->lip_time=face->lipTime;out->lip_end=face->lipEnd;
 memcpy(out->jaw,&face->jaw,sizeof(out->jaw));f->matrices_valid=0;return 0;
}

extern int XbFighterRegistry_BeginOriginal(void);
int XbBattleFighter_BeginOriginalObjects(void){return XbFighterRegistry_BeginOriginal();}
/* BtlChar_BindObject's native equivalents of file header word 3. */
int XbFighter_MemberTable(void *object,int file,void **out){
 XbBattleFighter *f=find_owner(object);if(!f || !f->model_lifecycle)return 0;
 *out=file==0?XbNativeModel_Header(f->native_model):file==1?f->decoded[2]:NULL;return 1;
}
