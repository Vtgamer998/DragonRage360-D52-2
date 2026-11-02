/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "sys/file.h"
#include "sys/heap.h"
#include "xb_resconv.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
FileReqQueue gFileReq;
static int reads,mode,status,opens,frees,stops,closes,size_override;
static char allocation[2048];
unsigned XbHeap_AllocationBytes(const void *p){return p==allocation?sizeof(allocation):0;}
void *Heap_Alloc(s32 n,u32 align,s32 flags,s32 heap){(void)align;(void)flags;(void)heap;assert(n==2048);return allocation;}
static int ADXF_GetFsizeSct(void *h){assert(h==&mode);return size_override?size_override:1;}
static void ADXM_ExecMain(void){}
static void *ADXF_Open(char *name,void *attr){(void)name;(void)attr;assert(!"unexpected named read");return NULL;}
void *File_OpenById(int id){assert(id==100);opens++;status=ADXF_STAT_STOP;return mode==3?NULL:&mode;}
static int ADXF_GetStat(void *handle){assert(handle==&mode);return status;}
static int ADXF_ReadNw(void *handle,int sectors,void *buf){assert(handle==&mode && sectors==1 && buf);reads++;if(mode){status=ADXF_STAT_ERROR;if(mode==2)XbRes_SetReadError(-23);return 0;}status=ADXF_STAT_READEND;return sectors;}
static void ADXF_Close(void *handle){assert(handle==&mode);closes++;}
static void ADXF_Stop(void *handle){assert(handle==&mode);stops++;status=ADXF_STAT_STOP;}
void Heap_Free(void *p){assert(p==allocation);XbRes_Forget(p,sizeof(allocation));frees++;}
static int converter(void *p,size_t n,int id){(void)p;(void)n;assert(id==100);return 0;}
/* ORIGINAL_FUNCTIONS */
static FileReq *queue_request(void){File_InitRequests();FileReq *r=(FileReq *)List_PopFront(&gFileReq.free);assert(r);r->id=100;r->sectors=1;r->buf=&mode;List_PushBack(&gFileReq.pending,&r->node);return r;}
int main(void){
 assert(!XbRes_RegisterChecked(100,100,converter,0));
 for(mode=0;mode<4;mode++){
  reads=0;assert(!XbRes_BeginCheckedReads());FileReq *r=queue_request();int done=0;for(int step=0;step<8 && !done;step++)done=File_UpdateRequests();
  if(!mode){assert(done && reads==1 && (r->flags&FILE_REQ_DONE) && File_GetPendingCount()==0);}
  else{assert(!done && reads==(mode==3?0:1) && !(r->flags&FILE_REQ_DONE) && File_GetPendingCount()==1);assert(XbRes_LastError()==(mode==2?-23:-898));}
  XbRes_EndCheckedReads();
 }
 mode=0;assert(!XbRes_BeginCheckedReads());File_InitRequests();opens=reads=0;
 assert(File_Request(999,NULL)==NULL && opens==0 && reads==0 && File_GetPendingCount()==0);XbRes_EndCheckedReads();
 mode=0;assert(!XbRes_BeginCheckedReads());File_InitRequests();opens=reads=0;
 assert(XbFile_RequestBounded(100,allocation,sizeof(allocation))==allocation && File_GetPendingCount()==1);
 int ready=0;for(int step=0;step<8 && !ready;step++)ready=File_UpdateRequests();assert(ready && reads==1 && File_GetPendingCount()==0);XbRes_EndCheckedReads();
 for(int cap=-1;cap<2048;cap+=1025){
  mode=0;assert(!XbRes_BeginCheckedReads());File_InitRequests();opens=reads=0;
  assert(!XbFile_RequestBounded(100,allocation,cap) && XbRes_LastError()==-900 && !reads && !File_GetPendingCount());
  XbRes_EndCheckedReads();
 }
 mode=0;assert(!XbRes_BeginCheckedReads());File_InitRequests();
 assert(!File_Request(100,allocation) && XbRes_LastError()==-900);XbRes_EndCheckedReads();
 mode=0;assert(!XbRes_BeginCheckedReads());File_InitRequests();reads=0;
 /* Original BtlResFile.size may be a non-sector-aligned FILE size. The
  * allocator's destination extent, not that nominal size, bounds the DMA. */
 assert(XbFile_RequestOwned(100,allocation,1001)==allocation);
 int owned_ready=0;for(int step=0;step<8 && !owned_ready;step++)owned_ready=File_UpdateRequests();
 assert(owned_ready && reads==1 && !XbRes_LastError());XbRes_EndCheckedReads();
 assert(!XbRes_BeginCheckedReads());File_InitRequests();opens=0;
 assert(!XbFile_RequestOwned(100,allocation+1,2048) && !opens && XbRes_LastError()==-900);XbRes_EndCheckedReads();
 assert(!XbRes_BeginCheckedReads());reads=0;
 assert(!File_LoadSyncEx(100,NULL,allocation,2047) && !reads && XbRes_LastError()==-900);XbRes_EndCheckedReads();
 for(mode=0;mode<4;mode++){
  assert(!XbRes_BeginCheckedReads());opens=reads=0;void *p=File_LoadSyncEx(100,NULL,allocation,sizeof(allocation));
  if(mode==0)assert(p==allocation && reads==1 && !XbRes_LastError());
  else assert(p==NULL && reads==(mode==3?0:1) && XbRes_LastError()==(mode==2?-23:-898));
  XbRes_EndCheckedReads();
 }
 for(int phase=0;phase<3;phase++){
  mode=phase==2?1:0;assert(!XbRes_BeginCheckedReads());File_InitRequests();frees=0;
  char borrowed[2048];memset(borrowed,0x77,sizeof(borrowed));
  assert(File_Request(100,NULL)==allocation);assert(XbFile_RequestBounded(100,borrowed,sizeof(borrowed))==borrowed);
  if(phase)File_UpdateRequests();
  if(phase==2)File_UpdateRequests();
  stops=closes=0;int error=XbRes_LastError();File_CancelRequests();
  assert(frees==1 && !File_GetPendingCount() && List_GetCount(&gFileReq.free)==FILE_REQ_MAX && gFileReq.state==FILE_QUEUE_IDLE && !gFileReq.adxf);
  assert(stops==(phase?1:0) && closes==(phase?1:0) && XbRes_LastError()==error);
  for(unsigned i=0;i<sizeof(borrowed);i++)assert((unsigned char)borrowed[i]==0x77);
  File_CancelRequests();assert(frees==1);XbRes_EndCheckedReads();
 }
 mode=0;assert(!XbRes_BeginCheckedReads());File_InitRequests();frees=0;assert(File_Request(100,NULL)==allocation);
 int completed=0;for(int n=0;n<8 && !completed;n++)completed=File_UpdateRequests();assert(completed);File_CancelRequests();assert(!frees);Heap_Free(allocation);assert(frees==1);XbRes_EndCheckedReads();
 mode=0;assert(!XbRes_BeginCheckedReads());File_InitRequests();frees=0;assert(File_Request(100,NULL)==allocation);XbFile_AbortRequestsRetainBuffers();assert(!frees && !File_GetPendingCount());Heap_Free(allocation);assert(frees==1);XbRes_EndCheckedReads();
 for(mode=0;mode<4;mode++){
  assert(!XbRes_BeginCheckedReads());frees=stops=0;
  void *p=File_LoadSyncEx(100,NULL,NULL,0);
  if(!mode){assert(p==allocation && !frees);Heap_Free(p);}
  else assert(!p && frees==(mode==3?0:1) && stops==(mode==3?0:1));
  XbRes_EndCheckedReads();
 }
 for(int test=0;test<4;test++){
  mode=test==1?3:0;size_override=test==2?-1:test==3?0x7fffffff:0;
  assert(!XbRes_BeginCheckedReads());opens=0;int n=File_GetSize(100);
  assert(opens==1);if(!test)assert(n==2048 && !XbRes_LastError());else assert(!n && XbRes_LastError()==-898);
  XbRes_EndCheckedReads();
 }
 size_override=0;mode=0;assert(!XbRes_BeginCheckedReads());opens=0;
 assert(!File_GetSize(999) && !opens && XbRes_LastError());XbRes_EndCheckedReads();
 puts("PASS original File_UpdateRequests: success completes; IO/conversion failures remain incomplete without retrying IO");return 0;
}
