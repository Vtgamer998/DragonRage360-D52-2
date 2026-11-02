# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loading error propagation and bounded pre-fighter no-script unwind.
Unsupported partial initialization/script failures remain fail-stop.
No partial Battle_Term is attempted: downstream subsystems may not exist.
"""
def replace(text,anchor,value):
    if text.count(anchor)!=1:raise ValueError('loading error anchor changed: '+anchor[:70])
    return text.replace(anchor,value)

def transform(source,text):
    if source=='src/sys/file.c':
        # Original boot owns these buffers until explicit session teardown.
        for old,new in [
            ('    File_InitAdx();','    File_InitAdx();\n    { extern int XbRes_LastError(void); if(XbRes_LastError())return; }'),
            ('    fc->buf = Heap_Alloc(0x190, 0x20, 0, HEAP_ANY);','    fc->buf = Heap_Alloc(0x190, 0x20, 0, HEAP_ANY);\n    if(!fc->buf){extern void XbRes_SetReadError(int);XbRes_SetReadError(-908);return;}'),
            ('        part->info = Heap_Alloc(part->infoSize, 0x20, 0, HEAP_ANY);','        part->info = Heap_Alloc(part->infoSize, 0x20, 0, HEAP_ANY);\n        if(!part->info){extern void XbRes_SetReadError(int);XbRes_SetReadError(-908);return;}'),
            ('        player->work = Heap_Alloc(player->workSize, 0x20, 0, HEAP_ANY);','        player->work = Heap_Alloc(player->workSize, 0x20, 0, HEAP_ANY);\n        if(!player->work){extern void XbRes_SetReadError(int);XbRes_SetReadError(-908);return;}'),
            ('        player->adxt = ADXT_Create(2, player->work, player->workSize);','        player->adxt = ADXT_Create(2, player->work, player->workSize);\n        if(!player->adxt){extern void XbRes_SetReadError(int);XbRes_SetReadError(-951);return;}')]:text=replace(text,old,new)
        text=replace(text,'s32 File_WaitPartition(s32 pt) {','s32 File_WaitPartition(s32 pt) {\n    unsigned polls=0;')
        text=replace(text,'    while (ADXF_GetPtStat(pt) != ADXF_STAT_READEND) {','    while (ADXF_GetPtStat(pt) != ADXF_STAT_READEND) {\n        if(ADXF_GetPtStat(pt)==ADXF_STAT_ERROR || ++polls>2400){\n            extern void XbRes_SetReadError(int);XbRes_SetReadError(-952);return 0;\n        }')
        cancel=text.index('void File_CancelRequests(void) {');finish=text.index('/* Queues an asynchronous read',cancel)
        text=text[:cancel]+'''/* Stop first, then return all pending entries. Already-completed outputs
 * belong to their callers; borrowed destinations are never freed here. */
void XbFile_AbortRequestsRetainBuffers(void) {
    FileReqQueue *queue=&gFileReq;
    FileReq *req;
    if (queue->adxf!=NULL) { ADXF_Stop(queue->adxf); ADXF_Close(queue->adxf); }
    for (req=(FileReq *)List_GetHead(&queue->pending);req!=NULL;req=(FileReq *)List_GetNext(&req->node)) {
        req->flags=0; req->buf=NULL;
    }
    queue->state=FILE_QUEUE_IDLE; queue->adxf=NULL;
    List_AppendList(&queue->free,&queue->pending);
}

void File_CancelRequests(void) {
    FileReqQueue *queue=&gFileReq;
    FileReq *req;
    if (queue->adxf!=NULL) { ADXF_Stop(queue->adxf); ADXF_Close(queue->adxf); queue->adxf=NULL; }
    for (req=(FileReq *)List_GetHead(&queue->pending);req!=NULL;req=(FileReq *)List_GetNext(&req->node)) {
        if (req->flags & FILE_REQ_OWNS_BUF) Heap_Free(req->buf);
    }
    XbFile_AbortRequestsRetainBuffers();
}

'''+text[finish:]
        start=text.index('s32 File_UpdateRequests(void) {');end=text.index('/* Size of file',start)
        part=text[start:end]
        part=replace(part,'    if (File_GetPendingCount() == 0) {','''    extern int XbRes_LastError(void);
    extern void XbRes_SetReadError(int);
    extern int XbRes_CheckedReadsActive(void);
    if (XbRes_LastError()) return 0;
    if (queue->adxf != NULL && ADXF_GetStat(queue->adxf) == ADXF_STAT_ERROR) {
        XbRes_SetReadError(-898);
        return 0;
    }
    if (File_GetPendingCount() == 0) {''')
        part=replace(part,'        queue->adxf = File_OpenById(req->id);','''        queue->adxf = File_OpenById(req->id);
        if (queue->adxf == NULL && XbRes_CheckedReadsActive()) { XbRes_SetReadError(-898); return 0; }''')
        part=replace(part,'    return done;','''    if (XbRes_LastError()) return 0;
    if (queue->adxf != NULL && ADXF_GetStat(queue->adxf) == ADXF_STAT_ERROR) {
        XbRes_SetReadError(-898);
        return 0;
    }
    return done;''')
        text=text[:start]+part+text[end:]
        for signature,stop in [('void *File_Request(s32 id, void *buf) {','/* Advances the request queue'),('void *File_LoadSyncEx(s32 id, char *name, void *buf, s32 unused) {','/* Returns 1 while the request queue')]:
            begin=text.index(signature);finish=text.index(stop,begin);body=text[begin:finish]
            anchor='    do {\n'
            guard='''    extern int XbRes_PreflightRead(int), XbRes_CheckedReadsActive(void);
    extern int XbRes_LastError(void);
    extern void XbRes_SetReadError(int);
    if (XbRes_PreflightRead(id)) return NULL;
'''
            if 'File_Request' in signature:
                guard+='''    if (XbRes_CheckedReadsActive() && List_GetCount(&queue->free)==0) { XbRes_SetReadError(-898); return NULL; }
'''
            if body.count(anchor)!=1 and 'File_Request' in signature:raise ValueError('request open changed')
            body=body.replace(anchor,guard+anchor,1)
            body=replace(body,'    } while (adxf == NULL);','''        if (adxf == NULL && XbRes_CheckedReadsActive()) { XbRes_SetReadError(-898); return NULL; }
    } while (adxf == NULL);''')
            body=replace(body,'    sectors = ADXF_GetFsizeSct(adxf);','''    sectors = ADXF_GetFsizeSct(adxf);
    if (XbRes_CheckedReadsActive() && (sectors<=0 || sectors>0x7fffffff/FILE_SECTOR_SIZE)) { ADXF_Close(adxf); XbRes_SetReadError(-898); return NULL; }''')
            capacity='capacity' if 'File_Request' in signature else 'unused'
            body=replace(body,'    dst = buf;',f'''    if (XbRes_CheckedReadsActive() && buf && ({capacity}<=0 || sectors>{capacity}/FILE_SECTOR_SIZE)) {{ ADXF_Close(adxf); XbRes_SetReadError(-900); return NULL; }}
    dst = buf;''')
            body=replace(body,'    ADXF_Close(adxf);' if 'File_Request' in signature else '    while (ADXF_ReadNw(adxf, sectors, dst) != sectors) {',
                '''    ADXF_Close(adxf);
    if (dst == NULL && XbRes_CheckedReadsActive()) { XbRes_SetReadError(-898); return NULL; }''' if 'File_Request' in signature else '''    if (dst == NULL && XbRes_CheckedReadsActive()) { ADXF_Close(adxf); XbRes_SetReadError(-898); return NULL; }
    while (ADXF_ReadNw(adxf, sectors, dst) != sectors) {
        if (XbRes_LastError() || ADXF_GetStat(adxf)==ADXF_STAT_ERROR) { if (!XbRes_LastError()) XbRes_SetReadError(-898); ADXF_Close(adxf); return NULL; }''')
            if 'File_LoadSyncEx' in signature:
                body=replace(body,'        ADXM_ExecMain();','''        ADXM_ExecMain();
        if (XbRes_LastError() || ADXF_GetStat(adxf)==ADXF_STAT_ERROR) { if (!XbRes_LastError()) XbRes_SetReadError(-898); ADXF_Close(adxf); return NULL; }''')
            if 'File_Request' in signature:
                body=replace(body,signature,'void *XbFile_RequestBounded(s32 id, void *buf, s32 capacity) {')
                body+='''
void *File_Request(s32 id, void *buf) {
    return XbFile_RequestBounded(id,buf,-1);
}

void *XbFile_RequestOwned(s32 id, void *buf, s32 nominal) {
    extern unsigned XbHeap_AllocationBytes(const void *);
    extern int XbRes_CheckedReadsActive(void);
    extern void XbRes_SetReadError(int);
    unsigned capacity;
    if (!buf || !XbRes_CheckedReadsActive()) return XbFile_RequestBounded(id,buf,nominal);
    capacity = XbHeap_AllocationBytes(buf);
    if (!capacity || capacity>0x7fffffffU) { XbRes_SetReadError(-900); return NULL; }
    return XbFile_RequestBounded(id,buf,(s32)capacity);
}
'''
            if 'File_LoadSyncEx' in signature:
                body=body.replace('ADXF_Close(adxf); return NULL; }', 'ADXF_Stop(adxf); ADXF_Close(adxf); if (!buf && dst) Heap_Free(dst); return NULL; }')
            text=text[:begin]+body+text[finish:]
        begin=text.index('s32 File_GetSize(s32 id) {');end=text.index('/* Blocking read of a file given by path',begin)
        body=text[begin:end]
        body=replace(body,'    do {','''    extern int XbRes_PreflightRead(int), XbRes_CheckedReadsActive(void);
    extern void XbRes_SetReadError(int);
    if (XbRes_PreflightRead(id)) return 0;
    do {''')
        body=replace(body,'    } while (adxf == NULL);','''        if (adxf == NULL && XbRes_CheckedReadsActive()) { XbRes_SetReadError(-898); return 0; }
    } while (adxf == NULL);''')
        body=replace(body,'    return sectors * FILE_SECTOR_SIZE;','''    if (XbRes_CheckedReadsActive() && (sectors<=0 || sectors>0x7fffffff/FILE_SECTOR_SIZE)) { XbRes_SetReadError(-898); return 0; }
    return sectors * FILE_SECTOR_SIZE;''')
        text=text[:begin]+body+text[end:]
        return text
    if source=='src/sys/loading.c':
        begin=text.index('void Load_RunBlocking(void) {');end=text.index('/* Fills the sprite',begin)
        before,after=text[:begin],text[end:];text=text[begin:end]
        text=replace(text,'    PadWatch_SetEnabled(0);','''    extern int XbRes_LastError(void);
    if (XbRes_LastError()) return;
    PadWatch_SetEnabled(0);''')
        text=replace(text,'    Load_InitScreen();','''    Load_InitScreen();
    if (XbRes_LastError()) {
        Dma_ResetBuffers();
        PadWatch_SetEnabled(1);
        return;
    }''')
        text=replace(text,'        busy = Job_Run();','''        busy = Job_Run();
        if (XbRes_LastError()) {
            Gfx_EndFrame(2);
            Dma_Flush();
            {extern int XbFreshBoot_Frame(int);XbFreshBoot_Frame(2);}
            break;
        }''')
        text=replace(text,'        Gfx_BeginFrame();','        {extern int XbFreshBoot_Frame(int);if(XbFreshBoot_Frame(1))break;}\n        Gfx_BeginFrame();')
        text=replace(text,'        if (done) {','        {extern int XbFreshBoot_Frame(int);if(XbFreshBoot_Frame(2))break;}\n        if (done) {')
        text=before+text+after
        old="""    do {
        gLoadScreen.type = Rand_Range(LOAD_TYPE_COUNT);
    } while (gLoadScreen.type + 1 == gProgress->lastLoadType);"""
        new="""    { extern int XbFreshBoot_ForcedLoadingType(void);int forced=XbFreshBoot_ForcedLoadingType();
      if(forced>=0)gLoadScreen.type=forced;
      else { do {
        gLoadScreen.type = Rand_Range(LOAD_TYPE_COUNT);
      } while(gLoadScreen.type+1==gProgress->lastLoadType); }
    }"""
        text=replace(text,old,new)
        anchor='    Sprite_Unpack(gProgress->loadPack, gProgress->loadRes, NULL);'
        text=replace(text,anchor,'''    { extern int XbRes_LastError(void);
      extern void *XbBpe_DecodeBounded(void *,void *,u32,s32 *);
      if (XbRes_LastError()) return;
      if (!XbBpe_DecodeBounded(gProgress->loadPack,gProgress->loadRes,0x6800,NULL)) return;
    }''')
        text=replace(text,'    gLoadScreen.res = gProgress->loadRes;','    { extern int XbRes_LastError(void); if (XbRes_LastError()) return; }\n    gLoadScreen.res = gProgress->loadRes;')
        return text
    if source=='src/battle/battle_work.c':
        text=replace(text,'s32 Battle_Load(void) {','''s32 Battle_Load(void) {
    extern int XbBattleBoot_ObjectStateClear(void),XbRes_LastError(void);
    extern void XbRes_SetReadError(int);
    /* Never initialize the original registry over a diagnostic or live one. */
    if (XbRes_LastError()) return 0;
    if (!XbBattleBoot_ObjectStateClear()) { XbRes_SetReadError(-902); return 0; }''')
        return replace(text,'    Load_RunBlocking();','''    { extern int XbFreshBoot_DrivesLoading(void);
      extern void XbFreshBoot_PumpLoading(void);
      if (XbFreshBoot_DrivesLoading()) XbFreshBoot_PumpLoading();
      else Load_RunBlocking();
    }
    if (XbRes_LastError()) {
        extern int XbBtlLoad_AbortInitial(void);
        /* These initializers already returned, and no fighters exist yet.
         * Script loads and failures inside the initializers remain fail-stop. */
        if (XbBtlLoad_AbortInitial()) {
            BtlObj_Term(); BtlScript_Free(); Gsc_Exit();
        }
        return 0;
    }''')
    if source in ('src/battle/battle_load.c','src/battle/btl_obj.c'):
        anchor='#define File_Request3(id, buf, size) ((void *(*)(s32, void *, s32))File_Request)(id, buf, size)'
        # BtlResFile.size is the current FILE size, overwritten by SetFileId;
        # it is not the capacity of a fixed model/animation allocation.
        api='XbFile_RequestOwned' if source=='src/battle/btl_obj.c' else 'XbFile_RequestBounded'
        text=replace(text,anchor,f'''extern void *{api}(s32,void *,s32);
#define File_Request3(id, buf, size) {api}(id, buf, size)''')
        from initial_load_source import transform as initial_transform
        return initial_transform(source,text)
    return text
