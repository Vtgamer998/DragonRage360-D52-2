/* SPDX-License-Identifier: MIT
 * The mounted game disc image, shared by the whole platform layer. The launcher's own ISO reader (src/main.c)
 * opens the image for single diagnostics; the game needs it open for its whole run, so this module keeps one
 * mount. Reads are NOT thread-safe: whoever streams audio from another thread must serialize with its own lock
 * (TODO: a lock here once the sound thread exists). */
#ifndef XB_DISC_H
#define XB_DISC_H
#include "dr_disc.h"

/* path as the launcher writes them: "game:/isos/x.iso", "usb0:/isos/x.iso". 0 = mounted, else a DR_* error or
   a negative NTSTATUS-derived value. Mounting twice unmounts the first image. */
int XbDisc_Mount(const char *path);
void XbDisc_Unmount(void);
const dr_disc *XbDisc_Get(void); /* NULL when nothing is mounted */
uint64_t XbDisc_Generation(void); /* changes on unmount/remount; invalidates open ISO tokens */

/* A game file by the PC port's relative name: "pzs3us1/00042.bin" (partition 1, index 42) or "disc/DATA/NAME.EXT".
   0 = found. */
int XbDisc_FindFile(const char *rel, dr_span *out);

/* Reads n bytes at offset `off` of the file. The range must lie inside the file. 0 = ok. */
int XbDisc_ReadSpan(const dr_span *span, uint64_t off, void *buf, size_t n);
/* A whole small file by xsdk path ("game:/x.dat"), malloc'ed; the caller free()s it. 0 = ok. */
int XbDisc_ReadHostFile(const char *path, uint8_t **buf, size_t *size);
#endif
