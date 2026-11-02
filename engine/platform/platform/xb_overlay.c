/* SPDX-License-Identifier: MIT */
#include "xb_overlay.h"
#include <stdlib.h>
#include <string.h>
enum { LIMIT=4*1024*1024,REGIONS=2048 };
static struct {XbOverlayRegion *regions;size_t count;uint8_t *initial;int busy,ready;} ov;
static int contains(uintptr_t lo,uintptr_t hi,uintptr_t at,size_t n)
{return hi>=lo && at>=lo && at<=hi && n<=hi-at;}
static int intersects(uintptr_t a,size_t an,uintptr_t b,size_t bn)
{return a<=b?b-a<an:a-b<bn;}
int XbOverlay_Ready(void){return ov.ready;}
int XbOverlay_Close(void) {
    if(ov.busy)return XBOV_STATE;
    free(ov.initial);free(ov.regions);memset(&ov,0,sizeof(ov));return 0;
}
int XbOverlay_Prepare(const XbOverlayCatalog *c,const XbDataFiles *files) {
    if(ov.busy || ov.ready)return XBOV_STATE;
    if(!c)return XBOV_CATALOG;
    XbOverlayCatalog local=*c;c=&local;
    if(c->version!=1 || !c->units || !c->regions || !c->count ||
       c->count>REGIONS || c->reloc_count>65536 || (c->reloc_count && !c->relocs) ||
       !c->entry || !files || !files->read || !c->source_size || c->source_size>LIMIT)return XBOV_CATALOG;
    size_t total=0;uintptr_t lo=(uintptr_t)c->data_begin,hi=(uintptr_t)c->data_end;
    if(hi<=lo || contains(lo,hi,(uintptr_t)c->entry,1))return XBOV_RANGE;
    for(size_t i=0;i<c->count;i++) {
        const XbOverlayRegion *r=c->regions+i;uintptr_t a=(uintptr_t)r->begin,b=(uintptr_t)r->end;
        if((r->policy!=XBOV_RESET && r->policy!=XBOV_PRESERVE) || b<a || b-a!=r->expected || !r->expected || r->expected>LIMIT-total || !contains(lo,hi,a,r->expected))return XBOV_RANGE;
        for(size_t j=0;j<i;j++)if(intersects(a,r->expected,(uintptr_t)c->regions[j].begin,c->regions[j].expected))return XBOV_RANGE;
        total+=r->expected;
    }
    for(size_t i=0;i<c->reloc_count;i++) {
        const XbOverlayReloc *r=c->relocs+i;int found=0;
        if(r->width!=4 && r->width!=8)return XBOV_RELOC;
        for(size_t j=0;j<c->count;j++)if(contains((uintptr_t)c->regions[j].begin,(uintptr_t)c->regions[j].end,(uintptr_t)r->slot,r->width))found=1;
        if(!found)return XBOV_RELOC;
        uint64_t value=0;if(r->width==4){uint32_t word;memcpy(&word,r->slot,4);value=word;}else memcpy(&value,r->slot,8);
        if(value!=(uint64_t)r->expected)return XBOV_RELOC;
    }
    /* Source bytes only establish identity. No source instruction or pointer
     * is ever a destination, a callable entry or a value in the native reset. */
    ov.busy=1;
    XbOverlayRegion *regions=malloc(c->count*sizeof(*regions));uint8_t *initial=malloc(total);
    if(!regions || !initial){free(regions);free(initial);ov.busy=0;return XBOV_MEMORY;}
    memcpy(regions,c->regions,c->count*sizeof(*regions));
    size_t at=0;
    for(size_t i=0;i<c->count;i++){memcpy(initial+at,regions[i].begin,regions[i].expected);at+=regions[i].expected;}
    uint8_t *source=NULL;size_t size=0;
    int error=files->read(files->ctx,"DBZP.BIN",&source,&size);
    uint64_t hash=0xcbf29ce484222325ull;
    if(!error && source && size==c->source_size)for(size_t i=0;i<size;i++)hash=(hash^source[i])*0x100000001b3ull;
    if(error || !source || size!=c->source_size || hash!=c->source_fnv) {free(source);free(regions);free(initial);ov.busy=0;return XBOV_SOURCE;}
    free(source);
    ov.regions=regions;ov.count=c->count;ov.initial=initial;ov.ready=1;ov.busy=0;return 0;
}
int XbOverlay_Restore(int index) {
    if(ov.busy || !ov.ready)return XBOV_STATE;
    if(index!=0)return XBOV_CATALOG;
    ov.busy=1;size_t at=0;
    for(size_t i=0;i<ov.count;i++){if(ov.regions[i].policy==XBOV_RESET)memcpy(ov.regions[i].begin,ov.initial+at,ov.regions[i].expected);at+=ov.regions[i].expected;}
    ov.busy=0;return 0;
}
#ifndef XB_HOST_TEST
#include "xb_disc.h"
/* Absent in the standalone launcher. The strict engine build always generates
 * a strong catalogue; missing catalogue is a boot error, never success. */
extern const XbOverlayCatalog gXbOverlayCatalog __attribute__((weak));
static uint64_t mount_generation;
static int overlay_read(void *ctx,const char *name,uint8_t **out,size_t *size) {
    (void)ctx;dr_span span;*out=NULL;*size=0;
    if(strcmp(name,"DBZP.BIN") || XbDisc_FindFile("disc/BIN/DBZP.BIN",&span) || span.size>LIMIT)return -1;
    *out=malloc((size_t)span.size);if(!*out)return -1;
    if(XbDisc_ReadSpan(&span,0,*out,(size_t)span.size)){free(*out);*out=NULL;return -1;}
    *size=(size_t)span.size;return 0;
}
int XbOverlay_PrepareBuild(void) {
    if(!&gXbOverlayCatalog || !XbDisc_Get())return XBOV_CATALOG;
    XbDataFiles files={overlay_read,NULL};int e=XbOverlay_Prepare(&gXbOverlayCatalog,&files);
    if(!e)mount_generation=XbDisc_Generation();
    return e;
}
int XbOverlay_Load(int index) {
    if(!XbDisc_Get() || mount_generation!=XbDisc_Generation())return XBOV_STATE;
    return XbOverlay_Restore(index);
}
#endif
