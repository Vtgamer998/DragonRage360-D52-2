/* SPDX-License-Identifier: AGPL-3.0-or-later
 * GS page/block/column tables and address equations adapted from
 * dfdx047 / z3xox Tenkaichi3DecompAndroid port/src/gs/gs_core.c
 * commit 3ca419c7966b11438e2dc3c9a7e1914f102ed545.
 * CPU-endian-independent loads; no native casts of VRAM bytes.
 */
#include "dr_gs_texture.h"
#include <string.h>
static const uint8_t kBlock32[4][8] = {{0, 1, 4, 5, 16, 17, 20, 21}, {2, 3, 6, 7, 18, 19, 22, 23}, {8, 9, 12, 13, 24, 25, 28, 29}, {10, 11, 14, 15, 26, 27, 30, 31}};
static const uint8_t kBlock16[8][4] = {{0, 2, 8, 10}, {1, 3, 9, 11}, {4, 6, 12, 14}, {5, 7, 13, 15}, {16, 18, 24, 26}, {17, 19, 25, 27}, {20, 22, 28, 30}, {21, 23, 29, 31}};
static const uint8_t kBlock16S[8][4] = {{0, 2, 16, 18}, {1, 3, 17, 19}, {8, 10, 24, 26}, {9, 11, 25, 27}, {4, 6, 20, 22}, {5, 7, 21, 23}, {12, 14, 28, 30}, {13, 15, 29, 31}};
static const uint8_t kCol32[8][8] = {{0, 1, 4, 5, 8, 9, 12, 13}, {2, 3, 6, 7, 10, 11, 14, 15}, {16, 17, 20, 21, 24, 25, 28, 29}, {18, 19, 22, 23, 26, 27, 30, 31},
                                     {32, 33, 36, 37, 40, 41, 44, 45}, {34, 35, 38, 39, 42, 43, 46, 47}, {48, 49, 52, 53, 56, 57, 60, 61}, {50, 51, 54, 55, 58, 59, 62, 63}};
static const uint8_t kCol16[8][16] = {
    {0, 2, 8, 10, 16, 18, 24, 26, 1, 3, 9, 11, 17, 19, 25, 27}, {4, 6, 12, 14, 20, 22, 28, 30, 5, 7, 13, 15, 21, 23, 29, 31},
    {32, 34, 40, 42, 48, 50, 56, 58, 33, 35, 41, 43, 49, 51, 57, 59}, {36, 38, 44, 46, 52, 54, 60, 62, 37, 39, 45, 47, 53, 55, 61, 63},
    {64, 66, 72, 74, 80, 82, 88, 90, 65, 67, 73, 75, 81, 83, 89, 91}, {68, 70, 76, 78, 84, 86, 92, 94, 69, 71, 77, 79, 85, 87, 93, 95},
    {96, 98, 104, 106, 112, 114, 120, 122, 97, 99, 105, 107, 113, 115, 121, 123}, {100, 102, 108, 110, 116, 118, 124, 126, 101, 103, 109, 111, 117, 119, 125, 127}};
static const uint8_t kCol8First[4][16] = {{0, 4, 16, 20, 32, 36, 48, 52, 2, 6, 18, 22, 34, 38, 50, 54}, {8, 12, 24, 28, 40, 44, 56, 60, 10, 14, 26, 30, 42, 46, 58, 62},
                                          {33, 37, 49, 53, 1, 5, 17, 21, 35, 39, 51, 55, 3, 7, 19, 23}, {41, 45, 57, 61, 9, 13, 25, 29, 43, 47, 59, 63, 11, 15, 27, 31}};
static const uint8_t kCol4First[4][32] = {
    {0, 8, 32, 40, 64, 72, 96, 104, 2, 10, 34, 42, 66, 74, 98, 106, 4, 12, 36, 44, 68, 76, 100, 108, 6, 14, 38, 46, 70, 78, 102, 110},
    {16, 24, 48, 56, 80, 88, 112, 120, 18, 26, 50, 58, 82, 90, 114, 122, 20, 28, 52, 60, 84, 92, 116, 124, 22, 30, 54, 62, 86, 94, 118, 126},
    {65, 73, 97, 105, 1, 9, 33, 41, 67, 75, 99, 107, 3, 11, 35, 43, 69, 77, 101, 109, 5, 13, 37, 45, 71, 79, 103, 111, 7, 15, 39, 47},
    {81, 89, 113, 121, 17, 25, 49, 57, 83, 91, 115, 123, 19, 27, 51, 59, 85, 93, 117, 125, 21, 29, 53, 61, 87, 95, 119, 127, 23, 31, 55, 63}};
