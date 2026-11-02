/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_resconv.h"
#include <assert.h>
#include <stdio.h>
typedef int s32;
static int scoped_driver,driver_calls,normal_calls;
static int XbFreshBoot_DrivesLoading(void){return scoped_driver;}
static int mode,objects,subsystems,loops,terms,initializers,aborts,cleanup;
static int XbBattleBoot_ObjectStateClear(void){return mode!=2;}
static int XbBtlLoad_AbortInitial(void){aborts++;return mode!=3;}
static void BtlObj_Term(void){cleanup++;}
static void BtlScript_Free(void){cleanup++;}
static void Gsc_Exit(void){cleanup++;}
static int gBtlScriptCommands[1];
static struct {int running;} work;
/* A named type lets the ORIGINAL Battle_Main read running. */
typedef struct {int running;} Work;
#undef Battle_GetWork
static Work *get_work(void){return (Work *)&work;}
#define Battle_GetWork get_work
static void BtlObj_Init(int n){assert(n==1);initializers++;}
static void Gsc_InitDefault(int *commands){assert(commands==gBtlScriptCommands);}
static void BtlScript_Init(void){}
static void BtlLoad_PushInitialJob(void){}
static void Load_RunBlocking(void){normal_calls++;if(mode==1 || mode==3)XbRes_SetReadError(-40);}
static void XbFreshBoot_PumpLoading(void){driver_calls++;if(mode==1 || mode==3)XbRes_SetReadError(-40);}
static int BattleSide_GetModelSlot(int side){return side;}
static int BtlObj_CreateChara(int slot){objects++;return slot;}
static void BattleSide_SetObjId(int side,int id){assert(side==id);}
#define SUBSYSTEM(name) static void name(void){subsystems++;}
SUBSYSTEM(StgFx_Init) SUBSYSTEM(StgModel_InitStage) SUBSYSTEM(BtlAiMgr_Init) SUBSYSTEM(BtlCam_Init) SUBSYSTEM(StgNav_Init) SUBSYSTEM(BtlGame_Init) SUBSYSTEM(Ot_Init) SUBSYSTEM(Battle_Restart)
static void BtlChar_AllocAll(int n){assert(n==2);subsystems++;}
static void BtlScene_Init(int n){assert(n==0);subsystems++;}
static void Battle_Loop(void){loops++;}
static void Battle_Term(void){terms++;}
void XbFreshBoot_TraceInit(const char *name){assert(name);}
/* ORIGINAL_FUNCTIONS */
int main(void){
 for(scoped_driver=0;scoped_driver<2;scoped_driver++) for(mode=0;mode<5;mode++){
  driver_calls=normal_calls=0;
  objects=subsystems=loops=terms=initializers=aborts=cleanup=0;work.running=0;assert(!XbRes_BeginCheckedReads());if(mode==4)XbRes_SetReadError(-41);int result=Battle_Main();
  if(mode){assert(!result && !objects && !subsystems && !loops && !terms && !work.running);assert(XbRes_LastError()==(mode==2?-902:mode==4?-41:-40));assert(initializers==(mode==1 || mode==3));assert(aborts==(mode==1 || mode==3));assert(cleanup==(mode==1?3:0));}
  else assert(result==1 && objects==2 && subsystems==10 && loops==1 && terms==1 && !work.running);
  assert(driver_calls==(scoped_driver && mode!=2 && mode!=4));
  assert(normal_calls==(!scoped_driver && mode!=2 && mode!=4));
  XbRes_EndCheckedReads();
 }
 puts("PASS original Battle_Load/Init/Main CONTROL FLOW: fresh no-script read failure unwinds completed initializers; occupied/pre-failed sessions never initialize; unsupported script failure stays fail-stop; no fighters/Loop/Term on error");return 0;
}
