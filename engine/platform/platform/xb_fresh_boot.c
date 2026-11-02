/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "port_compat.h"
#include "xb_fresh_boot.h"
#include "xb_battle_runtime.h"
#include "xb_battle_boot.h"
#include "xb_initial_load.h"
#include "xb_resconv.h"
#include "xb_mem.h"
#include "xb_heap.h"
#include "xb_adxt.h"
#include "xb_file.h"
#include "xb_pad.h"
#include "sys/common.h"
#include "sys/heap.h"
#include "sys/job.h"
#include "sys/file.h"
#include "sys/loading.h"
#include "sys/save.h"
#include "sys/snd.h"
#include "battle/battle_work.h"
#include "battle/battle_setup.h"
#include "battle/view_a.h"
#include "battle/btl_obj.h"
#include "battle/btl_demo_cam.h"
#include "battle/btl_seq.h"
#include "battle/btl_char_mgr.h"
#include "sys/gfxm_e.h"
#include "battle/hud_a.h"
#include <string.h>
#include <time.h>
#include "dr_vu1.h"
#include "dr_xenos.h"
#include "dr_targets.h"
#include "dr_gs_profile.h"
#include "dr_capture_demo.h"
#include <xsdk/log.h>
extern void BtlObj_Init(s32),BtlObj_Term(void);
extern void Gsc_InitDefault(s32 *),Gsc_Exit(void);
extern void BtlScript_Init(void),BtlScript_Free(void),Progress_Init(void);
extern s32 gBtlScriptCommands[];
extern int Heap_GetFreeSize(void);
static int drive_loading,phase4_complete,inject_partial_failure,session_owned;
static int (*presentation)(void);
static int original_type=-1;
static int trace_quiet,cancel_test;
static uint64_t frame_times[3600];
static int action_check,action_fast,charge_check;
static unsigned direct_effect_packets;
void XbFreshBoot_CountDirectEffect(void){direct_effect_packets++;}
static unsigned action_tick;
static int (*saved_pad_poll)(int,XbPadState *);
static int scripted_pad_poll(int index,XbPadState *out){
 if(index)return saved_pad_poll?saved_pad_poll(index,out):-1;
 memset(out,0,sizeof(*out));out->connected=1;
 if(charge_check){if((action_tick>=4 && action_tick<240) || (action_tick>=272 && action_tick<352))out->lt=255;return 0;}
 if(action_tick>=4 && action_tick<10)out->buttons=0x4000;
 if(action_tick>=16 && action_tick<32)out->lt=255;
 if(action_tick>=36 && action_tick<42)out->buttons=0x2000;
 if(action_tick>=48 && action_tick<54)out->buttons=0x8000;
 return 0;
}
static int cancel_requested(unsigned fight_frames){
 if(action_check && fight_frames>=(charge_check?384u:64u)){xsdk_log_write("D46 action check completed requested FIGHT frames");return 1;}
 if(!action_check && cancel_test && fight_frames>=4){xsdk_log_write("D46 automated between-frame cancel after four FIGHT frames");return 1;}
 XbPadState pad;memset(&pad,0,sizeof(pad));
 return gXbPadPoll && !gXbPadPoll(0,&pad) && pad.connected && (pad.buttons&0x30u)==0x30u;
}
static uint64_t probe_us(void);
static uint64_t update_started,draw_started,last_update_us,last_draw_us;
/* D49 frame budget: lifetime stage timings snapshotted before each battle
 * frame, diffed after it; reveals the per-frame time no counter explains. */
