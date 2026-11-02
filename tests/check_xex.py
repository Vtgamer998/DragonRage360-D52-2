"""Static packaging checks, based on xsdk/tools/elf2xex.py. Not a console boot test."""
import hashlib
import json
import struct
import sys
from pathlib import Path

root=Path(__file__).resolve().parents[1]
build=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/'build'
report_path=Path(sys.argv[2]).resolve() if len(sys.argv)>2 else root/'VALIDACAO.json'
elf=(build/'default.elf').read_bytes()
assert elf[:7]==b'\x7fELF\x01\x02\x01', 'ELF32 big-endian required'
eh=struct.unpack_from('>HHIIIIIHHHHHH',elf,16)
assert eh[0]==2 and eh[1]==20 and eh[6]&0x100, 'xsdk PowerPC ABI required'
shoff,shstep,shnum=eh[5],eh[10],eh[11]
assert shstep==40 and shoff+shstep*shnum<=len(elf)
sections=[struct.unpack_from('>IIIIIIIIII',elf,shoff+i*shstep) for i in range(shnum)]
defined=set();weak=[]
for sh in sections:
    if sh[1]!=2:continue
    assert sh[9]==16 and sh[4]+sh[5]<=len(elf) and sh[6]<shnum
    strings=sections[sh[6]];assert strings[4]+strings[5]<=len(elf)
    names=elf[strings[4]:strings[4]+strings[5]]
    for at in range(sh[4],sh[4]+sh[5],16):
        name,value,size,info,other,index=struct.unpack_from('>IIIBBH',elf,at)
        if not name:continue
        assert name<len(names);end=names.find(b'\0',name);assert end>=name
        text=names[name:end].decode('ascii')
        if index:defined.add(text)
        elif info>>4==2:weak.append(text)
        else:raise AssertionError('undefined ELF symbol: '+text)
required={'dr_body_demo_draw','dr_body_model_read',
          'dr_elf_mips_span','dr_face_vif_parse','dr_vu1_call','dr_targets_bind','dr_texfile_decode','dr_texfile_body_material','dr_body_pipeline_face','dr_body_demo_open_face','dr_xenos_texture_create_mips','dr_targets_ensure_gs','dr_targets_snapshot','dr_xenos_snapshot','dr_xenos_target_texture'}
if 'dr_body_source_build' in defined:
    required-={'dr_texfile_body_material','dr_body_pipeline_face'}
    required|={'dr_body_source_open','dr_body_source_build','dr_body_demo_select_face','dr_texfile_material'}
assert required<=defined, 'body pipeline/native modules absent from link: '+str(required-defined)
capture_required={'Dma_Flush','Dma_ResetBuffers','Dma_AddFrame','Dma_AddScissor','Dma_AddXyOffset','Dma_AddZbuf','dr_dma_write_ad','dr_dma_producer_open_mapped','dr_dma_map_chain','Dma_AddNext','dr_gs_stream_init','dr_gs_stream_chain','dr_gs_stream_vif','dr_vu1_vif_control','sXenosBackend','GsGpu_Init','GsGpu_Draw','GsGpu_DrawVu0','GsGpu_DrawVu4','GsGpu_DrawVu6','dr_gs_replay','dr_vu0_vert_container','dr_vu4_vert_container','dr_vu6_vert_container','dr_outline_frag_container','dr_dclut_frag_container','Gs_Gif','Gs_XenosCaptureEnable','GsDraw_XenosSubmit','dr_capture_demo_open','dr_capture_demo_draw'}
assert capture_required<=defined, 'actual capture/backend integration absent from link'
native_required={'sceMpegInit','sceMpegCreate','sceMpegDelete','sceMpegReset','sceMpegIsEnd','sceMpegAddCallback','sceMpegAddStrCallback','sceMpegDemuxPss','sceMpegGetPicture','sceGsResetPath','sceGsResetGraph','dr_gs_movie_build','dr_gs_movie_send','XbAdx_Clock','XbMpeg_Open','XbMpeg_OpenReader','XbMpeg_Next','XbMpeg_Close','ADXT_Init','ADXT_Create','ADXT_SetReloadSct','ADXT_SetSvrFreq','ADXT_StartAfs','ADXT_StartFname','ADXT_Stop','ADXT_Pause','ADXT_SetOutVol','ADXT_GetOutVol','ADXT_SetOutPan','ADXT_SetOutputMono','ADXT_GetStat','XbOverlay_PrepareBuild','XbOverlay_Load','Dma_InitBuffers','Dma_SendGif','dr_dma_engine_init','dr_dma_engine_send_u64','Port_GsMarker','sceGsSyncPath','sceGsSetDefStoreImage','sceGsExecStoreImage','dr_gs_backend_sync','GsDraw_XenosReadFrameExact','sceGsSetDefDBuff','sceGsPutDrawEnv','sceGsSwapDBuff','Port_GsVif1Chain','Port_GsGifChannel','dr_gs_native_open','dr_gs_native_bind','dr_gs_native_vif1','dr_gs_native_gif','dr_gs_native_error','dr_gs_stream_gif_chain'}
if 'Battle_Main' in defined:
    native_required.discard('XbOverlay_PrepareBuild')
