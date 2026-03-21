#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"

#include <stdio.h>
#include <cmath>
#include <vector>
#include <cstdlib>
#include <string>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <functional>
#include <memory>
#include <sstream>
#include <cctype>
#include <cfloat>
#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <iterator>
#include <array>
#include <thread>
#include <atomic>

#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>
#include <ShObjIdl.h>
#include <wincodec.h>

#include "math/math_types.h"
#include "camera/camera3d.h"
#include "editor/prefs.h"
#include "editor/project.h"
#include "ui/import_pipeline.h"
#include "ui/asset_browser.h"
#include "io/meta_io.h"
#include "io/mesh_io.h"
#include "platform/dreamcast/build_helpers.h"
#include "scene/scene_io.h"
#include "nodes/NodeTypes.h"
#include "navmesh/NavMeshBuilder.h"
#include "runtime/runtime_bridge.h"
#include "runtime/script_compile.h"
#include "runtime/physics.h"
#include "editor/file_dialogs.h"
#include "math/math_utils.h"
#include "io/texture_io.h"
#include "io/anim_io.h"
#include "platform/dreamcast/dc_codegen.h"
#include "ui/mesh_inspector.h"
#include "vmu/vmu_tool.h"
#include "vmu/vmu_tool_ui.h"
#include "ui/scene_tabs.h"
#include "scene/scene_manager.h"
#include "ui/scene_outliner.h"
#include "ui/inspector.h"
#include "ui/main_menu.h"
#include "viewport/viewport_render.h"
#include "viewport/viewport_transform.h"
#include "viewport/node_gizmos.h"
#include "viewport/background.h"
#include "viewport/static_mesh_render.h"
#include "viewport/viewport_selection.h"
#include "editor/viewport_nav.h"
#include "viewport/node_helpers.h"
#include "editor/editor_state.h"
#include "editor/undo.h"
#include "editor/hotkeys.h"
#include "editor/frame_loop.h"
#include "ui/toolbar.h"
#include "ui/assets_panel.h"
#include <GL/gl.h>

#ifndef GL_POINT_SPRITE
#define GL_POINT_SPRITE 0x8861
#endif
#ifndef GL_COORD_REPLACE
#define GL_COORD_REPLACE 0x8862
#endif
#include <GLFW/glfw3.h>

