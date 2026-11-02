/* SPDX-License-Identifier: AGPL-3.0-or-later
 * D52-1: loopback lobby self-test. It drives the real DrLobby state machine
 * over UDP on 127.0.0.1, but it does not prove a two-console session, Wi-Fi,
 * the router or the Xbox 360 network stack.
 */
#include "lobby_selftest.h"
#include "lobby.h"
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
    for(int round=0;round<D52_SELFTEST_ROUNDS;round++) {
        *clock+=D52_SELFTEST_STEP_MS;
        dr_lobby_tick(client,*clock);settle();
        dr_lobby_tick(host,*clock);settle();
        dr_lobby_tick(client,*clock);
        if(host->state==LOBBY_PAIRED && client->state==LOBBY_PAIRED) return 0;
    }
    return -1;
}
int D52_LobbySelfTest(void)
{
    DrLobby host,client;
    dr_lobby_init(&host);dr_lobby_init(&client);
    int result=0;
    uint32_t clock=1000;
    uint16_t port=0;

    D50_Log("D52 lobby selftest: loopback only; two-console session still needs a physical test");

    if(!dr_lobby_start(&host,1,NULL,0,clock,0x11111111u)) {
        D50_Log("FAIL: D52 lobby selftest host start: %s",host.message);
        result=-1;goto done;
    }
    port=bound_port(&host);
    if(!port) {
        D50_Log("FAIL: D52 lobby selftest host ephemeral port");
        result=-2;goto done;
    }
    if(!dr_lobby_start(&client,0,"127.0.0.1",port,clock,0x22222222u)) {
        D50_Log("FAIL: D52 lobby selftest client start: %s",client.message);
        result=-3;goto done;
    }
    if(pair_loopback(&host,&client,&clock)) {
        D50_Log("FAIL: D52 lobby selftest pairing host=%s(%d) client=%s(%d) clock=%u",
                dr_lobby_state_name(host.state),host.state,
                dr_lobby_state_name(client.state),client.state,(unsigned)clock);
        result=-4;goto done;
    }
    if(host.session!=client.session || host.nonce!=client.nonce) {
        D50_Log("FAIL: D52 lobby selftest session host=0x%08X/0x%08X client=0x%08X/0x%08X",
                host.session,host.nonce,client.session,client.nonce);
        result=-5;goto done;
    }
    D50_Log("PASS: D52 lobby selftest connected role=host+client players=2 port=%u session=0x%08X",
            (unsigned)port,host.session);

    /* The host closes explicitly: the client must leave without waiting 12 s. */
    dr_lobby_stop(&host);
    settle();
    dr_lobby_tick(&client,clock+100);
    if(client.state!=LOBBY_OFF) {
        D50_Log("FAIL: D52 lobby selftest explicit leave client=%s(%d)",
                dr_lobby_state_name(client.state),client.state);
        result=-6;goto done;
    }
    D50_Log("PASS: D52 lobby selftest explicit leave role=client players=0");
    dr_lobby_stop(&client);

    /* Cable pull: close the client without LEAVE; the host recovers at 12 s. */
    clock=10000;
    if(!dr_lobby_start(&host,1,NULL,0,clock,0x33333333u)) {
        D50_Log("FAIL: D52 lobby selftest second host start: %s",host.message);
        result=-7;goto done;
    }
    port=bound_port(&host);
    if(!port) {
        D50_Log("FAIL: D52 lobby selftest second host ephemeral port");
        result=-8;goto done;
    }
    if(!dr_lobby_start(&client,0,"127.0.0.1",port,clock,0x44444444u)) {
        D50_Log("FAIL: D52 lobby selftest second client start: %s",client.message);
        result=-9;goto done;
    }
    if(pair_loopback(&host,&client,&clock)) {
        D50_Log("FAIL: D52 lobby selftest second pairing host=%s(%d) client=%s(%d) clock=%u",
                dr_lobby_state_name(host.state),host.state,
                dr_lobby_state_name(client.state),client.state,(unsigned)clock);
        result=-10;goto done;
    }
    /* Drain the last queued heartbeat, then pull the cable. */
    dr_lobby_tick(&host,clock);
    close(client.fd);client.fd=-1;
    dr_lobby_tick(&host,clock+12001);
    if(host.state!=LOBBY_HOST || host.peer.sin_port) {
        D50_Log("FAIL: D52 lobby selftest host timeout host=%s(%d) peer_port=%u",
                dr_lobby_state_name(host.state),host.state,
                (unsigned)ntohs(host.peer.sin_port));
        result=-11;goto done;
    }
    D50_Log("PASS: D52 lobby selftest host timeout role=host players=1 after 12001 ms");
    dr_lobby_stop(&client);dr_lobby_stop(&host);

    /* Search timeout, including the unsigned millisecond wrap. */
    if(!dr_lobby_start(&client,0,"127.0.0.1",port,0xffffff00u,0x55555555u)) {
        D50_Log("FAIL: D52 lobby selftest search client start: %s",client.message);
        result=-12;goto done;
    }
    dr_lobby_tick(&client,200);
    if(client.state!=LOBBY_SEARCH) {
        D50_Log("FAIL: D52 lobby selftest search state=%s(%d) before deadline",
                dr_lobby_state_name(client.state),client.state);
        result=-13;goto done;
    }
    dr_lobby_tick(&client,31000);
    if(client.state!=LOBBY_OFF) {
        D50_Log("FAIL: D52 lobby selftest search timeout state=%s(%d)",
                dr_lobby_state_name(client.state),client.state);
        result=-14;goto done;
    }
    D50_Log("PASS: D52 lobby selftest client search timeout role=client players=0 after 31256 ms with clock wrap");
    D50_Log("PASS: D52 lobby selftest loopback pairing, explicit leave, host timeout, client search timeout; two-console session still needs a physical test");

done:
    dr_lobby_stop(&client);dr_lobby_stop(&host);
    if(result) D50_Log("FAIL: D52 lobby selftest loopback result=%d",result);
    return result;
}
