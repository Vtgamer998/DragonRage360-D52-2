/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "port_compat.h"
#undef malloc
#include "xb_battle_common.h"
#include "battle/btl_char_mgr.h"
#include "battle/view_b.h"
#include "sys/common.h"
#include <stdint.h>
#include <string.h>

#if UINTPTR_MAX==UINT32_MAX
extern u32 BtlAnim_GetFlags(u32);
extern void *BtlChar_GetVoiceEntry(s32);
static uint32_t word(const void *p){uint32_t u;memcpy(&u,p,4);return u;}
_Static_assert(sizeof(BtlCharMgr)==0x280,"original roster ABI");
#endif
int XbBattleCommon_ProbeConsumers(XbBattleCommon *c,XbBattleConsumerProbe *out){
 if(!out)return -880;
 memset(out,0,sizeof(*out));
#if UINTPTR_MAX!=UINT32_MAX
 (void)c;return -882;
#else
 if(!c || gBtlChars || gCommonRes)return -882;
 BtlCharMgr roster={0};CommonRes common={0};XbBattleConsumerProbe r={0};
 void *items=NULL;size_t bytes=0;
 int e=XbBattleCommon_GetItems(c,&items,&bytes,&r.characters,&r.items);if(e)return e;
 for(unsigned i=0;i<6;i++){
  const void *p;size_t n;e=XbBattleCommon_GetRosterTable(c,i,&p,&n);if(e)return e;
  roster.tbl[i]=(u32 *)p;
 }
 common.data[2]=items;common.size[2]=(s32)bytes;
 /* Zero fighter count: this publishes TABLE READERS, not combatants. Only
  * synchronous bounded calls occur; no frame/audio/reset/render callbacks. */
 gBtlChars=&roster;gCommonRes=&common;
 for(unsigned i=0;i<414;i++){
  if(BtlAnim_GetFlags(i)!=roster.tbl[0][i]){e=-883;goto done;}r.animations++;
 }
 if(BtlAnim_GetFlags(414) || BtlAnim_GetFlags(UINT32_MAX)){e=-883;goto done;}
 for(unsigned i=0;i<57;i++){
  if(BtlChar_GetVoiceEntry((s32)i)!=(uint8_t *)roster.tbl[2]+8*i){e=-883;goto done;}r.voices++;
 }
 for(unsigned i=0;i<r.characters;i++){
  const uint8_t *ch=(uint8_t *)items+(word((uint8_t *)items+4)&~3u)+i*60;
  uint16_t cost;memcpy(&cost,ch+12,2);
  if(ChrTbl_GetCost((s32)i)!=cost){e=-883;goto done;}
  u16 ids[8]={0};s32 stats[5],ability[4];ItemSet_GetStats(ids,stats,ability,(s32)i);
  if(stats[4]!=(s32)word(ch)){e=-883;goto done;}
  if(!i){r.first_cost=cost;r.first_ai=stats[4];}
 }
 for(unsigned i=0;i<r.items;i++){
  const uint8_t *it=(uint8_t *)items+(word((uint8_t *)items+8)&~3u)+i*40;
  u16 ids[8]={0};s32 stats[5],ability[4];ids[0]=(u16)(i+1);
  ItemSet_GetStats(ids,stats,ability,0);
  for(unsigned j=0;j<4;j++){
   int16_t v;memcpy(&v,it+12+j*2,2);
   if(stats[j]!=(it[0]==2?0:v) || ability[j]!=(it[0]==2?0:(s32)word(it+24+j*4))){e=-883;goto done;}
  }
 }
 *out=r;
done:
 gCommonRes=NULL;gBtlChars=NULL;return e;
#endif
}
