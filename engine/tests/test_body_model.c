/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "dr_body_model.h"
#include "dr_elf.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void put(unsigned char *p,unsigned v) {for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static int read_memory(void *p,uint64_t at,void *out,size_t n) {memcpy(out,(unsigned char *)p+at,n);return 0;}
int main(void)
{
    unsigned char p[0x100]={0};memcpy(p,"pmdl",4);put(p+0x6c,0x80);
    put(p+0x80,64);p[0xc4]=2;p[0xc6]=1; /* Child pops itself and root. */
    DrBodyPart parts[4],before[4];memset(parts,0xa5,sizeof(parts));
    DrBodyModel model;size_t bad;
    assert(!dr_body_model_read(p,sizeof(p),parts,4,&model,&bad));
    assert(model.parts==2 && !model.chains && parts[0].parent==UINT16_MAX && parts[1].parent==0);
    memcpy(before,parts,sizeof(parts));
    for(size_t n=0;n<sizeof(p);n++) {
        assert(dr_body_model_read(p,n,parts,4,&model,&bad));
        assert(!memcmp(parts,before,sizeof(parts)));
    }
    assert(dr_body_model_read(p,sizeof(p),parts,1,&model,&bad)==DR_MODEL_CAPACITY);
    p[0xc4]=3;assert(dr_body_model_read(p,sizeof(p),parts,4,&model,&bad)==DR_MODEL_INVALID);p[0xc4]=2;
    p[0xc8]=1;assert(dr_body_model_read(p,sizeof(p),parts,4,&model,&bad)==DR_MODEL_INVALID);p[0xc8]=0;
    put(p+0x80,80);assert(dr_body_model_read(p,sizeof(p),parts,4,&model,&bad)==DR_MODEL_INVALID);put(p+0x80,64);
    assert(!memcmp(parts,before,sizeof(parts)));
    unsigned char elf[256]={0};memcpy(elf,"\x7f" "ELF",4);elf[4]=elf[5]=elf[6]=1;elf[18]=8;
    put(elf+28,52);elf[42]=32;elf[44]=1;put(elf+52,1);put(elf+56,128);put(elf+60,0x2000);put(elf+68,64);
    dr_disc d={.read=read_memory,.opaque=elf,.size=sizeof(elf)};dr_span file={.size=sizeof(elf)},span;
    assert(!dr_elf_mips_span(&d,&file,0x2010,48,&span) && span.offset==144 && span.size==48);
    assert(dr_elf_mips_span(&d,&file,0x2010,49,&span)==DR_NOT_FOUND && !span.size);
    assert(dr_elf_mips_span(&d,&file,0x2040,1,&span)==DR_NOT_FOUND);
    elf[44]=2;memcpy(elf+84,elf+52,32);
    assert(dr_elf_mips_span(&d,&file,0x2010,4,&span)==DR_FORMAT);
    elf[44]=1;put(elf+68,129);
    assert(dr_elf_mips_span(&d,&file,0x2010,4,&span)==DR_RANGE);
    puts("PASS body reader: hierarchy, all truncations, no partial output, bounds; ELF mapping and overlap rejection");
    return 0;
}
