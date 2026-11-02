/* SPDX-License-Identifier: AGPL-3.0-or-later
 * D52-2: loopback online battle self-test.
 *
 * It drives two real DrLobby instances and two real lockstep harness
 * instances over UDP on 127.0.0.1. It proves that both sides agree on frame
 * numbers and on the state hash while inputs are delayed, reordered and
 * duplicated.
 *
 * It does NOT prove a two-console session, Wi-Fi, a router, the Xbox 360
 * network stack, or that the real bt3xbx battle loop is deterministic.
 */
#include "online_battle_selftest.h"
#include "lobby.h"
#include "online_battle.h"
#include "xsdk_compat.h"
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#define D52_ROUNDS 160

/* Give the local stack a moment to queue a datagram between two ticks. */
static void settle(void)
{
    struct pollfd idle={.fd=-1,.events=0,.revents=0};
    (void)poll(&idle,1,1);
}

static uint16_t bound_port(const DrLobby *l)
{
    struct sockaddr_in address; socklen_t size=sizeof address;
    if(l->fd<0 || getsockname(l->fd,(struct sockaddr *)&address,&size)) return 0;
    return ntohs(address.sin_port);
}

/* D52-1 pairing, unchanged: the D52-2 layer must not disturb it. */
static int pair_loopback(DrLobby *host,DrLobby *client,uint32_t *clock)
{
    uint16_t port;
    if(!dr_lobby_start(host,1,NULL,0,*clock,0x11111111u)) return -1;
    port=bound_port(host);
    if(!port) return -2;
    if(!dr_lobby_start(client,0,"127.0.0.1",port,*clock,0x22222222u)) return -3;
    for(int round=0;round<200;round++) {
        *clock+=10;
        dr_lobby_tick(client,*clock);settle();
        dr_lobby_tick(host,*clock);settle();
        if(host->state==LOBBY_PAIRED && client->state==LOBBY_PAIRED) return 0;
    }
    return -4;
}

/* Deterministic input patterns, one per side, so the run is repeatable. */
static uint32_t pattern_a(uint32_t frame)
{
    uint32_t in=0;
    if((frame/3u)%2u) in|=1u<<0;          /* left  */
    else              in|=1u<<1;          /* right */
    if((frame/5u)%2u) in|=1u<<4;          /* A     */
    return in;
}
static uint32_t pattern_b(uint32_t frame)
{
    uint32_t in=0;
    if((frame/2u)%2u) in|=1u<<2;          /* up    */
    else              in|=1u<<3;          /* down  */
    if((frame/7u)%2u) in|=1u<<1;          /* right */
    return in;
}

/* Fault injection: pull `n` datagrams off a socket and throw them away, the
 * way a congested LAN would. Only used after the lobby is paired. */
static unsigned drop_datagrams(int fd,unsigned n)
{
    unsigned char p[64];struct sockaddr_in from;socklen_t size=sizeof from;
    unsigned dropped=0;
    while(dropped<n) {
        ssize_t got=recvfrom(fd,p,sizeof p,MSG_DONTWAIT,
                             (struct sockaddr *)&from,&size);
        if(got<=0) break;
        dropped++;
    }
    return dropped;
}

