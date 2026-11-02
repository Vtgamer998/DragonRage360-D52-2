/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_mpeg2.h"
#include "xb_disc.h"
#include <stdlib.h>
typedef struct {dr_span span;uint64_t generation;} MovieSource;
static int read_source(void *ctx,uint64_t at,void *dst,size_t n) {
    const MovieSource *s=ctx;
    if(!XbDisc_Get() || s->generation!=XbDisc_Generation())return -1;
    return XbDisc_ReadSpan(&s->span,at,dst,n);
}
static void close_source(void *ctx){free(ctx);}
static int validate_source(void *ctx) {
    const MovieSource *s=ctx;
    return !XbDisc_Get() || s->generation!=XbDisc_Generation()?-1:0;
}
XbMpeg *XbMpeg_Open(const char *path) {
    if(!path || !XbDisc_Get())return NULL;
    MovieSource *s=malloc(sizeof(*s));if(!s)return NULL;
    s->generation=XbDisc_Generation();
    if(XbDisc_FindFile(path,&s->span)){free(s);return NULL;}
    XbMpegReader reader={s,s->span.size,read_source,close_source,validate_source};
    XbMpeg *m=XbMpeg_OpenReader(&reader);
    if(!m)free(s);
    return m;
}