typedef struct {uint64_t replay,preflight,emit,select,store,bind,movie,present;} DrStageSnapshot;
static DrStageSnapshot stage_prev;
void dr_gs_backend_frame_times(uint64_t *movie_us,uint64_t *present_us);
static void stage_capture(DrStageSnapshot *s){
 DrCaptureProfile cp;DrXenosProfile xp;DrTargetProfile tp;uint64_t mv,pr;
 dr_capture_original_profile(&cp);dr_xenos_profile(&xp);dr_targets_profile(&tp);dr_gs_backend_frame_times(&mv,&pr);
 s->replay=cp.replay_us;s->preflight=xp.preflight_us;s->emit=xp.emit_us;s->select=xp.select_us;
 s->store=tp.store_us;s->bind=tp.bind_us;s->movie=mv;s->present=pr;
}
void XbFreshBoot_TraceInit(const char *name){
 if(trace_quiet){
  if(!strcmp(name,"Battle_Update"))update_started=probe_us();
  else if(!strcmp(name,"Battle_Update returned"))last_update_us=probe_us()-update_started;
  else if(!strcmp(name,"Battle_Draw"))draw_started=probe_us();
  else if(!strcmp(name,"Battle_Draw returned"))last_draw_us=probe_us()-draw_started;
 }else xsdk_log_write("D46 entering %s",name);
}
static BtlSeqFunc saved_ready_update;
static s32 ready_fast_update(void *ctx){s32 next=BTL_SEQ_READY;for(unsigned i=0;i<128 && next==BTL_SEQ_READY;i++)next=saved_ready_update(ctx);return next;}
static BtlSeqState *intro_poll_table;
static BtlSeqPollFunc saved_intro_poll,saved_talk_poll;
static int talk_wrapped,talk_skip_sent;
static int intro_skip_sent;
static int skip_stage_poll(void){
 if(saved_intro_poll && saved_intro_poll())return 1;
 if(!intro_skip_sent){intro_skip_sent=1;xsdk_log_write("D46 fighter probe: one skip through original intro poll");return 1;}
 return 0;
}
static int skip_talk_poll(void){
 if(saved_talk_poll && saved_talk_poll())return 1;
 if(!talk_skip_sent){talk_skip_sent=1;xsdk_log_write("D46 fight probe: one skip through original talk poll");return 1;}
 return 0;
}
static void restore_intro_poll(void){
 if(intro_poll_table){if(saved_ready_update){intro_poll_table[2].update=saved_ready_update;saved_ready_update=NULL;}if(talk_wrapped){intro_poll_table[1].poll=saved_talk_poll;talk_wrapped=0;saved_talk_poll=NULL;}intro_poll_table[0].poll=saved_intro_poll;intro_poll_table=NULL;saved_intro_poll=NULL;}
}
static uint64_t probe_us(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u;}
extern int Battle_Init(void),Battle_Term(void),Battle_StepFrame(void);
extern void Font_Init(int),FontIcon_Init(void),Font_Term(void);
static int original_phase4,last_original_state;
extern unsigned gXbGfxBeginFrames,gXbGfxEndFrames;
static int (*original_frame)(int);
extern AfsPartition gAfsPartitionTbl[];
extern AdxPlayer gAdxPlayerTbl[];
extern AdxFcacheParam gAdxFcacheParam;
extern void Pad_Init(void),PadWatch_Init(void),PadWatch_Term(void),Fade_Init(void),Fade_Term(void),Vu0_Init(void);
extern void *sceGsSyncVCallback(int (*)(int));
static int presentation_quiescent;
int XbFreshBoot_SetOriginalLoading(int type,int (*frame)(int)){
 if(session_owned || type < -1 || type>2)return -902;
 presentation_quiescent=0;original_type=type;original_frame=frame;original_phase4=0;last_original_state=-1;return 0;
}
int XbFreshBoot_ForcedLoadingType(void){return session_owned?original_type:-1;}
int XbFreshBoot_Frame(int phase){
 if(!session_owned || original_type<0 || !original_frame)return 0;
 if(phase==1 && gJobQueue && Job_GetCount()){
  BtlJob *j=(BtlJob *)Queue_Peek(gJobQueue);
  if(j && j->state!=last_original_state){last_original_state=j->state;xsdk_log_write("D46 original job type=%d state=%d pending=%d",original_type,j->state,File_GetPendingCount());}
  if(j && j->state==4)original_phase4=1;
 }
 if(phase==1)presentation_quiescent=0;
 int e=original_frame(phase);if(phase==2 && e!=-950)presentation_quiescent=1;
 if(e && drive_loading)XbRes_SetReadError(e);
 return e;
}
static void close_file_boot(void){
 sceGsSyncVCallback(NULL);
 for(unsigned i=0;i<ADX_PLAYER_COUNT;i++){
  if(gAdxPlayerTbl[i].adxt)XbAdx_Destroy(gAdxPlayerTbl[i].adxt);
  gAdxPlayerTbl[i].adxt=NULL;Heap_Free(gAdxPlayerTbl[i].work);gAdxPlayerTbl[i].work=NULL;
 }
 for(unsigned i=0;i<FILE_PT_COUNT;i++){Heap_Free(gAfsPartitionTbl[i].info);gAfsPartitionTbl[i].info=NULL;}
 Heap_Free(gAdxFcacheParam.buf);gAdxFcacheParam.buf=NULL;
}
int XbFreshBoot_SetPresentation(int (*frame)(void)){if(session_owned)return -902;presentation=frame;return 0;}
static int present_loading(void){
 if(!presentation)return 0;
 Load_UpdateScreen();
 int e=presentation();if(e)XbRes_SetReadError(e);return e;
}
int XbFreshBoot_DrivesLoading(void){return drive_loading && session_owned && original_type<0;}
void XbFreshBoot_PumpLoading(void){
 int rc=0,previous=-1,phase4=0,complete=0;unsigned polls;
 if(!session_owned || !XbBattleBoot_OriginalObjectStateActive()){XbRes_SetReadError(-902);return;}
 Load_InitScreen();
 if(XbRes_LastError())return;
 xsdk_log_write("D46 fresh: original Load_InitScreen type=%u prepared; CPU sprites prepared",(unsigned)gLoadScreen.type);
 for(polls=0;polls<65536 && Job_GetCount() && !XbRes_LastError();polls++){
  BtlJob *job=(BtlJob *)Queue_Peek(gJobQueue);
  if(!job){rc=-902;XbRes_SetReadError(rc);return;}
  if(job->state!=previous){previous=job->state;xsdk_log_write("D46 fresh: original initial job state=%d pending=%d free_heap=%d",previous,File_GetPendingCount(),Heap_GetFreeSize());}
  if(job->state==4)phase4=1;
  if((polls&3u)==0 && present_loading())break;
  Snd_Update();Job_Run();
  if(inject_partial_failure && previous==4 && !XbRes_LastError()){
   inject_partial_failure=0;
   xsdk_log_write("D46 recovery: injecting unregistered ID after original phase4 queued %d files",File_GetPendingCount());
   (void)XbRes_PreflightRead(0x7fffffff);
  }
 }
 if(!XbRes_LastError() && presentation && !Job_GetCount())for(unsigned frame=0;frame<12;frame++)if(present_loading())break;
 rc=XbRes_LastError();
 if(!rc && (Job_GetCount() || File_GetPendingCount() || BtlJob_GetFreeCount()!=BTL_JOB_COUNT || !phase4))rc=-938;
 complete=!rc;
 xsdk_log_write("%s: D46 fresh original initial job result=%d phase4=%d complete=%d polls=%u; queue complete; no Battle_Init/Update/Main",rc?"FAIL":"PASS",rc,phase4,complete,polls);

 if(rc)XbRes_SetReadError(rc);
 phase4_complete=complete;
}
/* Separate from Battle_Load: this prefix probe deliberately stops before
 * fighter creation and closes all owners. Never invoke Battle_Load on top. */
