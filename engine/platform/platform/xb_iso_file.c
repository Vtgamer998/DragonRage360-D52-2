/* SPDX-License-Identifier: MIT
 * Sony read-only byte-file operations, using the real bounded ISO9660 reader.
 * sceCdlFILE layout: ps2dev/ps2sdk common/include/libcdvd-common.h (public).
 * Its date field is explicitly unavailable (zero); game Disc_Identify only
 * checks the return value. This is not a general libcdvd implementation.
 * No host paths, write flags, PS2 overlays or executable conversions here.
 */
#include "xb_iso_file.h"
#include "xb_disc.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
/* Keep the public ABI calls visible in the diagnostic's ELF as well as in
 * engine links; its probe must exercise these entry points, not inline copies. */
#if defined(__GNUC__)
#define XB_ISO_ENTRY __attribute__((noinline))
#else
#define XB_ISO_ENTRY
#endif
enum { SLOTS=32, CHUNK=65536 };
static struct {dr_span span;uint64_t mount;uint32_t generation;int32_t pos;int used;} slots[SLOTS];
static uint32_t next_generation;
static int path_of(const char *name,char path[256]) {
    if(!name) return -1;
    if(!strncasecmp(name,"cdrom0:",7))name+=7;
    else if(!strncasecmp(name,"cdrom:",6))name+=6;
    while(*name=='/' || *name=='\\')name++;
    size_t n=0;
    for(;*name && *name!=';';name++) {
        if(n==255 || *name==':')return -1;
        path[n++]=*name=='\\'?'/':*name;
    }
    if(*name==';') {name++;if(*name!='1' || name[1])return -1;}
    path[n]=0;return n?0:-1;
}
static int locate(const char *name,dr_span *span,char path[256]) {
    const dr_disc *disc=XbDisc_Get();
    if(!disc || path_of(name,path) || dr_find(disc,path,span)!=DR_OK ||
       span->directory || span->size>INT32_MAX)return -1;
    return 0;
}
static int index_of(int32_t fd) {
    if(fd<32)return -1;
    unsigned i=(unsigned)fd%SLOTS,g=(unsigned)fd/SLOTS;
    if(!slots[i].used || slots[i].generation!=g)return -1;
    if(!XbDisc_Get() || slots[i].mount!=XbDisc_Generation()) {slots[i].used=0;return -1;}
    return (int)i;
}
void XbIsoFiles_CloseAll(void) {for(unsigned i=0;i<SLOTS;i++)slots[i].used=0;}
XB_ISO_ENTRY int32_t sceOpen(char *name,int32_t flags) {
    char path[256];dr_span span;
    if(flags!=1 || locate(name,&span,path))return -1;
    uint64_t mount=XbDisc_Generation();
    for(unsigned i=0;i<SLOTS;i++) {
        if(slots[i].used && slots[i].mount!=mount)slots[i].used=0;
        if(!slots[i].used) {
            if(next_generation==(uint32_t)INT32_MAX/SLOTS)return -1;
            slots[i].generation=++next_generation;slots[i].span=span;
            slots[i].mount=mount;slots[i].pos=0;slots[i].used=1;
            return (int32_t)(slots[i].generation*SLOTS+i);
        }
    }
    return -1;
}
XB_ISO_ENTRY int32_t sceClose(int32_t fd) {
    int i=index_of(fd);if(i<0)return -1;slots[i].used=0;return 0;
}
XB_ISO_ENTRY int32_t sceLseek(int32_t fd,int32_t offset,int32_t whence) {
    int i=index_of(fd);if(i<0 || whence<0 || whence>2)return -1;
    int64_t base=whence==0?0:whence==1?slots[i].pos:(int64_t)slots[i].span.size;
    int64_t next=base+offset;if(next<0 || next>INT32_MAX)return -1;
    slots[i].pos=(int32_t)next;return (int32_t)next;
}
XB_ISO_ENTRY int32_t sceRead(int32_t fd,void *buf,int32_t size) {
    int i=index_of(fd);if(i<0 || size<0 || (!buf && size))return -1;
    if(!size || (uint64_t)slots[i].pos>=slots[i].span.size)return 0;
    size_t want=(size_t)size;uint64_t left=slots[i].span.size-(uint32_t)slots[i].pos;
    if(want>left)want=(size_t)left;
    uint8_t *scratch=malloc(want<CHUNK?want:CHUNK);if(!scratch)return -1;
    size_t done=0;
    while(done<want) {
        size_t part=want-done;if(part>CHUNK)part=CHUNK;
        if(XbDisc_ReadSpan(&slots[i].span,(uint32_t)slots[i].pos,scratch,part)!=DR_OK)break;
        memcpy((uint8_t *)buf+done,scratch,part);done+=part;slots[i].pos+=(int32_t)part;
    }
    free(scratch);return done?(int32_t)done:-1;
}
XB_ISO_ENTRY int32_t sceCdSearchFile(void *file,char *name) {
    char path[256];dr_span span;
    if(!file || locate(name,&span,path) || span.offset%2048 || span.offset/2048>UINT32_MAX)return 0;
    uint8_t result[32]={0};uint32_t lsn=(uint32_t)(span.offset/2048),size=(uint32_t)span.size;
    memcpy(result,&lsn,4);memcpy(result+4,&size,4);
    const char *base=strrchr(path,'/');base=base?base+1:path;
    size_t n=strlen(base);if(n>16)n=16;memcpy(result+8,base,n);
    memcpy(file,result,sizeof(result));return 1;
}
int XbIsoFiles_Probe(void) {
    uint8_t info[32],header[20];uint32_t size;
    char name[]="cdrom0:\\SLUS_216.78;1";
    if(sceCdSearchFile(info,name)!=1)return -1;
    memcpy(&size,info+4,4);
    int fd=sceOpen(name,1);if(fd<0)return -1;
    int ok=size>=52 && sceLseek(fd,0,2)==(int32_t)size &&
        sceLseek(fd,0,0)==0 && sceRead(fd,header,20)==20 &&
        !memcmp(header,"\x7f" "ELF\x01\x01\x01",7) &&
        header[16]==2 && header[17]==0 && header[18]==8 && header[19]==0;
    int closed=sceClose(fd);return ok && !closed?0:-1;
}
