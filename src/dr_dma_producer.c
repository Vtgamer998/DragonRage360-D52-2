/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Packet layouts and fill algorithm adapted from upstream src/sys/dma.c
 * 3ca419c7966b11438e2dc3c9a7e1914f102ed545. Explicit LE stores replace the
 * original mixture of native u32/u64 stores. Every producer reserves its
 * entire packet before changing the list. No PS2 MMIO or host pointer tags.
 */
#include "dr_dma_producer.h"
#include <stdlib.h>
#include <string.h>
#ifdef DR_XBOX360
#include <xsdk/log.h>
#endif
enum { BYTES=0x100000, TAIL=64, REF_BYTES=0x800000 };
enum { PENDING_MAX=16384, DIRECT_MAX=0x10000 };
typedef struct {uint32_t offset,bytes;uint8_t kind;} Pending;
static struct {
    unsigned char *buffer[2];size_t used[2];unsigned current;
    unsigned char *owner[3];int direct_active;
    DrDmaSubmit submit;void *opaque;int error;
    unsigned char *refs;size_t refs_used;
    unsigned char *sink;Pending pending[PENDING_MAX];unsigned npending;
    DrDmaSubmitMapped mapped_submit;
    struct {const unsigned char *bytes;size_t size;DrDmaKind kind;uint32_t address;} maps[16];
    unsigned nmaps;size_t mapped_bytes;
} dma;
static int swap_mode=-1;
static int must_swap(void)
{
    if(swap_mode>=0)return swap_mode;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__==__ORDER_BIG_ENDIAN__
    return 1;
#else
    return 0;
#endif
}
void dr_dma_native_swap(int mode) {swap_mode=mode<0?-1:!!mode;}
int dr_dma_pending_regions(void) {return (int)dma.npending;}
static void reverse(unsigned char *p,unsigned width)
{for(unsigned i=0;i<width/2;i++){unsigned char t=p[i];p[i]=p[width-1-i];p[width-1-i]=t;}}
static void convert(unsigned char *p,size_t bytes,DrDmaKind kind)
{
    if(!must_swap() || kind==DR_DMA_RAW)return;
    unsigned w=kind==DR_DMA_W64?8:4;
    for(size_t i=0;i+w<=bytes;i+=w)reverse(p+i,w);
}
static void resolve_pending(void)
{
    for(unsigned i=0;i<dma.npending;i++)
        convert(dma.buffer[dma.current]+dma.pending[i].offset,dma.pending[i].bytes,(DrDmaKind)dma.pending[i].kind);
    dma.npending=0;
}
static void le32(unsigned char *p,uint32_t x)
{for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(x>>(i*8));}
static void le64(unsigned char *p,uint64_t x)
{for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(x>>(i*8));}
static __attribute__((noinline)) int fail(int error) {
    if(!dma.error){
#ifdef DR_XBOX360
        xsdk_log_write("DMA first error=%d caller=%p used=%u pending=%u maps=%u direct=%d",error,__builtin_return_address(0),(unsigned)dma.used[dma.current],dma.npending,dma.nmaps,dma.direct_active);
#endif
        dma.error=error;
    }
    return dma.error;
}
static int ready(void)
{return (!dma.submit && !dma.mapped_submit)?fail(DR_DMA_STATE):dma.error;}
static unsigned char *reserve(size_t n)
{
    if(ready())return NULL;
    if(dma.direct_active){fail(DR_DMA_STATE);return NULL;}
    if(!n || n%16) {fail(DR_DMA_INVALID);return NULL;}
    if(n>BYTES-TAIL-dma.used[dma.current]) {
#ifdef DR_XBOX360
        xsdk_log_write("DMA reserve overflow requested=%u used=%u caller=%p",(unsigned)n,(unsigned)dma.used[dma.current],__builtin_return_address(0));
#endif
        fail(DR_DMA_FULL);return NULL;
    }
    unsigned char *p=dma.buffer[dma.current]+dma.used[dma.current];
    dma.used[dma.current]+=n;return p;
}
static void chain_tag(unsigned char *p,uint32_t qwc)
{le32(p,0x10000000|qwc);le32(p+4,0);le32(p+8,0x10000000);le32(p+12,0x50000000|qwc);}
static void end_tag(unsigned char *p)
{memset(p,0,16);le32(p,0x70000000);}
static int open_producer(DrDmaSubmit submit,DrDmaSubmitMapped mapped,void *opaque)
{
    if(dma.submit || dma.mapped_submit)return DR_DMA_STATE;
    if(!submit && !mapped)return DR_DMA_INVALID;
    unsigned char *a=malloc(BYTES+15),*b=malloc(BYTES+15);
    if(!a || !b) {free(b);free(a);return DR_DMA_MEMORY;}
    unsigned char *s=malloc(BYTES+15);
    if(!s) {free(b);free(a);return DR_DMA_MEMORY;}
    memset(&dma,0,sizeof(dma));dma.owner[0]=a;dma.owner[1]=b;dma.owner[2]=s;
    dma.buffer[0]=(void *)(((uintptr_t)a+15)&~(uintptr_t)15);
    dma.buffer[1]=(void *)(((uintptr_t)b+15)&~(uintptr_t)15);
    dma.sink=(void *)(((uintptr_t)s+15)&~(uintptr_t)15);
    dma.submit=submit;dma.mapped_submit=mapped;dma.opaque=opaque;Dma_ResetBuffers();return 0;
}
int dr_dma_producer_open(DrDmaSubmit submit,void *opaque)
{return open_producer(submit,NULL,opaque);}
int dr_dma_producer_open_mapped(DrDmaSubmitMapped submit,void *opaque)
{return open_producer(NULL,submit,opaque);}
int dr_dma_producer_close(void)
{
    if(!dma.submit && !dma.mapped_submit)return DR_DMA_STATE;
    free(dma.refs);free(dma.owner[2]);free(dma.owner[1]);free(dma.owner[0]);memset(&dma,0,sizeof(dma));return 0;
}
int dr_dma_producer_error(void) {return dma.error;}
int dr_dma_producer_is_open(void) {return dma.submit || dma.mapped_submit;}
int dr_dma_producer_view(DrGsSpan *span)
{
    if(!span)return DR_DMA_INVALID;
    if(!dma.submit && !dma.mapped_submit)return DR_DMA_STATE;
    if(dma.direct_active)return DR_DMA_STATE;
    resolve_pending();
    *span=(DrGsSpan){0x41000000u+dma.current*BYTES,dma.buffer[dma.current],dma.used[dma.current]};
    return 0;
}
int dr_dma_append_canonical(const void *data,size_t bytes)
{
    if(ready())return dma.error;
    if(!data || !bytes || bytes%16)return fail(DR_DMA_INVALID);
    unsigned char *p=reserve(bytes);if(!p)return dma.error;
    memcpy(p,data,bytes);return 0;
}
/* Exact TexBltPacket ABI: four native u32 DMA/VIF words, then four
 * native u64 GIF/A+D words. File IMAGE payloads remain canonical bytes. */
