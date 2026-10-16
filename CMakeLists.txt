cmake_minimum_required(VERSION 3.21)
project(DOOM VERSION 1.10 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DOOM_BUILD_X11 "Build the historical Linux X11/OSS executable" OFF)
option(DOOM_BUILD_SDL "Build the native playable SDL3 app" OFF)
option(DOOM_RENDER3D "Build the accelerated 3D renderer (SDL_gpu: Metal or Vulkan)" ON)
option(DOOM_TEST_GPU "Test 3D renderer gameplay (requires a desktop with Metal or Vulkan)" OFF)
set(DOOM_GAME_DATA "" CACHE FILEPATH "Optional IWAD to bundle as the default game")
option(DOOM_SOUNDFONT "Bundle the GeneralUser GS SoundFont (32 MB download) for SoundFont music" ON)
set(DOOM_RELEASE_VERSION "0.1.0" CACHE STRING "Release version shown in the Mac app")
option(DOOM_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
include(CTest)
include(TestBigEndian)
test_big_endian(DOOM_BIG_ENDIAN)

add_library(doom_options INTERFACE)
target_compile_definitions(doom_options INTERFACE NORMALUNIX DOOM_PORTABLE)
if(DOOM_BIG_ENDIAN)
    target_compile_definitions(doom_options INTERFACE DOOM_BIG_ENDIAN)
endif()
if(WIN32)
    target_compile_definitions(doom_options INTERFACE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
else()
    target_compile_definitions(doom_options INTERFACE _POSIX_C_SOURCE=200809L)
endif()
if(MSVC)
    target_compile_options(doom_options INTERFACE /W3)
else()
    target_compile_options(doom_options INTERFACE
        -Wall -Werror=implicit-function-declaration -Werror=incompatible-pointer-types
        -Werror=int-to-pointer-cast -Werror=pointer-to-int-cast)
endif()
if(DOOM_ENABLE_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR "Sanitizer preset requires GCC or Clang")
    endif()
    target_compile_options(doom_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(doom_options INTERFACE -fsanitize=address,undefined)
endif()

set(DOOM_SOURCES
    linuxdoom-1.10/am_map.c
    linuxdoom-1.10/d_deh.c
    linuxdoom-1.10/d_items.c
    linuxdoom-1.10/d_main.c
    linuxdoom-1.10/d_net.c
    linuxdoom-1.10/doomdef.c
    linuxdoom-1.10/doomstat.c
    linuxdoom-1.10/dstrings.c
    linuxdoom-1.10/f_finale.c
    linuxdoom-1.10/f_wipe.c
    linuxdoom-1.10/g_game.c
    linuxdoom-1.10/hu_lib.c
    linuxdoom-1.10/hu_stuff.c
    linuxdoom-1.10/info.c
    linuxdoom-1.10/m_argv.c
    linuxdoom-1.10/m_bbox.c
    linuxdoom-1.10/m_cheat.c
    linuxdoom-1.10/m_menu.c
    linuxdoom-1.10/m_misc.c
    linuxdoom-1.10/m_random.c
    linuxdoom-1.10/p_ceilng.c
    linuxdoom-1.10/p_doors.c
    linuxdoom-1.10/p_enemy.c
    linuxdoom-1.10/p_floor.c
    linuxdoom-1.10/p_inter.c
    linuxdoom-1.10/p_lights.c
    linuxdoom-1.10/p_map.c
    linuxdoom-1.10/p_maputl.c
    linuxdoom-1.10/p_mobj.c
    linuxdoom-1.10/p_plats.c
    linuxdoom-1.10/p_pspr.c
    linuxdoom-1.10/p_saveg.c
    linuxdoom-1.10/p_setup.c
    linuxdoom-1.10/p_sight.c
    linuxdoom-1.10/p_spec.c
    linuxdoom-1.10/p_switch.c
    linuxdoom-1.10/p_telept.c
    linuxdoom-1.10/p_tick.c
    linuxdoom-1.10/p_user.c
    linuxdoom-1.10/r_bsp.c
    linuxdoom-1.10/r_data.c
    linuxdoom-1.10/r_draw.c
    linuxdoom-1.10/r_main.c
    linuxdoom-1.10/r_plane.c
    linuxdoom-1.10/r_segs.c
    linuxdoom-1.10/r_sky.c
    linuxdoom-1.10/r_things.c
    linuxdoom-1.10/s_sound.c
    linuxdoom-1.10/sounds.c
    linuxdoom-1.10/st_lib.c
    linuxdoom-1.10/st_stuff.c
    linuxdoom-1.10/tables.c
    linuxdoom-1.10/v_video.c
    linuxdoom-1.10/wi_stuff.c
)
# The engine parts every game program shares: WAD access, the zone, byte
# order and fixed point.
add_library(doom_engine STATIC linuxdoom-1.10/m_fixed.c linuxdoom-1.10/m_swap.c
    linuxdoom-1.10/w_wad.c linuxdoom-1.10/z_zone.c)
target_include_directories(doom_engine PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/linuxdoom-1.10")
target_link_libraries(doom_engine PUBLIC doom_options)
add_library(doom_core STATIC ${DOOM_SOURCES})
target_include_directories(doom_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/linuxdoom-1.10")
target_link_libraries(doom_core PUBLIC doom_options doom_engine)
if(NOT WIN32)
    target_link_libraries(doom_core PUBLIC m)
endif()

# A game program's parts: its simulation and its answers to
# platform/game_profile.h. The shared platform sources (renderer, audio, F4
# panel) are compiled into each game program against that game's headers.
add_library(doom_game INTERFACE)
target_sources(doom_game INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/platform/games/doom/profile.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/games/doom/materials.c")
target_link_libraries(doom_game INTERFACE doom_core)

# Heretic: Raven's game code (heretic/), with heretic/derp/ answering the
# DOOM headers the shared platform code includes.
set(HERETIC_SOURCES
    heretic/am_map.c heretic/ct_chat.c heretic/d_main.c heretic/d_net.c heretic/f_finale.c
    heretic/g_game.c heretic/in_lude.c heretic/info.c heretic/m_misc.c heretic/mn_menu.c
    heretic/p_ceilng.c heretic/p_doors.c heretic/p_enemy.c heretic/p_floor.c heretic/p_inter.c
    heretic/p_lights.c heretic/p_map.c heretic/p_maputl.c heretic/p_mobj.c heretic/p_plats.c
    heretic/p_pspr.c heretic/p_setup.c heretic/p_sight.c heretic/p_spec.c heretic/p_switch.c
    heretic/p_telept.c heretic/p_tick.c heretic/p_user.c heretic/r_bsp.c heretic/r_data.c
    heretic/r_draw.c heretic/r_main.c heretic/r_plane.c heretic/r_segs.c heretic/r_things.c
    heretic/s_sound.c heretic/sb_bar.c heretic/sounds.c heretic/tables.c heretic/v_video.c
)
add_library(heretic_core STATIC ${HERETIC_SOURCES})
target_include_directories(heretic_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/heretic"
    "${CMAKE_CURRENT_SOURCE_DIR}/heretic/derp")
target_link_libraries(heretic_core PUBLIC doom_options doom_engine)
if(NOT MSVC)
    # Raven's code as released: K&R-style declarations and DOS-era casts.
    # Watcom's char was signed (-j); ticcmds and tables rely on it. Source
    # options come after doom_options' -Wall, so the quieting sticks.
    target_compile_options(heretic_core PRIVATE -fsigned-char)
    set_source_files_properties(${HERETIC_SOURCES} PROPERTIES COMPILE_OPTIONS
        "-Wno-unused;-Wno-unused-but-set-variable;-Wno-parentheses;-Wno-dangling-else;-Wno-missing-braces;-Wno-char-subscripts;-Wno-switch;-Wno-deprecated-non-prototype;-Wno-unsequenced;-Wno-misleading-indentation;-Wno-sometimes-uninitialized;-Wno-logical-not-parentheses;-Wno-sizeof-pointer-memaccess;-Wno-pointer-sign;-Wno-absolute-value;-Wno-shift-negative-value;-Wno-format;-Wno-non-literal-null-conversion;-Wno-unknown-warning-option")
endif()
if(NOT WIN32)
    target_link_libraries(heretic_core PUBLIC m)
endif()
add_library(heretic_game INTERFACE)
target_sources(heretic_game INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/platform/games/heretic/profile.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/games/heretic/materials.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/heretic/i_derp.c")
target_link_libraries(heretic_game INTERFACE heretic_core)

# A complete link of the engine without graphics/audio dependencies.
add_library(doom_headless_platform OBJECT platform/i_headless.c platform/i_portable_system.c)
target_link_libraries(doom_headless_platform PUBLIC doom_core)
add_executable(doom-headless platform/headless_main.c)
target_link_libraries(doom-headless PRIVATE doom_headless_platform)

if(DOOM_BUILD_SDL)
    # enable_language() must run at directory scope, not inside the functions
    # below, and before SDL is added as a subproject.
    if(DOOM_RENDER3D)
        enable_language(CXX)
    endif()
    if(APPLE)
        enable_language(OBJCXX)
    endif()
    find_package(SDL3 3.2 CONFIG QUIET)
    if(NOT SDL3_FOUND)
        include(FetchContent)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TESTS OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(SDL3
            URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
            URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3)
        FetchContent_MakeAvailable(SDL3)
    endif()
    # Nuked OPL3 (LGPL-2.1+) emulates the FM chip for OPL music.
    include(FetchContent)
    FetchContent_Declare(NukedOPL3
        URL https://github.com/nukeykt/Nuked-OPL3/archive/765ec962e473aeb767e4cba74ffdc8f588ffbfe8.tar.gz
        URL_HASH SHA256=2fad908c3904d3ef51b55e0d6a8980c7142942e2d0baa7e99bf038cf0a41199d)
    FetchContent_MakeAvailable(NukedOPL3)
    add_library(nuked_opl3 STATIC "${nukedopl3_SOURCE_DIR}/opl3.c")
    target_include_directories(nuked_opl3 PUBLIC "${nukedopl3_SOURCE_DIR}")
    # TinySoundFont (MIT) plays General MIDI scores from a SoundFont bank.
    FetchContent_Declare(TinySoundFont
        URL https://github.com/schellingb/TinySoundFont/archive/853a0a171759f1ddba0de1442133a75912bbeffa.tar.gz
        URL_HASH SHA256=76c356df524f71f2a34f67fa0cb2cb946036fe7a65d2930a849569c5e742fa57)
    FetchContent_MakeAvailable(TinySoundFont)
    # Music: the score loaders and player, the OPL synth (built from the chip
    # manual and the lump formats, see platform/audio/opl_music_spec.md) and
    # the SoundFont synth. platform/i_music.c and platform/audio/mixer_music.c
    # are the game's side.
    add_library(doom_music STATIC platform/audio/music_score.c platform/audio/music_player.c
        platform/audio/opl_chip.c platform/audio/opl_genmidi.c platform/audio/opl_voices.c
        platform/audio/sf2_synth.c)
    target_include_directories(doom_music PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/platform/audio")
    target_include_directories(doom_music PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/linuxdoom-1.10")
    target_include_directories(doom_music PRIVATE "${tinysoundfont_SOURCE_DIR}")
    target_link_libraries(doom_music PUBLIC nuked_opl3 doom_options)
    if(NOT WIN32)
        target_link_libraries(doom_music PUBLIC m)
    endif()
    # GeneralUser GS v2.0.3 (S. Christian Collins; its own license allows
    # bundling) is the default SoundFont bank, beside the game in soundfonts/.
    if(DOOM_SOUNDFONT)
        set(DOOM_SOUNDFONT_COMMIT 97049183643d5fc5a9322a69c5b09efb667c6c3a)
        set(DOOM_SOUNDFONT_DIR "${CMAKE_CURRENT_BINARY_DIR}/soundfonts")
        # A source release carries the bank, so it builds offline.
        set(DOOM_SOUNDFONT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/soundfonts")
        if(NOT EXISTS "${DOOM_SOUNDFONT_DIR}/GeneralUser-GS.sf2" AND EXISTS "${DOOM_SOUNDFONT_SOURCE}/GeneralUser-GS.sf2")
            file(COPY "${DOOM_SOUNDFONT_SOURCE}/GeneralUser-GS.sf2" "${DOOM_SOUNDFONT_SOURCE}/GeneralUser-GS-LICENSE.txt"
                DESTINATION "${DOOM_SOUNDFONT_DIR}")
        endif()
        if(NOT EXISTS "${DOOM_SOUNDFONT_DIR}/GeneralUser-GS.sf2")
            file(DOWNLOAD
                "https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/${DOOM_SOUNDFONT_COMMIT}/GeneralUser-GS.sf2"
                "${DOOM_SOUNDFONT_DIR}/GeneralUser-GS.sf2"
                EXPECTED_HASH SHA256=9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe)
        endif()
        if(NOT EXISTS "${DOOM_SOUNDFONT_DIR}/GeneralUser-GS-LICENSE.txt")
            file(DOWNLOAD
                "https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/${DOOM_SOUNDFONT_COMMIT}/documentation/LICENSE.txt"
                "${DOOM_SOUNDFONT_DIR}/GeneralUser-GS-LICENSE.txt")
        endif()
    endif()
    # Renders a WAD's music to WAV files without the game, through either synth:
    # doom-music-render <wad> [-song D_E1M1|all] [-synth opl|sf2] [-sf2 bank.sf2] ...
    add_executable(doom-music-render tests/music_render.c)
    target_link_libraries(doom-music-render PRIVATE doom_music)
    target_include_directories(doom-music-render PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/linuxdoom-1.10")
    # Renders the made-up effects (sparks, dying lamps, glass) after a pistol shot, and the ambient
    # sounds of lamps, screens, liquids and fire, dry and in rooms:
    # doom-sfx-render <wad> [-kind name|ambient|all] [-room dry|room|hall|all] [-rate 11025] [-level 0.6] [-gun on|off] [-out dir]
    add_executable(doom-sfx-render tests/sfx_render.c platform/audio/impact_synth.c platform/audio/spark_synth.c
        platform/audio/glass_synth.c platform/audio/fracture_synth.c platform/audio/step_synth.c platform/audio/ambient_synth.c platform/audio/machine_synth.c
        platform/audio/liquid_synth.c platform/audio/fire_synth.c platform/audio/resample.c platform/audio/reverb.c)
    target_include_directories(doom-sfx-render PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/platform")
    if(NOT WIN32)
        target_link_libraries(doom-sfx-render PRIVATE m)
    endif()
    # The 3D scene, one file per subsystem; scene3d/internal.h holds what they share.
    set(DOOM_SCENE3D_SOURCES
        platform/scene3d/atmosphere.cpp
        platform/scene3d/bake_atlas.cpp
        platform/scene3d/bake_indirect.cpp
        platform/scene3d/bake_lights.cpp
        platform/scene3d/bake_sun.cpp
        platform/scene3d/glass_light.cpp
        platform/scene3d/blood.cpp
        platform/scene3d/caustics.cpp
        platform/scene3d/liquid_edges.cpp
        platform/scene3d/embers.cpp
        platform/scene3d/config.cpp
        platform/scene3d/exposure.cpp
        platform/scene3d/geometry.cpp
        platform/scene3d/images.cpp
        platform/scene3d/impacts.cpp
        platform/scene3d/lights.cpp
        platform/scene3d/liquids.cpp
        platform/scene3d/loading.cpp
        platform/scene3d/map.cpp
        platform/scene3d/relight.cpp
        platform/scene3d/scene.cpp
        platform/scene3d/shadows.cpp
        platform/scene3d/shot_lights.cpp
        platform/scene3d/sprite_normals.cpp
        platform/scene3d/surface_lights.cpp
        platform/scene3d/thing_light.cpp
        platform/scene3d/weapon.cpp
        platform/scene3d/windows.cpp
    )
    # The SDL_gpu backend that draws it; gpu3d/internal.h holds what its files share.
    set(DOOM_GPU3D_SOURCES
        platform/gpu3d/device.cpp
        platform/gpu3d/draw.cpp
        platform/gpu3d/frame.cpp
        platform/gpu3d/loading.cpp
        platform/gpu3d/pipelines.cpp
    )
    if(NOT MSVC)
        # The engine headers' #pragma interface, once per file.
        set_source_files_properties(${DOOM_SCENE3D_SOURCES} ${DOOM_GPU3D_SOURCES} platform/settings_menu.cpp PROPERTIES COMPILE_OPTIONS -Wno-unknown-pragmas)
    endif()
    # The accelerated 3D renderer: SDL_gpu drives Metal on macOS and Vulkan on
    # Linux and Windows (SDL_GPU_DRIVER=vulkan selects MoltenVK on a Mac).
    function(doom_add_renderer target enabled)
        if(NOT enabled)
            return()
        endif()
        target_sources(${target} PRIVATE ${DOOM_SCENE3D_SOURCES} ${DOOM_GPU3D_SOURCES} platform/settings_panel.cpp
            platform/settings_menu.cpp)
        set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
        target_compile_definitions(${target} PRIVATE DOOM_RENDER3D)
    endfunction()
    # Sound in the map for the SDL mixer: room reverb, muffling, the map reads they share, sample playback,
    # and the made-up sounds: impacts and the ambience read from the map's textures.
    set(DOOM_AUDIO_SOURCES platform/audio/map.c platform/audio/occlusion.c platform/audio/resample.c platform/audio/reverb.c platform/audio/room.c
        platform/audio/treble.c platform/audio/made_bank.c platform/audio/ambience.c platform/audio/ambience_map.c platform/audio/ambience_voices.c platform/audio/acoustic_graph.c platform/audio/steps.c
        platform/audio/impact_synth.c platform/audio/spark_synth.c platform/audio/glass_synth.c platform/audio/fracture_synth.c platform/audio/step_synth.c
        platform/audio/ambient_synth.c platform/audio/machine_synth.c platform/audio/liquid_synth.c platform/audio/fire_synth.c)
    add_executable(doom MACOSX_BUNDLE platform/sdl_main.c platform/i_sdl_video.c
        platform/i_sdl_sound.c platform/i_music.c platform/audio/mixer_music.c platform/i_singleplayer.c ${DOOM_AUDIO_SOURCES}
        platform/i_portable_system.c platform/wad_probe.c)
    target_link_libraries(doom PRIVATE doom_game SDL3::SDL3 doom_music)
    target_compile_definitions(doom PRIVATE DOOM_NATIVE)
    set_target_properties(doom PROPERTIES WIN32_EXECUTABLE TRUE)
    doom_add_renderer(doom ${DOOM_RENDER3D})
    if(APPLE)
        target_sources(doom PRIVATE platform/i_mac_music.c platform/i_wad_launcher.mm)
        set_source_files_properties(platform/i_wad_launcher.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc;-D_DARWIN_C_SOURCE")
        target_link_libraries(doom PRIVATE "-framework AudioToolbox" "-framework CoreFoundation" "-framework Cocoa")
        # The identifier keeps the project's old name: changing it would make
        # macOS treat the app as new and forget the launcher's WAD folder.
        set_target_properties(doom PROPERTIES
            OUTPUT_NAME "Play DOOM"
            OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED YES
            MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.robongrui.duct-tape-doom"
            MACOSX_BUNDLE_BUNDLE_NAME "Play DOOM"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/platform/Info.plist.in")
        if(DOOM_SOUNDFONT)
            add_custom_command(TARGET doom POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:doom>/Contents/Resources/soundfonts"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DOOM_SOUNDFONT_DIR}/GeneralUser-GS.sf2"
                    "${DOOM_SOUNDFONT_DIR}/GeneralUser-GS-LICENSE.txt" "$<TARGET_BUNDLE_DIR:doom>/Contents/Resources/soundfonts")
        endif()
        if(DOOM_GAME_DATA)
            add_custom_command(TARGET doom POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:doom>/Contents/Resources"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DOOM_GAME_DATA}"
                    "$<TARGET_BUNDLE_DIR:doom>/Contents/Resources/default.wad")
        endif()
    else()
        if(DOOM_SOUNDFONT)
            add_custom_command(TARGET doom POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:doom>/soundfonts"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DOOM_SOUNDFONT_DIR}/GeneralUser-GS.sf2"
                    "${DOOM_SOUNDFONT_DIR}/GeneralUser-GS-LICENSE.txt" "$<TARGET_FILE_DIR:doom>/soundfonts")
        endif()
    endif()
    if(NOT APPLE AND DOOM_GAME_DATA)
        add_custom_command(TARGET doom POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DOOM_GAME_DATA}" "$<TARGET_FILE_DIR:doom>/default.wad")
    endif()
    # The Heretic program: derp-heretic -iwad heretic.wad
    add_executable(derp-heretic platform/sdl_main.c platform/i_sdl_video.c
        platform/i_sdl_sound.c platform/i_music.c platform/audio/mixer_music.c platform/i_singleplayer.c ${DOOM_AUDIO_SOURCES}
        platform/i_portable_system.c platform/wad_probe.c)
    target_link_libraries(derp-heretic PRIVATE heretic_game SDL3::SDL3 doom_music)
    target_compile_definitions(derp-heretic PRIVATE DOOM_NATIVE)
    doom_add_renderer(derp-heretic ${DOOM_RENDER3D})
    if(APPLE)
        target_sources(derp-heretic PRIVATE platform/i_mac_music.c platform/i_wad_launcher.mm)
        target_link_libraries(derp-heretic PRIVATE "-framework AudioToolbox" "-framework CoreFoundation" "-framework Cocoa")
        set_target_properties(derp-heretic PROPERTIES OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED YES)
        # One app plays both: the launcher hands Heretic WADs to derp-heretic
        # beside Play DOOM in Contents/MacOS (sdl_main.c).
        add_dependencies(doom derp-heretic)
        # A target of its own, so a Heretic-only change reaches the bundle
        # even when Play DOOM itself doesn't relink.
        add_custom_target(derp-heretic-bundle ALL
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:derp-heretic>"
                "$<TARGET_BUNDLE_DIR:doom>/Contents/MacOS/derp-heretic"
            VERBATIM)
        add_dependencies(derp-heretic-bundle doom derp-heretic)
    endif()
    if(BUILD_TESTING)
        function(doom_add_smoke target game renderer)
            add_executable(${target} ${ARGN} platform/i_sdl_video.c
                platform/i_sdl_sound.c platform/i_music.c platform/audio/mixer_music.c platform/i_singleplayer.c ${DOOM_AUDIO_SOURCES}
                platform/i_portable_system.c)
            target_compile_definitions(${target} PRIVATE DOOM_TEST_DRIVER)
            target_link_libraries(${target} PRIVATE ${game} SDL3::SDL3 doom_music)
            doom_add_renderer(${target} ${renderer})
            if(APPLE)
                target_sources(${target} PRIVATE platform/i_mac_music.c)
                target_link_libraries(${target} PRIVATE "-framework AudioToolbox" "-framework CoreFoundation")
            endif()
        endfunction()
        # Plays every map of a WAD and reports slow views and their causes:
        # doom-perf-smoke <iwad> [-maps MAP01,MAP02] [-spacing units] [-budget ms]
        if(DOOM_RENDER3D)
            doom_add_smoke(doom-perf-smoke doom_game ON tests/perf_smoke.c)
        endif()
    endif()
    if(BUILD_TESTING AND DOOM_GAME_DATA)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/smoke-data")
        doom_add_smoke(doom-native-smoke doom_game OFF tests/native_smoke.c)
        add_test(NAME native-gameplay-save-load COMMAND doom-native-smoke "${DOOM_GAME_DATA}")
        set_tests_properties(native-gameplay-save-load PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/smoke-data"
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
            TIMEOUT 30)
        if(DOOM_TEST_GPU)
            doom_add_smoke(doom-gpu-smoke doom_game ON tests/native_smoke.c)
            add_test(NAME gpu-gameplay-save-load COMMAND doom-gpu-smoke "${DOOM_GAME_DATA}")
            set_tests_properties(gpu-gameplay-save-load PROPERTIES
                WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/smoke-data" TIMEOUT 60)
            # One map, coarsely: checks the profiling path, not the timings.
            add_test(NAME gpu-performance-smoke COMMAND doom-perf-smoke "${DOOM_GAME_DATA}" -maps E1M1 -spacing 1024
                -out perf-smoke -graphics smoke-graphics.cfg)
            set_tests_properties(gpu-performance-smoke PROPERTIES
                WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/smoke-data" TIMEOUT 120)
        endif()
    endif()
endif()

# platform/gpu_shaders.h is generated from platform/shaders and committed;
# `cmake --build <dir> --target doom-gpu-shaders` regenerates it.
find_program(DOOM_GLSLANG glslangValidator)
find_program(DOOM_SPIRV_CROSS spirv-cross)
if(DOOM_GLSLANG AND DOOM_SPIRV_CROSS)
    add_custom_target(doom-gpu-shaders
        COMMAND ${CMAKE_COMMAND} -DGLSLANG=${DOOM_GLSLANG} -DSPIRV_CROSS=${DOOM_SPIRV_CROSS} -DWORK=${CMAKE_CURRENT_BINARY_DIR}/gpu-shaders
            -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_gpu_shaders.cmake
        COMMENT "Regenerating platform/gpu_shaders.h")
endif()

if(DOOM_BUILD_X11)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "The historical X11/OSS backend is Linux-only; use the portable targets here")
    endif()
    find_package(X11 REQUIRED)
    if(NOT X11_Xext_FOUND)
        message(FATAL_ERROR "The historical backend requires Xext development files")
    endif()
    add_executable(doom-x11 linuxdoom-1.10/i_main.c linuxdoom-1.10/i_net.c
        linuxdoom-1.10/i_sound.c linuxdoom-1.10/i_video.c platform/i_portable_system.c)
    target_compile_definitions(doom-x11 PRIVATE LINUX)
    target_include_directories(doom-x11 PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(doom-x11 PRIVATE doom_core ${X11_LIBRARIES} ${X11_Xext_LIB})
endif()

if(BUILD_TESTING)
    if(APPLE)
        enable_language(CXX)
        add_executable(doom-surface-lighting-tests tests/surface_lighting.cpp)
        target_compile_features(doom-surface-lighting-tests PRIVATE cxx_std_17)
        add_test(NAME surface-lighting-transitions COMMAND doom-surface-lighting-tests)
        add_executable(doom-baked-lighting-tests tests/baked_lighting.cpp)
        target_compile_features(doom-baked-lighting-tests PRIVATE cxx_std_17)
        add_test(NAME baked-lighting-rays COMMAND doom-baked-lighting-tests)
        add_executable(doom-weapon-lighting-tests tests/weapon_lighting.cpp)
        target_compile_features(doom-weapon-lighting-tests PRIVATE cxx_std_17)
        add_test(NAME weapon-delighting COMMAND doom-weapon-lighting-tests)
        add_executable(doom-sprite-eyes-tests tests/sprite_eyes.cpp)
        target_compile_features(doom-sprite-eyes-tests PRIVATE cxx_std_17)
        add_test(NAME sprite-eye-detection COMMAND doom-sprite-eyes-tests)
    endif()
    add_executable(doom-screen-glass-tests tests/screen_glass.c)
    if(UNIX AND NOT APPLE)
        target_link_libraries(doom-screen-glass-tests PRIVATE m)
    endif()
    add_test(NAME screen-glass-detection COMMAND doom-screen-glass-tests)
    add_executable(doom-wad-probe-tests tests/wad_probe.c platform/wad_probe.c)
    add_test(NAME wad-loader-probe COMMAND doom-wad-probe-tests)
    add_executable(doom-portability-tests tests/portability.c)
    target_link_libraries(doom-portability-tests PRIVATE doom_headless_platform)
    add_test(NAME portability COMMAND doom-portability-tests)
    add_test(NAME headless-help COMMAND doom-headless --help)
    add_test(NAME headless-requires-demo COMMAND doom-headless)
    if(DOOM_GLSLANG AND DOOM_SPIRV_CROSS)
        # Checks that platform/shaders compile; the committed header is not touched.
        add_test(NAME gpu-shaders-compile COMMAND ${CMAKE_COMMAND} -DGLSLANG=${DOOM_GLSLANG} -DSPIRV_CROSS=${DOOM_SPIRV_CROSS}
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/gpu_shaders_check.h -DWORK=${CMAKE_CURRENT_BINARY_DIR}/gpu-shaders
            -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_gpu_shaders.cmake)
    endif()
    set_tests_properties(headless-requires-demo PROPERTIES WILL_FAIL TRUE)
endif()
