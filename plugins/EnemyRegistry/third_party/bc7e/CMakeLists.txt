# Keep the encoder in the plugin, not in a separately deployed DLL. A custom
# ISPC command also works with our Visual Studio generator.
find_program(BC7E_ISPC_EXECUTABLE NAMES ispc REQUIRED)
set(BC7E_OBJECTS
    "${CMAKE_CURRENT_BINARY_DIR}/bc7e.obj"
    "${CMAKE_CURRENT_BINARY_DIR}/bc7e_sse2.obj"
    "${CMAKE_CURRENT_BINARY_DIR}/bc7e_sse4.obj"
    "${CMAKE_CURRENT_BINARY_DIR}/bc7e_avx2.obj")
set(BC7E_HEADERS
    "${CMAKE_CURRENT_BINARY_DIR}/bc7e_ispc.h"
    "${CMAKE_CURRENT_BINARY_DIR}/bc7e_ispc_sse2.h"
    "${CMAKE_CURRENT_BINARY_DIR}/bc7e_ispc_sse4.h"
    "${CMAKE_CURRENT_BINARY_DIR}/bc7e_ispc_avx2.h")
add_custom_command(OUTPUT ${BC7E_OBJECTS} ${BC7E_HEADERS}
    COMMAND "${BC7E_ISPC_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/bc7e.ispc"
        -O2 --arch=x86-64 --target-os=windows
        --target=sse2-i32x4,sse4-i32x4,avx2-i32x8
        --opt=disable-assertions --wno-perf
        -h "${CMAKE_CURRENT_BINARY_DIR}/bc7e_ispc.h"
        -o "${CMAKE_CURRENT_BINARY_DIR}/bc7e.obj"
    DEPENDS bc7e.ispc
    COMMENT "Compiling bc7e SIMD encoder (SSE2/SSE4/AVX2 + CPU dispatcher)"
    VERBATIM)
set_source_files_properties(${BC7E_OBJECTS} PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
add_library(EnemyRegistryBc7e STATIC ${BC7E_OBJECTS} ${BC7E_HEADERS})
set_target_properties(EnemyRegistryBc7e PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(EnemyRegistryBc7e PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
