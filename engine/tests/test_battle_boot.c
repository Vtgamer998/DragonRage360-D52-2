#include "xb_battle_boot.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 XbBattleBootGate gate={0};
 assert(XbBattleBoot_ReadyForOriginal(&gate));
 assert(XbBattleBoot_Enter(&gate,XB_BATTLE_BOOT_DIAGNOSTIC)==0);
 assert(!XbBattleBoot_ReadyForOriginal(&gate));
 assert(XbBattleBoot_Enter(&gate,XB_BATTLE_BOOT_ORIGINAL)==XB_BATTLE_BOOT_BUSY);
 assert(XbBattleBoot_Leave(&gate,XB_BATTLE_BOOT_ORIGINAL)==XB_BATTLE_BOOT_BAD_STATE);
 assert(XbBattleBoot_Leave(&gate,XB_BATTLE_BOOT_DIAGNOSTIC)==0);
 assert(XbBattleBoot_Enter(&gate,XB_BATTLE_BOOT_ORIGINAL)==0);
 assert(XbBattleBoot_Enter(&gate,XB_BATTLE_BOOT_ORIGINAL)==XB_BATTLE_BOOT_BUSY);
 assert(XbBattleBoot_Leave(&gate,XB_BATTLE_BOOT_ORIGINAL)==0);
 assert(gate.generation==2);
 puts("D19 boot gate: 9 assertions passed");return 0;
}