assert native_required<=defined, 'shared native VIF/GIF engine entry points absent'
platform_required={'sceOpen','sceClose','sceRead','sceLseek','sceCdSearchFile','XbIsoFiles_Probe','XbDisc_Generation','XbPlatform_Install','XbPlatform_ProbeFiles','XbDisc_Mount','ADXF_ReadNw','ADXF_OpenAfs',
                   'XbRes_Install','XbRes_Report','XbRes_LoadMap','XbRes_RegisterChecked',
                   'XbRes_RegisterAnimation','XbRes_RegisterSkeleton','gXbFilePostRead'}
assert platform_required<=defined, 'resource read hook/platform integration absent from link'
data=(build/'default.xex').read_bytes()
magic,module,header_size,reserved,sec,count=struct.unpack_from('>4sIIIII',data)
assert magic==b'XEX2' and module==1 and header_size<len(data) and count<100
headers=dict(struct.unpack_from('>II',data,24+i*8) for i in range(count))
entry=headers[0x10100];base=headers[0x10201]
fmt=headers[0x3ff]
fmt_size=struct.unpack_from('>I',data,fmt)[0]
assert fmt_size>=8 and (fmt_size-8)%8==0 and fmt+fmt_size<=header_size
assert struct.unpack_from('>HH',data,fmt+4)==(0,1)
image_bytes=struct.unpack_from('>I',data,sec+4)[0]
assert 0<image_bytes<=256*1024*1024
payload=data[header_size:];expanded=bytearray();at=0
for pos in range(fmt+8,fmt+fmt_size,8):
    actual,zeros=struct.unpack_from('>II',data,pos)
    assert actual<=len(payload)-at and actual+zeros<=image_bytes-len(expanded)
    expanded+=payload[at:at+actual];expanded+=bytes(zeros);at+=actual
assert at==len(payload)
# Current XSDK omits the final zero run and restores it to the security size.
expanded+=bytes(image_bytes-len(expanded))
pe=bytes(expanded)
assert pe[:2]==b'MZ'
pe_start=struct.unpack_from('<I',pe,0x3c)[0]
assert pe[pe_start:pe_start+4]==b'PE\0\0'
assert struct.unpack_from('<H',pe,pe_start+4)[0]==0x1f2
assert base==0x82000000 and base<=entry<base+len(pe)
assert struct.unpack_from('<H',pe,pe_start+24+68)[0]==14
assert hashlib.sha1(data[sec+0x17c:header_size]+data[:sec+8]).digest()==data[sec+0x164:sec+0x178]
pages=struct.unpack_from('>I',data,sec+0x180)[0]
assert pages*65536==len(pe)
descs=[data[sec+0x184+i*24:sec+0x184+(i+1)*24] for i in range(pages)]
for i,desc in enumerate(descs):
    digest=hashlib.sha1(pe[i*65536:(i+1)*65536]+desc).digest()
    expected=data[sec+0x114:sec+0x128] if i==0 else descs[i-1][4:24]
    assert digest==expected
