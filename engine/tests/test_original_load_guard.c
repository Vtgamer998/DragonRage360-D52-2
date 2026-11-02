/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_battle_boot.h"
#include "xb_resconv.h"
#include "battle/btl_obj.h"
#include "sys/file.h"
#include "sys/job.h"
#include "sys/common.h"
#include "sys/loading.h"
#include "sys/save.h"
#include "sys/snd.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
BtlObjWork *gBtlObjWork;FileReqQueue gFileReq;Queue *gJobQueue;s32 gGscActive;
static int diagnostic_resources;
CommonRes *gCommonRes;Progress *gProgress;SaveData *gSaveData;SndMgr *gSndMgr;SndRpc *gSndRpc;
static CommonRes common;static Progress progress;static SaveData save;static SndMgr sound;static SndRpc rpc;
static unsigned char dummy;static LoadSprite sprites[LOAD_SPRITE_COUNT];
static int root_format(void *p,size_t n,int id){(void)p;(void)n;(void)id;assert(!"root formats mocked, never parsed by guard test");return -1;}

int XbBattleRuntime_HasResources(void){return diagnostic_resources;}
/* ORIGINAL_FUNCTIONS */
int main(void){
 assert(!XbRes_RegisterChecked(2,4,root_format,0));
 Queue queue;QueueNode nodes[JOB_MAX];memset(&queue,0,sizeof(queue));memset(nodes,0,sizeof(nodes));
 assert(!XbBattleBoot_ObjectStateClear() && !XbBattleBoot_OriginalObjectStateActive());
 assert(!XbRes_BeginCheckedReads());assert(!XbBattleBoot_ObjectStateClear());gJobQueue=&queue;File_InitRequests();assert(!XbBattleBoot_ObjectStateClear());
 queue.nodes=nodes;queue.capacity=JOB_MAX;assert(!XbBattleBoot_ObjectStateClear());
 gCommonRes=&common;common.boot=&dummy;for(unsigned i=0;i<COMMON_FILE_COUNT;i++){common.data[i]=&dummy;common.size[i]=1;}
 gProgress=&progress;progress.loadPack=progress.loadRes=&dummy;progress.loadSprites=sprites;
 gSaveData=&save;gSndMgr=&sound;gSndRpc=&rpc;assert(!XbBattleBoot_ObjectStateClear());
 XbRes_EndCheckedReads();assert(!XbRes_RegisterChecked(5,5,root_format,0));assert(!XbRes_BeginCheckedReads());assert(XbBattleBoot_ObjectStateClear());
 queue.head=nodes;assert(!XbBattleBoot_ObjectStateClear());queue.head=NULL;
 FileReq *req=(FileReq *)List_PopFront(&gFileReq.free);assert(req);List_PushBack(&gFileReq.pending,&req->node);assert(!XbBattleBoot_ObjectStateClear());
 static unsigned char work;gBtlObjWork=(BtlObjWork *)&work;assert(!XbBattleBoot_OriginalObjectStateActive());gGscActive=1;assert(XbBattleBoot_OriginalObjectStateActive());
 diagnostic_resources=1;assert(!XbBattleBoot_OriginalObjectStateActive());diagnostic_resources=0;
 gBtlObjWork=NULL;assert(!XbBattleBoot_ObjectStateClear());gGscActive=0;List_AppendList(&gFileReq.free,&gFileReq.pending);assert(XbBattleBoot_ObjectStateClear());
 gFileReq.adxf=&work;assert(!XbBattleBoot_ObjectStateClear());gFileReq.adxf=NULL;
 diagnostic_resources=1;assert(!XbBattleBoot_ObjectStateClear());diagnostic_resources=0;
 XbRes_EndCheckedReads();assert(!XbBattleBoot_ObjectStateClear() && !XbBattleBoot_OriginalObjectStateActive());
 puts("PASS actual original-load guards with original Job/Queue/List/File readers: requires strict transport, common bindings/roots and initialized empty queues; refuses existing GSC/objects/diagnostic resources/active IO; recovery guard allows a nonempty initialized read queue; common format bindings/root objects mocked");return 0;
}
