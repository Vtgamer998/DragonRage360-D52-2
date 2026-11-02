/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "xb_resconv.h"
#include <assert.h>
#include <stdio.h>
typedef int s32;
#define PROGRESS_FLAG_LOADING 0x4000
static struct {int flags;} progress,*gProgress=&progress;
static int mode,watch,begin,end,jobs,draw,flush,frame_begin,frame_end;
static int XbFreshBoot_Frame(int phase){
 if(phase==1)frame_begin++;else{assert(phase==2);frame_end++;}
 if((mode==4 && phase==1) || (mode==5 && phase==2)){XbRes_SetReadError(-30-mode);return -30-mode;}return 0;
}
static void PadWatch_SetEnabled(int x){watch=x;}
static void Dma_ResetBuffers(void){}
static void Load_InitScreen(void){if(mode==1)XbRes_SetReadError(-31);}
static void Fade_ResetAll(void){}
static void Fade_Start(int a,int b,float t){(void)a;(void)b;(void)t;}
static int Fade_IsDone(int n){(void)n;return 1;}
static void Snd_Update(void){}
static void Gfx_BeginFrame(void){begin++;}
static void Pad_Update(void){}
static int Job_Run(void){jobs++;if(mode==2)XbRes_SetReadError(-32);return 0;}
static void Load_UpdateScreen(void){}
static void Load_DrawScreen(void){draw++;}
static void Gfx_EndFrame(int n){assert(n==2);end++;}
static void Dma_Flush(void){flush++;}
static void Load_ReadInput(void){assert(!"unexpected extra loading frame");}
/* ORIGINAL_FUNCTIONS */
int main(void){
 for(mode=0;mode<6;mode++){
  progress.flags=0;watch=1;begin=end=jobs=draw=flush=frame_begin=frame_end=0;assert(!XbRes_BeginCheckedReads());if(mode==3)XbRes_SetReadError(-33);Load_RunBlocking();
  assert(watch==1 && !progress.flags && begin==end);
  if(mode==0)assert(jobs==1 && draw==1 && flush==1 && !XbRes_LastError());
  if(mode==1 || mode==3)assert(jobs==0 && begin==0);
  if(mode==2)assert(jobs==1 && draw==0 && begin==1 && flush==1 && XbRes_LastError()==-32);
  if(mode==0 || mode==2 || mode==5)assert(frame_begin==1 && frame_end==1);
  if(mode==4)assert(begin==0 && frame_begin==1 && frame_end==0 && XbRes_LastError()==-34);
  if(mode==5)assert(begin==1 && draw==1 && flush==1 && XbRes_LastError()==-35);
  XbRes_EndCheckedReads();
 }
 puts("PASS original Load_RunBlocking: initial/screen/job failures exit; frame and pad/loading flag restored");return 0;
}
