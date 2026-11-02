/* SPDX-License-Identifier: MIT; host native DATA reset, not gameplay. */
#include "xb_overlay.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static struct {int selected;double scale;void (*callback)(int);uint8_t bytes[33];} menu;
static unsigned calls;static void entry(int n){calls+=(unsigned)n;}
static const char *source_path;static int mutate,io_error;
static XbOverlayCatalog catalog;static XbOverlayRegion region;
static int read_source(void *ctx,const char *name,uint8_t **out,size_t *size) {
    (void)ctx;assert(!strcmp(name,"DBZP.BIN"));
    assert(XbOverlay_Restore(0)==XBOV_STATE && XbOverlay_Close()==XBOV_STATE);
    assert(XbOverlay_Prepare(&catalog,NULL)==XBOV_STATE);
    if(io_error)return -1;
    FILE *f=fopen(source_path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));*size=(size_t)ftell(f);rewind(f);
    *out=malloc(*size);assert(*out && fread(*out,1,*size,f)==*size);fclose(f);
    if(mutate){region.begin=(uint8_t *)(uintptr_t)entry;region.end=region.begin+region.expected;catalog.count=0;menu.selected=999;}
    return 0;
}
int main(int argc,char **argv) {
    assert(argc==2);source_path=argv[1];menu.selected=42;menu.scale=1.75;menu.callback=entry;
    for(unsigned i=0;i<33;i++)menu.bytes[i]=(uint8_t)(i*3);
    unsigned char expected[sizeof(menu)],code[16];memcpy(expected,&menu,sizeof(menu));memcpy(code,(const void *)(uintptr_t)entry,16);
    XbOverlayReloc reloc={&menu.callback,(uintptr_t)entry,sizeof(void *)};
    region=(XbOverlayRegion){(uint8_t *)&menu,(uint8_t *)(&menu+1),sizeof(menu),XBOV_RESET};
    catalog=(XbOverlayCatalog){1,1,&region,1,&reloc,1,(uint8_t *)&menu,(uint8_t *)(&menu+1),563996,0x91a321108fc31289ull,entry};
    XbDataFiles files={read_source,NULL};
    assert(XbOverlay_Restore(0)==XBOV_STATE);
    XbOverlayCatalog bad=catalog;bad.version=2;assert(XbOverlay_Prepare(&bad,&files)==XBOV_CATALOG);
    bad=catalog;bad.data_end=bad.data_begin;assert(XbOverlay_Prepare(&bad,&files)==XBOV_RANGE);
    XbOverlayRegion wrong=region;wrong.begin=(uint8_t *)(uintptr_t)entry;wrong.end=wrong.begin+wrong.expected;
    bad=catalog;bad.regions=&wrong;assert(XbOverlay_Prepare(&bad,&files)==XBOV_RANGE);
    XbOverlayRegion overlap[2]={region,region};bad=catalog;bad.count=2;bad.regions=overlap;assert(XbOverlay_Prepare(&bad,&files)==XBOV_RANGE);
    reloc.expected++;assert(XbOverlay_Prepare(&catalog,&files)==XBOV_RELOC);reloc.expected--;
    reloc.width=2;assert(XbOverlay_Prepare(&catalog,&files)==XBOV_RELOC);reloc.width=sizeof(void *);
    bad=catalog;bad.source_fnv^=1;assert(XbOverlay_Prepare(&bad,&files)==XBOV_SOURCE && !memcmp(expected,&menu,sizeof(menu)));
    io_error=1;assert(XbOverlay_Prepare(&catalog,&files)==XBOV_SOURCE);io_error=0;
    mutate=1;assert(!XbOverlay_Prepare(&catalog,&files));assert(menu.selected==999 && XbOverlay_Ready());
    assert(XbOverlay_Prepare(&catalog,&files)==XBOV_STATE);
    assert(XbOverlay_Restore(1)==XBOV_CATALOG && menu.selected==999);
    for(unsigned i=0;i<100;i++){
        memset(&menu,0xcc,sizeof(menu));assert(!XbOverlay_Restore(0));assert(!memcmp(expected,&menu,sizeof(menu)));
        menu.callback(1);
    }
    assert(calls==100 && !memcmp(code,(const void *)(uintptr_t)entry,16));
    assert(!XbOverlay_Close() && !XbOverlay_Ready() && XbOverlay_Restore(0)==XBOV_STATE);
    mutate=0;region=(XbOverlayRegion){(uint8_t *)&menu,(uint8_t *)(&menu+1),sizeof(menu),XBOV_PRESERVE};
    catalog.count=1;assert(!XbOverlay_Prepare(&catalog,&files));menu.selected=567;
    assert(!XbOverlay_Restore(0) && menu.selected==567);assert(!XbOverlay_Close());
    puts("PASS: actual native overlay loader, real USA source identity; native pointers/mixed fields restored 100 times; code unchanged; malformed regions, relocs, IO/hash errors, alias mutation and recursion refused; no PS2 bytes executed");
}
