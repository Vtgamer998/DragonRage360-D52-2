# SPDX-License-Identifier: AGPL-3.0-or-later
"""Bounded initial-job ownership; abort supports the no-script pre-fighter load.
Does not recover allocation failures inside BtlObj_Init/Gsc_Init themselves.
"""
import re

def one(text,anchor,value):
    if text.count(anchor)!=1:raise ValueError('initial-load source changed: '+anchor[:80])
    return text.replace(anchor,value)

def transform(source,text):
    if source=='src/battle/battle_load.c':
        begin=text.index('s32 BtlLoad_StepInitial(BtlJob *job) {');end=text.index('/* Starts a plain stage reload',begin)
        part=text[begin:end]
        part=one(part,'    switch (job->state) {','''    extern int XbRes_LastError(void);
    extern void XbRes_SetReadError(int);
    if (XbRes_LastError()) return 0;
    switch (job->state) {''')
        for side in (0,1):
            call=f'        BattleSide_SetModelSlot({side}, BtlObj_RequestCharaModel({side}, BattleSide_GetStartChara({side}), BattleSide_GetStartCostume({side}), BattleSide_GetStartVariant({side})));'
            part=one(part,call,f'''        {{
            s32 handle=BtlObj_RequestCharaModel({side},BattleSide_GetStartChara({side}),BattleSide_GetStartCostume({side}),BattleSide_GetStartVariant({side}));
            if (handle<0 || XbRes_LastError()) {{ XbRes_SetReadError(XbRes_LastError()?XbRes_LastError():-907); return 0; }}
            BattleSide_SetModelSlot({side},handle);
        }}''')
        for expr,size in [('res->stage','res->stageSize'),('res->bank','res->bankSize'),('m->buf[0]','BTL_MEMBER_BUF_SIZE'),('m->buf[1]','BTL_MEMBER_BUF_SIZE')]:
            anchor=f'        memset({expr}, 0, {size});'
            # Member lines have eight extra spaces. Preserve their indentation.
            pattern=re.compile(r'(?m)^(\s*)memset\('+re.escape(expr)+r', 0, '+re.escape(size)+r'\);$')
            matches=list(pattern.finditer(part))
            if len(matches)!=1:raise ValueError('initial allocation guard changed: '+expr)
            part=pattern.sub(lambda m:m[1]+f'if (!{expr}) {{ XbRes_SetReadError(-908); return 0; }}\n'+m[1]+f'memset({expr}, 0, {size});',part)
        pattern=re.compile(r'(?m)^([ \t]*)([^\n;]+?) = (File_Request3\([^\n]+\));$')
        assignments=list(pattern.finditer(part))
        if len(assignments)!=8:raise ValueError('initial request assignments changed: '+str(len(assignments)))
        def request(m):
            indent,destination,call=m.groups()
            return indent+'{\n'+indent+'    void *requested='+call+';\n'+indent+'    if (!requested) { XbRes_SetReadError(XbRes_LastError()?XbRes_LastError():-907); return 0; }\n'+indent+'    '+destination+'=requested;\n'+indent+'}'
        part=pattern.sub(request,part)
        for call in ('Snd_LoadBank(4, res->sndCommon, 0);','Snd_LoadBank(8, res->sndStage, 0);','Snd_LoadBank(0x10, res->sndChara[0], 0);','Snd_LoadBank(0x20, res->sndChara[1], 0);'):
            part=one(part,call,call+'\n        if (XbRes_LastError()) return 0;')
        text=text[:begin]+part+text[end:]
        anchor='/* Frees every buffer of the first load and unloads the four battle sound banks. */'
        cleanup='''/* Abort only a no-script initial job before fighter creation. The caller
 * still owns BtlObj/GSC initialization and must terminate those separately.
 * Retaining queue destinations lets typed owners release each buffer once. */
s32 XbBtlLoad_AbortInitial(void) {
    BattleRes *res;
    BattleMember *member;
    s32 side,i;
    extern void XbFile_AbortRequestsRetainBuffers(void);
    extern s32 BtlObj_GetCount(void);
    extern int XbBattleBoot_OriginalObjectStateActive(void);
    if (gCommonRes==NULL || !XbBattleBoot_OriginalObjectStateActive()) return 0;
    res=BATTLE_RES;
    if (Battle_GetScript()>=0 || res->script!=NULL || BtlObj_GetCount()!=0) return 0;
    Job_Clear();
    XbFile_AbortRequestsRetainBuffers();
    Snd_Reset();
    Snd_UnloadBank(4); Snd_UnloadBank(8);
    Snd_UnloadBank(0x10); Snd_UnloadBank(0x20);
    for (side=0;side<2;side++) {
        for (i=0;i<5;i++) {
            member=BattleSide_GetMember(side,i);
            Heap_Free(member->buf[0]); member->buf[0]=NULL;
            Heap_Free(member->buf[1]); member->buf[1]=NULL;
            member->data=NULL;
        }
        BattleSide_SetModelSlot(side,-1);
        BattleSide_SetObjId(side,-1);
    }
    Heap_Free(res->sndCommon); Heap_Free(res->sndStage);
    Heap_Free(res->sndChara[0]); Heap_Free(res->sndChara[1]);
    Heap_Free(res->stage); Heap_Free(res->bank);
    Heap_Free(res->unk8); Heap_Free(res->transition);
    memset(res,0,sizeof(BattleRes));
    BtlJob_InitPool();
    return 1;
}

'''
        return one(text,anchor,cleanup+anchor)
    if source=='src/battle/btl_obj.c':
        start=text.index('void BtlRes_LoadFile(BtlResFile *file, s32 fromHeap) {');end=text.index('/* After a reload has been read',start)
        return text[:start]+'''void BtlRes_LoadFile(BtlResFile *file, s32 fromHeap) {
    s32 id=file->id;
    s32 size=file->size;
    void *requested;
    if (fromHeap==1) {
        BtlRes_FreeFile(file);
        /* FreeFile clears id/size: retain the selected request identity. */
        file->id=id>=0?id:-1; file->size=id>=0?size:0;
        if (id>=0) {
            requested=File_Request3(id,NULL,0);
            if (requested) file->buf=requested;
        }
    } else if (id>=0) {
        requested=File_Request3(id,file->buf,file->size);
        if (requested) file->buf=requested;
    }
}

'''+text[end:]
    return text
