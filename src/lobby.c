/* SPDX-License-Identifier: MIT
 * Independent experimental lobby protocol. Does not carry battle inputs.
 * All wire integers are little endian; no native structs are transmitted.
 */
#include "lobby.h"
#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
/* D52-1 control types 1..7. D52-2 adds INPUT(8) and HASH(9); control types
 * keep their numbers so an old and a new console can still pair. */
enum { DISCOVER=1,OFFER,JOIN,ACCEPT,HEARTBEAT,LEAVE,BUSY,INPUT=8,HASH=9, PACKET=32 };
static uint32_t rd(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void wr(unsigned char *p,uint32_t v) { for(int i=0;i<4;i++) p[i]=(unsigned char)(v>>(i*8)); }
static int same(const struct sockaddr_in *a,const struct sockaddr_in *b)
{ return a->sin_addr.s_addr==b->sin_addr.s_addr && a->sin_port==b->sin_port; }
static void send_packet(DrLobby *l,const struct sockaddr_in *to,unsigned type,uint32_t nonce,uint32_t session)
{
    unsigned char p[PACKET]={0}; memcpy(p,"DRL1",4);wr(p+4,1);wr(p+8,type);wr(p+12,nonce);wr(p+16,session);
    (void)sendto(l->fd,p,sizeof(p),0,(const struct sockaddr *)to,sizeof(*to));
}
void dr_lobby_init(DrLobby *l) { memset(l,0,sizeof(*l));l->fd=-1;strcpy(l->message,"Rede desligada."); }
const char *dr_lobby_state_name(int state)
{
    switch(state) {
    case LOBBY_HOST: return "host";
    case LOBBY_SEARCH: return "search";
    case LOBBY_JOIN: return "join";
    case LOBBY_PAIRED: return "paired";
    default: return "off";
    }
}
int dr_lobby_parse_target(const char *line,char *target,size_t size,uint16_t *port)
{
    char host[16]={0},port_text[16]={0},extra[2]={0};
    struct in_addr address;
    unsigned long value;
    char *end=NULL;
    if(!line || !target || size<16 || !port) return 0;
    int fields=sscanf(line,"%15s %15s %1s",host,port_text,extra);
    if(fields<1 || fields>2 || inet_pton(AF_INET,host,&address)!=1) return 0;
    *port=7371;
    if(fields>=2) {
        value=strtoul(port_text,&end,10);
        if(!end || *end || !value || value>65535) return 0;
        *port=(uint16_t)value;
    }
    snprintf(target,size,"%s",host);
    return 1;
}
void dr_lobby_stop(DrLobby *l)
{
    if(l->fd>=0) {
        if(l->session && l->peer.sin_port) send_packet(l,&l->peer,LEAVE,l->nonce,l->session);
        close(l->fd);
    }
    dr_lobby_init(l);
}
int dr_lobby_start(DrLobby *l,int host,const char *target,uint16_t port,uint32_t now,uint32_t nonce)
{
    dr_lobby_stop(l);l->host=host;l->nonce=nonce?nonce:1;l->session=host?l->nonce:0;
    l->target.sin_family=AF_INET;l->target.sin_port=htons(port);
    if(inet_pton(AF_INET,target?target:"255.255.255.255",&l->target.sin_addr)!=1) goto fail;
    l->fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(l->fd<0) goto fail;
    int yes=1;
    if(setsockopt(l->fd,SOL_SOCKET,SO_BROADCAST,&yes,sizeof(yes))) goto fail;
    struct sockaddr_in local={.sin_family=AF_INET,.sin_port=host?htons(port):0};
    if(bind(l->fd,(const struct sockaddr *)&local,sizeof(local))) goto fail;
    l->state=host?LOBBY_HOST:LOBBY_SEARCH;l->started=now;l->last_seen=now;l->last_send=now-1000;
    snprintf(l->message,sizeof(l->message),host?"Sala aberta UDP %u. Aguardando cliente...":"Buscando host UDP %u...",port);return 1;
fail:
    dr_lobby_stop(l);strcpy(l->message,"Falha ao abrir rede. Confira IP e porta ocupada.");return 0;
}
static void paired(DrLobby *l)
{
    char ip[16]="?";inet_ntop(AF_INET,&l->peer.sin_addr,ip,sizeof(ip));
    l->state=LOBBY_PAIRED;
    snprintf(l->message,sizeof(l->message),"Lobby conectado: %s %s:%u\nMotor de luta ainda indisponível.",l->host?"cliente":"host",ip,(unsigned)ntohs(l->peer.sin_port));
}
void dr_lobby_tick(DrLobby *l,uint32_t now)
{
    if(l->fd<0) return;
    l->tick_now=now;
    /* Fixed budget prevents an incoming flood from monopolizing the frame. */
    for(int budget=0;budget<16;budget++) {
        struct pollfd ready={.fd=l->fd,.events=POLLIN};
        if(poll(&ready,1,0)<=0 || !(ready.revents&POLLIN)) break;
        unsigned char p[PACKET+1];struct sockaddr_in from={0};socklen_t size=sizeof(from);
        /* D52-2: peek first. An INPUT/HASH datagram belongs to the
         * synchronization layer, so leave it in the queue instead of
         * consuming and discarding it here. */
        ssize_t n=recvfrom(l->fd,p,sizeof(p),MSG_PEEK,(struct sockaddr *)&from,&size);
        if(n==PACKET && !memcmp(p,"DRL1",4) && rd(p+4)==1) {
            unsigned peeked=rd(p+8);
            if(peeked==INPUT || peeked==HASH) break;
        }
        size=sizeof(from);
        n=recvfrom(l->fd,p,sizeof(p),0,(struct sockaddr *)&from,&size);
        if(n!=PACKET || from.sin_family!=AF_INET || memcmp(p,"DRL1",4) || rd(p+4)!=1 || rd(p+20) || rd(p+24) || rd(p+28)) continue;
        unsigned type=rd(p+8);uint32_t nonce=rd(p+12),session=rd(p+16);
        if(!nonce) continue;
        if(l->host) {
            if(type==DISCOVER && !session && l->state==LOBBY_HOST && !l->peer.sin_port)
                send_packet(l,&from,OFFER,nonce,l->session);
            else if(type==JOIN && session==l->session) {
                if(l->peer.sin_port && (!same(&from,&l->peer) || nonce!=l->nonce)) send_packet(l,&from,BUSY,nonce,session);
                else {
                    l->peer=from;l->nonce=nonce;l->last_seen=now;
                    send_packet(l,&from,ACCEPT,nonce,session);
                }
            } else if(same(&from,&l->peer) && session==l->session && nonce==l->nonce) {
                if(type==HEARTBEAT) {l->last_seen=now;paired(l);}
                if(type==LEAVE) {l->peer.sin_port=0;l->state=LOBBY_HOST;strcpy(l->message,"Cliente saiu. Aguardando novo cliente...");}
            }
        } else {
            if(type==OFFER && l->state==LOBBY_SEARCH && nonce==l->nonce && session &&
               (l->target.sin_addr.s_addr==htonl(INADDR_BROADCAST) || same(&from,&l->target))) {
                l->peer=from;l->session=session;l->state=LOBBY_JOIN;l->last_send=now-1000;l->last_seen=now;
                strcpy(l->message,"Host encontrado. Conectando...");
            } else if(same(&from,&l->peer) && nonce==l->nonce && session==l->session) {
                if(type==ACCEPT && (l->state==LOBBY_JOIN || l->state==LOBBY_PAIRED)) {l->last_seen=now;paired(l);l->last_send=now-1000;}
                if(type==HEARTBEAT && l->state==LOBBY_PAIRED) l->last_seen=now;
                if(type==BUSY || type==LEAVE) {dr_lobby_stop(l);strcpy(l->message,"Host ocupado ou sala encerrada. Busque novamente.");return;}
            }
        }
    }
    if((l->state==LOBBY_PAIRED || l->state==LOBBY_JOIN || (l->host && l->peer.sin_port)) && (uint32_t)(now-l->last_seen)>12000) {
        if(l->host) {l->peer.sin_port=0;l->state=LOBBY_HOST;strcpy(l->message,"Cliente desconectado. Aguardando outro...");}
        else {dr_lobby_stop(l);strcpy(l->message,"Conexão perdida. Busque novamente.");return;}
    }
    if(l->state==LOBBY_SEARCH && (uint32_t)(now-l->started)>30000) {
        dr_lobby_stop(l);strcpy(l->message,"Nenhum host em 30 segundos. Confira a rede.");return;
    }
    if((uint32_t)(now-l->last_send)<500) return;
    l->last_send=now;
    if(l->state==LOBBY_SEARCH) send_packet(l,&l->target,DISCOVER,l->nonce,0);
    if(l->state==LOBBY_JOIN) send_packet(l,&l->peer,JOIN,l->nonce,l->session);
    if(l->state==LOBBY_PAIRED) send_packet(l,&l->peer,HEARTBEAT,l->nonce,l->session);
}


/* ===================== D52-2 synchronization layer ===================== *
 * The D52-1 state machine above is deliberately untouched. These helpers
 * only run while the lobby is paired, and dr_lobby_tick already refuses any
 * datagram whose trailer words are non-zero - which is exactly what INPUT
 * and HASH carry - so control packets and input packets never collide.
 *
 * Wire (all little endian, no native structs):
 *   INPUT  [0..3]="DRL1" [4..7]=1 [8..11]=8 [12..15]=nonce [16..19]=session
 *          [20..23]=input value [24..27]=frame [28..31]=ack
 *   HASH   [0..3]="DRL1" [4..7]=1 [8..11]=9 [12..15]=nonce [16..19]=session
 *          [20..23]=state hash [24..27]=frame [28..31]=0
 *
 * Both consoles number frames from 0 and only advance when the input for the
 * frame is known, so a lost datagram stalls the simulation instead of making
 * the two machines run different frames. Frames are buffered in a 16-deep
 * window, which absorbs reordering, duplicates and short bursts of loss. */

enum { D52_INPUT_RTX_MS=120, D52_INPUT_RTX_MAX=6 };

static void send_input_packet(DrLobby *l,const struct sockaddr_in *to,
                              uint32_t nonce,uint32_t session,
                              uint32_t value,uint32_t frame,uint32_t ack)
{
    unsigned char p[PACKET]={0};
    memcpy(p,"DRL1",4);
    wr(p+4,1);wr(p+8,INPUT);wr(p+12,nonce);wr(p+16,session);
    wr(p+20,value);wr(p+24,frame);wr(p+28,ack);
    (void)sendto(l->fd,p,sizeof p,0,(const struct sockaddr *)to,sizeof *to);
}

static void send_hash_packet(DrLobby *l,const struct sockaddr_in *to,
                             uint32_t nonce,uint32_t session,
                             uint32_t hash,uint32_t frame)
{
    unsigned char p[PACKET]={0};
    memcpy(p,"DRL1",4);
    wr(p+4,1);wr(p+8,HASH);wr(p+12,nonce);wr(p+16,session);
    wr(p+20,hash);wr(p+24,frame);
    (void)sendto(l->fd,p,sizeof p,0,(const struct sockaddr *)to,sizeof *to);
}

/* Queue the local input for `frame` and put it on the wire. Frames start at
 * 1; 0 always means "nothing yet" on both sides of the protocol.
 *
 * Returns 1 when the caller can consider its input for this frame delivered
 * (either just sent, or already confirmed by the peer), 0 only when the lobby
 * is not paired or the frame is beyond the window. Treating an already
 * acked frame as success matters: the lockstep harness asks once per frame
 * and would deadlock if a confirmed frame reported failure. */
int dr_lobby_send_input(DrLobby *l,uint32_t frame,uint32_t input)
{
    unsigned slot;
    if(!l || l->fd<0 || l->state!=LOBBY_PAIRED) return 0;
    if(!frame) return 0;
    if((int32_t)(frame-(l->tx_highest+D52_INPUT_DEPTH))>0) return 0; /* beyond window */
    if((int32_t)(frame-l->tx_acked)<=0) return 1;   /* peer already has it */
    slot=frame&D52_INPUT_MASK;
    l->tx_value[slot]=input;
    l->tx_frame_of[slot]=frame;
    l->tx_sent|=1u<<slot;
    l->tx_sent_at[slot]=l->tick_now;
    l->tx_rtx[slot]=0;
    if((int32_t)(frame-l->tx_highest)>0 || !l->tx_highest) l->tx_highest=frame;
    send_input_packet(l,&l->peer,l->nonce,l->session,input,frame,l->rx_frame);
    return 1;
}

/* Drain INPUT datagrams into the receive window. Non-INPUT datagrams are
 * left in the socket for dr_lobby_tick, so nothing the state machine needs
 * is ever discarded here. */
static void lobby_pump_inputs(DrLobby *l)
{
    for(int budget=0;budget<32;budget++) {
        unsigned char p[PACKET];struct sockaddr_in from={0};
        socklen_t size=sizeof from;
        ssize_t n=recvfrom(l->fd,p,sizeof p,MSG_PEEK|MSG_DONTWAIT,
                           (struct sockaddr *)&from,&size);
        if(n!=(ssize_t)PACKET) return;
        if(memcmp(p,"DRL1",4) || rd(p+4)!=1) return;
        if(rd(p+8)!=INPUT) return;                  /* control: leave for tick */
        size=sizeof from;
        n=recvfrom(l->fd,p,sizeof p,MSG_DONTWAIT,(struct sockaddr *)&from,&size);
        if(n!=(ssize_t)PACKET) return;
        if(rd(p+16)!=l->session) continue;          /* not our session */
        uint32_t value=rd(p+20),frame=rd(p+24),ack=rd(p+28);
        unsigned slot;
        if((int32_t)(ack-l->tx_acked)>0) {          /* peer got our frames */
            if((int32_t)(ack-l->tx_acked)>=(int32_t)D52_INPUT_DEPTH)
                l->tx_sent=0;
            else
                for(uint32_t f=l->tx_acked+1;f<=ack;f++)
                    l->tx_sent&=~(1u<<(f&D52_INPUT_MASK));
            l->tx_acked=ack;
        }
        if((int32_t)(frame-l->rx_frame)<=0) continue;      /* old or duplicate */
        if((int32_t)(frame-(l->rx_frame+D52_INPUT_DEPTH))>0) continue; /* too far */
        slot=frame&D52_INPUT_MASK;
        l->rx_value[slot]=value;
        l->rx_frame_of[slot]=frame;
        l->rx_present|=1u<<slot;
        while(l->rx_present&(1u<<((l->rx_frame+1)&D52_INPUT_MASK))) {
            l->rx_frame++;
            l->rx_present&=~(1u<<(l->rx_frame&D52_INPUT_MASK));
        }
    }
}

/* Return the next peer input in frame order, or 0 when nothing new arrived.
 * The frame stays queued until dr_lobby_consume_input is called. */
int dr_lobby_peek_input(DrLobby *l,uint32_t *frame,uint32_t *input)
{
    if(!l || l->fd<0 || l->state!=LOBBY_PAIRED || !frame || !input) return 0;
    lobby_pump_inputs(l);
    if((int32_t)(l->rx_frame-l->rx_delivered)<=0) return 0;
    *frame=l->rx_delivered+1;
    *input=l->rx_value[*frame&D52_INPUT_MASK];
    return 1;
}

void dr_lobby_consume_input(DrLobby *l)
{
    if(!l) return;
    if((int32_t)(l->rx_frame-l->rx_delivered)>0) l->rx_delivered++;
}

/* Frames the peer is still missing are re-sent after a short delay. This is
 * the live-combat path; the 12 s window in dr_lobby_tick only notices a dead
 * link and is far too slow to hide an input gap. */
void dr_lobby_retransmit(DrLobby *l,uint32_t now)
{
    unsigned slot;uint32_t frame;
    if(!l || l->fd<0 || l->state!=LOBBY_PAIRED) return;
    lobby_pump_inputs(l);
    for(frame=l->tx_acked+1;frame<=l->tx_highest;frame++) {
        slot=frame&D52_INPUT_MASK;
        if(!(l->tx_sent&(1u<<slot)) || l->tx_frame_of[slot]!=frame) continue;
        if(l->tx_rtx[slot]>=D52_INPUT_RTX_MAX) continue;
        if((uint32_t)(now-l->tx_sent_at[slot])<D52_INPUT_RTX_MS) continue;
        send_input_packet(l,&l->peer,l->nonce,l->session,
                          l->tx_value[slot],frame,l->rx_frame);
        l->tx_sent_at[slot]=now;
        l->tx_rtx[slot]++;
        return;                                     /* one resend per tick */
    }
}

void dr_lobby_send_hash(DrLobby *l,uint32_t hash,uint32_t frame)
{
    if(!l || l->fd<0 || l->state!=LOBBY_PAIRED) return;
    send_hash_packet(l,&l->peer,l->nonce,l->session,hash,frame);
}

int dr_lobby_receive_hash(DrLobby *l,uint32_t *hash,uint32_t *frame)
{
    if(!l || l->fd<0 || l->state!=LOBBY_PAIRED || !hash) return 0;
    for(;;) {
        unsigned char p[PACKET];struct sockaddr_in from={0};
        socklen_t size=sizeof from;
        ssize_t n=recvfrom(l->fd,p,sizeof p,MSG_PEEK|MSG_DONTWAIT,
                           (struct sockaddr *)&from,&size);
        if(n!=(ssize_t)PACKET) return 0;
        if(memcmp(p,"DRL1",4) || rd(p+4)!=1) return 0;
        if(rd(p+8)!=HASH) return 0;                 /* control: leave for tick */
        size=sizeof from;
        n=recvfrom(l->fd,p,sizeof p,MSG_DONTWAIT,(struct sockaddr *)&from,&size);
        if(n!=(ssize_t)PACKET) return 0;
        if(rd(p+16)!=l->session) continue;
        *hash=rd(p+20);
        if(frame) *frame=rd(p+24);
        return 1;
    }
}

uint32_t compute_lobby_state_hash(const DrLobby *l)
{
    /* FNV-1a over the fields both consoles must agree on. */
    uint32_t h=2166136261u;
    if(!l) return h;
    const uint32_t fields[]={
        (uint32_t)l->state,(uint32_t)l->host,l->nonce,l->session,
        l->peer.sin_addr.s_addr,(uint32_t)ntohs(l->peer.sin_port),
        l->target.sin_addr.s_addr,(uint32_t)ntohs(l->target.sin_port),
    };
    for(size_t i=0;i<sizeof fields/sizeof fields[0];i++)
        for(int b=0;b<4;b++){h^=(fields[i]>>(b*8))&0xFFu;h*=16777619u;}
    return h;
}

uint32_t compute_battle_state_hash(const void *state,size_t size)
{
    /* FNV-1a: byte-exact, endian-defined, identical on both consoles. */
    const unsigned char *p=state;
    uint32_t h=2166136261u;
    if(!p) return h;
    for(size_t i=0;i<size;i++){h^=p[i];h*=16777619u;}
    return h;
}
