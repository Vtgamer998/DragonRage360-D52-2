/* SPDX-License-Identifier: MIT
 * D52-2: Determinism and synchronization of entries in the lobby D52-1
 * This module integrates the lobby with the bt3xbx battle engine for
 * online multiplayer support with deterministic state synchronization.
 */
#include "lobby.h"
#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdint.h>

enum { DISCOVER=1,OFFER,JOIN,ACCEPT,HEARTBEAT,LEAVE,BUSY,INPUT,HASH, PACKET=32 };
static uint32_t rd(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void wr(unsigned char *p,uint32_t v) { for(int i=0;i<4;i++) p[i]=(unsigned char)(v>>(i*8)); }
static int same(const struct sockaddr_in *a,const struct sockaddr_in *b)
{ return a->sin_addr.s_addr==b->sin_addr.s_addr && a->sin_port==b->sin_port; }
static void send_packet(DrLobby *l,const struct sockaddr_in *to,unsigned type,uint32_t nonce,uint32_t session)
{
    unsigned char p[PACKET]={0}; memcpy(p,"DRL1",4);wr(p+4,1);wr(p+8,type);wr(p+12,nonce);wr(p+16,session);
    (void)sendto(l->fd,p,sizeof(p),0,(const struct sockaddr *)to,sizeof *to);
}

/* D52-2: Send input packet with input data */
static void send_input_packet(DrLobby *l,const struct sockaddr_in *to,uint32_t nonce,uint32_t session,uint32_t input_data)
{
    unsigned char p[PACKET]={0}; memcpy(p,"DRL1",4);wr(p+4,1);wr(p+8,INPUT);wr(p+12,nonce);wr(p+16,session);wr(p+20,input_data);
    (void)sendto(l->fd,p,sizeof(p),0,(const struct sockaddr *)to,sizeof *to);
}

/* D52-2: Send hash packet with state hash */
static void send_hash_packet(DrLobby *l,const struct sockaddr_in *to,uint32_t nonce,uint32_t session,uint32_t hash)
{
    unsigned char p[PACKET]={0}; memcpy(p,"DRL1",4);wr(p+4,1);wr(p+8,HASH);wr(p+12,nonce);wr(p+16,session);wr(p+20,hash);
    (void)sendto(l->fd,p,sizeof(p),0,(const struct sockaddr *)to,sizeof *to);
}

/* D52-2: Send input data to the peer */
void dr_lobby_send_input(DrLobby *l,uint32_t input_data)
{
    if(!l || l->state!=LOBBY_PAIRED) return;
    
    /* Send input packet to peer */
    send_input_packet(l,&l->peer,l->nonce,l->session,input_data);
}

/* D52-2: Receive input data from the peer */
int dr_lobby_receive_input(DrLobby *l,uint32_t *input_data)
{
    if(!l || l->state!=LOBBY_PAIRED) return 0;
    
    struct sockaddr_in from;
    socklen_t fromlen=sizeof(from);
    unsigned char packet[PACKET];
    
    /* Check for pending input packet */
    ssize_t n=recvfrom(l->fd,packet,sizeof(packet),MSG_DONTWAIT,(struct sockaddr *)&from,&fromlen);
    if(n<=0) return 0;
    
    if(n!=PACKET || memcmp(packet,"DRL1",4)) return 0;
    if(rd(packet+4)!=1) return 0;
    if(rd(packet+8)!=INPUT) return 0;
    if(rd(packet+12)!=l->nonce || rd(packet+16)!=l->session) return 0;
    
    *input_data=rd(packet+20);
    return 1;
}

/* D52-2: Send state hash to peer */
void dr_lobby_send_hash(DrLobby *l,uint32_t hash)
{
    if(!l || l->state!=LOBBY_PAIRED) return;
    
    send_hash_packet(l,&l->peer,l->nonce,l->session,hash);
}

/* D52-2: Receive state hash from peer */
int dr_lobby_receive_hash(DrLobby *l,uint32_t *hash)
{
    if(!l || l->state!=LOBBY_PAIRED) return 0;
    
    struct sockaddr_in from;
    socklen_t fromlen=sizeof(from);
    unsigned char packet[PACKET];
    
    ssize_t n=recvfrom(l->fd,packet,sizeof(packet),MSG_DONTWAIT,(struct sockaddr *)&from,&fromlen);
    if(n<=0) return 0;
    
    if(n!=PACKET || memcmp(packet,"DRL1",4)) return 0;
    if(rd(packet+4)!=1) return 0;
    if(rd(packet+8)!=HASH) return 0;
    if(rd(packet+12)!=l->nonce || rd(packet+16)!=l->session) return 0;
    
    *hash=rd(packet+20);
    return 1;
}

/* D52-2: Compute battle state hash for synchronization */
uint32_t compute_battle_state_hash(const void *battle_state, size_t size)
{
    if(!battle_state || size==0) return 0;
    
    uint32_t hash=0;
    const unsigned char *p=battle_state;
    
    /* XOR-based hash over the battle state */
    for(size_t i=0;i<size;i++) {
        hash^=p[i];
        hash=(hash<<1)|(hash>>31);
    }
    
    return hash;
}

/* D52-1: Compute lobby state hash */
uint32_t compute_lobby_state_hash(const DrLobby *l)
{
    if(!l) return 0;
    
    uint32_t hash=0;
    /* Hash the essential lobby state */
    hash^=(uint32_t)l->state;
    hash^=(uint32_t)l->host;
    hash^=l->nonce;
    hash^=l->session;
    
    /* Include peer info if paired */
    if(l->state==LOBBY_PAIRED) {
        hash^=l->peer.sin_addr.s_addr;
        hash^=l->peer.sin_port;
    }
    
    return hash;
}

/* D52-1: Retransmit logic with clock wrap handling */
void dr_lobby_retransmit(DrLobby *l, uint32_t now)
{
    if(!l) return;
    
    /* Retransmit after 12 seconds timeout */
    if((now - l->last_send) > 12000) {
        if(l->state==LOBBY_SEARCH || l->state==LOBBY_JOIN) {
            send_packet(l,&l->target,l->state==LOBBY_SEARCH?DISCOVER:JOIN,l->nonce,0);
        }
        l->last_send=now;
    }
}

void dr_lobby_init(DrLobby *l) { memset(l,0,sizeof *l);l->fd=-1;l->state=LOBBY_OFF; }
const char *dr_lobby_state_name(int s)
{ return s==LOBBY_OFF?"off":s==LOBBY_HOST?"host":s==LOBBY_SEARCH?"search":s==LOBBY_JOIN?"join":s==LOBBY_PAIRED?"paired":"unknown"; }

int dr_lobby_parse_target(const char *line,char *target,size_t size,uint16_t *port)
{
    char extra; unsigned p=7371;
    if(!line || !target || !size || !port) return 0;
    if(sscanf(line,"%15s %u %c",target,&p,&extra)>2 || p==0 || p>65535) return 0;
    *port=(uint16_t)p; return 1;
}

int dr_lobby_start(DrLobby *l,int host,const char *target,uint16_t port,uint32_t now,uint32_t nonce)
{
    struct sockaddr_in addr;
    if(!l) return -1;
    dr_lobby_stop(l);
    l->fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(l->fd<0) return -1;
    int opt=1; setsockopt(l->fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof opt);
    memset(&addr,0,sizeof addr);
    addr.sin_family=AF_INET; addr.sin_port=htons(port);
    addr.sin_addr.s_addr=INADDR_ANY;
    if(bind(l->fd,(struct sockaddr *)&addr,sizeof addr)<0) { dr_lobby_stop(l); return -2; }
    l->host=host; l->nonce=nonce; l->started=l->last_send=now;
    l->state=host?LOBBY_HOST:LOBBY_SEARCH;
    if(target && inet_pton(AF_INET,target,&l->target.sin_addr)==1) {
        l->target.sin_family=AF_INET; l->target.sin_port=htons(port);
        l->state=LOBBY_JOIN;
        send_packet(l,&l->target,JOIN,nonce,0);
    } else if(!host) {
        struct sockaddr_in bc;
        memset(&bc,0,sizeof bc);
        bc.sin_family=AF_INET; bc.sin_port=htons(port);
        inet_pton(AF_INET,"255.255.255.255",&bc.sin_addr);
        send_packet(l,&bc,DISCOVER,nonce,0);
    }
    return 0;
}

void dr_lobby_tick(DrLobby *l,uint32_t now)
{
    unsigned char packet[PACKET]; struct sockaddr_in from;
    if(!l || l->fd<0 || l->state==LOBBY_OFF) return;
    for(;;) {
        socklen_t len=sizeof from;
        ssize_t n=recvfrom(l->fd,packet,sizeof packet,MSG_DONTWAIT,(struct sockaddr *)&from,&len);
        if(n<=0) break;
        if(n!=PACKET || memcmp(packet,"DRL1",4) || rd(packet+4)!=1) continue;
        unsigned type=rd(packet+8),nonce=rd(packet+12),session=rd(packet+16);
        switch(type) {
        case DISCOVER: if(l->state==LOBBY_HOST) send_packet(l,&from,OFFER,l->nonce,0); break;
        case OFFER: if(l->state==LOBBY_SEARCH || l->state==LOBBY_JOIN) {
                l->peer=from; l->session=session?session:nonce;
                send_packet(l,&from,JOIN,l->nonce,l->session); l->state=LOBBY_JOIN; } break;
        case JOIN: if(l->state==LOBBY_HOST && !l->peer.sin_port) {
                l->peer=from; l->session=nonce; send_packet(l,&from,ACCEPT,l->nonce,l->session); l->state=LOBBY_PAIRED; } break;
        case ACCEPT: if(l->state==LOBBY_JOIN) { l->peer=from; l->state=LOBBY_PAIRED; } break;
        case HEARTBEAT: if(same(&from,&l->peer)) l->last_seen=now; break;
        case LEAVE: if(same(&from,&l->peer)) dr_lobby_stop(l); return;
        case BUSY: if(l->state==LOBBY_JOIN) { snprintf(l->message,sizeof l->message,"Host ocupado."); dr_lobby_stop(l); return; }
        default: break;
        }
    }
    if(l->state==LOBBY_SEARCH || l->state==LOBBY_JOIN)
        if(now-l->last_send>=1000) {
            if(l->target.sin_addr.s_addr) send_packet(l,&l->target,l->state==LOBBY_JOIN?JOIN:DISCOVER,l->nonce,l->session);
            else { struct sockaddr_in bc={.sin_family=AF_INET,.sin_port=l->target.sin_port?l->target.sin_port:htons(7371)};
                   inet_pton(AF_INET,"255.255.255.255",&bc.sin_addr); send_packet(l,&bc,DISCOVER,l->nonce,0); }
            l->last_send=now;
        }
    if(l->state==LOBBY_PAIRED) {
        if(now-l->last_send>=500) { send_packet(l,&l->peer,HEARTBEAT,l->nonce,l->session); l->last_send=now; }
        if(now-l->last_seen>=5000) { snprintf(l->message,sizeof l->message,"Conexão perdida."); dr_lobby_stop(l); }
        else snprintf(l->message,sizeof l->message,"Conectado a %s:%u",inet_ntoa(l->peer.sin_addr),ntohs(l->peer.sin_port));
    } else if(l->state==LOBBY_HOST)
        snprintf(l->message,sizeof l->message,"Host aguardando...");
    else snprintf(l->message,sizeof l->message,"Procurando host...");
    
    /* D52-1: Call retransmit with clock wrap handling */
    dr_lobby_retransmit(l,now);
}

void dr_lobby_stop(DrLobby *l)
{
    if(!l) return;
    if(l->fd>=0) close(l->fd);
    memset(l,0,sizeof *l); l->fd=-1; l->state=LOBBY_OFF;
}
