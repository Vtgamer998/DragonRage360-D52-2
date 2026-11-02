/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Native adaptation of pinned src/sys/movie.c, no IPU/DMA MMIO. */
#include "common.h"
#include "sys/adx.h"
#include "sys/dma.h"
#include "sys/fade.h"
#include "sys/file.h"
#include "sys/gfx.h"
#include "sys/heap.h"
#include "sys/movie.h"
#include "sys/pad.h"
#include "dr_gs_movie.h"
#include <stdint.h>
extern int XbAdx_Clock(void *,uint64_t *,int *);
#include "xb_time.h"
#include "xb_movie_clock.h"
#include "xsdk/log.h"
extern int XbSonyMpeg_End(void *);
extern int XbSonyMpeg_Error(void *);
extern int XbSonyMpeg_CanQueue(void *,u32);
extern void Port_VBlank(void);
static int movie_eof,movie_error;

extern void *memset(void *dst, s32 c, u32 n);
extern void *memcpy(void *dst, const void *src, u32 n);

/* libmpeg */
extern s32 sceMpegInit(void);
extern s32 sceMpegCreate(sceMpeg *mp, u8 *work, s32 size);
extern s32 sceMpegDelete(sceMpeg *mp);
extern s32 sceMpegGetPicture(sceMpeg *mp, u8 *rgb, s32 mbCount);
extern s32 sceMpegReset(sceMpeg *mp);
extern s32 sceMpegIsEnd(sceMpeg *mp);
extern void *sceMpegAddCallback(sceMpeg *mp, s32 type, sceMpegCallback cb, void *data);
extern void *sceMpegAddStrCallback(sceMpeg *mp, s32 strType, s32 ch, sceMpegCallback cb, void *data);
extern s32 sceMpegDemuxPss(sceMpeg *mp, u8 *data, s32 len);

/* CRI */
extern ADXF ADXF_Open(char *fname, void *atr);
extern void ADXF_Close(ADXF adxf);
extern s32 ADXF_ReadNw(ADXF adxf, s32 nsct, void *buf);
extern s32 ADXF_GetStat(ADXF adxf);
extern s32 ADXF_Tell(ADXF adxf);    /* file position in sectors (locked wrapper around adxf_Tell) */
extern s32 ADXT_GetStat(ADXT_HN adxt);
extern void ADXT_Pause(ADXT_HN adxt, s32 sw);
extern s32 func_00277A70(ADXT_HN adxt); /* non-zero while the player's own stream must not be disturbed (inferred) */
extern void ADXM_ExecMain(void);

/* hardware timer helpers (src/sys/timer.c, declared locally): timer 0 counts horizontal blanks */
extern void Timer_Init(s32 ch);      /* sets the timer up */
extern void Timer_Start(s32 ch);     /* starts it and clears the count */
extern void Timer_Reset(s32 ch);     /* clears the count */
extern f32 Timer_GetFrames(s32 ch);  /* time since the count was cleared, in 1/60 s */

extern void Pad_Update(void);

extern AdxPlayer gAdxPlayerTbl[6];
extern s32 gMovieRunCount;
extern u8 *gMovieEsBuf;
extern s32 gMovieEsSize;
extern u8 *gMovieEsWrite;
extern u8 *gMovieEsDmaStart;
extern u8 *gMovieEsDmaEnd;
/* "idle", "nodata" and "full": string literals in the original (the compiler puts strings of 8 bytes or less in
   .sdata, 0x2FE980..0x2FE998, and still addresses them with lui/addiu). Kept as externs until the data is linked. */
extern char gMovieTagIdle[];
extern char gMovieTagNoData[];
extern char gMovieTagFull[];

#ifdef PORT
extern volatile u32 *Port_DmaChcr(s32 channel);
#define D4_CHCR Port_DmaChcr(4)
#else
#define D4_CHCR ((volatile u32 *)0x1000B400)
#endif
#define D4_MADR ((volatile u32 *)0x1000B410)
#define D4_QWC ((volatile u32 *)0x1000B420)

#ifdef PORT
#define UNCACHED(p) ((void *)(p)) /* no uncached mirror of memory on PC */
#else
#define UNCACHED(p) ((void *)(((u32)(p) & 0x0FFFFFFF) | 0x20000000))
#endif

