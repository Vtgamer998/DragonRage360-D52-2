/* SPDX-License-Identifier: MIT
 * The game's file access on Xbox 360: counterpart of port/src/plat_file.c of the PC port, same CRI ADXF entry
 * points, but over the mounted ISO (xb_disc.c) instead of a folder of extracted files.
 *
 * The game reads everything through CRI's ADXF library: three AFS archives ("partitions" 0..2), files addressed
 * by (partition, index), read in 2048-byte sectors, asynchronously. Here a read finishes at once.
 *
 * Differences from the PC port, on purpose:
 *   - no mods/ folder and no BT3_FILE_ALIAS (stages/songs added from outside the disc): not supported yet;
 *   - no host paths. The PC's sound code (snd_adx.c, snd_se.c, fight_panels.c) calls Port_FilePath and opens a
 *     FILE*; an ISO has none. Those callers must use XbFile_Locate + XbFile_Read below (TODO: adapt them);
 *   - bytes are returned exactly as on the disc (MIPS, little-endian). Converting resources that the game reads
 *     with native loads is NOT done here; gXbFilePostRead is the hook where the resource converters of the demo
 *     (dr_anim_prepare, dr_skeleton_prepare, ...) attach per file.
 */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "xb_disc.h"
#include "xb_file.h"
#include "xb_resconv.h"
#include "xsdk/log.h"

#define SECTOR 2048
enum { STAT_STOP = 1, STAT_READING = 2, STAT_READEND = 3, STAT_ERROR = 4 };

extern void *Port_LowAlloc(size_t size); /* xb_plat_mem.c: below 4 GB, zeroed (the game keeps the handle in 4 bytes) */
extern void Port_LowFree(void *addr);
extern void Port_VBlank(void); /* TODO: the platform's vertical blank (timing module, not written yet) */

/* What the game holds for an open file. */
typedef struct XbFile {
    dr_span span;  /* where the file is in the image */
    int32_t sizeSct;
    int32_t posSct;
    int32_t stat;
    int32_t ptid;  /* partition, or -1 for a file outside the archives */
    int32_t flid;
} XbFile;

/* Hook for per-resource conversion after a read: called with the bytes just read (n valid bytes at buf, the rest
   of the last sector zeroed), `off` = where in the file they start and `fileSize` = the file's size in bytes, so a
   converter can tell a whole-file read (off 0, n >= fileSize) from a partial one. NULL = none. xb_resconv.c
   installs itself here (XbRes_Install). */
void (*gXbFilePostRead)(int ptid, int flid, uint64_t off, uint64_t fileSize, void *buf, size_t n);

static char sPartDir[8][16]; /* "pzs3us1" per partition id, from ADXF_LoadPartitionNw */
char gPortMoviePath[512];    /* kept empty: no movie decoder on this platform yet; defined so plat code can link */

/* The path the PC port would use as its data root: the settings, saves, fingerprints and net files live there. */
const char *Port_FileRoot(void) { return "game:"; }

/* A game file by (partition, index) or, with fname, a loose file of the disc's DATA directory. 0 = found. */
int XbFile_Locate(int ptid, int flid, const char *fname, dr_span *out) {
    char rel[256];

    if (!out || (fname==NULL && (ptid<0 || ptid>2 || flid<0))) return -1;

    if (fname != NULL) {
        int n = snprintf(rel, sizeof(rel), "disc/DATA/");

        for (; *fname != '\0' && *fname != ';' && n < 255; fname++) {
            rel[n++] = *fname == '\\' ? '/' : (char)toupper((unsigned char)*fname);
        }
        rel[n] = '\0';
    } else {
        if (sPartDir[ptid & 7][0] == '\0') {
            return -1;
        }
        snprintf(rel, sizeof(rel), "%s/%05d.bin", sPartDir[ptid & 7], flid);
    }
    return XbDisc_FindFile(rel, out) == DR_OK ? 0 : -1;
}

/* Reads bytes [off, off+n) of a located file; whatever lies past its end is zeroed. Returns the bytes that came
   from the file. */
size_t XbFile_Read(const dr_span *span, uint64_t off, void *buf, size_t n) {
    if (!span || (!buf && n)) return 0;
    uint64_t have = off < span->size ? span->size - off : 0;
    size_t got = have < n ? (size_t)have : n;

    if (got != 0 && XbDisc_ReadSpan(span, off, buf, got) != DR_OK) {
        got = 0;
    }
    memset((uint8_t *)buf + got, 0, n - got);
    return got;
}

