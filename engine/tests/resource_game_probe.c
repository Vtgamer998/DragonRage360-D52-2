/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <stdlib.h>
#include "battle/bobj_a.h"
#include "battle/bobj_b.h"
#include "sys/mem_read.h"
/* Executes the original game's hit-window evaluator, no substitute combat code.
 * Host native structs are used consistently; this is not a PPC layout test. */
int resource_game_probe(void *motion)
{
    BobjObj *obj=calloc(1,sizeof(*obj));int result=0;
    if(!obj) return 0;
    obj->anim.layer[0].data=motion;obj->hit.scale=2.0f;
    obj->anim.prevFrame=4.0f;obj->anim.frame=6.0f;
    BtlObjAnim_UpdateEvents(obj);
    if(obj->hit.hitIndex!=0 || obj->hit.nodeMask!=8 || obj->hit.sphere!=1 || obj->hit.x!=1.25f || obj->hit.r0!=4.0f) goto end;
    if(!BtlObjAnim_TestEvent(obj,1)) goto end;
    obj->anim.prevFrame=10.0f;obj->anim.frame=11.0f;
    BtlObjAnim_UpdateEvents(obj);
    if(obj->hit.hitIndex>=0 || obj->hit.nodeMask!=0) goto end;
    result=1;
end:
    free(obj);return result;
}
int resource_mem_probe(const unsigned char *p)
{
    return Mem_ReadU32(p+1)==0x89abcdefu && Mem_ReadS32(p+1)==(-1985229329) &&
        Mem_ReadU16(p+5)==0xfffe && Mem_ReadS16(p+5)==-2 && Mem_ReadF32(p+7)==-2.5f;
}
void *Heap_Alloc(s32 n,u32 align,s32 tail,s32 heap)
{ (void)align;(void)tail;(void)heap;return n>=0?calloc(1,(size_t)n):NULL; }

int resource_file_probe(void *pack,int bytes,int offset)
{
    BObjFile file={0};file.buf=pack;file.size=bytes;
    return BObjFile_GetEntry(&file,1)==(unsigned char *)pack+offset &&
           BObjFile_GetEntry(&file,0)==NULL && BObjFile_GetEntry(&file,-1)==NULL &&
           BObjFile_GetEntry(&file,4097)==NULL;
}
