/* SPDX-License-Identifier: AGPL-3.0-or-later */
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
typedef uint32_t u32;
static unsigned char *source;static size_t extent;static int tracked,owned,error,copies;static size_t copied;
void xsdk_log_write(const char *fmt,...){(void)fmt;}
int XbRes_ReadFile(const void *p,const void **base,size_t *n,int *id){if(!tracked || (uintptr_t)p<(uintptr_t)source || (uintptr_t)p-(uintptr_t)source>=extent)return 0;*base=source;*n=extent;*id=0x171;return 1;}
u32 XbRes_Le32(const void *src){const unsigned char*p=src;return p[0]|(u32)p[1]<<8|(u32)p[2]<<16|(u32)p[3]<<24;}
int dr_dma_reject_packet(void){return error=-420;}
int dr_dma_snapshot_chain(const void *p,size_t n,u32 *address){assert(p==source);copies++;copied=n;*address=0x43000000;return 0;}
int XbFighter_ChainSpan(const void *p,size_t *n){if(!owned||p!=source)return 0;*n=extent;return 1;}
static int shadow_chain_span(const void*p,size_t*n){(void)p;(void)n;return 0;}
int dr_dma_snapshot_words(const void*p,size_t n,u32*a){return dr_dma_snapshot_chain(p,n,a);}
/* ORIGINAL_FUNCTION */
static void reset(unsigned qwc,unsigned kind){memset(source,0,512);u32 tag=kind<<28|qwc;for(int i=0;i<4;i++)source[i]=(unsigned char)(tag>>(8*i));extent=512;tracked=1;owned=0;error=copies=0;copied=0;}
int main(void){
 source=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);assert(source!=MAP_FAILED && (uintptr_t)source<=UINT32_MAX-4096);u32 ptr=(u32)(uintptr_t)source;
 reset(3,6);assert(XbVif_ChainAddress(ptr)==0x43000000 && copies==1 && copied==64 && !error);
 reset(32,6);assert(!XbVif_ChainAddress(ptr) && error && !copies);
 reset(0,6);extent=15;assert(!XbVif_ChainAddress(ptr) && error && !copies);
 reset(1,2);assert(!XbVif_ChainAddress(ptr) && error && !copies);
 reset(1,6);tracked=0;assert(!XbVif_ChainAddress(ptr) && error && !copies);
 reset(1,6);assert(!XbVif_ChainAddress(ptr+1) && error && !copies);
 reset(1,6);assert(!XbVif_ChainAddress(0) && error && !copies);
 reset(3,6);tracked=0;owned=1;assert(XbVif_ChainAddress(ptr)==0x43000000 && copied==64);
 reset(32,6);tracked=0;owned=1;assert(!XbVif_ChainAddress(ptr) && error && !copies);
 reset(3,6);tracked=0;owned=1;assert(!XbVif_ChainAddress(ptr+16) && error && !copies);
 assert(!munmap(source,4096));puts("PASS actual VIF CALL bridge: tracked-file or owned-model RET only; numeric LE header; exact extent; untracked/misaligned/truncated/other DMA tags rejected before snapshot");return 0;
}