/* Builds the chain that uploads a w x h RGBA32 picture stored as 16x16 macroblocks in column order. */
void MovieTag_BuildImage(u32 *tags, u8 *image, s32 x, s32 y, s32 w, s32 h) {
    MovieTag_BuildBlocks(tags, image, MOVIE_MB_SIZE, x, y, w, h);
}

/* Builds the same chain with every block reading the same 16x16 pixels. */
void MovieTag_BuildFill(u32 *tags, u8 *image, s32 x, s32 y, s32 w, s32 h) {
    MovieTag_BuildBlocks(tags, image, 0, x, y, w, h);
}

/* Builds a GIF DMA chain that copies 16x16 blocks, columns first, from `image` to frame buffer page 0 at (x, y). */
void MovieTag_BuildBlocks(u32 *tags, u8 *image, s32 step, s32 x, s32 y, s32 w, s32 h) {
    if(dr_gs_movie_build(tags,image,step,x,y,w,h)) {
        movie_error=1;xsdk_log_write("movie: invalid native upload descriptor");
    }
}

/* Writes one DMA source-chain tag. */
void MovieTag_SetDmaTag(u64 *p, s32 spr, s32 addr, s32 irq, s32 id, s32 pce, s32 qwc) {
    *p = ((u64)spr << 63) | ((u64)(addr & ~0xF) << 32) | ((u64)(u32)irq << 31) | ((u64)(u32)id << 28) |
         ((u64)(u32)pce << 26) | (u64)(u32)qwc;
}

/* Writes one GIF tag. */
void MovieTag_SetGifTag(u32 *p, u64 regs, s32 nreg, s32 flg, s32 prim, s32 pre, s32 eop, s32 nloop) {
    p[0] = (eop << 15) | nloop;
    p[1] = (pre << 14) | (prim << 15) | (flg << 26) | (nreg << 28);
    p[2] = regs & 0xFFFFFFFF;
    p[3] = regs >> 32;
}

/* Writes one A+D quadword: a GS register value and the register number. */
void MovieTag_SetReg(u32 *p, s32 reg, u64 value) {
    p[2] = reg;
    p[1] = value >> 32;
    p[0] = value & 0xFFFFFFFF;
    p[3] = 0;
}

/* Writes BITBLTBUF: destination buffer pointer, width and pixel format. */
void MovieTag_SetBitBltBuf(u32 *p, s32 dbp, s32 dbw, s32 dpsm) {
    MovieTag_SetReg(p, 0x50, ((u64)dpsm << 56) | ((u64)dbw << 48) | ((u64)dbp << 32));
}

/* Writes TRXPOS: destination position. */
void MovieTag_SetTrxPos(u32 *p, s32 dir, s32 x, s32 y) {
    MovieTag_SetReg(p, 0x51, ((u64)dir << 59) | ((u64)y << 48) | ((u64)x << 32));
}

/* Writes TRXREG: size of the transferred rectangle. */
void MovieTag_SetTrxReg(u32 *p, s32 w, s32 h) {
    MovieTag_SetReg(p, 0x52, ((u64)h << 32) | (u64)(u32)w);
}

/* Writes TRXDIR: 0 starts a host-to-local transfer. */
void MovieTag_SetTrxDir(u32 *p, s32 dir) {
    MovieTag_SetReg(p, 0x53, (u64)(u32)dir);
}

/* Starts an upload chain on the GIF DMA channel without waiting for it. */
void MovieTag_Send(u32 *tags) {
    int error=dr_gs_movie_send(tags);
    if(error){movie_error=1;xsdk_log_write("movie: GIF upload failed %d",error);}
}

/* Initialises libmpeg; called once at boot. */
void Movie_Init(void) {
    sceMpegInit();
}

/* Closes the movie if one is open. */
void Movie_Term(void) {
    Movie_Close();
}

