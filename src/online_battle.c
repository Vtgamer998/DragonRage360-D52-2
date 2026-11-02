/* SPDX-License-Identifier: MIT
 * D52-2: deterministic online battle harness.
 *
 * This is the seam between the lobby transport (src/lobby.c) and the battle
 * engine. It is a *lockstep* harness: both consoles only simulate a frame
 * once the input for that frame is known from both sides, so the two machines
 * always run the same (frame, inputs) sequence. That is what makes the state
 * hash meaningful.
 *
 * The simulation below is a small integer-only stand-in. D53 replaces
 * `sim_step()` with the real bt3xbx Battle_Main loop; everything else here
 * (frame numbering, input queueing, hash compare) stays as it is.
 */
#include "online_battle.h"
#include "lobby.h"
#include <string.h>

enum { SIM_ARENA_W=640, SIM_ARENA_H=360, SIM_HP_MAX=1000, SIM_SPEED=4 };

/* Input bit layout (matches the pad order the demo UI exposes):
 *   bit0 left bit1 right bit2 up bit3 down
 *   bit4 A     bit5 B     bit6 X bit7 Y */
enum { IN_LEFT=1u<<0, IN_RIGHT=1u<<1, IN_UP=1u<<2, IN_DOWN=1u<<3,
       IN_A=1u<<4, IN_B=1u<<5, IN_X=1u<<6, IN_Y=1u<<7 };

static int32_t clampi(int32_t v,int32_t lo,int32_t hi)
{ return v<lo?lo:(v>hi?hi:v); }

/* Pure function of (previous state, both inputs): the definition of
 * determinism. No time, no RNG, no pointers, no floats. */
static void sim_step(const DrOnlineBattle *prev,DrOnlineBattle *next,
                     uint32_t in_a,uint32_t in_b)
{
    int i;
    next->px[0]=prev->px[0]; next->px[1]=prev->px[1];
    next->py[0]=prev->py[0]; next->py[1]=prev->py[1];
    next->hp[0]=prev->hp[0]; next->hp[1]=prev->hp[1];
    next->in[0]=in_a; next->in[1]=in_b;
    for(i=0;i<2;i++) {
        uint32_t in=i?in_b:in_a;
        next->vx[i]=0; next->vy[i]=0;
        if(in&IN_LEFT)  next->vx[i]=-SIM_SPEED;
        if(in&IN_RIGHT) next->vx[i]= SIM_SPEED;
        if(in&IN_UP)    next->vy[i]=-SIM_SPEED;
        if(in&IN_DOWN)  next->vy[i]= SIM_SPEED;
        next->px[i]=clampi(prev->px[i]+next->vx[i],0,SIM_ARENA_W-16);
        next->py[i]=clampi(prev->py[i]+next->vy[i],0,SIM_ARENA_H-16);
    }
    /* Melee: A damages the opponent while the two boxes overlap. */
    if((in_a&IN_A) && (next->px[0]+16>next->px[1]) && (next->px[1]+16>next->px[0])
       && (next->py[0]+16>next->py[1]) && (next->py[1]+16>next->py[0]))
        next->hp[1]=clampi(next->hp[1]-10,0,SIM_HP_MAX);
    if((in_b&IN_A) && (next->px[0]+16>next->px[1]) && (next->px[1]+16>next->px[0])
       && (next->py[0]+16>next->py[1]) && (next->py[1]+16>next->py[0]))
        next->hp[0]=clampi(next->hp[0]-10,0,SIM_HP_MAX);
}

/* Hash the state word by word: no struct padding can ever leak into it. */
static uint32_t sim_hash(const DrOnlineBattle *b)
{
    uint32_t w[16];
    size_t n=0;
    w[n++]=b->frame;
    for(int i=0;i<2;i++) w[n++]=(uint32_t)b->px[i];
    for(int i=0;i<2;i++) w[n++]=(uint32_t)b->py[i];
    for(int i=0;i<2;i++) w[n++]=(uint32_t)b->vx[i];
    for(int i=0;i<2;i++) w[n++]=(uint32_t)b->vy[i];
    for(int i=0;i<2;i++) w[n++]=(uint32_t)b->hp[i];
    w[n++]=b->in[0];
    w[n++]=b->in[1];
    return compute_battle_state_hash(w,n*sizeof w[0]);
}

