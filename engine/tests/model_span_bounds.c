#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
typedef struct {size_t chain_offset,chain_bytes;} DrBodyPart;
typedef struct {unsigned char *model;size_t model_size;DrBodyPart part[256];unsigned count;void *face[8];size_t face_bytes[8];} XbNativeModel;
/* ACTUAL_SPAN_FUNCTION */
int main(void){_Alignas(16) unsigned char raw[512]={0},face[64]={0};XbNativeModel m={.model=raw,.model_size=512,.count=1,.part={{64,160}}};size_t n=999;
assert(XbNativeModel_ChainSpan(&m,raw+64,&n)&&n==160);
assert(!XbNativeModel_ChainSpan(&m,raw+80,&n)&&n==0);
assert(!XbNativeModel_ChainSpan(&m,raw+65,&n));
m.part[0].chain_bytes=SIZE_MAX;assert(!XbNativeModel_ChainSpan(&m,raw+64,&n));
m.part[0].chain_offset=SIZE_MAX;assert(!XbNativeModel_ChainSpan(&m,raw+64,&n));
m.face[3]=face;m.face_bytes[3]=64;assert(XbNativeModel_ChainSpan(&m,face,&n)&&n==64);
assert(!XbNativeModel_ChainSpan(&m,face+16,&n));m.face_bytes[3]=0;assert(!XbNativeModel_ChainSpan(&m,face,&n));
assert(!XbNativeModel_ChainSpan(NULL,face,&n));assert(!XbNativeModel_ChainSpan(&m,NULL,&n));assert(!XbNativeModel_ChainSpan(&m,face,NULL));puts("PASS actual native-model span: exact mesh/face starts only; overflow, interior, unaligned, empty and null rejected");}
