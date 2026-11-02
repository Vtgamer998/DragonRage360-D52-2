/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Freestanding i386 test of the ACTUAL patched game function, 32-bit pointers.
 * Emulation (when needed) is Linux i386 user mode, NOT Xbox/PPC emulation.
 */
#include <stddef.h>
#include "battle/bobj_a.h"
#undef BOBJ_NODE_MAX
#include "battle/bobj_b.h"
_Static_assert(sizeof(void *)==4,"32-bit test ABI");
_Static_assert(offsetof(BObj,slot)==0x14,"shared object slot view");
_Static_assert(offsetof(BObj,mdl)==offsetof(BobjObj,mdl),"shared model block");
static BobjObj objects[12],unknown;
static BObjSlot slots[12];
static BobjWork fighter_work;
static unsigned char pack[160],before[160];
void *memcpy(void *a,const void *b,size_t n) {unsigned char *d=a;const unsigned char *s=b;for(size_t i=0;i<n;i++)d[i]=s[i];return a;}
void *memset(void *a,int c,size_t n) {unsigned char *d=a;for(size_t i=0;i<n;i++)d[i]=(unsigned char)c;return a;}
int memcmp(const void *a,const void *b,size_t n) {const unsigned char *x=a,*y=b;for(size_t i=0;i<n;i++)if(x[i]!=y[i])return x[i]-y[i];return 0;}
void *BtlObj_Get(int i) {return i>=0 && i<12?&objects[i]:NULL;}
static int test(void)
{
    pack[0]=1;pack[4]=12;pack[8]=160;pack[14]=30;
    slots[0].file[0].buf=(void *)pack;slots[0].file[0].size=sizeof(pack);
    ((BObj *)&objects[0])->slot=&slots[0];objects[0].mdl.modelAnims[0]=(void *)(pack+12);
    memcpy(before,pack,sizeof(pack));
    BtlObjAnim_PlayModel(&objects[0],0,BOBJ_PLAY_LOOP);
    if(!objects[0].anim.layer[0].data || objects[0].anim.layer[0].data==(void *)(pack+12) || objects[0].anim.layer[0].length!=30 || objects[0].anim.mode!=BOBJ_PLAY_LOOP) return 1;
    if(memcmp(before,pack,sizeof(pack))) return 2;
    void *stable=objects[0].anim.layer[0].data;
    BtlObjAnim_PlayModel(&objects[0],0,BOBJ_PLAY_LOOP);
    if(objects[0].anim.layer[0].data!=stable || objects[0].anim.layer[0].length!=30) return 3;
    pack[14]=45;BtlObjAnim_PlayModel(&objects[0],0,BOBJ_PLAY_ONCE);
    if(objects[0].anim.layer[0].length!=45 || objects[0].anim.mode!=BOBJ_PLAY_ONCE || pack[14]!=45) return 4;
    /* Fighter uses the actual work layer; the no-work object used the side arena. */
    ((BObj *)&objects[1])->slot=&slots[0];objects[1].work=&fighter_work;objects[1].mdl.modelAnims[0]=(void *)(pack+12);
    BtlObjAnim_PlayModel(&objects[1],0,BOBJ_PLAY_LOOP);
    if(objects[1].anim.layer[0].data!=(void *)fighter_work.layer[0] || objects[1].anim.layer[0].length!=45) return 5;
    objects[0].mdl.modelAnims[0]=NULL;BtlObjAnim_PlayModel(&objects[0],0,BOBJ_PLAY_LOOP);
    if(objects[0].anim.layer[0].data || objects[0].anim.mode!=0) return 6;
    objects[0].mdl.modelAnims[0]=(void *)(pack+12);pack[8]=255;
    BtlObjAnim_PlayModel(&objects[0],0,BOBJ_PLAY_LOOP);
    if(objects[0].anim.layer[0].data || objects[0].anim.mode!=0) return 7;
    pack[8]=160;pack[14]=55;BtlObjAnim_PlayModel(&objects[0],0,BOBJ_PLAY_LOOP);
    if(objects[0].anim.layer[0].data!=stable || objects[0].anim.layer[0].length!=55) return 8;
    ((BObj *)&unknown)->slot=&slots[0];unknown.mdl.modelAnims[0]=(void *)(pack+12);
    BtlObjAnim_PlayModel(&unknown,0,BOBJ_PLAY_LOOP);
    if(unknown.anim.layer[0].data || unknown.anim.mode!=0) return 9;
    return 0;
}
void _start(void) {int result=test();__asm__ volatile("int $0x80"::"a"(1),"b"(result));__builtin_unreachable();}