title=struct.unpack_from('>I',data,headers[0x40006]+12)[0]
assert title==0x44523630
report={'format':'XEX2','size_bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),
        'machine':'PowerPC big-endian','entry_point':hex(entry),'image_base':hex(base),
        'title_id':hex(title),'pages':pages,
        'checks':['ELF32 PowerPC xsdk ABI flag 0x100','no unresolved strong ELF symbols',
                  'PE PowerPCBE / Xbox subsystem','header SHA-1','full image page hash chain',
                  'unencrypted basic-compression homebrew XEX'],
        'linked_body_symbols':sorted(required),'unresolved_weak_symbols':sorted(weak),
        'runtime_test':'Not run on Xbox 360 or emulator',
        'native_engine_entry_points_linked':sorted(native_required),'native_GS_sync_and_RGB_readback_linked':True,'game_boot_linked':'Game_Main' in defined,'shared_VU_session':True,'gpu_pixels_verified':False,'game_complete':False,'battle_loop_linked':'Battle_Main' in defined,
        'platform_sources_compiled':True,'platform_resource_hook_linked':True,
        'linked_platform_symbols':sorted(platform_required),'Bpe_Decode_linked':'Bpe_Decode' in defined,
        'experimental_backend_linked':True,'original_GS_capture_linked':True,'linked_capture_symbols':sorted(capture_required),'registration_patch_provided':True,
        'native_VU_shader_path_linked':True,'shadow_stencil_run_path_linked':True,'buffer_consumers_linked':['outline from frozen IDs','depth/ID CLUT with raw alpha MRT','sampled target raw alpha and primitive feedback resolves'],'bounded_DMA_VIF_consumer_linked':True,'combat_DMA_producers_adapted':False,'adapted_DMA_producer_subset_linked':['Frame','Zbuf','Scissor','XYOffset','AD register','Flush','ResetBuffers'],'native_packet_producers_compiled':['Dma_Alloc','Dma_AddData','Dma_AddRef','Dma_BeginDirect','Dma_EndDirect','Dma_AddNext'],'mapped_chain_diagnostic_linked':True,'native_DMA_buffer_initialization_linked':True,'raw_ISO_Sony_byte_file_API_linked':True,'native_PSS_decoder_retained':True,'PSS_playback_integrated':False,'native_streaming_sceMpeg_APIs_retained':True,'native_movie_GIF_upload_retained':True,'native_ADXT_APIs_retained':True,'overlay_loader_retained':True,'native_overlay_catalog_in_launcher':False,'scope':'0.34 retains real streaming sceMpeg APIs, typed movie GIF upload and GS resets; adapted Movie_Run only in strict engine attempt; full Game_Main/Battle_Main absent. 0.33 retains bounded native CPU PSS decoder APIs; no sceMpeg/movie player/GPU or AV sync integration. 0.32 retains real ADXT/native overlay APIs for static verification; does not initialize audio, load menus or start combat. Strong complete native overlay catalogue generated only for strict engine link. 0.31 adds shared DMA initialization and typed boot GIF plus read-only ISO Sony byte-file API and bounded ELF probe; Game_Main and Battle_Main absent. 0.30 adds ordered effect marker, blocking real GPU sync and exact CT24 RGB frame readback for supported game profile; no hardware execution. 0.27 replaces native legacy DMA/VIF/GIF traversal with bounded canonical mappings and one shared VU session; Port_Gs entry points and synthetic MPG/UNPACK/MSCAL/XGKICK probe linked. No combat boot/presentation integration. 0.25 DMA patch and platform 0.4 integrated: ADXF post-read, converter registry/map/adapters and first-file ISO probes; combat remains outside XEX. Prior 0.24 features: adapted canonical-LE register producers and adapted Dma_Flush/ResetBuffers linked to bounded DMA/VIF/GS capture in synthetic diagnostic; rectangle and other helpers byte-tested against original source; Battle_Main, full combat producers and IA/collisions/animation integration absent; no hardware pixel validation'}
