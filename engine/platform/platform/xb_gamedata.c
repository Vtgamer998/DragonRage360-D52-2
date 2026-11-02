/* SPDX-License-Identifier: MIT
 * Checked loading of typed native data runs from the user's PS2 programs.
 * No native pointer is taken from the external manifest. All failure checks
 * happen before commit; this module never attempts to execute the game.
 */
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xb_gamedata.h"
#ifdef XB_HOST_TEST
#define XB_LOG(...) (fprintf(stderr,__VA_ARGS__),fputc('\n',stderr))
#else
#include "xb_disc.h"
#include "xsdk/log.h"
#define XB_LOG(...) xsdk_log_write(__VA_ARGS__)
#endif
#define ROM_BASE 0x100000u
#define ROM_END 0x2ff180u
#define MAX_FILE 0x4000000u
#define MAX_RECORDS 65536u
#define MAX_TARGETS 64u
#define MAX_SECTIONS 512u
volatile uint32_t gPortDataStripped=2;
static XbDataTarget targets[MAX_TARGETS];
static size_t target_count;
static int loading;
typedef struct Cover {uint32_t off,size;} Cover;
typedef struct Run {uint8_t *dst;const uint8_t *src;uint32_t length,width,address;} Run;
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint16_t le16(const uint8_t *p){return (uint16_t)(p[0]|p[1]<<8);}
static uint64_t le64(const uint8_t *p){return (uint64_t)le32(p)|(uint64_t)le32(p+4)<<32;}
static int overlap(uintptr_t a,size_t an,uintptr_t b,size_t bn){return a<=b?b-a<an:a-b<bn;}
int XbGameData_SetTargets(const XbDataTarget *maps,size_t count)
{
    if(loading)return XBGD_STATE;
    if(!maps || !count || count>MAX_TARGETS)return XBGD_BAD_TARGET;
    for(size_t i=0;i<count;i++) {
        if(!maps[i].bytes || !maps[i].size || maps[i].size>0x100000000ull-maps[i].address ||
           (uintptr_t)maps[i].bytes>UINTPTR_MAX-maps[i].size)return XBGD_BAD_TARGET;
        for(size_t j=0;j<i;j++)if(overlap(maps[i].address,maps[i].size,maps[j].address,maps[j].size) ||
            overlap((uintptr_t)maps[i].bytes,maps[i].size,(uintptr_t)maps[j].bytes,maps[j].size))return XBGD_BAD_TARGET;
    }
    memcpy(targets,maps,count*sizeof(*maps));target_count=count;return XBGD_OK;
}
int XbGameData_ClearTargets(void)
{if(loading)return XBGD_STATE;memset(targets,0,sizeof(targets));target_count=0;return XBGD_OK;}
static uint8_t *destination(uint32_t address,uint32_t length)
{
    for(size_t i=0;i<target_count;i++)if(address>=targets[i].address) {
        size_t at=address-targets[i].address;
        if(at<=targets[i].size && length<=targets[i].size-at)return (uint8_t *)targets[i].bytes+at;
    }
    return NULL;
}
/* Require a real 32-bit LE MIPS executable and complete section headers.
 * The bitmap-free coverage list distinguishes actual file bytes from holes;
 * even zero-valued holes cannot be silently used as table values. */
