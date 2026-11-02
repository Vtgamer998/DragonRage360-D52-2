/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <stdio.h>
#include "xb_disc.h"
#include "xb_resconv.h"
#include "xb_platform.h"
#include "xb_file.h"
#include "xb_demo_resources.h"
#include "xb_common_items.h"
#include "xb_common_pack.h"
#include "xb_model_pack.h"
#include "xb_stage_pack.h"
#include "xb_match_ui_pack.h"
#include "xb_effect_pack.h"
#include "xb_sound_bank.h"
#include "xb_motion_pack.h"
#include "xb_iso_file.h"
#include "xsdk/log.h"
void XbPlatform_Install(void) {
    XbRes_Install();
    xsdk_log_write("platform: stage0 full pack registration=%d",XbStagePack_RegisterInitial());
    xsdk_log_write("platform: match UI file6 registration=%d",XbMatchUiPack_RegisterInitial());
    xsdk_log_write("platform: initial effect packs registration=%d",XbEffectPack_RegisterInitial());
    xsdk_log_write("platform: initial model packs registration=%d",XbModelPack_RegisterInitial());
    xsdk_log_write("platform: common packs 2,3,5 registration=%d",XbCommonPack_Register());
    xsdk_log_write("platform: common bank 0x14b registration=%d",XbSoundBank_RegisterCommon());
    xsdk_log_write("platform: common file4 item tables registration=%d",XbRes_RegisterCommonItems());
    xsdk_log_write("platform: original loading sheet converters registration=%d",XbRes_RegisterLoadingScreens());
    xsdk_log_write("platform: stage0/characters0,1 initial sound bank registration=%d",XbSoundBank_RegisterInitial());
    xsdk_log_write("platform: character0,1 embedded motion pack registration=%d",XbMotionPack_RegisterInitial());
    xsdk_log_write("platform: character0,1 initial member texture registration=%d",XbRes_RegisterInitialMembers());
    xsdk_log_write("platform: explicit resource map installed ranges=%d",XbRes_LoadMap("game:/resource-converters.txt"));
    xsdk_log_write("platform: resource post-read installed; no format ids guessed; combat not started");
}
int XbPlatform_ProbeFiles(const char *iso) {
    uint8_t sector[2048];int i,rc=XbDisc_Mount(iso);
    if(rc)return rc;
    rc=XbIsoFiles_Probe();
    xsdk_log_write("platform: raw ISO open/read/seek/search USA ELF probe rc=%d; no executable loaded",rc);
    for(i=0;i<3 && !rc;i++) {
        char name[24];void *h;
        (void)snprintf(name,sizeof(name),"PZS3US%d.AFS",i);
        rc=ADXF_LoadPartitionNw(i,name,0,0);
        if(rc)break;
        h=ADXF_OpenAfs(i,0);
        if(!h){rc=-1;break;}
        if(ADXF_ReadNw(h,1,sector)!=1)rc=-1;
        (void)ADXF_Close(h);
        XbRes_Forget(sector,sizeof(sector));
    }
    XbRes_Report();XbIsoFiles_CloseAll();XbDisc_Unmount();
    xsdk_log_write("platform: ADXF first-file sector probes rc=%d; not a full battle resource inventory",rc);
    return rc;
}
