/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "port_compat.h"
#undef malloc
#include "xb_model_native.h"
#include "dr_container.h"
#include "dr_body_model.h"
#include "dr_face_vif.h"
#include "dr_texfile.h"
#include "sys/gfxm_e.h"
#include "battle/bobj_b.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#if UINTPTR_MAX==UINT32_MAX
_Static_assert(sizeof(TexEntry)==0x40 && sizeof(TexFile)==0x14,"native texture ABI");
_Static_assert(sizeof(ObjMdlObj)==0x1670 && sizeof(BObj)==0x1670,"native object views");
_Static_assert(offsetof(ObjMdlObj,parts)==0xd60 && offsetof(BObj,face)==0x1664,"native object node/face offsets");
#endif
struct XbNativeModel {
 unsigned char *model;size_t model_size;DrBodyPart part[256];unsigned count;int bound;
 TexFile *tex[7];void *face[8],*lip[100];size_t face_bytes[8];unsigned char chain[0x350];
};
static uint32_t rd(const void *p){return dr_resource_le32(p);}
static void scalar(void *out,const void *p,unsigned width){
 uint64_t v=0;for(unsigned i=0;i<width;i++)v|=(uint64_t)((const unsigned char *)p)[i]<<(8*i);
 if(width==2){uint16_t n=(uint16_t)v;memcpy(out,&n,2);}else if(width==4){uint32_t n=(uint32_t)v;memcpy(out,&n,4);}else memcpy(out,&v,8);
}
static void fields(void *dst,const void *src,unsigned a,unsigned z,unsigned w){for(unsigned i=a;i<z;i+=w)scalar((unsigned char *)dst+i,(const unsigned char *)src+i,w);}
static void wr(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
typedef struct Rebase {unsigned char *chain;uint32_t min_tbp,min_cbp;int phase,single;} Rebase;
static int rebase_visit(void *ctx,const DrFaceVifCommand *c){
 Rebase *r=ctx;if(c->code!=0x6c058000)return 0;
 if(c->data_bytes<40)return -870;
 unsigned char *p=r->chain+c->code_offset;uint32_t a=rd(p+20),b=rd(p+24),d=rd(p+36),e=rd(p+40);
 uint32_t tbp=a&0x3fff,cbp=(b>>5)&0x3fff;
 if(!r->phase){if(tbp<r->min_tbp)r->min_tbp=tbp;if(cbp<r->min_cbp)r->min_cbp=cbp;return 0;}
 if(tbp<r->min_tbp || cbp<r->min_cbp)return -870;
 uint32_t nt=tbp-r->min_tbp+0x3480,nc=cbp-r->min_cbp+0x3c00,nc2=((e>>5)&0x3fff)+0x3d00;
 if(nt>0x3fff || nc>0x3fff || nc2>0x3fff)return -870;
 if(r->phase==2){wr(p+20,(a&~0x3fffu)|nt);wr(p+24,(b&0xfff8001fu)|(nc<<5));wr(p+36,(d&~0x3fffu)|0x3d40);wr(p+40,(e&0xfff8001fu)|(nc2<<5));}
 return 0;
}
static int visit(Rebase *r,size_t n){size_t bad;return dr_face_vif_parse(r->chain,n,rebase_visit,r,NULL,&bad);}
static int texture(const void *src,size_t size,TexFile **out){
 if(size<32 || size>16*1024*1024)return -870;
 unsigned count=rd(src),off=rd((const unsigned char *)src+4)*4u;
 if(!count || count>256 || off<32 || off>size || count>(size-off)/64)return -870;
 /* Native consumers use bounded upload packets; decoding is a separate check. */
 for(unsigned i=0;i<count;i++){int e=dr_texfile_validate_transfers(src,size,i);if(e)return e;}
 unsigned char *copy=malloc(size);if(!copy)return -851;memcpy(copy,src,size);
 fields(copy,src,0,8,4);TexFile *f=(TexFile *)copy;f->ent=(TexEntry *)(copy+off);
 for(unsigned i=0;i<count;i++){
  const unsigned char *s=(const unsigned char *)src+off+i*64;TexEntry *e=&f->ent[i];
  fields(e,s,0,0x28,4);scalar(&e->tex0,s+0x30,8);e->pix=e->pixOfs?copy+(size_t)e->pixOfs*4:NULL;e->clut=e->clutOfs?copy+(size_t)e->clutOfs*4:NULL;
  if(e->unk28[4]>=16){free(copy);return -870;}
 }
 *out=f;return 0;
}
void XbNativeModel_Free(XbNativeModel *m){if(!m)return;for(unsigned i=0;i<7;i++)free(m->tex[i]);for(unsigned i=0;i<8;i++)free(m->face[i]);for(unsigned i=0;i<100;i++)free(m->lip[i]);free(m->model);free(m);}
unsigned XbNativeModel_Parts(XbNativeModel *m){return m?m->count:0;}
int XbNativeModel_Prepare(const void *pack,size_t size,XbNativeModel **out){
 if(!out)return -870;
 *out=NULL;
 if(sizeof(void *)!=4 || !pack || size>16*1024*1024)return -870;
 XbNativeModel *m=calloc(1,sizeof(*m));if(!m)return -851;int e;size_t off,n,bad;
 e=dr_pack_memory_entry(pack,size,2,&off,&n,NULL);if(e)goto fail;
 DrBodyModel model;e=dr_body_model_read((const unsigned char *)pack+off,n,m->part,256,&model,&bad);if(e)goto fail;
 m->model=malloc(n);if(!m->model){e=-851;goto fail;}memcpy(m->model,(const unsigned char *)pack+off,n);m->model_size=n;m->count=model.parts;
 const unsigned char *raw=(const unsigned char *)pack+off;
 scalar(m->model+12,raw+12,4);fields(m->model,raw,0x18,0x2c,4);fields(m->model,raw,0x58,0x70,4);
 Rebase rb={.min_tbp=0x7fffffff,.min_cbp=0x7fffffff};
 for(unsigned phase=0;phase<3;phase++)for(unsigned i=0;i<m->count;i++){
  DrBodyPart *p=&m->part[i];if(!p->enabled || !p->chain_bytes)continue;
  rb.phase=phase;rb.chain=m->model+p->chain_offset;e=visit(&rb,p->chain_bytes);if(e)goto fail;
 }
 for(unsigned i=0;i<m->count;i++){
  DrBodyPart *p=&m->part[i];unsigned char *dst=m->model+p->record_offset;const unsigned char *src=raw+p->record_offset;
  fields(dst,src,0,4,4);fields(dst,src,4,12,2);fields(dst,src,12,64,4);if(p->record_bytes>64)fields(dst,src,64,96,4);
 }
 static const unsigned groups[]={0,11,12,13,14,15,16};
 for(unsigned i=0;i<7;i++){
  e=dr_pack_memory_entry(pack,size,groups[i],&off,&n,NULL);if(e==DR_NOT_FOUND){e=0;continue;}if(e)goto fail;
  e=texture((const unsigned char *)pack+off,n,&m->tex[i]);if(e)goto fail;
 }
 if(!m->tex[1]){e=-870;goto fail;}
 ObjMdlFile *hdr=(ObjMdlFile *)m->model;
 if(m->model[0x4c]>=m->tex[1]->count){e=-870;goto fail;}
 for(unsigned i=0;i<m->tex[1]->count;i++)if(i>=hdr->texFirst && i<(unsigned)hdr->texFirst+hdr->texCount && (hdr->texNoAlpha==0 || i!=hdr->texNoAlpha)){
  TexEntry *t=&m->tex[1]->ent[i];if(!t->clut || t->clutSize<160){e=-870;goto fail;}
 }
 for(unsigned i=2;i<7;i++)if(m->tex[i])for(unsigned j=0;j<m->tex[i]->count;j++)if(!m->tex[i]->ent[j].clut || m->tex[i]->ent[j].clutSize<160){e=-870;goto fail;}
 for(unsigned i=0;i<8;i++){
  e=dr_pack_memory_entry(pack,size,3+i,&off,&n,NULL);if(e==DR_NOT_FOUND){e=0;continue;}if(e)goto fail;
  m->face[i]=malloc(n);if(!m->face[i]){e=-851;goto fail;}memcpy(m->face[i],(const unsigned char *)pack+off,n);m->face_bytes[i]=n;
  Rebase r={.chain=m->face[i],.min_tbp=0,.min_cbp=0,.single=1};for(r.phase=1;r.phase<3;r.phase++){e=visit(&r,n);if(e)goto fail;}
 }
 e=dr_pack_memory_entry(pack,size,22,&off,&n,NULL);if(e || n<sizeof(m->chain)){e=-870;goto fail;}
 const unsigned char *chain=(const unsigned char *)pack+off;memcpy(m->chain,chain,sizeof(m->chain));
 fields(m->chain,chain,0x10,0xd0,4);fields(m->chain,chain,0xe0,0x100,2);fields(m->chain,chain,0x110,0x190,4);fields(m->chain,chain,0x1a0,0x1c0,2);
 fields(m->chain,chain,0x1d0,0x250,4);fields(m->chain,chain,0x260,0x2e0,4);fields(m->chain,chain,0x300,0x340,4);
 for(unsigned i=0;i<100;i++){
  e=dr_pack_memory_entry(pack,size,52+i,&off,&n,NULL);if(e==DR_NOT_FOUND){e=0;continue;}if(e)goto fail;
  const unsigned char *s=(const unsigned char *)pack+off;if(n<16 || memcmp(s,"LIPS",4) || rd(s+4)!=2){e=-870;goto fail;}
  unsigned count=(unsigned)s[8]|(unsigned)s[9]<<8;if(!count || count>(n-16)/4){e=-870;goto fail;}
  unsigned prev=0;for(unsigned j=0;j<count;j++){unsigned frame=(unsigned)s[16+j*4]|(unsigned)s[17+j*4]<<8;if(j && frame<=prev){e=-870;goto fail;}prev=frame;}
  m->lip[i]=malloc(n);if(!m->lip[i]){e=-851;goto fail;}memcpy(m->lip[i],s,n);scalar((unsigned char *)m->lip[i]+8,s+8,2);fields(m->lip[i],s,16,16+count*4,2);
 }
 *out=m;return 0;
fail:XbNativeModel_Free(m);return e;
}
void XbNativeModel_BindTables(XbNativeModel *m,void *object,const void *template_mdl,size_t bytes){
 BObj *obj=object;memcpy(&obj->mdl,template_mdl,bytes);obj->mdl.unk00=m->model;obj->mdl.unk04=m->tex[1];obj->mdl.unk38=m->tex[0];
 obj->mdl.model=(BObjModel *)m->model;obj->mdl.bounds=(BObjBound *)(m->model+((ObjMdlFile *)m->model)->meshOfs);
 obj->mdl.tex=(BObjTexTable *)m->tex[1];obj->mdl.eyes=(BObjTexTable *)m->tex[2];
 for(unsigned i=0;i<8;i++){obj->mdl.mouthA[i]=m->face[i];obj->mdl.mouthB[i]=i<4?NULL:(BObjTexTable *)m->tex[i-1];}
 for(unsigned i=0;i<100;i++)obj->mdl.lip[i]=m->lip[i];
 obj->mdl.chain=(BObjChainParam *)m->chain;
}
extern void BtlObjMdl_InitAlphaSlots(void *,ObjMdl *,s32);
extern s32 BtlObjVis_AllocFreeBit(void);
extern s32 BtlObjVis_AllocBit(s32);
void XbNativeModel_BindFiles(XbNativeModel *m,void *object){
 ObjMdlObj *obj=object;ObjMdl *mdl=&obj->mdl;ObjMdlFile *hdr=(ObjMdlFile *)m->model;
 hdr->tbp=0x3480;hdr->cbp=0x3c00;hdr->cbp2=0x3d00;hdr->tbp2=0x3d40;hdr->unk68=0x3e40;
 if(!m->bound)hdr->alphaRow=BtlObjVis_AllocFreeBit();else BtlObjVis_AllocBit(hdr->alphaRow);
 mdl->model=hdr;mdl->meshes=(ObjMdlMesh *)(m->model+hdr->meshOfs);mdl->tex=m->tex[1];
 if(!m->bound){BtlObjMdl_InitAlphaSlots(obj->res,mdl,hdr->alphaRow);m->bound=1;}
}

int XbNativeModel_Mesh(XbNativeModel *m,unsigned i,const void **vif,size_t *bytes){
 if(!m || i>=m->count || !vif || !bytes)return -870;
 DrBodyPart *p=&m->part[i];*vif=p->chain_bytes?m->model+p->chain_offset:NULL;*bytes=p->chain_bytes;return 0;
}

int XbNativeTexture_Prepare(const void *bytes,size_t size,void **out){
 if(!out)return -870;
 *out=NULL;if(sizeof(void *)!=4 || !bytes)return -870;
 TexFile *file=NULL;int e=texture(bytes,size,&file);if(!e)*out=file;return e;
}
int XbNativeModel_ChainSpan(XbNativeModel *m,const void *ptr,size_t *bytes){
 if(bytes)*bytes=0;
 if(!m || !ptr || !bytes || ((uintptr_t)ptr&15))return 0;
 for(unsigned i=0;i<m->count;i++){
  DrBodyPart *p=&m->part[i];
  if(p->chain_bytes && p->chain_offset<=m->model_size && p->chain_bytes<=m->model_size-p->chain_offset &&
     ptr==m->model+p->chain_offset){*bytes=p->chain_bytes;return 1;}
 }
 for(unsigned i=0;i<8;i++)if(ptr==m->face[i] && m->face_bytes[i]){*bytes=m->face_bytes[i];return 1;}
 return 0;
}
void XbNativeTexture_Free(void *file){free(file);}
void *XbNativeModel_Header(XbNativeModel *model){return model?model->model:NULL;}
