/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Format: include/sys/gfxm_b_b.h and port/tools/menu_font.py at upstream
 * 3ca419c7966b11438e2dc3c9a7e1914f102ed545. No game pixels are embedded. */
#include "dr_texfile.h"
#include "dr_gs_texture.h"
#include "dr_container.h"
#include <stdlib.h>
#include <string.h>
enum {INVALID=-300,UNSUPPORTED=-301,CAPACITY=-302,MEMORY=-303};
static uint32_t word(const unsigned char *p) {return dr_resource_le32(p);}
static uint64_t wide(const unsigned char *p) {return word(p)|(uint64_t)word(p+4)<<32;}
int dr_texfile_prepare(void *data,size_t size,int big_endian)
{
    if(!data || size<32 || size>16u*1024*1024)return INVALID;
    unsigned char *p=data;uint32_t count=word(p);
    uint64_t table=(uint64_t)word(p+4)*4;
    if(!count || count>256 || table<32 || (table&15) ||
       table>size || count>(size-table)/64)return INVALID;
    /* Complete validation precedes every write, including pointer clearing. */
    for(uint32_t i=0;i<count;i++) {
        int rc=dr_texfile_validate_transfers(data,size,i);if(rc)return rc;
        if(p[(size_t)table+i*64+0x2c]>=16)return UNSUPPORTED;
    }
    if(big_endian) {
        for(unsigned at=0;at<8;at+=4)
            for(unsigned j=0;j<2;j++){unsigned char t=p[at+j];p[at+j]=p[at+3-j];p[at+3-j]=t;}
    }
    memset(p+0x10,0,4);
    for(uint32_t i=0;i<count;i++) {
        unsigned char *e=p+(size_t)table+i*64;
        if(big_endian) {
            for(unsigned at=0;at<0x28;at+=4)
                for(unsigned j=0;j<2;j++){unsigned char t=e[at+j];e[at+j]=e[at+3-j];e[at+3-j]=t;}
            for(unsigned j=0;j<4;j++){unsigned char t=e[0x30+j];e[0x30+j]=e[0x37-j];e[0x37-j]=t;}
        }
        memset(e+0x38,0,8);
    }
    return 0;
}
typedef struct {const unsigned char *pixels;uint32_t width,height,bw;} Upload;
static int packet(const unsigned char *p,size_t size,uint32_t blt,Upload *u)
{
    if(size<128 || size%16 || blt&~63u)return UNSUPPORTED;
    if(word(p) || word(p+4) || word(p+8) || word(p+12)!=(0x50000000u|(uint32_t)(size/16-1)))return INVALID;
    if(wide(p+16)!=UINT64_C(0x1000000000008003) || wide(p+24)!=14 ||
       wide(p+32) || wide(p+40)!=0x51 || wide(p+56)!=0x52 || wide(p+64) || wide(p+72)!=0x53)return UNSUPPORTED;
    uint64_t dimensions=wide(p+48);uint32_t w=(uint32_t)dimensions,h=(uint32_t)(dimensions>>32);
    if(!w || !h || w>1024 || h>1024)return INVALID;
    size_t bytes=(size_t)w*h*4;
    if(bytes%16 || bytes>0x7fff*16 || size!=bytes+128)return UNSUPPORTED;
    if(wide(p+80)!=(UINT64_C(0x0800000000008000)|bytes/16) || wide(p+88))return INVALID;
    const unsigned char *tail=p+96+bytes;
    if(wide(tail)!=UINT64_C(0x1000000000008001) || wide(tail+8)!=14 || wide(tail+16) || wide(tail+24)!=0x3f)return INVALID;
    *u=(Upload){p+96,w,h,blt};return 0;
}
static int locate(const unsigned char *p,size_t size,size_t table_end,uint32_t off,uint32_t bytes,uint32_t blt,Upload *u)
{
    uint64_t at=(uint64_t)off*4;
    if(!off || at<table_end || at>size || bytes>size-at)return INVALID;
    return packet(p+(size_t)at,bytes,blt,u);
}
static int upload(unsigned char *vram,unsigned char *covered,const Upload *u,uint32_t bp)
{
    for(uint32_t y=0;y<u->height;y++)for(uint32_t x=0;x<u->width;x++) {
        size_t off;unsigned shift,bits;int rc=dr_gs_address(bp,u->bw,0,x,y,&off,&shift,&bits);if(rc)return rc;
        if(bp==0 && off>=0x3fc000)return UNSUPPORTED;
        memcpy(vram+off,u->pixels+((size_t)y*u->width+x)*4,4);
        covered[off>>5]|=(unsigned char)(1u<<((off>>2)&7));
    }
    return 0;
}
int dr_font_prepare(void *data,size_t size,int be)
{
    if(!data || size<128 || size>1024*1024 || (be!=0 && be!=1))return INVALID;
    unsigned char *p=data;
    if(memcmp(p,"5tnF 2SP",8) || word(p+8)!=1 || word(p+12)!=64 || word(p+16)!=1)return INVALID;
    size_t glyph=word(p+20),count=word(p+24);
    if(!count || count>4096 || glyph<128 || glyph>size || count>(size-glyph)/6 || !word(p+32) || word(p+32)>128)return INVALID;
    unsigned char *e=p+64;uint64_t tex=wide(e+48);
    unsigned width=1u<<((tex>>26)&15),height=1u<<((tex>>30)&15),psm=(tex>>20)&63;
    if(width>1024 || height>1024 || (psm!=19 && psm!=20) || ((tex>>51)&1023))return UNSUPPORTED;
    Upload image,palette;uint32_t a=word(e),b=word(e+4),an=word(e+8),bn=word(e+12);
    if(a<128 || a>size || an>size-a || b<128 || b>size || bn>size-b || (a<b?an>b-a:bn>a-b) || a+an>glyph || b+bn>glyph)return INVALID;
    int rc=packet(p+a,an,word(e+24),&image);if(rc)return rc;
    rc=packet(p+b,bn,word(e+28),&palette);if(rc)return rc;
    size_t expected=(size_t)width*height;if(psm==20)expected/=2;
    if((size_t)image.width*image.height*4!=expected || palette.width!=16 || palette.height!=16)return UNSUPPORTED;
    if(be){
        for(unsigned at=12;at<36;at+=4)for(unsigned j=0;j<2;j++){unsigned char t=p[at+j];p[at+j]=p[at+3-j];p[at+3-j]=t;}
        for(unsigned at=0;at<40;at+=4)for(unsigned j=0;j<2;j++){unsigned char t=e[at+j];e[at+j]=e[at+3-j];e[at+3-j]=t;}
        for(unsigned j=0;j<4;j++){unsigned char t=e[48+j];e[48+j]=e[55-j];e[55-j]=t;}
        for(size_t at=glyph;at<glyph+count*6;at+=2){unsigned char t=p[at];p[at]=p[at+1];p[at+1]=t;}
    }
    memset(e+56,0,8);return 0;
}
static int present(const unsigned char *covered,uint32_t bp,uint32_t bw,uint32_t psm,uint32_t x,uint32_t y)
{
    size_t off;unsigned shift,bits;int rc=dr_gs_address(bp,bw,psm,x,y,&off,&shift,&bits);
    return !rc && (covered[off>>5]&(1u<<((off>>2)&7)));
}
/* Palette-only entries intentionally pair alternate palettes with another image. */
int dr_texfile_validate_palette(const void *data,size_t size,uint32_t index){
 if(!data || size<32)return INVALID;
 const unsigned char *p=data;uint32_t count=word(p);uint64_t table=(uint64_t)word(p+4)*4,end=table+(uint64_t)count*64;
 if(!count || count>1024 || index>=count || table<32 || end>size)return INVALID;
 const unsigned char *e=p+(size_t)table+index*64;
 if(word(e) || word(e+8) || word(e+16) || wide(e+48))return INVALID;
 Upload pal;int rc=locate(p,size,(size_t)end,word(e+4),word(e+12),word(e+28),&pal);
 if(rc)return rc;
 return ((pal.width==16 && pal.height==16) || (pal.width==8 && pal.height==2))?0:UNSUPPORTED;
}
/* Validate the transfer representation without claiming that the upload
 * initializes every texel in TEX0. Some original stage textures upload only
 * part of a GS block. Indexed pixel-only entries may use an external CLUT,
 * as Tex_UploadClut deliberately skips clutOfs==0. The RGBA decoder still
 * requires its own palette and complete coverage. */
