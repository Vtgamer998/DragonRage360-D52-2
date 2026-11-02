/* SPDX-License-Identifier: MIT */
#ifndef XB_OVERLAY_H
#define XB_OVERLAY_H
#include <stddef.h>
#include <stdint.h>
#include "xb_gamedata.h"
enum { XBOV_OK=0,XBOV_CATALOG=-610,XBOV_STATE=-611,XBOV_RANGE=-612,
       XBOV_RELOC=-613,XBOV_SOURCE=-614,XBOV_MEMORY=-615 };
enum { XBOV_RESET=1,XBOV_PRESERVE=2 };
typedef struct {uint8_t *begin,*end;size_t expected;unsigned policy;} XbOverlayRegion;
typedef struct {const void *slot;uintptr_t expected;unsigned width;} XbOverlayReloc;
typedef struct {
    uint32_t version,units;const XbOverlayRegion *regions;size_t count;
    const XbOverlayReloc *relocs;size_t reloc_count;
    uint8_t *data_begin,*data_end;size_t source_size;uint64_t source_fnv;
    void (*entry)(int);
} XbOverlayCatalog;
/* Trusted build catalogue only. Destinations are resident native DATA, never
 * file addresses. Native relocations have already been applied by the linker.
 * Snapshot captures initial native globals before any menu has run. */
int XbOverlay_Prepare(const XbOverlayCatalog *,const XbDataFiles *);
int XbOverlay_Restore(int index);
int XbOverlay_Close(void);
int XbOverlay_Ready(void);
/* Console binding to generated catalogue and current mounted ISO. */
int XbOverlay_PrepareBuild(void);
int XbOverlay_Load(int index);
#endif