/* Opens a movie: the .PSS video file and the .ADX sound file (started paused), all buffers and the decoder. */
void Movie_Open(char *pss, char *adx) {
    ADXF adxf;
    Movie *m;
    MovieBuf *buf;
    s32 i;

    Movie_Close();movie_eof=movie_error=0;
    adxf=ADXF_Open(pss,NULL);
    if(!adxf){movie_error=1;xsdk_log_write("movie: PSS open failed");return;}
    Adx_PlayFilePaused(ADX_CH_BGM, adx, 0x40, 0);
    gMovie = Heap_Alloc(sizeof(Movie), 0x20, 0, 2);
    if(!gMovie){ADXF_Close(adxf);Adx_Stop(ADX_CH_BGM);movie_error=1;return;}
    memset(gMovie, 0, sizeof(Movie));
    m = gMovie;
    m->adxf = adxf;
    m->adxt = gAdxPlayerTbl[ADX_CH_BGM].adxt;
    m->rgbSize = MOVIE_RGB_SIZE;
    m->tagSize = MOVIE_TAG_SIZE;
    m->workSize = MOVIE_WORK_SIZE;
    m->esSize = MOVIE_ES_SIZE;
    m->rgb = Heap_Alloc(MOVIE_RGB_SIZE, 0x40, 0, 2);
    m->tags[0] = Heap_Alloc(m->tagSize, 0x40, 0, 2);
    m->tags[1] = Heap_Alloc(m->tagSize, 0x40, 0, 2);
    m->work = Heap_Alloc(m->workSize, 0x40, 0, 2);
    m->es = Heap_Alloc(m->esSize, 0x40, 0, 2);
    for (i = 0, buf = m->buf; i < MOVIE_BUF_COUNT; i++, buf++) {
        buf->data = Heap_Alloc(MOVIE_BUF_SIZE, 0x40, 0, 2);
    }
    if(!m->rgb || !m->tags[0] || !m->tags[1] || !m->work || !m->es) {
        movie_error=1;Movie_Close();return;
    }
    for(i=0;i<MOVIE_BUF_COUNT;i++)if(!m->buf[i].data){movie_error=1;Movie_Close();return;}
    if(sceMpegCreate(&m->mpeg,m->work,m->workSize)<0){movie_error=1;Movie_Close();return;}
    sceMpegAddStrCallback(&m->mpeg, SCE_MPEG_STR_M2V, 0, (sceMpegCallback)Movie_CbVideoData, NULL);
    sceMpegAddCallback(&m->mpeg, SCE_MPEG_CB_NODATA, Movie_CbNoData, NULL);
    sceMpegAddCallback(&m->mpeg, SCE_MPEG_CB_BACKGROUND, Movie_CbBackground, NULL);
    sceMpegAddCallback(&m->mpeg, SCE_MPEG_CB_ERROR, Movie_CbError, NULL);
    MovieTag_BuildImage(m->tags[0], m->rgb, 0, 0, MOVIE_WIDTH, MOVIE_HEIGHT);
    MovieTag_BuildImage(m->tags[1], m->rgb, 0, MOVIE_HEIGHT, MOVIE_WIDTH, MOVIE_HEIGHT);
}

/* Stops the sound, deletes the decoder, closes the file and frees everything Movie_Open allocated. */
void Movie_Close(void) {
    Movie *m = gMovie;
    MovieBuf *buf;
    s32 i;

    if (m != NULL) {
        Adx_Stop(ADX_CH_BGM);
        sceMpegReset(&m->mpeg);
        sceMpegDelete(&m->mpeg);
        ADXF_Close(m->adxf);
        Heap_Free(m->rgb);
        Heap_Free(m->tags[0]);
        Heap_Free(m->tags[1]);
        Heap_Free(m->work);
        Heap_Free(m->es);
        for (i = 0, buf = m->buf; i < MOVIE_BUF_COUNT; i++, buf++) {
            Heap_Free(buf->data);
        }
        Heap_Free(m);
        gMovie = NULL;
    }
}

/* Plays the opening movie from start to end (or until it is skipped). */
void Movie_PlayOpening(void) {
    Movie_Open("zs3usop.pss", "zs3usop.adx");
    Movie_Run();
    Movie_Close();
}

/* Plays the ending movie. */
void Movie_PlayEnding(void) {
    Movie_Open("zs3used.pss", "zs3used.adx");
    Movie_Run();
    Movie_Close();
}

