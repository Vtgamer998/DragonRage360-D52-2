/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <stddef.h>
#include "battle/bobj_a.h"
_Static_assert(BOBJ_NODE_MAX==71,"node count");
_Static_assert(BOBJ_WORK_LAYER_SIZE==0xc000,"layer capacity");
_Static_assert(offsetof(BobjAnim,length)==2,"motion length");
_Static_assert(offsetof(BobjAnim,track)==6,"track offsets");
_Static_assert(offsetof(BobjAnim,event)==0x94,"event start");
_Static_assert(offsetof(BobjTrack,u)==4,"track data");
_Static_assert(sizeof(BobjPackedQuat)==8,"quaternion word pair");
_Static_assert(sizeof(BobjKey)==24,"full key");
_Static_assert(sizeof(BobjAnimEvent)==16,"event");
_Static_assert(sizeof(BobjAnimShape)==28,"hit shape");

_Static_assert(sizeof(BobjBone)==64,"bone record");
_Static_assert(offsetof(BobjBone,rest)==48,"bone rest vector");
#undef BOBJ_NODE_MAX
#include "battle/bobj_b.h"
_Static_assert(offsetof(BObj,slot)==0x14,"slot pointer in target object");
_Static_assert(offsetof(BObj,mdl)==0x18,"model block in target object");
_Static_assert(sizeof(BObjFile)==16,"target file record");
