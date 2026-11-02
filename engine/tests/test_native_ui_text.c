/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
int PortTbo_TextId(const char *,int);int PortTbo_TextInfo(int,int);
uint32_t *TboText_Pixels(uint32_t,uint32_t *,uint32_t *,uint32_t *);
uint32_t TboText_Version(uint32_t);
int XbA11y_Observe(int,const uint16_t *,int);int XbA11y_Dispatch(void);
void XbA11y_SetConsumer(void (*)(const uint16_t *,int,void *),void *);
static int spoken;
static void consume(const uint16_t *s,int flags,void *ctx){assert(ctx==&spoken && s[0]=='A' && flags==1);assert(XbA11y_Dispatch()==-1);spoken++;}
int main(void) {
    assert(PortTbo_TextId(NULL,0)<0 && PortTbo_TextId("",0)<0 && PortTbo_TextId("\xff",0)<0);
    int id=PortTbo_TextId("H",0);assert(id>=256 && PortTbo_TextId("H",0)==id);
    assert(PortTbo_TextInfo(id,0)==10000/13 && PortTbo_TextInfo(id,2)==24000/13);
    uint32_t v,w,h;uint8_t *p=(void *)TboText_Pixels((uint32_t)id,&v,&w,&h);
    assert(p && v==TboText_Version((uint32_t)id) && w==14 && h==48);
    unsigned top=0,bottom=0;
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)if(p[(y*w+x)*4+3]){
        assert(p[(y*w+x)*4]==255 && p[(y*w+x)*4+3]==128);
        if(y<24)top++;else bottom++;
    }
    assert(top>20 && bottom>top);free(p);
    id=PortTbo_TextId("TEST",2);p=(void *)TboText_Pixels((uint32_t)id,&v,&w,&h);assert(p && h==24);free(p);
    assert(!TboText_Pixels(0,&v,&w,&h) && !v && !w && !h);
    uint16_t a[]={'A',10,'B',0};assert(XbA11y_Observe(0,a,1)==1 && XbA11y_Observe(0,a,1)==0);
    a[0]='Z';assert(!XbA11y_Dispatch());XbA11y_SetConsumer(consume,&spoken);
    assert(XbA11y_Dispatch()==1 && spoken==1 && XbA11y_Dispatch()==0);
    puts("PASS: real bitmap text pixels/outline, metrics, stable cache IDs, bounds, canonical RGBA and copied accessibility events/reentry");return 0;
}
