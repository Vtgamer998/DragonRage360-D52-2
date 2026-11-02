/* SPDX-License-Identifier: MIT
 * D53-A: the reference engine backend.
 *
 * A small integer-only battle stand-in: two fighters in a 640x360 arena,
 * movement on the d-pad/face buttons and a melee hit when the boxes overlap.
 *
 * It exists so the adapter contract can be proven on a host without the
 * Xbox toolchain, the GPU or the ISO data:
 *
 *   - it is frame driven (update() advances exactly one frame, reads no
 *     clock), which is the property the lockstep layer depends on;
 *   - it is deterministic (no RNG, no floats, no pointers in the state);
 *   - the hash is FNV-1a over explicit words, so struct padding can never
 *     leak into it.
 *
 * D53-B replaces this backend with bt3xbx_adapter.c; the adapter contract,
 * the phase machine and the tests do not change.
 */
#include "engine_adapter.h"
#include <string.h>

enum { REF_ARENA_W=640, REF_ARENA_H=360, REF_HP_MAX=1000, REF_SPEED=4,
       REF_BOX=16 };

/* Input bit layout, shared with the D52-2 wire format and the Xbox pad:
 *   bit0 left bit1 right bit2 up bit3 down
 *   bit4 A     bit5 B     bit6 X bit7 Y */
enum { IN_LEFT=1u<<0, IN_RIGHT=1u<<1, IN_UP=1u<<2, IN_DOWN=1u<<3,
       IN_A=1u<<4 };

static int32_t clampi(int32_t v,int32_t lo,int32_t hi)
{ return v<lo?lo:(v>hi?hi:v); }

void dr_reference_reset(DrReferenceState *s)
{
    if(!s) return;
    memset(s,0,sizeof *s);
    s->hp[0]=s->hp[1]=REF_HP_MAX;
    s->px[0]=160; s->px[1]=480;
    s->py[0]=s->py[1]=REF_ARENA_H/2;
    s->started=1;
}

static int ref_start(void *ctx)
{
    DrReferenceState *s=ctx;
    if(!s) return -1;
    dr_reference_reset(s);
    return 0;
}

/* Pure function of (previous state, both inputs): the definition of
 * determinism. Same inputs in, same state out, on any machine. */
static int ref_update(void *ctx,uint32_t in0,uint32_t in1)
{
    DrReferenceState *s=ctx;
    int32_t nx[2],ny[2];
    int i;
    if(!s || !s->started) return -1;

    for(i=0;i<2;i++) {
        uint32_t in=i?in1:in0;
        int32_t vx=0,vy=0;
        if(in&IN_LEFT)  vx=-REF_SPEED;
        if(in&IN_RIGHT) vx= REF_SPEED;
        if(in&IN_UP)    vy=-REF_SPEED;
        if(in&IN_DOWN)  vy= REF_SPEED;
        s->vx[i]=vx; s->vy[i]=vy;
        nx[i]=clampi(s->px[i]+vx,0,REF_ARENA_W-REF_BOX);
        ny[i]=clampi(s->py[i]+vy,0,REF_ARENA_H-REF_BOX);
    }
    s->px[0]=nx[0]; s->px[1]=nx[1];
    s->py[0]=ny[0]; s->py[1]=ny[1];
    s->in[0]=in0; s->in[1]=in1;

    /* Melee: A damages the opponent while the two boxes overlap. */
    if((nx[0]+REF_BOX>nx[1]) && (nx[1]+REF_BOX>nx[0]) &&
       (ny[0]+REF_BOX>ny[1]) && (ny[1]+REF_BOX>ny[0])) {
        if(in0&IN_A) s->hp[1]=clampi(s->hp[1]-10,0,REF_HP_MAX);
        if(in1&IN_A) s->hp[0]=clampi(s->hp[0]-10,0,REF_HP_MAX);
    }
    s->frame++;
    return 0;                          /* the battle never ends by itself */
}

static int ref_end(void *ctx)
{
    DrReferenceState *s=ctx;
    if(!s) return -1;
    s->started=0;
    return 0;
}

/* FNV-1a over explicit words: no struct padding, no floats, no pointers, no
 * RNG. Both consoles walk the same list in the same order. */
static uint32_t ref_hash(void *ctx)
{
    const DrReferenceState *s=ctx;
    uint32_t w[16];
    size_t n=0,i;
    uint32_t h=2166136261u;
    if(!s || !s->started) return 0;
    w[n++]=s->frame;
    w[n++]=(uint32_t)s->started;
    for(i=0;i<2;i++) w[n++]=(uint32_t)s->px[i];
    for(i=0;i<2;i++) w[n++]=(uint32_t)s->py[i];
    for(i=0;i<2;i++) w[n++]=(uint32_t)s->vx[i];
    for(i=0;i<2;i++) w[n++]=(uint32_t)s->vy[i];
    for(i=0;i<2;i++) w[n++]=(uint32_t)s->hp[i];
    w[n++]=s->in[0];
    w[n++]=s->in[1];
    for(i=0;i<n;i++) {
        h^=(w[i])&0xFFu;         h*=16777619u;
        h^=(w[i]>>8)&0xFFu;      h*=16777619u;
        h^=(w[i]>>16)&0xFFu;     h*=16777619u;
        h^=(w[i]>>24)&0xFFu;     h*=16777619u;
    }
    return h;
}

const DrEngineOps dr_reference_ops = {
    "reference", ref_start, ref_update, ref_end, ref_hash
};