int D52_OnlineBattleSelfTest(void)
{
    static DrLobby host,client;
    static DrOnlineBattle side0,side1;
    uint32_t clock=1000;
    int result=0;
    uint32_t rounds=0;

    dr_lobby_init(&host);dr_lobby_init(&client);

    D50_Log("D52-2 selftest: loopback only; two-console session still needs a physical test");

    if(pair_loopback(&host,&client,&clock)) {
        D50_Log("FAIL: D52-2 lobby pairing host=%s(%d) client=%s(%d)",
                dr_lobby_state_name(host.state),host.state,
                dr_lobby_state_name(client.state),client.state);
        result=-1;goto out;
    }
    D50_Log("PASS: D52-2 lobby paired with the unmodified D52-1 handshake");

    if(dr_online_battle_init(&side0,&host,0)!=1 ||
       dr_online_battle_init(&side1,&client,1)!=1) {
        D50_Log("FAIL: D52-2 online battle init");
        result=-2;goto out;
    }
    D50_Log("PASS: D52-2 lockstep harness bound host=side0 client=side1");

    /* Run the lockstep loop. Every 9th round one side is held back, which
     * queues its datagrams and makes them arrive out of order relative to the
     * other side's frame advance. */
    for(rounds=0;rounds<D52_ROUNDS;rounds++) {
        uint32_t host_remote=0,client_remote=0;
        int hold_host=((rounds%9)==3);
        int hold_client=((rounds%9)==6);

        if(!hold_host) {
            dr_lobby_tick(&host,clock);
            (void)dr_online_battle_update(&side0,
                                          pattern_a(dr_online_battle_frame(&side0)),
                                          &host_remote);
        }
        if(!hold_client) {
            dr_lobby_tick(&client,clock);
            (void)dr_online_battle_update(&side1,
                                          pattern_b(dr_online_battle_frame(&side1)),
                                          &client_remote);
        }
        settle();
        clock+=10;

        /* A second pass drains anything that arrived late. */
        dr_lobby_tick(&host,clock);dr_lobby_tick(&client,clock);
        settle();
        clock+=10;
        (void)dr_online_battle_update(&side0,
                                      pattern_a(dr_online_battle_frame(&side0)),
                                      &host_remote);
        (void)dr_online_battle_update(&side1,
                                      pattern_b(dr_online_battle_frame(&side1)),
                                      &client_remote);

        /* Fault injection: throw away a few datagrams on both sides and let
         * the 120 ms retransmission recover them. */
        if(rounds==30) {
            unsigned d=drop_datagrams(host.fd,4)+drop_datagrams(client.fd,4);
            D50_Log("D52-2 fault injection: dropped %u datagram(s), "
                    "retransmission must recover",d);
        }

        if(host.state!=LOBBY_PAIRED || client.state!=LOBBY_PAIRED) {
            D50_Log("FAIL: D52-2 lobby dropped at round %u host=%s client=%s",
                    (unsigned)rounds,dr_lobby_state_name(host.state),
                    dr_lobby_state_name(client.state));
            result=-3;goto out;
        }
        if((rounds%30)==29)
            D50_Log("D52-2 progress round=%u host_frame=%u client_frame=%u "
                    "host_tx=%u/%u client_tx=%u/%u",
                    (unsigned)rounds,
                    (unsigned)dr_online_battle_frame(&side0),
                    (unsigned)dr_online_battle_frame(&side1),
                    (unsigned)host.tx_acked,(unsigned)host.tx_highest,
                    (unsigned)client.tx_acked,(unsigned)client.tx_highest);
        if(dr_online_battle_frame(&side0)>570000000u) break; /* sanity */
    }

    /* Both sides must have simulated the same number of frames. */
    if(dr_online_battle_frame(&side0)!=dr_online_battle_frame(&side1)) {
        D50_Log("FAIL: D52-2 frame counters diverged host=%u client=%u",
                (unsigned)dr_online_battle_frame(&side0),
                (unsigned)dr_online_battle_frame(&side1));
        D50_Log("     host tx_acked=%u tx_highest=%u rx_frame=%u rx_delivered=%u",
                (unsigned)host.tx_acked,(unsigned)host.tx_highest,
                (unsigned)host.rx_frame,(unsigned)host.rx_delivered);
        D50_Log("     client tx_acked=%u tx_highest=%u rx_frame=%u rx_delivered=%u",
                (unsigned)client.tx_acked,(unsigned)client.tx_highest,
                (unsigned)client.rx_frame,(unsigned)client.rx_delivered);
        result=-4;goto out;
    }
    if(dr_online_battle_frame(&side0)<10) {
        D50_Log("FAIL: D52-2 only %u frame(s) simulated",
                (unsigned)dr_online_battle_frame(&side0));
        result=-5;goto out;
    }
    D50_Log("PASS: D52-2 both sides at frame %u",
            (unsigned)dr_online_battle_frame(&side0));

    /* And the same state hash. */
    if(dr_online_battle_get_state_hash(&side0)!=dr_online_battle_get_state_hash(&side1)) {
        D50_Log("FAIL: D52-2 state hash mismatch host=%08X client=%08X",
                (unsigned)dr_online_battle_get_state_hash(&side0),
                (unsigned)dr_online_battle_get_state_hash(&side1));
        result=-6;goto out;
    }
    D50_Log("PASS: D52-2 state hash matches: %08X",
            (unsigned)dr_online_battle_get_state_hash(&side0));

    /* No hash may have been flagged as a desync, and each side must actually
     * have compared the peer's hashes (otherwise the test proves nothing). */
    if(dr_online_battle_desync(&side0,NULL,NULL,NULL) ||
       dr_online_battle_desync(&side1,NULL,NULL,NULL)) {
        uint32_t frame=0,local=0,remote=0;
        (void)dr_online_battle_desync(&side0,&frame,&local,&remote);
        D50_Log("FAIL: D52-2 desync frame=%u local=%08X remote=%08X",
                (unsigned)frame,(unsigned)local,(unsigned)remote);
        result=-7;goto out;
    }
    if(dr_online_battle_matched(&side0)==0 || dr_online_battle_matched(&side1)==0) {
        D50_Log("FAIL: D52-2 no peer hash compared host=%u client=%u",
                (unsigned)dr_online_battle_matched(&side0),
                (unsigned)dr_online_battle_matched(&side1));
        result=-8;goto out;
    }
    D50_Log("PASS: D52-2 %u peer hashes verified per side, no desync",
            (unsigned)dr_online_battle_matched(&side0));

out:
    dr_online_battle_shutdown(&side0);
    dr_online_battle_shutdown(&side1);
    dr_lobby_stop(&host);
    dr_lobby_stop(&client);
    return result;
}
