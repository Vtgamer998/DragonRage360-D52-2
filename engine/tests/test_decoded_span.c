#include "xb_resconv.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
void XbRes_TestRead(int,int,uint64_t,uint64_t,void*,size_t);
static int refuse;
static int prepare(void *p,size_t n,int id){assert(p && n==64 && id==7);return refuse?-99:0;}
int main(void){unsigned char packed[16]={64},decoded[64]={0};const void*base;size_t n;int id;
assert(!XbRes_RegisterChecked(7,7,prepare,1));assert(!XbRes_BeginCheckedReads());
XbRes_TestRead(1,6,0,16,packed,16);assert(XbRes_BpeSourceSize(packed)==16);
assert(!XbRes_ReadFile(decoded,&base,&n,&id));XbRes_AfterBpe(packed,decoded,64);assert(!XbRes_LastError());
assert(XbRes_ReadFile(decoded+48,&base,&n,&id)&&base==decoded&&n==64&&id==7);
assert(!XbRes_ReadFile(decoded+64,&base,&n,&id));XbRes_Forget(decoded,64);assert(!XbRes_ReadFile(decoded,&base,&n,&id));
refuse=1;XbRes_AfterBpe(packed,decoded,64);assert(XbRes_LastError()==-99&&!XbRes_ReadFile(decoded,&base,&n,&id));
XbRes_EndCheckedReads();puts("PASS: checked BPE output provenance, exact extent, conversion refusal and free revocation");}
