/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "gfx_present.h"
#include "gfx_probe.h"
#include "xsdk_compat.h"
#include "dr_xenos.h"
#include <xsdk/gpu.h>
#include <xsdk/present.h>
#include <xsdk/texture.h>

#define D50_PRESENT_BEGIN_MS 5000u
#define D50_PRESENT_WAIT_MS  1000u
#define D50_PRESENT_FAIL     (-1000)

/* Reads one A8R8G8B8 pixel from the frame at `address`. The resolve writes the
 * whole front buffer, so the texture is display-sized even when the probe drew
 * a smaller logical area. */
static int read_pixel(uint32_t address,uint32_t display_width,uint32_t display_height,
                      uint32_t x,uint32_t y,uint32_t *out)
{
    if(x>=display_width || y>=display_height)return -1;
    uint32_t row_bytes=display_width*4u;
    static uint32_t row[1280];
    if(row_bytes>sizeof row)return -1;
    XsdkTexture frame;
    NTSTATUS status=xsdk_texture_init(&frame,XSDK_FORMAT_A8R8G8B8,
                                      display_width,display_height,1,true);
    if(!NT_SUCCESS(status))return -1;
    xsdk_texture_attach(&frame,xsdk_present_pixels(address));
    xsdk_texture_download_rows(&frame,0,y,1,row,row_bytes);
    *out=__builtin_bswap32(row[x]);
    return 0;
}
static int color_matches(uint32_t got,uint32_t expected,unsigned tolerance)
{
    for(unsigned channel=0;channel<3;channel++){
        unsigned a=(got>>(channel*8))&255u,b=(expected>>(channel*8))&255u;
        unsigned difference=a>b?a-b:b-a;
        if(difference>tolerance)return 0;
    }
    return 1;
}
int D50_GfxPresentRun(uint32_t logical_width,uint32_t logical_height,
                      uint32_t display_width,uint32_t display_height,
                      uint32_t frames)
{
    if(logical_width<16 || logical_width>1280 || logical_height<16 ||
       logical_height>720 || (logical_width&31)){
        D50_Log("FAIL: D51 gfx probe INIT invalid logical size %ux%u",
                logical_width,logical_height);
        return DR_XENOS_INVALID;
    }
    if(!display_width || display_width>1280 || !display_height ||
       display_height>720 || (display_width&31)){
        D50_Log("FAIL: D51 gfx probe INIT invalid front buffer %ux%u",
                display_width,display_height);
        return DR_XENOS_INVALID;
    }
    if(!frames)frames=1;
    uint64_t started=D50_NowUs();
    int error=dr_xenos_open();
    uint32_t texture=0;
    if(!error){
        const unsigned char white[4]={255,255,255,255};
        error=dr_xenos_texture_create(1,1,white,&texture);
    }
    if(error){
        if(texture)dr_xenos_texture_destroy(texture);
        dr_xenos_close();
        D50_Log("FAIL: D51 gfx probe INIT dr_xenos error=%d",error);
        return error;
    }
    D50_Log("PASS: D51 gfx probe INIT logical=%ux%u front_buffer=%ux%u frames=%u; "
            "dr_xenos open, white 1x1 texture created",
            logical_width,logical_height,display_width,display_height,frames);

    uint32_t address=0,presented=0;
    for(uint32_t frame=0;frame<frames && !error;frame++){
        address=0;
        NTSTATUS status=xsdk_present_begin(&address,D50_PRESENT_BEGIN_MS);
        if(!NT_SUCCESS(status)){
            D50_Log("FAIL: D51 gfx probe PRESENT present_begin frame=%u status=0x%08X",
                    frame,(unsigned)status);
            error=D50_PRESENT_FAIL;
            break;
        }
        int begin_error=dr_xenos_begin(logical_width,logical_height);
        if(begin_error){
            error=begin_error;
            D50_Log("FAIL: D51 gfx probe PRESENT dr_xenos_begin frame=%u error=%d",
                    frame,error);
        }else{
            int draw_error=D50_GfxSceneDraw(logical_width,logical_height,texture);
            int end_error=dr_xenos_end();
            error=draw_error?draw_error:end_error;
            if(error)D50_Log("FAIL: D51 gfx probe PRESENT scene frame=%u error=%d",
                             frame,error);
        }
        /* The buffer from present_begin must always be resolved and returned,
         * even when the draw failed, or the display keeps waiting for it. */
        xsdk_gpu_resolve(address,display_width,display_height,0xFF000000);
        xsdk_present_end();
        status=xsdk_gpu_wait(xsdk_gpu_fence(),D50_PRESENT_WAIT_MS);
        if(!NT_SUCCESS(status)){
            if(!error){
                D50_Log("FAIL: D51 gfx probe PRESENT gpu_wait frame=%u status=0x%08X",
                        frame,(unsigned)status);
                error=D50_PRESENT_FAIL;
            }
        }
        presented++;
        if(frame==0)
            D50_Log("D51 gfx probe: first frame presented address=0x%08X",address);
    }
    uint64_t elapsed=D50_NowUs()-started;
    if(!error)
        D50_Log("PASS: D51 gfx probe PRESENT frames=%u/%u elapsed_us=%llu; "
                "every present_end returned and every GPU wait passed",
                presented,frames,(unsigned long long)elapsed);
    else
        D50_Log("FAIL: D51 gfx probe PRESENT frames=%u/%u elapsed_us=%llu error=%d",
                presented,frames,(unsigned long long)elapsed,error);

    D50GfxExpectation expected=D50_GfxExpectation(logical_width,logical_height);
    uint32_t center=0,corner=0;
    int visual=-1; /* -1 not attempted, 0 mismatch, 1 matched */
    if(!error){
        int center_read=read_pixel(address,display_width,display_height,
                                   expected.center_x,expected.center_y,&center);
        int corner_read=read_pixel(address,display_width,display_height,
                                   expected.corner_x,expected.corner_y,&corner);
        if(center_read || corner_read){
            D50_Log("FAIL: D51 gfx probe VISUAL readback xy=%u,%u/%u,%u status=texture",
                    expected.center_x,expected.center_y,
                    expected.corner_x,expected.corner_y);
            error=D50_PRESENT_FAIL;
        }else{
            int center_ok=color_matches(center,expected.triangle_argb,1);
            int corner_ok=color_matches(corner,expected.background_argb,1);
            visual=center_ok&&corner_ok?1:0;
            D50_Log("%s: D51 gfx probe VISUAL center xy=%u,%u got=%08X expected=%08X "
                    "corner xy=%u,%u got=%08X expected=%08X tolerance=1; "
                    "front-buffer readback only, not scanout or human sight",
                    visual?"PASS":"FAIL",
                    expected.center_x,expected.center_y,(unsigned)center,
                    (unsigned)expected.triangle_argb,
                    expected.corner_x,expected.corner_y,(unsigned)corner,
                    (unsigned)expected.background_argb);
            if(!visual)error=D50_PRESENT_FAIL;
        }
    }
    if(texture)dr_xenos_texture_destroy(texture);
    int closed=dr_xenos_close();
    if(!error)error=closed;
    if(!error)
        D50_Log("D51 gfx probe: PHYSICAL CONFIRMATION PENDING; log proves init, "
                "present submission and front-buffer bytes, not that a person saw the triangle");
    else if(visual==0)
        D50_Log("D51 gfx probe: visual readback mismatch; do not claim console rendering");
    else if(visual<0)
        D50_Log("D51 gfx probe: no visual readback; do not claim console rendering");
    return error;
}
