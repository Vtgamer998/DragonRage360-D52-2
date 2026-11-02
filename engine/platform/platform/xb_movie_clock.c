/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_movie_clock.h"
int XbMovieClock_Due(XbMovieClock *c,uint32_t frame,uint64_t now,uint64_t audio,int ended) {
    if(!c)return -1;
    if(!c->started){c->started=1;c->last_progress=now;c->media=audio;}
    if(now<c->last_progress || audio<c->media)return -1;
    if(audio>c->media){c->media=audio;c->last_progress=now;}
    if(ended && !c->tail){c->tail=1;c->wall_anchor=now;c->media_anchor=audio;}
    uint64_t time=c->tail?c->media_anchor+now-c->wall_anchor:audio;
    if(!c->tail && now-c->last_progress>2000000000ull)return -1;
    return time>=(uint64_t)frame*1001000000000ull/30000;
}
