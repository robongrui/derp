# The 3D renderer's shaders, compiled from platform/shaders as part of the build (included by
# CMakeLists.txt when DOOM_RENDER3D is on). Each shader becomes a header of its own in
# <build>/gpu-shaders (scripts/compile_gpu_shader.cmake); gpu_shaders.h and gpu_compute_shaders.h
# there include them, and pipelines.cpp and compute.cpp include those. A shader compiles again when
# its source or one of the files beside it changes. Needs glslangValidator, and spirv-cross when
# Metal is built: the Vulkan SDK (VULKAN_SDK is searched), or a system's glslang and spirv-cross.
#
# DOOM_SHADER_FORMATS picks the backends built in: this platform's own by default (Metal on macOS,
# Vulkan elsewhere); "spv;msl" for both, e.g. to try MoltenVK on a Mac (SDL_GPU_DRIVER=vulkan).
set(DOOM_SHADER_FORMATS "" CACHE STRING "Shader backends in the 3D renderer: spv (Vulkan), msl (Metal) or both; empty for the platform's own")
if(DOOM_SHADER_FORMATS)
    set(doom_shader_formats ${DOOM_SHADER_FORMATS})
elseif(APPLE)
    set(doom_shader_formats msl)
else()
    set(doom_shader_formats spv)
