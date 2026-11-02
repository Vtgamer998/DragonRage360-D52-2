/* SPDX-License-Identifier: MIT */
#ifndef DR_ONLINE_BATTLE_H
#define DR_ONLINE_BATTLE_H
#include <stdint.h>
#include "lobby.h"
#include "engine_adapter.h"

#define D52_HASH_RING 16u

/* Snapshot of the reference backend's state, for rendering or logging.
 * Only meaningful when the instance is bound to dr_reference_ops: with the
 * real engine the battle is drawn by the engine itself. */
typedef struct {
    uint32_t frame;
    int32_t position_x[2], position_y[2];
    int32_t health[2];
} BattleStateView;

/* One lockstep battle instance. A plain struct (no heap, no globals) so the
 * host test can run both sides in one process while the console keeps a
 * single static instance.
 *
 * Fields are internal; call the dr_online_battle_* accessors instead. */
typedef struct {
    DrLobby *lobby;
    int active;
    int side;                   /* 0 = player one, 1 = player two */
    uint32_t frame;             /* next protocol frame to simulate (starts 1) */
    /* Backend state. When bound to dr_reference_ops (the default) this is the
     * ctx handed to the adapter, so the simulation lives here. */
    DrReferenceState ref;
    DrEngineAdapter engine;     /* D53-A: the seam to the battle engine */
    int local_ready, remote_ready;
    uint32_t local_value, remote_value;
    /* our own hashes by frame, so a late peer hash can still be checked */
    uint32_t own_hash[D52_HASH_RING];
    uint32_t own_hash_frame[D52_HASH_RING];
    int desynced;
    uint32_t desync_frame, desync_local, desync_remote;
    uint32_t matched;
} DrOnlineBattle;

/* Bind an instance to a paired lobby and start the DEFAULT backend
 * (dr_reference_ops, the integer test simulation).
 *   side - 0 when this console drives player one (the host), 1 for player
 *          two. Both consoles must use opposite sides, otherwise they build
 *          different states from the same inputs.
 * Returns 1 on success, 0 if the lobby is not paired (retry next frame),
 * -1 on error (see dr_engine_error()). */
int dr_online_battle_init(DrOnlineBattle *b,DrLobby *lobby,int side);

/* Same, but with an explicit backend: bind dr_bt3xbx_ops here to run the real
 * engine instead of the reference simulation. ctx must stay valid for the
 * lifetime of the instance; NULL ctx means "use b->ref" (only correct for the
 * reference backend). */
int dr_online_battle_init_with(DrOnlineBattle *b,DrLobby *lobby,int side,
                               const DrEngineOps *ops,void *ctx);

/* Advance the lockstep simulation.
 *   local_input  - this console's pad bits for the current frame
 *   remote_input - receives the peer's pad bits for the current frame (0
 *                  while they are still unknown)
 * Returns  1 when the frame was simulated,
 *          0 when waiting for the peer's input,
 *         -1 when the instance is not active or the backend failed.
 * The caller must keep calling this every tick: a frame only advances once
 * both inputs for it are known, which is what keeps the two consoles on
 * identical frames. */
int dr_online_battle_update(DrOnlineBattle *b,uint32_t local_input,
                            uint32_t *remote_input);

/* Desync report. Returns 1 when a peer hash disagreed with ours and fills in
 * the frame and the two hashes. */
int dr_online_battle_desync(const DrOnlineBattle *b,uint32_t *frame,
                            uint32_t *local,uint32_t *remote);
/* Number of frames whose peer hash matched ours. */
uint32_t dr_online_battle_matched(const DrOnlineBattle *b);

/* Hash of the current engine state (deterministic, byte-exact); 0 while not
 * running. Delegates to the bound backend. */
uint32_t dr_online_battle_get_state_hash(const DrOnlineBattle *b);
/* Next protocol frame the instance will simulate (starts at 1). */
uint32_t dr_online_battle_frame(const DrOnlineBattle *b);
/* 1 while the instance is bound and running. */
int dr_online_battle_active(const DrOnlineBattle *b);
/* Reference backend state for rendering. Returns 0 on success, -1 when the
 * instance is inactive or bound to another backend. */
int dr_online_battle_get_state(const DrOnlineBattle *b,BattleStateView *view);
/* Name of the bound backend, for the log. */
const char *dr_online_battle_backend(const DrOnlineBattle *b);
/* Unbind, stop the engine and clear the instance. Safe to call twice. */
void dr_online_battle_shutdown(DrOnlineBattle *b);

#endif
