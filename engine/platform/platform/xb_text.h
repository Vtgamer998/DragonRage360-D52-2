/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_TEXT_H
#define XB_TEXT_H
#include <stdint.h>
#include <stddef.h>
typedef struct {uint16_t code,u,v;} XbFontGlyph;
typedef struct {const uint16_t *original,*translated;} XbTranslation;
/* Explicit native UTF-16 catalog, copied atomically; original ISO text is
 * returned unchanged if no exact translation is installed. Single owner. */
int XbText_SetTranslations(const XbTranslation *,size_t);
const uint16_t *UiText_TranslateString(const uint16_t *);
/* Native font metadata copied from the game's converted FontGlyph array.
 * Does not replace original Tex_Upload/GS glyph drawing with a PC font. */
void MenuText_RegisterGlyphs(int,int,const void *,int,int);
int XbText_FindGlyph(int cbp,uint16_t code,XbFontGlyph *,int *line_height);
int XbText_LastError(void);
void XbText_Close(void);
#endif
