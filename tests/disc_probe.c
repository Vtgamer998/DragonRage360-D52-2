/* SPDX-License-Identifier: MIT -- host diagnostic, NOT an Xbox executable */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "dr_disc.h"
#include <stdio.h>
#include <sys/types.h>
static int file_read(void *ctx,uint64_t at,void *out,size_t n) {
    if(at>INT64_MAX || fseeko((FILE *)ctx,(off_t)at,SEEK_SET)) return -1;
    return fread(out,1,n,(FILE *)ctx)==n?0:-1;
}
int main(int argc,char **argv) {
    FILE *fp; off_t size; dr_disc d; dr_span s; int rc; unsigned char b[4096]; uint64_t at;
    if(argc<2 || argc>3) {fprintf(stderr,"Usage: disc-probe game.iso [disc/PATH or pzs3us0/00000.bin]\n");return 2;}
    fp=fopen(argv[1],"rb"); if(!fp) {perror(argv[1]);return 2;}
    if(fseeko(fp,0,SEEK_END) || (size=ftello(fp))<0) {fclose(fp);return 2;}
    rc=dr_mount(&d,file_read,fp,(uint64_t)size);
    if(!rc && argc==2) {
        rc=dr_find(&d,"SLUS_216.78",&s);
        if(!rc) rc=dr_find(&d,"BIN/DBZP.BIN",&s);
        if(!rc) puts("USA filenames found. Content hashes and game execution NOT verified.");
    }
    if(!rc && argc==3) {
        rc=dr_game_file(&d,argv[2],&s);
        if(!rc && s.directory) rc=DR_FORMAT;
        for(at=0;!rc && at<s.size;) {
            size_t n=(size_t)(s.size-at>sizeof(b)?sizeof(b):s.size-at);
            rc=dr_read(&d,&s,at,b,n);
            if(!rc && fwrite(b,1,n,stdout)!=n) rc=DR_IO;
            at+=n;
        }
    }
    if(rc) fprintf(stderr,"disc-probe: %s\n",dr_error(rc));
    fclose(fp); return rc?1:0;
}
