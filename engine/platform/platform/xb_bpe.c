/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Big-endian path of original Bpe_Decode: use the demo's bounded decoder,
 * then convert the expanded resource. Caller-supplied dst must fit rawSize,
 * as required by the original interface; unknown source buffers are rejected.
 */
#include <stdint.h>
#include "xb_resconv.h"
#include "dr_resource.h"
extern void *Heap_Alloc(int32_t size,int32_t alignment,int32_t flags,int32_t heap);
extern void Heap_Free(void *);
void *XbBpe_DecodeBounded(void *src,void *dst,uint32_t capacity,int32_t *rawSize) {
    size_t n=XbRes_BpeSourceSize(src),written=0;
    uint32_t raw;
    void *out;
    if(rawSize)*rawSize=0;
    if(XbRes_CheckedReadsActive() && XbRes_LastError())return 0;
    if(n<8){XbRes_SetReadError(-899);return 0;}
    raw=XbRes_Le32(src);
    if(!raw || raw>INT32_MAX || (dst && raw>capacity) || XbRes_Le32((uint8_t *)src+4)>n-8){XbRes_SetReadError(-899);return 0;}
    out=dst?dst:Heap_Alloc((int32_t)raw,0x20,0,2 /* HEAP_ANY */);
    if(!out){XbRes_SetReadError(-899);return 0;}
    if(dr_bpe_decode(src,n,out,raw,&written)!=DR_RES_OK || written!=raw) {
        if(!dst)Heap_Free(out);
        XbRes_SetReadError(-899);
        return 0;
    }
    XbRes_AfterBpe(src,out,(int32_t)written);
    if(XbRes_LastError()) { if(!dst)Heap_Free(out);
        return 0; }
    if(rawSize)*rawSize=(int32_t)written;
    return out;
}
void *XbBpe_Decode(void *src,void *dst,int32_t *rawSize) {
    return XbBpe_DecodeBounded(src,dst,INT32_MAX,rawSize);
}
