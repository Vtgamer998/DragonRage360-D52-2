/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "dr_gs_native.h"
#include <string.h>
#include "dr_vu1.h"
static struct {DrGsStream stream;DrGsSpan spans[64];size_t count;int opened,busy;} native;
int dr_gs_native_reject(int e)
{if(!native.opened)return DR_STREAM_INVALID;if(!native.stream.error)native.stream.error=e?e:DR_STREAM_INVALID;return native.stream.error;}
int dr_gs_native_error(void) {return native.opened?native.stream.error:DR_STREAM_INVALID;}
int dr_gs_native_open(DrStreamGif gif,void *opaque)
{
    if(native.opened || !gif)return DR_STREAM_INVALID;
    memset(&native,0,sizeof(native));
    int e=dr_gs_stream_init(&native.stream,gif,opaque);if(e)return e;
    native.opened=1;return 0;
}
int dr_gs_native_limits(uint32_t tags,uint32_t words,uint32_t instructions) {
    if(!native.opened || native.busy || native.count || native.stream.error || !tags || tags>65536 || !words || words>4194304 || !instructions || instructions>4000000)return DR_STREAM_INVALID;
    native.stream.tag_budget=tags;native.stream.word_budget=words;native.stream.vu_budget=instructions;return 0;
}
int dr_gs_native_close(void)
{
    if(!native.opened || native.busy)return DR_STREAM_INVALID;
    dr_vu1_model_renderer(NULL,NULL);dr_vu1_stage_renderer(NULL,NULL);dr_vu1_reset(NULL,NULL);memset(&native,0,sizeof(native));return 0;
}
int dr_gs_native_reset(void)
{
    if(!native.opened || native.busy || native.count)return DR_STREAM_INVALID;
    DrStreamGif gif=native.stream.gif;void *opaque=native.stream.opaque;
    uint32_t tags=native.stream.tag_budget,words=native.stream.word_budget,instructions=native.stream.vu_budget;
    int e=dr_gs_stream_init(&native.stream,gif,opaque);
    if(!e){native.stream.tag_budget=tags;native.stream.word_budget=words;native.stream.vu_budget=instructions;}
    return e;
}
int dr_gs_native_bind(const DrGsSpan *spans,size_t count)
{
    if(!native.opened)return DR_STREAM_INVALID;
    if(native.stream.error)return native.stream.error;
    if(native.busy || native.count || !spans || !count || count>64)return dr_gs_native_reject(DR_STREAM_INVALID);
    size_t total=0;
    for(size_t i=0;i<count;i++) {
        if(!spans[i].bytes || !spans[i].size || spans[i].size>0x4000000u-total ||
           spans[i].size>0x100000000ull-spans[i].address)
            return dr_gs_native_reject(DR_STREAM_RANGE);
        total+=spans[i].size;
        for(size_t j=0;j<i;j++)if((uint64_t)spans[i].address<(uint64_t)spans[j].address+spans[j].size &&
            (uint64_t)spans[j].address<(uint64_t)spans[i].address+spans[i].size)
            return dr_gs_native_reject(DR_STREAM_RANGE);
    }
    memcpy(native.spans,spans,count*sizeof(*spans));native.count=count;return 0;
}
void dr_gs_native_unbind(void)
{if(native.busy){dr_gs_native_reject(DR_STREAM_INVALID);return;}memset(native.spans,0,sizeof(native.spans));native.count=0;}
int dr_gs_native_vif1(uint32_t address,int tte)
{
    if(!native.opened)return DR_STREAM_INVALID;
    if(native.busy)return dr_gs_native_reject(DR_STREAM_INVALID);
    if(!native.count)return dr_gs_native_reject(DR_STREAM_RANGE);
    native.busy=1;int e=dr_gs_stream_chain(&native.stream,native.spans,native.count,address,tte);
    native.busy=0;return e;
}
int dr_gs_native_gif(uint32_t address,uint32_t qwc,int chain)
{
    if(!native.opened)return DR_STREAM_INVALID;
    if(native.stream.error)return native.stream.error;
    if(!native.count)return dr_gs_native_reject(DR_STREAM_RANGE);
    if(native.busy)return dr_gs_native_reject(DR_STREAM_INVALID);
    if(chain==1) {
        native.busy=1;int e=dr_gs_stream_gif_chain(&native.stream,native.spans,native.count,address);
        native.busy=0;return e;
    }
    if(chain!=0 || (address&15) || qwc>65536)return dr_gs_native_reject(DR_STREAM_INVALID);
    if(!qwc)return 0;
    uint64_t bytes=(uint64_t)qwc*16;
    for(size_t i=0;i<native.count;i++)if(address>=native.spans[i].address) {
        size_t at=address-native.spans[i].address;
        if(at<=native.spans[i].size && bytes<=native.spans[i].size-at) {
            memset(&native.stream.stats,0,sizeof(native.stream.stats));
            native.busy=1;
            int e=dr_gs_stream_gif(&native.stream,(const unsigned char *)native.spans[i].bytes+at,(size_t)bytes);
            native.busy=0;return e;
        }
    }
    return dr_gs_native_reject(DR_STREAM_RANGE);
}
int dr_gs_native_stats(DrGsStreamStats *out)
{if(!native.opened || !out)return DR_STREAM_INVALID;*out=native.stream.stats;return 0;}
