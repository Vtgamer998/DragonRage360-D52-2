XSDK ?= ../xsdk
.DEFAULT_GOAL := all
TOOLCHAIN ?= ../xsdk-gcc/bin/powerpc-xsdk-elf-
CC := $(TOOLCHAIN)gcc
# Retain real native component APIs for static verification in the diagnostic.
NATIVE_API_ROOTS := -Wl,--undefined=sceMpegInit -Wl,--undefined=sceMpegCreate -Wl,--undefined=sceMpegDelete -Wl,--undefined=sceMpegReset -Wl,--undefined=sceMpegIsEnd -Wl,--undefined=sceMpegAddCallback -Wl,--undefined=sceMpegAddStrCallback -Wl,--undefined=sceMpegDemuxPss -Wl,--undefined=sceMpegGetPicture -Wl,--undefined=sceGsResetPath -Wl,--undefined=sceGsResetGraph -Wl,--undefined=dr_gs_movie_build -Wl,--undefined=dr_gs_movie_send -Wl,--undefined=XbAdx_Clock -Wl,--undefined=ADXT_Init -Wl,--undefined=ADXT_Create -Wl,--undefined=ADXT_SetReloadSct -Wl,--undefined=ADXT_SetSvrFreq -Wl,--undefined=ADXT_StartAfs -Wl,--undefined=ADXT_StartFname -Wl,--undefined=ADXT_Stop -Wl,--undefined=ADXT_Pause -Wl,--undefined=ADXT_SetOutVol -Wl,--undefined=ADXT_GetOutVol -Wl,--undefined=ADXT_SetOutPan -Wl,--undefined=ADXT_SetOutputMono -Wl,--undefined=ADXT_GetStat -Wl,--undefined=XbOverlay_PrepareBuild -Wl,--undefined=XbOverlay_Load -Wl,--undefined=XbMpeg_Open -Wl,--undefined=XbMpeg_Next -Wl,--undefined=XbMpeg_Close
CFLAGS := -mcpu=cell -fno-pic -std=gnu17 -MMD -MP -O2 -g -ffunction-sections -fdata-sections -Wall -Wextra -Werror -I$(XSDK)/include -Isrc -Iengine/graphics -Iengine/capture/port/src/gs -Iengine/platform/platform -Id50 -DDR_XBOX360
OBJECTS := build/dr_ot_native.o build/dr_gs_movie.o build/dr_dma_engine.o build/dr_gs_native.o build/dr_dma_producer.o build/dr_gs_stream.o build/gs_draw.o build/gs_core.o build/gs_sony.o build/dr_gs_backend.o build/dr_gs_replay.o build/dr_gs_batch.o build/dr_capture_demo.o build/dr_capture_platform.o build/main.o build/demo_ui.o build/dr_disc.o build/bt3_wire.o build/net_check.o build/lobby.o build/dr_anim.o build/dr_bpe.o build/dr_resource_selftest.o build/dr_container.o build/dr_stage_collision.o build/dr_skeleton.o build/dr_xenos.o build/dr_gs_texture.o build/dr_graphics_demo.o build/dr_body_demo.o build/dr_texfile.o build/dr_body_pipeline.o build/dr_body_model.o build/dr_elf.o build/dr_face_vif.o build/dr_targets.o build/dr_vu1.o build/dr_vu1_selftest.o build/save_unlock.o build/save_export.o build/d50/xsdk_compat.o build/d50/gfx_probe.o build/d50/gfx_present.o build/d50/lobby_selftest.o
.PHONY: all clean host-test
PLATFORM_SOURCES := $(addprefix engine/platform/platform/,$(shell cat engine/platform/platform-sources.txt))
PLATFORM_OBJECTS := $(patsubst engine/platform/platform/%.c,build/%.o,$(PLATFORM_SOURCES))
OBJECTS += $(PLATFORM_OBJECTS)
$(PLATFORM_OBJECTS): build/%.o: engine/platform/platform/%.c
	$(CC) $(CFLAGS) -I$(UPSTREAM)/include -I$(UPSTREAM)/port/include -DPORT_BIGENDIAN -DXB_USE_NTALLOC -DXB_USE_XAM -DXB_USE_NTDELAY -c $< -o $@
