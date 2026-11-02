#include <xsdk/thread.h>
/* SPDX-License-Identifier: MIT
 * xsdk API usage based on samples/template/main.c and src/file.c (MIT, Mena).
 * Xbox ABI and 64-bit reads: xsdk/include/xsdk/kernel.h, publicly documented by xsdk.
 */
#include "demo_ui.h"
#include "dr_disc.h"
#include "net_check.h"
#include "lobby.h"
#include "lobby_selftest.h"
#include "online_battle.h"
#ifdef DR_HAVE_BT3XBX
#include "bt3xbx_adapter.h"
#endif
#include "dr_resource.h"
#include "dr_container.h"
#include "dr_graphics_demo.h"
#include "dr_body_demo.h"
#include "dr_gs_texture.h"
#include "dr_vu1.h"
#include "xb_platform.h"
#ifdef DR_TYPED_CATALOG
#include "xb_typed_data.h"
#include "dr_stage_collision.h"
#ifdef DR_ENGINE_LINKED
#include "xb_battle_stage.h"
#include "xb_battle_fighter.h"
#include "xb_battle_common.h"
#include "xb_battle_runtime.h"
#include "xb_transport_probe.h"
#include <xsdk/log.h>
static XbBattleCommon *native_common;
static XbBattleRuntime *native_runtime;
static int native_managers;
static int native_audio,native_stage_running,native_scene_running;
static unsigned native_scene_frames;
static int native_typed_values_loaded;
static int fresh_boot_requested;
#include "xb_fresh_boot.h"
static int transport_only,transport_model_requested,transport_model_pending;
#include "xb_disc.h"
#include "dr_texfile.h"
#include "dr_xenos.h"
#include <stdlib.h>
static uint32_t loading_preview_texture;
static unsigned loading_preview_frames;
static int loading_preview_open(void){
    dr_span span;size_t bytes=0;DrTexFileImage image;
    int error=XbDisc_FindFile("pzs3us1/00960.bin",&span);
    if(error || !span.size || span.size>0x6800)return error?error:-938;
    unsigned char *packed=malloc((size_t)span.size),*raw=malloc(0x6800),*rgba=malloc(1024*1024*4);
    if(!packed || !raw || !rgba){free(packed);free(raw);free(rgba);return -908;}
    error=XbDisc_ReadSpan(&span,0,packed,(size_t)span.size);
    if(!error)error=dr_bpe_decode(packed,(size_t)span.size,raw,0x6800,&bytes);
    if(!error)error=dr_texfile_decode(raw,bytes,0,rgba,1024*1024*4,&image);
    if(!error)error=dr_xenos_open();
    if(!error){
        error=dr_xenos_texture_create(image.width,image.height,rgba,&loading_preview_texture);
        if(error)dr_xenos_close();
        else xsdk_log_write("D46 loading asset GPU upload: file=0x3c1 image=0 size=%ux%u; separate raw asset preview, not original Load_RunBlocking",image.width,image.height);
    }
    free(packed);free(raw);free(rgba);return error;
}
static int loading_preview_draw(unsigned width,unsigned height){
    DrGsVertex quad[]={
        {384,104,0,128,128,128,128,0,0,1},{896,104,0,128,128,128,128,1,0,1},{384,616,0,128,128,128,128,0,1,1},
        {384,616,0,128,128,128,128,0,1,1},{896,104,0,128,128,128,128,1,0,1},{896,616,0,128,128,128,128,1,1,1}
    };
    DrXenosDraw draw={.logical_width=1280,.logical_height=720,.depth=1,.gs_material=1,.textured=1,.tcc=1,.sampler=6,.scissor={0,0,(int)width,(int)height}};
    int error=dr_xenos_begin(width,height);if(error)return error;
    error=dr_xenos_draw(quad,6,loading_preview_texture,&draw);int end=dr_xenos_end();return error?error:end;
}
static XbBattleFighter *native_fighters[2];
static int native_object_diagnostic;
static XbBattleStageCollision *native_stage;
static void release_native_stage(void){
 native_audio=0;native_stage_running=0;native_scene_running=0;native_scene_frames=0;
 if(native_managers){int e=XbBattleRuntime_EndManagers(native_runtime);if(e){xsdk_log_write("native manager cleanup refused: %d",e);return;}native_managers=0;}
 for(unsigned i=0;i<2;i++){XbBattleFighter_FreeModelAnimation(native_fighters[i]);native_fighters[i]=NULL;}
 if(native_object_diagnostic){int e=XbBattleFighter_EndObjectDiagnostic();if(e){xsdk_log_write("native object cleanup refused: %d",e);return;}native_object_diagnostic=0;}
 if(native_runtime){int e=XbBattleRuntime_Close(native_runtime);if(e){xsdk_log_write("native common cleanup refused: %d",e);return;}native_runtime=NULL;}
 XbBattleCommon_Free(native_common);native_common=NULL;
 if(native_stage){int e=XbBattleStage_UninstallCollision(native_stage);if(!e)e=XbBattleStage_FreeCollision(native_stage);if(!e)native_stage=NULL;}
}
#endif
#include "xb_disc.h"
#endif
#include <arpa/inet.h>
#include <xsdk/net.h>
#include "save_export.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "xsdk/gpu.h"
#include "xsdk/kernel.h"
#include "xsdk/log.h"
#include "xsdk/present.h"
#include "dr_capture_demo.h"
#include "gfx_present.h"
#include "xsdk/text.h"
#include "xsdk/texture.h"

static DemoUi ui;
/* D52-2: one lockstep battle instance, bound while the lobby is paired. */
static DrOnlineBattle online_battle;
/* D53-B: set when binding the engine backend failed. Battle_Init allocates
 * every battle subsystem, so a failed one must never be retried blindly on
 * the next tick: the flag stops the retry until the lobby drops. */
static int online_bind_failed;
#ifdef DR_HAVE_BT3XBX
static DrBt3xbxState bt3xbx_state;
#endif
static uint32_t display_width,display_height;
static bool confirm_seen,back_seen,navigation_seen,first_frame;
/* D51: 0 off, 1 d51-gfx-probe (640x480), 2 d51-gfx-probe-720 (1280x720). */
static int d51_gfx_probe;
/* D52: run the loopback lobby self-test once, after the interface pixel check. */
static int d52_lobby_selftest;
static uint64_t now_ms(void)
{
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)!=0) return 0;
    return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
static void draw_text(int x,int y,unsigned scale,uint32_t color,const char *s)
{
    xsdk_text_draw(x,y,scale,color,s);
}
/* Rectangles through xsdk's tested full-block glyph and scissor, no extra shader/compiler dependency.
 * At 720p, 128 blocks cover a row. Always reset scissor after drawing a rectangle. */
