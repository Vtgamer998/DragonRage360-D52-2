/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_resconv.h"
#include "xb_demo_resources.h"
#include "dr_resource.h"
#include "dr_texfile.h"
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define XB_NATIVE_BIG_ENDIAN 1
#else
#define XB_NATIVE_BIG_ENDIAN 0
#endif
static int animation(void *buf, size_t n, int id) {
    (void)id; return dr_anim_prepare(buf,n,XB_NATIVE_BIG_ENDIAN);
}
static int skeleton(void *buf, size_t n, int id) {
    size_t nodes=0;
    (void)id; return dr_skeleton_prepare(buf,n,XB_NATIVE_BIG_ENDIAN,&nodes);
}
int XbRes_RegisterAnimation(int first, int last, int packed) {
    return XbRes_RegisterChecked(first,last,animation,packed);
}
int XbRes_RegisterSkeleton(int first, int last, int packed) {
    return XbRes_RegisterChecked(first,last,skeleton,packed);
}
static int loading_screen(void *buf,size_t n,int id) {
    if(!buf || n<32 || n>0x6800 || id<0x3c1 || id>0x3c3)return -897;
    /* Sprite_DrawList references entries 0..1, 0..2, 0..1 respectively. */
    if(XbRes_Le32(buf)<(id==0x3c2?3u:2u))return -897;
    return dr_texfile_prepare(buf,n,XB_NATIVE_BIG_ENDIAN);
}
int XbRes_RegisterLoadingScreens(void) {
    return XbRes_RegisterChecked(0x3c1,0x3c3,loading_screen,1);
}
static int member_texture(void *p,size_t n,int id){
    if(!p || n<32 || n>0x1000 || (id!=8 && id!=11) || XbRes_Le32(p)!=1)return -897;
    return dr_texfile_prepare(p,n,XB_NATIVE_BIG_ENDIAN);
}
int XbRes_RegisterInitialMembers(void){
    static const XbResCheckedRange ranges[]={{8,8,member_texture,0},{11,11,member_texture,0}};
    return XbRes_RegisterCheckedBatch(ranges,2);
}