int dr_dma_add_texture_header(const void *packet)
{
    if(ready())return dma.error;
    if(!packet)return fail(DR_DMA_INVALID);
    unsigned char *p=reserve(48);if(!p)return dma.error;
    memcpy(p,packet,48);convert(p,16,DR_DMA_W32);convert(p+16,32,DR_DMA_W64);
    return 0;
}
int dr_dma_append_words(const uint32_t *words,size_t count)
{
    if(ready())return dma.error;
    if(!words || !count || count%4 || count>(BYTES-TAIL)/4)return fail(DR_DMA_INVALID);
    unsigned char *p=reserve(count*4);if(!p)return dma.error;
    for(size_t i=0;i<count;i++)le32(p+i*4,words[i]);
    return 0;
}
static void ad(unsigned char *p,uint32_t reg,uint64_t value)
{le64(p,value);le64(p+8,reg);}
static int registers(uint32_t a,uint32_t b,uint64_t value,int flush)
{
    if(a>127 || (b!=UINT32_MAX && b>127))return fail(DR_DMA_INVALID);
    unsigned n=b==UINT32_MAX?1:2;unsigned char *p=reserve((n+2)*16);if(!p)return dma.error;
    chain_tag(p,n+1);if(!flush)le32(p+8,0);
    le64(p+16,(uint64_t)n|0x1000000000008000ull);le64(p+24,14);
    ad(p+32,a,value);if(n==2)ad(p+48,b,value);return 0;
}
int dr_dma_write_ad(uint32_t address,unsigned long long value)
{return registers(address,UINT32_MAX,value,1);}
void Dma_Flush(void)
{
    if(ready())return;
    if(dma.direct_active){fail(DR_DMA_STATE);return;}
    resolve_pending();
    /* Always reserved separately from application packets, even at capacity. */
    unsigned char *p=dma.buffer[dma.current]+dma.used[dma.current];
    chain_tag(p,2);le64(p+16,0x1000000000008001ull);le64(p+24,14);
    ad(p+32,0x61,0);end_tag(p+48);
    DrGsSpan span={0x41000000u+dma.current*BYTES,dma.buffer[dma.current],dma.used[dma.current]+TAIL};
    DrGsSpan spans[18];unsigned char *copies[16]={0};unsigned i;
    spans[0]=span;int error=0;
    for(i=0;i<dma.nmaps;i++) {
        copies[i]=malloc(dma.maps[i].size);
        if(!copies[i]){error=DR_DMA_MEMORY;break;}
        memcpy(copies[i],dma.maps[i].bytes,dma.maps[i].size);
        convert(copies[i],dma.maps[i].size,dma.maps[i].kind);
        spans[i+1]=(DrGsSpan){dma.maps[i].address,copies[i],dma.maps[i].size};
    }
    size_t nspans=dma.nmaps+1;
    if(dma.refs_used)spans[nspans++]=(DrGsSpan){0x43000000u,dma.refs,dma.refs_used};
    if(!error)error=dma.mapped_submit?dma.mapped_submit(dma.opaque,spans,nspans,span.address):dma.submit(dma.opaque,&span,span.address);
    for(i=0;i<dma.nmaps;i++)free(copies[i]);
    if(error) {fail(error);return;} // Failed work is not silently reused/swapped.
    dma.current^=1;dma.used[dma.current]=0;end_tag(dma.buffer[dma.current]);
    dma.nmaps=0;dma.mapped_bytes=0;dma.refs_used=0;
}
void Dma_ResetBuffers(void)
{
    if(!dma.submit && !dma.mapped_submit) {fail(DR_DMA_STATE);return;}
    dma.used[0]=dma.used[1]=0;dma.npending=0;dma.direct_active=0;dma.nmaps=0;dma.mapped_bytes=0;dma.refs_used=0;end_tag(dma.buffer[0]);end_tag(dma.buffer[1]);
}
int32_t Dma_GetBufSize(void) {return (dma.submit || dma.mapped_submit)?BYTES-TAIL:0;}
int32_t Dma_GetUsedSize(void) {return (dma.submit || dma.mapped_submit)?(int32_t)dma.used[dma.current]:0;}
int32_t Dma_GetFreeSize(void) {return Dma_GetBufSize()-Dma_GetUsedSize();}
void Dma_AddTexFlush(void) {(void)registers(0x3f,UINT32_MAX,0,1);}
void Dma_AddFrame(int32_t bp,int32_t bw,int32_t mask)
{
    if(bp<0 || bp>511 || bw<0 || bw>63) {fail(DR_DMA_INVALID);return;}
    (void)registers(0x4c,0x4d,(uint64_t)(uint32_t)bp|(uint64_t)(uint32_t)bw<<16|(uint64_t)(uint32_t)mask<<32,1);
}
void Dma_AddClamp(int32_t ws,int32_t wt,int32_t u0,int32_t u1,int32_t v0,int32_t v1)
{
    if(ws<0 || ws>3 || wt<0 || wt>3 || u0<0 || u0>1023 || u1<0 || u1>1023 || v0<0 || v0>1023 || v1<0 || v1>1023) {fail(DR_DMA_INVALID);return;}
    (void)registers(8,9,(uint64_t)ws|(uint64_t)wt<<2|(uint64_t)u0<<4|(uint64_t)u1<<14|(uint64_t)v0<<24|(uint64_t)v1<<34,1);
}
void Dma_AddScissor(int32_t x0,int32_t x1,int32_t y0,int32_t y1)
{
    if(x0<0 || x0>2047 || x1<0 || x1>2047 || y0<0 || y0>2047 || y1<0 || y1>2047) {fail(DR_DMA_INVALID);return;}
    (void)registers(0x40,0x41,(uint64_t)x0|(uint64_t)x1<<16|(uint64_t)y0<<32|(uint64_t)y1<<48,1);
}
void Dma_AddXyOffset(int32_t x,int32_t y)
{
    if(x<0 || x>4095 || y<0 || y>4095) {fail(DR_DMA_INVALID);return;}
    (void)registers(0x18,0x19,(uint64_t)x<<4|(uint64_t)y<<36,1);
}
void Dma_AddZbuf(int32_t bp,int32_t mask)
{
    if(bp<0 || bp>511 || mask<0 || mask>1) {fail(DR_DMA_INVALID);return;}
    (void)registers(0x4e,UINT32_MAX,(uint64_t)bp|0x31000000ull|(uint64_t)mask<<32,1);
}
void Dma_AddTex0(unsigned long long v) {(void)registers(6,UINT32_MAX,v,1);}
void Dma_AddTex1(unsigned long long v) {(void)registers(0x14,UINT32_MAX,v,1);}
void Dma_AddFba(int32_t v)
{if(v<0 || v>1)fail(DR_DMA_INVALID);else (void)registers(0x4a,UINT32_MAX,(uint32_t)v,1);}
void Dma_AddColClamp(unsigned long long v) {(void)registers(0x46,UINT32_MAX,v,1);}
void Dma_AddAlpha(unsigned long long v) {(void)registers(0x42,UINT32_MAX,v,0);}
void Dma_AddFillRect(int32_t x,int32_t y,int32_t w,int32_t h,uint32_t rgba)
{
    if(x<0 || y<0 || w<0 || h<0 || w>4095 || h>4095 || x>4095-(w/32)*32 || y>4095-h) {fail(DR_DMA_INVALID);return;}
    /* Preserve the original floor(w/32), including a zero-strip packet. */
    unsigned n=(unsigned)w/32;unsigned char *p=reserve((n+8)*16);if(!p)return;
    chain_tag(p,n+7);p+=16;le64(p,0x1000000000000005ull);le64(p+8,14);p+=16;
    ad(p,0x42,0x44);p+=16;ad(p,0x47,0x30000);p+=16;ad(p,0x4a,0);p+=16;
    ad(p,0,0x46);p+=16;ad(p,1,(uint64_t)rgba|0x3f80000000000000ull);p+=16;
    le64(p,(uint64_t)n|0x2400000000008000ull);le64(p+8,0x55);p+=16;
    for(unsigned i=0;i<n;i++,p+=16) {
        le64(p,((uint64_t)(uint32_t)x+i*32)<<4|(uint64_t)(uint32_t)y<<20);
        le64(p+8,((uint64_t)(uint32_t)x+(i+1)*32)<<4|(uint64_t)(uint32_t)(y+h)<<20);
    }
}