static void draw_rect(int x,int y,int w,int h,uint32_t color)
{
    static char blocks[128*3+1];
    if(w<=0 || h<=0 || x<0 || y<0) return;
    if(!blocks[0]) {
        for(int i=0;i<128;i++) memcpy(blocks+i*3,"\xE2\x96\x88",3);
    }
    unsigned cols=(unsigned)(w+19)/20;
    if(cols>128) cols=128;
    char line[128*3+1]; memcpy(line,blocks,cols*3); line[cols*3]=0;
    xsdk_gpu_set_scissor((unsigned)x,(unsigned)y,(unsigned)w,(unsigned)h);
    for(int row=0;row<h;row+=40) xsdk_text_draw(x,y+row,2,color,line);
    xsdk_gpu_set_scissor(0,0,display_width,display_height);
}
static const DemoPaint paint={draw_text,draw_rect};
static int compare_paths(const void *a,const void *b) {return strcasecmp(a,b);}
static void scan_isos(void)
{
    static const char *const roots[]={"game:/isos","game:","usb0:/isos","usb1:/isos"};
    ui.count=0; ui.selection=0;
    for(unsigned r=0;r<sizeof(roots)/sizeof(roots[0]);r++) {
        DIR *dir=opendir(roots[r]);
        if(!dir) {xsdk_log_write("scan: %s unavailable: errno %d",roots[r],errno);continue;}
        struct dirent *entry;
        while((entry=readdir(dir))!=NULL && ui.count<DEMO_MAX_ISOS) {
            size_t n=strlen(entry->d_name);
            if(n<5 || strcasecmp(entry->d_name+n-4,".iso")) continue;
            int len=snprintf(ui.paths[ui.count],DEMO_PATH,"%s/%s",roots[r],entry->d_name);
            if(len<0 || len>=DEMO_PATH) continue;
            ui.count++;
        }
        if(closedir(dir)!=0) xsdk_log_write("scan: closedir failed: errno %d",errno);
    }
    qsort(ui.paths,(size_t)ui.count,sizeof(ui.paths[0]),compare_paths);
    for(int i=0;i<ui.count;i++) {
        const char *base=strrchr(ui.paths[i],'/');
        snprintf(ui.names[i],sizeof(ui.names[i]),"%.95s",base?base+1:ui.paths[i]);
    }
    xsdk_log_write("scan: found %d ISO(s), limit %d",ui.count,DEMO_MAX_ISOS);
}
static int image_read(void *ctx,uint64_t offset,void *data,size_t n)
{
    HANDLE h=*(HANDLE *)ctx;
    IO_STATUS_BLOCK io;
    NTSTATUS status=NtReadFile(h,0,NULL,NULL,&io,data,(uint32_t)n,&offset);
    if(!NT_SUCCESS(status) || io.information!=n) {
        xsdk_log_write("iso: NtReadFile offset %llu size %u: 0x%08X",(unsigned long long)offset,(unsigned)n,(unsigned)status);
        return -1;
    }
    return 0;
}
static NTSTATUS open_image(const char *path,HANDLE *handle,uint64_t *size)
{
    /* Kernel paths follow xsdk/src/path.c's established drive mapping. */
    char resolved[DEMO_PATH+40]; const char *tail=path;
    const char *prefix="";
    if(!strncasecmp(path,"usb0:/",6)) {prefix="\\Device\\Mass0\\";tail=path+6;}
    if(!strncasecmp(path,"usb1:/",6)) {prefix="\\Device\\Mass1\\";tail=path+6;}
    int len=snprintf(resolved,sizeof(resolved),"%s%s",prefix,tail);
    if(len<0 || (unsigned)len>=sizeof(resolved)) return STATUS_INVALID_PARAMETER;
    for(char *p=resolved;*p;p++) if(*p=='/') *p='\\';
    ANSI_STRING name={(uint16_t)len,(uint16_t)(len+1),resolved};
    OBJECT_ATTRIBUTES attrs={resolved[0]=='\\'?0:OB_DOS_DEVICES,&name,0}; /* xsdk/src/path.c */
    IO_STATUS_BLOCK io;
    NTSTATUS status=NtCreateFile(handle,(uint32_t)GENERIC_READ|SYNCHRONIZE,&attrs,&io,NULL,
        FILE_ATTRIBUTE_NORMAL,FILE_SHARE_READ,FILE_OPEN,FILE_SYNCHRONOUS_IO_NONALERT|FILE_NON_DIRECTORY_FILE);
    if(!NT_SUCCESS(status)) return status;
    FILE_NETWORK_OPEN_INFORMATION info;
    status=NtQueryInformationFile(*handle,&io,&info,sizeof(info),FILE_NETWORK_OPEN_INFORMATION_CLASS);
    if(!NT_SUCCESS(status)) {NtClose(*handle);*handle=0;return status;}
    *size=info.end_of_file;return STATUS_SUCCESS;
}
static unsigned body_draw_report_pending,body_probe_frames;
static int body_cache_autotest;
static int open_body_demo(int face)
{
    if(!ui.count)scan_isos();
    if(!ui.count) {
        snprintf(ui.modal,sizeof(ui.modal),"Nenhuma ISO encontrada.\nColoque sua ISO USA SLUS-21678 na pasta isos/.\nO modelo é lido da sua ISO.");return DR_NOT_FOUND;
    }
    int selected=ui.selected_iso>=0 && ui.selected_iso<ui.count?ui.selected_iso:0;
    HANDLE handle=0;uint64_t size=0;NTSTATUS status=open_image(ui.paths[selected],&handle,&size);
    if(!NT_SUCCESS(status)) {
        snprintf(ui.modal,sizeof(ui.modal),"Falha ao abrir a ISO: %08X.",(unsigned)status);return DR_IO;
    }
    dr_disc disc;DrBodyPipelineStats stats;int rc=dr_mount(&disc,image_read,&handle,size);
    if(!rc)rc=dr_body_demo_open_face(&disc,face,&stats);
    NtClose(handle);
    if(rc)snprintf(ui.modal,sizeof(ui.modal),"Falha ao carregar o modelo: %d.\nFormato esperado: ISO USA SLUS-21678.\nConsulte default.log.",rc);
    else xsdk_log_write("body reference pose: parts=%u chains=%u MSCNT=%u GIF=%u triangles=%u original_instructions=%u",
        stats.parts,stats.body_chains,stats.mscnt,stats.gif_packets,stats.triangles,stats.program_instructions);
    if(!rc){
        body_draw_report_pending=1;body_probe_frames=0;
        DrBodyDemoProfile p;dr_body_demo_profile(&p);
        xsdk_log_write("D46 model profile: model_reads=%u bytes=%u body_builds=%u uploads=%u source_ms=%.3f body_ms=%.3f textures_ms=%.3f total_ms=%.3f; CPU cache, not a battle",p.model_reads,(unsigned)p.source_bytes,p.body_builds,p.texture_uploads,p.source_us/1000.0,p.body_us/1000.0,p.texture_us/1000.0,p.total_us/1000.0);
    }
    if(!rc)xsdk_log_write("body: %u base-layer materials loaded, face=%d mask=%02X",dr_body_demo_material_count(),face,(unsigned)stats.face_mask);
    xsdk_log_write("%s: ISO body diagnostic open rc=%d; no battle; separate package texture previews",rc?"FAIL":"PASS",rc);
    return rc;
}
static void verify_iso(void)
{
    const int selected=ui.selection;
    if(selected<0 || selected>=ui.count) return;
    ui.iso_valid=false;
    HANDLE handle=0; uint64_t size=0;
    NTSTATUS status=open_image(ui.paths[selected],&handle,&size);
    if(!NT_SUCCESS(status)) {
        snprintf(ui.modal,sizeof(ui.modal),"Não foi possível abrir a ISO.\nCódigo: 0x%08X\nConsulte default.log para os detalhes.",(unsigned)status);
        xsdk_log_write("FAIL: ISO open %s: 0x%08X",ui.paths[selected],(unsigned)status);return;
    }
    dr_disc disc; dr_span file;
    int rc=dr_mount(&disc,image_read,&handle,size);
    const char *missing="estrutura ISO9660";
    static const char *const required[]={"SLUS_216.78","BIN/DBZP.BIN","DATA/PZS3US0.AFS","DATA/PZS3US1.AFS","DATA/PZS3US2.AFS"};
    uint32_t entries=0;
    for(unsigned i=0;!rc && i<sizeof(required)/sizeof(required[0]);i++) {
        missing=required[i];rc=dr_find(&disc,required[i],&file);
        if(!rc && file.directory) rc=DR_FORMAT;
        if(!rc && i>=2) {
            unsigned char h[8]; rc=dr_read(&disc,&file,0,h,sizeof(h));
            if(!rc && memcmp(h,"AFS\0",4)) rc=DR_FORMAT;
            if(!rc) {
                uint32_t count=(uint32_t)h[4]|(uint32_t)h[5]<<8|(uint32_t)h[6]<<16|(uint32_t)h[7]<<24;
                if(count==0 || 8+(uint64_t)count*8>file.size) rc=DR_FORMAT;
                else {
                    entries+=count;
                    dr_span span; rc=dr_afs_entry(&disc,&file,0,&span);
                    unsigned char sample[32];
                    if(!rc) rc=dr_read(&disc,&span,0,sample,span.size<sizeof(sample)?(size_t)span.size:sizeof(sample));
                }
            }
        }
    }
    NTSTATUS close_status=NtClose(handle);
    if(!NT_SUCCESS(close_status)) xsdk_log_write("iso: NtClose failed: 0x%08X",(unsigned)close_status);
    if(rc) {
        snprintf(ui.modal,sizeof(ui.modal),"ISO não reconhecida ou danificada.\nEtapa: %.50s\nErro: %s\nFormato esperado: ISO USA SLUS-21678.",missing,dr_error(rc));
        xsdk_log_write("FAIL: ISO %s: %s",missing,dr_error(rc));return;
    }
    ui.iso_valid=true;ui.selected_iso=selected;
    snprintf(ui.iso_name,sizeof(ui.iso_name),"%s",ui.names[selected]);
    snprintf(ui.modal,sizeof(ui.modal),"Arquivos USA encontrados.\nTamanho: %llu MiB · %u entradas AFS\nLeitura dos arquivos internos concluída.\n\nIntegridade por hash ainda não verificada.\nEsta demo ainda não inicia as lutas.",
        (unsigned long long)(size/1048576),(unsigned)entries);
    xsdk_log_write("PASS: ISO structures and first AFS entries readable: %s, %llu bytes",ui.paths[selected],(unsigned long long)size);
    (void)XbPlatform_ProbeFiles(ui.paths[selected]);
}
/* Diagnostic path exercises exact ISO/AFS/BIN spans with the user's selected resource.
 * It does not select fighters, start the battle loop or display a model. */
