/* SPDX-License-Identifier: MIT */
#ifndef DR_ONLINE_BATTLE_H
#define DR_ONLINE_BATTLE_H
#include <stdint.h>
#include "lobby.h"

/* View structure for battle state */
typedef struct {
    uint32_t frame;
    int32_t position_x[2], position_y[2];
    int32_t health[2];
} BattleStateView;

/* Initialize online battle with paired lobby connection */
int dr_online_battle_init(DrLobby *lobby);

/* Update battle state with input synchronization.
 * Returns: 1 on success, 0 if not active, -1 on desync */
int dr_online_battle_update(uint32_t local_input, uint32_t *remote_input);

/* Get current battle state hash */
uint32_t dr_online_battle_get_state_hash(void);

/* Shutdown online battle mode */
void dr_online_battle_shutdown(void);

/* Check if online battle is active */
int dr_online_battle_active(void);

/* Get battle state for rendering */
int dr_online_battle_get_state(BattleStateView *view);

#endif