/* The player's frame loop: decodes and shows one picture per frame until the stream ends or START skips it. */
void Movie_Run(void) {
    Movie *m=gMovie;
    XbMovieClock clock={0};u32 frame=0;int started=0,skip=0,wait=0;
    if(!m || movie_error)return;
    gMovieRunCount++;
    Movie_FillBufs();Movie_Demux();
    while(!movie_error && !sceMpegIsEnd(&m->mpeg)) {
        uint64_t audio,now;int ended,due,result;
        Gfx_BeginFrame();Pad_Update();
        if(!skip && (gPad[0].gamePressed & MOVIE_SKIP_BUTTON)) {Fade_Start(0,FADE_OUT,0.5f);skip=1;}
        if(skip && Fade_IsDone(0) && ++wait>=4)break;
        result=sceMpegGetPicture(&m->mpeg,m->rgb,MOVIE_MB_COUNT);
        if(result<0){if(!sceMpegIsEnd(&m->mpeg))movie_error=1;break;}
        if(result!=0){movie_error=1;xsdk_log_write("movie: no progress feeding decoder");break;}
        if(started)for(;;) {
            if(XbAdx_Clock(m->adxt,&audio,&ended)){movie_error=1;break;}
            now=XbTime_NowNs();due=XbMovieClock_Due(&clock,frame,now,audio,ended);
            if(due<0){movie_error=1;xsdk_log_write("movie: audio clock stalled or regressed");break;}
            if(due)break;
            Port_VBlank();
        }
        if(movie_error)break;
        if(sceGsSyncPath(0,0)<0){movie_error=1;break;}
        MovieTag_Send(gGfx.field?m->tags[0]:m->tags[1]);
        if(movie_error || sceGsSyncPath(0,0)<0){movie_error=1;break;}
        Dma_Flush();Gfx_EndFrame(1);
        if(!started) {
            Adx_Resume(ADX_CH_BGM);m->audioOn=1;started=1;
            if(XbAdx_Clock(m->adxt,&audio,&ended)){movie_error=1;break;}
            (void)XbMovieClock_Due(&clock,0,XbTime_NowNs(),audio,ended);
        }
        frame++;
    }
    Adx_Stop(ADX_CH_BGM);m->audioOn=0;
    Fade_Start(0,FADE_OUT,0.0f);
    Gfx_BeginFrame();Dma_Flush();Gfx_EndFrame(1);
    Gfx_BeginFrame();Dma_Flush();Gfx_EndFrame(1);Fade_Reset(0);
    xsdk_log_write("movie: presented %u frames error=%d decoder=%d",frame,movie_error,XbSonyMpeg_Error(&m->mpeg));
}

/* libmpeg stream callback: appends demuxed video data to the ring buffer; returns 0 when it does not fit. */
s32 Movie_CbVideoData(sceMpeg *mp, sceMpegCbDataStr *str, void *data) {
    (void)data;
    if(!str || (str->len && !str->data) || str->type!=SCE_MPEG_STR_M2V)return 0;
    return XbSonyMpeg_CanQueue(mp,str->len);
}

/* libmpeg "no data" callback: makes sure up to 0x1000 bytes are queued and sends them to the IPU by DMA. */
s32 Movie_CbNoData(sceMpeg *mp, void *cbData, void *data) {
    int bytes;(void)cbData;(void)data;
    bytes=Movie_Demux();
    if(!bytes && !movie_eof){Movie_ReadAhead("native no data");bytes=Movie_Demux();}
    if(movie_eof && Movie_FindBuf(1)==NULL) {
        if(XbSonyMpeg_End(mp)<0)movie_error=1;
        return !movie_error;
    }
    return bytes>0 && !movie_error;
}

/* libmpeg "background" callback, called while the decoder is busy: demuxes and reads ahead. */
s32 Movie_CbBackground(sceMpeg *mp, void *cbData, void *data) {
    (void)mp;(void)cbData;(void)data;
    if(!movie_eof)Movie_ReadAhead("native background");
    return !movie_error;
}