all: build/default.xex
build/%.o: src/%.c
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@
# D50/D51 platform layer and probes. Kept out of src/ so the bt3xbx port can
# evolve without touching the title's original sources.
build/d50/%.o: d50/%.c
	mkdir -p build/d50
	$(CC) $(CFLAGS) -c $< -o $@
build/default.elf: $(OBJECTS) $(XSDK)/build/libxsdk.a
	$(CC) -static -nostdlib -T $(XSDK)/src/xsdk.ld -Wl,--gc-sections,-Map,build/default.map -o $@ $(OBJECTS) $(NATIVE_API_ROOTS) -Wl,--start-group $(XSDK)/build/libxsdk.a -lc -lm -lgcc -Wl,--end-group
build/default.xex: build/default.elf
	python3 $(XSDK)/tools/elf2xex.py --title-id 0x44523630 --title-name "DragonRage360 Demo" $< $@
host-test:
	mkdir -p build
	gcc -std=c17 -Wall -Wextra -Werror -Isrc src/demo_ui.c tests/test_ui.c -o build/test-ui
	./build/test-ui
# D50 milestones 1-2: xsdk platform layer + clear/triangle probe, host mocks only.
.PHONY: d50-probe-test
d50-probe-test:
	mkdir -p build
	gcc -std=gnu17 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Iengine/tests -Iengine/graphics -Isrc -Ivalidation-tooling/xsdk/include d50/test_d50_probe.c d50/gfx_probe.c d50/gfx_present.c d50/xsdk_compat.c engine/graphics/dr_xenos.c engine/graphics/dr_targets.c engine/graphics/dr_gs_texture.c engine/graphics/dr_graphics_demo.c -lm -o build/test-d50-probe
	ASAN_OPTIONS=detect_leaks=0 ./build/test-d50-probe
# D52-1: loopback UDP lobby self-test, host only.
.PHONY: d52-lobby-test
d52-lobby-test:
	mkdir -p build
	gcc -std=gnu17 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isrc -Id50 d50/test_d52_lobby.c d50/lobby_selftest.c d50/xsdk_compat.c src/lobby.c -o build/test-d52-lobby
# D52-2: loopback UDP lobby + online battle synchronization self-test, host only.
.PHONY: d52-online-test
d52-online-test:
	mkdir -p build
	gcc -std=gnu17 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isrc -Id50 d50/test_d52_online.c d50/online_battle_selftest.c d50/xsdk_compat.c src/online_battle.c src/lobby.c -o build/test-d52-online
	ASAN_OPTIONS=detect_leaks=0 ./build/test-d52-online
	ASAN_OPTIONS=detect_leaks=0 ./build/test-d52-lobby
clean:
	rm -rf build
.PHONY: disc-test
disc-test:
	mkdir -p build
	gcc -std=c99 -Wall -Wextra -Werror -Isrc src/dr_disc.c tests/disc_probe.c -o build/disc-probe
	python3 tests/test_disc.py
.PHONY: save-test
save-test:
	mkdir -p build
	gcc -std=c17 -Wall -Wextra -Werror -Isrc src/save_unlock.c src/save_export.c tests/save/test_export.c -o build/test-save-export
	python3 tests/save/test_save.py build/test-save-export

.PHONY: lobby-test
lobby-test:
	mkdir -p build
	gcc -std=c17 -Wall -Wextra -Werror -Isrc src/lobby.c tests/test_lobby.c -o build/test-lobby
	./build/test-lobby

-include $(OBJECTS:.o=.d)

# Reference VU arithmetic must not become fused multiply-add.
build/dr_vu1.o: CFLAGS += -ffp-contract=off

build/dr_body_pipeline.o: CFLAGS += -ffp-contract=off

build/dr_stage_collision.o: CFLAGS += -mcheck-fpr-alignment

# Additional compile checks; actual GS capture/backend now also enter the title link.
# Combat simulation and full PS2 platform layers remain absent.
.PHONY: engine-backend-check
engine-backend-check: build/dr_gs_backend.experimental.o build/dr_gs_replay.experimental.o
build/dr_gs_backend.experimental.o: engine/graphics/dr_gs_backend.c engine/graphics/dr_gs_backend.h engine/upstream/gs_draw.h
	$(CC) $(CFLAGS) -DDR_XBOX360 -mcheck-fpr-alignment -Iengine/graphics -Iengine/upstream -c $< -o $@
