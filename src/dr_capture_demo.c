/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Synthetic register fixture through the actual gs_core -> gs_draw -> GsBackend.
 * This is renderer integration; it does not start the game's combat simulation.
 */
#include "dr_capture_demo.h"
#include "dr_gs_backend.h"
#include "dr_gs_native.h"
#include "dr_dma_producer.h"
#include "dr_dma_engine.h"
#include "dr_vu1.h"
#include "dr_gs_sdk.h"
#include "gs_internal.h"
#include "xsdk/log.h"
#include <string.h>
#include <math.h>
#include <time.h>
static DrCaptureProfile capture_profile;
void dr_capture_original_profile(DrCaptureProfile *p){if(p)*p=capture_profile;}
static uint64_t capture_us(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u;}
extern int gGsMainFbp;
extern int sceGsSetDefDBuff(void *,int,int,int,int,int,int);
extern int sceGsSwapDBuff(void *,int);
void Gs_XenosCaptureEnable(int enabled);
static int opened;
static int stream_error;
static int stream_gif(void *opaque,const unsigned char *bytes,uint32_t count)
{(void)opaque;Gs_Gif(bytes,count);return 0;}
static int stage_shader(void *opaque,const uint8_t *env,uint32_t prim,const float *v,uint32_t n,const float *matrix){
    (void)opaque;if(!GsGpu_Enabled())return DR_VU_INVALID;
    uint32_t before=gsVuVertCount,indices=gsVuIdxCount,skipped=gsSkipped;
    Gs_Gif(env,2);Gs_RegWrite(0,prim);GsGpu_DrawVu4((int)((prim>>9)&1),v,n,matrix);
    /* draw_state explicitly skips GS memory reinterpretation/stale-target passes.
     * They emit no VU geometry; treating that successful cull as a broken strip
     * used to abort longer actions. Never accept partial emission or backend errors. */
    if(!dr_gs_backend_error(NULL) && gsVuVertCount==before && gsVuIdxCount==indices && gsSkipped-skipped==1u){capture_profile.culled_strips++;return 0;}
    if(gsVuVertCount-before!=n || gsVuIdxCount-indices!=(n-2)*3 || dr_gs_backend_error(NULL)){
        xsdk_log_write("HLE strip rejected n=%u vertices=%u indices=%u skipped=%u prim=%08x frame0=%016llx frame1=%016llx tex0=%016llx tex1=%016llx",n,gsVuVertCount-before,gsVuIdxCount-indices,gsSkipped,prim,(unsigned long long)gGs.frame[0],(unsigned long long)gGs.frame[1],(unsigned long long)gGs.tex0[0],(unsigned long long)gGs.tex0[1]);
        dr_gs_backend_reject();return DR_VU_INVALID;
    }return 0;
}
static int model_shader(void *opaque,const uint8_t *env,uint32_t prim,const float *v,uint32_t n,const float *constants,unsigned layer){
    (void)opaque;if(!GsGpu_Enabled())return DR_VU_INVALID;
    uint32_t before=gsVuVertCount,indices=gsVuIdxCount,skipped=gsSkipped;
    Gs_Gif(env,2);Gs_RegWrite(0,prim);GsGpu_DrawVu0((int)layer,(int)(((layer==2?gGs.prim:prim)>>9)&1),v,n,constants);
    /* draw_state explicitly skips GS memory reinterpretation/stale-target passes.
     * They emit no VU geometry; treating that successful cull as a broken strip
     * used to abort longer actions. Never accept partial emission or backend errors. */
    if(!dr_gs_backend_error(NULL) && gsVuVertCount==before && gsVuIdxCount==indices && gsSkipped-skipped==1u){capture_profile.culled_strips++;return 0;}
    if(gsVuVertCount-before!=n || gsVuIdxCount-indices!=(n-2)*3 || dr_gs_backend_error(NULL)){
        xsdk_log_write("HLE strip rejected n=%u vertices=%u indices=%u skipped=%u prim=%08x frame0=%016llx frame1=%016llx tex0=%016llx tex1=%016llx",n,gsVuVertCount-before,gsVuIdxCount-indices,gsSkipped,prim,(unsigned long long)gGs.frame[0],(unsigned long long)gGs.frame[1],(unsigned long long)gGs.tex0[0],(unsigned long long)gGs.tex0[1]);
        dr_gs_backend_reject();return DR_VU_INVALID;
    }return 0;
}
static void le64(uint8_t *p,uint64_t v) {for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(i*8));}
static void le32(uint8_t *p,uint32_t v) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
/* Synthetic VU upload and XGKICK through the very Port_GsVif1Chain entry
 * used by the engine. No commercial resources and no simulation started. */
