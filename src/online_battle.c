/* SPDX-License-Identifier: MIT
 * D52-2: Online Battle Integration Module
 * Integrates the lobby with the bt3xbx battle engine for deterministic
 * online multiplayer with input synchronization and state hashing.
 */
#include "online_battle.h"
#include "lobby.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* Battle state structure for synchronization */
typedef struct {
    uint32_t frame;
    int32_t position_x[2], position_y[2];
    int32_t velocity_x[2], velocity_y[2];
    int32_t health[2];
    uint32_t input_state[2];
    uint32_t input_frame[2];
} BattleState;

/* Global battle state */
static BattleState battle_state;
static DrLobby *lobby_connection;
static int online_active;

/* Initialize online battle mode */
int dr_online_battle_init(DrLobby *lobby)
{
    if(!lobby || lobby->state != LOBBY_PAIRED) return -1;
    
    memset(&battle_state, 0, sizeof(battle_state));
    lobby_connection = lobby;
    online_active = 1;
    
    /* Initialize deterministic state */
    battle_state.position_x[0] = 100;
    battle_state.position_x[1] = 300;
    battle_state.position_y[0] = 200;
    battle_state.position_y[1] = 200;
    battle_state.health[0] = 1000;
    battle_state.health[1] = 1000;
    
    return 0;
}

/* Update online battle state with input synchronization */
int dr_online_battle_update(uint32_t local_input, uint32_t *remote_input)
{
    if(!online_active || !lobby_connection) return 0;
    
    /* Send local input to remote player */
    dr_lobby_send_input(lobby_connection, local_input);
    
    /* Try to receive remote input */
    uint32_t received_input = 0;
    if(dr_lobby_receive_input(lobby_connection, &received_input)) {
        *remote_input = received_input;
    } else {
        *remote_input = 0;  /* No remote input this frame */
    }
    
    /* Update battle state deterministically */
    battle_state.frame++;
    battle_state.input_state[0] = local_input;
    battle_state.input_state[1] = *remote_input;
    
    /* Simple deterministic physics simulation */
    for(int i = 0; i < 2; i++) {
        /* Process input */
        if(battle_state.input_state[i] & 0x0001) {  /* Left */
            battle_state.velocity_x[i] = -5;
        } else if(battle_state.input_state[i] & 0x0002) {  /* Right */
            battle_state.velocity_x[i] = 5;
        } else {
            battle_state.velocity_x[i] = 0;
        }
        
        if(battle_state.input_state[i] & 0x0004) {  /* Up */
            battle_state.velocity_y[i] = -5;
        } else if(battle_state.input_state[i] & 0x0008) {  /* Down */
            battle_state.velocity_y[i] = 5;
        } else {
            battle_state.velocity_y[i] = 0;
        }
        
        /* Update positions */
        battle_state.position_x[i] += battle_state.velocity_x[i];
        battle_state.position_y[i] += battle_state.velocity_y[i];
    }
    
    /* Compute and send state hash */
    uint32_t state_hash = dr_online_battle_get_state_hash();
    dr_lobby_send_hash(lobby_connection, state_hash);
    
    /* Check for remote hash */
    uint32_t remote_hash = 0;
    if(dr_lobby_receive_hash(lobby_connection, &remote_hash)) {
        if(remote_hash != state_hash) {
            /* Desync detected - could trigger rollback here */
            return -1;
        }
    }
    
    return 1;
}

/* Get current battle state hash */
uint32_t dr_online_battle_get_state_hash(void)
{
    return compute_battle_state_hash(&battle_state, sizeof(battle_state));
}

/* Shutdown online battle mode */
void dr_online_battle_shutdown(void)
{
    online_active = 0;
    lobby_connection = NULL;
    memset(&battle_state, 0, sizeof(battle_state));
}

/* Check if online battle is active */
int dr_online_battle_active(void)
{
    return online_active;
}

/* Get battle state for rendering */
int dr_online_battle_get_state(BattleStateView *view)
{
    if(!view || !online_active) return -1;
    
    view->frame = battle_state.frame;
    view->position_x[0] = battle_state.position_x[0];
    view->position_x[1] = battle_state.position_x[1];
    view->position_y[0] = battle_state.position_y[0];
    view->position_y[1] = battle_state.position_y[1];
    view->health[0] = battle_state.health[0];
    view->health[1] = battle_state.health[1];
    
    return 0;
}