build/dr_gs_replay.experimental.o: engine/graphics/dr_gs_replay.c engine/graphics/dr_gs_replay.h engine/upstream/gs_draw.h
	$(CC) $(CFLAGS) -DDR_XBOX360 -mcheck-fpr-alignment -Iengine/graphics -Iengine/upstream -c $< -o $@

-include build/dr_gs_backend.experimental.d build/dr_gs_replay.experimental.d

build/gs_draw.o: engine/capture/port/src/gs/gs_draw.c
	$(CC) $(CFLAGS) -mcheck-fpr-alignment -c $< -o $@
build/gs_core.o: engine/capture/port/src/gs/gs_core.c
	$(CC) $(CFLAGS) -mcheck-fpr-alignment -c $< -o $@
build/gs_sony.o: engine/capture/port/src/gs/gs_sony.c
	$(CC) $(CFLAGS) -c $< -o $@
build/dr_gs_backend.o: engine/graphics/dr_gs_backend.c
	$(CC) $(CFLAGS) -mcheck-fpr-alignment -c $< -o $@
build/dr_gs_replay.o: engine/graphics/dr_gs_replay.c
	$(CC) $(CFLAGS) -mcheck-fpr-alignment -c $< -o $@

# Strict integration attempt: never overwrites build/default.elf or default.xex.
# This target still fails on missing services/data. No generated stubs.
UPSTREAM ?= ../../dragonrage-upstream
FLOAT_MODE ?= native-ps2
PLUGIN_ARGS ?=
.PHONY: battle-link
battle-link: $(OBJECTS) $(XSDK)/build/libxsdk.a
	python3 engine/tools/link_battle.py $(UPSTREAM) $(CC) $(XSDK) build/battle-native --float-mode $(FLOAT_MODE) --platform $(PLUGIN_ARGS)

build/dr_dma_engine.o: engine/graphics/dr_dma_engine.c engine/graphics/dr_dma_engine.h
	$(CC) $(CFLAGS) -c $< -o $@

build/dr_gs_movie.o: engine/graphics/dr_gs_movie.c engine/graphics/dr_gs_movie.h
	$(CC) $(CFLAGS) -c $< -o $@

# Links the real combat code with a diagnostic entry, never calls the fight.
.PHONY: engine-linked-diagnostic
engine-linked-diagnostic: $(OBJECTS) $(XSDK)/build/libxsdk.a
	python3 engine/tools/link_battle.py $(UPSTREAM) $(CC) $(XSDK) build/battle-native --float-mode $(FLOAT_MODE) --platform --fixed-battle $(PLUGIN_ARGS)
	python3 engine/tools/test_native_data_sections.py build/battle-native build/battle-native/tests
	python3 engine/tools/build_engine_diagnostic.py build/battle-native $(XSDK) build/engine-diagnostic
	python3 tests/check_xex.py build/engine-diagnostic build/engine-diagnostic/VALIDACAO.json

# ISO is an external user-owned input. Extract only its two programs locally;
# never distribute build/typed-input. Partial catalogs never authorize combat.
ISO ?= game.iso
.PHONY: typed-data-diagnostic
typed-data-diagnostic: $(OBJECTS) $(XSDK)/build/libxsdk.a
	python3 engine/tools/link_battle.py $(UPSTREAM) $(CC) $(XSDK) build/typed-base --float-mode $(FLOAT_MODE) --platform --fixed-battle $(PLUGIN_ARGS)
	python3 engine/tools/extract_iso.py $(ISO) build/typed-input --programs-only
	python3 engine/tools/build_typed_manifest.py $(UPSTREAM) build/typed-base build/typed-input/disc build/typed-manifest
	python3 engine/tools/test_typed_manifest.py build/typed-manifest/typed-manifest.json build/typed-input/disc build/typed-test --sanitize
	python3 engine/tools/test_water_pool.py build/typed-input/disc/SLUS_216.78 build/typed-manifest/water-pool-proof.json
	python3 engine/tools/test_reviewed_unused.py build/typed-manifest/typed-manifest.json build/typed-base $(CC) build/unused-native-test
	python3 engine/tools/test_fixed_resources.py $(XSDK) $(ISO) build/fixed-resources --sanitize
	python3 engine/tools/link_battle.py $(UPSTREAM) $(CC) $(XSDK) build/fixed-manifest --float-mode $(FLOAT_MODE) --platform --fixed-battle --typed-catalog build/typed-manifest/typed_catalog.c $(PLUGIN_ARGS)
	python3 engine/tools/build_engine_diagnostic.py build/fixed-manifest $(XSDK) build/typed-diagnostic
	python3 engine/tools/verify_typed_catalog.py build/typed-manifest/typed-manifest.json build/typed-diagnostic/default.elf build/typed-diagnostic/catalog-verification.json
	python3 tests/check_xex.py build/typed-diagnostic build/typed-diagnostic/VALIDACAO.json

