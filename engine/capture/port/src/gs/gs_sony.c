/*
 * The three Sony graphics-library calls that actually put something into GS memory. The game leaves the
 * per-frame CLEAR of the colour and depth buffers to the library: sceGsSetDefDBuff builds, for each of the two
 * buffers, a GIF packet "drawing environment + clear sprite" inside the sceGsDBuff structure (the game then
 * patches the sprite's corners and colour), and sceGsSwapDBuff sends the packet of the buffer about to be drawn.
 * Without them nothing clears the depth buffer between frames (seen as smears trailing behind camera pans).
 * The layout is libgraph's (include/sys/gfx.h in the game sources documents it).
 */
#include <stdint.h>
#include <string.h>
#include "../port_addr.h"
#ifdef DR_XBOX360
#include "dr_gs_native.h"
#include "dr_gs_sdk.h"
#include "dr_gs_backend.h"
#include "gs_draw.h"
#include <stdlib.h>
#endif

extern void Port_GsGifChannel(uint32_t addr, uint32_t qwc, int chain);
int gGsMainFbp = -1; /* frame-buffer address of the frame being drawn (from sceGsSwapDBuff); -1 before the first swap */

typedef struct { uint64_t v, addr; } Reg;                      /* one A+D register write */
typedef struct { Reg frame, zbuf, xyoffset, scissor, prmodecont, colclamp, dthe, test; } DrawEnv;
typedef struct { Reg testa, prim, rgbaq, xyz2a, xyz2b, testb; } Clear;
typedef struct {
    uint64_t disp[2][5];
    uint64_t giftag0[2];
    DrawEnv draw0;
    Clear clear0;
    uint64_t giftag1[2];
    DrawEnv draw1;
    Clear clear1;
} DBuff;

static void set(Reg *r, uint64_t v, uint64_t addr) {
    r->v = v;
    r->addr = addr;
}

static void fill(uint64_t *tag, DrawEnv *d, Clear *c, uint32_t fbp, uint32_t zbp, int psm, int w, int h, int ztest, int zpsm, int clear) {
    uint64_t test = ztest ? (1ull << 16) | ((uint64_t)(ztest & 3) << 17) : 0;

    tag[0] = (uint64_t)(clear ? 14 : 8) | (1ull << 15) | (1ull << 60); /* NLOOP, EOP, one register per loop */
    tag[1] = 0xE;                                                      /* A+D */
    set(&d->frame, fbp | (uint64_t)((w + 63) / 64) << 16 | (uint64_t)psm << 24, 0x4C);
    set(&d->zbuf, zbp | (uint64_t)(zpsm & 15) << 24 | (uint64_t)(ztest == 0) << 32, 0x4E);
    set(&d->xyoffset, (uint64_t)((2048 - w / 2) << 4) | (uint64_t)((2048 - h / 2) << 4) << 32, 0x18);
    set(&d->scissor, (uint64_t)(w - 1) << 16 | (uint64_t)(h - 1) << 48, 0x40);
    set(&d->prmodecont, 1, 0x1A);
    set(&d->colclamp, 1, 0x46);
    set(&d->dthe, (psm & 2) ? 1 : 0, 0x45);
    set(&d->test, test, 0x47);
    set(&c->testa, (1ull << 16) | (1ull << 17), 0x47); /* depth test ALWAYS: the sprite writes every pixel's depth */
    set(&c->prim, 6, 0x00);
    set(&c->rgbaq, 0, 0x01);
    set(&c->xyz2a, (uint64_t)((2048 - w / 2) << 4) | (uint64_t)((2048 - h / 2) << 4) << 16, 0x05);
    set(&c->xyz2b, (uint64_t)((2048 + w / 2) << 4) | (uint64_t)((2048 + h / 2) << 4) << 16, 0x05);
    set(&c->testb, test, 0x47);
}

int sceGsSetDefDBuff(void *p, int psm, int w, int h, int ztest, int zpsm, int clear) {
    DBuff *db = p;
#ifdef DR_XBOX360
    if(!p || (psm!=0 && psm!=2) || w<=0 || w>2048 || h<=0 || h>2048 ||
       ztest<0 || ztest>3 || (zpsm!=0x30 && zpsm!=0x31 && zpsm!=0x32 && zpsm!=0x3a) ||
       (((uint64_t)w*h*((psm&2)?2:4)+8191)/8192)*3>512)
        return dr_gs_native_reject(DR_STREAM_INVALID);
#endif
    uint32_t bytes = (psm & 2) ? 2 : 4, pages = ((uint32_t)w * (uint32_t)h * bytes + 8191) / 8192;

    memset(db, 0, sizeof(*db));
    fill(db->giftag0, &db->draw0, &db->clear0, 0, pages * 2, psm, w, h, ztest, zpsm, clear);
    fill(db->giftag1, &db->draw1, &db->clear1, pages, pages * 2, psm, w, h, ztest, zpsm, clear);
    return 0;
}

