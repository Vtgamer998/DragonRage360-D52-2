/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Native external bank header; HD/sequence/ADPCM payload stays canonical LE.
 * Keep prepared banks alive through Snd_UnloadBank and deferred uploads. */
static int lookup(const Bank *,unsigned,uint32_t *,uint32_t *,uint32_t *,int);
struct XbSoundBank {uint8_t *data,loop[128];int max;};
static XbSoundBank *prepared_banks[8];
int XbSfx_PrepareBank(const void *source,size_t n,int mask,XbSoundBank **out){
 if(!out)return BAD;
 *out=NULL;if(!source || n<20 || mask<=0 || mask>255 || (mask&(mask-1)))return BAD;
 unsigned place=8;for(unsigned i=0;i<8;i++)if(!prepared_banks[i]){place=i;break;}if(place==8)return MEMORY;
 const uint8_t *p=source;uint32_t o[4];for(unsigned i=0;i<4;i++)o[i]=le32(p+4+i*4);
 if(le32(p)!=3 || o[0]<20 || o[3]>n)return FORMAT;
 for(unsigned i=0;i<4;i++)if((o[i]&3) || (i && o[i]<=o[i-1]))return FORMAT;
 size_t body=o[1]-o[0],hd=o[2]-o[1],seq=o[3]-o[2];
 size_t bm=(mask==16 || mask==32)?0x8f000:mask==8?0x13800:0xa4800;
 size_t hm=(mask==16 || mask==32)?0x1000:mask==8?0x800:0x20000;
 size_t sm=(mask==8 || mask==16 || mask==32)?0x800:0x20000;
 if(!body || body%16 || body>bm || hd>hm || seq>sm)return BOUNDS;
 size_t vn;const uint8_t *v=chunk(p+o[1],hd,"IECSigaV",16,&vn);
 if(!v || le32(v+12)>=128 || 16+(uint64_t)(le32(v+12)+1)*4>vn)return FORMAT;
 XbSoundBank *b=calloc(1,sizeof(*b));if(!b)return MEMORY;b->max=(int)le32(v+12);
 for(int i=0;i<=b->max;i++){
  uint32_t at=le32(v+16+i*4);if(at>vn || vn-at<8){free(b);return BOUNDS;}
  uint32_t offset=le32(v+at),rate=le16(v+at+4);
  if(offset>=body || body-offset<16 || offset%16 || !rate || rate>48000 || v[at+6]>1){free(b);return FORMAT;}
  b->loop[i]=v[at+6];
 }
 Bank view={.hd=(uint8_t *)p+o[1],.hn=hd,.sq=(uint8_t *)p+o[2],.sn=seq,.spu=0,.spu_end=(uint32_t)body};
 uint32_t a,z,r;int e=lookup(&view,0,&a,&z,&r,0);if(e){free(b);return e;}
 b->data=malloc(o[3]);if(!b->data){free(b);return MEMORY;}memcpy(b->data,p,o[3]);
 for(unsigned i=0;i<5;i++){uint32_t value=le32(p+4*i);memcpy(b->data+4*i,&value,4);}
 prepared_banks[place]=b;*out=b;return 0;
}
void *XbSfx_BankData(XbSoundBank *b){return b?b->data:NULL;}
void XbSfx_FreeBank(XbSoundBank *b){if(!b)return;for(unsigned i=0;i<8;i++)if(prepared_banks[i]==b){prepared_banks[i]=NULL;free(b->data);free(b);return;}}
int XbSfx_TryVagMax(const void *p,int *out){for(unsigned i=0;i<8;i++)if(prepared_banks[i] && prepared_banks[i]->data==p){*out=prepared_banks[i]->max;return 1;}return 0;}
int XbSfx_TryLoopFlags(const void *p,void *out){for(unsigned i=0;i<8;i++)if(prepared_banks[i] && prepared_banks[i]->data==p){memcpy(out,prepared_banks[i]->loop,(size_t)prepared_banks[i]->max+1);return 1;}return 0;}
