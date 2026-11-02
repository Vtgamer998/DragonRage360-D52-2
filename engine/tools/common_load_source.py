# SPDX-License-Identifier: AGPL-3.0-or-later
"""Strict common boot ownership and failure propagation; formats stay explicit.

Reload writes existing fixed buffers. On failure those bytes are invalid and
the sticky read error must block consumers; this is not a snapshot rollback.
"""
def once(text, anchor, replacement):
    if text.count(anchor) != 1:
        raise ValueError('common boot anchor changed: '+anchor[:80])
    return text.replace(anchor, replacement)

def transform(source, text):
    if source == 'src/main.c':
        for call in ('Common_Init();', 'Common_LoadBoot();', 'Common_Reload();'):
            text = once(text, '    '+call, '    '+call+'''
    { extern int XbRes_LastError(void); if (XbRes_LastError()) return; }''')
        return text
    if source != 'src/sys/common.c':
        return text
    declarations = '''
#include "xb_resconv.h"
extern void *XbFile_RequestOwned(s32,void *,s32);
extern void XbFile_AbortRequestsRetainBuffers(void);
/* Error ownership stays with the strict scope's caller. */
'''
    text = declarations+text
    text = once(text, 'void Common_Init(void) {', '''void Common_Init(void) {
    if (XbRes_CheckedReadsActive()) {
        if (XbRes_LastError()) return;
        if (gCommonRes) { XbRes_SetReadError(-909); return; }
        gCommonRes=Heap_Alloc(sizeof(CommonRes),0x20,0,HEAP_ANY);
        if (!gCommonRes) { XbRes_SetReadError(-908); return; }
        memset(gCommonRes,0,sizeof(CommonRes));
        return;
    }''')
    text = once(text, 'void Common_LoadBoot(void) {', '''void Common_LoadBoot(void) {
    if (XbRes_CheckedReadsActive()) {
        CommonRes staged;
        s32 i;
        if (XbRes_LastError()) return;
        if (!gCommonRes || gCommonRes->boot || gCommonRes->data[0] ||
            gCommonRes->data[1] || gCommonRes->data[2]) {
            XbRes_SetReadError(-909); return;
        }
        /* Refuse missing formats before any allocation or IO. */
        for (i=2;i<=5;i++) if (XbRes_PreflightRead(i)) return;
        memset(&staged,0,sizeof(staged));
        staged.boot=File_LoadSync(COMMON_FILE_BOOT,NULL,0);
        if (!staged.boot || XbRes_LastError()) goto boot_fail;
        for (i=0;i<COMMON_FILE_COUNT;i++) {
            staged.size[i]=File_GetSize(i+2);
            if (XbRes_LastError() || staged.size[i]<=0) goto boot_fail;
            staged.data[i]=Heap_Alloc(staged.size[i],0x40,0,HEAP_ANY);
            if (!staged.data[i]) goto boot_fail;
        }
        gCommonRes->boot=staged.boot;
        gCommonRes->unk10=0;
        for (i=0;i<COMMON_FILE_COUNT;i++) {
            gCommonRes->data[i]=staged.data[i];
            gCommonRes->size[i]=staged.size[i];
        }
        return;
boot_fail:
        if (!XbRes_LastError()) XbRes_SetReadError(-908);
        for (i=0;i<COMMON_FILE_COUNT;i++) if (staged.data[i]) Heap_Free(staged.data[i]);
        if (staged.boot) Heap_Free(staged.boot);
        return;
    }''')
    text = once(text, 'void Common_Reload(void) {', '''void Common_Reload(void) {
    if (XbRes_CheckedReadsActive()) {
        s32 i;
        void *requested, *bank=NULL;
        if (XbRes_LastError()) return;
        /* Never cancel another subsystem's queue or replace an owner with NULL. */
        if (!gCommonRes || !gCommonRes->boot || File_IsBusy() || File_GetPendingCount()) {
            XbRes_SetReadError(-909); return;
        }
        for (i=0;i<COMMON_FILE_COUNT;i++) {
            if (!gCommonRes->data[i] || gCommonRes->size[i]<=0) {
                XbRes_SetReadError(-909); return;
            }
            if (XbRes_PreflightRead(i+2)) return;
        }
        if (XbRes_PreflightRead(COMMON_FILE_SND_BANK)) return;
        for (i=0;i<COMMON_FILE_COUNT;i++) {
            requested=XbFile_RequestOwned(i+2,gCommonRes->data[i],gCommonRes->size[i]);
            if (!requested || XbRes_LastError()) goto reload_fail;
        }
        bank=XbFile_RequestOwned(COMMON_FILE_SND_BANK,NULL,0);
        if (!bank || XbRes_LastError()) goto reload_fail;
        while (!File_UpdateRequests()) if (XbRes_LastError()) goto reload_fail;
        if (XbRes_LastError()) goto reload_fail;
        /* sync=1 drains the transfer before the temporary source is freed. */
        Snd_LoadBank(1,bank,1);
        if (XbRes_LastError()) goto reload_fail;
        Heap_Free(bank);
        return;
reload_fail:
        if (!XbRes_LastError()) XbRes_SetReadError(-907);
        XbFile_AbortRequestsRetainBuffers();
        if (bank) Heap_Free(bank);
        return;
    }''')
    return text
