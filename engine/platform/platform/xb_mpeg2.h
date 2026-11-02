/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_MPEG2_H
#define XB_MPEG2_H
#include <stddef.h>
#include <stdint.h>
/* Single-owner CPU decoder for BT3 USA progressive I-frame 512x448 PSS.
 * No Sony callbacks, PS2 MMIO, GPU upload, audio clock or fight integration.
 * read returns 0 only after filling n bytes; all ranges are checked here.
 * An accepted reader is owned until Close, including its optional close hook.
 * Failed OpenReader leaves ownership with the caller. */
typedef struct XbMpeg XbMpeg;
typedef struct {
    void *context;
    uint64_t size;
    int (*read)(void *,uint64_t,void *,size_t);
    void (*close)(void *);
    int (*validate)(void *); /* optional: called before each frame, 0 = valid */
} XbMpegReader;
enum { XB_MPEG_WIDTH=512, XB_MPEG_HEIGHT=448,
       XB_MPEG_RGBA_BYTES=512*448*4,
       XB_MPEG_IO=-1, XB_MPEG_FORMAT=-2, XB_MPEG_MEMORY=-3,
       XB_MPEG_LIMIT=-4, XB_MPEG_ARGUMENT=-5, XB_MPEG_NEED_INPUT=2 };
/* Push mode: PES demux feeds bounded ES; NeedInput never consumes a partial
 * picture or changes output. EndStream flushes the last complete picture. */
XbMpeg *XbMpeg_OpenStream(void);
size_t XbMpeg_StreamSpace(const XbMpeg *);
int XbMpeg_PushES(XbMpeg *,const void *,size_t);
int XbMpeg_EndStream(XbMpeg *);
XbMpeg *XbMpeg_OpenReader(const XbMpegReader *reader);
/* Uses the real mounted ISO; every refill rejects a changed mount generation. */
XbMpeg *XbMpeg_Open(const char *disc_relative_path);
void XbMpeg_Close(XbMpeg *movie);
/* 1 frame, 0 clean end, negative error (sticky except invalid output argument).
 * RGBA output is unchanged on failure; capacity must cover the fixed frame. */
int XbMpeg_Next(XbMpeg *movie,void *rgba,size_t capacity);
int XbMpeg_Error(const XbMpeg *movie);
#endif
