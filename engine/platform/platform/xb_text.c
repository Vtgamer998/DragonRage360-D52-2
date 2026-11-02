/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Font metadata layout/registration contract: pinned port/src/gs/menu_text.c.
 * Translation contract: pinned port/src/gs/ui_text_translate.c. No SDL/fonts.
 */
#include "xb_text.h"
#include <stdlib.h>
#include <string.h>
typedef struct {XbFontGlyph *glyphs;size_t count;int height;} Sheet;
static Sheet sheets[7];
typedef struct {uint16_t *original,*translated;size_t length;} Line;
static Line *lines;static size_t count;static int error;
int XbText_LastError(void){return error;}
static size_t length(const uint16_t *p) {
    if(!p)return 4097;
    for(size_t i=0;i<=4096;i++)if(!p[i])return i;
    return 4097;
}
static void free_lines(Line *p,size_t n){for(size_t i=0;i<n;i++){free(p[i].original);free(p[i].translated);}free(p);}
int XbText_SetTranslations(const XbTranslation *input,size_t n) {
    error=0;if(n>256 || (!input && n))return error=-1;
    Line *copy=calloc(n?n:1,sizeof(*copy));if(!copy)return error=-2;
    for(size_t i=0;i<n;i++) {
        size_t a=length(input[i].original),b=length(input[i].translated);
        if(a>4096 || b>4096 || !a){free_lines(copy,n);return error=-1;}
        for(size_t j=0;j<i;j++)if(copy[j].length==a && !memcmp(copy[j].original,input[i].original,(a+1)*2)) {
            free_lines(copy,n);return error=-1;
        }
        copy[i].original=malloc((a+1)*2);copy[i].translated=malloc((b+1)*2);
        if(!copy[i].original || !copy[i].translated){free_lines(copy,n);return error=-2;}
        memcpy(copy[i].original,input[i].original,(a+1)*2);memcpy(copy[i].translated,input[i].translated,(b+1)*2);copy[i].length=a;
    }
    free_lines(lines,count);lines=copy;count=n;return 0;
}
const uint16_t *UiText_TranslateString(const uint16_t *original) {
    if(!count || !original)return original;
    size_t n=length(original);if(n>4096)return original;
    for(size_t i=0;i<count;i++)if(lines[i].length==n && !memcmp(lines[i].original,original,(n+1)*2))return lines[i].translated;
    return original;
}
void MenuText_RegisterGlyphs(int tbp,int cbp,const void *data,int n,int height) {
    error=0;int slot=cbp>=0x2a00?(cbp-0x2a00)/0x208:-1;
    if(slot<0 || slot>=7 || cbp!=0x2a00+slot*0x208 || tbp!=cbp+8 || !data || n<1 || n>65536 || height<1 || height>64){error=-1;return;}
    size_t bytes=(size_t)n*sizeof(XbFontGlyph);XbFontGlyph *copy=malloc(bytes);
    if(!copy){error=-2;return;}memcpy(copy,data,bytes);
    free(sheets[slot].glyphs);sheets[slot]=(Sheet){copy,(size_t)n,height};
}
int XbText_FindGlyph(int cbp,uint16_t code,XbFontGlyph *out,int *height) {
    if(!out || !height || cbp<0x2a00)return -1;
    unsigned slot=(unsigned)(cbp-0x2a00)/0x208;
    if(slot>=7 || cbp!=0x2a00+(int)slot*0x208)return -1;
    for(size_t i=0;i<sheets[slot].count;i++)if(sheets[slot].glyphs[i].code==code) {
        *out=sheets[slot].glyphs[i];*height=sheets[slot].height;return 0;
    }
    return -1;
}
void XbText_Close(void) {
    for(unsigned i=0;i<7;i++){free(sheets[i].glyphs);sheets[i]=(Sheet){0};}
    free_lines(lines,count);lines=NULL;count=0;error=0;
}
