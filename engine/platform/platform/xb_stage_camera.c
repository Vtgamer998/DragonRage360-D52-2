/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Schema: original btl_demo_cam.h DemoCamAnim/Channel/Key. */
#include "xb_stage_camera.h"
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <float.h>
static uint32_t word(const unsigned char *s){return s[0]|(uint32_t)s[1]<<8|(uint32_t)s[2]<<16|(uint32_t)s[3]<<24;}
static float number(const unsigned char *s){uint32_t w=word(s);float f;memcpy(&f,&w,4);return f;}
static int span(size_t n,size_t at,size_t count,size_t stride){return at<36 || (at&3) || at>n || count>(n-at)/stride;}
static void encode(unsigned char *d,const unsigned char *s,size_t at,size_t n,int be){
 for(size_t i=at;i<at+n;i+=4){uint32_t w=word(s+i);for(unsigned j=0;j<4;j++)d[i+j]=(unsigned char)(w>>(8*(be?3-j:j)));}
}
int XbStageCamera_PrepareEndian(void *destination,const void *source,size_t n,int be){
 if(!destination || !source || destination==source || n<36 || (be!=0 && be!=1))return -896;
 const unsigned char *s=source;unsigned char *d=destination;
 size_t table=word(s+8),count=word(s+12),defaults=word(s+24),optional=word(s+28);
 if(word(s)!=0x6e414d43 || !word(s+4) || count>8 || span(n,table,count,16) || span(n,defaults,1,32) || (optional && optional>=n) || !isfinite(number(s+20)) || number(s+20)<0)return -896;
 for(unsigned i=0;i<8;i++)if(i!=6 && !isfinite(number(s+defaults+i*4)))return -896;
 unsigned seen=0;
 for(size_t i=0;i<count;i++){
  const unsigned char *row=s+table+i*16;size_t index=word(row),keys=word(row+4),nk=word(row+8);
  if(index>=8 || (seen&(1u<<index)) || !nk || span(n,keys,nk,32))return -896;
  seen|=1u<<index;float previous=-FLT_MAX;
  for(size_t k=0;k<nk;k++){
   const unsigned char *key=s+keys+k*32;float time=number(key+4);
   if(!isfinite(time) || !isfinite(number(key+8)) || time<previous)return -896;
   previous=time;
  }
 }
 /* Validation completes before publishing any native word. */
 memcpy(d,s,n);encode(d,s,0,36,be);encode(d,s,defaults,32,be);
 for(size_t i=0;i<count;i++){
  size_t row=table+i*16,keys=word(s+row+4),nk=word(s+row+8);encode(d,s,row,16,be);
  for(size_t k=0;k<nk;k++)encode(d,s,keys+k*32,12,be);
 }
 return 0;
}

int XbStageCamera_Prepare(void*d,const void*s,size_t n){return XbStageCamera_PrepareEndian(d,s,n,__BYTE_ORDER__==__ORDER_BIG_ENDIAN__);}
