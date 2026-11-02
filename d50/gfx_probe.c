/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "gfx_probe.h"
#include "xsdk_compat.h"
#include "dr_xenos.h"
/* The readback is A8R8G8B8 as the title's other pixel probes read it. */
#define D50_BACKGROUND 0xFF101030u
#define D50_TRIANGLE   0xFFFFFF00u
static void vertex(DrGsVertex *v,float x,float y,unsigned char r,unsigned char g,
                   unsigned char b,unsigned char a,float s,float t)
{
    v->x=x;v->y=y;v->z=0.5f;v->r=r;v->g=g;v->b=b;v->a=a;v->s=s;v->t=t;v->q=1.0f;
}
static void fill_quad(DrGsVertex v[6],float x0,float y0,float x1,float y1,
                      unsigned char r,unsigned char g,unsigned char b,unsigned char a)
{
    vertex(&v[0],x0,y0,r,g,b,a,0.0f,0.0f);vertex(&v[1],x1,y0,r,g,b,a,1.0f,0.0f);
    vertex(&v[2],x0,y1,r,g,b,a,0.0f,1.0f);vertex(&v[3],x0,y1,r,g,b,a,0.0f,1.0f);
    vertex(&v[4],x1,y0,r,g,b,a,1.0f,0.0f);vertex(&v[5],x1,y1,r,g,b,a,1.0f,1.0f);
}
D50GfxExpectation D50_GfxExpectation(uint32_t width,uint32_t height)
{
    D50GfxExpectation e;
    e.background_argb=D50_BACKGROUND;e.triangle_argb=D50_TRIANGLE;
    e.center_x=width/2;e.center_y=height*7/12;
    e.corner_x=width/8;e.corner_y=height/8;
    return e;
}
int D50_GfxSceneDraw(uint32_t width,uint32_t height,uint32_t texture)
{
    if(width<16 || width>1280 || height<16 || height>720 || (width&31))
        return DR_XENOS_INVALID;
    DrGsVertex background[6],triangle[3];
    fill_quad(background,0.0f,0.0f,(float)width,(float)height,16,16,48,255);
    /* Logical pixel coordinates: a triangle at the centre, safely away from
     * the edges so the centroid and corner probes cannot land on an edge. */
    vertex(&triangle[0],(float)width*0.5f,(float)height*0.25f,255,255,0,255,0.0f,0.0f);
    vertex(&triangle[1],(float)width*0.75f,(float)height*0.75f,255,255,0,255,1.0f,0.0f);
    vertex(&triangle[2],(float)width*0.25f,(float)height*0.75f,255,255,0,255,0.0f,1.0f);
    DrXenosDraw draw={0};
    draw.logical_width=width;draw.logical_height=height;
    draw.topology=0;draw.blend=0;draw.depth=1;draw.depth_write=1;
    draw.scissor[0]=0;draw.scissor[1]=0;draw.scissor[2]=(int)width;draw.scissor[3]=(int)height;
    int error=dr_xenos_draw(background,6,texture,&draw);
    if(!error)error=dr_xenos_draw(triangle,3,texture,&draw);
    return error;
}
int D50_GfxProbeRun(uint32_t width,uint32_t height)
{
    if(width<16 || width>1280 || height<16 || height>720 || (width&31)){
        D50_Log("D50 probe: invalid size %ux%u",width,height);
        return DR_XENOS_INVALID;
    }
    uint64_t started=D50_NowUs();
    int error=dr_xenos_open();
    uint32_t texture=0;
    if(!error){
        const unsigned char white[4]={255,255,255,255};
        error=dr_xenos_texture_create(1,1,white,&texture);
    }
    if(!error)error=dr_xenos_begin(width,height);
    if(!error){
        int draw_error=D50_GfxSceneDraw(width,height,texture);
        int end_error=dr_xenos_end();
        error=draw_error?draw_error:end_error;
    }
    if(!error)error=dr_xenos_sync();
    if(texture)dr_xenos_texture_destroy(texture);
    int closed=dr_xenos_close();
    if(!error)error=closed;
    uint64_t elapsed=D50_NowUs()-started;
    if(error)D50_Log("FAIL: D50 gfx probe background+triangle size=%ux%u error=%d elapsed_us=%llu",width,height,error,(unsigned long long)elapsed);
    else D50_Log("PASS: D50 gfx probe background+triangle size=%ux%u elapsed_us=%llu",width,height,(unsigned long long)elapsed);
    return error;
}