static void test_iso_resource(void)
{
    if(ui.selection<0 || ui.selection>=ui.count) return;
    char path[64];unsigned entry_index=0,compressed=0;
    FILE *config=fopen("game:/battle_resource.txt","r");
    int valid=config && fscanf(config,"%63s %u %u",path,&entry_index,&compressed)==3 && compressed<=1;
    if(config) fclose(config);
    if(!valid) {
        snprintf(ui.modal,sizeof(ui.modal),"Configure battle_resource.txt ao lado do XEX.\nFormato e exemplo no LEIA-ME.txt.\nO teste lê uma animação da ISO selecionada.\nNão inicia uma luta.");return;
    }
    HANDLE handle=0;uint64_t size=0;
    NTSTATUS status=open_image(ui.paths[ui.selection],&handle,&size);
    if(!NT_SUCCESS(status)) {snprintf(ui.modal,sizeof(ui.modal),"Não foi possível abrir a ISO: %08X.",(unsigned)status);return;}
    dr_disc disc;dr_span pack,entry;unsigned flags=0;size_t written=0;
    int rc=dr_mount(&disc,image_read,&handle,size);
    if(!rc) rc=dr_game_file(&disc,path,&pack);
    if(!rc) rc=dr_pack_entry(&disc,&pack,entry_index,&entry,&flags);
    void *scratch=NULL,*motion=NULL;
    if(!rc) {
        scratch=malloc(262144);motion=malloc(49152);
        if(!scratch || !motion) rc=DR_RANGE;
        else rc=dr_load_motion(&disc,&entry,(int)compressed,1,scratch,262144,motion,49152,&written);
    }
    free(motion);free(scratch);NtClose(handle);
    xsdk_log_write("%s: ISO AFS/BIN motion path=%s index=%u compressed=%u flags=%u bytes=%u error=%d",
                   rc?"FAIL":"PASS",path,entry_index,compressed,flags,(unsigned)written,rc);
    if(rc) snprintf(ui.modal,sizeof(ui.modal),"Recurso recusado: erro %d.\nConfira caminho, índice e tipo no arquivo.\nO contêiner pode não ser compatível.\nConsulte default.log. Nenhuma luta iniciada.",rc);
    else snprintf(ui.modal,sizeof(ui.modal),"Animação validada e convertida: %u bytes.\nLeitura ISO / AFS / BIN concluída.\n\nEste teste não desenha o personagem.\nO boot da luta ainda não está pronto.",(unsigned)written);
}
#ifdef DR_TYPED_CATALOG
static int stage_count_triangle(void *ctx,uint32_t polygon,const DrStageTriangle *triangle)
{
    (void)polygon;(void)triangle;(*(unsigned *)ctx)++;return 1;
}
#ifdef DR_ENGINE_LINKED
static int read_battle_resource(unsigned id,void **bytes,size_t *size)
{
    char path[64];dr_span span;*bytes=NULL;*size=0;snprintf(path,sizeof(path),"pzs3us1/%05u.bin",id-1);
    int e=XbDisc_FindFile(path,&span);if(e)return e;
    if(!span.size || span.size>8*1024*1024)return DR_STAGE_BOUNDS;
    void *p=malloc((size_t)span.size);if(!p)return DR_STAGE_MEMORY;
    e=XbDisc_ReadSpan(&span,0,p,(size_t)span.size);if(e){free(p);return e;}
    *bytes=p;*size=(size_t)span.size;return 0;
}
static int prepare_fighter_models(void)
{
    void *common_pack=NULL;size_t common_bytes=0;XbBattleCommonInfo common_info;
    int common_error=read_battle_resource(2,&common_pack,&common_bytes);
    if(!common_error)common_error=XbBattleCommon_Prepare(common_pack,common_bytes,&native_common,&common_info);
    if(common_error){free(common_pack);common_pack=NULL;}
    if(common_error){xsdk_log_write("native common battle tables: refused error=%d; no manager boot",common_error);return common_error;}
    void *item_pack=NULL;size_t item_bytes=0;
    common_error=read_battle_resource(4,&item_pack,&item_bytes);
    if(!common_error)common_error=XbBattleCommon_AttachItems(native_common,item_pack,item_bytes);
    if(common_error){free(item_pack);free(common_pack);return common_error;}
    XbBattleConsumerProbe consumer;
    common_error=XbBattleCommon_ProbeConsumers(native_common,&consumer);
    xsdk_log_write("original table consumers: error=%d animations=%u voices=%u characters=%u items=%u; temporary reader view, NOT fighter manager boot",common_error,consumer.animations,consumer.voices,consumer.characters,consumer.items);
    if(!common_error)common_error=XbBattleRuntime_Open(common_pack,common_bytes,item_pack,item_bytes,&native_runtime);
    free(item_pack);free(common_pack);
    if(common_error)return common_error;
    void *scene_pack=NULL;size_t scene_bytes=0;XbCommon3Info scene_info={0};
    common_error=read_battle_resource(3,&scene_pack,&scene_bytes);
    if(!common_error)common_error=XbBattleRuntime_AttachCommon3(native_runtime,scene_pack,scene_bytes,&scene_info);
    free(scene_pack);
    xsdk_log_write("common3 native resources: error=%d entries=%u prepared=%u TexFiles=%u image_or_palette_entries=%u unprepared=%u; complete scene remains unavailable",common_error,scene_info.entries,scene_info.prepared,scene_info.textures,scene_info.images,scene_info.unprepared);
    if(common_error)return common_error;
    void *ai_data=NULL;size_t ai_bytes=0;
    common_error=XbBattleCommon_GetAiData(native_common,&ai_data,&ai_bytes);
    if(common_error)return common_error;
    xsdk_log_write("native common AI resource: address=%p bytes=%u; internal native pointers prepared; manager NOT initialized",ai_data,(unsigned)ai_bytes);
    xsdk_log_write("native common battle tables: roster=%u/%u/%u/%u/%u/%u AI_steps=%u rules=%u profiles=%u; owned CPU views, BtlChar/BtlAi NOT initialized",
        common_info.roster_records[0],common_info.roster_records[1],common_info.roster_records[2],common_info.roster_records[3],common_info.roster_records[4],common_info.roster_records[5],common_info.ai_steps,common_info.ai_rules,common_info.ai_profiles);
    int registry=XbBattleFighter_BeginOriginalObjects();if(registry)return registry;native_object_diagnostic=1;
    for(unsigned side=0;side<2;side++){
        void *model=NULL,*anims=NULL;size_t mn=0,an=0;XbFighterPrepared stats;int e;
        e=read_battle_resource(0x590+side*10,&model,&mn);
        if(!e)e=read_battle_resource(0x598+side*10,&anims,&an);
        if(!e)e=XbBattleFighter_PrepareModelAnimation(model,mn,anims,an,&native_fighters[side],&stats);
        free(model);free(anims);
        if(e){xsdk_log_write("fighter model-animation preparation: side=%u refused error=%d",side,e);return e;}
        void *effects=NULL;size_t en=0;XbCharacterEffectsInfo effect_info={0};
        e=read_battle_resource(0x599+side*10,&effects,&en);
        if(!e)e=XbBattleFighter_AttachEffectManagers(native_fighters[side],effects,en,&effect_info);
        free(effects);
        xsdk_log_write("native character effects: side=%u error=%d sets=%u textures=%u sprites=%u cameras=%u technique_packs_prepared=%u unprepared=%u",side,e,effect_info.emitter_sets,effect_info.textures,effect_info.sprite_packs,effect_info.cameras,effect_info.prepared_technique_packs,effect_info.unprepared_technique_packs);
        if(e)return e;
        int id=-1;e=XbBattleFighter_CreateModelObject(native_fighters[side],side,&id);
        xsdk_log_write("fighter real BtlObj_CreateChara/Setup: side=%u id=%d error=%d; original BtlObj_Init(1), native CPU resources/pools/materials; BtlChar/BtlAi NOT initialized",side,id,e);
        if(!e)e=XbBattleFighter_PlayModel(native_fighters[side],7,2);
        uint32_t frame=0,length=0;
        if(!e)e=XbBattleFighter_StepModel(native_fighters[side],&frame,&length);
        unsigned body=0,spheres=0,boxes=0;
        if(!e)e=XbBattleFighter_PlayMotion(native_fighters[side],0,2);
        if(!e)e=XbBattleFighter_StepModel(native_fighters[side],&frame,&length);
        XbFighterFaceState face;
        if(!e)e=XbBattleFighter_SetEye(native_fighters[side],9);
        if(!e)e=XbBattleFighter_SetFace(native_fighters[side],2,0);
        for(unsigned tick=0;!e && tick<8;tick++)e=XbBattleFighter_StepFace(native_fighters[side],&face);
        if(!e)xsdk_log_write("native CPU facial: side=%u eye=%d mode=%d shape=%d lip=%u/%u time=%f; no speech clock or GPU presentation",side,face.eye,face.mode,face.shape,face.lip_index,face.lip_count,(double)face.lip_time);
        if(!e)e=XbBattleFighter_SamplePose(native_fighters[side]);
        if(!e)e=XbBattleFighter_CalcMatrices(native_fighters[side]);
        if(!e)e=XbBattleFighter_UpdateVolumes(native_fighters[side],&body,&spheres,&boxes);
        xsdk_log_write("fighter native pose/body: side=%u error=%d body_parts=%u attack_spheres=%u attack_boxes=%u; renderer/character managers still unavailable",side,e,body,spheres,boxes);
        unsigned textures=0;if(!e)e=XbBattleFighter_PrepareTextures(native_fighters[side],&textures);
        XbFighterMesh mesh;XbFighterTexture texture;
        if(!e)e=XbBattleFighter_GetMesh(native_fighters[side],0,&mesh);
        if(!e && textures)e=XbBattleFighter_GetTexture(native_fighters[side],0,&texture);
        unsigned faces=0;for(unsigned shape=0;shape<8 && !e;shape++){const void *vif;size_t n;int rc=XbBattleFighter_GetFaceGeometry(native_fighters[side],shape,&vif,&n);if(!rc)faces++;else if(rc!=DR_NOT_FOUND)e=rc;}
        xsdk_log_write("fighter renderer inputs: side=%u error=%d CPU_textures=%u facial_geometry_sets=%u; no draw submission; face state is CPU-only",side,e,textures,faces);
        xsdk_log_write("fighter model-animation: side=%u error=%d bones=%u motions=%u empty=%u model_motions=%u frame=%u/%u; NOT spawned in combat",side,e,stats.bones,stats.motions,stats.empty_motions,stats.model_motions,frame,length);
        if(e)return e;
    }
    int retarget=XbBattleFighter_PlayRetargeted(native_fighters[0],native_fighters[1],0);
    if(!retarget)retarget=XbBattleFighter_SamplePose(native_fighters[0]);
    if(!retarget)retarget=XbBattleFighter_CalcMatrices(native_fighters[0]);
    xsdk_log_write("fighter original retarget warm-up: error=%d; character managers NOT registered",retarget);
    extern float D_002F2420[213];
    if(!retarget)retarget=XbBattleFighter_BindCommonReference(native_fighters[0],D_002F2420,213);
    if(!retarget)retarget=XbBattleFighter_PlayCommonRetargeted(native_fighters[0],native_fighters[1],0);
    if(!retarget)retarget=XbBattleFighter_SamplePose(native_fighters[0]);
    if(!retarget)retarget=XbBattleFighter_CalcMatrices(native_fighters[0]);
    xsdk_log_write("fighter common reference retarget: error=%d; eight native table windows per fighter; no combat boot",retarget);
    if(retarget)return retarget;
    int boot=XbBattleRuntime_BeginManagers(native_runtime);
    if(!boot)native_managers=1;
    XbBattleRuntimeInfo runtime_info;
    if(!boot)boot=XbBattleRuntime_GetInfo(native_runtime,&runtime_info);
    xsdk_log_write("original CPU manager boot: error=%d; BtlObj_Init(1), Save_Init, match setup, BtlChar_AllocAll(2) and BtlAiMgr_Init; Battle_Update NOT called",boot);
    if(!boot)for(unsigned side=0;side<2;side++)xsdk_log_write("original initialized fighter: side=%u HP=%d Ki=%d AI_type=%d AI_level=%d; GPU battle remains unavailable",side,runtime_info.health[side],runtime_info.ki[side],runtime_info.ai_type[side],runtime_info.ai_level[side]);
    if(!boot){
        void *banks[4]={0};size_t bytes[4]={0};unsigned ids[4]={0x14a,0x14e,0xbda,0xbdb};int audio=0;
        for(unsigned i=0;i<4 && !audio;i++)audio=read_battle_resource(ids[i],&banks[i],&bytes[i]);
        if(!audio)audio=XbBattleRuntime_BeginAudio(native_runtime,(const void *const *)banks,bytes);
        for(unsigned i=0;i<4;i++)free(banks[i]);
        if(!audio){extern int Snd_QueuePlay(int,unsigned,unsigned,unsigned,int,int);native_audio=1;audio=Snd_QueuePlay(16,0,96,64,0,0)<0?-894:0;}
        xsdk_log_write("original audio diagnostic: error=%d; four banks and side-0 voice request; confirm audible output on console",audio);
        if(!audio){int stage=XbBattleRuntime_BeginStage(native_runtime,native_stage);
            native_stage_running=!stage;if(stage)audio=stage;
            xsdk_log_write("original stage init/reset: error=%d; native geometry and original File_Init/ADX players; no Battle_Init or GPU draw",stage);
            if(!stage){int scene=XbBattleRuntime_BeginScene(native_runtime);native_scene_running=!scene;
                xsdk_log_write("original camera/effect scene Init: error=%d; GPU scene drawing and Battle_Update still blocked",scene);
                if(!scene){unsigned endpoints=0;int camera=XbBattleStage_CheckCameras(native_stage,&endpoints);
                    xsdk_log_write("original stage intro cameras: error=%d endpoints=%u; CPU evaluation only",camera,endpoints);
                    if(camera)scene=camera;
                    if(!scene){unsigned textures=0,groups=0;int effects=XbBattleStage_CheckEffects(native_stage,&textures,&groups);
                        xsdk_log_write("original internal stage effects: error=%d textures=%u mesh_groups=%u; CPU binding only",effects,textures,groups);
                        if(effects)scene=effects;
                        if(!scene){unsigned meshes=0,triangles=0;int surface=XbBattleStage_CheckSurfaces(native_stage,&meshes,&triangles);
                            xsdk_log_write("original stage surfaces: error=%d meshes=%u triangles=%u; original CPU scene, no GPU draw",surface,meshes,triangles);
                            if(surface)scene=surface;
                            if(!scene){unsigned cameras=0,points=0;int all=XbBattleStage_CheckAllCameras(&cameras,&points);
                                xsdk_log_write("original camera evaluator across 39 stages: error=%d cameras=%u endpoints=%u; CPU only",all,cameras,points);
                                if(all)scene=all;}}}}



                if(!scene){int chars=XbBattleRuntime_BeginSceneCharacters(native_runtime);
                    xsdk_log_write("original effect character tasks: error=%d; ten prepared technique packs bound, no shot requests or GPU fight draw",chars);
                    if(chars)audio=chars;}
                else audio=scene;}
        }
        boot=audio;
    }
    return boot;
}
#endif
static int test_stage_collision(void)
{
    dr_span span;uint8_t *pack=NULL;DrStageCollision view;unsigned triangles=0;
    int error=XbDisc_FindFile("pzs3us1/00368.bin",&span);
    if(!error && (!span.size || span.size>8*1024*1024))error=DR_STAGE_BOUNDS;
    if(!error){pack=malloc((size_t)span.size);if(!pack)error=DR_STAGE_MEMORY;}
    if(!error)error=XbDisc_ReadSpan(&span,0,pack,(size_t)span.size);
    if(!error)error=dr_stage_collision_open(pack,(size_t)span.size,&view);
    if(!error){
        const float min[3]={-1e10f,-1e10f,-1e10f},max[3]={1e10f,1e10f,1e10f};int result;
        error=dr_stage_collision_query(&view,0,-1,min,max,stage_count_triangle,&triangles,&result);
        xsdk_log_write("stage-collision: error=%d zones=%u meshes=%u nodes=%u leaves=%u zone0_triangles=%u bounds=%f,%f,%f; NOT installed in battle",
            error,view.zones,view.meshes,view.nodes,view.polygons,triangles,(double)view.radius,(double)view.top,(double)view.bottom);
    }else xsdk_log_write("stage-collision: refused error=%d; NOT installed in battle",error);
#ifdef DR_ENGINE_LINKED
    if(!error){
        error=XbBattleStage_PrepareCollision(pack,(size_t)span.size,&native_stage);
        if(!error)error=XbBattleStage_PrepareStarts(native_stage,pack,(size_t)span.size);
        if(!error)error=XbBattleStage_PrepareParameters(native_stage,pack,(size_t)span.size);
        XbStageGeometryInfo geometry={0};
        if(!error)error=XbBattleStage_PrepareGeometry(native_stage,pack,(size_t)span.size,&geometry);
        xsdk_log_write("native stage geometry: error=%d groups=%u materials=%u meshes=%u parts=%u nodes=%u cells=%u textures=%u; GPU not submitted",error,geometry.groups,geometry.materials,geometry.meshes,geometry.parts,geometry.nodes,geometry.cells,geometry.textures);
        if(!error)error=XbBattleStage_InstallCollision(native_stage);
        for(unsigned side=0;!error && side<2;side++){
            float pos[4],rot[4];int zone=-1;
            error=XbBattleStage_GetPlacement(native_stage,side,0,pos,rot,&zone);
            xsdk_log_write("native grounded start: side=%u error=%d zone=%d position=%f,%f,%f yaw=%f; stage NOT_READY",side,error,zone,(double)pos[0],(double)pos[1],(double)pos[2],(double)rot[1]);
        }
        if(!error){const float min[3]={-1e10f,-1e10f,-1e10f},max[3]={1e10f,1e10f,1e10f};int result;triangles=0;
            error=XbBattleStage_QueryCollision(native_stage,0,min,max,stage_count_triangle,&triangles,&result);}
        xsdk_log_write("native stage collision: error=%d zone0_triangles=%u; gBtlStage installed with NOT_READY; combat remains blocked",error,triangles);
        if(error && native_stage){
            (void)XbBattleStage_UninstallCollision(native_stage);
            if(!XbBattleStage_FreeCollision(native_stage))native_stage=NULL;
        }
    }
#endif
    free(pack);return error;
}
static int original_loading_opened;
static int original_inject_completed_frame_error;
static unsigned original_loading_frames;
static int original_loading_present(void){
    extern void Gfx_Init(void),Load_DrawScreen(void);
    extern int dr_gs_backend_display(uint32_t,int,int);
    if(!original_loading_opened){
        int e=dr_capture_original_open(display_width,display_height);if(e)return e;
        original_loading_opened=1;Gfx_Init();
        {extern int gGsMainFbp;gGsMainFbp=0x70;}
        e=dr_gs_backend_display(0x70,0,0);if(e)return e;
    }
    uint32_t address;NTSTATUS status=xsdk_present_begin(&address,5000);
    if(!NT_SUCCESS(status))return -950;
    xsdk_gpu_set_target(display_width,display_height);
    Load_DrawScreen();int e=dr_capture_original_submit();
    xsdk_gpu_resolve(address,display_width,display_height,0xff000000);
    xsdk_present_end();
    status=xsdk_gpu_wait(xsdk_gpu_fence(),5000);
    if(!e && !NT_SUCCESS(status))e=-950;
    if(!original_loading_frames || e)xsdk_log_write("%s: D46 original Load_DrawScreen -> DMA/VIF/GIF -> gs_draw -> Xenos GPU submit result=%d; not battle",e?"FAIL":"PASS",e);
    original_loading_frames++;return e;
}
static uint32_t original_frame_address;
static int original_full_present(int phase){
    extern void Gfx_Init(void);
    if(phase==0){
        if(!original_loading_opened){int e=dr_capture_original_open(display_width,display_height);if(e)return e;original_loading_opened=1;Gfx_Init();}
        return 0;
    }
    if(phase==1){
        NTSTATUS s=xsdk_present_begin(&original_frame_address,5000);
        if(!NT_SUCCESS(s))return -950;
        xsdk_gpu_set_target(display_width,display_height);return 0;
    }
    int e=dr_capture_original_submit();
    xsdk_gpu_resolve(original_frame_address,display_width,display_height,0xff000000);xsdk_present_end();
    if(!NT_SUCCESS(xsdk_gpu_wait(xsdk_gpu_fence(),5000)))e=-950;
    if(!e && original_inject_completed_frame_error){
        extern int BtlObj_GetCount(void);
        /* Fixed no-script profile creates its two objects only AFTER the
         * blocking loading screen. Inject after the completed GPU wait. */
        if(BtlObj_GetCount()==2){original_inject_completed_frame_error=0;e=-956;xsdk_log_write("D46 injected completed-frame error=-956 after Init/draw and successful GPU wait; recovery diagnostic only");}
    }
    original_loading_frames++;return e;
}
static int original_cpu_present(int phase){
    if(phase==0)return original_full_present(0);
    if(phase==1)return 0;
    return dr_capture_original_decode();
}
static void test_typed_data(void)
{
    if(ui.selection<0 || ui.selection>=ui.count) {
        snprintf(ui.modal,sizeof(ui.modal),"Nenhuma ISO encontrada para o teste de dados.");return;
    }
#ifdef DR_ENGINE_LINKED
    release_native_stage();
    if(native_stage){snprintf(ui.modal,sizeof(ui.modal),"Falha ao liberar o cenário anterior. Consulte default.log.");return;}
#endif
    int error=XbDisc_Mount(ui.paths[ui.selection]);
#ifdef DR_ENGINE_LINKED
    if(transport_only){
        if(!error)error=XbTransportProbe_Run();
        if(!error && transport_model_requested)transport_model_pending=1;
        if(!error && !transport_model_requested && !loading_preview_texture){int preview=loading_preview_open();xsdk_log_write("D46 loading asset preview open=%d",preview);}
        snprintf(ui.modal,sizeof(ui.modal),"Transporte estrito D46: erro %d.\nConsulte default.log.\nEste modo não carrega a sessão da luta.",error);
        return;
    }
#endif
    if(!error){
        unsigned flags=XBTD_ALLOW_PARTIAL;
#ifdef DR_ENGINE_LINKED
        /* Reapplying DATA would erase live heap pointers while Heap_Init's
         * private ready flag remains set. Subsequent runs validate only. */
        if(fresh_boot_requested)flags=0;
        if(native_typed_values_loaded)flags|=XBTD_VALIDATE_ONLY;
#endif
        error=XbTypedData_LoadFromDisc(&gXbTypedMainCatalog,flags);
#ifdef DR_ENGINE_LINKED
        if(!error)native_typed_values_loaded=1;
#endif
    }
    xsdk_log_write("%s: typed native-data load error=%d regions=%u ops=%u complete=%u data_stripped=%u; combat BLOCKED",
        error?"FAIL":"PASS",error,(unsigned)gXbTypedMainCatalog.region_count,
        (unsigned)gXbTypedMainCatalog.op_count,gXbTypedMainCatalog.complete,(unsigned)gPortDataStripped);
#ifdef DR_ENGINE_LINKED
    if(!error && !fresh_boot_requested){
        int transport=XbTransportProbe_Run();
        xsdk_log_write("%s: D46 full-file original transport probe result=%d",transport?"FAIL":"PASS",transport);
        if(transport)error=transport;
    }
#endif
 #ifdef DR_ENGINE_LINKED
    if(fresh_boot_requested){
        if(!error && fresh_boot_requested==4)error=XbFreshBoot_SetPresentation(original_loading_present);
        if(!error && fresh_boot_requested==3)error=XbFreshBoot_RecoveryProbe();
        for(unsigned cycle=0;!error && cycle<(fresh_boot_requested==8?2u:fresh_boot_requested>=6?1u:fresh_boot_requested==5?3u:2u);cycle++){
            xsdk_log_write("D46 fresh session cycle=%u mode=%d",cycle+1,fresh_boot_requested);
            if((fresh_boot_requested==18 || fresh_boot_requested==20)){xsdk_log_write("D46 CPU-only action diagnostic: original producers/decode; NO GPU replay or presentation");error=XbFreshBoot_SetOriginalLoading((int)cycle,original_cpu_present);}else
            if(fresh_boot_requested>=5)error=XbFreshBoot_SetOriginalLoading((int)cycle,original_full_present);
            if(fresh_boot_requested==8 && cycle==0)original_inject_completed_frame_error=1;
            if(!error)error=fresh_boot_requested>=19?XbFreshBoot_ChargeCheck():fresh_boot_requested>=17?XbFreshBoot_ActionFastCheck():fresh_boot_requested==16?XbFreshBoot_ActionCheck():fresh_boot_requested==15?XbFreshBoot_BattleCheck():fresh_boot_requested==14?XbFreshBoot_BattleProbe():fresh_boot_requested==13?XbFreshBoot_FightProbe():fresh_boot_requested==12?XbFreshBoot_StageProbe():fresh_boot_requested==11?XbFreshBoot_FighterProbe():fresh_boot_requested==10?XbFreshBoot_IntroProbe():fresh_boot_requested==9?XbFreshBoot_FramesProbe():(fresh_boot_requested==7 || (fresh_boot_requested==8 && cycle==0))?XbFreshBoot_FrameProbe():(fresh_boot_requested==6 || fresh_boot_requested==8)?XbFreshBoot_InitProbe():fresh_boot_requested>=2?XbFreshBoot_LoadProbe():XbFreshBoot_Probe();
            if(!error && fresh_boot_requested!=18 && fresh_boot_requested!=20 && (fresh_boot_requested==7 || fresh_boot_requested>=9 || (fresh_boot_requested==8 && cycle==0))){
                xsdk_log_write("D46 frame submitted; holding presented image 3 seconds for external capture; not a continuous battle loop");
                xsdk_thread_sleep(3000);
            }
            if(fresh_boot_requested==8 && cycle==0 && error==-955){
                int clean=XbFreshBoot_SetOriginalLoading(-1,NULL);
                if(!clean && original_loading_opened){clean=dr_capture_demo_close();if(!clean)original_loading_opened=0;}
                xsdk_log_write("%s: D46 rejected frame recovery gate/backend=%d; next cycle must Init without executable restart",clean?"FAIL":"PASS",clean);
                if(!clean)error=0;
            }
        }
        if(fresh_boot_requested>=4){
            int retained=0;
            if(fresh_boot_requested>=5){retained=XbFreshBoot_SetOriginalLoading(-1,NULL);if(!error)error=retained;}
            int detach=XbFreshBoot_SetPresentation(NULL);if(!error)error=detach;
            if(!retained && !detach && original_loading_opened){int close=dr_capture_demo_close();if(!close)original_loading_opened=0;if(!error)error=close;}
            if(retained || detach)xsdk_log_write("FAIL: original session still owns resources; backend retained; restart executable");
            xsdk_log_write("D46 original loading presentation frames=%u result=%d",original_loading_frames,error);
        }
        if(error)snprintf(ui.modal,sizeof(ui.modal),"Batalha experimental interrompida: %d. Consulte default.log.",error);
        else snprintf(ui.modal,sizeof(ui.modal),"Sessão experimental encerrada. Consulte default.log. O jogo completo ainda está em desenvolvimento.");
        return;
    }
 #endif
    int collision_error=error?error:test_stage_collision();
#ifdef DR_ENGINE_LINKED
    if(!error && !collision_error)collision_error=prepare_fighter_models();
#endif
    if(error)snprintf(ui.modal,sizeof(ui.modal),"Dados recusados: erro %d.\nNenhuma tabela alterada pelo carregador.\nConsulte default.log. Luta bloqueada.",error);
    else snprintf(ui.modal,sizeof(ui.modal),"Dados tipados: %u operações aplicadas.\nRegiões: %u · manifesto completo: %u\n\nApenas campos confirmados foram escritos.\nPreparação nativa: erro %d (0 = diagnóstico OK).\nO boot da luta permanece bloqueado.",
        (unsigned)gXbTypedMainCatalog.op_count,(unsigned)gXbTypedMainCatalog.region_count,gXbTypedMainCatalog.complete,collision_error);
}
#endif
static void settings_load(void)
{
    unsigned char data[8]; FILE *f=fopen("game:/demo.cfg","rb"); if(!f) return;
    size_t n=fread(data,1,sizeof(data),f);fclose(f);
    if(n==sizeof(data) && !memcmp(data,"DR02",4) && data[4]<=1 && data[5]<=1 && data[6]<=1) {
        ui.high_contrast=data[4];ui.fast_repeat=data[5];ui.show_help=data[6];
    }
}
static void settings_save(void)
{
    unsigned char data[8]={'D','R','0','2',ui.high_contrast,ui.fast_repeat,ui.show_help,0};
    FILE *f=fopen("game:/demo.cfg","wb");
    if(!f) {snprintf(ui.modal,sizeof(ui.modal),"Não foi possível salvar as opções.\nVerifique se a pasta permite gravação.");return;}
    bool ok=fwrite(data,1,sizeof(data),f)==sizeof(data);
    if(fclose(f)!=0) ok=false;
    if(!ok) snprintf(ui.modal,sizeof(ui.modal),"Falha ao salvar as opções.\nConsulte o log e tente novamente.");
    xsdk_log_write("%s: save demo settings",ok?"PASS":"FAIL");
}
static void check_rendered_frame(uint32_t address)
{
    NTSTATUS s=xsdk_gpu_wait(xsdk_gpu_fence(),1000);
    bool valid=NT_SUCCESS(s);
    if(valid) {
        XsdkTexture frame;
        s=xsdk_texture_init(&frame,XSDK_FORMAT_A8R8G8B8,display_width,display_height,1,true);
        valid=NT_SUCCESS(s);
        if(valid) {
            xsdk_texture_attach(&frame,xsdk_present_pixels(address));
            static uint32_t pixels[1280];
            xsdk_texture_download_rows(&frame,0,72,1,pixels,sizeof(pixels));
            uint32_t pixel=__builtin_bswap32(pixels[60]);
            valid=pixel==0xFFFF8A1F;
            xsdk_log_write("render: hero tile pixel %08X",(unsigned)pixel);
        }
    }
    xsdk_log_write("%s: interface first frame orange tile",valid?"PASS":"FAIL");
    first_frame=valid;
}
/* One optional, explicit hardware probe after the interface's first pixel
 * check. Never enters Game_Main or combat. Strict tokens avoid typo fallback. */
