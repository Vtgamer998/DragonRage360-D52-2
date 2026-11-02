/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_resconv.h"
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
void XbRes_TestRead(int,int,uint64_t,uint64_t,void*,size_t);
void *XbBpe_Decode(void *,void *,int32_t *);
static int allocations,frees,reject;
void *Heap_Alloc(int32_t n,int32_t a,int32_t b,int32_t c){(void)a;(void)b;(void)c;allocations++;return malloc((size_t)n);}
void Heap_Free(void *p){frees++;free(p);}
static int raw_file(void *p,size_t n,int id){(void)p;(void)n;assert(id==20);return 0;}
static int checked(void *p,size_t n,int id){(void)p;assert(n==8 && id==10);return reject?-23:0;}
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static size_t fixture(uint8_t *p){size_t n=8;p[n++]=127;for(int i=0;i<128;i++)p[n++]=(uint8_t)i;p[n++]=127;for(int i=128;i<256;i++)p[n++]=(uint8_t)i;p[n++]=0;p[n++]=8;for(int i=1;i<=8;i++)p[n++]=(uint8_t)i;put(p,8);put(p+4,(uint32_t)n-8);return n;}
int main(void){
 assert(!XbRes_RegisterChecked(10,10,checked,1));assert(!XbRes_RegisterChecked(20,20,raw_file,0));
 for(int mode=0;mode<8;mode++){
  uint8_t p[300];size_t n=fixture(p);int32_t raw=99;allocations=frees=0;reject=mode==4;
  if(mode==2)put(p+4,UINT32_MAX);
  if(mode==3)p[n-9]=255;
  assert(!XbRes_BeginCheckedReads());if(mode!=1)XbRes_TestRead(1,mode==7?19:9,0,n,p,n);
  uint8_t destination[9];memset(destination,0xa5,sizeof(destination));
  void *out=mode>=5?XbBpe_DecodeBounded(p,destination,mode==5?7:8,&raw):XbBpe_Decode(p,NULL,&raw);
  if(mode==5 || mode==7)for(unsigned j=0;j<sizeof(destination);j++)assert(destination[j]==0xa5);
  if(mode==6){assert(out==destination && raw==8 && destination[8]==0xa5 && !allocations);XbRes_EndCheckedReads();continue;}
  if(mode==0){assert(out && raw==8 && allocations==1 && !frees && !XbRes_LastError());assert(!memcmp(out,"\1\2\3\4\5\6\7\10",8));Heap_Free(out);}
  else{assert(!out && !raw && XbRes_LastError()==(mode==4?-23:(mode==1 || mode==7)?-897:-899));assert(allocations==frees);int before=allocations;assert(!XbBpe_Decode(p,NULL,&raw) && allocations==before);}
  XbRes_EndCheckedReads();
 }
 assert(!XbRes_BeginCheckedReads());int32_t raw=99;assert(!XbBpe_Decode(NULL,NULL,&raw) && !raw && XbRes_LastError()==-897);XbRes_EndCheckedReads();
 puts("PASS bounded BPE errors: null/untracked/raw-file/entry-capacity/truncated/header/checked-converter failure reaches sticky load error; owned allocations freed");return 0;
}