int dr_gs_address(uint32_t bp,uint32_t bw,uint32_t psm,uint32_t x,uint32_t y,
                  size_t *word_byte,unsigned *shift,unsigned *bits)
{
    uint32_t a,word;
    if(!word_byte || !shift || !bits || bp>0x3fff || bw>63 || x>4095 || y>4095) return DR_GS_INVALID;
    if(!bw) bw=1;
    switch(psm) {
    case 2:case 0x0a:
        a=((bp+((y>>1)&~0x1fu)*bw+((x>>1)&~0x1fu)+
           (psm==0x0a?kBlock16S:kBlock16)[(y>>3)&7][(x>>4)&3])<<7)+kCol16[y&7][x&15];
        word=a>>1;*shift=(a&1)*16;*bits=16;break;
    case 0x13: {
        unsigned col=(y&15)>>2,sx=(col&1)?((x&15)^4):(x&15);
        unsigned column=kCol8First[y&3][sx]+col*64;
        a=((bp+((y>>1)&~0x1fu)*(bw>>1)+((x>>2)&~0x1fu)+kBlock32[(y>>4)&3][(x>>4)&7])<<8)+column;
        word=a>>2;*shift=(a&3)*8;*bits=8;break;
    }
    case 0x14: {
        unsigned col=(y&15)>>2,sx=(col&1)?((x&31)^4):(x&31);
        unsigned column=kCol4First[y&3][sx]+col*128;
        a=((bp+((y>>2)&~0x1fu)*(bw>>1)+((x>>2)&~0x1fu)+kBlock16[(y>>4)&7][(x>>5)&3])<<9)+column;
        word=a>>3;*shift=(a&7)*4;*bits=4;break;
    }
    case 0:case 1:case 0x1b:case 0x24:case 0x2c:
        word=((bp+(y&~0x1fu)*bw+((x>>1)&~0x1fu)+kBlock32[(y>>3)&3][(x>>3)&7])<<6)+kCol32[y&7][x&7];
        *shift=psm==0x1b || psm==0x24?24:psm==0x2c?28:0;
        *bits=psm==0?32:psm==1?24:psm==0x1b?8:4;break;
    default:return DR_GS_UNSUPPORTED;
    }
    *word_byte=(word&0xfffff)*4u;return DR_GS_OK;
}
int dr_gs_raw_read(const void *vram,size_t size,uint32_t bp,uint32_t bw,uint32_t psm,
                   uint32_t x,uint32_t y,uint32_t *value)
{
    size_t off;unsigned shift,bits;
    if(!vram || !value || size<0x400000) return DR_GS_CAPACITY;
    int error=dr_gs_address(bp,bw,psm,x,y,&off,&shift,&bits);if(error) return error;
    const unsigned char *p=(const unsigned char *)vram+off;
    uint32_t word=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
    *value=(word>>shift)&(bits==32?0xffffffffu:((1u<<bits)-1));return DR_GS_OK;
}
static uint32_t expand(uint32_t c,uint32_t psm,const DrGsTexture *t)
{
    if(psm==0) return c;
    if(psm==1) {
        c&=0xffffff;
        return c|((t->aem && !c?0:t->ta0)<<24);
    }
    uint32_t a=(c&0x8000)?t->ta1:(t->aem && !(c&0x7fff)?0:t->ta0);
    return ((c&31)<<3)|(((c>>5)&31)<<11)|(((c>>10)&31)<<19)|(a<<24);
}
int dr_gs_texture_decode(const void *vram,size_t size,const DrGsTexture *t,void *rgba,size_t capacity)
{
    if(!t || !rgba || !vram || size<0x400000 || !t->width || !t->height || t->width>1024 || t->height>1024 ||
       t->tbp>0x3fff || t->tbw>63 || t->cbp>0x3fff || t->ta0>255 || t->ta1>255 || t->aem>1) return DR_GS_INVALID;
    size_t off;unsigned shift,bits;
    int error=dr_gs_address(t->tbp,t->tbw,t->psm,0,0,&off,&shift,&bits);if(error) return error;
    if(bits<=8 && (t->csm!=0 || (t->cpsm!=0 && t->cpsm!=2 && t->cpsm!=0x0a))) return DR_GS_UNSUPPORTED;
    size_t bytes=(size_t)t->width*t->height*4;if(bytes>capacity) return DR_GS_CAPACITY;
    unsigned char *out=rgba;
    for(uint32_t y=0;y<t->height;y++) for(uint32_t x=0;x<t->width;x++) {
        uint32_t c=0;dr_gs_raw_read(vram,size,t->tbp,t->tbw,t->psm,x,y,&c);
        if(bits<=8) {
            if(bits==8) c=(c&0xe7)|((c&8)<<1)|((c&0x10)>>1);
            dr_gs_raw_read(vram,size,t->cbp,1,t->cpsm,c&(bits==8?15:7),c>>(bits==8?4:3),&c);
            c=expand(c,t->cpsm,t);
        } else c=expand(c,t->psm,t);
        size_t i=((size_t)y*t->width+x)*4;
        out[i]=(unsigned char)c;out[i+1]=(unsigned char)(c>>8);out[i+2]=(unsigned char)(c>>16);out[i+3]=(unsigned char)(c>>24);
    }
    return DR_GS_OK;
}
int dr_gs_texture_selftest(void)
{
    size_t offset=0;unsigned shift=0,bits=0;
    if(dr_gs_address(0,1,0,1,0,&offset,&shift,&bits) || offset!=4 || shift || bits!=32) return 0;
    if(dr_gs_address(0,2,0x13,1,0,&offset,&shift,&bits) || offset!=4 || shift || bits!=8) return 0;
    if(dr_gs_address(0,2,0x14,1,0,&offset,&shift,&bits) || offset!=4 || shift || bits!=4) return 0;
    return dr_gs_address(0,1,0x3f,0,0,&offset,&shift,&bits)==DR_GS_UNSUPPORTED;
}
