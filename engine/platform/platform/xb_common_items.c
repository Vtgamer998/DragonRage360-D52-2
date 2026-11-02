/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_common_items.h"
#include "xb_resconv.h"
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define XB_NATIVE_BIG_ENDIAN 1
#else
#define XB_NATIVE_BIG_ENDIAN 0
#endif

#include "xb_battle_common.h"
static int common_items(void *p,size_t n,int id){
    if(id!=4)return -897;
    return XbBattleCommon_PrepareItems(p,n,XB_NATIVE_BIG_ENDIAN);
}
int XbRes_RegisterCommonItems(void){
    return XbRes_RegisterChecked(4,4,common_items,0);
}
