/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Native ASCII bitmap fallback for the PC TBO text contract. No TrueType/SDL.
 * Public-domain font data attributed in xb_ascii_font.h.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "xb_ascii_font.h"
enum {BASE=0x100,SLOTS=128};
typedef struct {char text[128];uint32_t version;unsigned length;int kind;} Line;
static Line lines[SLOTS];static uint32_t serial;static unsigned replace;
int PortTbo_TextId(const char *text,int kind) {
    if(!text || !*text || kind<0 || kind>2)return -1;
    size_t n=0;while(n<128 && text[n]){if((unsigned char)text[n]<32 || (unsigned char)text[n]>126)return -1;n++;}
    if(n>=128)return -1;
    for(unsigned i=0;i<SLOTS;i++)if(lines[i].version && lines[i].kind==kind && !strcmp(lines[i].text,text))return BASE+(int)i;
    unsigned i=replace++%SLOTS;if(!++serial)++serial;
    memcpy(lines[i].text,text,n+1);lines[i].length=(unsigned)n;lines[i].kind=kind;lines[i].version=serial;
    return BASE+(int)i;
}
static Line *line(int id){return id>=BASE && id<BASE+SLOTS && lines[id-BASE].version?lines+id-BASE:NULL;}
int PortTbo_TextInfo(int id,int what) {
    Line *l=line(id);if(!l)return 0;
    int value=what==0?(int)l->length*10:what==1?(int)l->length*10+4:what==2?24:what==3?18:what==4?2:0;
    return value*1000/13; /* measured H capital is 13 pixels */
}
uint32_t TboText_Version(uint32_t id){Line *l=line((int)id);return l?l->version:0;}
uint32_t *TboText_Pixels(uint32_t id,uint32_t *version,uint32_t *width,uint32_t *height) {
    if(!version || !width || !height)return NULL;
    *version=*width=*height=0;Line *l=line((int)id);if(!l)return NULL;
    unsigned w=l->length*10+4,h=l->kind==2?24:48;
    uint8_t *pixels=calloc((size_t)w*h,4);if(!pixels)return NULL;
    for(unsigned i=0;i<l->length;i++)for(unsigned y=0;y<20;y++)for(unsigned x=0;x<10;x++)
        if(native_ascii[(unsigned char)l->text[i]-32][y]&(0x8000u>>x)) {
            unsigned px=i*10+x+2,py=y+2;uint8_t *p=pixels+(py*w+px)*4;
            p[0]=p[1]=p[2]=255;p[3]=0x80;
            if(l->kind!=2)for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++) {
                uint8_t *o=pixels+(((py+24+dy)*w+px+dx)*4);o[0]=o[1]=o[2]=255;o[3]=0x80;
            }
        }
    *width=w;*height=h;*version=l->version;
    return (uint32_t *)(void *)pixels; /* canonical RGBA byte order on PPC too */
}
