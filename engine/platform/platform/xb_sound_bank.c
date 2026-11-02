/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Bounded bank header transport for original Snd_SetupBank. Based on the
 * public decomp's sys/snd.h and the existing native bank sampler contract.
 */
#include "xb_sound_bank.h"
#include "xb_resconv.h"
#include <string.h>
enum {BAD=-710,BOUNDS=-711,FORMAT=-712};
static uint32_t le(const uint8_t *p){return XbRes_Le32(p);}
static uint32_t head(const uint8_t *p,int be){return be?((uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]):le(p);}
static unsigned half(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
static const uint8_t *chunk(const uint8_t *p,size_t n,const char *tag,size_t minimum,size_t *available){
    if(n<minimum)return NULL;
    for(size_t at=0;at<=n-minimum;at+=4)if(!memcmp(p+at,tag,8)){*available=n-at;return p+at;}
    return NULL;
}
int XbSoundBank_Inspect(const void *source,size_t n,unsigned mask,int be,XbSoundBankInfo *info){
    if(!source || !info || n<20 || (mask && mask!=1 && mask!=4 && mask!=8 && mask!=16 && mask!=32))return BAD;
    const uint8_t *p=source;XbSoundBankInfo value={0};uint32_t offsets[4];
    if(head(p,be)!=3)return FORMAT;
    for(unsigned i=0;i<4;i++){
        offsets[i]=head(p+4+i*4,be);
        if((offsets[i]&3) || offsets[i]>n || (i && offsets[i]<=offsets[i-1]))return BOUNDS;
    }
    if(offsets[0]<20)return BOUNDS;
    size_t body=offsets[1]-offsets[0],hd=offsets[2]-offsets[1],seq=offsets[3]-offsets[2];
    size_t bm=(mask==16 || mask==32)?0x8f000:mask==8?0x13800:0xa4800;
    size_t hm=(mask==16 || mask==32)?0x1000:mask==8?0x800:0x20000;
    size_t sm=(mask==8 || mask==16 || mask==32)?0x800:0x20000;
    if(!body || body%16 || body>bm || hd>hm || seq>sm)return BOUNDS;
    size_t vn=0,sn=0,tn=0;
    const uint8_t *v=chunk(p+offsets[1],hd,"IECSigaV",16,&vn);
    const uint8_t *s=chunk(p+offsets[2],seq,"IECSqseS",0x34,&sn);
    const uint8_t *t=chunk(p+offsets[1],hd,"IECSbteS",0x24,&tn);
    if(!v || !s || !t || le(v+12)>=128 || 16+(uint64_t)(le(v+12)+1)*4>vn)return FORMAT;
    value.max_sample=le(v+12);
    for(unsigned i=0;i<=value.max_sample;i++){
        uint32_t at=le(v+16+i*4);
        if(at>vn || vn-at<8)return BOUNDS;
        uint32_t offset=le(v+at);unsigned rate=half(v+at+4);
        if(offset>=body || body-offset<16 || offset%16 || !rate || rate>48000 || v[at+6]>1)return FORMAT;
        value.loop[i]=v[at+6];
    }
    /* The implemented sampler requires a supported first sequence/timbre.
     * This does not claim that every sequencer program is playable. */
    if(sn<0x38)return BOUNDS;
    uint32_t at=le(s+0x34);unsigned stride=half(t+0x20),max=half(t+0x22);
    if(at>sn || sn-at<0x15 || s[at+0x11]!=0x90 || stride<2 || s[at+0x13]>max)return FORMAT;
    uint64_t sample=0x24+(uint64_t)s[at+0x13]*stride;
    if(sample+2>tn || half(t+(size_t)sample)>value.max_sample)return FORMAT;
    value.body=offsets[0];value.hd=offsets[1];value.sequence=offsets[2];value.end=offsets[3];
    *info=value;return 0;
}
int XbSoundBank_Prepare(void *source,size_t n,unsigned mask,int be){
    XbSoundBankInfo info;if(!mask)return BAD;
    int e=XbSoundBank_Inspect(source,n,mask,0,&info);if(e)return e;
    if(be)XbRes_Swap32(source,5);
    return 0;
}
static int initial_bank(void *p,size_t n,int id){
    unsigned mask=id==0x14b?1:id==0x14a?4:id==0x14e?8:(id==0xbda || id==0xc7b)?16:(id==0xbdb || id==0xc7c)?32:0;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__==__ORDER_BIG_ENDIAN__
    return XbSoundBank_Prepare(p,n,mask,1);
#else
    return XbSoundBank_Prepare(p,n,mask,0);
#endif
}
int XbSoundBank_RegisterCommon(void){return XbRes_RegisterChecked(0x14b,0x14b,initial_bank,0);}
int XbSoundBank_RegisterInitial(void){
    static const XbResCheckedRange ranges[]={
        {0x14a,0x14a,initial_bank,0},{0x14e,0x14e,initial_bank,0},
        {0xbda,0xbda,initial_bank,0},{0xbdb,0xbdb,initial_bank,0},
        {0xc7b,0xc7b,initial_bank,0},{0xc7c,0xc7c,initial_bank,0}
    };
    return XbRes_RegisterCheckedBatch(ranges,6);
}
static int native_info(const void *p,XbSoundBankInfo *info){
    size_t n=XbRes_SourceSize(p);if(!n)return 0;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__==__ORDER_BIG_ENDIAN__
    return XbSoundBank_Inspect(p,n,0,1,info)==0;
#else
    return XbSoundBank_Inspect(p,n,0,0,info)==0;
#endif
}
int XbSoundBank_TryVagMax(const void *p,int *out){
    XbSoundBankInfo info;if(!out || !native_info(p,&info))return 0;
    *out=(int)info.max_sample;return 1;
}
int XbSoundBank_TryLoopFlags(const void *p,void *out){
    XbSoundBankInfo info;if(!out || !native_info(p,&info))return 0;
    memcpy(out,info.loop,info.max_sample+1);return 1;
}
int XbSoundBank_RequireReadBank(const void *p,unsigned mask){
    if(!XbRes_CheckedReadsActive())return 0;
    if(XbRes_LastError())return XbRes_LastError();
    XbSoundBankInfo info;size_t n=XbRes_SourceSize(p);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__==__ORDER_BIG_ENDIAN__
    int e=XbSoundBank_Inspect(p,n,mask,1,&info);
#else
    int e=XbSoundBank_Inspect(p,n,mask,0,&info);
#endif
    if(e)XbRes_SetReadError(e);
    return e;
}
