/* SPDX-License-Identifier: AGPL-3.0-or-later */
#define main previous_resource_tests
#include "real_iso_sanitize.c"
#undef main
#include "dr_texfile.h"
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    FILE *f=fopen(argv[1],"rb");assert(f);assert(!fseeko(f,0,SEEK_END));off_t total=ftello(f);assert(total>0);
    dr_disc disc;assert(!dr_mount(&disc,file_read,f,(uint64_t)total));
    unsigned files=0,textures=0,formats[3]={0};unsigned char *rgba=malloc(4*1024*1024);assert(rgba);
    for(unsigned ch=0;ch<161;ch++)for(unsigned skin=0;skin<8;skin++) {
        size_t size;unsigned char *pack=load(&disc,0x590+ch*10+skin,&size);
        if(size==64) {free(pack);continue;}
        unsigned count=dr_resource_le32(pack);
        for(unsigned i=0;i<count && i<=16;i++) {
            if(i!=0 && i<3)continue;
            size_t off,n;int r=dr_pack_memory_entry(pack,size,i,&off,&n,NULL);
            if(r==DR_NOT_FOUND) {continue;}assert(!r);
            const unsigned char *t=pack+off;
            if(n>=4 && (dr_resource_le32(t)&0xffff0000u)==0x60000000u)continue;
            files++;unsigned entries=dr_resource_le32(t);
            for(unsigned j=0;j<entries;j++) {
                DrTexFileImage info;int rc=dr_texfile_decode(t,n,j,rgba,4*1024*1024,&info);
                if(rc)fprintf(stderr,"texture failed ch=%u skin=%u resource=%u index=%u rc=%d\n",ch,skin,i,j,rc);
                assert(!rc && info.rgba_bytes==(size_t)info.width*info.height*4);
                formats[info.psm==0?0:info.psm==19?1:2]++;textures++;
            }
        }
        free(pack);
    }
    assert(files==2148 && textures==16156 && formats[0]==4441 && formats[1]==1396 && formats[2]==10319);
    printf("PASS TexFile real corpus: files=%u textures=%u CT32=%u PSMT8=%u PSMT4=%u; CPU only\n",files,textures,formats[0],formats[1],formats[2]);
    free(rgba);fclose(f);return 0;
}