/* ---- 0.25: producers whose callers write the packet themselves ---------- */
static int note(unsigned char *p,size_t bytes,DrDmaKind kind)
{
    if(dma.npending>=PENDING_MAX)return fail(DR_DMA_FULL);
    dma.pending[dma.npending++]=(Pending){(uint32_t)(p-dma.buffer[dma.current]),(uint32_t)bytes,(uint8_t)kind};
    return 0;
}
void *dr_dma_alloc_kind(int32_t size,DrDmaKind kind)
{
    if(ready())return size>=0 && (size_t)size<=BYTES?dma.sink:NULL;
    if(size<0 || (unsigned)kind>DR_DMA_W64) {fail(DR_DMA_INVALID);return NULL;}
    if(dma.direct_active){fail(DR_DMA_STATE);return dma.sink;}
    size_t n=(size_t)size/16*16;
    if(!n)return dma.buffer[dma.current]+dma.used[dma.current]; /* original: no advance */
    unsigned char *p=reserve(n);
    if(!p)return n<=BYTES?dma.sink:NULL; /* sticky error; caller writes go to a sink */
    memset(p,0,n);
    if(note(p,n,kind))return dma.sink;
    return p;
}
void *Dma_Alloc(int32_t size) {return dr_dma_alloc_kind(size,DR_DMA_W32);}
unsigned long long *Dma_BeginDirect(void)
{
    if(ready())return (unsigned long long *)(void *)(dma.sink?dma.sink+16:NULL);
    if(dma.direct_active){fail(DR_DMA_STATE);return (void *)(dma.sink+16);}
    if(BYTES-TAIL-dma.used[dma.current]<16+DIRECT_MAX) {fail(DR_DMA_FULL);return (unsigned long long *)(void *)(dma.sink+16);}
    unsigned char *tag=dma.buffer[dma.current]+dma.used[dma.current];
    memset(tag,0,16);
    dma.direct_active=1;
    return (unsigned long long *)(void *)(tag+16);
}
/* Preflight for adapted native-u64 GIF writers. This only authorizes writes
 * into an active Direct region or an explicitly registered W64 allocation.
 * It does not register an arbitrary pointer or convert the buffer early. */