static int native_vif_probe(void)
{
    uint8_t bytes[96]={0};
    le32(bytes,0x70000005); /* END + 5 quadwords of VIF words */
    le32(bytes+16,0x4a030000); /* MPG three instructions */
    le32(bytes+20,0x800006fc);le32(bytes+24,0x2ff); /* XGKICK vi0 */
    le32(bytes+28,0x8000033c);le32(bytes+32,0x400002ff); /* E */
    le32(bytes+36,0x8000033c);le32(bytes+40,0x2ff); /* delay */
    le32(bytes+44,0x6c020000); /* UNPACK GIF packet to VU address zero */
    le64(bytes+48,0x1000000000008001ull);le64(bytes+56,14);
    le64(bytes+64,0x44);le64(bytes+72,0x42); /* ALPHA_1 */
    le32(bytes+80,0x14000000); /* MSCAL program zero */
    DrGsSpan span={0x43000000,bytes,sizeof(bytes)};
    int e=dr_gs_native_bind(&span,1);
    if(!e){Port_GsVif1Chain(span.address,0);e=dr_gs_native_error();}
    dr_gs_native_unbind();
    DrGsStreamStats stats={0};
    if(!e)e=dr_gs_native_stats(&stats);
    if(!e && (stats.vu_calls!=1 || stats.gif_quadwords!=2 || gGs.alpha[0]!=0x44))e=DR_STREAM_COMMAND;
    /* The GIF-channel entry point shares mappings and GS state, without
     * resetting VU memory/programs. It receives canonical bytes as well. */
    if(!e) {
        le64(bytes+64,0x48);
        span=(DrGsSpan){0x43000000,bytes+48,32};
        e=dr_gs_native_bind(&span,1);
        if(!e){Port_GsGifChannel(span.address,2,0);e=dr_gs_native_error();
            if(!e && gGs.alpha[0]!=0x48)e=DR_STREAM_COMMAND;}
        dr_gs_native_unbind();
    }
    if(!e) {
        const uint64_t init_gif[12]={0x1000000000008005ull,14,0,0x49,0,0x22,0,0x45,0,8,0,9};
        e=dr_dma_engine_send_u64(init_gif,6);
        if(e)return e;
        uint64_t db[0x230/8];
        e=sceGsSetDefDBuff(db,0,512,448,3,0x31,1);
        if(!e)e=sceGsSwapDBuff(db,0);
        if(!e && ((gGs.frame[0]&511)!=0 || gGs.xyoffset[0]!=(0x720000007000ull)))e=DR_STREAM_COMMAND;
        if(!e)e=sceGsSyncPath(0,0);
        if(!e) {
            uint8_t store[0x70],pixel[3];
            e=sceGsSetDefStoreImage(store,0,8,1,0,0,1,1);
            if(!e)e=sceGsExecStoreImage(store,pixel);
        }
    }
    xsdk_log_write("native Port_Gs VIF/MPG/UNPACK/MSCAL/XGKICK, GIF and DBuff clear probe rc=%d",e);
    return e;
}
static void reg(uint32_t address,uint64_t value)
{
    /* Typed native register producer, explicit LE serialization, then real
     * Dma_Flush -> bounded VIF/GIF. No mixed native/file packet guessing. */
    if(!stream_error)stream_error=dr_dma_write_ad(address,value);
    if(!stream_error) {Dma_Flush();stream_error=dr_dma_producer_error();}
}
static void xyz(unsigned x,unsigned y,unsigned z)
{reg(5,(uint64_t)(x*16)|((uint64_t)y*16<<16)|((uint64_t)z<<32));}
static void palette(void)
{
    // Synthetic CLUT, uploaded through the original GS transfer path.
    uint8_t bytes[1040];le64(bytes,64ull|(2ull<<58));le64(bytes+8,0);
    for(unsigned i=0;i<256;i++) {
        unsigned id=(i&0xe7)|((i&8)<<1)|((i&16)>>1);
        bytes[16+i*4]=(uint8_t)(id);bytes[17+i*4]=(uint8_t)(255-id);
        bytes[18+i*4]=64;bytes[19+i*4]=(uint8_t)(255-id);
    }
    reg(0x50,512ull<<32|1ull<<48);reg(0x51,0);reg(0x52,16ull|16ull<<32);reg(0x53,0);
    Gs_Gif(bytes,65);
}
static void buffer_effects(void)
{
    // Copy main target IDs into the independent effect snapshot, as the game
    // marks GfxPost_CopyAlphaToDepth. The depth alias itself is not drawn.
    reg(0,6|16);reg(0x4c,64ull|8ull<<16);reg(0x4e,64ull|1ull<<24|1ull<<32);reg(6,1ull<<14);
    xyz(0,0,0);xyz(512,448,0);
    // A real captured depth-page byte -> CLUT pass, alpha writes only.
    reg(0x4c,8ull<<16|0x00ffffffull<<32);
    reg(6,2048ull|1ull<<14|0x1bull<<20|512ull<<37);
    xyz(0,0,0);xyz(512,448,0);
    // Outline must continue sampling the FROZEN IDs, after live aux changed.
    if(!stream_error) {Port_GsMarker(1);Dma_Flush();stream_error=dr_dma_producer_error();}
    // The fog-depth variant of the same original consumer.
    reg(0x4c,64ull|8ull<<16);xyz(0,0,0);xyz(512,448,0);
    reg(0x4c,8ull<<16|0x00ffffffull<<32);xyz(0,0,0);xyz(512,448,0);
    reg(0x4c,8ull<<16);
}
int dr_capture_demo_open(uint32_t width,uint32_t height)
{
    if(opened) return -1;
    int e=dr_gs_backend_attach(width,height);if(e)return e;
    if(!GsGpu_Init()) return -2;
    stream_error=dr_gs_native_open(stream_gif,NULL);
    if(stream_error) {GsDraw_XenosClose();return stream_error;}
    Dma_InitBuffers();stream_error=dr_dma_engine_error();
    if(stream_error) {dr_dma_engine_close();dr_gs_native_close();GsDraw_XenosClose();return stream_error;}
    Dma_ResetBuffers();
    Gs_XenosCaptureEnable(1);
    /* Exercise mapped NEXT and its patched return through the real consumer.
     * Native u32 stores are converted in a temporary snapshot at Flush. */
    _Alignas(16) uint32_t chain[8]={0x10000000,0,0,0,0x20000000,0,0,0};
    uint32_t address;
    stream_error=dr_dma_map_chain(chain,sizeof(chain),DR_DMA_W32,&address);
    if(!stream_error){Dma_AddNext(chain,chain+4);Dma_Flush();stream_error=dr_dma_producer_error();}
    xsdk_log_write("mapped DMA NEXT/return diagnostic rc=%d",stream_error);
    if(!stream_error)stream_error=native_vif_probe();
    if(!stream_error)palette();
    if(stream_error) {dr_dma_engine_close();dr_gs_native_close();GsDraw_XenosClose();Gs_XenosCaptureEnable(0);return stream_error;}
    gGsMainFbp=0;opened=1;
    xsdk_log_write("GS capture: adapted native producers/Dma_Flush -> bounded DMA/VIF DIRECT -> actual gs_core/GIF -> gs_draw -> sXenosBackend; synthetic data, no battle");
    return 0;
}
int dr_capture_demo_draw(void)
{
    if(opened!=1)return -1;
    if(stream_error)return stream_error;
    Dma_AddFrame(0,8,0);Dma_AddZbuf(64,1);
    Dma_AddScissor(0,511,0,447);Dma_AddXyOffset(0,0);Dma_Flush();
    stream_error=dr_dma_producer_error();if(stream_error)return stream_error;
    reg(0x47,1ull<<16|1ull<<17); /* ZTE / ALWAYS */
    reg(0x1a,1);
    // Clear through the original sprite capture before this frame's geometry.
    reg(0x4e,64|1ull<<24);reg(0,6);reg(1,0x001c100d);xyz(0,0,0);xyz(512,448,0);
    reg(0x4e,1ull<<32|64);reg(0,3|8); /* triangle, gouraud */
    reg(1,0x802020d0);xyz(60,100,0);
    reg(1,0x8020d020);xyz(450,180,0);
    reg(1,0x80d02020);xyz(210,390,0);
    if(!gsDrawCount || !gsVertCount)return -3;
    /* The three original vertex programs consume original capture arrays and
     * animated synthetic constants. This does not load a fighter or run combat. */
    float constants[96]={0};
    for(unsigned i=0;i<4;i++)constants[i*5]=constants[16+i*5]=constants[56+i*5]=1;
    for(unsigned i=88;i<96;i++)constants[i]=128;
    constants[91]=3; // a distinct synthetic object ID for the outline consumer
    constants[40]=1;constants[44]=0;constants[48]=0;constants[52]=0;
    constants[12]=sinf(gGsFrame*0.025f)*30;
    float model[36]={80,160,10000,0.5f, 0,1,0,0, 0,0,1,0,
        180,160,10000,0.5f, 0,1,0,0, 1,0,1,0,
        80,260,10000,0.5f, 0,1,0,0, 0,1,1,0};
    reg(0x4e,64|1ull<<24);reg(0x47,1ull<<16|2ull<<17);
    GsGpu_DrawVu0(0,0,model,3,constants);
    float stage[36]={240,160,11000,1, 128,64,32,128, 0,0,1,0,
        340,160,11000,1, 32,128,64,128, 1,0,1,0,
        240,260,11000,1, 64,32,128,128, 0,1,1,0};
    memset(constants,0,sizeof(constants));for(unsigned i=0;i<4;i++)constants[i*5]=1;
    GsGpu_DrawVu4(0,stage,3,constants);
    float shadow[48]={360,160,12000,1, 0,0,0,0, 48,48,48,128,
        0,0,0,0, 460,160,12000,1, 0,0,0,0, 48,48,48,128,
        0,0,0,0, 360,260,12000,1, 0,0,0,0, 48,48,48,128};
    for(unsigned i=0;i<4;i++)constants[48+i*5]=1;
    constants[64]=1;
    GsGpu_DrawVu6(0,shadow,3,constants);
    /* Two coincident original layer-2 draws test overlap masking. Baseline
     * alpha is zero here; DATE accepts it, FBA marks it, stencil masks overlap. */
    memset(constants,0,sizeof(constants));
    for(unsigned i=0;i<4;i++)constants[i*5]=constants[16+i*5]=constants[56+i*5]=1;
    constants[68]=320;constants[69]=160;constants[91]=64;
    reg(0,3|8|64);reg(0x42,0x44);reg(0x4e,1ull<<32|64|1ull<<24);
    reg(0x47,1ull<<16|1ull<<17|1ull<<14);reg(0x4a,1);
    GsGpu_DrawVu0(2,0,model,3,constants);GsGpu_DrawVu0(2,0,model,3,constants);
    reg(0x4a,0);reg(0x47,1ull<<16|1ull<<17);
    buffer_effects();
    if(stream_error)return stream_error;
    gGsFrame++;
    return GsDraw_XenosSubmit();
}
int dr_capture_demo_close(void)
{
    if(!opened)return 0;
    int e=GsDraw_XenosClose();if(e)return e;
    e=dr_dma_engine_close();if(e)return e;
    dr_gs_native_close();Gs_XenosCaptureEnable(0);opened=0;return 0;
}
/* Real original producer session: no palette, synthetic VIF or fixture draws. */
int dr_capture_original_open(uint32_t width,uint32_t height)
{
    if(opened)return -1;
    int e=dr_gs_backend_attach(width,height);if(e)return e;
    if(!GsGpu_Init())return -2;
    e=dr_gs_native_open(stream_gif,NULL);
    if(e){GsDraw_XenosClose();return e;}
    /* Real geometry has clipped 63-vertex batches; the synthetic 10k VU
     * limit expires while clipping the last vertex. Retain finite guards. */
    e=dr_gs_native_limits(8192,2359296,250000);
    if(e){dr_gs_native_close();GsDraw_XenosClose();return e;}
    Dma_InitBuffers();e=dr_dma_engine_error();
    if(e){dr_dma_engine_close();dr_gs_native_close();GsDraw_XenosClose();return e;}
    Dma_ResetBuffers();Gs_XenosCaptureEnable(1);opened=2;stream_error=0;
    dr_vu1_stage_renderer(stage_shader,NULL);
    dr_vu1_model_renderer(model_shader,NULL);
    xsdk_log_write("D36 original renderer opened; no synthetic packets submitted");
    return 0;
}
static int original_submit(int gpu)
{
    if(opened!=2)return -1;
    uint64_t started=capture_us();Dma_Flush();capture_profile.dma_us=capture_us()-started;
    capture_profile.draws=gsDrawCount;capture_profile.vertices=gsVertCount;capture_profile.vu_vertices=gsVuVertCount;
    int e=dr_dma_engine_error();if(!e)e=dr_gs_native_error();
    if(e)return e;
    if(!gsDrawCount || !gsVertCount)return -3;
    static unsigned frames;
    if(!frames){
        xsdk_log_write("D36 original loading capture draws=%u vertices=%u targets=%d frame=%08x offset=%08x",gsDrawCount,gsVertCount,gsTargetCount,(unsigned)gGs.frame[0],(unsigned)gGs.xyoffset[0]);
        for(unsigned i=0;i<gsDrawCount && i<8;i++){
            GsDraw *d=&gsDraws[i];Vtx empty={0};
            Vtx *v=d->count && d->first<gsVertCount?&gsVerts[d->first]:&empty;
            xsdk_log_write("D36 draw %u target=%d fbp=%u count=%u texmode=%d tex=%08x xy=%.1f,%.1f rgba=%u,%u,%u,%u",i,d->target,d->target>=0 && d->target<gsTargetCount?(unsigned)gsTargets[d->target].fbp:999,d->count,d->mode[0],(unsigned)d->tex,(double)v->x,(double)v->y,v->r,v->g,v->b,v->a);
        }
    }
    frames++;gGsFrame++;
    started=capture_us();
    if(gpu)e=GsDraw_XenosSubmit();
    else {
        /* Same bounded CPU frame retirement as the pinned native frame_end.
         * No queued arena is allowed to auto-flush through the GPU backend. */
        for(int i=0;i<gsTargetCount;i++)gsTargets[i].draws=0;
        gsDrawCount=gsVertCount=gsVuVertCount=gsVuIdxCount=gsVuUniCount=0;
        gsAnchor=0;gsSkipped=gsNative=0;
        extern unsigned gGsFbUploads;gGsFbUploads=0;
    }
    capture_profile.replay_us=capture_us()-started;return e;
}

int dr_capture_original_submit(void){return original_submit(1);}
int dr_capture_original_decode(void){return original_submit(0);}
