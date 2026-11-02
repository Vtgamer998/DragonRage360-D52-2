/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_text.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void) {
    uint16_t a[]={0xfeff,'A',0},b[]={0xfeff,'B',0},missing[]={'Z',0};
    assert(UiText_TranslateString(a)==a && !UiText_TranslateString(NULL));
    XbTranslation t={a,b};assert(!XbText_SetTranslations(&t,1));b[1]='C';
    assert(UiText_TranslateString(a)[1]=='B' && UiText_TranslateString(missing)==missing);
    XbTranslation duplicates[2]={{a,b},{a,b}};assert(XbText_SetTranslations(duplicates,2)<0);
    assert(UiText_TranslateString(a)[1]=='B');
    assert(!XbText_SetTranslations(NULL,0) && UiText_TranslateString(a)==a);
    XbFontGlyph glyphs[2]={{'H',17,32},{'A',23,44}},out;int height;
    /* Unaligned native metadata must be copied without native loads. */
    unsigned char bytes[sizeof(glyphs)+1];memcpy(bytes+1,glyphs,sizeof(glyphs));
    MenuText_RegisterGlyphs(0x2a08,0x2a00,bytes+1,2,24);assert(!XbText_LastError());
    memset(bytes,0,sizeof(bytes));assert(!XbText_FindGlyph(0x2a00,'A',&out,&height) && out.u==23 && out.v==44 && height==24);
    MenuText_RegisterGlyphs(0,0x2a00,glyphs,2,24);assert(XbText_LastError()<0);
    assert(!XbText_FindGlyph(0x2a00,'H',&out,&height) && out.u==17);
    assert(XbText_FindGlyph(0x2a00,'Z',&out,&height)<0);
    XbText_Close();assert(XbText_FindGlyph(0x2a00,'A',&out,&height)<0);
    puts("PASS: native glyph copies, bounds, unaligned metadata, lookup, UTF16 catalog ownership, fallback and atomic duplicate rejection");return 0;
}
