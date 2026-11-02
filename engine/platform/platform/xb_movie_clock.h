/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_MOVIE_CLOCK_H
#define XB_MOVIE_CLOCK_H
#include <stdint.h>
typedef struct {uint64_t media,last_progress,wall_anchor,media_anchor;int started,tail;} XbMovieClock;
/* 29.97 fps. 1 = picture due, 0 = wait, -1 = nonmonotonic/stalled audio.
 * Audio ends explicitly before tail switches to wall time. No frame skipping. */
int XbMovieClock_Due(XbMovieClock *,uint32_t frame,uint64_t now,uint64_t played_ns,int audio_ended);
#endif