std::filesystem::path GetExecutableDirectory()
{
    char pathBuf[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(NULL, pathBuf, (DWORD)MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return std::filesystem::current_path();
    std::filesystem::path p(pathBuf);
    return p.parent_path();
}

static std::filesystem::path ResolveEditorAssetPath(const std::filesystem::path& relPath)
{
    std::vector<std::filesystem::path> roots;
    auto addRootChain = [&](std::filesystem::path base)
    {
        std::error_code ec;
        for (int i = 0; i < 7 && !base.empty(); ++i)
        {
            if (std::filesystem::exists(base, ec) && !ec)
                roots.push_back(base);
            std::filesystem::path parent = base.parent_path();
            if (parent == base) break;
            base = parent;
        }
    };

    addRootChain(std::filesystem::current_path());
    addRootChain(GetExecutableDirectory());

    std::vector<std::filesystem::path> relCandidates;
    relCandidates.push_back(relPath);

    auto relGeneric = relPath.generic_string();
    if (relGeneric.rfind("assets/", 0) == 0)
        relCandidates.push_back(std::filesystem::path("Assets") / relPath.lexically_relative("assets"));
    else if (relGeneric.rfind("Assets/", 0) == 0)
        relCandidates.push_back(std::filesystem::path("assets") / relPath.lexically_relative("Assets"));

    std::error_code ec;
    for (const auto& root : roots)
    {
        for (const auto& rel : relCandidates)
        {
            std::filesystem::path p = root / rel;
            if (std::filesystem::exists(p, ec) && !ec)
                return p;
        }
    }
    return {};
}

// Shared editor state moved to editor/editor_state.cpp
// UndoAction, PushUndo, DoUndo, DoRedo moved to editor/undo.cpp

// (edge-detect statics and copy/paste statics moved to editor/hotkeys.cpp)


// (import state moved to ui/assets_panel.cpp)
// gPendingDroppedImports moved to editor/frame_loop.cpp


// MakeUniqueAssetPath, CreateSceneAssetAt, CreateSceneAsset,
// CreateAssetFolderAt, CreateAssetFolder — moved to editor/asset_browser.cpp

// CreateMaterialAsset moved to ui/asset_browser.cpp

// Material, mesh/slot, and slots-manifest wrappers removed — call NebulaAssets:: / NebulaNodes:: directly

// TryGetNodeWorldPosByName, IsCameraUnderNode3D, ReparentStaticMeshKeepWorldPos,
// ResetStaticMeshTransformsKeepWorld moved to viewport/node_helpers.cpp

std::string ToProjectRelativePath(const std::filesystem::path& p)
{
    if (gProjectDir.empty())
        return p.filename().generic_string();

    std::error_code ec;
    std::filesystem::path rel = std::filesystem::relative(p, std::filesystem::path(gProjectDir), ec);
    if (ec) return p.filename().generic_string();
    return rel.generic_string();
}

// SanitizeToken -> editor/import_pipeline.cpp

// ResolveMaterialTexturePath -> editor/import_pipeline.cpp

// FindTextureByMaterialNameFallback -> editor/import_pipeline.cpp

// SaveNebSlotsManifest wrapper removed -> use NebulaAssets:: directly

std::filesystem::path GetNebMeshMetaPath(const std::filesystem::path& absMeshPath)
{
    return absMeshPath.parent_path() / "animmeta" / (absMeshPath.stem().string() + ".animmeta.animmeta");
}

// GetNebMeshVtxAnimLinkPath, LoadNebMeshVtxAnimLink, SaveNebMeshVtxAnimLink -> editor/import_pipeline.cpp

// ImportModelTexturesAndGenerateMaterials -> editor/import_pipeline.cpp

// Texture metadata wrappers removed — call NebulaAssets:: directly

// gQuitConfirmOpen moved to ui/toolbar.cpp
// gRenamePath, gRenameBuffer, gInlineRenamePath, gInlineRenameBuffer, gInlineRenameFocus,
// gPendingDelete, gDoDelete, gRenameModalOpen, gAssetsCurrentDir, gSelectedAssetPath,
// gSelectedAssetPathSetTime — moved to editor/asset_browser.cpp
// gMaterialInspectorOpen, gMaterialInspectorPath, gNebTexInspectorOpen,
// gNebTexInspectorPath — moved to editor/editor_state.cpp
// gMaterialInspectorOpen2, gMaterialInspectorPath2, gNebTexInspectorOpen2,
// gNebTexInspectorPath2, gPreviewSaturnSampling, gStaticAnimClipCache,
// gNebMeshInspectorState — moved to editor/mesh_inspector.cpp

// DeleteAssetPath, MoveAssetToTrash, RenameAssetPath, DuplicateAssetPath,
// NormalizePathRef, RewritePathRefForRename, UpdateAssetReferencesForRename,
// BeginInlineAssetRename, CommitInlineAssetRename — moved to editor/asset_browser.cpp

// Scene wrappers removed — call NebulaScene:: directly

// HasUnsavedProjectChanges, RefreshOpenSceneTabMetadataForPath, LoadSceneFromPath,
// SetActiveScene, OpenSceneFile, SaveActiveScene, SaveAllProjectChanges
// — moved to scene/scene_manager.cpp

// DrawAssetsBrowser — moved to editor/asset_browser.cpp

// DrawNebMeshInspectorWindow - moved to editor/mesh_inspector.cpp

// LoadImageWIC, LoadTextureWIC, SaveVmuMonoPng, ExportNebTexturePNG moved to io/texture_io.cpp

// CreateCircleTexture, LoadNebTexture, GetNebTexture, ProjectToScreenGL, GetLocalAxes
// moved to viewport/viewport_render.cpp

int main(int, char**)
{
    if (!glfwInit())
        return 1;

    EditorViewportNav nav;
    EditorFrameContext ctx;

    LoadPreferences(ctx.uiScale, ctx.themeMode);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Nebula", nullptr, nullptr);
    if (window)
    {
        UINT iw = 0, ih = 0;
        std::vector<unsigned char> icoBGRA;
        std::filesystem::path iconPath = ResolveEditorAssetPath("assets/nebula_logo.ico");
        std::wstring iconW = iconPath.wstring();
        if (!iconW.empty() && LoadImageWIC(iconW.c_str(), iw, ih, icoBGRA))
        {
            std::vector<unsigned char> icoRGBA(icoBGRA.size());
            for (size_t i = 0; i + 3 < icoBGRA.size(); i += 4)
            {
                icoRGBA[i + 0] = icoBGRA[i + 2];
                icoRGBA[i + 1] = icoBGRA[i + 1];
                icoRGBA[i + 2] = icoBGRA[i + 0];
                icoRGBA[i + 3] = icoBGRA[i + 3];
            }
            GLFWimage img;
            img.width = (int)iw;
            img.height = (int)ih;
            img.pixels = icoRGBA.data();
            glfwSetWindowIcon(window, 1, &img);
        }
    }
    if (!window)
        return 1;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glfwSetWindowUserPointer(window, &nav);
    InstallViewportScrollCallback(window);
    glfwSetDropCallback(window, [](GLFWwindow*, int count, const char** paths)
    {
        for (int i = 0; i < count; ++i)
        {
            if (paths && paths[i] && paths[i][0] != '\0')
                gPendingDroppedImports.push_back(paths[i]);
        }
    });

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    ImGui::GetIO().FontGlobalScale = ctx.uiScale;

    std::filesystem::path uiIconPath = ResolveEditorAssetPath("assets/nebula_logo.ico");
    if (!uiIconPath.empty())
    {
        std::wstring w = uiIconPath.wstring();
        ctx.uiIconTex = LoadTextureWIC(w.c_str());
    }

    ctx.lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(window))
        TickEditorFrame(window, nav, ctx);

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

