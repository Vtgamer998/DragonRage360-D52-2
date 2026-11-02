/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "dr_elf.h"
#include <string.h>
static uint32_t word(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned half(const unsigned char *p) {return p[0]|(unsigned)p[1]<<8;}
int dr_elf_mips_span(const dr_disc *d,const dr_span *file,uint32_t va,size_t n,dr_span *out)
{
    if(!out)return DR_FORMAT;
    memset(out,0,sizeof(*out));
    if(!d || !file || file->directory || !n || file->size<52 ||
       file->offset>d->size || file->size>d->size-file->offset)return DR_RANGE;
    unsigned char h[52];int rc=dr_read(d,file,0,h,sizeof(h));if(rc)return rc;
    if(memcmp(h,"\x7f" "ELF",4) || h[4]!=1 || h[5]!=1 || h[6]!=1 || half(h+18)!=8)return DR_UNSUPPORTED;
    unsigned count=half(h+44),step=half(h+42);uint32_t table=word(h+28);
    if(!count || count>128 || step<32 || step>256 || (uint64_t)table+(uint64_t)step*count>file->size)return DR_FORMAT;
    dr_span selected={0};unsigned found=0;
    for(unsigned i=0;i<count;i++) {
        unsigned char ph[32];rc=dr_read(d,file,(uint64_t)table+(uint64_t)i*step,ph,sizeof(ph));if(rc)return rc;
        if(word(ph)!=1)continue;
        uint32_t at=word(ph+4),base=word(ph+8),size=word(ph+16);
        if((uint64_t)at+size>file->size)return DR_RANGE;
        if(va>=base && (uint64_t)va-base<size && n<=(uint64_t)size-(va-base)) {
            selected=(dr_span){.offset=file->offset+at+(va-base),.size=n};found++;
        }
    }
    if(found!=1)return found?DR_FORMAT:DR_NOT_FOUND;
    *out=selected;return 0;
}