int sceGsPutDrawEnv(uint64_t *giftag) {
#ifdef DR_XBOX360
    /* SetDefDBuff and the game edit native u64 fields. Serialize them here;
     * neither a native pointer-as-address nor a whole-buffer word swap works. */
    if(!giftag)return dr_gs_native_reject(DR_STREAM_INVALID);
    uint32_t n=(uint32_t)(giftag[0]&0x7fff);
    if((n!=8 && n!=14) || ((giftag[0]>>58)&3)!=0 ||
       (giftag[0]>>60)!=1 || giftag[1]!=14)
        return dr_gs_native_reject(DR_STREAM_INVALID);
    unsigned char canonical[15*16];
    for(unsigned i=0;i<2*(n+1);i++) {
        uint64_t value=giftag[i];
        for(unsigned b=0;b<8;b++)canonical[i*8+b]=(unsigned char)(value>>(8*b));
    }
    DrGsSpan span={0x44000000,canonical,(n+1)*16};
    int e=dr_gs_native_bind(&span,1);
    if(e)return e;
    Port_GsGifChannel(span.address,n+1,0);e=dr_gs_native_error();
    dr_gs_native_unbind();return e;
#else
    Port_GsGifChannel(Port_Addr(giftag), 1 + (uint32_t)(giftag[0] & 0x7FFF), 0);
    return 0;
#endif
}

int sceGsSwapDBuff(void *p, int id) {
    DBuff *db = p;
#ifdef DR_XBOX360
    if(!p)return dr_gs_native_reject(DR_STREAM_INVALID);
#endif
    /* the buffer this frame is drawn into: what the renderer shows when the frame is finished */
    gGsMainFbp = (int)(((id & 1) ? db->draw1.frame.v : db->draw0.frame.v) & 0x1FF);
    return sceGsPutDrawEnv((id & 1) ? db->giftag1 : db->giftag0);
}

#ifdef DR_XBOX360
int32_t sceGsSyncPath(int32_t,uint16_t);
extern void Gs_GifReset(void);
extern void Gs_XenosCaptureEnable(int);
int32_t sceGsResetPath(void) {
    int e=sceGsSyncPath(0,0);if(e)return e;
    e=dr_gs_native_reset();if(e)return e;
    Gs_GifReset();return 0;
}
int32_t sceGsResetGraph(int32_t mode,int32_t inter,int32_t omode,int32_t ffmd) {
    if(mode || inter!=1 || omode!=2 || (ffmd!=0 && ffmd!=1))return DR_GS_SDK_ARGUMENT;
    int e=sceGsSyncPath(0,0);if(e)return e;
    e=dr_gs_native_reset();if(e)return e;
    Gs_XenosCaptureEnable(1);gGsMainFbp=-1;return 0;
}
/* A descriptor belongs to its caller; use memcpy for the game's byte arrays. */
typedef struct {
    uint32_t magic,version;
    int32_t bp,bw,psm,x,y,w,h;
} NativeStore;
static int valid_store(const NativeStore *s) {
    return s->magic==0x58534753 && s->version==1 && s->bp>=0 && s->bp<0x1c00 &&
        !(s->bp&7) && s->bw==8 && s->psm==1 && s->x>=0 && s->y>=0 &&
        s->w>0 && s->h>0 && s->w<=512 && s->h<=448 &&
        s->x<=512-s->w && (s->bp%0xe00)/8+s->y<=448-s->h;
}
int32_t sceGsSyncPath(int32_t mode,uint16_t timeout) {
    if(mode || timeout) return DR_GS_SDK_ARGUMENT;
    int e=GsDraw_XenosSubmit();return e?e:dr_gs_backend_sync();
}
int32_t sceGsSetDefStoreImage(void *p,int16_t bp,int16_t bw,int16_t psm,
                            int16_t x,int16_t y,int16_t w,int16_t h) {
    if(!p) return DR_GS_SDK_ARGUMENT;
    NativeStore s={0x58534753,1,bp,bw,psm,x,y,w,h};
    memset(p,0,0x70);
    if(!valid_store(&s)) return DR_GS_SDK_ARGUMENT;
    memcpy(p,&s,sizeof(s));return 0;
}
int32_t sceGsExecStoreImage(void *p,void *dst) {
    if(!p || !dst) return DR_GS_SDK_ARGUMENT;
    NativeStore s;memcpy(&s,p,sizeof(s));
    if(!valid_store(&s)) return DR_GS_SDK_ARGUMENT;
    uint8_t *rgba=malloc(512*448*4);
    if(!rgba) return DR_GS_SDK_MEMORY;
    int e=sceGsSyncPath(0,0);
    if(!e && !GsDraw_XenosReadFrameExact((uint32_t)(s.bp/0xe00)*0x70,rgba)) e=DR_GS_SDK_READ;
    if(!e) {
        uint8_t *out=dst;
        for(int y=0;y<s.h;y++) {
            const uint8_t *row=rgba+(((s.bp%0xe00)/8+s.y+y)*512+s.x)*4;
            for(int x=0;x<s.w;x++) {memcpy(out,row+x*4,3);out+=3;}
        }
    }
    free(rgba);return e;
}
#endif
