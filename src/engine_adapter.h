/* SPDX-License-Identifier: MIT
 * D53-A: the seam between the lockstep harness (src/online_battle.c) and a
 * battle engine.
 *
 * The adapter exists so that swapping the placeholder simulation for the real
 * bt3xbx Battle_Main is a change of one pointer, not a refactor of the engine.
 * Two backends are expected:
 *
 *   dr_reference_ops  - small integer-only simulation, used by the host tests
 *                       and by CI. It has no engine, no GPU and no ISO data.
 *   dr_bt3xbx_ops     - the real engine (src/bt3xbx_adapter.c), built only
 *                       when DR_HAVE_BT3XBX is defined, i.e. in the XEX.
 *
 * Everything in this header is backend agnostic and is exercised by the tests
 * whatever backend is bound.
 */
#ifndef DR_ENGINE_ADAPTER_H
#define DR_ENGINE_ADAPTER_H
#include <stdint.h>
#include <stddef.h>

/* Lifecycle of one adapter instance. The phase machine is the whole point of
 * this file: it makes an out-of-order call (update before start, end twice,
 * start while running) a reported error instead of a corrupted engine. */
typedef enum {
    DR_ENGINE_UNBOUND = 0,  /* no backend bound; nothing may be called */
    DR_ENGINE_IDLE,         /* bound; start() is allowed */
    DR_ENGINE_RUNNING,      /* start() succeeded; update()/end() allowed */
    DR_ENGINE_ENDED         /* end() ran; start() is allowed again */
} DrEnginePhase;

/* One engine backend. Every function pointer must be non-NULL to bind.
 *
 *   start  - begin a battle. 0 = started, <0 = refused (the instance stays
 *            IDLE so the caller may retry).
 *   update - simulate exactly ONE frame from the two pad words. in0 is player
 *            one, in1 player two, both in the D52-2 input bit layout.
 *            0 = frame simulated, 1 = the battle finished, <0 = backend error.
 *            The instance stays RUNNING after 0 and after 1 so that end() can
 *            still release the engine; a <0 only sets the failed flag for the
 *            same reason.
 *   end    - release the battle. Called at most once per start(), guaranteed
 *            by the phase machine, which is what makes shutdown safe.
 *   hash   - deterministic hash of the current simulation state. Both
 *            consoles must return the same number for the same inputs.
 *            Pointer-valued fields are excluded by the backend: heap addresses
 *            are not part of the simulation.
 *
 * The backend must be frame driven: update() advances exactly one frame and
 * must not read a wall clock, or the two consoles would drift apart. */
typedef struct {
    const char *name;
    int (*start)(void *ctx);
    int (*update)(void *ctx,uint32_t in0,uint32_t in1);
    int (*end)(void *ctx);
    uint32_t (*hash)(void *ctx);
} DrEngineOps;

/* One adapter instance. A plain struct with no heap and no globals, so a host
 * test can run two of them side by side while the console keeps one static. */
typedef struct {
    const DrEngineOps *ops;
    void *ctx;
    DrEnginePhase phase;
    uint32_t frame;             /* frames simulated since the last start() */
    uint32_t starts, ends, updates; /* counters for the tests and the log */
    int finished;               /* update() returned 1 at least once */
    int failed;                 /* update() returned <0 at least once */
    char error[160];            /* why the last rejected call was rejected */
} DrEngineAdapter;

/* Bind a backend and reset the instance to IDLE. Returns 0, or <0 when a is
 * NULL or ops/ctx is missing a piece; then the instance stays UNBOUND and
 * error says which. Binding again over whatever was there is allowed. */
int dr_engine_bind(DrEngineAdapter *a,const DrEngineOps *ops,void *ctx);

/* Phase-guarded calls. Each returns 0 on success (update() returns the
 * backend's code) and <0 when the phase forbids it; in that case the backend
 * is NOT called and error explains why. They never crash on a bad order. */
int dr_engine_start(DrEngineAdapter *a);
int dr_engine_update(DrEngineAdapter *a,uint32_t in0,uint32_t in1);
int dr_engine_end(DrEngineAdapter *a);

/* Hash of the current state, valid only while RUNNING; 0 otherwise. */
uint32_t dr_engine_hash(const DrEngineAdapter *a);

/* Introspection, for logs and tests. */
DrEnginePhase dr_engine_phase(const DrEngineAdapter *a);
const char *dr_engine_phase_name(DrEnginePhase phase);
const char *dr_engine_error(const DrEngineAdapter *a);
const char *dr_engine_backend_name(const DrEngineAdapter *a);

/* ---- reference backend: the test simulation ---------------------------- *
 * Integer only, no floats, no RNG, no time: two instances fed the same
 * inputs produce byte-identical state, which is exactly what the D53-A test
 * checks. It stands in for bt3xbx wherever the engine cannot be built. */
typedef struct {
    uint32_t frame;             /* frame just simulated */
    int32_t px[2], py[2];       /* positions in a 640x360 arena */
    int32_t vx[2], vy[2];       /* velocities of that frame */
    int32_t hp[2];              /* 1000 at start, -10 per hit */
    uint32_t in[2];             /* the two pad words used for that frame */
    int started;
} DrReferenceState;

/* The backend table and its reset helper. */
extern const DrEngineOps dr_reference_ops;
void dr_reference_reset(DrReferenceState *s);

#endif
