/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_battle_boot.h"
int XbBattleBoot_Enter(XbBattleBootGate *gate,XbBattleBootMode mode){
 if(!gate || (mode!=XB_BATTLE_BOOT_DIAGNOSTIC && mode!=XB_BATTLE_BOOT_ORIGINAL))return XB_BATTLE_BOOT_BAD_STATE;
 if(gate->mode!=XB_BATTLE_BOOT_NONE)return XB_BATTLE_BOOT_BUSY;
 gate->mode=mode;gate->generation++;return XB_BATTLE_BOOT_OK;
}
int XbBattleBoot_Leave(XbBattleBootGate *gate,XbBattleBootMode mode){
 if(!gate || gate->mode!=mode || mode==XB_BATTLE_BOOT_NONE)return XB_BATTLE_BOOT_BAD_STATE;
 gate->mode=XB_BATTLE_BOOT_NONE;return XB_BATTLE_BOOT_OK;
}
int XbBattleBoot_ReadyForOriginal(const XbBattleBootGate *gate){
 return gate && gate->mode==XB_BATTLE_BOOT_NONE;
}