static XbFile *open_span(int ptid, int flid, const char *fname) {
    dr_span span;
    XbFile *f;

    if (XbFile_Locate(ptid, flid, fname, &span) != 0) {
        xsdk_log_write("file: not found: partition %d index %d name %s", ptid, flid, fname != NULL ? fname : "-");
        return NULL;
    }
    f = Port_LowAlloc(sizeof(XbFile)); /* zeroed */
    if (f == NULL) {
        return NULL;
    }
    f->span = span;
    f->sizeSct = (int32_t)((span.size + SECTOR - 1) / SECTOR);
    f->stat = STAT_STOP;
    f->ptid = fname != NULL ? -1 : ptid;
    f->flid = flid;
    return f;
}

/* "pzs3us1.afs" (any case, any directory, optional ";1") -> partition directory name "pzs3us1". */
int ADXF_LoadPartitionNw(int ptid, char *fname, void *dir, void *ptinfo) {
    const char *base = fname, *p;
    int n = 0;

    if(ptid<0 || ptid>2 || !fname)return -1;

    (void)dir;
    (void)ptinfo;
    for (p = fname; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    for (p = base; *p != '\0' && *p != '.' && n < 15; p++) {
        sPartDir[ptid & 7][n++] = (char)tolower((unsigned char)*p);
    }
    sPartDir[ptid & 7][n] = '\0';
    return 0;
}

int ADXF_GetPtStat(int ptid) { return sPartDir[ptid & 7][0] != '\0' ? STAT_READEND : STAT_STOP; }

void *ADXF_OpenAfs(int ptid, int flid) { return open_span(ptid, flid, NULL); }

/* A file outside the archives, named relative to the disc's data directory. */
void *ADXF_Open(char *fname, void *atr) {
    (void)atr;
    return open_span(0, 0, fname);
}

void ADXF_Close(void *h) {
    if (h != NULL) {
        Port_LowFree(h);
    }
}

int ADXF_GetFsizeSct(void *h) { return ((XbFile *)h)->sizeSct; }

/* Reads nsct sectors at the current position; the part of the last sector behind the end of the file is zero. */
int ADXF_ReadNw(void *h, int nsct, void *buf) {
    XbFile *f = h;
    uint64_t off;
    size_t want, got;

    if(!f)return 0;
    if(nsct<0 || (!buf && nsct) || (size_t)nsct>SIZE_MAX/SECTOR) {
        f->stat=STAT_ERROR;return 0;
    }

    if (nsct > f->sizeSct - f->posSct) {
        nsct = f->sizeSct - f->posSct;
    }
    if (nsct <= 0) {
        f->stat = STAT_READEND;
        return 0;
    }
    off = (uint64_t)f->posSct * SECTOR;
    want = (size_t)nsct * SECTOR;
    got = XbFile_Read(&f->span, off, buf, want);
    if (got == 0 && off < f->span.size) {
        f->stat = STAT_ERROR; /* the disc read itself failed (already logged by xb_disc.c) */
        return 0;
    }
    if (gXbFilePostRead != NULL) {
        gXbFilePostRead(f->ptid, f->flid, off, f->span.size, buf, got);
        if (XbRes_LastError()!=0) { f->stat=STAT_ERROR; return 0; }
    }
    f->posSct += nsct;
    f->stat = STAT_READEND;
    return nsct;
}

int ADXF_Stop(void *h) {
    ((XbFile *)h)->stat = STAT_STOP;
    return 0;
}
int ADXF_GetStat(void *h) { return ((XbFile *)h)->stat; }
int ADXF_Tell(void *h) { return ((XbFile *)h)->posSct; }

/* CRI set-up and servicing: nothing to do without a drive or a second thread. */
void ADXPS2_ExecVint(int mode) { (void)mode; }
void ADXPS2_SetupDvdFs(void *param) { (void)param; }
void ADXPS2_LoadFcacheDvd(void *param) { (void)param; }
void ADXPS2_SetupThrd(void *param, int unk) { (void)param; (void)unk; }
void ADXM_ExecMain(void) {}
void ADXM_WaitVsync(void) { Port_VBlank(); }
void ADXERR_EntryErrFunc(void (*func)(void *obj, char *msg), void *obj) { (void)func; (void)obj; }

/* The drive: a PS2 DVD is always present and idle (0x14 = PS2 DVD, status 10 = paused). */
int sceCdGetDiskTypeSafe(void) { return 0x14; }
int sceCdStatus(void) { return 10; }