/* libmpeg error callback: carries on. */
s32 Movie_CbError(sceMpeg *mp, void *cbData, void *data) {
    (void)mp;(void)data;movie_error=1;
    xsdk_log_write("movie: MPEG error %d",cbData?*(s32 *)cbData:-1);return 0;
}

/* Returns the oldest filled read buffer (once its data has arrived) or the first empty one; NULL if none. */
MovieBuf *Movie_FindBuf(s32 full) {
    Movie *m = gMovie;
    MovieBuf *best = NULL;
    MovieBuf *buf = m->buf;
    s32 i;
    s32 end;

    if (full) {
        for (i = 0; i < MOVIE_BUF_COUNT; i++, buf++) {
            if (buf->used) {
                if (best == NULL || buf->seq < best->seq) {
                    best = buf;
                }
            }
        }
        if (best != NULL) {
            end = best->startSct + best->sectors;
            if (ADXF_Tell(m->adxf) < end) {
                Movie_WaitRead(end);
            }
        }
    } else {
        for (i = 0; i < MOVIE_BUF_COUNT; i++, buf++) {
            if (!buf->used) {
                best = buf;
                break;
            }
        }
    }
    return best;
}

/* Starts reading the next 0x100 sectors of the file into a buffer; returns the bytes requested. */
/* The six header stores sit in a `do { } while (0)` (presumably a statement macro in the original). Written as
   plain statements, every order of them compiles to the same wrong store order with `n << 11` computed late: the
   order comes from the scheduler, and the loop notes of the wrapper change it (found by decomp-permuter). */
s32 Movie_ReadBuf(MovieBuf *buf, char *tag) {
    Movie *m = gMovie;
    s32 pos;
    s32 n;

    if (buf == NULL) {
        return 0;
    }
    Movie_WaitRead(-1);
    pos = ADXF_Tell(m->adxf);
    n = ADXF_ReadNw(m->adxf, MOVIE_BUF_SECTORS, buf->data);
    if (n <= 0) {
        movie_eof=1;if(n<0)movie_error=1;
        return 0;
    }
    ADXM_ExecMain();
    do {
        buf->used = 1;
        buf->startSct = pos;
        buf->pos = 0;
        buf->sectors = n;
        buf->bytes = n << 11;
        buf->seq = m->seq;
    } while (0);
    m->seq++;
    return n << 11;
}

/* Waits for the disc read (to a sector, or to its end with -1), pausing the sound if the wait drags on. */
void Movie_WaitRead(s32 sector) {
    Movie *m=gMovie;s32 stat;(void)sector;
    if(!m)return;
    do {stat=ADXF_GetStat(m->adxf);if(stat==ADXF_STAT_READING)Port_VBlank();}while(stat==ADXF_STAT_READING);
    if(stat==4){movie_error=1;xsdk_log_write("movie: disc read failed");}
}

/* Starts a read into the first empty buffer, if there is one. */
s32 Movie_ReadAhead(char *tag) {
    MovieBuf *buf = Movie_FindBuf(0);
    s32 ret = 0;

    if (buf != NULL) {
        ret = Movie_ReadBuf(buf, tag);
    }
    return ret;
}

/* Starts reads until no buffer is empty; returns the bytes requested. */
s32 Movie_FillBufs(void) {
    s32 total = 0;
    MovieBuf *buf;

    while ((buf = Movie_FindBuf(0)) != NULL) {
        s32 n=Movie_ReadBuf(buf,gMovieTagFull);if(n<=0)break;total+=n;
    }
    return total;
}

/* Feeds the filled buffers to the demuxer in read order until it stops taking data; returns the bytes taken. */
s32 Movie_Demux(void) {
    MovieBuf *buf;
    s32 len;
    s32 n;
    s32 total;
    sceMpeg *mp;

    mp = &gMovie->mpeg;
    total = 0;
    do {
        buf = Movie_FindBuf(1);
        if (buf == NULL) {
            break;
        }
        len = buf->bytes - buf->pos;
        n = sceMpegDemuxPss(mp, buf->data + buf->pos, len);
        if(n<0){movie_error=1;break;}
        total += n;
        if (n >= len) {
            buf->used = 0;
            continue;
        }
        buf->pos += n;
        break;
    } while (1);
    return total;
}
