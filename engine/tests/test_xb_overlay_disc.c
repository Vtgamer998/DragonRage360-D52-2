/* SPDX-License-Identifier: MIT; reuses only the host NT transport fixture. */
#define main unused_sony_file_tests
#include "test_xb_iso_file.c"
#undef main
#include "xb_overlay.h"
static uint8_t native_data[64];
static unsigned entry_calls;
static void native_entry(int n){entry_calls+=(unsigned)n;}
static const XbOverlayRegion native_region={native_data,native_data+64,64,XBOV_RESET};
static const XbOverlayReloc native_reloc={native_data+8,(uintptr_t)native_entry,sizeof(void *)};
const XbOverlayCatalog gXbOverlayCatalog={1,1,&native_region,1,&native_reloc,1,native_data,native_data+64,563996,0x91a321108fc31289ull,native_entry};
int main(int argc,char **argv){
    assert(argc==2);image_path=argv[1];assert(XbOverlay_PrepareBuild()==XBOV_CATALOG);
    assert(!XbDisc_Mount("game:/test.iso"));
    memset(native_data,0x11,64);void (*fn)(int)=native_entry;memcpy(native_data+8,&fn,sizeof(fn));
    uint8_t initial[64];memcpy(initial,native_data,64);
    assert(!XbOverlay_PrepareBuild());memset(native_data,0xaa,64);
    assert(!XbOverlay_Load(0) && !memcmp(initial,native_data,64));
    memcpy(&fn,native_data+8,sizeof(fn));fn(1);assert(entry_calls==1);
    assert(XbOverlay_Load(1)==XBOV_CATALOG);
    XbDisc_Unmount();memset(native_data,0xbb,64);assert(XbOverlay_Load(0)==XBOV_STATE && native_data[0]==0xbb);
    assert(!XbDisc_Mount("game:/test.iso"));assert(XbOverlay_Load(0)==XBOV_STATE);
    assert(!XbOverlay_Close());memcpy(native_data,initial,64);assert(!XbOverlay_PrepareBuild());
    fail_read=1; /* Restore consumes native snapshots, not another disc/code read. */
    memset(native_data,0xcc,64);assert(!XbOverlay_Load(0) && !memcmp(initial,native_data,64));fail_read=0;
    assert(!XbOverlay_Close());XbDisc_Unmount();assert(!image);
    puts("PASS: actual console overlay binding and actual ISO mount/parser, only NT transport mocked; real DBZP identity checked; native pointers restored; remount/unmount refused; no MIPS execution");
}