# Native stage adapter uses the original typed views; port_compat fixes s64 on PPC32.
build/xb_battle_stage.o: CFLAGS += -I$(UPSTREAM)/include -I$(UPSTREAM)/port/include -mcheck-fpr-alignment

build/xb_battle_fighter.o: CFLAGS += -I$(UPSTREAM)/include -I$(UPSTREAM)/port/include -mcheck-fpr-alignment

build/xb_fighter_registry.o: CFLAGS += -I$(UPSTREAM)/include -I$(UPSTREAM)/port/include -mcheck-fpr-alignment

# Refuse accidental decomp duplicates, compatibility-header shadowing and raw
# binary tables before compiling or linking platform modules.
.PHONY: platform-audit
platform-audit:
	python3 engine/tools/audit_platform_sources.py .
$(OBJECTS): | platform-audit
build/default.elf: | platform-audit

build/xb_model_native.o: CFLAGS += -I$(UPSTREAM)/include -I$(UPSTREAM)/port/include -mcheck-fpr-alignment

# Original roster/common reader layouts used by the bounded D9 probe.
build/xb_battle_consumers.o: CFLAGS += -I$(UPSTREAM)/include -I$(UPSTREAM)/port/include -mcheck-fpr-alignment

build/xb_battle_runtime.o: CFLAGS += -I$(UPSTREAM)/include -I$(UPSTREAM)/port/include -mcheck-fpr-alignment

# D19 isolated-session ownership gate (host-only until original boot prerequisites are met).
.PHONY: battle-boot-test
battle-boot-test:
	mkdir -p build
	gcc -std=c17 -Wall -Wextra -Werror -Iengine/platform/platform engine/platform/platform/xb_battle_boot.c engine/tests/test_battle_boot.c -o build/test-battle-boot
	./build/test-battle-boot

.PHONY: stage-camera-test
stage-camera-test:
	mkdir -p build
	gcc -std=c17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iengine/platform/platform engine/platform/platform/xb_stage_camera.c engine/tests/test_stage_camera.c -o build/test-stage-camera
	./build/test-stage-camera

.PHONY: checked-reads-test
checked-reads-test:
	mkdir -p build
	gcc -std=c17 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -DXB_HOST_TEST -Iengine/platform/platform engine/platform/platform/xb_resconv.c engine/tests/test_checked_reads.c -o build/test-checked-reads
	ASAN_OPTIONS=detect_leaks=0 ./build/test-checked-reads

.PHONY: checked-file-test
checked-file-test:
	mkdir -p build
	gcc -std=c17 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -DXB_HOST_TEST -Iengine/tests/host -Isrc -Iengine/platform/platform engine/platform/platform/xb_resconv.c engine/platform/platform/xb_plat_file.c engine/tests/test_checked_file_transport.c -o build/test-checked-file
	ASAN_OPTIONS=detect_leaks=0 ./build/test-checked-file

.PHONY: bpe-failure-test
bpe-failure-test:
	mkdir -p build
	gcc -std=c17 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -DXB_HOST_TEST -Isrc -Iengine/platform/platform engine/platform/platform/xb_resconv.c engine/platform/platform/xb_bpe.c src/dr_bpe.c engine/tests/test_bpe_failures.c -o build/test-bpe-failures
	ASAN_OPTIONS=detect_leaks=0 ./build/test-bpe-failures

.PHONY: loading-errors-test
loading-errors-test:
	python3 engine/tools/test_loading_errors.py $(UPSTREAM) build/loading-errors-test

