/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DR_TARGETS_H
#define DR_TARGETS_H
#include "xsdk/texture.h"
/* Internal saved surfaces for the native core. One thread; one surface in eDRAM.
 * Legacy surfaces preserve color/D24 depth. GS surfaces additionally save raw
 * alpha and restore stencil, using MRT and tiled passes. GPU pixels unverified.
 * Every bind/resolve invalidates drawing state; caller must rebind all of it.
 * No present or external eDRAM commands between bind and store. */
typedef struct {uint32_t stores,loads,fence_waits,stencil_restore_passes,zero_stencil_restores,command_splits;
    /* Lifetime CPU-side stage timings; bind includes any nested store it performs. */
    uint64_t store_us,bind_us;} DrTargetProfile;
void dr_targets_profile(DrTargetProfile *);
uint32_t dr_targets_epoch(void);
int dr_targets_set_wait_timeout(uint32_t milliseconds);
int dr_targets_open(void);
int dr_targets_ensure(unsigned id,uint32_t width,uint32_t height);
/* GS surfaces use color + raw alpha + D24S8 and SDK tiled passes. */
int dr_targets_ensure_gs(unsigned id,uint32_t width,uint32_t height);
int dr_targets_is_gs(unsigned id);
int dr_targets_ready(unsigned id);
/* 0=color, 1=raw GS alpha, 2=depth. Never free these borrowed textures. */
const XsdkTexture *dr_targets_texture(unsigned id,unsigned attachment);
/* End the current GS pass, capture raw alpha, and resume the same target.
 * kind 0 is DATE; kind 1 is the independent post-effect alpha snapshot. */
int dr_targets_snapshot(unsigned kind);
const XsdkTexture *dr_targets_snapshot_texture(unsigned kind,unsigned id,uint32_t width,uint32_t height);
int dr_targets_bind(unsigned id);
int dr_targets_store(void);
/* May resolve/rebind before a whole draw; caller rebinds all draw state. */
int dr_targets_ensure_commands(uint32_t reserve);
/* Conservative: called whenever a draw can make stencil nonzero. */
void dr_targets_stencil_written(void);
int dr_targets_size(unsigned id,uint32_t *width,uint32_t *height);
int dr_targets_drop(unsigned id);
int dr_targets_close(void);
#endif
