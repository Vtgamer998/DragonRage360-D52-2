/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Renderer-only native profile. These are application-owned render settings,
 * not substitutes for the absent PS2 address space, battle/rollback or audio.
 * Optional desktop font/texture-pack extensions are excluded in gs_draw.c.
 */
#include <string.h>
int gPortMenuMode,gPortResim;
int gPortMusicPercent=100,gPortSePercent=100,gPortVoicePercent=100,gPortStagePercent=100;
int Port_Setting(const char *name,int fallback)
{
    if(!strcmp(name,"scale4"))return 4;
    if(!strcmp(name,"texture_pack") || !strcmp(name,"astc"))return 0;
    return fallback;
}
/* This profile preserves the original 512x448 composition; no game projection widening. */
int Port_IsWide(void) {return 0;}
int Port_AspectMilli(void) {return 1333;}
int Port_PageAspectMilli(void) {return 1333;}
