cmake_minimum_required(VERSION 3.20)
project(NebulaDreamcast)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/thirdparty/imgui")

add_executable(NebulaEditor
    "${CMAKE_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_SOURCE_DIR}/src/audio3d.cpp"
    "${CMAKE_SOURCE_DIR}/src/audio3d.h"
    "${CMAKE_SOURCE_DIR}/src/editor/camera3d_v2.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/camera3d_v2.h"
    "${CMAKE_SOURCE_DIR}/src/editor/math_types.h"
    "${CMAKE_SOURCE_DIR}/src/camera/Camera3D.cpp"
    "${CMAKE_SOURCE_DIR}/src/camera/Camera3D.h"
    "${CMAKE_SOURCE_DIR}/src/assets/meta_io.cpp"
    "${CMAKE_SOURCE_DIR}/src/assets/meta_io.h"
    "${CMAKE_SOURCE_DIR}/src/scene/SceneIO.cpp"
    "${CMAKE_SOURCE_DIR}/src/scene/SceneIO.h"
    "${CMAKE_SOURCE_DIR}/src/scene/scene_io.h"
    "${CMAKE_SOURCE_DIR}/src/scene/NodeTypes.h"
    "${CMAKE_SOURCE_DIR}/src/scene/Audio3DNode.cpp"
    "${CMAKE_SOURCE_DIR}/src/scene/StaticMeshNode.cpp"
    "${CMAKE_SOURCE_DIR}/src/scene/Camera3DNode.cpp"
    "${CMAKE_SOURCE_DIR}/src/scene/Node3D.cpp"
    "${CMAKE_SOURCE_DIR}/src/scene/scene_types.h"
    "${CMAKE_SOURCE_DIR}/src/platform/dreamcast/build_helpers.cpp"
    "${CMAKE_SOURCE_DIR}/src/platform/dreamcast/build_helpers.h"
    "${CMAKE_SOURCE_DIR}/src/nebula_dreamcast.cpp"
    "${CMAKE_SOURCE_DIR}/src/nebula_dreamcast.h"
    "${CMAKE_SOURCE_DIR}/nebula.rc"
    "${CMAKE_SOURCE_DIR}/src/navmesh/NavMeshBuilder.cpp"
    "${CMAKE_SOURCE_DIR}/src/navmesh/NavMeshBuilder.h"
)

set(IMGUI_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl2.cpp"
)

target_sources(NebulaEditor PRIVATE ${IMGUI_SOURCES})

add_definitions(-D_CRT_SECURE_NO_WARNINGS)

add_subdirectory("${CMAKE_SOURCE_DIR}/thirdparty/glfw")
add_subdirectory("${CMAKE_SOURCE_DIR}/thirdparty/recastnavigation")

include(FetchContent)
FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG v5.3.1
)
FetchContent_MakeAvailable(assimp)

if (WIN32)
    target_link_libraries(NebulaEditor PRIVATE glfw opengl32 gdi32 user32 shell32 winmm windowscodecs assimp Recast Detour)
    target_compile_definitions(NebulaEditor PRIVATE GLFW_EXPOSE_NATIVE_WIN32)
endif()

option(NEBULA_USE_DREAMCAST_SDK "Attach Dreamcast SDK (KOS) to NebulaEditor target" OFF)

# Expose Dreamcast SDK paths as cache entries for CMake GUI / Visual Studio.
set(KOS_BASE "$ENV{KOS_BASE}" CACHE PATH "Path to KallistiOS base (KOS_BASE)")
set(KOS_CC_BASE "$ENV{KOS_CC_BASE}" CACHE PATH "Path to KOS toolchain base (KOS_CC_BASE)")

# Auto-detect common Windows layouts if cache/env values are empty.
if (NOT KOS_BASE)
    set(_KOS_BASE_CANDIDATES
        "C:/DreamSDK/opt/toolchains/dc/kos"
        "C:/opt/toolchains/dc/kos"
    )
    foreach(_cand IN LISTS _KOS_BASE_CANDIDATES)
        if (EXISTS "${_cand}/include/kos.h")
            set(KOS_BASE "${_cand}" CACHE PATH "Path to KallistiOS base (KOS_BASE)" FORCE)
            break()
        endif()
    endforeach()
endif()

if (NOT KOS_CC_BASE)
    set(_KOS_CC_BASE_CANDIDATES
        "C:/DreamSDK/opt/toolchains/dc"
        "C:/opt/toolchains/dc"
    )
    foreach(_cand IN LISTS _KOS_CC_BASE_CANDIDATES)
        if (EXISTS "${_cand}/sh-elf/sh-elf/lib")
            set(KOS_CC_BASE "${_cand}" CACHE PATH "Path to KOS toolchain base (KOS_CC_BASE)" FORCE)
            break()
        endif()
    endforeach()
endif()

# Visible dependency target in Visual Studio / CMake.
add_library(DreamcastSDK INTERFACE)
set_target_properties(DreamcastSDK PROPERTIES FOLDER "Dependencies")

if (NEBULA_USE_DREAMCAST_SDK)
    if (NOT KOS_BASE)
        message(FATAL_ERROR "KOS_BASE is required when NEBULA_USE_DREAMCAST_SDK=ON. Install DreamSDK and set KOS_BASE (expected C:/DreamSDK/opt/toolchains/dc/kos).")
    endif()
    if (NOT KOS_CC_BASE)
        message(FATAL_ERROR "KOS_CC_BASE is required when NEBULA_USE_DREAMCAST_SDK=ON. Install DreamSDK and set KOS_CC_BASE (expected C:/DreamSDK/opt/toolchains/dc).")
    endif()

    target_compile_definitions(DreamcastSDK INTERFACE NEBULA_DREAMCAST)
    target_include_directories(DreamcastSDK INTERFACE
        "${KOS_BASE}/include"
        "${KOS_BASE}/kernel/arch/dreamcast/include"
        "${KOS_BASE}/addons/include"
    )
    target_link_directories(DreamcastSDK INTERFACE
        "${KOS_BASE}/lib/dreamcast"
        "${KOS_BASE}/addons/lib/dreamcast"
        "${KOS_CC_BASE}/sh-elf/sh-elf/lib"
    )

    target_link_libraries(NebulaEditor PRIVATE DreamcastSDK)

    message(STATUS "Dreamcast SDK wiring ENABLED")
    message(STATUS "KOS_BASE=${KOS_BASE}")
    message(STATUS "KOS_CC_BASE=${KOS_CC_BASE}")
else()
    message(STATUS "Dreamcast SDK wiring DISABLED for desktop build (NEBULA_USE_DREAMCAST_SDK=OFF)")
endif()

include_directories(
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_SOURCE_DIR}/thirdparty/glfw/include
)