static int probe(int full_load) {
 trace_quiet=0;
 int rc,baseline=0,scope=0,audio=0,objects=0,scripts=0,queue=0,file_boot=0,watch=0,fonts=0;
 int complete=0,frame_failure=0;phase4_complete=0;
 unsigned gfx_begin=gXbGfxBeginFrames,gfx_end=gXbGfxEndFrames;
 if(File_IsBusy() || File_GetPendingCount() || XbRes_CheckedReadsActive())return -909;
 rc=XbBattleRuntime_EnterOriginal();if(rc)return rc;session_owned=1;
 if(XbMem_InitHeap()){rc=-908;goto done;}
 Heap_Init();if(!XbHeap_Ready()){rc=-908;goto done;}
 baseline=Heap_GetFreeSize();
 /* Initializers below are original and expect allocation to succeed. Refuse
  * an already depleted arena before entering them; allocation checks remain
  * in the strict initial-job path itself. */
 if(baseline<0x1800000){rc=-908;goto done;}
 rc=XbRes_BeginCheckedReads();if(rc)goto done;scope=1;File_InitRequests();
 if(original_type<0){rc=ADXF_LoadPartitionNw(1,"PZS3US1.AFS",NULL,NULL);if(rc)goto done;File_InitRequests();}
 xsdk_log_write("D46 fresh: Snd_Init begin");Snd_Init();audio=1;
 if((rc=XbRes_LastError()))goto done;
 if(original_type>=0){
  file_boot=1;xsdk_log_write("D46 original File_Init/ADX begin");File_Init();
  if((rc=XbRes_LastError()))goto done;
  for(unsigned i=0;i<ADX_PLAYER_COUNT;i++)if(!gAdxPlayerTbl[i].adxt){rc=-951;goto done;}
  xsdk_log_write("PASS: D46 original File_Init: partitions0,1 and six ADXT players initialized");
 }
 xsdk_log_write("D46 fresh: Common_Init/LoadBoot/Reload begin");
 Common_Init();if((rc=XbRes_LastError()))goto done;
 Common_LoadBoot();if((rc=XbRes_LastError()))goto done;
 Common_Reload();if((rc=XbRes_LastError()))goto done;
 xsdk_log_write("D46 fresh: Progress_Init/Save_Init/Job_Init begin");
 Progress_Init();Save_Init();Job_Init();queue=1;
 Battle_ClearWork();BattleSetup_SetRule(0,0,0,0,0,0,0);
 BattleSetup_SetSide(0,0,0,1,0,0,0,NULL);
 BattleSetup_SetSide(1,2,1,1,0,0,0,NULL);
 /* The original loader expects a percentage, not a normalized fraction. */
 BattleSetup_SetMember(0,0,0,0,0,0,100.0f,NULL);
 BattleSetup_SetMember(1,0,1,0,0,0,100.0f,NULL);
 BattleSetup_Finish();
 if(original_type>=0){Vu0_Init();Pad_Init();Fade_Init();PadWatch_Init();watch=1;if((rc=XbFreshBoot_Frame(0)))goto done;}

 if(full_load>=2){Font_Init(1);FontIcon_Init();fonts=1;}
 if(Battle_GetScript()>=0 || !XbBattleBoot_ObjectStateClear()){rc=-902;goto done;}
 if(full_load){
  drive_loading=1;
  int loaded;
  if(full_load>=2){
   Battle_GetWork()->running=1;
   int initialized=Battle_Init();drive_loading=0;
   xsdk_log_write("D46 actual Battle_Init returned=%d read_error=%d",initialized,XbRes_LastError());
   if(initialized || XbRes_LastError()){
    xsdk_log_write("FAIL: D46 Battle_Init partial state retained; restart required; NOT_READY unchanged");return -954;
   }
   for(unsigned poll=0;poll<65536 && Job_GetCount() && !XbRes_LastError();poll++){Snd_Update();Job_Run();}
   if(Job_GetCount() || XbRes_LastError()){
    xsdk_log_write("FAIL: D46 post-init reload did not complete; state retained; restart required");return -954;
   }
   xsdk_log_write("PASS: D46 Battle_Init and post-init reload complete; no Battle_Update/Main");
   if(gBtlChars && gBtlChars->count==2)for(unsigned side=0;side<2;side++){
    const BtlMgrGauge *g=&gBtlChars->chars[side].members[0].gauge;
    xsdk_log_write("%s: D46 starting health side=%u value=%d maximum=%d",g->health>0 && g->health==g->healthMax?"PASS":"FAIL",side,g->health,g->healthMax);
   }
   if(full_load==6 || full_load>=8){
    if(!gBtlSeq || BtlSeq_GetState()!=BTL_SEQ_STAGE_INTRO){xsdk_log_write("FAIL: D46 sequence state unexpected, owners retained");return -957;}
    intro_poll_table=gBtlSeq->table;if(action_fast){saved_ready_update=intro_poll_table[2].update;intro_poll_table[2].update=ready_fast_update;xsdk_log_write("D46 test only: original READY handler fast-forwarded; fight simulation unchanged");}saved_intro_poll=intro_poll_table[0].poll;intro_skip_sent=0;intro_poll_table[0].poll=skip_stage_poll;
    if(full_load>=8){saved_talk_poll=intro_poll_table[1].poll;talk_skip_sent=0;talk_wrapped=1;intro_poll_table[1].poll=skip_talk_poll;}
   }
   if(full_load>=3){
    unsigned fight_seen=0,cancelled=0,natural_end=0;
    unsigned limit=full_load==9?3600u:full_load==8?128u:full_load==7?33u:full_load==6?8u:full_load==5?512u:full_load==4?64u:1u,completed=0;
    uint64_t started=probe_us();trace_quiet=1;
    for(unsigned frame=0;frame<limit;frame++){
     if(full_load==9 && cancel_requested(fight_seen)){cancelled=1;xsdk_log_write("D46 battle stopped between completed frames; normal original termination follows");break;}
     action_tick=fight_seen;
     if(action_check && !charge_check && (fight_seen==4 || fight_seen==16 || fight_seen==36 || fight_seen==48))xsdk_log_write("D46 scripted action tick=%u buttons=%04x LT=%u",fight_seen,fight_seen==4?0x4000:fight_seen==36?0x2000:fight_seen==48?0x8000:0,fight_seen==16?255:0);
     uint64_t frame_started=probe_us();stage_capture(&stage_prev);int step=Battle_StepFrame();
     frame_times[frame]=probe_us()-frame_started;
     if(action_check && (charge_check || fight_seen==5 || fight_seen==11 || fight_seen==18 || fight_seen==33 || fight_seen==39 || fight_seen==51) && gBtlChars)xsdk_log_write("D46 action response tick=%u action=%d ki=%d health=%d",fight_seen,gBtlChars->chars[0].action,gBtlChars->chars[0].members[0].gauge.ki,gBtlChars->chars[0].members[0].gauge.health);
     if(full_load==6 || (full_load==8 && frame%16u==0) || frame%32u==0 || frame+1==limit || step || XbRes_LastError())xsdk_log_write("D46 original Battle_StepFrame frame=%u returned=%d read_error=%d",frame,step,XbRes_LastError());
     if(step<0 || XbRes_LastError()){
      if(!presentation_quiescent || XbRes_LastError() || Job_GetCount() || File_GetPendingCount()){
       restore_intro_poll();xsdk_log_write("FAIL: D46 frame failure has read_error=%d jobs=%d pending=%d; owners retained; NOT_READY=2",XbRes_LastError(),Job_GetCount(),File_GetPendingCount());return -955;
      }
      frame_failure=-955;
      xsdk_log_write("D46 frame rejected after completed Init and quiesced presentation; terminating original session, NOT_READY=2");break;
     }
     if(full_load==6 || (full_load==8 && frame%16u==0) || frame%32u==0 || frame+1==limit){
      xsdk_log_write("D46 frame components simulation_us=%llu producers_us=%llu",(unsigned long long)last_update_us,(unsigned long long)last_draw_us);
      {DrStageSnapshot now;stage_capture(&now);uint64_t fr=frame_times[frame];
       uint64_t d_replay=now.replay-stage_prev.replay,d_store=now.store-stage_prev.store,d_bind=now.bind-stage_prev.bind,
                d_movie=now.movie-stage_prev.movie,d_present=now.present-stage_prev.present,
                d_pre=now.preflight-stage_prev.preflight,d_emit=now.emit-stage_prev.emit,d_sel=now.select-stage_prev.select;
       uint64_t sim=last_update_us,prod=last_draw_us;
       uint64_t outer=sim+prod+d_replay+d_bind+d_movie+d_present; /* bind includes nested stores */
       uint64_t unaccounted=fr>outer?fr-outer:0;
       uint64_t loose=sim+prod+d_replay+d_movie+d_present; /* ignoring overlapping target counters */
       uint64_t unaccounted_loose=fr>loose?fr-loose:0;
       xsdk_log_write("D46 frame budget total_us=%llu sim=%llu producers=%llu replay=%llu bind=%llu store=%llu movie=%llu present=%llu replay_stages=%llu/%llu/%llu unaccounted=%llu range_lo=%llu",
        (unsigned long long)fr,(unsigned long long)sim,(unsigned long long)prod,(unsigned long long)d_replay,(unsigned long long)d_bind,(unsigned long long)d_store,
        (unsigned long long)d_movie,(unsigned long long)d_present,(unsigned long long)d_pre,(unsigned long long)d_emit,(unsigned long long)d_sel,
        (unsigned long long)unaccounted,(unsigned long long)unaccounted_loose);}
      extern unsigned gGsTexturePressureSweeps,gGsTexturePressureFallbacks;
      xsdk_log_write("D46 texture pressure sweeps=%u white_fallbacks=%u direct_effect_packets=%u",gGsTexturePressureSweeps,gGsTexturePressureFallbacks,direct_effect_packets);
      DrCaptureProfile cp;dr_capture_original_profile(&cp);
      DrGsReplayProfile rp;dr_gs_replay_profile(&rp);xsdk_log_write("D46 replay prepare_us=%llu xenos_submit_us=%llu source_draws=%u replay_commands=%u merged_draws=%u expanded_vertices=%u",(unsigned long long)rp.build_us,(unsigned long long)rp.submit_us,rp.source_draws,rp.replay_commands,rp.merged_draws,rp.vertices);
      xsdk_log_write("D46 last submit DMA_us=%llu replay_us=%llu draws=%u CPU_vertices=%u VU_vertices=%u culled_strips=%u",(unsigned long long)cp.dma_us,(unsigned long long)cp.replay_us,cp.draws,cp.vertices,cp.vu_vertices,cp.culled_strips);
      DrXenosProfile xp;DrTargetProfile tp;dr_xenos_profile(&xp);dr_targets_profile(&tp);
      xsdk_log_write("D46 GPU lifetime draws=%u arena_restarts=%u vertex_waits=%u target_stores=%u target_loads=%u target_waits=%u stencil_passes=%u known_zero_restores=%u",xp.draws,xp.arena_restarts,xp.fence_waits,tp.stores,tp.loads,tp.fence_waits,tp.stencil_restore_passes,tp.zero_stencil_restores);
      xsdk_log_write("D46 constants literal_sets=%u skips=%u uniform_regs=%u skipped_regs=%u",xp.literal_sets,xp.literal_skips,xp.constant_registers,xp.constant_skips);
      xsdk_log_write("D46 validation scanned_vertices=%u reused_vertices=%u state_sets=%u state_skips=%u",xp.validated_vertices,xp.reused_validation_vertices,xp.state_sets,xp.state_skips);
      xsdk_log_write("D46 replay stages selects=%u preflight_us=%llu emit_us=%llu snapshot_us=%llu",xp.target_selects,(unsigned long long)xp.preflight_us,(unsigned long long)xp.emit_us,(unsigned long long)xp.select_us);
      xsdk_log_write("D46 target times store_us=%llu bind_us=%llu splits=%u",(unsigned long long)tp.store_us,(unsigned long long)tp.bind_us,tp.command_splits);
      {uint64_t movie_us=0,present_us=0;void dr_gs_backend_frame_times(uint64_t *,uint64_t *);dr_gs_backend_frame_times(&movie_us,&present_us);xsdk_log_write("D46 backend stage movie_us=%llu present_us=%llu",movie_us,present_us);}
      uint32_t md,mf;dr_vu1_model_profile(&md,&mf);xsdk_log_write("D46 model GPU draws=%u fallbacks=%u command_splits=%u",md,mf,tp.command_splits);
      uint32_t instructions,draws,fallbacks;dr_vu1_profile(&instructions,&draws,&fallbacks);
      xsdk_log_write("D46 profile frames=%u elapsed_us=%llu VU_instructions=%u stage_GPU_draws=%u stage_fallbacks=%u",frame+1,(unsigned long long)(probe_us()-started),instructions,draws,fallbacks);
      xsdk_log_write("D46 sequence frame=%u state=%d demo_flags=%08x time=%.9g length=%.9g camera=%.9g,%.9g,%.9g",frame,BtlSeq_GetState(),gDemoCam->flags,(double)DemoCam_GetTime(),(double)DemoCam_GetLength(),(double)gBtlCamView->pos.x,(double)gBtlCamView->pos.y,(double)gBtlCamView->pos.z);
      if(gHud)xsdk_log_write("D46 HUD flags=%08x gauge=%u timer=%u notice=%u",gHud->flags,gHud->gauge!=NULL,gHud->timer!=NULL,gHud->notice!=NULL);
      BtlObjVis *v=BtlObjVis_Get();
      xsdk_log_write("D46 visible objects frame=%u views=%d flags=%08x list0=%d list1=%d",frame,v->viewCount,v->flags,v->lists[0].count,v->lists[1].count);
      for(int side=0;side<2;side++){int id=BattleSide_GetObjId(side);BtlObj *obj=BtlObj_Get(id);if(obj)xsdk_log_write("D46 fighter side=%d id=%d active=%d bound=%d noAnim=%d state=%08x view0=%08x",side,id,obj->active,obj->unk04,obj->noAnim,obj->state.flags,obj->state.views[0].flags);}
     }
     completed++;
     if(full_load>=8 && BtlSeq_GetState()==BTL_SEQ_FIGHT){fight_seen++;if(full_load==8 && fight_seen>=4){xsdk_log_write("PASS: D46 reached original FIGHT state and submitted four bounded fight-state frames");break;}}
     if(step){natural_end=1;break;}
    }
    trace_quiet=0;
    if(full_load==9)xsdk_log_write("D46 experimental battle completed_steps=%u FIGHT_frames=%u cancelled=%u natural_end=%u limit=%u",completed,fight_seen,cancelled,natural_end,limit);
    if(completed){
     unsigned over=0;for(unsigned i=0;i<completed;i++){if(frame_times[i]>33333)over++;for(unsigned j=i;j>0&&frame_times[j]<frame_times[j-1];j--){uint64_t t=frame_times[j];frame_times[j]=frame_times[j-1];frame_times[j-1]=t;}}
     unsigned p95=(completed*95u+99u)/100u-1u;
     xsdk_log_write("D46 frame times samples=%u min_us=%llu median_us=%llu p95_us=%llu max_us=%llu over_33333us=%u",completed,(unsigned long long)frame_times[0],(unsigned long long)frame_times[completed/2],(unsigned long long)frame_times[p95],(unsigned long long)frame_times[completed-1],over);
    }
    if(!frame_failure)xsdk_log_write("PASS: D46 original simulation/draw iterations=%u submitted; inspect presented pixels; Battle_Main not called",completed);
   }
   restore_intro_poll();Battle_Term();Battle_GetWork()->running=0;objects=scripts=0;loaded=1;
   if(frame_failure){rc=frame_failure;complete=1;goto done;}
  }else loaded=Battle_Load();
  drive_loading=0;
  objects=gBtlObjWork!=NULL;scripts=objects;
  rc=XbRes_LastError();complete=original_type>=0?!rc:phase4_complete;
  if(!rc && (!loaded || (full_load<2 && BtlObj_GetCount()!=2)))rc=-902;
  if(original_type>=0){
   unsigned begins=gXbGfxBeginFrames-gfx_begin,ends=gXbGfxEndFrames-gfx_end;
   if(!rc && (!original_phase4 || !begins || begins!=ends))rc=-953;
   xsdk_log_write("%s: D46 full original load type=%d begin=%u end=%u phase4=%d result=%d",rc?"FAIL":"PASS",original_type,begins,ends,original_phase4,rc);
  }
  if(full_load<2)xsdk_log_write("%s: D46 actual Battle_Load result=%d objects=%d error=%d; scoped loading driver; NOT Battle_Init/Update/Main",rc?"FAIL":"PASS",loaded,objects?BtlObj_GetCount():0,rc);
  if(objects){
   for(int side=0;side<2;side++){
    int id=BattleSide_GetObjId(side);
    if(id>=0 && BtlObj_Get(id))BtlObj_Destroy(id);
   }
  }
 }else{
  xsdk_log_write("D46 fresh: BtlObj_Init begin");BtlObj_Init(1);objects=1;
  xsdk_log_write("D46 fresh: GSC/script begin");
  Gsc_InitDefault(gBtlScriptCommands);BtlScript_Init();scripts=1;
  BtlLoad_PushInitialJob();
  if(!XbRes_LastError())XbFreshBoot_PumpLoading();
  rc=XbRes_LastError();complete=phase4_complete;
 }
 done:
 restore_intro_poll();
 /* Abort only the pre-fighter/no-script prefix. If it refuses, preserve the
  * owners and gate; freeing underneath an unknown live state is unsafe. */
 if(objects && !XbBtlLoad_AbortInitial()){
  xsdk_log_write("FAIL: D46 fresh rollback refused; owners retained; restart required");
  return -902;
 }
 if(objects)BtlObj_Term();
 if(scripts){BtlScript_Free();Gsc_Exit();}
 if(fonts)Font_Term();
 if(watch){PadWatch_Term();Fade_Term();}
 if(queue)Job_Destroy();
 if(gSaveData){Heap_Free(gSaveData);gSaveData=NULL;}
 memset(&gLoadScreen,0,sizeof(gLoadScreen));
 if(gProgress){Heap_Free(gProgress->loadPack);Heap_Free(gProgress->loadRes);Heap_Free(gProgress->loadSprites);Heap_Free(gProgress);gProgress=NULL;}
 if(scope)XbFile_AbortRequestsRetainBuffers();
 if(audio){Snd_UnloadBank(1);Snd_Term();}
 if(file_boot)close_file_boot();
 if(gCommonRes){for(unsigned i=0;i<3;i++)Heap_Free(gCommonRes->data[i]);Heap_Free(gCommonRes->boot);Heap_Free(gCommonRes);gCommonRes=NULL;}
 if(scope){XbRes_Report();XbRes_EndCheckedReads();}
 if(baseline && Heap_GetFreeSize()!=baseline){xsdk_log_write("FAIL: D46 fresh heap mismatch before=%d after=%d",baseline,Heap_GetFreeSize());rc=-938;}
 {int leave=XbBattleRuntime_LeaveOriginal();if(leave)rc=leave;else session_owned=0;}
 xsdk_log_write("D46 fresh close result=%d complete=%d; NOT_READY unchanged",rc,complete);
 return rc;
}

