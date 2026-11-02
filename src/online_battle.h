/* SPDX-License-Identifier: MIT */
#ifndef DR_ONLINE_BATTLE_H
#define DR_ONLINE_BATTLE_H
#include <stdint.h>
#include "lobby.h"

#define D52_HASH_RING 16u

/* Snapshot of the harness state, for rendering or logging. */
typedef struct {
    uint32_t frame;
    int32_t position_x[2], position_y[2];
    int32_t health[2];
} BattleStateView;

/* One lockstep battle instance. It is a plain struct (no heap, no globals) so
 * the host test can run both sides in one process and the console can keep a
 * single static instance.
 *
 * Fields are internal; call the dr_online_battle_* accessors instead of
 * touching them directly. */
typedef struct {
    DrLobby *lobby;
    int active;
    int side;                   /* 0 = player one, 1 = player two */
    uint32_t frame;             /* next frame to simulate (starts at 1) */
    /* simulation state: every field is a 32-bit integer */
    int32_t px[2], py[2];
    int32_t vx[2], vy[2];
    int32_t hp[2];
    uint32_t in[2];
    int local_ready, remote_ready;
    uint32_t local_value, remote_value;
    /* our own hashes by frame, so a late peer hash can still be checked */
    uint32_t own_hash[D52_HASH_RING];
    uint32_t own_hash_frame[D52_HASH_RING];
    int desynced;
    uint32_t desync_frame, desync_local, desync_remote;
    uint32_t matched;
} DrOnlineBattle;

/* Bind an instance to a paired lobby.
 *   side - 0 when this console drives player one (the host), 1 for player
 *          two. Both consoles must use opposite sides, otherwise they build
 *          different states from the same inputs.
 * Returns 1 on success, 0 if the lobby is not paired (retry next frame). */
int dr_online_battle_init(DrOnlineBattle *b,DrLobby *lobby,int side);

/* Advance the lockstep simulation.
 *   local_input  - this console's pad bits for the current frame
 *   remote_input - receives the peer's pad bits for the current frame (0
 *                  while they are still unknown)
 * Returns  1 when the frame was simulated,
 *          0 when waiting for the peer's input,
 *         -1 when the instance is not active.
 * The caller must keep calling this every tick: a frame only advances once
 * both inputs for it are known, which is what keeps the two consoles on
 * identical frames. */
int dr_online_battle_update(DrOnlineBattle *b, uint32_t local_input,
                            uint32_t *remote_input);

/* Desync report. Returns 1 when a peer hash disagreed with ours and fills in
 * the frame and the two hashes. */
int dr_online_battle_desync(const DrOnlineBattle *b,uint32_t *frame,
                            uint32_t *local,uint32_t *remote);
/* Number of frames whose peer hash matched ours. */
uint32_t dr_online_battle_matched(const DrOnlineBattle *b);

/* Hash of the current state (deterministic, byte-exact). */
uint32_t dr_online_battle_get_state_hash(const DrOnlineBattle *b);
/* Next frame the instance will simulate (starts at 1). */
uint32_t dr_online_battle_frame(const DrOnlineBattle *b);
/* 1 while the instance is bound to a lobby. */
int dr_online_battle_active(const DrOnlineBattle *b);
/* Current state for rendering. Returns 0 on success. */
int dr_online_battle_get_state(const DrOnlineBattle *b,BattleStateView *view);
/* Unbind and clear the instance. */
void dr_online_battle_shutdown(DrOnlineBattle *b);

#endif
