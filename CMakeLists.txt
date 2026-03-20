cmake_minimum_required(VERSION 3.20)
project(NebulaDreamcast)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/thirdparty/imgui")

add_executable(NebulaEditor
    "${CMAKE_SOURCE_DIR}/src/editor/main.cpp"
    "${CMAKE_SOURCE_DIR}/src/nodes/Camera3DNode.h"
    "${CMAKE_SOURCE_DIR}/src/camera/camera3d.cpp"
    "${CMAKE_SOURCE_DIR}/src/camera/camera3d.h"
    "${CMAKE_SOURCE_DIR}/src/math/math_types.h"
    "${CMAKE_SOURCE_DIR}/src/math/math_utils.cpp"
    "${CMAKE_SOURCE_DIR}/src/math/math_utils.h"
    "${CMAKE_SOURCE_DIR}/src/editor/prefs.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/prefs.h"
    "${CMAKE_SOURCE_DIR}/src/editor/project.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/project.h"
    "${CMAKE_SOURCE_DIR}/src/editor/file_dialogs.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/file_dialogs.h"
    "${CMAKE_SOURCE_DIR}/src/io/texture_io.cpp"
    "${CMAKE_SOURCE_DIR}/src/io/texture_io.h"
    "${CMAKE_SOURCE_DIR}/src/io/meta_io.cpp"
    "${CMAKE_SOURCE_DIR}/src/io/meta_io.h"
    "${CMAKE_SOURCE_DIR}/src/io/mesh_io.cpp"
    "${CMAKE_SOURCE_DIR}/src/io/mesh_io.h"
    "${CMAKE_SOURCE_DIR}/src/io/anim_io.cpp"
    "${CMAKE_SOURCE_DIR}/src/io/anim_io.h"
    "${CMAKE_SOURCE_DIR}/src/scene/scene_io.cpp"
    "${CMAKE_SOURCE_DIR}/src/scene/scene_io.h"
    "${CMAKE_SOURCE_DIR}/src/scene/scene_manager.cpp"
    "${CMAKE_SOURCE_DIR}/src/scene/scene_manager.h"
    "${CMAKE_SOURCE_DIR}/src/nodes/NodeTypes.h"
    "${CMAKE_SOURCE_DIR}/src/nodes/Audio3DNode.cpp"
    "${CMAKE_SOURCE_DIR}/src/nodes/StaticMesh3DNode.cpp"
    "${CMAKE_SOURCE_DIR}/src/nodes/Node3DNode.cpp"
    "${CMAKE_SOURCE_DIR}/src/nodes/NavMesh3DNode.cpp"
    "${CMAKE_SOURCE_DIR}/src/platform/dreamcast/build_helpers.cpp"
    "${CMAKE_SOURCE_DIR}/src/platform/dreamcast/build_helpers.h"
    "${CMAKE_SOURCE_DIR}/nebula.rc"
    "${CMAKE_SOURCE_DIR}/src/navmesh/NavMeshBuilder.cpp"
    "${CMAKE_SOURCE_DIR}/src/navmesh/NavMeshBuilder.h"
    "${CMAKE_SOURCE_DIR}/src/runtime/runtime_bridge.cpp"
    "${CMAKE_SOURCE_DIR}/src/runtime/runtime_bridge.h"
    "${CMAKE_SOURCE_DIR}/src/runtime/script_compile.cpp"
    "${CMAKE_SOURCE_DIR}/src/runtime/script_compile.h"
    "${CMAKE_SOURCE_DIR}/src/runtime/physics.cpp"
    "${CMAKE_SOURCE_DIR}/src/runtime/physics.h"
    "${CMAKE_SOURCE_DIR}/src/runtime/collision.cpp"
    "${CMAKE_SOURCE_DIR}/src/runtime/collision.h"
    "${CMAKE_SOURCE_DIR}/src/runtime/fallback_controller.cpp"
    "${CMAKE_SOURCE_DIR}/src/runtime/fallback_controller.h"
    "${CMAKE_SOURCE_DIR}/src/platform/dreamcast/dc_codegen.cpp"
    "${CMAKE_SOURCE_DIR}/src/platform/dreamcast/dc_codegen.h"
    "${CMAKE_SOURCE_DIR}/src/ui/asset_browser.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/asset_browser.h"
    "${CMAKE_SOURCE_DIR}/src/ui/mesh_inspector.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/mesh_inspector.h"
    "${CMAKE_SOURCE_DIR}/src/ui/import_pipeline.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/import_pipeline.h"
    "${CMAKE_SOURCE_DIR}/src/vmu/vmu_tool.cpp"
    "${CMAKE_SOURCE_DIR}/src/vmu/vmu_tool.h"
    "${CMAKE_SOURCE_DIR}/src/vmu/vmu_tool_ui.cpp"
    "${CMAKE_SOURCE_DIR}/src/vmu/vmu_tool_ui.h"
    "${CMAKE_SOURCE_DIR}/src/ui/scene_tabs.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/scene_tabs.h"
    "${CMAKE_SOURCE_DIR}/src/ui/scene_outliner.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/scene_outliner.h"
    "${CMAKE_SOURCE_DIR}/src/ui/inspector.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/inspector.h"
    "${CMAKE_SOURCE_DIR}/src/ui/main_menu.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/main_menu.h"
    "${CMAKE_SOURCE_DIR}/src/viewport/viewport_render.cpp"
    "${CMAKE_SOURCE_DIR}/src/viewport/viewport_render.h"
    "${CMAKE_SOURCE_DIR}/src/viewport/viewport_transform.cpp"
    "${CMAKE_SOURCE_DIR}/src/viewport/viewport_transform.h"
    "${CMAKE_SOURCE_DIR}/src/viewport/node_helpers.cpp"
    "${CMAKE_SOURCE_DIR}/src/viewport/node_helpers.h"
    "${CMAKE_SOURCE_DIR}/src/viewport/node_gizmos.cpp"
    "${CMAKE_SOURCE_DIR}/src/viewport/node_gizmos.h"
    "${CMAKE_SOURCE_DIR}/src/viewport/background.cpp"
    "${CMAKE_SOURCE_DIR}/src/viewport/background.h"
    "${CMAKE_SOURCE_DIR}/src/viewport/static_mesh_render.cpp"
    "${CMAKE_SOURCE_DIR}/src/viewport/static_mesh_render.h"
    "${CMAKE_SOURCE_DIR}/src/viewport/picking.cpp"
    "${CMAKE_SOURCE_DIR}/src/viewport/picking.h"
    "${CMAKE_SOURCE_DIR}/src/editor/editor_state.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/editor_state.h"
    "${CMAKE_SOURCE_DIR}/src/editor/undo.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/undo.h"
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
    ${CMAKE_SOURCE_DIR}/src
)

