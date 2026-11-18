# Native Switch port. Run from the repo root with `make -C switch` in the SDK shell.
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

ifeq ($(strip $(DEVKITPRO)),)
$(error Set DEVKITPRO to the devkitPro installation directory)
endif
# GCC is a native Windows executable, and switch.specs reads DEVKITPRO itself.
ifneq ($(filter MSYS% MINGW% CYGWIN%,$(shell uname -s)),)
DEVKITPRO := $(shell cygpath -am '$(DEVKITPRO)')
endif
export DEVKITPRO

DEVKITA64 ?= $(DEVKITPRO)/devkitA64
LIBNX := $(DEVKITPRO)/libnx
PORTLIBS := $(DEVKITPRO)/portlibs/switch
TOOLBIN := $(DEVKITPRO)/tools/bin
CC := $(DEVKITA64)/bin/aarch64-none-elf-gcc
# Use the SDK's C++ link driver for the final NRO ELF.
CXX := $(DEVKITA64)/bin/aarch64-none-elf-g++

GAME := ../pseudocode/src
XPORT ?= ../.deps/xport
RUNTIME := $(XPORT)/src
BUILD := build
NAME := DemolitionRacer
VERSION := 0.1.3
BINARY := $(BUILD)/$(NAME)
BUILD_MAKEFILE := $(firstword $(MAKEFILE_LIST))

# Keep in the same order as APP_SOURCES in pseudocode/src/platform/web/Makefile.
GAME_SOURCES := \
 game_main.c game_data.c game_link_stubs.c draft_agent1.c draft_agent1_extra.c draft_agent2.c draft_agent3.c \
 game_bootstrap.c game_missing.c game_pres_init.c game_pres_main.c game_pres_support.c game_pres_menu.c \
 game_race_start.c game_race_setup.c game_race_init.c game_pres_model.c game_pres_basic.c game_pres_high_scores.c \
 game_race_math.c game_race_arena.c game_pres_options.c game_pres_selection.c game_pres_records.c game_pres_extra.c \
 game_race_track.c game_pres_controls.c game_race_palette.c game_race_packets.c game_race_effects.c game_race_hud_init.c \
 game_race_input_init.c game_race_palette_tick.c game_race_audio.c game_race_text.c game_race_camera.c game_race_physics.c \
 game_race_motion_angles.c game_race_hud.c game_results.c game_memory_card.c game_results_init.c game_results_draw.c \
 game_results_menu.c game_results_two_player.c game_race_hud_status.c game_race_hud_panels.c game_race_flare.c \
 game_race_view.c game_race_view_modes.c game_race_vehicle_render.c game_race_hud_stats.c game_race_car_draw.c \
 game_race_car_lighting.c game_race_finish.c game_race_pause.c game_race_pause_menu.c game_race_pause_audio.c \
 game_race_car_effects.c game_race_car_surface.c game_race_frame_render.c game_race_frame_effects.c game_race_scene.c \
 game_race_scene_packets.c game_race_scene_subdivide_ft.c game_race_scene_subdivide_gt.c game_race_scene_clip_gt.c \
 game_race_scene_clip_ft.c game_race_car_parts.c game_race_car_packets.c game_race_frame_objects.c game_race_frame_animation.c \
 game_race_object_effects.c game_race_object_collision.c game_race_controls.c game_race_driving_effects.c game_race_driving.c \
 game_race_driving_control.c game_race_driving_force.c game_race_driving_callbacks.c game_race_driving_alignment.c \
 game_race_driving_engine.c game_race_driving_contact.c game_race_driving_matrix.c game_race_driving_recovery.c \
 game_race_driving_collision.c game_race_ai_actions.c game_race_ai_steering.c game_race_ai_motion.c game_race_ai_routes.c \
 game_race_ai_path.c game_race_ai_velocity.c game_race_pair_response.c game_race_pair_damage.c game_race_pair_effects.c \
 game_race_damage_region.c game_race_damage_total.c game_race_detach.c game_race_debris.c game_race_damage_uv.c \
 game_race_damage_deform.c game_race_damage_parts.c game_race_pair_math.c game_race_pair_slow.c game_race_pair_vertical.c \
 game_race_pair_objects.c game_pickups.c game_race_pair_state.c game_race_collision_feedback.c game_race_collision_sound.c \
 game_race_ai_recovery_route.c game_race_ai_special_motion.c game_race_ai_special_contact.c game_movie_boot.c \
 game_movie_controller.c game_movie_sdk.c

