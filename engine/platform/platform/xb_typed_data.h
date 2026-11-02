/* SPDX-License-Identifier: MIT */
#ifndef XB_TYPED_DATA_H
#define XB_TYPED_DATA_H
#include "xb_gamedata.h"
enum {XBTD_VALIDATE_ONLY=1, XBTD_ALLOW_PARTIAL=2};
enum {XBTD_COPY=0,XBTD_POINTER=1,XBTD_ZERO=2};
enum {XBTD_BAD_CATALOG=720,XBTD_INCOMPLETE=721,XBTD_POINTER_MISMATCH=722,
      XBTD_SOURCE=723,XBTD_HASH=724,XBTD_MEMORY=725,XBTD_STATE=726};
/* Catalogs are compiled trusted metadata, never read as native pointers from
 * an external file. Region storage is borrowed and must belong to the caller. */
typedef struct XbTypedRegion {
    void *bytes;size_t size;uint32_t source_file,source_offset;
} XbTypedRegion;
typedef struct XbTypedOp {
    uint32_t region,offset,length,width,kind,expected_mips;
    uintptr_t native_pointer;
} XbTypedOp;
typedef struct XbTypedCatalog {
    uint32_t version,complete;
    const XbTypedRegion *regions;size_t region_count;
    const XbTypedOp *ops;size_t op_count;
    uint64_t source_hash;
} XbTypedCatalog;
/* All checks precede every destination write. validate-only never commits.
 * Partial preview explicitly requires ALLOW_PARTIAL and NEVER clears
 * gPortDataStripped or authorizes Battle_Main. Calls are main-thread only. */
/* Boot code may clear the legacy readiness marker only after all typed data
 * AND battle resources/platform initialization have succeeded. Partial loads
 * never do this. Battle_Main checks this guard before accessing its work. */
int XbTypedData_RequireBattleData(void);
int XbTypedData_Load(const XbDataFiles *,const XbTypedCatalog *,unsigned flags);
int XbTypedData_LoadFromDisc(const XbTypedCatalog *,unsigned flags);
extern const XbTypedCatalog gXbTypedMainCatalog;
#endif