int XbFreshBoot_Probe(void){return probe(0);}
int XbFreshBoot_LoadProbe(void){return probe(1);}

int XbFreshBoot_RecoveryProbe(void){
 inject_partial_failure=1;
 int rc=probe(0);inject_partial_failure=0;
 if(rc!=-897){xsdk_log_write("FAIL: D46 partial recovery unexpected error=%d",rc);return rc?rc:-902;}
 xsdk_log_write("PASS: D46 partial phase4 refused expected=-897; session closed; following fresh load must succeed");
 return 0;
}

int XbFreshBoot_InitProbe(void){return probe(2);}

int XbFreshBoot_FrameProbe(void){return probe(3);}
int XbFreshBoot_FramesProbe(void){return probe(4);}
int XbFreshBoot_IntroProbe(void){return probe(5);}
int XbFreshBoot_FighterProbe(void){return probe(6);}
int XbFreshBoot_StageProbe(void){return probe(7);}
int XbFreshBoot_FightProbe(void){return probe(8);}
int XbFreshBoot_BattleProbe(void){return probe(9);}
int XbFreshBoot_BattleCheck(void){cancel_test=1;int rc=probe(9);cancel_test=0;return rc;}
int XbFreshBoot_ActionCheck(void){
 action_check=1;action_tick=0;saved_pad_poll=gXbPadPoll;gXbPadPoll=scripted_pad_poll;
 int rc=probe(9);gXbPadPoll=saved_pad_poll;saved_pad_poll=NULL;action_check=0;return rc;
}

