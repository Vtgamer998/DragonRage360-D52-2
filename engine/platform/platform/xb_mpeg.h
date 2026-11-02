/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_MPEG_H
#define XB_MPEG_H
#include <stdint.h>
/* Native layout compatible with include/sys/movie.h; no Sony binary state. */
typedef struct {
    int32_t width,height,frameCount;
    int64_t pts,dts;uint64_t flags;int64_t pts2nd,dts2nd;uint64_t flags2nd;
    void *sys;
} XbMpegObject;
typedef struct {int32_t type;uint8_t *header,*data;uint32_t len;int64_t pts,dts;} XbMpegStreamData;
typedef int32_t (*XbMpegCallback)(void *,void *,void *);
int32_t sceMpegInit(void);
int32_t sceMpegCreate(void *,uint8_t *,int32_t);
int32_t sceMpegDelete(void *);
int32_t sceMpegReset(void *);
int32_t sceMpegGetPicture(void *,uint8_t *,int32_t);
int32_t sceMpegIsEnd(void *);
void *sceMpegAddCallback(void *,int32_t,XbMpegCallback,void *);
void *sceMpegAddStrCallback(void *,int32_t,int32_t,XbMpegCallback,void *);
int32_t sceMpegDemuxPss(void *,uint8_t *,int32_t);
/* Native EOF only after all file bytes/PES data were supplied, not starvation. */
int XbSonyMpeg_End(void *);
int XbSonyMpeg_Error(void *);
int XbSonyMpeg_CanQueue(void *,uint32_t);
#endif
