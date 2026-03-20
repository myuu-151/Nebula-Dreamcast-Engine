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

#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

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
#include "viewport/picking.h"
#include "viewport/node_helpers.h"
#include "editor/editor_state.h"
#include "editor/undo.h"
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

// main.cpp-only statics
static bool gKeyG = false;
static bool gKeyR = false;
static bool gKeyS = false;
static bool gKeyX = false;
static bool gKeyY = false;
static bool gKeyZ = false;
static bool gKeyEsc = false;
static bool gKeyDel = false;

static bool gHasCopiedNode = false;
static Audio3DNode gCopiedNode;
static bool gHasCopiedStatic = false;
static StaticMesh3DNode gCopiedStatic;


// Import state (still in main.cpp — used by import pipeline)
std::string gImportPath;
Assimp::Importer gImportAssimp;
const aiScene* gImportScene = nullptr;
std::vector<bool> gImportAnimConvert;
std::string gImportBaseNebMeshPath;
int gImportBasisMode = 1; // 0=None(raw), 1=Blender(-Z Forward, Y Up), 2=Maya(+Z Forward, Y Up)
static std::vector<std::string> gPendingDroppedImports;


// MakeUniqueAssetPath, CreateSceneAssetAt, CreateSceneAsset,
// CreateAssetFolderAt, CreateAssetFolder — moved to editor/asset_browser.cpp

static std::filesystem::path CreateMaterialAsset(const std::filesystem::path& assetsRoot)
{
    std::filesystem::create_directories(assetsRoot);
    std::filesystem::path matPath = MakeUniqueAssetPath(assetsRoot, "NewMaterial", ".nebmat");
    std::ofstream out(matPath, std::ios::out | std::ios::trunc);
    if (out.is_open())
    {
        out << "texture=\n";
        out << "uv_scale=0\n";
        out << "saturn_allow_uv_repeat=0\n";
        out << "uv_scale_u=1\n";
        out << "uv_scale_v=1\n";
        out << "uv_offset_u=0\n";
        out << "uv_offset_v=0\n";
        out << "uv_rotate_deg=0\n";
        out << "shading_mode=0\n";
        out << "light_rotation=0\n";
        out << "light_pitch=0\n";
        out << "light_roll=0\n";
        out << "shadow_intensity=1\n";
    }
    return matPath;
}

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

