/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "battle/btl_obj.h"
#include <assert.h>
#include <stdio.h>
static unsigned char previous[32],fresh[32];
static int frees,requests,refuse,last_id,last_size;static void *last_buffer;
void Heap_Free(void *p){assert(p==previous);frees++;}
static void *request(int id,void *p,int n){requests++;last_id=id;last_size=n;last_buffer=p;return refuse?NULL:p?p:fresh;}
#define File_Request3(id,p,n) request(id,p,n)
/* ORIGINAL_FUNCTIONS */
int main(void){
 for(int heap=0;heap<2;heap++)for(refuse=0;refuse<2;refuse++){
  BtlResFile file={0};file.buf=previous;file.id=42;file.size=1001;frees=requests=0;
  BtlRes_LoadFile(&file,heap);assert(requests==1 && last_id==42 && file.id==42 && file.size==1001);
  if(heap)assert(frees==1 && !last_buffer && last_size==0 && file.buf==(refuse?NULL:fresh));
  else assert(!frees && last_buffer==previous && last_size==1001 && file.buf==previous);
 }
 BtlResFile disabled={0};disabled.buf=previous;disabled.id=-1;disabled.size=99;frees=requests=0;BtlRes_LoadFile(&disabled,1);assert(frees==1 && !requests && !disabled.buf && disabled.id==-1 && !disabled.size);
 puts("PASS original BtlRes_LoadFile/FreeFile: selected id/size survive replacing an allocated file; refused borrowed destination retains owner; disabled resource cleared; no model format/device execution");return 0;
}
