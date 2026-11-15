option(ENEMY_REGISTRY_DEPLOY "Deploy EnemyRegistry after building" ON)
option(ENEMY_ICON_ATLAS_USE_BC7 "Encode the shared icon atlas as BC7 instead of RGBA8" ON)
set(ENEMY_REGISTRY_DEPLOY_ROOT "" CACHE PATH "Development game root; override explicitly for final delivery")
configure_plugin_project(EnemyRegistry NO_DEPLOY)
add_subdirectory(third_party/bc7e)

# Cache identity follows the actual atlas implementation and ISPC recipe,
# without invalidating for unrelated plugin code or relying on a manual bump.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    EnemyIconAtlas.cpp third_party/bc7e/bc7e.ispc third_party/bc7e/CMakeLists.txt)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/EnemyIconAtlas.cpp" ATLAS_SOURCE_HASH)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bc7e/bc7e.ispc" BC7E_SOURCE_HASH)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bc7e/CMakeLists.txt" BC7E_RECIPE_HASH)
execute_process(COMMAND "${BC7E_ISPC_EXECUTABLE}" --version
    OUTPUT_VARIABLE BC7E_COMPILER_VERSION COMMAND_ERROR_IS_FATAL ANY)
string(SHA256 ATLAS_ENCODER_KEY "${ATLAS_SOURCE_HASH};${BC7E_SOURCE_HASH};${BC7E_RECIPE_HASH};${BC7E_COMPILER_VERSION}")
configure_file(EnemyIconAtlasBuild.h.in EnemyIconAtlasBuild.h @ONLY)

# UDE is resolved explicitly during CI registration, not through PE imports.
target_link_libraries(EnemyRegistry PRIVATE EnemyRegistryBc7e)
target_sources(EnemyRegistry PRIVATE
    Plugin.cpp
    EnemyIdRegistry.cpp
    EnemyRegistrationHooks.cpp
    EnemyArrayCapacityPatches.cpp
    EnemyEnumConfig.cpp
    EnemyMessageOverrides.cpp
    EnemyIconDiscovery.cpp
    EnemyIconAtlas.cpp
    EnemyQuestVisibility.cpp)
target_include_directories(EnemyRegistry PRIVATE ${CMAKE_SOURCE_DIR}/plugins/ClassInjector/include)
target_compile_definitions(EnemyRegistry PRIVATE NOMINMAX)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/EnemyRegistry.toml" "# Zero-type provider: install before native static constructors.\n")
add_custom_command(TARGET EnemyRegistry POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/EnemyRegistry.toml" "$<TARGET_FILE_DIR:EnemyRegistry>/EnemyRegistry.toml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bc7e/LICENSE" "$<TARGET_FILE_DIR:EnemyRegistry>/EnemyRegistry.bc7e.LICENSE.txt")
if(ENEMY_REGISTRY_DEPLOY)
    if(NOT ENEMY_REGISTRY_DEPLOY_ROOT)
        message(FATAL_ERROR "Set ENEMY_REGISTRY_DEPLOY_ROOT explicitly to enable deployment")
    endif()
    add_custom_command(TARGET EnemyRegistry POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ENEMY_REGISTRY_DEPLOY_ROOT}/reframework/plugins"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ENEMY_REGISTRY_DEPLOY_ROOT}/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:EnemyRegistry>" "${ENEMY_REGISTRY_DEPLOY_ROOT}/reframework/plugins/EnemyRegistry.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/EnemyRegistry.toml" "${ENEMY_REGISTRY_DEPLOY_ROOT}/reframework/plugins/EnemyRegistry.toml"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bc7e/LICENSE" "${ENEMY_REGISTRY_DEPLOY_ROOT}/licenses/EnemyRegistry.bc7e.LICENSE.txt")
endif()
add_executable(EnemyRegistryTests EXCLUDE_FROM_ALL Tests/EnemyIdRegistryTests.cpp EnemyIdRegistry.cpp EnemyArrayCapacityPatches.cpp EnemyEnumConfig.cpp EnemyIconAtlas.cpp)
target_include_directories(EnemyRegistryTests PRIVATE ${CMAKE_SOURCE_DIR}/common/include ${CMAKE_SOURCE_DIR}/plugins/ClassInjector/include)
target_link_libraries(EnemyRegistryTests PRIVATE common_lib nlohmann_json::nlohmann_json EnemyRegistryBc7e ZLIB::ZLIB)
target_compile_options(EnemyRegistryTests PRIVATE /utf-8)
target_compile_definitions(EnemyRegistryTests PRIVATE NOMINMAX)
add_executable(EnemyIconAtlasTests EXCLUDE_FROM_ALL Tests/EnemyIconAtlasTests.cpp EnemyIdRegistry.cpp EnemyIconAtlas.cpp)
target_include_directories(EnemyIconAtlasTests PRIVATE ${CMAKE_SOURCE_DIR}/common/include)
target_link_libraries(EnemyIconAtlasTests PRIVATE common_lib nlohmann_json::nlohmann_json EnemyRegistryBc7e ZLIB::ZLIB)
target_compile_options(EnemyIconAtlasTests PRIVATE /utf-8)
target_compile_definitions(EnemyIconAtlasTests PRIVATE NOMINMAX)

foreach(target EnemyRegistry EnemyRegistryTests EnemyIconAtlasTests)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endforeach()
