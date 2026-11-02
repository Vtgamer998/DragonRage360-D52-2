/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "lobby.h"
#include <time.h>
#include <errno.h>
#include <assert.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
static uint16_t host_start(DrLobby *h,uint32_t t,uint32_t nonce)
{
    assert(dr_lobby_start(h,1,NULL,0,t,nonce));
    struct sockaddr_in a;socklen_t n=sizeof(a);assert(!getsockname(h->fd,(struct sockaddr *)&a,&n));return ntohs(a.sin_port);
}
static void ticks(DrLobby *h,DrLobby *c,uint32_t from,uint32_t to)
{ for(uint32_t t=from;t<to;t+=100) {struct timespec pause={0,1000000};dr_lobby_tick(c,t);nanosleep(&pause,NULL);dr_lobby_tick(h,t);nanosleep(&pause,NULL);dr_lobby_tick(c,t);} }
static void invalid_packet(DrLobby *h,const unsigned char *p,size_t n)
{
    struct sockaddr_in a={.sin_family=AF_INET};socklen_t size=sizeof(a);
    assert(!getsockname(h->fd,(struct sockaddr *)&a,&size));assert(inet_pton(AF_INET,"127.0.0.1",&a.sin_addr)==1);
    int s=socket(AF_INET,SOCK_DGRAM,0);assert(s>=0);
    assert(sendto(s,p,n,0,(struct sockaddr *)&a,sizeof(a))==(ssize_t)n);close(s);
}
int main(void)
{
    DrLobby h,c,third;dr_lobby_init(&h);dr_lobby_init(&c);dr_lobby_init(&third);
    uint16_t port=host_start(&h,100,123);
    unsigned char bad[33]={0};memcpy(bad,"DRL1",4);bad[4]=2;bad[8]=3;bad[12]=6;bad[16]=123;
    invalid_packet(&h,bad,32);bad[4]=1;invalid_packet(&h,bad,31);invalid_packet(&h,bad,33);
    bad[20]=1;invalid_packet(&h,bad,32);dr_lobby_tick(&h,100);assert(h.state==LOBBY_HOST && !h.peer.sin_port);
    assert(dr_lobby_start(&c,0,"127.0.0.1",port,100,456));
    /* First discovery response is deliberately discarded: periodic retries recover. */
    dr_lobby_tick(&c,100);dr_lobby_tick(&h,100);
    unsigned char drop[64];assert(recv(c.fd,drop,sizeof(drop),0)==32);
    ticks(&h,&c,200,3200);assert(h.state==LOBBY_PAIRED && c.state==LOBBY_PAIRED);
    assert(h.session==c.session && h.nonce==c.nonce);
    assert(dr_lobby_start(&third,0,"127.0.0.1",port,3200,789));
    for(uint32_t t=3200;t<6200;t+=100) {ticks(&h,&c,t,t+100);dr_lobby_tick(&third,t);}
    assert(third.state==LOBBY_SEARCH);assert(h.nonce==456);
    dr_lobby_stop(&third);
    /* Unrelated endpoint with correct fields must not refresh a paired host. */
    memset(bad,0,sizeof(bad));memcpy(bad,"DRL1",4);bad[4]=1;bad[8]=5;bad[12]=200;bad[13]=1;bad[16]=123;
    uint32_t seen=h.last_seen;invalid_packet(&h,bad,32);dr_lobby_tick(&h,6200);assert(h.last_seen==seen);
    /* Explicit host leave is recognized; no timeout required. */
    dr_lobby_stop(&h);dr_lobby_tick(&c,6300);assert(c.state==LOBBY_OFF);
    port=host_start(&h,7000,987);
    assert(dr_lobby_start(&c,0,"127.0.0.1",port,7000,654));ticks(&h,&c,7000,9500);
    assert(h.state==LOBBY_PAIRED && c.state==LOBBY_PAIRED);
    /* Simulate cable pull: close without sending LEAVE. */
    close(c.fd);c.fd=-1;dr_lobby_tick(&h,23000);assert(h.state==LOBBY_HOST && !h.peer.sin_port);
    dr_lobby_stop(&c);dr_lobby_stop(&h);
    assert(dr_lobby_start(&c,0,"127.0.0.1",port,100,1));dr_lobby_tick(&c,30101);assert(c.state==LOBBY_OFF);
    assert(!dr_lobby_start(&c,0,"invalid",port,1,1));assert(c.fd==-1);
    /* lobby_host.txt parser: IP only, IP+port, and invalid inputs. */
    char target[16];uint16_t target_port=0;
    assert(dr_lobby_parse_target("192.168.1.10",target,sizeof target,&target_port)==1 &&
           !strcmp(target,"192.168.1.10") && target_port==7371);
    assert(dr_lobby_parse_target("192.168.1.10 7000\n",target,sizeof target,&target_port)==1 &&
           target_port==7000);
    assert(!dr_lobby_parse_target("192.168.1.10 0",target,sizeof target,&target_port));
    assert(!dr_lobby_parse_target("192.168.1.10 70000",target,sizeof target,&target_port));
    assert(!dr_lobby_parse_target("192.168.1.10 abc",target,sizeof target,&target_port));
    assert(!dr_lobby_parse_target("192.168.1.10 7000 extra",target,sizeof target,&target_port));
    assert(!dr_lobby_parse_target("999.1.1.1",target,sizeof target,&target_port));
    assert(!dr_lobby_parse_target("",target,sizeof target,&target_port));
    assert(!dr_lobby_parse_target(NULL,target,sizeof target,&target_port));
    /* Some sandbox environments have no route to the LAN broadcast address. */
    int probe=socket(AF_INET,SOCK_DGRAM,0),yes=1;
    assert(probe>=0 && !setsockopt(probe,SOL_SOCKET,SO_BROADCAST,&yes,sizeof(yes)));
    struct sockaddr_in broadcast={.sin_family=AF_INET,.sin_port=htons(port),.sin_addr={.s_addr=0xffffffffu}};
    if(sendto(probe,"probe",5,0,(struct sockaddr *)&broadcast,sizeof(broadcast))<0) {
        assert(errno==ENETUNREACH || errno==EACCES || errno==EPERM);
        puts("SKIP: LAN broadcast transport unavailable in this environment; direct-IP discovery tested.");
    } else {
        port=host_start(&h,40000,1001);
        assert(dr_lobby_start(&c,0,NULL,port,40000,1002));ticks(&h,&c,40000,43500);
        assert(h.state==LOBBY_PAIRED && c.state==LOBBY_PAIRED);
        dr_lobby_stop(&c);dr_lobby_stop(&h);
    }
    close(probe);
    /* Unsigned millisecond wrap-around keeps search deadlines valid. */
    assert(dr_lobby_start(&c,0,"127.0.0.1",port,0xffffff00u,1));dr_lobby_tick(&c,200);assert(c.state==LOBBY_SEARCH);
    dr_lobby_tick(&c,31000);assert(c.state==LOBBY_OFF);dr_lobby_stop(&c);
    puts("PASS: actual UDP discovery and direct-IP lobby handshake, lost offer retry, full room, invalid packets, source filtering, leave, timeout, clock wrap, lobby_host.txt parsing");
}
