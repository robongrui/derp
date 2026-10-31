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
    linuxdoom-1.10/f_library.c
    linuxdoom-1.10/f_wipe.c
    linuxdoom-1.10/g_game.c
    linuxdoom-1.10/hu_lib.c
    linuxdoom-1.10/hu_stuff.c
    linuxdoom-1.10/info.c
    linuxdoom-1.10/m_argv.c
    linuxdoom-1.10/m_bbox.c
    linuxdoom-1.10/m_cheat.c
    linuxdoom-1.10/m_menu.c
    linuxdoom-1.10/m_library.c
    linuxdoom-1.10/m_kiosk.c
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
    linuxdoom-1.10/p_phobos.c
    linuxdoom-1.10/p_library.c
    linuxdoom-1.10/p_library_people.c
    linuxdoom-1.10/p_library_shaft.c
    linuxdoom-1.10/p_library_day.c
    linuxdoom-1.10/p_library_books.c
    linuxdoom-1.10/p_library_census.c
    linuxdoom-1.10/p_library_reader.c
    linuxdoom-1.10/p_library_fallers.c
    linuxdoom-1.10/p_library_figures.c
    linuxdoom-1.10/p_library_kiosk.c
    linuxdoom-1.10/p_library_babel.c
    linuxdoom-1.10/p_library_save.c
    platform/library/shake256.c
    platform/library/library.c
    platform/library/book_check.c
    platform/library/biography.c
    platform/library/biography_template.c
    linuxdoom-1.10/p_boots.c
    linuxdoom-1.10/p_weather.c
    linuxdoom-1.10/p_air.c
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
    linuxdoom-1.10/st_library.c
    linuxdoom-1.10/st_stuff.c
    linuxdoom-1.10/tables.c
    linuxdoom-1.10/v_video.c
    linuxdoom-1.10/wi_stuff.c
)
# The engine parts every game program shares: WAD access, the zone, byte
# order and fixed point.
add_library(doom_engine STATIC linuxdoom-1.10/m_fixed.c linuxdoom-1.10/m_swap.c
    linuxdoom-1.10/w_wad.c linuxdoom-1.10/z_zone.c platform/derp_info.c)
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
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/games/doom/materials.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/games/doom/authored.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/games/doom/map_fixes.c")
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
    # doom-sfx-render <wad> [-kind name|ambient|all] [-room dry|room|hall|all] [-rate Hz] [-level 0.6] [-gun on|off] [-out dir]
    # Without -rate, each effect uses the same native rate as its game bank.
    add_executable(doom-sfx-render tests/sfx_render.c platform/audio/impact_synth.c platform/audio/spark_synth.c
        platform/audio/glass_synth.c platform/audio/fracture_synth.c platform/audio/step_synth.c platform/audio/ambient_synth.c platform/audio/machine_synth.c
        platform/audio/liquid_synth.c platform/audio/fire_synth.c platform/audio/flyby_synth.c platform/audio/gore_synth.c platform/audio/hidden_synth.c platform/audio/resample.c platform/audio/reverb.c)
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
        platform/scene3d/spray.cpp
        platform/scene3d/body_motion.cpp
        platform/scene3d/death_director.cpp
        platform/scene3d/decompression.cpp
        platform/scene3d/death_falls.cpp
        platform/scene3d/ragdoll.cpp
        platform/scene3d/ragdoll_body.cpp
        platform/scene3d/ragdoll_test.cpp
        platform/scene3d/wounds.cpp
        platform/scene3d/carried.cpp
        platform/scene3d/book_view.cpp
        platform/scene3d/rush.cpp
        platform/scene3d/eyes.cpp
        platform/scene3d/speech.cpp
        platform/scene3d/caustics.cpp
        platform/scene3d/liquid_edges.cpp
        platform/scene3d/embers.cpp
        platform/scene3d/config.cpp
        platform/scene3d/window_size.cpp
        platform/scene3d/exposure.cpp
        platform/scene3d/geometry.cpp
        platform/scene3d/images.cpp
        platform/scene3d/impacts.cpp
        platform/scene3d/lights.cpp
        platform/scene3d/liquids.cpp
        platform/scene3d/puddles.cpp
        platform/scene3d/loading.cpp
        platform/scene3d/map.cpp
        platform/scene3d/relight.cpp
        platform/scene3d/scene.cpp
        platform/scene3d/pacing.cpp
        platform/scene3d/frame_stats.cpp
        platform/scene3d/stats_overlay.cpp
        platform/scene3d/shadows.cpp
        platform/scene3d/shot_lights.cpp
        platform/scene3d/space.cpp
        platform/scene3d/sprite_normals.cpp
        platform/scene3d/surface_lights.cpp
        platform/scene3d/thing_light.cpp
        platform/scene3d/weapon.cpp
        platform/scene3d/wear.cpp
        platform/scene3d/weather.cpp
        platform/scene3d/sun.cpp
        platform/scene3d/wind.cpp
        platform/scene3d/climate.cpp
        platform/scene3d/ground_heat.cpp
        platform/scene3d/trapped_air.cpp
        platform/scene3d/drafts.cpp
        platform/scene3d/rooms.cpp
        platform/scene3d/rain.cpp
        platform/scene3d/nukage_rain.cpp
        platform/scene3d/craters.cpp
        platform/scene3d/snow.cpp
        platform/scene3d/sand.cpp
        platform/scene3d/whiteout.cpp
        platform/scene3d/water_ice.cpp
        platform/scene3d/ash.cpp
        platform/scene3d/fog.cpp
        platform/scene3d/dry_ice.cpp
        platform/scene3d/dry_ice_basins.cpp
        platform/scene3d/dry_ice_intake.cpp
        platform/scene3d/floor_film.cpp
        platform/scene3d/blown_in.cpp
        platform/scene3d/air_push.cpp
        platform/scene3d/beam_air.cpp
        platform/scene3d/open_air.cpp
        platform/scene3d/disturbances.cpp
        platform/scene3d/powder.cpp
        platform/scene3d/disturbance_sources.cpp
        platform/scene3d/ground_dents.cpp
        platform/scene3d/windows.cpp
        platform/scene3d/wall_mirrors.cpp
    )
    # The SDL_gpu backend that draws it; gpu3d/internal.h holds what its files share.
    set(DOOM_GPU3D_SOURCES
        platform/gpu3d/device.cpp
        platform/gpu3d/draw.cpp
        platform/gpu3d/frame.cpp
        platform/gpu3d/presentation.cpp
        platform/gpu3d/loading.cpp
        platform/gpu3d/pipelines.cpp
    )
    # Local dev tools (devtools/, not published): devtools.cmake adds their sources to the
    # scene's and sets DOOM_DEVTOOLS, which builds the hooks they use.
    include(devtools/devtools.cmake OPTIONAL)
    # The FPS readout's version line: the release's tag, else a local build's.
    if(DOOM_RELEASE_VERSION STREQUAL "0.1.0")
        set(DOOM_SHOWN_VERSION "DEV")
    else()
        set(DOOM_SHOWN_VERSION "${DOOM_RELEASE_VERSION}")
    endif()
    set_source_files_properties(platform/scene3d/stats_overlay.cpp PROPERTIES COMPILE_DEFINITIONS "DOOM_SHOWN_VERSION=\"${DOOM_SHOWN_VERSION}\"")
    if(NOT MSVC)
        # The engine headers' #pragma interface, once per file.
        set_source_files_properties(${DOOM_SCENE3D_SOURCES} ${DOOM_GPU3D_SOURCES} platform/settings_menu.cpp platform/small_font.cpp PROPERTIES COMPILE_OPTIONS -Wno-unknown-pragmas)
    endif()
    # The accelerated 3D renderer: SDL_gpu drives Metal on macOS and Vulkan on
    # Linux and Windows (SDL_GPU_DRIVER=vulkan selects MoltenVK on a Mac).
    function(doom_add_renderer target enabled)
        if(NOT enabled)
            return()
        endif()
        target_sources(${target} PRIVATE ${DOOM_SCENE3D_SOURCES} ${DOOM_GPU3D_SOURCES} platform/settings_panel.cpp
            platform/settings_menu.cpp platform/small_font.cpp platform/cpu_time.c)
        set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
        target_compile_definitions(${target} PRIVATE DOOM_RENDER3D)
        if(DOOM_DEVTOOLS)
            target_compile_definitions(${target} PRIVATE DOOM_DEVTOOLS)
        endif()
    endfunction()
    # Sound in the map for the SDL mixer: room reverb, muffling, the map reads they share, sample playback,
    # and the made-up sounds: impacts and the ambience read from the map's textures. The room map
    # (rooms.c) is for the renderer and the sound alike, so every program builds it here.
    set(DOOM_AUDIO_SOURCES platform/rooms.c platform/audio/map.c platform/audio/occlusion.c platform/audio/resample.c platform/audio/reverb.c platform/audio/room.c platform/audio/beyond.c platform/audio/relief.c platform/audio/flyby.c
        platform/audio/treble.c platform/audio/made_bank.c platform/audio/ambience.c platform/audio/ambience_map.c platform/audio/ambience_voices.c platform/audio/acoustic_graph.c platform/audio/steps.c platform/audio/frozen.c
        platform/audio/impact_synth.c platform/audio/spark_synth.c platform/audio/glass_synth.c platform/audio/fracture_synth.c platform/audio/step_synth.c
        platform/audio/ambient_synth.c platform/audio/machine_synth.c platform/audio/computer_synth.c platform/audio/liquid_synth.c platform/audio/fire_synth.c
        platform/audio/flyby_synth.c platform/audio/gore_synth.c platform/audio/hidden_synth.c platform/audio/hidden.c platform/audio/vacuum.c
        platform/audio/ringing.c platform/audio/head_knock.c platform/audio/falling_air.c platform/audio/library_steps.c
        platform/audio/library_acoustics.c)
    add_executable(doom MACOSX_BUNDLE platform/sdl_main.c platform/i_sdl_video.c
        platform/i_sdl_sound.c platform/i_music.c platform/audio/mixer_music.c platform/i_singleplayer.c ${DOOM_AUDIO_SOURCES}
        platform/i_portable_system.c platform/wad_probe.c)
    target_link_libraries(doom PRIVATE doom_game SDL3::SDL3 doom_music)
    target_compile_definitions(doom PRIVATE DOOM_NATIVE)
    set_target_properties(doom PROPERTIES WIN32_EXECUTABLE TRUE)
    doom_add_renderer(doom ${DOOM_RENDER3D})
    if(APPLE)
        target_sources(doom PRIVATE platform/i_mac_music.c platform/i_wad_launcher.mm platform/icon/derp.icns)
        set_source_files_properties(platform/i_wad_launcher.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc;-D_DARWIN_C_SOURCE")
        # Made from platform/icon/derp-logo.png by scripts/make-mac-icon.swift.
        set_source_files_properties(platform/icon/derp.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        target_link_libraries(doom PRIVATE "-framework AudioToolbox" "-framework CoreFoundation" "-framework Cocoa")
        # The identifier keeps the project's old name: changing it would make
        # macOS treat the app as new and forget the launcher's WAD folder.
        set_target_properties(doom PROPERTIES
            OUTPUT_NAME "Play DERP"
            OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED YES
            MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.robongrui.duct-tape-doom"
            MACOSX_BUNDLE_BUNDLE_NAME "Play DERP"
            MACOSX_BUNDLE_ICON_FILE derp.icns
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
        if(WIN32)
            target_sources(doom PRIVATE platform/icon/derp.rc)
            set_target_properties(doom PROPERTIES OUTPUT_NAME "Play DERP")
        else()
            # The window icon, loaded beside the program (i_sdl_video.c).
            # Made from platform/icon/derp-logo.png by scripts/make-icons.py.
            set_target_properties(doom PROPERTIES OUTPUT_NAME play-derp)
            add_custom_command(TARGET doom POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/platform/icon/derp.png"
                    "$<TARGET_FILE_DIR:doom>/play-derp.png")
        endif()
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
    # Episode 0.5's map (scp_7179_b/library.wad, committed), shipped beside
    # the game, which loads it (d_main.c, I_ProgramFile). Its generator
    # (scp_7179_b/mapgen) is private: where it is, the map is generated by it
    # and the committed copy kept the same; elsewhere the committed copy is it.
    set(library_map "${CMAKE_CURRENT_SOURCE_DIR}/scp_7179_b/library.wad")
    set(library_generator "${CMAKE_CURRENT_SOURCE_DIR}/scp_7179_b/mapgen/library.py")
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND AND EXISTS "${library_generator}")
        file(GLOB library_map_sources "${CMAKE_CURRENT_SOURCE_DIR}/scp_7179_b/mapgen/*.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/scp_7179_b/ending/*.wav")
        add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/library.wad"
            COMMAND Python3::Interpreter "${library_generator}" "${CMAKE_CURRENT_BINARY_DIR}/library.wad"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/library.wad" "${library_map}"
            DEPENDS ${library_map_sources} "${CMAKE_CURRENT_SOURCE_DIR}/platform/library/layout.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/scp_7179_b/words/2of12.txt"
            COMMENT "Generating episode 0.5's map")
    elseif(EXISTS "${library_map}")
        add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/library.wad"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${library_map}" "${CMAKE_CURRENT_BINARY_DIR}/library.wad"
            DEPENDS "${library_map}"
            COMMENT "Episode 0.5's map, as committed")
    endif()
    if(EXISTS "${library_generator}" OR EXISTS "${library_map}")
        add_custom_target(doom-library-map DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/library.wad")
        add_dependencies(doom doom-library-map)
        if(APPLE)
            add_custom_command(TARGET doom POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:doom>/Contents/Resources"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/library.wad"
                    "$<TARGET_BUNDLE_DIR:doom>/Contents/Resources/library.wad")
        else()
            add_custom_command(TARGET doom POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/library.wad"
                    "$<TARGET_FILE_DIR:doom>/library.wad")
        endif()
    endif()
    # The Heretic program: derp-heretic -iwad heretic.wad
    add_executable(derp-heretic platform/sdl_main.c platform/i_sdl_video.c
        platform/i_sdl_sound.c platform/i_music.c platform/audio/mixer_music.c platform/i_singleplayer.c ${DOOM_AUDIO_SOURCES}
        platform/i_portable_system.c platform/wad_probe.c)
    target_link_libraries(derp-heretic PRIVATE heretic_game SDL3::SDL3 doom_music)
    target_compile_definitions(derp-heretic PRIVATE DOOM_NATIVE)
    doom_add_renderer(derp-heretic ${DOOM_RENDER3D})
    if(WIN32)
        target_sources(derp-heretic PRIVATE platform/icon/derp.rc)
    endif()
    if(APPLE)
        target_sources(derp-heretic PRIVATE platform/i_mac_music.c platform/i_wad_launcher.mm)
        target_link_libraries(derp-heretic PRIVATE "-framework AudioToolbox" "-framework CoreFoundation" "-framework Cocoa")
        set_target_properties(derp-heretic PROPERTIES OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED YES)
        # One app plays both: the launcher hands Heretic WADs to derp-heretic
        # beside Play DERP in Contents/MacOS (sdl_main.c).
        add_dependencies(doom derp-heretic)
        # A target of its own, so a Heretic-only change reaches the bundle
        # even when Play DERP itself doesn't relink.
        add_custom_target(derp-heretic-bundle ALL
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:derp-heretic>"
                "$<TARGET_BUNDLE_DIR:doom>/Contents/MacOS/derp-heretic"
            VERBATIM)
        add_dependencies(derp-heretic-bundle doom derp-heretic)
    endif()
    if(BUILD_TESTING)
        add_executable(doom-movement-audio-tests tests/movement_audio.c
            platform/audio/resample.c platform/audio/treble.c platform/audio/step_synth.c platform/audio/frozen.c)
        target_include_directories(doom-movement-audio-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/linuxdoom-1.10"
            "${CMAKE_CURRENT_SOURCE_DIR}/platform/audio")
        target_link_libraries(doom-movement-audio-tests PRIVATE SDL3::SDL3 doom_options)
        if(NOT WIN32)
            target_link_libraries(doom-movement-audio-tests PRIVATE m)
        endif()
        add_test(NAME movement-audio-routing COMMAND doom-movement-audio-tests)
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
        # Measures what the reverb hears at spots of a level, hidden (tests/acoustic_levels.py drives it):
        # doom-room-probe <iwad> [-file pwad] [-warp ...] [-out dir] [-at name:x,y,facing]...
        doom_add_smoke(doom-room-probe doom_game OFF tests/room_probe.c)
        # Plays every map of a WAD and reports slow views and their causes:
        # doom-perf-smoke <iwad> [-maps MAP01,MAP02] [-spacing units] [-budget ms]
        if(DOOM_RENDER3D)
            doom_add_smoke(doom-perf-smoke doom_game ON tests/perf_smoke.c)
            doom_add_smoke(doom-glass-smoke doom_game ON tests/glass_smoke.cpp)
            add_executable(doom-glass-fracture-tests tests/glass_fracture.cpp)
            target_compile_features(doom-glass-fracture-tests PRIVATE cxx_std_17)
            add_test(NAME glass-fracture-geometry COMMAND doom-glass-fracture-tests)
            add_executable(doom-wound-attachment-tests tests/wound_attachment.cpp)
            target_compile_features(doom-wound-attachment-tests PRIVATE cxx_std_17)
            add_test(NAME wound-animation-attachment COMMAND doom-wound-attachment-tests)
            add_executable(doom-wounds-tests tests/wounds.cpp platform/scene3d/wounds.cpp)
            target_compile_features(doom-wounds-tests PRIVATE cxx_std_17)
            target_include_directories(doom-wounds-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/linuxdoom-1.10")
            target_link_libraries(doom-wounds-tests PRIVATE doom_options SDL3::SDL3)
            if(NOT MSVC)
                target_compile_options(doom-wounds-tests PRIVATE -Wno-unknown-pragmas)
            endif()
            add_test(NAME wound-hit-placement COMMAND doom-wounds-tests)
        endif()
    endif()
    if(BUILD_TESTING AND DOOM_GAME_DATA)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/smoke-data")
        doom_add_smoke(doom-native-smoke doom_game OFF tests/native_smoke.c)
        add_test(NAME native-gameplay-save-load COMMAND doom-native-smoke "${DOOM_GAME_DATA}")
        set_tests_properties(native-gameplay-save-load PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/smoke-data"
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
            TIMEOUT 30 LABELS game)
        # Room acoustics on test levels: tunnels told apart, ringing as tubes, other rooms as before.
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            add_test(NAME tunnel-acoustics COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tests/acoustic_levels.py"
                check "$<TARGET_FILE:doom-room-probe>" "${DOOM_GAME_DATA}" "${CMAKE_CURRENT_BINARY_DIR}/acoustic-levels")
            set_tests_properties(tunnel-acoustics PROPERTIES TIMEOUT 120 LABELS game)
        endif()
        if(DOOM_TEST_GPU)
            doom_add_smoke(doom-gpu-smoke doom_game ON tests/native_smoke.c)
            add_test(NAME gpu-gameplay-save-load COMMAND doom-gpu-smoke "${DOOM_GAME_DATA}")
            set_tests_properties(gpu-gameplay-save-load PROPERTIES
                WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/smoke-data" TIMEOUT 60 LABELS game)
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
    add_executable(doom-acoustic-glass-tests tests/acoustic_glass.c
        platform/audio/acoustic_graph.c platform/audio/occlusion.c)
    target_include_directories(doom-acoustic-glass-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/linuxdoom-1.10")
    target_link_libraries(doom-acoustic-glass-tests PRIVATE doom_options)
    if(UNIX AND NOT APPLE)
        target_link_libraries(doom-acoustic-glass-tests PRIVATE m)
    endif()
    add_test(NAME acoustic-glass-break COMMAND doom-acoustic-glass-tests)
    add_executable(doom-tunnel-reverb-tests tests/tunnel_reverb.c platform/audio/reverb.c)
    target_include_directories(doom-tunnel-reverb-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/platform/audio")
    if(UNIX AND NOT APPLE)
        target_link_libraries(doom-tunnel-reverb-tests PRIVATE m)
    endif()
    add_test(NAME tunnel-reverb-stability COMMAND doom-tunnel-reverb-tests)
    add_executable(doom-glass-audio-tests tests/glass_audio.c
        platform/audio/made_bank.c platform/audio/impact_synth.c platform/audio/spark_synth.c
        platform/audio/glass_synth.c platform/audio/fracture_synth.c platform/audio/step_synth.c
        platform/audio/flyby_synth.c platform/audio/gore_synth.c platform/audio/ambient_synth.c platform/audio/machine_synth.c
        platform/audio/liquid_synth.c platform/audio/fire_synth.c platform/audio/computer_synth.c platform/audio/hidden_synth.c)
    if(NOT WIN32)
        target_link_libraries(doom-glass-audio-tests PRIVATE m)
    endif()
    add_test(NAME glass-audio-synthesis COMMAND doom-glass-audio-tests)
    add_executable(doom-computer-audio-tests tests/computer_audio.c
        platform/audio/made_bank.c platform/audio/impact_synth.c platform/audio/spark_synth.c
        platform/audio/glass_synth.c platform/audio/fracture_synth.c platform/audio/step_synth.c
        platform/audio/flyby_synth.c platform/audio/gore_synth.c platform/audio/ambient_synth.c platform/audio/machine_synth.c
        platform/audio/liquid_synth.c platform/audio/fire_synth.c platform/audio/computer_synth.c platform/audio/hidden_synth.c
        platform/audio/ambience_voices.c platform/audio/resample.c)
    add_executable(doom-computer-presence-tests tests/computer_presence.c platform/audio/ambience.c)
    target_include_directories(doom-computer-presence-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/linuxdoom-1.10")
    target_link_libraries(doom-computer-presence-tests PRIVATE doom_options)
    if(NOT WIN32)
        target_link_libraries(doom-computer-audio-tests PRIVATE m)
        target_link_libraries(doom-computer-presence-tests PRIVATE m)
    endif()
    add_test(NAME computer-audio-synthesis COMMAND doom-computer-audio-tests)
    add_test(NAME computer-room-presence COMMAND doom-computer-presence-tests)
    add_executable(doom-wad-probe-tests tests/wad_probe.c platform/wad_probe.c)
    add_test(NAME wad-loader-probe COMMAND doom-wad-probe-tests)
    add_executable(doom-derp-info-tests tests/derp_info.c)
    target_link_libraries(doom-derp-info-tests PRIVATE doom_headless_platform)
    add_test(NAME derpinfo-reader COMMAND doom-derp-info-tests)
    add_executable(doom-library-tests tests/library.c platform/library/shake256.c
        platform/library/library.c platform/library/book_check.c)
    add_test(NAME library-math COMMAND doom-library-tests)
    add_executable(doom-library-biography-tests tests/library_biography.c platform/library/shake256.c
        platform/library/library.c platform/library/book_check.c platform/library/biography.c
        platform/library/biography_template.c)
    add_test(NAME library-biography COMMAND doom-library-biography-tests)
    # The library map's generator: its BSP lands every point in its own sector.
    # The generator and the scenarios built on it are private (scp_7179_b/mapgen):
    # these run only where it is.
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/scp_7179_b/mapgen/verify.py")
        add_test(NAME library-map-bsp COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/scp_7179_b/mapgen/verify.py" --people)
        # Playing the library headless (scp_7179_b/scenarios.py): its
        # shelves need DOOM II's or Freedom's textures, so only with those.
        get_filename_component(library_iwad "${DOOM_GAME_DATA}" NAME)
        string(TOLOWER "${library_iwad}" library_iwad)
        if(library_iwad MATCHES "^(freedoom1|freedoom2|doom2|tnt|plutonia)\\.wad$")
            set(library_smoke)
            if(TARGET doom-native-smoke)
                set(library_smoke --smoke "$<TARGET_FILE:doom-native-smoke>")
            endif()
            add_test(NAME library-scenarios COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/scp_7179_b/scenarios.py"
                --headless "$<TARGET_FILE:doom-headless>" --iwad "${DOOM_GAME_DATA}" ${library_smoke})
            set_tests_properties(library-scenarios PROPERTIES TIMEOUT 3600)
        endif()
    endif()
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