int dr_texfile_validate_transfers(const void *data,size_t size,uint32_t index){
 if(!data || size<32)return INVALID;
 const unsigned char *p=data;uint32_t count=word(p);uint64_t table=(uint64_t)word(p+4)*4,end=table+(uint64_t)count*64;
 if(!count || count>1024 || index>=count || table<32 || end>size)return INVALID;
 const unsigned char *e=p+(size_t)table+index*64;uint64_t tex=wide(e+48);
 if(!word(e))return dr_texfile_validate_palette(data,size,index);
 unsigned width=1u<<((tex>>26)&15),height=1u<<((tex>>30)&15),psm=(tex>>20)&63;
 if(width>1024 || height>1024 || (psm!=0 && psm!=19 && psm!=20) || ((tex>>51)&1023))return UNSUPPORTED;
 Upload pix,pal;int rc=locate(p,size,(size_t)end,word(e),word(e+8),word(e+24),&pix);if(rc)return rc;
 size_t expected=(size_t)width*height;if(psm==0)expected*=4;else if(psm==20)expected=(expected+1)/2;
 if((size_t)pix.width*pix.height*4!=expected)return UNSUPPORTED;
 if(word(e+4) || word(e+12)){rc=locate(p,size,(size_t)end,word(e+4),word(e+12),word(e+28),&pal);if(rc)return rc;
  if((psm==19 && (pal.width!=16 || pal.height!=16)) || (psm==20 && (pal.width!=8 || pal.height!=2)))return UNSUPPORTED;
 }
 return 0;
}
int dr_texfile_decode(const void *data,size_t size,uint32_t index,void *rgba,size_t capacity,DrTexFileImage *info)
{
    if(info)memset(info,0,sizeof(*info));
    if(!data || !rgba || !info || size<32)return INVALID;
    const unsigned char *p=data;uint32_t count=word(p);uint64_t table=(uint64_t)word(p+4)*4,end=table+(uint64_t)count*64;
    if(!count || count>1024 || index>=count || table<32 || end>size)return INVALID;
    const unsigned char *e=p+(size_t)table+index*64;uint64_t tex=wide(e+48);
    DrGsTexture t={.width=1u<<((tex>>26)&15),.height=1u<<((tex>>30)&15),
        .tbw=(uint32_t)(tex>>14)&63,.psm=(uint32_t)(tex>>20)&63,.cbp=0x3fc0};
    if(t.width>1024 || t.height>1024 || (t.psm!=0 && t.psm!=19 && t.psm!=20) ||
       ((tex>>51)&1023))return UNSUPPORTED; /* CPSM/CSM/CSA must be zero; CLD is external state. */
    size_t bytes=(size_t)t.width*t.height*4;if(bytes>capacity)return CAPACITY;
    Upload pix,pal={0};int rc=locate(p,size,(size_t)end,word(e),word(e+8),word(e+24),&pix);if(rc)return rc;
    size_t source=(size_t)pix.width*pix.height*4,expected=(size_t)t.width*t.height;
    if(t.psm==0)expected*=4;else if(t.psm==20)expected=(expected+1)/2;
    if(source!=expected)return UNSUPPORTED;
    if(t.psm!=0) {
        rc=locate(p,size,(size_t)end,word(e+4),word(e+12),word(e+28),&pal);if(rc)return rc;
        if((t.psm==19 && (pal.width!=16 || pal.height!=16)) ||
           (t.psm==20 && (pal.width!=8 || pal.height!=2)))return UNSUPPORTED;
    }
    unsigned char *vram=calloc(1,0x400000),*covered=calloc(1,0x20000);
    if(!vram || !covered) {free(vram);free(covered);return MEMORY;}
    rc=upload(vram,covered,&pix,0);
    if(!rc && t.psm!=0)rc=upload(vram,covered,&pal,t.cbp);
    for(uint32_t y=0;!rc && y<t.height;y++)for(uint32_t x=0;x<t.width;x++)
        if(!present(covered,0,t.tbw,t.psm,x,y)) {rc=INVALID;break;}
    if(!rc)rc=dr_gs_texture_decode(vram,0x400000,&t,rgba,capacity);
    free(vram);free(covered);
    if(!rc)*info=(DrTexFileImage){t.width,t.height,t.psm,bytes};
    return rc;
}
int dr_texfile_model_image(const dr_disc *disc,const char *path,uint32_t index,void *rgba,size_t capacity,DrTexFileImage *info)
{
    if(info)memset(info,0,sizeof(*info));
    if(!disc || !path || !info)return INVALID;
    dr_span pack;int rc=dr_game_file(disc,path,&pack);if(rc)return rc;
    if(!pack.size || pack.size>16*1024*1024)return INVALID;
    unsigned char *data=malloc((size_t)pack.size);if(!data)return MEMORY;
    rc=dr_read(disc,&pack,0,data,(size_t)pack.size);size_t off,n;
    if(!rc)rc=dr_pack_memory_entry(data,(size_t)pack.size,0,&off,&n,NULL);
    if(!rc)rc=dr_texfile_decode(data+off,n,index,rgba,capacity,info);
    free(data);return rc;
}
int dr_texfile_material(const void *data,size_t size,uint64_t tex0,uint32_t *index)
{
    if(index)*index=UINT32_MAX;
    if(!data || !index || size<32)return INVALID;
    const unsigned char *p=data;uint32_t n=word(p);uint64_t at=(uint64_t)word(p+4)*4;
    if(!n || n>1024 || at<32 || at+(uint64_t)n*64>size)return INVALID;
    uint64_t pointers=UINT64_C(0x3fff)|(UINT64_C(0x3fff)<<37);
    uint64_t mask=~((UINT64_C(7)<<34)|(UINT64_C(7)<<61));unsigned matches=0;uint32_t selected=0;
    for(uint32_t i=0;i<n;i++) {
        const unsigned char *e=p+(size_t)at+i*64;uint32_t tbp=word(e+32),cbp=word(e+36);
        if(tbp>0x3fff || cbp>0x3fff)return UNSUPPORTED;
        uint64_t key=(wide(e+48)&~pointers)|tbp|((uint64_t)cbp<<37);
        if((key&mask)==(tex0&mask)) {selected=i;matches++;}
    }
    if(matches!=1)return matches?INVALID:DR_NOT_FOUND;
    *index=selected;return 0;
}
int dr_texfile_body_material(const dr_disc *disc,const char *path,uint64_t tex0,void *rgba,size_t capacity,
                            DrTexFileImage *info,uint32_t *index)
{
    if(info) {memset(info,0,sizeof(*info));}if(index) {*index=UINT32_MAX;}
    if(!disc || !path || !info || !index)return INVALID;
    dr_span pack;int rc=dr_game_file(disc,path,&pack);if(rc)return rc;
    if(!pack.size || pack.size>16*1024*1024)return INVALID;
    unsigned char *data=malloc((size_t)pack.size);if(!data)return MEMORY;
    rc=dr_read(disc,&pack,0,data,(size_t)pack.size);size_t off,n;uint32_t selected=0;
    /* BtlObj_BindTables: mdl->unk04 = BObjFile_GetEntry(file,0xC),
     * whose original table uses one-based indices: canonical entry 11. */
    if(!rc)rc=dr_pack_memory_entry(data,(size_t)pack.size,11,&off,&n,NULL);
    if(!rc)rc=dr_texfile_material(data+off,n,tex0,&selected);
    if(!rc)rc=dr_texfile_decode(data+off,n,selected,rgba,capacity,info);
    if(!rc)*index=selected;
    free(data);return rc;
}
