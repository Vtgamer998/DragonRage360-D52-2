/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Format references: original include/sys/gfxm_b_b.h, src/battle/bobj_a.c
 * BtlObj_BindTables and port/src/gs/gs_core.c VIF / gs_vu1.c UnpackWords,
 * revision 3ca419c7966b11438e2dc3c9a7e1914f102ed545.
 * Unlike TexFile these blocks contain geometry command streams, not texture
 * entry tables. RET returns to an outer CALL; its microprogram/state are external.
 */
#include "dr_face_vif.h"
#include <string.h>
static uint32_t le32(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int reject(size_t *bad,size_t at,int error)
{if(bad)*bad=at;return error;}
static int walk(const unsigned char *p,size_t end,DrFaceVifVisitor visitor,void *ctx,
                 DrFaceVifStats *stats,size_t *bad)
{
    size_t at=8; /* The upper two DMA-tag words are VIF commands too. */
    while(at<end) {
        DrFaceVifCommand c={0};c.code_offset=at;c.code=le32(p+at);at+=4;
        c.command=(c.code>>24)&0x7f;c.num=(c.code>>16)&255;c.immediate=c.code&65535;
        c.data_offset=at;
        switch(c.command) {
        case 0:stats->noops++;break;
        case 0x17:stats->continues++;break;
        case 0x6c:
            c.data_bytes=(size_t)(c.num?c.num:256)*16;
            if(c.data_bytes>end-at)return reject(bad,c.code_offset,DR_FACE_TRUNCATED);
            stats->unpacks++;stats->vectors+=c.num?c.num:256;break;
        default:return reject(bad,c.code_offset,DR_FACE_UNSUPPORTED);
        }
        stats->commands++;at+=c.data_bytes;
        if(visitor && visitor(ctx,&c))return DR_FACE_CALLBACK;
    }
    return 0;
}
int dr_face_vif_parse(const void *data,size_t size,DrFaceVifVisitor visitor,void *ctx,
                      DrFaceVifStats *out,size_t *bad)
{
    if(out) {memset(out,0,sizeof(*out));}if(bad) {*bad=SIZE_MAX;}
    if(!data)return reject(bad,0,DR_FACE_INVALID);
    if(size<32)return reject(bad,size,DR_FACE_TRUNCATED);
    const unsigned char *p=data;uint32_t tag=le32(p);
    if((tag&0xffff0000u)!=0x60000000u || le32(p+4))return reject(bad,0,DR_FACE_UNSUPPORTED);
    size_t payload=(size_t)(tag&65535)*16,end=16+payload;
    if(end>size || size-end<16)return reject(bad,0,DR_FACE_TRUNCATED);
    /* Each real pack has one zero-QWC END guard and up to 48 bytes of padding.
     * Guard/padding aren't executed as part of the RET's VIF input. */
    if(size-end>64 || (size-end)%16 || le32(p+end)!=0x70000000u)
        return reject(bad,end,DR_FACE_INVALID);
    for(size_t i=end+4;i<size;i++)if(p[i])return reject(bad,i,DR_FACE_INVALID);
    DrFaceVifStats stats={.dma_payload_bytes=payload,.guard_offset=end};
    int error=walk(p,end,NULL,NULL,&stats,bad);if(error)return error;
    if(visitor) {
        DrFaceVifStats ignored={0};error=walk(p,end,visitor,ctx,&ignored,bad);
        if(error)return error;
    }
    if(out) {*out=stats;}return 0;
}