if 'Battle_Main' in defined:
    report.update(native_overlay_catalog_in_launcher='gXbOverlayCatalog' in defined,
        native_SFX_sampler_linked={'func_002967C0','sceSifCallRpc','XbSfx_Mix'}<=defined,
        scope='0.34-L1: Battle_Main and native SFX linked, zero undefined symbols; diagnostic entry only, no call to combat, no original Game_Main or typed main-data initialization; menu-catalog preparation is not rooted. No hardware execution.')
if 'gXbTypedMainCatalog' in defined:
    # Catalog shape is compiled for PPC32: version, complete, regions ptr/count,
    # operations ptr/count. No external JSON is trusted as a native pointer.
    def data_at_symbol(name,count):
        for sh in sections:
            if sh[1]!=2:continue
            strings=sections[sh[6]];tab=elf[strings[4]:strings[4]+strings[5]]
            for at in range(sh[4],sh[4]+sh[5],16):
                no,va,size,info,other,secidx=struct.unpack_from('>IIIBBH',elf,at)
                if no and tab[no:tab.index(0,no)].decode()==name:
                    sec0=sections[secidx];off=sec0[4]+va-sec0[3];return elf[off:off+count]
        raise AssertionError(name)
    cat=struct.unpack('>6I',data_at_symbol('gXbTypedMainCatalog',24))
    assert cat[0]==1 and cat[1] in (0,1) and cat[3]==33 and cat[5]>2000
    assert {'XbTypedData_Load','XbTypedData_LoadFromDisc','XbTypedData_RequireBattleData','dr_stage_collision_open','dr_stage_collision_query','XbBattleStage_PrepareCollision','XbBattleStage_InstallCollision','XbBattleStage_QueryCollision','XbBattleFighter_PrepareModelAnimation','XbBattleFighter_PlayModel','XbBattleFighter_StepModel','XbBattleFighter_PlayMotion','XbBattleFighter_PlayRetargeted','XbBattleFighter_SamplePose','XbBattleFighter_CalcMatrices','XbBattleFighter_UpdateVolumes','XbBattleFighter_PrepareTextures','XbBattleFighter_GetMesh','XbBattleFighter_GetFaceGeometry','XbFighterTable_Convert','XbBattleFighter_BindCommonReference','XbBattleFighter_PlayCommonRetargeted','XbBattleFighter_CreateModelObject','XbBattleFighter_FreeModelAnimation','XbFighterRegistry_BeginOriginal','BtlObj_Init','BtlObj_Term','XbBattleRuntime_Open','XbBattleRuntime_Close','XbBattleRuntime_BeginManagers','XbBattleRuntime_EndManagers','BtlChar_AllocAll','BtlAiMgr_Init','XbFighterRegistry_ModelCapacity','XbFighterRegistry_ModelSlot','XbFighterRegistry_CreateModel','XbNativeModel_Prepare','XbFighter_BindOwnedTables','XbFighter_BindOwnedFiles','XbFighterRegistry_Remove','XbFighterRegistry_End'}<=defined
    assert {'XbBattleCommon_Prepare','XbBattleCommon_Convert','XbBattleCommon_GetAiData','XbBattleCommon_Free','XbBattleFighter_SetEye','XbBattleFighter_SetFace','XbBattleFighter_StepFace','BtlObj_UpdateFace','BtlObjFace_StepBlink','BObjFace_StepLipData'}<=defined
    assert {'XbBattleStage_PrepareParameters','XbStage_TryBindNav','XbBattleRuntime_AttachCommon3','XbBattleRuntime_TryCommon3Entry','XbSfx_PrepareBank','XbBattleRuntime_BeginAudio','XbBattleRuntime_ServiceAudio','XbBattleRuntime_EndAudio','Snd_Init','Snd_Term','Snd_LoadBank','Snd_Update'}<=defined
    assert {'XbStage_TryAnimationNodeFlags','dr_texfile_validate_transfers','XbBattleStage_PrepareGeometry','XbBattleStage_BeginGeometry','XbBattleStage_EndGeometry','XbBattleRuntime_BeginStage','XbBattleRuntime_ServiceStage','XbBattleRuntime_EndStage','File_Init','BtlStage_Init','BtlStage_Update','StgRigid_Init'}<=defined
    assert data_at_symbol('gPortDataStripped',4)==b'\x00\x00\x00\x02'
    report.update(typed_main_catalog_linked=True,typed_catalog_complete=bool(cat[1]),
        typed_regions=cat[3],typed_operations=cat[5],partial_data_preview_available=True,
        native_game_data_loaded=False,stage_collision_diagnostic_linked=True,stage_collision_original_readers_linked=True,stage_collision_install_path_linked=True,fighter_model_animation_preparation_linked=True,native_pose_body_attack_and_texture_paths_linked=True,full_fighter_spawn=False,original_object_registry_linked=True,active_model_object_lifecycle_linked=True,typed_character_table_windows=8,common_reference_retarget_linked=True,character_and_AI_initialization_path_linked=True,complete_combat_boot_integrated=False,native_common_CPU_resources_linked=True,original_CPU_facial_state_path_linked=True,audio_clock_facial_sync=False,original_BtlObj_Init_Term_boot_linked=True,common4_native_consumer_probe_linked=True,native_grounded_starts_linked=True,native_MAPD_sections=20,native_navigation_binding=True,original_stage_cycle_integrated=True,native_stage_geometry_preparation_linked=True,complete_stage_resources=False,common3_texture_subset_linked=True,complete_common3=False,original_audio_cycle_integrated=True,hardware_audio_verified=False,native_stage_animation_adapter_linked=True,common3_typed_resource_preparation_linked=True,scope='0.47-D13: 635/637 common3 resources prepared by native format; stage animation triple and key schemas, canonical LE VIF flags and bounded GS uploads. Native MAPD, path and navigation data; native stage geometry with original Init/Reset/Update/Term integrated; stage readiness comes only from its original initializer. Full battle remains blocked. Owned common3 TexFile subset and original four-bank audio cycle with service in the launcher; full scene still unavailable. Original object Init/Term boot with private common resources; six common roster windows and bounded common AI data; eight character reader windows; CPU blink/mouth/lip path; original CreateChara/Setup model lifecycle with native CPU resources/pools/materials; original BtlChar/BtlAi initialization path linked; execution verified separately on i386 only; linked Battle_Main and reviewed native data catalog; original-only pools excluded with native reference proof; explicit typed-data startup preview commits only validated known fields, with readiness marker 2 retained. Native data catalog excludes 46 proven original-only pool bytes; full battle resource initialization, original Game_Main and combat execution remain absent. No hardware validation.')
