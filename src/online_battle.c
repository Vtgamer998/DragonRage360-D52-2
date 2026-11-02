/* SPDX-License-Identifier: MIT
 * D52-2 / D53-A: deterministic online battle harness.
 *
 * This is the seam between the lobby transport (src/lobby.c) and a battle
 * engine. It is a *lockstep* harness: both consoles only simulate a frame
 * once the input for that frame is known from both sides, so the two machines
 * always run the same (frame, inputs) sequence. That is what makes the state
 * hash meaningful.
 *
 * D53-A moved the simulation itself behind the engine adapter
 * (src/engine_adapter.h): this file no longer contains sim_step(), it calls
 * dr_engine_update() on whatever backend was bound. The default is the
 * reference simulation so the D52-2 tests keep proving the transport; binding
 * dr_bt3xbx_ops switches to the real engine without touching this file.
 *
 * Frame numbering: the PROTOCOL numbers frames from 1 (0 means "nothing"),
 * while the adapter counts updates from 0. After simulating protocol frame F
 * the adapter's counter is also F, which is why the hash published for frame
 * F comes straight from dr_engine_hash().
 */
#include "online_battle.h"
#include "lobby.h"
#include <string.h>

int dr_online_battle_init_with(DrOnlineBattle *b,DrLobby *l,int side,
                               const DrEngineOps *ops,void *ctx)
{
    if(!b) return -1;
    memset(b,0,sizeof *b);
    if(!l || l->state!=LOBBY_PAIRED) return 0;
    b->lobby=l;
    b->side=(side==1);
    b->frame=1;                     /* protocol frames start at 1 */
    if(!ops) ops=&dr_reference_ops;
    if(!ctx) ctx=&b->ref;           /* only valid for the reference backend */
    if(dr_engine_bind(&b->engine,ops,ctx)!=0) return -1;
    if(dr_engine_start(&b->engine)!=0) return -1;
    b->active=1;
    return 1;
}

int dr_online_battle_init(DrOnlineBattle *b,DrLobby *l,int side)
{
    return dr_online_battle_init_with(b,l,side,&dr_reference_ops,NULL);
}

int dr_online_battle_update(DrOnlineBattle *b,uint32_t local_input,
                            uint32_t *remote_input)
{
    uint32_t hash;
    if(!b || !b->active || !b->lobby) return -1;

    /* Retransmit anything the peer has not acknowledged yet. */
    dr_lobby_retransmit(b->lobby,b->lobby->tick_now);

    /* Take the peer's input only when it is the frame we are waiting for.
     * Anything newer has to stay in the lobby's queue: consuming it here and
     * discarding it would lose the frame while the ack still claims it
     * arrived, which deadlocks both sides. */
    if(!b->remote_ready) {
        uint32_t f,v;
        if(dr_lobby_peek_input(b->lobby,&f,&v) && f==b->frame) {
            b->remote_value=v;
            b->remote_ready=1;
            dr_lobby_consume_input(b->lobby);
        }
    }
    /* Answers to our hashes arrive the same way. */
    {
        uint32_t f,h;
        while(dr_lobby_receive_hash(b->lobby,&h,&f)) {
            unsigned slot=(unsigned)(f&(D52_HASH_RING-1u));
            if(!b->own_hash_frame[slot] || b->own_hash_frame[slot]!=f) continue;
            if(b->own_hash[slot]!=h) {
                b->desynced=1;
                b->desync_frame=f;
                b->desync_local=b->own_hash[slot];
                b->desync_remote=h;
            } else {
                b->matched++;
            }
        }
    }

    /* Queue our input for this frame exactly once; retries are the lobby's. */
    if(!b->local_ready) {
        if(dr_lobby_send_input(b->lobby,b->frame,local_input)) {
            b->local_value=local_input;
            b->local_ready=1;
        }
    }
    if(remote_input) *remote_input=b->remote_ready?b->remote_value:0;

    if(!b->local_ready || !b->remote_ready) return 0;  /* waiting for peer */

    /* Both inputs for this frame are known. `side` decides which player the
     * local pad drives, so both consoles hand the adapter the same (in0,in1)
     * pair: the adapter is side-agnostic and always simulates player 0 vs
     * player 1. */
    {
        uint32_t in0,in1;
        if(b->side) { in0=b->remote_value; in1=b->local_value; }
        else        { in0=b->local_value;  in1=b->remote_value; }
        if(dr_engine_update(&b->engine,in0,in1)<0) {
            b->active=0;
            return -1;              /* backend failed; end() still allowed */
        }
    }

    /* Publish the hash for the frame just simulated. */
    hash=dr_engine_hash(&b->engine);
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
{ return b&&b->active?dr_engine_hash(&b->engine):0; }

uint32_t dr_online_battle_frame(const DrOnlineBattle *b)
{ return b?b->frame:0; }

int dr_online_battle_active(const DrOnlineBattle *b)
{ return b&&b->active; }

const char *dr_online_battle_backend(const DrOnlineBattle *b)
{ return b?dr_engine_backend_name(&b->engine):"(none)"; }

int dr_online_battle_get_state(const DrOnlineBattle *b,BattleStateView *view)
{
    if(!b||!view||!b->active) return -1;
    /* Only the reference backend keeps its state in the instance. */
    if(b->engine.ops!=&dr_reference_ops) return -1;
    view->frame=b->ref.frame;
    view->position_x[0]=b->ref.px[0]; view->position_x[1]=b->ref.px[1];
    view->position_y[0]=b->ref.py[0]; view->position_y[1]=b->ref.py[1];
    view->health[0]=b->ref.hp[0];     view->health[1]=b->ref.hp[1];
    return 0;
}

void dr_online_battle_shutdown(DrOnlineBattle *b)
{
    if(!b) return;
    if(b->active) (void)dr_engine_end(&b->engine);   /* at most once */
    memset(b,0,sizeof *b);
}