int dr_dma_w64_range(void *ptr,size_t bytes)
{
    uintptr_t at=(uintptr_t)ptr;
    if(ready())return dma.error;
    if(!ptr || !bytes || bytes%16 || at%16 || at>UINTPTR_MAX-bytes)
        return fail(DR_DMA_INVALID);
    uintptr_t begin=(uintptr_t)dma.buffer[dma.current]+dma.used[dma.current]+16;
    if(dma.direct_active && at>=begin && at-begin<=DIRECT_MAX &&
       bytes<=DIRECT_MAX-(at-begin))return 0;
    for(unsigned i=0;i<dma.npending;i++) {
        Pending *r=&dma.pending[i];
        uintptr_t base=(uintptr_t)dma.buffer[dma.current]+r->offset;
        if(r->kind==DR_DMA_W64 && at>=base && at-base<=r->bytes &&
           bytes<=r->bytes-(at-base))return 0;
    }
    for(unsigned i=0;i<dma.nmaps;i++) {
        uintptr_t base=(uintptr_t)dma.maps[i].bytes;
        if(dma.maps[i].kind==DR_DMA_W64 && at>=base && at-base<=dma.maps[i].size &&
           bytes<=dma.maps[i].size-(at-base))return 0;
    }
    return fail(DR_DMA_INVALID);
}
int dr_dma_reject_packet(void) {return fail(DR_DMA_INVALID);}
void Dma_EndDirect(unsigned long long *end)
{
    if(!dma.submit && !dma.mapped_submit) {fail(DR_DMA_STATE);return;}
    unsigned char *e=(unsigned char *)(void *)end;
    if(dma.error)return;
    if(!dma.direct_active){fail(DR_DMA_STATE);return;}
    unsigned char *tag=dma.buffer[dma.current]+dma.used[dma.current];
    uintptr_t start=(uintptr_t)tag,at=(uintptr_t)e;
    if(at==start+16){dma.direct_active=0;return;}
    if(at<start || at-start<32 || (at-start)%16 || at-start>16+DIRECT_MAX) {fail(DR_DMA_INVALID);return;}
    uint32_t qwc=(uint32_t)((at-start)/16-1);
    chain_tag(tag,qwc);
    if(note(tag+16,(size_t)qwc*16,DR_DMA_W64))return;
    dma.used[dma.current]+=(size_t)(at-start);dma.direct_active=0;
}
void dr_dma_add_data_kind(const void *src,int32_t size,DrDmaKind kind)
{
    if(ready())return;
    if(size<0 || (unsigned)kind>DR_DMA_W64 || (size>0 && !src)) {fail(DR_DMA_INVALID);return;}
    size_t n=(size_t)size/16*16;
    if(!n)return;
    unsigned char *p=reserve(n);if(!p)return;
    memcpy(p,src,n);convert(p,n,kind);
}
void Dma_AddData(void *src,int32_t size) {dr_dma_add_data_kind(src,size,DR_DMA_W32);}
void dr_dma_add_ref_kind(const void *addr,int32_t size,DrDmaKind kind)
{
    if(ready())return;
    if(size<0 || (unsigned)kind>DR_DMA_W64 || (size>0 && !addr) || size/16>0xFFFF) {fail(DR_DMA_INVALID);return;}
    size_t n=(size_t)size/16*16;
    /* Mapped submissions keep REF payloads out of the command list. Copy at
     * enqueue time preserves this adapter's existing snapshot semantics. A
     * single canonical span is bounded separately; no guest pointer is tagged. */
    if(dma.mapped_submit && n){
        if(n>REF_BYTES-dma.refs_used){fail(DR_DMA_FULL);return;}
        if(!dma.refs){dma.refs=malloc(REF_BYTES);if(!dma.refs){fail(DR_DMA_MEMORY);return;}}
        unsigned char *p=reserve(16);if(!p)return;
        memcpy(dma.refs+dma.refs_used,addr,n);convert(dma.refs+dma.refs_used,n,kind);
        le32(p,0x30000000u|(uint32_t)(n/16));le32(p+4,0x43000000u+(uint32_t)dma.refs_used);le32(p+8,0);le32(p+12,0);
        dma.refs_used+=n;return;
    }
    unsigned char *p=reserve(16+n);if(!p)return;
    le32(p,0x10000000u|(uint32_t)(n/16));le32(p+4,0);le32(p+8,0);le32(p+12,0);
    if(n){memcpy(p+16,addr,n);convert(p+16,n,kind);}
}
static uint32_t read_native_word(const unsigned char *,int);
/* GfxClut_InitPacket ABI: native u32 control headers and byte RGBA image.
 * The ten reviewed callers pass ref at offset 0x494, not arbitrary REF tags. */