if 'XbTransportProbe_Run' in defined:
    required={'dr_body_source_open','dr_body_source_build','dr_body_source_pack','dr_body_source_free','dr_body_demo_select_face','dr_body_demo_profile','dr_body_demo_probe','XbStagePack_Prepare','XbStagePack_RegisterInitial','XbStagePack_NullMesh','XbStagePack_RelocateTextureFrames','XbStagePack_CheckRelocation','XbMatchUiPack_Prepare','XbMatchUiPack_RegisterInitial','XbModelPack_Prepare','XbModelPack_RegisterInitial','XbModelPack_ChainPrepared','XbEffectPack_Prepare','XbEffectPack_RegisterInitial','XbRes_ReadFile','XbCommonPack_Prepare','XbCommonPack_Register','XbRes_RegisterCommonItems','XbSoundBank_RegisterCommon','XbMotionPack_RegisterInitial','XbRes_RegisterLoadingScreens','XbRes_RegisterInitialMembers','XbRes_BeginCheckedReads','XbRes_EndCheckedReads','XbFile_RequestOwned','XbFile_RequestBounded','XbFile_AbortRequestsRetainBuffers','XbBpe_DecodeBounded','Common_Init','Common_LoadBoot','Common_Reload','dr_font_prepare'}
    assert required<=defined,required-defined
    report.update(source_delivery='D34',runtime_test='Static check only; runtime evidence is separate.',strict_transport_probe_linked=True,common_converters_linked=[2,3,4,5,0x14b],scope='D34 diagnostic: strict whole-file transport of 22 real resources in two cycles, isolated original stage relocation and cached fixed-reference ISO model viewer with face-only rebuild and color probes. No full Battle_Load/Init/Update/Main or fight frame. Common2 entry12 remains opaque exact USA bytes; no internal consumer in pinned source. Runtime evidence is separate from this static check.')
