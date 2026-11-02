/* SPDX-License-Identifier: MIT
 * The game's own data, fetched from the user's disc (counterpart of Port_LoadGameData in port/src/plat_mem.c of
 * the PC port).
 *
 * A program made by tools/xb_strip_data.py has the data tables of the game's two programs set to zero, and a list
 * next to it (<program>.dat, format BT3X below) of where each run of bytes comes from: the user's SLUS_216.78 (as
 * a flat image from 0x100000) or BIN/DBZP.BIN. XbGameData_Load copies them in, BEFORE anything else of the game
 * runs.
 *
 * What differs from the PC port: the PS2's data is little-endian and the Xbox 360 is big-endian, so every run says
 * how wide its units are (1, 2, 4 or 8 bytes) and is byte-swapped by that width on the way in. Without the width
 * the loader cannot tell a table of words from a table of bytes. Anything the data holds that is not a plain unit
 * (a struct with mixed fields is still units of 1, 2, 4, 8: the tool splits it) is the tool's business, not this
 * file's.
 *
 * List format (all numbers little-endian, whatever the console):
 *   "BT3X", u32 count, u64 FNV-1a hash of the SOURCE bytes of all runs in order (before swapping),
 *   then count records of 5 x u32: address in the program, source (0 = SLUS_216.78, 1 = DBZP.BIN), offset in the
 *   source, length, unit width.
 */
#ifndef XB_GAMEDATA_H
#define XB_GAMEDATA_H
#include <stddef.h>
#include <stdint.h>

enum {
    XBGD_OK = 0,
    XBGD_BAD_LIST = 1,     /* not a BT3X list, or cut short */
    XBGD_NO_ELF = 2,       /* SLUS_216.78 missing or not an ELF */
    XBGD_NO_DBZP = 3,      /* BIN/DBZP.BIN missing */
    XBGD_BAD_RECORD = 4,   /* a run points outside its source, or has a bad width */
    XBGD_HASH = 5,         /* checksum mismatch; no destination writes */
    XBGD_NO_LIST = 6,      /* no list file */
    XBGD_NO_TARGETS = 7,  /* native data regions were not registered */
    XBGD_BAD_TARGET = 8,  /* destination outside the explicit writable regions */
    XBGD_OVERLAP = 9,     /* overlapping destination records */
    XBGD_MEMORY = 10,     /* allocation failed */
    XBGD_STATE = 11       /* loader/registration called recursively */
};

/* How the loader gets the two programs. read gives a malloc'ed copy of the named file (free() it): "SLUS_216.78"
   or "DBZP.BIN". Returns 0 on success. */
typedef struct XbDataFiles {
    int (*read)(void *ctx, const char *name, uint8_t **buf, size_t *size);
    void *ctx;
} XbDataFiles;

/* Register the native DATA regions owned by this build, before loading.
 * Address is a 32-bit link address, bytes is its actual writable storage.
 * Descriptors are copied; storage is borrowed until cleared. Neither logical
 * nor physical ranges may overlap. Invalid registration preserves old maps.
 * The API is single-thread only; re-entry from a file callback is rejected. */
typedef struct XbDataTarget {uint32_t address;void *bytes;size_t size;} XbDataTarget;
int XbGameData_SetTargets(const XbDataTarget *,size_t);
int XbGameData_ClearTargets(void);

/* Copies the runs into registered DATA storage at address + bias (bias: where the program was really loaded minus where it was
   linked; 0 normally). Validates all sources, destinations and checksum BEFORE
   any write. Exact manifest length is required; no overlapping records or ELF
   holes. allowModified: a hash mismatch (a translation or a mod of the USA release) is accepted. */
int XbGameData_Load(const XbDataFiles *files, const uint8_t *list, size_t listSize, long bias, int allowModified);

/* On the console: the list from `listPath` (an xsdk path such as "game:/dragonrage.dat") and the programs from the
   mounted disc (xb_disc.h: XbDisc_Mount first). Returns XBGD_*; XBGD_NO_LIST means "no list: the data is linked in"
   (a developer build), the caller must treat absence as a boot blocker for a skeleton-data build. */
int XbGameData_LoadFromDisc(const char *listPath, long bias, int allowModified);

/* Set to 1 in the program's file by the strip tool: this program does not work without its list. 2 (not 0) so that
   it lives in the file, not in zero-filled memory. */
extern volatile uint32_t gPortDataStripped;
#endif
