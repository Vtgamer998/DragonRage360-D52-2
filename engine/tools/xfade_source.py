# SPDX-License-Identifier: AGPL-3.0-or-later
"""Checked adaptations of the pinned stg_b.c cross-fade consumer."""
def transform(text):
    def replace(old, new):
        nonlocal text
        if text.count(old) != 1:
            raise ValueError('cross-fade source anchor changed: ' + old[:70])
        text = text.replace(old, new)

    replace('''    if (gScrXfade->buf[0] == NULL) {
        gScrXfade->buf[0] = Heap_Alloc(0x60090, 0x20, 0, 2);
        gScrXfade->buf[1] = Heap_Alloc(0x60090, 0x20, 0, 2);
    }''', '''    void *a, *b;
    if (!gScrXfade) { XbRes_SetReadError(-423); return; }
    if (gScrXfade->buf[0] && gScrXfade->buf[1]) return;
    if (gScrXfade->buf[0] || gScrXfade->buf[1]) {
        XbRes_SetReadError(-423); return;
    }
    a = Heap_Alloc(0x60090, 0x20, 0, 2);
    if (!a) { XbRes_SetReadError(-423); return; }
    b = Heap_Alloc(0x60090, 0x20, 0, 2);
    if (!b) { Heap_Free(a); XbRes_SetReadError(-423); return; }
    gScrXfade->buf[0] = a;
    gScrXfade->buf[1] = b;''')
    replace('''    if (gScrXfade->buf[0] != NULL) {
        Heap_Free(gScrXfade->buf[0]);
        Heap_Free(gScrXfade->buf[1]);
        gScrXfade->buf[0] = NULL;
        gScrXfade->buf[1] = NULL;
    }''', '''    if (!gScrXfade) return;
    if (gScrXfade->buf[0]) Heap_Free(gScrXfade->buf[0]);
    if (gScrXfade->buf[1]) Heap_Free(gScrXfade->buf[1]);
    gScrXfade->buf[0] = NULL;
    gScrXfade->buf[1] = NULL;''')
    replace('''    sceGsSetDefStoreImage(si, sbp, w >> 6, 1, 0, 0, w, h);
    FlushCache(0);
    sceGsExecStoreImage(si, (u8 *)p + 0x70);''', '''    if (!p || w != 512 || h != 224) {
        XbRes_SetReadError(-420); return;
    }
    if (sceGsSetDefStoreImage(si, sbp, w >> 6, 1, 0, 0, w, h)) {
        XbRes_SetReadError(-420); return;
    }
    FlushCache(0);
    if (sceGsExecStoreImage(si, (u8 *)p + 0x70)) {
        XbRes_SetReadError(-420); return;
    }''')
    replace('''    u8 si[0x70];
    ScrXfade *xf = gScrXfade;''', '''    u8 si[0x70];
    if (!gScrXfade || index < 0 || index >= 2) {
        XbRes_SetReadError(-420); return;
    }
    ScrXfade *xf = gScrXfade;''')
    replace('''    w = 0x200;
    h = 0xE0;''', '''    if (!gScrXfade || !gScrXfade->buf[0] || !gScrXfade->buf[1]) {
        dr_dma_reject_packet(); return;
    }
    w = 0x200;
    h = 0xE0;''')
    replace('Dma_AddData(&gScrXfade->chain[i], 0x10);', '''if (dr_dma_add_xfade_ref(&gScrXfade->chain[i], gScrXfade->buf[i], 0x60090)) return;''')
    replace('''        ScrXfade_AllocBuffers();
        ScrXfade_Capture();''', '''        ScrXfade_AllocBuffers();
        if (XbRes_LastError()) return;
        ScrXfade_Capture();''')
    return '''extern void XbRes_SetReadError(int);
extern int XbRes_LastError(void);
extern int dr_dma_reject_packet(void);
extern int dr_dma_add_xfade_ref(const void *, const void *, unsigned long);
''' + text
