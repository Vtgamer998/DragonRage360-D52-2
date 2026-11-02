/* SPDX-License-Identifier: AGPL-3.0-or-later */
#define _GNU_SOURCE
#include "battle/battle_work.h"
#include "battle/battle_setup.h"
#include "sys/common.h"
#include "sys/loading.h"
#include "sys/save.h"
#include "sys/heap.h"
#include "xb_resconv.h"
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
static union {uint64_t align;unsigned char bytes[32+sizeof(BattleRes)];} common;
CommonRes *gCommonRes=(CommonRes *)common.bytes;
#define BATTLE_RES ((BattleRes *)(common.bytes+32))
#define BTL_STAGE_BANK(stage) ((stage)+BTL_FILE_SND_STAGE)
#define File_Request3(id,p,n) request(id,p,n)
static SaveData save;SaveData *gSaveData=&save;static Progress progress;Progress *gProgress=&progress;
static BattleMember members[2][5];static int slots[2],objects[2];
static struct {void *p;size_t n;} allocated[32];
HeapBlock *gHeapStart[HEAP_COUNT],*gHeapEnd[HEAP_COUNT];
s32 gHeapSize[HEAP_COUNT],gHeapDebugValue,gHeapDebugUnused,gHeapUnk70[HEAP_COUNT],gHeapUnk78[HEAP_COUNT];u8 gHeapDebugFlag;
static void *arena;static size_t arena_size;static s32 baseline;
void *TestHeapMalloc(u32 n){assert(!arena);arena_size=n;arena=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);assert(arena!=MAP_FAILED && (uintptr_t)arena<=UINT32_MAX);return arena;}
void *ActualHeap_Alloc(s32,u32,s32,s32);void ActualHeap_Free(void *);s32 Heap_GetFreeSize(void);
static int live,allocations,requests,models,fail_alloc,fail_request,fail_model,model_error,script,fighters;
static int stopped,closed,drained,unloads,pool_resets,relocated,job_freed,sound_calls,fail_sound;
static int original_object_owner;
int XbBattleBoot_OriginalObjectStateActive(void){return original_object_owner;}
void *Heap_Alloc(s32 n,u32 a,s32 b,s32 c){(void)a;(void)b;(void)c;assert(n>0);if(++allocations==fail_alloc)return NULL;void *p=ActualHeap_Alloc(n,a,b,c);assert(p);memset(p,0x77,(size_t)n);for(unsigned i=0;i<32;i++)if(!allocated[i].p){allocated[i].p=p;allocated[i].n=(size_t)n;live++;return p;}assert(0);return NULL;}
void Heap_Free(void *p){if(!p)return;assert(stopped && closed && drained);for(unsigned i=0;i<32;i++)if(allocated[i].p==p){ActualHeap_Free(p);allocated[i].p=NULL;live--;return;}assert(!"lost owner or duplicate free");}
static void *request(s32 id,void *p,s32 capacity){(void)id;assert(capacity>=0);if(++requests==fail_request){XbRes_SetReadError(-23);return NULL;}if(p)return p;p=Heap_Alloc(2048,64,0,2);if(!p)XbRes_SetReadError(-908);return p;}
s32 Battle_GetScript(void){return script;}
s32 BtlObj_GetCount(void){return fighters;}
s32 Battle_GetStartStage(void){return 0;}
s32 BattleSide_GetStartChara(s32 side){return side;}
s32 BattleSide_GetStartCostume(s32 side){(void)side;return 0;}
s32 BattleSide_GetStartVariant(s32 side){(void)side;return 0;}
s32 BtlObj_RequestCharaModel(s32 a,s32 b,s32 c,s32 d){(void)b;(void)c;(void)d;models++;if(models==fail_model){if(model_error){XbRes_SetReadError(-23);return a;}return -1;}return a;}
void BattleSide_SetModelSlot(s32 side,s32 slot){slots[side]=slot;}
void BattleSide_SetObjId(s32 side,s32 id){objects[side]=id;}
BattleMember *BattleSide_GetMember(s32 side,s32 member){assert(side>=0 && side<2 && member>=0 && member<5);return &members[side][member];}
s32 BattleSide_GetMemberCount(s32 side){(void)side;return 1;}
s32 Battle_IsSplitScreen(void){return 0;}
static int File_UpdateRequests(void){return !XbRes_LastError();}
void Res_RelocateOffsets(void *dst,void *base,void *hdr){(void)dst;assert(base && base==hdr);relocated++;}
s32 BtlJob_Free(BtlJob *job){assert(job);job_freed++;return 1;}
void BtlJob_InitPool(void){pool_resets++;}
static void Job_Clear(void){stopped++;}
void XbFile_AbortRequestsRetainBuffers(void){assert(stopped);closed++;}
void Snd_Reset(void){assert(closed);drained++;}
void Snd_UnloadBank(s32 mask){assert(drained && (mask==4 || mask==8 || mask==16 || mask==32));unloads++;}
void Snd_LoadBank(s32 mask,void *p,s32 sync){(void)mask;(void)p;(void)sync;if(++sound_calls==fail_sound)XbRes_SetReadError(-24);}
s32 Snd_IsUploadDone(void){return 1;}
BattleEvents *Battle_GetEventWork(void){assert(!"script branch excluded");return NULL;}
s32 Gsc_LoadFile(void *p){(void)p;assert(0);return 0;}
void Gsc_RunAction(s32 a,s32 b){(void)a;(void)b;assert(0);}
void BtlScript_ScanEvents(s32 a){(void)a;assert(0);}
void BtlScript_Nop(void){assert(0);}
void BattleSetup_Finish(void){assert(0);}
/* ORIGINAL_FUNCTIONS */
static void reset(void){
 assert(!live && Heap_GetFreeSize()==baseline);memset(&common,0,sizeof(common));memset(members,0,sizeof(members));
 for(unsigned i=0;i<2;i++){members[i][0].chara=(s32)i;slots[i]=objects[i]=-1;}
 allocations=requests=models=fail_alloc=fail_request=fail_model=model_error=fighters=0;script=-1;
 stopped=closed=drained=unloads=pool_resets=relocated=job_freed=sound_calls=fail_sound=0;assert(!XbRes_BeginCheckedReads());
 original_object_owner=1;
}
static void verify_owners(void){
 for(unsigned j=0;j<32;j++)if(allocated[j].p){
  int owners=0;void *p=allocated[j].p;
  owners+=(BATTLE_RES->stage==p)+(BATTLE_RES->bank==p)+(BATTLE_RES->unk8==p)+(BATTLE_RES->transition==p);
  owners+=(BATTLE_RES->sndCommon==p)+(BATTLE_RES->sndStage==p)+(BATTLE_RES->sndChara[0]==p)+(BATTLE_RES->sndChara[1]==p);
  for(unsigned s=0;s<2;s++)for(unsigned i=0;i<5;i++)owners+=(members[s][i].buf[0]==p)+(members[s][i].buf[1]==p);
  assert(owners==1);
 }
}
static void abort_and_verify(void){
 int error=XbRes_LastError();verify_owners();assert(XbBtlLoad_AbortInitial()==1);
 assert(!live && Heap_GetFreeSize()==baseline && stopped==1 && closed==1 && drained==1 && unloads==4 && pool_resets==1 && XbRes_LastError()==error);
 for(unsigned s=0;s<2;s++){assert(slots[s]==-1 && objects[s]==-1 && members[s][0].chara==(s32)s);for(unsigned i=0;i<5;i++)assert(!members[s][i].buf[0] && !members[s][i].buf[1] && !members[s][i].data);}
 unsigned char zero[sizeof(BattleRes)]={0};assert(!memcmp(BATTLE_RES,zero,sizeof(zero)));XbRes_EndCheckedReads();
}
int main(void){
 Heap_Init();baseline=Heap_GetFreeSize();assert(baseline>8*1024*1024);
 for(int failure=0;failure<=23;failure++){
  reset();fail_alloc=failure;BtlJob job={0};job.state=4;assert(!BtlLoad_StepInitial(&job));
  if(failure){assert(XbRes_LastError()==-908 && job.state==4);}
  else{assert(!XbRes_LastError() && job.state==5 && live==23 && requests==4 && models==2);assert(BtlLoad_StepInitial(&job)==1 && relocated==2 && job_freed==1);}
  abort_and_verify();
 }
 for(int failure=1;failure<=4;failure++){
  reset();fail_request=failure;BtlJob job={0};job.state=4;assert(!BtlLoad_StepInitial(&job) && XbRes_LastError()==-23 && job.state==4 && live==22);abort_and_verify();
 }
 for(int failure=1;failure<=2;failure++)for(int kind=0;kind<2;kind++){
  reset();fail_model=failure;model_error=kind;BtlJob job={0};job.state=4;assert(!BtlLoad_StepInitial(&job) && !allocations && !requests && XbRes_LastError()==(kind?-23:-907));abort_and_verify();
 }
 reset();XbRes_SetReadError(-22);BtlJob failed={0};failed.state=4;assert(!BtlLoad_StepInitial(&failed) && !allocations && !models);abort_and_verify();
 reset();BATTLE_RES->sndCommon=Heap_Alloc(2048,64,0,2);BATTLE_RES->sndStage=Heap_Alloc(2048,64,0,2);BATTLE_RES->sndChara[0]=Heap_Alloc(2048,64,0,2);BATTLE_RES->sndChara[1]=Heap_Alloc(2048,64,0,2);fail_sound=3;BtlJob sound={0};sound.state=2;assert(!BtlLoad_StepInitial(&sound) && sound.state==2 && sound_calls==3 && XbRes_LastError()==-24);abort_and_verify();
 reset();fail_alloc=10;BtlJob retry={0};retry.state=4;assert(!BtlLoad_StepInitial(&retry) && XbRes_LastError()==-908);abort_and_verify();
 /* Retry on the SAME heap/configuration; only the read-error scope and
  * injected-fault counters reset. No Common/Battle/Heap initializer runs. */
 allocations=requests=models=fail_alloc=0;stopped=closed=drained=unloads=pool_resets=relocated=job_freed=0;
 assert(!XbRes_BeginCheckedReads());retry.state=4;assert(!BtlLoad_StepInitial(&retry) && retry.state==5 && !XbRes_LastError());assert(BtlLoad_StepInitial(&retry)==1);abort_and_verify();
 reset();script=0;assert(!XbBtlLoad_AbortInitial() && !stopped && !closed && !drained);script=-1;fighters=1;assert(!XbBtlLoad_AbortInitial() && !stopped);fighters=0;original_object_owner=0;assert(!XbBtlLoad_AbortInitial() && !stopped);XbRes_EndCheckedReads();
 assert(!munmap(arena,arena_size));
 puts("PASS original initial-job phase4/5 + actual game heap CONTROL FLOW: 23 allocation failures, 4 read refusals, 4 model failures, latched/audio errors; typed owners retained, scoped abort frees exactly once in order; script/live-fighter abort refused. Heap free space restored after every failure; IO/audio/formats/model requests simulated; no successful real battle boot.");return 0;
}