int dr_online_battle_init(DrOnlineBattle *b,DrLobby *l,int side)
{
    if(!b) return -1;
    memset(b,0,sizeof *b);
    if(!l || l->state!=LOBBY_PAIRED) return 0;
    b->lobby=l;
    b->active=1;
    b->side=(side==1);
    b->frame=1;
    b->hp[0]=b->hp[1]=SIM_HP_MAX;
    b->px[0]=160; b->px[1]=480;
    b->py[0]=b->py[1]=SIM_ARENA_H/2;
    return 1;
}

/* Compare a peer hash for frame F against the hash we computed for F.
 * Reporting is left to the caller so this file stays portable between the
 * Xbox build and the host test build (neither pulls in xsdk here). */
static void check_peer_hash(DrOnlineBattle *b,uint32_t hash,uint32_t frame)
{
    unsigned slot=(unsigned)(frame&(D52_HASH_RING-1u));
    if(!b->own_hash_frame[slot] || b->own_hash_frame[slot]!=frame) return;
    if(b->own_hash[slot]!=hash) {
        b->desynced=1;
        b->desync_frame=frame;
        b->desync_local=b->own_hash[slot];
        b->desync_remote=hash;
    } else {
        b->matched++;
    }
}

int dr_online_battle_update(DrOnlineBattle *b,uint32_t local_input,
                            uint32_t *remote_input)
{
    uint32_t f,v,hash;
    if(!b || !b->active || !b->lobby) return -1;

    /* Retransmit anything the peer has not acknowledged yet. */
    dr_lobby_retransmit(b->lobby,b->lobby->tick_now);

    /* Drain every peer input the transport has ordered for us. */
    while(dr_lobby_receive_input(b->lobby,&f,&v)) {
        if(f==b->frame && !b->remote_ready) { b->remote_value=v; b->remote_ready=1; }
        /* Frames past b->frame stay buffered inside the lobby. */
    }
    /* Answers to our hashes arrive the same way. */
    while(dr_lobby_receive_hash(b->lobby,&hash,&f)) check_peer_hash(b,hash,f);

    /* Queue our input for this frame exactly once; retries are the lobby's. */
    if(!b->local_ready) {
        if(dr_lobby_send_input(b->lobby,b->frame,local_input)) {
            b->local_value=local_input;
            b->local_ready=1;
        }
    }
    if(remote_input) *remote_input=b->remote_ready?b->remote_value:0;

    if(!b->local_ready || !b->remote_ready) return 0;  /* waiting for peer */

    /* Both inputs for this frame are known: step and publish the hash.
     * `side` decides which player the local pad drives, so both consoles
     * build the identical state from the same two input streams. */
    {
        DrOnlineBattle next=*b;
        if(b->side) sim_step(b,&next,b->remote_value,b->local_value);
        else        sim_step(b,&next,b->local_value,b->remote_value);
        next.frame=b->frame;
        *b=next;
    }
    hash=sim_hash(b);
    b->own_hash[b->frame&(D52_HASH_RING-1u)]=hash;
    b->own_hash_frame[b->frame&(D52_HASH_RING-1u)]=b->frame;
    dr_lobby_send_hash(b->lobby,hash,b->frame);

    b->frame++;
    b->local_ready=b->remote_ready=0;
    b->local_value=b->remote_value=0;
    return 1;
}

int dr_online_battle_desync(const DrOnlineBattle *b,uint32_t *frame,
                            uint32_t *local,uint32_t *remote)
{
    if(!b || !b->desynced) return 0;
    if(frame) *frame=b->desync_frame;
    if(local) *local=b->desync_local;
    if(remote)*remote=b->desync_remote;
    return 1;
}

uint32_t dr_online_battle_matched(const DrOnlineBattle *b)
{ return b?b->matched:0; }

uint32_t dr_online_battle_get_state_hash(const DrOnlineBattle *b)
{ return b&&b->active?sim_hash(b):0; }

uint32_t dr_online_battle_frame(const DrOnlineBattle *b)
{ return b?b->frame:0; }

int dr_online_battle_active(const DrOnlineBattle *b)
{ return b&&b->active; }

int dr_online_battle_get_state(const DrOnlineBattle *b,BattleStateView *view)
{
    if(!b||!view||!b->active) return -1;
    view->frame=b->frame;
    view->position_x[0]=b->px[0]; view->position_x[1]=b->px[1];
    view->position_y[0]=b->py[0]; view->position_y[1]=b->py[1];
    view->health[0]=b->hp[0];     view->health[1]=b->hp[1];
    return 0;
}

void dr_online_battle_shutdown(DrOnlineBattle *b)
{
    if(!b) return;
    memset(b,0,sizeof *b);
}
