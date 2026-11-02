/* SPDX-License-Identifier: AGPL-3.0-or-later
 * DMA semantics: upstream gs_core.c/run_chain and include/sys/dma.h at
 * 3ca419c. Byte reads and bounded mappings replace native pointer traversal.
 * VIF executes synchronously through the checked LE reference VU interpreter.
 */
#include "dr_gs_stream.h"
#include "dr_vu1.h"
#include <string.h>
static uint32_t le32(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int fail(DrGsStream *s,int e) {if(!s->error)s->error=e;return s->error;}
static void kick(void *opaque,const unsigned char *p,uint32_t n)
{
    DrGsStream *s=opaque;if(s->error)return;
    int e=s->gif(s->opaque,p,n);if(e)fail(s,e);else s->stats.gif_quadwords+=n;
}
int dr_gs_stream_init(DrGsStream *s,DrStreamGif gif,void *opaque)
{
    if(!s || !gif)return DR_STREAM_INVALID;
    memset(s,0,sizeof(*s));s->gif=gif;s->opaque=opaque;s->cl=s->wl=1;
    s->tag_budget=8192;s->word_budget=262144;s->vu_budget=10000;
    dr_vu1_reset(kick,s);return 0;
}
static int payload(DrGsStream *s)
{
    uint32_t c=(s->pending>>24)&127,n=(s->pending>>16)&255,imm=s->pending&65535;
    if(c==0x4a)return dr_vu1_program(imm,s->payload,s->need);
    if(c>=0x60)return dr_vu1_unpack(c,n,imm,s->payload,s->need);
    return dr_vu1_vif_control(s->pending,s->payload,s->need);
}
int dr_gs_stream_vif(DrGsStream *s,const void *bytes,size_t size)
{
    if(!s || !s->gif)return DR_STREAM_INVALID;
    if(s->error)return s->error;
    if((!bytes && size) || size%4)return fail(s,DR_STREAM_INVALID);
    if(!s->word_budget || s->word_budget>4194304 || !s->vu_budget || s->vu_budget>4000000 ||
       s->stats.words>s->word_budget || size/4>s->word_budget-s->stats.words)return fail(s,DR_STREAM_BUDGET);
    s->stats.words+=(uint32_t)(size/4);
    const unsigned char *p=bytes;
    while(size) {
        if(s->direct) {
            if(!s->direct_have && size>=16) {
                uint32_t n=(uint32_t)(size/16);if(n>s->direct)n=s->direct;
                kick(s,p,n);s->direct-=n;p+=n*16;size-=n*16;
            } else {
                size_t n=16-s->direct_have;if(n>size)n=size;
                memcpy(s->quad+s->direct_have,p,n);s->direct_have+=(uint32_t)n;p+=n;size-=n;
                if(s->direct_have==16) {kick(s,s->quad,1);s->direct--;s->direct_have=0;}
            }
            if(s->error)return s->error;
            continue;
        }
        if(s->need) {
            size_t n=s->need-s->have;if(n>size)n=size;
            memcpy(s->payload+s->have,p,n);s->have+=(uint32_t)n;p+=n;size-=n;
            if(s->have==s->need) {
                int e=payload(s);s->need=s->have=0;if(e)return fail(s,e);
            }
            continue;
        }
        uint32_t code=le32(p),c=(code>>24)&127,n=(code>>16)&255,imm=code&65535;
        p+=4;size-=4;s->pending=code;s->stats.fail_command=code;
        int e=0;
        switch(c) {
        case 0: case 6: case 7: case 0x10: case 0x11: case 0x13:
            break; /* NOP/MSKPATH3/MARK and synchronous flush operations */
        case 1:
            s->cl=(imm&255)?imm&255:256;s->wl=(imm>>8)?imm>>8:256;
            e=dr_vu1_vif_control(code,NULL,0);break;
        case 2: case 3: case 4: case 5:
            e=dr_vu1_vif_control(code,NULL,0);break;
        case 0x14: case 0x15: case 0x17:
            s->stats.vu_calls++;
            e=dr_vu1_call(c==0x17?-1:(int)imm,s->vu_budget);break;
        case 0x20:s->need=4;break;
        case 0x30: case 0x31:s->need=16;break;
        case 0x4a:s->need=(n?n:256)*8;break;
        case 0x50: case 0x51:s->direct=imm?imm:65536;break;
        default:
            if(c>=0x60) {
                unsigned vn=(c>>2)&3,vl=c&3,count=n?n:256;
                if(vl==3 && vn!=3)return fail(s,DR_STREAM_COMMAND);
                unsigned bits=vn==3 && vl==3?16:(32u>>vl)*(vn+1);
                if(s->wl>s->cl)count=s->cl*(count/s->wl)+(count%s->wl>s->cl?s->cl:count%s->wl);
                s->need=((count*bits+31)/32)*4;
                if(!s->need || s->need>sizeof(s->payload))return fail(s,DR_STREAM_COMMAND);
            } else return fail(s,DR_STREAM_COMMAND);
        }
        if(e)return fail(s,e);
        if(s->error)return s->error;
    }
    return 0;
}
int dr_gs_stream_finish(DrGsStream *s)
{
    if(!s || !s->gif)return DR_STREAM_INVALID;
    if(s->error)return s->error;
    if(s->need || s->direct || s->direct_have)return fail(s,DR_STREAM_TRUNCATED);
    return 0;
}
static const unsigned char *mapped(const DrGsSpan *spans,size_t n,uint32_t address,uint32_t bytes)
{
    if((uint64_t)address+bytes>0x100000000ull)return NULL;
    for(size_t i=0;i<n;i++)if(address>=spans[i].address) {
        uint64_t at=(uint64_t)address-spans[i].address;
        if(at<=spans[i].size && bytes<=spans[i].size-at)return (const unsigned char *)spans[i].bytes+(size_t)at;
    }
    return NULL;
}
static int chain(DrGsStream *s,const DrGsSpan *spans,size_t n,uint32_t address,int tte,int gif)
{
    if(!s || !s->gif)return DR_STREAM_INVALID;
    if(s->error)return s->error;
    if(!spans || !n || n>64 || (tte!=0 && tte!=1))return fail(s,DR_STREAM_INVALID);
    if(!s->tag_budget || s->tag_budget>100000 || !s->word_budget || s->word_budget>4194304)return fail(s,DR_STREAM_BUDGET);
    for(size_t i=0;i<n;i++) {
        if(!spans[i].bytes || !spans[i].size || spans[i].size>0x100000000ull-spans[i].address)return fail(s,DR_STREAM_RANGE);
        for(size_t j=0;j<i;j++)if((uint64_t)spans[i].address<(uint64_t)spans[j].address+spans[j].size &&
            (uint64_t)spans[j].address<(uint64_t)spans[i].address+spans[i].size)return fail(s,DR_STREAM_RANGE);
    }
    memset(&s->stats,0,sizeof(s->stats));
    uint32_t stack[2],sp=0;
    for(;;) {
        s->stats.fail_address=address;
        if(s->stats.tags>=s->tag_budget)return fail(s,DR_STREAM_BUDGET);
        s->stats.tags++;
        if(address&15)return fail(s,DR_STREAM_RANGE);
        const unsigned char *tag=mapped(spans,n,address,16);if(!tag)return fail(s,DR_STREAM_RANGE);
        uint32_t word=le32(tag),qwc=word&65535,id=(word>>28)&7,ref=le32(tag+4),count=qwc*16;
        /* Scratchpad selection must be mapped explicitly; never mask it into
         * an unrelated low address. The native game-address mapping is exact. */
        uint64_t inline_address=(uint64_t)address+16,next=inline_address+count;
        if(inline_address>0x100000000ull ||
           ((id!=0 && id!=3 && id!=4) && next>0x100000000ull))return fail(s,DR_STREAM_RANGE);
        uint32_t data_address=(id==0 || id==3 || id==4)?ref:(uint32_t)inline_address;
        const unsigned char *data=count?mapped(spans,n,data_address,count):NULL;
        if(count && (!data || (data_address&15)))return fail(s,DR_STREAM_RANGE);
        int end=0;
        switch(id) {
        case 0:end=1;break;
        case 1:if(next>UINT32_MAX)return fail(s,DR_STREAM_RANGE);address=(uint32_t)next;break;
        case 2:address=ref;break;
        case 3:case 4:
            if(inline_address>UINT32_MAX)return fail(s,DR_STREAM_RANGE);
            address=(uint32_t)inline_address;break;
        case 5:
            if(sp==2)return fail(s,DR_STREAM_STACK);
            if(next>UINT32_MAX)return fail(s,DR_STREAM_RANGE);
            stack[sp++]=(uint32_t)next;address=ref;break;
        case 6:
            if(!sp)return fail(s,DR_STREAM_STACK);
            address=stack[--sp];break;
        case 7:end=1;break;
        }
        int e=tte?dr_gs_stream_vif(s,tag+8,8):0;
        if(!e && count)e=gif?dr_gs_stream_gif(s,data,count):dr_gs_stream_vif(s,data,count);
        if(e)return e;
        if(end)return gif?s->error:dr_gs_stream_finish(s);
    }
}

int dr_gs_stream_gif(DrGsStream *s,const void *bytes,size_t size)
{
    if(!s || !s->gif)return DR_STREAM_INVALID;
    if(s->error)return s->error;
    if((!bytes && size) || size%16)return fail(s,DR_STREAM_INVALID);
    if(!s->word_budget || s->word_budget>4194304 || s->stats.words>s->word_budget ||
       size/4>s->word_budget-s->stats.words)return fail(s,DR_STREAM_BUDGET);
    s->stats.words+=(uint32_t)(size/4);
    if(size)kick(s,bytes,(uint32_t)(size/16));
    return s->error;
}
int dr_gs_stream_chain(DrGsStream *s,const DrGsSpan *spans,size_t n,uint32_t address,int tte)
{return chain(s,spans,n,address,tte,0);}
int dr_gs_stream_gif_chain(DrGsStream *s,const DrGsSpan *spans,size_t n,uint32_t address)
{return chain(s,spans,n,address,0,1);}
