/* SPDX-License-Identifier: MIT
 * Actual xb_disc/ISO/file adapters; only NT file transport replaced by host IO.
 */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "xb_iso_file.h"
#include "xb_disc.h"
#include "xsdk/kernel.h"
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
static const char *image_path;static FILE *image;static int fail_read;
void xsdk_log_write(const char *f,...){assert(f);}
NTSTATUS NtCreateFile(HANDLE *h,uint32_t access,const OBJECT_ATTRIBUTES *a,IO_STATUS_BLOCK *io,
    const uint64_t *size,uint32_t flags,uint32_t share,uint32_t disp,uint32_t options) {
    (void)access;(void)size;(void)flags;(void)share;(void)disp;(void)options;
    assert(a && a->name && io && !image);
    if(strcmp(a->name->pointer,"game:\\test.iso"))return STATUS_INVALID_PARAMETER;
    image=fopen(image_path,"rb");if(!image)return STATUS_INVALID_PARAMETER;
    *h=1;io->status=STATUS_SUCCESS;io->information=0;return STATUS_SUCCESS;
}
NTSTATUS NtClose(HANDLE h){assert(h==1 && image);fclose(image);image=NULL;return STATUS_SUCCESS;}
NTSTATUS NtQueryInformationFile(HANDLE h,IO_STATUS_BLOCK *io,void *out,uint32_t n,uint32_t type) {
    assert(h==1 && image && n==sizeof(FILE_NETWORK_OPEN_INFORMATION) && type==FILE_NETWORK_OPEN_INFORMATION_CLASS);
    FILE_NETWORK_OPEN_INFORMATION *info=out;memset(out,0,n);
    assert(!fseeko(image,0,SEEK_END));info->end_of_file=(uint64_t)ftello(image);
    io->information=n;return STATUS_SUCCESS;
}
NTSTATUS NtReadFile(HANDLE h,HANDLE event,void *apc,void *ctx,IO_STATUS_BLOCK *io,void *out,uint32_t n,const uint64_t *at) {
    assert(h==1 && image && !event && !apc && !ctx && at);
    if(fail_read){memset(out,0xcc,n);io->information=0;return STATUS_INVALID_PARAMETER;}
    if(fseeko(image,(off_t)*at,SEEK_SET))return STATUS_INVALID_PARAMETER;
    io->information=(uint32_t)fread(out,1,n,image);return STATUS_SUCCESS;
}
static void copy_file(char *name,const char *dest) {
    int fd=sceOpen(name,1);assert(fd>=32);FILE *f=fopen(dest,"wb");assert(f);
    uint8_t buf[65536];int n;
    while((n=sceRead(fd,buf,sizeof(buf)))>0)assert(fwrite(buf,1,(size_t)n,f)==(size_t)n);
    assert(n==0 && !sceClose(fd));fclose(f);
}
int main(int argc,char **argv) {
    assert(argc==4);image_path=argv[1];
    assert(sceOpen("SYSTEM.CNF",1)==-1 && !sceCdSearchFile((char[32]){0},"SYSTEM.CNF"));
    assert(!XbDisc_Mount("game:/test.iso") && !XbIsoFiles_Probe());
    uint8_t info[66];memset(info,0xa5,sizeof(info));
    assert(sceCdSearchFile(info+1,"cdrom0:\\SLUS_216.78;1")==1);
    uint32_t lsn,size;memcpy(&lsn,info+1,4);memcpy(&size,info+5,4);
    assert(lsn==308 && size==2115684 && !memcmp(info+9,"SLUS_216.78",11));
    assert(info[0]==0xa5);for(unsigned i=33;i<sizeof(info);i++)assert(info[i]==0xa5);
    for(unsigned i=25;i<33;i++)assert(!info[i]); /* date explicitly unavailable */
    assert(!sceCdSearchFile(info,"absent.bin") && !sceCdSearchFile(NULL,"SYSTEM.CNF"));
    assert(sceOpen(NULL,1)==-1 && sceOpen("SYSTEM.CNF",2)==-1);
    assert(sceOpen("cdrom0:/../SYSTEM.CNF",1)==-1 && sceOpen("game:/SYSTEM.CNF",1)==-1);
    assert(sceOpen("SYSTEM.CNF;2",1)==-1 && sceOpen("BIN",1)==-1);
    char longpath[300];memset(longpath,'x',299);longpath[299]=0;assert(sceOpen(longpath,1)==-1);
    int fd=sceOpen("cdrom0:/system.cnf;1",1);assert(fd>=32);
    uint8_t data[66];memset(data,0xa5,sizeof(data));
    assert(sceLseek(fd,0,2)==57 && sceLseek(fd,0,0)==0);
    assert(sceRead(fd,NULL,0)==0 && sceRead(fd,NULL,1)==-1 && sceRead(fd,data,-1)==-1);
    assert(sceLseek(fd,-1,0)==-1 && sceLseek(fd,0,3)==-1 && sceLseek(fd,0,1)==0);
    fail_read=1;assert(sceRead(fd,data,20)==-1 && sceLseek(fd,0,1)==0);
    for(unsigned i=0;i<sizeof(data);i++)assert(data[i]==0xa5);
    fail_read=0;
    assert(sceRead(fd,data,sizeof(data))==57 && !memcmp(data,"BOOT2",5));
    for(unsigned i=57;i<sizeof(data);i++)assert(data[i]==0xa5);
    assert(sceRead(fd,data,1)==0 && sceLseek(fd,-1,2)==56 && sceRead(fd,data,5)==1);
    assert(sceLseek(fd,10,2)==67 && sceRead(fd,data,1)==0);
    assert(!sceClose(fd) && sceClose(fd)==-1 && sceRead(fd,data,1)==-1);
    int newer=sceOpen("SYSTEM.CNF",1);assert(newer!=fd && !sceClose(newer));
    int handles[32];for(unsigned i=0;i<32;i++){handles[i]=sceOpen("SYSTEM.CNF",1);assert(handles[i]>=32);}
    assert(sceOpen("SYSTEM.CNF",1)==-1);
    for(unsigned i=0;i<32;i++)assert(!sceClose(handles[i]));
    fd=sceOpen("SYSTEM.CNF",1);uint64_t generation=XbDisc_Generation();
    assert(!XbDisc_Mount("game:/test.iso") && XbDisc_Generation()!=generation);
    assert(sceRead(fd,data,1)==-1);
    newer=sceOpen("SYSTEM.CNF",1);assert(newer!=fd && !sceClose(newer));
    fd=sceOpen("SYSTEM.CNF",1);XbIsoFiles_CloseAll();assert(sceRead(fd,data,1)==-1);
    copy_file("cdrom0:\\SLUS_216.78;1",argv[2]);copy_file("cdrom0:\\BIN\\DBZP.BIN;1",argv[3]);
    fd=sceOpen("SYSTEM.CNF",1);XbDisc_Unmount();assert(sceRead(fd,data,1)==-1);
    assert(XbDisc_Mount("game:/absent.iso")!=0 && !XbDisc_Get());
    assert(!image);
    puts("PASS: real ISO through actual xb_disc/dr_disc/Sony byte-file adapter; NT transport mocked; bounds, EOF, short reads, seeks, errors, descriptors, stale mounts and exact ELF/overlay extraction; no game execution");
    return 0;
}