static int flat_image(const uint8_t *elf,size_t size,uint8_t **image,Cover *cover,size_t *count)
{
    if(!elf || size<52 || size>MAX_FILE || memcmp(elf,"\177ELF\1\1\1",7) ||
       le16(elf+16)!=2 || le16(elf+18)!=8 || le32(elf+20)!=1 || le16(elf+40)!=52 ||
       le16(elf+46)!=40)return XBGD_NO_ELF;
    uint32_t shoff=le32(elf+32);uint32_t n=le16(elf+48);
    if(!n || n>MAX_SECTIONS || shoff<52 || shoff>size || n>(size-shoff)/40)return XBGD_NO_ELF;
    *count=0;
    for(uint32_t i=0;i<n;i++) {
        const uint8_t *sh=elf+shoff+i*40;
        uint32_t type=le32(sh+4),flags=le32(sh+8),addr=le32(sh+12),off=le32(sh+16),len=le32(sh+20);
        if(!(flags&2) || type==8 || !len)continue;
        if((uint64_t)addr+len>0x100000000ull || off>size || len>size-off)return XBGD_NO_ELF;
        if(addr>=ROM_END || (uint64_t)addr+len<=ROM_BASE)continue;
        if(addr<ROM_BASE || len>ROM_END-addr)return XBGD_NO_ELF;
        for(size_t j=0;j<*count;j++)if(overlap(addr-ROM_BASE,len,cover[j].off,cover[j].size))return XBGD_NO_ELF;
        cover[*count]=(Cover){addr-ROM_BASE,len};(*count)++;
    }
    if(!*count)return XBGD_NO_ELF;
    *image=calloc(1,ROM_END-ROM_BASE);if(!*image)return XBGD_MEMORY;
    for(uint32_t i=0;i<n;i++) {
        const uint8_t *sh=elf+shoff+i*40;
        uint32_t addr=le32(sh+12),len=le32(sh+20);
        if((le32(sh+8)&2) && le32(sh+4)!=8 && len && addr>=ROM_BASE && addr<ROM_END)
            memcpy(*image+addr-ROM_BASE,elf+le32(sh+16),len);
    }
    return XBGD_OK;
}
static int covered(const Cover *cover,size_t count,uint32_t off,uint32_t length)
{
    uint64_t end=(uint64_t)off+length,at=off;
    while(at<end) {
        uint64_t next=at;
        for(size_t i=0;i<count;i++)if(at>=cover[i].off && at<(uint64_t)cover[i].off+cover[i].size)
            next=(uint64_t)cover[i].off+cover[i].size;
        if(next==at)return 0;
        at=next;
    }
    return 1;
}
static int by_address(const void *a,const void *b)
{uint32_t x=((const Run *)a)->address,y=((const Run *)b)->address;return (x>y)-(x<y);}
int XbGameData_Load(const XbDataFiles *files,const uint8_t *list,size_t listSize,long bias,int allowModified)
{
    if(loading)return XBGD_STATE;
    if(!files || !files->read || !list || listSize<16 || memcmp(list,"BT3X",4))return XBGD_BAD_LIST;
    uint32_t count=le32(list+4);uint64_t want=le64(list+8),hash=0xcbf29ce484222325ull;
    if(!count || count>MAX_RECORDS || (listSize-16)/20!=count || (listSize-16)%20)return XBGD_BAD_LIST;
#if LONG_MAX > 0xffffffffLL
    if(bias<-(int64_t)UINT32_MAX || bias>(int64_t)UINT32_MAX)return XBGD_BAD_TARGET;
#endif
    if(!target_count)return XBGD_NO_TARGETS;
    loading=1;
    uint8_t *elf=NULL,*src[2]={NULL,NULL};size_t elfSize=0,sourceSize[2]={ROM_END-ROM_BASE,0},nc=0;
    Cover *cover=calloc(MAX_SECTIONS,sizeof(*cover));Run *runs=calloc(count,sizeof(*runs));
    int rc=XBGD_OK;
    if(!cover || !runs){rc=XBGD_MEMORY;goto done;}
    if(files->read(files->ctx,"SLUS_216.78",&elf,&elfSize) || !elf){rc=XBGD_NO_ELF;goto done;}
    rc=flat_image(elf,elfSize,&src[0],cover,&nc);if(rc)goto done;
    free(elf);elf=NULL;
    if(files->read(files->ctx,"DBZP.BIN",&src[1],&sourceSize[1]) || !src[1] || !sourceSize[1] || sourceSize[1]>MAX_FILE){rc=XBGD_NO_DBZP;goto done;}
    /* Decode into private Run records before any writes, so a manifest
     * aliased with a registered destination cannot change later addresses. */
    for(uint32_t i=0;i<count;i++) {
        const uint8_t *r=list+16+(size_t)i*20;
        uint32_t addr=le32(r),which=le32(r+4),off=le32(r+8),len=le32(r+12),width=le32(r+16);
        if(which>1 || !len || off>sourceSize[which] || len>sourceSize[which]-off ||
           !(width==1 || width==2 || width==4 || width==8) || len%width || off%width ||
           (which==0 && !covered(cover,nc,off,len))){rc=XBGD_BAD_RECORD;goto done;}
        int64_t adjusted=(int64_t)addr+(int64_t)bias;
        if(adjusted<0 || adjusted>UINT32_MAX || (uint64_t)adjusted+len>0x100000000ull ||
           (uint64_t)adjusted%width){rc=XBGD_BAD_TARGET;goto done;}
        uint8_t *dst=destination((uint32_t)adjusted,len);
        if(!dst){rc=XBGD_BAD_TARGET;goto done;}
        runs[i]=(Run){dst,src[which]+off,len,width,(uint32_t)adjusted};
        for(uint32_t k=0;k<len;k++)hash=(hash^runs[i].src[k])*0x100000001b3ull;
    }
    qsort(runs,count,sizeof(*runs),by_address);
    for(uint32_t i=1;i<count;i++)if((uint64_t)runs[i-1].address+runs[i-1].length>runs[i].address){rc=XBGD_OVERLAP;goto done;}
    if(hash!=want && !allowModified){rc=XBGD_HASH;goto done;}
    if(hash!=want)XB_LOG("gamedata: explicitly accepting modified source checksum");
    for(uint32_t i=0;i<count;i++) {
        Run *r=&runs[i];
        for(uint32_t at=0;at<r->length;at+=r->width)
            for(uint32_t k=0;k<r->width;k++)r->dst[at+k]=r->src[at+r->width-1-k];
    }
    XB_LOG("gamedata: committed %u checked data runs; no game started",(unsigned)count);
done:
    if(rc)XB_LOG("gamedata: rejected with error %d; destinations untouched",rc);
    free(elf);free(src[0]);free(src[1]);free(cover);free(runs);loading=0;return rc;
}
#ifndef XB_HOST_TEST
static int disc_read(void *ctx,const char *name,uint8_t **buf,size_t *size)
{
    char rel[64];dr_span span;(void)ctx;
    *buf=NULL;*size=0;
    snprintf(rel,sizeof(rel),strcmp(name,"DBZP.BIN")==0?"disc/BIN/%s":"disc/%s",name);
    if(XbDisc_FindFile(rel,&span)!=DR_OK || !span.size || span.size>MAX_FILE)return -1;
    *buf=malloc((size_t)span.size);if(!*buf)return -1;
    if(XbDisc_ReadSpan(&span,0,*buf,(size_t)span.size)!=DR_OK){free(*buf);*buf=NULL;return -1;}
    *size=(size_t)span.size;return 0;
}
int XbGameData_LoadFromDisc(const char *listPath,long bias,int allowModified)
{
    XbDataFiles files={disc_read,NULL};uint8_t *list=NULL;size_t size=0;
    if(!listPath || XbDisc_ReadHostFile(listPath,&list,&size)!=DR_OK)return XBGD_NO_LIST;
    int rc=XbGameData_Load(&files,list,size,bias,allowModified);free(list);return rc;
}
#endif

