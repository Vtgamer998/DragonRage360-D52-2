/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Calls the real GsBackend object with engine globals and platform/GPU mocks.
 * Does not link gs_draw.c or execute Xenos. */
#define main previous_native_tests
#include "test_xenos.c"
#undef main
#include "dr_gs_backend.h"
#include "dr_gs_replay.h"
#include "dr_targets.h"
#include "xsdk/present.h"
#include <stdlib.h>
#ifndef DR_REAL_CAPTURE
unsigned gGsFbUploads;
uint32_t Gs_VramRead(uint32_t bp,uint32_t bw,uint32_t psm,uint32_t x,uint32_t y){(void)bp;(void)bw;(void)psm;return 0x80000000|x|(y<<8);}
int gsScale4=4,gsTargetW=1024,gsTargetH=1024,gsTargetCount,gGsMainFbp,gsAnchor;
unsigned gsSkipped,gsNative;
float *gsVuVerts;uint32_t *gsVuIdx;Vu0Uniform *gsVuUni;float gsZMax=16777215.0f;
Vtx *gsVerts;GsDraw *gsDraws;GsTarget gsTargets[MAX_TARGETS];
uint32_t gsDrawCount,gsVertCount,gsVuVertCount,gsVuIdxCount,gsVuUniCount;
#endif
static unsigned gpu_opens,gpu_closes,present_opens,present_closes,presents,errors;
NTSTATUS xsdk_gpu_open(void) {gpu_opens++;return STATUS_SUCCESS;}
NTSTATUS xsdk_gpu_close(void) {gpu_closes++;return STATUS_SUCCESS;}
void xsdk_log_write(const char *format,...) {assert(format);errors++;}
NTSTATUS xsdk_present_open(XsdkDisplayMode mode,uint32_t count,uint32_t interval,uint32_t *w,uint32_t *h)
{assert(mode==XSDK_DISPLAY_720P && count==2 && interval==1);*w=1280;*h=720;present_opens++;return STATUS_SUCCESS;}
NTSTATUS xsdk_present_begin(uint32_t *address,uint32_t timeout) {assert(timeout==1000);*address=0x9000000;return STATUS_SUCCESS;}
void xsdk_gpu_resolve(uint32_t address,uint32_t width,uint32_t height,uint32_t clear)
{assert(!tile_recording && target_ready && address==0x9000000 && width==1280 && height==720 && !clear);target_ready=0;}
void xsdk_present_end(void) {presents++;}
void xsdk_present_close(void) {present_closes++;}
static int download_pattern;
void xsdk_texture_download_rows(const XsdkTexture *t,uint32_t level,uint32_t first,uint32_t count,void *data,uint32_t row_bytes)
{assert(t && !level && first<448 && count==1 && row_bytes==4096);if(!download_pattern) memset(data,0x5a,row_bytes);
 else for(unsigned x=0;x<row_bytes/4;x++) {
     uint8_t *p=(uint8_t *)data+4*x;
     p[0]=(uint8_t)(x+(t->address>>12));p[1]=(uint8_t)first;p[2]=(uint8_t)(first>>8);p[3]=0x80;
 }}
static void *pixels(unsigned bytes) {void *p=malloc(bytes);assert(p);memset(p,128,bytes);return p;}
#ifndef DR_BACKEND_TEST_MAIN
#define DR_BACKEND_TEST_MAIN main
#endif
int DR_BACKEND_TEST_MAIN(void)
{
    previous_native_tests();
    assert(sXenosBackend.init()==1 && !strcmp(sXenosBackend.name,"xenos"));
    assert(sXenosBackend.whiteTex()!=0 && sXenosBackend.texSlotsLeft()==255);
    GsTex texture=sXenosBackend.texCreate(8,8,GS_TEXFMT_RGBA8,2);assert(texture);
    assert(sXenosBackend.texUpload(texture,0,8,8,GS_TEXFMT_RGBA8,pixels(256),256)==1);
    assert(sXenosBackend.texUpload(texture,1,4,4,GS_TEXFMT_RGBA8,pixels(64),64)==1);
    sXenosBackend.targetEnsure(0);
    assert(sXenosBackend.targetColor(0) && sXenosBackend.targetAux(0) && sXenosBackend.targetDepth(0));
    uint32_t key=(15u<<16)|(1u<<10);int pipe=sXenosBackend.pipeGet(key);
    assert(pipe==0 && sXenosBackend.pipeGet(key)==pipe);
    Vtx vertices[3]={{0,0,.5f,128,128,128,128,0,0,1},{32,0,.5f,128,128,128,128,1,0,1},
        {0,32,.5f,128,128,128,128,0,1,1}};
    GsDraw commands[3]={{.first=0,.count=3,.target=0,.tex=texture,.pipeline=pipe,
        .mode={1,0,1,0},.misc={0,0,1,0},.scissor={0,0,1024,1024}},
        {.native=5,.target=0},{.first=0,.count=3,.target=0,.tex=texture,.pipeline=pipe,
        .mode={1,0,1,0},.misc={0,2,0,0},.scissor={0,0,1024,1024}}};
    gsVerts=vertices;gsDraws=commands;gsVertCount=3;gsDrawCount=3;gsTargetCount=1;
    gsTargets[0].fbp=0;gGsMainFbp=0;model_enabled=1;
    next_color=0x11223344;next_aux=128;next_stencil=0x6d;
    sXenosBackend.frameEnd();
    uint32_t failed;assert(dr_gs_backend_error(&failed)==0 && failed==UINT32_MAX);
    assert(!gsDrawCount && !gsVertCount && presents==1 && dr_targets_ready(0));
    uint8_t *download=malloc(512*448*4);assert(download);
    assert(sXenosBackend.targetRead(0,download)==1 && download[0]==0x5a && download[512*448*4-1]==0x5a);free(download);
    sXenosBackend.targetEnsure(1);sXenosBackend.targetCopy(0,1);
    assert(dr_gs_backend_error(NULL)==0 && dr_targets_ready(1));
    assert(last_blend.write_mask==XSDK_WRITE_RED); // copy the source raw alpha into the destination MRT
    assert(bound_textures[2]->address==dr_targets_texture(0,1)->address);
    assert(images[dr_targets_texture(1,1)->address/65536].color==128);
    sXenosBackend.texDestroy(texture);
    GsTex replacement=sXenosBackend.texCreate(8,8,GS_TEXFMT_RGBA8,1);
    assert(replacement && replacement!=texture);
    assert(sXenosBackend.texUpload(texture,0,8,8,GS_TEXFMT_RGBA8,pixels(256),256)==0);
    assert(dr_gs_backend_error(NULL)==DR_XENOS_INVALID && errors);
    assert(dr_gs_backend_close()==0);
    assert(sXenosBackend.init()==1);
    sXenosBackend.targetEnsure(0);pipe=sXenosBackend.pipeGet(key);
    commands[0].vu=4;commands[0].pipeline=pipe;commands[0].tex=sXenosBackend.whiteTex();
    gsVertCount=3;gsDrawCount=1;unsigned before=draws;
    sXenosBackend.frameEnd();
    assert(dr_gs_backend_error(&failed)==DR_GS_UNSUPPORTED && failed==0 && draws==before);
    assert(dr_gs_backend_close()==0);
    assert(gpu_opens==gpu_closes && present_opens==present_closes);
    model_enabled=0;
    for(unsigned i=0;i<4096;i++) assert(!images[i].live);
    puts("PASS: real sXenosBackend callbacks, textures/mips/ownership/generation, DATE marker/FBA replay, present/read/copy, unknown VU failure; MOCK GPU and capture globals only");
    return 0;
}
