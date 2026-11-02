/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Real-ISO memory-safety run, CPU only. Does not execute the game or shaders. */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "dr_container.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
static int file_read(void *ctx,uint64_t at,void *out,size_t n)
{
    if(at>INT64_MAX || fseeko(ctx,(off_t)at,SEEK_SET))return -1;
    return fread(out,1,n,ctx)==n?0:-1;
}
static void *load(const dr_disc *disc,unsigned fid,size_t *size)
{
    char path[64];dr_span span;
    snprintf(path,sizeof(path),"pzs3us1/%05u.bin",fid-1);
    assert(!dr_game_file(disc,path,&span) && span.size<=16*1024*1024);
    *size=(size_t)span.size;void *data=malloc(*size?*size:1);assert(data);
    assert(!dr_read(disc,&span,0,data,*size));return data;
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    FILE *f=fopen(argv[1],"rb");assert(f);
    assert(!fseeko(f,0,SEEK_END));off_t file_size=ftello(f);assert(file_size>0);
    dr_disc disc;assert(!dr_mount(&disc,file_read,f,(uint64_t)file_size));
    unsigned char *raw=malloc(0xc000),*be=malloc(0xc000),*truncated=malloc(0xc000);assert(raw && be && truncated);
    unsigned animations=0,empty=0,models=0,absent=0,truncations=0;
    for(unsigned c=0;c<161;c++) {
        size_t size;unsigned char *pack=load(&disc,0x598+c*10,&size);
        assert(size>=4 && dr_resource_le32(pack)>=0x19e);
        for(unsigned i=0;i<0x19e;i++) {
            size_t at,n;int r=dr_pack_memory_entry(pack,size,i,&at,&n,NULL);
            if(r==DR_NOT_FOUND) {empty++;continue;}assert(!r);
            size_t written;assert(!dr_bpe_decode(pack+at,n,raw,0xc000,&written));
            if(!written) {empty++;continue;}
            assert(!dr_anim_prepare(raw,written,0));memcpy(be,raw,written);
            assert(!dr_anim_prepare(be,written,1));animations++;
            if(c==0 && i==0)for(size_t cut=0;cut<written;cut+=17) {
                memcpy(be,raw,written);memcpy(truncated,raw,written);
                r=dr_anim_prepare(be,cut,1);
                if(r)assert(!memcmp(be,truncated,written));
                truncations++;
            }
        }
        free(pack);
        for(unsigned skin=0;skin<8;skin++) {
            pack=load(&disc,0x590+c*10+skin,&size);
            if(size==64 && dr_resource_le32(pack)==1 && dr_resource_le32(pack+4)==64 && dr_resource_le32(pack+8)==64) {
                for(size_t i=12;i<size;i++) {assert(!pack[i]);}
                absent++;free(pack);continue;
            }
            size_t at,n;assert(!dr_pack_memory_entry(pack,size,2,&at,&n,NULL));
            assert(n>=0x70 && !memcmp(pack+at,"pmdl",4));
            size_t node=dr_resource_le32(pack+at+0x6c);assert(node>=0x70 && node<n);
            size_t nodes;assert(!dr_skeleton_prepare(pack+at+node,n-node,1,&nodes));assert(nodes>0);models++;
            free(pack);
        }
    }
    printf("PASS real ISO: animations=%u empty_motion_slots=%u models=%u absent_skins=%u truncated_real_animation_checks=%u\n",
           animations,empty,models,absent,truncations);
    free(truncated);free(be);free(raw);fclose(f);return 0;
}
