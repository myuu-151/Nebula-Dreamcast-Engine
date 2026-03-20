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

#include "core/math_types.h"
#include "core/camera3d.h"
#include "editor/prefs.h"
#include "editor/project.h"
#include "ui/import_pipeline.h"
#include "ui/asset_browser.h"
#include "core/meta_io.h"
#include "core/mesh_io.h"
#include "platform/dreamcast/build_helpers.h"
#include "scene/scene_io.h"
#include "nodes/NodeTypes.h"
#include "navmesh/NavMeshBuilder.h"
#include "runtime/runtime_bridge.h"
#include "runtime/script_compile.h"
#include "runtime/physics.h"
#include "editor/file_dialogs.h"
#include "core/math_utils.h"
#include "core/texture_io.h"
#include "core/anim_io.h"
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
static float gDbgDx = 0.0f;
static float gDbgDy = 0.0f;

static bool gHasCopiedNode = false;
static Audio3DNode gCopiedNode;
static bool gHasCopiedStatic = false;
static StaticMesh3DNode gCopiedStatic;

static bool gWireframePreview = false;

// Import state (still in main.cpp — used by import pipeline)
std::string gImportPath;
Assimp::Importer gImportAssimp;
const aiScene* gImportScene = nullptr;
std::vector<bool> gImportAnimConvert;
std::string gImportBaseNebMeshPath;
int gImportBasisMode = 1; // 0=None(raw), 1=Blender(-Z Forward, Y Up), 2=Maya(+Z Forward, Y Up)
static std::vector<std::string> gPendingDroppedImports;

static float gRotateStartX = 0.0f;
static float gRotateStartY = 0.0f;
static float gRotateStartZ = 0.0f;
static float gRotateStartMouseX = 0.0f;
static float gRotateStartMouseY = 0.0f;

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

static GLuint gCheckerOverlayTex = 0;

// DeleteAssetPath, MoveAssetToTrash, RenameAssetPath, DuplicateAssetPath,
// NormalizePathRef, RewritePathRefForRename, UpdateAssetReferencesForRename,
// BeginInlineAssetRename, CommitInlineAssetRename — moved to editor/asset_browser.cpp

// Scene wrappers removed — call NebulaScene:: directly

// HasUnsavedProjectChanges, RefreshOpenSceneTabMetadataForPath, LoadSceneFromPath,
// SetActiveScene, OpenSceneFile, SaveActiveScene, SaveAllProjectChanges
// — moved to scene/scene_manager.cpp

// DrawAssetsBrowser — moved to editor/asset_browser.cpp

// DrawNebMeshInspectorWindow - moved to editor/mesh_inspector.cpp

