/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_BATTLE_COMMON_H
#define XB_BATTLE_COMMON_H
#include <stddef.h>
typedef struct XbBattleCommon XbBattleCommon;
typedef struct XbBattleCommonInfo {
 unsigned roster_records[6],ai_steps,ai_rules,ai_profiles;
} XbBattleCommonInfo;
/* Owned selected CPU resources from common file 2, never a whole converted
 * CommonRes. The remaining entries, common files 3/4, battle setup and stage
 * are NOT prepared here. No globals or readiness flags are modified. */
int XbBattleCommon_Prepare(const void *,size_t,XbBattleCommon **,XbBattleCommonInfo *);
void XbBattleCommon_Free(XbBattleCommon *);
int XbBattleCommon_GetRosterTable(XbBattleCommon *,unsigned,const void **,size_t *);
/* Mutable native AI data with internal pointers, ONLY on pointer32 targets.
 * Borrowed for the owner's lifetime; not an initialized BtlAi manager. */
int XbBattleCommon_GetAiData(XbBattleCommon *,void **,size_t *);
/* Endian encoder for independent field-level validation. Transactional and
 * non-overlapping. Kinds 0..5 are the roster tables, 6 is common AI data.
 * AI pointer slots remain zero; Prepare relocates them only on pointer32. */
int XbBattleCommon_Convert(unsigned,const void *,size_t,int,void *,size_t,size_t *);
/* Transactional native common-4 tables; every character/item scalar is typed.
 * This does not supply common file 3 or initialize fighter managers. */
int XbBattleCommon_AttachItems(XbBattleCommon *,const void *,size_t);
/* In-place raw file 4; validates the entire file before any mutation.
 * Relative offsets stay relative; this does not bind consumers or init a fight. */
int XbBattleCommon_PrepareItems(void *,size_t,int);
int XbBattleCommon_ConvertItems(const void *,size_t,int,void *,size_t);
int XbBattleCommon_GetItems(XbBattleCommon *,void **,size_t *,unsigned *,unsigned *);
typedef struct XbBattleConsumerProbe {unsigned animations,voices,characters,items;int first_cost,first_ai;} XbBattleConsumerProbe;
/* Temporary isolated installation in original readers, always undone before
 * returning. Refuses occupied globals; never allocates/resets fighters. */
int XbBattleCommon_ProbeConsumers(XbBattleCommon *,XbBattleConsumerProbe *);
#endif
