/* SPDX-License-Identifier: MIT
 * Independent canonical serializer for the semantic fields in official
 * z3xox/Tenkaichi3Decomp headless.c Port_NetDigest, commit 7b43a75.
 * Same FNV-1a digest as the current PC build; independent of native layout.
 * This does not implement a session, game-state capture or rollback.
 */
#include "bt3_sync.h"
int bt3_sync_bytes(uint8_t out[BT3_SYNC_BYTES],const Bt3SyncState *s) {
    if(!out || !s)return -1;
    uint32_t words[14]={s->arrived,(uint32_t)s->mode,(uint32_t)s->first,
        (uint32_t)s->battle,(uint32_t)s->sequence,(uint32_t)s->clock,
        (uint32_t)s->hp[0],(uint32_t)s->hp[1],s->position_bits[0][0],
        s->position_bits[0][1],s->position_bits[0][2],s->position_bits[1][0],
        s->position_bits[1][1],s->position_bits[1][2]};
    for(unsigned i=0;i<14;i++)for(unsigned j=0;j<4;j++)out[i*4+j]=(uint8_t)(words[i]>>(j*8));
    return 0;
}
int bt3_sync_digest(const Bt3SyncState *s,uint64_t *out) {
    uint8_t bytes[BT3_SYNC_BYTES];
    if(!out || bt3_sync_bytes(bytes,s))return -1;
    uint64_t hash=0xcbf29ce484222325ull;
    for(unsigned i=0;i<sizeof(bytes);i++)hash=(hash^bytes[i])*0x100000001b3ull;
    *out=hash;return 0;
}