# Private input files extracted from the user's disc; never packaged.
.PHONY: loading-resources-test
loading-resources-test:
	mkdir -p build
	gcc -std=c17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffunction-sections -fdata-sections -DXB_HOST_TEST -Isrc -Iengine/platform/platform engine/platform/platform/xb_resconv.c engine/platform/platform/xb_bpe.c engine/platform/platform/xb_demo_resources.c src/dr_bpe.c src/dr_texfile.c src/dr_gs_texture.c src/dr_container.c engine/tests/test_loading_resources.c -Wl,--gc-sections -o build/test-loading-resources
	ASAN_OPTIONS=detect_leaks=0 ./build/test-loading-resources $(LOADING_INPUTS)

.PHONY: heap-capacity-test
heap-capacity-test:
	mkdir -p build
	gcc -std=gnu17 -O1 -g -fno-builtin -ffunction-sections -fdata-sections -fsanitize=undefined -DPORT_BIGENDIAN -Dmalloc=TestHeapMalloc -I$(UPSTREAM)/include -Iengine/platform/platform -c engine/combat/heap.c -o build/heap-capacity.o
	gcc -std=gnu17 -O1 -g -fno-builtin -ffunction-sections -fdata-sections -fsanitize=undefined -I$(UPSTREAM)/include -c $(UPSTREAM)/src/sys/align.c -o build/heap-capacity-align.o
	gcc -std=gnu17 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined -I$(UPSTREAM)/include -Iengine/platform/platform engine/tests/test_heap_capacity.c build/heap-capacity.o build/heap-capacity-align.o -Wl,--gc-sections -o build/test-heap-capacity
	./build/test-heap-capacity

.PHONY: sound-bank-test initial-audio-job-test
sound-bank-test:
	python3 engine/tools/test_sound_bank_transport.py $(UPSTREAM) build/sound-bank-test $(BANK_INPUTS)
initial-audio-job-test:
	python3 engine/tools/test_initial_audio_job.py $(UPSTREAM) build/initial-audio-job-test $(BANK_INPUTS)

.PHONY: motion-transport-test member-transport-test
motion-transport-test:
	python3 engine/tools/test_motion_transport.py $(UPSTREAM) build/motion-transport-test $(MOTION_INPUTS)
member-transport-test:
	mkdir -p build
	gcc -std=c17 -O1 -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections -fsanitize=address,undefined -DXB_HOST_TEST -Isrc -Iengine/platform/platform engine/tests/test_member_transport.c engine/platform/platform/xb_resconv.c engine/platform/platform/xb_demo_resources.c src/dr_texfile.c src/dr_gs_texture.c src/dr_bpe.c -Wl,--gc-sections -o build/test-member-transport
	ASAN_OPTIONS=detect_leaks=0 ./build/test-member-transport $(MEMBER_INPUTS)

.PHONY: embedded-sources-test
embedded-sources-test:
	mkdir -p build
	gcc -std=c17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -DXB_HOST_TEST -Iengine/platform/platform engine/tests/test_embedded_sources.c engine/platform/platform/xb_resconv.c -o build/test-embedded-sources
	ASAN_OPTIONS=detect_leaks=0 ./build/test-embedded-sources

.PHONY: initial-load-ownership-test res-load-ownership-test
initial-load-ownership-test:
	python3 engine/tools/test_initial_load_ownership.py $(UPSTREAM) build/initial-load-ownership-test
res-load-ownership-test:
	python3 engine/tools/test_res_load_ownership.py $(UPSTREAM) build/res-load-ownership-test

.PHONY: common-boot-test common4-transport-test
common-boot-test:
	python3 engine/tools/test_common_boot.py $(UPSTREAM) build/common-boot-test
common4-transport-test:
	python3 engine/tools/test_common4_transport.py $(UPSTREAM) $(COMMON4_INPUT) build/common4-transport-test

.PHONY: common-pack-transport-test
common-pack-transport-test:
	python3 engine/tools/test_common_pack_transport.py build/common-pack-transport-test $(COMMON_PACK_INPUTS)

# Original game/menu source snapshot; integrity + host syntax, not an Xbox runtime test.
.PHONY: original-source-audit
original-source-audit:
	python3 adaptation/tools/audit_original.py
