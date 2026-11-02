/* SPDX-License-Identifier: MIT */
#ifndef DR_LOBBY_H
#define DR_LOBBY_H
#include <stddef.h>
#include <stdint.h>
#include <netinet/in.h>
enum { LOBBY_OFF, LOBBY_HOST, LOBBY_SEARCH, LOBBY_JOIN, LOBBY_PAIRED };
/* D52-2: depth of the input window each side keeps for reordering. Must be a
 * power of two because frames are mapped to slots with `frame & MASK`. */
#define D52_INPUT_DEPTH 16u
#define D52_INPUT_MASK  (D52_INPUT_DEPTH-1u)
typedef struct {
    int fd, state, host;
    uint32_t nonce, session, last_send, last_seen, started;
    struct sockaddr_in target, peer;
    char message[240];
    /* D52-2: deterministic input / state-hash layer. Everything below is only
     * used while state==LOBBY_PAIRED; the D52-1 fields above are untouched so
     * an old console still pairs with a new one. */
    uint32_t tick_now;          /* last now passed to dr_lobby_tick */
    /* transmit window: frames we have queued but the peer may not have */
    uint32_t tx_highest;        /* newest frame handed to us by the game */
    uint32_t tx_acked;          /* highest frame the peer confirmed */
    uint32_t tx_sent;           /* bitmask of slots holding an unacked frame */
    uint32_t tx_value[D52_INPUT_DEPTH];
    uint32_t tx_frame_of[D52_INPUT_DEPTH];
    uint32_t tx_sent_at[D52_INPUT_DEPTH];
    uint8_t  tx_rtx[D52_INPUT_DEPTH];
    /* receive window: frames the peer sent, reordered and deduplicated */
    uint32_t rx_frame;          /* highest contiguous frame received */
    uint32_t rx_delivered;      /* highest frame already handed to the game */
    uint32_t rx_present;        /* bitmask of buffered frames past rx_frame */
    uint32_t rx_value[D52_INPUT_DEPTH];
    uint32_t rx_frame_of[D52_INPUT_DEPTH];
} DrLobby;
void dr_lobby_init(DrLobby *l);
/* Stable name for logs: "off", "host", "search", "join" or "paired". */
const char *dr_lobby_state_name(int state);
/* Parse one lobby_host.txt line: "IPv4" (default UDP 7371) or "IPv4 PORT".
 * target must hold at least 16 bytes. Returns 1 on success, 0 on invalid
 * address/port or extra fields. */
int dr_lobby_parse_target(const char *line,char *target,size_t size,uint16_t *port);
/* target NULL means LAN broadcast. port=7371 in production; override for tests. */
int dr_lobby_start(DrLobby *l,int host,const char *target,uint16_t port,uint32_t now,uint32_t nonce);
void dr_lobby_tick(DrLobby *l,uint32_t now);
void dr_lobby_stop(DrLobby *l);

/* ---- D52-2: deterministic synchronization ------------------------------
 * Wire layout (all little endian, no native structs):
 *   INPUT: [0..3]="DRL1" [4..7]=1 [8..11]=8 [12..15]=nonce [16..19]=session
 *          [20..23]=input [24..27]=frame [28..31]=ack
 *   HASH : [0..3]="DRL1" [4..7]=1 [8..11]=9 [12..15]=nonce [16..19]=session
 *          [20..23]=state hash [24..27]=frame [28..31]=0
 * dr_lobby_tick ignores these packets (it only accepts zeroed trailer words
 * for control types), so the control state machine is unaffected. */

/* Queue the local input for frame `frame`. Frame numbers start at 0 and both
 * consoles must hand them over in the same order. Returns 1 if queued. */
int dr_lobby_send_input(DrLobby *l,uint32_t frame,uint32_t input);
/* Return the next peer input in frame order without taking it out of the
 * queue, so the caller can decide whether it is the frame it needs. */
int dr_lobby_peek_input(DrLobby *l,uint32_t *frame,uint32_t *input);
/* Hand the frame returned by dr_lobby_peek_input to the game. Call it only
 * for the frame you actually used; frames you are not ready for must stay in
 * the queue, otherwise they are lost and the peer's ack says they arrived. */
void dr_lobby_consume_input(DrLobby *l);
/* Publish the battle state hash for `frame`. */
void dr_lobby_send_hash(DrLobby *l,uint32_t hash,uint32_t frame);
/* Read the peer's hash. Returns 1 and stores hash/frame, 0 when none. */
int dr_lobby_receive_hash(DrLobby *l,uint32_t *hash,uint32_t *frame);
/* Retransmit an unacknowledged input after a short window (120 ms, up to 6
 * attempts). The 12 s window in dr_lobby_tick only detects a dead link. */
void dr_lobby_retransmit(DrLobby *l,uint32_t now);
/* Deterministic hashes: same bytes in, same number out, on both consoles. */
uint32_t compute_lobby_state_hash(const DrLobby *l);
uint32_t compute_battle_state_hash(const void *state,size_t size);

#endif