#include "xb_typed_data.h"
static int typed_loading;
int XbTypedData_RequireBattleData(void){return gPortDataStripped?XBTD_INCOMPLETE:0;}
int XbTypedData_Load(const XbDataFiles *files,const XbTypedCatalog *c,unsigned flags)
{
    if(typed_loading || loading)return XBTD_STATE;
    if(!files || !files->read || !c || c->version!=1 || c->complete>1 ||
       flags&~(XBTD_VALIDATE_ONLY|XBTD_ALLOW_PARTIAL) || !c->regions ||
       !c->ops || !c->region_count || c->region_count>64 ||
       !c->op_count || c->op_count>65536)return XBTD_BAD_CATALOG;
    if(!c->complete && !(flags&XBTD_ALLOW_PARTIAL))return XBTD_INCOMPLETE;
    /* Snapshot descriptors before callbacks, so even aliasing cannot mutate
     * bounds or source pointers during validation/commit. */
    XbTypedRegion regions[64];memcpy(regions,c->regions,c->region_count*sizeof(*regions));
    XbTypedOp *ops=malloc(c->op_count*sizeof(*ops));
    if(!ops)return XBTD_MEMORY;
    memcpy(ops,c->ops,c->op_count*sizeof(*ops));
    size_t nr=c->region_count,no=c->op_count;unsigned complete=c->complete;uint64_t want=c->source_hash;
    for(size_t i=0;i<nr;i++) {
        XbTypedRegion *r=regions+i;
        if(!r->bytes || !r->size || r->size>MAX_FILE || r->source_file>1 ||
           (uintptr_t)r->bytes>UINTPTR_MAX-r->size){free(ops);return XBTD_BAD_CATALOG;}
        for(size_t j=0;j<i;j++)if(overlap((uintptr_t)r->bytes,r->size,(uintptr_t)regions[j].bytes,regions[j].size)){
            free(ops);return XBTD_BAD_CATALOG;
        }
    }
    typed_loading=loading=1;
    uint8_t *elf=NULL,*src[2]={NULL,NULL};size_t es=0,sz[2]={ROM_END-ROM_BASE,0},nc=0;
    Cover *cover=calloc(MAX_SECTIONS,sizeof(*cover));int rc=0;uint64_t hash=0xcbf29ce484222325ull;
    if(!cover){rc=XBTD_MEMORY;goto typed_done;}
    if(files->read(files->ctx,"SLUS_216.78",&elf,&es) || !elf || flat_image(elf,es,&src[0],cover,&nc)){
        rc=XBTD_SOURCE;goto typed_done;
    }
    if(files->read(files->ctx,"DBZP.BIN",&src[1],&sz[1]) || !src[1] || !sz[1] || sz[1]>MAX_FILE){rc=XBTD_SOURCE;goto typed_done;}
    for(size_t i=0;i<nr;i++) {
        XbTypedRegion *r=regions+i;
        if(r->source_offset>sz[r->source_file] || r->size>sz[r->source_file]-r->source_offset ||
           (!r->source_file && !covered(cover,nc,r->source_offset,(uint32_t)r->size))){rc=XBTD_SOURCE;goto typed_done;}
    }
    for(size_t i=0;i<no;i++) {
        XbTypedOp *op=ops+i;
        if(op->region>=nr || !op->length ||
           !(op->width==1 || op->width==2 || op->width==4 || op->width==8) ||
           op->length%op->width || op->kind>XBTD_ZERO){rc=XBTD_BAD_CATALOG;goto typed_done;}
        XbTypedRegion *r=regions+op->region;
        if(op->offset>r->size || op->length>r->size-op->offset ||
           (op->kind==XBTD_POINTER && (op->length!=4 || op->width!=4 || op->native_pointer>UINT32_MAX ||
             ((!op->expected_mips)!=(!op->native_pointer))))){rc=XBTD_BAD_CATALOG;goto typed_done;}
        /* Catalog is sorted; reject overlap without quadratic allocation. */
        if(i && (op->region<ops[i-1].region || (op->region==ops[i-1].region &&
            (uint64_t)ops[i-1].offset+ops[i-1].length>op->offset))){rc=XBTD_BAD_CATALOG;goto typed_done;}
        const uint8_t *p=src[r->source_file]+r->source_offset+op->offset;
        if(op->kind==XBTD_POINTER && le32(p)!=op->expected_mips){rc=XBTD_POINTER_MISMATCH;goto typed_done;}
        for(uint32_t j=0;j<op->length;j++) {
            if(op->kind==XBTD_ZERO && p[j]){rc=XBTD_BAD_CATALOG;goto typed_done;}
            hash=(hash^p[j])*0x100000001b3ull;
        }
    }
    if(hash!=want){rc=XBTD_HASH;goto typed_done;}
    if(!(flags&XBTD_VALIDATE_ONLY))for(size_t i=0;i<no;i++) {
        XbTypedOp *op=ops+i;XbTypedRegion *r=regions+op->region;
        uint8_t *dst=(uint8_t *)r->bytes+op->offset;
        const uint8_t *p=src[r->source_file]+r->source_offset+op->offset;
        if(op->kind==XBTD_POINTER) {
            uint32_t value=(uint32_t)op->native_pointer;
            memcpy(dst,&value,4); /* native pointer representation, including host tests */
        }else {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            memcpy(dst,p,op->length);
#else
            for(uint32_t at=0;at<op->length;at+=op->width)
                for(uint32_t k=0;k<op->width;k++)dst[at+k]=p[at+op->width-1-k];
#endif
        }
    }
    XB_LOG("typed-data: %u ops validated; complete=%u; commit=%u; combat NOT started",(unsigned)no,complete,!(flags&XBTD_VALIDATE_ONLY));
typed_done:
    free(elf);free(src[0]);free(src[1]);free(cover);free(ops);typed_loading=loading=0;
    return rc;
}
#ifndef XB_HOST_TEST
int XbTypedData_LoadFromDisc(const XbTypedCatalog *c,unsigned flags)
{XbDataFiles files={disc_read,NULL};return XbTypedData_Load(&files,c,flags);}
#endif
