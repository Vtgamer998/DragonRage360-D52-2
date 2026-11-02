/* SPDX-License-Identifier: MIT */
#ifndef DR_LOBBY_H
#define DR_LOBBY_H
#include <stddef.h>
#include <stdint.h>
#include <netinet/in.h>
enum { LOBBY_OFF, LOBBY_HOST, LOBBY_SEARCH, LOBBY_JOIN, LOBBY_PAIRED };
typedef struct {
    int fd, state, host;
    uint32_t nonce, session, last_send, last_seen, started;
    struct sockaddr_in target, peer;
    char message[240];
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

/* D52-2: Determinism and synchronization */
void dr_lobby_send_input(DrLobby *l,uint32_t input_data);
int dr_lobby_receive_input(DrLobby *l,uint32_t *input_data);
void dr_lobby_send_hash(DrLobby *l,uint32_t hash);
int dr_lobby_receive_hash(DrLobby *l,uint32_t *hash);
uint32_t compute_lobby_state_hash(const DrLobby *l);
uint32_t compute_battle_state_hash(const void *battle_state, size_t size);
void dr_lobby_retransmit(DrLobby *l, uint32_t now);

#endif