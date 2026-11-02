/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_battle_fighter.h"
#include "dr_container.h"
#include "dr_face_vif.h"
#include <string.h>
#include "battle/btl_obj.h"
BtlObjWork *gBtlObjWork;
BtlObj *gBtlObjTbl[BTL_OBJ_MAX];
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <math.h>
/* Storage for the original's unused table-retarget branch. Tests use
 * per-skeleton retargeting; these values are never read. */
void *Bpe_Decode(void *src,void *dst,int *size){(void)src;(void)dst;(void)size;abort();} /* forbidden legacy fallback in owned-motion tests */
size_t XbRes_BpeSourceSize(const void *p){(void)p;abort();}
void *XbBpe_DecodeBounded(void *s,void *d,unsigned n,int *z){(void)s;(void)d;(void)n;(void)z;abort();}
float D_002F2420[213];
#ifdef HAVE_REAL_REFERENCE
extern const float real_reference[213];
#endif
extern void BtlObjMdl_BindFiles(void *);
float D_002EC2C0[4]={0,0,0,1};
float D_003337D0[4];void *gSaveData,*gCommonRes,*gProgress;
static void *load(const char *name,size_t *n){FILE *f=fopen(name,"rb");assert(f && !fseek(f,0,SEEK_END));long s=ftell(f);assert(s>0 && !fseek(f,0,SEEK_SET));*n=(size_t)s;void *p=malloc(*n);assert(p && fread(p,1,*n,f)==*n);fclose(f);return p;}
typedef struct MaterialCheck {const unsigned char *bytes;unsigned blocks;} MaterialCheck;
static int material_visitor(void *ctx,const DrFaceVifCommand *c){
 MaterialCheck *check=ctx;if(c->code!=0x6c058000)return 0;
 const unsigned char *p=check->bytes+c->code_offset;
 assert((dr_resource_le32(p+20)&0x3fff)>=0x3480);
 assert(((dr_resource_le32(p+24)>>5)&0x3fff)>=0x3c00);
 assert((dr_resource_le32(p+36)&0x3fff)==0x3d40);
 assert(((dr_resource_le32(p+40)>>5)&0x3fff)>=0x3d00);check->blocks++;return 0;
}
int main(int argc,char **argv){setvbuf(stdout,NULL,_IOLBF,0);assert(argc==3 || argc==5);size_t mn,an;void *m=load(argv[1],&mn),*a=load(argv[2],&an);XbBattleFighter *f=NULL;XbFighterPrepared s;
 XbBattleFighter *bad=(XbBattleFighter *)(uintptr_t)1;XbFighterPrepared bads;
 assert(XbBattleFighter_PrepareModelAnimation(m,32,a,an,&bad,&bads)<0 && !bad);
 assert(XbBattleFighter_PrepareModelAnimation(m,mn,a,16,&bad,&bads)<0 && !bad);
 int e=XbBattleFighter_PrepareModelAnimation(m,mn,a,an,&f,&s);if(e)fprintf(stderr,"prepare error=%d\n",e);assert(!e && f);XbFighterMesh early;assert(XbBattleFighter_GetMesh(f,0,&early)<0);free(m);free(a);
 const void *table;size_t tn;static const size_t expected[]={256,7824,676,160,0x244,0xc8,0x200,360};
 for(unsigned k=0;k<XB_FIGHTER_TABLES;k++){assert(!XbBattleFighter_GetNativeTable(f,k,&table,&tn));assert(tn==expected[k]);}
 assert(XbBattleFighter_PlayCommonRetargeted(f,f,0)<0);
 #ifdef HAVE_REAL_REFERENCE
 assert(XbBattleFighter_BindCommonReference(f,real_reference,212)<0);
 assert(!XbBattleFighter_BindCommonReference(f,real_reference,213));
 #endif
 assert(s.bones==69 && s.motions+s.empty_motions==414 && s.model_motions==1 && s.empty_model_motions==7);
 assert(XbBattleFighter_PlayModel(f,0,2)==DR_LOAD_EMPTY);assert(!XbBattleFighter_PlayModel(f,7,2));uint32_t frame,length;
 for(unsigned i=0;i<200;i++){assert(!XbBattleFighter_StepModel(f,&frame,&length));assert(frame==(i+1)%length);}
 unsigned picked=0;int play;
 do{play=XbBattleFighter_PlayMotion(f,picked,2);if(play==DR_LOAD_EMPTY)picked++;else break;}while(picked<414);
 assert(!play && picked<414);float p0[4],p1[4],rot[4];assert(!XbBattleFighter_SamplePose(f));assert(!XbBattleFighter_GetNodePose(f,0,p0,rot));
 #if UINTPTR_MAX==UINT32_MAX
 assert(!XbBattleFighter_BeginObjectDiagnostic());assert(XbBattleFighter_BeginObjectDiagnostic()<0);
 int objid=-1;assert(!XbBattleFighter_RegisterObject(f,&objid));assert(objid==0 && BtlObj_GetCount()==1 && BtlObj_Get(0) && BtlObj_Get(0)->active==0);
 assert(XbBattleFighter_RegisterObject(f,&objid)<0);assert(XbBattleFighter_EndObjectDiagnostic()<0);
 size_t badn,bada;void *badmodel=load(argv[1],&badn),*badmotion=load(argv[2],&bada);size_t tx,txbytes;assert(!dr_pack_memory_entry(badmodel,badn,11,&tx,&txbytes,NULL));
 memset((unsigned char *)badmodel+tx+4,255,4);XbBattleFighter *badfighter;XbFighterPrepared badstats;assert(!XbBattleFighter_PrepareModelAnimation(badmodel,badn,badmotion,bada,&badfighter,&badstats));free(badmodel);free(badmotion);
 int badid;assert(!XbBattleFighter_RegisterObject(badfighter,&badid));assert(XbBattleFighter_SetupModelObject(badfighter,1)<0);
 assert(BtlObjPoolE0_GetFreeCount()==512 && BtlObjPool70_GetFreeCount()==4 && BtlObjPool3_GetFreeCount()==5);XbBattleFighter_FreeModelAnimation(badfighter);
 assert(!XbBattleFighter_UnregisterObject(f));assert(!XbBattleFighter_EndObjectDiagnostic());assert(!XbBattleFighter_BeginObjectDiagnostic());assert(!XbBattleFighter_RegisterObject(f,&objid));

 #endif
 #if UINTPTR_MAX==UINT32_MAX
 float before[16],after[16];assert(!XbBattleFighter_CalcMatrices(f));assert(!XbBattleFighter_GetNodeMatrix(f,3,before));
 #endif
 for(unsigned i=0;i<30;i++){assert(!XbBattleFighter_StepModel(f,&frame,&length));assert(!XbBattleFighter_SamplePose(f));}
 assert(!XbBattleFighter_GetNodePose(f,0,p1,rot));
 #if UINTPTR_MAX==UINT32_MAX
 assert(!XbBattleFighter_CalcMatrices(f));assert(!XbBattleFighter_GetNodeMatrix(f,3,after));float delta=0;for(unsigned i=0;i<16;i++)delta+=fabsf(before[i]-after[i]);assert(delta>0.000001f);printf("animated_matrix_delta=%f\n",delta);
 #endif
 #if UINTPTR_MAX==UINT32_MAX
 assert(!XbBattleFighter_PlayRetargeted(f,f,picked));assert(!XbBattleFighter_SamplePose(f));
 #ifdef HAVE_REAL_REFERENCE
 assert(!XbBattleFighter_PlayCommonRetargeted(f,f,picked));assert(!XbBattleFighter_SamplePose(f));assert(!XbBattleFighter_CalcMatrices(f));
 printf("real common-table retarget sampled and matrices calculated\n");
 #endif
 #endif
 #if UINTPTR_MAX==UINT32_MAX
 float matrix[16];assert(!XbBattleFighter_CalcMatrices(f));assert(!XbBattleFighter_GetNodeMatrix(f,3,matrix));for(unsigned i=0;i<16;i++)assert(isfinite(matrix[i]));
 unsigned bp,sp,bb;assert(!XbBattleFighter_UpdateVolumes(f,&bp,&sp,&bb));assert(bp==22 && sp<=19 && bb<=19);printf("body_parts=%u attack_spheres=%u attack_boxes=%u\n",bp,sp,bb);
 unsigned attack;assert(!XbBattleFighter_FindAttackMotion(f,&attack));assert(!XbBattleFighter_PlayMotion(f,attack,2));unsigned hits=0;
 for(unsigned i=0;i<200;i++){assert(!XbBattleFighter_StepModel(f,&frame,&length));assert(!XbBattleFighter_SamplePose(f));assert(!XbBattleFighter_CalcMatrices(f));assert(!XbBattleFighter_UpdateVolumes(f,&bp,&sp,&bb));if(sp+bb)hits++;}
 assert(hits);printf("attack_motion=%u active_volume_frames=%u\n",attack,hits);
 #endif
 printf("pose motion=%u root0=%f root30=%f quaternion_w=%f\n",picked,p0[1],p1[1],rot[3]);
 printf("PASS: bones=%u decoded=%u empty=%u model=%u/%u; original PlayModel/Step 200 frames length=%u; source freed; no combat spawn\n",s.bones,s.motions,s.empty_motions,s.model_motions,s.empty_model_motions,length);
 #if UINTPTR_MAX==UINT32_MAX
 if(argc==5){size_t on,oa;void *om=load(argv[3],&on),*op=load(argv[4],&oa);XbBattleFighter *other;XbFighterPrepared os;assert(!XbBattleFighter_PrepareModelAnimation(om,on,op,oa,&other,&os));free(om);free(op);int oid=-1;assert(!XbBattleFighter_RegisterObject(other,&oid));assert(oid==1 && BtlObj_GetCount()==2 && BtlObj_Get(oid)->active==0);
 const void *native_table;size_t native_bytes;assert(!XbBattleFighter_GetNativeTable(other,XB_FIGHTER_ATTACK,&native_table,&native_bytes));
 assert(*(void **)((unsigned char *)BtlObj_Get(oid)+0x920)==native_table && native_bytes==7824);
 assert(!XbBattleFighter_SetupModelObject(f,0));assert(!XbBattleFighter_SetupModelObject(other,1));
 assert(BtlObjPoolE0_GetFreeCount()==374 && BtlObjPool70_GetFreeCount()==2 && BtlObjPool3_GetFreeCount()==3);
 assert(!XbBattleFighter_PlayRetargeted(f,other,55));XbBattleFighter_FreeModelAnimation(other);
 assert(BtlObjPoolE0_GetFreeCount()==443 && BtlObjPool70_GetFreeCount()==3 && BtlObjPool3_GetFreeCount()==4);assert(!BtlObj_Get(oid) && BtlObj_GetCount()==1);assert(!XbBattleFighter_SamplePose(f));assert(!XbBattleFighter_CalcMatrices(f));printf("cross-owner retarget survived source fighter free\n");assert(!XbBattleFighter_DestroyModelObject(f));}
 #endif
 #if UINTPTR_MAX==UINT32_MAX
 int setup=XbBattleFighter_SetupModelObject(f,0);printf("original BtlObj_Setup rc=%d\n",setup);assert(!setup);
 assert(BtlObj_Get(objid)->active==1 && BtlObj_Get(objid)->unk04==1);
 XbFighterFaceState face;assert(!XbBattleFighter_SetEye(f,1));assert(!XbBattleFighter_StepFace(f,&face) && face.eye==1);
 assert(XbBattleFighter_SetEye(f,999)<0);assert(!XbBattleFighter_SetEye(f,0));assert(!XbBattleFighter_StepFace(f,&face) && face.eye==0);
 assert(!XbBattleFighter_SetEye(f,9));unsigned blink_open=0,blink_closed=0;
 for(unsigned i=0;i<600;i++){assert(!XbBattleFighter_StepFace(f,&face));if(face.eye)blink_closed++;else blink_open++;}assert(blink_open && blink_closed);
 assert(XbBattleFighter_SetFace(f,14,0)<0);assert(XbBattleFighter_SetFace(f,2,100)<0);
 assert(!XbBattleFighter_SetFace(f,10,0));assert(!XbBattleFighter_StepFace(f,&face));assert(face.mode==10 && face.shape==6 && fabsf(face.jaw[0]-0.3665191f)<0.00001f);
 assert(XbBattleFighter_SetFace(f,3,0)<0);assert(!XbBattleFighter_SetFace(f,0,0));
 assert(!XbBattleFighter_SetFace(f,3,0));for(unsigned i=0;i<200;i++)assert(!XbBattleFighter_StepFace(f,&face));assert(!XbBattleFighter_SetFace(f,0,0));
 unsigned lip_track=0;while(lip_track<100 && XbBattleFighter_SetFace(f,2,lip_track))lip_track++;assert(lip_track<100);
 assert(!XbBattleFighter_StepFace(f,&face));assert(face.lip_count>1);unsigned lip_count=face.lip_count;float lip_end=face.lip_end;assert(lip_end>=0 && lip_end<20000);
 for(unsigned i=0;i<(unsigned)(lip_end/2)+4;i++)assert(!XbBattleFighter_StepFace(f,&face));assert(!XbBattleFighter_SetFace(f,0,0));
 printf("original CPU facial blink=%u/%u hold-jaw/talk/lip track=%u keys=%u end=%f PASS; no audio clock/GPU proof\n",blink_open,blink_closed,lip_track,lip_count,lip_end);
 unsigned alpha_bits=gBtlObjWork->vis.allocBits;BtlObjMdl_BindFiles(BtlObj_Get(objid));assert(gBtlObjWork->vis.allocBits==alpha_bits);
 assert(BtlObjPoolE0_GetFreeCount()==512-69 && BtlObjPool70_GetFreeCount()==3 && BtlObjPool3_GetFreeCount()==4);
 assert(!XbBattleFighter_PlayMotion(f,0,2));
 for(unsigned i=0;i<100;i++){assert(!XbBattleFighter_StepModel(f,&frame,&length));assert(!XbBattleFighter_SamplePose(f));assert(!XbBattleFighter_CalcMatrices(f));}
 unsigned material_blocks=0;for(unsigned i=0;i<s.bones;i++){XbFighterMesh input;assert(!XbBattleFighter_GetMesh(f,i,&input));if(input.vif_bytes){size_t bad;MaterialCheck check={input.vif,0};assert(!dr_face_vif_parse(input.vif,input.vif_bytes,material_visitor,&check,NULL,&bad));material_blocks+=check.blocks;}}assert(material_blocks);printf("native rebased canonical VIF material blocks=%u\n",material_blocks);
 assert(!XbBattleFighter_DestroyModelObject(f));assert(!XbBattleFighter_PlayMotion(f,0,2));assert(!XbBattleFighter_SamplePose(f));assert(!XbBattleFighter_CalcMatrices(f));assert(BtlObjPoolE0_GetFreeCount()==512 && BtlObjPool70_GetFreeCount()==4 && BtlObjPool3_GetFreeCount()==5);
 printf("real Setup/init/parts/materials/face/shadow/animation/destroy lifecycle PASS; BtlChar/BtlAi absent\n");
 #endif
 unsigned textures=0;int te=XbBattleFighter_PrepareTextures(f,&textures);printf("texture preparation rc=%d count=%u\n",te,textures);assert(!te && textures);
 for(unsigned i=0;i<textures;i++){XbFighterTexture t;assert(!XbBattleFighter_GetTexture(f,i,&t));assert(t.rgba && t.bytes==(size_t)t.width*t.height*4);}
 unsigned faces=0;for(unsigned i=0;i<8;i++){const void *vif;size_t bytes;int rc=XbBattleFighter_GetFaceGeometry(f,i,&vif,&bytes);if(!rc){assert(vif && bytes);faces++;}else assert(rc==DR_NOT_FOUND);}assert(faces);
 XbFighterMesh mesh;
 #if UINTPTR_MAX==UINT32_MAX
 assert(!XbBattleFighter_GetMesh(f,0,&mesh));
 #else
 assert(XbBattleFighter_GetMesh(f,0,&mesh)<0);mesh.node=0;
 #endif
 printf("renderer inputs textures=%u facial_geometries=%u node0=%u; no GPU submission\n",textures,faces,mesh.node);
 #if UINTPTR_MAX==UINT32_MAX
 assert(!XbBattleFighter_UnregisterObject(f));assert(!BtlObj_Get(0) && BtlObj_GetCount()==0);
 assert(!XbBattleFighter_EndObjectDiagnostic());assert(!XbBattleFighter_BeginObjectDiagnostic());
 assert(!XbBattleFighter_CreateModelObject(f,0,&objid));assert(objid==0 && BtlObj_Get(objid)->active==1 && BtlObj_Get(objid)->unk04==1);
 assert(BtlObjPoolE0_GetFreeCount()==443 && BtlObjPool70_GetFreeCount()==3 && BtlObjPool3_GetFreeCount()==4);
 assert(!XbBattleFighter_PlayMotion(f,0,2));assert(!XbBattleFighter_SamplePose(f));assert(!XbBattleFighter_CalcMatrices(f));
 printf("real BtlObj_CreateChara -> Setup path PASS; native slot and charaWork installed\n");
 #endif
 XbBattleFighter_FreeModelAnimation(f);
 #if UINTPTR_MAX==UINT32_MAX
 assert(BtlObj_GetCount()==0);assert(!XbBattleFighter_EndObjectDiagnostic());assert(!gBtlObjWork);
 printf("original object lists/map/pools/resources cleanup PASS; no BtlChar/BtlAi boot\n");
 #endif
 return 0;}
