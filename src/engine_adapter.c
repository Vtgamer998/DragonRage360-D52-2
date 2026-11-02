/* SPDX-License-Identifier: MIT
 * D53-A: engine adapter lifecycle.
 *
 * The phase machine below is what makes start/end safe. It guarantees:
 *
 *   - end() runs at most once per start(), so a battle is never released
 *     twice (double Battle_Term would free the engine's work twice);
 *   - update() is refused unless the engine is running, so the engine is
 *     never stepped before Battle_Init or after Battle_Term;
 *   - a failed start() leaves the instance IDLE, so the caller can retry;
 *   - a backend error during update() keeps the instance RUNNING, so end()
 *     can still release whatever the engine allocated;
 *   - nothing ever crashes on a mis-ordered call: the backend simply is not
 *     called, and error says why.
 */
#include "engine_adapter.h"
#include <stdio.h>
#include <string.h>

static int refuse(DrEngineAdapter *a,const char *msg)
{
    if(a) {
        snprintf(a->error,sizeof a->error,"%s",msg);
        a->error[sizeof a->error-1]=0;
    }
    return -1;
}

int dr_engine_bind(DrEngineAdapter *a,const DrEngineOps *ops,void *ctx)
{
    if(!a) return -1;
    memset(a,0,sizeof *a);
    a->phase=DR_ENGINE_UNBOUND;
    if(!ops) return refuse(a,"bind: ops is NULL");
    if(!ops->name) return refuse(a,"bind: ops->name is NULL");
    if(!ops->start) return refuse(a,"bind: ops->start is NULL");
    if(!ops->update) return refuse(a,"bind: ops->update is NULL");
    if(!ops->end) return refuse(a,"bind: ops->end is NULL");
    if(!ops->hash) return refuse(a,"bind: ops->hash is NULL");
    if(!ctx) return refuse(a,"bind: ctx is NULL");
    a->ops=ops;
    a->ctx=ctx;
    a->phase=DR_ENGINE_IDLE;
    a->error[0]=0;
    return 0;
}

int dr_engine_start(DrEngineAdapter *a)
{
    int rc;
    if(!a) return -1;
    if(a->phase==DR_ENGINE_UNBOUND) return refuse(a,"start: no backend bound");
    if(a->phase==DR_ENGINE_RUNNING) return refuse(a,"start: already running");
    /* IDLE and ENDED both mean "not running": a restart is legal. */
    rc=a->ops->start(a->ctx);
    if(rc<0) {
        a->phase=DR_ENGINE_IDLE;       /* stay startable */
        return refuse(a,"start: backend refused to start");
    }
    a->phase=DR_ENGINE_RUNNING;
    a->frame=0;
    a->finished=0;
    a->failed=0;
    a->starts++;
    a->error[0]=0;
    return 0;
}

int dr_engine_update(DrEngineAdapter *a,uint32_t in0,uint32_t in1)
{
    int rc;
    if(!a) return -1;
    if(a->phase!=DR_ENGINE_RUNNING)
        return refuse(a,"update: engine is not running");
    rc=a->ops->update(a->ctx,in0,in1);
    if(rc<0) {
        /* Keep RUNNING: the caller must still be able to call end() to
         * release whatever the engine allocated before it failed. */
        a->failed=1;
        return refuse(a,"update: backend reported an error");
    }
    a->frame++;
    a->updates++;
    if(rc==1) a->finished=1;            /* battle over: end() still allowed */
    a->error[0]=0;
    return rc;
}

int dr_engine_end(DrEngineAdapter *a)
{
    int rc;
    if(!a) return -1;
    if(a->phase!=DR_ENGINE_RUNNING)
        return refuse(a,"end: engine is not running");
    /* Move out of RUNNING first: even if the backend fails we consider the
     * engine released, otherwise a second end() would be allowed. */
    a->phase=DR_ENGINE_ENDED;
    a->ends++;
    rc=a->ops->end(a->ctx);
    if(rc<0) return refuse(a,"end: backend reported an error");
    a->error[0]=0;
    return 0;
}

uint32_t dr_engine_hash(const DrEngineAdapter *a)
{
    /* Only meaningful while the engine holds a live state. */
    if(!a || !a->ops || !a->ctx || a->phase!=DR_ENGINE_RUNNING) return 0;
    return a->ops->hash(a->ctx);
}

DrEnginePhase dr_engine_phase(const DrEngineAdapter *a)
{ return a?a->phase:DR_ENGINE_UNBOUND; }

const char *dr_engine_phase_name(DrEnginePhase phase)
{
    switch(phase) {
    case DR_ENGINE_UNBOUND: return "unbound";
    case DR_ENGINE_IDLE:    return "idle";
    case DR_ENGINE_RUNNING: return "running";
    case DR_ENGINE_ENDED:   return "ended";
    }
    return "?";
}

const char *dr_engine_error(const DrEngineAdapter *a)
{ return a?a->error:"(null adapter)"; }

const char *dr_engine_backend_name(const DrEngineAdapter *a)
{ return (a&&a->ops&&a->ops->name)?a->ops->name:"(none)"; }
