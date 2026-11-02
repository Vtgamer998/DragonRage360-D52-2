/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_match_ui_pack.h"
#include "xb_resconv.h"
#include "dr_container.h"
#include "dr_texfile.h"
#include <stdint.h>
#include <string.h>
#ifdef malloc
#undef malloc
#endif
#include <stdlib.h>
#define BIG (__BYTE_ORDER__==__ORDER_BIG_ENDIAN__)
int XbMatchUiPack_Prepare(void*buf,size_t n){
 if(!buf||n<199*4||n>2*1024*1024||XbRes_Le32(buf)!=197)return -945;
 unsigned prev=199*4;for(unsigned i=0;i<=197;i++){unsigned at=XbRes_Le32((const uint8_t*)buf+4+i*4);if((at&3)||at<prev||at>n)return -945;prev=at;}if(prev!=n)return -945;
 const uint8_t*s=buf;uint8_t*d=malloc(n);if(!d)return -944;memcpy(d,s,n);int e=0;
 for(unsigned i=0;i<197;i++){
  size_t at,bytes;unsigned flags;e=dr_pack_memory_entry(s,n,i,&at,&bytes,&flags);
  if(e==DR_NOT_FOUND){e=0;continue;}if(e||flags){e=-945;break;}
  if(i<7)e=dr_texfile_prepare(d+at,bytes,BIG);
  else{
   if(bytes<4||bytes%2||s[at]!=0xff||s[at+1]!=0xfe){e=-945;break;}
   int end=0;for(size_t j=0;j<bytes;j+=2){uint16_t v=s[at+j]|(unsigned)s[at+j+1]<<8;if(j&&!v)end=1;memcpy(d+at+j,&v,2);}
   if(!end){e=-945;break;}
  }if(e)break;
 }
 if(!e){for(unsigned i=0;i<199;i++){uint32_t v=XbRes_Le32(s+i*4);memcpy(d+i*4,&v,4);}memcpy(buf,d,n);}free(d);return e;
}
static int prepare(void*p,size_t n,int id){if(id!=6)return -945;return XbMatchUiPack_Prepare(p,n);}
int XbMatchUiPack_RegisterInitial(void){return XbRes_RegisterChecked(6,6,prepare,0);}