static int startup_test_load(void)
{
    FILE *f=fopen("game:/startup_test.txt","r");
    if(!f)return DEMO_NONE;
    char kind[32],extra[2];int n=fscanf(f,"%31s %1s",kind,extra);fclose(f);
    if(n==1 && !strcmp(kind,"menu"))return DEMO_SCAN;
    if(n==1 && !strcmp(kind,"graphics"))return DEMO_GRAPHICS_TEST;
    if(n==1 && !strcmp(kind,"model-cache-test")){body_cache_autotest=1;return DEMO_BODY_TEST;}
    if(n==1 && !strcmp(kind,"model"))return DEMO_BODY_TEST;
    /* D51: explicit hardware graphics probe. Runs at tick 2, after the
     * interface's first pixel check, and presents its own frames. */
    if(n==1 && !strcmp(kind,"d51-gfx-probe")){d51_gfx_probe=1;return DEMO_NONE;}
    if(n==1 && !strcmp(kind,"d51-gfx-probe-720")){d51_gfx_probe=2;return DEMO_NONE;}
    /* D52: loopback UDP lobby self-test. Proves the protocol on this console's
     * local stack; the two-console session still needs a physical test. */
    if(n==1 && !strcmp(kind,"d52-lobby-selftest")){d52_lobby_selftest=1;return DEMO_NONE;}
#ifdef DR_TYPED_CATALOG
    if(n==1 && !strcmp(kind,"typed-data"))return DEMO_TYPED_TEST;
#ifdef DR_ENGINE_LINKED
#ifdef DR_XENIA_DISPLAY
    if(n==1 && !strcmp(kind,"original-charge-cpu")){fresh_boot_requested=20;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"original-charge-check")){fresh_boot_requested=19;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"original-actions-cpu")){fresh_boot_requested=18;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"original-actions-fast")){fresh_boot_requested=17;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"original-actions-check")){fresh_boot_requested=16;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"original-battle-check")){fresh_boot_requested=15;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"original-battle")){fresh_boot_requested=14;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"original-fight")){fresh_boot_requested=13;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"original-stage")){fresh_boot_requested=12;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"original-fighter")){fresh_boot_requested=11;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"original-intro")){fresh_boot_requested=10;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"original-frame")){fresh_boot_requested=7;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"original-frames")){fresh_boot_requested=9;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"original-recovery")){fresh_boot_requested=8;return DEMO_TYPED_TEST;}
