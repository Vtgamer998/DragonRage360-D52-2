/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Synthetic PSMT8/CSM1 fixture. Diagnostic only, no fighter/game asset.
 */
#include "dr_graphics_demo.h"
#include "dr_gs_texture.h"
#include "dr_xenos.h"
#include <stdlib.h>
static uint32_t checker;
static int put_pixel(unsigned char *vram,unsigned bp,unsigned bw,unsigned psm,unsigned x,unsigned y,uint32_t value)
{
    size_t off;unsigned shift,bits;
    if(dr_gs_address(bp,bw,psm,x,y,&off,&shift,&bits)) return 0;
    uint32_t word=(uint32_t)vram[off]|(uint32_t)vram[off+1]<<8|(uint32_t)vram[off+2]<<16|(uint32_t)vram[off+3]<<24;
    uint32_t mask=bits==32?0xffffffffu:(1u<<bits)-1;
    word=(word&~(mask<<shift))|((value&mask)<<shift);
    for(unsigned i=0;i<4;i++) vram[off+i]=(unsigned char)(word>>(i*8));
    return 1;
}
int dr_graphics_demo_open(void)
{
    int error=dr_xenos_open();if(error) return error;
    unsigned char *vram=calloc(1,0x400000),*rgba=malloc(64*64*4);
    if(!vram || !rgba) {free(vram);free(rgba);dr_xenos_close();return DR_XENOS_FULL;}
    for(unsigned y=0;y<64;y++) for(unsigned x=0;x<64;x++)
        put_pixel(vram,0,2,0x13,x,y,((x/8+y/8)&1)?16:8);
    /* CSM1 exchanges index bits 3 and 4; palette bytes are GS RGBA little-endian. */
    put_pixel(vram,512,1,0,0,1,0x801f8aff); /* logical palette index 8 -> physical 16 */
    put_pixel(vram,512,1,0,8,0,0x80ffd36f); /* logical palette index 16 -> physical 8 */
    const DrGsTexture texture={.width=64,.height=64,.tbp=0,.tbw=2,.psm=0x13,.cbp=512,.cpsm=0,.ta1=128};
    error=dr_gs_texture_decode(vram,0x400000,&texture,rgba,64*64*4);
    if(!error) error=dr_xenos_texture_create(64,64,rgba,&checker);
    if(!error) error=dr_xenos_target_create_gs(0,1280,720);
    free(vram);free(rgba);
    if(error) dr_xenos_close();
    return error;
}
int dr_graphics_demo_draw(uint32_t width,uint32_t height)
{
    if(!width || !height || width>1280 || height>720) return DR_XENOS_INVALID;
    int error=dr_xenos_begin_target(0);if(error) return error;
    const DrGsVertex background[]={
        {0,0,.1f,128,128,128,128,0,0,1},{1280,0,.1f,128,128,128,128,4,0,1},{0,720,.1f,128,128,128,128,0,4,1},
        {0,720,.1f,128,128,128,128,0,4,1},{1280,0,.1f,128,128,128,128,4,0,1},{1280,720,.1f,128,128,128,128,4,4,1}
    };
    const DrGsVertex triangle[]={
        {460,220,.5f,128,128,128,64,0,0,1},{1000,560,.5f,128,128,128,64,1,1,1},{400,560,.5f,128,128,128,64,0,1,1}
    };
    const DrGsVertex glow[]={
        {900,260,.6f,128,128,128,64,0,0,1},{1160,500,.6f,128,128,128,64,1,1,1},{790,500,.6f,128,128,128,64,0,1,1}
    };
    DrXenosDraw draw={.logical_width=1280,.logical_height=720,.depth=1,.scissor={0,0,1280,720}};
    error=dr_xenos_draw(background,6,checker,&draw);
    if(!error) error=dr_xenos_snapshot(0);
    draw.date=2;draw.fba=1;
    draw.blend=1;if(!error) error=dr_xenos_draw(triangle,3,checker,&draw);
    draw.fba=0;
    draw.blend=2;if(!error) error=dr_xenos_draw(glow,3,checker,&draw);
    int end=dr_xenos_end();if(!error) error=end;if(error) return error;
    error=dr_xenos_begin(width,height);if(error) return error;
    DrGsVertex quad[6];
    for(unsigned i=0;i<6;i++) {
        quad[i]=background[i];quad[i].x*=width/1280.0f;quad[i].y*=height/720.0f;
        quad[i].s*=.25f;quad[i].t*=.25f;
    }
    draw=(DrXenosDraw){.logical_width=width,.logical_height=height,.depth=1,.gs_material=1,
        .textured=2,.tcc=1,.texture_rect={0,0,1,1},.scissor={0,0,(int)width,(int)height}};
    error=dr_xenos_draw(quad,6,dr_xenos_target_texture(0),&draw);
    end=dr_xenos_end();return error?error:end;
}
int dr_graphics_demo_close(void) {checker=0;return dr_xenos_close();}
