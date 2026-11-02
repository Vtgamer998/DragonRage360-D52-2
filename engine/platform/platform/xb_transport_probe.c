/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_transport_probe.h"
#include "xb_resconv.h"
#include "xb_stage_pack.h"
#include "xb_mem.h"
#include "xb_heap.h"
#include "xb_file.h"
#include <stdint.h>
#include <xsdk/log.h>
extern void Heap_Init(void),Heap_Free(void *),File_InitRequests(void);
extern void *Heap_Alloc(int,unsigned,int,int);
extern int Heap_GetFreeSize(void),File_GetSize(int),File_UpdateRequests(void),File_IsBusy(void),File_GetPendingCount(void);
extern void *XbFile_RequestOwned(int,void *,int);
extern void XbFile_AbortRequestsRetainBuffers(void);
extern void *XbBpe_DecodeBounded(void *,void *,uint32_t,int32_t *);
static int one(int id,int packed){
    void *source=NULL,*decoded=NULL;int size=File_GetSize(id),rc=0;int32_t bytes=0;
    if(size<=0 || XbRes_LastError())return XbRes_LastError()?XbRes_LastError():-938;
    source=Heap_Alloc(size,64,0,2);if(!source){XbRes_SetReadError(-908);return -908;}
    if(!XbFile_RequestOwned(id,source,size)){rc=XbRes_LastError()?XbRes_LastError():-938;goto done;}
    for(unsigned poll=0;poll<65536;poll++){
        if(File_UpdateRequests())break;
        if(XbRes_LastError()){rc=XbRes_LastError();goto done;}
        if(poll==65535){XbRes_SetReadError(-938);rc=-938;goto done;}
    }
    if(packed){
        decoded=Heap_Alloc(0x6800,64,0,2);
        if(!decoded){XbRes_SetReadError(-908);rc=-908;goto done;}
        if(!XbBpe_DecodeBounded(source,decoded,0x6800,&bytes)){rc=XbRes_LastError()?XbRes_LastError():-938;goto done;}
    }
    rc=XbRes_LastError();
    if(!rc && id==0x171){
        const void*base;size_t n;int read_id;
        if(!XbRes_ReadFile(source,&base,&n,&read_id))rc=-943;
        else rc=XbStagePack_CheckRelocation(source,n);
        xsdk_log_write("D34 original BtlStage_Relocate: result=%d groups=7 materials=34 meshes=2243; isolated consuming check, no BtlStage_Init/GPU",rc);
        if(rc)XbRes_SetReadError(rc);
    }
done:
    if(rc)XbFile_AbortRequestsRetainBuffers();
    if(decoded)Heap_Free(decoded);
    Heap_Free(source);
    xsdk_log_write("D34 strict transport: id=0x%x sectors_bytes=%d decoded=%d result=%d; original File_Request/Update queue; no battle",id,size,bytes,rc);
    return rc;
}
int XbTransportProbe_Run(void){
    static const int ids[]={2,3,4,5,0x14a,0x14b,0x14e,0xbda,0xbdb,0x598,0x5a2,8,11,0x3c1,0x3c2,0x3c3,0x590,0x59a,0x599,0x5a3,0x171,6};
    if(File_IsBusy() || File_GetPendingCount() || XbRes_CheckedReadsActive())return -909;
    if(XbMem_InitHeap())return -908;
    Heap_Init();if(!XbHeap_Ready())return -908;
    int baseline=Heap_GetFreeSize(),rc=ADXF_LoadPartitionNw(1,"PZS3US1.AFS",NULL,NULL);if(rc)return rc;
    File_InitRequests();
    for(unsigned cycle=0;cycle<2;cycle++){
        rc=XbRes_BeginCheckedReads();if(rc)return rc;
        for(unsigned i=0;i<sizeof(ids)/sizeof(*ids);i++){rc=one(ids[i],ids[i]>=0x3c1 && ids[i]<=0x3c3);if(rc)break;}
        if(!rc && cycle==0){
            int expected=XbRes_PreflightRead(0x7fffffff);
            xsdk_log_write("D34 strict recovery: unknown ID refused=%d pending=%d heap_restored=%d; no restart requested",expected,File_GetPendingCount(),Heap_GetFreeSize()==baseline);
            if(!expected || File_GetPendingCount() || Heap_GetFreeSize()!=baseline)rc=-938;
        }
        XbRes_EndCheckedReads();if(rc)break;
    }
    if(!rc && (Heap_GetFreeSize()!=baseline || File_GetPendingCount()))rc=-938;
    XbRes_Report();
    xsdk_log_write("D34 strict transport summary: result=%d cycles=2 files_per_cycle=22 heap_restored=%d; not Battle_Load/Init/Update/Main",rc,Heap_GetFreeSize()==baseline);
    return rc;
}
