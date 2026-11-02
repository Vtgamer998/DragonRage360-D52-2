#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "xb_resconv.h"
void XbRes_TestRead(int,int,uint64_t,uint64_t,void*,size_t);
void *Bpe_Decode(void *src, void *dst, int32_t *rawSize);
void *Heap_Alloc(int size,int a,int b,int c){(void)a;(void)b;(void)c;return malloc(size);}
static int calls; static int lastId;
static void conv(void*b,size_t n,int id){calls++;lastId=id;XbRes_Swap32(b,n/4);}
int main(void){
  /* SwapPlan */
  uint8_t r[16]={1,2,3,4, 5,6, 7,8, 9,10,11,12,13,14,15,16}; /* I H H B4... -> I2? use "IH2B4" */
  uint8_t rec[24]; memcpy(rec,r,16); memset(rec+16,0xAA,8); uint8_t two[32]; memcpy(two,r,16); memcpy(two+16,r,16);
  int prc=XbRes_SwapPlan(two,32,"IH2B4x4*");
  printf("plan rc=%d: %02X%02X%02X%02X %02X%02X %02X%02X %02X%02X (want 04030201 0605 0807 090A)\n",prc,two[0],two[1],two[2],two[3],two[4],two[5],two[6],two[7],two[8],two[9]);
  printf("second record: %02X%02X%02X%02X (want 04030201)\n",two[16],two[17],two[18],two[19]);
  printf("plan size mismatch rc=%d (want -1)\n",XbRes_SwapPlan(two,30,"IH2B4x4*"));
  /* packed file: header LE {raw=8, pack=...}, table identity, one block of 8 literals */
  uint8_t pk[8+1+128+1+128+2+8]; int n=0; uint32_t raw=8;
  n=8; pk[n++]=127; for(int i=0;i<128;i++)pk[n++]=i; pk[n++]=127; for(int i=128;i<256;i++)pk[n++]=i;
  pk[n++]=0; pk[n++]=8; /* blockSize: high byte first */
  uint8_t data[8]={1,2,3,4,5,6,7,8}; memcpy(pk+n,data,8); n+=8;
  uint32_t pack=n-8; for(int k=0;k<4;k++){pk[k]=raw>>(8*k);pk[4+k]=pack>>(8*k);}
  XbRes_Register(1,10,conv,1); XbRes_Register(20,30,conv,0);
  printf("overlap register rc=%d (want -1)\n",XbRes_Register(5,25,conv,0));
  XbRes_TestRead(1,5,0,n,pk,n);              /* id 6, packed: only remembered */
  printf("after read calls=%d (want 0)\n",calls);
  int32_t sz=0; uint8_t *out=Bpe_Decode(pk,NULL,&sz);
  printf("decoded size=%d calls=%d id=%d out=%02X %02X %02X %02X %02X %02X %02X %02X (want 8 1 6, 04 03 02 01 08 07 06 05)\n",sz,calls,lastId,out[0],out[1],out[2],out[3],out[4],out[5],out[6],out[7]);
  /* the buffer is read into again by another file: no stale conversion */
  XbRes_TestRead(1,500,0,n,pk,n); calls=0; free(out); Bpe_Decode(pk,NULL,&sz);
  printf("stale buffer: calls=%d (want 0)\n",calls);
  /* plain registered file, partial read, unregistered */
  uint8_t f[8]={1,2,3,4,5,6,7,8}; XbRes_TestRead(2,0xD48-0xD48+22-20+0,0,8,f,8); /* id 0xD48+2? */
  XbRes_TestRead(0,0,0,8,f,8);
  XbRes_Report();
  return 0;}
