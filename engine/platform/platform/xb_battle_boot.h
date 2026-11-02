/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_BATTLE_BOOT_H
#define XB_BATTLE_BOOT_H
/* Exclusive ownership gate for diagnostic versus original battle sessions.
 * This does not mark resources ready or call the original Battle_Init. */
typedef enum XbBattleBootMode {
 XB_BATTLE_BOOT_NONE=0, XB_BATTLE_BOOT_DIAGNOSTIC=1,
 XB_BATTLE_BOOT_ORIGINAL=2
} XbBattleBootMode;
typedef struct XbBattleBootGate { XbBattleBootMode mode; unsigned generation; } XbBattleBootGate;
enum {XB_BATTLE_BOOT_OK=0, XB_BATTLE_BOOT_BUSY=-901, XB_BATTLE_BOOT_BAD_STATE=-902};
int XbBattleBoot_Enter(XbBattleBootGate *gate,XbBattleBootMode mode);
int XbBattleBoot_Leave(XbBattleBootGate *gate,XbBattleBootMode mode);
int XbBattleBoot_ReadyForOriginal(const XbBattleBootGate *gate);
/* Require strict transport, registered common converters and installed roots.
 * The diagnostic's installed resources/registry
 * cannot be booted over or reclaimed by the original load rollback. */
int XbBattleBoot_ObjectStateClear(void);
int XbBattleBoot_OriginalObjectStateActive(void);
#endif
