/* Actual capture/core/backend, GPU API mocked. No fabricated capture globals. */
#define DR_REAL_CAPTURE 1
#define DR_BACKEND_TEST_MAIN unused_mock_capture_test
#include "test_gs_backend.c"
#include "gs_internal.h"
#include "dr_capture_demo.h"
#include "dr_gs_sdk.h"
#include "dr_dma_engine.h"
#include "dr_gs_native.h"
#include "dr_gs_movie.h"
extern int sceGsResetPath(void);
extern int sceGsResetGraph(int,int,int,int);
#include "dr_dma_producer.h"
extern int sceGsSetDefDBuff(void *,int,int,int,int,int,int);
extern int sceGsSwapDBuff(void *,int);
static int foreign_submit(void *ctx,const DrGsSpan *span,uint32_t addr)
{(void)ctx;(void)span;(void)addr;assert(0);return DR_DMA_INVALID;}
static int foreign_gif(void *ctx,const unsigned char *bytes,uint32_t n)
{(void)ctx;(void)bytes;(void)n;assert(0);return DR_STREAM_COMMAND;}
static void le64(uint8_t *p,uint64_t v) {for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));}
static void upload(unsigned psm,unsigned bp,unsigned bw,unsigned width,unsigned height,const uint8_t *pixels,unsigned count)
{
    Gs_RegWrite(0x50,(uint64_t)bp<<32|(uint64_t)bw<<48|(uint64_t)psm<<56);
    Gs_RegWrite(0x51,0);Gs_RegWrite(0x52,width|(uint64_t)height<<32);Gs_RegWrite(0x53,0);
    uint8_t bytes[81]={0};unsigned qwc=(count+15)/16;
    assert(qwc<=4);le64(bytes+1,qwc|(2ull<<58));le64(bytes+9,0);
    memcpy(bytes+17,pixels,count);Gs_Gif(bytes+1,1+qwc);
}
static void check_rgb(const uint8_t *out,unsigned row,unsigned x,unsigned w,unsigned h,uint32_t address) {
    for(unsigned y=0;y<h;y++) for(unsigned k=0;k<w;k++) {
        assert(*out++==(uint8_t)(x+k+(address>>12)));
        assert(*out++==(uint8_t)(row+y));assert(*out++==(uint8_t)((row+y)>>8));
    }
}
static void sdk_store_test(void) {
    unsigned char a[0x71],b[0x71],out[56];
    assert(sceGsSyncPath(1,0)==DR_GS_SDK_ARGUMENT);
    assert(sceGsSyncPath(0,1)==DR_GS_SDK_ARGUMENT);
    assert(!sceGsSyncPath(0,0));download_pattern=1;
    assert(!sceGsSetDefStoreImage(a+1,0,8,1,3,5,4,2));
    assert(!sceGsSetDefStoreImage(b+1,0x700,8,1,3,5,4,2));
    memset(out,0xab,sizeof(out));assert(!sceGsExecStoreImage(a+1,out+16));
    check_rgb(out+16,5,3,4,2,dr_targets_texture(0,0)->address);
    for(unsigned i=0;i<16;i++)assert(out[i]==0xab && out[40+i]==0xab);
    assert(!sceGsExecStoreImage(b+1,out+16));
    check_rgb(out+16,229,3,4,2,dr_targets_texture(0,0)->address);
    assert(!sceGsSetDefStoreImage(a+1,0xe00,8,1,0,0,4,2));
    memset(out,0xab,sizeof(out));assert(sceGsExecStoreImage(a+1,out+16)==DR_GS_SDK_READ);
    for(unsigned i=0;i<sizeof(out);i++)assert(out[i]==0xab);
    uint64_t db[0x260/8];
    assert(!sceGsSetDefDBuff(db,0,512,448,1,0x30,1));
    assert(!sceGsSwapDBuff(db,1) && !sceGsSyncPath(0,0));
    assert(!sceGsExecStoreImage(a+1,out+16));
    int target=GsDraw_TargetGet(0x70,0);assert(target>=0);
    check_rgb(out+16,0,0,4,2,dr_targets_texture((unsigned)target,0)->address);
    assert(sceGsSetDefStoreImage(a+1,0,8,1,0,0,513,2)==DR_GS_SDK_ARGUMENT);
    memset(out,0xab,sizeof(out));assert(sceGsExecStoreImage(a+1,out+16)==DR_GS_SDK_ARGUMENT);
    for(unsigned i=0;i<sizeof(out);i++)assert(out[i]==0xab);
    assert(sceGsSetDefStoreImage(a+1,0,8,0,0,0,4,2)==DR_GS_SDK_ARGUMENT);
    assert(sceGsSetDefStoreImage(a+1,0x700,8,1,0,224,4,1)==DR_GS_SDK_ARGUMENT);
    assert(sceGsSetDefStoreImage(NULL,0,8,1,0,0,4,2)==DR_GS_SDK_ARGUMENT);
    download_pattern=0;gGsMainFbp=0;
    Gs_RegWrite(0x4c,8ull<<16);Gs_RegWrite(0x4e,1ull<<32|64);
    Gs_RegWrite(0x18,0);Gs_RegWrite(0x40,511ull<<16|447ull<<48);
}
static void movie_upload_test(void) {
    assert(!dr_capture_demo_open(1280,720));
    uint8_t *mb=malloc(512*448*4);assert(mb);uint8_t descriptor[129];
    for(unsigned repeat=0;repeat<4;repeat++) {
        unsigned second=repeat&1;
        for(unsigned x=0;x<32;x++)for(unsigned y=0;y<28;y++)for(unsigned i=0;i<256;i++) {
            unsigned gx=x*16+i%16,gy=y*16+i/16;uint8_t *p=mb+((x*28+y)*256+i)*4;
            p[0]=(uint8_t)(gx+42);p[1]=(uint8_t)gy;p[2]=(uint8_t)((gy>>8)|(second<<1));p[3]=0x80;
        }
        assert(!dr_gs_movie_build(descriptor+1,mb,1024,0,second?448:0,512,448));
        assert(!dr_gs_movie_send(descriptor+1));
        for(unsigned y=0;y<448;y++)for(unsigned x=0;x<512;x++) {
            uint32_t expected=(uint8_t)(x+42)|((uint32_t)(uint8_t)y<<8)|((uint32_t)((y>>8)|(second<<1))<<16)|0x80000000u;
            assert(Gs_VramRead(second?0xe00:0,8,0,x,y)==expected);
        }
        unsigned before=draws;assert(!GsDraw_XenosSubmit() && draws>before);
        assert(uploaded_pixel[0]==42 && uploaded_pixel[1]==0 && uploaded_pixel[2]==(second<<1) && uploaded_pixel[3]==0x80);
    }
    /* Display selection must not follow the last draw target. */
    assert(!dr_gs_backend_display(0,0,0));gGsMainFbp=0x70;
    assert(!GsDraw_XenosSubmit());uint32_t displayed_a=bound_textures[0]->address;
    assert(!dr_gs_backend_display(0x70,0,0));gGsMainFbp=0;
    assert(!GsDraw_XenosSubmit());uint32_t displayed_b=bound_textures[0]->address;
    assert(displayed_a!=displayed_b);
    assert(dr_gs_backend_display(0,1,0)==DR_GS_UNSUPPORTED);
    assert(!GsDraw_XenosSubmit() && bound_textures[0]->address==displayed_b);
    assert(!dr_gs_backend_display(0,0,0));gGsMainFbp=0x70;
    assert(!GsDraw_XenosSubmit() && bound_textures[0]->address==displayed_a);
    assert(dr_gs_movie_build(descriptor,mb,1,0,0,512,448)<0 && dr_gs_movie_send(descriptor)<0);
    assert(sceGsResetGraph(1,1,2,0)==DR_GS_SDK_ARGUMENT);
    assert(!sceGsResetPath() && !sceGsResetGraph(0,1,2,1));
    /* Reset path discards a partial GIF tag; next packet is a fresh A+D run. */
    uint8_t partial[16]={0};le64(partial,2|(1ull<<60));le64(partial+8,14);Gs_Gif(partial,1);
    assert(!sceGsResetPath());uint8_t fresh[32]={0};le64(fresh,1|(1ull<<60));le64(fresh+8,14);le64(fresh+16,0x44);le64(fresh+24,0x42);
    Gs_Gif(fresh,2);assert(gGs.alpha[0]==0x44);
    assert(!dr_capture_demo_close());free(mb);
}
int main(void)
{
    movie_upload_test();
    for(unsigned iteration=0;iteration<9;iteration++) {
        assert(dr_capture_demo_open(1280,720)==0);
        unsigned before=draws;
        unsigned vu_before=vu_draws,outline_before=outline_draws,clut_before=clut_draws;
        int rc=dr_capture_demo_draw();
        if(rc || vu_draws!=vu_before+4) {
            uint32_t failed;dr_gs_backend_error(&failed);
            fprintf(stderr,"capture rc=%d failed=%u drawCount=%u verts=%u VU=%u expected=%u\n",rc,failed,gsDrawCount,gsVertCount,vu_draws,vu_before+4);
            for(unsigned k=0;k<gsDrawCount;k++) fprintf(stderr,"descriptor %u: vu=%d native=%d pipe=%d count=%u misc=%f,%f,%f,%f\n",k,gsDraws[k].vu,gsDraws[k].native,gsDraws[k].pipeline,gsDraws[k].count,gsDraws[k].misc[0],gsDraws[k].misc[1],gsDraws[k].misc[2],gsDraws[k].misc[3]);
        }
        assert(rc==0 && draws>before && vu_draws==vu_before+4);
        assert(vu_constants[70]==16777215.0f);
        assert(!memcmp(uploaded_pixel,"\x00\xff\x40\xff",4));
        assert(outline_draws==outline_before+1 && clut_draws==clut_before+2);
        assert(outline_aux_address==clut_aux_address && outline_aux_address!=dr_targets_texture(0,1)->address);
        assert(!gsDrawCount && !gsVertCount && !presents && !gpu_opens && !present_opens);
        sdk_store_test();
        Gs_RegWrite(0x4e,1ull<<32|64);Gs_RegWrite(0x47,1ull<<16|1ull<<17);
        const uint8_t rgba[16]={0xff,0x8a,0x1f,0x80,0x6f,0xd3,0xff,0x40,1,2,3,4,5,6,7,8};
        upload(0,512,1,2,2,rgba,16);
        assert(Gs_VramRead(512,1,0,0,0)==0x801f8aff);
        assert(Gs_VramRead(512,1,0,1,0)==0x40ffd36f);
        assert(Gs_VramRead(512,1,0,0,1)==0x04030201);
        const uint8_t indices[16]={0x21,0x43,0x65,0x87,0xa9,0xcb,0xed,0x0f};
        upload(0x13,600,2,4,4,indices,16);
        assert(Gs_VramRead(600,2,0x13,0,0)==0x21 && Gs_VramRead(600,2,0x13,3,1)==0x0f);
        upload(0x14,640,2,8,2,indices,8);
        assert(Gs_VramRead(640,2,0x14,0,0)==1 && Gs_VramRead(640,2,0x14,1,0)==2);
        upload(2,680,1,2,2,rgba,8);
        assert(Gs_VramRead(680,1,2,0,0)==0x8aff && Gs_VramRead(680,1,2,1,0)==0x801f);
        /* Decode actual core VRAM into capture's canonical RGBA upload. */
        Gs_RegWrite(0,3|8|16);Gs_RegWrite(6,512ull|1ull<<14|1ull<<26|1ull<<30|1ull<<34);
        Gs_RegWrite(8,0); // repeat/nearest => supported sampler 0
        GsVertex tri[3]={{.x=40,.y=40,.r=128,.g=128,.b=128,.a=128,.q=1},
            {.x=80,.y=40,.s=1,.r=128,.g=128,.b=128,.a=128,.q=1},
            {.x=40,.y=80,.t=1,.r=128,.g=128,.b=128,.a=128,.q=1}};
        GsGpu_Draw(3,0,tri);
        assert(gsDrawCount && gsTexCount==1);
        assert(uploaded_pixel[0]==0xff && uploaded_pixel[1]==0x8a && uploaded_pixel[2]==0x1f && uploaded_pixel[3]==0x80);
        assert(GsDraw_XenosSubmit()==0);
        for(unsigned sampler=0;sampler<8;sampler++) {
            Gs_RegWrite(0x14,(sampler&1)?32:0);
            Gs_RegWrite(8,((sampler&2)?1:0)|((sampler&4)?4:0));
            GsGpu_Draw(3,0,tri);assert(gsDrawCount && gsDraws[0].sampler==(int)sampler);
            unsigned first=sampler_calls;assert(GsDraw_XenosSubmit()==0);
            uint32_t expected=((sampler&1)?XSDK_FILTER_LINEAR:XSDK_FILTER_POINT) |
                (((sampler&2)?XSDK_ADDRESS_CLAMP:XSDK_ADDRESS_WRAP)<<4) |
                (((sampler&4)?XSDK_ADDRESS_CLAMP:XSDK_ADDRESS_WRAP)<<8) | (1u<<12);
            int seen=0;for(unsigned j=first;j<sampler_calls;j++)if(sampler_history[j]==expected)seen=1;
            assert(seen);
        }
        Gs_RegWrite(0,3|8);Gs_RegWrite(0x4a,1);
        GsGpu_Draw(3,0,tri);assert(gsDrawCount==1 && gsDraws[0].misc[2]==1);
        Gs_RegWrite(0x4a,0);Gs_RegWrite(0x47,1ull<<16|1ull<<17|1ull<<14|1ull<<15);
        GsGpu_Draw(3,0,tri);
        assert(gsDrawCount==3 && gsDraws[1].native==5 && gsDraws[2].misc[1]==2);
        assert(GsDraw_XenosSubmit()==0);
        // A cleared CT32 target may now feed its own next primitive. The two
        // sprites must remain separate descriptors/resolves, even with equal state.
        Gs_RegWrite(0,6|16);Gs_RegWrite(6,8ull<<14|9ull<<26|9ull<<30|1ull<<34);
        Gs_RegWrite(8,1|4);Gs_RegWrite(0x14,32);
        Gs_RegWrite(0x47,1ull<<16|1ull<<17);
        GsGpu_Draw(6,0,tri);GsGpu_Draw(6,0,tri);
        assert(gsDrawCount==2 && gsDraws[0].tex_is_target && gsDraws[1].tex_is_target);
        assert(gsDraws[0].src==gsDraws[0].target && gsDraws[1].src==gsDraws[1].target);
        unsigned self_passes=tile_passes;
        assert(GsDraw_XenosSubmit()==0 && tile_passes>=self_passes+3);
        assert(bound_textures[2]->address==dr_targets_texture(0,1)->address);
        Gs_RegWrite(0,3|8);
        // Original layer-2 shadow capture creates a stencil run and clear boundary.
        float constants[96]={0};for(unsigned j=0;j<4;j++)constants[j*5]=constants[16+j*5]=constants[56+j*5]=1;
        for(unsigned j=88;j<96;j++)constants[j]=128;
        float mesh[36]={40,40,100,1, 0,1,0,0, 0,0,1,0,
            80,40,100,1, 0,1,0,0, 1,0,1,0, 40,80,100,1, 0,1,0,0, 0,1,1,0};
        Gs_RegWrite(0x4a,1);Gs_RegWrite(0x47,1ull<<16|1ull<<17|1ull<<14);
        GsGpu_DrawVu0(2,0,mesh,3,constants);
        GsGpu_DrawVu0(2,0,mesh,3,constants);
        unsigned clears=shadow_clears,shadow_before=shadow_draws;
        assert(GsDraw_XenosSubmit()==0 && shadow_clears==clears+1 && shadow_draws==shadow_before+1);
        Gs_RegWrite(0x4a,0);Gs_RegWrite(0x47,1ull<<16|1ull<<17);
        // Bad LAST commands must reject the whole frame, including its first draw.
        int expected=DR_XENOS_INVALID;
        if(iteration<4) {
            GsGpu_Draw(3,0,tri);GsGpu_DrawVu4(0,mesh,3,constants);
            assert(gsDrawCount==2);
            if(iteration==0) {gsDraws[1].vu=4;expected=DR_GS_UNSUPPORTED;}
            if(iteration==1) gsVuIdx[gsDraws[1].first+2]=gsVuVertCount;
            if(iteration==2) gsVuUni[gsDraws[1].uniform].misc[2]=0;
            if(iteration==3) gsVuVerts[0]=NAN;
        } else if(iteration<6) {
            Gs_RegWrite(0x4c,1ull|8ull<<16);GsGpu_Draw(3,0,tri);
            if(iteration==5) {
                Gs_RegWrite(0,6|16);Gs_RegWrite(0x4c,64ull|8ull<<16);
                Gs_RegWrite(0x4e,64ull|1ull<<32);Gs_RegWrite(6,32ull|1ull<<14);
                GsGpu_Draw(6,0,tri);Gs_RegWrite(0x4c,8ull<<16);
            }
            GsGpu_Native(1);expected=DR_XENOS_STATE;
        } else {
            GsGpu_Draw(3,0,tri);GsGpu_Native(1);GsDraw *last=&gsDraws[gsDrawCount-1];
            last->native=3;last->src=iteration==7?2:1;last->tex=gsWhite;
            last->pipeline=sXenosBackend.pipeGet((1u<<22)|(15u<<16));last->misc[0]=65535;
            if(iteration==8) {last->pipeline=sXenosBackend.pipeGet((1u<<22)|(15u<<16)|11);expected=DR_GS_UNSUPPORTED;}
        }
        before=draws;assert(GsDraw_XenosSubmit()==expected && draws==before);
        assert(dr_capture_demo_close()==0);
        assert(!gsDraws && !gsVerts && !gsTexCount && !gsTargetCount);
    }
    assert(dr_capture_demo_open(1280,720)==0 && !dr_capture_demo_draw());
    unsigned char descriptor[0x70],output[3]={1,2,3};
    assert(!sceGsSetDefStoreImage(descriptor,0,8,1,0,0,1,1));
    wait_result=STATUS_IO_TIMEOUT;
    assert(sceGsExecStoreImage(descriptor,output)!=0 && output[0]==1 && output[1]==2 && output[2]==3);
    wait_result=STATUS_SUCCESS;assert(!dr_capture_demo_close());
    assert(sceGsSyncPath(0,0)!=0);
    assert(!dr_capture_demo_open(1280,720));
    Dma_AddTexFlush();int used=Dma_GetUsedSize();assert(used>0);
    Dma_InitBuffers();assert(!dr_dma_engine_error() && Dma_GetUsedSize()==used);
    uint64_t words[4]={0x1000000000008001ull,14,0x44,0x42};
    uint8_t unaligned[33];memcpy(unaligned+1,words,sizeof(words));
    assert(!dr_dma_engine_send_u64(unaligned+1,2) && gGs.alpha[0]==0x44);
    assert(Dma_GetUsedSize()==used);Dma_Flush();assert(!dr_dma_producer_error());
    Dma_SendGif(0xdead0000,2);assert(dr_dma_engine_error()==DR_DMA_INVALID);
    Dma_InitBuffers();assert(dr_dma_engine_error()==DR_DMA_INVALID);
    assert(!dr_capture_demo_close());
    for(unsigned bad=0;bad<3;bad++) {
        assert(!dr_capture_demo_open(1280,720));
        assert(dr_dma_engine_send_u64(bad==0?NULL:words,bad==1?0:bad==2?65537:2)==DR_DMA_INVALID);
        assert(!dr_capture_demo_close());
    }
    assert(dr_dma_engine_init()!=0 && !dr_dma_producer_is_open());
    assert(!dr_dma_engine_close());
    assert(!dr_gs_native_open(foreign_gif,NULL));
    assert(!dr_dma_producer_open(foreign_submit,NULL));
    Dma_AddTexFlush();used=Dma_GetUsedSize();
    assert(dr_dma_engine_init()==DR_DMA_STATE && Dma_GetUsedSize()==used);
    assert(!dr_dma_engine_close() && dr_dma_producer_is_open() && Dma_GetUsedSize()==used);
    assert(!dr_dma_producer_close() && !dr_gs_native_close());
    for(unsigned i=0;i<4096;i++) assert(!images[i].live);
    assert(!gpu_opens && !gpu_closes && !present_opens && !present_closes && !presents);
    puts("PASS: shared native Port_Gs VIF/MPG/UNPACK/MSCAL/XGKICK, GIF and DBuff clear probe; full gs_core/gs_draw/GsBackend compiled; unaligned LE GIF, CT32/CT16/T8/T4 uploads, original capture draw/replay, unmerged self-target consumers and raw-alpha source binding, canonical RGBA, borrowed lifecycle, nine open/close cycles, original outline/CLUT consumers, frozen IDs and wrong-owner preflight, three native VU programs, indexed preflight, shadow-run stencil clear, filter/clamp capture states, DATE/FBA marker capture; unknown VU rejected; GS sync and independent RGB readback descriptors, exact targets and crop rows, untouched output on failure, fence timeout; native DMA buffer init is idempotent, typed/unaligned boot GIF submission and bounded address rejection; GPU APIs MOCKED, no hardware pixels");
}
