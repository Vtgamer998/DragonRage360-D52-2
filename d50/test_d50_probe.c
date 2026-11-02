/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Host test for D50 milestones 1 and 2 and D51 milestone 3. Uses the GPU
 * call/lifetime mocks from test_xenos.c; no rasterization, no PowerPC, no
 * console claim. The D51 readback mock returns the colour the test programs,
 * so it exercises coordinate and comparison logic, not GPU pixels. */
#define main previous_lifetime_tests
#include "test_xenos.c"
#undef main
#include "gfx_probe.h"
#include "gfx_present.h"
#include "xsdk_compat.h"
#include <assert.h>
#include <string.h>

static unsigned presents,resolves;
static uint32_t present_address=0x40000000;
static int present_failure;
static uint32_t mock_triangle_row,mock_triangle_color=0xFFFFFF00,mock_background_color=0xFF101030;

NTSTATUS xsdk_present_begin(uint32_t *address,uint32_t timeout_ms)
{
    assert(timeout_ms==5000);
    if(present_failure)return (NTSTATUS)0xC0000001u;
    *address=present_address;return STATUS_SUCCESS;
}
void xsdk_present_end(void){presents++;}
volatile uint32_t *xsdk_present_pixels(uint32_t address)
{
    assert(address==present_address);static uint32_t pixels[1280];return pixels;
}
void xsdk_gpu_resolve(uint32_t address,uint32_t width,uint32_t height,uint32_t clear)
{
    assert(address==present_address && width && height);(void)clear;resolves++;
}
void xsdk_texture_download_rows(const XsdkTexture *texture,uint32_t level,
                                uint32_t first_row,uint32_t row_count,void *data,
                                uint32_t row_bytes)
{
    (void)texture;(void)level;assert(row_count==1);
    uint32_t color=first_row==mock_triangle_row?mock_triangle_color:mock_background_color;
    uint32_t *pixels=data;
    for(uint32_t i=0;i<row_bytes/4;i++)pixels[i]=__builtin_bswap32(color);
}
int main(void)
{
    previous_lifetime_tests();
    /* Milestone 1: file, memory, time and log. */
    D50Buffer missing={0};
    assert(D50_ReadFile("/nonexistent/d50-probe-input",&missing)==-1);
    assert(missing.data==NULL && missing.size==0);
    assert(D50_ReadFile(NULL,&missing)==-1);
    void *block=D50_Alloc(64);
    assert(block && ((unsigned char *)block)[0]==0 && ((unsigned char *)block)[63]==0);
    D50_Release(block);
    assert(D50_Alloc(0)==NULL);
    uint64_t t0=D50_NowUs(),t1=D50_NowUs();
    assert(t0<=t1);
    /* Milestone 2: background plus one triangle through dr_xenos, validated by
     * the same preflight the title uses. */
    unsigned before=draws;
    assert(D50_GfxProbeRun(640,480)==0);
    assert(draws==before+2); /* background quad + triangle */
    assert(last_primitive==XSDK_PRIMITIVE_TRIANGLE_LIST);
    assert(vertex_bytes==3*12*4);
    /* Invalid sizes are refused before touching the GPU. */
    before=draws;
    assert(D50_GfxProbeRun(100,480)!=0 && draws==before);
    assert(D50_GfxProbeRun(640,0)!=0 && draws==before);
    /* 1280x720 is the supported maximum. */
    before=draws;
    assert(D50_GfxProbeRun(1280,720)==0 && draws==before+2);
    /* D51: real present path, 3 frames at 640x480 into a 1280x720 front buffer. */
    D50GfxExpectation expected=D50_GfxExpectation(640,480);
    mock_triangle_row=expected.center_y;
    before=draws;unsigned p0=presents,r0=resolves;
    assert(D50_GfxPresentRun(640,480,1280,720,3)==0);
    assert(presents==p0+3 && resolves==r0+3 && draws==before+6);
    /* 1280x720 logical size, the second D51 validation step. */
    expected=D50_GfxExpectation(1280,720);mock_triangle_row=expected.center_y;
    before=draws;p0=presents;
    assert(D50_GfxPresentRun(1280,720,1280,720,2)==0);
    assert(presents==p0+2 && draws==before+4);
    /* A visual readback mismatch must fail the probe. */
    mock_triangle_color=0xFF00FF00;
    assert(D50_GfxPresentRun(640,480,1280,720,1)!=0);
    mock_triangle_color=0xFFFFFF00;
    /* present_begin failure must not draw. */
    present_failure=1;before=draws;
    assert(D50_GfxPresentRun(640,480,1280,720,1)!=0 && draws==before);
    present_failure=0;
    /* Invalid logical or front-buffer sizes are refused before the GPU. */
    before=draws;
    assert(D50_GfxPresentRun(100,480,1280,720,1)!=0 && draws==before);
    assert(D50_GfxPresentRun(640,480,0,720,1)!=0 && draws==before);
    D50_Log("PASS: D50 milestones 1-2 and D51 present/readback path, host mocks only");
    return 0;
}