int dr_dma_add_clut_ref(const void *reference) {
    if(ready())return dma.error;
    if(!reference)return fail(DR_DMA_INVALID);
    const unsigned char *ref=reference,*base=ref-0x494;int big=must_swap();
    if(read_native_word(ref,big)!=0x30000049u || read_native_word(ref+4,big)!=(uint32_t)(uintptr_t)base || read_native_word(ref+8,big) || read_native_word(ref+12,big))return fail(DR_DMA_INVALID);
    unsigned char packet[0x490] __attribute__((aligned(16)));
    memcpy(packet,base,sizeof(packet));convert(packet,0x70,DR_DMA_W32);convert(packet+0x470,0x20,DR_DMA_W32);
    dr_dma_add_ref_kind(packet,sizeof(packet),DR_DMA_RAW);return dma.error;
}
/* ScrXfade_StoreHalf's fixed 512x224 RGB24 upload. The caller supplies
 * the owned buffer explicitly; a REF pointer is never reconstructed. */
int dr_dma_add_xfade_ref(const void *reference,const void *source,size_t capacity) {
    if(ready())return dma.error;
    enum { IMAGE=512*224*3, PACKET=IMAGE+0x90 };
    if(!reference || !source || capacity<PACKET)return fail(DR_DMA_INVALID);
    const unsigned char *ref=reference,*src=source;int big=must_swap();
    if(read_native_word(ref,big)!=(0x30000000u+IMAGE/16+9) ||
       read_native_word(ref+4,big)!=(uint32_t)(uintptr_t)src ||
       read_native_word(ref+8,big) || read_native_word(ref+12,big) ||
       read_native_word(src,big) || read_native_word(src+4,big) ||
       read_native_word(src+8,big) || read_native_word(src+12,big)!=(0x50000000u+IMAGE/16+8))return fail(DR_DMA_INVALID);
    unsigned char *packet=malloc(PACKET);if(!packet)return fail(DR_DMA_MEMORY);
    memcpy(packet,src,PACKET);
    convert(packet,0x10,DR_DMA_W32);convert(packet+0x10,0x60,DR_DMA_W64);
    convert(packet+0x70+IMAGE,0x20,DR_DMA_W64);
    static const unsigned offsets[]={0x28,0x38,0x48,0x58,0x54088};
    static const unsigned regs[]={0x50,0x51,0x52,0x53,0x3f};
    int valid=1;
    for(unsigned i=0;i<5;i++){
        const unsigned char *at=packet+offsets[i];
        if(read_native_word(at,0)!=regs[i] || read_native_word(at+4,0))valid=0;
    }
    /* Validate every fixed GIF/control word before publishing a snapshot.
     * Image bytes remain untouched. BITBLTBUF destination base is dynamic. */
    if(read_native_word(packet+0x10,0)!=0x8004u || read_native_word(packet+0x14,0)!=0x10000000u ||
       read_native_word(packet+0x18,0)!=14 || read_native_word(packet+0x1c,0) ||
       read_native_word(packet+0x20,0)!=0x01000000u ||
       (read_native_word(packet+0x24,0)&0xffffc000u)!=0x01080000u ||
       read_native_word(packet+0x30,0) || read_native_word(packet+0x34,0) ||
       read_native_word(packet+0x40,0)!=512 || read_native_word(packet+0x44,0)!=224 ||
       read_native_word(packet+0x50,0) || read_native_word(packet+0x54,0) ||
       read_native_word(packet+0x60,0)!=(IMAGE/16|0x8000u) || read_native_word(packet+0x64,0)!=0x08000000u ||
       read_native_word(packet+0x68,0) || read_native_word(packet+0x6c,0) ||
       read_native_word(packet+IMAGE+0x70,0)!=0x8001u || read_native_word(packet+IMAGE+0x74,0)!=0x10000000u ||
       read_native_word(packet+IMAGE+0x78,0)!=14 || read_native_word(packet+IMAGE+0x7c,0) ||
       read_native_word(packet+IMAGE+0x80,0) || read_native_word(packet+IMAGE+0x84,0))valid=0;
    if(!valid){free(packet);return fail(DR_DMA_INVALID);}
    dr_dma_add_ref_kind(packet,PACKET,DR_DMA_RAW);free(packet);return dma.error;
}
/* Explicit immutable inline chain snapshot; caller proves source extent. */
static int snapshot_chain(const void *src,size_t bytes,uint32_t *address,DrDmaKind kind) {
    if(ready())return dma.error;
    if(!dma.mapped_submit || !src || !address || !bytes || bytes%16 || bytes>REF_BYTES-dma.refs_used)return fail(DR_DMA_INVALID);
    if(!dma.refs){dma.refs=malloc(REF_BYTES);if(!dma.refs)return fail(DR_DMA_MEMORY);}
    memcpy(dma.refs+dma.refs_used,src,bytes);convert(dma.refs+dma.refs_used,bytes,kind);
    *address=0x43000000u+(uint32_t)dma.refs_used;dma.refs_used+=bytes;return 0;
}
int dr_dma_snapshot_chain(const void *src,size_t bytes,uint32_t *address){return snapshot_chain(src,bytes,address,DR_DMA_RAW);}
int dr_dma_snapshot_words(const void *src,size_t bytes,uint32_t *address){return snapshot_chain(src,bytes,address,DR_DMA_W32);}
void Dma_AddRef(void *addr,int32_t size) {dr_dma_add_ref_kind(addr,size,DR_DMA_RAW);}

