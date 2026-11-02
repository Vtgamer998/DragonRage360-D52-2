/* SPDX-License-Identifier: AGPL-3.0-or-later
 * D52-2: loopback online battle self-test. It drives the real DrLobby
 * state machine and the online battle module over UDP on 127.0.0.1,
 * but it does not prove a two-console session, Wi-Fi, the router or
 * the Xbox 360 network stack.
 */
#include "online_battle_selftest.h"
#include "lobby.h"
#include "online_battle.h"
#include "xsdk_compat.h"
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#define D52_SELFTEST_ROUNDS 200
#define D52_SELFTEST_STEP_MS 10
#define D52_SELFTEST_PAUSE_MS 1

/* Give the local stack a moment to queue a datagram between two ticks. */
static void settle(void)
{
    struct pollfd idle={.fd=-1,.events=0,.revents=0};
    (void)poll(&idle,1,D52_SELFTEST_PAUSE_MS);
}
static uint16_t bound_port(const DrLobby *l)
{
    struct sockaddr_in address; socklen_t size=sizeof address;
    if(l->fd<0 || getsockname(l->fd,(struct sockaddr *)&address,&size)) return 0;
    return ntohs(address.sin_port);
}
static int pair_loopback(DrLobby *host,DrLobby *client,uint32_t *clock)
{
    if(dr_lobby_start(host,1,NULL,0,*clock,*clock)) return -1;
    uint16_t port=bound_port(host);
    if(!port) return -2;
    char target[16]="127.0.0.1";
    if(dr_lobby_start(client,0,target,port,*clock,*clock^0xA5A5A5A5u)) return -3;
    for(int round=0;round<D52_SELFTEST_ROUNDS;round++) {
        if(host->state==LOBBY_PAIRED && client->state==LOBBY_PAIRED) return 0;
        dr_lobby_tick(host,*clock);
        dr_lobby_tick(client,*clock);
        settle();
        *clock+=D52_SELFTEST_STEP_MS;
    }
    return -4;
}
int D52_OnlineBattleSelfTest(void)
{
    int result=0;
    uint32_t clock=1000;
    DrLobby host,client;
    dr_lobby_init(&host);
    dr_lobby_init(&client);
    if(pair_loopback(&host,&client,&clock)) {
        xsdk_log_write("FAIL: D52-2 loopback lobby pairing failed");
        result=-1;
        goto out;
    }
    xsdk_log_write("PASS: D52-2 loopback lobby paired");

    /* Initialize online battles for both sides */
    if(dr_online_battle_init(&host) || dr_online_battle_init(&client)) {
        xsdk_log_write("FAIL: D52-2 online battle init failed");
        result=-2;
        goto out;
    }
    xsdk_log_write("PASS: D52-2 online battle initialized");

    /* Run synchronized input exchange for a few rounds */
    uint32_t host_input=0x10000001, client_input=0x20000002;
    for(int round=0;round<D52_SELFTEST_ROUNDS;round++) {
        uint32_t remote_host_input=0, remote_client_input=0;
        int host_update=dr_online_battle_update(host_input,&remote_host_input);
        int client_update=dr_online_battle_update(client_input,&remote_client_input);
        if(host_update<0 || client_update<0) {
            xsdk_log_write("FAIL: D52-2 desync detected at round %d",round);
            result=-3;
            goto out;
        }
        /* Vary inputs a little */
        host_input=(host_input<<1)|((host_input>>31)&1);
        client_input=(client_input<<2)|((client_input>>30)&3);
        settle();
        clock+=D52_SELFTEST_STEP_MS;
        dr_lobby_tick(&host,clock);
        dr_lobby_tick(&client,clock);
        settle();
        clock+=D52_SELFTEST_STEP_MS;
    }
    uint32_t host_hash=dr_online_battle_get_state_hash();
    uint32_t client_hash=dr_online_battle_get_state_hash();
    if(host_hash!=client_hash) {
        xsdk_log_write("FAIL: D52-2 final state hash mismatch: host=%08X client=%08X",host_hash,client_hash);
        result=-4;
        goto out;
    }
    xsdk_log_write("PASS: D52-2 final state hash matches: %08X",host_hash);

out:
    dr_online_battle_shutdown();
    dr_lobby_stop(&host);
    dr_lobby_stop(&client);
    return result;
}