#else
    /* Console: CPU-only diagnostics (no GPU replay, no present) to bisect the ki-charge crash. */
    if(n==1 && !strcmp(kind,"original-charge-cpu")){fresh_boot_requested=20;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"original-actions-cpu")){fresh_boot_requested=18;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"original-battle")){fresh_boot_requested=14;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"original-fight")){fresh_boot_requested=13;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"original-stage")){fresh_boot_requested=12;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"original-fighter")){fresh_boot_requested=11;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"original-frames")){fresh_boot_requested=9;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"original-frame")){xsdk_log_write("BLOCKED: original-frame is an emulator-only investigation; native combat DMA path incomplete, NOT_READY=2");return DEMO_NONE;}
#endif
    if(n==1 && !strcmp(kind,"original-init")){fresh_boot_requested=6;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"original-load")){fresh_boot_requested=5;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"fresh-gpu")){fresh_boot_requested=4;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"fresh-recovery")){fresh_boot_requested=3;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"fresh-load")){fresh_boot_requested=2;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"fresh-boot")){fresh_boot_requested=1;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"transport-model")){transport_only=1;transport_model_requested=1;return DEMO_TYPED_TEST;}
    if(n==1 && !strcmp(kind,"transport")){transport_only=1;return DEMO_TYPED_TEST;}
