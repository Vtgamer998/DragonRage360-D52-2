/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "battle/battle_work.h"
#include "battle/battle_setup.h"
#include "sys/common.h"
#include "sys/file.h"
#include "sys/job.h"
#include "sys/loading.h"
#include "sys/save.h"
#include "sys/snd.h"
#include "sys/heap.h"
#include "xb_resconv.h"
#include "xb_sound_bank.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
FileReqQueue gFileReq;Queue *gJobQueue;BtlJobPool gBtlJobPool;
static union {uint64_t alignment;uint8_t bytes[0x20+sizeof(BattleRes)];} common;
CommonRes *gCommonRes=(CommonRes *)common.bytes;
static SaveData save;SaveData *gSaveData=&save;
static Progress progress;Progress *gProgress=&progress;
#define BATTLE_RES ((BattleRes *)((u8 *)gCommonRes+0x20))
#define BTL_STAGE_BANK(stage) ((stage)+BTL_FILE_SND_STAGE)
#define File_Request3(id,buf,size) XbFile_RequestBounded(id,buf,size)
void XbRes_TestRead(int,int,uint64_t,uint64_t,void *,size_t);
static struct Allocation {void *p;size_t n;} allocations[32];
static int live,reads,uploads;
void *Heap_Alloc(s32 n,u32 a,s32 f,s32 h){(void)a;(void)f;(void)h;assert(n>0);void *p=calloc(1,(size_t)n);assert(p);for(unsigned i=0;i<32;i++)if(!allocations[i].p){allocations[i]=(struct Allocation){p,(size_t)n};live++;return p;}assert(0);return NULL;}
void Heap_Free(void *p){if(!p)return;for(unsigned i=0;i<32;i++)if(allocations[i].p==p){XbRes_Forget(p,allocations[i].n);free(p);allocations[i].p=NULL;live--;return;}assert(!"double/unknown free");}
unsigned XbHeap_AllocationBytes(const void *p){for(unsigned i=0;i<32;i++)if(allocations[i].p==p)return (unsigned)allocations[i].n;return 0;}
static struct Input {int id,status;uint8_t *bytes;size_t n;} inputs[4];
void *File_OpenById(s32 id){for(unsigned i=0;i<4;i++)if(inputs[i].id==id){inputs[i].status=ADXF_STAT_STOP;return &inputs[i];}assert(!"unexpected resource phase");return NULL;}
static int ADXF_GetFsizeSct(ADXF h){struct Input *in=h;return (int)((in->n+2047)/2048);}
static int ADXF_GetStat(ADXF h){struct Input *in=h;return in->status;}
static int ADXF_ReadNw(ADXF h,int sectors,void *dst){struct Input *in=h;size_t n=(size_t)sectors*2048;assert(n>=in->n);memset(dst,0,n);memcpy(dst,in->bytes,in->n);XbRes_TestRead(1,in->id-1,0,in->n,dst,n);in->status=XbRes_LastError()?ADXF_STAT_ERROR:ADXF_STAT_READEND;reads++;return sectors;}
static void ADXF_Close(ADXF h){assert(h);}
static int XbSfx_TryVagMax(const void *p,int *out){(void)p;(void)out;return 0;}
static int XbSfx_TryLoopFlags(const void *p,void *out){(void)p;(void)out;return 0;}
static u32 SpuHeap_Alloc(s32 n){(void)n;assert(0);return 0;}
static void *IopHeap_Alloc(s32 n){(void)n;assert(0);return NULL;}
/* Device upload is mocked; original Snd_SetupBank still consumes the file. */
void Snd_LoadBank(u32 mask,SndBankFile *file,s32 sync){assert(!sync && !XbSoundBank_RequireReadBank(file,mask));SndBank bank={0};Snd_SetupBank(&bank,mask,file,0);assert(bank.bodySize>0 && bank.vagMax>0);uploads++;}
s32 Snd_IsUploadDone(void){return uploads==4;}
s32 Battle_GetStartStage(void){return 0;}s32 BattleSide_GetStartChara(s32 side){assert(side==0 || side==1);return side;}
s32 Battle_GetScript(void){return -1;}
/* Unexercised model/script phases must trap if the fixture advances too far. */
s32 BattleSide_GetStartCostume(s32 x){(void)x;assert(0);return 0;}
s32 BattleSide_GetStartVariant(s32 x){(void)x;assert(0);return 0;}
s32 BtlObj_RequestCharaModel(s32 a,s32 b,s32 c,s32 d){(void)a;(void)b;(void)c;(void)d;assert(0);return 0;}
void BattleSide_SetModelSlot(s32 a,s32 b){(void)a;(void)b;assert(0);}
BattleMember *BattleSide_GetMember(s32 a,s32 b){(void)a;(void)b;assert(0);return NULL;}
s32 BattleSide_GetMemberCount(s32 a){(void)a;assert(0);return 0;}
s32 Battle_IsSplitScreen(void){assert(0);return 0;}
void Res_RelocateOffsets(void *a,void *b,void *c){(void)a;(void)b;(void)c;assert(0);}
BattleEvents *Battle_GetEventWork(void){assert(0);return NULL;}
s32 Gsc_LoadFile(void *p){(void)p;assert(0);return 0;}
void Gsc_RunAction(s32 a,s32 b){(void)a;(void)b;assert(0);}
void BtlScript_ScanEvents(s32 a){(void)a;assert(0);}
void BtlScript_Nop(void){assert(0);}
void BattleSetup_Finish(void){assert(0);}
/* ORIGINAL_FUNCTIONS */
int main(int argc,char **argv){
 assert(argc==5);static const int ids[4]={0x14a,0x14e,0xbda,0xbdb};assert(!XbSoundBank_RegisterInitial());
 for(unsigned i=0;i<4;i++){FILE *f=fopen(argv[i+1],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);assert(n>0);rewind(f);inputs[i].id=ids[i];inputs[i].n=(size_t)n;inputs[i].bytes=malloc((size_t)n);assert(inputs[i].bytes && fread(inputs[i].bytes,1,(size_t)n,f)==(size_t)n);fclose(f);}
 for(int pass=0;pass<2;pass++){
  assert(!live);memset(&common,0,sizeof(common));uploads=reads=0;File_InitRequests();Job_Init();assert(live==2);assert(!XbRes_BeginCheckedReads());
  BtlLoad_PushInitialJob();assert(Job_GetCount()==1 && File_GetPendingCount()==4 && live==6);
  BtlJob *job=Queue_Peek(gJobQueue);assert(job && job->state==1);
  int steps=0;while(job->state<4 && steps++<64){assert(Job_Run()==1 && !XbRes_LastError());}
  assert(job->state==4 && reads==4 && uploads==4 && File_GetPendingCount()==0 && live==2);
  assert(!BATTLE_RES->sndCommon && !BATTLE_RES->sndStage && !BATTLE_RES->sndChara[0] && !BATTLE_RES->sndChara[1]);
  Job_Clear();Job_Destroy();assert(!gJobQueue && !live);XbRes_EndCheckedReads();
 }
 for(unsigned i=0;i<4;i++)free(inputs[i].bytes);
 puts("PASS original initial job states 0..3 + original Job/Queue/File request scheduling + original bank metadata with 4 real banks, twice. STOP before model phase 4. Host IO/heap/device upload mocked; not Battle_Load success or audible audio.");return 0;
}