// LoadImageWIC, LoadTextureWIC, SaveVmuMonoPng, ExportNebTexturePNG moved to core/texture_io.cpp

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

        // Transform interaction
        if (gSelectedAudio3D >= 0 && gSelectedAudio3D < (int)gAudio3DNodes.size())
        {
            auto& n = gAudio3DNodes[gSelectedAudio3D];
            const int selectedId = gSelectedAudio3D;
            if (gTransformMode != Transform_None)
            {
                float dx = 0.0f;
                float dy = 0.0f;
                if (!gTransforming)
                {
                    gTransforming = true;
                    gLastTransformMouseX = io.MousePos.x;
                    gLastTransformMouseY = io.MousePos.y;
                }
                else
                {
                    dx = (float)(io.MousePos.x - gLastTransformMouseX);
                    dy = (float)(io.MousePos.y - gLastTransformMouseY);
                    gLastTransformMouseX = io.MousePos.x;
                    gLastTransformMouseY = io.MousePos.y;
                }

                if ((dx != 0.0f || dy != 0.0f) && !gHasTransformSnapshot)
                {
                    BeginTransformSnapshot();
                }

                Vec3 right, upAxis, forwardAxis;
                forwardAxis = forward;
                right = { forwardAxis.y * up.z - forwardAxis.z * up.y,
                          forwardAxis.z * up.x - forwardAxis.x * up.z,
                          forwardAxis.x * up.y - forwardAxis.y * up.x };
                float rlen = sqrtf(right.x*right.x + right.y*right.y + right.z*right.z);
                if (rlen > 0.0001f) { right.x /= rlen; right.y /= rlen; right.z /= rlen; }
                upAxis = { right.y * forwardAxis.z - right.z * forwardAxis.y,
                           right.z * forwardAxis.x - right.x * forwardAxis.z,
                           right.x * forwardAxis.y - right.y * forwardAxis.x };
                float ulen = sqrtf(upAxis.x*upAxis.x + upAxis.y*upAxis.y + upAxis.z*upAxis.z);
                if (ulen > 0.0001f) { upAxis.x /= ulen; upAxis.y /= ulen; upAxis.z /= ulen; }

                float distToCam = sqrtf((n.x - eye.x)*(n.x - eye.x) + (n.y - eye.y)*(n.y - eye.y) + (n.z - eye.z)*(n.z - eye.z));
                float moveScale = 0.0015f * distToCam;

                if (gTransformMode == Transform_Grab)
                {
                    Vec3 delta = { right.x * -dx * moveScale + upAxis.x * -dy * moveScale,
                                   right.y * -dx * moveScale + upAxis.y * -dy * moveScale,
                                   right.z * -dx * moveScale + upAxis.z * -dy * moveScale };

                    if (gAxisLock == 'X') { n.x += delta.x; }
                    else if (gAxisLock == 'Y') { n.y += delta.y; }
                    else if (gAxisLock == 'Z') { n.z += delta.z; }
                    else { n.x += delta.x; n.y += delta.y; n.z += delta.z; }

                    if (ImGui::IsMouseClicked(0))
                    {
                        if (!gHasTransformSnapshot) BeginTransformSnapshot();
                        EndTransformSnapshot();
                        gTransformMode = Transform_None;
                    }
                }
                else if (gTransformMode == Transform_Rotate)
                {
                    float rotScale = 1.5f;
                    if (!gHasRotatePreview || gRotatePreviewIndex != selectedId)
                    {
                        gHasRotatePreview = true;
                        gRotatePreviewIndex = selectedId;
                        gRotateStartX = n.rotX;
                        gRotateStartY = n.rotY;
                        gRotateStartZ = n.rotZ;
                        gRotateStartMouseX = io.MousePos.x;
                        gRotateStartMouseY = io.MousePos.y;
                    }

                    float rdx = (float)(io.MousePos.x - gRotateStartMouseX);
                    float rdy = (float)(io.MousePos.y - gRotateStartMouseY);
                    gRotatePreviewX = gRotateStartX;
                    gRotatePreviewY = gRotateStartY;
                    gRotatePreviewZ = gRotateStartZ;

                    if (gAxisLock == 'X') gRotatePreviewX += rdy * rotScale;
                    else if (gAxisLock == 'Y') gRotatePreviewY += rdx * rotScale;
                    else if (gAxisLock == 'Z') gRotatePreviewZ += rdx * rotScale;
                    else { gRotatePreviewY += rdx * rotScale; gRotatePreviewX += rdy * rotScale; }

                    if (ImGui::IsMouseClicked(0))
                    {
                        n.rotX = gRotatePreviewX;
                        n.rotY = gRotatePreviewY;
                        n.rotZ = gRotatePreviewZ;
                        gRotateStartX = n.rotX;
                        gRotateStartY = n.rotY;
                        gRotateStartZ = n.rotZ;
                        gRotateStartMouseX = io.MousePos.x;
                        gRotateStartMouseY = io.MousePos.y;
                        gHasRotatePreview = false;
                        if (!gHasTransformSnapshot) BeginTransformSnapshot();
                        EndTransformSnapshot();
                        gTransformMode = Transform_None;
                    }
                }
                else if (gTransformMode == Transform_Scale)
                {
                    float s = 1.0f + ((-dy + dx) * 0.03f);
                    if (s < 0.01f) s = 0.01f;

                    n.innerRadius *= s;
                    n.outerRadius *= s;
                    if (n.innerRadius < 0.01f) n.innerRadius = 0.01f;
                    if (n.outerRadius < 0.01f) n.outerRadius = 0.01f;

                    if (gAxisLock == 'X') n.scaleX *= s;
                    else if (gAxisLock == 'Y') n.scaleY *= s;
                    else if (gAxisLock == 'Z') n.scaleZ *= s;
                    else { n.scaleX *= s; n.scaleY *= s; n.scaleZ *= s; }

                    if (ImGui::IsMouseClicked(0))
                    {
                        if (!gHasTransformSnapshot) BeginTransformSnapshot();
                        EndTransformSnapshot();
                        gTransformMode = Transform_None;
                    }
                }

                if (gTransformMode == Transform_Scale) { gDbgDx = dx; gDbgDy = dy; }
            }
            else
            {
                gTransforming = false;
            }
        }
        else if (gSelectedStaticMesh >= 0 && gSelectedStaticMesh < (int)gStaticMeshNodes.size())
        {
            auto& n = gStaticMeshNodes[gSelectedStaticMesh];
            const int selectedId = 10000 + gSelectedStaticMesh;
            if (gTransformMode != Transform_None)
            {
                float dx = 0.0f;
                float dy = 0.0f;
                if (!gTransforming)
                {
                    gTransforming = true;
                    gLastTransformMouseX = io.MousePos.x;
                    gLastTransformMouseY = io.MousePos.y;
                }
                else
                {
                    dx = (float)(io.MousePos.x - gLastTransformMouseX);
                    dy = (float)(io.MousePos.y - gLastTransformMouseY);
                    gLastTransformMouseX = io.MousePos.x;
                    gLastTransformMouseY = io.MousePos.y;
                }

                if ((dx != 0.0f || dy != 0.0f) && !gHasTransformSnapshot)
                {
                    BeginTransformSnapshot();
                }

                Vec3 right, upAxis, forwardAxis;
                forwardAxis = forward;
                right = { forwardAxis.y * up.z - forwardAxis.z * up.y,
                          forwardAxis.z * up.x - forwardAxis.x * up.z,
                          forwardAxis.x * up.y - forwardAxis.y * up.x };
                float rlen = sqrtf(right.x*right.x + right.y*right.y + right.z*right.z);
                if (rlen > 0.0001f) { right.x /= rlen; right.y /= rlen; right.z /= rlen; }
                upAxis = { right.y * forwardAxis.z - right.z * forwardAxis.y,
                           right.z * forwardAxis.x - right.x * forwardAxis.z,
                           right.x * forwardAxis.y - right.y * forwardAxis.x };
                float ulen = sqrtf(upAxis.x*upAxis.x + upAxis.y*upAxis.y + upAxis.z*upAxis.z);
                if (ulen > 0.0001f) { upAxis.x /= ulen; upAxis.y /= ulen; upAxis.z /= ulen; }

                float distToCam = sqrtf((n.x - eye.x)*(n.x - eye.x) + (n.y - eye.y)*(n.y - eye.y) + (n.z - eye.z)*(n.z - eye.z));
                float moveScale = 0.0015f * distToCam;

                if (gTransformMode == Transform_Grab)
                {
                    Vec3 delta = { right.x * -dx * moveScale + upAxis.x * -dy * moveScale,
                                   right.y * -dx * moveScale + upAxis.y * -dy * moveScale,
                                   right.z * -dx * moveScale + upAxis.z * -dy * moveScale };

                    if (gAxisLock == 'X') { n.x += delta.x; }
                    else if (gAxisLock == 'Y') { n.y += delta.y; }
                    else if (gAxisLock == 'Z') { n.z += delta.z; }
                    else { n.x += delta.x; n.y += delta.y; n.z += delta.z; }

                    if (ImGui::IsMouseClicked(0))
                    {
                        if (!gHasTransformSnapshot) BeginTransformSnapshot();
                        EndTransformSnapshot();
                        gTransformMode = Transform_None;
                    }
                }
                else if (gTransformMode == Transform_Rotate)
                {
                    float rotScale = 1.5f;
                    if (!gHasRotatePreview || gRotatePreviewIndex != selectedId)
                    {
                        gHasRotatePreview = true;
                        gRotatePreviewIndex = selectedId;
                        gRotateStartX = n.rotX;
                        gRotateStartY = n.rotY;
                        gRotateStartZ = n.rotZ;
                        gRotateStartMouseX = io.MousePos.x;
                        gRotateStartMouseY = io.MousePos.y;
                    }

                    float rdx = (float)(io.MousePos.x - gRotateStartMouseX);
                    float rdy = (float)(io.MousePos.y - gRotateStartMouseY);
                    gRotatePreviewX = gRotateStartX;
                    gRotatePreviewY = gRotateStartY;
                    gRotatePreviewZ = gRotateStartZ;

                    if (gAxisLock == 'X') gRotatePreviewX += rdy * rotScale;
                    else if (gAxisLock == 'Y') gRotatePreviewY += rdx * rotScale;
                    else if (gAxisLock == 'Z') gRotatePreviewZ += rdx * rotScale;
                    else { gRotatePreviewY += rdx * rotScale; gRotatePreviewX += rdy * rotScale; }

                    if (ImGui::IsMouseClicked(0))
                    {
                        n.rotX = gRotatePreviewX;
                        n.rotY = gRotatePreviewY;
                        n.rotZ = gRotatePreviewZ;
                        gRotateStartX = n.rotX;
                        gRotateStartY = n.rotY;
                        gRotateStartZ = n.rotZ;
                        gRotateStartMouseX = io.MousePos.x;
                        gRotateStartMouseY = io.MousePos.y;
                        gHasRotatePreview = false;
                        if (!gHasTransformSnapshot) BeginTransformSnapshot();
                        EndTransformSnapshot();
                        gTransformMode = Transform_None;
                    }
                }
                else if (gTransformMode == Transform_Scale)
                {
                    float s = 1.0f + ((-dy + dx) * 0.03f);
                    if (s < 0.01f) s = 0.01f;

                    if (gAxisLock == 'X') n.scaleX *= s;
                    else if (gAxisLock == 'Y') n.scaleY *= s;
                    else if (gAxisLock == 'Z') n.scaleZ *= s;
                    else { n.scaleX *= s; n.scaleY *= s; n.scaleZ *= s; }

                    if (ImGui::IsMouseClicked(0))
                    {
                        if (!gHasTransformSnapshot) BeginTransformSnapshot();
                        EndTransformSnapshot();
                        gTransformMode = Transform_None;
                    }
                }

                if (gTransformMode == Transform_Scale) { gDbgDx = dx; gDbgDy = dy; }
            }
            else
            {
                gTransforming = false;
            }
        }
        else if (gSelectedNode3D >= 0 && gSelectedNode3D < (int)gNode3DNodes.size())
        {
            auto& n = gNode3DNodes[gSelectedNode3D];
            const int selectedId = 40000 + gSelectedNode3D;
            if (gTransformMode != Transform_None)
            {
                float dx = 0.0f;
                float dy = 0.0f;
                if (!gTransforming)
                {
                    gTransforming = true;
                    gLastTransformMouseX = io.MousePos.x;
                    gLastTransformMouseY = io.MousePos.y;
                }
                else
                {
                    dx = (float)(io.MousePos.x - gLastTransformMouseX);
                    dy = (float)(io.MousePos.y - gLastTransformMouseY);
                    gLastTransformMouseX = io.MousePos.x;
                    gLastTransformMouseY = io.MousePos.y;
                }

                if ((dx != 0.0f || dy != 0.0f) && !gHasTransformSnapshot)
                {
                    BeginTransformSnapshot();
                }

                Vec3 right, upAxis, forwardAxis;
                forwardAxis = forward;
                right = { forwardAxis.y * up.z - forwardAxis.z * up.y,
                          forwardAxis.z * up.x - forwardAxis.x * up.z,
                          forwardAxis.x * up.y - forwardAxis.y * up.x };
                float rlen = sqrtf(right.x*right.x + right.y*right.y + right.z*right.z);
                if (rlen > 0.0001f) { right.x /= rlen; right.y /= rlen; right.z /= rlen; }
                upAxis = { right.y * forwardAxis.z - right.z * forwardAxis.y,
                           right.z * forwardAxis.x - right.x * forwardAxis.z,
                           right.x * forwardAxis.y - right.y * forwardAxis.x };
                float ulen = sqrtf(upAxis.x*upAxis.x + upAxis.y*upAxis.y + upAxis.z*upAxis.z);
                if (ulen > 0.0001f) { upAxis.x /= ulen; upAxis.y /= ulen; upAxis.z /= ulen; }

                float distToCam = sqrtf((n.x - eye.x)*(n.x - eye.x) + (n.y - eye.y)*(n.y - eye.y) + (n.z - eye.z)*(n.z - eye.z));
                float moveScale = 0.0015f * distToCam;

                if (gTransformMode == Transform_Grab)
                {
                    Vec3 delta = { right.x * -dx * moveScale + upAxis.x * -dy * moveScale,
                                   right.y * -dx * moveScale + upAxis.y * -dy * moveScale,
                                   right.z * -dx * moveScale + upAxis.z * -dy * moveScale };

                    if (gAxisLock == 'X') { n.x += delta.x; }
                    else if (gAxisLock == 'Y') { n.y += delta.y; }
                    else if (gAxisLock == 'Z') { n.z += delta.z; }
                    else { n.x += delta.x; n.y += delta.y; n.z += delta.z; }

                    if (ImGui::IsMouseClicked(0))
                    {
                        if (!gHasTransformSnapshot) BeginTransformSnapshot();
                        EndTransformSnapshot();
                        gTransformMode = Transform_None;
                    }
                }
                else if (gTransformMode == Transform_Rotate)
                {
                    float rotScale = 1.5f;
                    if (!gHasRotatePreview || gRotatePreviewIndex != selectedId)
                    {
                        gHasRotatePreview = true;
                        gRotatePreviewIndex = selectedId;
                        gRotateStartX = n.rotX;
                        gRotateStartY = n.rotY;
                        gRotateStartZ = n.rotZ;
                        gRotateStartMouseX = io.MousePos.x;
                        gRotateStartMouseY = io.MousePos.y;
                    }

                    float rdx = (float)(io.MousePos.x - gRotateStartMouseX);
                    float rdy = (float)(io.MousePos.y - gRotateStartMouseY);
                    gRotatePreviewX = gRotateStartX;
                    gRotatePreviewY = gRotateStartY;
                    gRotatePreviewZ = gRotateStartZ;

                    if (gAxisLock == 'X') gRotatePreviewX += rdy * rotScale;
                    else if (gAxisLock == 'Y') gRotatePreviewY += rdx * rotScale;
                    else if (gAxisLock == 'Z') gRotatePreviewZ += rdx * rotScale;
                    else { gRotatePreviewY += rdx * rotScale; gRotatePreviewX += rdy * rotScale; }

                    if (ImGui::IsMouseClicked(0))
                    {
                        n.rotX = gRotatePreviewX;
                        n.rotY = gRotatePreviewY;
                        n.rotZ = gRotatePreviewZ;
                        SyncNode3DQuatFromEuler(n);
                        gRotateStartX = n.rotX;
                        gRotateStartY = n.rotY;
                        gRotateStartZ = n.rotZ;
                        gRotateStartMouseX = io.MousePos.x;
                        gRotateStartMouseY = io.MousePos.y;
                        gHasRotatePreview = false;
                        if (!gHasTransformSnapshot) BeginTransformSnapshot();
                        EndTransformSnapshot();
                        gTransformMode = Transform_None;
                    }
                }
                else if (gTransformMode == Transform_Scale)
                {
                    float s = 1.0f + ((-dy + dx) * 0.03f);
                    if (s < 0.01f) s = 0.01f;

                    if (gAxisLock == 'X') n.scaleX *= s;
                    else if (gAxisLock == 'Y') n.scaleY *= s;
                    else if (gAxisLock == 'Z') n.scaleZ *= s;
                    else { n.scaleX *= s; n.scaleY *= s; n.scaleZ *= s; }

                    if (ImGui::IsMouseClicked(0))
                    {
                        if (!gHasTransformSnapshot) BeginTransformSnapshot();
                        EndTransformSnapshot();
                        gTransformMode = Transform_None;
                    }
                }

                if (gTransformMode == Transform_Scale) { gDbgDx = dx; gDbgDy = dy; }
            }
            else
            {
                gTransforming = false;
            }
        }
        else if (gSelectedNavMesh3D >= 0 && gSelectedNavMesh3D < (int)gNavMesh3DNodes.size())
        {
            auto& n = gNavMesh3DNodes[gSelectedNavMesh3D];
            const int selectedId = 50000 + gSelectedNavMesh3D;
            if (gTransformMode != Transform_None)
            {
                float dx = 0.0f;
                float dy = 0.0f;
                if (!gTransforming)
                {
                    gTransforming = true;
                    gLastTransformMouseX = io.MousePos.x;
                    gLastTransformMouseY = io.MousePos.y;
                }
                else
                {
                    dx = (float)(io.MousePos.x - gLastTransformMouseX);
                    dy = (float)(io.MousePos.y - gLastTransformMouseY);
                    gLastTransformMouseX = io.MousePos.x;
                    gLastTransformMouseY = io.MousePos.y;
                }

                if ((dx != 0.0f || dy != 0.0f) && !gHasTransformSnapshot)
                {
                    BeginTransformSnapshot();
                }

                Vec3 right, upAxis, forwardAxis;
                forwardAxis = forward;
                right = { forwardAxis.y * up.z - forwardAxis.z * up.y,
                          forwardAxis.z * up.x - forwardAxis.x * up.z,
                          forwardAxis.x * up.y - forwardAxis.y * up.x };
                float rlen = sqrtf(right.x*right.x + right.y*right.y + right.z*right.z);
                if (rlen > 0.0001f) { right.x /= rlen; right.y /= rlen; right.z /= rlen; }
                upAxis = { right.y * forwardAxis.z - right.z * forwardAxis.y,
                           right.z * forwardAxis.x - right.x * forwardAxis.z,
                           right.x * forwardAxis.y - right.y * forwardAxis.x };
                float ulen = sqrtf(upAxis.x*upAxis.x + upAxis.y*upAxis.y + upAxis.z*upAxis.z);
                if (ulen > 0.0001f) { upAxis.x /= ulen; upAxis.y /= ulen; upAxis.z /= ulen; }

                float distToCam = sqrtf((n.x - eye.x)*(n.x - eye.x) + (n.y - eye.y)*(n.y - eye.y) + (n.z - eye.z)*(n.z - eye.z));
                float moveScale = 0.0015f * distToCam;

                if (gTransformMode == Transform_Grab)
                {
                    Vec3 delta = { right.x * -dx * moveScale + upAxis.x * -dy * moveScale,
                                   right.y * -dx * moveScale + upAxis.y * -dy * moveScale,
                                   right.z * -dx * moveScale + upAxis.z * -dy * moveScale };

                    if (gAxisLock == 'X') { n.x += delta.x; }
                    else if (gAxisLock == 'Y') { n.y += delta.y; }
                    else if (gAxisLock == 'Z') { n.z += delta.z; }
                    else { n.x += delta.x; n.y += delta.y; n.z += delta.z; }

                    if (ImGui::IsMouseClicked(0))
                    {
                        if (!gHasTransformSnapshot) BeginTransformSnapshot();
                        EndTransformSnapshot();
                        gTransformMode = Transform_None;
                    }
                }
                else if (gTransformMode == Transform_Rotate)
                {
                    float rotScale = 1.5f;
                    if (!gHasRotatePreview || gRotatePreviewIndex != selectedId)
                    {
                        gHasRotatePreview = true;
                        gRotatePreviewIndex = selectedId;
                        gRotateStartX = n.rotX;
                        gRotateStartY = n.rotY;
                        gRotateStartZ = n.rotZ;
                        gRotateStartMouseX = io.MousePos.x;
                        gRotateStartMouseY = io.MousePos.y;
                    }

                    float rdx = (float)(io.MousePos.x - gRotateStartMouseX);
                    float rdy = (float)(io.MousePos.y - gRotateStartMouseY);
                    gRotatePreviewX = gRotateStartX;
                    gRotatePreviewY = gRotateStartY;
                    gRotatePreviewZ = gRotateStartZ;

                    if (gAxisLock == 'X') gRotatePreviewX += rdy * rotScale;
                    else if (gAxisLock == 'Y') gRotatePreviewY += rdx * rotScale;
                    else if (gAxisLock == 'Z') gRotatePreviewZ += rdx * rotScale;
                    else { gRotatePreviewY += rdx * rotScale; gRotatePreviewX += rdy * rotScale; }

                    if (ImGui::IsMouseClicked(0))
                    {
                        n.rotX = gRotatePreviewX;
                        n.rotY = gRotatePreviewY;
                        n.rotZ = gRotatePreviewZ;
                        gRotateStartX = n.rotX;
                        gRotateStartY = n.rotY;
                        gRotateStartZ = n.rotZ;
                        gRotateStartMouseX = io.MousePos.x;
                        gRotateStartMouseY = io.MousePos.y;
                        gHasRotatePreview = false;
                        if (!gHasTransformSnapshot) BeginTransformSnapshot();
                        EndTransformSnapshot();
                        gTransformMode = Transform_None;
                    }
                }
                else if (gTransformMode == Transform_Scale)
                {
                    float s = 1.0f + ((-dy + dx) * 0.03f);
                    if (s < 0.01f) s = 0.01f;

                    if (gAxisLock == 'X') n.scaleX *= s;
                    else if (gAxisLock == 'Y') n.scaleY *= s;
                    else if (gAxisLock == 'Z') n.scaleZ *= s;
                    else { n.scaleX *= s; n.scaleY *= s; n.scaleZ *= s; }

                    if (ImGui::IsMouseClicked(0))
                    {
                        if (!gHasTransformSnapshot) BeginTransformSnapshot();
                        EndTransformSnapshot();
                        gTransformMode = Transform_None;
                    }
                }

                if (gTransformMode == Transform_Scale) { gDbgDx = dx; gDbgDy = dy; }
            }
            else
            {
                gTransforming = false;
            }
        }

        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(proj.m);
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(view.m);

        // Background gradient — fully reset GL state so vertex colors are not
        // modulated by leftover lighting / material state from the previous frame.
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_ALPHA_TEST);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_BLEND);
        glDisable(GL_LIGHTING);
        glDisable(GL_LIGHT0);
        glDisable(GL_COLOR_MATERIAL);
        glDisable(GL_NORMALIZE);
        glShadeModel(GL_SMOOTH);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        glBegin(GL_QUADS);
        if (gPlayMode)
        {
            glColor3f(0.0f, 0.0f, 0.0f);
            glVertex3f(-1.0f, -1.0f, 0.0f);
            glVertex3f( 1.0f, -1.0f, 0.0f);
            glVertex3f( 1.0f,  1.0f, 0.0f);
            glVertex3f(-1.0f,  1.0f, 0.0f);
        }
        else if (themeMode == 0)
        {
            glColor3f(0.031f, 0.035f, 0.047f); // bottom #08090C
            glVertex3f(-1.0f, -1.0f, 0.0f);
            glVertex3f( 1.0f, -1.0f, 0.0f);
            glColor3f(0.078f, 0.086f, 0.110f); // top #14161C
            glVertex3f( 1.0f,  1.0f, 0.0f);
            glVertex3f(-1.0f,  1.0f, 0.0f);
        }
        else if (themeMode == 1)
        {
            glColor3f(0.165f, 0.212f, 0.239f); // bottom #2A363D
            glVertex3f(-1.0f, -1.0f, 0.0f);
            glVertex3f( 1.0f, -1.0f, 0.0f);
            glColor3f(0.427f, 0.498f, 0.537f); // top #6D7F89
            glVertex3f( 1.0f,  1.0f, 0.0f);
            glVertex3f(-1.0f,  1.0f, 0.0f);
        }
        else if (themeMode == 2)
        {
            glColor3f(0.086f, 0.133f, 0.161f); // bottom #162229
            glVertex3f(-1.0f, -1.0f, 0.0f);
            glVertex3f( 1.0f, -1.0f, 0.0f);
            glColor3f(0.459f, 0.659f, 0.698f); // top #75A8B2
            glVertex3f( 1.0f,  1.0f, 0.0f);
            glVertex3f(-1.0f,  1.0f, 0.0f);
        }
        else if (themeMode == 3)
        {
            glColor3f(0.353f, 0.349f, 0.361f); // bottom #5A595C
            glVertex3f(-1.0f, -1.0f, 0.0f);
            glVertex3f( 1.0f, -1.0f, 0.0f);
            glColor3f(0.498f, 0.494f, 0.514f); // top #7F7E83
            glVertex3f( 1.0f,  1.0f, 0.0f);
            glVertex3f(-1.0f,  1.0f, 0.0f);
        }
        else
        {
            glColor3f(0.0f, 0.0f, 0.0f); // bottom #000000
            glVertex3f(-1.0f, -1.0f, 0.0f);
            glVertex3f( 1.0f, -1.0f, 0.0f);
            glColor3f(0.0f, 0.0f, 0.0f); // top #000000
            glVertex3f( 1.0f,  1.0f, 0.0f);
            glVertex3f(-1.0f,  1.0f, 0.0f);
        }
        glEnd();

        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glEnable(GL_DEPTH_TEST);

        // Fog disabled for now (was hiding stars)
        glDisable(GL_FOG);

        // Procedural stars (world-space)
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_POINT_SMOOTH);

        if (gCheckerOverlayTex == 0)
        {
            const int texSize = 64;
            std::vector<unsigned char> pixels(texSize * texSize * 4);
            for (int y = 0; y < texSize; ++y)
            {
                for (int x = 0; x < texSize; ++x)
                {
                    bool even = (((x >> 3) + (y >> 3)) & 1) == 0;
                    int i = (y * texSize + x) * 4;
                    if (even)
                    {
                        // White checker cells are transparent
                        pixels[i + 0] = 220; pixels[i + 1] = 200; pixels[i + 2] = 255; pixels[i + 3] = 0;
                    }
                    else
                    {
                        // Purple checker cells are visible
                        pixels[i + 0] = 120; pixels[i + 1] = 90; pixels[i + 2] = 235; pixels[i + 3] = 255;
                    }
                }
            }

            glGenTextures(1, &gCheckerOverlayTex);
            glBindTexture(GL_TEXTURE_2D, gCheckerOverlayTex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texSize, texSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        glPointSize(2.0f);
        glBegin(GL_POINTS);
        static bool starsInit = false;
        static float stars[4000][3];
        static float starPhase[4000];
        static float starSpeed[4000];
        static float starTint[4000];
        static float starSize[4000];
        static float nebula[400][3];
        static float nebulaCol[400][3];
        if (!starsInit)
        {
            starsInit = true;
            for (int i = 0; i < 4000; ++i)
            {
                float u = (float)rand() / (float)RAND_MAX;
                float v = (float)rand() / (float)RAND_MAX;
                float theta = u * 6.28318f;
                float phi = acosf(2.0f * v - 1.0f);
                float r = 500.0f;
                stars[i][0] = r * sinf(phi) * cosf(theta);
                stars[i][1] = r * cosf(phi);
                stars[i][2] = r * sinf(phi) * sinf(theta);
                starPhase[i] = (float)rand() / (float)RAND_MAX * 6.28318f;
                starSpeed[i] = 0.5f + (float)rand() / (float)RAND_MAX * 1.5f;
                starTint[i] = (float)rand() / (float)RAND_MAX; // 0..1
                starSize[i] = 1.0f + (float)rand() / (float)RAND_MAX * 999.0f;
            }
            for (int i = 0; i < 400; ++i)
            {
                float u = (float)rand() / (float)RAND_MAX;
                float v = (float)rand() / (float)RAND_MAX;
                float theta = u * 6.28318f;
                float phi = acosf(2.0f * v - 1.0f);
                float r = 480.0f;
                nebula[i][0] = r * sinf(phi) * cosf(theta);
                nebula[i][1] = r * cosf(phi);
                nebula[i][2] = r * sinf(phi) * sinf(theta);
                nebulaCol[i][0] = 0.20f + 0.40f * ((float)rand() / (float)RAND_MAX);
                nebulaCol[i][1] = 0.15f + 0.30f * ((float)rand() / (float)RAND_MAX);
                nebulaCol[i][2] = 0.30f + 0.45f * ((float)rand() / (float)RAND_MAX);
            }

            // (reverted)
        }
        double t = glfwGetTime();

        if (themeMode == 0)
        {
            // Nebula (mid, world-space)
            glPointSize(10.0f);
            for (int i = 0; i < 400; ++i)
            {
                glColor4f(nebulaCol[i][0], nebulaCol[i][1], nebulaCol[i][2], 0.6f);
                glVertex3f(nebula[i][0], nebula[i][1], nebula[i][2]);
            }

            // Stars (front) � GL_POINTS buckets (3/6/12px)
            auto drawPoints = [&](float sizeMin, float sizeMax, float px)
            {
                glPointSize(px);
                glBegin(GL_POINTS);
                for (int i = 0; i < 4000; ++i)
                {
                    if (starSize[i] < sizeMin || starSize[i] > sizeMax) continue;
                    float b = 0.3f + 0.7f * (0.5f + 0.5f * sinf((float)t * starSpeed[i] + starPhase[i]));
                    if (starTint[i] < 0.5f) glColor4f(b, b, b, b);
                    else glColor4f(0.6f * b, 0.7f * b, 1.0f * b, b);
                    glVertex3f(stars[i][0], stars[i][1], stars[i][2]);
                }
                glEnd();
            };

            drawPoints(0.0f, 300.0f, 3.0f);
            drawPoints(300.0f, 700.0f, 6.0f);
            drawPoints(700.0f, 10000.0f, 10.0f);
        }

        // (reverted)

        // Grid
        const int gridSize = 10;
        const float spacing = 0.5f;
        float half = gridSize * spacing;

        glColor3f(0.6f, 0.6f, 0.6f);
        glBegin(GL_LINES);
        for (int i = -gridSize; i <= gridSize; ++i)
        {
            if (i == 0) continue; // skip axes so colors stay clean
            float x = i * spacing;
            glVertex3f(x, 0.0f, -half);
            glVertex3f(x, 0.0f, half);

            float z = i * spacing;
            glVertex3f(-half, 0.0f, z);
            glVertex3f(half, 0.0f, z);
        }
        glEnd();

        // Axes
        glBegin(GL_LINES);
        glColor3f(0.8f, 0.2f, 0.2f);
        glVertex3f(-half, 0.001f, 0.0f);
        glVertex3f(half, 0.001f, 0.0f);

        // amber Z axis
        glColor3f(0.95f, 0.65f, 0.1f);
        glVertex3f(0.0f, 0.001f, -half);
        glVertex3f(0.0f, 0.001f, half);

        // border removed
        glEnd();

        // Audio3D visual indicators (wire sphere + vertical line)
        for (int i = 0; i < (int)gAudio3DNodes.size(); ++i)
        {
            const auto& a = gAudio3DNodes[i];
            bool selected = (gSelectedAudio3D == i);
            if (gHideUnselectedWireframes && !selected) continue;

            float rOuter = a.outerRadius;
            float rInner = a.innerRadius;
            if (rOuter < rInner) std::swap(rOuter, rInner);
            float scaleAvg = (a.scaleX + a.scaleY + a.scaleZ) / 3.0f;
            if (scaleAvg < 0.01f) scaleAvg = 0.01f;
            if (selected) glColor3f(1.0f, 1.0f, 1.0f);
            else glColor3f(0.2f, 0.6f, 1.0f);

            // Vertical marker
            glBegin(GL_LINES);
            glVertex3f(a.x, a.y, a.z);
            glVertex3f(a.x, a.y + 1.0f, a.z);
            glEnd();

            const int segments = 16;
            const int rings = 12;

            auto drawSphere = [&](float r, float cr, float cg, float cb)
            {
                if (selected) glColor3f(1.0f, 1.0f, 1.0f);
                else glColor3f(cr, cg, cb);
                // Latitude rings
                for (int j = 1; j < rings; ++j)
                {
                    float v = (float)j / (float)rings;
                    float phi = v * 3.1415926f;
                    float y = cosf(phi) * r;
                    float rr = sinf(phi) * r;

                    glBegin(GL_LINE_LOOP);
                    for (int i = 0; i < segments; ++i)
                    {
                        float t = (float)i / (float)segments * 6.2831853f;
                        float x = cosf(t) * rr;
                        float z = sinf(t) * rr;
                        glVertex3f(x, y, z);
                    }
                    glEnd();
                }

                // Longitude rings
                for (int i = 0; i < segments; ++i)
                {
                    float t = (float)i / (float)segments * 6.2831853f;
                    float cx = cosf(t);
                    float cz = sinf(t);

                    glBegin(GL_LINE_LOOP);
                    for (int j = 0; j <= rings; ++j)
                    {
                        float v = (float)j / (float)rings;
                        float phi = v * 3.1415926f;
                        float y = cosf(phi) * r;
                        float rr = sinf(phi) * r;
                        float x = cx * rr;
                        float z = cz * rr;
                        glVertex3f(x, y, z);
                    }
                    glEnd();
                }
            };

            glPushMatrix();
            glTranslatef(a.x, a.y, a.z);
            float drawRotX = a.rotX;
            float drawRotY = a.rotY;
            float drawRotZ = a.rotZ;
            if (gTransformMode == Transform_Rotate && gHasRotatePreview && gRotatePreviewIndex == i)
            {
                drawRotX = gRotatePreviewX;
                drawRotY = gRotatePreviewY;
                drawRotZ = gRotatePreviewZ;
            }
            glRotatef(drawRotX, 1.0f, 0.0f, 0.0f);
            glRotatef(drawRotY, 0.0f, 1.0f, 0.0f);
            glRotatef(drawRotZ, 0.0f, 0.0f, 1.0f);

            drawSphere(rOuter, 0.2f, 0.6f, 1.0f);
            drawSphere(rInner, 0.1f, 1.0f, 0.4f);
            drawSphere(0.25f * scaleAvg, 1.0f, 0.9f, 0.2f); // node core

            // Rotation axes (visual)
            Vec3 rAxis, uAxis, fAxis;
            GetLocalAxes(a, rAxis, uAxis, fAxis);
            float axisLen = 0.6f * scaleAvg;
            glBegin(GL_LINES);
            glColor3f(1.0f, 0.2f, 0.2f); // X
            glVertex3f(0.0f, 0.0f, 0.0f);
            glVertex3f(rAxis.x * axisLen, rAxis.y * axisLen, rAxis.z * axisLen);
            glColor3f(0.2f, 1.0f, 0.2f); // Y
            glVertex3f(0.0f, 0.0f, 0.0f);
            glVertex3f(uAxis.x * axisLen, uAxis.y * axisLen, uAxis.z * axisLen);
            glColor3f(0.2f, 0.4f, 1.0f); // Z
            glVertex3f(0.0f, 0.0f, 0.0f);
            glVertex3f(fAxis.x * axisLen, fAxis.y * axisLen, fAxis.z * axisLen);
            glEnd();

            glPopMatrix();

            // Infinite axis line for G/R + axis lock (world axis)
            if ((gTransformMode == Transform_Grab || gTransformMode == Transform_Rotate) && gAxisLock && gSelectedAudio3D == i)
            {
                Vec3 axis = { 1.0f, 0.0f, 0.0f };
                if (gAxisLock == 'Y') axis = { 0.0f, 1.0f, 0.0f };
                if (gAxisLock == 'Z') axis = { 0.0f, 0.0f, 1.0f };

                float len = 2000.0f;
                glLineWidth(2.5f);
                glBegin(GL_LINES);
                if (gAxisLock == 'X') glColor3f(1.0f, 0.4f, 0.4f);
                else if (gAxisLock == 'Y') glColor3f(0.4f, 1.0f, 0.4f);
                else glColor3f(0.4f, 0.6f, 1.0f);
                glVertex3f(a.x - axis.x * len, a.y - axis.y * len, a.z - axis.z * len);
                glVertex3f(a.x + axis.x * len, a.y + axis.y * len, a.z + axis.z * len);
                glEnd();
                glLineWidth(1.0f);
            }
        }

        // Camera3D visual helpers (original fallback marker)

        for (int i = 0; i < (int)gCamera3DNodes.size(); ++i)
        {
            const auto& c = gCamera3DNodes[i];
            bool selected = (gSelectedCamera3D == i);
            if (gHideUnselectedWireframes && !selected) continue;
            // Don't draw helper for the camera currently driving the play viewport.
            if (gPlayMode && activeCam == &gCamera3DNodes[i]) continue;

            glDisable(GL_TEXTURE_2D);
            if (selected) glColor3f(1.0f, 1.0f, 1.0f);
            else if (c.main) glColor3f(0.2f, 1.0f, 0.4f);
            else glColor3f(0.1f, 0.8f, 0.3f);

            float cwx, cwy, cwz, cwrx, cwry, cwrz;
            GetCamera3DWorldTR(i, cwx, cwy, cwz, cwrx, cwry, cwrz);
            glPushMatrix();
            glTranslatef(cwx, cwy, cwz);
            glRotatef(cwrx, 1.0f, 0.0f, 0.0f);
            glRotatef(cwry, 0.0f, 1.0f, 0.0f);
            glRotatef(cwrz, 0.0f, 0.0f, 1.0f);

            // original helper marker
            glBegin(GL_LINES);
            glVertex3f(0.0f, 0.0f, 0.0f); glVertex3f(0.0f, 0.0f, 1.0f);
            glVertex3f(0.0f, 0.0f, 0.0f); glVertex3f(0.3f, 0.2f, 0.7f);
            glVertex3f(0.0f, 0.0f, 0.0f); glVertex3f(-0.3f, 0.2f, 0.7f);
            glVertex3f(0.0f, 0.0f, 0.0f); glVertex3f(0.3f, -0.2f, 0.7f);
            glVertex3f(0.0f, 0.0f, 0.0f); glVertex3f(-0.3f, -0.2f, 0.7f);
            glEnd();

            glPopMatrix();
        }

        // Node3D rendering (uses primitive mesh, defaults to cube_primitive)
        glDisable(GL_TEXTURE_2D);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        for (int i = 0; i < (int)gNode3DNodes.size(); ++i)
        {
            const auto& n = gNode3DNodes[i];
            const bool selected = (gSelectedNode3D == i);
            if (selected) glColor3f(1.0f, 1.0f, 1.0f);
            else glColor3f(0.55f, 0.9f, 1.0f);

            float wx, wy, wz, wqw, wqx, wqy, wqz, wsx, wsy, wsz;
            GetNode3DWorldTRSQuat(i, wx, wy, wz, wqw, wqx, wqy, wqz, wsx, wsy, wsz);
            glPushMatrix();
            glTranslatef(wx, wy, wz);
            { float rm[16]; QuatToGLMatrix(wqw, wqx, wqy, wqz, rm); glMultMatrixf(rm); }
            // Apply local bounds offset (collision-only, does not affect parent/child transforms).
            glTranslatef(n.boundPosX, n.boundPosY, n.boundPosZ);
            // Apply local collision extents to Node3D box size, without affecting hierarchy inheritance.
            const float ex = std::max(0.0f, n.extentX * 2.0f);
            const float ey = std::max(0.0f, n.extentY * 2.0f);
            const float ez = std::max(0.0f, n.extentZ * 2.0f);
            glScalef(wsx * ex, wsy * ey, wsz * ez);

            const float q = 0.5f;
            glBegin(GL_QUADS);
            // Front
            glVertex3f(-q, -q,  q); glVertex3f( q, -q,  q); glVertex3f( q,  q,  q); glVertex3f(-q,  q,  q);
            // Back
            glVertex3f( q, -q, -q); glVertex3f(-q, -q, -q); glVertex3f(-q,  q, -q); glVertex3f( q,  q, -q);
            // Left
            glVertex3f(-q, -q, -q); glVertex3f(-q, -q,  q); glVertex3f(-q,  q,  q); glVertex3f(-q,  q, -q);
            // Right
            glVertex3f( q, -q,  q); glVertex3f( q, -q, -q); glVertex3f( q,  q, -q); glVertex3f( q,  q,  q);
            // Top
            glVertex3f(-q,  q,  q); glVertex3f( q,  q,  q); glVertex3f( q,  q, -q); glVertex3f(-q,  q, -q);
            // Bottom
            glVertex3f(-q, -q, -q); glVertex3f( q, -q, -q); glVertex3f( q, -q,  q); glVertex3f(-q, -q,  q);
            glEnd();
            glPopMatrix();
        }

        // NavMesh3D bounds rendering (wireframe box, cyan for positive, red for negator)
        for (int i = 0; i < (int)gNavMesh3DNodes.size(); ++i)
        {
            const auto& n = gNavMesh3DNodes[i];
            if (!n.navBounds) continue;
            const bool selected = (gSelectedNavMesh3D == i);
            if (selected) glColor3f(1.0f, 1.0f, 1.0f);
            else if (n.navNegator) glColor3f(1.0f, 0.25f, 0.25f);
            else glColor3f(n.wireR, n.wireG, n.wireB);

            glLineWidth(n.wireThickness);
            glPushMatrix();
            glTranslatef(n.x, n.y, n.z);
            glRotatef(n.rotX, 1.0f, 0.0f, 0.0f);
            glRotatef(n.rotY, 0.0f, 1.0f, 0.0f);
            glRotatef(n.rotZ, 0.0f, 0.0f, 1.0f);
            glScalef(n.scaleX * n.extentX, n.scaleY * n.extentY, n.scaleZ * n.extentZ);

            const float q = 0.5f;
            glBegin(GL_QUADS);
            glVertex3f(-q, -q,  q); glVertex3f( q, -q,  q); glVertex3f( q,  q,  q); glVertex3f(-q,  q,  q);
            glVertex3f( q, -q, -q); glVertex3f(-q, -q, -q); glVertex3f(-q,  q, -q); glVertex3f( q,  q, -q);
            glVertex3f(-q, -q, -q); glVertex3f(-q, -q,  q); glVertex3f(-q,  q,  q); glVertex3f(-q,  q, -q);
            glVertex3f( q, -q,  q); glVertex3f( q, -q, -q); glVertex3f( q,  q, -q); glVertex3f( q,  q,  q);
            glVertex3f(-q,  q,  q); glVertex3f( q,  q,  q); glVertex3f( q,  q, -q); glVertex3f(-q,  q, -q);
            glVertex3f(-q, -q, -q); glVertex3f( q, -q, -q); glVertex3f( q, -q,  q); glVertex3f(-q, -q,  q);
            glEnd();
            glPopMatrix();
            glLineWidth(1.0f);
        }

        // StaticMesh3D rendering (basic)
        glPolygonMode(GL_FRONT_AND_BACK, gWireframePreview ? GL_LINE : GL_FILL);
        for (int i = 0; i < (int)gStaticMeshNodes.size(); ++i)
        {
            const auto& s = gStaticMeshNodes[i];
            const bool selected = (gSelectedStaticMesh == i);
            if (s.mesh.empty() || gProjectDir.empty()) continue;
            std::filesystem::path meshPath = std::filesystem::path(gProjectDir) / s.mesh;
            const NebMesh* mesh = GetNebMesh(meshPath);
            if (!mesh || !mesh->valid) continue;

            std::vector<Vec3> staticAnimPosed;
            const std::vector<Vec3>* renderPositions = &mesh->positions;
            // Determine which anim to use: slot preview > play-mode active slot > legacy vtxAnim
            std::string resolvedAnimRef;
            if (i == gStaticAnimPreviewNode && gStaticAnimPreviewSlot >= 0 && gStaticAnimPreviewSlot < s.animSlotCount && !s.animSlots[gStaticAnimPreviewSlot].path.empty())
                resolvedAnimRef = s.animSlots[gStaticAnimPreviewSlot].path;
            else if (gPlayMode && s.runtimeTest && gEditorAnimActiveSlot.count((int)i) && gEditorAnimActiveSlot[(int)i] >= 0 && gEditorAnimActiveSlot[(int)i] < s.animSlotCount)
                resolvedAnimRef = s.animSlots[gEditorAnimActiveSlot[(int)i]].path;
            else if (!s.vtxAnim.empty())
                resolvedAnimRef = s.vtxAnim;
            if (!resolvedAnimRef.empty())
            {
                std::filesystem::path animPath = ResolveProjectAssetPath(resolvedAnimRef);
                if (!animPath.empty() && std::filesystem::exists(animPath))
                {
                    std::string key = animPath.generic_string();
                    auto it = gStaticAnimClipCache.find(key);
                    if (it == gStaticAnimClipCache.end())
                    {
                        NebAnimClip clip;
                        std::string err;
                        if (LoadNebAnimClip(animPath, clip, err))
                            it = gStaticAnimClipCache.emplace(key, std::move(clip)).first;
                    }
                    if (it != gStaticAnimClipCache.end() && it->second.valid && it->second.vertexCount <= mesh->positions.size() && !it->second.frames.empty())
                    {
                        int frame = 0;
                        if (gPlayMode && s.runtimeTest && gEditorAnimPlaying.count((int)i) && gEditorAnimPlaying[(int)i])
                        {
                            float elapsed = gEditorAnimTime.count((int)i) ? gEditorAnimTime[(int)i] : 0.0f;
                            float speed = gEditorAnimSpeed.count((int)i) ? gEditorAnimSpeed[(int)i] : 1.0f;
                            const float fps = std::max(1.0f, it->second.fps);
                            int f = (int)std::floor(elapsed * fps);
                            if (it->second.frameCount > 0)
                                frame = f % (int)it->second.frameCount;
                        }
                        else if (gPlayMode && s.runtimeTest)
                        {
                            const float fps = std::max(1.0f, it->second.fps);
                            int f = (int)std::floor((float)glfwGetTime() * fps);
                            if (it->second.frameCount > 0)
                                frame = f % (int)it->second.frameCount;
                        }
                        else if (i == gStaticAnimPreviewNode)
                        {
                            frame = std::max(0, std::min(gStaticAnimPreviewFrame, (int)it->second.frameCount - 1));
                        }
                        frame = std::max(0, std::min(frame, (int)it->second.frames.size() - 1));

                        const std::vector<Vec3>& f0 = it->second.frames[0];
                        const std::vector<Vec3>& ff = it->second.frames[(size_t)frame];
                        const size_t nv = std::min(f0.size(), std::min(ff.size(), mesh->positions.size()));
                        if (nv > 0)
                        {
                            staticAnimPosed.resize(mesh->positions.size());
                            if (it->second.meshAligned)
                            {
                                for (size_t vi = 0; vi < nv; ++vi)
                                    staticAnimPosed[vi] = ff[vi];
                            }
                            else
                            {
                                Vec3 bMin = mesh->positions[0], bMax = mesh->positions[0], bC = {0,0,0};
                                Vec3 aMin = f0[0], aMax = f0[0], aC = {0,0,0};
                                for (size_t vi = 0; vi < nv; ++vi)
                                {
                                    const Vec3& bp = mesh->positions[vi];
                                    const Vec3& ap = f0[vi];
                                    bC.x += bp.x; bC.y += bp.y; bC.z += bp.z;
                                    aC.x += ap.x; aC.y += ap.y; aC.z += ap.z;
                                    bMin.x = std::min(bMin.x, bp.x); bMin.y = std::min(bMin.y, bp.y); bMin.z = std::min(bMin.z, bp.z);
                                    bMax.x = std::max(bMax.x, bp.x); bMax.y = std::max(bMax.y, bp.y); bMax.z = std::max(bMax.z, bp.z);
                                    aMin.x = std::min(aMin.x, ap.x); aMin.y = std::min(aMin.y, ap.y); aMin.z = std::min(aMin.z, ap.z);
                                    aMax.x = std::max(aMax.x, ap.x); aMax.y = std::max(aMax.y, ap.y); aMax.z = std::max(aMax.z, ap.z);
                                }
                                const float invN = 1.0f / (float)nv;
                                bC.x *= invN; bC.y *= invN; bC.z *= invN;
                                aC.x *= invN; aC.y *= invN; aC.z *= invN;

                                const float bDiag = sqrtf((bMax.x-bMin.x)*(bMax.x-bMin.x) + (bMax.y-bMin.y)*(bMax.y-bMin.y) + (bMax.z-bMin.z)*(bMax.z-bMin.z));
                                const float aDiag = sqrtf((aMax.x-aMin.x)*(aMax.x-aMin.x) + (aMax.y-aMin.y)*(aMax.y-aMin.y) + (aMax.z-aMin.z)*(aMax.z-aMin.z));
                                float absScale = 1.0f;
                                if (aDiag > 1e-6f && bDiag > 1e-6f)
                                    absScale = bDiag / aDiag;

                                for (size_t vi = 0; vi < nv; ++vi)
                                {
                                    staticAnimPosed[vi].x = bC.x + (ff[vi].x - aC.x) * absScale;
                                    staticAnimPosed[vi].y = bC.y + (ff[vi].y - aC.y) * absScale;
                                    staticAnimPosed[vi].z = bC.z + (ff[vi].z - aC.z) * absScale;
                                }
                            }
                            for (size_t vi = nv; vi < mesh->positions.size(); ++vi)
                                staticAnimPosed[vi] = mesh->positions[vi];
                            renderPositions = &staticAnimPosed;
                        }
                    }
                }
            }

            struct MatState { GLuint tex = 0; bool flipU = false; bool flipV = false; float satU = 1.0f; float satV = 1.0f; float uvScale = 0.0f; float uvScaleU = 1.0f; float uvScaleV = 1.0f; int shadingMode = 0; float lightRotation = 0.0f; float lightPitch = 0.0f; float lightRoll = 0.0f; float shadowIntensity = 1.0f; int shadingUv = -1; };
            std::unordered_map<int, MatState> matState;
            auto getMatState = [&](int matIndex) -> MatState {
                auto it = matState.find(matIndex);
                if (it != matState.end()) return it->second;

                MatState st{};
                std::string matRef = NebulaNodes::GetStaticMeshMaterialByIndex(s, matIndex);
                if (matRef.empty() && matIndex == 0) matRef = s.material; // legacy scene fallback
                if (!matRef.empty())
                {
                    std::filesystem::path matPath = std::filesystem::path(gProjectDir) / matRef;
                    if (matPath.extension() == ".nebtex")
                    {
                        st.tex = GetNebTexture(matPath);
                        if (st.tex == 0 && std::filesystem::exists(matPath))
                        {
                            gNebTextureCache.erase(matPath.string());
                            st.tex = GetNebTexture(matPath);
                        }
                        NebulaAssets::LoadNebTexFlipOptions(matPath, st.flipU, st.flipV);
                        if (gPreviewSaturnSampling)
                            GetNebTexSaturnPadUvScale(matPath, st.satU, st.satV);
                    }
                    else
                    {
                        NebulaAssets::LoadMaterialUvScale(matPath, st.uvScale);
                        { float su=1,sv=1,ou=0,ov=0,rd=0; NebulaAssets::LoadMaterialUvTransform(matPath,su,sv,ou,ov,rd); st.uvScaleU=su; st.uvScaleV=sv; }
                        st.shadingMode = NebulaAssets::LoadMaterialShadingMode(matPath);
                        st.lightRotation = NebulaAssets::LoadMaterialLightRotation(matPath);
                        st.lightPitch = NebulaAssets::LoadMaterialLightPitch(matPath);
                        st.lightRoll = NebulaAssets::LoadMaterialLightRoll(matPath);
                        st.shadowIntensity = NebulaAssets::LoadMaterialShadowIntensity(matPath);
                        st.shadingUv = NebulaAssets::LoadMaterialShadingUv(matPath);
                        std::string texPath;
                        if (NebulaAssets::LoadMaterialTexture(matPath, texPath) && !texPath.empty())
                        {
                            std::filesystem::path tpath = std::filesystem::path(gProjectDir) / texPath;
                            st.tex = GetNebTexture(tpath);
                            if (st.tex == 0 && std::filesystem::exists(tpath))
                            {
                                gNebTextureCache.erase(tpath.string());
                                st.tex = GetNebTexture(tpath);
                            }
                            if (tpath.extension() == ".nebtex")
                            {
                                NebulaAssets::LoadNebTexFlipOptions(tpath, st.flipU, st.flipV);
                                if (gPreviewSaturnSampling)
                                    GetNebTexSaturnPadUvScale(tpath, st.satU, st.satV);
                            }
                        }
                    }
                }
                matState[matIndex] = st;
                return st;
            };

            int seedMatIndex = s.materialSlot;
            for (size_t idx = 0; idx + 2 < mesh->indices.size(); idx += 3)
            {
                uint16_t i0 = mesh->indices[idx + 0];
                uint16_t i1 = mesh->indices[idx + 1];
                uint16_t i2 = mesh->indices[idx + 2];
                if (i0 >= mesh->positions.size() || i1 >= mesh->positions.size() || i2 >= mesh->positions.size())
                    continue;
                int triIndex = (int)(idx / 3);
                if (mesh->hasFaceMaterial && triIndex >= 0 && triIndex < (int)mesh->faceMaterial.size())
                    seedMatIndex = (int)mesh->faceMaterial[triIndex];
                break;
            }

            // Warm all slot material states up-front so slot visibility is not dependent on draw order.
            for (int si = 0; si < kStaticMeshMaterialSlots; ++si) (void)getMatState(si);

            // Check if any material slot uses lit shading
            bool anyLit = false;
            for (auto& kv : matState) { if (kv.second.shadingMode == 1 || kv.second.shadingUv >= 0) { anyLit = true; break; } }

            // Compute smooth vertex normals by position so shading is
            // always smooth across the entire geometry, even across
            // material/UV seam splits where vertices are duplicated.
            // Groups by original (rest-pose) mesh positions for stable
            // welding even under animation, computes normals from rendered
            // positions for correct lighting.
            std::vector<Vec3> smoothNormals;
            if (anyLit && renderPositions && !mesh->indices.empty())
            {
                const size_t vertCount = renderPositions->size();
                const std::vector<Vec3>& basePos = mesh->positions; // rest-pose for grouping

                // Compute adaptive weld distance from bounding box diagonal
                // so it works regardless of model scale.
                Vec3 bMin = basePos[0], bMax = basePos[0];
                for (size_t vi = 1; vi < vertCount && vi < basePos.size(); ++vi)
                {
                    const Vec3& p = basePos[vi];
                    if (p.x < bMin.x) bMin.x = p.x; if (p.x > bMax.x) bMax.x = p.x;
                    if (p.y < bMin.y) bMin.y = p.y; if (p.y > bMax.y) bMax.y = p.y;
                    if (p.z < bMin.z) bMin.z = p.z; if (p.z > bMax.z) bMax.z = p.z;
                }
                float diag = sqrtf((bMax.x-bMin.x)*(bMax.x-bMin.x) + (bMax.y-bMin.y)*(bMax.y-bMin.y) + (bMax.z-bMin.z)*(bMax.z-bMin.z));
                const float weldDist = std::max(0.01f, diag * 0.005f); // 0.5% of bounding box
                const float weldDist2 = weldDist * weldDist;
                std::vector<size_t> vertToGroup(vertCount, (size_t)-1);
                std::vector<Vec3> groupNormalAccum;
                size_t numGroups = 0;

                for (size_t vi = 0; vi < vertCount; ++vi)
                {
                    if (vertToGroup[vi] != (size_t)-1) continue;
                    const Vec3& pa = (vi < basePos.size()) ? basePos[vi] : (*renderPositions)[vi];
                    size_t gi = numGroups++;
                    vertToGroup[vi] = gi;
                    groupNormalAccum.push_back({0.0f, 0.0f, 0.0f});
                    // Find all other unassigned verts at the same position
                    for (size_t vj = vi + 1; vj < vertCount; ++vj)
                    {
                        if (vertToGroup[vj] != (size_t)-1) continue;
                        const Vec3& pb = (vj < basePos.size()) ? basePos[vj] : (*renderPositions)[vj];
                        float dx = pa.x - pb.x, dy = pa.y - pb.y, dz = pa.z - pb.z;
                        if (dx*dx + dy*dy + dz*dz <= weldDist2)
                            vertToGroup[vj] = gi;
                    }
                }

                // Accumulate face normals per position group
                for (size_t idx = 0; idx + 2 < mesh->indices.size(); idx += 3)
                {
                    uint16_t i0 = mesh->indices[idx + 0];
                    uint16_t i1 = mesh->indices[idx + 1];
                    uint16_t i2 = mesh->indices[idx + 2];
                    if (i0 >= vertCount || i1 >= vertCount || i2 >= vertCount)
                        continue;
                    const Vec3& p0 = (*renderPositions)[i0];
                    const Vec3& p1 = (*renderPositions)[i1];
                    const Vec3& p2 = (*renderPositions)[i2];
                    float ex = p1.x - p0.x, ey = p1.y - p0.y, ez = p1.z - p0.z;
                    float fx = p2.x - p0.x, fy = p2.y - p0.y, fz = p2.z - p0.z;
                    // Negate cross product to compensate for flipped winding from
                    // import basis X-negate (handedness flip reverses triangle order)
                    float nx = -(ey * fz - ez * fy);
                    float ny = -(ez * fx - ex * fz);
                    float nz = -(ex * fy - ey * fx);
                    size_t g0 = vertToGroup[i0], g1 = vertToGroup[i1], g2 = vertToGroup[i2];
                    groupNormalAccum[g0].x += nx; groupNormalAccum[g0].y += ny; groupNormalAccum[g0].z += nz;
                    groupNormalAccum[g1].x += nx; groupNormalAccum[g1].y += ny; groupNormalAccum[g1].z += nz;
                    groupNormalAccum[g2].x += nx; groupNormalAccum[g2].y += ny; groupNormalAccum[g2].z += nz;
                }

                // Normalize group normals
                for (auto& n : groupNormalAccum)
                {
                    float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
                    if (len > 1e-8f) { n.x /= len; n.y /= len; n.z /= len; }
                    else { n.x = 0.0f; n.y = 1.0f; n.z = 0.0f; }
                }

                // Assign the shared group normal to every vertex index
                smoothNormals.resize(vertCount);
                for (size_t vi = 0; vi < vertCount; ++vi)
                    smoothNormals[vi] = groupNormalAccum[vertToGroup[vi]];

                // DEBUG: print weld stats once per mesh per frame
                static int dbgFrame = 0;
                if (dbgFrame++ < 5)
                    fprintf(stderr, "[SMOOTH] verts=%zu groups=%zu weldDist=%.6f diag=%.4f\n", vertCount, numGroups, weldDist, diag);
            }

            // Set up GL lighting if any material is lit
            if (anyLit)
            {
                glEnable(GL_LIGHTING);
                glEnable(GL_LIGHT0);
                glEnable(GL_COLOR_MATERIAL);
                glEnable(GL_NORMALIZE); // renormalize after modelview scale
                glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
                glShadeModel(GL_SMOOTH);
            }

            MatState primaryState = getMatState(seedMatIndex);
            if (primaryState.tex != 0 && mesh->hasUv)
            {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, primaryState.tex);
            }
            else
            {
                glDisable(GL_TEXTURE_2D);
            }

            float wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz;
            GetStaticMeshWorldTRS(i, wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz);
            glPushMatrix();
            glTranslatef(wx, wy, wz);
            // If parented under Node3D, use quaternion rotation (avoids Euler gimbal lock)
            int parentNode3DIdx = -1;
            if (!s.parent.empty()) parentNode3DIdx = FindNode3DByName(s.parent);
            if (parentNode3DIdx >= 0)
            {
                // Parent (Node3D) rotation via quaternion matrix
                float pqw, pqx, pqy, pqz, _px, _py, _pz, _psx, _psy, _psz;
                GetNode3DWorldTRSQuat(parentNode3DIdx, _px, _py, _pz, pqw, pqx, pqy, pqz, _psx, _psy, _psz);
                { float rm[16]; QuatToGLMatrix(pqw, pqx, pqy, pqz, rm); glMultMatrixf(rm); }
                // Child local rotation on top (visual offset)
                glRotatef(s.rotX, 1.0f, 0.0f, 0.0f);
                glRotatef(s.rotY, 0.0f, 1.0f, 0.0f);
                glRotatef(s.rotZ, 0.0f, 0.0f, 1.0f);
            }
            else
            {
                // Standalone StaticMesh rotation axis remap:
                // X <- Z (pitch/back-forth), Y <- X (yaw left-right), Z <- Y (roll/spin)
                glRotatef(wrz, 1.0f, 0.0f, 0.0f);
                glRotatef(wrx, 0.0f, 1.0f, 0.0f);
                glRotatef(wry, 0.0f, 0.0f, 1.0f);
            }
            glScalef(wsx, wsy, wsz);

            // Transform smooth normals to camera space via full modelview matrix.
            // This gives view-dependent shading (light moves with camera) matching DC build.
            // Note: material light values will visually match DC when the editor camera
            // is at the same angle as the DC game camera.
            std::vector<Vec3> csNormals;
            if (!smoothNormals.empty())
            {
                float mv[16];
                glGetFloatv(GL_MODELVIEW_MATRIX, mv);
                csNormals.resize(smoothNormals.size());
                for (size_t vi = 0; vi < smoothNormals.size(); ++vi)
                {
                    const Vec3& n = smoothNormals[vi];
                    float tx = mv[0]*n.x + mv[4]*n.y + mv[8]*n.z;
                    float ty = mv[1]*n.x + mv[5]*n.y + mv[9]*n.z;
                    float tz = mv[2]*n.x + mv[6]*n.y + mv[10]*n.z;
                    float len = sqrtf(tx*tx + ty*ty + tz*tz);
                    if (len > 1e-8f) { csNormals[vi] = {tx/len, ty/len, tz/len}; }
                    else { csNormals[vi] = {0.0f, 1.0f, 0.0f}; }
                }
            }

            auto setWireColorForMat = [&](int faceMat)
            {
                if (selected) { glColor3f(1.0f, 1.0f, 1.0f); return; }
                float r = 0.45f + 0.40f * fabsf(sinf((float)(faceMat * 1.7f + 0.3f)));
                float g = 0.45f + 0.40f * fabsf(sinf((float)(faceMat * 2.3f + 1.1f)));
                float b = 0.45f + 0.40f * fabsf(sinf((float)(faceMat * 1.1f + 2.2f)));
                glColor3f(r, g, b);
            };

            if (gWireframePreview && mesh->hasFaceTopology && !mesh->faceVertexCounts.empty())
            {
                // Quad/polygon wireframe path: draw polygon boundary edges from source face topology
                // so quads do not show triangulation diagonals.
                glDisable(GL_TEXTURE_2D);
                glBegin(GL_LINES);
                int triCursor = 0;
                const int triCount = (int)(mesh->indices.size() / 3);
                for (size_t fi = 0; fi < mesh->faceVertexCounts.size() && triCursor < triCount; ++fi)
                {
                    int fv = (int)mesh->faceVertexCounts[fi];
                    if (fv < 3) continue;
                    int faceTriCount = std::max(1, fv - 2);
                    if (triCursor + faceTriCount > triCount) break;

                    int faceMat = 0;
                    if (mesh->hasFaceMaterial && triCursor >= 0 && triCursor < (int)mesh->faceMaterial.size())
                        faceMat = (int)mesh->faceMaterial[triCursor];
                    setWireColorForMat(faceMat);

                    std::vector<uint16_t> poly;
                    poly.reserve((size_t)fv);
                    uint16_t v0 = mesh->indices[triCursor * 3 + 0];
                    uint16_t v1 = mesh->indices[triCursor * 3 + 1];
                    poly.push_back(v0);
                    poly.push_back(v1);
                    for (int k = 0; k < faceTriCount; ++k)
                    {
                        uint16_t vk = mesh->indices[(triCursor + k) * 3 + 2];
                        poly.push_back(vk);
                    }
                    triCursor += faceTriCount;

                    for (size_t k = 0; k < poly.size(); ++k)
                    {
                        uint16_t ia = poly[k];
                        uint16_t ib = poly[(k + 1) % poly.size()];
                        if (ia >= renderPositions->size() || ib >= renderPositions->size()) continue;
                        const Vec3& a = (*renderPositions)[ia];
                        const Vec3& b = (*renderPositions)[ib];
                        glVertex3f(a.x, a.y, a.z);
                        glVertex3f(b.x, b.y, b.z);
                    }
                }
                glEnd();
            }
            else
            {
                if (gWireframePreview) glDisable(GL_TEXTURE_2D);
                // If no material is lit, disable lighting for this mesh's triangles
                if (!anyLit) glDisable(GL_LIGHTING);
                glBegin(GL_TRIANGLES);
                // Force first triangle to bind from its own face material slot.
                GLuint boundTex = 0xFFFFFFFFu;
                int curLit = -1; // -1 = uninitialized
                int curShadingUv = -2; // -2 = uninitialized
                float curLightRot = -999.0f;
                float curLightPit = -999.0f;
                float curLightRol = -999.0f;
                float curShadowInt = -999.0f;
                float swLx = 0.0f, swLy = -1.0f, swLz = 0.0f;
                float swAmb = 0.35f, swDif = 0.9f, swShQ = 0.0f;
                for (size_t idx = 0; idx + 2 < mesh->indices.size(); idx += 3)
                {
                    uint16_t i0 = mesh->indices[idx + 0];
                    uint16_t i1 = mesh->indices[idx + 1];
                    uint16_t i2 = mesh->indices[idx + 2];
                    if (i0 >= renderPositions->size() || i1 >= renderPositions->size() || i2 >= renderPositions->size())
                        continue;

                    int triIndex = (int)(idx / 3);
                    int faceMat = 0;
                    if (mesh->hasFaceMaterial && triIndex >= 0 && triIndex < (int)mesh->faceMaterial.size())
                        faceMat = (int)mesh->faceMaterial[triIndex];
                    MatState triState = getMatState(faceMat);
                    if (gWireframePreview) setWireColorForMat(faceMat);
                    else glColor3f(1.0f, 1.0f, 1.0f);
                    int triLit = triState.shadingMode;
                    bool needRestart = (triState.tex != boundTex) || (triLit != curLit) || (triState.shadingUv != curShadingUv) || ((triLit == 1 || triState.shadingUv >= 0) && (triState.lightRotation != curLightRot || triState.lightPitch != curLightPit || triState.lightRoll != curLightRol || triState.shadowIntensity != curShadowInt));
                    if (needRestart)
                    {
                        glEnd();
                        if (triLit != curLit || triState.shadingUv != curShadingUv)
                        {
                            // All shading now done in software to match DC build exactly
                            glDisable(GL_LIGHTING);
                            curLit = triLit;
                            curShadingUv = triState.shadingUv;
                        }
                        if ((triLit == 1 || triState.shadingUv >= 0) && (triState.lightRotation != curLightRot || triState.lightPitch != curLightPit || triState.lightRoll != curLightRol))
                        {
                            // Match DC build light direction: spherical coords from yaw/pitch
                            float yRad = triState.lightRotation * 3.14159265f / 180.0f;
                            float xRad = triState.lightPitch * 3.14159265f / 180.0f;
                            float dx = sinf(yRad) * cosf(xRad);
                            float dy = sinf(xRad);
                            float dz = cosf(yRad) * cosf(xRad);
                            float len = sqrtf(dx * dx + dy * dy + dz * dz);
                            if (len > 1e-8f) { dx /= len; dy /= len; dz /= len; }
                            else { dx = 0.0f; dy = -1.0f; dz = 0.0f; }
                            swLx = dx; swLy = dy; swLz = dz;
                            curLightRot = triState.lightRotation;
                            curLightPit = triState.lightPitch;
                            curLightRol = triState.lightRoll;
                        }
                        if ((triLit == 1 || triState.shadingUv >= 0) && triState.shadowIntensity != curShadowInt)
                        {
                            float si = triState.shadowIntensity;
                            if (si < 0.0f) si = 0.0f; if (si > 2.0f) si = 2.0f;
                            swAmb = 0.35f;
                            swDif = 0.9f - 0.25f * si;
                            swShQ = 0.25f * si;
                            curShadowInt = triState.shadowIntensity;
                        }
                        if (triState.tex != 0 && mesh->hasUv) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, triState.tex); }
                        else glDisable(GL_TEXTURE_2D);
                        glBegin(GL_TRIANGLES);
                        boundTex = triState.tex;
                    }

                    if (triState.tex != 0 && mesh->hasUv && i0 < mesh->uvs.size()) { float u = mesh->uvs[i0].x; float v = 1.0f - mesh->uvs[i0].y; float uvMul = powf(2.0f, -triState.uvScale); u *= uvMul * triState.uvScaleU; v *= uvMul * triState.uvScaleV; if (gPreviewSaturnSampling) { u *= triState.satU; v *= triState.satV; } if (triState.flipU) u = 1.0f - u; if (triState.flipV) v = 1.0f - v; glTexCoord2f(u, v); }
                    if ((triState.shadingUv >= 0 || triLit == 1) && i0 < csNormals.size()) { const Vec3& sn = csNormals[i0]; float d = sn.x * swLx + sn.y * swLy + sn.z * swLz; if (d < 0.0f) d = 0.0f; float c = swAmb + d * swDif - ((sn.y < 0.0f) ? (-sn.y * swShQ) : 0.0f) + (1.0f - fabsf(sn.z)) * 0.12f; glColor3f(c, c, c); }
                    const Vec3& v0 = (*renderPositions)[i0];
                    glVertex3f(v0.x, v0.y, v0.z);
                    if (triState.tex != 0 && mesh->hasUv && i1 < mesh->uvs.size()) { float u = mesh->uvs[i1].x; float v = 1.0f - mesh->uvs[i1].y; float uvMul = powf(2.0f, -triState.uvScale); u *= uvMul * triState.uvScaleU; v *= uvMul * triState.uvScaleV; if (gPreviewSaturnSampling) { u *= triState.satU; v *= triState.satV; } if (triState.flipU) u = 1.0f - u; if (triState.flipV) v = 1.0f - v; glTexCoord2f(u, v); }
                    if ((triState.shadingUv >= 0 || triLit == 1) && i1 < csNormals.size()) { const Vec3& sn = csNormals[i1]; float d = sn.x * swLx + sn.y * swLy + sn.z * swLz; if (d < 0.0f) d = 0.0f; float c = swAmb + d * swDif - ((sn.y < 0.0f) ? (-sn.y * swShQ) : 0.0f) + (1.0f - fabsf(sn.z)) * 0.12f; glColor3f(c, c, c); }
                    const Vec3& v1 = (*renderPositions)[i1];
                    glVertex3f(v1.x, v1.y, v1.z);
                    if (triState.tex != 0 && mesh->hasUv && i2 < mesh->uvs.size()) { float u = mesh->uvs[i2].x; float v = 1.0f - mesh->uvs[i2].y; float uvMul = powf(2.0f, -triState.uvScale); u *= uvMul * triState.uvScaleU; v *= uvMul * triState.uvScaleV; if (gPreviewSaturnSampling) { u *= triState.satU; v *= triState.satV; } if (triState.flipU) u = 1.0f - u; if (triState.flipV) v = 1.0f - v; glTexCoord2f(u, v); }
                    if ((triState.shadingUv >= 0 || triLit == 1) && i2 < csNormals.size()) { const Vec3& sn = csNormals[i2]; float d = sn.x * swLx + sn.y * swLy + sn.z * swLz; if (d < 0.0f) d = 0.0f; float c = swAmb + d * swDif - ((sn.y < 0.0f) ? (-sn.y * swShQ) : 0.0f) + (1.0f - fabsf(sn.z)) * 0.12f; glColor3f(c, c, c); }
                    const Vec3& v2 = (*renderPositions)[i2];
                    glVertex3f(v2.x, v2.y, v2.z);
                }
                glEnd();
            }

            // Restore lighting state after mesh rendering
            if (anyLit)
            {
                glDisable(GL_LIGHTING);
                glDisable(GL_LIGHT0);
                glDisable(GL_COLOR_MATERIAL);
                glDisable(GL_NORMALIZE);
                glShadeModel(GL_FLAT);
            }

            // Green wireframe overlay for navmesh-ready triangles inside a NavMesh3D bounds
            if (s.navmeshReady && !selected && !gNavMesh3DNodes.empty())
            {
                // Collect positive (non-negator) bounds AABBs + wireframe style
                struct NavBounds { float minX, minY, minZ, maxX, maxY, maxZ; float r, g, b, thick; };
                struct CullBounds { float minX, minY, minZ, maxX, maxY, maxZ; float threshold; };
                std::vector<NavBounds> posBounds;
                std::vector<CullBounds> cullBounds;
                for (int ni = 0; ni < (int)gNavMesh3DNodes.size(); ++ni)
                {
                    const auto& nm = gNavMesh3DNodes[ni];
                    if (!nm.navBounds) continue;
                    float hx = nm.scaleX * nm.extentX * 0.5f;
                    float hy = nm.scaleY * nm.extentY * 0.5f;
                    float hz = nm.scaleZ * nm.extentZ * 0.5f;
                    if (!nm.navNegator)
                        posBounds.push_back({ nm.x - hx, nm.y - hy, nm.z - hz, nm.x + hx, nm.y + hy, nm.z + hz, nm.wireR, nm.wireG, nm.wireB, nm.wireThickness });
                    if (nm.cullWalls)
                        cullBounds.push_back({ nm.x - hx, nm.y - hy, nm.z - hz, nm.x + hx, nm.y + hy, nm.z + hz, nm.wallCullThreshold });
                }

                if (!posBounds.empty())
                {
                    // Use color/thickness from first positive bounds
                    float overlayR = posBounds[0].r, overlayG = posBounds[0].g, overlayB = posBounds[0].b;
                    float overlayThick = posBounds[0].thick;

                    bool anyDrawn = false;
                    for (size_t idx = 0; idx + 2 < mesh->indices.size(); idx += 3)
                    {
                        uint16_t i0 = mesh->indices[idx + 0];
                        uint16_t i1 = mesh->indices[idx + 1];
                        uint16_t i2 = mesh->indices[idx + 2];
                        if (i0 >= renderPositions->size() || i1 >= renderPositions->size() || i2 >= renderPositions->size())
                            continue;
                        const Vec3& lv0 = (*renderPositions)[i0];
                        const Vec3& lv1 = (*renderPositions)[i1];
                        const Vec3& lv2 = (*renderPositions)[i2];

                        // World-space positions (scale + translate)
                        float wp[3][3] = {
                            { wx + lv0.x * wsx, wy + lv0.y * wsy, wz + lv0.z * wsz },
                            { wx + lv1.x * wsx, wy + lv1.y * wsy, wz + lv1.z * wsz },
                            { wx + lv2.x * wsx, wy + lv2.y * wsy, wz + lv2.z * wsz }
                        };

                        bool triInside = false;
                        for (int vi = 0; vi < 3 && !triInside; ++vi)
                        {
                            for (int bi = 0; bi < (int)posBounds.size() && !triInside; ++bi)
                            {
                                const auto& b = posBounds[bi];
                                if (wp[vi][0] >= b.minX && wp[vi][0] <= b.maxX &&
                                    wp[vi][1] >= b.minY && wp[vi][1] <= b.maxY &&
                                    wp[vi][2] >= b.minZ && wp[vi][2] <= b.maxZ)
                                {
                                    triInside = true;
                                    overlayR = b.r; overlayG = b.g; overlayB = b.b;
                                    overlayThick = b.thick;
                                }
                            }
                        }

                        // Wall cull check: skip near-vertical faces inside cull-walls bounds
                        if (triInside && !cullBounds.empty())
                        {
                            float cx = (wp[0][0] + wp[1][0] + wp[2][0]) / 3.0f;
                            float cy = (wp[0][1] + wp[1][1] + wp[2][1]) / 3.0f;
                            float cz = (wp[0][2] + wp[1][2] + wp[2][2]) / 3.0f;
                            for (int ci = 0; ci < (int)cullBounds.size(); ++ci)
                            {
                                const auto& cb = cullBounds[ci];
                                if (cx >= cb.minX && cx <= cb.maxX &&
                                    cy >= cb.minY && cy <= cb.maxY &&
                                    cz >= cb.minZ && cz <= cb.maxZ)
                                {
                                    // Compute face normal from world-space triangle
                                    float e1x = wp[1][0] - wp[0][0], e1y = wp[1][1] - wp[0][1], e1z = wp[1][2] - wp[0][2];
                                    float e2x = wp[2][0] - wp[0][0], e2y = wp[2][1] - wp[0][1], e2z = wp[2][2] - wp[0][2];
                                    float nx = e1y * e2z - e1z * e2y;
                                    float ny = e1z * e2x - e1x * e2z;
                                    float nz = e1x * e2y - e1y * e2x;
                                    float nlen = sqrtf(nx * nx + ny * ny + nz * nz);
                                    if (nlen > 1e-8f)
                                    {
                                        ny /= nlen;
                                        // Cull near-vertical faces (wall-like): normal.y close to 0
                                        if (fabsf(ny) < cb.threshold)
                                            triInside = false;
                                    }
                                    break;
                                }
                            }
                        }

                        if (triInside)
                        {
                            if (!anyDrawn)
                            {
                                glDisable(GL_TEXTURE_2D);
                                glDisable(GL_LIGHTING);
                                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                                glEnable(GL_POLYGON_OFFSET_LINE);
                                glPolygonOffset(-1.0f, -1.0f);
                                glLineWidth(overlayThick);
                                glColor3f(overlayR, overlayG, overlayB);
                                glBegin(GL_TRIANGLES);
                                anyDrawn = true;
                            }
                            glVertex3f(lv0.x, lv0.y, lv0.z);
                            glVertex3f(lv1.x, lv1.y, lv1.z);
                            glVertex3f(lv2.x, lv2.y, lv2.z);
                        }
                    }
                    if (anyDrawn)
                    {
                        glEnd();
                        glDisable(GL_POLYGON_OFFSET_LINE);
                        glPolygonOffset(0.0f, 0.0f);
                        glLineWidth(1.0f);
                        glPolygonMode(GL_FRONT_AND_BACK, gWireframePreview ? GL_LINE : GL_FILL);
                    }
                }
            }

            // Selected checker overlay: fullscreen texture masked by selected mesh shape
            if (selected && gCheckerOverlayTex != 0)
            {
                const float checkerPx = 8.0f;

                // 1) Write selected mesh silhouette into stencil (respect depth)
                glEnable(GL_STENCIL_TEST);
                glDisable(GL_SCISSOR_TEST);
                glClearStencil(0);
                glClear(GL_STENCIL_BUFFER_BIT);
                glStencilMask(0xFF);
                glStencilFunc(GL_ALWAYS, 1, 0xFF);
                glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                glDepthMask(GL_FALSE);
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(-1.0f, -1.0f);
                glDepthFunc(GL_LEQUAL);
                glDisable(GL_TEXTURE_2D);

                glBegin(GL_TRIANGLES);
                for (size_t idx = 0; idx + 2 < mesh->indices.size(); idx += 3)
                {
                    uint16_t i0 = mesh->indices[idx + 0];
                    uint16_t i1 = mesh->indices[idx + 1];
                    uint16_t i2 = mesh->indices[idx + 2];
                    if (i0 >= renderPositions->size() || i1 >= renderPositions->size() || i2 >= renderPositions->size())
                        continue;
                    const Vec3& v0 = (*renderPositions)[i0];
                    const Vec3& v1 = (*renderPositions)[i1];
                    const Vec3& v2 = (*renderPositions)[i2];
                    glVertex3f(v0.x, v0.y, v0.z);
                    glVertex3f(v1.x, v1.y, v1.z);
                    glVertex3f(v2.x, v2.y, v2.z);
                }
                glEnd();

                // 2) Draw fullscreen checker only where stencil matches
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glDepthMask(GL_TRUE);
                glDepthFunc(GL_LESS);
                glDisable(GL_POLYGON_OFFSET_FILL);

                glStencilMask(0x00);
                glStencilFunc(GL_EQUAL, 1, 0xFF);
                glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

                GLint vp[4] = { 0, 0, 1, 1 };
                glGetIntegerv(GL_VIEWPORT, vp);
                const float uMax = (float)vp[2] / (checkerPx * 8.0f);
                const float vMax = (float)vp[3] / (checkerPx * 8.0f);

                glDisable(GL_DEPTH_TEST);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, gCheckerOverlayTex);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

                glMatrixMode(GL_PROJECTION);
                glPushMatrix();
                glLoadIdentity();
                glMatrixMode(GL_MODELVIEW);
                glPushMatrix();
                glLoadIdentity();

                glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
                glTexCoord2f(uMax, 0.0f); glVertex3f( 1.0f, -1.0f, 0.0f);
                glTexCoord2f(uMax, vMax); glVertex3f( 1.0f,  1.0f, 0.0f);
                glTexCoord2f(0.0f, vMax); glVertex3f(-1.0f,  1.0f, 0.0f);
                glEnd();

                glPopMatrix();
                glMatrixMode(GL_PROJECTION);
                glPopMatrix();
                glMatrixMode(GL_MODELVIEW);

                glDisable(GL_BLEND);
                glEnable(GL_DEPTH_TEST);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                glStencilMask(0xFF);
                glStencilFunc(GL_ALWAYS, 0, 0xFF);
                glDisable(GL_STENCIL_TEST);
            }

            glPopMatrix();

            glDisable(GL_TEXTURE_2D);
        }
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        // Clean up all GL state that mesh rendering may have enabled so it
        // never leaks into the next frame's gradient or ImGui pass.
        glDisable(GL_LIGHTING);
        glDisable(GL_LIGHT0);
        glDisable(GL_COLOR_MATERIAL);
        glDisable(GL_NORMALIZE);
        glDisable(GL_TEXTURE_2D);
        glShadeModel(GL_SMOOTH);

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
                ImGui::Text("dXY: %.2f %.2f", gDbgDx, gDbgDy);
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

        if (ImGui::IsMouseClicked(0) && mouseInViewport)
        {
            glfwFocusWindow(window);
            float bestDist = 1e9f; // px
            int bestAudioIndex = -1;
            int bestStaticIndex = -1;
            int bestCameraIndex = -1;
            int bestNode3DIndex = -1;
            int bestNavMeshIndex = -1;

            auto pickNearest = [&](float sx, float sy) -> float {
                float dx = io.MousePos.x - sx;
                float dy = io.MousePos.y - sy;
                return sqrtf(dx*dx + dy*dy);
            };
            auto clearBest = [&]() { bestAudioIndex = -1; bestStaticIndex = -1; bestCameraIndex = -1; bestNode3DIndex = -1; bestNavMeshIndex = -1; };

            for (int i = 0; i < (int)gAudio3DNodes.size(); ++i)
            {
                float sx, sy;
                if (ProjectToScreenGL({ gAudio3DNodes[i].x, gAudio3DNodes[i].y, gAudio3DNodes[i].z }, sx, sy, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y))
                {
                    float d = pickNearest(sx, sy);
                    if (d < bestDist) { bestDist = d; clearBest(); bestAudioIndex = i; }
                }
            }
            // StaticMesh3D: test mouse against projected triangles for accurate picking.
            // Falls back to origin-point distance if mesh data is unavailable.
            for (int i = 0; i < (int)gStaticMeshNodes.size(); ++i)
            {
                const auto& s = gStaticMeshNodes[i];
                bool hitMesh = false;

                // Try triangle-based picking if mesh geometry is loaded
                if (!s.mesh.empty() && !gProjectDir.empty())
                {
                    std::filesystem::path meshPath = std::filesystem::path(gProjectDir) / s.mesh;
                    const NebMesh* mesh = GetNebMesh(meshPath);
                    if (mesh && mesh->valid && !mesh->positions.empty() && mesh->indices.size() >= 3)
                    {
                        float wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz;
                        GetStaticMeshWorldTRS(i, wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz);

                        // Build rotation matrix (same axis remap as rendering)
                        int pickParentN3D = (!s.parent.empty()) ? FindNode3DByName(s.parent) : -1;
                        bool parentedNode3D = (pickParentN3D >= 0);
                        // Parent quaternion rotation matrix (3x3, row-major) for Node3D parents
                        float pr00=1,pr01=0,pr02=0, pr10=0,pr11=1,pr12=0, pr20=0,pr21=0,pr22=1;
                        // Euler trig for standalone meshes
                        float cx1=1,sx1=0,cy1=1,sy1=0,cz1=1,sz1=0;
                        if (parentedNode3D)
                        {
                            float pqw2, pqx2, pqy2, pqz2, _px2, _py2, _pz2, _psx2, _psy2, _psz2;
                            GetNode3DWorldTRSQuat(pickParentN3D, _px2, _py2, _pz2, pqw2, pqx2, pqy2, pqz2, _psx2, _psy2, _psz2);
                            float xx2=pqx2*pqx2, yy2=pqy2*pqy2, zz2=pqz2*pqz2;
                            float xy2=pqx2*pqy2, xz2=pqx2*pqz2, yz2=pqy2*pqz2;
                            float wx2=pqw2*pqx2, wy2=pqw2*pqy2, wz2=pqw2*pqz2;
                            pr00=1-2*(yy2+zz2); pr01=2*(xy2-wz2); pr02=2*(xz2+wy2);
                            pr10=2*(xy2+wz2); pr11=1-2*(xx2+zz2); pr12=2*(yz2-wx2);
                            pr20=2*(xz2-wy2); pr21=2*(yz2+wx2); pr22=1-2*(xx2+yy2);
                        }
                        else
                        {
                            float arx = wrz, ary = wrx, arz = wry;
                            float rrx = arx * 3.14159f / 180.0f, rry = ary * 3.14159f / 180.0f, rrz = arz * 3.14159f / 180.0f;
                            cx1 = cosf(rrx); sx1 = sinf(rrx); cy1 = cosf(rry); sy1 = sinf(rry); cz1 = cosf(rrz); sz1 = sinf(rrz);
                        }

                        // Child local rotation for parented meshes
                        float ccx = 1, csx2 = 0, ccy = 1, csy2 = 0, ccz = 1, csz2 = 0;
                        if (parentedNode3D)
                        {
                            float crx = s.rotX * 3.14159f / 180.0f, cry = s.rotY * 3.14159f / 180.0f, crz = s.rotZ * 3.14159f / 180.0f;
                            ccx = cosf(crx); csx2 = sinf(crx); ccy = cosf(cry); csy2 = sinf(cry); ccz = cosf(crz); csz2 = sinf(crz);
                        }

                        float scaleX = io.DisplayFramebufferScale.x, scaleY = io.DisplayFramebufferScale.y;
                        float mx = io.MousePos.x, my = io.MousePos.y;

                        for (size_t idx = 0; idx + 2 < mesh->indices.size(); idx += 3)
                        {
                            float svx[3], svy[3];
                            bool allOk = true;
                            for (int vi = 0; vi < 3; ++vi)
                            {
                                uint16_t ii = mesh->indices[idx + vi];
                                if (ii >= mesh->positions.size()) { allOk = false; break; }
                                Vec3 v = mesh->positions[ii];
                                v.x *= wsx; v.y *= wsy; v.z *= wsz;

                                if (parentedNode3D)
                                {
                                    // Child rotation first
                                    float t;
                                    t = v.x*ccz - v.y*csz2; v.y = v.x*csz2 + v.y*ccz; v.x = t;
                                    t = v.x*ccy + v.z*csy2; v.z = -v.x*csy2 + v.z*ccy; v.x = t;
                                    t = v.y*ccx - v.z*csx2; v.z = v.y*csx2 + v.z*ccx; v.y = t;
                                    // Parent rotation via quaternion matrix
                                    float vx2 = pr00*v.x + pr01*v.y + pr02*v.z;
                                    float vy2 = pr10*v.x + pr11*v.y + pr12*v.z;
                                    float vz2 = pr20*v.x + pr21*v.y + pr22*v.z;
                                    v.x = vx2; v.y = vy2; v.z = vz2;
                                }
                                else
                                {
                                    // Standalone rotation via Euler
                                    float t;
                                    t = v.x*cz1 - v.y*sz1; v.y = v.x*sz1 + v.y*cz1; v.x = t;
                                    t = v.x*cy1 + v.z*sy1; v.z = -v.x*sy1 + v.z*cy1; v.x = t;
                                    t = v.y*cx1 - v.z*sx1; v.z = v.y*sx1 + v.z*cx1; v.y = t;
                                }

                                v.x += wx; v.y += wy; v.z += wz;

                                if (!ProjectToScreenGL(v, svx[vi], svy[vi], scaleX, scaleY))
                                { allOk = false; break; }
                            }
                            if (!allOk) continue;

                            // Point-in-triangle test (2D barycentric)
                            float d0x = svx[1] - svx[0], d0y = svy[1] - svy[0];
                            float d1x = svx[2] - svx[0], d1y = svy[2] - svy[0];
                            float d2x = mx - svx[0], d2y = my - svy[0];
                            float dot00 = d0x*d0x + d0y*d0y;
                            float dot01 = d0x*d1x + d0y*d1y;
                            float dot02 = d0x*d2x + d0y*d2y;
                            float dot11 = d1x*d1x + d1y*d1y;
                            float dot12 = d1x*d2x + d1y*d2y;
                            float invDenom = dot00*dot11 - dot01*dot01;
                            if (fabsf(invDenom) < 1e-10f) continue;
                            invDenom = 1.0f / invDenom;
                            float u = (dot11*dot02 - dot01*dot12) * invDenom;
                            float v2 = (dot00*dot12 - dot01*dot02) * invDenom;
                            if (u >= 0.0f && v2 >= 0.0f && (u + v2) <= 1.0f)
                            {
                                hitMesh = true;
                                bestDist = 0.0f;
                                clearBest();
                                bestStaticIndex = i;
                                break;
                            }
                        }
                    }
                }

                // Fallback to origin-point distance if no triangle hit
                if (!hitMesh)
                {
                    float sx, sy;
                    if (ProjectToScreenGL({ s.x, s.y, s.z }, sx, sy, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y))
                    {
                        float d = pickNearest(sx, sy);
                        if (d < bestDist) { bestDist = d; clearBest(); bestStaticIndex = i; }
                    }
                }
            }
            for (int i = 0; i < (int)gCamera3DNodes.size(); ++i)
            {
                float sx, sy;
                if (ProjectToScreenGL({ gCamera3DNodes[i].x, gCamera3DNodes[i].y, gCamera3DNodes[i].z }, sx, sy, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y))
                {
                    float d = pickNearest(sx, sy);
                    if (d < bestDist) { bestDist = d; clearBest(); bestCameraIndex = i; }
                }
            }
            for (int i = 0; i < (int)gNode3DNodes.size(); ++i)
            {
                float sx, sy;
                if (ProjectToScreenGL({ gNode3DNodes[i].x, gNode3DNodes[i].y, gNode3DNodes[i].z }, sx, sy, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y))
                {
                    float d = pickNearest(sx, sy);
                    if (d < bestDist) { bestDist = d; clearBest(); bestNode3DIndex = i; }
                }
            }
            for (int i = 0; i < (int)gNavMesh3DNodes.size(); ++i)
            {
                float sx, sy;
                if (ProjectToScreenGL({ gNavMesh3DNodes[i].x, gNavMesh3DNodes[i].y, gNavMesh3DNodes[i].z }, sx, sy, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y))
                {
                    float d = pickNearest(sx, sy);
                    if (d < bestDist) { bestDist = d; clearBest(); bestNavMeshIndex = i; }
                }
            }

            auto deselectAll = [&]() {
                gSelectedAudio3D = -1; gSelectedStaticMesh = -1;
                gSelectedCamera3D = -1; gSelectedNode3D = -1; gSelectedNavMesh3D = -1;
            };

            if (gTransformMode == Transform_None)
            {
                int prevAudioSel = gSelectedAudio3D;
                int prevStaticSel = gSelectedStaticMesh;
                int prevCameraSel = gSelectedCamera3D;
                int prevNode3DSel = gSelectedNode3D;
                int prevNavMeshSel = gSelectedNavMesh3D;
                if (bestDist < 80.0f)
                {
                    gSelectedAudio3D = bestAudioIndex;
                    gSelectedStaticMesh = bestStaticIndex;
                    gSelectedCamera3D = bestCameraIndex;
                    gSelectedNode3D = bestNode3DIndex;
                    gSelectedNavMesh3D = bestNavMeshIndex;
                }
                else
                {
                    deselectAll();
                }

                if (gSelectedAudio3D != prevAudioSel || gSelectedStaticMesh != prevStaticSel ||
                    gSelectedCamera3D != prevCameraSel || gSelectedNode3D != prevNode3DSel ||
                    gSelectedNavMesh3D != prevNavMeshSel)
                {
                    gTransforming = false;
                    gTransformMode = Transform_None;
                    gAxisLock = 0;
                    gLastTransformMouseX = io.MousePos.x;
                    gLastTransformMouseY = io.MousePos.y;
                }
            }
            else if (gTransformMode == Transform_Rotate)
            {
                if (bestDist >= 80.0f)
                {
                    deselectAll();
                    gTransformMode = Transform_None;
                    gAxisLock = 0;
                    gHasRotatePreview = false;
                }
            }
            else
            {
                if (bestDist >= 80.0f)
                {
                    deselectAll();
                    gTransformMode = Transform_None;
                    gAxisLock = 0;
                }
            }
        }
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