#endif
#endif
    xsdk_log_write("FAIL: unsupported startup_test.txt diagnostic token");
    return DEMO_NONE;
}
/* D52: stable names so default.log can be grepped for lobby events. */
static const char *lobby_role_name(const DrLobby *l)
{
    if(l->state==LOBBY_OFF) return "none";
    return l->host?"host":"client";
}
static int lobby_player_count(const DrLobby *l)
{
    if(l->state==LOBBY_OFF) return 0;
    return l->state==LOBBY_PAIRED?2:1;
}
/* Map the demo's pad bits onto the D52-2 input word. The demo already folds
 * the left stick into DEMO_UP/DOWN/LEFT/RIGHT, so one word carries both. */
static uint32_t demo_pad_to_input(uint16_t b)
{
    uint32_t i=0;
    if(b&DEMO_LEFT)  i|=1u<<0;
    if(b&DEMO_RIGHT) i|=1u<<1;
    if(b&DEMO_UP)    i|=1u<<2;
    if(b&DEMO_DOWN)  i|=1u<<3;
    if(b&DEMO_A)     i|=1u<<4;
    if(b&DEMO_B)     i|=1u<<5;
    if(b&DEMO_X)     i|=1u<<6;
    if(b&DEMO_Y)     i|=1u<<7;
    return i;
}
static const char *lobby_event_name(int previous,int current,const char *message)
{
    if(current==LOBBY_PAIRED) return "connected";
    if(previous==LOBBY_PAIRED && current==LOBBY_HOST)
        return strstr(message,"desconectado")?"timeout":"disconnected";
    if(previous!=LOBBY_OFF && current==LOBBY_OFF)
        return (strstr(message,"perdida")||strstr(message,"Nenhum host"))?"timeout":"closed";
    if(current==LOBBY_HOST) return "hosting";
    if(current==LOBBY_SEARCH) return "searching";
    if(current==LOBBY_JOIN) return "joining";
    return "idle";
}
int main(void)
{
    XbPlatform_Install();
    DrLobby lobby;dr_lobby_init(&lobby);int logged_lobby_state=LOBBY_OFF;
    uint16_t logged_lobby_peer=0;
    demo_init(&ui);settings_load();
#ifdef DR_ENGINE_LINKED
    extern int Battle_Main(void);
    extern volatile uint32_t gPortDataStripped;
    xsdk_log_write("DragonRage360 D46: native diagnostic; Battle_Main linked at %p",(void *)(uintptr_t)Battle_Main);
    xsdk_log_write("engine: data_stripped=%u; combat boot BLOCKED: full Battle_Load/Battle_Init and scene presentation remain incomplete",(unsigned)gPortDataStripped);
#else
    xsdk_log_write("DragonRage360 D46: component diagnostic; Battle_Main not linked in this build.");
#endif
    xsdk_log_write("%s: BPE decode + animation endian synthetic self-test",dr_resource_selftest()?"PASS":"FAIL");
    xsdk_log_write("%s: bounded BIN + motion + skeleton synthetic self-test",dr_container_selftest()?"PASS":"FAIL");
    xsdk_log_write("%s: GS texture address synthetic self-test",dr_gs_texture_selftest()?"PASS":"FAIL");
    xsdk_log_write("%s: original VU reference synthetic two-bone microprogram",dr_vu1_selftest()?"PASS":"FAIL");
    NTSTATUS status=xsdk_gpu_open();
    if(!NT_SUCCESS(status)) {xsdk_log_write("FAIL: gpu_open 0x%08X",(unsigned)status);return 1;}
    xsdk_gpu_set_depth_clear(0,0); /* Reference GS depth is reversed (greater is nearer). */
#ifdef DR_XENIA_DISPLAY
    if(dr_xenos_set_wait_timeout(30000)){xsdk_log_write("FAIL: Xenia GPU wait policy");return 1;}
    xsdk_log_write("GPU wait policy: Xenia software rasterizer 30000 ms; console default remains 1000 ms");
    xsdk_log_write("display policy: explicit Xenia test; retaining kernel viewport and gamma LUT");
    status=xsdk_present_open(XSDK_DISPLAY_KERNEL_KEEP_LUT,2,1,&display_width,&display_height);
#else
    status=xsdk_present_open(XSDK_DISPLAY_720P,2,1,&display_width,&display_height);
#endif
    if(!NT_SUCCESS(status)) {xsdk_log_write("FAIL: present_open 0x%08X",(unsigned)status);return 1;}
    status=xsdk_text_open();
    if(!NT_SUCCESS(status)) {xsdk_log_write("FAIL: text_open 0x%08X",(unsigned)status);xsdk_present_close();return 1;}
    xsdk_log_write("PASS: display and text opened %ux%u",(unsigned)display_width,(unsigned)display_height);
    bool graphics_mode=false,body_mode=false,capture_mode=false;unsigned graphics_frames=0;int body_face=-1,body_request=-2;unsigned body_loading_shown=0,body_auto_step=0;uint64_t body_auto_next=0;
    int startup_test=startup_test_load();
    if(d51_gfx_probe)
        xsdk_log_write("startup: D51 gfx probe requested token=%s; will run after the interface pixel check",
                       d51_gfx_probe==2?"d51-gfx-probe-720":"d51-gfx-probe");
    if(d52_lobby_selftest)
        xsdk_log_write("startup: D52 lobby selftest requested token=d52-lobby-selftest; will run after the interface pixel check");
    xsdk_log_write("startup: requested action=%d; automatic probe after interface pixel check",startup_test);
    uint16_t last=0,repeat_direction=0;uint64_t next_repeat=0;bool running=true;
    for(unsigned tick=0;running;tick++) {
        if(body_request>=-1 && body_loading_shown>=2){
            body_face=body_request;body_request=-2;body_loading_shown=0;
            body_mode=open_body_demo(body_face)==0;body_auto_next=now_ms()+300;
        }
        INPUT_STATE input={0}; unsigned port;
        ui.connected=false;
        for(port=0;port<4;port++) if(XamInputGetState(port,0,&input)==ERROR_SUCCESS) {ui.connected=true;break;}
        if(!ui.connected) memset(&input,0,sizeof(input));
        ui.buttons=input.gamepad.buttons;ui.lx=input.gamepad.thumb_lx;ui.ly=input.gamepad.thumb_ly;
        ui.rx=input.gamepad.thumb_rx;ui.ry=input.gamepad.thumb_ry;
        ui.lt=input.gamepad.left_trigger;ui.rt=input.gamepad.right_trigger;
        uint16_t buttons=ui.buttons;
        if(ui.ly>16000) buttons|=DEMO_UP;
        if(ui.ly< -16000) buttons|=DEMO_DOWN;
        if(ui.lx< -16000) buttons|=DEMO_LEFT;
        if(ui.lx>16000) buttons|=DEMO_RIGHT;
        uint16_t pressed=buttons&~last;
        uint16_t direction=buttons&15;
        uint64_t now=now_ms();
        if(direction!=repeat_direction) {repeat_direction=direction;next_repeat=now+380;}
        else if(direction && now>=next_repeat) {pressed|=direction;next_repeat=now+(ui.fast_repeat?90:160);}
        last=buttons;
        if(pressed&DEMO_A) confirm_seen=true;
        if(pressed&DEMO_B) back_seen=true;
        if(pressed&15) navigation_seen=true;
        if(body_mode&&body_cache_autotest&&body_auto_step<9&&now>=body_auto_next){
            pressed|=DEMO_X;body_auto_step++;body_auto_next=now+300;
            xsdk_log_write("D46 automatic cache diagnostic: selection %u/9",body_auto_step);
        }
        int action=DEMO_NONE;
#if defined(DR_ENGINE_LINKED) && defined(DR_TYPED_CATALOG)
        if(loading_preview_texture){
            if(pressed&(DEMO_B|DEMO_BACK)){int close=dr_xenos_close();if(!close)loading_preview_texture=0;}
        } else
#endif
        if(body_request>=-1){if(pressed&(DEMO_B|DEMO_BACK)){body_request=-2;body_loading_shown=0;}}
        else if(capture_mode) {
            if(pressed&(DEMO_B|DEMO_BACK)) {
                int error=dr_capture_demo_close();if(!error)capture_mode=false;
                xsdk_log_write("capture integration close: %d",error);
            }
        } else if(body_mode) {
            if(pressed&(DEMO_B|DEMO_BACK)) {
                int error=dr_body_demo_close();if(!error)body_mode=false;
                xsdk_log_write("body diagnostic close: %d",error);
            } else if(pressed&DEMO_X) {
                int next=dr_body_demo_next_face(body_face);DrBodyPipelineStats stats;
                int error=dr_body_demo_select_face(next,&stats);DrBodyDemoProfile p;dr_body_demo_profile(&p);
                xsdk_log_write("D46 expression: face=%d result=%d cache_hit=%u time_ms=%.3f model_reads=%u body_builds=%u face_builds=%u uploads=%u facial_vertices=%u; no body rebuild",next,error,p.face_cache_hit,p.expression_us/1000.0,p.model_reads,p.body_builds,p.face_builds,p.texture_uploads,p.face_vertices);
                if(!error){body_face=next;body_draw_report_pending=1;body_probe_frames=0;}
            }
        } else if(graphics_mode) {
            if(pressed&DEMO_Y) {
                int error=dr_graphics_demo_close();
                if(!error) {graphics_mode=false;error=dr_capture_demo_open(display_width,display_height);capture_mode=!error;}
                xsdk_log_write("capture integration open: %d",error);
            }
            if(pressed&(DEMO_B|DEMO_BACK)) {
                int error=dr_graphics_demo_close();graphics_mode=false;
                xsdk_log_write("graphics diagnostic close: %d",error);
            }
        } else action=demo_input(&ui,pressed);
        if(tick==2 && startup_test!=DEMO_NONE && running && !ui.exit_prompt &&
           !graphics_mode && !body_mode && !capture_mode) {
            action=startup_test;startup_test=DEMO_NONE;
            xsdk_log_write("startup: executing diagnostic action=%d",action);
            if(action==DEMO_BODY_TEST || action==DEMO_TYPED_TEST)scan_isos();
        }
        /* D51: one explicit probe, after the interface's first pixel check.
         * It presents its own frames and then returns to the UI loop. */
        if(d51_gfx_probe && tick>=2 && running && !ui.exit_prompt &&
           !graphics_mode && !body_mode && !capture_mode) {
            int mode=d51_gfx_probe;d51_gfx_probe=0;
            uint32_t probe_width=mode==2?1280u:640u,probe_height=mode==2?720u:480u;
            xsdk_log_write("D51 gfx probe: executing token=%s logical=%ux%u front_buffer=%ux%u frames=180",
                           mode==2?"d51-gfx-probe-720":"d51-gfx-probe",
                           (unsigned)probe_width,(unsigned)probe_height,
                           (unsigned)display_width,(unsigned)display_height);
            int probe=D50_GfxPresentRun(probe_width,probe_height,display_width,display_height,180);
            xsdk_log_write("%s: D51 gfx probe returned %d; NOT_READY=2; physical confirmation still pending",
                           probe?"FAIL":"PASS",probe);
            if(probe) snprintf(ui.modal,sizeof(ui.modal),
                               "Probe D51 falhou: %d.\nConsulte default.log.",probe);
        }
        /* D52: one loopback lobby self-test after the interface pixel check.
         * It never contacts a real peer; the two-console session is separate. */
        if(d52_lobby_selftest && tick>=2 && running && !ui.exit_prompt &&
           !graphics_mode && !body_mode && !capture_mode) {
            d52_lobby_selftest=0;
            xsdk_log_write("D52 lobby selftest: starting loopback UDP host+client; two-console session still needs a physical test");
            int error=D52_LobbySelfTest();
            xsdk_log_write("%s: D52 lobby selftest loopback returned %d; NOT_READY=2; physical two-console test still pending",
                           error?"FAIL":"PASS",error);
            if(error) snprintf(ui.modal,sizeof(ui.modal),
                               "Autoteste do lobby falhou: %d.\nConsulte default.log.",error);
        }
#ifdef DR_TYPED_CATALOG
        if(action==DEMO_TYPED_TEST){test_typed_data();
#ifdef DR_ENGINE_LINKED
            xsdk_log_write("typed-data diagnostic returned to UI loop; scene=%d stage=%d",native_scene_running,native_stage_running);
#endif
        }
#endif
#ifdef DR_ENGINE_LINKED
        if(transport_model_pending){transport_model_pending=0;body_request=-1;body_loading_shown=0;}
#endif
        if(action==DEMO_GRAPHICS_TEST) {
            int error=dr_graphics_demo_open();
            xsdk_log_write("%s: native GS MRT/DATE/FBA diagnostic open, error=%d",error?"FAIL":"PASS",error);
            graphics_mode=error==0;graphics_frames=0;
            if(error) snprintf(ui.modal,sizeof(ui.modal),"Falha ao preparar o diagnóstico gráfico: %d.\nConsulte default.log.",error);
        }
        if(action==DEMO_BODY_TEST) {body_request=-1;body_loading_shown=0;}
        if(action==DEMO_EXIT) running=false;
        if(action==DEMO_SCAN) scan_isos();
        if(action==DEMO_VERIFY) verify_iso();
        if(action==DEMO_PLAY){
#if defined(DR_ENGINE_LINKED) && defined(DR_TYPED_CATALOG)
            if(ui.selection>=0 && ui.selection<ui.count){
                ui.selected_iso=ui.selection;fresh_boot_requested=14;
                test_typed_data();fresh_boot_requested=0;last=0;repeat_direction=0;
                xsdk_log_write("D46 library play returned to menu");
            }
#else
            snprintf(ui.modal,sizeof(ui.modal),"Esta compilação não inclui o caminho de batalha.");
#endif
        }
        if(action==DEMO_RESOURCE_TEST) test_iso_resource();
        if(action==DEMO_SAVE) settings_save();
        if(action==DEMO_NET_CHECK) demo_net_check(ui.modal,sizeof(ui.modal));
        if(action==DEMO_LOBBY_STOP) {
            int was=lobby.state;
            dr_lobby_stop(&lobby);
            if(was!=LOBBY_OFF) xsdk_log_write("lobby event=stop role=local players=0: user closed the session");
        }
        if(action==DEMO_LOBBY_HOST || action==DEMO_LOBBY_SEARCH || action==DEMO_LOBBY_DIRECT) {
            struct in_addr address;int error=xsdk_net_address(&address);
            char ip[16]={0};unsigned net_port=7371;int valid=1;
            if(action==DEMO_LOBBY_DIRECT) {
                /* Accept "192.168.1.10" (default UDP 7371) or "192.168.1.10 7371". */
                char line[64]={0};
                FILE *config=fopen("game:/lobby_host.txt","r");
                int read=config && fgets(line,sizeof line,config)?1:0;
                if(config) fclose(config);
                uint16_t parsed_port=7371;
                valid=read && dr_lobby_parse_target(line,ip,sizeof ip,&parsed_port);
                net_port=parsed_port;
            }
            if(error) {
                xsdk_log_write("lobby event=error role=none players=0: console has no IPv4");
                snprintf(ui.modal,sizeof(ui.modal),"Console sem IPv4. Confira a rede e tente novamente.");
            } else if(!valid) {
                xsdk_log_write("lobby event=error role=%s players=0: lobby_host.txt needs an IPv4 and optional UDP port",
                               action==DEMO_LOBBY_HOST?"host":"client");
                snprintf(ui.modal,sizeof(ui.modal),"Crie lobby_host.txt ao lado do XEX:\n192.168.1.10\nou 192.168.1.10 7371\nUse o IP do console host.");
            } else {
                int started=dr_lobby_start(&lobby,action==DEMO_LOBBY_HOST,ip[0]?ip:NULL,(uint16_t)net_port,(uint32_t)now,(uint32_t)now^address.s_addr);
                if(!started) {
                    xsdk_log_write("lobby event=error role=%s players=0 target=%s:%u: %s",
                                   action==DEMO_LOBBY_HOST?"host":"client",
                                   action==DEMO_LOBBY_HOST?"local":ip,(unsigned)net_port,lobby.message);
                    snprintf(ui.modal,sizeof(ui.modal),"%s\nConsulte default.log.",lobby.message);
                } else {
                    if(action==DEMO_LOBBY_HOST && lobby.fd>=0) {
                        char local[16];inet_ntop(AF_INET,&address,local,sizeof(local));
                        snprintf(lobby.message,sizeof(lobby.message),"Host %s:%u. Aguardando cliente...",local,(unsigned)net_port);
                    }
                    xsdk_log_write("lobby event=start role=%s players=1 target=%s:%u",
                                   action==DEMO_LOBBY_HOST?"host":"client",
                                   action==DEMO_LOBBY_HOST?"local":ip,(unsigned)net_port);
                }
            }
        }
        dr_lobby_tick(&lobby,(uint32_t)now);
        /* D52-2: lockstep online battle once the lobby is paired. The host is
         * player one, the client player two; both consoles number frames from
         * 1 and only advance when the input for that frame is known.
         * D53-B: with DR_HAVE_BT3XBX the bound backend is the REAL bt3xbx
         * engine (start = Battle_Init, update = Battle_StepFrame, end =
         * Battle_Term, hash over gBattleWork); every other build keeps the
         * reference adapter, which is what the host tests exercise. */
        if(lobby.state == LOBBY_PAIRED && !dr_online_battle_active(&online_battle)
           && !online_bind_failed) {
            int bound;
#ifdef DR_HAVE_BT3XBX
            bound=dr_online_battle_init_with(&online_battle,&lobby,lobby.host?0:1,
                                             &dr_bt3xbx_ops,&bt3xbx_state);
#else
            bound=dr_online_battle_init(&online_battle,&lobby,lobby.host?0:1);
#endif
            if(bound==1) {
                xsdk_log_write("D53: online battle ready role=%s side=%d backend=%s",
                               lobby.host?"host":"client",lobby.host?0:1,
                               dr_online_battle_backend(&online_battle));
            } else if(bound<0) {
                /* Never retry: Battle_Init allocates the whole battle. */
                online_bind_failed=1;
                xsdk_log_write("D53: engine backend refused to start: %s",
                               dr_engine_error(&online_battle.engine));
            }
        }
        if(dr_online_battle_active(&online_battle)) {
            uint32_t local_input=demo_pad_to_input(buttons);
            uint32_t remote_input=0;
            (void)dr_online_battle_update(&online_battle,local_input,&remote_input);
            uint32_t frame,local_hash,remote_hash;
            if(dr_online_battle_desync(&online_battle,&frame,&local_hash,&remote_hash))
                xsdk_log_write("D52-2 DESYNC frame=%u local=%08X remote=%08X; "
                               "rollback not implemented yet",
                               (unsigned)frame,(unsigned)local_hash,
                               (unsigned)remote_hash);
            /* The remote input reaches the engine through the adapter: the
             * harness calls dr_engine_update(in0,in1) once both inputs for the
             * frame are known, and the backend injects them via gXbPadPoll. */
        }
        if(dr_online_battle_active(&online_battle) && lobby.state != LOBBY_PAIRED) {
            xsdk_log_write("D52-2: online battle stopped after %u frame(s), "
                           "%u hash(es) verified",
                           (unsigned)dr_online_battle_frame(&online_battle),
                           (unsigned)dr_online_battle_matched(&online_battle));
            dr_online_battle_shutdown(&online_battle);
        }
        /* The pairing is gone, so a backend that refused to start may be tried
         * again the next time a peer connects. */
        if(lobby.state != LOBBY_PAIRED) online_bind_failed=0;
        snprintf(ui.network,sizeof(ui.network),"%s",lobby.message);
        ui.lobby_role=lobby.state==LOBBY_OFF?0:(lobby.host?1:2);
        ui.players=lobby_player_count(&lobby);
        if(lobby.state!=logged_lobby_state || lobby.peer.sin_port!=logged_lobby_peer) {
            char peer[16]="none";
            if(lobby.peer.sin_port) inet_ntop(AF_INET,&lobby.peer.sin_addr,peer,sizeof(peer));
            xsdk_log_write("lobby event=%s role=%s players=%d state=%s(%d) peer=%s:%u: %s",
                           lobby_event_name(logged_lobby_state,lobby.state,lobby.message),
                           lobby_role_name(&lobby),ui.players,
                           dr_lobby_state_name(lobby.state),lobby.state,
                           peer,(unsigned)ntohs(lobby.peer.sin_port),lobby.message);
            logged_lobby_state=lobby.state;logged_lobby_peer=lobby.peer.sin_port;
        }

        if(action==DEMO_SAVE_NEW || action==DEMO_SAVE_UNLOCK) {
            int ok=dr_save_export("game:",action==DEMO_SAVE_UNLOCK,ui.modal,sizeof(ui.modal));
            xsdk_log_write("%s: separate unlocked save export",ok?"PASS":"FAIL");
        }
        if(!running) break;
#if defined(DR_TYPED_CATALOG) && defined(DR_ENGINE_LINKED)
        if(native_stage_running){if(native_scene_running && !native_scene_frames)xsdk_log_write("native frame: entering original BtlStage_Update");
            int e=XbBattleRuntime_ServiceStage(native_runtime);if(e){xsdk_log_write("stage service failed: %d",e);native_stage_running=0;}
            else if(native_scene_running && !native_scene_frames)xsdk_log_write("native frame: original BtlStage_Update returned");}
        if(native_scene_running){if(!native_scene_frames)xsdk_log_write("native frame: entering original scene Update/PostUpdate");
            int e=XbBattleRuntime_ServiceScene(native_runtime);if(e){xsdk_log_write("scene service failed: %d",e);native_scene_running=0;}
            else {native_scene_frames++;if(native_scene_frames==1 || native_scene_frames==3 || native_scene_frames==60)
                xsdk_log_write("original effect scene CPU Update/PostUpdate: frame=%u; no scene GPU draw",native_scene_frames);}}
        if(native_audio){int e=XbBattleRuntime_ServiceAudio(native_runtime);if(e){xsdk_log_write("audio service failed: %d",e);native_audio=0;}}
#endif
        uint32_t address;
        status=xsdk_present_begin(&address,5000);
        if(!NT_SUCCESS(status)) {xsdk_log_write("FAIL: present_begin 0x%08X",(unsigned)status);break;}
        xsdk_gpu_set_target(display_width,display_height);
#if defined(DR_ENGINE_LINKED) && defined(DR_TYPED_CATALOG)
        if(loading_preview_texture){
            int error=loading_preview_draw(display_width,display_height);
            if(!loading_preview_frames || error)xsdk_log_write("D46 real loading asset GPU draw: result=%d; no battle frame",error);
            loading_preview_frames++;
            xsdk_text_draw(50,44,2,0xffe0e0ff,"RECURSO REAL DA TELA DE CARGA - D46");
            xsdk_text_draw(52,650,1,0xffe0e0ff,"Prévia da imagem da ISO - sem luta - B voltar");
        } else
#endif
        if(capture_mode) {
            int error=dr_capture_demo_draw();
            if(error) {xsdk_log_write("FAIL: actual capture draw rc=%d",error);if(!dr_capture_demo_close())capture_mode=false;}
            xsdk_text_draw(50,44,2,0xffe0e0ff,"CAPTURA ORIGINAL GS · 0.33 EXPERIMENTAL");
            xsdk_text_draw(52,100,1,0xffe0e0ff,"Captura GS / VU / sombras / contorno / tabelas por ID e Z");
            xsdk_text_draw(52,650,1,0xffe0e0ff,"Dados sintéticos · sem combate · B voltar");
        } else if(body_mode) {
            int error=dr_body_demo_draw(display_width,display_height);
            if(body_draw_report_pending||error)xsdk_log_write("%s: D46 real ISO model GPU draw: result=%d; fixed reference pose, direct Xenos viewer, not original gs_draw/Battle",error?"FAIL":"PASS",error);
            body_draw_report_pending=0;
            if(error) {
                xsdk_log_write("FAIL: real body diagnostic draw rc=%d",error);
                if(!dr_body_demo_close())body_mode=false;
            }
            xsdk_text_draw(50,44,2,0xffe0e0ff,"MODELO DA ISO · POSE DE REFERÊNCIA · D46");
            xsdk_text_draw(52,100,1,0xffe0e0ff,"Corpo e materiais reais / programa 0 da VU1");
            xsdk_text_draw(950,175,1,0xffe0e0ff,"Texturas do pacote");
            xsdk_text_draw(52,650,1,0xffe0e0ff,"Pose fixa / sem combate · X expressão · B voltar");
        } else if(graphics_mode) {
            int error=dr_graphics_demo_draw(display_width,display_height);
            if(error) {
                xsdk_log_write("FAIL: native graphics diagnostic draw, error=%d",error);
                dr_graphics_demo_close();graphics_mode=false;
            }
            xsdk_text_draw(50,44,2,0xff101020,"DIAGNÓSTICO GS · DATE/FBA · D46");
            xsdk_text_draw(52,100,1,0xff101020,"PSMT8 / paleta / MODULATE / alpha / aditivo");
            xsdk_text_draw(52,650,1,0xff101020,"Dados sintéticos · Y captura original · B voltar");
        } else if(body_request>=-1){
            xsdk_text_draw(120,300,2,0xffe0e0ff,"Carregando modelo da ISO...");
            xsdk_text_draw(120,350,1,0xffe0e0ff,"Preparando modelo e texturas. B cancela antes da preparação.");
            body_loading_shown++;
        } else demo_draw(&ui,&paint,tick);
        xsdk_gpu_resolve(address,display_width,display_height,0xFF0D0F1C);
        if(tick==1 && !graphics_mode && !body_mode && !capture_mode) check_rendered_frame(address);
        if(body_mode && ++body_probe_frames==2 && display_width==1280 && display_height==720){
            NTSTATUS ready=xsdk_gpu_wait(xsdk_gpu_fence(),1000);XsdkTexture frame;
            if(NT_SUCCESS(ready))ready=xsdk_texture_init(&frame,XSDK_FORMAT_A8R8G8B8,display_width,display_height,1,true);
            if(NT_SUCCESS(ready)){
                xsdk_texture_attach(&frame,xsdk_present_pixels(address));static uint32_t pixels[1280];
                for(unsigned i=0;i<2;i++){
                    DrBodyDemoProbe p;if(dr_body_demo_probe(i,&p))continue;
                    xsdk_texture_download_rows(&frame,0,p.y,1,pixels,sizeof(pixels));uint32_t got=__builtin_bswap32(pixels[p.x]);
                    int match=1;for(unsigned k=0;k<3;k++)if(abs((int)((got>>(k*8))&255)-(int)((p.argb>>(k*8))&255))>1)match=0;
                    xsdk_log_write("%s: D46 texture color probe %u xy=%u,%u source_RGBA=%08X expected_blended=%08X GPU=%08X tolerance=1; RGB before display LUT, not a color calibration",match?"PASS":"FAIL",i,p.x,p.y,(unsigned)p.rgba,(unsigned)p.argb,(unsigned)got);
                }
            }else xsdk_log_write("FAIL: D46 texture color readback status=%08X",(unsigned)ready);
        }
        if(graphics_mode && ++graphics_frames==2) {
            NTSTATUS ready=xsdk_gpu_wait(xsdk_gpu_fence(),1000);
            if(NT_SUCCESS(ready)) {
                XsdkTexture frame;
                ready=xsdk_texture_init(&frame,XSDK_FORMAT_A8R8G8B8,display_width,display_height,1,true);
                if(NT_SUCCESS(ready)) {
                    xsdk_texture_attach(&frame,xsdk_present_pixels(address));
                    static uint32_t pixels[1280];
                    xsdk_texture_download_rows(&frame,0,190,1,pixels,sizeof(pixels));
                    uint32_t pixel=__builtin_bswap32(pixels[190]);
                    xsdk_log_write("%s: native PSMT8/MODULATE diagnostic pixel %08X expected FFFF8A1F",
                                   pixel==0xffff8a1f?"PASS":"FAIL",(unsigned)pixel);
                }
            }
            if(!NT_SUCCESS(ready)) xsdk_log_write("FAIL: native graphics diagnostic readback status=%08X",(unsigned)ready);
        }
        xsdk_present_end();
    }
    xsdk_log_write("checks: render=%d navigate=%d A=%d B=%d ISO=%d",first_frame,navigation_seen,confirm_seen,back_seen,ui.iso_valid);
    if(capture_mode) dr_capture_demo_close();
    if(graphics_mode) dr_graphics_demo_close();
    if(body_mode) dr_body_demo_close();
#if defined(DR_ENGINE_LINKED) && defined(DR_TYPED_CATALOG)
    if(loading_preview_texture)dr_xenos_close();
    release_native_stage();
#endif
    dr_lobby_stop(&lobby);
    xsdk_text_close();xsdk_present_close();
    status=xsdk_gpu_close();
    if(!NT_SUCCESS(status)) xsdk_log_write("FAIL: gpu_close 0x%08X",(unsigned)status);
    return first_frame?0:1;
}