/* Original shadow buffers are native u32 packets in live object-owned pools.
 * Accept only the current exact buffer of a live object and a pool-owned node. */
static int shadow_chain_span(const void *ptr,size_t *bytes){
 if(!session_owned || !gBtlObjWork || !ptr || !bytes || ((uintptr_t)ptr&15))return 0;
 ObjShadowPool *pool=(ObjShadowPool *)&gBtlObjWork->pool3;
 for(int i=0;i<BTL_OBJ_MAX;i++){
  BtlObj *live=BtlObj_Get(i);if(!live || !live->active)continue;
  ObjMdlObj *obj=(ObjMdlObj *)live;ObjShadow *shadow=obj->shadow;if(!shadow)continue;
  for(unsigned j=0;j<OBJ_SHADOW_MAX;j++)if(shadow==&pool->nodes[j]){
   ObjShadowWork *w=shadow->work;
   if(!w || w->flip<0 || w->flip>1 || !shadow->count || shadow->count>=OBJ_SHADOW_PKT_TRIS)break;
   if(ptr==w->pkt[w->flip]){*bytes=(size_t)ObjShadow_CalcPacketWords(OBJ_SHADOW_PKT_TRIS)*4;return 1;}
   break;
  }
 }
 return 0;
}

/* Exact tracked-file inline RET bridge. No native-pointer masking or unbounded
 * source scan. Other chain layouts remain rejected until explicitly ported. */
