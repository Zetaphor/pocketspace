cmake_minimum_required(VERSION 3.20)
project(pocketspace CXX)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(native-resource-key-regression EXCLUDE_FROM_ALL tests/native-resource-key-regression.cpp)
target_include_directories(native-resource-key-regression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(native-resource-key-regression PRIVATE -UNDEBUG)
add_executable(native-scene-resources-regression EXCLUDE_FROM_ALL tests/native-scene-resources-regression.cpp)
target_include_directories(native-scene-resources-regression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(native-scene-resources-regression PRIVATE -UNDEBUG)
add_executable(runtime-stats-regression EXCLUDE_FROM_ALL tests/runtime-stats-regression.cpp)
target_include_directories(runtime-stats-regression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_executable(scene-lookup-regression EXCLUDE_FROM_ALL tests/scene-lookup-regression.cpp)
target_include_directories(scene-lookup-regression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_executable(lifecycle-input-regression EXCLUDE_FROM_ALL tests/lifecycle-input-regression.cpp src/frame_input.cpp)
target_include_directories(lifecycle-input-regression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(lifecycle-input-regression PRIVATE pthread)
target_compile_options(lifecycle-input-regression PRIVATE -UNDEBUG)
include(cmake/Physics.cmake)
option(SF_BUILD_ENGINE "Build the OpenVR overlay executable" ON)
if(NOT SF_BUILD_ENGINE)
  return()
endif()
set(SDK_INCLUDE "$ENV{HOME}/.sdk/include" CACHE PATH "OpenVR headers")
set(QJS_INCLUDE "$ENV{HOME}/Code/third_party/quickjs-ng" CACHE PATH "QuickJS headers")
set(QJS_LIB "$ENV{HOME}/Code/third_party/quickjs-ng/build/libqjs.a" CACHE FILEPATH "QuickJS library")
set(STEAMVR_BIN "/opt/steamvr/bin/linuxarm64" CACHE PATH "SteamVR binaries")
find_path(DRM_INCLUDE drm_fourcc.h PATH_SUFFIXES libdrm REQUIRED)
find_package(Freetype REQUIRED)
include(cmake/MeshCache.cmake)
add_library(mikktspace STATIC third_party/mikktspace.cpp)
add_library(sf-environment STATIC src/lighting_environment.cpp)
target_link_libraries(sf-environment PUBLIC GLESv2)
add_executable(animation-profile-regression EXCLUDE_FROM_ALL tests/animation-profile-regression.cpp src/model_asset.cpp)
target_include_directories(animation-profile-regression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(animation-profile-regression PRIVATE SF_TEST_HOOKS=1)
target_compile_options(animation-profile-regression PRIVATE -UNDEBUG)
target_link_libraries(animation-profile-regression PRIVATE meshoptimizer mikktspace sf-environment EGL GLESv2)

# Engine code compiled once for the production executable and once, with test
# hooks and assertions, for every engine regression.
set(SF_ENGINE_SOURCES src/script_api.cpp src/render_host.cpp src/model_asset.cpp src/controller_input.cpp src/frame_input.cpp src/panel.cpp)
add_library(pocketspace-objects OBJECT ${SF_ENGINE_SOURCES})
add_library(pocketspace-test-objects OBJECT EXCLUDE_FROM_ALL ${SF_ENGINE_SOURCES})
target_compile_options(pocketspace-test-objects PRIVATE -UNDEBUG)
target_compile_definitions(pocketspace-test-objects PRIVATE SF_TEST_HOOKS=1)
foreach(objects pocketspace-objects pocketspace-test-objects)
  target_include_directories(${objects} PRIVATE ${SDK_INCLUDE} ${QJS_INCLUDE} ${DRM_INCLUDE} ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(${objects} PRIVATE sf-physics meshoptimizer mikktspace sf-environment Freetype::Freetype)
endforeach()

function(sf_engine_target target)
  target_include_directories(${target} PRIVATE ${SDK_INCLUDE} ${QJS_INCLUDE} ${DRM_INCLUDE})
  target_link_directories(${target} PRIVATE ${STEAMVR_BIN})
  target_link_libraries(${target} PRIVATE sf-physics meshoptimizer mikktspace sf-environment openvr_api ${QJS_LIB} Freetype::Freetype pthread EGL GLESv2 gbm drm m)
endfunction()

add_executable(pocketspace src/main.cpp $<TARGET_OBJECTS:pocketspace-objects>)
sf_engine_target(pocketspace)

# Regressions keep their assertions even in a Release build. Test hooks change
# engine types, so every target linking the test objects defines them too.
foreach(test animation-blend-regression animation-regression asset-lifetime-regression auto-lod-regression
    control-regression culling-regression directional-light-regression environment-regression event-bus-regression
    examples-regression gpu-skinning-regression instancing-regression lifecycle-script-regression light-work-regression
    morph-regression multi-script-lifecycle-regression multi-script-render-regression pbr-depth-regression
    pbr-index-production-regression pbr-lod-regression pbr-regression physics-api-regression point-light-regression
    render-occlusion-regression rigid-batch-regression scene-container-regression scene-persistence-regression
    script-recovery-regression sheen-regression spot-shadow-regression spotlight-cone-regression spotlight-regression
    pbr-cost-profile render-stress-benchmark)
  add_executable(${test} EXCLUDE_FROM_ALL tests/${test}.cpp $<TARGET_OBJECTS:pocketspace-test-objects>)
  target_compile_definitions(${test} PRIVATE SF_TEST_HOOKS=1)
  target_compile_options(${test} PRIVATE -UNDEBUG)
  sf_engine_target(${test})
endforeach()
# These check production objects directly.
foreach(test projected-bounds-regression panel-submission-regression render-group-split-regression)
  add_executable(${test} EXCLUDE_FROM_ALL tests/${test}.cpp $<TARGET_OBJECTS:pocketspace-objects>)
  sf_engine_target(${test})
endforeach()
add_executable(input-arbitration-regression EXCLUDE_FROM_ALL tests/input-arbitration-regression.cpp)
target_compile_options(input-arbitration-regression PRIVATE -UNDEBUG)

# A separate tree for correctness runs. Production and profiling builds keep their
# optimization; regression sources compile about three times faster with assertions intact.
option(SF_FAST_TESTS "Compile regression targets without optimization" OFF)
if(SF_FAST_TESTS)
  get_directory_property(sf_targets BUILDSYSTEM_TARGETS)
  foreach(sf_target ${sf_targets})
    # These measure production performance or compare exact pixels that depend on
    # the fused multiply-add instructions optimized ARM64 builds use.
    if(sf_target MATCHES "-regression$" AND NOT sf_target MATCHES "^(physics-performance|pbr-index-production)-regression$")
      target_compile_options(${sf_target} PRIVATE -O0)
    endif()
  endforeach()
  # The shared test engine stays optimized: it is compiled once, and the exact
  # pixel checks in pbr-index-production-regression need its optimized arithmetic.
endif()
