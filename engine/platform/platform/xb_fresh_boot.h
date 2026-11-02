/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_FRESH_BOOT_H
#define XB_FRESH_BOOT_H
/* Scoped original boot and bounded simulation probes, with independent
 * common/progress/save/object/job owners. No Battle_Main entry authorization;
 * gPortDataStripped remains unchanged. Partial initializers can retain owners. */
int XbFreshBoot_SetPresentation(int (*frame)(void));
int XbFreshBoot_SetOriginalLoading(int type,int (*frame)(int));
int XbFreshBoot_Frame(int phase);
int XbFreshBoot_ForcedLoadingType(void);
int XbFreshBoot_Probe(void);
int XbFreshBoot_LoadProbe(void);
int XbFreshBoot_InitProbe(void);
int XbFreshBoot_FrameProbe(void);
int XbFreshBoot_FramesProbe(void);
int XbFreshBoot_IntroProbe(void);
int XbFreshBoot_FighterProbe(void);
int XbFreshBoot_StageProbe(void);
int XbFreshBoot_FightProbe(void);
int XbFreshBoot_BattleProbe(void);
int XbFreshBoot_BattleCheck(void);
int XbFreshBoot_ActionCheck(void);
int XbFreshBoot_ChargeCheck(void);
int XbFreshBoot_ActionFastCheck(void);
void XbFreshBoot_TraceInit(const char *);
int XbFreshBoot_RecoveryProbe(void);
/* Internal scoped queue driver, used only while LoadProbe owns the gate. */
int XbFreshBoot_DrivesLoading(void);
void XbFreshBoot_PumpLoading(void);
#endif
