# SPDX-License-Identifier: AGPL-3.0-or-later
"""Pinned fixed-profile adaptations.
Explicit PC UI/session notifications are omitted. Owned fighter objects use
bounded native node/decoded-motion bridges; other objects keep original paths.
Body local points get a defined homogeneous W; attack point arguments satisfy
16-byte vector contracts. Every source anchor must match exactly.
"""
import re

def transform(source, text):
    if source == 'src/battle/stg_b.c':
        from xfade_source import transform as xfade_transform
        text = xfade_transform(text)
    # These original records define numeric flag bits, not host bitfield ABI.
    flag_sources={
        'src/battle/bobj_a.c':('FLAG_BITS', 'state.flags', {'visible':2,'part1':0x10,'part2':0x20,'part3':0x40,'part4':0x80,'hideA':0x100,'node76':0x40000000}),
        'src/battle/btl_obj.c':('FLAG_BITS','flags',{'black':0x80000,'pass1':0x100000,'pass2':0x200000}),
        'src/sys/gfxm_d_b.c':('FLAG_BITS','flags',{'visible':2,'bits4':None}),
        'src/sys/gfxm_d_c.c':('OBJ_FLAG_BITS','flags',{'visible':2,'bit2':4,'colorStage':0x10000,'colorA':0x20000})}
    if source in flag_sources:
        macro,path,masks=flag_sources[source]
        def flag_read(m):
            obj,field=m.group(1),m.group(2)
            if field not in masks:raise ValueError('unreviewed object flag '+field)
            flags='('+obj+')->'+path
            return '(('+flags+' >> 4) & 0xFFFu)' if masks[field] is None else '(('+flags+' & '+hex(masks[field])+'u)!=0)'
        pattern=re.escape(macro)+r'\(([^()]+)\)->([A-Za-z0-9_]+)'
        text,count=re.subn(pattern,flag_read,text)
        expected={'src/battle/bobj_a.c':62,'src/battle/btl_obj.c':5,'src/sys/gfxm_d_b.c':2,'src/sys/gfxm_d_c.c':4}[source]
        if not count or (expected is not None and count!=expected):raise ValueError('object flag access sites changed')
    if source in ('src/sys/common.c','src/main.c'):
        from common_load_source import transform as common_transform
        text=common_transform(source,text)
    if source in ('src/battle/battle_load.c','src/battle/btl_obj.c'):
        from loading_error_source import transform as load_transform
        text=load_transform(source,text)
    if source in ('src/sys/file.c','src/sys/loading.c','src/battle/battle_work.c'):
        from loading_error_source import transform as load_transform
        return load_transform(source,text), []

    if source=='src/battle/eft_ad_b.c':
        old='    memcpy(dst, &pkt.gifTag, (u16)pkt.dmaTag << 4);'
        if text.count(old)!=1:raise ValueError('immediate effect triangle copy source changed')
        text='extern int dr_ot_payload(void *,const void *,unsigned long,int);\nextern int dr_dma_w64_range(void *,unsigned long);\nextern int dr_dma_reject_packet(void);\nextern void XbFreshBoot_CountDirectEffect(void);\n'+text
        return text.replace(old,"""    if (dr_dma_w64_range(dst, (u16)pkt.dmaTag << 4) ||
        dr_ot_payload(dst, &pkt.gifTag, (u16)pkt.dmaTag << 4, __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)) {
        dr_dma_reject_packet(); return;
    }
    XbFreshBoot_CountDirectEffect();"""),[]

    if source=='src/battle/col_c.c':
        old='    *(u16 *)*tag = ((*pkt - *tag) - 2) / 6;'
        if text.count(old)!=1:raise ValueError('font GIF loop patch source changed')
        text='extern int dr_dma_w64_range(void *,unsigned long);\nextern int dr_dma_reject_packet(void);\n'+text
        return text.replace(old,"""    unsigned long begin,end,bytes,count;
    if (!pkt || !tag || !*pkt || !*tag) { dr_dma_reject_packet(); return; }
    begin=(unsigned long)*tag;end=(unsigned long)*pkt;
    if (end<begin || end-begin<16 || (end-begin-16)%48) { dr_dma_reject_packet(); return; }
    bytes=end-begin;count=(bytes-16)/48;
    if (count>0x7FFF || dr_dma_w64_range(*tag,bytes)) { dr_dma_reject_packet(); return; }
    /* Replace numeric low 16 bits, as the original LE halfword store does.
     * A native BE halfword store would destroy NREG/FLG in bits 48..63. */
    **tag=(**tag & ~0xFFFFULL) | (u64)count;"""),[]

    if source=='src/battle/hud_a.c':
        # The setters and readers share the original low five numeric bits.
        for field,mask in [('showGauges',1),('showTimer',2),('showNotice',4),('showCombo',8),('showPrompt',16)]:
            old='gHud->'+field+' = on;'
            if text.count(old)!=2:raise ValueError('HUD setter sites changed: '+field)
            text=text.replace(old,'gHud->flags = (gHud->flags & ~'+str(mask)+'u) | (((u32)on & 1u) * '+str(mask)+'u);')
        return text,[]
    if source=='src/battle/hud_c_c.c':
        for old,new in [('spr->hidden = show == 0;','spr->flags = (spr->flags & ~1u) | (show == 0);'),('spr->mirror = mirror;','spr->flags = (spr->flags & ~2u) | (((u32)mirror & 1u) << 1);')]:
            if text.count(old)!=1:raise ValueError('HUD sprite setter changed')
            text=text.replace(old,new)
        return text,[]
    if source=='src/battle/hud_e_b.c':
        old='node->mirror = HUD_SCR(side) != 0;'
        if text.count(old)!=1:raise ValueError('HUD node mirror setter changed')
        text=text.replace(old,'node->flags = (node->flags & ~2u) | ((HUD_SCR(side) != 0) ? 2u : 0u);')
        return text,[]

    if source=='src/battle/btl_tech_b.c':
        # These outputs must be produced in a separate full expression before
        # their use as another argument; C does not order function arguments.
        edits=[
            ('BtlKiBlast_ScaleKiByAbility(chr, BtlKiBlast_GetCurrent(chr, &kind)->ki, kind)', 'BtlKiBlast_GetCurrent(chr, &kind)', 'BtlKiBlast_ScaleKiByAbility(chr, rec->ki, kind)'),
            ('BtlKiBlast_ScaleKiByAbility(chr, BtlKiBlast_GetOfHit(hit, &chr, &kind)->ki, kind)', 'BtlKiBlast_GetOfHit(hit, &chr, &kind)', 'BtlKiBlast_ScaleKiByAbility(chr, rec->ki, kind)'),
            ('BtlKiBlast_ScaleSpeedByAbility(chr, kind, BtlKiBlast_GetCurrent(chr, &kind)->speed * 10.0f * BTL_KMH_PER_FRAME)', 'BtlKiBlast_GetCurrent(chr, &kind)', 'BtlKiBlast_ScaleSpeedByAbility(chr, kind, rec->speed * 10.0f * BTL_KMH_PER_FRAME)'),
            ('BtlKiBlast_ScaleSpeedByAbility(chr, kind, BtlKiBlast_GetOfHit(hit, &chr, &kind)->speed * 10.0f * BTL_KMH_PER_FRAME)', 'BtlKiBlast_GetOfHit(hit, &chr, &kind)', 'BtlKiBlast_ScaleSpeedByAbility(chr, kind, rec->speed * 10.0f * BTL_KMH_PER_FRAME)'),
            ('BtlKiBlast_MapReact(chr, BtlKiBlast_GetCurrent(chr, &kind)->react, kind)', 'BtlKiBlast_GetCurrent(chr, &kind)', 'BtlKiBlast_MapReact(chr, rec->react, kind)'),
            ('BtlKiBlast_MapReact(chr, BtlKiBlast_GetOfHit(hit, &chr, &kind)->react, kind)', 'BtlKiBlast_GetOfHit(hit, &chr, &kind)', 'BtlKiBlast_MapReact(chr, rec->react, kind)'),
        ]
        for old,getter,new in edits:
            line='    return '+old+';'
            if text.count(line)!=1:raise ValueError('ki getter sequencing source changed')
            text=text.replace(line,'    BtlKiBlastData *rec = '+getter+';\n    return '+new+';')
        return text,[]
    if source=='src/battle/eft_ae.c':
        # Numeric low bits are the original ABI; C byte bitfields reverse on BE.
        if text.count('BTL_TASK_BITS(task)->updated = 1;')!=1 or text.count('BTL_TASK_BITS(task)->dead = 1;')!=1:
            raise ValueError('task byte flag writers changed')
        text=text.replace('BTL_TASK_BITS(task)->updated = 1;','task->bits |= 2u;')
        text=text.replace('BTL_TASK_BITS(task)->dead = 1;','task->bits |= 1u;')
        text=text.replace('BTL_TASK_BITS(task)->updated','((task->bits & 2u)!=0)')
        text=text.replace('BTL_TASK_BITS(task)->dead','((task->bits & 1u)!=0)')
        if 'BTL_TASK_BITS(' in text:raise ValueError('unreviewed task byte flag access')
        return text, []

    if source=='src/sys/gfx_ot.c':
        start=text.index('s32 Ot_Send(void) {');end=text.index('/* Queues the GS state',start)
        text=text[:start]+r'''static int XbOt_Range(const void *p, unsigned size, const void *base, unsigned bytes) {
    unsigned long a=(unsigned long)p,b=(unsigned long)base;
    return base && a>=b && a-b<=bytes && size<=bytes-(a-b);
}
static int XbOt_PrimRange(const void *p, unsigned size) {
    if(((unsigned long)p & 15u)!=0)return 0;
    return XbOt_Range(p,size,gOt->buf,gOt->bufSize) ||
        XbOt_Range(p,size,gOt->headBuf[0],OT_SLOT_COUNT*OT_HEAD_SIZE) ||
        XbOt_Range(p,size,gOt->headBuf[1],OT_SLOT_COUNT*OT_HEAD_SIZE);
}
s32 Ot_Send(void) {
    extern int dr_dma_producer_error(void),dr_dma_reject_packet(void),dr_dma_w64_range(void *,unsigned long);
    extern s32 Dma_GetFreeSize(void);
    extern void xsdk_log_write(const char *,...);
    extern int dr_ot_payload(void *,const void *,unsigned long,int);
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    const int ot_big=1;
#else
    const int ot_big=0;
#endif
    OtPrim *prim;u8 *dst,*chunk;unsigned total=0,steps=0,tag;
    s32 available=Dma_GetFreeSize();
    if(gOtTail==NULL)return 0;
    /* Preflight the complete borrowed list before any command or memcpy.
     * Header words were written as native u32: qwc is numeric bits 0..15,
     * not the first host u16 on a big-endian processor. */
    for(prim=gOtHead;prim;prim=prim->next){
        if(++steps>0x10000u || !XbOt_PrimRange(prim,sizeof(*prim))){xsdk_log_write("OT header rejected step=%u ptr=%p",steps,prim);dr_dma_reject_packet();return 0;}
        memcpy(&tag,prim,4);unsigned bytes=(tag&0xFFFFu)*16u;
        if(!XbOt_PrimRange(prim,sizeof(*prim)+bytes) || bytes>0x10000u || available<0x11000 || bytes>(unsigned)available-0x11000u-total){xsdk_log_write("OT extent rejected step=%u tag=%08x bytes=%u total=%u",steps,tag,bytes,total);dr_dma_reject_packet();return 0;}
        if(dr_ot_payload(NULL,prim+1,bytes,ot_big)){xsdk_log_write("OT GIF rejected step=%u bytes=%u tag=%016llx regs=%016llx",steps,bytes,((u64 *)(prim+1))[0],((u64 *)(prim+1))[1]);dr_dma_reject_packet();return 0;}
        total+=bytes;
    }
    Ot_AddEnv(0);if(dr_dma_producer_error())return 0;
    chunk=dst=(u8 *)Dma_BeginDirect();
    if(dr_dma_producer_error())return 0;
    for(prim=gOtHead;prim;prim=prim->next){
        memcpy(&tag,prim,4);unsigned bytes=(tag&0xFFFFu)*16u;
        /* Keep complete GIF packets in painter order; DIRECT has a separate
         * bounded 64KiB window, not a 64KiB limit on the complete OT. */
        if(bytes>0x10000u-(unsigned)(dst-chunk)){
            Dma_EndDirect((u64 *)dst);if(dr_dma_producer_error())return 0;
            chunk=dst=(u8 *)Dma_BeginDirect();if(dr_dma_producer_error())return 0;
        }
        if(bytes && dr_dma_w64_range(dst,bytes))return 0;
        if(dr_ot_payload(dst,prim+1,bytes,ot_big)){dr_dma_reject_packet();return 0;}dst+=bytes;
    }
    Dma_EndDirect((u64 *)dst);if(dr_dma_producer_error())return 0;
    Ot_AddRestoreEnv();return !dr_dma_producer_error();
}

'''+text[end:]
        return text, []

    if source=='src/sys/gfxm_a.c':
        # DMATag.qwc is bits 0..15, independent of the host C bitfield order.
        # Reading the BE bitfield turned 0x20000005 into 8192 QW instead of 5.
        anchor='pkt.tag.h.qwc << 4'
        if text.count(anchor)!=2:raise ValueError('pan blur packet count source changed')
        text=text.replace(anchor,'(pkt.tag.w & 0xFFFFu) << 4')
        text='extern int dr_dma_add_clut_ref(const void *);\n'+text
        for expr,count in [('gStgPanBlur->tex.upload',2),('gObjOutline->tex.upload',1),('gStgGlare.tex.upload',1),('tint->tex.upload',1),('gObjGlow->cur->upload',1)]:
            old='Dma_AddData('+expr+', 0x10);'
            if text.count(old)!=count:raise ValueError('CLUT upload caller changed: '+expr)
            text=text.replace(old,'dr_dma_add_clut_ref('+expr+');')
        return text, []

    clut_calls={'src/sys/gfxm_b.c':['gGfxAlphaKey->ref','gGfxDepthFog.ref'],'src/battle/stg_c.c':['haze->upload'],'src/battle/stg_b.c':['tone->load']}
    if source in clut_calls:
        text='extern int dr_dma_add_clut_ref(const void *);\n'+text
        for expr in clut_calls[source]:
            old='Dma_AddData('+expr+', 0x10);'
            if text.count(old)!=1:raise ValueError('CLUT upload caller changed: '+expr)
            text=text.replace(old,'dr_dma_add_clut_ref('+expr+');')
        return text, []

    if source=='src/sys/gfxm_b_b.c':
        call='Dma_AddData(&gTexBltPacket, sizeof(TexBltPacket));'
        if text.count(call)!=2:raise ValueError('native texture upload header source changed')
        text='extern int dr_dma_add_texture_header(const void *);\n'+text
        return text.replace(call,'dr_dma_add_texture_header(&gTexBltPacket);'), []

    if source=='src/sys/vu1_packet.c':
        text='extern u32 XbVif_ChainAddress(u32);\n'+text
        for old,new,count in [('chain & VU1_ADDR_MASK','XbVif_ChainAddress(chain)',5),('(obj + 0x60) & VU1_ADDR_MASK','XbVif_ChainAddress(obj + 0x60)',2),('(u32)node->vif & VU1_ADDR_MASK','XbVif_ChainAddress((u32)node->vif)',1)]:
            if text.count(old)!=count:raise ValueError('native VIF CALL source changed: '+old)
            text=text.replace(old,new)
        anchor='void Vu1Node_OrFlags(Vu1Node *node, u32 bits0, u32 bits1) {\n'
        if text.count(anchor)!=1:raise ValueError('stage node flag reader changed')
        text=text.replace(anchor,anchor+'    extern int XbStage_TryAnimationNodeFlags(void *,u32,u32);\n    if(XbStage_TryAnimationNodeFlags(node,bits0,bits1))return;\n')
        # The original copies pos.w from the adjacent frame word. Its partial
        # pos[3] declaration makes that read undefined in C; preserve its bits.
        helper='static f32 XbAnimationKeyW(const Vu1Key *key){ f32 value; memcpy(&value,&key->frame,4); return value; }\n'
        signature='void Vu1Node_Animate(Vu1Node *node, Vu1Track *track, void *mtx, f32 frame) {'
        if text.count(signature)!=1 or text.count('(next - 1)->pos[3]')!=1 or text.count('next->pos[3]')!=1:raise ValueError('animation frame-word alias changed')
        text=text.replace(signature,helper+signature).replace('(next - 1)->pos[3]','XbAnimationKeyW(next - 1)').replace('next->pos[3]','XbAnimationKeyW(next)')
        return text, []
    if source=='src/battle/stgm_a.c':
        anchor='StgModel_SetTexture(*(u8 *)&mats[i].cur);'
        if text.count(anchor)!=1:raise ValueError('stage texture byte alias changed')
        return text.replace(anchor,'StgModel_SetTexture(mats[i].cur & 255u);'), []
    if source=='src/battle/eft_m.c':
        anchor='    f->unk8 = gEftAuraCfg->flame[part].unk0;'
        if text.count(anchor)!=1:raise ValueError('aura node byte alias changed')
        return text.replace(anchor,'    { u32 native_node; memcpy(&native_node,&gEftAuraCfg->flame[part],4); f->unk8=native_node&255u; }'), []
    if source=='src/sys/rigid.c':
        anchor='    Vec4 zero = { 0.0f, 0.0f, 0.0f, 1.0f };'
        if text.count(anchor)!=1:raise ValueError('rigid sphere origin changed')
        return text.replace(anchor,'    Vec4 zero __attribute__((aligned(16))) = { 0.0f, 0.0f, 0.0f, 1.0f };'), []
    if source=='src/sys/gfxm_e_d.c':
        anchor='        Res_RelocateOffsets(&gBtlStage->tex, (u8 *)base, (TexFile *)(gBtlStage->base + gBtlStage->texOfs));'
        if text.count(anchor)!=1:raise ValueError('stage texture binder changed')
        text=text.replace(anchor,'        extern int XbStage_TryBindGeometryTexture(void *,void **);\n        if(!XbStage_TryBindGeometryTexture(base,(void **)&gBtlStage->tex)) {\n'+anchor+'\n        }\n        XbStagePack_RelocateTextureFrames(base);')
        for old,new in [('d->ptr04 = base + o04;', 'd->ptr04 = o04 ? base + o04 : XbStagePack_NullMesh(base);'),('STG_RELOC(void *, d1->ptr8);','if (d1->ptr8) { STG_RELOC(void *, d1->ptr8); } else d1->ptr8=XbStagePack_NullMesh(base);')]:
            if text.count(old)!=1:raise ValueError('stage null mesh relocation changed')
            text=text.replace(old,new)
        return 'extern void *XbStagePack_NullMesh(void *);\nextern void XbStagePack_RelocateTextureFrames(void *);\n'+text, []
    if source=='src/battle/btl_scene.c':
        anchor='s32 *BtlScene_GetCommonEntry(s32 idx) {\n'
        if text.count(anchor)!=1:raise ValueError('common3 entry binding changed')
        return text.replace(anchor,anchor+'    extern int XbBattleRuntime_TryCommon3Entry(int,void **);\n    void *native_entry=NULL;\n    if(XbBattleRuntime_TryCommon3Entry(idx,&native_entry)) return native_entry;\n'), []
    if source=='src/sys/snd.c':
        for signature,body in [
            ('s32 SndHd_GetVagMax(SndBankFile *file) {\n', '    extern int XbSfx_TryVagMax(const void *,int *), XbSoundBank_TryVagMax(const void *,int *);\n    int maximum; if(XbSfx_TryVagMax(file,&maximum) || XbSoundBank_TryVagMax(file,&maximum)) return maximum;\n'),
            ('void SndHd_GetLoopFlags(SndBankFile *file, u8 *flags) {\n', '    extern int XbSfx_TryLoopFlags(const void *,void *), XbSoundBank_TryLoopFlags(const void *,void *);\n    if(XbSfx_TryLoopFlags(file,flags) || XbSoundBank_TryLoopFlags(file,flags)) return;\n')]:
            if text.count(signature)!=1:raise ValueError('sound metadata reader changed')
            text=text.replace(signature,signature+body)
        for signature in ('void Snd_LoadBank(u32 mask, SndBankFile *file, s32 sync) {\n','void Snd_ReloadBank(u32 mask, SndBankFile *file, s32 sync) {\n'):
            if text.count(signature)!=1:raise ValueError('original sound bank load reader changed')
            text=text.replace(signature,signature+'    extern int XbSoundBank_RequireReadBank(const void *,unsigned);\n    if(XbSoundBank_RequireReadBank(file,mask)) return;\n')
        start=text.index('void Snd_Term(void) {');end=text.index('/* Stops everything:',start)
        block=text[start:end]
        if block.count('    Snd_RpcTerm();')!=1 or block.count('bank->spuAddr == 0')!=1:raise ValueError('sound termination changed')
        block=block.replace('    Snd_RpcTerm();\n','').replace('bank->spuAddr == 0','bank->spuAddr != 0')
        block=block.replace('    IopHeap_Free(gSndMgr->iopBuf);','    Snd_RpcTerm();\n    Heap_Free(gSndRpc);\n    gSndRpc = NULL;\n    IopHeap_Free(gSndMgr->iopBuf);')
        return text[:start]+block+text[end:], []
    if source=='src/battle/eft_det_b_b.c':
        anchor='    file = STGNAV_RES->stageFile;'
        if text.count(anchor)!=1:raise ValueError('navigation binding changed')
        code='    extern int XbStage_TryBindNav(void **);\n    void *native_nav=NULL;\n    if (XbStage_TryBindNav(&native_nav)) {\n        if (native_nav == NULL) return -2;\n        gStgNav->data=native_nav;\n        gStgNav->data->nodes=(StgNavNode *)((u8 *)native_nav+16);\n        gStgNav->ready=1;\n        return 0;\n    }\n'
        return text.replace(anchor,code+anchor), []
    if source=='src/battle/btl_input.c':
        anchor='        u32 bit = 1 << i;'
        if text.count(anchor)!=2:raise ValueError('input bit-counter source changed')
        return text.replace(anchor,'        u32 bit = 1u << i;'), []
    if source=='src/sys/save.c':
        anchor='#include "sys/save.h"'
        if text.count(anchor)!=1:raise ValueError('save include source changed')
        text=text.replace(anchor,anchor+'\n/* Character masks include bit 63: shift an unsigned 64-bit value. */\n#undef SAVE_CHARA_MASK\n#define SAVE_CHARA_MASK(id) (1ULL << ((id) % 64))')
        return text, []
    if source=='src/battle/btl_obj.c':
        anchor='    light->res = (u8 *)res->data[0] + ((((u32 *)res->data[0])[1] >> 2) << 2);\n    Res_RelocateOffsets(&light->res, light->res, light->res);'
        if text.count(anchor)!=1:raise ValueError('original light resource binding changed')
        text=text.replace(anchor,'    extern int XbBattleRuntime_BindLight(void **);\n    if (!XbBattleRuntime_BindLight(&light->res)) {\n'+anchor+'\n    }')
        return text, []
    if source=='src/battle/btl_char_mgr.c':
        anchor='            chr->objTbl[i] = FILE_TABLE(obj->file[i], 3);'
        if text.count(anchor)!=1:raise ValueError('original member table binding changed')
        text=text.replace(anchor,'            extern int XbFighter_MemberTable(void *,int,void **);\n            void *native_table=NULL;\n            if (XbFighter_MemberTable(obj,i,&native_table)) chr->objTbl[i]=native_table;\n            else '+anchor.strip())
        return text, []
    if source=='src/battle/stg_a_b.c':
        if text.count('    StgGroundHit hit;')!=2:raise ValueError('stage ground locals changed')
        text=text.replace('    StgGroundHit hit;','    StgGroundHit hit __attribute__((aligned(16)));')
        bind='void BtlStage_BindFile(BtlStage *stage) {\n'
        if text.count(bind)!=1:raise ValueError('stage data binder changed')
        text=text.replace(bind,bind+'    extern int XbStage_TryBindGeometryData(void *);\n    if(XbStage_TryBindGeometryData(stage))return;\n')
        start='s32 BtlStage_GetStartPlace(s32 side, Vec4 *pos, Vec4 *rot, s32 alt) {\n'
        place='s32 BtlStage_GetPlace(Vec4 *pos, Vec4 *rot) {\n'
        for signature,args in ((start,'side,pos,rot,alt'),(place,'0,pos,rot,-1')):
            if text.count(signature)!=1:raise ValueError('stage placement source changed')
            guard=('    extern int XbStage_TryStartPlace(int,void *,void *,int,int *);\n'
                   '    int native_zone=-1;\n'
                   '    if (!pos || !rot) return -1;\n'
                   '    if (XbStage_TryStartPlace('+args+',&native_zone)) return native_zone;\n'
                   '    if (!gBtlStage || !BtlStage_IsReady() || !gBtlStage->data) {\n'
                   '        memset(pos,0,sizeof(*pos)); memset(rot,0,sizeof(*rot)); return -1;\n'
                   '    }\n')
            text=text.replace(signature,signature+guard)
        return text, []
    if source=='src/battle/bobj_a.c':
        node='BObjNode *BtlObj_GetNode(BObj *obj, s32 node) {\n'
        load='BobjAnim *BtlObjAnim_Load(BobjObj *obj, void *buf, s32 anim, s32 *size) {\n'
        if text.count(node)!=1 or text.count(load)!=1:raise ValueError('native fighter hook source changed')
        text=text.replace(node,node+'    extern int XbFighter_ResolveNode(void *, int, void **);\n    void *native_node;\n    if (XbFighter_ResolveNode(obj,node,&native_node)) return (BObjNode *)native_node;\n')
        text='#include "xb_resconv.h"\n'+text
        text=text.replace(load,load+'    extern int XbFighter_LoadDecoded(void *,void *,int,int *,void **);\n    void *native_motion;\n    if (!size) return NULL;\n    *size=0;\n    if (!obj || anim<0 || anim>=BOBJ_ANIM_MAX+8) return NULL;\n    if (XbFighter_LoadDecoded(obj,buf,anim,size,&native_motion)) return (BobjAnim *)native_motion;\n')
        call='        Bpe_Decode(obj->mdl.anims[anim], buf, size);'
        if text.count(call)!=1:raise ValueError('original embedded motion decode source changed')
        text=text.replace(call,'''        if (!buf) return NULL;
        size_t packed=XbRes_BpeSourceSize(obj->mdl.anims[anim]);
        if (packed<8) return NULL;
        /* A validated zero-length motion is an absent slot, not an error. */
        if (!XbRes_Le32(obj->mdl.anims[anim])) return NULL;
        if (!XbBpe_DecodeBounded(obj->mdl.anims[anim],buf,0xc000,size)) return NULL;''')
        retarget='    bone = obj->mdl.nodes;\n    if (useOwn != 0'
        if text.count(retarget)!=1:raise ValueError('reference retarget source changed')
        text=text.replace(retarget,'    extern const float *XbFighter_CommonReference(void *,const float *);\n    const float *native_reference=XbFighter_CommonReference(obj,D_002F2420);\n    if (useTable && !native_reference) return;\n'+retarget)
        for suffix in ('id * 3','id * 3 + 1','id * 3 + 2'):
            anchor='keys[i].'+{'id * 3':'x','id * 3 + 1':'y','id * 3 + 2':'z'}[suffix]+' -= D_002F2420['+suffix+'];'
            if text.count(anchor)!=1:raise ValueError('reference table reader changed')
            text=text.replace(anchor,anchor.replace('D_002F2420[','native_reference['))
        bind='void BtlObj_BindTables(BObj *obj) {\n'
        if text.count(bind)!=1:raise ValueError('owned table binder source changed')
        text=text.replace(bind,bind+'    extern int XbFighter_BindOwnedTables(void *);\n    if (XbFighter_BindOwnedTables(obj)) return;\n')
        top='top = ((BObjModelBytes *)mdl->model)->flagsTop;'
        if text.count(top)!=2:raise ValueError('model flags byte readers changed')
        text=text.replace(top,'top = (s32)(mdl->model->flags >> 24);')
        point='        Vec3_Sub(&local, &part->offset, &part->bone->origin);'
        if text.count(point)!=1:raise ValueError('body local-point source changed')
        text=text.replace(point,'        local.w = 1.0f; /* homogeneous local point, not uninitialized stack data */\n'+point)
        marker='void BtlObjHit_BuildVolumes(BobjObj *obj) {'
        start=text.index(marker);end=text.index('/* Local rotation matrix',start)
        block=text[start:end]
        if block.count('    Vec4 center;')!=1:raise ValueError('attack center source changed')
        block=block.replace('    Vec4 center;','    Vec4 center __attribute__((aligned(16)));')
        block=block.replace('    Vec4 offset;','    Vec4 offset;\n    Vec4 boundsPoint __attribute__((aligned(16)));')
        for side in ('min','max'):
            call='                ColBox_AddPointKeepW(&work->atkBox, box.'+side+');'
            if block.count(call)!=2:raise ValueError('attack bound point source changed')
            setup='                boundsPoint.x=box.'+side+'[0]; boundsPoint.y=box.'+side+'[1]; boundsPoint.z=box.'+side+'[2]; boundsPoint.w=1.0f;\n'
            block=block.replace(call,setup+'                ColBox_AddPointKeepW(&work->atkBox, &boundsPoint);')
        text=text[:start]+block+text[end:]
        return text, []
    if source=='src/sys/gfxm_e_c.c':
        anchor='void MdlTex_RebaseChain(s32 single, void *chain, s32 tbp, s32 cbp, s32 tbp2, s32 cbp2, u32 minTbp, u32 minCbp) {\n'
        if text.count(anchor)!=1:raise ValueError('model VIF rebase entry changed')
        return text.replace(anchor,anchor+'    extern int XbModelPack_ChainPrepared(int,const void *,int,int,int,int,unsigned,unsigned);\n    if (XbModelPack_ChainPrepared(single,chain,tbp,cbp,tbp2,cbp2,minTbp,minCbp)) return;\n'), []
    if source=='src/sys/gfxm_e_b.c':
        bind='void BtlObjMdl_BindFiles(ObjMdlObj *obj) {\n'
        if text.count(bind)!=1:raise ValueError('owned native files binder source changed')
        for anchor in ['!mdl->file->bound','mdl->model->bound = 1;']:
            if text.count(anchor)!=1:raise ValueError('model bound bit source changed')
        text=text.replace('!mdl->file->bound','!(((u32 *)mdl->file)[3] & 0x80000000u)')
        text=text.replace('mdl->model->bound = 1;','((u32 *)mdl->model)[3] |= 0x80000000u;')
        return text.replace(bind,bind+'    extern int XbFighter_BindOwnedFiles(void *);\n    if (XbFighter_BindOwnedFiles(obj)) return;\n'), []
    expected = {
        'src/sys/gfx.c': ('PortQuit_Draw', 1),
        'src/battle/battle_load.c': ('DrNet_BattleFinished', 1),
        'src/battle/btl_act_e.c': ('Port_ClashLog', 1),
        'src/battle/btl_act_f.c': ('Port_ClashLog', 2),
    }
    if source not in expected:
        return text, []
    name, count = expected[source]
    pattern = re.compile(r'(?m)^(\s*)'+name+r'\([^;\n]*\);')
    matches = list(pattern.finditer(text))
    if len(matches) != count:
        raise ValueError(f'{source}: expected {count} explicit {name} call sites, got {len(matches)}')
    return pattern.sub(lambda m: m[1]+'/* fixed offline build omits PC hook '+name+' */', text), [name]*count
