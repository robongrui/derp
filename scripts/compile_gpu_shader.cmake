# One shader of platform/shaders for one backend, as a byte array in a header of its own: the
# build's step per shader (CMakeLists.txt, gpu_shaders.cmake), so they compile in parallel and only
# those whose sources changed compile again.
#   cmake -DGLSLANG=glslangValidator [-DSPIRV_CROSS=spirv-cross] -DNAME=world_frag -DSOURCE=world.frag
#         [-DDEFINES=OPAQUE,PLAIN] -DFORMAT=spv|msl -DOUTPUT=world_frag.spv.h -P scripts/compile_gpu_shader.cmake
# spv: SPIR-V for Vulkan (Windows, Linux, the Deck). msl: through SPIRV-Cross to Metal's shading
# language (macOS); METAL renumbers storage buffers for SDL_gpu's Metal layout, and MSL uses the
# SPIR-V binding numbers directly as buffer/texture/sampler indices.
cmake_minimum_required(VERSION 3.21)
get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(flags)
if(DEFINES)
    string(REPLACE "," ";" list "${DEFINES}")
    foreach(define IN LISTS list)
        list(APPEND flags "-D${define}")
    endforeach()
endif()
if(FORMAT STREQUAL "msl")
    list(APPEND flags -DMETAL)
endif()
set(spirv "${OUTPUT}.spv")
execute_process(COMMAND "${GLSLANG}" -V --target-env vulkan1.0 --quiet ${flags} -o "${spirv}" "${root}/platform/shaders/${SOURCE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Shader ${NAME} (${SOURCE}) failed:\n${output}")
endif()
set(code "${spirv}")
if(FORMAT STREQUAL "msl")
    set(code "${OUTPUT}.metal")
    execute_process(COMMAND "${SPIRV_CROSS}" --msl --msl-version 20100 --msl-decoration-binding --output "${code}" "${spirv}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "MSL translation of ${NAME} failed:\n${output}")
    endif()
endif()
file(READ "${code}" bytes HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${bytes}")
string(REPEAT "0x..," 16 line)   # CMake's expressions have no {16}
string(REGEX REPLACE "(${line})" "\\1\n" bytes "${bytes}")
file(WRITE "${OUTPUT}" "static const unsigned char doom_${FORMAT}_${NAME}[]={\n${bytes}};\n")
file(REMOVE "${spirv}" "${OUTPUT}.metal")