u32 XbVif_ChainAddress(u32 ptr) {
    extern int dr_dma_snapshot_chain(const void *,size_t,u32 *),dr_dma_snapshot_words(const void *,size_t,u32 *),dr_dma_reject_packet(void);
    extern int XbFighter_ChainSpan(const void *,size_t *);
    const void *file;size_t size;int id,native_words=0;u32 address=0;
    const unsigned char *src=(const void *)(uintptr_t)ptr;
    if(!ptr || (ptr&15u))goto bad;
    if(XbRes_ReadFile(src,&file,&size,&id)){
        size_t offset=(uintptr_t)src-(uintptr_t)file;if(offset>size)goto bad;size-=offset;
    }else if(XbFighter_ChainSpan(src,&size)){}
    else if(shadow_chain_span(src,&size))native_words=1;
    else goto bad;
    if(size<16)goto bad;
    u32 tag;if(native_words)memcpy(&tag,src,4);else tag=XbRes_Le32(src);size_t bytes=((size_t)(tag&0xFFFFu)+1u)*16u;
    if(((tag>>28)&7u)!=6u || bytes>size)goto bad;
    if(native_words){if(dr_dma_snapshot_words(src,bytes,&address))return 0;}
    else if(dr_dma_snapshot_chain(src,bytes,&address))return 0;
    return address;
 bad:
    xsdk_log_write("VIF CALL rejected source=%08x: untracked, unaligned or unsupported inline RET",ptr);
    dr_dma_reject_packet();return 0;
}

int XbFreshBoot_ActionFastCheck(void){action_fast=1;int rc=XbFreshBoot_ActionCheck();action_fast=0;return rc;}

int XbFreshBoot_ChargeCheck(void){charge_check=1;action_fast=1;int rc=XbFreshBoot_ActionCheck();charge_check=0;action_fast=0;return rc;}
