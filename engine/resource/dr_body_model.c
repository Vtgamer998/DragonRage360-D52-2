/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Layout: include/sys/gfxm_e.h ObjMdlMesh and gfxm_d_c.h ObjDrawPart;
 * hierarchy: include/battle/bobj_a.h BobjBone and src/sys/vu1_packet.c.
 * Upstream revision 3ca419c7966b11438e2dc3c9a7e1914f102ed545. */
#include "dr_body_model.h"
#include "dr_face_vif.h"
#include <string.h>
static uint32_t word(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint16_t half(const unsigned char *p) {return (uint16_t)(p[0]|(unsigned)p[1]<<8);}
static float scalar(const unsigned char *p) {uint32_t bits=word(p);float f;memcpy(&f,&bits,4);return f;}
static int error(size_t *bad,size_t at,int code) {if(bad)*bad=at;return code;}
static int scan(const unsigned char *p,size_t size,DrBodyPart *parts,size_t cap,DrBodyModel *model,size_t *bad)
{
    size_t at=word(p+0x6c);uint16_t stack[256];unsigned depth=0,count=0,chains=0;
    if(at<0x70 || at&15)return error(bad,0x6c,DR_MODEL_INVALID);
    for(;;) {
        if(at>size || size-at<64)return error(bad,at,DR_MODEL_TRUNCATED);
        if(count>=cap || count==256)return error(bad,at,DR_MODEL_CAPACITY);
        const unsigned char *v=p+at;uint32_t next=word(v);uint16_t pop=half(v+4),last=half(v+6),enabled=half(v+8);
        size_t bytes=next;
        if(!next && last && !enabled)bytes=64;
        if(bytes<64 || bytes&15)return error(bad,at,DR_MODEL_INVALID);
        if(bytes>size-at)return error(bad,at,DR_MODEL_TRUNCATED);
        if(pop>depth+1)return error(bad,at+4,DR_MODEL_INVALID);
        size_t chain=0,n=0;
        if(bytes>64) {
            if(bytes<128)return error(bad,at,DR_MODEL_INVALID);
            chain=at+96;n=bytes-96;size_t vif_bad;
            if(dr_face_vif_parse(p+chain,n,NULL,NULL,NULL,&vif_bad))
                return error(bad,vif_bad==SIZE_MAX?chain:chain+vif_bad,DR_MODEL_VIF);
            chains++;
        } else if(enabled)return error(bad,at+8,DR_MODEL_INVALID);
        if(parts) {
            DrBodyPart *out=&parts[count];memset(out,0,sizeof(*out));
            out->record_offset=at;out->record_bytes=bytes;out->chain_offset=chain;out->chain_bytes=n;
            out->parent=depth?stack[depth-1]:UINT16_MAX;out->pop=pop;out->last=last;
            out->node=half(v+10);out->enabled=enabled;out->flags=half(v+12);
            for(unsigned i=0;i<4;i++) {
                out->pivot_a[i]=scalar(v+16+i*4);out->pivot_b[i]=scalar(v+32+i*4);out->rest[i]=scalar(v+48+i*4);
            }
            if(chain)for(unsigned i=0;i<8;i++)out->bound_words[i]=word(v+64+i*4);
        }
        stack[depth++]=(uint16_t)count;depth-=pop;count++;
        if(last)break;
        if(!next)return error(bad,at,DR_MODEL_INVALID);
        at+=next;
    }
    if(model)*model=(DrBodyModel){.flags=word(p+12),.parts=count,.chains=chains,.first_part=word(p+0x6c)};
    return 0;
}
int dr_body_model_read(const void *data,size_t size,DrBodyPart *parts,size_t cap,DrBodyModel *out,size_t *bad)
{
    if(out) {memset(out,0,sizeof(*out));}if(bad) {*bad=SIZE_MAX;}
    if(!data || !parts || !out || !cap)return error(bad,0,DR_MODEL_INVALID);
    if(size<0x70)return error(bad,size,DR_MODEL_TRUNCATED);
    const unsigned char *p=data;if(memcmp(p,"pmdl",4))return error(bad,0,DR_MODEL_INVALID);
    int rc=scan(p,size,NULL,cap,NULL,bad);if(rc)return rc;
    return scan(p,size,parts,cap,out,bad);
}
