/* SPDX-License-Identifier: MIT
 * ISO9660 level-1/2 single-extent, 2048-byte data sectors; read-only.
 * Explicit byte decoding: no host-endian or unaligned integer casts.
 */
#include "dr_disc.h"
#include <string.h>

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint32_t be32(const unsigned char *p) {
    return (uint32_t)p[3] | (uint32_t)p[2]<<8 | (uint32_t)p[1]<<16 | (uint32_t)p[0]<<24;
}
static int bounds(uint64_t offset, uint64_t size, uint64_t limit) {
    return offset<=limit && size<=limit-offset;
}
static int raw(const dr_disc *d, uint64_t at, void *out, size_t n) {
    if (!bounds(at,n,d->size)) return DR_RANGE;
    return d->read(d->opaque,at,out,n) == 0 ? DR_OK : DR_IO;
}
static int record(const dr_disc *d, const unsigned char *p, size_t n, dr_span *s) {
    uint64_t base, length;
    if (n<34 || p[0]<34 || p[0]>n || p[32]==0 || (unsigned)33+p[32]>p[0]) return DR_FORMAT;
    if (le32(p+2)!=be32(p+6) || le32(p+10)!=be32(p+14)) return DR_FORMAT;
    if (p[25]&0x80 || p[26] || p[27]) return DR_UNSUPPORTED;
    if (p[28]!=1 || p[29]!=0 || p[30]!=0 || p[31]!=1) return DR_UNSUPPORTED;
    base=((uint64_t)le32(p+2)+p[1])*2048;
    length=le32(p+10);
    if (!bounds(base,length,d->size)) return DR_RANGE;
    s->offset=base; s->size=length; s->directory=!!(p[25]&2);
    return DR_OK;
}
int dr_mount(dr_disc *d, dr_read_at read, void *ctx, uint64_t size) {
    unsigned char sec[2048]; unsigned i; int rc;
    if (!d || !read) return DR_FORMAT;
    memset(d,0,sizeof(*d)); d->read=read; d->opaque=ctx; d->size=size;
    for(i=16;i<80;i++) {
        rc=raw(d,(uint64_t)i*2048,sec,sizeof(sec)); if(rc) return rc;
        if(memcmp(sec+1,"CD001",5) || sec[6]!=1) return DR_FORMAT;
        if(sec[0]==255) return DR_NOT_FOUND;
        if(sec[0]!=1) continue;
        if(sec[128]!=0 || sec[129]!=8 || sec[130]!=8 || sec[131]!=0) return DR_UNSUPPORTED;
        if(le32(sec+80)!=be32(sec+84)) return DR_FORMAT;
        if((uint64_t)le32(sec+80)*2048>size) return DR_RANGE;
        d->size=(uint64_t)le32(sec+80)*2048;
        rc=record(d,sec+156,2048-156,&d->root);
        if(rc) return rc;
        return d->root.directory ? DR_OK : DR_FORMAT;
    }
    return DR_UNSUPPORTED;
}
int dr_read(const dr_disc *d,const dr_span *s,uint64_t off,void *buf,size_t n) {
    if(!d || !s || (!buf && n)) return DR_FORMAT;
    if(!bounds(s->offset,s->size,d->size) || !bounds(off,n,s->size)) return DR_RANGE;
    if(!n) return DR_OK;
    return raw(d,s->offset+off,buf,n);
}
static unsigned upper(unsigned c) {return c>='a' && c<='z' ? c-32 : c;}
static int same_name(const unsigned char *p,size_t pn,const char *s,size_t sn) {
    size_t i,n=0; while(n<pn && p[n]!=';') n++;
    if(n!=sn) return 0;
    for(i=0;i<n;i++) if(upper(p[i])!=upper((unsigned char)s[i])) return 0;
    return 1;
}
static int child(const dr_disc *d,const dr_span *dir,const char *name,size_t len,dr_span *out) {
    unsigned char sec[2048]; uint64_t at; size_t n,pos; int rc;
    if(!dir->directory) return DR_NOT_FOUND;
    /* Bound malformed directory work; PS2 disc directories are far smaller. */
    if(dir->size>16u*1024u*1024u) return DR_UNSUPPORTED;
    for(at=0;at<dir->size;at+=n) {
        n=(size_t)(dir->size-at>2048?2048:dir->size-at);
        rc=dr_read(d,dir,at,sec,n); if(rc) return rc;
        for(pos=0;pos<n && sec[pos];pos+=sec[pos]) {
            const unsigned char *p=sec+pos;
            if(p[0]<34 || p[0]>n-pos || (unsigned)33+p[32]>p[0]) return DR_FORMAT;
            if(p[32]==1 && p[33]<=1) continue;
            if(same_name(p+33,p[32],name,len)) return record(d,p,n-pos,out);
        }
    }
    return DR_NOT_FOUND;
}
int dr_find(const dr_disc *d,const char *path,dr_span *out) {
    dr_span current,next; const char *start; size_t n; int rc; unsigned depth=0;
    if(!d || !path || !out) return DR_FORMAT;
    current=d->root; if(*path=='/') path++;
    while(*path) {
        start=path; while(*path && *path!='/') path++; n=(size_t)(path-start);
        if(!n || n>255 || ++depth>16 || (n==1 && start[0]=='.') ||
           (n==2 && start[0]=='.' && start[1]=='.')) return DR_FORMAT;
        rc=child(d,&current,start,n,&next); if(rc) return rc; current=next;
        if(*path=='/') {path++; if(!*path && !current.directory) return DR_NOT_FOUND;}
    }
    *out=current; return DR_OK;
}
int dr_afs_entry(const dr_disc *d,const dr_span *afs,uint32_t index,dr_span *out) {
    unsigned char h[8]; uint32_t count,off,size; uint64_t table_end; int rc;
    if(!d || !afs || !out || afs->directory) return DR_FORMAT;
    rc=dr_read(d,afs,0,h,8); if(rc) return rc;
    if(memcmp(h,"AFS\0",4)) return DR_FORMAT;
    count=le32(h+4); table_end=8+(uint64_t)count*8;
    if(table_end>afs->size) return DR_FORMAT;
    if(index>=count) return DR_NOT_FOUND;
    rc=dr_read(d,afs,8+(uint64_t)index*8,h,8); if(rc) return rc;
    off=le32(h); size=le32(h+4);
    if(!bounds(off,size,afs->size) || (size && off<table_end)) return DR_RANGE;
    out->offset=afs->offset+off; out->size=size; out->directory=0;
    return DR_OK;
}
int dr_game_file(const dr_disc *d,const char *rel,dr_span *out) {
    char path[]="DATA/PZS3US0.AFS"; dr_span afs; uint32_t index=0; unsigned i; int rc;
    if(!rel) return DR_FORMAT;
    if(strncmp(rel,"disc/",5)==0) return dr_find(d,rel+5,out);
    if(strlen(rel)!=17 || strncmp(rel,"pzs3us",6) || rel[6]<'0' || rel[6]>'2' ||
       rel[7]!='/' || strcmp(rel+13,".bin")) return DR_NOT_FOUND;
    for(i=8;i<13;i++) {if(rel[i]<'0'||rel[i]>'9') return DR_FORMAT; index=index*10+(unsigned)(rel[i]-'0');}
    path[11]=rel[6]; rc=dr_find(d,path,&afs); if(rc) return rc;
    return dr_afs_entry(d,&afs,index,out);
}
const char *dr_error(int e) {
    switch(e) {case DR_OK:return "ok";case DR_IO:return "read failed";
    case DR_FORMAT:return "invalid format";case DR_RANGE:return "outside image/file bounds";
    case DR_NOT_FOUND:return "not found";case DR_UNSUPPORTED:return "unsupported ISO feature";default:return "unknown";}
}