/* Explicit chain address map. No masked native pointer is accepted as a
 * transport address. Other packet writers must use this API themselves. */
static int overlap(uintptr_t a,size_t an,uintptr_t b,size_t bn)
{return a<=b?b-a<an:a-b<bn;}
int dr_dma_map_chain(void *bytes,size_t size,DrDmaKind kind,uint32_t *address)
{
    if(ready())return dma.error;
    if(!dma.mapped_submit)return fail(DR_DMA_STATE);
    if(!bytes || !size || size%16 || !address || (unsigned)kind>DR_DMA_W64 ||
       (uintptr_t)bytes>UINTPTR_MAX-size)return fail(DR_DMA_INVALID);
    if(dma.nmaps==16 || size>0x800000-dma.mapped_bytes)return fail(DR_DMA_FULL);
    for(unsigned i=0;i<2;i++)if(overlap((uintptr_t)bytes,size,(uintptr_t)dma.buffer[i],BYTES))return fail(DR_DMA_INVALID);
    for(unsigned i=0;i<dma.nmaps;i++)if(overlap((uintptr_t)bytes,size,(uintptr_t)dma.maps[i].bytes,dma.maps[i].size))return fail(DR_DMA_INVALID);
    uint32_t base=0x42000000u+(uint32_t)dma.mapped_bytes;
    unsigned m=dma.nmaps++;
    dma.maps[m].bytes=bytes;dma.maps[m].size=size;dma.maps[m].kind=kind;dma.maps[m].address=base;
    dma.mapped_bytes+=size;*address=base;return 0;
}
static int locate(const void *ptr,size_t need,uint32_t *address,DrDmaKind *kind)
{
    uintptr_t p=(uintptr_t)ptr,b=(uintptr_t)dma.buffer[dma.current];
    if(p>=b && p-b<=BYTES && need<=BYTES-(p-b)) {
        *address=0x41000000u+dma.current*BYTES+(uint32_t)(p-b);*kind=DR_DMA_RAW;
        for(unsigned i=0;i<dma.npending;i++) {
            Pending *r=&dma.pending[i];
            if(p-b>=r->offset && p-b-r->offset<=r->bytes && need<=r->bytes-(p-b-r->offset)){*kind=(DrDmaKind)r->kind;break;}
        }
        return 0;
    }
    for(unsigned i=0;i<dma.nmaps;i++) {
        b=(uintptr_t)dma.maps[i].bytes;
        if(p>=b && p-b<=dma.maps[i].size && need<=dma.maps[i].size-(p-b)) {
            *address=dma.maps[i].address+(uint32_t)(p-b);*kind=dma.maps[i].kind;return 0;
        }
    }
    return fail(DR_DMA_INVALID);
}
int dr_dma_chain_address(const void *ptr,uint32_t *address)
{
    DrDmaKind kind;
    if(ready())return dma.error;
    if(!ptr || !address)return fail(DR_DMA_INVALID);
    return locate(ptr,16,address,&kind);
}
static uint32_t read_native_word(const unsigned char *p,int big)
{
    uint32_t v=0;for(unsigned i=0;i<4;i++)v|=(uint32_t)p[i]<<((big?3-i:i)*8);return v;
}
void Dma_AddNext(void *addr,uint32_t *retTag)
{
    uint32_t target,back;DrDmaKind target_kind,return_kind;
    if(ready())return;
    if(!dma.mapped_submit){fail(DR_DMA_STATE);return;}
    if(!addr || !retTag){fail(DR_DMA_INVALID);return;}
    if(locate(addr,16,&target,&target_kind) || locate(retTag,16,&back,&return_kind))return;
    if((target&15) || (back&15) || return_kind==DR_DMA_W64){fail(DR_DMA_INVALID);return;}
    unsigned char *ret=(unsigned char *)(void *)retTag;
    int big=return_kind==DR_DMA_W32 && must_swap();
    if(((read_native_word(ret,big)>>28)&7)!=2){fail(DR_DMA_INVALID);return;}
    unsigned char *tag=reserve(16);if(!tag)return;
    uint32_t resume=0x41000000u+dma.current*BYTES+(uint32_t)dma.used[dma.current];
    le32(tag,0x20000000);le32(tag+4,target);le32(tag+8,0);le32(tag+12,0);
    for(unsigned i=0;i<4;i++)ret[4+i]=(unsigned char)(resume>>((big?3-i:i)*8));
}

/* Effect IDs travel in the ordered GIF stream, as in upstream gs_marker.c. */
void Port_GsMarker(int effect)
{ (void)dr_dma_write_ad(0x7f,(uint32_t)effect); }