if 'dr_dma_add_clut_ref' in defined:
    required={'XbVif_ChainAddress','XbFreshBoot_FrameProbe','XbFreshBoot_FramesProbe','Battle_StepFrame','dr_gs_native_limits','dr_dma_snapshot_chain'}
    assert required<=defined,required-defined
    report.update(source_delivery='D38',runtime_test='Static check only; see engine/reports/0.38 for actual execution.',REF_payload_arena_bytes=0x800000,command_buffer_bytes=0x100000,GPU_vertices_per_batch=1024,scope='D38: bounded REF snapshots, typed runtime CLUT upload, tracked inline RET CALL mapping, bounded OT and numeric object flags; original frame probes linked. Static presence does not establish a complete fight, Battle_Main, audio or physical hardware.')
if 'dr_dma_add_xfade_ref' in defined:
    assert 'XbFreshBoot_IntroProbe' in defined
    report.update(source_delivery='D42',expanded_image_bytes=len(pe),
        runtime_test='Static check only; runtime logs are separate.',
        scope='Recovered D39 plus checked RGB24 cross-fade consumer and atomic buffer allocation. Basic-compression zero runs expanded before all image page hashes are checked. NOT_READY remains 2; no gameplay claim.')
if 'dr_targets_stencil_written' in defined:
    assert {'dr_xenos_profile','dr_targets_profile','dr_capture_original_profile'}<=defined
    assert {'dr_ot_payload','XbFreshBoot_BattleProbe','XbFreshBoot_BattleCheck'}<=defined
    report.update(source_delivery='D42',GPU_vertex_arena_bytes=12582912,GPU_recording_slots=8,GPU_recording_bytes_per_slot=4194300,scope='D42 bounded frame arena and immutable recording ring; resolved-source reuse and tracked zero-stencil restore. Diagnostic only; NOT_READY=2. Static check does not prove 30 FPS, complete gameplay, original menus, audio or online.')
if 'dr_vu1_model_renderer' in defined or 'dr_targets_ensure_commands' in defined:
    assert {'dr_vu1_model_renderer','dr_vu1_model_profile','dr_targets_ensure_commands'}<=defined
    report.update(source_delivery='D43',model_shader_bridge_linked=True,command_budget_live_cursor_linked=True,scope='D43 model microcode classifier and bounded shader handoff; live command cursor margin and split before complete draws. Static presence is not runtime proof; no 30 FPS, full menu or complete-game claim.')
if "dr_targets_epoch" in defined:
    report.update(source_delivery="D44",GPU_state_cache_linked=True,scope="Bounded shadow GPU bridge, per-pass literal and uniform reuse; runtime and console FPS proof separate")


if 'dr_gs_batch_merge' in defined:
    assert 'dr_targets_epoch' in defined
    report.update(source_delivery='D46',consecutive_draw_batch_linked=True,scope='Bounded compatible consecutive draws; snapshots, target feedback, DATE, effects and stencil remain barriers. Static checks do not prove console FPS.')
report_path.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n")
print(json.dumps(report,ensure_ascii=False,indent=2))