# Reuse the PSX software GPU; Switch presents its pixels directly through libnx.
RUNTIME_SOURCES := psx.c psx_gpu.c psx_font.c psx_spu.c psx_seq.c psx_xa.c \
 psx_mdec.c psx_press.c psx_vlc.c psx_stream.c xport_fmv.c xport_trace.c
GAME_OBJS := $(GAME_SOURCES:%.c=$(BUILD)/game/%.o)
RUNTIME_OBJS := $(RUNTIME_SOURCES:%.c=$(BUILD)/xport/%.o)
OBJS := $(GAME_OBJS) $(RUNTIME_OBJS) $(BUILD)/platform.o $(BUILD)/diagnostics.o $(BUILD)/framebuffer_blit.o

ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
DEFINES := -D__SWITCH__ -DAP_SWITCH -DXPORT_NATIVE -DFIELD_RATE=60 \
 -DWND_WIDTH=960 -DWND_HEIGHT=720 -DWND_TITLE='"Demolition Racer"' -DSHORT_NAME='"DR"' \
 -DDRACER_VERSION='"$(VERSION)"' -DNDEBUG
INCLUDES := -I"$(GAME)" -I"$(RUNTIME)" -isystem "$(LIBNX)/include" -isystem "$(PORTLIBS)/include"
CFLAGS ?= -O2 -g -pipe
# The PsyQ-compatible integer trig names overlap GCC's complex math builtins.
COMPAT := -fms-extensions -funsigned-char -fno-strict-aliasing -fwrapv \
 -fno-builtin-ccos -fno-builtin-csin -fno-builtin-catan
COMPILE.c = "$(CC)" $(ARCH) $(DEFINES) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) -std=gnu11 $(COMPAT) \
 -ffunction-sections -fdata-sections -MMD -MP -c
LIBS := -lnx -lm

.PHONY: all objects clean
all: $(BINARY).nro
# Compile the game and reusable runtime without the platform objects.
objects: $(GAME_OBJS) $(RUNTIME_OBJS)

$(BUILD)/game/%.o: $(GAME)/%.c $(BUILD_MAKEFILE)
	@mkdir -p "$(@D)"
	$(COMPILE.c) "$<" -o "$@"

$(BUILD)/xport/%.o: $(RUNTIME)/%.c $(BUILD_MAKEFILE)
	@mkdir -p "$(@D)"
	$(COMPILE.c) "$<" -o "$@"

$(BUILD)/%.o: %.c $(BUILD_MAKEFILE)
	@mkdir -p "$(@D)"
	$(COMPILE.c) "$<" -o "$@"

# Windows GCC/collect2 need a writable native temp path even for an object-only link.
# Use the build tree so this works independently of the SDK shell's /tmp mount.
ifneq ($(filter MSYS% MINGW% CYGWIN%,$(shell uname -s)),)
NATIVE_BUILD := $(shell cygpath -am '$(BUILD)')
LINK_ENV := MSYS2_ENV_CONV_EXCL='TMPDIR;TEMP;TMP;DEVKITPRO' \
 TMPDIR="$(NATIVE_BUILD)" TEMP="$(NATIVE_BUILD)" TMP="$(NATIVE_BUILD)"
endif

$(BINARY).elf: $(OBJS) $(BUILD_MAKEFILE)
	$(LINK_ENV) "$(CXX)" $(ARCH) -specs="$(LIBNX)/switch.specs" -Wl,-Map,"$(BINARY).map" $(LDFLAGS) -o "$@" \
	 $(OBJS) -L"$(PORTLIBS)/lib" -L"$(LIBNX)/lib" $(LIBS)

$(BINARY).nacp: $(BUILD_MAKEFILE)
	@mkdir -p "$(@D)"
	"$(TOOLBIN)/nacptool" --create "Demolition Racer" "Demolition Racer xport contributors" "$(VERSION)" "$@"

$(BINARY).nro: $(BINARY).elf $(BINARY).nacp
	"$(TOOLBIN)/elf2nro" "$<" "$@" --nacp="$(BINARY).nacp"

clean:
	rm -rf -- "$(BUILD)"

-include $(OBJS:.o=.d)
