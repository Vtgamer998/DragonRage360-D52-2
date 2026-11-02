/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_stage_effects.h"
#include <stdint.h>
#include <string.h>
#include <math.h>
static uint32_t c3u(const unsigned char *s){return s[0]|(uint32_t)s[1]<<8|(uint32_t)s[2]<<16|(uint32_t)s[3]<<24;}
static int fields(uint8_t *d,const uint8_t *s,size_t n,size_t a,size_t b,int floats){
 if(a>b || b>n || (b-a)%4)return -890;
 for(size_t i=a;i<b;i+=4){uint32_t u=c3u(s+i);if(floats){float f;memcpy(&f,&u,4);if(!isfinite(f))return -890;}memcpy(d+i,&u,4);}return 0;
}
#define C3W(a,b) do{int e=fields(d,s,n,a,b,0);if(e)return e;}while(0)
#define C3F(a,b) do{int e=fields(d,s,n,a,b,1);if(e)return e;}while(0)
static int c3mesh(uint8_t *d,const uint8_t *s,size_t n){
 if(n<16 || c3u(s)!=0x53454244)return -890;
 size_t groups=c3u(s+4),tri=c3u(s+8),vtx=c3u(s+12);
 if(!groups || groups>(n-16)/16 || tri<16+groups*16 || vtx<tri || vtx>n || (tri&15) || (vtx&15))return -890;
 size_t triangles=(vtx-tri)/16,vertices=(n-vtx)/48;
 C3W(0,16+groups*16);
 for(size_t i=0;i<groups;i++){
  size_t at=16+i*16,count=c3u(s+at),first=c3u(s+at+8);
  if(first>triangles || count>triangles-first)return -890;
  for(size_t j=first;j<first+count;j++){
   for(unsigned k=0;k<3;k++)if(c3u(s+tri+j*16+k*4)>=vertices)return -890;
   C3W(tri+j*16,tri+j*16+16);
  }
 }
 C3F(vtx,vtx+vertices*48);return 0;
}
static int surface_span(size_t n,size_t at,size_t count,size_t stride){return (at&15) || at<32 || at>n || count>(n-at)/stride;}
static int surface(uint8_t *d,const uint8_t *s,size_t n){
 if(n<96)return -890;
 C3F(0,52);C3W(52,56);C3F(56,64);
 const uint8_t *f=s+64;size_t bytes=n-64;
 size_t nt=c3u(f+16),tx=c3u(f+20),nm=c3u(f+24),mo=c3u(f+28);
 if(c3u(f)!=0x336c646d || c3u(f+4)!=4 || nt>32 || surface_span(bytes,tx,nt,64) || surface_span(bytes,mo,nm,96) || c3u(f+12)>bytes)return -890;
 C3W(64,96);
 for(size_t i=0;i<nt;i++){
  size_t row=64+tx+i*64;size_t a=c3u(s+row),b=c3u(s+row+4),na=c3u(s+row+8),nb=c3u(s+row+12);
  if((na && surface_span(bytes,a,na,1)) || (nb && surface_span(bytes,b,nb,1)))return -890;
  C3W(row,row+40);
  for(unsigned k=0;k<2;k++){uint64_t value=c3u(s+row+40+k*8)|(uint64_t)c3u(s+row+44+k*8)<<32;memcpy(d+row+40+k*8,&value,8);}
  memset(d+row+56,0,8);
 }
 for(size_t i=0;i<nm;i++){
  size_t row=64+mo+i*96;size_t tex=c3u(s+row),count=c3u(s+row+4),tri=c3u(s+row+8);uint32_t flags=c3u(s+row+32);
  if((tex>=nt && !(flags&0x800u)) || surface_span(bytes,tri,count,144))return -890;
  C3W(row,row+12);C3F(row+12,row+28);C3W(row+28,row+48);C3F(row+48,row+96);
  C3F(64+tri,64+tri+count*144);
 }
 return 0;
}
int XbStageEffects_PrepareEntry(unsigned index,void *dest,const void *source,size_t n){
 if(!dest || !source || dest==source)return -890;
 uint8_t *d=dest;const uint8_t *s=source;
 memcpy(d,s,n);
 if(index==12)return surface(d,s,n);
 if(index==6){if(n<32)return -890;C3W(0,16);return c3mesh(d+16,s+16,n-16);}
 if(index==21){if(n!=64)return -890;C3F(0,64);return 0;}
 if(index==22){if(n!=64)return -890;C3F(0,56);C3W(56,64);return 0;}
 return -890;
}
