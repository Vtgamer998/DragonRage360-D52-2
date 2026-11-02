/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_resconv.h"
#include "xb_demo_resources.h"
#include "dr_texfile.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void XbRes_TestRead(int,int,uint64_t,uint64_t,void *,size_t);
int main(int argc,char **argv){
 assert(argc==3);assert(!XbRes_RegisterInitialMembers());
 for(unsigned i=0;i<2;i++){
  FILE *f=fopen(argv[i+1],"rb");assert(f);unsigned char p[4096],big[4096],before[4096];size_t n=fread(p,1,sizeof(p),f);assert(feof(f) && n==3424);fclose(f);
  assert(!XbRes_BeginCheckedReads());XbRes_TestRead(1,(i?11:8)-1,0,n,p,sizeof(p));assert(!XbRes_LastError() && XbRes_Le32(p)==1 && !XbRes_Le32(p+16));
  assert(!dr_texfile_validate_transfers(p,n,0));memcpy(big,p,n);assert(!dr_texfile_prepare(big,n,1));assert(!memcmp(p+96,big+96,n-96));XbRes_EndCheckedReads();
  memcpy(before,p,n);memset(p+4,0xff,4);memcpy(before,p,n);assert(!XbRes_BeginCheckedReads());XbRes_TestRead(1,(i?11:8)-1,0,n,p,n);assert(XbRes_LastError() && !memcmp(p,before,n));XbRes_EndCheckedReads();
  printf("PASS member file id=%d: TexFile metadata, BE encoding, LE upload preservation, corrupt table refusal; no target-pointer relocation/GPU\n",i?11:8);
 }
 return 0;
}