static bool gQuitConfirmOpen = false;
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

    float orbitYaw = -139.75f;
    float orbitPitch = -14.3f;
    float viewYaw = -138.45f;
    float viewPitch = -12.1f;
    float distance = 3.2f;
    Vec3 orbitCenter = { 1.407f, 0.960f, 2.759f };
    Vec3 camPos = { 0.0f, 0.0f, 0.0f };
    double lastX = 0.0, lastY = 0.0;
    bool dragging = false;
    bool rotating = false;
    bool viewLocked = true;
    float scrollDelta = 0.0f;

    // Preserve editor camera when entering play mode so exit can restore cleanly.
    bool playCamSnapshotValid = false;
    float playSavedOrbitYaw = 0.0f;
    float playSavedOrbitPitch = 0.0f;
    float playSavedViewYaw = 0.0f;
    float playSavedViewPitch = 0.0f;
    float playSavedDistance = 10.0f;
    Vec3 playSavedOrbitCenter = { 0.0f, 0.0f, 0.0f };

    bool playSceneSnapshotValid = false;
    int playSavedActiveScene = -1;
    std::vector<SceneData> playSavedOpenScenes;
    std::vector<Audio3DNode> playSavedAudio3DNodes;
    std::vector<StaticMesh3DNode> playSavedStaticMeshNodes;
    std::vector<Camera3DNode> playSavedCamera3DNodes;
    std::vector<Node3DNode> playSavedNode3DNodes;
    std::vector<NavMesh3DNode> playSavedNavMesh3DNodes;

    bool showPreferences = false;
    bool showViewportDebugTab = false;
    float uiScale = 2.0f;
    int themeMode = 0; // 0=Space, 1=Slate, 2=Classic

    LoadPreferences(uiScale, themeMode);
    int currentScene = 0;
    const char* sceneItems[] = { "Scene 1", "Scene 2", "Scene 3" };
    bool draggingWindow = false;
    float pointRange[2] = { 0.0f, 0.0f };
    bool pointRangeInit = false;

    double lastTime = glfwGetTime();

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

    glfwSetWindowUserPointer(window, &scrollDelta);
    glfwSetScrollCallback(window, [](GLFWwindow* win, double, double yoff)
    {
        float* sd = (float*)glfwGetWindowUserPointer(win);
        if (sd) *sd += (float)yoff;
    });
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

    ImGui::GetIO().FontGlobalScale = uiScale;

    std::filesystem::path nebulaTexPath = ResolveEditorAssetPath("assets/nebula.png");
    std::filesystem::path uiIconPath = ResolveEditorAssetPath("assets/nebula_logo.ico");
    GLuint nebulaTex = 0;
    if (!nebulaTexPath.empty())
    {
        std::wstring w = nebulaTexPath.wstring();
        nebulaTex = LoadTextureWIC(w.c_str());
    }
    GLuint flareTex = 0;
    GLuint uiIconTex = 0;
    if (!uiIconPath.empty())
    {
        std::wstring w = uiIconPath.wstring();
        uiIconTex = LoadTextureWIC(w.c_str());
    }

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        if (!gPendingDroppedImports.empty())
        {
            auto dropped = gPendingDroppedImports;
            gPendingDroppedImports.clear();
            ImportAssetsToCurrentFolder(dropped);
        }

        double now = glfwGetTime();
        float deltaTime = (float)(now - lastTime);
        lastTime = now;
        if (deltaTime > 0.1f) deltaTime = 0.1f; // clamp

        PollScriptHotReloadV1(now);
        if (gScriptCompileState.load() != 0)
            PollPlayScriptCompile();
        TickPlayScriptRuntime(deltaTime, now);

        // Mouse orbit (MMB) / rotate in place (RMB)
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        int mmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE);
        int rmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);

        const float orbitSensitivity = 0.25f;
        const float rotateSensitivity = 0.10f;

        bool navMouseAllowed = !ImGui::GetIO().WantCaptureMouse;
        bool navKeyAllowed = !ImGui::GetIO().WantCaptureKeyboard;

        if (mmb == GLFW_PRESS && navMouseAllowed)
        {
            if (!dragging)
            {
                dragging = true;
                lastX = mx;
                lastY = my;

                // lock orbit center to current view target before orbiting
                float viewYawRad = viewYaw * 3.14159f / 180.0f;
                float viewPitchRad = viewPitch * 3.14159f / 180.0f;
                Vec3 forward = {
                    cosf(viewPitchRad) * cosf(viewYawRad),
                    sinf(viewPitchRad),
                    cosf(viewPitchRad) * sinf(viewYawRad)
                };
                // compute eye from current orbit params
                float oYawRad = orbitYaw * 3.14159f / 180.0f;
                float oPitchRad = orbitPitch * 3.14159f / 180.0f;
                Vec3 eye = {
                    orbitCenter.x - distance * cosf(oPitchRad) * cosf(oYawRad),
                    orbitCenter.y - distance * sinf(oPitchRad),
                    orbitCenter.z - distance * cosf(oPitchRad) * sinf(oYawRad)
                };
                orbitCenter = { eye.x + forward.x * distance, eye.y + forward.y * distance, eye.z + forward.z * distance };
                orbitYaw = viewYaw;
                orbitPitch = viewPitch;
            }
            else
            {
                double dx = mx - lastX;
                double dy = my - lastY;
                lastX = mx;
                lastY = my;

                orbitYaw   -= (float)dx * orbitSensitivity;
                orbitPitch -= (float)dy * orbitSensitivity;
                if (orbitPitch > 89.0f) orbitPitch = 89.0f;
                if (orbitPitch < -89.0f) orbitPitch = -89.0f;

                // keep view aligned to orbit (look at orbit center)
                viewLocked = true;
                viewYaw = orbitYaw;
                viewPitch = orbitPitch;
            }
        }
        else
        {
            dragging = false;
        }

        if (rmb == GLFW_PRESS && navMouseAllowed)
        {
            if (!rotating)
            {
                rotating = true;
                lastX = mx;
                lastY = my;
            }
            else
            {
                double dx = mx - lastX;
                double dy = my - lastY;
                lastX = mx;
                lastY = my;

                viewLocked = false;
                viewYaw   -= (float)dx * rotateSensitivity;
                viewPitch -= (float)dy * rotateSensitivity;
                if (viewPitch > 89.0f) viewPitch = 89.0f;
                if (viewPitch < -89.0f) viewPitch = -89.0f;
            }
        }
        else
        {
            rotating = false;
        }

        if (scrollDelta != 0.0f && navMouseAllowed)
        {
            float viewYawRad = viewYaw * 3.14159f / 180.0f;
            float viewPitchRad = viewPitch * 3.14159f / 180.0f;
            Vec3 forward = {
                cosf(viewPitchRad) * cosf(viewYawRad),
                sinf(viewPitchRad),
                cosf(viewPitchRad) * sinf(viewYawRad)
            };
            float move = scrollDelta * 0.5f;
            orbitCenter.x += forward.x * move;
            orbitCenter.y += forward.y * move;
            orbitCenter.z += forward.z * move;
            scrollDelta = 0.0f;
        }

        // WASD roaming (only while RMB held)
        if (rmb == GLFW_PRESS && navMouseAllowed && navKeyAllowed)
        {
            float moveSpeed = 5.0f; // units per second
            float move = moveSpeed * deltaTime;

            float yawRad = viewYaw * 3.14159f / 180.0f;
            Vec3 forwardXZ = { cosf(yawRad), 0.0f, sinf(yawRad) };
            Vec3 rightXZ = { -sinf(yawRad), 0.0f, cosf(yawRad) };

            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            {
                orbitCenter.x += forwardXZ.x * move;
                orbitCenter.z += forwardXZ.z * move;
            }
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            {
                orbitCenter.x -= forwardXZ.x * move;
                orbitCenter.z -= forwardXZ.z * move;
            }
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            {
                orbitCenter.x += rightXZ.x * move;
                orbitCenter.z += rightXZ.z * move;
            }
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            {
                orbitCenter.x -= rightXZ.x * move;
                orbitCenter.z -= rightXZ.z * move;
            }
            if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
            {
                orbitCenter.y += move;
            }
            if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            {
                orbitCenter.y -= move;
            }
        }

        // Fallback controls if script runtime is unavailable (e.g. cl.exe missing).
        if (gEnableCppPlayFallbackControls && gPlayMode && navKeyAllowed && (!gEditorScriptActive || !useScriptController))
        {

            const float moveStep = 5.0f * deltaTime;
            const float lookYawStep = 120.0f * deltaTime;
            const float lookPitchStep = 90.0f * deltaTime;
            const float turnSpeed = 360.0f * deltaTime;
            static double sFallbackLogTime = 0.0;
            static double sFreezeLogTime = 0.0;

            float inX = 0.0f, inY = 0.0f;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) inX += 1.0f;
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) inX -= 1.0f;
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) inY += 1.0f;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) inY -= 1.0f;
            float inLen = sqrtf(inX * inX + inY * inY);
            if (inLen > 1.0f)
            {
                inX /= inLen;
                inY /= inLen;
                inLen = 1.0f;
            }
            float lookYaw = 0.0f, lookPitch = 0.0f;
            if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS) lookYaw += 1.0f;
            if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS) lookYaw -= 1.0f;
            if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS) lookPitch += 1.0f;
            if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS) lookPitch -= 1.0f;

            // Hard fallback sync: keep CameraPivot at PlayerRoot position in C++ when script runtime is unavailable.
            {
                int playerIdx = FindNode3DByName("PlayerRoot");
                int pivotIdx = FindNode3DByName("CameraPivot");
                if (playerIdx >= 0 && pivotIdx >= 0 && playerIdx < (int)gNode3DNodes.size() && pivotIdx < (int)gNode3DNodes.size())
                {
                    gNode3DNodes[pivotIdx].x = gNode3DNodes[playerIdx].x;
                    gNode3DNodes[pivotIdx].y = gNode3DNodes[playerIdx].y;
                    gNode3DNodes[pivotIdx].z = gNode3DNodes[playerIdx].z;
                }
            }

            for (int ni = 0; ni < (int)gNode3DNodes.size(); ++ni)
            {
                auto& n3 = gNode3DNodes[ni];
                if (n3.name != "PlayerRoot") continue;

                const bool lookActive = (fabsf(lookYaw) > 0.0001f || fabsf(lookPitch) > 0.0001f);
                for (auto& cam : gCamera3DNodes)
                {
                    const bool camLinked = IsCameraUnderNode3D(cam, n3.name)
                        || (n3.name == "PlayerRoot" && cam.parent == "CameraPivot");
                    if (!camLinked) continue;
                    if (!lookActive) continue; // hard-freeze camera unless J/L/I/K is pressed

                    bool migratedOrbit = false;
                    if (fabsf(cam.orbitX) < 0.0001f && fabsf(cam.orbitZ) < 0.0001f)
                    {
                        // Seed orbit from local camera offset so J/L has visible effect immediately.
                        cam.orbitX = cam.x;
                        cam.orbitZ = cam.z;
                        cam.x = 0.0f;
                        cam.z = 0.0f;
                        migratedOrbit = true;
                    }

                    if (!migratedOrbit && fabsf(lookYaw) > 0.0001f)
                    {
                        const float yawRad = (-lookYaw * lookYawStep) * 3.14159f / 180.0f;
                        const float sn = sinf(yawRad), cs = cosf(yawRad);
                        float ox = cam.orbitX, oz = cam.orbitZ;
                        cam.orbitX = ox * cs - oz * sn;
                        cam.orbitZ = ox * sn + oz * cs;
                    }
                    if (fabsf(lookPitch) > 0.0001f)
                    {
                        float ox = cam.orbitX, oy = cam.orbitY, oz = cam.orbitZ;
                        float horiz = sqrtf(ox * ox + oz * oz);
                        float radius = sqrtf(horiz * horiz + oy * oy);
                        if (radius > 0.0001f)
                        {
                            float pitch = atan2f(oy, (horiz > 0.0001f ? horiz : 0.0001f));
                            pitch += (lookPitch * lookPitchStep) * 3.14159f / 180.0f;
                            const float lim = 1.39626f;
                            if (pitch > lim) pitch = lim;
                            if (pitch < -lim) pitch = -lim;
                            float newHoriz = cosf(pitch) * radius;
                            cam.orbitY = sinf(pitch) * radius;
                            if (horiz > 0.0001f)
                            {
                                float s = newHoriz / horiz;
                                cam.orbitX = ox * s;
                                cam.orbitZ = oz * s;
                            }
                            else
                            {
                                cam.orbitX = 0.0f;
                                cam.orbitZ = -newHoriz;
                            }
                        }
                    }

                    // Keep camera looking toward pivot from orbit offset (only during explicit look input).
                    {
                        float fx = -cam.orbitX, fy = -cam.orbitY, fz = -cam.orbitZ;
                        float fl = sqrtf(fx * fx + fy * fy + fz * fz);
                        if (fl > 0.0001f)
                        {
                            fx /= fl; fy /= fl; fz /= fl;
                            cam.rotY = atan2f(fx, fz) * 180.0f / 3.14159f;
                            cam.rotX = -atan2f(fy, sqrtf(fx * fx + fz * fz)) * 180.0f / 3.14159f;
                            cam.rotZ = 0.0f;
                        }
                    }
                }

                // SA1 camera-relative 8-way fallback (mode-locked):
                // use camera->pivot direction on XZ so W is away from camera, S is toward camera.
                float camToNodeX = 0.0f, camToNodeZ = 1.0f;
                bool gotFwd = false;
                bool gotCamForFreeze = false;
                float freezeOrbitX = 0.0f, freezeOrbitY = 0.0f, freezeOrbitZ = 0.0f;
                float freezeRotX = 0.0f, freezeRotY = 0.0f, freezeRotZ = 0.0f;
                for (const auto& cam : gCamera3DNodes)
                {
                    const bool camLinked = IsCameraUnderNode3D(cam, n3.name)
                        || (n3.name == "PlayerRoot" && cam.parent == "CameraPivot");
                    if (!camLinked) continue;
                    int ci = FindCamera3DByName(cam.name);
                    if (ci < 0) continue;

                    float cwx, cwy, cwz, cwrx, cwry, cwrz;
                    GetCamera3DWorldTR(ci, cwx, cwy, cwz, cwrx, cwry, cwrz);

                    float nwx, nwy, nwz, nwrx, nwry, nwrz, nwsx, nwsy, nwsz;
                    GetNode3DWorldTRS(ni, nwx, nwy, nwz, nwrx, nwry, nwrz, nwsx, nwsy, nwsz);

                    camToNodeX = nwx - cwx;
                    camToNodeZ = nwz - cwz;
                    gotFwd = true;
                    gotCamForFreeze = true;
                    freezeOrbitX = cam.orbitX;
                    freezeOrbitY = cam.orbitY;
                    freezeOrbitZ = cam.orbitZ;
                    freezeRotX = cam.rotX;
                    freezeRotY = cam.rotY;
                    freezeRotZ = cam.rotZ;
                    break;
                }
                if (!gotFwd)
                {
                    float yawRad = n3.rotY * 3.14159f / 180.0f;
                    camToNodeX = sinf(yawRad);
                    camToNodeZ = cosf(yawRad);
                }

                float fLen = sqrtf(camToNodeX * camToNodeX + camToNodeZ * camToNodeZ);
                if (fLen < 0.0001f)
                {
                    // Stable fallback when camera forward collapses (prevents spin jitter).
                    float yawRad = n3.rotY * 3.14159f / 180.0f;
                    camToNodeX = sinf(yawRad);
                    camToNodeZ = cosf(yawRad);
                    fLen = 1.0f;
                }
                camToNodeX /= fLen;
                camToNodeZ /= fLen;
                float camRightX = camToNodeZ;
                float camRightZ = -camToNodeX;

                // Build desired move heading from stable camera basis + WASD direction.
                float moveX = camRightX * inX + camToNodeX * inY;
                float moveZ = camRightZ * inX + camToNodeZ * inY;
                float moveLen = sqrtf(moveX * moveX + moveZ * moveZ);
                const float deadzone = 0.05f;
                float targetYaw = n3.rotY;
                float dy = 0.0f;
                if (moveLen > deadzone)
                {
                    moveX /= moveLen;
                    moveZ /= moveLen;

                    n3.x += moveX * moveStep;
                    n3.z += moveZ * moveStep;

                    targetYaw = atan2f(moveX, moveZ) * 180.0f / 3.14159f;
                    dy = targetYaw - n3.rotY;
                    while (dy > 180.0f) dy -= 360.0f;
                    while (dy < -180.0f) dy += 360.0f;
                    if (dy > turnSpeed) dy = turnSpeed;
                    if (dy < -turnSpeed) dy = -turnSpeed;
                    n3.rotY += dy;
                }

                // Keep CameraPivot snapped to PlayerRoot after movement, same frame.
                if (n3.name == "PlayerRoot")
                {
                    int pivotIdx = FindNode3DByName("CameraPivot");
                    if (pivotIdx >= 0 && pivotIdx < (int)gNode3DNodes.size())
                    {
                        gNode3DNodes[pivotIdx].x = n3.x;
                        gNode3DNodes[pivotIdx].y = n3.y;
                        gNode3DNodes[pivotIdx].z = n3.z;
                    }
                }

                if (inLen > 0.0001f)
                {
                    double now = glfwGetTime();
                    if (now - sFallbackLogTime >= 1.0)
                    {
                        sFallbackLogTime = now;
                        printf("[Fallback8Way] node=%s in=(%.2f,%.2f) camToNode=(%.2f,%.2f,%.2f) move=(%.2f,%.2f,%.2f) yaw=%.2f target=%.2f dy=%.2f\n",
                            n3.name.c_str(),
                            inX, inY,
                            camToNodeX, camToNodeZ, fLen,
                            moveX, moveZ, moveLen,
                            n3.rotY, targetYaw, dy);
                    }
                }

                if (!lookActive && inLen > 0.0001f && gotCamForFreeze)
                {
                    double now = glfwGetTime();
                    if (now - sFreezeLogTime >= 1.0)
                    {
                        sFreezeLogTime = now;
                        printf("[FallbackCamFreeze] lookActive=0 camOrbit=(%.3f,%.3f,%.3f) camRot=(%.3f,%.3f,%.3f)\n",
                            freezeOrbitX, freezeOrbitY, freezeOrbitZ,
                            freezeRotX, freezeRotY, freezeRotZ);
                    }
                }
            }
        }

        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        // Ensure full-frame clears (avoid stale stencil/color when scissor is left enabled by previous passes)
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        if (gShowVmuTool)
        {
            glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
            goto RenderImGuiOnly;
        }

        // Background layer (sky sphere) - rendered after view matrix

        // Camera
        float aspect = (float)display_w / (float)display_h;
        Mat4 proj = Mat4Perspective(45.0f * 3.14159f / 180.0f, aspect, 0.1f, 2000.0f);
        float orbitYawRad = orbitYaw * 3.14159f / 180.0f;
        float orbitPitchRad = orbitPitch * 3.14159f / 180.0f;
        Vec3 eye = {
            orbitCenter.x - distance * cosf(orbitPitchRad) * cosf(orbitYawRad),
            orbitCenter.y - distance * sinf(orbitPitchRad),
            orbitCenter.z - distance * cosf(orbitPitchRad) * sinf(orbitYawRad)
        };
        gEye = eye;

        Camera3DNode* activeCam = nullptr;
        if (!gCamera3DNodes.empty())
        {
            for (auto& c : gCamera3DNodes)
            {
                if (c.main && (!activeCam || c.priority > activeCam->priority))
                    activeCam = &c;
            }
            if (!activeCam)
                activeCam = &gCamera3DNodes[0];
        }

        NebulaCamera3D::View playView{};
        NebulaCamera3D::Projection playProj{};
        bool hasPlayCam = false;
        if (activeCam && gPlayMode)
        {
            int activeCamIdx = (int)(activeCam - &gCamera3DNodes[0]);
            float camX, camY, camZ, camRX, camRY, camRZ;
            GetCamera3DWorldTR(activeCamIdx, camX, camY, camZ, camRX, camRY, camRZ);

            Camera3D playCam = BuildCamera3DFromLegacyEuler(
                activeCam->name,
                activeCam->parent,
                camX, camY, camZ,
                camRX, camRY, camRZ,
                activeCam->perspective,
                activeCam->fovY,
                activeCam->nearZ,
                activeCam->farZ,
                activeCam->orthoWidth,
                activeCam->priority,
                activeCam->main);

            playView = NebulaCamera3D::BuildView(playCam);
            playProj = NebulaCamera3D::BuildProjection(playCam, aspect);
            hasPlayCam = true;

            proj = NebulaCamera3D::BuildProjectionMatrix(playProj);
            eye = playView.eye;

            Vec3 playForward = playView.basis.forward;
            viewYaw = atan2f(playForward.z, playForward.x) * 180.0f / 3.14159f;
            viewPitch = asinf(std::clamp(playForward.y, -1.0f, 1.0f)) * 180.0f / 3.14159f;

            static double sLastParityCamLog = -10.0;
            if ((now - sLastParityCamLog) >= 1.0)
            {
                sLastParityCamLog = now;
                printf("[CameraParity][EditorPlay] eye=(%.3f,%.3f,%.3f) f=(%.3f,%.3f,%.3f) r=(%.3f,%.3f,%.3f) u=(%.3f,%.3f,%.3f)\n",
                    playView.eye.x, playView.eye.y, playView.eye.z,
                    playView.basis.forward.x, playView.basis.forward.y, playView.basis.forward.z,
                    playView.basis.right.x, playView.basis.right.y, playView.basis.right.z,
                    playView.basis.up.x, playView.basis.up.y, playView.basis.up.z);
            }
        }

        UpdateAudio3DNodes(eye.x, eye.y, eye.z);

        // If view is locked, keep looking at orbit center
        if (viewLocked && !(activeCam && gPlayMode))
        {
            Vec3 dir = { orbitCenter.x - eye.x, orbitCenter.y - eye.y, orbitCenter.z - eye.z };
            float dlen = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            if (dlen > 0.0001f)
            {
                dir.x /= dlen; dir.y /= dlen; dir.z /= dlen;
                viewYaw = atan2f(dir.z, dir.x) * 180.0f / 3.14159f;
                viewPitch = asinf(dir.y) * 180.0f / 3.14159f;
            }
        }

        Vec3 forward{};
        Vec3 up = { 0.0f, 1.0f, 0.0f };

        if (hasPlayCam)
        {
            up = playView.basis.up;
            forward = playView.basis.forward;
        }
        else
        {
            float viewYawRad = viewYaw * 3.14159f / 180.0f;
            float viewPitchRad = viewPitch * 3.14159f / 180.0f;
            forward = {
                cosf(viewPitchRad) * cosf(viewYawRad),
                sinf(viewPitchRad),
                cosf(viewPitchRad) * sinf(viewYawRad)
            };
        }

        Mat4 view = {};
        if (hasPlayCam)
        {
            view = NebulaCamera3D::BuildViewMatrix(playView);
        }
        else
        {
            Vec3 target = { eye.x + forward.x, eye.y + forward.y, eye.z + forward.z };
            view = Mat4LookAt(eye, target, up);
        }

        // Editor parity toggle: mirror runtime + viewport horizontally (right-to-left).
        // This applies to both editor navigation view and play-camera runtime view.
        const bool kMirrorViewportRTL = false;
        if (kMirrorViewportRTL)
        {
            proj.m[0] = -proj.m[0];
        }

        // Transform hotkeys (GLFW-level, toggles)
        // Block when typing in an input field or hovering over any ImGui panel
        // (Inspector, Scene, etc.) so keys like S don't trigger Scale while editing values.
        {
            auto edge = [](bool now, bool& prev) { bool pressed = (now && !prev); prev = now; return pressed; };

            bool kG = (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS);
            bool kR = (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS);
            bool kS = (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS);
            bool kX = (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS);
            bool kY = (glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS);
            bool kZ = (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS);
            bool kEsc = (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS);
            bool kDel = (glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS);

            bool blockTransformKeys = ImGui::GetIO().WantTextInput || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);

            if (edge(kG, gKeyG) && !blockTransformKeys)
            {
                EndTransformSnapshot();
                if (gTransformMode == Transform_Grab) gTransformMode = Transform_None;
                else { gTransformMode = Transform_Grab; BeginTransformSnapshot(); }
                gAxisLock = 0;
            }
            if (edge(kR, gKeyR) && !blockTransformKeys)
            {
                EndTransformSnapshot();
                if (gTransformMode == Transform_Rotate) gTransformMode = Transform_None;
                else { gTransformMode = Transform_Rotate; BeginTransformSnapshot(); }
                gAxisLock = 0;
            }
            if (edge(kS, gKeyS) && !blockTransformKeys)
            {
                EndTransformSnapshot();
                if (gTransformMode == Transform_Scale) gTransformMode = Transform_None;
                else { gTransformMode = Transform_Scale; BeginTransformSnapshot(); }
                gAxisLock = 0;
            }
            if (!blockTransformKeys)
            {
                if (edge(kX, gKeyX)) gAxisLock = (gAxisLock == 'X') ? 0 : 'X';
                if (edge(kY, gKeyY)) gAxisLock = (gAxisLock == 'Y') ? 0 : 'Y';
                if (edge(kZ, gKeyZ)) gAxisLock = (gAxisLock == 'Z') ? 0 : 'Z';
            }
            else
            {
                // Still consume edges so we don't get a stale press when unblocked
                edge(kX, gKeyX); edge(kY, gKeyY); edge(kZ, gKeyZ);
            }
            if (edge(kEsc, gKeyEsc))
            {
                if (gPlayMode)
                {
                    gPlayMode = false;
                    gPlayOriginalScenes.clear();
                    EndPlayScriptRuntime();
                    if (playCamSnapshotValid)
                    {
                        orbitYaw = playSavedOrbitYaw;
                        orbitPitch = playSavedOrbitPitch;
                        viewYaw = playSavedViewYaw;
                        viewPitch = playSavedViewPitch;
                        distance = playSavedDistance;
                        orbitCenter = playSavedOrbitCenter;
                    }
                    if (playSceneSnapshotValid)
                    {
                        gOpenScenes = playSavedOpenScenes;
                        gActiveScene = playSavedActiveScene;
                        gAudio3DNodes = playSavedAudio3DNodes;
                        gStaticMeshNodes = playSavedStaticMeshNodes;
                        gCamera3DNodes = playSavedCamera3DNodes;
                        gNode3DNodes = playSavedNode3DNodes;
                        gNavMesh3DNodes = playSavedNavMesh3DNodes;
                    }
                }
                else
                {
                    CancelTransformSnapshot();
                    gTransformMode = Transform_None;
                    gAxisLock = 0;
                    gSelectedAudio3D = -1;
                    gHasRotatePreview = false;
                }
            }
            if (edge(kDel, gKeyDel))
            {
                if (gSelectedAudio3D >= 0 && gSelectedAudio3D < (int)gAudio3DNodes.size())
                {
                    int idx = gSelectedAudio3D;
                    Audio3DNode node = gAudio3DNodes[idx];
                    gAudio3DNodes.erase(gAudio3DNodes.begin() + idx);
                    gSelectedAudio3D = -1;
                    gTransformMode = Transform_None;
                    gAxisLock = 0;
                    gHasTransformSnapshot = false;

                    PushUndo({"Delete Audio3D",
                        [idx, node]() {
                            int i = idx;
                            if (i < 0) return;
                            if (i > (int)gAudio3DNodes.size()) i = (int)gAudio3DNodes.size();
                            gAudio3DNodes.insert(gAudio3DNodes.begin() + i, node);
                        },
                        [idx]() {
                            if (idx >= 0 && idx < (int)gAudio3DNodes.size())
                                gAudio3DNodes.erase(gAudio3DNodes.begin() + idx);
                        }
                    });
                }
                else if (gSelectedStaticMesh >= 0 && gSelectedStaticMesh < (int)gStaticMeshNodes.size())
                {
                    int idx = gSelectedStaticMesh;
                    StaticMesh3DNode node = gStaticMeshNodes[idx];
                    gStaticMeshNodes.erase(gStaticMeshNodes.begin() + idx);
                    gSelectedStaticMesh = -1;
                    gTransformMode = Transform_None;
                    gAxisLock = 0;
                    gHasTransformSnapshot = false;

                    PushUndo({"Delete StaticMesh3D",
                        [idx, node]() {
                            int i = idx;
                            if (i < 0) return;
                            if (i > (int)gStaticMeshNodes.size()) i = (int)gStaticMeshNodes.size();
                            gStaticMeshNodes.insert(gStaticMeshNodes.begin() + i, node);
                        },
                        [idx]() {
                            if (idx >= 0 && idx < (int)gStaticMeshNodes.size())
                                gStaticMeshNodes.erase(gStaticMeshNodes.begin() + idx);
                        }
                    });
                }
                else if (gSelectedNavMesh3D >= 0 && gSelectedNavMesh3D < (int)gNavMesh3DNodes.size())
                {
                    int idx = gSelectedNavMesh3D;
                    NavMesh3DNode node = gNavMesh3DNodes[idx];
                    gNavMesh3DNodes.erase(gNavMesh3DNodes.begin() + idx);
                    gSelectedNavMesh3D = -1;
                    gTransformMode = Transform_None;
                    gAxisLock = 0;
                    gHasTransformSnapshot = false;

                    PushUndo({"Delete NavMesh3D",
                        [idx, node]() {
                            int i = idx;
                            if (i < 0) return;
                            if (i > (int)gNavMesh3DNodes.size()) i = (int)gNavMesh3DNodes.size();
                            gNavMesh3DNodes.insert(gNavMesh3DNodes.begin() + i, node);
                        },
                        [idx]() {
                            if (idx >= 0 && idx < (int)gNavMesh3DNodes.size())
                                gNavMesh3DNodes.erase(gNavMesh3DNodes.begin() + idx);
                        }
                    });
                }
                else if (!gSelectedAssetPath.empty() && std::filesystem::exists(gSelectedAssetPath))
                {
                    gPendingDelete = gSelectedAssetPath;
                    gDoDelete = true;
                    gSelectedAssetPath.clear();
                }
            }
        }

        // Debug overlay/HUD moved below once viewport layout is known.

        // Node3D gravity + per-triangle ground collision + slope alignment in play mode.
        // Skip while scripts are still compiling so the player doesn't fall before controls load.
        if (gPlayMode && gScriptCompileState.load() == 0)
        {
            float dt = io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f);
            if (dt > 0.1f) dt = 0.1f;
            TickPlayModePhysics(dt);
        }


        // Tick editor animation playback time
        if (gPlayMode)
        {
            float dt = io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f);
            for (auto& [idx, playing] : gEditorAnimPlaying)
            {
                if (!playing) continue;
                float speed = gEditorAnimSpeed.count(idx) ? gEditorAnimSpeed[idx] : 1.0f;
                gEditorAnimTime[idx] += dt * speed;
                // Check if animation finished (non-looping)
                int slot = gEditorAnimActiveSlot.count(idx) ? gEditorAnimActiveSlot[idx] : -1;
                if (slot >= 0 && idx >= 0 && idx < (int)gStaticMeshNodes.size() && slot < gStaticMeshNodes[idx].animSlotCount)
                {
                    std::filesystem::path ap = ResolveProjectAssetPath(gStaticMeshNodes[idx].animSlots[slot].path);
                    if (!ap.empty())
                    {
                        auto it = gStaticAnimClipCache.find(ap.generic_string());
                        if (it != gStaticAnimClipCache.end() && it->second.valid && it->second.frameCount > 0)
                        {
                            float fps = std::max(1.0f, it->second.fps);
                            int f = (int)std::floor(gEditorAnimTime[idx] * fps);
                            if (f >= (int)it->second.frameCount)
                            {
                                gEditorAnimFinished[idx] = true;
                                bool shouldLoop = gEditorAnimLoop.count(idx) ? gEditorAnimLoop[idx] : true;
                                if (shouldLoop)
                                {
                                    float dur = (float)it->second.frameCount / fps;
                                    if (dur > 0.0f) { while (gEditorAnimTime[idx] >= dur) gEditorAnimTime[idx] -= dur; }
                                    else gEditorAnimTime[idx] = 0.0f;
                                }
                                else
                                {
                                    gEditorAnimPlaying[idx] = false;
                                    gEditorAnimTime[idx] = (float)(it->second.frameCount - 1) / fps;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Transform interaction (unified handler for all node types)
        TickTransformInteraction(forward, up, eye, io.MousePos.x, io.MousePos.y, ImGui::IsMouseClicked(0));

        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(proj.m);
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(view.m);

        // Background gradient, stars, nebula, grid, axes
        DrawViewportBackground(themeMode, gPlayMode);

        // Node gizmos (Audio3D spheres, Camera3D helpers, Node3D boxes, NavMesh3D bounds)
        DrawNodeGizmos(activeCam);

        // StaticMesh3D rendering
        RenderStaticMeshNodes();
RenderImGuiOnly:
        // ImGui
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Top toolbar with buttons
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(ImVec2(vp->Size.x, 28.0f * ImGui::GetIO().FontGlobalScale));
        ImGui::Begin("##TopBar", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.086f, 0.082f, 0.086f, 1.0f));   // #161516
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.086f, 0.082f, 0.086f, 1.0f));

        if (uiIconTex != 0)
        {
            float iconSize = ImGui::GetFontSize() + 8.0f;
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
            ImGui::Image((ImTextureID)(intptr_t)uiIconTex, ImVec2(iconSize, iconSize));
            ImGui::SameLine(0.0f, 6.0f);
        }
        float baseY = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(baseY + 4.0f);
        if (ImGui::Button("File"))
            ImGui::OpenPopup("FileMenu");
        ImGui::SameLine();
        ImGui::SetCursorPosY(baseY + 4.0f);
        if (ImGui::Button("Edit"))
            ImGui::OpenPopup("EditMenu");
        ImGui::SameLine();
        ImGui::SetCursorPosY(baseY + 4.0f);
        if (ImGui::Button("VMU"))
            ImGui::OpenPopup("ToolsMenu");
        ImGui::SameLine();
        ImGui::SetCursorPosY(baseY + 4.0f);
        if (ImGui::Button("Package"))
            ImGui::OpenPopup("PackageMenu");
        ImGui::SameLine();
        ImGui::SetCursorPosY(baseY + 4.0f);
        if (ImGui::Button("Play"))
        {
            if (!gPlayMode)
            {
                playSavedOrbitYaw = orbitYaw;
                playSavedOrbitPitch = orbitPitch;
                playSavedViewYaw = viewYaw;
                playSavedViewPitch = viewPitch;
                playSavedDistance = distance;
                playSavedOrbitCenter = orbitCenter;
                playCamSnapshotValid = true;

                // Snapshot scene/editor state so play-mode changes do not persist after stop.
                playSavedActiveScene = gActiveScene;
                playSavedOpenScenes = gOpenScenes;
                playSavedAudio3DNodes = gAudio3DNodes;
                playSavedStaticMeshNodes = gStaticMeshNodes;
                playSavedCamera3DNodes = gCamera3DNodes;
                playSavedNode3DNodes = gNode3DNodes;
                playSavedNavMesh3DNodes = gNavMesh3DNodes;
                playSceneSnapshotValid = true;
                gPlayOriginalScenes = gOpenScenes;

                gPlayMode = true;

                // Switch to default scene (matches DC boot behavior).
                // Use SetActiveScene so node arrays are properly swapped to
                // the default scene even if a different tab was active.
                if (!gProjectDir.empty())
                {
                    std::string defCfg = GetProjectDefaultScene(std::filesystem::path(gProjectDir));
                    if (!defCfg.empty())
                    {
                        std::filesystem::path defPath(defCfg);
                        if (defPath.is_relative())
                            defPath = std::filesystem::path(gProjectDir) / defPath;
                        std::error_code ec;
                        auto defCanon = std::filesystem::weakly_canonical(defPath, ec);
                        for (int i = 0; i < (int)gOpenScenes.size(); ++i)
                        {
                            auto scnCanon = std::filesystem::weakly_canonical(gOpenScenes[i].path, ec);
                            if (scnCanon == defCanon)
                            {
                                SetActiveScene(i);
                                break;
                            }
                        }
                    }
                }

                if (!BeginPlayScriptRuntime())
                {
                    // MSVC not available — revert play mode entirely
                    gPlayMode = false;
                    gPlayOriginalScenes.clear();
                    if (playSceneSnapshotValid)
                    {
                        gOpenScenes = playSavedOpenScenes;
                        gActiveScene = playSavedActiveScene;
                        gAudio3DNodes = playSavedAudio3DNodes;
                        gStaticMeshNodes = playSavedStaticMeshNodes;
                        gCamera3DNodes = playSavedCamera3DNodes;
                        gNode3DNodes = playSavedNode3DNodes;
                        gNavMesh3DNodes = playSavedNavMesh3DNodes;
                    }
                }
            }
            else
            {
                gPlayMode = false;
                gPlayOriginalScenes.clear();
                EndPlayScriptRuntime();
                if (playCamSnapshotValid)
                {
                    orbitYaw = playSavedOrbitYaw;
                    orbitPitch = playSavedOrbitPitch;
                    viewYaw = playSavedViewYaw;
                    viewPitch = playSavedViewPitch;
                    distance = playSavedDistance;
                    orbitCenter = playSavedOrbitCenter;
                }
                if (playSceneSnapshotValid)
                {
                    gOpenScenes = playSavedOpenScenes;
                    gActiveScene = playSavedActiveScene;
                    gAudio3DNodes = playSavedAudio3DNodes;
                    gStaticMeshNodes = playSavedStaticMeshNodes;
                    gCamera3DNodes = playSavedCamera3DNodes;
                    gNode3DNodes = playSavedNode3DNodes;
                    gNavMesh3DNodes = playSavedNavMesh3DNodes;
                }
            }
        }
        ImGui::SameLine();
        ImGui::SetCursorPosY(baseY + 4.0f);
        ImGui::Checkbox("Wireframe", &gWireframePreview);
        ImGui::SetCursorPosY(baseY + 4.0f);
        ImGui::PopStyleColor(3);

        // Window controls (right side)
        float barW = ImGui::GetWindowWidth();
        float btnW = ImGui::GetFontSize() * 1.6f;
        float btnH = ImGui::GetFrameHeight();
        float pad = ImGui::GetStyle().FramePadding.x;
        ImVec2 btnPos(barW - (btnW * 3.0f + pad * 2.0f) - 12.0f, baseY + 0.0f);
        ImGui::SetCursorPos(btnPos);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.086f, 0.082f, 0.086f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
        if (ImGui::Button("_", ImVec2(btnW, btnH))) glfwIconifyWindow(window);
        ImGui::SameLine();
        if (ImGui::Button("[]", ImVec2(btnW, btnH)))
        {
            if (glfwGetWindowAttrib(window, GLFW_MAXIMIZED)) glfwRestoreWindow(window);
            else glfwMaximizeWindow(window);
        }
        ImGui::SameLine();
        if (ImGui::Button("X", ImVec2(btnW, btnH)))
        {
            if (gShowVmuTool)
            {
                gShowVmuTool = false;
            }
            else
            {
                if (HasUnsavedProjectChanges()) gQuitConfirmOpen = true;
                else glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        }
        ImGui::PopStyleColor(3);

        if (gQuitConfirmOpen)
            ImGui::OpenPopup("ConfirmQuit");
        if (ImGui::BeginPopupModal("ConfirmQuit", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Unsaved changes detected.");
            ImGui::Text("Save All and Exit?");
            if (ImGui::Button("Save All + Exit"))
            {
                SaveAllProjectChanges();
                gQuitConfirmOpen = false;
                glfwSetWindowShouldClose(window, GLFW_TRUE);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                gQuitConfirmOpen = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Drag window by top bar (empty area)
        ImGuiIO& io = ImGui::GetIO();

        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))
        {
            if (io.KeyShift) DoRedo();
            else DoUndo();
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
        {
            if (io.KeyShift) SaveAllProjectChanges();
            else SaveActiveScene();
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C) && !ImGui::GetIO().WantTextInput)
        {
            gHasCopiedNode = false;
            gHasCopiedStatic = false;
            if (gSelectedAudio3D >= 0 && gSelectedAudio3D < (int)gAudio3DNodes.size())
            {
                gHasCopiedNode = true;
                gCopiedNode = gAudio3DNodes[gSelectedAudio3D];
            }
            else if (gSelectedStaticMesh >= 0 && gSelectedStaticMesh < (int)gStaticMeshNodes.size())
            {
                gHasCopiedStatic = true;
                gCopiedStatic = gStaticMeshNodes[gSelectedStaticMesh];
            }
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) && !ImGui::GetIO().WantTextInput)
        {
            if (gHasCopiedNode)
            {
                Audio3DNode node = gCopiedNode;
                // increment name
                std::string base = node.name;
                int num = 1;
                size_t pos = base.find_last_not_of("0123456789");
                if (pos != std::string::npos && pos + 1 < base.size())
                {
                    num = std::stoi(base.substr(pos + 1)) + 1;
                    base = base.substr(0, pos + 1);
                }
                node.name = base + std::to_string(num);

                int idx = (int)gAudio3DNodes.size();
                gAudio3DNodes.push_back(node);
                gSelectedAudio3D = idx;
                gSelectedStaticMesh = -1;

                PushUndo({"Duplicate Audio3D",
                    [idx]() { if (idx >= 0 && idx < (int)gAudio3DNodes.size()) gAudio3DNodes.erase(gAudio3DNodes.begin() + idx); },
                    [idx, node]() {
                        int i = idx;
                        if (i < 0) return;
                        if (i > (int)gAudio3DNodes.size()) i = (int)gAudio3DNodes.size();
                        gAudio3DNodes.insert(gAudio3DNodes.begin() + i, node);
                    }
                });
            }
            else if (gHasCopiedStatic)
            {
                StaticMesh3DNode node = gCopiedStatic;
                std::string base = node.name;
                int num = 1;
                size_t pos = base.find_last_not_of("0123456789");
                if (pos != std::string::npos && pos + 1 < base.size())
                {
                    num = std::stoi(base.substr(pos + 1)) + 1;
                    base = base.substr(0, pos + 1);
                }
                node.name = base + std::to_string(num);

                int idx = (int)gStaticMeshNodes.size();
                gStaticMeshNodes.push_back(node);
                gSelectedStaticMesh = idx;
                gSelectedAudio3D = -1;

                PushUndo({"Duplicate StaticMesh3D",
                    [idx]() { if (idx >= 0 && idx < (int)gStaticMeshNodes.size()) gStaticMeshNodes.erase(gStaticMeshNodes.begin() + idx); },
                    [idx, node]() {
                        int i = idx;
                        if (i < 0) return;
                        if (i > (int)gStaticMeshNodes.size()) i = (int)gStaticMeshNodes.size();
                        gStaticMeshNodes.insert(gStaticMeshNodes.begin() + i, node);
                    }
                });
            }
        }

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        {
            if (ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered())
                draggingWindow = true;
        }
        if (draggingWindow)
        {
            if (ImGui::IsMouseDown(0))
            {
                int wx, wy;
                glfwGetWindowPos(window, &wx, &wy);
                glfwSetWindowPos(window, wx + (int)io.MouseDelta.x, wy + (int)io.MouseDelta.y);
            }
            else
            {
                draggingWindow = false;
            }
        }

        DrawMainMenus(showPreferences, uiScale, themeMode);

        // Left panel: Scene tab
        const float leftPanelWidth = 220.0f * ImGui::GetIO().FontGlobalScale;
        const float rightPanelWidth = 260.0f * ImGui::GetIO().FontGlobalScale;
        const float topBarH = 28.0f * ImGui::GetIO().FontGlobalScale;
        const float leftPanelHeight = vp->Size.y - topBarH;
        const float assetsHeight = leftPanelHeight * 0.5f;

#if 0
        // Debug overlay (picking)
        if (gSelectedAudio3D >= 0 && gSelectedAudio3D < (int)gAudio3DNodes.size())
        {
            const auto& n = gAudio3DNodes[gSelectedAudio3D];
            float sx, sy;
            float sr = 0.0f;
            if (ProjectToScreenGL({ n.x, n.y, n.z }, sx, sy, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y))
            {
                float sxr, syr;
                if (ProjectToScreenGL({ n.x + n.outerRadius, n.y, n.z }, sxr, syr, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y))
                    sr = fabsf(sxr - sx);
                ImGui::GetForegroundDrawList()->AddCircle(ImVec2(sx, sy), sr, IM_COL32(255, 255, 0, 255), 32, 2.0f);
            }
        }

        // Debug HUD
        {
            ImGui::SetNextWindowBgAlpha(0.6f);
            ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + leftPanelWidth + 10.0f, vp->Pos.y + topBarH + 10.0f));
            ImGui::Begin("##DebugHUD", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);
            ImGui::Text("Mouse: %.1f, %.1f", io.MousePos.x, io.MousePos.y);
            ImGui::Text("Selected: %d", gSelectedAudio3D);
            ImGui::Text("Mode: %d  Axis: %c", (int)gTransformMode, gAxisLock ? gAxisLock : '-');
            if (gSelectedAudio3D >= 0 && gSelectedAudio3D < (int)gAudio3DNodes.size())
            {
                const auto& n = gAudio3DNodes[gSelectedAudio3D];
                ImGui::Text("Pos: %.2f %.2f %.2f", n.x, n.y, n.z);
                ImGui::Text("Rot: %.1f %.1f %.1f", n.rotX, n.rotY, n.rotZ);
                ImGui::Text("Scale: %.2f %.2f %.2f", n.scaleX, n.scaleY, n.scaleZ);
            }
            ImGui::Text("Viewport min: %.1f, %.1f", vp->Pos.x + leftPanelWidth, vp->Pos.y + topBarH);
            ImGui::Text("Viewport max: %.1f, %.1f", vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
            ImGui::End();
        }

#endif
        // Viewport selection (click)
        float vpMinX = vp->Pos.x + leftPanelWidth;
        float vpMinY = vp->Pos.y + topBarH;
        float vpMaxX = vp->Pos.x + vp->Size.x - rightPanelWidth;
        float vpMaxY = vp->Pos.y + vp->Size.y;
        bool mouseInViewport = (io.MousePos.x >= vpMinX && io.MousePos.x <= vpMaxX &&
                                io.MousePos.y >= vpMinY && io.MousePos.y <= vpMaxY);

        if (showViewportDebugTab)
        {
            ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + 8.0f, vp->Pos.y + topBarH + 8.0f), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.75f);
            ImGui::Begin("Debug", &showViewportDebugTab,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
            ImGui::Text("Viewport");
            ImGui::Text("min: %.1f, %.1f", vpMinX, vpMinY);
            ImGui::Text("max: %.1f, %.1f", vpMaxX, vpMaxY);
            ImGui::Text("size: %.1f x %.1f", vpMaxX - vpMinX, vpMaxY - vpMinY);
            ImGui::Separator();
            ImGui::Text("Editor Camera");
            ImGui::Text("pos: %.3f, %.3f, %.3f", camPos.x, camPos.y, camPos.z);
            ImGui::Text("view rot(yaw/pitch): %.2f, %.2f", viewYaw, viewPitch);
            ImGui::Text("orbit rot(yaw/pitch): %.2f, %.2f", orbitYaw, orbitPitch);
            ImGui::Text("orbit center: %.3f, %.3f, %.3f", orbitCenter.x, orbitCenter.y, orbitCenter.z);
            ImGui::End();
        }

        TickViewportPicking(window, io.MousePos.x, io.MousePos.y,
                            io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y,
                            ImGui::IsMouseClicked(0) && mouseInViewport);
        DrawSceneOutliner(vp, topBarH, leftPanelWidth, leftPanelHeight, assetsHeight);

        // Left panel (bottom half): Assets tab
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + topBarH + (leftPanelHeight - assetsHeight)));
        ImGui::SetNextWindowSize(ImVec2(leftPanelWidth, assetsHeight));
        ImGui::Begin("Assets", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);
        ImGui::Text("Assets");
        ImGui::Separator();

        if (!gProjectDir.empty())
        {
            std::filesystem::path assetsRoot = std::filesystem::path(gProjectDir) / "Assets";

            if (ImGui::BeginPopupContextWindow("AssetsContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
            {
                std::filesystem::path targetDir = gAssetsCurrentDir.empty() ? assetsRoot : gAssetsCurrentDir;

                if (ImGui::BeginMenu("Create Asset"))
                {
                    if (ImGui::MenuItem("Scene"))
                    {
                        std::filesystem::path created = CreateSceneAsset(targetDir);
                        PushUndo({"Create Scene",
                            [created]() { DeleteAssetPath(created); },
                            [created]() { CreateSceneAssetAt(created); }
                        });
                    }
                    if (ImGui::MenuItem("Material"))
                    {
                        std::filesystem::path created = CreateMaterialAsset(targetDir);
                        PushUndo({"Create Material",
                            [created]() { DeleteAssetPath(created); },
                            [created]() { CreateMaterialAsset(created.parent_path()); }
                        });
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Import Asset"))
                {
                    auto pickedList = PickImportAssetDialog("Import Asset");
                    ImportAssetsToCurrentFolder(pickedList);
                }
                if (ImGui::MenuItem("Convert Asset"))
                {
                    gImportBaseNebMeshPath.clear();
                    std::string picked = PickFbxFileDialog("Convert FBX");
                    if (!picked.empty())
                    {
                        gImportPath = picked;
                        gImportScene = gImportAssimp.ReadFile(gImportPath,
                            aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);
                        gImportAnimConvert.clear();
                        if (gImportScene && gImportScene->mNumAnimations > 0)
                            gImportAnimConvert.resize(gImportScene->mNumAnimations, false);
                        gImportPopupOpen = true;
                    }
                }
                if (ImGui::MenuItem("New Folder"))
                {
                    std::filesystem::path created = CreateAssetFolder(targetDir);
                    PushUndo({"Create Folder",
                        [created]() { DeleteAssetPath(created); },
                        [created]() { CreateAssetFolderAt(created); }
                    });
                }
                ImGui::EndPopup();
            }

            DrawAssetsBrowser(assetsRoot);

            if (gRenameModalOpen)
            {
                ImGui::OpenPopup("RenameItem");
            }
            if (ImGui::BeginPopupModal("RenameItem", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::InputText("New name", gRenameBuffer, sizeof(gRenameBuffer));
                if (ImGui::Button("OK"))
                {
                    if (!gRenamePath.empty())
                    {
                        std::filesystem::path oldPath = gRenamePath;
                        std::filesystem::path newPath = RenameAssetPath(gRenamePath, gRenameBuffer);
                        if (!newPath.empty())
                        {
                            PushUndo({"Rename Asset",
                                [oldPath, newPath]() { if (std::filesystem::exists(newPath)) std::filesystem::rename(newPath, oldPath); },
                                [oldPath, newPath]() { if (std::filesystem::exists(oldPath)) std::filesystem::rename(oldPath, newPath); }
                            });

                            UpdateAssetReferencesForRename(oldPath, newPath);

                            if (oldPath.extension() == ".nebscene" && newPath.extension() == ".nebscene")
                            {
                                for (auto& s : gOpenScenes)
                                {
                                    if (s.path == oldPath)
                                    {
                                        s.path = newPath;
                                        s.name = newPath.stem().string();
                                    }
                                }
                            }
                        }
                        gRenamePath.clear();
                    }
                    gRenameModalOpen = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    gRenameModalOpen = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (gImportPopupOpen)
            {
                ImGui::OpenPopup("Convert Asset");
            }
            if (ImGui::BeginPopupModal("Convert Asset", &gImportPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("FBX: %s", gImportPath.c_str());
                if (!gImportScene)
                {
                    ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Failed to load FBX.");
                }
                else
                {
                    if (gImportScene->mNumAnimations == 0)
                    {
                        ImGui::Text("(no animations)");
                    }
                    else if (ImGui::TreeNode("Animations"))
                    {
                        for (unsigned int i = 0; i < gImportScene->mNumAnimations; ++i)
                        {
                            const aiAnimation* anim = gImportScene->mAnimations[i];
                            std::string animName = anim->mName.length > 0 ? anim->mName.C_Str() : ("Anim" + std::to_string(i + 1));
                            bool checked = gImportAnimConvert[i];
                            if (ImGui::Checkbox(animName.c_str(), &checked))
                                gImportAnimConvert[i] = checked;
                        }
                        ImGui::TreePop();
                    }
                    ImGui::Checkbox("Delta compress (store frame deltas)", &gImportDeltaCompress);
                    ImGui::Checkbox("Duplicate sample rate (2x)", &gImportDoubleSampleRate);
                    ImGui::Checkbox("Include animation provenance mapping", &gImportUseProvenanceMapping);
                    if (!gImportBaseNebMeshPath.empty())
                        ImGui::Text("Target mesh: %s", gImportBaseNebMeshPath.c_str());
                    ImGui::Text("Export: .nebanim BE, 16.16 fixed, %.0f fps (pos only)", gImportDoubleSampleRate ? 24.0f : 12.0f);
                    ImGui::Text("Mesh import: .nebmesh BE, 8.8 fixed, indexed (UV0 if present)");
                    if (gImportDeltaCompress)
                        ImGui::Text("Delta: int16 8.8 per component (smaller)");
                    if (!gImportWarning.empty())
                        ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1), "%s", gImportWarning.c_str());
                }

                if (ImGui::Button("Convert Animations"))
                {
                    int exported = 0;
                    gImportWarning.clear();
                    if (gImportScene && gImportScene->mNumAnimations > 0)
                    {
                        std::filesystem::path inPath(gImportPath);
                        std::string base = inPath.stem().string();
                        std::filesystem::path dir = inPath.parent_path();

                        // Default to source-near anim/ folder.
                        std::filesystem::path animDir = dir / "anim";

                        // If conversion is launched from NebMesh inspector, place output alongside
                        // imported asset folders (mesh/mat/tex/anim) for that asset root.
                        if (!gImportBaseNebMeshPath.empty())
                        {
                            std::filesystem::path nebPath = std::filesystem::path(gImportBaseNebMeshPath);
                            if (nebPath.is_relative())
                            {
                                if (!gProjectDir.empty()) nebPath = std::filesystem::path(gProjectDir) / nebPath;
                                else nebPath = dir / nebPath;
                            }
                            std::filesystem::path meshDir = nebPath.parent_path();
                            if (!meshDir.empty())
                            {
                                std::string leaf = meshDir.filename().string();
                                std::transform(leaf.begin(), leaf.end(), leaf.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                                if (leaf == "mesh")
                                    animDir = meshDir.parent_path() / "anim";
                                else
                                    animDir = meshDir / "anim";
                            }
                        }

                        std::error_code animEc;
                        std::filesystem::create_directories(animDir, animEc);
                        if (!std::filesystem::exists(animDir))
                        {
                            if (!gImportWarning.empty()) gImportWarning += " | ";
                            gImportWarning += "Failed to create anim/ folder; exporting beside source";
                            animDir = dir;
                        }

                        uint32_t targetNebMeshVerts = 0;
                        NebMesh targetNebMesh;
                        if (!gImportBaseNebMeshPath.empty())
                        {
                            std::filesystem::path nebPath = std::filesystem::path(gImportBaseNebMeshPath);
                            if (nebPath.is_relative())
                            {
                                if (!gProjectDir.empty()) nebPath = std::filesystem::path(gProjectDir) / nebPath;
                                else nebPath = dir / nebPath;
                            }
                            if (LoadNebMesh(nebPath, targetNebMesh) && targetNebMesh.valid)
                            {
                                targetNebMeshVerts = (uint32_t)targetNebMesh.positions.size();
                            }
                            else
                            {
                                if (!gImportWarning.empty()) gImportWarning += " | ";
                                gImportWarning += "Target .nebmesh load failed";
                            }
                        }

                        for (unsigned int i = 0; i < gImportScene->mNumAnimations; ++i)
                        {
                            if (!gImportAnimConvert[i]) continue;
                            const aiAnimation* anim = gImportScene->mAnimations[i];
                            std::string animName = anim->mName.length > 0 ? anim->mName.C_Str() : ("Anim" + std::to_string(i + 1));
                            animName = SanitizeName(animName);

                            // Export a single merged .nebanim per animation by concatenating all FBX meshes.
                            std::vector<unsigned int> meshIndices;
                            uint32_t mergedVerts = 0;
                            for (unsigned int mi = 0; mi < gImportScene->mNumMeshes; ++mi)
                            {
                                const aiMesh* m = gImportScene->mMeshes[mi];
                                if (!m) continue;
                                meshIndices.push_back(mi);
                                mergedVerts += (uint32_t)m->mNumVertices;
                            }

                            if (meshIndices.empty())
                            {
                                if (!gImportWarning.empty()) gImportWarning += " | ";
                                gImportWarning += animName + ": no valid FBX mesh found";
                                continue;
                            }

                            std::filesystem::path outPath = animDir / (base + "_" + animName + ".nebanim");
                            std::string warn;
                            if (ExportNebAnimation(gImportScene, anim, meshIndices, outPath, warn,
                                gImportDeltaCompress, targetNebMeshVerts,
                                targetNebMesh.valid ? &targetNebMesh : nullptr, nullptr,
                                gImportDoubleSampleRate ? 2.0f : 1.0f, false))
                            {
                                exported++;
                                if (!warn.empty())
                                {
                                    if (!gImportWarning.empty()) gImportWarning += " | ";
                                    gImportWarning += animName + ": " + warn;
                                }
                            }
                        }
                    }
                    gViewportToast = exported > 0 ? ("Exported " + std::to_string(exported) + " .nebanim") : "No animations exported";
                    gViewportToastUntil = glfwGetTime() + 2.0;
                    gImportPopupOpen = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    gImportPopupOpen = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
        }
        else
        {
            ImGui::Text("(no project selected)");
        }

        ImGui::End();

        // Right panel: Inspector (extracted to src/ui/inspector.cpp)
        gViewYaw = viewYaw;
        gViewPitch = viewPitch;
        gViewDistance = distance;
        gOrbitCenter = orbitCenter;
        gDisplayW = display_w;
        gDisplayH = display_h;
        DrawInspectorPanel(vp, topBarH, rightPanelWidth);
        viewYaw = gViewYaw;
        viewPitch = gViewPitch;
        distance = gViewDistance;
        orbitCenter = gOrbitCenter;
        // NOTE: eye is recomputed each frame from orbitCenter/viewYaw/viewPitch/distance

        DrawSceneTabs(vp, topBarH, leftPanelWidth, rightPanelWidth);

        DrawVmuToolUI(vp);

        DrawNebMeshInspectorWindow(ImGui::GetIO().DeltaTime);

        // Script compile progress bar
        if (gScriptCompileState.load() == 1)
        {
            int done = gScriptCompileDone.load();
            int total = gScriptCompileTotal;
            float pct = (total > 0) ? (float)done / (float)total : 0.0f;

            char label[128];
            snprintf(label, sizeof(label), "Compiling scripts... %d / %d", done, total);
            ImVec2 textSize = ImGui::CalcTextSize(label);

            float barW = 300.0f;
            if (barW < textSize.x + 24.0f) barW = textSize.x + 24.0f;
            float barH = 32.0f;

            // Center in viewport
            float vpCenterX = vp->Pos.x + leftPanelWidth + (vp->Size.x - leftPanelWidth - rightPanelWidth) * 0.5f;
            float vpCenterY = vp->Pos.y + topBarH + (vp->Size.y - topBarH) * 0.5f;
            ImVec2 pos(vpCenterX - barW * 0.5f, vpCenterY - barH * 0.5f);

            auto* dl = ImGui::GetForegroundDrawList();
            // Background
            dl->AddRectFilled(pos, ImVec2(pos.x + barW, pos.y + barH), IM_COL32(20, 20, 20, 220), 6.0f);
            // Fill
            float fillW = (barW - 4.0f) * pct;
            if (fillW > 0.0f)
                dl->AddRectFilled(ImVec2(pos.x + 2.0f, pos.y + 2.0f), ImVec2(pos.x + 2.0f + fillW, pos.y + barH - 2.0f), IM_COL32(80, 160, 255, 255), 4.0f);
            // Border
            dl->AddRect(pos, ImVec2(pos.x + barW, pos.y + barH), IM_COL32(100, 100, 100, 200), 6.0f);
            // Text
            dl->AddText(ImVec2(pos.x + (barW - textSize.x) * 0.5f, pos.y + (barH - textSize.y) * 0.5f), IM_COL32(255, 255, 255, 255), label);
        }

        // Save toast
        if (!gViewportToast.empty() && glfwGetTime() < gViewportToastUntil)
        {
            ImVec2 textSize = ImGui::CalcTextSize(gViewportToast.c_str());
            ImVec2 pos(vp->Pos.x + leftPanelWidth + 12.0f, vp->Pos.y + topBarH + 36.0f);
            ImGui::GetForegroundDrawList()->AddRectFilled(pos, ImVec2(pos.x + textSize.x + 12.0f, pos.y + textSize.y + 8.0f), IM_COL32(0, 0, 0, 180), 4.0f);
            ImGui::GetForegroundDrawList()->AddText(ImVec2(pos.x + 6.0f, pos.y + 4.0f), IM_COL32(255, 255, 255, 255), gViewportToast.c_str());
        }

#if 0
        ImGui::Begin("Nebula");
        ImGui::Text("Viewport prototype");
        ImGui::Separator();
        if (!pointRangeInit)
        {
            glGetFloatv(GL_POINT_SIZE_RANGE, pointRange);
            pointRangeInit = true;
        }
        ImGui::Text("Debug: distance=%.2f FOV=45", distance);
        ImGui::Text("Star radius=200, count=4000");
        ImGui::Text("Screen-space star size=40px");
        ImGui::Text("GL_POINT_SIZE_RANGE: %.1f - %.1f", pointRange[0], pointRange[1]);
        ImGui::End();
#endif

        ImGui::Render();
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