endif()
find_program(DOOM_GLSLANG glslangValidator HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
if(NOT DOOM_GLSLANG)
    message(FATAL_ERROR "The 3D renderer's shaders need glslangValidator: install the Vulkan SDK (https://vulkan.lunarg.com), "
        "or glslang (apt install glslang-tools, brew install glslang), or set DOOM_GLSLANG.")
endif()
if("msl" IN_LIST doom_shader_formats)
    find_program(DOOM_SPIRV_CROSS spirv-cross HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
    if(NOT DOOM_SPIRV_CROSS)
        message(FATAL_ERROR "The 3D renderer's Metal shaders need spirv-cross: install the Vulkan SDK (https://vulkan.lunarg.com), "
            "or SPIRV-Cross (apt install spirv-cross, brew install spirv-cross), or set DOOM_SPIRV_CROSS.")
    endif()
endif()

# name|source|defines (comma separated)
set(doom_gpu_shaders
    "world_vert|world.vert|"
    "reflect_vert|world.vert|REFLECT"
    "screen_vert|screen.vert|"
    "sky_vert|screen.vert|SKY"
    "present_vert|screen.vert|PRESENT"
    "world_frag|world.frag|"
    "opaque_frag|world.frag|OPAQUE"
    "plain_frag|world.frag|OPAQUE,PLAIN"
    "plain_walls_frag|world.frag|OPAQUE,PLAIN,WALLS"
    "plain_walls_quiet_frag|world.frag|OPAQUE,PLAIN,WALLS,QUIET"
    "plain_walls_air_frag|world.frag|OPAQUE,PLAIN,WALLS,CLEAR_AIR"
    "plain_walls_quiet_air_frag|world.frag|OPAQUE,PLAIN,WALLS,QUIET,CLEAR_AIR"
    "plain_flats_frag|world.frag|OPAQUE,PLAIN,FLATS"
    "plain_flats_quiet_frag|world.frag|OPAQUE,PLAIN,FLATS,QUIET"
    "plain_flats_air_frag|world.frag|OPAQUE,PLAIN,FLATS,CLEAR_AIR"
    "plain_flats_quiet_air_frag|world.frag|OPAQUE,PLAIN,FLATS,QUIET,CLEAR_AIR"
    "cutout_frag|world.frag|CUTOUT"
    "coverage_frag|world.frag|COVERAGE"
    "core_frag|world.frag|COVERAGE,CORE"
    "overlay_frag|world.frag|OVERLAY"
    "sky_frag|sky.frag|"
    "sky_surface_frag|sky.frag|SURFACE"
    "sky_field_frag|sky.frag|SKY_CLEAR,SKY_CITY=0,SKY_HELL=0,SKY_SPIRE=0,SKY_TREE=0,SKY_FIELD=1,SKY_CLOUD=0"
    "sky_surface_field_frag|sky.frag|SURFACE,SKY_CLEAR,SKY_CITY=0,SKY_HELL=0,SKY_SPIRE=0,SKY_TREE=0,SKY_FIELD=1,SKY_CLOUD=0"
    "sky_field_cloud_frag|sky.frag|SKY_CLEAR,SKY_CITY=0,SKY_HELL=0,SKY_SPIRE=0,SKY_TREE=0,SKY_FIELD=1,SKY_CLOUD=1"
    "sky_surface_field_cloud_frag|sky.frag|SURFACE,SKY_CLEAR,SKY_CITY=0,SKY_HELL=0,SKY_SPIRE=0,SKY_TREE=0,SKY_FIELD=1,SKY_CLOUD=1"
    "sky_cloud_frag|sky.frag|SKY_CLEAR,SKY_CITY=0,SKY_HELL=0,SKY_SPIRE=0,SKY_TREE=0,SKY_FIELD=0,SKY_CLOUD=1"
    "sky_surface_cloud_frag|sky.frag|SURFACE,SKY_CLEAR,SKY_CITY=0,SKY_HELL=0,SKY_SPIRE=0,SKY_TREE=0,SKY_FIELD=0,SKY_CLOUD=1"
    "sky_bake_frag|sky.frag|SKY_BAKE"
    "sky_cached_frag|sky.frag|SKY_CACHED"
    "sky_cached_surface_frag|sky.frag|SKY_CACHED,SURFACE"
    "shadow_frag|shadow.frag|"
    "decal_frag|shadow.frag|DECAL"
    "particle_frag|flat.frag|"
    "solid_frag|flat.frag|SOLID"
    "heat_frag|flat.frag|HEAT"
    "panel_frag|panel.frag|"
    "emission_frag|emission.frag|"
    "blur_frag|blur.frag|"
    "mist_frag|mist.frag|"
    "present_frag|present.frag|"
    "depth_frag|depth.frag|"
    "glass_frag|glass.frag|")
# The bakes' kernels (PLAN-gpu-bakes.md).
set(doom_gpu_kernels
    "fill_comp|compute/fill.comp|"
    "trace_rays_comp|compute/trace_rays.comp|"
    "ambient_comp|compute/ambient.comp|"
    "sun_comp|compute/sun.comp|"
    "bounce_trace_comp|compute/bounce_trace.comp|"
    "bounce_gather_comp|compute/bounce_gather.comp|"
    "lights_comp|compute/lights.comp|")

set(doom_shader_dir "${CMAKE_CURRENT_SOURCE_DIR}/platform/shaders")
set(DOOM_SHADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/gpu-shaders")
file(MAKE_DIRECTORY "${DOOM_SHADER_OUT}")
# What a shader may include: the drawing shaders' files and the kernels' (compute/) apart.
file(GLOB_RECURSE doom_shader_parts CONFIGURE_DEPENDS "${doom_shader_dir}/*.glsl" "${doom_shader_dir}/*.h")
set(doom_kernel_parts ${doom_shader_parts})
list(FILTER doom_shader_parts EXCLUDE REGEX "/compute/")
list(FILTER doom_kernel_parts INCLUDE REGEX "/compute/")

set(doom_shader_outputs)
# One list into <build>/gpu-shaders/<header>: a step per shader and backend, and for a backend not
# built in an empty array of the same name (the code takes sizeof of both; the device never asks
# for that backend: device.cpp).
function(doom_shader_header header guard list parts)
    set(text "/* Generated by scripts/gpu_shaders.cmake from platform/shaders; do not edit. */\n#ifndef ${guard}\n#define ${guard}\n")
    set(outputs ${doom_shader_outputs})
    foreach(entry IN LISTS ${list})
        string(REPLACE "|" ";" fields "${entry}")
        list(GET fields 0 name)
        list(GET fields 1 source)
        list(GET fields 2 defines)
        foreach(format spv msl)
            if(NOT format IN_LIST doom_shader_formats)
                string(APPEND text "static const unsigned char doom_${format}_${name}[1]={0};\n")
                continue()
            endif()
            set(out "${DOOM_SHADER_OUT}/${name}.${format}.h")
            add_custom_command(OUTPUT "${out}"
                COMMAND ${CMAKE_COMMAND} "-DGLSLANG=${DOOM_GLSLANG}" "-DSPIRV_CROSS=${DOOM_SPIRV_CROSS}" "-DNAME=${name}"
                    "-DSOURCE=${source}" "-DDEFINES=${defines}" "-DFORMAT=${format}" "-DOUTPUT=${out}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_gpu_shader.cmake"
                DEPENDS "${doom_shader_dir}/${source}" ${${parts}} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_gpu_shader.cmake"
                COMMENT "Shader ${name} (${format})" VERBATIM)
            list(APPEND outputs "${out}")
            string(APPEND text "#include \"${name}.${format}.h\"\n")
        endforeach()
    endforeach()
    string(APPEND text "#endif\n")
    file(CONFIGURE OUTPUT "${DOOM_SHADER_OUT}/${header}" CONTENT "${text}" @ONLY)
    set(doom_shader_outputs ${outputs} PARENT_SCOPE)
endfunction()
doom_shader_header(gpu_shaders.h DOOM_GPU_SHADERS_H doom_gpu_shaders doom_shader_parts)
doom_shader_header(gpu_compute_shaders.h DOOM_GPU_COMPUTE_SHADERS_H doom_gpu_kernels doom_kernel_parts)
# `cmake --build <dir> --target doom-gpu-shaders` compiles the shaders alone; the renderer's
# programs depend on it (doom_add_renderer).
add_custom_target(doom-gpu-shaders DEPENDS ${doom_shader_outputs})
