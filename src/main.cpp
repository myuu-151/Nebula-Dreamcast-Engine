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
#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <iterator>
#include <array>

#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>
#include <ShObjIdl.h>
#include <wincodec.h>

#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "audio3d.h"
#include "editor/math_types.h"
#include "editor/camera3d_v2.h"
#include "camera/Camera3D.h"
#include "assets/meta_io.h"
#include "platform/dreamcast/build_helpers.h"
#include "scene/SceneIO.h"
#include "scene/NodeTypes.h"
#include <GL/gl.h>

#ifndef GL_POINT_SPRITE
#define GL_POINT_SPRITE 0x8861
#endif
#ifndef GL_COORD_REPLACE
#define GL_COORD_REPLACE 0x8862
#endif
#include <GLFW/glfw3.h>

static void WriteU16BE(std::ofstream& out, uint16_t v);
static bool ExportNebTexturePNG(const std::filesystem::path& pngPath, const std::filesystem::path& outPath, std::string& warning);

static std::filesystem::path GetExecutableDirectory()
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

// Camera basis helpers moved to src/camera/Camera3D.

static std::string gProjectDir;
static std::string gProjectFile;
static std::vector<std::string> gRecentProjects;

// User-overridable tool paths (Preferences)
static std::string gPrefDreamSdkHome = "C:\\DreamSDK";
static std::string gPrefVcvarsPath;

struct UndoAction
{
    std::string label;
    std::function<void()> undo;
    std::function<void()> redo;
};

static std::vector<UndoAction> gUndoStack;
static std::vector<UndoAction> gRedoStack;

static void PushUndo(const UndoAction& action)
{
    gUndoStack.push_back(action);
    gRedoStack.clear();
}

static void EndTransformSnapshot();
static void DoUndo();
static void DoRedo();

enum TransformMode
{
    Transform_None = 0,
    Transform_Grab,
    Transform_Rotate,
    Transform_Scale
};

static int gSelectedAudio3D = -1;
static int gSelectedStaticMesh = -1;
static int gSelectedCamera3D = -1;
static int gSelectedNode3D = -1;
static int gInspectorPinnedAudio3D = -1;
static int gInspectorPinnedStaticMesh = -1;
static int gInspectorPinnedCamera3D = -1;
static int gInspectorPinnedNode3D = -1;
static TransformMode gTransformMode = Transform_None;
static char gAxisLock = 0; // 'X','Y','Z'
static bool gTransforming = false;
static double gLastTransformMouseX = 0.0;
static double gLastTransformMouseY = 0.0;

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
static int gInspectorSel = -1;
static char gInspectorName[256] = {};

static bool gHasCopiedNode = false;
static Audio3DNode gCopiedNode;
static bool gHasCopiedStatic = false;
static StaticMesh3DNode gCopiedStatic;

static int gNodeRenameIndex = -1;
static bool gNodeRenameStatic = false;
static bool gNodeRenameCamera = false;
static bool gNodeRenameNode3D = false;
static char gNodeRenameBuffer[256] = {};
static bool gNodeRenameOpen = false;

static std::string gViewportToast;
static double gViewportToastUntil = 0.0;
static bool gShowVmuTool = false;
static std::unordered_set<std::string> gCollapsedAudioRoots;
static std::unordered_set<std::string> gCollapsedStaticRoots;
static std::unordered_set<std::string> gCollapsedCameraRoots;
static std::unordered_set<std::string> gCollapsedNode3DRoots;
static bool gVmuHasImage = false;
static bool gVmuLoadOnBoot = false;
static std::string gVmuAssetPath;
struct VmuAnimLayer { std::string name; bool visible = true; int frameStart = 0; int frameEnd = 0; std::string linkedAsset; };
static std::vector<VmuAnimLayer> gVmuAnimLayers = { {"Layer 1", true, 0, 0} };
static int gVmuAnimLayerSel = 0;
static int gVmuAnimTotalFrames = 24;
static int gVmuAnimPlayhead = 0;
static bool gVmuAnimPlaying = false;
static bool gVmuAnimLoop = false;
static int gVmuAnimSpeedMode = 1; // 0=slow, 1=normal, 2=fast
static double gVmuAnimAccum = 0.0;
static bool gVmuDrawMode = false;
static int gVmuCurrentLoadedType = 0; // 0=none,1=png,2=vmuanim
static std::string gVmuLinkedPngPath;
static std::string gVmuLinkedAnimPath;
static bool gVmuStrokeActive = false;
static int gVmuLastDrawX = -1;
static int gVmuLastDrawY = -1;
static std::vector<std::array<uint8_t, 48 * 32>> gVmuUndoStack;
static std::array<uint8_t, 48 * 32> gVmuMono = {};

static bool gHideUnselectedWireframes = false;

static std::vector<SceneData> gOpenScenes;
static int gActiveScene = -1;
static int gForceSelectSceneTab = -1;
static bool gPlayMode = false;
static bool gRequestDreamcastGenerate = false;
static bool gEnableScriptHotReload = false;
static std::unordered_map<std::string, std::filesystem::file_time_type> gScriptHotReloadKnownMtimes;
static std::string gScriptHotReloadTrackedProjectDir;
static double gScriptHotReloadNextPollAt = 0.0;
static unsigned long long gScriptHotReloadGeneration = 0;

static HMODULE gEditorScriptModule = nullptr;
static std::string gEditorScriptPath;
static bool gEditorScriptActive = false;
static bool gEditorScriptStarted = false;
static double gEditorScriptNextTickLog = 0.0;
// Script-off baseline: keep play controls engine-owned unless explicitly re-enabled.
static bool useScriptController = true;
// Keep script-owned controls on desktop, but keep C++ fallback enabled on Dreamcast
// until full runtime script parity is confirmed.
static bool gEnableCppPlayFallbackControls = false;
using EditorScriptStartFn = void(*)(void);
using EditorScriptUpdateFn = void(*)(float);
using EditorScriptSceneSwitchFn = void(*)(const char*);
static EditorScriptStartFn gEditorScriptOnStart = nullptr;
static EditorScriptUpdateFn gEditorScriptOnUpdate = nullptr;
static EditorScriptSceneSwitchFn gEditorScriptOnSceneSwitch = nullptr;

// Optional editor runtime hook. v1 only signals "scripts changed"; no compile/rebind yet.
using ScriptHotReloadCallback = void(*)(const std::vector<std::filesystem::path>& changedFiles, bool manualTrigger);
static ScriptHotReloadCallback gOnScriptHotReloadEvent = nullptr;

static bool gWireframePreview = false;
static bool gSaveAllInProgress = false;
static std::vector<StaticMesh3DNode> gStaticMeshNodes;
static std::vector<Camera3DNode> gCamera3DNodes;
static std::vector<Node3DNode> gNode3DNodes;

static bool gImportPopupOpen = false;
static std::string gImportPath;
static Assimp::Importer gImportAssimp;
static const aiScene* gImportScene = nullptr;
static std::vector<bool> gImportAnimConvert;
static std::string gImportWarning;
static bool gImportDeltaCompress = false;
static int gImportBasisMode = 1; // 0=None(raw), 1=Blender(-Z Forward, Y Up), 2=Maya(+Z Forward, Y Up)
static std::vector<std::string> gPendingDroppedImports;

static bool gHasTransformSnapshot = false;
static bool gTransformIsStatic = false;
static bool gTransformIsNode3D = false;
static int gTransformIndex = -1;
static Audio3DNode gTransformBefore;
static StaticMesh3DNode gTransformBeforeStatic;
static Node3DNode gTransformBeforeNode3D;

static bool gHasRotatePreview = false;
static int gRotatePreviewIndex = -1;
static float gRotatePreviewX = 0.0f;
static float gRotatePreviewY = 0.0f;
static float gRotatePreviewZ = 0.0f;
static float gRotateStartX = 0.0f;
static float gRotateStartY = 0.0f;
static float gRotateStartZ = 0.0f;
static float gRotateStartMouseX = 0.0f;
static float gRotateStartMouseY = 0.0f;

static bool TransformChanged(const Audio3DNode& a, const Audio3DNode& b)
{
    return a.x != b.x || a.y != b.y || a.z != b.z ||
           a.rotX != b.rotX || a.rotY != b.rotY || a.rotZ != b.rotZ ||
           a.scaleX != b.scaleX || a.scaleY != b.scaleY || a.scaleZ != b.scaleZ;
}

static bool TransformChanged(const StaticMesh3DNode& a, const StaticMesh3DNode& b)
{
    return a.x != b.x || a.y != b.y || a.z != b.z ||
           a.rotX != b.rotX || a.rotY != b.rotY || a.rotZ != b.rotZ ||
           a.scaleX != b.scaleX || a.scaleY != b.scaleY || a.scaleZ != b.scaleZ;
}

static bool TransformChanged(const Node3DNode& a, const Node3DNode& b)
{
    return a.x != b.x || a.y != b.y || a.z != b.z ||
           a.rotX != b.rotX || a.rotY != b.rotY || a.rotZ != b.rotZ ||
           a.scaleX != b.scaleX || a.scaleY != b.scaleY || a.scaleZ != b.scaleZ;
}

static int FindStaticMeshByName(const std::string& name)
{
    return NebulaNodes::FindStaticMeshByName(gStaticMeshNodes, name);
}

static int FindNode3DByName(const std::string& name)
{
    return NebulaNodes::FindNode3DByName(gNode3DNodes, name);
}

static int FindCamera3DByName(const std::string& name)
{
    if (name.empty()) return -1;
    for (int i = 0; i < (int)gCamera3DNodes.size(); ++i)
    {
        if (gCamera3DNodes[i].name == name)
            return i;
    }
    return -1;
}

static bool TryGetParentByNodeName(const std::string& name, std::string& outParent)
{
    return NebulaNodes::TryGetParentByNodeName(gAudio3DNodes, gStaticMeshNodes, gCamera3DNodes, gNode3DNodes, name, outParent);
}

static bool WouldCreateHierarchyCycle(const std::string& childName, const std::string& candidateParentName)
{
    return NebulaNodes::WouldCreateHierarchyCycle(gAudio3DNodes, gStaticMeshNodes, gCamera3DNodes, gNode3DNodes, childName, candidateParentName);
}

static bool StaticMeshCreatesCycle(int childIdx, int candidateParentIdx)
{
    return NebulaNodes::StaticMeshCreatesCycle(gStaticMeshNodes, childIdx, candidateParentIdx);
}

static bool Node3DCreatesCycle(int childIdx, int candidateParentIdx)
{
    return NebulaNodes::Node3DCreatesCycle(gNode3DNodes, childIdx, candidateParentIdx);
}

static bool BuildScriptFileMtimeSnapshot(const std::filesystem::path& scriptsDir, std::unordered_map<std::string, std::filesystem::file_time_type>& outSnapshot)
{
    outSnapshot.clear();
    std::error_code ec;
    if (!std::filesystem::exists(scriptsDir, ec) || !std::filesystem::is_directory(scriptsDir, ec))
        return false;

    for (std::filesystem::recursive_directory_iterator it(scriptsDir, ec), end; !ec && it != end; it.increment(ec))
    {
        if (ec || !it->is_regular_file(ec))
            continue;

        std::filesystem::path p = it->path();
        if (p.extension() != ".c")
            continue;

        std::error_code relEc;
        std::filesystem::path rel = std::filesystem::relative(p, scriptsDir, relEc);
        std::string key = relEc ? p.generic_string() : rel.generic_string();

        std::error_code timeEc;
        std::filesystem::file_time_type ft = std::filesystem::last_write_time(p, timeEc);
        if (!timeEc)
            outSnapshot[key] = ft;
    }
    return true;
}

static void RunScriptHotReloadV1(const std::vector<std::filesystem::path>& changedFiles, bool manualTrigger)
{
    ++gScriptHotReloadGeneration;

    if (gOnScriptHotReloadEvent)
        gOnScriptHotReloadEvent(changedFiles, manualTrigger);

    std::string mode = manualTrigger ? "manual" : "auto";
    if (manualTrigger && changedFiles.empty())
    {
        gViewportToast = "Script Hot Reload v1: manual refresh complete (no compile/rebind)";
        printf("[ScriptHotReload] v1 %s refresh: state update only (no compile/rebind). generation=%llu\n", mode.c_str(), gScriptHotReloadGeneration);
    }
    else
    {
        gViewportToast = "Script Hot Reload v1: detected " + std::to_string((int)changedFiles.size()) + " .c change(s), state refreshed";
        printf("[ScriptHotReload] v1 %s reload: %zu .c file(s) changed. state update only (no compile/rebind). generation=%llu\n",
               mode.c_str(), changedFiles.size(), gScriptHotReloadGeneration);
        for (const auto& c : changedFiles)
            printf("[ScriptHotReload] changed: %s\n", c.generic_string().c_str());
    }
    gViewportToastUntil = glfwGetTime() + 3.0;
}

static void ForceScriptHotReloadNowV1()
{
    if (!gEnableScriptHotReload)
        return;

    if (gProjectDir.empty())
    {
        gViewportToast = "Script Hot Reload v1: open a project first";
        gViewportToastUntil = glfwGetTime() + 2.0;
        return;
    }

    std::filesystem::path scriptsDir = std::filesystem::path(gProjectDir) / "Scripts";
    std::unordered_map<std::string, std::filesystem::file_time_type> snapshot;
    if (!BuildScriptFileMtimeSnapshot(scriptsDir, snapshot))
    {
        gScriptHotReloadKnownMtimes.clear();
        gViewportToast = "Script Hot Reload v1: Scripts folder not found";
        gViewportToastUntil = glfwGetTime() + 2.5;
        printf("[ScriptHotReload] v1 manual refresh skipped: missing folder %s\n", scriptsDir.string().c_str());
        return;
    }

    std::vector<std::filesystem::path> changed;
    changed.reserve(snapshot.size());
    for (const auto& kv : snapshot)
    {
        auto it = gScriptHotReloadKnownMtimes.find(kv.first);
        if (it == gScriptHotReloadKnownMtimes.end() || it->second != kv.second)
            changed.push_back(std::filesystem::path(kv.first));
    }
    for (const auto& kv : gScriptHotReloadKnownMtimes)
    {
        if (snapshot.find(kv.first) == snapshot.end())
            changed.push_back(std::filesystem::path(kv.first));
    }

    gScriptHotReloadKnownMtimes = snapshot;
    RunScriptHotReloadV1(changed, true);
}

static void PollScriptHotReloadV1(double now)
{
    if (!gEnableScriptHotReload || gProjectDir.empty())
        return;

    if (gScriptHotReloadTrackedProjectDir != gProjectDir)
    {
        gScriptHotReloadTrackedProjectDir = gProjectDir;
        gScriptHotReloadKnownMtimes.clear();
        gScriptHotReloadNextPollAt = 0.0;
    }

    if (now < gScriptHotReloadNextPollAt)
        return;
    gScriptHotReloadNextPollAt = now + 0.75;

    std::filesystem::path scriptsDir = std::filesystem::path(gProjectDir) / "Scripts";
    std::unordered_map<std::string, std::filesystem::file_time_type> snapshot;
    if (!BuildScriptFileMtimeSnapshot(scriptsDir, snapshot))
    {
        gScriptHotReloadKnownMtimes.clear();
        return;
    }

    if (gScriptHotReloadKnownMtimes.empty())
    {
        gScriptHotReloadKnownMtimes = snapshot;
        return;
    }

    std::vector<std::filesystem::path> changed;
    for (const auto& kv : snapshot)
    {
        auto it = gScriptHotReloadKnownMtimes.find(kv.first);
        if (it == gScriptHotReloadKnownMtimes.end() || it->second != kv.second)
            changed.push_back(std::filesystem::path(kv.first));
    }
    for (const auto& kv : gScriptHotReloadKnownMtimes)
    {
        if (snapshot.find(kv.first) == snapshot.end())
            changed.push_back(std::filesystem::path(kv.first));
    }

    if (!changed.empty())
    {
        gScriptHotReloadKnownMtimes = snapshot;
        RunScriptHotReloadV1(changed, false);
    }
}

static std::filesystem::path ResolveGameplayScriptPath()
{
    if (gProjectDir.empty())
        return {};

    printf("[ScriptRuntime] ProjectDir=%s\n", gProjectDir.c_str());

    auto resolvePath = [](const std::string& rel) -> std::filesystem::path
    {
        if (rel.empty()) return {};
        std::filesystem::path p(rel);
        if (p.is_absolute())
            return p;
        return std::filesystem::path(gProjectDir) / p;
    };

    if (gSelectedNode3D >= 0 && gSelectedNode3D < (int)gNode3DNodes.size())
    {
        std::filesystem::path p = resolvePath(gNode3DNodes[gSelectedNode3D].script);
        if (!p.empty() && std::filesystem::exists(p))
            return p;
    }

    for (const auto& n : gNode3DNodes)
    {
        std::filesystem::path p = resolvePath(n.script);
        if (!p.empty() && std::filesystem::exists(p))
            return p;
    }

    std::filesystem::path fallback = std::filesystem::path(gProjectDir) / "Scripts" / "WASD_Node3D_Nav.c";
    if (std::filesystem::exists(fallback))
    {
        printf("[ScriptRuntime] ResolvedScript=%s\n", fallback.string().c_str());
        return fallback;
    }

    return {};
}

static bool WriteEditorScriptBridgeFile(const std::filesystem::path& path)
{
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open())
        return false;

    out << "#include <Windows.h>\n";
    out << "static FARPROC nb_get(const char* name){ HMODULE exe = GetModuleHandleA(NULL); return exe ? GetProcAddress(exe, name) : NULL; }\n";
    out << "void NB_RT_GetNode3DPosition(const char* name, float outPos[3]){ typedef void(*Fn)(const char*, float*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_GetNode3DPosition\"); if(fn) fn(name,outPos); }\n";
    out << "void NB_RT_SetNode3DPosition(const char* name, float x, float y, float z){ typedef void(*Fn)(const char*, float, float, float); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_SetNode3DPosition\"); if(fn) fn(name,x,y,z); }\n";
    out << "void NB_RT_GetNode3DRotation(const char* name, float outRot[3]){ typedef void(*Fn)(const char*, float*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_GetNode3DRotation\"); if(fn) fn(name,outRot); }\n";
    out << "void NB_RT_SetNode3DRotation(const char* name, float x, float y, float z){ typedef void(*Fn)(const char*, float, float, float); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_SetNode3DRotation\"); if(fn) fn(name,x,y,z); }\n";
    out << "void NB_RT_GetCameraWorldForward(const char* name, float outFwd[3]){ typedef void(*Fn)(const char*, float*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_GetCameraWorldForward\"); if(fn) fn(name,outFwd); }\n";
    out << "void NB_RT_GetCameraOrbit(const char* name, float outOrbit[3]){ typedef void(*Fn)(const char*, float*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_GetCameraOrbit\"); if(fn) fn(name,outOrbit); }\n";
    out << "void NB_RT_SetCameraOrbit(const char* name, float x, float y, float z){ typedef void(*Fn)(const char*, float, float, float); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_SetCameraOrbit\"); if(fn) fn(name,x,y,z); }\n";
    out << "void NB_RT_GetCameraRotation(const char* name, float outRot[3]){ typedef void(*Fn)(const char*, float*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_GetCameraRotation\"); if(fn) fn(name,outRot); }\n";
    out << "void NB_RT_SetCameraRotation(const char* name, float x, float y, float z){ typedef void(*Fn)(const char*, float, float, float); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_SetCameraRotation\"); if(fn) fn(name,x,y,z); }\n";
    out << "int NB_RT_IsCameraUnderNode3D(const char* cameraName, const char* nodeName){ typedef int(*Fn)(const char*, const char*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_IsCameraUnderNode3D\"); return fn ? fn(cameraName,nodeName) : 0; }\n";
    return true;
}

static bool CompileEditorScriptDLL(const std::filesystem::path& scriptPath, std::filesystem::path& outDllPath, std::string& outError)
{
    if (scriptPath.empty())
    {
        outError = "no script path";
        return false;
    }

    std::filesystem::path outDir = std::filesystem::path(gProjectDir) / "Intermediate" / "EditorScript";
    std::filesystem::path buildLogPath = outDir / "nb_script_build.log";
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    if (ec)
    {
        outError = "failed to create Intermediate/EditorScript";
        return false;
    }

    printf("[ScriptRuntime] OutDir=%s\n", outDir.string().c_str());
    printf("[ScriptRuntime] BuildLog=%s\n", buildLogPath.string().c_str());

    std::filesystem::path bridgePath = outDir / "nb_editor_bridge.c";
    if (!WriteEditorScriptBridgeFile(bridgePath))
    {
        outError = "failed to write editor bridge file";
        return false;
    }

    outDllPath = outDir / "nb_script.dll";
    std::filesystem::remove(outDllPath, ec);
    std::filesystem::remove(buildLogPath, ec);
    std::filesystem::remove(outDir / "nb_script.exp", ec);
    std::filesystem::remove(outDir / "nb_script.lib", ec);
    std::filesystem::remove(outDir / "nb_script.pdb", ec);

    auto SanitizeCmdPath = [](const std::filesystem::path& p) -> std::string
    {
        std::string s = p.generic_string(); // use forward slashes for cmd robustness
        s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
        s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
        return s;
    };

    std::string dllPathStr = SanitizeCmdPath(outDllPath);
    std::string scriptPathStr = SanitizeCmdPath(scriptPath);
    std::string bridgePathStr = SanitizeCmdPath(bridgePath);
    std::string buildLogPathStr = SanitizeCmdPath(buildLogPath);

    std::string clPrefix = "cl";
    int hasCl = system("cmd /c \"where cl >nul 2>nul\"");
    if (hasCl != 0)
    {
        const std::filesystem::path vcvarsCandidates[] = {
            // VS 2022 layout (most common)
            "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files (x86)/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files (x86)/Microsoft Visual Studio/2022/Professional/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files (x86)/Microsoft Visual Studio/2022/Enterprise/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvarsall.bat",

            // Custom/legacy VS major folder variants seen on some systems
            "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files/Microsoft Visual Studio/18/Enterprise/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files/Microsoft Visual Studio/18/BuildTools/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files (x86)/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files (x86)/Microsoft Visual Studio/18/Professional/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files (x86)/Microsoft Visual Studio/18/Enterprise/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat",
            "C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Auxiliary/Build/vcvars64.bat",
            "C:/Program Files/Microsoft Visual Studio/18/Enterprise/VC/Auxiliary/Build/vcvars64.bat",
            "C:/Program Files/Microsoft Visual Studio/18/BuildTools/VC/Auxiliary/Build/vcvars64.bat",
            "C:/Program Files (x86)/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat",
            "C:/Program Files (x86)/Microsoft Visual Studio/18/Professional/VC/Auxiliary/Build/vcvars64.bat",
            "C:/Program Files (x86)/Microsoft Visual Studio/18/Enterprise/VC/Auxiliary/Build/vcvars64.bat",
            "C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/Auxiliary/Build/vcvars64.bat",

            // VS 2019 fallback
            "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files (x86)/Microsoft Visual Studio/2019/Enterprise/VC/Auxiliary/Build/vcvarsall.bat",
            "C:/Program Files (x86)/Microsoft Visual Studio/2019/BuildTools/VC/Auxiliary/Build/vcvarsall.bat"
        };

        std::filesystem::path vcvarsPath;
        if (!gPrefVcvarsPath.empty())
        {
            std::filesystem::path userVcvars = gPrefVcvarsPath;
            if (std::filesystem::exists(userVcvars))
                vcvarsPath = userVcvars;
        }
        if (vcvarsPath.empty())
        {
            for (const auto& cand : vcvarsCandidates)
            {
                if (std::filesystem::exists(cand))
                {
                    vcvarsPath = cand;
                    break;
                }
            }
        }

        if (vcvarsPath.empty())
        {
            outError = "cl.exe not found in PATH and vcvarsall.bat not found";
            return false;
        }

        std::string vcvarsStr = SanitizeCmdPath(vcvarsPath);
        std::string vcvarsLower = vcvarsStr;
        std::transform(vcvarsLower.begin(), vcvarsLower.end(), vcvarsLower.begin(), [](unsigned char ch){ return (char)std::tolower(ch); });
        bool isVcvarsAll = (vcvarsLower.find("vcvarsall.bat") != std::string::npos);
        clPrefix = "call \"" + vcvarsStr + "\"" + (isVcvarsAll ? " x64" : "") + " && cl";
        printf("[ScriptRuntime] Using VC env fallback: %s\n", vcvarsStr.c_str());
    }

    std::string cmd;
    cmd += "cmd /c \"";
    cmd += clPrefix;
    cmd += " /nologo /LD /TC /O2 /Fe:\"";
    cmd += dllPathStr;
    cmd += "\" \"";
    cmd += scriptPathStr;
    cmd += "\" \"";
    cmd += bridgePathStr;
    cmd += "\" /link user32.lib > \"";
    cmd += buildLogPathStr;
    cmd += "\" 2>&1\"";

    printf("[ScriptRuntime] CompileCmd=%s\n", cmd.c_str());

    int rc = system(cmd.c_str());
    if (rc != 0 || !std::filesystem::exists(outDllPath))
    {
        outError = std::string("compile failed (see ") + buildLogPath.string() + ")";
        return false;
    }

    return true;
}

static void UnloadEditorScriptRuntime()
{
    if (gEditorScriptModule)
    {
        FreeLibrary(gEditorScriptModule);
        gEditorScriptModule = nullptr;
    }
    gEditorScriptOnStart = nullptr;
    gEditorScriptOnUpdate = nullptr;
    gEditorScriptOnSceneSwitch = nullptr;
    gEditorScriptActive = false;
    gEditorScriptStarted = false;
    gEditorScriptPath.clear();
}

static bool LoadEditorScriptRuntime(const std::filesystem::path& scriptPath)
{
    UnloadEditorScriptRuntime();

    std::filesystem::path dllPath;
    std::string err;
    if (!CompileEditorScriptDLL(scriptPath, dllPath, err))
    {
        printf("[ScriptRuntime] compile failed: %s\n", err.c_str());
        gViewportToast = std::string("Script runtime failed: ") + err;
        gViewportToastUntil = glfwGetTime() + 2.5;
        return false;
    }

    gEditorScriptModule = LoadLibraryA(dllPath.string().c_str());
    if (!gEditorScriptModule)
    {
        printf("[ScriptRuntime] LoadLibrary failed for %s\n", dllPath.string().c_str());
        gViewportToast = "Script runtime failed: LoadLibrary";
        gViewportToastUntil = glfwGetTime() + 2.5;
        return false;
    }

    gEditorScriptOnStart = (EditorScriptStartFn)GetProcAddress(gEditorScriptModule, "NB_Game_OnStart");
    gEditorScriptOnUpdate = (EditorScriptUpdateFn)GetProcAddress(gEditorScriptModule, "NB_Game_OnUpdate");
    gEditorScriptOnSceneSwitch = (EditorScriptSceneSwitchFn)GetProcAddress(gEditorScriptModule, "NB_Game_OnSceneSwitch");

    printf("[ScriptRuntime] loaded %s\n", scriptPath.string().c_str());
    printf("[ScriptRuntime] DLL=%s\n", dllPath.string().c_str());
    printf("[ScriptRuntime] symbols: start=%s update=%s scene=%s\n",
        gEditorScriptOnStart ? "yes" : "no",
        gEditorScriptOnUpdate ? "yes" : "no",
        gEditorScriptOnSceneSwitch ? "yes" : "no");

    gEditorScriptActive = true;
    gEditorScriptPath = scriptPath.string();
    gViewportToast = std::string("Script runtime active: ") + scriptPath.filename().string();
    gViewportToastUntil = glfwGetTime() + 2.5;
    return true;
}

static void BeginPlayScriptRuntime()
{
    if (!useScriptController)
    {
        gEditorScriptActive = false;
        gEditorScriptStarted = false;
        printf("[ScriptRuntime] skipped (engine-owned controls mode)\n");
        return;
    }

    std::filesystem::path scriptPath = ResolveGameplayScriptPath();
    if (scriptPath.empty())
    {
        printf("[ScriptRuntime] no gameplay script resolved\n");
        gViewportToast = "Script runtime failed: no script";
        gViewportToastUntil = glfwGetTime() + 2.5;
        return;
    }

    if (!LoadEditorScriptRuntime(scriptPath))
        return;

    gEditorScriptNextTickLog = 0.0;

    if (gEditorScriptOnStart && useScriptController)
    {
        gEditorScriptOnStart();
        gEditorScriptStarted = true;
        printf("[ScriptRuntime] OnStart called\n");
    }
    else
    {
        gEditorScriptStarted = false;
        printf("[ScriptRuntime] OnStart missing or skipped\n");
    }
}

static void EndPlayScriptRuntime()
{
    UnloadEditorScriptRuntime();
}

static void TickPlayScriptRuntime(float dt, double now)
{
    if (!gPlayMode || !gEditorScriptActive || !gEditorScriptOnUpdate || !useScriptController)
        return;

    gEditorScriptOnUpdate(dt);
    if (now >= gEditorScriptNextTickLog)
    {
        gEditorScriptNextTickLog = now + 1.0;
        printf("[ScriptRuntime] OnUpdate tick\n");
    }
}

static void NotifyScriptSceneSwitch()
{
    if (!gPlayMode || !gEditorScriptActive || !gEditorScriptOnSceneSwitch || !useScriptController)
        return;
    if (gActiveScene < 0 || gActiveScene >= (int)gOpenScenes.size())
        return;
    gEditorScriptOnSceneSwitch(gOpenScenes[gActiveScene].name.c_str());
    printf("[ScriptRuntime] OnSceneSwitch: %s\n", gOpenScenes[gActiveScene].name.c_str());
}

static void GetStaticMeshWorldTRS(int idx, float& ox, float& oy, float& oz, float& orx, float& ory, float& orz, float& osx, float& osy, float& osz)
{
    NebulaNodes::GetStaticMeshWorldTRS(gStaticMeshNodes, gNode3DNodes, idx, ox, oy, oz, orx, ory, orz, osx, osy, osz);
}

static void GetNode3DWorldTRS(int idx, float& ox, float& oy, float& oz, float& orx, float& ory, float& orz, float& osx, float& osy, float& osz)
{
    NebulaNodes::GetNode3DWorldTRS(gStaticMeshNodes, gNode3DNodes, idx, ox, oy, oz, orx, ory, orz, osx, osy, osz);
}

static void GetCamera3DWorldTR(int idx, float& ox, float& oy, float& oz, float& orx, float& ory, float& orz)
{
    if (idx < 0 || idx >= (int)gCamera3DNodes.size())
    {
        ox = oy = oz = 0.0f;
        orx = ory = orz = 0.0f;
        return;
    }

    auto rotateOffsetEuler = [](float& x, float& y, float& z, float rxDeg, float ryDeg, float rzDeg)
    {
        const float rx = rxDeg * 3.14159f / 180.0f;
        const float ry = ryDeg * 3.14159f / 180.0f;
        const float rz = rzDeg * 3.14159f / 180.0f;

        const float sx = sinf(rx), cx = cosf(rx);
        const float sy = sinf(ry), cy = cosf(ry);
        const float sz = sinf(rz), cz = cosf(rz);

        float ty = y * cx - z * sx;
        float tz = y * sx + z * cx;
        y = ty; z = tz;

        float tx = x * cy + z * sy;
        tz = -x * sy + z * cy;
        x = tx; z = tz;

        tx = x * cz - y * sz;
        ty = x * sz + y * cz;
        x = tx; y = ty;
    };

    const auto& c = gCamera3DNodes[idx];
    // Base camera local offset. Orbit offset is conditional on having a parent pivot.
    ox = c.x; oy = c.y; oz = c.z;
    if (!c.parent.empty())
    {
        ox += c.orbitX;
        oy += c.orbitY;
        oz += c.orbitZ;
    }
    orx = c.rotX; ory = c.rotY; orz = c.rotZ;

    std::string p = c.parent;
    int guard = 0;
    while (!p.empty() && guard++ < 256)
    {
        bool found = false;
        for (const auto& a : gAudio3DNodes)
        {
            if (a.name == p)
            {
                ox += a.x; oy += a.y; oz += a.z;
                p = a.parent;
                found = true;
                break;
            }
        }
        if (found) continue;
        for (const auto& s : gStaticMeshNodes)
        {
            if (s.name == p)
            {
                // Parent rotation should orbit the camera offset around parent pivot.
                rotateOffsetEuler(ox, oy, oz, s.rotX, s.rotY, s.rotZ);
                ox += s.x; oy += s.y; oz += s.z;
                orx += s.rotX; ory += s.rotY; orz += s.rotZ;
                p = s.parent;
                found = true;
                break;
            }
        }
        if (found) continue;
        for (const auto& pc : gCamera3DNodes)
        {
            if (pc.name == p)
            {
                rotateOffsetEuler(ox, oy, oz, pc.rotX, pc.rotY, pc.rotZ);
                ox += pc.x; oy += pc.y; oz += pc.z;
                orx += pc.rotX; ory += pc.rotY; orz += pc.rotZ;
                p = pc.parent;
                found = true;
                break;
            }
        }
        if (found) continue;
        for (const auto& n : gNode3DNodes)
        {
            if (n.name == p)
            {
                rotateOffsetEuler(ox, oy, oz, n.rotX, n.rotY, n.rotZ);
                ox += n.x; oy += n.y; oz += n.z;
                orx += n.rotX; ory += n.rotY; orz += n.rotZ;
                p = n.parent;
                found = true;
                break;
            }
        }
        if (!found) break;
    }
}

static void BeginTransformSnapshot()
{
    if (gSelectedAudio3D >= 0 && gSelectedAudio3D < (int)gAudio3DNodes.size())
    {
        gHasTransformSnapshot = true;
        gTransformIsStatic = false;
        gTransformIsNode3D = false;
        gTransformIndex = gSelectedAudio3D;
        gTransformBefore = gAudio3DNodes[gSelectedAudio3D];
        return;
    }

    if (gSelectedStaticMesh >= 0 && gSelectedStaticMesh < (int)gStaticMeshNodes.size())
    {
        gHasTransformSnapshot = true;
        gTransformIsStatic = true;
        gTransformIsNode3D = false;
        gTransformIndex = gSelectedStaticMesh;
        gTransformBeforeStatic = gStaticMeshNodes[gSelectedStaticMesh];
        return;
    }

    if (gSelectedNode3D >= 0 && gSelectedNode3D < (int)gNode3DNodes.size())
    {
        gHasTransformSnapshot = true;
        gTransformIsStatic = false;
        gTransformIsNode3D = true;
        gTransformIndex = gSelectedNode3D;
        gTransformBeforeNode3D = gNode3DNodes[gSelectedNode3D];
    }
}

static void EndTransformSnapshot()
{
    if (!gHasTransformSnapshot) return;

    if (gTransformIsStatic)
    {
        if (gTransformIndex < 0 || gTransformIndex >= (int)gStaticMeshNodes.size())
        {
            gHasTransformSnapshot = false;
            return;
        }

        StaticMesh3DNode before = gTransformBeforeStatic;
        StaticMesh3DNode after = gStaticMeshNodes[gTransformIndex];
        if (TransformChanged(before, after))
        {
            int idx = gTransformIndex;
            PushUndo({"Transform StaticMesh3D",
                [idx, before]() { if (idx >= 0 && idx < (int)gStaticMeshNodes.size()) gStaticMeshNodes[idx] = before; },
                [idx, after]() { if (idx >= 0 && idx < (int)gStaticMeshNodes.size()) gStaticMeshNodes[idx] = after; }
            });
        }
    }
    else if (gTransformIsNode3D)
    {
        if (gTransformIndex < 0 || gTransformIndex >= (int)gNode3DNodes.size())
        {
            gHasTransformSnapshot = false;
            return;
        }

        Node3DNode before = gTransformBeforeNode3D;
        Node3DNode after = gNode3DNodes[gTransformIndex];
        if (TransformChanged(before, after))
        {
            int idx = gTransformIndex;
            PushUndo({"Transform Node3D",
                [idx, before]() { if (idx >= 0 && idx < (int)gNode3DNodes.size()) gNode3DNodes[idx] = before; },
                [idx, after]() { if (idx >= 0 && idx < (int)gNode3DNodes.size()) gNode3DNodes[idx] = after; }
            });
        }
    }
    else
    {
        if (gTransformIndex < 0 || gTransformIndex >= (int)gAudio3DNodes.size())
        {
            gHasTransformSnapshot = false;
            return;
        }

        Audio3DNode before = gTransformBefore;
        Audio3DNode after = gAudio3DNodes[gTransformIndex];
        if (TransformChanged(before, after))
        {
            int idx = gTransformIndex;
            PushUndo({"Transform Node",
                [idx, before]() { if (idx >= 0 && idx < (int)gAudio3DNodes.size()) gAudio3DNodes[idx] = before; },
                [idx, after]() { if (idx >= 0 && idx < (int)gAudio3DNodes.size()) gAudio3DNodes[idx] = after; }
            });
        }
    }

    gHasTransformSnapshot = false;
}

static void DoUndo()
{
    // If a transform is active or pending, finalize it so Ctrl+Z undoes the transform first.
    if (gTransformMode != Transform_None || gHasTransformSnapshot)
    {
        EndTransformSnapshot();
        gTransformMode = Transform_None;
        gAxisLock = 0;
    }

    if (gUndoStack.empty()) return;
    UndoAction action = gUndoStack.back();
    gUndoStack.pop_back();
    if (action.undo) action.undo();
    gRedoStack.push_back(action);
}

static void DoRedo()
{
    // If a transform is active or pending, finalize it so redo targets last transform first.
    if (gTransformMode != Transform_None || gHasTransformSnapshot)
    {
        EndTransformSnapshot();
        gTransformMode = Transform_None;
        gAxisLock = 0;
    }

    if (gRedoStack.empty()) return;
    UndoAction action = gRedoStack.back();
    gRedoStack.pop_back();
    if (action.redo) action.redo();
    gUndoStack.push_back(action);
}

static std::string GetPrefsPath()
{
    // Store preferences next to the editor executable
    return "editor_prefs.ini";
}

static void LoadPreferences(float& uiScale, int& themeMode)
{
    std::ifstream in(GetPrefsPath());
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line))
    {
        if (line.rfind("uiScale=", 0) == 0)
        {
            uiScale = std::stof(line.substr(8));
        }
        else if (line.rfind("themeMode=", 0) == 0)
        {
            themeMode = std::stoi(line.substr(10));
        }
        else if (line.rfind("spaceTheme=", 0) == 0)
        {
            // legacy support
            themeMode = (line.substr(11) == "1") ? 0 : 1;
        }
        else if (line.rfind("dreamSdkHome=", 0) == 0)
        {
            gPrefDreamSdkHome = line.substr(13);
        }
        else if (line.rfind("vcvarsPath=", 0) == 0)
        {
            gPrefVcvarsPath = line.substr(10);
        }
    }
}

static void SavePreferences(float uiScale, int themeMode)
{
    std::ofstream out(GetPrefsPath(), std::ios::out | std::ios::trunc);
    if (!out.is_open()) return;
    out << "uiScale=" << uiScale << "\n";
    out << "themeMode=" << themeMode << "\n";
    out << "dreamSdkHome=" << gPrefDreamSdkHome << "\n";
    out << "vcvarsPath=" << gPrefVcvarsPath << "\n";
}

static std::string GetFolderName(const std::string& path)
{
    std::filesystem::path p(path);
    return p.filename().string();
}

static void AddRecentProject(const std::string& projFile)
{
    if (projFile.empty()) return;
    gRecentProjects.erase(std::remove(gRecentProjects.begin(), gRecentProjects.end(), projFile), gRecentProjects.end());
    gRecentProjects.insert(gRecentProjects.begin(), projFile);
    if (gRecentProjects.size() > 10) gRecentProjects.resize(10);
}

static std::string GetProjectDefaultScene(const std::filesystem::path& projectDir)
{
    if (projectDir.empty()) return "";
    std::ifstream in(projectDir / "Config.ini");
    if (!in.is_open()) return "";

    std::string line;
    while (std::getline(in, line))
    {
        if (line.rfind("defaultScene=", 0) == 0)
            return line.substr(13);
    }
    return "";
}

static bool SetProjectDefaultScene(const std::filesystem::path& projectDir, const std::filesystem::path& scenePath)
{
    if (projectDir.empty() || scenePath.empty()) return false;

    std::filesystem::path cfgPath = projectDir / "Config.ini";
    std::vector<std::string> lines;

    {
        std::ifstream in(cfgPath);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
    }

    std::filesystem::path outPath = scenePath;
    std::error_code ec;
    std::filesystem::path rel = std::filesystem::relative(scenePath, projectDir, ec);
    if (!ec && !rel.empty()) outPath = rel;

    std::string defaultLine = "defaultScene=" + outPath.generic_string();
    bool replaced = false;
    for (auto& l : lines)
    {
        if (l.rfind("defaultScene=", 0) == 0)
        {
            l = defaultLine;
            replaced = true;
            break;
        }
    }
    if (!replaced) lines.push_back(defaultLine);

    std::ofstream out(cfgPath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        out << lines[i];
        if (i + 1 < lines.size()) out << "\n";
    }
    return true;
}

static void CreateNebulaProject(const std::string& folder)
{
    if (folder.empty()) return;

    std::filesystem::path projDir(folder);
    std::filesystem::create_directories(projDir);

    std::string projName = GetFolderName(folder);
    if (projName.empty()) projName = "NebulaProject";

    // Normalize accidental extension in folder name (e.g. "MyGame.neb")
    auto endsWithNoCase = [](const std::string& s, const std::string& suf)
    {
        if (s.size() < suf.size()) return false;
        for (size_t i = 0; i < suf.size(); ++i)
        {
            char a = (char)tolower((unsigned char)s[s.size() - suf.size() + i]);
            char b = (char)tolower((unsigned char)suf[i]);
            if (a != b) return false;
        }
        return true;
    };
    if (endsWithNoCase(projName, ".nebproj"))
    {
        projName.resize(projName.size() - 8);
    }
    else if (endsWithNoCase(projName, ".neb"))
    {
        projName.resize(projName.size() - 4);
    }
    if (projName.empty()) projName = "NebulaProject";

    std::filesystem::create_directories(projDir / "Assets");
    std::filesystem::create_directories(projDir / "Scripts");
    std::filesystem::create_directories(projDir / "Intermediate");

    // Config.ini (project name)
    {
        std::ofstream cfg(projDir / "Config.ini", std::ios::out | std::ios::trunc);
        if (cfg.is_open())
        {
            cfg << "project=" << projName;
        }
    }

    // Project file (.nebproj)
    {
        std::filesystem::path projPath = projDir / (projName + ".nebproj");
        std::ofstream proj(projPath, std::ios::out | std::ios::trunc);
        if (proj.is_open())
        {
            proj << "name=" << projName;
        }

        // Cleanup legacy project filename if present
        std::filesystem::path legacyProjPath = projDir / (projName + ".neb");
        if (std::filesystem::exists(legacyProjPath))
        {
            std::error_code ec;
            std::filesystem::remove(legacyProjPath, ec);
        }

        gProjectFile = projPath.string();
        AddRecentProject(gProjectFile);
    }

    gProjectDir = projDir.string();
}

static std::filesystem::path MakeUniqueAssetPath(const std::filesystem::path& root, const std::string& baseName, const std::string& ext)
{
    for (int i = 0; i < 1000; ++i)
    {
        std::string suffix = (i == 0) ? "" : std::to_string(i);
        std::filesystem::path p = root / (baseName + suffix + ext);
        if (!std::filesystem::exists(p))
            return p;
    }
    return root / (baseName + ext);
}

static void CreateSceneAssetAt(const std::filesystem::path& scenePath)
{
    std::filesystem::create_directories(scenePath.parent_path());
    std::ofstream out(scenePath, std::ios::out | std::ios::trunc);
    if (out.is_open())
    {
        out << "name=" << scenePath.stem().string() << "\n";
    }
}

static std::filesystem::path CreateSceneAsset(const std::filesystem::path& assetsRoot)
{
    std::filesystem::create_directories(assetsRoot);
    std::filesystem::path scenePath = MakeUniqueAssetPath(assetsRoot, "NewScene", ".nebscene");
    CreateSceneAssetAt(scenePath);
    return scenePath;
}

static void CreateAssetFolderAt(const std::filesystem::path& folder)
{
    std::filesystem::create_directories(folder);
}

static std::filesystem::path CreateAssetFolder(const std::filesystem::path& assetsRoot)
{
    std::filesystem::create_directories(assetsRoot);
    std::filesystem::path folder = MakeUniqueAssetPath(assetsRoot, "NewFolder", "");
    CreateAssetFolderAt(folder);
    return folder;
}

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
    }
    return matPath;
}

static bool LoadMaterialTexture(const std::filesystem::path& matPath, std::string& outTex)
{
    return NebulaAssets::LoadMaterialTexture(matPath, outTex);
}

static bool LoadMaterialUvScale(const std::filesystem::path& matPath, float& outUvScale)
{
    return NebulaAssets::LoadMaterialUvScale(matPath, outUvScale);
}

static bool LoadMaterialAllowUvRepeat(const std::filesystem::path& matPath, bool& outAllowUvRepeat)
{
    return NebulaAssets::LoadMaterialAllowUvRepeat(matPath, outAllowUvRepeat);
}

static void LoadMaterialUvTransform(const std::filesystem::path& matPath, float& su, float& sv, float& ou, float& ov, float& rotDeg)
{
    NebulaAssets::LoadMaterialUvTransform(matPath, su, sv, ou, ov, rotDeg);
}

static bool SaveMaterialAllFields(const std::filesystem::path& matPath, const std::string& tex, float uvScale, bool allowUvRepeat, float su, float sv, float ou, float ov, float rotDeg)
{
    return NebulaAssets::SaveMaterialAllFields(matPath, tex, uvScale, allowUvRepeat, su, sv, ou, ov, rotDeg);
}

static bool SaveMaterialTexture(const std::filesystem::path& matPath, const std::string& tex)
{
    return NebulaAssets::SaveMaterialTexture(matPath, tex);
}

static bool SaveMaterialUvScale(const std::filesystem::path& matPath, float uvScale)
{
    return NebulaAssets::SaveMaterialUvScale(matPath, uvScale);
}

static bool SaveMaterialAllowUvRepeat(const std::filesystem::path& matPath, bool allowUvRepeat)
{
    return NebulaAssets::SaveMaterialAllowUvRepeat(matPath, allowUvRepeat);
}

static bool SaveMaterialUvTransform(const std::filesystem::path& matPath, float su, float sv, float ou, float ov, float rotDeg)
{
    return NebulaAssets::SaveMaterialUvTransform(matPath, su, sv, ou, ov, rotDeg);
}

static std::string GetStaticMeshPrimaryMaterial(const StaticMesh3DNode& n)
{
    return NebulaNodes::GetStaticMeshPrimaryMaterial(n);
}

static std::string GetStaticMeshMaterialByIndex(const StaticMesh3DNode& n, int matIndex)
{
    return NebulaNodes::GetStaticMeshMaterialByIndex(n, matIndex);
}

static std::string GetStaticMeshSlotLabel(const StaticMesh3DNode& n, int slotIndex)
{
    return NebulaNodes::GetStaticMeshSlotLabel(n, slotIndex, gProjectDir);
}

static std::filesystem::path GetNebSlotsPathForMesh(const std::filesystem::path& absMeshPath)
{
    return NebulaAssets::GetNebSlotsPathForMesh(absMeshPath);
}

static bool LoadNebSlotsManifestFile(const std::filesystem::path& slotFilePath, std::vector<std::string>& outSlots)
{
    return NebulaAssets::LoadNebSlotsManifestFile(slotFilePath, outSlots, gProjectDir);
}

static bool LoadNebSlotsManifest(const std::filesystem::path& absMeshPath, std::vector<std::string>& outSlots)
{
    return NebulaAssets::LoadNebSlotsManifest(absMeshPath, outSlots, gProjectDir);
}

static void AutoAssignMaterialSlotsFromMesh(StaticMesh3DNode& n)
{
    NebulaNodes::AutoAssignMaterialSlotsFromMesh(n);
}

static bool TryGetNodeWorldPosByName(const std::string& name, float& ox, float& oy, float& oz)
{
    if (name.empty()) return false;

    int si = FindStaticMeshByName(name);
    if (si >= 0)
    {
        float rx, ry, rz, sx, sy, sz;
        GetStaticMeshWorldTRS(si, ox, oy, oz, rx, ry, rz, sx, sy, sz);
        return true;
    }

    int ni = FindNode3DByName(name);
    if (ni >= 0)
    {
        float rx, ry, rz, sx, sy, sz;
        GetNode3DWorldTRS(ni, ox, oy, oz, rx, ry, rz, sx, sy, sz);
        return true;
    }

    return false;
}

static bool IsCameraUnderNode3D(const Camera3DNode& cam, const std::string& nodeName)
{
    if (nodeName.empty()) return false;
    if (cam.parent == nodeName) return true;

    std::string cur = cam.name;
    std::string p;
    int guard = 0;
    while (guard++ < 256 && TryGetParentByNodeName(cur, p))
    {
        if (p == nodeName) return true;
        if (p.empty() || p == cur) break;
        cur = p;
    }
    return false;
}

static void GetLocalAxesFromEuler(float rotX, float rotY, float rotZ, Vec3& right, Vec3& up, Vec3& forward);

#if defined(_WIN32)
#define NB_RT_EXPORT extern "C" __declspec(dllexport)
#else
#define NB_RT_EXPORT extern "C"
#endif

NB_RT_EXPORT void NB_RT_GetNode3DPosition(const char* name, float outPos[3])
{
    if (!outPos)
        return;
    outPos[0] = outPos[1] = outPos[2] = 0.0f;
    if (!name)
        return;
    int idx = FindNode3DByName(name);
    if (idx < 0)
        return;
    const auto& n = gNode3DNodes[idx];
    outPos[0] = n.x;
    outPos[1] = n.y;
    outPos[2] = n.z;
}

NB_RT_EXPORT void NB_RT_SetNode3DPosition(const char* name, float x, float y, float z)
{
    if (!name)
        return;
    int idx = FindNode3DByName(name);
    if (idx < 0)
        return;
    auto& n = gNode3DNodes[idx];
    n.x = x;
    n.y = y;
    n.z = z;
}

NB_RT_EXPORT void NB_RT_GetNode3DRotation(const char* name, float outRot[3])
{
    if (!outRot)
        return;
    outRot[0] = outRot[1] = outRot[2] = 0.0f;
    if (!name)
        return;
    int idx = FindNode3DByName(name);
    if (idx < 0)
        return;
    const auto& n = gNode3DNodes[idx];
    outRot[0] = n.rotX;
    outRot[1] = n.rotY;
    outRot[2] = n.rotZ;
}

NB_RT_EXPORT void NB_RT_SetNode3DRotation(const char* name, float x, float y, float z)
{
    if (!name)
        return;
    int idx = FindNode3DByName(name);
    if (idx < 0)
        return;
    auto& n = gNode3DNodes[idx];
    n.rotX = x;
    n.rotY = y;
    n.rotZ = z;
}

NB_RT_EXPORT void NB_RT_GetCameraWorldForward(const char* name, float outFwd[3])
{
    if (!outFwd)
        return;
    outFwd[0] = 0.0f;
    outFwd[1] = 0.0f;
    outFwd[2] = 1.0f;
    if (!name)
        return;
    int idx = FindCamera3DByName(name);
    if (idx < 0)
        return;
    float wx, wy, wz, wrx, wry, wrz;
    GetCamera3DWorldTR(idx, wx, wy, wz, wrx, wry, wrz);
    Vec3 right{}, up{}, forward{};
    GetLocalAxesFromEuler(wrx, wry, wrz, right, up, forward);
    outFwd[0] = forward.x;
    outFwd[1] = forward.y;
    outFwd[2] = forward.z;
}

NB_RT_EXPORT void NB_RT_GetCameraOrbit(const char* name, float outOrbit[3])
{
    if (!outOrbit)
        return;
    outOrbit[0] = outOrbit[1] = outOrbit[2] = 0.0f;
    if (!name)
        return;
    int idx = FindCamera3DByName(name);
    if (idx < 0)
        return;
    const auto& c = gCamera3DNodes[idx];
    outOrbit[0] = c.orbitX;
    outOrbit[1] = c.orbitY;
    outOrbit[2] = c.orbitZ;
}

NB_RT_EXPORT void NB_RT_SetCameraOrbit(const char* name, float x, float y, float z)
{
    if (!name)
        return;
    int idx = FindCamera3DByName(name);
    if (idx < 0)
        return;
    auto& c = gCamera3DNodes[idx];
    c.orbitX = x;
    c.orbitY = y;
    c.orbitZ = z;
}

NB_RT_EXPORT void NB_RT_GetCameraRotation(const char* name, float outRot[3])
{
    if (!outRot)
        return;
    outRot[0] = outRot[1] = outRot[2] = 0.0f;
    if (!name)
        return;
    int idx = FindCamera3DByName(name);
    if (idx < 0)
        return;
    const auto& c = gCamera3DNodes[idx];
    outRot[0] = c.rotX;
    outRot[1] = c.rotY;
    outRot[2] = c.rotZ;
}

NB_RT_EXPORT void NB_RT_SetCameraRotation(const char* name, float x, float y, float z)
{
    if (!name)
        return;
    int idx = FindCamera3DByName(name);
    if (idx < 0)
        return;
    auto& c = gCamera3DNodes[idx];
    c.rotX = x;
    c.rotY = y;
    c.rotZ = z;
}

NB_RT_EXPORT int NB_RT_IsCameraUnderNode3D(const char* cameraName, const char* nodeName)
{
    if (!cameraName || !nodeName)
        return 0;
    int camIdx = FindCamera3DByName(cameraName);
    if (camIdx < 0)
        return 0;
    return IsCameraUnderNode3D(gCamera3DNodes[camIdx], nodeName) ? 1 : 0;
}

static void ReparentStaticMeshKeepWorldPos(int childIdx, const std::string& newParent)
{
    if (childIdx < 0 || childIdx >= (int)gStaticMeshNodes.size()) return;

    float wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz;
    GetStaticMeshWorldTRS(childIdx, wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz);

    gStaticMeshNodes[childIdx].parent = newParent;

    if (newParent.empty())
    {
        gStaticMeshNodes[childIdx].x = wx;
        gStaticMeshNodes[childIdx].y = wy;
        gStaticMeshNodes[childIdx].z = wz;
        return;
    }

    float px = 0.0f, py = 0.0f, pz = 0.0f;
    if (TryGetNodeWorldPosByName(newParent, px, py, pz))
    {
        gStaticMeshNodes[childIdx].x = wx - px;
        gStaticMeshNodes[childIdx].y = wy - py;
        gStaticMeshNodes[childIdx].z = wz - pz;
    }
}

static void ResetStaticMeshTransformsKeepWorld(int idx)
{
    if (idx < 0 || idx >= (int)gStaticMeshNodes.size()) return;
    float wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz;
    GetStaticMeshWorldTRS(idx, wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz);
    auto& s = gStaticMeshNodes[idx];
    s.parent.clear();
    s.x = wx; s.y = wy; s.z = wz;
    s.rotX = wrx; s.rotY = wry; s.rotZ = wrz;
    s.scaleX = wsx; s.scaleY = wsy; s.scaleZ = wsz;
}

static std::string ToProjectRelativePath(const std::filesystem::path& p)
{
    if (gProjectDir.empty())
        return p.filename().generic_string();

    std::error_code ec;
    std::filesystem::path rel = std::filesystem::relative(p, std::filesystem::path(gProjectDir), ec);
    if (ec) return p.filename().generic_string();
    return rel.generic_string();
}

static std::string SanitizeToken(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (char c : in)
    {
        unsigned char uc = (unsigned char)c;
        if (std::isalnum(uc) || c == '_' || c == '-') out.push_back(c);
        else if (std::isspace(uc) || c == '.') out.push_back('_');
    }
    if (out.empty()) out = "slot";
    return out;
}

static bool ResolveMaterialTexturePath(const aiScene* scene, const aiMaterial* mat, const std::filesystem::path& modelPath, std::filesystem::path& outTexPath, std::string& warn)
{
    if (!scene || !mat) return false;

    aiString texName;
    bool found = false;
#if defined(aiTextureType_BASE_COLOR)
    if (!found && mat->GetTexture(aiTextureType_BASE_COLOR, 0, &texName) == AI_SUCCESS) found = true;
#endif
    if (!found && mat->GetTexture(aiTextureType_DIFFUSE, 0, &texName) == AI_SUCCESS) found = true;
    if (!found && mat->GetTexture(aiTextureType_UNKNOWN, 0, &texName) == AI_SUCCESS) found = true;
    if (!found) return false;

    std::string raw = texName.C_Str();
    if (raw.empty()) return false;
    if (raw[0] == '*')
    {
        if (!warn.empty()) warn += " | ";
        warn += "Embedded FBX textures not supported yet (" + raw + ")";
        return false;
    }

    std::filesystem::path p(raw);
    if (p.is_relative())
    {
        std::filesystem::path p1 = modelPath.parent_path() / p;
        if (std::filesystem::exists(p1)) { outTexPath = p1; return true; }

        std::filesystem::path p2 = modelPath.parent_path() / p.filename();
        if (std::filesystem::exists(p2)) { outTexPath = p2; return true; }
    }
    else if (std::filesystem::exists(p))
    {
        outTexPath = p;
        return true;
    }

    if (!warn.empty()) warn += " | ";
    warn += "Texture file missing: " + raw;
    return false;
}

static bool FindTextureByMaterialNameFallback(const std::filesystem::path& modelPath, const std::string& materialName, std::filesystem::path& outTexPath)
{
    if (materialName.empty()) return false;
    std::filesystem::path dir = modelPath.parent_path();
    if (dir.empty() || !std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) return false;

    auto toLower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    };

    const std::string matRaw = toLower(materialName);
    const std::string matSan = toLower(SanitizeToken(materialName));

    for (const auto& e : std::filesystem::directory_iterator(dir))
    {
        if (!e.is_regular_file()) continue;
        std::string ext = toLower(e.path().extension().string());
        if (ext != ".png" && ext != ".tga" && ext != ".jpg" && ext != ".jpeg" && ext != ".bmp" && ext != ".gif" && ext != ".webp")
            continue;

        std::string stemRaw = toLower(e.path().stem().string());
        std::string stemSan = toLower(SanitizeToken(e.path().stem().string()));
        if (stemRaw == matRaw || stemSan == matRaw || stemRaw == matSan || stemSan == matSan)
        {
            outTexPath = e.path();
            return true;
        }
    }
    return false;
}

static bool SaveNebSlotsManifest(const std::filesystem::path& absMeshPath, const std::vector<std::string>& slotMaterials)
{
    return NebulaAssets::SaveNebSlotsManifest(absMeshPath, slotMaterials);
}

static int ImportModelTexturesAndGenerateMaterials(const aiScene* scene,
    const std::filesystem::path& modelPath,
    const std::filesystem::path& targetDir,
    const std::filesystem::path& meshOut,
    std::string& warn)
{
    if (!scene) return 0;
    std::filesystem::create_directories(targetDir);
    std::filesystem::path texDir = targetDir / "tex";
    std::filesystem::path matDir = targetDir / "mat";
    std::filesystem::create_directories(texDir);
    std::filesystem::create_directories(matDir);

    int generated = 0;

    // Keep only materials actually referenced by meshes, in stable internal index order.
    std::vector<unsigned int> used;
    std::vector<unsigned char> seen(scene->mNumMaterials, 0);
    for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
    {
        const aiMesh* mesh = scene->mMeshes[m];
        if (!mesh) continue;
        if (mesh->mMaterialIndex < scene->mNumMaterials && !seen[mesh->mMaterialIndex])
        {
            seen[mesh->mMaterialIndex] = 1;
            used.push_back(mesh->mMaterialIndex);
        }
    }
    std::sort(used.begin(), used.end());

    std::string meshStem = SanitizeToken(meshOut.stem().string());
    std::vector<std::string> importedSlotMaterials(kStaticMeshMaterialSlots);
    for (size_t ui = 0; ui < used.size(); ++ui)
    {
        unsigned int mi = used[ui];
        const aiMaterial* mat = scene->mMaterials[mi];
        aiString matName;
        std::string matNameSafe = "material" + std::to_string((int)ui + 1);
        if (mat && mat->Get(AI_MATKEY_NAME, matName) == AI_SUCCESS && matName.length > 0)
            matNameSafe = SanitizeToken(matName.C_Str());

        float uvSu = 1.0f, uvSv = 1.0f, uvOu = 0.0f, uvOv = 0.0f, uvRotDeg = 0.0f;
        if (mat)
        {
            aiUVTransform uvT{};
#if defined(aiTextureType_BASE_COLOR)
            if (mat->Get(AI_MATKEY_UVTRANSFORM(aiTextureType_BASE_COLOR, 0), uvT) == AI_SUCCESS ||
                mat->Get(AI_MATKEY_UVTRANSFORM(aiTextureType_DIFFUSE, 0), uvT) == AI_SUCCESS)
#else
            if (mat->Get(AI_MATKEY_UVTRANSFORM(aiTextureType_DIFFUSE, 0), uvT) == AI_SUCCESS)
#endif
            {
                uvSu = uvT.mScaling.x; uvSv = uvT.mScaling.y;
                uvOu = uvT.mTranslation.x; uvOv = uvT.mTranslation.y;
                uvRotDeg = uvT.mRotation * 57.2957795f;
            }
        }

        std::filesystem::path texSrc;
        bool texFound = ResolveMaterialTexturePath(scene, mat, modelPath, texSrc, warn);
        if (!texFound)
        {
            std::string matNameRaw = matNameSafe;
            if (mat && mat->Get(AI_MATKEY_NAME, matName) == AI_SUCCESS && matName.length > 0)
                matNameRaw = matName.C_Str();
            if (FindTextureByMaterialNameFallback(modelPath, matNameRaw, texSrc))
            {
                texFound = true;
                if (!warn.empty()) warn += " | ";
                warn += "Texture fallback by material name: " + matNameRaw + " -> " + texSrc.filename().string();
            }
        }
        std::string texRel;
        if (texFound)
        {
            char midx[8];
            snprintf(midx, sizeof(midx), "%02u", (unsigned)(ui + 1));
            std::string texBase = meshStem + "_" + std::string(midx) + "_" + SanitizeToken(texSrc.stem().string());
            std::filesystem::path texOut = MakeUniqueAssetPath(texDir, texBase, ".nebtex");
            std::string texWarn;
            if (ExportNebTexturePNG(texSrc, texOut, texWarn))
            {
                texRel = ToProjectRelativePath(texOut);
                if (!texWarn.empty())
                {
                    if (!warn.empty()) warn += " | ";
                    warn += texWarn;
                }
            }
            else
            {
                if (!warn.empty()) warn += " | ";
                warn += "Texture import failed: " + texSrc.string();
            }
        }

        char midx[8];
        snprintf(midx, sizeof(midx), "%02u", (unsigned)(ui + 1));
        std::string matBase = "m_" + meshStem + "_" + std::string(midx) + "_" + matNameSafe;
        std::filesystem::path matOut = MakeUniqueAssetPath(matDir, matBase, ".nebmat");
        if (SaveMaterialTexture(matOut, texRel))
        {
            SaveMaterialUvTransform(matOut, uvSu, uvSv, uvOu, uvOv, uvRotDeg);
            generated++;
            if ((int)ui < kStaticMeshMaterialSlots)
                importedSlotMaterials[(int)ui] = ToProjectRelativePath(matOut);
        }
    }

    if (!SaveNebSlotsManifest(meshOut, importedSlotMaterials))
    {
        if (!warn.empty()) warn += " | ";
        warn += "Failed to write .nebslots manifest in nebslot folder";
    }

    return generated;
}

static std::filesystem::path GetNebTexMetaPath(const std::filesystem::path& nebtexPath)
{
    return NebulaAssets::GetNebTexMetaPath(nebtexPath);
}

static int LoadNebTexWrapMode(const std::filesystem::path& nebtexPath)
{
    return NebulaAssets::LoadNebTexWrapMode(nebtexPath);
}

static int LoadNebTexSaturnNpotMode(const std::filesystem::path& nebtexPath)
{
    return NebulaAssets::LoadNebTexSaturnNpotMode(nebtexPath);
}

static bool LoadNebTexAllowUvRepeat(const std::filesystem::path& nebtexPath)
{
    return NebulaAssets::LoadNebTexAllowUvRepeat(nebtexPath);
}

static int LoadNebTexFilterMode(const std::filesystem::path& nebtexPath)
{
    return NebulaAssets::LoadNebTexFilterMode(nebtexPath);
}

static bool SaveNebTexWrapMode(const std::filesystem::path& nebtexPath, int mode)
{
    return NebulaAssets::SaveNebTexWrapMode(nebtexPath, mode);
}

static void LoadNebTexFlipOptions(const std::filesystem::path& nebtexPath, bool& flipU, bool& flipV)
{
    NebulaAssets::LoadNebTexFlipOptions(nebtexPath, flipU, flipV);
}

static bool ReadNebTexDimensions(const std::filesystem::path& nebtexPath, int& outW, int& outH)
{
    return NebulaAssets::ReadNebTexDimensions(nebtexPath, outW, outH);
}

static bool SaveNebTexSaturnNpotMode(const std::filesystem::path& nebtexPath, int mode)
{
    return NebulaAssets::SaveNebTexSaturnNpotMode(nebtexPath, mode);
}

static bool SaveNebTexFlipOptions(const std::filesystem::path& nebtexPath, bool flipU, bool flipV)
{
    return NebulaAssets::SaveNebTexFlipOptions(nebtexPath, flipU, flipV);
}

static bool SaveNebTexAllowUvRepeat(const std::filesystem::path& nebtexPath, bool allowUvRepeat)
{
    return NebulaAssets::SaveNebTexAllowUvRepeat(nebtexPath, allowUvRepeat);
}

static bool SaveNebTexFilterMode(const std::filesystem::path& nebtexPath, int filterMode)
{
    return NebulaAssets::SaveNebTexFilterMode(nebtexPath, filterMode);
}

static std::filesystem::path gRenamePath;
static char gRenameBuffer[256] = {};
static std::filesystem::path gInlineRenamePath;
static char gInlineRenameBuffer[256] = {};
static bool gInlineRenameFocus = false;
static std::filesystem::path gPendingDelete;
static bool gDoDelete = false;
static bool gRenameModalOpen = false;
static bool gQuitConfirmOpen = false;
static std::filesystem::path gAssetsCurrentDir;
static std::filesystem::path gSelectedAssetPath;
static double gSelectedAssetPathSetTime = 0.0;
static bool gMaterialInspectorOpen = false;
static std::filesystem::path gMaterialInspectorPath;
static bool gNebTexInspectorOpen = false;
static std::filesystem::path gNebTexInspectorPath;
// Secondary (bottom-half) inspector panes for asset inspectors.
static bool gMaterialInspectorOpen2 = false;
static std::filesystem::path gMaterialInspectorPath2;
static bool gNebTexInspectorOpen2 = false;
static std::filesystem::path gNebTexInspectorPath2;
static bool gPreviewSaturnSampling = true;

struct NebFaceRecord
{
    uint8_t arity = 3;            // 3 or 4 for Saturn path
    uint8_t winding = 0;          // 0=clockwise, 1=counter-clockwise (author-space hint)
    uint16_t material = 0;
    uint16_t indices[4] = { 0, 0, 0, 0 };
    Vec3 uvs[4] = {};
};

struct NebMesh
{
    std::vector<Vec3> positions;
    std::vector<Vec3> uvs;
    std::vector<uint16_t> indices;
    std::vector<uint16_t> faceMaterial; // per-triangle material index
    std::vector<uint8_t> faceVertexCounts; // original polygon arity per source face (v4+)
    std::vector<NebFaceRecord> faceRecords; // canonical authored face stream (v5+)
    bool hasUv = false;
    bool hasFaceMaterial = false;
    bool hasFaceTopology = false;
    bool hasFaceRecords = false;
    bool valid = false;
};

static std::unordered_map<std::string, NebMesh> gNebMeshCache;
static std::unordered_map<std::string, GLuint> gNebTextureCache;
static GLuint gCheckerOverlayTex = 0;

static void DeleteAssetPath(const std::filesystem::path& p)
{
    if (!std::filesystem::exists(p)) return;
    if (std::filesystem::is_directory(p))
        std::filesystem::remove_all(p);
    else
        std::filesystem::remove(p);
}

static std::filesystem::path MoveAssetToTrash(const std::filesystem::path& p)
{
    std::error_code ec;
    if (!std::filesystem::exists(p, ec) || ec) return {};

    std::filesystem::path trash = std::filesystem::temp_directory_path(ec) / "nebula_trash";
    if (ec) return {};
    std::filesystem::create_directories(trash, ec);
    if (ec) return {};

    std::filesystem::path base = trash / p.filename();
    std::filesystem::path dst = base;
    for (int i = 0; i < 1000; ++i)
    {
        if (!std::filesystem::exists(dst, ec) || ec) break;
        dst = trash / (base.stem().string() + "_" + std::to_string(i) + base.extension().string());
    }

    ec.clear();
    std::filesystem::rename(p, dst, ec);
    if (!ec) return dst;

    // Fallback when rename fails (e.g., access denied/cross-device): try copy + remove.
    ec.clear();
    if (std::filesystem::is_directory(p, ec) && !ec)
    {
        std::filesystem::copy(p, dst, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) return {};
        std::filesystem::remove_all(p, ec);
        if (ec) return {};
        return dst;
    }

    ec.clear();
    std::filesystem::copy_file(p, dst, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return {};
    std::filesystem::remove(p, ec);
    if (ec) return {};
    return dst;
}

static std::filesystem::path RenameAssetPath(const std::filesystem::path& p, const std::string& newName)
{
    if (newName.empty()) return {};
    std::filesystem::path target = p.parent_path() / newName;
    if (p.has_extension())
        target.replace_extension(p.extension());
    std::filesystem::rename(p, target);
    return target;
}

static std::filesystem::path DuplicateAssetPath(const std::filesystem::path& p)
{
    if (!std::filesystem::exists(p)) return {};

    std::filesystem::path parent = p.parent_path();
    std::string stem = p.stem().string();
    std::string ext = p.extension().string();

    std::filesystem::path candidate;
    for (int i = 1; i < 1000; ++i)
    {
        std::string suffix = (i == 1) ? "_copy" : ("_copy" + std::to_string(i));
        if (std::filesystem::is_directory(p))
            candidate = parent / (p.filename().string() + suffix);
        else
            candidate = parent / (stem + suffix + ext);

        if (!std::filesystem::exists(candidate))
            break;
    }

    if (candidate.empty()) return {};

    std::error_code ec;
    if (std::filesystem::is_directory(p))
        std::filesystem::copy(p, candidate, std::filesystem::copy_options::recursive, ec);
    else
        std::filesystem::copy_file(p, candidate, std::filesystem::copy_options::overwrite_existing, ec);

    if (ec) return {};
    return candidate;
}

static std::string NormalizePathRef(std::string s)
{
    return NebulaScene::NormalizePathRef(s);
}

static bool RewritePathRefForRename(std::string& ref, const std::string& oldRel, const std::string& newRel, bool isDir)
{
    return NebulaScene::RewritePathRefForRename(ref, oldRel, newRel, isDir);
}

static void UpdateAssetReferencesForRename(const std::filesystem::path& oldPath, const std::filesystem::path& newPath)
{
    NebulaScene::UpdateAssetReferencesForRename(
        oldPath,
        newPath,
        gProjectDir,
        gAudio3DNodes,
        gStaticMeshNodes,
        gNode3DNodes,
        gOpenScenes,
        gSelectedAssetPath);

    auto remapPath = [&](std::filesystem::path& p)
    {
        if (p.empty()) return;
        std::string cur = p.generic_string();
        std::string oldS = oldPath.generic_string();
        if (cur == oldS || cur.rfind(oldS + "/", 0) == 0)
        {
            std::string tail = cur.substr(oldS.size());
            p = std::filesystem::path(newPath.generic_string() + tail);
        }
    };

    remapPath(gAssetsCurrentDir);
    remapPath(gMaterialInspectorPath);
    remapPath(gMaterialInspectorPath2);
    remapPath(gNebTexInspectorPath);
    remapPath(gNebTexInspectorPath2);

    if (gInlineRenamePath == oldPath)
        gInlineRenamePath = newPath;
}

static void BeginInlineAssetRename(const std::filesystem::path& p, const std::string& displayName)
{
    gInlineRenamePath = p;
    strncpy_s(gInlineRenameBuffer, displayName.c_str(), sizeof(gInlineRenameBuffer) - 1);
    gInlineRenameFocus = true;
}

static void CommitInlineAssetRename()
{
    if (gInlineRenamePath.empty()) return;
    std::filesystem::path oldPath = gInlineRenamePath;
    std::string newName = gInlineRenameBuffer;
    gInlineRenamePath.clear();
    gInlineRenameFocus = false;

    if (newName.empty()) return;
    std::filesystem::path newPath = RenameAssetPath(oldPath, newName);
    if (!newPath.empty() && newPath != oldPath)
    {
        PushUndo({"Rename Asset",
            [oldPath, newPath]() { if (std::filesystem::exists(newPath)) std::filesystem::rename(newPath, oldPath); },
            [oldPath, newPath]() { if (std::filesystem::exists(oldPath)) std::filesystem::rename(oldPath, newPath); }
        });
        UpdateAssetReferencesForRename(oldPath, newPath);
        gSelectedAssetPath = newPath;
        gSelectedAssetPathSetTime = ImGui::GetTime();
        gViewportToast = "Renamed " + oldPath.stem().string();
    }
    else
    {
        gViewportToast = "Rename failed";
    }
    gViewportToastUntil = glfwGetTime() + 2.0;
}

static std::string EncodeSceneToken(const std::string& s)
{
    return NebulaScene::EncodeSceneToken(s);
}

static void DecodeSceneToken(std::string& s)
{
    NebulaScene::DecodeSceneToken(s);
}

static std::string BuildSceneText(const std::filesystem::path& path, const std::vector<Audio3DNode>& nodes, const std::vector<StaticMesh3DNode>& statics, const std::vector<Camera3DNode>& cameras, const std::vector<Node3DNode>& node3d)
{
    return NebulaScene::BuildSceneText(path, nodes, statics, cameras, node3d);
}

static void SaveSceneToPath(const std::filesystem::path& path, const std::vector<Audio3DNode>& nodes)
{
    NebulaScene::SaveSceneToPath(path, nodes);
}

static void SaveSceneToPath(const std::filesystem::path& path, const std::vector<Audio3DNode>& nodes, const std::vector<StaticMesh3DNode>& statics, const std::vector<Camera3DNode>& cameras, const std::vector<Node3DNode>& node3d)
{
    NebulaScene::SaveSceneToPath(path, nodes, statics, cameras, node3d);
}

static bool HasUnsavedProjectChanges()
{
    if (gProjectDir.empty()) return false;
    for (int i = 0; i < (int)gOpenScenes.size(); ++i)
    {
        const auto& s = gOpenScenes[i];
        const std::vector<Audio3DNode>& nodes = (i == gActiveScene) ? gAudio3DNodes : s.nodes;
        const std::vector<StaticMesh3DNode>& statics = (i == gActiveScene) ? gStaticMeshNodes : s.staticMeshes;
        const std::vector<Camera3DNode>& cameras = (i == gActiveScene) ? gCamera3DNodes : s.cameras;
        const std::vector<Node3DNode>& node3d = (i == gActiveScene) ? gNode3DNodes : s.node3d;

        std::string expected = BuildSceneText(s.path, nodes, statics, cameras, node3d);
        std::ifstream in(s.path, std::ios::in | std::ios::binary);
        if (!in.is_open()) return true;
        std::string current((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (current != expected) return true;
    }
    return false;
}

static void RefreshOpenSceneTabMetadataForPath(const std::filesystem::path& path)
{
    for (auto& s : gOpenScenes)
    {
        if (s.path == path)
            s.name = path.stem().string();
    }
}

static bool LoadSceneFromPath(const std::filesystem::path& path, SceneData& outScene)
{
    return NebulaScene::LoadSceneFromPath(path, outScene);
}

static void SetActiveScene(int index)
{
    if (index < 0 || index >= (int)gOpenScenes.size()) return;
    // save current nodes to current scene
    if (gActiveScene >= 0 && gActiveScene < (int)gOpenScenes.size())
    {
        gOpenScenes[gActiveScene].nodes = gAudio3DNodes;
        gOpenScenes[gActiveScene].staticMeshes = gStaticMeshNodes;
        gOpenScenes[gActiveScene].cameras = gCamera3DNodes;
        gOpenScenes[gActiveScene].node3d = gNode3DNodes;
    }
    gActiveScene = index;
    gAudio3DNodes = gOpenScenes[gActiveScene].nodes;
    gStaticMeshNodes = gOpenScenes[gActiveScene].staticMeshes;
    gCamera3DNodes = gOpenScenes[gActiveScene].cameras;
    gNode3DNodes = gOpenScenes[gActiveScene].node3d;
    gForceSelectSceneTab = index;
    NotifyScriptSceneSwitch();
}

static void OpenSceneFile(const std::filesystem::path& path)
{
    SceneData scene;
    if (!LoadSceneFromPath(path, scene)) return;

    // If already open, just activate
    for (int i = 0; i < (int)gOpenScenes.size(); ++i)
    {
        if (gOpenScenes[i].path == path)
        {
            SetActiveScene(i);
            return;
        }
    }

    gOpenScenes.push_back(scene);
    SetActiveScene((int)gOpenScenes.size() - 1);
}

static void SaveActiveScene()
{
    if (gActiveScene < 0 || gActiveScene >= (int)gOpenScenes.size())
    {
        gViewportToast = "No active scene to save";
        gViewportToastUntil = glfwGetTime() + 2.0;
        return;
    }
    gOpenScenes[gActiveScene].nodes = gAudio3DNodes;
    gOpenScenes[gActiveScene].staticMeshes = gStaticMeshNodes;
    gOpenScenes[gActiveScene].cameras = gCamera3DNodes;
    gOpenScenes[gActiveScene].node3d = gNode3DNodes;
    SaveSceneToPath(gOpenScenes[gActiveScene].path, gOpenScenes[gActiveScene].nodes, gOpenScenes[gActiveScene].staticMeshes, gOpenScenes[gActiveScene].cameras, gOpenScenes[gActiveScene].node3d);
    RefreshOpenSceneTabMetadataForPath(gOpenScenes[gActiveScene].path);
    gViewportToast = "Saved " + gOpenScenes[gActiveScene].name;
    gViewportToastUntil = glfwGetTime() + 2.0;
}

static void SaveAllProjectChanges()
{
    if (gOpenScenes.empty())
    {
        gViewportToast = "No open scenes to save";
        gViewportToastUntil = glfwGetTime() + 2.0;
        return;
    }

    // Sync active scene nodes first.
    if (gActiveScene >= 0 && gActiveScene < (int)gOpenScenes.size())
    {
        gOpenScenes[gActiveScene].nodes = gAudio3DNodes;
        gOpenScenes[gActiveScene].staticMeshes = gStaticMeshNodes;
        gOpenScenes[gActiveScene].cameras = gCamera3DNodes;
        gOpenScenes[gActiveScene].node3d = gNode3DNodes;
    }

    // Save every open scene in the current project.
    gSaveAllInProgress = true;
    for (auto& s : gOpenScenes)
    {
        SaveSceneToPath(s.path, s.nodes, s.staticMeshes, s.cameras, s.node3d);
        RefreshOpenSceneTabMetadataForPath(s.path);
    }
    gSaveAllInProgress = false;

    gViewportToast = "Saved all changes in current project";
    gViewportToastUntil = glfwGetTime() + 2.0;
}

static void DrawAssetsBrowser(const std::filesystem::path& root)
{
    if (!std::filesystem::exists(root))
    {
        ImGui::Text("(Assets folder missing)");
        return;
    }

    if (gAssetsCurrentDir.empty())
        gAssetsCurrentDir = root;

    // Back button
    if (gAssetsCurrentDir != root)
    {
        if (ImGui::Selectable("[..]"))
        {
            gAssetsCurrentDir = gAssetsCurrentDir.parent_path();
            if (gAssetsCurrentDir.string().find(root.string()) != 0)
                gAssetsCurrentDir = root;
        }
    }

    for (auto& entry : std::filesystem::directory_iterator(gAssetsCurrentDir))
    {
        const auto& p = entry.path();
        if (entry.is_regular_file() && p.extension() == ".meta")
            continue; // internal sidecar files (e.g. nebtex compatibility metadata)
        if (entry.is_regular_file() && p.extension() == ".fbx")
            continue; // hide source interchange files in normal asset browsing
        // .nebslots manifests are visible in Assets for manual inspection/selection.

        std::string name = p.filename().string();
        if (entry.is_regular_file())
        {
            name = p.stem().string();
        }

        std::string itemId = p.string();
        ImGui::PushID(itemId.c_str());
        if (entry.is_directory())
        {
            bool selected = (gSelectedAssetPath == p);
            if (gInlineRenamePath == p)
            {
                if (gInlineRenameFocus)
                {
                    ImGui::SetKeyboardFocusHere();
                    gInlineRenameFocus = false;
                }
                ImGuiInputTextFlags rf = ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue;
                bool submit = ImGui::InputText("##inline_rename", gInlineRenameBuffer, sizeof(gInlineRenameBuffer), rf);
                if (submit || ImGui::IsItemDeactivatedAfterEdit()) CommitInlineAssetRename();
                if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape)) { gInlineRenamePath.clear(); gInlineRenameFocus = false; }
            }
            else if (ImGui::Selectable((name + "/").c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
            {
                const bool isDouble = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                if (!selected)
                {
                    gSelectedAssetPath = p;
                    gSelectedAssetPathSetTime = ImGui::GetTime();
                }

                if (isDouble)
                {
                    gSelectedAssetPath = p;
                    gSelectedAssetPathSetTime = ImGui::GetTime();
                    gAssetsCurrentDir = p;
                    ImGui::PopID();
                    break;
                }
            }
        }
        else if (entry.is_regular_file())
        {
            bool selected = (gSelectedAssetPath == p);
            if (gInlineRenamePath == p)
            {
                if (gInlineRenameFocus)
                {
                    ImGui::SetKeyboardFocusHere();
                    gInlineRenameFocus = false;
                }
                ImGuiInputTextFlags rf = ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue;
                bool submit = ImGui::InputText("##inline_rename", gInlineRenameBuffer, sizeof(gInlineRenameBuffer), rf);
                if (submit || ImGui::IsItemDeactivatedAfterEdit()) CommitInlineAssetRename();
                if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape)) { gInlineRenamePath.clear(); gInlineRenameFocus = false; }
            }
            else if (ImGui::Selectable(name.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
            {
                const bool isDouble = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                if (!selected)
                {
                    gSelectedAssetPath = p;
                    gSelectedAssetPathSetTime = ImGui::GetTime();
                }

                if (isDouble)
                {
                    gSelectedAssetPath = p;
                    gSelectedAssetPathSetTime = ImGui::GetTime();
                    if (p.extension() == ".nebscene")
                    {
                        OpenSceneFile(p);
                    }
                    else
                    {
                        bool topOccupied =
                            (gSelectedAudio3D >= 0) || (gSelectedStaticMesh >= 0) || (gSelectedCamera3D >= 0) ||
                            (gInspectorPinnedAudio3D >= 0) || (gInspectorPinnedStaticMesh >= 0) || (gInspectorPinnedCamera3D >= 0) || (gInspectorPinnedNode3D >= 0) ||
                            (gMaterialInspectorOpen && !gMaterialInspectorPath.empty()) ||
                            (gNebTexInspectorOpen && !gNebTexInspectorPath.empty());

                        if (!topOccupied)
                        {
                            // No top inspector active: replace top inspector focus.
                            gSelectedAudio3D = -1;
                            gSelectedStaticMesh = -1;
                            gSelectedCamera3D = -1;
                            gSelectedNode3D = -1;
                            gInspectorPinnedAudio3D = -1;
                            gInspectorPinnedStaticMesh = -1;
                            gInspectorPinnedCamera3D = -1;
                            gInspectorPinnedNode3D = -1;
                            gInspectorSel = -1;

                            gMaterialInspectorOpen = false;
                            gMaterialInspectorPath.clear();
                            gNebTexInspectorOpen = false;
                            gNebTexInspectorPath.clear();

                            if (p.extension() == ".nebmat")
                            {
                                gMaterialInspectorOpen = true;
                                gMaterialInspectorPath = p;
                            }
                            else if (p.extension() == ".nebtex")
                            {
                                gNebTexInspectorOpen = true;
                                gNebTexInspectorPath = p;
                            }
                        }
                        else
                        {
                            // Top already occupied: open asset inspector in bottom half.
                            if (p.extension() == ".nebmat")
                            {
                                gMaterialInspectorOpen2 = true;
                                gMaterialInspectorPath2 = p;
                                gNebTexInspectorOpen2 = false;
                                gNebTexInspectorPath2.clear();
                            }
                            else if (p.extension() == ".nebtex")
                            {
                                gNebTexInspectorOpen2 = true;
                                gNebTexInspectorPath2 = p;
                                gMaterialInspectorOpen2 = false;
                                gMaterialInspectorPath2.clear();
                            }
                        }
                    }
                }
            }
        }

        if (ImGui::BeginPopupContextItem("AssetItem"))
        {
            if (p.extension() == ".nebscene")
            {
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.086f, 0.082f, 0.086f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.086f, 0.082f, 0.086f, 1.0f));
                if (ImGui::MenuItem("Open Scene"))
                {
                    OpenSceneFile(p);
                }
                ImGui::PopStyleColor(3);

                if (ImGui::MenuItem("Save Scene"))
                {
                    bool canSave = gSaveAllInProgress || (gActiveScene >= 0 && gActiveScene < (int)gOpenScenes.size() && gOpenScenes[gActiveScene].path == p);
                    if (canSave)
                    {
                        SaveSceneToPath(p, gAudio3DNodes, gStaticMeshNodes, gCamera3DNodes, gNode3DNodes);
                        RefreshOpenSceneTabMetadataForPath(p);
                        gViewportToast = "Saved " + p.stem().string();
                    }
                    else
                    {
                        gViewportToast = "cannot save outside of active scene";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                if (ImGui::MenuItem("Set as default scene"))
                {
                    if (!gProjectDir.empty() && SetProjectDefaultScene(std::filesystem::path(gProjectDir), p))
                    {
                        gViewportToast = "Default scene set: " + p.stem().string();
                    }
                    else
                    {
                        gViewportToast = "Failed to set default scene";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                ImGui::Separator();
            }
            if (ImGui::MenuItem("Rename"))
            {
                BeginInlineAssetRename(p, name);
            }
            if (ImGui::MenuItem("Duplicate"))
            {
                std::filesystem::path dup = DuplicateAssetPath(p);
                if (!dup.empty())
                {
                    gViewportToast = "Duplicated " + p.stem().string();
                    gSelectedAssetPath = dup;
                }
                else
                {
                    gViewportToast = "Duplicate failed";
                }
                gViewportToastUntil = glfwGetTime() + 2.0;
            }
            if (p.extension() == ".nebmat")
            {
                if (ImGui::MenuItem("Inspector"))
                {
                    gMaterialInspectorOpen = true;
                    gMaterialInspectorPath = p;
                }
            }
            if (p.extension() == ".png")
            {
                if (ImGui::MenuItem("Save as .nebtex"))
                {
                    std::filesystem::path texOut = p;
                    texOut.replace_extension(".nebtex");
                    std::string warn;
                    if (ExportNebTexturePNG(p, texOut, warn))
                    {
                        gViewportToast = "Saved " + texOut.filename().string();
                        if (!warn.empty()) gImportWarning = warn;
                    }
                    else
                    {
                        gViewportToast = "Save .nebtex failed";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
            }
            if (p.extension() == ".nebtex")
            {
                if (ImGui::MenuItem("Save .nebtex"))
                {
                    // touch/write-through option for quick explicit save from dropdown
                    std::ifstream in(p, std::ios::binary);
                    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                    if (!bytes.empty())
                    {
                        std::ofstream out(p, std::ios::binary | std::ios::trunc);
                        out.write(bytes.data(), (std::streamsize)bytes.size());
                        gViewportToast = "Saved " + p.filename().string();
                    }
                    else
                    {
                        gViewportToast = "Save .nebtex failed";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                if (ImGui::MenuItem("Generate Material"))
                {
                    std::filesystem::path matPath = p.parent_path() / ("m_" + p.stem().string() + ".nebmat");
                    if (std::filesystem::exists(matPath))
                    {
                        int n = 1;
                        std::filesystem::path base = matPath;
                        while (std::filesystem::exists(matPath))
                        {
                            matPath = base.parent_path() / (base.stem().string() + "_" + std::to_string(n++) + base.extension().string());
                        }
                    }

                    std::string texRef = p.string();
                    if (!gProjectDir.empty())
                    {
                        std::error_code ec;
                        std::filesystem::path rel = std::filesystem::relative(p, std::filesystem::path(gProjectDir), ec);
                        if (!ec) texRef = rel.generic_string();
                    }

                    if (SaveMaterialTexture(matPath, texRef))
                    {
                        gViewportToast = "Generated " + matPath.filename().string();
                        gSelectedAssetPath = matPath;
                    }
                    else
                    {
                        gViewportToast = "Generate material failed";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                if (ImGui::MenuItem("Inspector"))
                {
                    gNebTexInspectorOpen = true;
                    gNebTexInspectorPath = p;
                    gMaterialInspectorOpen = false;
                    gMaterialInspectorPath.clear();
                    gSelectedAudio3D = -1;
                    gSelectedStaticMesh = -1;
                    gSelectedCamera3D = -1;
                    gSelectedNode3D = -1;
                    gInspectorPinnedAudio3D = -1;
                    gInspectorPinnedStaticMesh = -1;
                    gInspectorPinnedCamera3D = -1;
                    gInspectorPinnedNode3D = -1;
                }
            }
            if (ImGui::MenuItem("Delete"))
            {
                gPendingDelete = p;
                gDoDelete = true;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    if (!gInlineRenamePath.empty() &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsAnyItemHovered())
    {
        gInlineRenamePath.clear();
        gInlineRenameFocus = false;
    }

    if (gDoDelete && !gPendingDelete.empty())
    {
        std::filesystem::path original = gPendingDelete;
        auto trashPath = std::make_shared<std::filesystem::path>(MoveAssetToTrash(original));
        if (!trashPath->empty())
        {
            PushUndo({"Delete Asset",
                [original, trashPath]() {
                    std::error_code ec;
                    if (!trashPath->empty() && std::filesystem::exists(*trashPath, ec) && !ec)
                        std::filesystem::rename(*trashPath, original, ec);
                },
                [original, trashPath]() {
                    std::error_code ec;
                    if (std::filesystem::exists(original, ec) && !ec)
                        *trashPath = MoveAssetToTrash(original);
                }
            });
        }
        else
        {
            gViewportToast = "Delete failed (file/folder in use or access denied)";
            gViewportToastUntil = glfwGetTime() + 2.0;
        }
        gPendingDelete.clear();
        gDoDelete = false;
    }
}

static bool LoadImageWIC(const wchar_t* path, UINT& outW, UINT& outH, std::vector<unsigned char>& outPixelsBGRA)
{
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    (void)hr;

    auto Cleanup = [&]()
    {
        if (converter) converter->Release();
        if (frame) frame->Release();
        if (decoder) decoder->Release();
        if (factory) factory->Release();
    };

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory))))
        return false;

    if (FAILED(factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder)))
    {
        Cleanup();
        return false;
    }

    if (FAILED(decoder->GetFrame(0, &frame)))
    {
        Cleanup();
        return false;
    }

    if (FAILED(factory->CreateFormatConverter(&converter)))
    {
        Cleanup();
        return false;
    }

    if (FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
    {
        Cleanup();
        return false;
    }

    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    std::vector<unsigned char> pixels(w * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, (UINT)pixels.size(), pixels.data())))
    {
        Cleanup();
        return false;
    }

    outW = w;
    outH = h;
    outPixelsBGRA = std::move(pixels);
    Cleanup();
    return true;
}

static GLuint LoadTextureWIC(const wchar_t* path)
{
    UINT w = 0, h = 0;
    std::vector<unsigned char> pixels;
    if (!LoadImageWIC(path, w, h, pixels))
        return 0;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, pixels.data());

    return tex;
}

static bool SaveVmuMonoPng(const std::filesystem::path& outPath, const std::array<uint8_t, 48 * 32>& mono)
{
#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* props = nullptr;

    auto Cleanup = [&]() {
        if (props) props->Release();
        if (frame) frame->Release();
        if (encoder) encoder->Release();
        if (stream) stream->Release();
        if (factory) factory->Release();
        if (SUCCEEDED(hr)) CoUninitialize();
    };

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
    {
        Cleanup();
        return false;
    }
    if (FAILED(factory->CreateStream(&stream)))
    {
        Cleanup();
        return false;
    }

    std::wstring wpath = outPath.wstring();
    if (FAILED(stream->InitializeFromFilename(wpath.c_str(), GENERIC_WRITE)))
    {
        Cleanup();
        return false;
    }
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)))
    {
        Cleanup();
        return false;
    }
    if (FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache)))
    {
        Cleanup();
        return false;
    }
    if (FAILED(encoder->CreateNewFrame(&frame, &props)))
    {
        Cleanup();
        return false;
    }
    if (FAILED(frame->Initialize(props)))
    {
        Cleanup();
        return false;
    }

    UINT w = 48, h = 32;
    if (FAILED(frame->SetSize(w, h)))
    {
        Cleanup();
        return false;
    }
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&fmt)))
    {
        Cleanup();
        return false;
    }

    std::vector<uint8_t> bgra((size_t)w * (size_t)h * 4u, 255u);
    for (UINT y = 0; y < h; ++y)
    {
        for (UINT x = 0; x < w; ++x)
        {
            const size_t pi = ((size_t)y * (size_t)w + (size_t)x);
            const uint8_t on = mono[pi] ? 0u : 255u;
            const size_t i = pi * 4u;
            bgra[i + 0] = on;
            bgra[i + 1] = on;
            bgra[i + 2] = on;
            bgra[i + 3] = 255u;
        }
    }

    if (FAILED(frame->WritePixels(h, w * 4u, (UINT)bgra.size(), bgra.data())))
    {
        Cleanup();
        return false;
    }
    if (FAILED(frame->Commit()) || FAILED(encoder->Commit()))
    {
        Cleanup();
        return false;
    }

    Cleanup();
    return true;
#else
    (void)outPath;
    (void)mono;
    return false;
#endif
}

static bool ExportNebTexturePNG(const std::filesystem::path& pngPath, const std::filesystem::path& outPath, std::string& warning)
{
    UINT w = 0, h = 0;
    std::vector<unsigned char> pixels;
    std::wstring wpath = pngPath.wstring();
    if (!LoadImageWIC(wpath.c_str(), w, h, pixels))
        return false;

    std::ofstream out(outPath, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;

    const char magic[4] = { 'N','E','B','T' };
    uint16_t format = 1; // RGB555
    uint16_t flags = 0;
    out.write(magic, 4);
    WriteU16BE(out, (uint16_t)w);
    WriteU16BE(out, (uint16_t)h);
    WriteU16BE(out, format);
    WriteU16BE(out, flags);

    bool clampWarn = false;
    for (UINT i = 0; i < w * h; ++i)
    {
        uint8_t b = pixels[i * 4 + 0];
        uint8_t g = pixels[i * 4 + 1];
        uint8_t r = pixels[i * 4 + 2];
        uint16_t r5 = (uint16_t)(r >> 3);
        uint16_t g5 = (uint16_t)(g >> 3);
        uint16_t b5 = (uint16_t)(b >> 3);
        uint16_t rgb555 = (uint16_t)((r5 << 10) | (g5 << 5) | b5);
        WriteU16BE(out, rgb555);
    }

    if (clampWarn)
    {
        warning = "Color clamp";
    }
    return true;
}

static GLuint CreateCircleTexture(int size)
{
    std::vector<unsigned char> pixels(size * size * 4);
    float c = (size - 1) * 0.5f;
    float radius = c;
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            float dx = x - c;
            float dy = y - c;
            float d = sqrtf(dx * dx + dy * dy);
            float a = 1.0f - (d / radius);
            if (a < 0.0f) a = 0.0f;
            float alpha = a * a;
            int i = (y * size + x) * 4;
            pixels[i + 0] = 255;
            pixels[i + 1] = 255;
            pixels[i + 2] = 255;
            pixels[i + 3] = (unsigned char)(alpha * 255.0f);
        }
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return tex;
}

static Mat4 Mat4Identity()
{
    Mat4 r = {};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

static int RunCommand(const char* cmd)
{
    return NebulaDreamcastBuild::RunCommand(cmd);
}

static bool IsDiscImageFilePath(const std::filesystem::path& p)
{
    return NebulaDreamcastBuild::IsDiscImageFilePath(p);
}

static bool IsLikelySaturnImageBin(const std::filesystem::path& p)
{
    return NebulaDreamcastBuild::IsLikelySaturnImageBin(p);
}

static bool GenerateCueForBuild(const std::filesystem::path& buildDir, const std::filesystem::path& projectDir, std::filesystem::path& outCue)
{
    return NebulaDreamcastBuild::GenerateCueForBuild(buildDir, projectDir, outCue);
}

static std::string PickFolderDialog(const char* title)
{
#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileDialog* pfd = nullptr;
    std::string result;

    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))))
    {
        DWORD options = 0;
        pfd->GetOptions(&options);
        pfd->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        if (title) pfd->SetTitle(std::wstring(title, title + strlen(title)).c_str());

        if (SUCCEEDED(pfd->Show(nullptr)))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item)) && item)
            {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                {
                    int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
                    if (len > 0)
                    {
                        std::string utf8(len - 1, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), len, nullptr, nullptr);
                        result = utf8;
                    }
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        pfd->Release();
    }

    if (SUCCEEDED(hr)) CoUninitialize();
    return result;
#else
    return "";
#endif
}

static std::string PickProjectFileDialog(const char* title)
{
#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileDialog* pfd = nullptr;
    std::string result;

    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))))
    {
        DWORD options = 0;
        pfd->GetOptions(&options);
        pfd->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        if (title) pfd->SetTitle(std::wstring(title, title + strlen(title)).c_str());

        COMDLG_FILTERSPEC filters[] = { { L"Nebula Project", L"*.nebproj" } };
        pfd->SetFileTypes(1, filters);
        pfd->SetDefaultExtension(L"nebproj");

        if (SUCCEEDED(pfd->Show(nullptr)))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item)) && item)
            {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                {
                    int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
                    if (len > 0)
                    {
                        std::string utf8(len - 1, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), len, nullptr, nullptr);
                        result = utf8;
                    }
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        pfd->Release();
    }

    if (SUCCEEDED(hr)) CoUninitialize();
    return result;
#else
    return "";
#endif
}

static std::string PickFbxFileDialog(const char* title)
{
#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileDialog* pfd = nullptr;
    std::string result;

    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))))
    {
        DWORD options = 0;
        pfd->GetOptions(&options);
        pfd->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        if (title) pfd->SetTitle(std::wstring(title, title + strlen(title)).c_str());

        COMDLG_FILTERSPEC filters[] = {
            { L"FBX Files", L"*.fbx" },
            { L"All Files", L"*.*" }
        };
        pfd->SetFileTypes(2, filters);
        pfd->SetDefaultExtension(L"fbx");

        if (SUCCEEDED(pfd->Show(nullptr)))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item)) && item)
            {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                {
                    int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
                    if (len > 0)
                    {
                        std::string utf8(len - 1, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), len, nullptr, nullptr);
                        result = utf8;
                    }
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        pfd->Release();
    }

    if (SUCCEEDED(hr)) CoUninitialize();
    return result;
#else
    return "";
#endif
}

static std::string PickPngFileDialog(const char* title)
{
#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileDialog* pfd = nullptr;
    std::string result;

    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))))
    {
        DWORD options = 0;
        pfd->GetOptions(&options);
        pfd->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        if (title) pfd->SetTitle(std::wstring(title, title + strlen(title)).c_str());

        COMDLG_FILTERSPEC filters[] = {
            { L"PNG Files", L"*.png" },
            { L"All Files", L"*.*" }
        };
        pfd->SetFileTypes(2, filters);
        pfd->SetDefaultExtension(L"png");

        if (SUCCEEDED(pfd->Show(nullptr)))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item)) && item)
            {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                {
                    int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
                    if (len > 0)
                    {
                        std::string utf8(len - 1, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), len, nullptr, nullptr);
                        result = utf8;
                    }
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        pfd->Release();
    }

    if (SUCCEEDED(hr)) CoUninitialize();
    return result;
#else
    return "";
#endif
}

static bool LoadVmuPngToMono(const std::string& path, std::string& outErr)
{
    UINT iw = 0, ih = 0;
    std::vector<unsigned char> bgra;
    std::wstring w = std::filesystem::path(path).wstring();
    if (!LoadImageWIC(w.c_str(), iw, ih, bgra))
    {
        outErr = "VMU Tool: failed to load PNG";
        return false;
    }
    if (iw != 48 || ih != 32)
    {
        outErr = "VMU Tool: PNG must be exactly 48x32";
        return false;
    }

    for (UINT y = 0; y < 32; ++y)
    {
        for (UINT x = 0; x < 48; ++x)
        {
            size_t i = ((size_t)y * (size_t)iw + (size_t)x) * 4u;
            unsigned char b = bgra[i + 0];
            unsigned char g = bgra[i + 1];
            unsigned char r = bgra[i + 2];
            unsigned char a = bgra[i + 3];
            unsigned char lum = (unsigned char)((30u * r + 59u * g + 11u * b) / 100u);
            gVmuMono[(size_t)y * 48u + (size_t)x] = (a > 127 && lum < 128) ? 1 : 0;
        }
    }

    gVmuHasImage = true;
    gVmuAssetPath = path;
    gVmuCurrentLoadedType = 1;
    return true;
}

static std::string PickVmuFrameDataDialog(const char* title)
{
#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileDialog* pfd = nullptr;
    std::string result;

    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))))
    {
        DWORD options = 0;
        pfd->GetOptions(&options);
        pfd->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        if (title) pfd->SetTitle(std::wstring(title, title + strlen(title)).c_str());

        COMDLG_FILTERSPEC filters[] = {
            { L"VMU Frame Data", L"*.vmuanim" },
            { L"All Files", L"*.*" }
        };
        pfd->SetFileTypes(2, filters);
        pfd->SetDefaultExtension(L"vmuanim");

        if (SUCCEEDED(pfd->Show(nullptr)))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item)) && item)
            {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                {
                    int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
                    if (len > 0)
                    {
                        std::string utf8(len - 1, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), len, nullptr, nullptr);
                        result = utf8;
                    }
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        pfd->Release();
    }

    if (SUCCEEDED(hr)) CoUninitialize();
    return result;
#else
    return "";
#endif
}

static bool SaveVmuFrameData(const std::filesystem::path& outPath)
{
    std::ofstream out(outPath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;
    out << "VMUANIM 1\n";
    out << "TOTAL_FRAMES\t" << gVmuAnimTotalFrames << "\n";
    out << "PLAYHEAD\t" << gVmuAnimPlayhead << "\n";
    out << "LOOP\t" << (gVmuAnimLoop ? 1 : 0) << "\n";
    out << "SPEED_MODE\t" << gVmuAnimSpeedMode << "\n";
    out << "LAYER_COUNT\t" << gVmuAnimLayers.size() << "\n";
    for (const auto& l : gVmuAnimLayers)
    {
        std::string nm = l.name;
        for (char& c : nm) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
        out << "LAYER\t" << nm << "\t" << (l.visible ? 1 : 0) << "\t" << l.frameStart << "\t" << l.frameEnd << "\t" << l.linkedAsset << "\n";
    }
    return true;
}

static bool LoadVmuFrameData(const std::filesystem::path& inPath)
{
    std::ifstream in(inPath);
    if (!in.is_open()) return false;

    std::vector<VmuAnimLayer> loadedLayers;
    int loadedFrames = 24;
    int loadedPlayhead = 0;
    int loadedLoop = 0;
    int loadedSpeedMode = 1;
    std::string line;
    bool headerOk = false;

    while (std::getline(in, line))
    {
        if (line.rfind("VMUANIM ", 0) == 0) { headerOk = true; continue; }
        if (line.rfind("TOTAL_FRAMES\t", 0) == 0) { loadedFrames = atoi(line.c_str() + 13); continue; }
        if (line.rfind("PLAYHEAD\t", 0) == 0) { loadedPlayhead = atoi(line.c_str() + 9); continue; }
        if (line.rfind("LOOP\t", 0) == 0) { loadedLoop = atoi(line.c_str() + 5); continue; }
        if (line.rfind("SPEED_MODE\t", 0) == 0) { loadedSpeedMode = atoi(line.c_str() + 11); continue; }
        if (line.rfind("LAYER\t", 0) == 0)
        {
            std::vector<std::string> parts;
            size_t start = 0;
            while (start <= line.size())
            {
                size_t p = line.find('\t', start);
                if (p == std::string::npos) { parts.push_back(line.substr(start)); break; }
                parts.push_back(line.substr(start, p - start));
                start = p + 1;
            }
            if (parts.size() >= 6)
            {
                VmuAnimLayer l;
                l.name = parts[1];
                l.visible = (atoi(parts[2].c_str()) != 0);
                l.frameStart = atoi(parts[3].c_str());
                l.frameEnd = atoi(parts[4].c_str());
                l.linkedAsset = parts[5];
                loadedLayers.push_back(l);
            }
        }
    }

    if (!headerOk) return false;
    if (loadedFrames < 1) loadedFrames = 1;
    if (loadedLayers.empty()) loadedLayers.push_back({ "Layer 1", true, 0, 0, "" });

    gVmuAnimTotalFrames = loadedFrames;
    gVmuAnimPlayhead = std::max(0, std::min(loadedPlayhead, gVmuAnimTotalFrames - 1));
    gVmuAnimLoop = (loadedLoop != 0);
    gVmuAnimSpeedMode = std::max(0, std::min(loadedSpeedMode, 2));
    gVmuAnimLayers = loadedLayers;
    gVmuAnimLayerSel = 0;
    gVmuCurrentLoadedType = 2;
    return true;
}

static std::vector<std::string> PickImportAssetDialog(const char* title)
{
#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileDialog* pfd = nullptr;
    std::vector<std::string> results;

    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))))
    {
        DWORD options = 0;
        pfd->GetOptions(&options);
        pfd->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_ALLOWMULTISELECT);
        if (title) pfd->SetTitle(std::wstring(title, title + strlen(title)).c_str());

        COMDLG_FILTERSPEC filters[] = {
            { L"FBX/VTXA/PNG", L"*.fbx;*.vtxa;*.png" },
            { L"FBX Files", L"*.fbx" },
            { L"VTXA Files", L"*.vtxa" },
            { L"PNG Files", L"*.png" },
            { L"All Files", L"*.*" }
        };
        pfd->SetFileTypes(5, filters);
        pfd->SetDefaultExtension(L"fbx");

        if (SUCCEEDED(pfd->Show(nullptr)))
        {
            IFileOpenDialog* pOpen = nullptr;
            if (SUCCEEDED(pfd->QueryInterface(IID_PPV_ARGS(&pOpen))) && pOpen)
            {
                IShellItemArray* items = nullptr;
                if (SUCCEEDED(pOpen->GetResults(&items)) && items)
                {
                    DWORD count = 0;
                    items->GetCount(&count);
                    for (DWORD i = 0; i < count; ++i)
                    {
                        IShellItem* item = nullptr;
                        if (SUCCEEDED(items->GetItemAt(i, &item)) && item)
                        {
                            PWSTR path = nullptr;
                            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                            {
                                int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
                                if (len > 0)
                                {
                                    std::string utf8(len - 1, '\0');
                                    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), len, nullptr, nullptr);
                                    results.push_back(utf8);
                                }
                                CoTaskMemFree(path);
                            }
                            item->Release();
                        }
                    }
                    items->Release();
                }
                pOpen->Release();
            }
        }
        pfd->Release();
    }

    if (SUCCEEDED(hr)) CoUninitialize();
    return results;
#else
    return {};
#endif
}

static Mat4 Mat4Multiply(const Mat4& a, const Mat4& b)
{
    Mat4 r = {};
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            r.m[row * 4 + col] =
                a.m[row * 4 + 0] * b.m[0 * 4 + col] +
                a.m[row * 4 + 1] * b.m[1 * 4 + col] +
                a.m[row * 4 + 2] * b.m[2 * 4 + col] +
                a.m[row * 4 + 3] * b.m[3 * 4 + col];
        }
    }
    return r;
}

static aiMatrix4x4 AiComposeTRS(const aiVector3D& t, const aiQuaternion& r, const aiVector3D& s)
{
    aiMatrix4x4 m = aiMatrix4x4(r.GetMatrix());
    m.a1 *= s.x; m.a2 *= s.x; m.a3 *= s.x;
    m.b1 *= s.y; m.b2 *= s.y; m.b3 *= s.y;
    m.c1 *= s.z; m.c2 *= s.z; m.c3 *= s.z;
    m.a4 = t.x;
    m.b4 = t.y;
    m.c4 = t.z;
    return m;
}

static aiVector3D AiInterpVec(const aiVector3D& a, const aiVector3D& b, double t)
{
    return a + (b - a) * (float)t;
}

static aiQuaternion AiInterpQuat(const aiQuaternion& a, const aiQuaternion& b, double t)
{
    aiQuaternion out;
    aiQuaternion::Interpolate(out, a, b, (float)t);
    out.Normalize();
    return out;
}

static const aiNodeAnim* AiFindChannel(const aiAnimation* anim, const aiString& name)
{
    if (!anim) return nullptr;
    for (unsigned int i = 0; i < anim->mNumChannels; ++i)
    {
        if (anim->mChannels[i]->mNodeName == name)
            return anim->mChannels[i];
    }
    return nullptr;
}

static aiVector3D AiSamplePosition(const aiNodeAnim* channel, double time)
{
    if (!channel || channel->mNumPositionKeys == 0) return aiVector3D(0, 0, 0);
    if (channel->mNumPositionKeys == 1) return channel->mPositionKeys[0].mValue;
    for (unsigned int i = 0; i + 1 < channel->mNumPositionKeys; ++i)
    {
        const auto& a = channel->mPositionKeys[i];
        const auto& b = channel->mPositionKeys[i + 1];
        if (time <= b.mTime)
        {
            double dt = (b.mTime - a.mTime);
            double t = (dt > 0.0) ? (time - a.mTime) / dt : 0.0;
            return AiInterpVec(a.mValue, b.mValue, t);
        }
    }
    return channel->mPositionKeys[channel->mNumPositionKeys - 1].mValue;
}

static aiVector3D AiSampleScale(const aiNodeAnim* channel, double time)
{
    if (!channel || channel->mNumScalingKeys == 0) return aiVector3D(1, 1, 1);
    if (channel->mNumScalingKeys == 1) return channel->mScalingKeys[0].mValue;
    for (unsigned int i = 0; i + 1 < channel->mNumScalingKeys; ++i)
    {
        const auto& a = channel->mScalingKeys[i];
        const auto& b = channel->mScalingKeys[i + 1];
        if (time <= b.mTime)
        {
            double dt = (b.mTime - a.mTime);
            double t = (dt > 0.0) ? (time - a.mTime) / dt : 0.0;
            return AiInterpVec(a.mValue, b.mValue, t);
        }
    }
    return channel->mScalingKeys[channel->mNumScalingKeys - 1].mValue;
}

static aiQuaternion AiSampleRotation(const aiNodeAnim* channel, double time)
{
    if (!channel || channel->mNumRotationKeys == 0) return aiQuaternion();
    if (channel->mNumRotationKeys == 1) return channel->mRotationKeys[0].mValue;
    for (unsigned int i = 0; i + 1 < channel->mNumRotationKeys; ++i)
    {
        const auto& a = channel->mRotationKeys[i];
        const auto& b = channel->mRotationKeys[i + 1];
        if (time <= b.mTime)
        {
            double dt = (b.mTime - a.mTime);
            double t = (dt > 0.0) ? (time - a.mTime) / dt : 0.0;
            return AiInterpQuat(a.mValue, b.mValue, t);
        }
    }
    return channel->mRotationKeys[channel->mNumRotationKeys - 1].mValue;
}

static aiMatrix4x4 AiNodeLocalAtTime(const aiNode* node, const aiAnimation* anim, double time)
{
    if (!node) return aiMatrix4x4();
    const aiNodeAnim* channel = AiFindChannel(anim, node->mName);
    if (!channel) return node->mTransformation;

    aiVector3D t = AiSamplePosition(channel, time);
    aiVector3D s = AiSampleScale(channel, time);
    aiQuaternion r = AiSampleRotation(channel, time);
    return AiComposeTRS(t, r, s);
}

static bool AiFindNodeGlobal(const aiNode* node, const aiAnimation* anim, double time, const aiMatrix4x4& parent, const aiNode* target, aiMatrix4x4& out)
{
    aiMatrix4x4 local = AiNodeLocalAtTime(node, anim, time);
    aiMatrix4x4 global = parent * local;
    if (node == target)
    {
        out = global;
        return true;
    }
    for (unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        if (AiFindNodeGlobal(node->mChildren[i], anim, time, global, target, out))
            return true;
    }
    return false;
}

static const aiNode* AiFindNodeWithMesh(const aiNode* node, unsigned int meshIndex)
{
    if (!node) return nullptr;
    for (unsigned int i = 0; i < node->mNumMeshes; ++i)
    {
        if (node->mMeshes[i] == meshIndex)
            return node;
    }
    for (unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        const aiNode* found = AiFindNodeWithMesh(node->mChildren[i], meshIndex);
        if (found) return found;
    }
    return nullptr;
}

static aiVector3D AiTransformPoint(const aiMatrix4x4& m, const aiVector3D& p)
{
    aiVector3D r;
    r.x = m.a1 * p.x + m.a2 * p.y + m.a3 * p.z + m.a4;
    r.y = m.b1 * p.x + m.b2 * p.y + m.b3 * p.z + m.b4;
    r.z = m.c1 * p.x + m.c2 * p.y + m.c3 * p.z + m.c4;
    return r;
}

static std::string SanitizeName(const std::string& name)
{
    std::string out;
    out.reserve(name.size());
    for (char c : name)
    {
        if (std::isalnum((unsigned char)c) || c == '_' || c == '-') out.push_back(c);
        else if (c == ' ') out.push_back('_');
    }
    if (out.empty()) out = "Anim";
    return out;
}

static void WriteU32BE(std::ofstream& out, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)((v >> 24) & 0xFF), (uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 8) & 0xFF), (uint8_t)(v & 0xFF) };
    out.write((const char*)b, 4);
}

static void WriteU16BE(std::ofstream& out, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)((v >> 8) & 0xFF), (uint8_t)(v & 0xFF) };
    out.write((const char*)b, 2);
}

static bool ReadU32BE(std::ifstream& in, uint32_t& outVal)
{
    uint8_t b[4];
    if (!in.read((char*)b, 4)) return false;
    outVal = (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | uint32_t(b[3]);
    return true;
}

static bool ReadU16BE(std::ifstream& in, uint16_t& outVal)
{
    uint8_t b[2];
    if (!in.read((char*)b, 2)) return false;
    outVal = (uint16_t(b[0]) << 8) | uint16_t(b[1]);
    return true;
}

static bool ReadS16BE(std::ifstream& in, int16_t& outVal)
{
    uint16_t v;
    if (!ReadU16BE(in, v)) return false;
    outVal = (int16_t)v;
    return true;
}

static void WriteS32BE(std::ofstream& out, int32_t v)
{
    uint8_t b[4] = { (uint8_t)((v >> 24) & 0xFF), (uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 8) & 0xFF), (uint8_t)(v & 0xFF) };
    out.write((const char*)b, 4);
}

static void WriteS16BE(std::ofstream& out, int16_t v)
{
    uint8_t b[2] = { (uint8_t)((v >> 8) & 0xFF), (uint8_t)(v & 0xFF) };
    out.write((const char*)b, 2);
}

static uint16_t ToU16Clamped(uint32_t v)
{
    return (v > 0xFFFFu) ? 0xFFFFu : (uint16_t)v;
}

static int16_t ToS16Fixed8_8(float v)
{
    float scaled = v * 256.0f;
    if (scaled > 32767.0f) scaled = 32767.0f;
    if (scaled < -32768.0f) scaled = -32768.0f;
    return (int16_t)std::lround(scaled);
}

static int32_t ToFixed16_16(float v)
{
    const float scale = 65536.0f;
    float clamped = std::fmax(std::fmin(v, 32767.0f), -32768.0f);
    return (int32_t)std::lround(clamped * scale);
}

static Vec3 ApplyImportBasis(const Vec3& v)
{
    switch (gImportBasisMode)
    {
    case 1: // Blender (-Z forward, Y up) -> Nebula basis (+ corrective -90ï¿½ X so imported mesh is upright)
        return Vec3{ v.z, v.y, -v.x };
    case 2: // Maya-style (+Z forward, Y up)
        return Vec3{ v.y, -v.x, v.z };
    default:
        return v;
    }
}

static uint8_t ComputeFaceWindingHint(const Vec3& a, const Vec3& b, const Vec3& c)
{
    // Sign of projected area on XY after import-basis transform.
    const float x1 = b.x - a.x;
    const float y1 = b.y - a.y;
    const float x2 = c.x - a.x;
    const float y2 = c.y - a.y;
    const float area2 = x1 * y2 - y1 * x2;
    return (area2 >= 0.0f) ? 1u : 0u;
}

static bool ExportNebMesh(const aiScene* scene, const std::filesystem::path& outPath, std::string& warning)
{
    if (!scene || scene->mNumMeshes == 0) return false;

    const uint32_t maxVerts = 2048;
    const uint32_t maxIndices = 65535;

    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    bool hasUv0 = false;
    std::vector<int> meshUvChannel(scene->mNumMeshes, -1);
    std::vector<aiMatrix4x4> meshGlobal(scene->mNumMeshes, aiMatrix4x4());
    for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
    {
        const aiMesh* mesh = scene->mMeshes[m];
        if (!mesh) continue;
        vertexCount += mesh->mNumVertices;
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
        {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices >= 3)
                indexCount += (face.mNumIndices - 2) * 3; // fan triangulation
        }
        int bestUvChannel = -1;
        float bestSpan = -1.0f;
        for (int ch = 0; ch < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++ch)
        {
            if (!(mesh->HasTextureCoords(ch) && mesh->mNumUVComponents[ch] >= 2)) continue;
            float minU = mesh->mTextureCoords[ch][0].x, maxU = minU;
            float minV = mesh->mTextureCoords[ch][0].y, maxV = minV;
            for (unsigned int v = 1; v < mesh->mNumVertices; ++v)
            {
                aiVector3D uv = mesh->mTextureCoords[ch][v];
                minU = std::min(minU, uv.x); maxU = std::max(maxU, uv.x);
                minV = std::min(minV, uv.y); maxV = std::max(maxV, uv.y);
            }
            float span = (maxU - minU) + (maxV - minV);
            if (span > bestSpan)
            {
                bestSpan = span;
                bestUvChannel = ch;
            }
        }
        meshUvChannel[m] = bestUvChannel;
        if (bestUvChannel >= 0) hasUv0 = true;

        const aiNode* meshNode = AiFindNodeWithMesh(scene->mRootNode, m);
        aiMatrix4x4 g;
        if (meshNode && AiFindNodeGlobal(scene->mRootNode, nullptr, 0.0, aiMatrix4x4(), meshNode, g))
            meshGlobal[m] = g;
        else
            meshGlobal[m] = aiMatrix4x4();
    }

    if (vertexCount > maxVerts)
        warning = "Vertex limit exceeded (" + std::to_string(vertexCount) + ">" + std::to_string(maxVerts) + ")";
    if (indexCount > maxIndices)
    {
        if (!warning.empty()) warning += "; ";
        warning += "Index limit exceeded (" + std::to_string(indexCount) + ">" + std::to_string(maxIndices) + ")";
    }

    std::ofstream out(outPath, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;

    const char magic[4] = { 'N','E','B','M' };
    uint32_t version = 5; // v5 adds canonical authored face records stream (optional via flags)
    uint32_t flags = 0u;
    if (hasUv0) flags |= 1u;      // bit0 = UV0
    flags |= 2u;                  // bit1 = face material index stream
    flags |= 4u;                  // bit2 = original face topology stream (vertex count per face)
    flags |= 8u;                  // bit3 = canonical face records stream
    uint32_t posFracBits = 8; // 8.8 fixed

    out.write(magic, 4);
    WriteU32BE(out, version);
    WriteU32BE(out, flags);
    WriteU32BE(out, vertexCount);
    WriteU32BE(out, indexCount);
    WriteU32BE(out, posFracBits);

    bool clampWarn = false;
    const float scale = (float)(1 << posFracBits);
    auto toPos = [&](float val) -> int16_t {
        float scaled = val * scale;
        if (scaled > 32767.0f) { clampWarn = true; scaled = 32767.0f; }
        if (scaled < -32768.0f) { clampWarn = true; scaled = -32768.0f; }
        return (int16_t)std::lround(scaled);
    };

    for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
    {
        const aiMesh* mesh = scene->mMeshes[m];
        if (!mesh) continue;
        for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
        {
            aiVector3D p = mesh->mVertices[v];
            Vec3 pv{ p.x, p.y, p.z };
            Vec3 tv = ApplyImportBasis(pv);
            WriteS16BE(out, toPos(tv.x));
            WriteS16BE(out, toPos(tv.y));
            WriteS16BE(out, toPos(tv.z));
        }
    }

    if (hasUv0)
    {
        for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
        {
            const aiMesh* mesh = scene->mMeshes[m];
            if (!mesh) continue;
            int uvCh = ((int)m < (int)meshUvChannel.size()) ? meshUvChannel[m] : -1;
            bool meshHasUv = (uvCh >= 0 && mesh->HasTextureCoords(uvCh) && mesh->mNumUVComponents[uvCh] >= 2);
            for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
            {
                aiVector3D uv = meshHasUv ? mesh->mTextureCoords[uvCh][v] : aiVector3D(0, 0, 0);
                WriteS16BE(out, ToS16Fixed8_8(uv.x));
                WriteS16BE(out, ToS16Fixed8_8(uv.y));
            }
        }
    }

    std::vector<uint16_t> faceMatStream;
    faceMatStream.reserve(indexCount / 3);
    std::vector<uint8_t> faceTopoStream;
    faceTopoStream.reserve(indexCount / 3);
    std::vector<NebFaceRecord> faceRecordStream;
    faceRecordStream.reserve(indexCount / 3);

    // Compact used FBX material indices into contiguous slot indices (0..N-1).
    std::vector<unsigned char> usedMat(scene->mNumMaterials, 0);
    for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
    {
        const aiMesh* mesh = scene->mMeshes[m];
        if (!mesh) continue;
        if (mesh->mMaterialIndex < scene->mNumMaterials)
            usedMat[mesh->mMaterialIndex] = 1;
    }
    std::vector<int> matToSlot(scene->mNumMaterials, -1);
    int nextSlot = 0;
    for (unsigned int mi = 0; mi < scene->mNumMaterials; ++mi)
        if (usedMat[mi]) matToSlot[mi] = nextSlot++;

    uint32_t baseVertex = 0;
    for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
    {
        const aiMesh* mesh = scene->mMeshes[m];
        if (!mesh) continue;
        unsigned int sourceMat = mesh->mMaterialIndex;
        uint16_t matIdx = 0;
        if (sourceMat < matToSlot.size() && matToSlot[sourceMat] >= 0)
            matIdx = (uint16_t)std::min<int>(matToSlot[sourceMat], 65535);
        else
            matIdx = (uint16_t)std::min<unsigned int>(sourceMat, 65535u);
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
        {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices < 3) continue;

            faceTopoStream.push_back((uint8_t)std::min<unsigned int>(face.mNumIndices, 255u));

            int uvCh = ((int)m < (int)meshUvChannel.size()) ? meshUvChannel[m] : -1;
            auto getUv = [&](unsigned int localIdx)->Vec3 {
                if (!(uvCh >= 0 && mesh->HasTextureCoords(uvCh) && mesh->mNumUVComponents[uvCh] >= 2)) return Vec3{ 0,0,0 };
                if (localIdx >= mesh->mNumVertices) return Vec3{ 0,0,0 };
                aiVector3D uv = mesh->mTextureCoords[uvCh][localIdx];
                return Vec3{ uv.x, uv.y, 0.0f };
            };
            auto getPos = [&](unsigned int localIdx)->Vec3 {
                if (localIdx >= mesh->mNumVertices) return Vec3{ 0,0,0 };
                aiVector3D p = mesh->mVertices[localIdx];
                return ApplyImportBasis(Vec3{ p.x, p.y, p.z });
            };

            if (face.mNumIndices == 3 || face.mNumIndices == 4)
            {
                NebFaceRecord rec{};
                rec.arity = (uint8_t)face.mNumIndices;
                rec.material = matIdx;
                for (unsigned int ci = 0; ci < face.mNumIndices; ++ci)
                {
                    unsigned int li = face.mIndices[ci];
                    rec.indices[ci] = ToU16Clamped(li + baseVertex);
                    rec.uvs[ci] = getUv(li);
                }
                Vec3 pa = getPos(face.mIndices[0]);
                Vec3 pb = getPos(face.mIndices[1]);
                Vec3 pc = getPos(face.mIndices[2]);
                rec.winding = ComputeFaceWindingHint(pa, pb, pc);
                faceRecordStream.push_back(rec);
            }
            else
            {
                // Saturn-safe subset: split n-gons into authored fan triangles and serialize explicitly.
                for (unsigned int i = 1; i + 1 < face.mNumIndices; ++i)
                {
                    NebFaceRecord rec{};
                    rec.arity = 3;
                    rec.material = matIdx;
                    unsigned int li0 = face.mIndices[0];
                    unsigned int li1 = face.mIndices[i];
                    unsigned int li2 = face.mIndices[i + 1];
                    rec.indices[0] = ToU16Clamped(li0 + baseVertex);
                    rec.indices[1] = ToU16Clamped(li1 + baseVertex);
                    rec.indices[2] = ToU16Clamped(li2 + baseVertex);
                    rec.uvs[0] = getUv(li0);
                    rec.uvs[1] = getUv(li1);
                    rec.uvs[2] = getUv(li2);
                    rec.winding = ComputeFaceWindingHint(getPos(li0), getPos(li1), getPos(li2));
                    faceRecordStream.push_back(rec);
                }
            }

            // Triangulate polygon face as fan: (0, i, i+1)
            for (unsigned int i = 1; i + 1 < face.mNumIndices; ++i)
            {
                WriteU16BE(out, ToU16Clamped(face.mIndices[0] + baseVertex));
                WriteU16BE(out, ToU16Clamped(face.mIndices[i] + baseVertex));
                WriteU16BE(out, ToU16Clamped(face.mIndices[i + 1] + baseVertex));
                faceMatStream.push_back(matIdx);
            }
        }
        baseVertex += mesh->mNumVertices;
    }

    // v3+ optional stream: one uint16 material index per triangle face.
    for (uint16_t fm : faceMatStream)
        WriteU16BE(out, fm);

    // v4 optional stream: source polygon arity for each original face.
    WriteU32BE(out, (uint32_t)faceTopoStream.size());
    if (!faceTopoStream.empty())
        out.write((const char*)faceTopoStream.data(), (std::streamsize)faceTopoStream.size());

    // v5 optional stream: canonical authored face records (tri/quad corner order + UVs + winding hint + material).
    WriteU32BE(out, (uint32_t)faceRecordStream.size());
    for (const auto& fr : faceRecordStream)
    {
        out.put((char)fr.arity);
        out.put((char)fr.winding);
        WriteU16BE(out, fr.material);
        for (int ci = 0; ci < 4; ++ci)
        {
            WriteU16BE(out, fr.indices[ci]);
            WriteS16BE(out, ToS16Fixed8_8(fr.uvs[ci].x));
            WriteS16BE(out, ToS16Fixed8_8(fr.uvs[ci].y));
        }
    }

    if (clampWarn)
    {
        if (!warning.empty()) warning += "; ";
        warning += "Position clamp (too large for 8.8)";
    }

    return true;
}

static void CleanupNebMeshTopology(NebMesh& mesh)
{
    if (mesh.positions.empty()) return;

    const float posEps = 1.0f / 512.0f;
    const float uvEps = 1.0f / 512.0f;
    const float posEps2 = posEps * posEps;

    auto quant = [](float v, float e) -> int32_t { return (int32_t)std::lround(v / e); };
    auto pack3 = [](int32_t x, int32_t y, int32_t z) -> uint64_t {
        uint64_t ux = (uint32_t)x;
        uint64_t uy = (uint32_t)y;
        uint64_t uz = (uint32_t)z;
        uint64_t h = 1469598103934665603ull;
        h ^= ux + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h ^= uy + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h ^= uz + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        return h;
    };

    std::unordered_map<uint64_t, std::vector<uint16_t>> buckets;
    buckets.reserve(mesh.positions.size() * 2 + 1);

    std::vector<Vec3> newPos;
    std::vector<Vec3> newUv;
    newPos.reserve(mesh.positions.size());
    if (mesh.hasUv && mesh.uvs.size() == mesh.positions.size()) newUv.reserve(mesh.uvs.size());

    std::vector<uint16_t> remap(mesh.positions.size(), 0);

    for (size_t i = 0; i < mesh.positions.size(); ++i)
    {
        const Vec3& p = mesh.positions[i];
        const int32_t qx = quant(p.x, posEps);
        const int32_t qy = quant(p.y, posEps);
        const int32_t qz = quant(p.z, posEps);
        const uint64_t key = pack3(qx, qy, qz);

        uint16_t chosen = 0xFFFFu;
        auto it = buckets.find(key);
        if (it != buckets.end())
        {
            for (uint16_t cand : it->second)
            {
                const Vec3& cp = newPos[cand];
                float dx = cp.x - p.x, dy = cp.y - p.y, dz = cp.z - p.z;
                if ((dx * dx + dy * dy + dz * dz) > posEps2) continue;

                if (mesh.hasUv && mesh.uvs.size() == mesh.positions.size() && cand < newUv.size())
                {
                    const Vec3& u0 = mesh.uvs[i];
                    const Vec3& u1 = newUv[cand];
                    if (std::fabs(u0.x - u1.x) > uvEps || std::fabs(u0.y - u1.y) > uvEps)
                        continue;
                }

                chosen = cand;
                break;
            }
        }

        if (chosen == 0xFFFFu)
        {
            chosen = (uint16_t)newPos.size();
            newPos.push_back(p);
            if (mesh.hasUv && mesh.uvs.size() == mesh.positions.size()) newUv.push_back(mesh.uvs[i]);
            buckets[key].push_back(chosen);
        }

        remap[i] = chosen;
    }

    if (!newPos.empty() && newPos.size() < mesh.positions.size())
    {
        mesh.positions.swap(newPos);
        if (mesh.hasUv && mesh.uvs.size() == remap.size()) mesh.uvs.swap(newUv);
    }

    for (size_t i = 0; i < mesh.indices.size(); ++i)
    {
        uint16_t idx = mesh.indices[i];
        if (idx < remap.size()) mesh.indices[i] = remap[idx];
    }

    if (mesh.hasFaceRecords)
    {
        for (auto& fr : mesh.faceRecords)
        {
            const int ar = (fr.arity >= 3 && fr.arity <= 4) ? (int)fr.arity : 3;
            for (int ci = 0; ci < ar; ++ci)
            {
                uint16_t idx = fr.indices[ci];
                if (idx < remap.size()) fr.indices[ci] = remap[idx];
            }
        }
    }

    // Remove exact duplicate/degenerate triangles after weld (preserve first occurrence).
    auto triKey = [](uint16_t a, uint16_t b, uint16_t c, uint16_t mat) -> uint64_t {
        if (a > b) std::swap(a, b);
        if (b > c) std::swap(b, c);
        if (a > b) std::swap(a, b);
        uint64_t h = 1469598103934665603ull;
        h ^= (uint64_t)a + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h ^= (uint64_t)b + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h ^= (uint64_t)c + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h ^= (uint64_t)mat + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        return h;
    };

    std::vector<uint16_t> newIdx;
    std::vector<uint16_t> newMat;
    newIdx.reserve(mesh.indices.size());
    if (mesh.hasFaceMaterial) newMat.reserve(mesh.faceMaterial.size());

    std::unordered_set<uint64_t> seenTri;
    seenTri.reserve(mesh.indices.size() / 3 + 1);

    const size_t triCount = mesh.indices.size() / 3;
    for (size_t t = 0; t < triCount; ++t)
    {
        uint16_t a = mesh.indices[t * 3 + 0];
        uint16_t b = mesh.indices[t * 3 + 1];
        uint16_t c = mesh.indices[t * 3 + 2];
        if (a == b || b == c || c == a) continue;

        uint16_t mat = (mesh.hasFaceMaterial && t < mesh.faceMaterial.size()) ? mesh.faceMaterial[t] : 0;
        uint64_t key = triKey(a, b, c, mat);
        if (!seenTri.insert(key).second) continue;

        newIdx.push_back(a); newIdx.push_back(b); newIdx.push_back(c);
        if (mesh.hasFaceMaterial) newMat.push_back(mat);
    }

    if (!newIdx.empty() && newIdx.size() < mesh.indices.size())
    {
        mesh.indices.swap(newIdx);
        if (mesh.hasFaceMaterial) mesh.faceMaterial.swap(newMat);
    }

    // Stabilize triangle stream ordering to reduce frame-to-frame draw conflicts.
    {
        const size_t triN = mesh.indices.size() / 3;
        if (triN > 1)
        {
            std::vector<size_t> order(triN);
            for (size_t i = 0; i < triN; ++i) order[i] = i;

            auto triSortKey = [&](size_t t) {
                uint16_t a = mesh.indices[t * 3 + 0];
                uint16_t b = mesh.indices[t * 3 + 1];
                uint16_t c = mesh.indices[t * 3 + 2];
                uint16_t m = (mesh.hasFaceMaterial && t < mesh.faceMaterial.size()) ? mesh.faceMaterial[t] : 0;
                uint16_t lo = std::min<uint16_t>(a, std::min<uint16_t>(b, c));
                uint16_t hi = std::max<uint16_t>(a, std::max<uint16_t>(b, c));
                uint16_t mid = (uint16_t)(a + b + c - lo - hi);
                return std::array<uint16_t, 4>{ m, lo, mid, hi };
            };

            std::stable_sort(order.begin(), order.end(), [&](size_t l, size_t r) {
                auto kl = triSortKey(l);
                auto kr = triSortKey(r);
                return kl < kr;
            });

            std::vector<uint16_t> sortedIdx;
            std::vector<uint16_t> sortedMat;
            sortedIdx.reserve(mesh.indices.size());
            if (mesh.hasFaceMaterial) sortedMat.reserve(mesh.faceMaterial.size());

            for (size_t oi = 0; oi < triN; ++oi)
            {
                size_t t = order[oi];
                sortedIdx.push_back(mesh.indices[t * 3 + 0]);
                sortedIdx.push_back(mesh.indices[t * 3 + 1]);
                sortedIdx.push_back(mesh.indices[t * 3 + 2]);
                if (mesh.hasFaceMaterial)
                {
                    uint16_t m = (t < mesh.faceMaterial.size()) ? mesh.faceMaterial[t] : 0;
                    sortedMat.push_back(m);
                }
            }

            mesh.indices.swap(sortedIdx);
            if (mesh.hasFaceMaterial) mesh.faceMaterial.swap(sortedMat);
        }
    }

    if (mesh.hasFaceRecords && !mesh.faceRecords.empty())
    {
        std::vector<NebFaceRecord> newRecs;
        newRecs.reserve(mesh.faceRecords.size());
        std::unordered_set<uint64_t> seenRecTri;
        seenRecTri.reserve(mesh.faceRecords.size() + 1);

        for (auto fr : mesh.faceRecords)
        {
            int ar = (fr.arity >= 3 && fr.arity <= 4) ? (int)fr.arity : 3;
            if (ar == 3)
            {
                uint16_t a = fr.indices[0], b = fr.indices[1], c = fr.indices[2];
                if (a == b || b == c || c == a) continue;
                uint64_t key = triKey(a, b, c, fr.material);
                if (!seenRecTri.insert(key).second) continue;
            }
            newRecs.push_back(fr);
        }

        if (!newRecs.empty() && newRecs.size() < mesh.faceRecords.size())
            mesh.faceRecords.swap(newRecs);
    }
}

static bool LoadNebMesh(const std::filesystem::path& path, NebMesh& outMesh)
{
    std::ifstream in(path, std::ios::binary | std::ios::in);
    if (!in.is_open()) return false;

    char magic[4];
    if (!in.read(magic, 4)) return false;
    if (!(magic[0] == 'N' && magic[1] == 'E' && magic[2] == 'B' && magic[3] == 'M')) return false;

    uint32_t version = 0, flags = 0, vertexCount = 0, indexCount = 0, posFracBits = 8;
    if (!ReadU32BE(in, version)) return false;
    if (!ReadU32BE(in, flags)) return false;
    if (!ReadU32BE(in, vertexCount)) return false;
    if (!ReadU32BE(in, indexCount)) return false;
    if (!ReadU32BE(in, posFracBits)) return false;

    outMesh.positions.resize(vertexCount);
    outMesh.uvs.clear();
    outMesh.indices.resize(indexCount);
    outMesh.faceMaterial.clear();
    outMesh.faceVertexCounts.clear();
    outMesh.faceRecords.clear();
    outMesh.hasUv = (flags & 1u) != 0;
    outMesh.hasFaceMaterial = (flags & 2u) != 0;
    outMesh.hasFaceTopology = (flags & 4u) != 0;
    outMesh.hasFaceRecords = (flags & 8u) != 0;

    const float invScale = 1.0f / (float)(1 << posFracBits);
    for (uint32_t v = 0; v < vertexCount; ++v)
    {
        int16_t x, y, z;
        if (!ReadS16BE(in, x) || !ReadS16BE(in, y) || !ReadS16BE(in, z)) return false;
        outMesh.positions[v] = { x * invScale, y * invScale, z * invScale };
    }

    if (outMesh.hasUv)
    {
        outMesh.uvs.resize(vertexCount);
        const float uvInv = 1.0f / 256.0f;
        for (uint32_t v = 0; v < vertexCount; ++v)
        {
            int16_t u, vcoord;
            if (!ReadS16BE(in, u) || !ReadS16BE(in, vcoord)) return false;
            outMesh.uvs[v] = { u * uvInv, vcoord * uvInv, 0.0f };
        }
    }

    for (uint32_t i = 0; i < indexCount; ++i)
    {
        uint16_t idx;
        if (!ReadU16BE(in, idx)) return false;
        outMesh.indices[i] = idx;
    }

    if (outMesh.hasFaceMaterial)
    {
        uint32_t triCount = indexCount / 3u;
        outMesh.faceMaterial.resize(triCount, 0);
        for (uint32_t t = 0; t < triCount; ++t)
        {
            uint16_t fm = 0;
            if (!ReadU16BE(in, fm)) { outMesh.faceMaterial.clear(); outMesh.hasFaceMaterial = false; break; }
            outMesh.faceMaterial[t] = fm;
        }
    }

    if (outMesh.hasFaceTopology)
    {
        uint32_t faceCount = 0;
        if (!ReadU32BE(in, faceCount))
        {
            outMesh.faceVertexCounts.clear();
            outMesh.hasFaceTopology = false;
        }
        else if (faceCount > 0)
        {
            outMesh.faceVertexCounts.resize(faceCount, 3);
            if (!in.read((char*)outMesh.faceVertexCounts.data(), (std::streamsize)faceCount))
            {
                outMesh.faceVertexCounts.clear();
                outMesh.hasFaceTopology = false;
            }
        }
    }

    if (outMesh.hasFaceRecords && in.peek() != EOF)
    {
        uint32_t recCount = 0;
        if (!ReadU32BE(in, recCount))
        {
            outMesh.faceRecords.clear();
            outMesh.hasFaceRecords = false;
        }
        else if (recCount > 0)
        {
            outMesh.faceRecords.resize(recCount);
            bool ok = true;
            const float uvInv = 1.0f / 256.0f;
            for (uint32_t ri = 0; ri < recCount && ok; ++ri)
            {
                int a = in.get();
                int w = in.get();
                if (a == EOF || w == EOF) { ok = false; break; }
                outMesh.faceRecords[ri].arity = (uint8_t)a;
                outMesh.faceRecords[ri].winding = (uint8_t)w;
                if (!ReadU16BE(in, outMesh.faceRecords[ri].material)) { ok = false; break; }
                for (int ci = 0; ci < 4; ++ci)
                {
                    int16_t u = 0, v = 0;
                    if (!ReadU16BE(in, outMesh.faceRecords[ri].indices[ci]) || !ReadS16BE(in, u) || !ReadS16BE(in, v))
                    {
                        ok = false;
                        break;
                    }
                    outMesh.faceRecords[ri].uvs[ci] = Vec3{ u * uvInv, v * uvInv, 0.0f };
                }
            }
            if (!ok)
            {
                outMesh.faceRecords.clear();
                outMesh.hasFaceRecords = false;
            }
        }
    }

    CleanupNebMeshTopology(outMesh);

    outMesh.valid = true;
    return true;
}

static const NebMesh* GetNebMesh(const std::filesystem::path& path)
{
    std::string key = path.string();
    auto it = gNebMeshCache.find(key);
    if (it != gNebMeshCache.end()) return &it->second;

    NebMesh mesh;
    if (!LoadNebMesh(path, mesh))
    {
        mesh.valid = false;
    }
    gNebMeshCache[key] = std::move(mesh);
    return &gNebMeshCache[key];
}

static GLuint LoadNebTexture(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::in);
    if (!in.is_open()) return 0;

    char magic[4];
    if (!in.read(magic, 4)) return 0;
    if (!(magic[0] == 'N' && magic[1] == 'E' && magic[2] == 'B' && magic[3] == 'T')) return 0;

    uint16_t w = 0, h = 0, format = 0, flags = 0;
    if (!ReadU16BE(in, w) || !ReadU16BE(in, h) || !ReadU16BE(in, format) || !ReadU16BE(in, flags)) return 0;
    if (format != 1) return 0; // RGB555

    std::vector<unsigned char> rgba(w * h * 4);
    for (uint32_t i = 0; i < (uint32_t)w * (uint32_t)h; ++i)
    {
        uint16_t rgb;
        if (!ReadU16BE(in, rgb)) return 0;
        uint8_t r5 = (rgb >> 10) & 0x1F;
        uint8_t g5 = (rgb >> 5) & 0x1F;
        uint8_t b5 = (rgb) & 0x1F;
        uint8_t r = (r5 << 3) | (r5 >> 2);
        uint8_t g = (g5 << 3) | (g5 >> 2);
        uint8_t b = (b5 << 3) | (b5 >> 2);
        rgba[i * 4 + 0] = r;
        rgba[i * 4 + 1] = g;
        rgba[i * 4 + 2] = b;
        rgba[i * 4 + 3] = 255;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    int filterMode = LoadNebTexFilterMode(path);
    GLint glFilter = (filterMode == 0) ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glFilter);
    int wrapMode = LoadNebTexWrapMode(path);
    GLint wrapS = GL_REPEAT;
    GLint wrapT = GL_REPEAT;
    if (wrapMode == 1 || wrapMode == 2)
    {
        wrapS = GL_CLAMP;
        wrapT = GL_CLAMP;
    }
    else if (wrapMode == 3)
    {
#ifdef GL_MIRRORED_REPEAT
        wrapS = GL_MIRRORED_REPEAT;
        wrapT = GL_MIRRORED_REPEAT;
#else
        wrapS = GL_REPEAT;
        wrapT = GL_REPEAT;
#endif
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    return tex;
}

static bool IsPowerOfTwoU16(uint16_t v)
{
    return v != 0 && (v & (v - 1)) == 0;
}

static uint16_t NextPow2U16(uint16_t v)
{
    if (v <= 1) return 1;
    uint16_t p = 1;
    while (p < v && p < 32768) p = (uint16_t)(p << 1);
    return p;
}

static bool WriteTga24TopLeft(const std::filesystem::path& outPath, uint16_t w, uint16_t h, const std::vector<unsigned char>& bgr)
{
    std::ofstream out(outPath, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;

    unsigned char hdr[18] = {};
    hdr[2] = 2; // uncompressed true-color
    hdr[12] = (unsigned char)(w & 0xFF);
    hdr[13] = (unsigned char)((w >> 8) & 0xFF);
    hdr[14] = (unsigned char)(h & 0xFF);
    hdr[15] = (unsigned char)((h >> 8) & 0xFF);
    hdr[16] = 24;
    hdr[17] = 0x20; // top-left origin
    out.write((const char*)hdr, 18);
    out.write((const char*)bgr.data(), (std::streamsize)bgr.size());
    return true;
}

static bool GetNebTexSaturnPadUvScale(const std::filesystem::path& nebtexPath, float& outU, float& outV)
{
    outU = 1.0f;
    outV = 1.0f;
    std::ifstream in(nebtexPath, std::ios::binary | std::ios::in);
    if (!in.is_open()) return false;

    char magic[4];
    if (!in.read(magic, 4)) return false;
    if (!(magic[0] == 'N' && magic[1] == 'E' && magic[2] == 'B' && magic[3] == 'T')) return false;

    uint16_t w = 0, h = 0, format = 0, flags = 0;
    if (!ReadU16BE(in, w) || !ReadU16BE(in, h) || !ReadU16BE(in, format) || !ReadU16BE(in, flags)) return false;
    if (w == 0 || h == 0) return false;

    if (LoadNebTexSaturnNpotMode(nebtexPath) != 0)
        return true; // resample mode keeps full normalized UV domain

    uint16_t outW = std::max<uint16_t>(8, NextPow2U16(w));
    uint16_t outH = std::max<uint16_t>(8, NextPow2U16(h));
    if (outW == 0 || outH == 0) return false;

    outU = (float)w / (float)outW;
    outV = (float)h / (float)outH;
    return true;
}

static bool ConvertNebTexToTga24(const std::filesystem::path& nebtexPath, const std::filesystem::path& tgaOutPath, std::string& warn)
{
    std::ifstream in(nebtexPath, std::ios::binary | std::ios::in);
    if (!in.is_open()) return false;

    char magic[4];
    if (!in.read(magic, 4)) return false;
    if (!(magic[0] == 'N' && magic[1] == 'E' && magic[2] == 'B' && magic[3] == 'T')) return false;

    uint16_t w = 0, h = 0, format = 0, flags = 0;
    if (!ReadU16BE(in, w) || !ReadU16BE(in, h) || !ReadU16BE(in, format) || !ReadU16BE(in, flags)) return false;
    if (format != 1) return false;

    if (w > 256 || h > 256)
    {
        warn = "Saturn texture constraint warning: texture exceeds 256x256 and cannot be auto-padded safely.";
        return false;
    }

    std::vector<unsigned char> bgr((size_t)w * (size_t)h * 3);
    for (uint32_t i = 0; i < (uint32_t)w * (uint32_t)h; ++i)
    {
        uint16_t rgb;
        if (!ReadU16BE(in, rgb)) return false;
        uint8_t r5 = (rgb >> 10) & 0x1F;
        uint8_t g5 = (rgb >> 5) & 0x1F;
        uint8_t b5 = (rgb) & 0x1F;
        uint8_t r = (r5 << 3) | (r5 >> 2);
        uint8_t g = (g5 << 3) | (g5 >> 2);
        uint8_t b = (b5 << 3) | (b5 >> 2);
        bgr[i * 3 + 0] = b;
        bgr[i * 3 + 1] = g;
        bgr[i * 3 + 2] = r;
    }

    // JO-safe pad during Saturn packaging: enforce >=8, power-of-two, <=256.
    uint16_t outW = std::max<uint16_t>(8, NextPow2U16(w));
    uint16_t outH = std::max<uint16_t>(8, NextPow2U16(h));
    if (outW > 256 || outH > 256)
    {
        warn = "Saturn texture constraint warning: texture cannot be padded to JO-safe size within 256x256.";
        return false;
    }

    if (outW != w || outH != h)
    {
        int npotMode = LoadNebTexSaturnNpotMode(nebtexPath); // 0=pad, 1=resample
        if (npotMode == 0)
        {
            std::vector<unsigned char> padded((size_t)outW * (size_t)outH * 3, 0);
            for (uint16_t y = 0; y < h; ++y)
            {
                const unsigned char* src = &bgr[(size_t)y * (size_t)w * 3];
                unsigned char* dst = &padded[(size_t)y * (size_t)outW * 3];
                memcpy(dst, src, (size_t)w * 3);
            }
            bgr.swap(padded);
            if (!warn.empty()) warn += " | ";
            warn += "Auto-padded texture for Saturn JO constraints: " + std::to_string(w) + "x" + std::to_string(h) + " -> " + std::to_string(outW) + "x" + std::to_string(outH);
        }
        else
        {
            std::vector<unsigned char> resized((size_t)outW * (size_t)outH * 3, 0);
            for (uint16_t y = 0; y < outH; ++y)
            {
                uint16_t sy = (uint16_t)((uint32_t)y * (uint32_t)h / (uint32_t)outH);
                if (sy >= h) sy = (uint16_t)(h - 1);
                for (uint16_t x = 0; x < outW; ++x)
                {
                    uint16_t sx = (uint16_t)((uint32_t)x * (uint32_t)w / (uint32_t)outW);
                    if (sx >= w) sx = (uint16_t)(w - 1);
                    const unsigned char* src = &bgr[((size_t)sy * (size_t)w + (size_t)sx) * 3];
                    unsigned char* dst = &resized[((size_t)y * (size_t)outW + (size_t)x) * 3];
                    dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
                }
            }
            bgr.swap(resized);
            if (!warn.empty()) warn += " | ";
            warn += "Auto-resized texture for Saturn JO constraints: " + std::to_string(w) + "x" + std::to_string(h) + " -> " + std::to_string(outW) + "x" + std::to_string(outH);
        }
    }

    return WriteTga24TopLeft(tgaOutPath, outW, outH, bgr);
}

static bool ConvertTgaToJoSafeTga24(const std::filesystem::path& tgaInPath, const std::filesystem::path& tgaOutPath, std::string& warn)
{
    std::ifstream in(tgaInPath, std::ios::binary | std::ios::in);
    if (!in.is_open()) return false;

    unsigned char hdr[18] = {};
    if (!in.read((char*)hdr, 18)) return false;
    const uint8_t idLen = hdr[0];
    const uint8_t imageType = hdr[2];
    const uint16_t w = (uint16_t)hdr[12] | ((uint16_t)hdr[13] << 8);
    const uint16_t h = (uint16_t)hdr[14] | ((uint16_t)hdr[15] << 8);
    const uint8_t bpp = hdr[16];
    const uint8_t desc = hdr[17];

    if (imageType != 2 || (bpp != 24 && bpp != 32))
    {
        warn = "Saturn texture constraint warning: only uncompressed 24/32-bit TGA is supported for staging.";
        return false;
    }
    if (w == 0 || h == 0 || w > 256 || h > 256)
    {
        warn = "Saturn texture constraint warning: TGA dimensions must be within 1..256 for staging.";
        return false;
    }

    if (idLen > 0) in.seekg(idLen, std::ios::cur);

    const size_t srcStride = (size_t)(bpp / 8) * (size_t)w;
    std::vector<unsigned char> src((size_t)h * srcStride);
    if (!in.read((char*)src.data(), (std::streamsize)src.size())) return false;

    std::vector<unsigned char> bgr((size_t)w * (size_t)h * 3);
    const bool originTop = (desc & 0x20) != 0;
    for (uint16_t y = 0; y < h; ++y)
    {
        uint16_t sy = originTop ? y : (uint16_t)(h - 1 - y);
        const unsigned char* srow = &src[(size_t)sy * srcStride];
        unsigned char* drow = &bgr[(size_t)y * (size_t)w * 3];
        for (uint16_t x = 0; x < w; ++x)
        {
            drow[(size_t)x * 3 + 0] = srow[(size_t)x * (bpp / 8) + 0];
            drow[(size_t)x * 3 + 1] = srow[(size_t)x * (bpp / 8) + 1];
            drow[(size_t)x * 3 + 2] = srow[(size_t)x * (bpp / 8) + 2];
        }
    }

    uint16_t outW = std::max<uint16_t>(8, NextPow2U16(w));
    uint16_t outH = std::max<uint16_t>(8, NextPow2U16(h));
    if (outW > 256 || outH > 256)
    {
        warn = "Saturn texture constraint warning: TGA cannot be padded to JO-safe size within 256x256.";
        return false;
    }

    if (outW != w || outH != h)
    {
        std::vector<unsigned char> resized((size_t)outW * (size_t)outH * 3, 0);
        for (uint16_t y = 0; y < outH; ++y)
        {
            uint16_t sy = (uint16_t)((uint32_t)y * (uint32_t)h / (uint32_t)outH);
            if (sy >= h) sy = (uint16_t)(h - 1);
            for (uint16_t x = 0; x < outW; ++x)
            {
                uint16_t sx = (uint16_t)((uint32_t)x * (uint32_t)w / (uint32_t)outW);
                if (sx >= w) sx = (uint16_t)(w - 1);
                const unsigned char* src = &bgr[((size_t)sy * (size_t)w + (size_t)sx) * 3];
                unsigned char* dst = &resized[((size_t)y * (size_t)outW + (size_t)x) * 3];
                dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
            }
        }
        bgr.swap(resized);
        if (!warn.empty()) warn += " | ";
        warn += "Auto-resized TGA for Saturn JO constraints: " + std::to_string(w) + "x" + std::to_string(h) + " -> " + std::to_string(outW) + "x" + std::to_string(outH);
    }

    return WriteTga24TopLeft(tgaOutPath, outW, outH, bgr);
}

static GLuint GetNebTexture(const std::filesystem::path& path)
{
    std::string key = path.string();
    auto it = gNebTextureCache.find(key);
    if (it != gNebTextureCache.end()) return it->second;
    GLuint tex = LoadNebTexture(path);
    gNebTextureCache[key] = tex;
    return tex;
}

static bool EnsureDefaultCubeNebmesh(const std::filesystem::path& projectDir)
{
    if (projectDir.empty()) return false;
    std::filesystem::path assetsDir = projectDir / "Assets";
    std::filesystem::path fbx = assetsDir / "cube_primitive.fbx";
    std::filesystem::path nebmesh = assetsDir / "cube_primitive.nebmesh";
    if (std::filesystem::exists(nebmesh)) return true;
    if (!std::filesystem::exists(fbx)) return false;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(fbx.string(), aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);
    if (!scene) return false;
    std::string warn;
    return ExportNebMesh(scene, nebmesh, warn);
}

/* Camera helper model path logic removed; reverted to original built-in helper marker. */
/*static bool EnsureCameraHelperNebmesh(const std::filesystem::path& projectDir)
{
    std::vector<std::filesystem::path> assetRoots;
    if (!projectDir.empty()) assetRoots.push_back(projectDir / "Assets");

    std::filesystem::path cwd = std::filesystem::current_path();
    assetRoots.push_back(cwd / "assets");
    assetRoots.push_back(cwd / "Assets");
    assetRoots.push_back(cwd / ".." / "assets");
    assetRoots.push_back(cwd / ".." / "Assets");
    assetRoots.push_back(cwd / ".." / ".." / "assets");
    assetRoots.push_back(cwd / ".." / ".." / "Assets");
    assetRoots.push_back(cwd / ".." / ".." / ".." / "assets");
    assetRoots.push_back(cwd / ".." / ".." / ".." / "Assets");

    std::filesystem::path fbx;
    std::filesystem::path nebmesh;

    for (const auto& root : assetRoots)
    {
        std::filesystem::path n = root / "camera3d_helper.nebmesh";
        if (std::filesystem::exists(n))
            return true;

        std::filesystem::path f = root / "camera3d_helper.fbx";
        if (fbx.empty() && std::filesystem::exists(f))
        {
            fbx = f;
            nebmesh = n;
        }
    }

    if (fbx.empty()) return false;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(fbx.string(), aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);
    if (!scene) return false;
    std::string warn;
    return ExportNebMesh(scene, nebmesh, warn);
}
*/

static bool ExportNebAnimation(const aiScene* scene, const aiAnimation* anim, const std::filesystem::path& outPath, std::string& warning, bool deltaCompress)
{
    if (!scene || !anim || scene->mNumMeshes == 0) return false;

    const aiMesh* mesh = scene->mMeshes[0];
    const aiNode* meshNode = AiFindNodeWithMesh(scene->mRootNode, 0);
    if (!meshNode) meshNode = scene->mRootNode;

    double tps = anim->mTicksPerSecond != 0.0 ? anim->mTicksPerSecond : 24.0;
    double durationTicks = anim->mDuration;
    double durationSec = (tps > 0.0) ? (durationTicks / tps) : 0.0;
    const float fps = 12.0f;
    unsigned int frameCount = (unsigned int)std::max(1.0, std::floor(durationSec * fps + 0.5) + 1.0);

    const uint32_t maxVerts = 2048;
    const uint32_t maxFrames = 300;
    if (mesh->mNumVertices > maxVerts)
        warning = "Vertex limit exceeded (" + std::to_string(mesh->mNumVertices) + ">" + std::to_string(maxVerts) + ")";
    if (frameCount > maxFrames)
    {
        if (!warning.empty()) warning += "; ";
        warning += "Frame limit exceeded (" + std::to_string(frameCount) + ">" + std::to_string(maxFrames) + ")";
    }

    std::ofstream out(outPath, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;

    const char magic[4] = { 'N','E','B','0' };
    uint32_t version = deltaCompress ? 3 : 2;
    uint32_t flags = deltaCompress ? 1u : 0u;
    uint32_t vertexCount = mesh->mNumVertices;
    uint32_t frames = frameCount;
    uint32_t fpsFixed = ToFixed16_16(fps);
    const uint32_t deltaFracBits = 8;

    out.write(magic, 4);
    WriteU32BE(out, version);
    if (version >= 3) WriteU32BE(out, flags);
    WriteU32BE(out, vertexCount);
    WriteU32BE(out, frames);
    WriteU32BE(out, fpsFixed);
    if (version >= 3) WriteU32BE(out, deltaFracBits);

    aiMatrix4x4 identity;
    std::vector<aiVector3D> prev;
    prev.resize(mesh->mNumVertices);
    bool anyDeltaClamp = false;

    for (unsigned int f = 0; f < frameCount; ++f)
    {
        double timeSec = (double)f / (double)fps;
        double timeTicks = timeSec * tps;
        if (timeTicks > durationTicks) timeTicks = durationTicks;

        aiMatrix4x4 global;
        AiFindNodeGlobal(scene->mRootNode, anim, timeTicks, identity, meshNode, global);

        for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
        {
            aiVector3D p = mesh->mVertices[v];
            aiVector3D tp = global * p;
            Vec3 tv = ApplyImportBasis(Vec3{ tp.x, tp.y, tp.z });
            aiVector3D ta(tv.x, tv.y, tv.z);

            if (!deltaCompress || f == 0)
            {
                WriteS32BE(out, ToFixed16_16(ta.x));
                WriteS32BE(out, ToFixed16_16(ta.y));
                WriteS32BE(out, ToFixed16_16(ta.z));
            }
            else
            {
                aiVector3D d = ta - prev[v];
                float scale = (float)(1 << deltaFracBits);
                auto toDelta = [&](float val) -> int16_t {
                    float scaled = val * scale;
                    if (scaled > 32767.0f) { anyDeltaClamp = true; scaled = 32767.0f; }
                    if (scaled < -32768.0f) { anyDeltaClamp = true; scaled = -32768.0f; }
                    return (int16_t)std::lround(scaled);
                };
                WriteS16BE(out, toDelta(d.x));
                WriteS16BE(out, toDelta(d.y));
                WriteS16BE(out, toDelta(d.z));
            }
            prev[v] = ta;
        }
    }

    if (anyDeltaClamp)
    {
        if (!warning.empty()) warning += "; ";
        warning += "Delta clamp (movement too large)";
    }

    return true;
}

static Mat4 Mat4Perspective(float fovyRadians, float aspect, float znear, float zfar)
{
    float f = 1.0f / tanf(fovyRadians * 0.5f);
    Mat4 r = {};
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zfar + znear) / (znear - zfar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zfar * znear) / (znear - zfar);
    return r;
}

static Mat4 Mat4Orthographic(float left, float right, float bottom, float top, float znear, float zfar)
{
    Mat4 r = Mat4Identity();
    r.m[0] = 2.0f / (right - left);
    r.m[5] = 2.0f / (top - bottom);
    r.m[10] = -2.0f / (zfar - znear);
    r.m[12] = -(right + left) / (right - left);
    r.m[13] = -(top + bottom) / (top - bottom);
    r.m[14] = -(zfar + znear) / (zfar - znear);
    return r;
}

static Mat4 Mat4LookAt(const Vec3& eye, const Vec3& target, const Vec3& up)
{
    return NebulaCamera3D::BuildViewMatrix(NebulaCamera3D::BuildLookAtView(eye, target, up));
}

static void MulMat4Vec4(const Mat4& m, float x, float y, float z, float w, float& ox, float& oy, float& oz, float& ow)
{
    // Column-major (OpenGL)
    ox = m.m[0] * x + m.m[4] * y + m.m[8]  * z + m.m[12] * w;
    oy = m.m[1] * x + m.m[5] * y + m.m[9]  * z + m.m[13] * w;
    oz = m.m[2] * x + m.m[6] * y + m.m[10] * z + m.m[14] * w;
    ow = m.m[3] * x + m.m[7] * y + m.m[11] * z + m.m[15] * w;
}

static bool ProjectToScreen(const Vec3& world, const Mat4& view, const Mat4& proj, int w, int h, float& outX, float& outY)
{
    float vx, vy, vz, vw;
    MulMat4Vec4(view, world.x, world.y, world.z, 1.0f, vx, vy, vz, vw);
    float px, py, pz, pw;
    MulMat4Vec4(proj, vx, vy, vz, 1.0f, px, py, pz, pw);
    if (pw == 0.0f) return false;
    float ndcX = px / pw;
    float ndcY = py / pw;
    float ndcZ = pz / pw;
    if (ndcZ < -1.0f || ndcZ > 1.0f) return false;
    outX = (ndcX * 0.5f + 0.5f) * (float)w;
    outY = (1.0f - (ndcY * 0.5f + 0.5f)) * (float)h;
    return true;
}

static bool ProjectToScreenGL(const Vec3& world, float& outX, float& outY, float scaleX, float scaleY)
{
    float model[16], proj[16];
    int vp[4];
    glGetFloatv(GL_MODELVIEW_MATRIX, model);
    glGetFloatv(GL_PROJECTION_MATRIX, proj);
    glGetIntegerv(GL_VIEWPORT, vp);

    // Multiply proj * model * vec (column-major)
    float x = world.x, y = world.y, z = world.z;
    float mvx = model[0]*x + model[4]*y + model[8]*z + model[12];
    float mvy = model[1]*x + model[5]*y + model[9]*z + model[13];
    float mvz = model[2]*x + model[6]*y + model[10]*z + model[14];
    float mvw = model[3]*x + model[7]*y + model[11]*z + model[15];

    float px = proj[0]*mvx + proj[4]*mvy + proj[8]*mvz + proj[12]*mvw;
    float py = proj[1]*mvx + proj[5]*mvy + proj[9]*mvz + proj[13]*mvw;
    float pz = proj[2]*mvx + proj[6]*mvy + proj[10]*mvz + proj[14]*mvw;
    float pw = proj[3]*mvx + proj[7]*mvy + proj[11]*mvz + proj[15]*mvw;

    if (pw == 0.0f) return false;
    float ndcX = px / pw;
    float ndcY = py / pw;
    float ndcZ = pz / pw;
    if (ndcZ < -1.0f || ndcZ > 1.0f) return false;

    float sx = vp[0] + (ndcX * 0.5f + 0.5f) * (float)vp[2];
    float sy = vp[1] + (1.0f - (ndcY * 0.5f + 0.5f)) * (float)vp[3];

    // Convert framebuffer pixels to ImGui screen coords
    outX = sx / scaleX;
    outY = sy / scaleY;
    return true;
}

static void GetLocalAxesFromEuler(float rotX, float rotY, float rotZ, Vec3& right, Vec3& up, Vec3& forward)
{
    float rx = rotX * 3.14159f / 180.0f;
    float ry = rotY * 3.14159f / 180.0f;
    float rz = rotZ * 3.14159f / 180.0f;

    float cx = cosf(rx), sx = sinf(rx);
    float cy = cosf(ry), sy = sinf(ry);
    float cz = cosf(rz), sz = sinf(rz);

    // R = Rz * Ry * Rx
    float m00 = cy * cz;
    float m01 = cz * sx * sy - cx * sz;
    float m02 = sx * sz + cx * cz * sy;

    float m10 = cy * sz;
    float m11 = cx * cz + sx * sy * sz;
    float m12 = cx * sy * sz - cz * sx;

    float m20 = -sy;
    float m21 = cy * sx;
    float m22 = cx * cy;

    right   = { m00, m10, m20 };
    up      = { m01, m11, m21 };
    forward = { m02, m12, m22 };
}

// Canonical camera basis/view/projection helpers now live in src/camera/Camera3D.

static void GetLocalAxes(const Audio3DNode& n, Vec3& right, Vec3& up, Vec3& forward)
{
    GetLocalAxesFromEuler(n.rotX, n.rotY, n.rotZ, right, up, forward);
}

static void ImportAssetsToCurrentFolder(const std::vector<std::string>& pickedList)
{
    if (pickedList.empty()) return;

    std::filesystem::path targetDir = gAssetsCurrentDir.empty() ? (std::filesystem::path(gProjectDir) / "Assets") : gAssetsCurrentDir;
    int importedCount = 0;

    for (const auto& picked : pickedList)
    {
        std::filesystem::path inPath(picked);
        std::filesystem::path outPath = targetDir / inPath.filename();

        std::string ext = inPath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".vtxa")
        {
            std::filesystem::path animOut = outPath;
            animOut.replace_extension(".nebanim");
            std::error_code ec;
            std::filesystem::copy_file(inPath, animOut, std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec) importedCount++;
            gViewportToast = ec ? "Import failed" : ("Imported " + animOut.filename().string());
            gViewportToastUntil = glfwGetTime() + 2.0;
        }
        else if (ext == ".png")
        {
            std::filesystem::path texOut = outPath;
            texOut.replace_extension(".nebtex");
            std::string warn;
            if (ExportNebTexturePNG(inPath, texOut, warn))
            {
                importedCount++;
                gViewportToast = "Imported " + texOut.filename().string();
                if (!warn.empty()) gImportWarning = warn;
            }
            else
            {
                gViewportToast = "PNG import failed";
            }
            gViewportToastUntil = glfwGetTime() + 2.0;
        }
        else if (ext == ".fbx")
        {
            Assimp::Importer importer;
            const aiScene* scene = importer.ReadFile(picked,
                aiProcess_JoinIdenticalVertices |
                aiProcess_PreTransformVertices |
                aiProcess_GlobalScale);
            if (!scene)
            {
                gViewportToast = "FBX import failed";
                gViewportToastUntil = glfwGetTime() + 2.0;
            }
            else
            {
                std::string warn;
                std::error_code ec;
                std::filesystem::path meshesDir = targetDir / "mesh";
                std::filesystem::create_directories(meshesDir, ec);
                std::filesystem::path meshOut = meshesDir / inPath.filename();
                meshOut.replace_extension(".nebmesh");
                if (!ec && ExportNebMesh(scene, meshOut, warn))
                {
                    importedCount++;
                    int matCount = ImportModelTexturesAndGenerateMaterials(scene, inPath, targetDir, meshOut, warn);
                    gViewportToast = "Imported " + meshOut.filename().string();
                    if (matCount > 0)
                        gViewportToast += " + " + std::to_string(matCount) + " material slot(s)";
                    if (!warn.empty())
                        gImportWarning = warn;
                }
                else
                {
                    gViewportToast = "Mesh export failed";
                }
                gViewportToastUntil = glfwGetTime() + 2.0;
            }
        }
        else
        {
            gViewportToast = "Unsupported file";
            gViewportToastUntil = glfwGetTime() + 2.0;
        }
    }

    if (pickedList.size() > 1)
    {
        gViewportToast = "Imported " + std::to_string(importedCount) + " / " + std::to_string((int)pickedList.size()) + " assets";
        gViewportToastUntil = glfwGetTime() + 2.0;
    }
}

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

            Camera3DV2 playCam = BuildCamera3DV2FromLegacyEuler(
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

            if (edge(kG, gKeyG))
            {
                EndTransformSnapshot();
                if (gTransformMode == Transform_Grab) gTransformMode = Transform_None;
                else { gTransformMode = Transform_Grab; BeginTransformSnapshot(); }
                gAxisLock = 0;
            }
            if (edge(kR, gKeyR))
            {
                EndTransformSnapshot();
                if (gTransformMode == Transform_Rotate) gTransformMode = Transform_None;
                else { gTransformMode = Transform_Rotate; BeginTransformSnapshot(); }
                gAxisLock = 0;
            }
            if (edge(kS, gKeyS))
            {
                EndTransformSnapshot();
                if (gTransformMode == Transform_Scale) gTransformMode = Transform_None;
                else { gTransformMode = Transform_Scale; BeginTransformSnapshot(); }
                gAxisLock = 0;
            }
            if (edge(kX, gKeyX)) gAxisLock = (gAxisLock == 'X') ? 0 : 'X';
            if (edge(kY, gKeyY)) gAxisLock = (gAxisLock == 'Y') ? 0 : 'Y';
            if (edge(kZ, gKeyZ)) gAxisLock = (gAxisLock == 'Z') ? 0 : 'Z';
            if (edge(kEsc, gKeyEsc))
            {
                if (gPlayMode)
                {
                    gPlayMode = false;
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
                    }
                }
                else
                {
                    if (gHasTransformSnapshot)
                    {
                        if (gTransformIsStatic && gTransformIndex >= 0 && gTransformIndex < (int)gStaticMeshNodes.size())
                            gStaticMeshNodes[gTransformIndex] = gTransformBeforeStatic;
                        else if (gTransformIsNode3D && gTransformIndex >= 0 && gTransformIndex < (int)gNode3DNodes.size())
                            gNode3DNodes[gTransformIndex] = gTransformBeforeNode3D;
                        else if (gTransformIndex >= 0 && gTransformIndex < (int)gAudio3DNodes.size())
                            gAudio3DNodes[gTransformIndex] = gTransformBefore;
                    }
                    gHasTransformSnapshot = false;
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
                else if (!gSelectedAssetPath.empty() && std::filesystem::exists(gSelectedAssetPath))
                {
                    gPendingDelete = gSelectedAssetPath;
                    gDoDelete = true;
                    gSelectedAssetPath.clear();
                }
            }
        }

        // Debug overlay/HUD moved below once viewport layout is known.

        // Quick Node3D gravity + floor-plane collision test in play mode.
        // Collision is edge/bounds-based (quad perimeter/AABB), ignores mesh subdivision.
        if (gPlayMode)
        {
            struct FloorCollider
            {
                float minX, maxX;
                float minZ, maxZ;
                float y;
                int ownerNode3D = -1;
            };
            std::vector<FloorCollider> floorColliders;

            for (int si = 0; si < (int)gStaticMeshNodes.size(); ++si)
            {
                const auto& s = gStaticMeshNodes[si];
                if (s.mesh.empty() || gProjectDir.empty()) continue;

                if (!s.collisionSource) continue;

                std::filesystem::path meshPath = std::filesystem::path(gProjectDir) / s.mesh;
                const NebMesh* mesh = GetNebMesh(meshPath);
                if (!mesh || !mesh->valid || mesh->positions.empty()) continue;

                float wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz;
                GetStaticMeshWorldTRS(si, wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz);

                float minX = 1e30f, maxX = -1e30f;
                float minZ = 1e30f, maxZ = -1e30f;
                float topY = -1e30f;
                for (const auto& v : mesh->positions)
                {
                    // Edge/bounds-only collision approximation (no per-triangle subdivision tests)
                    float px = wx + v.x * wsx;
                    float py = wy + v.y * wsy;
                    float pz = wz + v.z * wsz;
                    if (px < minX) minX = px;
                    if (px > maxX) maxX = px;
                    if (pz < minZ) minZ = pz;
                    if (pz > maxZ) maxZ = pz;
                    if (py > topY) topY = py;
                }

                if (minX <= maxX && minZ <= maxZ)
                    floorColliders.push_back({ minX, maxX, minZ, maxZ, topY, -1 });
            }

            for (int ni = 0; ni < (int)gNode3DNodes.size(); ++ni)
            {
                const auto& s = gNode3DNodes[ni];
                if (!s.collisionSource) continue;

                float wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz;
                GetNode3DWorldTRS(ni, wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz);

                // Local bounds center offset, rotated by world orientation, independent from hierarchy transforms.
                Vec3 right{}, up{}, forward{};
                GetLocalAxesFromEuler(wrx, wry, wrz, right, up, forward);
                wx += right.x * s.boundPosX + up.x * s.boundPosY + forward.x * s.boundPosZ;
                wy += right.y * s.boundPosX + up.y * s.boundPosY + forward.y * s.boundPosZ;
                wz += right.z * s.boundPosX + up.z * s.boundPosY + forward.z * s.boundPosZ;

                const float hx = std::max(0.0f, s.extentX * wsx);
                const float hy = std::max(0.0f, s.extentY * wsy);
                const float hz = std::max(0.0f, s.extentZ * wsz);

                float minX = wx - hx, maxX = wx + hx;
                float minZ = wz - hz, maxZ = wz + hz;
                float topY = wy + hy;

                if (minX <= maxX && minZ <= maxZ)
                    floorColliders.push_back({ minX, maxX, minZ, maxZ, topY, ni });
            }

            const float gravity = -29.4f; // snappy arcade fall
            const float dt = io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f);
            for (int ni = 0; ni < (int)gNode3DNodes.size(); ++ni)
            {
                auto& n3 = gNode3DNodes[ni];
                if (!n3.physicsEnabled) continue;
                n3.velY += gravity * dt;
                n3.y += n3.velY * dt;

                for (const auto& fc : floorColliders)
                {
                    if (fc.ownerNode3D == ni) continue; // ignore self-collision source
                    if (n3.x >= fc.minX && n3.x <= fc.maxX && n3.z >= fc.minZ && n3.z <= fc.maxZ)
                    {
                        if (n3.y < fc.y)
                        {
                            n3.y = fc.y;
                            if (n3.velY < 0.0f) n3.velY = 0.0f;
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
                    Vec3 delta = { right.x * dx * moveScale + upAxis.x * -dy * moveScale,
                                   right.y * dx * moveScale + upAxis.y * -dy * moveScale,
                                   right.z * dx * moveScale + upAxis.z * -dy * moveScale };

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
                    Vec3 delta = { right.x * dx * moveScale + upAxis.x * -dy * moveScale,
                                   right.y * dx * moveScale + upAxis.y * -dy * moveScale,
                                   right.z * dx * moveScale + upAxis.z * -dy * moveScale };

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
                    Vec3 delta = { right.x * dx * moveScale + upAxis.x * -dy * moveScale,
                                   right.y * dx * moveScale + upAxis.y * -dy * moveScale,
                                   right.z * dx * moveScale + upAxis.z * -dy * moveScale };

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

        // Background gradient
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_ALPHA_TEST);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_BLEND);
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

            // Stars (front) ï¿½ GL_POINTS buckets (3/6/12px)
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

            float wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz;
            GetNode3DWorldTRS(i, wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz);
            glPushMatrix();
            glTranslatef(wx, wy, wz);
            glRotatef(wrx, 1.0f, 0.0f, 0.0f);
            glRotatef(wry, 0.0f, 1.0f, 0.0f);
            glRotatef(wrz, 0.0f, 0.0f, 1.0f);
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

            struct MatState { GLuint tex = 0; bool flipU = false; bool flipV = false; float satU = 1.0f; float satV = 1.0f; float uvScale = 0.0f; };
            std::unordered_map<int, MatState> matState;
            auto getMatState = [&](int matIndex) -> MatState {
                auto it = matState.find(matIndex);
                if (it != matState.end()) return it->second;

                MatState st{};
                std::string matRef = GetStaticMeshMaterialByIndex(s, matIndex);
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
                        LoadNebTexFlipOptions(matPath, st.flipU, st.flipV);
                        if (gPreviewSaturnSampling)
                            GetNebTexSaturnPadUvScale(matPath, st.satU, st.satV);
                    }
                    else
                    {
                        LoadMaterialUvScale(matPath, st.uvScale);
                        std::string texPath;
                        if (LoadMaterialTexture(matPath, texPath) && !texPath.empty())
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
                                LoadNebTexFlipOptions(tpath, st.flipU, st.flipV);
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
            // StaticMesh rotation axis remap:
            // X <- Z (pitch/back-forth), Y <- X (yaw left-right), Z <- Y (roll/spin)
            // If parented under Node3D, apply parent/world rotation directly (Node3D axis basis)
            // to avoid child-local mismatch against root node rotation.
            float sxRot = wrz;
            float syRot = wrx;
            float szRot = wry;
            if (!s.parent.empty() && FindNode3DByName(s.parent) >= 0)
            {
                sxRot = wrx;
                syRot = wry;
                szRot = wrz;
            }
            glRotatef(sxRot, 1.0f, 0.0f, 0.0f);
            glRotatef(syRot, 0.0f, 1.0f, 0.0f);
            glRotatef(szRot, 0.0f, 0.0f, 1.0f);
            glScalef(wsx, wsy, wsz);

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
                        if (ia >= mesh->positions.size() || ib >= mesh->positions.size()) continue;
                        const Vec3& a = mesh->positions[ia];
                        const Vec3& b = mesh->positions[ib];
                        glVertex3f(a.x, a.y, a.z);
                        glVertex3f(b.x, b.y, b.z);
                    }
                }
                glEnd();
            }
            else
            {
                if (gWireframePreview) glDisable(GL_TEXTURE_2D);
                glBegin(GL_TRIANGLES);
                // Force first triangle to bind from its own face material slot.
                GLuint boundTex = 0xFFFFFFFFu;
                for (size_t idx = 0; idx + 2 < mesh->indices.size(); idx += 3)
                {
                    uint16_t i0 = mesh->indices[idx + 0];
                    uint16_t i1 = mesh->indices[idx + 1];
                    uint16_t i2 = mesh->indices[idx + 2];
                    if (i0 >= mesh->positions.size() || i1 >= mesh->positions.size() || i2 >= mesh->positions.size())
                        continue;

                    int triIndex = (int)(idx / 3);
                    int faceMat = 0;
                    if (mesh->hasFaceMaterial && triIndex >= 0 && triIndex < (int)mesh->faceMaterial.size())
                        faceMat = (int)mesh->faceMaterial[triIndex];
                    MatState triState = getMatState(faceMat);
                    if (gWireframePreview) setWireColorForMat(faceMat);
                    else glColor3f(1.0f, 1.0f, 1.0f);
                    if (triState.tex != boundTex)
                    {
                        glEnd();
                        if (triState.tex != 0 && mesh->hasUv) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, triState.tex); }
                        else glDisable(GL_TEXTURE_2D);
                        glBegin(GL_TRIANGLES);
                        boundTex = triState.tex;
                    }

                    if (triState.tex != 0 && mesh->hasUv && i0 < mesh->uvs.size()) { float u = mesh->uvs[i0].x; float v = 1.0f - mesh->uvs[i0].y; float uvMul = powf(2.0f, -triState.uvScale); u *= uvMul; v *= uvMul; if (gPreviewSaturnSampling) { u *= triState.satU; v *= triState.satV; } if (triState.flipU) u = 1.0f - u; if (triState.flipV) v = 1.0f - v; glTexCoord2f(u, v); }
                    const Vec3& v0 = mesh->positions[i0];
                    glVertex3f(v0.x, v0.y, v0.z);
                    if (triState.tex != 0 && mesh->hasUv && i1 < mesh->uvs.size()) { float u = mesh->uvs[i1].x; float v = 1.0f - mesh->uvs[i1].y; float uvMul = powf(2.0f, -triState.uvScale); u *= uvMul; v *= uvMul; if (gPreviewSaturnSampling) { u *= triState.satU; v *= triState.satV; } if (triState.flipU) u = 1.0f - u; if (triState.flipV) v = 1.0f - v; glTexCoord2f(u, v); }
                    const Vec3& v1 = mesh->positions[i1];
                    glVertex3f(v1.x, v1.y, v1.z);
                    if (triState.tex != 0 && mesh->hasUv && i2 < mesh->uvs.size()) { float u = mesh->uvs[i2].x; float v = 1.0f - mesh->uvs[i2].y; float uvMul = powf(2.0f, -triState.uvScale); u *= uvMul; v *= uvMul; if (gPreviewSaturnSampling) { u *= triState.satU; v *= triState.satV; } if (triState.flipU) u = 1.0f - u; if (triState.flipV) v = 1.0f - v; glTexCoord2f(u, v); }
                    const Vec3& v2 = mesh->positions[i2];
                    glVertex3f(v2.x, v2.y, v2.z);
                }
                glEnd();
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
                    if (i0 >= mesh->positions.size() || i1 >= mesh->positions.size() || i2 >= mesh->positions.size())
                        continue;
                    const Vec3& v0 = mesh->positions[i0];
                    const Vec3& v1 = mesh->positions[i1];
                    const Vec3& v2 = mesh->positions[i2];
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
                playSceneSnapshotValid = true;

                gPlayMode = true;
                BeginPlayScriptRuntime();
            }
            else
            {
                gPlayMode = false;
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
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C))
        {
            if (gSelectedAudio3D >= 0 && gSelectedAudio3D < (int)gAudio3DNodes.size())
            {
                gHasCopiedNode = true;
                gHasCopiedStatic = false;
                gCopiedNode = gAudio3DNodes[gSelectedAudio3D];
            }
            else if (gSelectedStaticMesh >= 0 && gSelectedStaticMesh < (int)gStaticMeshNodes.size())
            {
                gHasCopiedStatic = true;
                gHasCopiedNode = false;
                gCopiedStatic = gStaticMeshNodes[gSelectedStaticMesh];
            }
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V))
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

        if (ImGui::BeginPopup("FileMenu"))
        {
            if (ImGui::MenuItem("New Project...", "Ctrl+Shift+N"))
            {
                std::string picked = PickFolderDialog("Select project folder");
                if (!picked.empty())
                {
                    CreateNebulaProject(picked);
                }
            }
            if (ImGui::MenuItem("Open Project...", "Ctrl+Shift+O"))
            {
                std::string projFile = PickProjectFileDialog("Open Nebula project");
                if (!projFile.empty())
                {
                    gProjectFile = projFile;
                    gProjectDir = std::filesystem::path(projFile).parent_path().string();
                    gAssetsCurrentDir.clear();

                    gOpenScenes.clear();
                    gActiveScene = -1;
                    gAudio3DNodes.clear();
                    gStaticMeshNodes.clear();
                    gCamera3DNodes.clear();
                    gNode3DNodes.clear();
                    gNode3DNodes.clear();
                    gSelectedAudio3D = -1;
                    gSelectedStaticMesh = -1;
                    gSelectedCamera3D = -1;
                    gTransformMode = Transform_None;
                    gTransforming = false;
                    gAxisLock = 0;
                    gUndoStack.clear();
                    gRedoStack.clear();

                    AddRecentProject(gProjectFile);
                }
            }
            if (ImGui::MenuItem("Close Project"))
            {
                gProjectFile.clear();
                gProjectDir.clear();
                gAssetsCurrentDir.clear();

                gOpenScenes.clear();
                gActiveScene = -1;
                gAudio3DNodes.clear();
                gStaticMeshNodes.clear();
                gCamera3DNodes.clear();
                gSelectedAudio3D = -1;
                gSelectedStaticMesh = -1;
                gSelectedCamera3D = -1;
                gTransformMode = Transform_None;
                gTransforming = false;
                gAxisLock = 0;
                gUndoStack.clear();
                gRedoStack.clear();
            }
            if (ImGui::BeginMenu("Open Recent"))
            {
                if (gRecentProjects.empty())
                {
                    ImGui::MenuItem("(none)", nullptr, false, false);
                }
                else
                {
                    for (const auto& p : gRecentProjects)
                    {
                        if (ImGui::MenuItem(p.c_str()))
                        {
                            gProjectFile = p;
                            gProjectDir = std::filesystem::path(p).parent_path().string();

                            gOpenScenes.clear();
                            gActiveScene = -1;
                            gAudio3DNodes.clear();
                            gStaticMeshNodes.clear();
                            gCamera3DNodes.clear();
                            gNode3DNodes.clear();
                            gSelectedAudio3D = -1;
                            gSelectedStaticMesh = -1;
                            gSelectedCamera3D = -1;
                            gTransformMode = Transform_None;
                            gTransforming = false;
                            gAxisLock = 0;
                            gUndoStack.clear();
                            gRedoStack.clear();

                            AddRecentProject(gProjectFile);
                        }
                    }
                }
                ImGui::EndMenu();
            }

            // Open File disabled
            if (ImGui::MenuItem("Save", "Ctrl+S")) SaveActiveScene();
            if (ImGui::MenuItem("Save All", "Ctrl+Shift+S")) SaveAllProjectChanges();
            if (!gEnableScriptHotReload)
            {
                if (ImGui::MenuItem("Enable Hot Reloading"))
                {
                    gEnableScriptHotReload = true;
                    gScriptHotReloadKnownMtimes.clear();
                    gScriptHotReloadTrackedProjectDir.clear();
                    gScriptHotReloadNextPollAt = 0.0;
                    gViewportToast = "Script Hot Reload v1 enabled (detect + state refresh only)";
                    gViewportToastUntil = glfwGetTime() + 2.5;
                }
            }
            else
            {
                if (ImGui::MenuItem("Reload Scripts Now"))
                {
                    ForceScriptHotReloadNowV1();
                }
                if (ImGui::MenuItem("Disable Hot Reloading"))
                {
                    gEnableScriptHotReload = false;
                    gScriptHotReloadKnownMtimes.clear();
                    gScriptHotReloadTrackedProjectDir.clear();
                    gViewportToast = "Script Hot Reload v1 disabled";
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences...", "Ctrl+," ))
            {
                showPreferences = true;
            }
            ImGui::Separator();
            ImGui::MenuItem("Exit", "Alt+F4");
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("EditMenu"))
        {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) DoUndo();
            if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z")) DoRedo();
            ImGui::Separator();
            ImGui::MenuItem("Cut", "Ctrl+X");
            ImGui::MenuItem("Copy", "Ctrl+C");
            ImGui::MenuItem("Paste", "Ctrl+V");
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("ToolsMenu"))
        {
            if (ImGui::MenuItem("VMU Tool"))
            {
                gShowVmuTool = true;
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("PackageMenu"))
        {
            if (ImGui::MenuItem("Build Dreamcast (NEBULA_DREAMCAST)", "") || gRequestDreamcastGenerate)
            {
                gRequestDreamcastGenerate = false;
                if (gProjectDir.empty())
                {
                    printf("[Package] No project selected. Use File -> New Project first.\n");
                }
                else
                {
                    bool useLegacyDreamcastBuilder = false;
                    if (!useLegacyDreamcastBuilder)
                    {
                        std::string projectDirFixed = gProjectDir;
                        {
                            auto replaceAll = [](std::string& s, const std::string& from, const std::string& to)
                            {
                                size_t pos = 0;
                                while ((pos = s.find(from, pos)) != std::string::npos)
                                {
                                    s.replace(pos, from.size(), to);
                                    pos += to.size();
                                }
                            };
                            // Recover accidental escape-decoding in Windows paths (e.g. "\\n" -> newline)
                            replaceAll(projectDirFixed, "\n", "\\n");
                            replaceAll(projectDirFixed, "\r", "\\r");
                            replaceAll(projectDirFixed, "\t", "\\t");
                            // Also fix actual control chars that may already be present in the path.
                            std::replace(projectDirFixed.begin(), projectDirFixed.end(), '\n', '\\');
                            projectDirFixed.erase(std::remove(projectDirFixed.begin(), projectDirFixed.end(), '\r'), projectDirFixed.end());
                            std::replace(projectDirFixed.begin(), projectDirFixed.end(), '\t', '\\');
                        }

                        std::filesystem::path buildDir = std::filesystem::path(projectDirFixed) / "build_dreamcast";
                        std::filesystem::create_directories(buildDir);
                        std::filesystem::path logPath = buildDir / "package.log";

                        std::filesystem::path scriptPath = buildDir / "_nebula_build_dreamcast.bat";

                        {
                            auto normWinPath = [](std::string s) {
                                std::replace(s.begin(), s.end(), '/', '\\');
                                return s;
                            };
                            std::string dreamSdkHome = gPrefDreamSdkHome.empty() ? std::string("C:\\DreamSDK") : normWinPath(gPrefDreamSdkHome);

                            std::ofstream bs(scriptPath, std::ios::out | std::ios::trunc);
                            if (bs.is_open())
                            {
                                bs << "@echo off\n";
                                bs << "setlocal\n";
                                bs << "set BUILD_DIR=%~dp0\n";
                                bs << "set DREAMSDK_HOME=" << dreamSdkHome << "\n";
                                bs << "set KOS_BASE=%DREAMSDK_HOME%\\opt\\toolchains\\dc\\kos\n";
                                bs << "set KOS_CC_BASE=%DREAMSDK_HOME%\\opt\\toolchains\\dc\n";
                                bs << "set MAKE_EXE=%DREAMSDK_HOME%\\usr\\bin\\make.exe\n";
                                bs << "set SCRAMBLE_EXE=%DREAMSDK_HOME%\\opt\\toolchains\\dc\\kos\\utils\\scramble\\scramble.exe\n";
                                bs << "set MAKEIP_EXE=%DREAMSDK_HOME%\\opt\\toolchains\\dc\\kos\\utils\\makeip\\makeip.exe\n";
                                bs << "set MKISOFS_EXE=%DREAMSDK_HOME%\\usr\\bin\\mkisofs.exe\n";
                                bs << "set CDI4DC_EXE=%DREAMSDK_HOME%\\usr\\bin\\cdi4dc.exe\n";
                                bs << "if not exist \"%MAKE_EXE%\" ( echo [DreamcastBuild] Missing DreamSDK make: %MAKE_EXE% & exit /b 1 )\n";
                                bs << "if not exist \"%KOS_BASE%\\include\\kos.h\" ( echo [DreamcastBuild] Missing DreamSDK KOS headers: %KOS_BASE%\\include\\kos.h & exit /b 1 )\n";
                                bs << "if not exist \"%KOS_BASE%\\lib\\dreamcast\\libkallisti.a\" ( echo [DreamcastBuild] Missing DreamSDK KOS runtime lib: %KOS_BASE%\\lib\\dreamcast\\libkallisti.a & exit /b 1 )\n";
                                bs << "if not exist \"%DREAMSDK_HOME%\\tmp\" mkdir \"%DREAMSDK_HOME%\\tmp\"\n";
                                bs << "set TMP=%DREAMSDK_HOME%\\tmp\n";
                                bs << "set TEMP=%DREAMSDK_HOME%\\tmp\n";
                                bs << "set PATH=%DREAMSDK_HOME%\\opt\\toolchains\\dc\\sh-elf\\bin;%DREAMSDK_HOME%\\usr\\bin;%DREAMSDK_HOME%\\mingw64\\bin;%PATH%\n";
                                bs << "set DREAMSDK_HOME_MSYS=%DREAMSDK_HOME:\\=/%\n";
                                bs << "if \"%DREAMSDK_HOME_MSYS:~1,1%\"==\":\" set DREAMSDK_HOME_MSYS=/%DREAMSDK_HOME_MSYS:~0,1%%DREAMSDK_HOME_MSYS:~2%\n";
                                bs << "set KOS_BASE_MSYS=%DREAMSDK_HOME_MSYS%/opt/toolchains/dc/kos\n";
                                bs << "set KOS_CC_BASE_MSYS=%DREAMSDK_HOME_MSYS%/opt/toolchains/dc\n";
                                bs << "pushd \"%BUILD_DIR%\"\n";
                                bs << "if exist \"*.o\" del /q *.o >nul 2>nul\n";
                                bs << "if exist \"scripts\" for /r scripts %%f in (*.o) do del /q \"%%f\" >nul 2>nul\n";
                                bs << "\"%MAKE_EXE%\" -f Makefile.dreamcast KOS_BASE=\"%KOS_BASE_MSYS%\" KOS_CC_BASE=\"%KOS_CC_BASE_MSYS%\" TMP=\"%TMP%\" TEMP=\"%TEMP%\"\n";
                                bs << "if errorlevel 1 exit /b 1\n";
                                bs << "sh-elf-objcopy -O binary nebula_dreamcast.elf nebula_dreamcast.bin\n";
                                bs << "if errorlevel 1 exit /b 1\n";
                                bs << "if exist \"%SCRAMBLE_EXE%\" (\n";
                                bs << "  \"%SCRAMBLE_EXE%\" nebula_dreamcast.bin 1ST_READ.BIN\n";
                                bs << ") else (\n";
                                bs << "  copy /Y nebula_dreamcast.bin 1ST_READ.BIN >nul\n";
                                bs << ")\n";
                                bs << "if errorlevel 1 exit /b 1\n";
                                bs << "if not exist ip.txt (\n";
                                bs << "  >ip.txt echo Hardware ID   : SEGA SEGAKATANA\n";
                                bs << "  >>ip.txt echo Maker ID      : SEGA ENTERPRISES\n";
                                bs << "  >>ip.txt echo Device Info   : CD-ROM1/1\n";
                                bs << "  >>ip.txt echo Area Symbols  : JUE\n";
                                bs << "  >>ip.txt echo Peripherals   : E000F10\n";
                                bs << "  >>ip.txt echo Product No    : T-00000\n";
                                bs << "  >>ip.txt echo Version       : V1.000\n";
                                bs << "  >>ip.txt echo Release Date  : 20260218\n";
                                bs << "  >>ip.txt echo Boot Filename : 1ST_READ.BIN\n";
                                bs << "  >>ip.txt echo SW Maker Name : NEBULA\n";
                                bs << "  >>ip.txt echo Game Title    : NEBULA DREAMCAST\n";
                                bs << ")\n";
                                bs << "if exist \"%MAKEIP_EXE%\" \"%MAKEIP_EXE%\" -f ip.txt IP.BIN\n";
                                bs << "if not exist cd_root mkdir cd_root\n";
                                bs << "copy /Y 1ST_READ.BIN cd_root\\1ST_READ.BIN >nul\n";
                                bs << "if not exist cd_root\\data mkdir cd_root\\data\n";
                                bs << "if not exist cd_root\\data\\meshes mkdir cd_root\\data\\meshes\n";
                                bs << "if not exist cd_root\\data\\textures mkdir cd_root\\data\\textures\n";
                                bs << "if not exist cd_root\\data\\scenes mkdir cd_root\\data\\scenes\n";
                                bs << "if exist \"%MKISOFS_EXE%\" if exist IP.BIN \"%MKISOFS_EXE%\" -C 0,11702 -V NEBULA_DC -G IP.BIN -l -o nebula_dreamcast.iso cd_root\n";
                                bs << "if exist \"%CDI4DC_EXE%\" if exist nebula_dreamcast.iso \"%CDI4DC_EXE%\" nebula_dreamcast.iso nebula_dreamcast.cdi\n";
                                bs << "popd\n";
                                bs << "exit /b 0\n";
                            }
                        }

                        if (std::filesystem::exists(scriptPath))
                        {
                            std::filesystem::path makefilePath = buildDir / "Makefile.dreamcast";
                            std::filesystem::path runtimeCPath = buildDir / "main.c";
                            std::filesystem::path entryCPath = buildDir / "entry.c";
                            std::filesystem::path gameStubPath = buildDir / "NebulaGameStub.c";

                            // Dreamcast script policy: compile project-local C scripts only.
                            // Source root: <Project>/Scripts (recursive), staged under build_dreamcast/scripts.
                            std::filesystem::path projectScriptsDir = std::filesystem::path(gProjectDir) / "Scripts";
                            std::filesystem::path buildScriptsDir = buildDir / "scripts";
                            std::vector<std::string> scriptSourcesForMake;
                            int scriptDiscoveredC = 0;
                            int scriptCopiedC = 0;
                            int scriptIgnoredCpp = 0;
                            {
                                std::error_code recEc;
                                std::filesystem::remove_all(buildScriptsDir, recEc);
                                std::filesystem::create_directories(buildScriptsDir, recEc);
                                if (std::filesystem::exists(projectScriptsDir))
                                {
                                    std::filesystem::recursive_directory_iterator it(projectScriptsDir, recEc), end;
                                    for (; !recEc && it != end; it.increment(recEc))
                                    {
                                        if (recEc) break;
                                        const auto& srcPath = it->path();
                                        std::error_code stEc;
                                        if (!std::filesystem::is_regular_file(srcPath, stEc)) continue;

                                        std::string ext = srcPath.extension().string();
                                        for (char& ch : ext) ch = (char)std::tolower((unsigned char)ch);
                                        if (ext == ".c")
                                        {
                                            ++scriptDiscoveredC;
                                            std::error_code relEc;
                                            std::filesystem::path rel = std::filesystem::relative(srcPath, projectScriptsDir, relEc);
                                            if (relEc) continue;
                                            std::filesystem::path dst = buildScriptsDir / rel;
                                            std::error_code mkEc;
                                            std::filesystem::create_directories(dst.parent_path(), mkEc);
                                            std::error_code cpEc;
                                            std::filesystem::copy_file(srcPath, dst, std::filesystem::copy_options::overwrite_existing, cpEc);
                                            if (!cpEc)
                                            {
                                                std::string relSrc = (std::filesystem::path("scripts") / rel).string();
                                                std::replace(relSrc.begin(), relSrc.end(), '\\', '/');
                                                scriptSourcesForMake.push_back(relSrc);
                                                ++scriptCopiedC;
                                            }
                                        }
                                        else if (ext == ".cpp" || ext == ".cc" || ext == ".cxx")
                                        {
                                            ++scriptIgnoredCpp;
                                        }
                                    }
                                }
                            }

                            std::vector<Audio3DNode> exportNodes = gAudio3DNodes;
                            std::vector<StaticMesh3DNode> exportStatics = gStaticMeshNodes;
                            std::vector<Camera3DNode> exportCameras = gCamera3DNodes;
                            std::string cameraSourceScene = "(editor)";
                            if (gActiveScene >= 0 && gActiveScene < (int)gOpenScenes.size() && !gOpenScenes[gActiveScene].path.empty())
                                cameraSourceScene = gOpenScenes[gActiveScene].path.lexically_normal().generic_string();

                            if (gActiveScene >= 0 && gActiveScene < (int)gOpenScenes.size())
                            {
                                gOpenScenes[gActiveScene].nodes = gAudio3DNodes;
                                gOpenScenes[gActiveScene].staticMeshes = gStaticMeshNodes;
                                gOpenScenes[gActiveScene].cameras = gCamera3DNodes;
                                gOpenScenes[gActiveScene].node3d = gNode3DNodes;
                            }

                            std::string defaultSceneCfg = GetProjectDefaultScene(std::filesystem::path(gProjectDir));
                            if (!defaultSceneCfg.empty())
                            {
                                std::filesystem::path defaultScenePath(defaultSceneCfg);
                                if (defaultScenePath.is_relative())
                                    defaultScenePath = std::filesystem::path(gProjectDir) / defaultScenePath;

                                bool foundOpen = false;
                                for (const auto& s : gOpenScenes)
                                {
                                    if (s.path == defaultScenePath)
                                    {
                                        exportNodes = s.nodes;
                                        exportStatics = s.staticMeshes;
                                        exportCameras = s.cameras;
                                        cameraSourceScene = defaultScenePath.lexically_normal().generic_string();
                                        foundOpen = true;
                                        break;
                                    }
                                }

                                if (!foundOpen)
                                {
                                    SceneData loaded{};
                                    if (LoadSceneFromPath(defaultScenePath, loaded))
                                    {
                                        exportNodes = loaded.nodes;
                                        exportStatics = loaded.staticMeshes;
                                        exportCameras = loaded.cameras;
                                        cameraSourceScene = defaultScenePath.lexically_normal().generic_string();
                                    }
                                }
                            }

                            Camera3DNode camSrc{};
                            bool haveCam = false;
                            // Match editor selection logic as closely as possible:
                            // 1) prefer main camera with highest priority
                            // 2) otherwise highest priority camera
                            // 3) legacy-name fallback (Camera3D1/Camera3D*)
                            int bestMainPri = -2147483647;
                            int bestAnyPri = -2147483647;
                            int bestMainIdx = -1;
                            int bestAnyIdx = -1;
                            int legacyIdx = -1;
                            for (int i = 0; i < (int)exportCameras.size(); ++i)
                            {
                                const auto& c = exportCameras[i];
                                if (c.main && c.priority > bestMainPri) { bestMainPri = c.priority; bestMainIdx = i; }
                                if (c.priority > bestAnyPri) { bestAnyPri = c.priority; bestAnyIdx = i; }
                                if (legacyIdx < 0 && (c.name == "Camera3D1" || c.name == "Camera3D" || c.name.rfind("Camera3D", 0) == 0)) legacyIdx = i;
                            }
                            int chosenCamIdx = (bestMainIdx >= 0) ? bestMainIdx : ((bestAnyIdx >= 0) ? bestAnyIdx : legacyIdx);
                            if (chosenCamIdx >= 0)
                            {
                                camSrc = exportCameras[chosenCamIdx];
                                haveCam = true;
                            }
                            if (!haveCam && !exportCameras.empty()) { camSrc = exportCameras[0]; haveCam = true; }
                            if (!haveCam) { camSrc.x = 0.f; camSrc.y = 2.f; camSrc.z = -6.f; camSrc.rotX = 0.f; camSrc.rotY = 0.f; camSrc.rotZ = 0.f; }
                            printf("[DreamcastBuild] Camera selection: haveCam=%d exportCameras=%d chosenIdx=%d\n", haveCam?1:0, (int)exportCameras.size(), chosenCamIdx);
                            printf("[DreamcastBuild]   camSrc: name='%s' pos=(%.3f,%.3f,%.3f) rot=(%.3f,%.3f,%.3f) orbit=(%.3f,%.3f,%.3f) fov=%.1f main=%d pri=%d parent='%s'\n",
                                camSrc.name.c_str(), camSrc.x, camSrc.y, camSrc.z,
                                camSrc.rotX, camSrc.rotY, camSrc.rotZ,
                                camSrc.orbitX, camSrc.orbitY, camSrc.orbitZ,
                                camSrc.fovY, camSrc.main?1:0, camSrc.priority, camSrc.parent.c_str());

                            StaticMesh3DNode meshSrc{};
                            bool haveMesh = false;
                            // Runtime currently renders one primary StaticMesh3D.
                            // Prefer first non-cube mesh (user-authored content), fallback to first mesh.
                            for (const auto& s : exportStatics)
                            {
                                if (s.mesh.find("cube_primitive") == std::string::npos)
                                {
                                    meshSrc = s;
                                    haveMesh = true;
                                    break;
                                }
                            }
                            if (!haveMesh && !exportStatics.empty()) { meshSrc = exportStatics[0]; haveMesh = true; }
                            if (!haveMesh) { meshSrc.x = 0.f; meshSrc.y = 0.f; meshSrc.z = 0.f; meshSrc.scaleX = 1.f; meshSrc.scaleY = 1.f; meshSrc.scaleZ = 1.f; }

                            // Resolve camera world transform using the same logic as the editor play mode
                            // (GetCamera3DWorldTR), but operating on export data instead of globals.
                            auto getCamera3DWorldTR_export = [&](const Camera3DNode& cam,
                                float& ox, float& oy, float& oz,
                                float& orx, float& ory, float& orz)
                            {
                                auto rotateOffsetEuler = [](float& x, float& y, float& z, float rxDeg, float ryDeg, float rzDeg)
                                {
                                    const float rx = rxDeg * 3.14159f / 180.0f;
                                    const float ry = ryDeg * 3.14159f / 180.0f;
                                    const float rz = rzDeg * 3.14159f / 180.0f;
                                    const float sx = sinf(rx), cx = cosf(rx);
                                    const float sy = sinf(ry), cy = cosf(ry);
                                    const float sz = sinf(rz), cz = cosf(rz);
                                    float ty = y * cx - z * sx;
                                    float tz = y * sx + z * cx;
                                    y = ty; z = tz;
                                    float tx = x * cy + z * sy;
                                    tz = -x * sy + z * cy;
                                    x = tx; z = tz;
                                    tx = x * cz - y * sz;
                                    ty = x * sz + y * cz;
                                    x = tx; y = ty;
                                };

                                ox = cam.x; oy = cam.y; oz = cam.z;
                                if (!cam.parent.empty())
                                {
                                    ox += cam.orbitX;
                                    oy += cam.orbitY;
                                    oz += cam.orbitZ;
                                }
                                orx = cam.rotX; ory = cam.rotY; orz = cam.rotZ;

                                std::string p = cam.parent;
                                int guard = 0;
                                while (!p.empty() && guard++ < 256)
                                {
                                    bool found = false;
                                    for (const auto& a : gAudio3DNodes)
                                    {
                                        if (a.name == p)
                                        {
                                            ox += a.x; oy += a.y; oz += a.z;
                                            p = a.parent;
                                            found = true;
                                            break;
                                        }
                                    }
                                    if (found) continue;
                                    for (const auto& s : exportStatics)
                                    {
                                        if (s.name == p)
                                        {
                                            rotateOffsetEuler(ox, oy, oz, s.rotX, s.rotY, s.rotZ);
                                            ox += s.x; oy += s.y; oz += s.z;
                                            orx += s.rotX; ory += s.rotY; orz += s.rotZ;
                                            p = s.parent;
                                            found = true;
                                            break;
                                        }
                                    }
                                    if (found) continue;
                                    for (const auto& pc : exportCameras)
                                    {
                                        if (pc.name == p)
                                        {
                                            rotateOffsetEuler(ox, oy, oz, pc.rotX, pc.rotY, pc.rotZ);
                                            ox += pc.x; oy += pc.y; oz += pc.z;
                                            orx += pc.rotX; ory += pc.rotY; orz += pc.rotZ;
                                            p = pc.parent;
                                            found = true;
                                            break;
                                        }
                                    }
                                    if (found) continue;
                                    for (const auto& n : gNode3DNodes)
                                    {
                                        if (n.name == p)
                                        {
                                            rotateOffsetEuler(ox, oy, oz, n.rotX, n.rotY, n.rotZ);
                                            ox += n.x; oy += n.y; oz += n.z;
                                            orx += n.rotX; ory += n.rotY; orz += n.rotZ;
                                            p = n.parent;
                                            found = true;
                                            break;
                                        }
                                    }
                                    if (!found) break;
                                }
                            };

                            float dcWorldX, dcWorldY, dcWorldZ, dcWorldRX, dcWorldRY, dcWorldRZ;
                            getCamera3DWorldTR_export(camSrc, dcWorldX, dcWorldY, dcWorldZ, dcWorldRX, dcWorldRY, dcWorldRZ);

                            Camera3DV2 dcCamV2 = BuildCamera3DV2FromLegacyEuler(
                                camSrc.name,
                                camSrc.parent,
                                dcWorldX, dcWorldY, dcWorldZ,
                                dcWorldRX, dcWorldRY, dcWorldRZ,
                                camSrc.perspective,
                                camSrc.fovY,
                                camSrc.nearZ,
                                camSrc.farZ,
                                camSrc.orthoWidth,
                                camSrc.priority,
                                camSrc.main);

                            NebulaCamera3D::View dcView = NebulaCamera3D::BuildView(dcCamV2);
                            NebulaCamera3D::Projection dcProj = NebulaCamera3D::BuildProjection(dcCamV2, 640.0f / 570.0f);

                            const float dcViewW = 640.0f;
                            const float dcViewH = 570.0f;
                            const float dcFocalY = (dcViewH * 0.5f) / std::max(1.0e-4f, std::tanf(dcProj.fovYRad * 0.5f));
                            const float dcFocalX = dcFocalY * dcProj.aspect;

                            printf("[DreamcastBuild] Camera: eye=(%.3f,%.3f,%.3f) target=(%.3f,%.3f,%.3f) fwd=(%.3f,%.3f,%.3f) up=(%.3f,%.3f,%.3f)\n",
                                dcView.eye.x, dcView.eye.y, dcView.eye.z,
                                dcView.target.x, dcView.target.y, dcView.target.z,
                                dcView.basis.forward.x, dcView.basis.forward.y, dcView.basis.forward.z,
                                dcView.basis.up.x, dcView.basis.up.y, dcView.basis.up.z);
                            printf("[DreamcastBuild] Projection: fovY=%.1f aspect=%.3f near=%.3f far=%.1f viewW=%.0f viewH=%.0f focalX=%.1f focalY=%.1f\n",
                                dcProj.fovYDeg, dcProj.aspect, dcProj.nearZ, dcProj.farZ,
                                dcViewW, dcViewH, dcFocalX, dcFocalY);

                            std::vector<Vec3> runtimeVerts;
                            std::vector<Vec3> runtimeUvs;
                            std::vector<uint16_t> runtimeIndices;
                            std::vector<Vec3> runtimeTriUvs;
                            std::vector<uint16_t> runtimeTriMat;
                            {
                                std::filesystem::path meshAbs;
                                if (!meshSrc.mesh.empty()) meshAbs = std::filesystem::path(gProjectDir) / meshSrc.mesh;

                                const NebMesh* nm = meshAbs.empty() ? nullptr : GetNebMesh(meshAbs);
                                if (nm && !nm->positions.empty() && nm->indices.size() >= 3)
                                {
                                    runtimeVerts = nm->positions;
                                    runtimeIndices = nm->indices;
                                    runtimeUvs.resize(runtimeVerts.size(), Vec3{ 0.0f, 0.0f, 0.0f });
                                    if (nm->uvs.size() == nm->positions.size())
                                    {
                                        for (size_t i = 0; i < runtimeVerts.size(); ++i) runtimeUvs[i] = nm->uvs[i];
                                    }

                                    if (nm->hasFaceRecords && !nm->faceRecords.empty())
                                    {
                                        runtimeIndices.clear();
                                        runtimeTriUvs.clear();
                                        for (const auto& fr : nm->faceRecords)
                                        {
                                            int ar = (fr.arity >= 3 && fr.arity <= 4) ? (int)fr.arity : 3;
                                            if (ar == 3)
                                            {
                                                runtimeIndices.push_back(fr.indices[0]);
                                                runtimeIndices.push_back(fr.indices[1]);
                                                runtimeIndices.push_back(fr.indices[2]);
                                                runtimeTriUvs.push_back(fr.uvs[0]);
                                                runtimeTriUvs.push_back(fr.uvs[1]);
                                                runtimeTriUvs.push_back(fr.uvs[2]);
                                                runtimeTriMat.push_back(fr.material);
                                            }
                                            else
                                            {
                                                uint16_t q[4] = { fr.indices[0], fr.indices[1], fr.indices[2], fr.indices[3] };
                                                Vec3 uv[4] = { fr.uvs[0], fr.uvs[1], fr.uvs[2], fr.uvs[3] };

                                                // Mirror parity correction + phase alignment (ported from Saturn canonical quad handling).
                                                bool mirrored = false;
                                                if (q[0] < runtimeVerts.size() && q[1] < runtimeVerts.size() && q[2] < runtimeVerts.size() && q[3] < runtimeVerts.size())
                                                {
                                                    const Vec3& p0 = runtimeVerts[q[0]];
                                                    const Vec3& p1 = runtimeVerts[q[1]];
                                                    const Vec3& p2 = runtimeVerts[q[2]];
                                                    const Vec3& p3 = runtimeVerts[q[3]];
                                                    Vec3 e10{ p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
                                                    Vec3 e30{ p3.x - p0.x, p3.y - p0.y, p3.z - p0.z };
                                                    Vec3 e20{ p2.x - p0.x, p2.y - p0.y, p2.z - p0.z };
                                                    Vec3 n{ e10.y * e20.z - e10.z * e20.y, e10.z * e20.x - e10.x * e20.z, e10.x * e20.y - e10.y * e20.x };
                                                    Vec3 cx{ e10.y * e30.z - e10.z * e30.y, e10.z * e30.x - e10.x * e30.z, e10.x * e30.y - e10.y * e30.x };
                                                    float geomDet = cx.x * n.x + cx.y * n.y + cx.z * n.z;

                                                    float du1 = uv[1].x - uv[0].x;
                                                    float dv1 = uv[1].y - uv[0].y;
                                                    float du2 = uv[3].x - uv[0].x;
                                                    float dv2 = uv[3].y - uv[0].y;
                                                    float uvDet = du1 * dv2 - dv1 * du2;
                                                    mirrored = ((geomDet * uvDet) < 0.0f);
                                                }

                                                if (mirrored)
                                                {
                                                    std::swap(q[0], q[1]);
                                                    std::swap(q[2], q[3]);
                                                    std::swap(uv[0], uv[1]);
                                                    std::swap(uv[2], uv[3]);
                                                }

                                                float uMin = uv[0].x, vMin = uv[0].y;
                                                for (int k = 1; k < 4; ++k) { uMin = std::min(uMin, uv[k].x); vMin = std::min(vMin, uv[k].y); }
                                                int bestRot = 0; float bestScore = 1e30f;
                                                for (int r = 0; r < 4; ++r)
                                                {
                                                    float du = uv[r].x - uMin;
                                                    float dv = uv[r].y - vMin;
                                                    float score = du * du + dv * dv;
                                                    if (score < bestScore) { bestScore = score; bestRot = r; }
                                                }

                                                uint16_t rq[4] = { q[bestRot & 3], q[(bestRot + 1) & 3], q[(bestRot + 2) & 3], q[(bestRot + 3) & 3] };
                                                Vec3 ruv[4] = { uv[bestRot & 3], uv[(bestRot + 1) & 3], uv[(bestRot + 2) & 3], uv[(bestRot + 3) & 3] };

                                                const int fan[2][3] = { {0,1,2}, {0,2,3} };
                                                for (int f = 0; f < 2; ++f)
                                                {
                                                    int i0 = fan[f][0], i1 = fan[f][1], i2 = fan[f][2];
                                                    runtimeIndices.push_back(rq[i0]);
                                                    runtimeIndices.push_back(rq[i1]);
                                                    runtimeIndices.push_back(rq[i2]);
                                                    runtimeTriUvs.push_back(ruv[i0]);
                                                    runtimeTriUvs.push_back(ruv[i1]);
                                                    runtimeTriUvs.push_back(ruv[i2]);
                                                    runtimeTriMat.push_back(fr.material);
                                                }
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    runtimeVerts = {
                                        {-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},{0.5f,0.5f,-0.5f},{-0.5f,0.5f,-0.5f},
                                        {-0.5f,-0.5f, 0.5f},{0.5f,-0.5f, 0.5f},{0.5f,0.5f, 0.5f},{-0.5f,0.5f, 0.5f}
                                    };
                                    runtimeIndices = {0,1,2, 0,2,3, 4,7,6, 4,6,5, 0,4,5, 0,5,1, 3,2,6, 3,6,7, 1,5,6, 1,6,2, 0,3,7, 0,7,4};
                                    runtimeUvs = {
                                        {0,1,0},{1,1,0},{1,0,0},{0,0,0},
                                        {0,1,0},{1,1,0},{1,0,0},{0,0,0}
                                    };
                                }

                                if (runtimeUvs.size() != runtimeVerts.size())
                                    runtimeUvs.resize(runtimeVerts.size(), Vec3{ 0.0f, 0.0f, 0.0f });

                                if (runtimeTriUvs.size() != runtimeIndices.size())
                                {
                                    runtimeTriUvs.clear();
                                    runtimeTriUvs.reserve(runtimeIndices.size());
                                    for (size_t ii = 0; ii < runtimeIndices.size(); ++ii)
                                    {
                                        uint16_t vi = runtimeIndices[ii];
                                        runtimeTriUvs.push_back((vi < runtimeUvs.size()) ? runtimeUvs[vi] : Vec3{ 0.0f, 0.0f, 0.0f });
                                    }
                                }

                                const size_t triCountLocal = runtimeIndices.size() / 3;
                                if (runtimeTriMat.size() != triCountLocal)
                                {
                                    runtimeTriMat.clear();
                                    runtimeTriMat.resize(triCountLocal, 0);
                                    if (!meshAbs.empty())
                                    {
                                        const NebMesh* nm2 = GetNebMesh(meshAbs);
                                        if (nm2 && nm2->faceMaterial.size() == triCountLocal)
                                        {
                                            for (size_t ti = 0; ti < triCountLocal; ++ti) runtimeTriMat[ti] = nm2->faceMaterial[ti];
                                        }
                                    }
                                }

                                // Safety fallback: some imported meshes collapse face-corner UVs to unit-square corners
                                // (0/1 only) which causes severe per-triangle checker distortion. If UV diversity is implausibly
                                // low for a larger indexed mesh, prefer indexed vertex UVs.
                                if (runtimeIndices.size() >= 96 && runtimeTriUvs.size() == runtimeIndices.size())
                                {
                                    std::set<uint32_t> uvKeys;
                                    for (const auto& uv : runtimeTriUvs)
                                    {
                                        int u = (int)std::lround(uv.x * 256.0f);
                                        int v = (int)std::lround(uv.y * 256.0f);
                                        uint32_t key = ((uint32_t)(u & 0xFFFF) << 16) | (uint32_t)(v & 0xFFFF);
                                        uvKeys.insert(key);
                                        if (uvKeys.size() > 8) break;
                                    }
                                    if (uvKeys.size() <= 4)
                                    {
                                        // Collapsed UV fallback: generate per-triangle dominant-axis planar UVs.
                                        float minX = runtimeVerts.empty() ? 0.0f : runtimeVerts[0].x;
                                        float maxX = minX;
                                        float minY = runtimeVerts.empty() ? 0.0f : runtimeVerts[0].y;
                                        float maxY = minY;
                                        float minZ = runtimeVerts.empty() ? 0.0f : runtimeVerts[0].z;
                                        float maxZ = minZ;
                                        for (const auto& p : runtimeVerts)
                                        {
                                            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
                                            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
                                            minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z);
                                        }
                                        float invX = (maxX - minX) > 1e-6f ? (1.0f / (maxX - minX)) : 1.0f;
                                        float invY = (maxY - minY) > 1e-6f ? (1.0f / (maxY - minY)) : 1.0f;
                                        float invZ = (maxZ - minZ) > 1e-6f ? (1.0f / (maxZ - minZ)) : 1.0f;

                                        runtimeTriUvs.clear();
                                        runtimeTriUvs.reserve(runtimeIndices.size());
                                        for (size_t ii = 0; ii + 2 < runtimeIndices.size(); ii += 3)
                                        {
                                            uint16_t ia = runtimeIndices[ii + 0];
                                            uint16_t ib = runtimeIndices[ii + 1];
                                            uint16_t ic = runtimeIndices[ii + 2];
                                            Vec3 pa = (ia < runtimeVerts.size()) ? runtimeVerts[ia] : Vec3{ 0,0,0 };
                                            Vec3 pb = (ib < runtimeVerts.size()) ? runtimeVerts[ib] : Vec3{ 0,0,0 };
                                            Vec3 pc = (ic < runtimeVerts.size()) ? runtimeVerts[ic] : Vec3{ 0,0,0 };

                                            Vec3 e1{ pb.x - pa.x, pb.y - pa.y, pb.z - pa.z };
                                            Vec3 e2{ pc.x - pa.x, pc.y - pa.y, pc.z - pa.z };
                                            Vec3 n{ e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
                                            float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);

                                            auto uvFrom = [&](const Vec3& p)->Vec3 {
                                                if (ax >= ay && ax >= az) return Vec3{ (p.z - minZ) * invZ, 1.0f - (p.y - minY) * invY, 0.0f };
                                                if (ay >= az)             return Vec3{ (p.x - minX) * invX, 1.0f - (p.z - minZ) * invZ, 0.0f };
                                                return Vec3{ (p.x - minX) * invX, 1.0f - (p.y - minY) * invY, 0.0f };
                                            };

                                            runtimeTriUvs.push_back(uvFrom(pa));
                                            runtimeTriUvs.push_back(uvFrom(pb));
                                            runtimeTriUvs.push_back(uvFrom(pc));
                                        }
                                    }
                                }
                            }

                            // Normalize triangle material ids to dense slot indices (0..N-1)
                            // so imported meshes with sparse/original material ids map correctly to
                            // editor-assigned material slot rows.
                            {
                                // Build stable mapping by ascending material id so slot rows match
                                // imported slot ordering (slot1->mat0, slot2->mat1, ...).
                                std::set<int> uniqueMats;
                                for (uint16_t tm : runtimeTriMat) uniqueMats.insert((int)tm);

                                std::unordered_map<int, int> triMatToSlot;
                                int nextSlot = 0;
                                for (int tm : uniqueMats)
                                {
                                    int assigned = nextSlot < kStaticMeshMaterialSlots ? nextSlot : (kStaticMeshMaterialSlots - 1);
                                    triMatToSlot[tm] = assigned;
                                    if (nextSlot < kStaticMeshMaterialSlots) nextSlot++;
                                }

                                for (size_t ti = 0; ti < runtimeTriMat.size(); ++ti)
                                {
                                    int tm = (int)runtimeTriMat[ti];
                                    auto it = triMatToSlot.find(tm);
                                    runtimeTriMat[ti] = (uint16_t)((it != triMatToSlot.end()) ? it->second : 0);
                                }
                            }

                            int runtimeSlotCount = 1;
                            for (uint16_t tm : runtimeTriMat) runtimeSlotCount = std::max(runtimeSlotCount, (int)tm + 1);
                            runtimeSlotCount = std::max(1, std::min(runtimeSlotCount, kStaticMeshMaterialSlots));

                            std::vector<int> runtimeSlotW((size_t)runtimeSlotCount, 64);
                            std::vector<int> runtimeSlotH((size_t)runtimeSlotCount, 64);
                            std::vector<float> runtimeSlotUScale((size_t)runtimeSlotCount, 1.0f);
                            std::vector<float> runtimeSlotVScale((size_t)runtimeSlotCount, 1.0f);
                            std::vector<int> runtimeSlotFilter((size_t)runtimeSlotCount, 1); // 0=nearest, 1=bilinear
                            std::vector<std::string> runtimeSlotDiskName((size_t)runtimeSlotCount);
                            // KOS strict: no per-slot material UV transform state in generated runtime.
                            std::vector<std::vector<uint16_t>> runtimeSlotTex((size_t)runtimeSlotCount);
                            auto fillCheckerSlot = [&](int si)
                            {
                                runtimeSlotW[(size_t)si] = 64;
                                runtimeSlotH[(size_t)si] = 64;
                                runtimeSlotUScale[(size_t)si] = 1.0f;
                                runtimeSlotVScale[(size_t)si] = 1.0f;
                                runtimeSlotFilter[(size_t)si] = 0; // checker/debug textures look best nearest
                                // KOS strict: no material UV transform state.
                                runtimeSlotTex[(size_t)si].assign(64 * 64, 0);
                                int r = 180 + (si * 29) % 60;
                                int g = 180 + (si * 47) % 60;
                                int b = 180 + (si * 13) % 60;
                                for (int y = 0; y < 64; ++y)
                                {
                                    for (int x = 0; x < 64; ++x)
                                    {
                                        int c = ((x >> 3) ^ (y >> 3)) & 1;
                                        int rr = c ? 255 : r;
                                        int gg = c ? 255 : g;
                                        int bb = c ? 255 : b;
                                        runtimeSlotTex[(size_t)si][(size_t)y * 64 + (size_t)x] = (uint16_t)(((rr >> 3) << 11) | ((gg >> 2) << 5) | (bb >> 3));
                                    }
                                }
                            };

                            auto loadNebTexNative = [&](const std::filesystem::path& texAbs, int& outW, int& outH, float& outUS, float& outVS, std::vector<uint16_t>& outPix)->bool
                            {
                                std::ifstream tin(texAbs, std::ios::binary | std::ios::in);
                                if (!tin.is_open()) return false;
                                char mag[4];
                                if (!tin.read(mag, 4)) return false;
                                if (!(mag[0] == 'N' && mag[1] == 'E' && mag[2] == 'B' && mag[3] == 'T')) return false;
                                uint16_t w = 0, h = 0, fmt = 0, flg = 0;
                                if (!ReadU16BE(tin, w) || !ReadU16BE(tin, h) || !ReadU16BE(tin, fmt) || !ReadU16BE(tin, flg)) return false;
                                if (fmt != 1 || w == 0 || h == 0) return false;

                                auto nextPow2 = [](int v) { int p = 1; while (p < v && p < 1024) p <<= 1; return p; };
                                int tw = nextPow2((int)w);
                                int th = nextPow2((int)h);
                                if (tw <= 0 || th <= 0 || tw > 1024 || th > 1024) return false;

                                std::vector<uint16_t> src((size_t)w * (size_t)h);
                                for (size_t i = 0; i < src.size(); ++i)
                                {
                                    uint16_t p = 0;
                                    if (!ReadU16BE(tin, p)) return false;
                                    uint8_t r5 = (uint8_t)((p >> 10) & 0x1F);
                                    uint8_t g5 = (uint8_t)((p >> 5) & 0x1F);
                                    uint8_t b5 = (uint8_t)(p & 0x1F);
                                    uint16_t g6 = (uint16_t)((g5 << 1) | (g5 >> 4));
                                    uint16_t rgb565 = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
                                    src[i] = rgb565;
                                }

                                int npotMode = LoadNebTexSaturnNpotMode(texAbs); // 0=pad, 1=resample
                                outW = tw; outH = th;
                                outPix.assign((size_t)tw * (size_t)th, 0);

                                if (npotMode == 1 && (w != tw || h != th))
                                {
                                    // Resample source into full POT texture domain.
                                    outUS = 1.0f;
                                    outVS = 1.0f;
                                    for (int y = 0; y < th; ++y)
                                    {
                                        int sy = std::min((int)h - 1, (int)((int64_t)y * (int64_t)h / std::max(1, th)));
                                        for (int x = 0; x < tw; ++x)
                                        {
                                            int sx = std::min((int)w - 1, (int)((int64_t)x * (int64_t)w / std::max(1, tw)));
                                            outPix[(size_t)y * (size_t)tw + (size_t)x] = src[(size_t)sy * (size_t)w + (size_t)sx];
                                        }
                                    }
                                }
                                else
                                {
                                    // Pad mode: preserve original texels, only fill top-left authored region.
                                    outUS = (float)w / (float)tw;
                                    outVS = (float)h / (float)th;
                                    for (int y = 0; y < (int)h; ++y)
                                    {
                                        for (int x = 0; x < (int)w; ++x)
                                        {
                                            outPix[(size_t)y * (size_t)tw + (size_t)x] = src[(size_t)y * (size_t)w + (size_t)x];
                                        }
                                    }
                                }
                                return true;
                            };

                            for (int si = 0; si < runtimeSlotCount; ++si)
                            {
                                bool loadedSlot = false;
                                std::string matRef;
                                if (si >= 0 && si < kStaticMeshMaterialSlots) matRef = meshSrc.materialSlots[si];
                                if (matRef.empty() && si == 0) matRef = meshSrc.material;

                                if (!matRef.empty())
                                {
                                    std::filesystem::path matAbs = std::filesystem::path(gProjectDir) / matRef;
                                    std::filesystem::path texAbs;
                                    if (matAbs.extension() == ".nebmat")
                                    {
                                        // KOS strict branch: ignore editor/Saturn-era UV transform fields in .nebmat.
                                        std::string texRel;
                                        if (LoadMaterialTexture(matAbs, texRel) && !texRel.empty()) texAbs = std::filesystem::path(gProjectDir) / texRel;
                                    }
                                    else if (matAbs.extension() == ".nebtex")
                                    {
                                        texAbs = matAbs;
                                    }

                                    if (!texAbs.empty() && std::filesystem::exists(texAbs))
                                    {
                                        loadedSlot = loadNebTexNative(texAbs, runtimeSlotW[(size_t)si], runtimeSlotH[(size_t)si], runtimeSlotUScale[(size_t)si], runtimeSlotVScale[(size_t)si], runtimeSlotTex[(size_t)si]);
                                        if (texAbs.extension() == ".nebtex")
                                        {
                                            runtimeSlotFilter[(size_t)si] = LoadNebTexFilterMode(texAbs);
                                            runtimeSlotDiskName[(size_t)si] = texAbs.filename().string();
                                        }
                                    }
                                }

                                if (!loadedSlot) fillCheckerSlot(si);

                                // slot mapping debug intentionally omitted in this path
                            }

                            std::vector<int> runtimeSlotFmt((size_t)runtimeSlotCount, 0); // 0=twiddled, 1=nontwiddled
                            std::vector<std::vector<uint16_t>> runtimeSlotTexUpload((size_t)runtimeSlotCount);
                            auto isPow2 = [](int v)->bool { return v > 0 && (v & (v - 1)) == 0; };
                            for (int si = 0; si < runtimeSlotCount; ++si)
                            {
                                int w = runtimeSlotW[(size_t)si], h = runtimeSlotH[(size_t)si];
                                const auto& src = runtimeSlotTex[(size_t)si];
                                auto& dst = runtimeSlotTexUpload[(size_t)si];
                                (void)isPow2; (void)w; (void)h;
                                runtimeSlotFmt[(size_t)si] = 1; // KOS strict validation path: force linear/nontwiddled for all slots
                                dst = src; // keep linear source order
                            }

                            std::string runtimeMeshDiskName;
                            if (!meshSrc.mesh.empty())
                                runtimeMeshDiskName = std::filesystem::path(meshSrc.mesh).filename().string();

                            std::filesystem::path cdDataDir = buildDir / "cd_root" / "data";
                            std::filesystem::path cdScenesDir = cdDataDir / "scenes";
                            std::filesystem::path cdMeshesDir = cdDataDir / "meshes";
                            std::filesystem::path cdTexturesDir = cdDataDir / "textures";
                            std::filesystem::path cdMatDir = cdDataDir / "materials";
                            std::filesystem::path cdVmuDir = cdDataDir / "vmu";
                            std::error_code stageEc;
                            std::filesystem::remove_all(cdScenesDir, stageEc);
                            std::filesystem::remove_all(cdMeshesDir, stageEc);
                            std::filesystem::remove_all(cdTexturesDir, stageEc);
                            std::filesystem::remove_all(cdMatDir, stageEc);
                            std::filesystem::remove_all(cdVmuDir, stageEc);
                            std::filesystem::create_directories(cdScenesDir);
                            std::filesystem::create_directories(cdMeshesDir);
                            std::filesystem::create_directories(cdTexturesDir);
                            std::filesystem::create_directories(cdMatDir);
                            std::filesystem::create_directories(cdVmuDir);

                            auto normalizeAbsKey = [](const std::filesystem::path& in)->std::string
                            {
                                std::error_code ec;
                                std::filesystem::path p = std::filesystem::weakly_canonical(in, ec);
                                if (ec) p = std::filesystem::absolute(in, ec);
                                if (ec) p = in;
                                return p.lexically_normal().generic_string();
                            };
                            auto stageUpperDiskNameFromAbsKey = [](const std::string& absKey)->std::string
                            {
                                std::string out = std::filesystem::path(absKey).filename().string();
                                if (out.empty()) out = "ASSET";
                                std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::toupper(c); });
                                return out;
                            };
                            auto stageShortDiskNameFromAbsKey = [](const std::string& absKey, const char* prefix, int ordinal)->std::string
                            {
                                std::string ext = std::filesystem::path(absKey).extension().string();
                                if (ext.empty()) ext = ".BIN";
                                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::toupper(c); });
                                char stem[16];
                                snprintf(stem, sizeof(stem), "%s%05d", (prefix && *prefix) ? prefix : "A", std::max(0, ordinal));
                                return std::string(stem) + ext;
                            };

                            std::vector<std::filesystem::path> sourceScenes;
                            {
                                std::set<std::string> dedup;
                                if (!defaultSceneCfg.empty())
                                {
                                    std::filesystem::path p(defaultSceneCfg);
                                    if (p.is_relative()) p = std::filesystem::path(gProjectDir) / p;
                                    std::string k = std::filesystem::weakly_canonical(p).generic_string();
                                    dedup.insert(k);
                                    sourceScenes.push_back(p);
                                }
                                std::filesystem::path scenesRoot = std::filesystem::path(gProjectDir) / "Assets" / "Scenes";
                                if (std::filesystem::exists(scenesRoot))
                                {
                                    for (const auto& e : std::filesystem::recursive_directory_iterator(scenesRoot))
                                    {
                                        if (!e.is_regular_file() || e.path().extension() != ".nebscene") continue;
                                        std::string k = std::filesystem::weakly_canonical(e.path()).generic_string();
                                        if (dedup.insert(k).second) sourceScenes.push_back(e.path());
                                    }
                                }
                            }
                            if (sourceScenes.empty())
                            {
                                std::filesystem::path fallbackScene = buildDir / "_runtime_default.nebscene";
                                SaveSceneToPath(fallbackScene, exportNodes, exportStatics, exportCameras, gNode3DNodes);
                                sourceScenes.push_back(fallbackScene);
                            }

                            struct LoadedSceneForStage
                            {
                                std::filesystem::path sourcePath;
                                SceneData data;
                            };
                            std::vector<LoadedSceneForStage> loadedScenes;
                            loadedScenes.reserve(sourceScenes.size());
                            for (const auto& scenePath : sourceScenes)
                            {
                                LoadedSceneForStage ls{};
                                ls.sourcePath = scenePath;
                                if (!LoadSceneFromPath(scenePath, ls.data)) continue;
                                loadedScenes.push_back(std::move(ls));
                            }

                            // Overlay in-memory editor scene data so unsaved/dirty mesh changes are included in DC build.
                            // This keeps staged .NEBSCENE content aligned with what user currently sees in editor.
                            {
                                auto normPathKey = [](const std::filesystem::path& p) -> std::string
                                {
                                    std::error_code ec;
                                    auto c = std::filesystem::weakly_canonical(p, ec);
                                    std::string k = (ec ? p.lexically_normal() : c).generic_string();
                                    std::transform(k.begin(), k.end(), k.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
                                    return k;
                                };

                                std::unordered_map<std::string, int> loadedIdxByKey;
                                for (int i = 0; i < (int)loadedScenes.size(); ++i)
                                {
                                    loadedIdxByKey[normPathKey(loadedScenes[i].sourcePath)] = i;
                                }

                                for (const auto& os : gOpenScenes)
                                {
                                    if (os.path.empty()) continue;
                                    std::string key = normPathKey(os.path);
                                    auto it = loadedIdxByKey.find(key);
                                    if (it != loadedIdxByKey.end())
                                    {
                                        loadedScenes[it->second].data = os;
                                        loadedScenes[it->second].sourcePath = os.path;
                                    }
                                }
                            }

                            // Bake parent hierarchy into world transforms for DC export.
                            // The DC runtime applies transforms flat (no parent chain walking),
                            // so we must flatten here to match what the editor renders.
                            for (auto& ls : loadedScenes)
                            {
                                auto findStaticByName = [&](const std::string& nm) -> int {
                                    for (int i = 0; i < (int)ls.data.staticMeshes.size(); ++i)
                                        if (ls.data.staticMeshes[i].name == nm) return i;
                                    return -1;
                                };
                                auto findNode3DByName = [&](const std::string& nm) -> int {
                                    for (int i = 0; i < (int)ls.data.node3d.size(); ++i)
                                        if (ls.data.node3d[i].name == nm) return i;
                                    return -1;
                                };
                                for (int i = 0; i < (int)ls.data.staticMeshes.size(); ++i)
                                {
                                    auto& sm = ls.data.staticMeshes[i];
                                    float wx = sm.x, wy = sm.y, wz = sm.z;
                                    float wrx = sm.rotX, wry = sm.rotY, wrz = sm.rotZ;
                                    float wsx = sm.scaleX, wsy = sm.scaleY, wsz = sm.scaleZ;
                                    std::string p = sm.parent;
                                    int guard = 0;
                                    while (!p.empty() && guard++ < 256)
                                    {
                                        int pi = findStaticByName(p);
                                        if (pi >= 0)
                                        {
                                            const auto& pn = ls.data.staticMeshes[pi];
                                            wx += pn.x; wy += pn.y; wz += pn.z;
                                            wrx += pn.rotX; wry += pn.rotY; wrz += pn.rotZ;
                                            wsx *= pn.scaleX; wsy *= pn.scaleY; wsz *= pn.scaleZ;
                                            p = pn.parent;
                                            continue;
                                        }
                                        int ni = findNode3DByName(p);
                                        if (ni >= 0)
                                        {
                                            const auto& pn = ls.data.node3d[ni];
                                            wx += pn.x; wy += pn.y; wz += pn.z;
                                            wrx += pn.rotX; wry += pn.rotY; wrz += pn.rotZ;
                                            wsx *= pn.scaleX; wsy *= pn.scaleY; wsz *= pn.scaleZ;
                                            p = pn.parent;
                                            continue;
                                        }
                                        break;
                                    }
                                    sm.x = wx; sm.y = wy; sm.z = wz;
                                    sm.rotX = wrx; sm.rotY = wry; sm.rotZ = wrz;
                                    sm.scaleX = wsx; sm.scaleY = wsy; sm.scaleZ = wsz;
                                }
                            }

                            std::set<std::string> sortedMeshAbs;
                            std::set<std::string> sortedTexAbs;
                            std::set<std::string> sortedMatAbs;
                            std::unordered_map<std::string, std::string> meshLogicalByAbs;
                            std::unordered_map<std::string, std::string> texLogicalByAbs;
                            printf("[DreamcastBuild] loadedScenes count: %d\n", (int)loadedScenes.size());
                            for (const auto& ls : loadedScenes)
                            {
                                printf("[DreamcastBuild]   scene '%s': %d staticMeshes, %d node3d\n",
                                    ls.sourcePath.string().c_str(),
                                    (int)ls.data.staticMeshes.size(),
                                    (int)ls.data.node3d.size());
                                for (const auto& sm : ls.data.staticMeshes)
                                {
                                    if (!sm.mesh.empty())
                                    {
                                        std::filesystem::path meshAbs = std::filesystem::path(gProjectDir) / sm.mesh;
                                        if (std::filesystem::exists(meshAbs))
                                        {
                                            std::string key = normalizeAbsKey(meshAbs);
                                            sortedMeshAbs.insert(key);
                                            if (meshLogicalByAbs.find(key) == meshLogicalByAbs.end())
                                            {
                                                std::string logical = std::filesystem::path(sm.mesh).filename().string();
                                                if (logical.empty()) logical = std::filesystem::path(key).filename().string();
                                                meshLogicalByAbs[key] = logical;
                                            }
                                        }
                                    }
                                    for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                                    {
                                        std::string matRef = sm.materialSlots[si];
                                        if (matRef.empty() && si == 0) matRef = sm.material;
                                        if (matRef.empty()) continue;

                                        std::filesystem::path matAbs = std::filesystem::path(gProjectDir) / matRef;
                                        std::filesystem::path texAbs;
                                        if (matAbs.extension() == ".nebmat")
                                        {
                                            if (std::filesystem::exists(matAbs))
                                            {
                                                std::string mkey = normalizeAbsKey(matAbs);
                                                sortedMatAbs.insert(mkey);
                                            }
                                            std::string texRel;
                                            if (LoadMaterialTexture(matAbs, texRel) && !texRel.empty())
                                                texAbs = std::filesystem::path(gProjectDir) / texRel;
                                        }
                                        else if (matAbs.extension() == ".nebtex")
                                        {
                                            texAbs = matAbs;
                                        }
                                        if (!texAbs.empty() && std::filesystem::exists(texAbs))
                                        {
                                            std::string key = normalizeAbsKey(texAbs);
                                            sortedTexAbs.insert(key);
                                            if (texLogicalByAbs.find(key) == texLogicalByAbs.end())
                                            {
                                                std::string logical = texAbs.filename().string();
                                                if (logical.empty()) logical = std::filesystem::path(key).filename().string();
                                                texLogicalByAbs[key] = logical;
                                            }
                                        }
                                    }
                                }
                            }

                            std::unordered_map<std::string, std::string> stagedMeshByAbs;
                            std::unordered_map<std::string, std::string> stagedTexByAbs;
                            std::unordered_map<std::string, std::string> stagedSceneByAbs;
                            std::vector<std::pair<std::string, std::string>> meshRefMapEntries;
                            std::vector<std::pair<std::string, std::string>> texRefMapEntries;
                            bool stagingNameCollision = false;
                            std::string stagingNameCollisionMessage;
                            {
                                std::unordered_map<std::string, std::string> meshAbsByOutName;
                                int meshOrdinal = 1;
                                for (const auto& key : sortedMeshAbs)
                                {
                                    std::string outName = stageShortDiskNameFromAbsKey(key, "M", meshOrdinal++);
                                    auto hit = meshAbsByOutName.find(outName);
                                    if (hit != meshAbsByOutName.end() && hit->second != key)
                                    {
                                        stagingNameCollision = true;
                                        stagingNameCollisionMessage = std::string("[DreamcastBuild] ERROR: mesh staging filename collision: ") + outName +
                                            " <= " + hit->second + " | " + key;
                                        break;
                                    }
                                    meshAbsByOutName[outName] = key;
                                    std::error_code ec;
                                    std::filesystem::copy_file(std::filesystem::path(key), cdMeshesDir / outName, std::filesystem::copy_options::overwrite_existing, ec);
                                    if (ec) continue;
                                    stagedMeshByAbs[key] = outName;
                                    meshRefMapEntries.push_back({ meshLogicalByAbs[key], outName });
                                    printf("[DreamcastBuild]   staged mesh: %s -> %s\n", meshLogicalByAbs[key].c_str(), outName.c_str());
                                }
                                printf("[DreamcastBuild] Total meshes collected: %d, staged: %d\n", (int)sortedMeshAbs.size(), (int)stagedMeshByAbs.size());
                                if (!stagingNameCollision)
                                {
                                    std::unordered_map<std::string, std::string> texAbsByOutName;
                                    int texOrdinal = 1;
                                    for (const auto& key : sortedTexAbs)
                                    {
                                        std::string outName = stageShortDiskNameFromAbsKey(key, "T", texOrdinal++);
                                        auto hit = texAbsByOutName.find(outName);
                                        if (hit != texAbsByOutName.end() && hit->second != key)
                                        {
                                            stagingNameCollision = true;
                                            stagingNameCollisionMessage = std::string("[DreamcastBuild] ERROR: texture staging filename collision: ") + outName +
                                                " <= " + hit->second + " | " + key;
                                            break;
                                        }
                                        texAbsByOutName[outName] = key;
                                        std::error_code ec;
                                        std::filesystem::copy_file(std::filesystem::path(key), cdTexturesDir / outName, std::filesystem::copy_options::overwrite_existing, ec);
                                        if (ec) continue;
                                        stagedTexByAbs[key] = outName;
                                        texRefMapEntries.push_back({ texLogicalByAbs[key], outName });
                                    }
                                }
                                if (!stagingNameCollision)
                                {
                                    std::unordered_map<std::string, std::string> matAbsByOutName;
                                    int matOrdinal = 1;
                                    for (const auto& key : sortedMatAbs)
                                    {
                                        std::string outName = stageShortDiskNameFromAbsKey(key, "MAT", matOrdinal++);
                                        auto hit = matAbsByOutName.find(outName);
                                        if (hit != matAbsByOutName.end() && hit->second != key)
                                        {
                                            stagingNameCollision = true;
                                            stagingNameCollisionMessage = std::string("[DreamcastBuild] ERROR: material staging filename collision: ") + outName +
                                                " <= " + hit->second + " | " + key;
                                            break;
                                        }
                                        matAbsByOutName[outName] = key;
                                        std::error_code ec;
                                        std::filesystem::copy_file(std::filesystem::path(key), cdMatDir / outName, std::filesystem::copy_options::overwrite_existing, ec);
                                        if (ec) continue;
                                    }
                                }
                                if (!stagingNameCollision)
                                {
                                    std::unordered_map<std::string, std::string> sceneAbsByOutName;
                                    int sceneOrdinal = 1;
                                    for (const auto& ls : loadedScenes)
                                    {
                                        std::string key = normalizeAbsKey(ls.sourcePath);
                                        std::string outName = stageShortDiskNameFromAbsKey(key, "S", sceneOrdinal++);
                                        auto hit = sceneAbsByOutName.find(outName);
                                        if (hit != sceneAbsByOutName.end() && hit->second != key)
                                        {
                                            stagingNameCollision = true;
                                            stagingNameCollisionMessage = std::string("[DreamcastBuild] ERROR: scene staging filename collision: ") + outName +
                                                " <= " + hit->second + " | " + key;
                                            break;
                                        }
                                        sceneAbsByOutName[outName] = key;
                                        stagedSceneByAbs[key] = outName;
                                    }
                                }
                            }
                            if (!meshSrc.mesh.empty())
                            {
                                std::string k = normalizeAbsKey(std::filesystem::path(gProjectDir) / meshSrc.mesh);
                                auto it = stagedMeshByAbs.find(k);
                                if (it != stagedMeshByAbs.end()) runtimeMeshDiskName = it->second;
                            }

                            std::vector<std::string> runtimeSceneFiles;
                            std::string defaultSceneRuntimeFile;
                            for (const auto& ls : loadedScenes)
                            {
                                const auto& scenePath = ls.sourcePath;
                                const auto& stagedScene = ls.data;

                                std::string sceneOutName;
                                {
                                    std::string sceneKey = normalizeAbsKey(scenePath);
                                    auto it = stagedSceneByAbs.find(sceneKey);
                                    if (it != stagedSceneByAbs.end()) sceneOutName = it->second;
                                    if (sceneOutName.empty()) sceneOutName = stageUpperDiskNameFromAbsKey(sceneKey);
                                }
                                std::filesystem::path sceneOutPath = cdScenesDir / sceneOutName;
                                std::ofstream so(sceneOutPath, std::ios::out | std::ios::trunc);
                                if (!so.is_open()) continue;

                                so << "scene=" << stagedScene.name << "\n";
                                int sceneWrittenMeshes = 0;
                                int sceneSkippedMeshes = 0;
                                for (const auto& sm : stagedScene.staticMeshes)
                                {
                                    std::filesystem::path meshAbs = std::filesystem::path(gProjectDir) / sm.mesh;
                                    std::string stagedMesh;
                                    {
                                        std::string key = normalizeAbsKey(meshAbs);
                                        auto it = stagedMeshByAbs.find(key);
                                        if (it != stagedMeshByAbs.end()) stagedMesh = it->second;
                                    }
                                    if (stagedMesh.empty()) { printf("[DreamcastBuild]   SKIP mesh '%s' (not staged)\n", sm.name.c_str()); ++sceneSkippedMeshes; continue; }

                                    std::array<std::string, kStaticMeshMaterialSlots> slotTexNames{};
                                    for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                                    {
                                        std::string matRef = sm.materialSlots[si];
                                        if (matRef.empty() && si == 0) matRef = sm.material;
                                        if (matRef.empty()) continue;

                                        std::filesystem::path matAbs = std::filesystem::path(gProjectDir) / matRef;
                                        std::filesystem::path texAbs;
                                        if (matAbs.extension() == ".nebmat")
                                        {
                                            std::string texRel;
                                            if (LoadMaterialTexture(matAbs, texRel) && !texRel.empty())
                                                texAbs = std::filesystem::path(gProjectDir) / texRel;
                                        }
                                        else if (matAbs.extension() == ".nebtex")
                                        {
                                            texAbs = matAbs;
                                        }
                                        if (!texAbs.empty())
                                        {
                                            std::string key = normalizeAbsKey(texAbs);
                                            auto it = stagedTexByAbs.find(key);
                                            if (it != stagedTexByAbs.end()) slotTexNames[(size_t)si] = it->second;
                                        }
                                    }

                                    so << "staticmesh " << stagedMesh << " "
                                       << sm.x << " " << sm.y << " " << sm.z << " "
                                       << sm.rotX << " " << sm.rotY << " " << sm.rotZ << " "
                                       << sm.scaleX << " " << sm.scaleY << " " << sm.scaleZ;
                                    for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                                        so << " " << (slotTexNames[(size_t)si].empty() ? "-" : slotTexNames[(size_t)si]);
                                    so << "\n";
                                    ++sceneWrittenMeshes;
                                }
                                printf("[DreamcastBuild] Scene '%s' -> %s: %d meshes written, %d skipped\n",
                                    stagedScene.name.c_str(), sceneOutName.c_str(), sceneWrittenMeshes, sceneSkippedMeshes);
                                runtimeSceneFiles.push_back(sceneOutName);
                                if (defaultSceneRuntimeFile.empty()) defaultSceneRuntimeFile = sceneOutName;
                            }
                            if (runtimeSceneFiles.empty())
                            {
                                runtimeSceneFiles.push_back("DEFAULT.NEBSCENE");
                                std::ofstream so(cdScenesDir / "DEFAULT.NEBSCENE", std::ios::out | std::ios::trunc);
                                if (so.is_open())
                                {
                                    so << "scene=Default\n";
                                    for (const auto& ls : loadedScenes)
                                    {
                                        for (const auto& sm : ls.data.staticMeshes)
                                        {
                                            std::filesystem::path meshAbs = std::filesystem::path(gProjectDir) / sm.mesh;
                                            std::string stagedMesh;
                                            {
                                                std::string key = normalizeAbsKey(meshAbs);
                                                auto it = stagedMeshByAbs.find(key);
                                                if (it != stagedMeshByAbs.end()) stagedMesh = it->second;
                                            }
                                            if (stagedMesh.empty()) continue;

                                            std::array<std::string, kStaticMeshMaterialSlots> slotTexNames{};
                                            for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                                            {
                                                std::string matRef = sm.materialSlots[si];
                                                if (matRef.empty() && si == 0) matRef = sm.material;
                                                if (matRef.empty()) continue;

                                                std::filesystem::path matAbs = std::filesystem::path(gProjectDir) / matRef;
                                                std::filesystem::path texAbs;
                                                if (matAbs.extension() == ".nebmat")
                                                {
                                                    std::string texRel;
                                                    if (LoadMaterialTexture(matAbs, texRel) && !texRel.empty())
                                                        texAbs = std::filesystem::path(gProjectDir) / texRel;
                                                }
                                                else if (matAbs.extension() == ".nebtex")
                                                {
                                                    texAbs = matAbs;
                                                }
                                                if (!texAbs.empty())
                                                {
                                                    std::string key = normalizeAbsKey(texAbs);
                                                    auto it = stagedTexByAbs.find(key);
                                                    if (it != stagedTexByAbs.end()) slotTexNames[(size_t)si] = it->second;
                                                }
                                            }

                                            so << "staticmesh " << stagedMesh << " "
                                               << sm.x << " " << sm.y << " " << sm.z << " "
                                               << sm.rotX << " " << sm.rotY << " " << sm.rotZ << " "
                                               << sm.scaleX << " " << sm.scaleY << " " << sm.scaleZ;
                                            for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                                                so << " " << (slotTexNames[(size_t)si].empty() ? "-" : slotTexNames[(size_t)si]);
                                            so << "\n";
                                        }
                                    }
                                }
                                defaultSceneRuntimeFile = "DEFAULT.NEBSCENE";
                            }

                            std::ofstream mc(runtimeCPath, std::ios::out | std::ios::trunc);
                            if (mc.is_open())
                            {
                                std::array<uint8_t, 48 * 32 / 8> vmuBootPacked{};
                                std::array<uint8_t, 48 * 32> vmuSourceMono = gVmuMono;
                                bool vmuSourceReady = gVmuHasImage;
                                if (gVmuLoadOnBoot)
                                {
                                    // Runtime source selection from persistent links: PNG link first, then linked FrameData layer range.
                                    std::string vmuErr;
                                    if (!gVmuLinkedPngPath.empty())
                                    {
                                        if (LoadVmuPngToMono(gVmuLinkedPngPath, vmuErr))
                                        {
                                            vmuSourceMono = gVmuMono;
                                            vmuSourceReady = gVmuHasImage;
                                        }
                                    }
                                    else
                                    {
                                        if (!gVmuLinkedAnimPath.empty())
                                        {
                                            LoadVmuFrameData(gVmuLinkedAnimPath);
                                        }
                                        std::string activeLayerAsset;
                                        for (int li = 0; li < (int)gVmuAnimLayers.size(); ++li)
                                        {
                                            const VmuAnimLayer& l = gVmuAnimLayers[(size_t)li];
                                            if (!l.visible || l.linkedAsset.empty()) continue;
                                            if (gVmuAnimPlayhead < l.frameStart || gVmuAnimPlayhead > l.frameEnd) continue;
                                            activeLayerAsset = l.linkedAsset;
                                        }
                                        if (!activeLayerAsset.empty() && LoadVmuPngToMono(activeLayerAsset, vmuErr))
                                        {
                                            vmuSourceMono = gVmuMono;
                                            vmuSourceReady = gVmuHasImage;
                                        }
                                    }
                                }

                                // Build baked VMU animation frames from linked layer ranges for Dreamcast runtime.
                                std::vector<std::array<uint8_t, 48 * 32 / 8>> vmuAnimPacked;
                                int vmuAnimEnabled = 0;
                                int vmuAnimFrameCount = std::max(1, gVmuAnimTotalFrames);
                                int vmuAnimLoopEnabled = gVmuAnimLoop ? 1 : 0;
                                int vmuAnimSpeedCode = std::max(0, std::min(gVmuAnimSpeedMode, 2));
                                {
                                    auto savedMono = gVmuMono;
                                    bool savedHas = gVmuHasImage;
                                    std::string savedPath = gVmuAssetPath;

                                    bool hasAnyLinkedLayer = false;
                                    for (const auto& l : gVmuAnimLayers)
                                        if (!l.linkedAsset.empty() && l.visible) { hasAnyLinkedLayer = true; break; }

                                    if (gVmuLoadOnBoot && hasAnyLinkedLayer)
                                    {
                                        vmuAnimEnabled = 1;
                                        vmuAnimPacked.resize((size_t)vmuAnimFrameCount);
                                        for (int f = 0; f < vmuAnimFrameCount; ++f)
                                        {
                                            std::array<uint8_t, 48 * 32 / 8> out{};
                                            std::string activeAsset;
                                            for (int li = 0; li < (int)gVmuAnimLayers.size(); ++li)
                                            {
                                                const VmuAnimLayer& l = gVmuAnimLayers[(size_t)li];
                                                if (!l.visible || l.linkedAsset.empty()) continue;
                                                if (f < l.frameStart || f > l.frameEnd) continue;
                                                activeAsset = l.linkedAsset;
                                            }

                                            if (!activeAsset.empty())
                                            {
                                                std::string err;
                                                if (LoadVmuPngToMono(activeAsset, err))
                                                {
                                                    for (int py = 0; py < 32; ++py)
                                                    {
                                                        for (int px = 0; px < 48; ++px)
                                                        {
                                                            if (gVmuMono[(size_t)py * 48u + (size_t)px])
                                                            {
                                                                const int dstY = 31 - py;
                                                                const int dstX = 47 - px;
                                                                const int bi = dstY * 6 + (dstX >> 3);
                                                                const int bit = 7 - (dstX & 7);
                                                                out[(size_t)bi] |= (uint8_t)(1u << bit);
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                            vmuAnimPacked[(size_t)f] = out;
                                        }
                                    }

                                    gVmuMono = savedMono;
                                    gVmuHasImage = savedHas;
                                    gVmuAssetPath = savedPath;
                                }

                                const int vmuBootEnabled = (gVmuLoadOnBoot && vmuSourceReady) ? 1 : 0;
                                if (vmuBootEnabled)
                                {
                                    for (int py = 0; py < 32; ++py)
                                    {
                                        for (int px = 0; px < 48; ++px)
                                        {
                                            if (vmuSourceMono[(size_t)py * 48u + (size_t)px])
                                            {
                                                const int dstY = 31 - py; // VMU LCD memory is vertically inverted vs editor preview.
                                                const int dstX = 47 - px; // VMU LCD horizontal orientation fix.
                                                const int bi = dstY * 6 + (dstX >> 3);
                                                const int bit = 7 - (dstX & 7);
                                                vmuBootPacked[(size_t)bi] |= (uint8_t)(1u << bit);
                                            }
                                        }
                                    }
                                }

                                // Disk-only VMU payload staging for runtime reads.
                                {
                                    std::filesystem::path vmuBootPath = cdVmuDir / "vmu_boot.bin";
                                    std::ofstream vb(vmuBootPath, std::ios::binary | std::ios::out | std::ios::trunc);
                                    if (vb.is_open()) vb.write((const char*)vmuBootPacked.data(), (std::streamsize)vmuBootPacked.size());

                                    std::filesystem::path vmuAnimPath = cdVmuDir / "vmu_anim.bin";
                                    std::ofstream va(vmuAnimPath, std::ios::binary | std::ios::out | std::ios::trunc);
                                    if (va.is_open() && !vmuAnimPacked.empty())
                                    {
                                        for (const auto& fr : vmuAnimPacked)
                                            va.write((const char*)fr.data(), (std::streamsize)fr.size());
                                    }
                                }

                                mc << "#include <kos.h>\n";
                                mc << "#include <dc/pvr.h>\n";
                                mc << "#include <dc/maple.h>\n";
                                mc << "#include <dc/maple/vmu.h>\n";
                                mc << "#include <math.h>\n";
                                mc << "#include <stdio.h>\n";
                                mc << "#include <stdlib.h>\n";
                                mc << "#include <string.h>\n";
                                mc << "#include <stdint.h>\n";
                                mc << "#include \"KosInput.h\"\n";
                                mc << "#include \"KosBindings.h\"\n";
                                mc << "\n";
                                mc << "extern void NB_Game_OnStart(void);\n";
                                mc << "extern void NB_Game_OnUpdate(float dt);\n";
                                mc << "extern void NB_Game_OnSceneSwitch(const char* sceneName);\n";
                                mc << "\n";
                                mc << "KOS_INIT_FLAGS(INIT_DEFAULT);\n";
                                mc << "\n";
                                mc << "typedef NB_Vec3 V3;\n";
                                mc << "typedef struct { float x,y,z,u,v; } SV;\n";
                                mc << "typedef NB_Mesh RuntimeMesh;\n";
                                mc << "typedef NB_Texture RuntimeTex;\n";
                                mc << "static inline unsigned twid(unsigned x, unsigned y) { unsigned z=0; for (unsigned b=0; b<16; ++b) { z |= ((x>>b)&1u) << (2u*b); z |= ((y>>b)&1u) << (2u*b+1u); } return z; }\n";
                                mc << "static char dc_uc(char c){ return (c>='a'&&c<='z')?(char)(c-('a'-'A')):c; }\n";
                                mc << "static int dc_eq_nocase(const char* a, const char* b){ if(!a||!b) return 0; while(*a&&*b){ if(dc_uc(*a)!=dc_uc(*b)) return 0; ++a; ++b; } return (*a==0&&*b==0)?1:0; }\n";
                                mc << "static FILE* dc_try_open_rb(const char* cand, char* outPath, int outPathSize){ FILE* f=fopen(cand,\"rb\"); if(f&&outPath&&outPathSize>0){ strncpy(outPath,cand,(size_t)outPathSize-1u); outPath[(size_t)outPathSize-1u]=0; } return f; }\n";
                                mc << "static FILE* dc_fopen_iso_compat(const char* path, char* outPath, int outPathSize){ if(!path||!path[0]) return 0; char cand[512]; FILE* f=0; strncpy(cand,path,sizeof(cand)-1u); cand[sizeof(cand)-1u]=0; f=dc_try_open_rb(cand,outPath,outPathSize); if(f) return f; strncpy(cand,path,sizeof(cand)-1u); cand[sizeof(cand)-1u]=0; for(size_t i=0;cand[i];++i) cand[i]=dc_uc(cand[i]); f=dc_try_open_rb(cand,outPath,outPathSize); if(f) return f; strncpy(cand,path,sizeof(cand)-1u); cand[sizeof(cand)-1u]=0; char* slash=strrchr(cand,'/'); char* bname=slash?slash+1:cand; char* dot=strrchr(bname,'.'); if(dot&&dc_eq_nocase(dot,\".nebmesh\")){ for(char* p=bname;p<dot;++p) *p=dc_uc(*p); strcpy(dot,\".NEBMESH\"); f=dc_try_open_rb(cand,outPath,outPathSize); if(f) return f; strcpy(dot,\".nebmesh\"); f=dc_try_open_rb(cand,outPath,outPathSize); if(f) return f; } else if(dot&&dc_eq_nocase(dot,\".nebtex\")){ for(char* p=bname;p<dot;++p) *p=dc_uc(*p); strcpy(dot,\".NEBTEX\"); f=dc_try_open_rb(cand,outPath,outPathSize); if(f) return f; strcpy(dot,\".nebtex\"); f=dc_try_open_rb(cand,outPath,outPathSize); if(f) return f; } return 0; }\n";
                                mc << "static int dc_try_load_nebmesh(const char* path, RuntimeMesh* out){ char resolved[512]; FILE* fp=dc_fopen_iso_compat(path,resolved,(int)sizeof(resolved)); if(!fp) return 0; fclose(fp); return NB_DC_LoadMesh(resolved, out); }\n";
                                mc << "static void dc_free_mesh(RuntimeMesh* m){ NB_DC_FreeMesh(m); }\n";
                                mc << "static int dc_try_load_nebtex(const char* path, RuntimeTex* out){ char resolved[512]; FILE* fp=dc_fopen_iso_compat(path,resolved,(int)sizeof(resolved)); if(!fp) return 0; fclose(fp); return NB_DC_LoadTexture(resolved, out); }\n";
                                mc << "static void dc_free_tex(RuntimeTex* t){ NB_DC_FreeTexture(t); }\n";
                                mc << "\n";
                                auto fstr = [](float v) { return std::to_string(v) + "f"; };
                                mc << "/* Dreamcast camera defaults come from scene camera export (+ optional neutral calibration offsets). */\n";
                                mc << "static const float kCamPosInit[3] = {" << fstr(dcView.eye.x) << "," << fstr(dcView.eye.y) << "," << fstr(dcView.eye.z) << "};\n";
                                mc << "static const float kCamTargetInit[3] = {" << fstr(dcView.target.x) << "," << fstr(dcView.target.y) << "," << fstr(dcView.target.z) << "};\n";
                                mc << "static const float kCamOrbitInit[3] = {" << fstr(camSrc.orbitX) << "," << fstr(camSrc.orbitY) << "," << fstr(camSrc.orbitZ) << "};\n";
                                mc << "static float gPivotOffset[3] = {0.0f,1.2f,0.0f};\n";
                                mc << "static float gCamPos[3] = {" << fstr(dcView.eye.x) << "," << fstr(dcView.eye.y) << "," << fstr(dcView.eye.z) << "};\n";
                                mc << "static float gCamForward[3] = {" << fstr(dcView.basis.forward.x) << "," << fstr(dcView.basis.forward.y) << "," << fstr(dcView.basis.forward.z) << "};\n";
                                mc << "static float gCamRight[3] = {" << fstr(dcView.basis.right.x) << "," << fstr(dcView.basis.right.y) << "," << fstr(dcView.basis.right.z) << "};\n";
                                mc << "static float gCamUp[3] = {" << fstr(dcView.basis.up.x) << "," << fstr(dcView.basis.up.y) << "," << fstr(dcView.basis.up.z) << "};\n";
                                mc << "static const float kProjFovYDeg = " << fstr(dcProj.fovYDeg) << ";\n";
                                mc << "static const float kProjAspect = " << fstr(dcProj.aspect) << ";\n";
                                mc << "static const float kProjNear = " << fstr(dcProj.nearZ) << ";\n";
                                mc << "static const float kProjFar = " << fstr(dcProj.farZ) << ";\n";
                                mc << "static const float kProjViewW = " << fstr(dcViewW) << ";\n";
                                mc << "static const float kProjViewH = " << fstr(dcViewH) << ";\n";
                                mc << "static const float kProjFocalX = " << fstr(dcFocalX) << ";\n";
                                mc << "static const float kProjFocalY = " << fstr(dcFocalY) << ";\n";
                                mc << "static const float kCamRot[3] = {" << fstr(camSrc.rotX) << "," << fstr(camSrc.rotY) << "," << fstr(camSrc.rotZ) << "};\n";
                                mc << "static float gMeshPos[3] = {" << fstr(meshSrc.x) << "," << fstr(meshSrc.y) << "," << fstr(meshSrc.z) << "};\n";
                                mc << "static float gMeshRot[3] = {" << fstr(meshSrc.rotX) << "," << fstr(meshSrc.rotY) << "," << fstr(meshSrc.rotZ) << "};\n";
                                mc << "static float gMeshScale[3] = {" << fstr(meshSrc.scaleX) << "," << fstr(meshSrc.scaleY) << "," << fstr(meshSrc.scaleZ) << "};\n";
                                mc << "static const char kPlayerMeshDisk[] = \"" << runtimeMeshDiskName << "\";\n";
                                mc << "static int gPlayerMeshIdx = -1;\n";
                                mc << "void NB_RT_GetMeshPosition(float outPos[3]){ if(!outPos) return; outPos[0]=gMeshPos[0]; outPos[1]=gMeshPos[1]; outPos[2]=gMeshPos[2]; }\n";
                                mc << "void NB_RT_SetMeshPosition(float x,float y,float z){ gMeshPos[0]=x; gMeshPos[1]=y; gMeshPos[2]=z; }\n";
                                mc << "void NB_RT_AddMeshPositionDelta(float dx,float dy,float dz){ gMeshPos[0]+=dx; gMeshPos[1]+=dy; gMeshPos[2]+=dz; }\n";
                                mc << "\n";
                                mc << "/* Script runtime bridge (name-tolerant fallback for DC runtime) */\n";
                                mc << "static float gRtOrbit[3] = {0.0f, 0.0f, 0.0f};\n";
                                mc << "static float gRtCamRot[3] = {0.0f, 0.0f, 0.0f};\n";
                                mc << "static int gOrbitInited = 0;\n";
                                mc << "static int gFollowAlign = 0;\n";
                                mc << "static const int kDcDebug = 0;\n";
                                mc << "void NB_RT_GetNode3DPosition(const char* name, float outPos[3]){ (void)name; if(!outPos) return; outPos[0]=gMeshPos[0]; outPos[1]=gMeshPos[1]; outPos[2]=gMeshPos[2]; }\n";
                                mc << "void NB_RT_SetNode3DPosition(const char* name, float x, float y, float z){ (void)name; gMeshPos[0]=x; gMeshPos[1]=y; gMeshPos[2]=z; }\n";
                                mc << "void NB_RT_GetNode3DRotation(const char* name, float outRot[3]){ (void)name; if(!outRot) return; outRot[0]=gMeshRot[0]; outRot[1]=gMeshRot[1]; outRot[2]=gMeshRot[2]; }\n";
                                mc << "void NB_RT_SetNode3DRotation(const char* name, float x, float y, float z){ (void)name; gMeshRot[0]=x; gMeshRot[1]=y; gMeshRot[2]=z; }\n";
                                mc << "void NB_RT_GetCameraOrbit(const char* name, float outOrbit[3]){ (void)name; if(!outOrbit) return; outOrbit[0]=gRtOrbit[0]; outOrbit[1]=gRtOrbit[1]; outOrbit[2]=gRtOrbit[2]; }\n";
                                mc << "void NB_RT_SetCameraOrbit(const char* name, float x, float y, float z){ (void)name; gRtOrbit[0]=x; gRtOrbit[1]=y; gRtOrbit[2]=z; gOrbitInited=1; { float tx=gMeshPos[0], ty=gMeshPos[1]+1.2f, tz=gMeshPos[2]; gCamPos[0]=tx + gRtOrbit[0]; gCamPos[1]=ty + gRtOrbit[1]; gCamPos[2]=tz + gRtOrbit[2]; { float fx=tx-gCamPos[0], fy=ty-gCamPos[1], fz=tz-gCamPos[2]; float fl=sqrtf(fx*fx+fy*fy+fz*fz); if(fl<1e-6f) fl=1.0f; fx/=fl; fy/=fl; fz/=fl; gCamForward[0]=fx; gCamForward[1]=fy; gCamForward[2]=fz; gCamUp[0]=0.0f; gCamUp[1]=1.0f; gCamUp[2]=0.0f; { float rx=-fz, ry=0.0f, rz=fx; float rl=sqrtf(rx*rx+ry*ry+rz*rz); if(rl<1e-6f){ rx=1.0f; ry=0.0f; rz=0.0f; rl=1.0f; } gCamRight[0]=rx/rl; gCamRight[1]=ry/rl; gCamRight[2]=rz/rl; } } } }\n";
                                mc << "void NB_RT_GetCameraRotation(const char* name, float outRot[3]){ (void)name; if(!outRot) return; outRot[0]=gRtCamRot[0]; outRot[1]=gRtCamRot[1]; outRot[2]=gRtCamRot[2]; }\n";
                                mc << "void NB_RT_SetCameraRotation(const char* name, float x, float y, float z){ (void)name; gRtCamRot[0]=x; gRtCamRot[1]=y; gRtCamRot[2]=z; { float rx=x*0.0174532925f, ry=y*0.0174532925f; float cx=cosf(rx), sx=sinf(rx), cy=cosf(ry), sy=sinf(ry); float fx=sy*cx, fy=-sx, fz=cy*cx; float fl=sqrtf(fx*fx+fy*fy+fz*fz); if(fl<1e-6f) fl=1.0f; fx/=fl; fy/=fl; fz/=fl; gCamForward[0]=fx; gCamForward[1]=fy; gCamForward[2]=fz; gCamUp[0]=0.0f; gCamUp[1]=1.0f; gCamUp[2]=0.0f; float rxv = gCamUp[1]*fz - gCamUp[2]*fy; float ryv = gCamUp[2]*fx - gCamUp[0]*fz; float rzv = gCamUp[0]*fy - gCamUp[1]*fx; float rl=sqrtf(rxv*rxv+ryv*ryv+rzv*rzv); if(rl<1e-6f){ rxv=1.0f; ryv=0.0f; rzv=0.0f; rl=1.0f; } gCamRight[0]=rxv/rl; gCamRight[1]=ryv/rl; gCamRight[2]=rzv/rl; } }\n";
                                mc << "void NB_RT_GetCameraWorldForward(const char* name, float outFwd[3]){ (void)name; if(!outFwd) return; outFwd[0]=gCamForward[0]; outFwd[1]=gCamForward[1]; outFwd[2]=gCamForward[2]; }\n";
                                mc << "int NB_RT_IsCameraUnderNode3D(const char* cameraName, const char* nodeName){ (void)cameraName; (void)nodeName; return 1; }\n";
                                mc << "static int gMirrorX = 1;\n";
                                mc << "static int gMirrorY = 1;\n";
                                mc << "static int gMirrorZ = 1;\n";
                                mc << "static int gMirrorLrIndex = 0;\n";
                                mc << "static const int kVmuLoadOnBoot = " << vmuBootEnabled << ";\n";
                                mc << "static const uint8_t kVmuBootPng[192] = {";
                                for (size_t vb = 0; vb < vmuBootPacked.size(); ++vb)
                                {
                                    mc << (int)vmuBootPacked[vb];
                                    if (vb + 1 < vmuBootPacked.size()) mc << ",";
                                }
                                mc << "};\n";
                                mc << "static const int kVmuAnimEnabled = " << vmuAnimEnabled << ";\n";
                                mc << "static const int kVmuAnimLoop = " << vmuAnimLoopEnabled << ";\n";
                                mc << "static const int kVmuAnimSpeedCode = " << vmuAnimSpeedCode << ";\n";
                                mc << "static const int kVmuAnimFrameCount = " << vmuAnimFrameCount << ";\n";
                                mc << "static const uint8_t kVmuAnimFrames[] = {";
                                if (!vmuAnimPacked.empty())
                                {
                                    for (size_t fi = 0; fi < vmuAnimPacked.size(); ++fi)
                                    {
                                        for (size_t bi = 0; bi < vmuAnimPacked[fi].size(); ++bi)
                                        {
                                            mc << (int)vmuAnimPacked[fi][bi];
                                            if (!(fi + 1 == vmuAnimPacked.size() && bi + 1 == vmuAnimPacked[fi].size())) mc << ",";
                                        }
                                    }
                                }
                                mc << "};\n";
                                mc << "static void NB_TryLoadVmuBootImage(void){\n";
                                mc << "  static uintptr_t sLastVmu = 0;\n";
                                mc << "  if(!kVmuLoadOnBoot) return;\n";
                                mc << "  maple_device_t* vmu = 0;\n";
                                mc << "  for(int i=0;i<8;++i){ maple_device_t* d = maple_enum_type(i, MAPLE_FUNC_LCD); if(d){ vmu = d; break; } }\n";
                                mc << "  uintptr_t cur = (uintptr_t)vmu;\n";
                                mc << "  if(cur == sLastVmu) return;\n";
                                mc << "  sLastVmu = cur;\n";
                                mc << "  if(!vmu){ dbgio_printf(\"[VMU] VMU LCD disconnected\\n\"); return; }\n";
                                mc << "  uint8_t diskBoot[192];\n";
                                mc << "  const uint8_t* src = kVmuBootPng;\n";
                                mc << "  FILE* fb = fopen(\"/cd/data/vmu/vmu_boot.bin\",\"rb\");\n";
                                mc << "  if(fb){ size_t n=fread(diskBoot,1,sizeof(diskBoot),fb); fclose(fb); if(n==sizeof(diskBoot)) src=diskBoot; }\n";
                                mc << "  if(kVmuAnimEnabled && kVmuAnimFrameCount > 0) src = &kVmuAnimFrames[0];\n";
                                mc << "  int rc = vmu_draw_lcd(vmu, (void*)src);\n";
                                mc << "  dbgio_printf(\"[VMU] load-on-boot draw rc=%d\\n\", rc);\n";
                                mc << "}\n";
                                mc << "static void NB_UpdateVmuAnim(float dt){\n";
                                mc << "  static maple_device_t* sVmu = 0;\n";
                                mc << "  static float sAccum = 0.0f;\n";
                                mc << "  static int sFrame = 0;\n";
                                mc << "  static int sDoneOneShot = 0;\n";
                                mc << "  static uint8_t* sAnimDisk = 0;\n";
                                mc << "  static int sAnimDiskFrames = 0;\n";
                                mc << "  static int sAnimTriedLoad = 0;\n";
                                mc << "  if(!kVmuLoadOnBoot || !kVmuAnimEnabled || kVmuAnimFrameCount <= 0) return;\n";
                                mc << "  if(!kVmuAnimLoop && sDoneOneShot) return;\n";
                                mc << "  if(!sAnimTriedLoad){\n";
                                mc << "    sAnimTriedLoad = 1;\n";
                                mc << "    FILE* fa=fopen(\"/cd/data/vmu/vmu_anim.bin\",\"rb\");\n";
                                mc << "    if(fa){ fseek(fa,0,SEEK_END); long sz=ftell(fa); fseek(fa,0,SEEK_SET); if(sz>=192){ sAnimDisk=(uint8_t*)malloc((size_t)sz); if(sAnimDisk){ size_t n=fread(sAnimDisk,1,(size_t)sz,fa); if((long)n==sz) sAnimDiskFrames=(int)(sz/192); else { free(sAnimDisk); sAnimDisk=0; } } } fclose(fa); }\n";
                                mc << "  }\n";
                                mc << "  if(!sVmu){ for(int i=0;i<8;++i){ maple_device_t* d=maple_enum_type(i, MAPLE_FUNC_LCD); if(d){ sVmu=d; break; } } }\n";
                                mc << "  if(!sVmu) return;\n";
                                mc << "  float fps = 8.0f; if(kVmuAnimSpeedCode==0) fps = 4.0f; else if(kVmuAnimSpeedCode==2) fps = 16.0f;\n";
                                mc << "  float step = 1.0f / fps;\n";
                                mc << "  int frameCount = (sAnimDiskFrames > 0) ? sAnimDiskFrames : kVmuAnimFrameCount;\n";
                                mc << "  sAccum += dt;\n";
                                mc << "  while(sAccum >= step){\n";
                                mc << "    sAccum -= step;\n";
                                mc << "    const uint8_t* frame = (sAnimDiskFrames > 0) ? (sAnimDisk + (size_t)sFrame * 192u) : (&kVmuAnimFrames[(size_t)sFrame * 192u]);\n";
                                mc << "    vmu_draw_lcd(sVmu, (void*)frame);\n";
                                mc << "    sFrame++;\n";
                                mc << "    if(sFrame >= frameCount){ sFrame = 0; if(!kVmuAnimLoop){ sDoneOneShot = 1; break; } }\n";
                                mc << "  }\n";
                                mc << "}\n";
                                mc << "static void NB_SetMirrorFromIndex(int idx){\n";
                                mc << "  idx &= 7;\n";
                                mc << "  gMirrorX = (idx & 1) ? -1 : 1;\n";
                                mc << "  gMirrorY = (idx & 2) ? -1 : 1;\n";
                                mc << "  gMirrorZ = (idx & 4) ? -1 : 1;\n";
                                mc << "  gMirrorLrIndex = idx;\n";
                                mc << "  dbgio_printf(\"[Mirror] idx=%d => X=%d Y=%d Z=%d\\n\", gMirrorLrIndex, gMirrorX, gMirrorY, gMirrorZ);\n";
                                mc << "}\n";
                                mc << "enum { MAX_SLOT = 16, MAX_MESHES = 64 };\n";
                                mc << "typedef struct { char meshDisk[128]; char meshLogical[128]; float pos[3]; float rot[3]; float scale[3]; char texDisk[MAX_SLOT][128]; char texLogical[MAX_SLOT][128]; } SceneMeshMeta;\n";
                                mc << "static SceneMeshMeta gSceneMeshes[MAX_MESHES];\n";
                                mc << "static int gSceneMeshCount = 0;\n";
                                mc << "static char gSceneName[64] = \"Default\";\n";
                                mc << "static int gSceneIndex = 0;\n";
                                mc << "static const char* gSceneFiles[] = {";
                                for (size_t si = 0; si < runtimeSceneFiles.size(); ++si)
                                {
                                    mc << "\"" << runtimeSceneFiles[si] << "\"";
                                    if (si + 1 < runtimeSceneFiles.size()) mc << ",";
                                }
                                mc << "};\n";
                                mc << "static const int gSceneCount = " << (int)runtimeSceneFiles.size() << ";\n";
                                mc << "static const char* kDefaultSceneFile = \"" << defaultSceneRuntimeFile << "\";\n";
                                mc << "typedef struct { const char* logical; const char* staged; } NB_RefMap;\n";
                                mc << "static const NB_RefMap kMeshRefMap[] = {";
                                for (size_t mi = 0; mi < meshRefMapEntries.size(); ++mi)
                                {
                                    mc << "{\"" << meshRefMapEntries[mi].first << "\",\"" << meshRefMapEntries[mi].second << "\"}";
                                    if (mi + 1 < meshRefMapEntries.size()) mc << ",";
                                }
                                mc << "};\n";
                                mc << "static const int kMeshRefMapCount = " << (int)meshRefMapEntries.size() << ";\n";
                                mc << "static const NB_RefMap kTexRefMap[] = {";
                                for (size_t ti = 0; ti < texRefMapEntries.size(); ++ti)
                                {
                                    mc << "{\"" << texRefMapEntries[ti].first << "\",\"" << texRefMapEntries[ti].second << "\"}";
                                    if (ti + 1 < texRefMapEntries.size()) mc << ",";
                                }
                                mc << "};\n";
                                mc << "static const int kTexRefMapCount = " << (int)texRefMapEntries.size() << ";\n";
                                mc << "static const char* NB_ResolveMappedRef(const char* logical, const NB_RefMap* map, int count){ if(!logical||!logical[0]||!map||count<=0) return logical; for(int i=0;i<count;++i){ if((map[i].logical&&dc_eq_nocase(logical,map[i].logical))||(map[i].staged&&dc_eq_nocase(logical,map[i].staged))) return map[i].staged; } const char* slash=strrchr(logical,'/'); const char* name=slash?slash+1:logical; for(int i=0;i<count;++i){ if((map[i].logical&&dc_eq_nocase(name,map[i].logical))||(map[i].staged&&dc_eq_nocase(name,map[i].staged))) return map[i].staged; } return logical; }\n";
                                mc << "static int NB_ApplyLoadedSceneState(void){ int meshCount=NB_DC_GetSceneMeshCount(); if(meshCount<=0) return 0; if(meshCount>MAX_MESHES) meshCount=MAX_MESHES; gSceneMeshCount=meshCount; for(int mi=0; mi<gSceneMeshCount; ++mi){ SceneMeshMeta* sm=&gSceneMeshes[mi]; memset(sm,0,sizeof(*sm)); { const char* mesh=NB_DC_GetSceneMeshPathAt(mi); if(mesh&&mesh[0]){ strncpy(sm->meshLogical,mesh,sizeof(sm->meshLogical)-1); const char* rm=NB_ResolveMappedRef(mesh,kMeshRefMap,kMeshRefMapCount); strncpy(sm->meshDisk,rm?rm:mesh,sizeof(sm->meshDisk)-1); } } { float p[3]={0}, r[3]={0}, s[3]={1,1,1}; NB_DC_GetSceneTransformAt(mi,p,r,s); sm->pos[0]=p[0]; sm->pos[1]=p[1]; sm->pos[2]=p[2]; sm->rot[0]=r[0]; sm->rot[1]=r[1]; sm->rot[2]=r[2]; sm->scale[0]=s[0]; sm->scale[1]=s[1]; sm->scale[2]=s[2]; } for(int i=0;i<MAX_SLOT;++i){ const char* tp=NB_DC_GetSceneTexturePathAt(mi,i); if(tp&&tp[0]){ strncpy(sm->texLogical[i],tp,127); const char* rt=NB_ResolveMappedRef(tp,kTexRefMap,kTexRefMapCount); strncpy(sm->texDisk[i],rt?rt:tp,127); } } } { const char* nm=NB_DC_GetSceneName(); if(nm&&nm[0]){ strncpy(gSceneName,nm,sizeof(gSceneName)-1); gSceneName[sizeof(gSceneName)-1]=0; } } if(gSceneMeshCount>0){ gMeshPos[0]=gSceneMeshes[0].pos[0]; gMeshPos[1]=gSceneMeshes[0].pos[1]; gMeshPos[2]=gSceneMeshes[0].pos[2]; gMeshRot[0]=gSceneMeshes[0].rot[0]; gMeshRot[1]=gSceneMeshes[0].rot[1]; gMeshRot[2]=gSceneMeshes[0].rot[2]; gMeshScale[0]=gSceneMeshes[0].scale[0]; gMeshScale[1]=gSceneMeshes[0].scale[1]; gMeshScale[2]=gSceneMeshes[0].scale[2]; } return 1; }\n";
                                mc << "static int NB_LoadScene(const char* sceneFile){ if(!sceneFile||!sceneFile[0]) return 0; char path[256]; snprintf(path,sizeof(path),\"/cd/data/scenes/%s\",sceneFile); if(!NB_DC_LoadScene(path)) return 0; return NB_ApplyLoadedSceneState(); }\n";
                                mc << "static int NB_LoadSceneIndex(int idx){ if(gSceneCount<=0) return 0; while(idx<0) idx+=gSceneCount; idx%=gSceneCount; char path[256]; snprintf(path,sizeof(path),\"/cd/data/scenes/%s\",gSceneFiles[idx]); if(!NB_DC_SwitchScene(path)) return 0; if(!NB_ApplyLoadedSceneState()) return 0; gSceneIndex=idx; return 1; }\n";
                                mc << "static int NB_NextScene(void){ return NB_LoadSceneIndex(gSceneIndex+1); }\n";
                                mc << "static int NB_PrevScene(void){ return NB_LoadSceneIndex(gSceneIndex-1); }\n";
                                mc << "\n";
                                mc << "static inline float deg2rad(float d){ return d*0.0174532925f; }\n";
                                mc << "\n";
                                mc << "static V3 rot_xyz(V3 v, float rx, float ry, float rz) {\n";
                                mc << "  float sx=sinf(rx), cx=cosf(rx), sy=sinf(ry), cy=cosf(ry), sz=sinf(rz), cz=cosf(rz);\n";
                                mc << "  float x=v.x*cz - v.y*sz, y=v.x*sz + v.y*cz; v.x=x; v.y=y;\n";
                                mc << "  x=v.x*cy + v.z*sy; float z=-v.x*sy + v.z*cy; v.x=x; v.z=z;\n";
                                mc << "  y=v.y*cx - v.z*sx; z=v.y*sx + v.z*cx; v.y=y; v.z=z;\n";
                                mc << "  return v;\n";
                                mc << "}\n";
                                mc << "\n";
                                mc << "static float dot3(V3 a, V3 b){ return a.x*b.x + a.y*b.y + a.z*b.z; }\n";
                                mc << "static V3 sub3(V3 a, V3 b){ V3 r={a.x-b.x,a.y-b.y,a.z-b.z}; return r; }\n";
                                mc << "static V3 cross3(V3 a, V3 b){ V3 r={a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; return r; }\n";
                                mc << "static V3 norm3(V3 v){ float m=sqrtf(v.x*v.x+v.y*v.y+v.z*v.z); if(m<1e-6f){V3 z={0,0,1}; return z;} V3 r={v.x/m,v.y/m,v.z/m}; return r; }\n";
                                mc << "\n";
                                mc << "/* Camera basis (cached per frame) */\n";
                                mc << "static V3 gBF,gBR,gBU,gBE;\n";
                                mc << "static void cam_update_basis(void){\n";
                                mc << "  gBE=(V3){gCamPos[0],gCamPos[1],gCamPos[2]};\n";
                                mc << "  gBF=norm3((V3){gCamForward[0],gCamForward[1],gCamForward[2]});\n";
                                mc << "  gBU=norm3((V3){gCamUp[0],gCamUp[1],gCamUp[2]});\n";
                                mc << "  gBR=norm3(cross3(gBU,gBF));\n";
                                mc << "  if(fabsf(dot3(gBR,gBR))<1e-6f) gBR=norm3((V3){gCamRight[0],gCamRight[1],gCamRight[2]});\n";
                                mc << "  if(fabsf(dot3(gBR,gBR))<1e-6f) gBR=norm3(cross3((V3){0,1,0},gBF));\n";
                                mc << "  gBU=norm3(cross3(gBF,gBR));\n";
                                mc << "}\n";
                                mc << "static V3 w2c(V3 w){ V3 d=sub3(w,gBE); return (V3){dot3(d,gBR),dot3(d,gBU),dot3(d,gBF)}; }\n";
                                mc << "static const float kClipNear = 0.05f;\n";
                                mc << "/* Frustum side-plane slopes (with 10% margin to avoid edge popping) */\n";
                                mc << "static const float kFrustSlopeX = (kProjViewW * 0.5f * 1.1f) / kProjFocalX;\n";
                                mc << "static const float kFrustSlopeY = (kProjViewH * 0.5f * 1.1f) / kProjFocalY;\n";
                                mc << "static void proj_sv(float cx, float cy, float cz, float u, float v, SV *out){\n";
                                mc << "  out->x=(kProjViewW*0.5f)+(cx/cz)*kProjFocalX;\n";
                                mc << "  out->y=(kProjViewH*0.5f)-(cy/cz)*kProjFocalY;\n";
                                mc << "  out->z=1.0f/cz; out->u=u; out->v=v;\n";
                                mc << "}\n";
                                mc << "static int proj_cs(V3 cp, SV *out){\n";
                                mc << "  if(cp.z<kClipNear) return 0;\n";
                                mc << "  out->x=(kProjViewW*0.5f)+(cp.x/cp.z)*kProjFocalX;\n";
                                mc << "  out->y=(kProjViewH*0.5f)-(cp.y/cp.z)*kProjFocalY;\n";
                                mc << "  out->z=1.0f/cp.z;\n";
                                mc << "  return 1;\n";
                                mc << "}\n";
                                mc << "static int project_point(V3 wp, SV *out){ return proj_cs(w2c(wp),out); }\n";
                                mc << "/* Sutherland-Hodgman clip: clip polygon against plane nx*x+ny*y+nz*z+nd>=0 */\n";
                                mc << "typedef struct { float x,y,z,u,v; } CV;\n";
                                mc << "static int clip_poly(CV* in, int n, CV* out, float nx, float ny, float nz, float nd){\n";
                                mc << "  if(n<3) return 0;\n";
                                mc << "  int m=0;\n";
                                mc << "  for(int i=0;i<n;i++){\n";
                                mc << "    CV *a=&in[i], *b=&in[(i+1)%n];\n";
                                mc << "    float da=a->x*nx+a->y*ny+a->z*nz+nd;\n";
                                mc << "    float db=b->x*nx+b->y*ny+b->z*nz+nd;\n";
                                mc << "    if(da>=0) out[m++]=*a;\n";
                                mc << "    if((da>=0)!=(db>=0)){\n";
                                mc << "      float t=da/(da-db);\n";
                                mc << "      out[m].x=a->x+t*(b->x-a->x); out[m].y=a->y+t*(b->y-a->y); out[m].z=a->z+t*(b->z-a->z);\n";
                                mc << "      out[m].u=a->u+t*(b->u-a->u); out[m].v=a->v+t*(b->v-a->v); m++;\n";
                                mc << "    }\n";
                                mc << "  }\n";
                                mc << "  return m;\n";
                                mc << "}\n";
                                mc << "\n";
                                mc << "static void draw_tri(pvr_poly_hdr_t *hdr, SV a, SV b, SV c, uint32 argb) {\n";
                                mc << "  pvr_vertex_t v;\n";
                                mc << "  pvr_prim(hdr, sizeof(*hdr));\n";
                                mc << "  v.flags = PVR_CMD_VERTEX; v.x=a.x; v.y=a.y; v.z=a.z; v.u=a.u; v.v=a.v; v.argb=argb; v.oargb=0; pvr_prim(&v,sizeof(v));\n";
                                mc << "  v.flags = PVR_CMD_VERTEX; v.x=b.x; v.y=b.y; v.z=b.z; v.u=b.u; v.v=b.v; v.argb=argb; v.oargb=0; pvr_prim(&v,sizeof(v));\n";
                                mc << "  v.flags = PVR_CMD_VERTEX_EOL; v.x=c.x; v.y=c.y; v.z=c.z; v.u=c.u; v.v=c.v; v.argb=argb; v.oargb=0; pvr_prim(&v,sizeof(v));\n";
                                mc << "}\n";
                                mc << "\n";
                                mc << "int main(int argc, char **argv) {\n";
                                mc << "  (void)argc; (void)argv;\n";
                                mc << "  pvr_init_defaults();\n";
                                mc << "  dbgio_dev_select(\"fb\");\n";
                                mc << "  dbgio_printf(\"Nebula Dreamcast Runtime (Scene Export v2)\\n\");\n";
                                mc << "  dbgio_printf(\"[NEBULA][DC] Loading scene: %s (sceneCount=%d)\\n\", kDefaultSceneFile, gSceneCount);\n";
                                mc << "  if (!NB_LoadScene(kDefaultSceneFile)) {\n";
                                mc << "    if (!NB_LoadSceneIndex(0)) {\n";
                                mc << "      dbgio_printf(\"[NEBULA][DC] Scene load failed: %s\\n\", kDefaultSceneFile);\n";
                                mc << "      return 1;\n";
                                mc << "    }\n";
                                mc << "  }\n";
                                mc << "  dbgio_printf(\"[NEBULA][DC] Scene loaded: meshCount=%d sceneName=%s\\n\", gSceneMeshCount, gSceneName);\n";
                                mc << "  for(int mi=0; mi<gSceneMeshCount && mi<MAX_MESHES; ++mi) { dbgio_printf(\"[NEBULA][DC]   mesh[%d] disk=%s pos=(%.2f,%.2f,%.2f)\\n\", mi, gSceneMeshes[mi].meshDisk, gSceneMeshes[mi].pos[0], gSceneMeshes[mi].pos[1], gSceneMeshes[mi].pos[2]); }\n";
                                mc << "  gPlayerMeshIdx = -1;\n";
                                mc << "  for(int mi=0; mi<gSceneMeshCount && mi<MAX_MESHES; ++mi) { if(strcmp(gSceneMeshes[mi].meshDisk, kPlayerMeshDisk)==0){ gPlayerMeshIdx=mi; break; } }\n";
                                mc << "  dbgio_printf(\"[NEBULA][DC] Player mesh idx=%d disk=%s\\n\", gPlayerMeshIdx, kPlayerMeshDisk);\n";
                                mc << "  { V3 f=norm3((V3){gCamForward[0],gCamForward[1],gCamForward[2]}); V3 u=norm3((V3){gCamUp[0],gCamUp[1],gCamUp[2]}); V3 r=norm3(cross3(u,f)); if (fabsf(dot3(r,r)) < 1e-6f) r=norm3((V3){gCamRight[0],gCamRight[1],gCamRight[2]}); if (fabsf(dot3(r,r)) < 1e-6f) r=norm3(cross3((V3){0,1,0},f)); u=norm3(cross3(f,r)); dbgio_printf(\"[CameraParity][Runtime] eye=(%.3f,%.3f,%.3f) f=(%.3f,%.3f,%.3f) r=(%.3f,%.3f,%.3f) u=(%.3f,%.3f,%.3f)\\n\", gCamPos[0],gCamPos[1],gCamPos[2],f.x,f.y,f.z,r.x,r.y,r.z,u.x,u.y,u.z); }\n";
                                mc << "\n";
                                mc << "\n";
                                mc << "  static const int kVertCountEmbedded = " << runtimeVerts.size() << ";\n";
                                mc << "  static const V3 baseEmbedded[] = {\n";
                                for (size_t vi = 0; vi < runtimeVerts.size(); ++vi)
                                {
                                    const Vec3& v = runtimeVerts[vi];
                                    mc << "    {" << fstr(v.x) << "," << fstr(v.y) << "," << fstr(v.z) << "}";
                                    if (vi + 1 < runtimeVerts.size()) mc << ",";
                                    mc << "\n";
                                }
                                mc << "  };\n";
                                mc << "  static const int kTriCountEmbedded = " << (runtimeIndices.size() / 3) << ";\n";
                                mc << "  static const V3 triUvEmbedded[] = {\n";
                                for (size_t ui = 0; ui < runtimeTriUvs.size(); ++ui)
                                {
                                    const Vec3& uv = runtimeTriUvs[ui];
                                    mc << "    {" << fstr(uv.x) << "," << fstr(uv.y) << ",0.0f}";
                                    if (ui + 1 < runtimeTriUvs.size()) mc << ",";
                                    mc << "\n";
                                }
                                mc << "  };\n";
                                mc << "  static const uint16_t triMatEmbedded[] = {";
                                for (size_t ti = 0; ti < runtimeTriMat.size(); ++ti)
                                {
                                    mc << runtimeTriMat[ti];
                                    if (ti + 1 < runtimeTriMat.size()) mc << ",";
                                }
                                mc << "};\n";
                                mc << "  static const uint16_t trisEmbedded[] = {";
                                for (size_t ii = 0; ii < runtimeIndices.size(); ++ii)
                                {
                                    mc << runtimeIndices[ii];
                                    if (ii + 1 < runtimeIndices.size()) mc << ",";
                                }
                                mc << "};\n";
                                mc << "  typedef struct { RuntimeMesh diskMesh; RuntimeMesh activeMesh; int kVertCount; int kTriCount; V3* base; uint16_t* trisNormal; V3* triUvNormal; uint16_t* triMatNormal; uint16_t* trisFlipped; V3* triUvFlipped; pvr_poly_hdr_t hdrSlot[MAX_SLOT]; pvr_ptr_t slotTx[MAX_SLOT]; RuntimeTex diskTex[MAX_SLOT]; uint16_t slotW[MAX_SLOT]; uint16_t slotH[MAX_SLOT]; float slotUS[MAX_SLOT]; float slotVS[MAX_SLOT]; float slotHalfU[MAX_SLOT]; float slotHalfV[MAX_SLOT]; uint8_t slotFilter[MAX_SLOT]; uint8_t slotReady[MAX_SLOT]; int slotCount; int loaded; } RuntimeSceneMesh;\n";
                                mc << "  static RuntimeSceneMesh meshRt[MAX_MESHES];\n";
                                mc << "  memset(meshRt, 0, sizeof(meshRt));\n";
                                mc << "  static const uint16_t kFallbackWhite2x2[4] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };\n";
                                mc << "  for(int mi=0; mi<gSceneMeshCount && mi<MAX_MESHES; ++mi){ RuntimeSceneMesh* rm=&meshRt[mi]; SceneMeshMeta* sm=&gSceneMeshes[mi]; if(!sm->meshDisk[0]) continue; char mp[256]; snprintf(mp,sizeof(mp),\"/cd/data/meshes/%s\",sm->meshDisk); if(!dc_try_load_nebmesh(mp,&rm->diskMesh)) continue; rm->activeMesh=rm->diskMesh; rm->kVertCount=rm->activeMesh.vert_count; rm->kTriCount=rm->activeMesh.tri_count; rm->base=rm->activeMesh.pos; rm->trisNormal=rm->activeMesh.indices; rm->triUvNormal=rm->activeMesh.tri_uv; rm->triMatNormal=rm->activeMesh.tri_mat; rm->trisFlipped=(uint16_t*)malloc((size_t)rm->kTriCount*3u*sizeof(uint16_t)); rm->triUvFlipped=(V3*)malloc((size_t)rm->kTriCount*3u*sizeof(V3)); if(rm->trisFlipped&&rm->triUvFlipped){ for(int t=0;t<rm->kTriCount;++t){ rm->trisFlipped[t*3+0]=rm->trisNormal[t*3+0]; rm->trisFlipped[t*3+1]=rm->trisNormal[t*3+2]; rm->trisFlipped[t*3+2]=rm->trisNormal[t*3+1]; rm->triUvFlipped[t*3+0]=rm->triUvNormal[t*3+0]; rm->triUvFlipped[t*3+1]=rm->triUvNormal[t*3+2]; rm->triUvFlipped[t*3+2]=rm->triUvNormal[t*3+1]; } } else { free(rm->trisFlipped); free(rm->triUvFlipped); rm->trisFlipped=0; rm->triUvFlipped=0; } rm->slotCount=MAX_SLOT; for(int s=0;s<MAX_SLOT;++s){ const uint16_t* buf=kFallbackWhite2x2; int tw=2,th=2; float us=1.0f,vs=1.0f,hu=0.25f,hv=0.25f; int filt=0; if(sm->texDisk[s][0]){ char tp[256]; snprintf(tp,sizeof(tp),\"/cd/data/textures/%s\",sm->texDisk[s]); if(dc_try_load_nebtex(tp,&rm->diskTex[s])){ buf=rm->diskTex[s].pixels; tw=rm->diskTex[s].w; th=rm->diskTex[s].h; us=rm->diskTex[s].us; vs=rm->diskTex[s].vs; hu=0.5f/(float)(tw>0?tw:1); hv=0.5f/(float)(th>0?th:1); filt=1; } } rm->slotW[s]=(uint16_t)tw; rm->slotH[s]=(uint16_t)th; rm->slotUS[s]=us; rm->slotVS[s]=vs; rm->slotHalfU[s]=hu; rm->slotHalfV[s]=hv; rm->slotFilter[s]=(uint8_t)filt; rm->slotTx[s]=pvr_mem_malloc(tw*th*2); if(!rm->slotTx[s]) continue; pvr_txr_load_ex((void*)buf,rm->slotTx[s],tw,th,PVR_TXRLOAD_16BPP); pvr_poly_cxt_t cxt; int isPow2W=(tw>0)&&((tw&(tw-1))==0); int isPow2H=(th>0)&&((th&(th-1))==0); uint32 layoutFmt=(isPow2W&&isPow2H)?PVR_TXRFMT_TWIDDLED:PVR_TXRFMT_NONTWIDDLED; uint32 strideFmt=(layoutFmt==PVR_TXRFMT_TWIDDLED)?PVR_TXRFMT_POW2_STRIDE:PVR_TXRFMT_X32_STRIDE; uint32 fmt=PVR_TXRFMT_RGB565|PVR_TXRFMT_VQ_DISABLE|strideFmt|layoutFmt; pvr_filter_mode_t f=rm->slotFilter[s]?PVR_FILTER_BILINEAR:PVR_FILTER_NONE; pvr_poly_cxt_txr(&cxt,PVR_LIST_OP_POLY,fmt,tw,th,rm->slotTx[s],f); cxt.gen.culling=PVR_CULLING_NONE; cxt.depth.comparison=PVR_DEPTHCMP_GREATER; cxt.depth.write=PVR_DEPTHWRITE_ENABLE; pvr_poly_compile(&rm->hdrSlot[s],&cxt); rm->slotReady[s]=1; } rm->loaded=(rm->kVertCount>0&&rm->kTriCount>0&&rm->base&&rm->trisNormal&&rm->triUvNormal&&rm->triMatNormal)?1:0; }\n";
                                mc << "  int maxVertCount=0; for(int mi=0;mi<gSceneMeshCount&&mi<MAX_MESHES;++mi){ if(meshRt[mi].loaded&&meshRt[mi].kVertCount>maxVertCount) maxVertCount=meshRt[mi].kVertCount; }\n";
                                mc << "  SV* gSv=NULL; uint8_t* gOk=NULL; V3* gCs=NULL;\n";
                                mc << "  if(maxVertCount>0){ gSv=(SV*)malloc((size_t)maxVertCount*sizeof(SV)); gOk=(uint8_t*)malloc((size_t)maxVertCount); gCs=(V3*)malloc((size_t)maxVertCount*sizeof(V3)); if(!gSv||!gOk||!gCs){ free(gSv); free(gOk); free(gCs); gSv=NULL; gOk=NULL; gCs=NULL; maxVertCount=0; } }\n";
                                mc << "  NB_KOS_InitInput();\n";
                                mc << "  NB_TryLoadVmuBootImage();\n";
                                mc << "  NB_SetMirrorFromIndex(gMirrorLrIndex);\n";
                                mc << "  int sceneReady = 1;\n";
                                mc << "  int sceneSwitchReq = 0;\n";
                                mc << "  /* legacy orbit priming removed; unified control owns orbit state. */\n";
                                mc << "  NB_Game_OnStart();\n";
                                mc << "  /* Compute pivot offset: world camera position WITHOUT orbit minus mesh position. */\n";
                                mc << "  /* kCamPosInit already includes orbit; subtract orbit to get the target/pivot world pos. */\n";
                                mc << "  gPivotOffset[0] = (kCamPosInit[0] - kCamOrbitInit[0]) - gMeshPos[0];\n";
                                mc << "  gPivotOffset[1] = (kCamPosInit[1] - kCamOrbitInit[1]) - gMeshPos[1];\n";
                                mc << "  gPivotOffset[2] = (kCamPosInit[2] - kCamOrbitInit[2]) - gMeshPos[2];\n";
                                mc << "  {\n";
                                mc << "    gFollowAlign = 0;\n";
                                mc << "    gRtOrbit[0] = kCamOrbitInit[0]; gRtOrbit[1] = kCamOrbitInit[1]; gRtOrbit[2] = kCamOrbitInit[2];\n";
                                mc << "    gOrbitInited = 1;\n";
                                mc << "    if (kDcDebug) dbgio_printf(\"[NEBULA][DC] cameraInit followAlign=%d orbit=(%.3f,%.3f,%.3f)\\n\", gFollowAlign, gRtOrbit[0], gRtOrbit[1], gRtOrbit[2]);\n";
                                mc << "  }\n";
                                mc << "\n";
                                mc << "  for (;;) {\n";
                                mc << "    const float dt = 0.016f;\n";
                                mc << "    NB_KOS_PollInput();\n";
                                mc << "    NB_TryLoadVmuBootImage();\n";
                                mc << "    NB_UpdateVmuAnim(dt);\n";
                                mc << "\n";
                                mc << "    float inX = 0.0f, inY = 0.0f;\n";
                                mc << "    float lookYaw = 0.0f, lookPitch = 0.0f;\n";
                                mc << "    float stickX = 0.0f, stickY = 0.0f;\n";
                                mc << "    float ltr = 0.0f, rtr = 0.0f;\n";
                                mc << "    if (NB_KOS_HasController()) {\n";
                                mc << "      stickX = NB_KOS_GetStickX();\n";
                                mc << "      stickY = NB_KOS_GetStickY();\n";
                                mc << "      {\n";
                                mc << "        const float stickDead = 0.25f;\n";
                                mc << "        if (fabsf(stickX) > stickDead || fabsf(stickY) > stickDead) {\n";
                                mc << "          inX = stickX;\n";
                                mc << "          inY = -stickY;\n";
                                mc << "        } else {\n";
                                mc << "          if (NB_KOS_ButtonDown(NB_BTN_DPAD_LEFT)) inX -= 1.0f;\n";
                                mc << "          if (NB_KOS_ButtonDown(NB_BTN_DPAD_RIGHT)) inX += 1.0f;\n";
                                mc << "          if (NB_KOS_ButtonDown(NB_BTN_DPAD_UP)) inY += 1.0f;\n";
                                mc << "          if (NB_KOS_ButtonDown(NB_BTN_DPAD_DOWN)) inY -= 1.0f;\n";
                                mc << "        }\n";
                                mc << "      }\n";
                                mc << "      ltr = NB_KOS_GetLTrigger();\n";
                                mc << "      rtr = NB_KOS_GetRTrigger();\n";
                                mc << "      if (ltr > 0.15f) lookYaw += ltr;\n";
                                mc << "      if (rtr > 0.15f) lookYaw -= rtr;\n";
                                mc << "      if (NB_KOS_ButtonDown(NB_BTN_Y)) lookPitch += 1.0f;\n";
                                mc << "      if (NB_KOS_ButtonDown(NB_BTN_A)) lookPitch -= 1.0f;\n";
                                mc << "    }\n";
                                mc << "\n";
                                mc << "    float inLen = sqrtf(inX * inX + inY * inY);\n";
                                mc << "    if (inLen > 1.0f) { inX /= inLen; inY /= inLen; inLen = 1.0f; }\n";
                                mc << "\n";
                                mc << "    const float moveStep = 5.0f * dt;\n";
                                mc << "    const float lookYawStep = 120.0f * dt;\n";
                                mc << "    const float lookPitchStep = 90.0f * dt;\n";
                                mc << "    const float turnSpeed = 360.0f * dt;\n";
                                mc << "\n";
                                mc << "    V3 pivot = { gMeshPos[0] + gPivotOffset[0], gMeshPos[1] + gPivotOffset[1], gMeshPos[2] + gPivotOffset[2] };\n";
                                mc << "    if (!gOrbitInited) { gRtOrbit[0] = kCamOrbitInit[0]; gRtOrbit[1] = kCamOrbitInit[1]; gRtOrbit[2] = kCamOrbitInit[2]; gOrbitInited = 1; }\n";
                                mc << "\n";
                                mc << "    {\n";
                                mc << "      float camToNodeX = pivot.x - gCamPos[0];\n";
                                mc << "      float camToNodeZ = pivot.z - gCamPos[2];\n";
                                mc << "      float fLen = sqrtf(camToNodeX * camToNodeX + camToNodeZ * camToNodeZ);\n";
                                mc << "      if (fLen < 0.0001f) {\n";
                                mc << "        float yawRad = gMeshRot[1] * 0.0174532925f;\n";
                                mc << "        camToNodeX = sinf(yawRad);\n";
                                mc << "        camToNodeZ = cosf(yawRad);\n";
                                mc << "        fLen = 1.0f;\n";
                                mc << "      }\n";
                                mc << "      camToNodeX /= fLen;\n";
                                mc << "      camToNodeZ /= fLen;\n";
                                mc << "      float camRightX = camToNodeZ;\n";
                                mc << "      float camRightZ = -camToNodeX;\n";
                                mc << "      float moveX = camRightX * inX + camToNodeX * inY;\n";
                                mc << "      float moveZ = camRightZ * inX + camToNodeZ * inY;\n";
                                mc << "      float moveLen = sqrtf(moveX * moveX + moveZ * moveZ);\n";
                                mc << "      const float deadzone = 0.05f;\n";
                                mc << "      if (moveLen > deadzone) {\n";
                                mc << "        moveX /= moveLen;\n";
                                mc << "        moveZ /= moveLen;\n";
                                mc << "        gMeshPos[0] += moveX * moveStep;\n";
                                mc << "        gMeshPos[2] += moveZ * moveStep;\n";
                                mc << "        float targetYaw = atan2f(moveX, moveZ) * 57.2957795f;\n";
                                mc << "        float dy = targetYaw - gMeshRot[1];\n";
                                mc << "        while (dy > 180.0f) dy -= 360.0f;\n";
                                mc << "        while (dy < -180.0f) dy += 360.0f;\n";
                                mc << "        if (dy > turnSpeed) dy = turnSpeed;\n";
                                mc << "        if (dy < -turnSpeed) dy = -turnSpeed;\n";
                                mc << "        gMeshRot[1] += dy;\n";
                                mc << "      }\n";
                                mc << "    }\n";
                                mc << "\n";
                                mc << "    pivot = (V3){ gMeshPos[0] + gPivotOffset[0], gMeshPos[1] + gPivotOffset[1], gMeshPos[2] + gPivotOffset[2] };\n";
                                mc << "\n";
                                mc << "    if (fabsf(lookYaw) > 0.0001f || fabsf(lookPitch) > 0.0001f) {\n";
                                mc << "      if (fabsf(lookYaw) > 0.0001f) {\n";
                                mc << "        float yawRad = (-lookYaw * lookYawStep) * 0.0174532925f;\n";
                                mc << "        float sn = sinf(yawRad), cs = cosf(yawRad);\n";
                                mc << "        float ox = gRtOrbit[0], oz = gRtOrbit[2];\n";
                                mc << "        gRtOrbit[0] = ox * cs - oz * sn;\n";
                                mc << "        gRtOrbit[2] = ox * sn + oz * cs;\n";
                                mc << "      }\n";
                                mc << "      if (fabsf(lookPitch) > 0.0001f) {\n";
                                mc << "        float ox = gRtOrbit[0], oy = gRtOrbit[1], oz = gRtOrbit[2];\n";
                                mc << "        float horiz = sqrtf(ox * ox + oz * oz);\n";
                                mc << "        float radius = sqrtf(horiz * horiz + oy * oy);\n";
                                mc << "        if (radius > 0.0001f) {\n";
                                mc << "          float pitch = atan2f(oy, (horiz > 0.0001f ? horiz : 0.0001f));\n";
                                mc << "          pitch += (lookPitch * lookPitchStep) * 0.0174532925f;\n";
                                mc << "          const float lim = 1.39626f;\n";
                                mc << "          if (pitch > lim) pitch = lim;\n";
                                mc << "          if (pitch < -lim) pitch = -lim;\n";
                                mc << "          float newHoriz = cosf(pitch) * radius;\n";
                                mc << "          gRtOrbit[1] = sinf(pitch) * radius;\n";
                                mc << "          if (horiz > 0.0001f) {\n";
                                mc << "            float s = newHoriz / horiz;\n";
                                mc << "            gRtOrbit[0] = ox * s;\n";
                                mc << "            gRtOrbit[2] = oz * s;\n";
                                mc << "          } else {\n";
                                mc << "            gRtOrbit[0] = 0.0f;\n";
                                mc << "            gRtOrbit[2] = -newHoriz;\n";
                                mc << "          }\n";
                                mc << "        }\n";
                                mc << "      }\n";
                                mc << "    }\n";
                                mc << "\n";
                                mc << "    gCamPos[0] = pivot.x + gRtOrbit[0];\n";
                                mc << "    gCamPos[1] = pivot.y + gRtOrbit[1];\n";
                                mc << "    gCamPos[2] = pivot.z + gRtOrbit[2];\n";
                                mc << "\n";
                                mc << "    {\n";
                                mc << "      V3 nf = norm3((V3){pivot.x - gCamPos[0], pivot.y - gCamPos[1], pivot.z - gCamPos[2]});\n";
                                mc << "      V3 storedUp = {0,1,0};\n";
                                mc << "      V3 nr = norm3(cross3(storedUp, nf));\n";
                                mc << "      if (fabsf(dot3(nr,nr)) < 1e-6f) nr = norm3((V3){gCamRight[0], gCamRight[1], gCamRight[2]});\n";
                                mc << "      if (fabsf(dot3(nr,nr)) < 1e-6f) nr = (V3){1,0,0};\n";
                                mc << "      gCamForward[0]=nf.x; gCamForward[1]=nf.y; gCamForward[2]=nf.z;\n";
                                mc << "      gCamRight[0]=nr.x; gCamRight[1]=nr.y; gCamRight[2]=nr.z;\n";
                                mc << "      gCamUp[0]=storedUp.x; gCamUp[1]=storedUp.y; gCamUp[2]=storedUp.z;\n";
                                mc << "    }\n";
                                mc << "\n";
                                mc << "    if (kDcDebug) {\n";
                                mc << "      static int sDbg = 0;\n";
                                mc << "      if ((sDbg++ % 60) == 0) {\n";
                                mc << "        dbgio_printf(\"[NEBULA][DC] input move=(%.2f,%.2f) look=(%.2f,%.2f)\\n\", inX, inY, lookYaw, lookPitch);\n";
                                mc << "        dbgio_printf(\"[NEBULA][DC] player pos=(%.3f,%.3f,%.3f) rot=(%.3f,%.3f,%.3f)\\n\", gMeshPos[0], gMeshPos[1], gMeshPos[2], gMeshRot[0], gMeshRot[1], gMeshRot[2]);\n";
                                mc << "        dbgio_printf(\"[NEBULA][DC] cam pos=(%.3f,%.3f,%.3f) f=(%.3f,%.3f,%.3f) u=(%.3f,%.3f,%.3f) orbit=(%.3f,%.3f,%.3f)\\n\", gCamPos[0], gCamPos[1], gCamPos[2], gCamForward[0], gCamForward[1], gCamForward[2], gCamUp[0], gCamUp[1], gCamUp[2], gRtOrbit[0], gRtOrbit[1], gRtOrbit[2]);\n";
                                mc << "      }\n";
                                mc << "    }\n";
                                mc << "\n";
                                mc << "    NB_Game_OnUpdate(dt);\n";
                                mc << "    if (sceneSwitchReq != 0) {\n";
                                mc << "      int metaOk = (sceneSwitchReq > 0) ? NB_NextScene() : NB_PrevScene();\n";
                                mc << "      sceneSwitchReq = 0;\n";
                                mc << "      sceneReady = 0;\n";
                                mc << "      for(int mi=0; mi<MAX_MESHES; ++mi){ RuntimeSceneMesh* rm=&meshRt[mi]; for(int s=0;s<MAX_SLOT;++s){ if(rm->slotTx[s]){ pvr_mem_free(rm->slotTx[s]); rm->slotTx[s]=0; } dc_free_tex(&rm->diskTex[s]); } if(rm->trisFlipped){ free(rm->trisFlipped); rm->trisFlipped=0; } if(rm->triUvFlipped){ free(rm->triUvFlipped); rm->triUvFlipped=0; } dc_free_mesh(&rm->diskMesh); memset(rm,0,sizeof(*rm)); }\n";
                                mc << "      if (!metaOk) { dbgio_printf(\"[NEBULA][DC] Scene metadata switch failed\\n\"); }\n";
                                mc << "      else { for(int mi=0; mi<gSceneMeshCount && mi<MAX_MESHES; ++mi){ RuntimeSceneMesh* rm=&meshRt[mi]; SceneMeshMeta* sm=&gSceneMeshes[mi]; if(!sm->meshDisk[0]) continue; char mp[256]; snprintf(mp,sizeof(mp),\"/cd/data/meshes/%s\",sm->meshDisk); if(!dc_try_load_nebmesh(mp,&rm->diskMesh)) continue; rm->activeMesh=rm->diskMesh; rm->kVertCount=rm->activeMesh.vert_count; rm->kTriCount=rm->activeMesh.tri_count; rm->base=rm->activeMesh.pos; rm->trisNormal=rm->activeMesh.indices; rm->triUvNormal=rm->activeMesh.tri_uv; rm->triMatNormal=rm->activeMesh.tri_mat; rm->trisFlipped=(uint16_t*)malloc((size_t)rm->kTriCount*3u*sizeof(uint16_t)); rm->triUvFlipped=(V3*)malloc((size_t)rm->kTriCount*3u*sizeof(V3)); if(rm->trisFlipped&&rm->triUvFlipped){ for(int t=0;t<rm->kTriCount;++t){ rm->trisFlipped[t*3+0]=rm->trisNormal[t*3+0]; rm->trisFlipped[t*3+1]=rm->trisNormal[t*3+2]; rm->trisFlipped[t*3+2]=rm->trisNormal[t*3+1]; rm->triUvFlipped[t*3+0]=rm->triUvNormal[t*3+0]; rm->triUvFlipped[t*3+1]=rm->triUvNormal[t*3+2]; rm->triUvFlipped[t*3+2]=rm->triUvNormal[t*3+1]; } } else { free(rm->trisFlipped); free(rm->triUvFlipped); rm->trisFlipped=0; rm->triUvFlipped=0; } rm->slotCount=MAX_SLOT; for(int s=0;s<MAX_SLOT;++s){ const uint16_t* buf=kFallbackWhite2x2; int tw=2,th=2; float us=1.0f,vs=1.0f,hu=0.25f,hv=0.25f; int filt=0; if(sm->texDisk[s][0]){ char tp[256]; snprintf(tp,sizeof(tp),\"/cd/data/textures/%s\",sm->texDisk[s]); if(dc_try_load_nebtex(tp,&rm->diskTex[s])){ buf=rm->diskTex[s].pixels; tw=rm->diskTex[s].w; th=rm->diskTex[s].h; us=rm->diskTex[s].us; vs=rm->diskTex[s].vs; hu=0.5f/(float)(tw>0?tw:1); hv=0.5f/(float)(th>0?th:1); filt=1; } } rm->slotW[s]=(uint16_t)tw; rm->slotH[s]=(uint16_t)th; rm->slotUS[s]=us; rm->slotVS[s]=vs; rm->slotHalfU[s]=hu; rm->slotHalfV[s]=hv; rm->slotFilter[s]=(uint8_t)filt; rm->slotTx[s]=pvr_mem_malloc(tw*th*2); if(!rm->slotTx[s]) continue; pvr_txr_load_ex((void*)buf,rm->slotTx[s],tw,th,PVR_TXRLOAD_16BPP); pvr_poly_cxt_t cxt; int isPow2W=(tw>0)&&((tw&(tw-1))==0); int isPow2H=(th>0)&&((th&(th-1))==0); uint32 layoutFmt=(isPow2W&&isPow2H)?PVR_TXRFMT_TWIDDLED:PVR_TXRFMT_NONTWIDDLED; uint32 strideFmt=(layoutFmt==PVR_TXRFMT_TWIDDLED)?PVR_TXRFMT_POW2_STRIDE:PVR_TXRFMT_X32_STRIDE; uint32 fmt=PVR_TXRFMT_RGB565|PVR_TXRFMT_VQ_DISABLE|strideFmt|layoutFmt; pvr_filter_mode_t f=rm->slotFilter[s]?PVR_FILTER_BILINEAR:PVR_FILTER_NONE; pvr_poly_cxt_txr(&cxt,PVR_LIST_OP_POLY,fmt,tw,th,rm->slotTx[s],f); cxt.gen.culling=PVR_CULLING_NONE; cxt.depth.comparison=PVR_DEPTHCMP_GREATER; cxt.depth.write=PVR_DEPTHWRITE_ENABLE; pvr_poly_compile(&rm->hdrSlot[s],&cxt); rm->slotReady[s]=1; } rm->loaded=(rm->kVertCount>0&&rm->kTriCount>0&&rm->base&&rm->trisNormal&&rm->triUvNormal&&rm->triMatNormal)?1:0; if(rm->loaded) sceneReady=1; } { int newMax=0; for(int mi=0;mi<gSceneMeshCount&&mi<MAX_MESHES;++mi){ if(meshRt[mi].loaded&&meshRt[mi].kVertCount>newMax) newMax=meshRt[mi].kVertCount; } if(newMax>maxVertCount){ free(gSv); free(gOk); free(gCs); maxVertCount=newMax; gSv=(SV*)malloc((size_t)maxVertCount*sizeof(SV)); gOk=(uint8_t*)malloc((size_t)maxVertCount); gCs=(V3*)malloc((size_t)maxVertCount*sizeof(V3)); if(!gSv||!gOk||!gCs){ free(gSv); free(gOk); free(gCs); gSv=NULL; gOk=NULL; gCs=NULL; maxVertCount=0; } } } if(sceneReady){ gPlayerMeshIdx=-1; for(int mi=0;mi<gSceneMeshCount&&mi<MAX_MESHES;++mi){ if(strcmp(gSceneMeshes[mi].meshDisk,kPlayerMeshDisk)==0){ gPlayerMeshIdx=mi; break; } } NB_Game_OnSceneSwitch(gSceneName); } }\n";
                                mc << "    }\n";
                                mc << "    if (!sceneReady) {\n";
                                mc << "      pvr_wait_ready(); pvr_scene_begin(); pvr_list_begin(PVR_LIST_OP_POLY); pvr_list_finish(); pvr_scene_finish(); thd_sleep(16); continue;\n";
                                mc << "    }\n";
                                mc << "    cam_update_basis();\n";
                                mc << "    pvr_wait_ready();\n";
                                mc << "    pvr_scene_begin();\n";
                                mc << "    pvr_list_begin(PVR_LIST_OP_POLY);\n";
                                mc << "    for (int mi=0; mi<gSceneMeshCount && mi<MAX_MESHES; ++mi) {\n";
                                mc << "      RuntimeSceneMesh* rm = &meshRt[mi]; SceneMeshMeta* sm = &gSceneMeshes[mi];\n";
                                mc << "      if (!rm->loaded || rm->kVertCount <= 0 || rm->kTriCount <= 0 || !rm->base || !rm->trisNormal || !rm->triUvNormal || !rm->triMatNormal) continue;\n";
                                mc << "      if (!gSv || !gOk || !gCs || rm->kVertCount > maxVertCount) continue;\n";
                                mc << "      SV* sv = gSv; uint8_t* ok = gOk; V3* cs = gCs;\n";
                                mc << "      float smPos[3] = {sm->pos[0], sm->pos[1], sm->pos[2]};\n";
                                mc << "      float smRot[3] = {sm->rot[0], sm->rot[1], sm->rot[2]};\n";
                                mc << "      if (mi == gPlayerMeshIdx) { smPos[0]=gMeshPos[0]; smPos[1]=gMeshPos[1]; smPos[2]=gMeshPos[2]; smRot[0]=gMeshRot[0]; smRot[1]=gMeshRot[1]; smRot[2]=gMeshRot[2]; }\n";
                                mc << "      /* StaticMesh rotation axis remap: X<-Z, Y<-X, Z<-Y (matches editor OpenGL convention). */\n";
                                mc << "      /* Player mesh (parented under Node3D) uses identity remap. */\n";
                                mc << "      float rxr = (mi == gPlayerMeshIdx) ? deg2rad(smRot[0]) : deg2rad(smRot[2]);\n";
                                mc << "      float ryr = (mi == gPlayerMeshIdx) ? deg2rad(smRot[1]) : deg2rad(smRot[0]);\n";
                                mc << "      float rzr = (mi == gPlayerMeshIdx) ? deg2rad(smRot[2]) : deg2rad(smRot[1]);\n";
                                mc << "      for (int i=0;i<rm->kVertCount;++i){ V3 v = rm->base[i]; v.x *= sm->scale[0] * (float)gMirrorX; v.y *= sm->scale[1] * (float)gMirrorY; v.z *= sm->scale[2] * (float)gMirrorZ; v = rot_xyz(v, rxr, ryr, rzr); v.x += smPos[0]; v.y += smPos[1]; v.z += smPos[2]; cs[i] = w2c(v); ok[i] = proj_cs(cs[i], &sv[i]) ? 1 : 0; }\n";
                                mc << "      float mirrorDet = (sm->scale[0] * (float)gMirrorX) * (sm->scale[1] * (float)gMirrorY) * (sm->scale[2] * (float)gMirrorZ);\n";
                                mc << "      const int mirroredWinding = (mirrorDet < 0.0f) ? 1 : 0;\n";
                                mc << "      const uint16_t *tris = (mirroredWinding && rm->trisFlipped) ? rm->trisFlipped : rm->trisNormal;\n";
                                mc << "      const V3 *triUv = (mirroredWinding && rm->triUvFlipped) ? rm->triUvFlipped : rm->triUvNormal;\n";
                                mc << "      for (int t=0;t<rm->kTriCount;++t){\n";
                                mc << "        int ia=tris[t*3+0], ib=tris[t*3+1], ic=tris[t*3+2];\n";
                                mc << "        int sid = (int)rm->triMatNormal[t]; if (sid < 0 || sid >= rm->slotCount) sid = 0; if (!rm->slotReady[sid]) continue;\n";
                                mc << "        float us = rm->slotUS[sid], vs_ = rm->slotVS[sid]; float hu = rm->slotHalfU[sid], hv = rm->slotHalfV[sid];\n";
                                mc << "        uint8 tr = (uint8)(255 - ((sid * 17) & 31)); uint8 tg = (uint8)(255 - ((sid * 29) & 31)); uint8 tb = (uint8)(255 - ((sid * 11) & 31)); uint32 col = 0xFF000000 | (tr << 16) | (tg << 8) | tb;\n";
                                mc << "        /* Compute UV for each original vertex */\n";
                                mc << "        float uv[3][2];\n";
                                mc << "        for(int k=0;k<3;++k){ float u=triUv[t*3+k].x, v=1.0f-triUv[t*3+k].y; if(u<0.0f)u=0.0f; else if(u>1.0f)u=1.0f; if(v<0.0f)v=0.0f; else if(v>1.0f)v=1.0f; uv[k][0]=(u*(1.0f-2.0f*hu)+hu)*us; uv[k][1]=(v*(1.0f-2.0f*hv)+hv)*vs_; }\n";
                                mc << "        /* Quick accept: all 3 on-screen → fast path (no clipping) */\n";
                                mc << "        if (ok[ia] && ok[ib] && ok[ic]) {\n";
                                mc << "          SV sa=sv[ia],sb=sv[ib],sc=sv[ic];\n";
                                mc << "          float mnx=sa.x,mxx=sa.x,mny=sa.y,mxy=sa.y;\n";
                                mc << "          if(sb.x<mnx)mnx=sb.x; if(sb.x>mxx)mxx=sb.x; if(sb.y<mny)mny=sb.y; if(sb.y>mxy)mxy=sb.y;\n";
                                mc << "          if(sc.x<mnx)mnx=sc.x; if(sc.x>mxx)mxx=sc.x; if(sc.y<mny)mny=sc.y; if(sc.y>mxy)mxy=sc.y;\n";
                                mc << "          if(mnx>=-32.0f && mxx<=kProjViewW+32.0f && mny>=-32.0f && mxy<=kProjViewH+32.0f){\n";
                                mc << "            sa.u=uv[0][0]; sa.v=uv[0][1]; sb.u=uv[1][0]; sb.v=uv[1][1]; sc.u=uv[2][0]; sc.v=uv[2][1];\n";
                                mc << "            draw_tri(&rm->hdrSlot[sid],sa,sb,sc,col); continue;\n";
                                mc << "          }\n";
                                mc << "        }\n";
                                mc << "        /* Full frustum clip (Sutherland-Hodgman against 5 planes in camera space) */\n";
                                mc << "        CV polyA[12], polyB[12];\n";
                                mc << "        polyA[0]=(CV){cs[ia].x,cs[ia].y,cs[ia].z,uv[0][0],uv[0][1]};\n";
                                mc << "        polyA[1]=(CV){cs[ib].x,cs[ib].y,cs[ib].z,uv[1][0],uv[1][1]};\n";
                                mc << "        polyA[2]=(CV){cs[ic].x,cs[ic].y,cs[ic].z,uv[2][0],uv[2][1]};\n";
                                mc << "        int pn=3;\n";
                                mc << "        /* Near */\n";
                                mc << "        pn=clip_poly(polyA,pn,polyB, 0,0,1,-kClipNear); if(pn<3) continue; memcpy(polyA,polyB,pn*sizeof(CV));\n";
                                mc << "        /* Left: x + z*slopeX >= 0 */\n";
                                mc << "        pn=clip_poly(polyA,pn,polyB, 1,0,kFrustSlopeX,0); if(pn<3) continue; memcpy(polyA,polyB,pn*sizeof(CV));\n";
                                mc << "        /* Right: -x + z*slopeX >= 0 */\n";
                                mc << "        pn=clip_poly(polyA,pn,polyB, -1,0,kFrustSlopeX,0); if(pn<3) continue; memcpy(polyA,polyB,pn*sizeof(CV));\n";
                                mc << "        /* Bottom: y + z*slopeY >= 0 */\n";
                                mc << "        pn=clip_poly(polyA,pn,polyB, 0,1,kFrustSlopeY,0); if(pn<3) continue; memcpy(polyA,polyB,pn*sizeof(CV));\n";
                                mc << "        /* Top: -y + z*slopeY >= 0 */\n";
                                mc << "        pn=clip_poly(polyA,pn,polyB, 0,-1,kFrustSlopeY,0); if(pn<3) continue; memcpy(polyA,polyB,pn*sizeof(CV));\n";
                                mc << "        /* Project clipped polygon and fan-triangulate */\n";
                                mc << "        SV svp[12];\n";
                                mc << "        for(int i=0;i<pn;i++) proj_sv(polyB[i].x,polyB[i].y,polyB[i].z,polyB[i].u,polyB[i].v,&svp[i]);\n";
                                mc << "        for(int i=1;i<pn-1;i++) draw_tri(&rm->hdrSlot[sid],svp[0],svp[i],svp[i+1],col);\n";
                                mc << "      }\n";
                                mc << "    }\n";
                                mc << "\n";
                                mc << "    pvr_list_finish();\n";
                                mc << "    pvr_scene_finish();\n";
                                mc << "    thd_sleep(16);\n";
                                mc << "  }\n";
                                mc << "\n";
                                mc << "  return 0;\n";
                                mc << "}\n";
                            }

                            {
                                std::ofstream ec(entryCPath, std::ios::out | std::ios::trunc);
                                if (ec.is_open())
                                {
                                    ec << "/* compatibility file; build uses main.c directly */\n";
                                }
                            }

                            {
                                std::ofstream gs(gameStubPath, std::ios::out | std::ios::trunc);
                                if (gs.is_open())
                                {
                                    gs << "/* Auto-generated fallback gameplay hooks for Dreamcast runtime. */\n";
                                    gs << "void __attribute__((weak)) NB_Game_OnStart(void) {}\n";
                                    gs << "void __attribute__((weak)) NB_Game_OnUpdate(float dt) { (void)dt; }\n";
                                    gs << "void __attribute__((weak)) NB_Game_OnSceneSwitch(const char* sceneName) { (void)sceneName; }\n";
                                }
                            }

                            // KOS bindings are now consumed from engine source (src/platform/dreamcast)
                            // instead of generating local copies in build_dreamcast.

                            {
                                std::filesystem::path bindingsDir = std::filesystem::weakly_canonical(GetExecutableDirectory() / ".." / ".." / "src" / "platform" / "dreamcast");
                                std::string bindingsPosix = bindingsDir.string();
                                std::replace(bindingsPosix.begin(), bindingsPosix.end(), '\\', '/');
                                if (bindingsPosix.size() > 2 && std::isalpha((unsigned char)bindingsPosix[0]) && bindingsPosix[1] == ':')
                                {
                                    char drive = (char)std::tolower((unsigned char)bindingsPosix[0]);
                                    bindingsPosix = std::string("/") + drive + bindingsPosix.substr(2);
                                }

                                std::ofstream mk(makefilePath, std::ios::out | std::ios::trunc);
                                if (mk.is_open())
                                {
                                    mk << "TARGET = nebula_dreamcast.elf\n";
                                    mk << "NEBULA_DC_BINDINGS ?= " << bindingsPosix << "\n";
                                    mk << "VPATH += $(NEBULA_DC_BINDINGS)\n";
                                    mk << "SCRIPT_SOURCES = $(wildcard scripts/*.c)\n";
                                    mk << "SOURCES = main.c KosBindings.c KosInput.c $(SCRIPT_SOURCES) NebulaGameStub.c\n";
                                    mk << "OBJS = $(SOURCES:.c=.o)\n";
                                    mk << "KOS_BASE ?= /c/DreamSDK/opt/toolchains/dc/kos\n";
                                    mk << "KOS_CC_BASE ?= /c/DreamSDK/opt/toolchains/dc\n";
                                    mk << "CFLAGS += -I$(KOS_BASE)/include -I$(KOS_BASE)/kernel/arch/dreamcast/include -I$(KOS_BASE)/addons/include -I$(NEBULA_DC_BINDINGS) -I. -Iscripts\n";
                                    mk << "all: rm-elf $(TARGET)\n";
                                    mk << "include $(KOS_BASE)/Makefile.rules\n";
                                    mk << "%.o: %.c\n";
                                    mk << "\tsh-elf-gcc $(CFLAGS) -c $< -o $@\n";
                                    mk << "clean: rm-elf\n";
                                    mk << "\t-rm -f $(OBJS)\n";
                                    mk << "rm-elf:\n";
                                    mk << "\t-rm -f $(TARGET)\n";
                                    mk << "$(TARGET): $(OBJS)\n";
                                    mk << "\tkos-cc -o $(TARGET) $(OBJS)\n";
                                }
                            }

                            std::filesystem::path ipTxtPath = buildDir / "ip.txt";
                            {
                                std::ofstream ipf(ipTxtPath, std::ios::out | std::ios::trunc);
                                if (ipf.is_open())
                                {
                                    ipf << "Hardware ID   : SEGA SEGAKATANA\n";
                                    ipf << "Maker ID      : SEGA ENTERPRISES\n";
                                    ipf << "Device Info   : CD-ROM1/1\n";
                                    ipf << "Area Symbols  : JUE\n";
                                    ipf << "Peripherals   : E000F10\n";
                                    ipf << "Product No    : T-00000\n";
                                    ipf << "Version       : V1.000\n";
                                    ipf << "Release Date  : 20260218\n";
                                    ipf << "Boot Filename : 1ST_READ.BIN\n";
                                    ipf << "SW Maker Name : NEBULA\n";
                                    ipf << "Game Title    : NEBULA DREAMCAST\n";
                                }
                            }

                            {
                                std::ofstream lf(logPath, std::ios::out | std::ios::trunc);
                                if (lf.is_open())
                                {
                                    lf << "[DreamcastBuild] start\n";
                                    lf << "[DreamcastCameraConvention] RH +Y-up +Z-forward right=cross(up,forward) up=cross(forward,right)\n";
                                    lf << "[DreamcastCamera] source=" << cameraSourceScene
                                       << " srcPos=(" << camSrc.x << "," << camSrc.y << "," << camSrc.z << ")"
                                       << " srcRot=(" << camSrc.rotX << "," << camSrc.rotY << "," << camSrc.rotZ << ")"
                                       << " convPos=(" << dcView.eye.x << "," << dcView.eye.y << "," << dcView.eye.z << ")"
                                       << " convTarget=(" << dcView.target.x << "," << dcView.target.y << "," << dcView.target.z << ")"
                                       << " convForward=(" << dcView.basis.forward.x << "," << dcView.basis.forward.y << "," << dcView.basis.forward.z << ")"
                                       << " convRight=(" << dcView.basis.right.x << "," << dcView.basis.right.y << "," << dcView.basis.right.z << ")"
                                       << " convUp=(" << dcView.basis.up.x << "," << dcView.basis.up.y << "," << dcView.basis.up.z << ")"
                                       << " proj=(fov=" << dcProj.fovYDeg << ",a=" << dcProj.aspect << ",n=" << dcProj.nearZ << ",f=" << dcProj.farZ << ")\n";
                                    lf << "[DreamcastScripts] policy=.c only (recursive from <Project>/Scripts)\n";
                                    lf << "[DreamcastScripts] discovered_c=" << scriptDiscoveredC
                                       << " copied_c=" << scriptCopiedC
                                       << " ignored_cpp=" << scriptIgnoredCpp << "\n";
                                    for (const auto& s : scriptSourcesForMake)
                                        lf << "[DreamcastScripts] source=" << s << "\n";
                                    if (scriptSourcesForMake.empty())
                                        lf << "[DreamcastScripts] using generated weak stub only\n";
                                }
                            }

                            std::string buildDirCmd = buildDir.string();
                            std::replace(buildDirCmd.begin(), buildDirCmd.end(), '\\', '/');
                            std::string logPathCmd = logPath.string();
                            std::replace(logPathCmd.begin(), logPathCmd.end(), '\\', '/');

                            int rc = 1;
                            if (!stagingNameCollision)
                            {
                                std::string scriptPathCmd = buildDirCmd + "/_nebula_build_dreamcast.bat";
                                std::string batCmd = "cmd /c \"\"" + scriptPathCmd + "\" >> \"" + logPathCmd + "\" 2>&1\"";
                                rc = RunCommand(batCmd.c_str());
                            }
                            else
                            {
                                std::ofstream lf(logPath, std::ios::out | std::ios::app);
                                if (lf.is_open()) lf << stagingNameCollisionMessage << "\n";
                                printf("%s\n", stagingNameCollisionMessage.c_str());
                            }

                            std::filesystem::path elfPath = buildDir / "nebula_dreamcast.elf";
                            std::filesystem::path binPath = buildDir / "nebula_dreamcast.bin";
                            std::filesystem::path firstPath = buildDir / "1ST_READ.BIN";
                            std::filesystem::path isoPath = buildDir / "nebula_dreamcast.iso";
                            std::filesystem::path cdiPath = buildDir / "nebula_dreamcast.cdi";

                            bool haveElf = std::filesystem::exists(elfPath);
                            bool haveBin = std::filesystem::exists(binPath);
                            bool have1st = std::filesystem::exists(firstPath);
                            bool haveIso = std::filesystem::exists(isoPath);
                            bool haveCdi = std::filesystem::exists(cdiPath);
                            bool artifactsOk = haveElf && haveBin && have1st && haveIso && haveCdi;

                            if (stagingNameCollision)
                            {
                                gViewportToast = "Dreamcast build failed (staged filename collision; see package.log)";
                            }
                            else if (rc == 0 && artifactsOk)
                            {
                                gViewportToast = "Dreamcast build complete (see build_dreamcast)";
                            }
                            else
                            {
                                std::ofstream lf(logPath, std::ios::out | std::ios::app);
                                if (lf.is_open())
                                {
                                    lf << "[DreamcastBuild] buildDir=" << buildDir.string() << "\n";
                                    lf << "[DreamcastBuild] rc=" << rc << " artifactsOk=" << (artifactsOk ? 1 : 0) << "\n";
                                    lf << "[ArtifactPaths] elf=" << elfPath.string()
                                       << " | bin=" << binPath.string()
                                       << " | 1st=" << firstPath.string()
                                       << " | iso=" << isoPath.string()
                                       << " | cdi=" << cdiPath.string() << "\n";
                                    lf << "[Artifacts] elf=" << (haveElf ? 1 : 0)
                                       << " bin=" << (haveBin ? 1 : 0)
                                       << " 1st=" << (have1st ? 1 : 0)
                                       << " iso=" << (haveIso ? 1 : 0)
                                       << " cdi=" << (haveCdi ? 1 : 0) << "\n";

                                    int listed = 0;
                                    lf << "[DirList]\n";
                                    std::error_code ecList;
                                    for (const auto& de : std::filesystem::directory_iterator(buildDir, ecList))
                                    {
                                        lf << " - " << de.path().filename().string() << "\n";
                                        if (++listed >= 64) break;
                                    }
                                    if (ecList)
                                        lf << "[DirListError] " << ecList.message() << "\n";
                                    lf << "[DreamcastBuild] If script compile/link failed, check errors above and ensure gameplay hooks are valid C symbols.\n";
                                    lf << "[DreamcastBuild] Expected hooks: NB_Game_OnStart, NB_Game_OnUpdate, NB_Game_OnSceneSwitch.\n";
                                }

                                int generatedCount = (haveElf ? 1 : 0) + (haveBin ? 1 : 0) + (have1st ? 1 : 0) + (haveIso ? 1 : 0) + (haveCdi ? 1 : 0);
                                if (generatedCount > 0)
                                {
                                    gViewportToast = "Dreamcast artifacts generated (" + std::to_string(generatedCount) + "/5). Check build_dreamcast";
                                }
                                else
                                {
                                    gViewportToast = "Dreamcast build files generated. Check dreamcast_build";
                                }
                            }
                            gViewportToastUntil = glfwGetTime() + 4.0;
                        }
                    }

                    if (useLegacyDreamcastBuilder)
                    {
                    std::filesystem::path buildDir = std::filesystem::path(gProjectDir) / "build_saturn";
                    std::filesystem::path cdDir = buildDir / "cd";
                    std::filesystem::create_directories(cdDir);

                    // Export default scene (if configured), otherwise current editor scene snapshot.
                    std::vector<Audio3DNode> exportNodes = gAudio3DNodes;
                    std::vector<StaticMesh3DNode> exportStatics = gStaticMeshNodes;
                    std::vector<Camera3DNode> exportCameras = gCamera3DNodes;

                    // Bake simple parent hierarchy into world transforms for export.
                    auto findExportStaticByName = [&](const std::string& nm)->int {
                        for (int i = 0; i < (int)exportStatics.size(); ++i) if (exportStatics[i].name == nm) return i;
                        return -1;
                    };
                    for (int i = 0; i < (int)exportStatics.size(); ++i)
                    {
                        float wx = exportStatics[i].x, wy = exportStatics[i].y, wz = exportStatics[i].z;
                        float wrx = exportStatics[i].rotX, wry = exportStatics[i].rotY, wrz = exportStatics[i].rotZ;
                        float wsx = exportStatics[i].scaleX, wsy = exportStatics[i].scaleY, wsz = exportStatics[i].scaleZ;
                        std::string p = exportStatics[i].parent;
                        int guard = 0;
                        while (!p.empty() && guard++ < 256)
                        {
                            int pi = findExportStaticByName(p);
                            if (pi < 0) break;
                            const auto& pn = exportStatics[pi];
                            wx += pn.x; wy += pn.y; wz += pn.z;
                            wrx += pn.rotX; wry += pn.rotY; wrz += pn.rotZ;
                            wsx *= pn.scaleX; wsy *= pn.scaleY; wsz *= pn.scaleZ;
                            p = pn.parent;
                        }
                        exportStatics[i].x = wx; exportStatics[i].y = wy; exportStatics[i].z = wz;
                        exportStatics[i].rotX = wrx; exportStatics[i].rotY = wry; exportStatics[i].rotZ = wrz;
                        exportStatics[i].scaleX = wsx; exportStatics[i].scaleY = wsy; exportStatics[i].scaleZ = wsz;
                    }

                    if (gActiveScene >= 0 && gActiveScene < (int)gOpenScenes.size())
                    {
                        gOpenScenes[gActiveScene].nodes = gAudio3DNodes;
                        gOpenScenes[gActiveScene].staticMeshes = gStaticMeshNodes;
                        gOpenScenes[gActiveScene].cameras = gCamera3DNodes;
                    }

                    std::string defaultSceneCfg = GetProjectDefaultScene(std::filesystem::path(gProjectDir));
                    if (!defaultSceneCfg.empty())
                    {
                        std::filesystem::path defaultScenePath(defaultSceneCfg);
                        if (defaultScenePath.is_relative())
                            defaultScenePath = std::filesystem::path(gProjectDir) / defaultScenePath;

                        bool foundOpen = false;
                        for (const auto& s : gOpenScenes)
                        {
                            if (s.path == defaultScenePath)
                            {
                                exportNodes = s.nodes;
                                exportStatics = s.staticMeshes;
                                exportCameras = s.cameras;
                                foundOpen = true;
                                break;
                            }
                        }

                        if (!foundOpen)
                        {
                            SceneData loaded{};
                            if (LoadSceneFromPath(defaultScenePath, loaded))
                            {
                                exportNodes = loaded.nodes;
                                exportStatics = loaded.staticMeshes;
                                exportCameras = loaded.cameras;
                            }
                        }
                    }

                    SaveSceneToPath(buildDir / "scene_export.nebscene", exportNodes, exportStatics, exportCameras, gNode3DNodes);

                    std::vector<std::string> texWarnings;

                    // Minimal CD metadata required by JO engine mkisofs flags.
                    {
                        std::ofstream absTxt(cdDir / "ABS.TXT", std::ios::out | std::ios::trunc); if (absTxt.is_open()) absTxt << "Nebula Dreamcast Build";
                        std::ofstream cpyTxt(cdDir / "CPY.TXT", std::ios::out | std::ios::trunc); if (cpyTxt.is_open()) cpyTxt << "(C) Nebula";
                        std::ofstream bibTxt(cdDir / "BIB.TXT", std::ios::out | std::ios::trunc); if (bibTxt.is_open()) bibTxt << "Nebula Dreamcast Scene Package";
                    }

                    // Segment 2.0 baseline texture (Saturn-safe subset): reuse JO sample checker texture.
                    {
                        std::filesystem::path texSrc = std::filesystem::path("C:/Users/NoSig/Documents/SaturnDev/JO_engine/Samples/demo - 3D/cd/BOX.TGA");
                        std::filesystem::path texDstCd = cdDir / "BOX.TGA";
                        std::filesystem::path texDstRoot = buildDir / "BOX.TGA";
                        std::error_code ec;
                        if (std::filesystem::exists(texSrc))
                        {
                            std::filesystem::copy_file(texSrc, texDstCd, std::filesystem::copy_options::overwrite_existing, ec);
                            ec.clear();
                            std::filesystem::copy_file(texSrc, texDstRoot, std::filesystem::copy_options::overwrite_existing, ec);
                        }
                    }

                    // Generate Segment-1/2 Saturn runtime: camera + transformed mesh markers.
                    {
                        Camera3DNode camSrc{};
                        bool haveCam = false;
                        for (const auto& c : exportCameras)
                        {
                            if (c.main) { camSrc = c; haveCam = true; break; }
                        }
                        if (!haveCam && !exportCameras.empty()) { camSrc = exportCameras[0]; haveCam = true; }

                        if (!haveCam)
                        {
                            camSrc.x = 0.0f; camSrc.y = 0.0f; camSrc.z = -20.0f;
                            camSrc.rotX = 0.0f; camSrc.rotY = 0.0f; camSrc.rotZ = 0.0f;
                        }

                        Vec3 right{}, up{}, forward{};
                        GetLocalAxesFromEuler(camSrc.rotX, camSrc.rotY, camSrc.rotZ, right, up, forward);

                        constexpr float kSaturnScale = 8.0f;
                        int camX = (int)std::lround(camSrc.x * kSaturnScale);
                        int camY = (int)std::lround(-camSrc.y * kSaturnScale); // Saturn Y-axis correction (editor up -> Saturn up)
                        int camZ = (int)std::lround(camSrc.z * kSaturnScale);
                        int tgtX = (int)std::lround((camSrc.x + forward.x * 20.0f) * kSaturnScale);
                        int tgtY = (int)std::lround(-(camSrc.y + forward.y * 20.0f) * kSaturnScale);
                        int tgtZ = (int)std::lround((camSrc.z + forward.z * 20.0f) * kSaturnScale);

                        // Segment 2.1: per-mesh texture mapping (Saturn-safe subset).
                        // Clean stale staged TX##.TGA files so JO never sees old invalid dimensions.
                        auto clearStagedSaturnTextures = [&](const std::filesystem::path& dir)
                        {
                            std::error_code ec;
                            if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) return;
                            for (const auto& e : std::filesystem::directory_iterator(dir, ec))
                            {
                                if (ec) break;
                                if (!e.is_regular_file()) continue;
                                std::string stem = e.path().stem().string();
                                std::string ext = e.path().extension().string();
                                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                                if (ext != ".tga") continue;
                                if (stem.size() == 4 && stem[0] == 'T' && stem[1] == 'X' && std::isdigit((unsigned char)stem[2]) && std::isdigit((unsigned char)stem[3]))
                                {
                                    std::filesystem::remove(e.path(), ec);
                                    ec.clear();
                                }
                            }
                        };
                        clearStagedSaturnTextures(cdDir);
                        clearStagedSaturnTextures(buildDir);

                        constexpr int kMaxSaturnTextures = 64;
                        std::vector<int> meshTexSlot(std::max(1, (int)exportStatics.size()), -1);
                        std::vector<std::array<int, kStaticMeshMaterialSlots>> meshMatTexSlot(std::max(1, (int)exportStatics.size()));
                        std::vector<std::array<int, kStaticMeshMaterialSlots>> meshMatUvRepeatPow(std::max(1, (int)exportStatics.size()));
                        for (auto& arr : meshMatTexSlot) arr.fill(-1);
                        for (auto& arr : meshMatUvRepeatPow) arr.fill(0);
                        std::vector<std::string> slotFileName;
                        std::unordered_map<std::string, int> sourceToSlot;

                        auto toLower = [](std::string s) {
                            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                            return s;
                        };

                        auto stageTexturePath = [&](const std::filesystem::path& texPath, const std::string& meshName)->int
                        {
                            if (!std::filesystem::exists(texPath))
                            {
                                texWarnings.push_back("[Segment2.1] Missing texture: " + texPath.string() + " (mesh " + meshName + ")");
                                return -1;
                            }

                            std::string ext = toLower(texPath.extension().string());
                            if (ext != ".tga" && ext != ".nebtex")
                            {
                                texWarnings.push_back("[Segment2.1] Saturn constraint warning: unsupported texture format (only .tga/.nebtex): " + texPath.string() + " (mesh " + meshName + ")");
                                return -1;
                            }

                            std::string key = texPath.lexically_normal().string();
                            auto it = sourceToSlot.find(key);
                            if (it != sourceToSlot.end()) return it->second;

                            if ((int)slotFileName.size() >= kMaxSaturnTextures)
                            {
                                texWarnings.push_back("[Segment2.1] Texture limit exceeded (max " + std::to_string(kMaxSaturnTextures) + "), using fallback for mesh " + meshName);
                                return -1;
                            }

                            int slot = (int)slotFileName.size();
                            char nameBuf[16];
                            snprintf(nameBuf, sizeof(nameBuf), "TX%02d.TGA", slot);
                            std::string satName = nameBuf;
                            std::filesystem::path dstCd = cdDir / satName;
                            std::filesystem::path dstRoot = buildDir / satName;

                            std::error_code ec;
                            bool ok = false;
                            if (ext == ".tga")
                            {
                                std::string warn;
                                bool c1 = ConvertTgaToJoSafeTga24(texPath, dstCd, warn);
                                bool c2 = ConvertTgaToJoSafeTga24(texPath, dstRoot, warn);
                                ok = c1 && c2;
                                if (!warn.empty())
                                    texWarnings.push_back("[Segment2.1] " + warn + " Source: " + texPath.string() + " (mesh " + meshName + ")");
                            }
                            else
                            {
                                std::string warn;
                                bool c1 = ConvertNebTexToTga24(texPath, dstCd, warn);
                                bool c2 = ConvertNebTexToTga24(texPath, dstRoot, warn);
                                ok = c1 && c2;
                                if (!warn.empty())
                                    texWarnings.push_back("[Segment2.1] " + warn + " Source: " + texPath.string() + " (mesh " + meshName + ")");
                            }

                            if (!ok)
                            {
                                std::string em = ec ? ec.message() : std::string("conversion/copy failed");
                                texWarnings.push_back("[Segment2.1] Saturn constraint warning: failed to stage texture: " + texPath.string() + " (" + em + ")");
                                return -1;
                            }

                            sourceToSlot[key] = slot;
                            slotFileName.push_back(satName);
                            return slot;
                        };

                        for (size_t i = 0; i < exportStatics.size(); ++i)
                        {
                            const auto& sm = exportStatics[i];
                            if (gProjectDir.empty()) continue;

                            for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                            {
                                std::string matRef = GetStaticMeshMaterialByIndex(sm, si);
                                if (matRef.empty() && si == 0) matRef = sm.material; // legacy scene fallback
                                if (matRef.empty()) continue;
                                std::filesystem::path matPath = std::filesystem::path(gProjectDir) / matRef;

                                bool saturnUvExtended = false;
                                float matUvScale = 0.0f;
                                LoadMaterialAllowUvRepeat(matPath, saturnUvExtended);
                                LoadMaterialUvScale(matPath, matUvScale);
                                int repeatPow = 0;
                                if (saturnUvExtended)
                                {
                                    // Saturn extended-UV repeat uses negative uv_scale convention from editor:
                                    // uv_scale=-1 => 2x repeat, -2 => 4x repeat, -3 => 8x repeat.
                                    if (matUvScale < -0.001f)
                                    {
                                        repeatPow = std::max(0, (int)std::lround(-matUvScale));
                                        if (repeatPow > 3)
                                        {
                                            texWarnings.push_back("[SaturnUV] -uv_scale clamped to 3 for runtime safety: " + matRef + " (uv_scale=" + std::to_string(matUvScale) + ")");
                                            repeatPow = 3;
                                        }
                                    }
                                    else if (matUvScale > 0.001f)
                                    {
                                        texWarnings.push_back("[SaturnUV] positive uv_scale is not representable in extended repeat path; using 0: " + matRef + " (uv_scale=" + std::to_string(matUvScale) + ")");
                                    }
                                }
                                meshMatUvRepeatPow[i][si] = repeatPow;

                                std::string texPathStr;
                                if (!LoadMaterialTexture(matPath, texPathStr) || texPathStr.empty()) continue;
                                std::filesystem::path texPath = std::filesystem::path(texPathStr);
                                if (texPath.is_relative()) texPath = std::filesystem::path(gProjectDir) / texPath;
                                int staged = stageTexturePath(texPath, sm.name);
                                if (staged >= 0) meshMatTexSlot[i][si] = staged;
                            }

                            int activeSi = sm.materialSlot;
                            if (activeSi < 0 || activeSi >= kStaticMeshMaterialSlots) activeSi = 0;
                            meshTexSlot[i] = meshMatTexSlot[i][activeSi];

                            std::string dbg = "[DebugMatMap] " + sm.name + " slots:";
                            for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                            {
                                if (meshMatTexSlot[i][si] >= 0)
                                    dbg += " s" + std::to_string(si + 1) + "->TX" + std::to_string(meshMatTexSlot[i][si]);
                            }
                            texWarnings.push_back(dbg);
                        }

                        // Segment 3.0: bake real .nebmesh triangle geometry (Saturn-safe subset).
                        std::vector<int> geoHasMesh(std::max(1, (int)exportStatics.size()), 0);
                        std::vector<int> geoPolyCount(std::max(1, (int)exportStatics.size()), 0);
                        std::vector<int> geoDataOffset(std::max(1, (int)exportStatics.size()), 0);
                        std::vector<int> geoTexSlot(std::max(1, (int)exportStatics.size()), -1);
                        std::vector<int> geoPolyTexSlot; // one texture slot per generated polygon
                        std::vector<int> geoPolyUvRepeatPow; // per polygon repeat exponent transferred from material uv_scale (2^n)
                        std::vector<int> geoVertData; // 12 ints per polygon (4 vertices x xyz)
                        constexpr int kMaxSaturnTrianglesPerMesh = 65535;
                        constexpr bool kRequireCanonicalFaceRecords = false;

                        for (size_t i = 0; i < exportStatics.size(); ++i)
                        {
                            geoTexSlot[i] = meshTexSlot[i];
                            const auto& sm = exportStatics[i];
                            if (sm.mesh.empty() || gProjectDir.empty())
                                continue;

                            std::filesystem::path meshPath = std::filesystem::path(sm.mesh);
                            if (meshPath.is_relative())
                                meshPath = std::filesystem::path(gProjectDir) / meshPath;

                            NebMesh baked{};
                            if (!LoadNebMesh(meshPath, baked) || !baked.valid || baked.indices.size() < 3)
                            {
                                texWarnings.push_back("[Segment3.0] Mesh unavailable for Saturn geometry, using cube proxy: " + meshPath.string());
                                continue;
                            }

                            int triCount = (int)(baked.indices.size() / 3);
                            /* Triangle truncation safety fallback disabled per user request. */
                            if (triCount <= 0)
                                continue;

                            if (baked.hasFaceRecords && !baked.faceRecords.empty())
                            {
                                texWarnings.push_back("[Segment3.1] Canonical face records ACTIVE for " + sm.name + " faces=" + std::to_string((int)baked.faceRecords.size()));
                            }
                            else
                            {
                                texWarnings.push_back("[Segment3.1] Canonical face records MISSING for " + sm.name + " (re-export/re-import .nebmesh required)");
                                if (kRequireCanonicalFaceRecords)
                                {
                                    texWarnings.push_back("[Segment3.1] Skipping mesh due to strict canonical mode: " + sm.name);
                                    continue;
                                }
                            }

                            geoHasMesh[i] = 1;
                            geoDataOffset[i] = (int)geoVertData.size() / 12;

                            float sxm = std::max(0.01f, sm.scaleX);
                            float sym = std::max(0.01f, sm.scaleY);
                            float szm = std::max(0.01f, sm.scaleZ);
                            constexpr float kLocalMeshScale = 12.0f;

                            int dbgMissingPolyTex = 0;
                            int emittedPolyCount = 0;

                            auto lerp3 = [](const Vec3& a, const Vec3& b, float t)->Vec3 {
                                return Vec3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
                            };

                            auto emitPolyFromVertices = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d, int triMat, int repeatPow)
                            {
                                // Global Saturn bake-time V flip for all emitted polygons.
                                // (Do it in generated geometry order, not runtime input toggles.)
                                std::swap(a, d);
                                std::swap(b, c);

                                int ax = (int)std::lround(a.x * sxm * kLocalMeshScale);
                                int ay = (int)std::lround(-a.y * sym * kLocalMeshScale);
                                int az = (int)std::lround(a.z * szm * kLocalMeshScale);
                                int bx = (int)std::lround(b.x * sxm * kLocalMeshScale);
                                int by = (int)std::lround(-b.y * sym * kLocalMeshScale);
                                int bz = (int)std::lround(b.z * szm * kLocalMeshScale);
                                int cx = (int)std::lround(c.x * sxm * kLocalMeshScale);
                                int cy = (int)std::lround(-c.y * sym * kLocalMeshScale);
                                int cz = (int)std::lround(c.z * szm * kLocalMeshScale);
                                int dx = (int)std::lround(d.x * sxm * kLocalMeshScale);
                                int dy = (int)std::lround(-d.y * sym * kLocalMeshScale);
                                int dz = (int)std::lround(d.z * szm * kLocalMeshScale);

                                geoVertData.push_back(ax); geoVertData.push_back(ay); geoVertData.push_back(az);
                                geoVertData.push_back(bx); geoVertData.push_back(by); geoVertData.push_back(bz);
                                geoVertData.push_back(cx); geoVertData.push_back(cy); geoVertData.push_back(cz);
                                geoVertData.push_back(dx); geoVertData.push_back(dy); geoVertData.push_back(dz);

                                int triTexSlot = -1;
                                if (triMat >= 0 && triMat < kStaticMeshMaterialSlots) triTexSlot = meshMatTexSlot[i][triMat];
                                if (triTexSlot < 0) { triTexSlot = meshTexSlot[i]; dbgMissingPolyTex++; }
                                geoPolyTexSlot.push_back(triTexSlot);
                                geoPolyUvRepeatPow.push_back(repeatPow);
                                emittedPolyCount++;
                            };

                            auto emitPoly = [&](uint16_t ia, uint16_t ib, uint16_t ic, uint16_t id, int triMat)
                            {
                                if (ia >= baked.positions.size() || ib >= baked.positions.size() || ic >= baked.positions.size() || id >= baked.positions.size())
                                    return;

                                Vec3 a = baked.positions[ia];
                                Vec3 b = baked.positions[ib];
                                Vec3 c = baked.positions[ic];
                                Vec3 d = baked.positions[id];

                                int repeatPow = 0;
                                if (triMat >= 0 && triMat < kStaticMeshMaterialSlots)
                                    repeatPow = std::max(0, meshMatUvRepeatPow[i][triMat]);

                                bool isTri = (id == ia);
                                if (repeatPow <= 0)
                                {
                                    emitPolyFromVertices(a, b, c, d, triMat, repeatPow);
                                    return;
                                }

                                int effectivePow = std::min(repeatPow, 3);

                                if (!isTri)
                                {
                                    int div = 1 << effectivePow;
                                    for (int uy = 0; uy < div; ++uy)
                                    {
                                        float t0 = (float)uy / (float)div;
                                        float t1 = (float)(uy + 1) / (float)div;
                                        Vec3 l0 = lerp3(a, d, t0);
                                        Vec3 l1 = lerp3(a, d, t1);
                                        Vec3 r0 = lerp3(b, c, t0);
                                        Vec3 r1 = lerp3(b, c, t1);
                                        for (int ux = 0; ux < div; ++ux)
                                        {
                                            float s0 = (float)ux / (float)div;
                                            float s1 = (float)(ux + 1) / (float)div;
                                            Vec3 p00 = lerp3(l0, r0, s0);
                                            Vec3 p10 = lerp3(l0, r0, s1);
                                            Vec3 p11 = lerp3(l1, r1, s1);
                                            Vec3 p01 = lerp3(l1, r1, s0);
                                            emitPolyFromVertices(p00, p10, p11, p01, triMat, repeatPow);
                                        }
                                    }
                                }
                                else
                                {
                                    struct Tri3 { Vec3 a, b, c; };
                                    std::vector<Tri3> tris;
                                    tris.push_back(Tri3{ a, b, c });
                                    for (int level = 0; level < effectivePow; ++level)
                                    {
                                        std::vector<Tri3> next;
                                        next.reserve(tris.size() * 4);
                                        for (const Tri3& t : tris)
                                        {
                                            Vec3 ab = lerp3(t.a, t.b, 0.5f);
                                            Vec3 bc = lerp3(t.b, t.c, 0.5f);
                                            Vec3 ca = lerp3(t.c, t.a, 0.5f);
                                            next.push_back(Tri3{ t.a, ab, ca });
                                            next.push_back(Tri3{ ab, t.b, bc });
                                            next.push_back(Tri3{ ca, bc, t.c });
                                            next.push_back(Tri3{ ab, bc, ca });
                                        }
                                        tris.swap(next);
                                    }
                                    for (const Tri3& t : tris)
                                        emitPolyFromVertices(t.a, t.b, t.c, t.a, triMat, repeatPow);
                                }
                            };

                            if (baked.hasFaceRecords && !baked.faceRecords.empty())
                            {
                                // Segment 3.1b: canonical authored face path (no quad reconstruction heuristics).
                                // JO quads infer UVs from vertex order, so detect per-face mirrored mapping from
                                // geometry+UV handedness and apply a local permutation only when needed.
                                auto cross3 = [](const Vec3& a, const Vec3& b)->Vec3 {
                                    return Vec3{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
                                };
                                auto dot3 = [](const Vec3& a, const Vec3& b)->float {
                                    return a.x * b.x + a.y * b.y + a.z * b.z;
                                };

                                for (const auto& fr : baked.faceRecords)
                                {
                                    if (fr.arity == 4)
                                    {
                                        uint16_t q0 = fr.indices[0];
                                        uint16_t q1 = fr.indices[1];
                                        uint16_t q2 = fr.indices[2];
                                        uint16_t q3 = fr.indices[3];

                                        bool mirrored = false;
                                        if (q0 < baked.positions.size() && q1 < baked.positions.size() && q2 < baked.positions.size() && q3 < baked.positions.size())
                                        {
                                            const Vec3& p0 = baked.positions[q0];
                                            const Vec3& p1 = baked.positions[q1];
                                            const Vec3& p2 = baked.positions[q2];
                                            const Vec3& p3 = baked.positions[q3];

                                            Vec3 e10{ p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
                                            Vec3 e30{ p3.x - p0.x, p3.y - p0.y, p3.z - p0.z };
                                            Vec3 e20{ p2.x - p0.x, p2.y - p0.y, p2.z - p0.z };
                                            Vec3 n = cross3(e10, e20);
                                            float geomDet = dot3(cross3(e10, e30), n);

                                            float du1 = fr.uvs[1].x - fr.uvs[0].x;
                                            float dv1 = fr.uvs[1].y - fr.uvs[0].y;
                                            float du2 = fr.uvs[3].x - fr.uvs[0].x;
                                            float dv2 = fr.uvs[3].y - fr.uvs[0].y;
                                            float uvDet = du1 * dv2 - dv1 * du2;

                                            mirrored = ((geomDet * uvDet) < 0.0f);
                                        }

                                        Vec3 uv0 = fr.uvs[0];
                                        Vec3 uv1 = fr.uvs[1];
                                        Vec3 uv2 = fr.uvs[2];
                                        Vec3 uv3 = fr.uvs[3];

                                        if (mirrored)
                                        {
                                            // Local per-face mirror correction (U flip permutation).
                                            std::swap(q0, q1);
                                            std::swap(q2, q3);
                                            std::swap(uv0, uv1);
                                            std::swap(uv2, uv3);
                                        }

                                        // Phase alignment: pick a stable corner start from authored UVs,
                                        // then rotate quad order so Saturn uses consistent corner phase.
                                        uint16_t qv[4] = { q0, q1, q2, q3 };
                                        Vec3 uvs[4] = { uv0, uv1, uv2, uv3 };
                                        float uMin = uvs[0].x, vMin = uvs[0].y;
                                        for (int k = 1; k < 4; ++k)
                                        {
                                            if (uvs[k].x < uMin) uMin = uvs[k].x;
                                            if (uvs[k].y < vMin) vMin = uvs[k].y;
                                        }
                                        int bestRot = 0;
                                        float bestScore = 1e30f;
                                        for (int r = 0; r < 4; ++r)
                                        {
                                            const Vec3& uv = uvs[r];
                                            float du = uv.x - uMin;
                                            float dv = uv.y - vMin;
                                            float score = du * du + dv * dv;
                                            if (score < bestScore)
                                            {
                                                bestScore = score;
                                                bestRot = r;
                                            }
                                        }

                                        q0 = qv[bestRot & 3];
                                        q1 = qv[(bestRot + 1) & 3];
                                        q2 = qv[(bestRot + 2) & 3];
                                        q3 = qv[(bestRot + 3) & 3];

                                        emitPoly(q0, q1, q2, q3, (int)fr.material);
                                    }
                                    else
                                    {
                                        emitPoly(fr.indices[0], fr.indices[1], fr.indices[2], fr.indices[0], (int)fr.material);
                                    }
                                }
                            }
                            else if (baked.hasFaceTopology && !baked.faceVertexCounts.empty())
                            {
                                int triCursor = 0;
                                for (size_t fi = 0; fi < baked.faceVertexCounts.size() && triCursor < triCount; ++fi)
                                {
                                    int fv = (int)baked.faceVertexCounts[fi];
                                    if (fv < 3) continue;
                                    int faceTriCount = std::max(1, fv - 2);

                                    if (fv == 4 && triCursor + 1 < triCount)
                                    {
                                        uint16_t t0[3] = {
                                            baked.indices[triCursor * 3 + 0],
                                            baked.indices[triCursor * 3 + 1],
                                            baked.indices[triCursor * 3 + 2]
                                        };
                                        uint16_t t1[3] = {
                                            baked.indices[(triCursor + 1) * 3 + 0],
                                            baked.indices[(triCursor + 1) * 3 + 1],
                                            baked.indices[(triCursor + 1) * 3 + 2]
                                        };

                                        uint16_t shared[2] = { 0, 0 };
                                        int sharedCount = 0;
                                        uint16_t unique0 = t0[0];
                                        uint16_t unique1 = t1[0];

                                        auto hasIn = [](const uint16_t v[3], uint16_t x) {
                                            return (v[0] == x || v[1] == x || v[2] == x);
                                        };

                                        for (int vi = 0; vi < 3; ++vi)
                                        {
                                            if (hasIn(t1, t0[vi]))
                                            {
                                                if (sharedCount < 2) shared[sharedCount++] = t0[vi];
                                            }
                                            else
                                            {
                                                unique0 = t0[vi];
                                            }
                                        }
                                        for (int vi = 0; vi < 3; ++vi)
                                        {
                                            if (!hasIn(t0, t1[vi]))
                                            {
                                                unique1 = t1[vi];
                                                break;
                                            }
                                        }

                                        uint16_t a = t0[0], b = t0[1], c = t0[2], d = t1[2];
                                        if (sharedCount == 2)
                                        {
                                            // Legacy reconstruction fallback for old mesh versions.
                                            a = shared[0];
                                            b = unique0;
                                            c = shared[1];
                                            d = unique1;
                                        }

                                        int triMat = 0;
                                        if (baked.hasFaceMaterial && triCursor >= 0 && triCursor < (int)baked.faceMaterial.size()) triMat = (int)baked.faceMaterial[triCursor];
                                        emitPoly(a, b, c, d, triMat);
                                        triCursor += 2;
                                    }
                                    else
                                    {
                                        for (int ft = 0; ft < faceTriCount && triCursor < triCount; ++ft, ++triCursor)
                                        {
                                            uint16_t a = baked.indices[triCursor * 3 + 0];
                                            uint16_t b = baked.indices[triCursor * 3 + 1];
                                            uint16_t c = baked.indices[triCursor * 3 + 2];
                                            int triMat = 0;
                                            if (baked.hasFaceMaterial && triCursor >= 0 && triCursor < (int)baked.faceMaterial.size()) triMat = (int)baked.faceMaterial[triCursor];
                                            emitPoly(a, b, c, a, triMat);
                                        }
                                    }
                                }

                                for (; triCursor < triCount; ++triCursor)
                                {
                                    uint16_t a = baked.indices[triCursor * 3 + 0];
                                    uint16_t b = baked.indices[triCursor * 3 + 1];
                                    uint16_t c = baked.indices[triCursor * 3 + 2];
                                    int triMat = 0;
                                    if (baked.hasFaceMaterial && triCursor >= 0 && triCursor < (int)baked.faceMaterial.size()) triMat = (int)baked.faceMaterial[triCursor];
                                    emitPoly(a, b, c, a, triMat);
                                }
                            }
                            else
                            {
                                for (int t = 0; t < triCount; ++t)
                                {
                                    uint16_t a = baked.indices[t * 3 + 0];
                                    uint16_t b = baked.indices[t * 3 + 1];
                                    uint16_t c = baked.indices[t * 3 + 2];
                                    int triMat = 0;
                                    if (baked.hasFaceMaterial && t >= 0 && t < (int)baked.faceMaterial.size()) triMat = (int)baked.faceMaterial[t];
                                    emitPoly(a, b, c, a, triMat);
                                }
                            }

                            geoPolyCount[i] = emittedPolyCount;
                            texWarnings.push_back("[DebugFaceMap] " + sm.name + " fallbackPolyTex=" + std::to_string(dbgMissingPolyTex) + "/" + std::to_string(std::max(1, emittedPolyCount)));
                        }

                        std::ofstream mainC(buildDir / "main.c", std::ios::out | std::ios::trunc);
                        if (mainC.is_open())
                        {
                            mainC << "#include \"jo/jo.h\"\n";
                            mainC << "typedef struct MeshInst { int x,y,z, rx,ry,rz, sx,sy,sz; } MeshInst;\n";
                            mainC << "static jo_camera cam;\n";
                            mainC << "static int gDbgSprite = -1;\n";
                            mainC << "static int gTexSlots[64] = {"
                                  "-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,"
                                  "-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,"
                                  "-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,"
                                  "-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};\n";
                            mainC << "static jo_3d_mesh *gGeoMeshes[" << std::max(1, (int)exportStatics.size()) << "] = {0};\n";
                            mainC << "static jo_3d_mesh *gGeoMeshesBack[" << std::max(1, (int)exportStatics.size()) << "] = {0};\n";
                            mainC << "static jo_vertice cube_vertices[] = JO_3D_CUBE_VERTICES(8);\n";
                            mainC << "static jo_3d_quad cube_quads[6];\n";
                            mainC << "static int gInputRotX = 0;\n";
                            mainC << "static int gInputRotY = 0;\n";
                            mainC << "static int gFlipU = 0;\n";
                            mainC << "static int gFlipV = 0;\n";
                            mainC << "static int gNoCull = 0;\n";
                            mainC << "static int gNearClipAssist = 0;\n";
                            mainC << "static int gPrevC = 0;\n";
                            mainC << "static int gPrevA = 0;\n";
                            mainC << "static int gCamX = " << camX << ";\n";
                            mainC << "static int gCamY = " << camY << ";\n";
                            mainC << "static int gCamZ = " << camZ << ";\n";
                            mainC << "static int gTgtX = " << tgtX << ";\n";
                            mainC << "static int gTgtY = " << tgtY << ";\n";
                            mainC << "static int gTgtZ = " << tgtZ << ";\n";
                            mainC << "static int gStarInited = 0;\n";
                            mainC << "static int gStarCount = 24;\n";
                            mainC << "static int gStarX[24], gStarY[24], gStarZ[24], gStarBucket[24];\n";
                            mainC << "static const int gMeshCount = " << (int)exportStatics.size() << ";\n";
                            mainC << "static int gMeshCreateOk[" << std::max(1, (int)exportStatics.size()) << "] = {0};\n";
                            mainC << "static int gMeshCreateFailCount = 0;\n";
                            mainC << "static int gMeshCreateLastFail = -1;\n";
                            mainC << "static int gDrawMeshLimit = -1;\n";
                            mainC << "static MeshInst gMeshes[" << std::max(1, (int)exportStatics.size()) << "] = {\n";
                            if (exportStatics.empty())
                            {
                                mainC << "  {0,0,0,0,0,0,16,16,16}\n";
                            }
                            else
                            {
                                for (size_t i = 0; i < exportStatics.size(); ++i)
                                {
                                    const auto& s = exportStatics[i];
                                    int px = (int)std::lround(s.x * kSaturnScale);
                                    int py = (int)std::lround(-s.y * kSaturnScale);
                                    int pz = (int)std::lround(s.z * kSaturnScale);
                                    // Match StaticMesh rotation axis remap used by editor/runtime
                                    int rx = (int)std::lround(s.rotZ);
                                    int ry = (int)std::lround(s.rotX);
                                    int rz = (int)std::lround(s.rotY);
                                    int sx = (int)std::lround(std::max(0.25f, s.scaleX) * 16.0f);
                                    int sy = (int)std::lround(std::max(0.25f, s.scaleY) * 16.0f);
                                    int sz = (int)std::lround(std::max(0.25f, s.scaleZ) * 16.0f);
                                    mainC << "  {" << px << "," << py << "," << pz << "," << rx << "," << ry << "," << rz << "," << sx << "," << sy << "," << sz << "}";
                                    if (i + 1 < exportStatics.size()) mainC << ",";
                                    mainC << "\n";
                                }
                            }
                            mainC << "};\n";
                            mainC << "static int gMeshTex[" << std::max(1, (int)exportStatics.size()) << "] = {";
                            if (exportStatics.empty()) { mainC << "-1"; }
                            else { for (size_t i = 0; i < exportStatics.size(); ++i) { if (i != 0) mainC << ","; mainC << geoTexSlot[i]; } }
                            mainC << "};\n";
                            mainC << "static int gGeoHasMesh[" << std::max(1, (int)exportStatics.size()) << "] = {";
                            if (exportStatics.empty()) { mainC << "0"; }
                            else { for (size_t i = 0; i < exportStatics.size(); ++i) { if (i != 0) mainC << ","; mainC << geoHasMesh[i]; } }
                            mainC << "};\n";
                            mainC << "static int gGeoPolyCount[" << std::max(1, (int)exportStatics.size()) << "] = {";
                            if (exportStatics.empty()) { mainC << "0"; }
                            else { for (size_t i = 0; i < exportStatics.size(); ++i) { if (i != 0) mainC << ","; mainC << geoPolyCount[i]; } }
                            mainC << "};\n";
                            mainC << "static int gGeoDataOffset[" << std::max(1, (int)exportStatics.size()) << "] = {";
                            if (exportStatics.empty()) { mainC << "0"; }
                            else { for (size_t i = 0; i < exportStatics.size(); ++i) { if (i != 0) mainC << ","; mainC << geoDataOffset[i]; } }
                            mainC << "};\n";
                            mainC << "static int gGeoVertData[" << std::max(12, (int)geoVertData.size()) << "] = {";
                            if (geoVertData.empty()) { mainC << "0"; }
                            else { for (size_t i = 0; i < geoVertData.size(); ++i) { if (i != 0) mainC << ","; mainC << geoVertData[i]; } }
                            mainC << "};\n";
                            mainC << "static int gGeoPolyTexSlot[" << std::max(1, (int)geoPolyTexSlot.size()) << "] = {";
                            if (geoPolyTexSlot.empty()) { mainC << "-1"; }
                            else { for (size_t i = 0; i < geoPolyTexSlot.size(); ++i) { if (i != 0) mainC << ","; mainC << geoPolyTexSlot[i]; } }
                            mainC << "};\n";
                            mainC << "static int gGeoPolyUvRepeatPow[" << std::max(1, (int)geoPolyUvRepeatPow.size()) << "] = {";
                            if (geoPolyUvRepeatPow.empty()) { mainC << "0"; }
                            else { for (size_t i = 0; i < geoPolyUvRepeatPow.size(); ++i) { if (i != 0) mainC << ","; mainC << geoPolyUvRepeatPow[i]; } }
                            mainC << "};\n";
                            mainC << "static void apply_uv_flip_preview(void) {\n";
                            mainC << "  int i, p;\n";
                            mainC << "  for (i = 0; i < gMeshCount; ++i) {\n";
                            mainC << "    if (!gGeoHasMesh[i] || !gGeoMeshes[i] || gGeoPolyCount[i] <= 0) continue;\n";
                            mainC << "    for (p = 0; p < gGeoPolyCount[i]; ++p) {\n";
                            mainC << "      int di = (gGeoDataOffset[i] + p) * 12;\n";
                            mainC << "      int o0 = 0, o1 = 1, o2 = 2, o3 = 3;\n";
                            mainC << "      if (gFlipU) { int t0=o0, t2=o2; o0=o1; o1=t0; o2=o3; o3=t2; }\n";
                            mainC << "      if (gFlipV) { int t0=o0, t1=o1; o0=o3; o1=o2; o2=t1; o3=t0; }\n";
                            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshes[i], jo_int2fixed(gGeoVertData[di+o0*3+0]), jo_int2fixed(gGeoVertData[di+o0*3+1]), jo_int2fixed(gGeoVertData[di+o0*3+2]), p*4+0);\n";
                            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshes[i], jo_int2fixed(gGeoVertData[di+o1*3+0]), jo_int2fixed(gGeoVertData[di+o1*3+1]), jo_int2fixed(gGeoVertData[di+o1*3+2]), p*4+1);\n";
                            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshes[i], jo_int2fixed(gGeoVertData[di+o2*3+0]), jo_int2fixed(gGeoVertData[di+o2*3+1]), jo_int2fixed(gGeoVertData[di+o2*3+2]), p*4+2);\n";
                            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshes[i], jo_int2fixed(gGeoVertData[di+o3*3+0]), jo_int2fixed(gGeoVertData[di+o3*3+1]), jo_int2fixed(gGeoVertData[di+o3*3+2]), p*4+3);\n";
                            mainC << "      if (gGeoMeshesBack[i]) {\n";
                            mainC << "        jo_3d_set_mesh_vertice(gGeoMeshesBack[i], jo_int2fixed(gGeoVertData[di+o0*3+0]), jo_int2fixed(gGeoVertData[di+o0*3+1]), jo_int2fixed(gGeoVertData[di+o0*3+2]), p*4+0);\n";
                            mainC << "        jo_3d_set_mesh_vertice(gGeoMeshesBack[i], jo_int2fixed(gGeoVertData[di+o3*3+0]), jo_int2fixed(gGeoVertData[di+o3*3+1]), jo_int2fixed(gGeoVertData[di+o3*3+2]), p*4+1);\n";
                            mainC << "        jo_3d_set_mesh_vertice(gGeoMeshesBack[i], jo_int2fixed(gGeoVertData[di+o2*3+0]), jo_int2fixed(gGeoVertData[di+o2*3+1]), jo_int2fixed(gGeoVertData[di+o2*3+2]), p*4+2);\n";
                            mainC << "        jo_3d_set_mesh_vertice(gGeoMeshesBack[i], jo_int2fixed(gGeoVertData[di+o1*3+0]), jo_int2fixed(gGeoVertData[di+o1*3+1]), jo_int2fixed(gGeoVertData[di+o1*3+2]), p*4+3);\n";
                            mainC << "      }\n";
                            mainC << "    }\n";
                            mainC << "  }\n";
                            mainC << "}\n";
                            mainC << "static void update_input(void) {\n";
                            mainC << "  const int rotStep = 4;\n";
                            mainC << "  int c = jo_is_pad1_key_down(JO_KEY_C);\n";
                            mainC << "  {\n";
                            mainC << "    const int strafeStep = 3;\n";
                            mainC << "    int vx = gTgtX - gCamX;\n";
                            mainC << "    int vz = gTgtZ - gCamZ;\n";
                            mainC << "    int v2 = vx*vx + vz*vz;\n";
                            mainC << "    int vd = jo_sqrt(v2);\n";
                            mainC << "    int rx, rz;\n";
                            mainC << "    if (vd < 1) vd = 1;\n";
                            mainC << "    rx = (vz * strafeStep) / vd;\n";
                            mainC << "    rz = (-vx * strafeStep) / vd;\n";
                            mainC << "    if (jo_is_pad1_key_pressed(JO_KEY_LEFT)) { gCamX -= rx; gCamZ -= rz; gTgtX -= rx; gTgtZ -= rz; }\n";
                            mainC << "    if (jo_is_pad1_key_pressed(JO_KEY_RIGHT)) { gCamX += rx; gCamZ += rz; gTgtX += rx; gTgtZ += rz; }\n";
                            mainC << "  }\n";
                            mainC << "  {\n";
                            mainC << "    const int orbitStep = 3;\n";
                            mainC << "    int dxo = gCamX - gTgtX;\n";
                            mainC << "    int dzo = gCamZ - gTgtZ;\n";
                            mainC << "    int d2o = dxo*dxo + dzo*dzo;\n";
                            mainC << "    int do_ = jo_sqrt(d2o);\n";
                            mainC << "    if (do_ < 1) do_ = 1;\n";
                            mainC << "    if (jo_is_pad1_key_pressed(JO_KEY_L)) { gCamX += (dzo * orbitStep) / do_; gCamZ -= (dxo * orbitStep) / do_; }\n";
                            mainC << "    if (jo_is_pad1_key_pressed(JO_KEY_R)) { gCamX -= (dzo * orbitStep) / do_; gCamZ += (dxo * orbitStep) / do_; }\n";
                            mainC << "  }\n";
                            mainC << "  if (jo_is_pad1_key_pressed(JO_KEY_B))     gTgtY += 4;\n";
                            mainC << "  if (jo_is_pad1_key_pressed(JO_KEY_Y))     gTgtY -= 4;\n";
                            mainC << "  {\n";
                            mainC << "    const int zoomStep = 3;\n";
                            mainC << "    int dx = gCamX - gTgtX;\n";
                            mainC << "    int dy = gCamY - gTgtY;\n";
                            mainC << "    int dz = gCamZ - gTgtZ;\n";
                            mainC << "    int d2 = dx*dx + dy*dy + dz*dz;\n";
                            mainC << "    int d = jo_sqrt(d2);\n";
                            mainC << "    if (d < 1) d = 1;\n";
                            mainC << "    if (jo_is_pad1_key_pressed(JO_KEY_UP)) { gCamX -= (dx * zoomStep) / d; gCamY -= (dy * zoomStep) / d; gCamZ -= (dz * zoomStep) / d; }\n";
                            mainC << "    if (jo_is_pad1_key_pressed(JO_KEY_DOWN)) { gCamX += (dx * zoomStep) / d; gCamY += (dy * zoomStep) / d; gCamZ += (dz * zoomStep) / d; }\n";
                            mainC << "  }\n";
                            mainC << "  if (c && !gPrevC) { gFlipU = 0; gFlipV = 0; }\n";
                            mainC << "  gPrevC = c;\n";
                            mainC << "}\n";
                            mainC << "static void apply_near_clip_assist(void) {\n";
                            mainC << "  if (!gNearClipAssist) return;\n";
                            mainC << "  const int minDist = 180;\n";
                            mainC << "  int dx = gTgtX - gCamX;\n";
                            mainC << "  int dy = gTgtY - gCamY;\n";
                            mainC << "  int dz = gTgtZ - gCamZ;\n";
                            mainC << "  int d2 = dx*dx + dy*dy + dz*dz;\n";
                            mainC << "  int min2 = minDist * minDist;\n";
                            mainC << "  if (d2 > 0 && d2 < min2) {\n";
                            mainC << "    int d = jo_sqrt(d2);\n";
                            mainC << "    if (d < 1) d = 1;\n";
                            mainC << "    gCamX = gTgtX - (dx * minDist) / d;\n";
                            mainC << "    gCamY = gTgtY - (dy * minDist) / d;\n";
                            mainC << "    gCamZ = gTgtZ - (dz * minDist) / d;\n";
                            mainC << "  }\n";
                            mainC << "}\n";
                            mainC << "static void init_stars(void) {\n";
                            mainC << "  int i;\n";
                            mainC << "  if (gStarInited) return;\n";
                            mainC << "  gStarInited = 1;\n";
                            mainC << "  for (i = 0; i < gStarCount; ++i) {\n";
                            mainC << "    int a = (i * 73) & 1023;\n";
                            mainC << "    int b = (i * 151 + 97) & 1023;\n";
                            mainC << "    int r = 900 + (i % 5) * 180;\n";
                            mainC << "    int sx = (jo_cos(a) * r) >> 16;\n";
                            mainC << "    int sy = (jo_sin(b) * (r / 2)) >> 16;\n";
                            mainC << "    int sz = (jo_sin(a) * r) >> 16;\n";
                            mainC << "    gStarX[i] = gTgtX + sx;\n";
                            mainC << "    gStarY[i] = gTgtY + sy;\n";
                            mainC << "    gStarZ[i] = gTgtZ + sz;\n";
                            mainC << "    gStarBucket[i] = i % 3;\n";
                            mainC << "  }\n";
                            mainC << "}\n";
                            mainC << "static void draw_stars(void) {\n";
                            mainC << "  int i;\n";
                            mainC << "  if (gDbgSprite < 0) return;\n";
                            mainC << "  for (i = 0; i < gStarCount; ++i) {\n";
                            mainC << "    if (gStarBucket[i] == 0) jo_sprite_change_sprite_scale(0.25f);\n";
                            mainC << "    else if (gStarBucket[i] == 1) jo_sprite_change_sprite_scale(0.40f);\n";
                            mainC << "    else jo_sprite_change_sprite_scale(0.60f);\n";
                            mainC << "    jo_sprite_draw3D(gDbgSprite, gStarX[i], gStarY[i], gStarZ[i]);\n";
                            mainC << "  }\n";
                            mainC << "  jo_sprite_restore_sprite_scale();\n";
                            mainC << "}\n";
                            mainC << "static void draw(void) {\n";
                            mainC << "  int i;\n";
                            mainC << "  update_input();\n";
                            mainC << "  apply_uv_flip_preview();\n";
                            mainC << "  /* near-clip assist removed per user request */\n";
                            mainC << "  jo_3d_camera_set_viewpoint(&cam, gCamX, gCamY, gCamZ);\n";
                            mainC << "  jo_3d_camera_set_target(&cam, gTgtX, gTgtY, gTgtZ);\n";
                            mainC << "  jo_3d_camera_look_at(&cam);\n";
                            mainC << "  int drawCount = gMeshCount;\n";
                            mainC << "  int totalPolys = 0, submittedPolys = 0;\n";
                            mainC << "  const int polyBudget = 4096;\n";
                            mainC << "  int order[" << std::max(1, (int)exportStatics.size()) << "], dist2[" << std::max(1, (int)exportStatics.size()) << "], oi;\n";
                            mainC << "  /* draw limit disabled: always include full mesh list */\n";
                            mainC << "  for (i = 0; i < drawCount; ++i) {\n";
                            mainC << "    int dx = gMeshes[i].x - gCamX;\n";
                            mainC << "    int dy = gMeshes[i].y - gCamY;\n";
                            mainC << "    int dz = gMeshes[i].z - gCamZ;\n";
                            mainC << "    order[i] = i;\n";
                            mainC << "    dist2[i] = dx*dx + dy*dy + dz*dz;\n";
                            mainC << "    totalPolys += (gGeoHasMesh[i] && gGeoPolyCount[i] > 0) ? gGeoPolyCount[i] : 6;\n";
                            mainC << "  }\n";
                            mainC << "  /* strict index order draw: no distance sorting */\n";
                            mainC << "  /* solo mesh toggle removed: always draw full list */\n";
                            mainC << "  for (oi = 0; oi < drawCount; ++oi) {\n";
                            mainC << "    int mi = order[oi];\n";
                            mainC << "    int s, sid = 0;\n";
                            mainC << "    int meshPolys = (gGeoHasMesh[mi] && gGeoPolyCount[mi] > 0) ? gGeoPolyCount[mi] : 6;\n";
                            mainC << "    if (submittedPolys + meshPolys > polyBudget) continue;\n";
                            mainC << "    if (gMeshTex[mi] >= 0 && gMeshTex[mi] < 64 && gTexSlots[gMeshTex[mi]] >= 0) sid = gTexSlots[gMeshTex[mi]];\n";
                            mainC << "    jo_3d_push_matrix();\n";
                            mainC << "    jo_3d_translate_matrix(gMeshes[mi].x, gMeshes[mi].y, gMeshes[mi].z);\n";
                            mainC << "    jo_3d_rotate_matrix(gMeshes[mi].rx + gInputRotX, gMeshes[mi].ry + gInputRotY, gMeshes[mi].rz);\n";
                            mainC << "    if (gGeoHasMesh[mi] && gGeoMeshes[mi]) {\n";
                            mainC << "      jo_3d_mesh_draw(gGeoMeshes[mi]);\n";
                            mainC << "      if (gNoCull && gGeoMeshesBack[mi]) jo_3d_mesh_draw(gGeoMeshesBack[mi]);\n";
                            mainC << "    } else {\n";
                            mainC << "      for (s = 0; s < 6; ++s) jo_3d_set_texture(&cube_quads[s], sid);\n";
                            mainC << "      jo_3d_draw_array(cube_quads, 6);\n";
                            mainC << "    }\n";
                            mainC << "    jo_3d_pop_matrix();\n";
                            mainC << "    submittedPolys += meshPolys;\n";
                            mainC << "  }\n";
                            mainC << "  /* star pass removed */\n";
                            mainC << "  jo_printf(1, 1, \"Nebula Dreamcast S3\");\n";
                            mainC << "  jo_printf(1, 2, \"Meshes: " << (int)exportStatics.size() << " Cam: " << (int)exportCameras.size() << "\");\n";
                            mainC << "  jo_printf(1, 3, \"TexSlots: " << (int)slotFileName.size() << "\");\n";
                            mainC << "  jo_printf(1, 4, \"DPad L/R:Strafe  Shoulder L/R:Orbit  B/Y:LookUpDn\");\n";
                            mainC << "  jo_printf(1, 5, \"C:Reset UV\");\n";
                            mainC << "  jo_printf(1, 6, \"AllocFail:%d Last:%d Draw:%d\", gMeshCreateFailCount, gMeshCreateLastFail, drawCount);\n";
                            mainC << "  jo_printf(1, 7, \"Polys T:%d S:%d B:%d\", totalPolys, submittedPolys, polyBudget);\n";
                            mainC << "  if (gMeshCount > 0) jo_printf(1, 8, \"M0:%d P0:%d M1:%d P1:%d\", gMeshCreateOk[0], gGeoPolyCount[0], (gMeshCount>1?gMeshCreateOk[1]:-1), (gMeshCount>1?gGeoPolyCount[1]:-1));\n";
                            mainC << "  if (gMeshCount > 2) jo_printf(1, 9, \"M2:%d P2:%d M3:%d P3:%d\", gMeshCreateOk[2], gGeoPolyCount[2], (gMeshCount>3?gMeshCreateOk[3]:-1), (gMeshCount>3?gGeoPolyCount[3]:-1));\n";
                            mainC << "  if (gMeshCount > 4) jo_printf(1, 10, \"M4:%d P4:%d\", gMeshCreateOk[4], gGeoPolyCount[4]);\n";
                            mainC << "  if (drawCount > 0) jo_printf(1, 11, \"Order:%d %d %d %d %d\", order[0], (drawCount>1?order[1]:-1), (drawCount>2?order[2]:-1), (drawCount>3?order[3]:-1), (drawCount>4?order[4]:-1));\n";
                            mainC << "  jo_printf(1, 12, \"GEN:e0dfaa0+\");\n";
                            mainC << "  if (gDbgSprite >= 0) jo_sprite_draw3D(gDbgSprite, -140, 90, 320);\n";
                            mainC << "}\n";
                            mainC << "void jo_main(void) {\n";
                            mainC << "  int i;\n";
                            mainC << "  jo_core_init(JO_COLOR_Black);\n";
                            mainC << "  jo_3d_camera_init(&cam);\n";
                            mainC << "  jo_3d_camera_set_viewpoint(&cam, " << camX << ", " << camY << ", " << camZ << ");\n";
                            mainC << "  jo_3d_camera_set_target(&cam, " << tgtX << ", " << tgtY << ", " << tgtZ << ");\n";
                            mainC << "  jo_3d_create_cube(cube_quads, cube_vertices);\n";
                            mainC << "  gTexSlots[0] = jo_sprite_add_tga(JO_ROOT_DIR, \"BOX.TGA\", JO_COLOR_Transparent);\n";
                            for (size_t ti = 0; ti < slotFileName.size(); ++ti)
                            {
                                mainC << "  gTexSlots[" << ti << "] = jo_sprite_add_tga(JO_ROOT_DIR, \"" << slotFileName[ti] << "\", JO_COLOR_Transparent);\n";
                            }
                            mainC << "  gDbgSprite = gTexSlots[0];\n";
                            mainC << "  /* stars init removed */\n";
                            mainC << "  for (i = 0; i < gMeshCount; ++i) {\n";
                            mainC << "    int p, sid = 0;\n";
                            mainC << "    if (!gGeoHasMesh[i] || gGeoPolyCount[i] <= 0) continue;\n";
                            mainC << "    if (gMeshTex[i] >= 0 && gMeshTex[i] < 64 && gTexSlots[gMeshTex[i]] >= 0) sid = gTexSlots[gMeshTex[i]];\n";
                            mainC << "    gGeoMeshes[i] = jo_3d_create_mesh(gGeoPolyCount[i]);\n";
                            mainC << "    gGeoMeshesBack[i] = jo_3d_create_mesh(gGeoPolyCount[i]);\n";
                            mainC << "    gMeshCreateOk[i] = (gGeoMeshes[i] != JO_NULL && gGeoMeshesBack[i] != JO_NULL) ? 1 : 0;\n";
                            mainC << "    if (!gMeshCreateOk[i]) { ++gMeshCreateFailCount; gMeshCreateLastFail = i; continue; }\n";
                            mainC << "    for (p = 0; p < gGeoPolyCount[i]; ++p) {\n";
                            mainC << "      int di = (gGeoDataOffset[i] + p) * 12;\n";
                            mainC << "      int ts = gGeoPolyTexSlot[gGeoDataOffset[i] + p];\n";
                            mainC << "      if (ts >= 0 && ts < 64 && gTexSlots[ts] >= 0) sid = gTexSlots[ts];\n";
                            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshes[i], jo_int2fixed(gGeoVertData[di+0]), jo_int2fixed(gGeoVertData[di+1]), jo_int2fixed(gGeoVertData[di+2]), p*4+0);\n";
                            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshes[i], jo_int2fixed(gGeoVertData[di+3]), jo_int2fixed(gGeoVertData[di+4]), jo_int2fixed(gGeoVertData[di+5]), p*4+1);\n";
                            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshes[i], jo_int2fixed(gGeoVertData[di+6]), jo_int2fixed(gGeoVertData[di+7]), jo_int2fixed(gGeoVertData[di+8]), p*4+2);\n";
                            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshes[i], jo_int2fixed(gGeoVertData[di+9]), jo_int2fixed(gGeoVertData[di+10]), jo_int2fixed(gGeoVertData[di+11]), p*4+3);\n";
                            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshesBack[i], jo_int2fixed(gGeoVertData[di+0]), jo_int2fixed(gGeoVertData[di+1]), jo_int2fixed(gGeoVertData[di+2]), p*4+0);\n";
                            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshesBack[i], jo_int2fixed(gGeoVertData[di+9]), jo_int2fixed(gGeoVertData[di+10]), jo_int2fixed(gGeoVertData[di+11]), p*4+1);\n";
                            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshesBack[i], jo_int2fixed(gGeoVertData[di+6]), jo_int2fixed(gGeoVertData[di+7]), jo_int2fixed(gGeoVertData[di+8]), p*4+2);\n";
                            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshesBack[i], jo_int2fixed(gGeoVertData[di+3]), jo_int2fixed(gGeoVertData[di+4]), jo_int2fixed(gGeoVertData[di+5]), p*4+3);\n";
                            mainC << "      if (sid >= 0) { jo_3d_set_mesh_polygon_texture(gGeoMeshes[i], sid, p); jo_3d_set_mesh_polygon_texture(gGeoMeshesBack[i], sid, p); }\n";
                            mainC << "      else { jo_3d_set_mesh_polygon_color(gGeoMeshes[i], JO_COLOR_Green, p); jo_3d_set_mesh_polygon_color(gGeoMeshesBack[i], JO_COLOR_Green, p); }\n";
                            mainC << "    }\n";
                            mainC << "  }\n";
                            mainC << "  for (i = 0; i < 6; ++i) jo_3d_set_texture(&cube_quads[i], 0);\n";
                            mainC << "  jo_core_add_callback(draw);\n";
                            mainC << "  jo_core_run();\n";
                            mainC << "}\n";
                        }
                    }

                    // Build via JO engine toolchain to produce game.iso + game.cue in project/build_saturn.
                    std::filesystem::path joRoot;
                    {
                        std::filesystem::path cwd = std::filesystem::current_path();
                        std::vector<std::filesystem::path> roots = {
                            cwd / "thirdparty" / "JO_engine",
                            cwd / "thirdparty" / "joengine",
                            cwd / ".." / "thirdparty" / "JO_engine",
                            cwd / ".." / "thirdparty" / "joengine",
                            cwd / ".." / ".." / "thirdparty" / "JO_engine",
                            cwd / ".." / ".." / "thirdparty" / "joengine",
                            cwd / ".." / ".." / ".." / "JO_engine",
                            cwd / ".." / ".." / ".." / "joengine",
                            std::filesystem::path("C:/Users/NoSig/Documents/SaturnDev/JO_engine")
                        };
                        for (const auto& r : roots)
                        {
                            std::filesystem::path mk = r / "Compiler" / "COMMON" / "jo_engine_makefile";
                            std::filesystem::path makeExe = r / "Compiler" / "WINDOWS" / "Other Utilities" / "make.exe";
                            if (std::filesystem::exists(mk) && std::filesystem::exists(makeExe))
                            {
                                joRoot = r;
                                break;
                            }
                        }
                    }

                    if (joRoot.empty())
                    {
                        gViewportToast = "JO_engine toolchain not found";
                        gViewportToastUntil = glfwGetTime() + 3.0;
                    }
                    else
                    {
                        std::filesystem::path compilerDir = joRoot / "Compiler";
                        std::string joEngineSrc = (joRoot / "jo_engine").string();
                        std::string compDir = compilerDir.string();
                        std::replace(joEngineSrc.begin(), joEngineSrc.end(), '\\', '/');
                        std::replace(compDir.begin(), compDir.end(), '\\', '/');

                        {
                            std::ofstream mk(buildDir / "makefile", std::ios::out | std::ios::trunc);
                            if (mk.is_open())
                            {
                                mk << "JO_COMPILE_WITH_VIDEO_MODULE = 0\n";
                                mk << "JO_COMPILE_WITH_BACKUP_MODULE = 0\n";
                                mk << "JO_COMPILE_WITH_TGA_MODULE = 1\n";
                                mk << "JO_COMPILE_WITH_AUDIO_MODULE = 0\n";
                                mk << "JO_COMPILE_WITH_3D_MODULE = 1\n";
                                mk << "JO_COMPILE_WITH_PSEUDO_MODE7_MODULE = 0\n";
                                mk << "JO_COMPILE_WITH_EFFECTS_MODULE = 0\n";
                                mk << "JO_DEBUG = 1\n";
                                mk << "SRCS=main.c\n";
                                mk << "JO_ENGINE_SRC_DIR=" << joEngineSrc << "\n";
                                mk << "COMPILER_DIR=" << compDir << "\n";
                                mk << "include $(COMPILER_DIR)/COMMON/jo_engine_makefile\n";
                            }
                        }

                        std::string buildDirStr = buildDir.string();
                        std::string compilerWin = compilerDir.string();
                        std::filesystem::path logPath = buildDir / "package.log";
                        std::string logPathStr = logPath.string();
                        {
                            std::ofstream logOut(logPath, std::ios::out | std::ios::trunc);
                            if (logOut.is_open())
                            {
                                logOut << "[Package] buildDir=" << buildDir.string() << "\n";
                                logOut << "[Package] joRoot=" << joRoot.string() << "\n";
                                logOut << "[Package] compilerDir=" << compilerDir.string() << "\n";
                                if (!texWarnings.empty())
                                {
                                    for (const auto& w : texWarnings)
                                        logOut << w << "\n";
                                }
                            }
                        }
                        std::filesystem::path makeExe = compilerDir / "WINDOWS" / "Other Utilities" / "make.exe";
                        std::string makeExeStr = makeExe.string();

                        int buildRc = -1;
                        if (!std::filesystem::exists(makeExe))
                        {
                            printf("[Package] make.exe not found: %s\n", makeExeStr.c_str());
                            {
                                std::ofstream logOut(logPath, std::ios::out | std::ios::app);
                                if (logOut.is_open())
                                    logOut << "[Error] make.exe not found: " << makeExeStr << "\n";
                            }
                            gViewportToast = "Dreamcast build failed (make.exe missing)";
                            gViewportToastUntil = glfwGetTime() + 3.0;
                        }
                        else
                        {
                            std::string cmd =
                                "cmd /c \"set \"PATH=" + compilerWin + "\\WINDOWS\\bin;" + compilerWin + "\\WINDOWS\\Other Utilities;%PATH%\" && \"" + makeExeStr + "\" -C \"" + buildDirStr + "\" clean game.raw >> \"" + logPathStr + "\" 2>&1\"";
                            {
                                std::ofstream logOut(logPath, std::ios::out | std::ios::app);
                                if (logOut.is_open())
                                    logOut << "[Package] cmd=" << cmd << "\n";
                            }
                            buildRc = RunCommand(cmd.c_str());
                        }

                        std::filesystem::path cuePath;
                        if (buildRc != 0)
                        {
                            printf("[Package] Dreamcast build command failed (code=%d). Log: %s\n", buildRc, logPathStr.c_str());
                            gViewportToast = "Dreamcast build failed (see build_saturn/package.log)";
                        }
                        else
                        {
                            // Build the disc image explicitly (jo makefile iso target is fragile on some Windows setups).
                            std::filesystem::path rawPath = buildDir / "game.raw";
                            std::filesystem::path cd0Path = cdDir / "0.bin";
                            std::filesystem::path isoPath = buildDir / "game.iso";
                            std::filesystem::path mkisofsExe = compilerDir / "WINDOWS" / "Other Utilities" / "mkisofs.exe";
                            std::filesystem::path ipBin = compilerDir / "COMMON" / "IP.BIN";

                            if (!std::filesystem::exists(rawPath))
                            {
                                std::ofstream logOut(logPath, std::ios::out | std::ios::app);
                                if (logOut.is_open()) logOut << "[Error] Missing game.raw after build\n";
                                gViewportToast = "Dreamcast build failed (missing game.raw)";
                            }
                            else if (!std::filesystem::exists(mkisofsExe) || !std::filesystem::exists(ipBin))
                            {
                                std::ofstream logOut(logPath, std::ios::out | std::ios::app);
                                if (logOut.is_open()) logOut << "[Error] Missing mkisofs.exe or IP.BIN\n";
                                gViewportToast = "Dreamcast build failed (mkisofs/IP.BIN missing)";
                            }
                            else
                            {
                                std::error_code fsec;
                                std::filesystem::copy_file(rawPath, cd0Path, std::filesystem::copy_options::overwrite_existing, fsec);

                                std::string mkCmd =
                                    "cmd /c \"cd /d \"" + buildDir.string() +
                                    "\" && \"" + mkisofsExe.string() +
                                    "\" -quiet -sysid \"SEGA SATURN\" -volid \"SaturnApp\" -volset \"SaturnApp\" -sectype 2352" +
                                    " -publisher \"SEGA ENTERPRISES, LTD.\" -preparer \"SEGA ENTERPRISES, LTD.\" -appid \"SaturnApp\"" +
                                    " -abstract \"./cd/ABS.TXT\" -copyright \"./cd/CPY.TXT\" -biblio \"./cd/BIB.TXT\"" +
                                    " -generic-boot \"" + ipBin.string() + "\"" +
                                    " -full-iso9660-filenames -o \"./game.iso\" ./cd >> \"" + logPathStr + "\" 2>&1\"";

                                {
                                    std::ofstream logOut(logPath, std::ios::out | std::ios::app);
                                    if (logOut.is_open()) logOut << "[Package] mkisofs_cmd=" << mkCmd << "\n";
                                }

                                int mkRc = RunCommand(mkCmd.c_str());
                                if (mkRc == 0 && std::filesystem::exists(isoPath))
                                {
                                    cuePath = buildDir / "game.cue";
                                    std::ofstream cue(cuePath, std::ios::out | std::ios::trunc);
                                    if (cue.is_open())
                                    {
                                        cue << "FILE \"game.iso\" BINARY\n";
                                        cue << "  TRACK 01 MODE1/2048\n";
                                        cue << "    INDEX 01 00:00:00\n";
                                    }
                                }
                            }

                            if (!cuePath.empty() && std::filesystem::exists(cuePath))
                            {
                                printf("[Package] CUE ready: %s\n", cuePath.string().c_str());
                                gViewportToast = "Dreamcast package ready: " + cuePath.filename().string();
                            }
                            else
                            {
                                printf("[Package] Could not generate CUE in %s\n", buildDirStr.c_str());
                                gViewportToast = "Dreamcast package failed (see package.log)";
                            }
                        }
                        gViewportToastUntil = glfwGetTime() + 3.0;
                    }
                    }
                }
            }
            ImGui::EndPopup();
        }

        ImGui::End();

        if (showPreferences)
        {
            ImGui::Begin("Preferences", &showPreferences);
            if (ImGui::SliderFloat("UI Scale", &uiScale, 0.75f, 2.5f, "%.2f"))
            {
                ImGui::GetIO().FontGlobalScale = uiScale;
            }
            const char* themes[] = { "Space", "Slate (6D7F89 -> 2A363D)", "Arctic (75A8B2 -> 162229)", "Classic (7F7E83 -> 5A595C)", "Black" };
            ImGui::Combo("Theme", &themeMode, themes, IM_ARRAYSIZE(themes));
            ImGui::Checkbox("Hide unselected wireframes", &gHideUnselectedWireframes);

            static char dreamSdkBuf[512] = {0};
            static char vcvarsBuf[512] = {0};
            static bool prefPathInit = false;
            if (!prefPathInit)
            {
                strncpy(dreamSdkBuf, gPrefDreamSdkHome.c_str(), sizeof(dreamSdkBuf) - 1);
                strncpy(vcvarsBuf, gPrefVcvarsPath.c_str(), sizeof(vcvarsBuf) - 1);
                prefPathInit = true;
            }
            ImGui::InputText("DreamSDK Home", dreamSdkBuf, sizeof(dreamSdkBuf));
            ImGui::InputText("VC Vars Batch", vcvarsBuf, sizeof(vcvarsBuf));
            gPrefDreamSdkHome = dreamSdkBuf;
            gPrefVcvarsPath = vcvarsBuf;

            ImGui::Separator();
            if (ImGui::Button("Save"))
            {
                SavePreferences(uiScale, themeMode);
            }
            ImGui::End();
        }

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

            for (int i = 0; i < (int)gAudio3DNodes.size(); ++i)
            {
                const auto& n = gAudio3DNodes[i];
                float sx, sy;
                if (ProjectToScreenGL({ n.x, n.y, n.z }, sx, sy, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y))
                {
                    float dx = io.MousePos.x - sx;
                    float dy = io.MousePos.y - sy;
                    float d = sqrtf(dx*dx + dy*dy);
                    if (d < bestDist)
                    {
                        bestDist = d;
                        bestAudioIndex = i;
                        bestStaticIndex = -1;
                    }
                }
            }

            for (int i = 0; i < (int)gStaticMeshNodes.size(); ++i)
            {
                const auto& n = gStaticMeshNodes[i];
                float sx, sy;
                if (ProjectToScreenGL({ n.x, n.y, n.z }, sx, sy, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y))
                {
                    float dx = io.MousePos.x - sx;
                    float dy = io.MousePos.y - sy;
                    float d = sqrtf(dx*dx + dy*dy);
                    if (d < bestDist)
                    {
                        bestDist = d;
                        bestAudioIndex = -1;
                        bestStaticIndex = i;
                    }
                }
            }

            if (gTransformMode == Transform_None)
            {
                int prevAudioSel = gSelectedAudio3D;
                int prevStaticSel = gSelectedStaticMesh;
                if (bestDist < 80.0f)
                {
                    gSelectedAudio3D = bestAudioIndex;
                    gSelectedStaticMesh = bestStaticIndex;
                }
                else
                {
                    gSelectedAudio3D = -1;
                    gSelectedStaticMesh = -1;
                }

                if (gSelectedAudio3D != prevAudioSel || gSelectedStaticMesh != prevStaticSel)
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
                    gSelectedAudio3D = -1;
                    gSelectedStaticMesh = -1;
                    gTransformMode = Transform_None;
                    gAxisLock = 0;
                    gHasRotatePreview = false;
                }
            }
            else
            {
                if (bestDist >= 80.0f)
                {
                    gSelectedAudio3D = -1;
                    gSelectedStaticMesh = -1;
                    gTransformMode = Transform_None;
                    gAxisLock = 0;
                }
            }
        }
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + topBarH));
        ImGui::SetNextWindowSize(ImVec2(leftPanelWidth, leftPanelHeight - assetsHeight));
        ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);
        ImGui::Text("Scene");
        ImGui::Separator();

        if (ImGui::BeginPopupContextWindow("SceneContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::BeginMenu("Create Node"))
            {
                if (ImGui::MenuItem("Audio3D"))
                {
                    Audio3DNode node;
                    node.name = "Audio3D" + std::to_string((int)gAudio3DNodes.size() + 1);
                    int idx = (int)gAudio3DNodes.size();
                    gAudio3DNodes.push_back(node);

                    PushUndo({"Create Audio3D",
                        [idx]() { if (idx >= 0 && idx < (int)gAudio3DNodes.size()) gAudio3DNodes.erase(gAudio3DNodes.begin() + idx); },
                        [idx, node]() {
                            if (idx < 0) return;
                            int i = idx;
                            if (i > (int)gAudio3DNodes.size()) i = (int)gAudio3DNodes.size();
                            gAudio3DNodes.insert(gAudio3DNodes.begin() + i, node);
                        }
                    });
                }
                if (ImGui::MenuItem("StaticMesh3D"))
                {
                    StaticMesh3DNode node;
                    node.name = "StaticMesh3D" + std::to_string((int)gStaticMeshNodes.size() + 1);
                    if (!gProjectDir.empty())
                    {
                        EnsureDefaultCubeNebmesh(std::filesystem::path(gProjectDir));
                        node.mesh = "Assets/cube_primitive.nebmesh";
                        AutoAssignMaterialSlotsFromMesh(node);
                    }
                    int idx = (int)gStaticMeshNodes.size();
                    gStaticMeshNodes.push_back(node);

                    PushUndo({"Create StaticMesh3D",
                        [idx]() { if (idx >= 0 && idx < (int)gStaticMeshNodes.size()) gStaticMeshNodes.erase(gStaticMeshNodes.begin() + idx); },
                        [idx, node]() {
                            if (idx < 0) return;
                            int i = idx;
                            if (i > (int)gStaticMeshNodes.size()) i = (int)gStaticMeshNodes.size();
                            gStaticMeshNodes.insert(gStaticMeshNodes.begin() + i, node);
                        }
                    });
                }
                if (ImGui::MenuItem("Camera3D"))
                {
                    Camera3DNode node;
                    node.name = "Camera3D" + std::to_string((int)gCamera3DNodes.size() + 1);
                    bool hasMain = false;
                    for (const auto& c : gCamera3DNodes) { if (c.main) { hasMain = true; break; } }
                    if (!hasMain) node.main = true;
                    int idx = (int)gCamera3DNodes.size();
                    gCamera3DNodes.push_back(node);

                    PushUndo({"Create Camera3D",
                        [idx]() { if (idx >= 0 && idx < (int)gCamera3DNodes.size()) gCamera3DNodes.erase(gCamera3DNodes.begin() + idx); },
                        [idx, node]() {
                            if (idx < 0) return;
                            int i = idx;
                            if (i > (int)gCamera3DNodes.size()) i = (int)gCamera3DNodes.size();
                            gCamera3DNodes.insert(gCamera3DNodes.begin() + i, node);
                        }
                    });
                }
                if (ImGui::MenuItem("Node3D"))
                {
                    Node3DNode node;
                    node.name = "Node3D" + std::to_string((int)gNode3DNodes.size() + 1);
                    node.primitiveMesh = "assets/cube_primitive.nebmesh";
                    int idx = (int)gNode3DNodes.size();
                    gNode3DNodes.push_back(node);
                    gSelectedNode3D = idx;
                    gSelectedAudio3D = -1;
                    gSelectedStaticMesh = -1;
                    gSelectedCamera3D = -1;

                    PushUndo({"Create Node3D",
                        [idx]() { if (idx >= 0 && idx < (int)gNode3DNodes.size()) gNode3DNodes.erase(gNode3DNodes.begin() + idx); },
                        [idx, node]() {
                            if (idx < 0) return;
                            int i = idx;
                            if (i > (int)gNode3DNodes.size()) i = (int)gNode3DNodes.size();
                            gNode3DNodes.insert(gNode3DNodes.begin() + i, node);
                        }
                    });
                }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        // List Audio3D nodes
        auto findAudioByNameLocal = [&](const std::string& nm)->int { for (int ai=0; ai<(int)gAudio3DNodes.size(); ++ai) if (gAudio3DNodes[ai].name == nm) return ai; return -1; };
        for (int i = 0; i < (int)gAudio3DNodes.size(); ++i)
        {
            auto& n = gAudio3DNodes[i];

            // Hide descendants of collapsed Audio3D parents.
            bool skipAudioNode = false;
            {
                std::string p = n.parent;
                while (!p.empty())
                {
                    int pi = findAudioByNameLocal(p);
                    if (pi < 0) break;
                    if (gCollapsedAudioRoots.find(p) != gCollapsedAudioRoots.end()) { skipAudioNode = true; break; }
                    p = gAudio3DNodes[pi].parent;
                }
            }
            if (skipAudioNode) continue;

            ImGui::PushID(i);
            {
                bool hasAudioChild = false;
                for (int ci = 0; ci < (int)gAudio3DNodes.size(); ++ci) if (gAudio3DNodes[ci].parent == n.name) { hasAudioChild = true; break; }
                for (int ci = 0; !hasAudioChild && ci < (int)gStaticMeshNodes.size(); ++ci) if (gStaticMeshNodes[ci].parent == n.name) { hasAudioChild = true; break; }
                for (int ci = 0; !hasAudioChild && ci < (int)gCamera3DNodes.size(); ++ci) if (gCamera3DNodes[ci].parent == n.name) { hasAudioChild = true; break; }
                for (int ci = 0; !hasAudioChild && ci < (int)gNode3DNodes.size(); ++ci) if (gNode3DNodes[ci].parent == n.name) { hasAudioChild = true; break; }
                bool audioExpanded = (gCollapsedAudioRoots.find(n.name) == gCollapsedAudioRoots.end());
                if (hasAudioChild)
                {
                    if (ImGui::SmallButton(audioExpanded ? "v" : ">"))
                    {
                        if (audioExpanded) gCollapsedAudioRoots.insert(n.name);
                        else gCollapsedAudioRoots.erase(n.name);
                    }
                    ImGui::SameLine();
                }
            }

            bool selected = (gSelectedAudio3D == i);
            if (ImGui::Selectable(n.name.c_str(), selected))
            {
                gSelectedAudio3D = i;
                gSelectedStaticMesh = -1;
                gSelectedCamera3D = -1;
                gSelectedNode3D = -1;
                gTransforming = false;
                gTransformMode = Transform_None;
                gAxisLock = 0;
                gLastTransformMouseX = io.MousePos.x;
                gLastTransformMouseY = io.MousePos.y;
            }

            if (ImGui::BeginDragDropSource())
            {
                int payload = i;
                ImGui::SetDragDropPayload("SCENE_AUDIO3D", &payload, sizeof(int));
                ImGui::Text("%s", n.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                auto tryReparentToAudio = [&](const char* payloadType, auto&& getName, auto&& setParent)
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadType))
                    {
                        int child = *(const int*)payload->Data;
                        std::string childName;
                        if (getName(child, childName) && !WouldCreateHierarchyCycle(childName, n.name))
                            setParent(child, n.name);
                    }
                };
                tryReparentToAudio("SCENE_AUDIO3D",
                    [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gAudio3DNodes.size() || child == i) return false; childName = gAudio3DNodes[child].name; return true; },
                    [&](int child, const std::string& parent){ gAudio3DNodes[child].parent = parent; });
                tryReparentToAudio("SCENE_STATICMESH",
                    [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gStaticMeshNodes.size()) return false; childName = gStaticMeshNodes[child].name; return true; },
                    [&](int child, const std::string& parent){ ReparentStaticMeshKeepWorldPos(child, parent); });
                tryReparentToAudio("SCENE_CAMERA3D",
                    [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gCamera3DNodes.size()) return false; childName = gCamera3DNodes[child].name; return true; },
                    [&](int child, const std::string& parent){ gCamera3DNodes[child].parent = parent; });
                tryReparentToAudio("SCENE_NODE3D",
                    [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gNode3DNodes.size()) return false; childName = gNode3DNodes[child].name; return true; },
                    [&](int child, const std::string& parent){ gNode3DNodes[child].parent = parent; });
                ImGui::EndDragDropTarget();
            }

            bool requestDelete = false;
            if (ImGui::BeginPopupContextItem("NodeContext"))
            {
                if (ImGui::MenuItem("Rename"))
                {
                    gNodeRenameIndex = i;
                    gNodeRenameStatic = false;
                    gNodeRenameCamera = false;
                    gNodeRenameNode3D = false;
                    strncpy_s(gNodeRenameBuffer, n.name.c_str(), sizeof(gNodeRenameBuffer) - 1);
                    gNodeRenameOpen = true;
                }
                if (ImGui::MenuItem("Unlink Hierarchy"))
                {
                    n.parent.clear();
                    for (auto& a : gAudio3DNodes) if (a.parent == n.name) a.parent.clear();
                    for (auto& s : gStaticMeshNodes) if (s.parent == n.name) s.parent.clear();
                    for (auto& c : gCamera3DNodes) if (c.parent == n.name) c.parent.clear();
                    for (auto& n3 : gNode3DNodes) if (n3.parent == n.name) n3.parent.clear();
                }
                if (ImGui::MenuItem("Delete"))
                {
                    requestDelete = true;
                }
                ImGui::EndPopup();
            }

            if (requestDelete)
            {
                int idx = i;
                Audio3DNode deleted = n;
                gAudio3DNodes.erase(gAudio3DNodes.begin() + idx);
                for (auto& n3 : gNode3DNodes)
                    if (n3.parent == deleted.name) n3.parent.clear();
                if (gSelectedAudio3D == idx) gSelectedAudio3D = -1;
                else if (gSelectedAudio3D > idx) gSelectedAudio3D--;

                PushUndo({"Delete Audio3D",
                    [idx, deleted]() {
                        int i = idx;
                        if (i < 0) return;
                        if (i > (int)gAudio3DNodes.size()) i = (int)gAudio3DNodes.size();
                        gAudio3DNodes.insert(gAudio3DNodes.begin() + i, deleted);
                    },
                    [idx]() {
                        if (idx >= 0 && idx < (int)gAudio3DNodes.size()) gAudio3DNodes.erase(gAudio3DNodes.begin() + idx);
                    }
                });

                ImGui::PopID();
                continue;
            }


            ImGui::PopID();
        }

        if (!gStaticMeshNodes.empty())
        {
            std::function<void(int,int)> drawStaticNode = [&](int i, int depth)
            {
                auto& n = gStaticMeshNodes[i];
                ImGui::PushID(10000 + i);
                if (depth > 0) ImGui::Indent(14.0f * depth);

                bool hasStaticChild = false;
                for (int ci = 0; ci < (int)gStaticMeshNodes.size(); ++ci)
                    if (gStaticMeshNodes[ci].parent == n.name) { hasStaticChild = true; break; }
                for (int ci = 0; !hasStaticChild && ci < (int)gAudio3DNodes.size(); ++ci) if (gAudio3DNodes[ci].parent == n.name) { hasStaticChild = true; break; }
                for (int ci = 0; !hasStaticChild && ci < (int)gCamera3DNodes.size(); ++ci) if (gCamera3DNodes[ci].parent == n.name) { hasStaticChild = true; break; }
                for (int ci = 0; !hasStaticChild && ci < (int)gNode3DNodes.size(); ++ci) if (gNode3DNodes[ci].parent == n.name) { hasStaticChild = true; break; }
                bool staticExpanded = (gCollapsedStaticRoots.find(n.name) == gCollapsedStaticRoots.end());
                if (hasStaticChild)
                {
                    if (ImGui::SmallButton(staticExpanded ? "v" : ">"))
                    {
                        if (staticExpanded) gCollapsedStaticRoots.insert(n.name);
                        else gCollapsedStaticRoots.erase(n.name);
                        staticExpanded = !staticExpanded;
                    }
                    ImGui::SameLine();
                }
                bool selected = (gSelectedStaticMesh == i);
                if (ImGui::Selectable(n.name.c_str(), selected))
                {
                    gSelectedStaticMesh = i;
                    gSelectedAudio3D = -1;
                    gSelectedCamera3D = -1;
                    gSelectedNode3D = -1;
                    gTransforming = false;
                    gTransformMode = Transform_None;
                    gAxisLock = 0;
                    gLastTransformMouseX = io.MousePos.x;
                    gLastTransformMouseY = io.MousePos.y;
                }

                if (ImGui::BeginDragDropSource())
                {
                    int payload = i;
                    ImGui::SetDragDropPayload("SCENE_STATICMESH", &payload, sizeof(int));
                    ImGui::Text("%s", n.name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    auto tryReparentToStatic = [&](const char* payloadType, auto&& getName, auto&& setParent)
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadType))
                        {
                            int child = *(const int*)payload->Data;
                            std::string childName;
                            if (getName(child, childName) && !WouldCreateHierarchyCycle(childName, n.name))
                                setParent(child, n.name);
                        }
                    };
                    tryReparentToStatic("SCENE_AUDIO3D",
                        [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gAudio3DNodes.size()) return false; childName = gAudio3DNodes[child].name; return true; },
                        [&](int child, const std::string& parent){ gAudio3DNodes[child].parent = parent; });
                    tryReparentToStatic("SCENE_STATICMESH",
                        [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gStaticMeshNodes.size() || child == i) return false; childName = gStaticMeshNodes[child].name; return true; },
                        [&](int child, const std::string& parent){ ReparentStaticMeshKeepWorldPos(child, parent); });
                    tryReparentToStatic("SCENE_CAMERA3D",
                        [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gCamera3DNodes.size()) return false; childName = gCamera3DNodes[child].name; return true; },
                        [&](int child, const std::string& parent){ gCamera3DNodes[child].parent = parent; });
                    tryReparentToStatic("SCENE_NODE3D",
                        [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gNode3DNodes.size()) return false; childName = gNode3DNodes[child].name; return true; },
                        [&](int child, const std::string& parent){ gNode3DNodes[child].parent = parent; });
                    ImGui::EndDragDropTarget();
                }

                bool requestDelete = false;
                if (ImGui::BeginPopupContextItem("NodeContext"))
                {
                    if (ImGui::MenuItem("Rename"))
                    {
                        gNodeRenameIndex = i;
                        gNodeRenameStatic = true;
                        gNodeRenameCamera = false;
                        gNodeRenameNode3D = false;
                        strncpy_s(gNodeRenameBuffer, n.name.c_str(), sizeof(gNodeRenameBuffer) - 1);
                        gNodeRenameOpen = true;
                    }
                    if (!n.parent.empty() && ImGui::MenuItem("Unparent"))
                    {
                        n.parent.clear();
                    }
                    if (ImGui::MenuItem("Unlink Hierarchy"))
                    {
                        n.parent.clear();
                        for (auto& a : gAudio3DNodes) if (a.parent == n.name) a.parent.clear();
                        for (auto& sm : gStaticMeshNodes) if (sm.parent == n.name) sm.parent.clear();
                        for (auto& c : gCamera3DNodes) if (c.parent == n.name) c.parent.clear();
                        for (auto& n3 : gNode3DNodes) if (n3.parent == n.name) n3.parent.clear();
                    }
                    if (ImGui::MenuItem("Delete"))
                    {
                        requestDelete = true;
                    }
                    ImGui::EndPopup();
                }

                if (requestDelete)
                {
                    int idx = i;
                    StaticMesh3DNode deleted = n;
                    gStaticMeshNodes.erase(gStaticMeshNodes.begin() + idx);
                    for (auto& sm : gStaticMeshNodes)
                        if (sm.parent == deleted.name) sm.parent.clear();
                    for (auto& n3 : gNode3DNodes)
                        if (n3.parent == deleted.name) n3.parent.clear();
                    if (gSelectedStaticMesh == idx) gSelectedStaticMesh = -1;
                    else if (gSelectedStaticMesh > idx) gSelectedStaticMesh--;

                    PushUndo({"Delete StaticMesh3D",
                        [idx, deleted]() {
                            int i = idx;
                            if (i < 0) return;
                            if (i > (int)gStaticMeshNodes.size()) i = (int)gStaticMeshNodes.size();
                            gStaticMeshNodes.insert(gStaticMeshNodes.begin() + i, deleted);
                        },
                        [idx]() {
                            if (idx >= 0 && idx < (int)gStaticMeshNodes.size()) gStaticMeshNodes.erase(gStaticMeshNodes.begin() + idx);
                        }
                    });

                    if (depth > 0) ImGui::Unindent(14.0f * depth);
                    ImGui::PopID();
                    return;
                }

                if (staticExpanded)
                {
                    for (int ci = 0; ci < (int)gStaticMeshNodes.size(); ++ci)
                        if (gStaticMeshNodes[ci].parent == n.name) drawStaticNode(ci, depth + 1);
                }

                if (depth > 0) ImGui::Unindent(14.0f * depth);
                ImGui::PopID();
            };

            for (int i = 0; i < (int)gStaticMeshNodes.size(); ++i)
                if (gStaticMeshNodes[i].parent.empty()
                    || (FindStaticMeshByName(gStaticMeshNodes[i].parent) < 0
                        && FindNode3DByName(gStaticMeshNodes[i].parent) < 0))
                    drawStaticNode(i, 0);
        }

        if (!gCamera3DNodes.empty())
        {
            auto findCameraByNameLocal = [&](const std::string& nm)->int { for (int ci=0; ci<(int)gCamera3DNodes.size(); ++ci) if (gCamera3DNodes[ci].name == nm) return ci; return -1; };
            for (int i = 0; i < (int)gCamera3DNodes.size(); ++i)
            {
                auto& n = gCamera3DNodes[i];

                // If this camera is directly parented to a Node3D, it is shown under that Node3D branch.
                if (!n.parent.empty() && FindNode3DByName(n.parent) >= 0)
                    continue;

                // Hide descendants of collapsed Camera3D parents.
                bool skipCameraNode = false;
                {
                    std::string p = n.parent;
                    while (!p.empty())
                    {
                        int pi = findCameraByNameLocal(p);
                        if (pi < 0) break;
                        if (gCollapsedCameraRoots.find(p) != gCollapsedCameraRoots.end()) { skipCameraNode = true; break; }
                        p = gCamera3DNodes[pi].parent;
                    }
                }
                if (skipCameraNode) continue;

                ImGui::PushID(20000 + i);
                {
                    bool hasCameraChild = false;
                    for (int ci = 0; ci < (int)gCamera3DNodes.size(); ++ci) if (gCamera3DNodes[ci].parent == n.name) { hasCameraChild = true; break; }
                    for (int ci = 0; !hasCameraChild && ci < (int)gAudio3DNodes.size(); ++ci) if (gAudio3DNodes[ci].parent == n.name) { hasCameraChild = true; break; }
                    for (int ci = 0; !hasCameraChild && ci < (int)gStaticMeshNodes.size(); ++ci) if (gStaticMeshNodes[ci].parent == n.name) { hasCameraChild = true; break; }
                    for (int ci = 0; !hasCameraChild && ci < (int)gNode3DNodes.size(); ++ci) if (gNode3DNodes[ci].parent == n.name) { hasCameraChild = true; break; }
                    bool cameraExpanded = (gCollapsedCameraRoots.find(n.name) == gCollapsedCameraRoots.end());
                    if (hasCameraChild)
                    {
                        if (ImGui::SmallButton(cameraExpanded ? "v" : ">"))
                        {
                            if (cameraExpanded) gCollapsedCameraRoots.insert(n.name);
                            else gCollapsedCameraRoots.erase(n.name);
                        }
                        ImGui::SameLine();
                    }
                }

                bool selected = (gSelectedCamera3D == i);
                if (ImGui::Selectable(n.name.c_str(), selected))
                {
                    gSelectedCamera3D = i;
                    gSelectedAudio3D = -1;
                    gSelectedStaticMesh = -1;
                    gSelectedNode3D = -1;
                    gTransforming = false;
                    gTransformMode = Transform_None;
                    gAxisLock = 0;
                    gLastTransformMouseX = io.MousePos.x;
                    gLastTransformMouseY = io.MousePos.y;
                }

                if (ImGui::BeginDragDropSource())
                {
                    int payload = i;
                    ImGui::SetDragDropPayload("SCENE_CAMERA3D", &payload, sizeof(int));
                    ImGui::Text("%s", n.name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    auto tryReparentToCamera = [&](const char* payloadType, auto&& getName, auto&& setParent)
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadType))
                        {
                            int child = *(const int*)payload->Data;
                            std::string childName;
                            if (getName(child, childName) && !WouldCreateHierarchyCycle(childName, n.name))
                                setParent(child, n.name);
                        }
                    };
                    tryReparentToCamera("SCENE_AUDIO3D",
                        [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gAudio3DNodes.size()) return false; childName = gAudio3DNodes[child].name; return true; },
                        [&](int child, const std::string& parent){ gAudio3DNodes[child].parent = parent; });
                    tryReparentToCamera("SCENE_STATICMESH",
                        [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gStaticMeshNodes.size()) return false; childName = gStaticMeshNodes[child].name; return true; },
                        [&](int child, const std::string& parent){ ReparentStaticMeshKeepWorldPos(child, parent); });
                    tryReparentToCamera("SCENE_CAMERA3D",
                        [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gCamera3DNodes.size() || child == i) return false; childName = gCamera3DNodes[child].name; return true; },
                        [&](int child, const std::string& parent){ gCamera3DNodes[child].parent = parent; });
                    tryReparentToCamera("SCENE_NODE3D",
                        [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gNode3DNodes.size()) return false; childName = gNode3DNodes[child].name; return true; },
                        [&](int child, const std::string& parent){ gNode3DNodes[child].parent = parent; });
                    ImGui::EndDragDropTarget();
                }

                bool requestDelete = false;
                if (ImGui::BeginPopupContextItem("NodeContext"))
                {
                    if (ImGui::MenuItem("Rename"))
                    {
                        gNodeRenameIndex = i;
                        gNodeRenameStatic = false;
                        gNodeRenameCamera = true;
                        gNodeRenameNode3D = false;
                        strncpy_s(gNodeRenameBuffer, n.name.c_str(), sizeof(gNodeRenameBuffer) - 1);
                        gNodeRenameOpen = true;
                    }
                    if (ImGui::MenuItem("Unlink Hierarchy"))
                    {
                        n.parent.clear();
                        for (auto& a : gAudio3DNodes) if (a.parent == n.name) a.parent.clear();
                        for (auto& s : gStaticMeshNodes) if (s.parent == n.name) s.parent.clear();
                        for (auto& c : gCamera3DNodes) if (c.parent == n.name) c.parent.clear();
                        for (auto& n3 : gNode3DNodes) if (n3.parent == n.name) n3.parent.clear();
                    }
                    if (ImGui::MenuItem("Delete"))
                    {
                        requestDelete = true;
                    }
                    ImGui::EndPopup();
                }

                if (requestDelete)
                {
                    int idx = i;
                    Camera3DNode deleted = n;
                    gCamera3DNodes.erase(gCamera3DNodes.begin() + idx);
                    for (auto& n3 : gNode3DNodes)
                        if (n3.parent == deleted.name) n3.parent.clear();
                    if (deleted.main && !gCamera3DNodes.empty())
                    {
                        bool hasMain = false;
                        for (const auto& c : gCamera3DNodes) { if (c.main) { hasMain = true; break; } }
                        if (!hasMain) gCamera3DNodes[0].main = true;
                    }
                    if (gSelectedCamera3D == idx) gSelectedCamera3D = -1;
                    else if (gSelectedCamera3D > idx) gSelectedCamera3D--;

                    PushUndo({"Delete Camera3D",
                        [idx, deleted]() {
                            int i = idx;
                            if (i < 0) return;
                            if (i > (int)gCamera3DNodes.size()) i = (int)gCamera3DNodes.size();
                            gCamera3DNodes.insert(gCamera3DNodes.begin() + i, deleted);
                        },
                        [idx]() {
                            if (idx >= 0 && idx < (int)gCamera3DNodes.size()) gCamera3DNodes.erase(gCamera3DNodes.begin() + idx);
                        }
                    });

                    ImGui::PopID();
                    continue;
                }

                ImGui::PopID();
            }
        }

        if (!gNode3DNodes.empty())
        {
            std::vector<bool> node3dDrawn((size_t)gNode3DNodes.size(), false);
            std::function<void(int,int)> drawNode3D = [&](int i, int depth)
            {
                if (i < 0 || i >= (int)gNode3DNodes.size()) return;
                if (node3dDrawn[(size_t)i]) return;
                node3dDrawn[(size_t)i] = true;
                auto& n = gNode3DNodes[i];
                ImGui::PushID(40000 + i);
                if (depth > 0) ImGui::Indent(14.0f * depth);

                bool hasNode3DChild = false;
                for (int ci = 0; ci < (int)gNode3DNodes.size(); ++ci)
                    if (gNode3DNodes[ci].parent == n.name) { hasNode3DChild = true; break; }
                for (int ci = 0; !hasNode3DChild && ci < (int)gAudio3DNodes.size(); ++ci) if (gAudio3DNodes[ci].parent == n.name) { hasNode3DChild = true; break; }
                for (int ci = 0; !hasNode3DChild && ci < (int)gStaticMeshNodes.size(); ++ci) if (gStaticMeshNodes[ci].parent == n.name) { hasNode3DChild = true; break; }
                for (int ci = 0; !hasNode3DChild && ci < (int)gCamera3DNodes.size(); ++ci) if (gCamera3DNodes[ci].parent == n.name) { hasNode3DChild = true; break; }
                bool node3DExpanded = (gCollapsedNode3DRoots.find(n.name) == gCollapsedNode3DRoots.end());
                if (hasNode3DChild)
                {
                    if (ImGui::SmallButton(node3DExpanded ? "v" : ">"))
                    {
                        if (node3DExpanded) gCollapsedNode3DRoots.insert(n.name);
                        else gCollapsedNode3DRoots.erase(n.name);
                        node3DExpanded = !node3DExpanded;
                    }
                    ImGui::SameLine();
                }
                bool selected = (gSelectedNode3D == i);
                if (ImGui::Selectable(n.name.c_str(), selected))
                {
                    gSelectedNode3D = i;
                    gSelectedAudio3D = -1;
                    gSelectedStaticMesh = -1;
                    gSelectedCamera3D = -1;
                    gTransforming = false;
                    gTransformMode = Transform_None;
                    gAxisLock = 0;
                    gLastTransformMouseX = io.MousePos.x;
                    gLastTransformMouseY = io.MousePos.y;
                }

                if (ImGui::BeginDragDropSource())
                {
                    int payload = i;
                    ImGui::SetDragDropPayload("SCENE_NODE3D", &payload, sizeof(int));
                    ImGui::Text("%s", n.name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    auto tryReparentToNode3D = [&](const char* payloadType, auto&& getName, auto&& setParent)
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadType))
                        {
                            int child = *(const int*)payload->Data;
                            std::string childName;
                            if (getName(child, childName) && !WouldCreateHierarchyCycle(childName, n.name))
                                setParent(child, n.name);
                        }
                    };
                    tryReparentToNode3D("SCENE_AUDIO3D",
                        [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gAudio3DNodes.size()) return false; childName = gAudio3DNodes[child].name; return true; },
                        [&](int child, const std::string& parent){ gAudio3DNodes[child].parent = parent; });
                    tryReparentToNode3D("SCENE_STATICMESH",
                        [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gStaticMeshNodes.size()) return false; childName = gStaticMeshNodes[child].name; return true; },
                        [&](int child, const std::string& parent){ ReparentStaticMeshKeepWorldPos(child, parent); });
                    tryReparentToNode3D("SCENE_CAMERA3D",
                        [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gCamera3DNodes.size()) return false; childName = gCamera3DNodes[child].name; return true; },
                        [&](int child, const std::string& parent){ gCamera3DNodes[child].parent = parent; });
                    tryReparentToNode3D("SCENE_NODE3D",
                        [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gNode3DNodes.size() || child == i) return false; childName = gNode3DNodes[child].name; return true; },
                        [&](int child, const std::string& parent){ gNode3DNodes[child].parent = parent; });
                    ImGui::EndDragDropTarget();
                }

                bool requestDelete = false;
                if (ImGui::BeginPopupContextItem("NodeContext"))
                {
                    if (ImGui::MenuItem("Rename"))
                    {
                        gNodeRenameIndex = i;
                        gNodeRenameStatic = false;
                        gNodeRenameCamera = false;
                        gNodeRenameNode3D = true;
                        strncpy_s(gNodeRenameBuffer, n.name.c_str(), sizeof(gNodeRenameBuffer) - 1);
                        gNodeRenameOpen = true;
                    }
                    if (!n.parent.empty() && ImGui::MenuItem("Unparent"))
                    {
                        n.parent.clear();
                    }
                    if (ImGui::MenuItem("Unlink Hierarchy"))
                    {
                        n.parent.clear();
                        for (auto& a : gAudio3DNodes) if (a.parent == n.name) a.parent.clear();
                        for (auto& s : gStaticMeshNodes) if (s.parent == n.name) s.parent.clear();
                        for (auto& c : gCamera3DNodes) if (c.parent == n.name) c.parent.clear();
                        for (auto& nn : gNode3DNodes) if (nn.parent == n.name) nn.parent.clear();
                    }
                    if (ImGui::MenuItem("Delete"))
                    {
                        requestDelete = true;
                    }
                    ImGui::EndPopup();
                }

                if (requestDelete)
                {
                    int idx = i;
                    Node3DNode deleted = n;
                    gNode3DNodes.erase(gNode3DNodes.begin() + idx);
                    for (auto& nn : gNode3DNodes)
                        if (nn.parent == deleted.name) nn.parent.clear();
                    if (gSelectedNode3D == idx) gSelectedNode3D = -1;
                    else if (gSelectedNode3D > idx) gSelectedNode3D--;

                    PushUndo({"Delete Node3D",
                        [idx, deleted]() {
                            int i = idx;
                            if (i < 0) return;
                            if (i > (int)gNode3DNodes.size()) i = (int)gNode3DNodes.size();
                            gNode3DNodes.insert(gNode3DNodes.begin() + i, deleted);
                        },
                        [idx]() {
                            if (idx >= 0 && idx < (int)gNode3DNodes.size()) gNode3DNodes.erase(gNode3DNodes.begin() + idx);
                        }
                    });

                    if (depth > 0) ImGui::Unindent(14.0f * depth);
                    ImGui::PopID();
                    return;
                }

                if (node3DExpanded)
                {
                    for (int ci = 0; ci < (int)gNode3DNodes.size(); ++ci)
                        if (gNode3DNodes[ci].parent == n.name) drawNode3D(ci, depth + 1);

                    auto drawChildLeaf = [&](const std::string& label, bool selected, auto&& onSelect)
                    {
                        ImGui::PushID(label.c_str());
                        if (depth + 1 > 0) ImGui::Indent(14.0f * (depth + 1));
                        if (ImGui::Selectable(label.c_str(), selected)) onSelect();
                        if (depth + 1 > 0) ImGui::Unindent(14.0f * (depth + 1));
                        ImGui::PopID();
                    };

                    // Show direct non-Node3D children under Node3D parents so hierarchy linking is visible in-place.
                    for (int ai = 0; ai < (int)gAudio3DNodes.size(); ++ai)
                    {
                        if (gAudio3DNodes[ai].parent != n.name) continue;
                        drawChildLeaf(std::string("[Audio] ") + gAudio3DNodes[ai].name, gSelectedAudio3D == ai, [&]() {
                            gSelectedAudio3D = ai;
                            gSelectedStaticMesh = -1;
                            gSelectedCamera3D = -1;
                            gSelectedNode3D = -1;
                        });
                    }
                    for (int si = 0; si < (int)gStaticMeshNodes.size(); ++si)
                    {
                        if (gStaticMeshNodes[si].parent != n.name) continue;
                        drawChildLeaf(std::string("[StaticMesh3D] ") + gStaticMeshNodes[si].name, gSelectedStaticMesh == si, [&]() {
                            gSelectedAudio3D = -1;
                            gSelectedStaticMesh = si;
                            gSelectedCamera3D = -1;
                            gSelectedNode3D = -1;
                        });
                    }
                    for (int ci = 0; ci < (int)gCamera3DNodes.size(); ++ci)
                    {
                        if (gCamera3DNodes[ci].parent != n.name) continue;
                        drawChildLeaf(std::string("[Camera3D] ") + gCamera3DNodes[ci].name, gSelectedCamera3D == ci, [&]() {
                            gSelectedAudio3D = -1;
                            gSelectedStaticMesh = -1;
                            gSelectedCamera3D = ci;
                            gSelectedNode3D = -1;
                        });
                    }
                }

                if (depth > 0) ImGui::Unindent(14.0f * depth);
                ImGui::PopID();
            };

            for (int i = 0; i < (int)gNode3DNodes.size(); ++i)
                if (gNode3DNodes[i].parent.empty() || FindNode3DByName(gNode3DNodes[i].parent) < 0)
                    drawNode3D(i, 0);
            for (int i = 0; i < (int)gNode3DNodes.size(); ++i)
                if (!node3dDrawn[(size_t)i])
                    drawNode3D(i, 0);
        }

        ImGui::End();

        if (gNodeRenameOpen)
        {
            ImGui::OpenPopup("RenameNode");
        }
        if (ImGui::BeginPopupModal("RenameNode", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("New name", gNodeRenameBuffer, sizeof(gNodeRenameBuffer));
            if (ImGui::Button("OK"))
            {
                if (!gNodeRenameStatic && !gNodeRenameCamera && !gNodeRenameNode3D && gNodeRenameIndex >= 0 && gNodeRenameIndex < (int)gAudio3DNodes.size())
                {
                    int idx = gNodeRenameIndex;
                    std::string oldName = gAudio3DNodes[idx].name;
                    std::string newName = gNodeRenameBuffer;
                    gAudio3DNodes[idx].name = newName;
                    for (auto& a : gAudio3DNodes) if (a.parent == oldName) a.parent = newName;
                    for (auto& s : gStaticMeshNodes) if (s.parent == oldName) s.parent = newName;
                    for (auto& c : gCamera3DNodes) if (c.parent == oldName) c.parent = newName;
                    for (auto& n3 : gNode3DNodes) if (n3.parent == oldName) n3.parent = newName;

                    PushUndo({"Rename Node",
                        [idx, oldName]() {
                            if (idx >= 0 && idx < (int)gAudio3DNodes.size())
                            {
                                std::string cur = gAudio3DNodes[idx].name;
                                gAudio3DNodes[idx].name = oldName;
                                for (auto& a : gAudio3DNodes) if (a.parent == cur) a.parent = oldName;
                                for (auto& s : gStaticMeshNodes) if (s.parent == cur) s.parent = oldName;
                                for (auto& c : gCamera3DNodes) if (c.parent == cur) c.parent = oldName;
                                for (auto& n3 : gNode3DNodes) if (n3.parent == cur) n3.parent = oldName;
                            }
                        },
                        [idx, newName]() {
                            if (idx >= 0 && idx < (int)gAudio3DNodes.size())
                            {
                                std::string cur = gAudio3DNodes[idx].name;
                                gAudio3DNodes[idx].name = newName;
                                for (auto& a : gAudio3DNodes) if (a.parent == cur) a.parent = newName;
                                for (auto& s : gStaticMeshNodes) if (s.parent == cur) s.parent = newName;
                                for (auto& c : gCamera3DNodes) if (c.parent == cur) c.parent = newName;
                                for (auto& n3 : gNode3DNodes) if (n3.parent == cur) n3.parent = newName;
                            }
                        }
                    });
                }
                else if (gNodeRenameStatic && gNodeRenameIndex >= 0 && gNodeRenameIndex < (int)gStaticMeshNodes.size())
                {
                    int idx = gNodeRenameIndex;
                    std::string oldName = gStaticMeshNodes[idx].name;
                    std::string newName = gNodeRenameBuffer;
                    gStaticMeshNodes[idx].name = newName;
                    for (auto& sm : gStaticMeshNodes)
                        if (sm.parent == oldName) sm.parent = newName;
                    for (auto& n3 : gNode3DNodes)
                        if (n3.parent == oldName) n3.parent = newName;

                    PushUndo({"Rename StaticMesh3D",
                        [idx, oldName]() {
                            if (idx >= 0 && idx < (int)gStaticMeshNodes.size())
                            {
                                std::string cur = gStaticMeshNodes[idx].name;
                                gStaticMeshNodes[idx].name = oldName;
                                for (auto& sm : gStaticMeshNodes) if (sm.parent == cur) sm.parent = oldName;
                                for (auto& n3 : gNode3DNodes) if (n3.parent == cur) n3.parent = oldName;
                            }
                        },
                        [idx, newName]() {
                            if (idx >= 0 && idx < (int)gStaticMeshNodes.size())
                            {
                                std::string cur = gStaticMeshNodes[idx].name;
                                gStaticMeshNodes[idx].name = newName;
                                for (auto& sm : gStaticMeshNodes) if (sm.parent == cur) sm.parent = newName;
                                for (auto& n3 : gNode3DNodes) if (n3.parent == cur) n3.parent = newName;
                            }
                        }
                    });
                }
                else if (gNodeRenameCamera && gNodeRenameIndex >= 0 && gNodeRenameIndex < (int)gCamera3DNodes.size())
                {
                    int idx = gNodeRenameIndex;
                    std::string oldName = gCamera3DNodes[idx].name;
                    std::string newName = gNodeRenameBuffer;
                    gCamera3DNodes[idx].name = newName;
                    for (auto& a : gAudio3DNodes) if (a.parent == oldName) a.parent = newName;
                    for (auto& s : gStaticMeshNodes) if (s.parent == oldName) s.parent = newName;
                    for (auto& c : gCamera3DNodes) if (c.parent == oldName) c.parent = newName;
                    for (auto& n3 : gNode3DNodes) if (n3.parent == oldName) n3.parent = newName;

                    PushUndo({"Rename Camera3D",
                        [idx, oldName]() {
                            if (idx >= 0 && idx < (int)gCamera3DNodes.size())
                            {
                                std::string cur = gCamera3DNodes[idx].name;
                                gCamera3DNodes[idx].name = oldName;
                                for (auto& a : gAudio3DNodes) if (a.parent == cur) a.parent = oldName;
                                for (auto& s : gStaticMeshNodes) if (s.parent == cur) s.parent = oldName;
                                for (auto& c : gCamera3DNodes) if (c.parent == cur) c.parent = oldName;
                                for (auto& n3 : gNode3DNodes) if (n3.parent == cur) n3.parent = oldName;
                            }
                        },
                        [idx, newName]() {
                            if (idx >= 0 && idx < (int)gCamera3DNodes.size())
                            {
                                std::string cur = gCamera3DNodes[idx].name;
                                gCamera3DNodes[idx].name = newName;
                                for (auto& a : gAudio3DNodes) if (a.parent == cur) a.parent = newName;
                                for (auto& s : gStaticMeshNodes) if (s.parent == cur) s.parent = newName;
                                for (auto& c : gCamera3DNodes) if (c.parent == cur) c.parent = newName;
                                for (auto& n3 : gNode3DNodes) if (n3.parent == cur) n3.parent = newName;
                            }
                        }
                    });
                }
                else if (gNodeRenameNode3D && gNodeRenameIndex >= 0 && gNodeRenameIndex < (int)gNode3DNodes.size())
                {
                    int idx = gNodeRenameIndex;
                    std::string oldName = gNode3DNodes[idx].name;
                    std::string newName = gNodeRenameBuffer;
                    gNode3DNodes[idx].name = newName;
                    for (auto& a : gAudio3DNodes) if (a.parent == oldName) a.parent = newName;
                    for (auto& s : gStaticMeshNodes) if (s.parent == oldName) s.parent = newName;
                    for (auto& c : gCamera3DNodes) if (c.parent == oldName) c.parent = newName;
                    for (auto& n3 : gNode3DNodes) if (n3.parent == oldName) n3.parent = newName;

                    PushUndo({"Rename Node3D",
                        [idx, oldName]() {
                            if (idx >= 0 && idx < (int)gNode3DNodes.size())
                            {
                                std::string cur = gNode3DNodes[idx].name;
                                gNode3DNodes[idx].name = oldName;
                                for (auto& a : gAudio3DNodes) if (a.parent == cur) a.parent = oldName;
                                for (auto& s : gStaticMeshNodes) if (s.parent == cur) s.parent = oldName;
                                for (auto& c : gCamera3DNodes) if (c.parent == cur) c.parent = oldName;
                                for (auto& n3 : gNode3DNodes) if (n3.parent == cur) n3.parent = oldName;
                            }
                        },
                        [idx, newName]() {
                            if (idx >= 0 && idx < (int)gNode3DNodes.size())
                            {
                                std::string cur = gNode3DNodes[idx].name;
                                gNode3DNodes[idx].name = newName;
                                for (auto& a : gAudio3DNodes) if (a.parent == cur) a.parent = newName;
                                for (auto& s : gStaticMeshNodes) if (s.parent == cur) s.parent = newName;
                                for (auto& c : gCamera3DNodes) if (c.parent == cur) c.parent = newName;
                                for (auto& n3 : gNode3DNodes) if (n3.parent == cur) n3.parent = newName;
                            }
                        }
                    });
                }
                gNodeRenameOpen = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                gNodeRenameOpen = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

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
                    const char* basisModes[] = { "None (Raw)", "Blender (-Z Forward, Y Up)", "Maya (+Z Forward, Y Up)" };
                    ImGui::Combo("Import Basis", &gImportBasisMode, basisModes, IM_ARRAYSIZE(basisModes));
                    ImGui::Text("Export: .vtxa BE, 16.16 fixed, 12 fps (pos only)");
                    ImGui::Text("Mesh import: .nebmesh BE, 8.8 fixed, indexed (UV0 if present)");
                    if (gImportDeltaCompress)
                        ImGui::Text("Delta: int16 8.8 per component (smaller)");
                    if (!gImportWarning.empty())
                        ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1), "%s", gImportWarning.c_str());
                }

                if (ImGui::Button("Import"))
                {
                    int exported = 0;
                    gImportWarning.clear();
                    if (gImportScene && gImportScene->mNumAnimations > 0)
                    {
                        std::filesystem::path inPath(gImportPath);
                        std::string base = inPath.stem().string();
                        std::filesystem::path dir = inPath.parent_path();
                        for (unsigned int i = 0; i < gImportScene->mNumAnimations; ++i)
                        {
                            if (!gImportAnimConvert[i]) continue;
                            const aiAnimation* anim = gImportScene->mAnimations[i];
                            std::string animName = anim->mName.length > 0 ? anim->mName.C_Str() : ("Anim" + std::to_string(i + 1));
                            animName = SanitizeName(animName);
                            std::filesystem::path outPath = dir / (base + "_" + animName + ".vtxa");
                            std::string warn;
                            if (ExportNebAnimation(gImportScene, anim, outPath, warn, gImportDeltaCompress))
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
                    gViewportToast = exported > 0 ? ("Exported " + std::to_string(exported) + " .vtxa") : "No animations exported";
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

        // Right panel: Inspector (selection is pinned until closed with X)
        {
            bool topHasAssetInspector =
                (gMaterialInspectorOpen && !gMaterialInspectorPath.empty()) ||
                (gNebTexInspectorOpen && !gNebTexInspectorPath.empty());
            bool hasNodeSelection =
                (gSelectedAudio3D >= 0 && gSelectedAudio3D < (int)gAudio3DNodes.size()) ||
                (gSelectedStaticMesh >= 0 && gSelectedStaticMesh < (int)gStaticMeshNodes.size()) ||
                (gSelectedCamera3D >= 0 && gSelectedCamera3D < (int)gCamera3DNodes.size()) ||
                (gSelectedNode3D >= 0 && gSelectedNode3D < (int)gNode3DNodes.size());

            // If an asset inspector is occupying top and user selects a node, stack asset inspector to bottom.
            if (topHasAssetInspector && hasNodeSelection)
            {
                if (gMaterialInspectorOpen && !gMaterialInspectorPath.empty())
                {
                    gMaterialInspectorOpen2 = true;
                    gMaterialInspectorPath2 = gMaterialInspectorPath;
                    gNebTexInspectorOpen2 = false;
                    gNebTexInspectorPath2.clear();
                }
                else if (gNebTexInspectorOpen && !gNebTexInspectorPath.empty())
                {
                    gNebTexInspectorOpen2 = true;
                    gNebTexInspectorPath2 = gNebTexInspectorPath;
                    gMaterialInspectorOpen2 = false;
                    gMaterialInspectorPath2.clear();
                }

                gMaterialInspectorOpen = false;
                gMaterialInspectorPath.clear();
                gNebTexInspectorOpen = false;
                gNebTexInspectorPath.clear();
            }
        }

        if (gSelectedAudio3D >= 0 && gSelectedAudio3D < (int)gAudio3DNodes.size())
        {
            gInspectorPinnedAudio3D = gSelectedAudio3D;
            gInspectorPinnedStaticMesh = -1;
            gInspectorPinnedCamera3D = -1;
            gInspectorPinnedNode3D = -1;
        }
        else if (gSelectedStaticMesh >= 0 && gSelectedStaticMesh < (int)gStaticMeshNodes.size())
        {
            gInspectorPinnedStaticMesh = gSelectedStaticMesh;
            gInspectorPinnedAudio3D = -1;
            gInspectorPinnedCamera3D = -1;
            gInspectorPinnedNode3D = -1;
        }
        else if (gSelectedCamera3D >= 0 && gSelectedCamera3D < (int)gCamera3DNodes.size())
        {
            gInspectorPinnedCamera3D = gSelectedCamera3D;
            gInspectorPinnedAudio3D = -1;
            gInspectorPinnedStaticMesh = -1;
            gInspectorPinnedNode3D = -1;
        }
        else if (gSelectedNode3D >= 0 && gSelectedNode3D < (int)gNode3DNodes.size())
        {
            gInspectorPinnedNode3D = gSelectedNode3D;
            gInspectorPinnedAudio3D = -1;
            gInspectorPinnedStaticMesh = -1;
            gInspectorPinnedCamera3D = -1;
        }

        if (gInspectorPinnedAudio3D >= (int)gAudio3DNodes.size()) gInspectorPinnedAudio3D = -1;
        if (gInspectorPinnedStaticMesh >= (int)gStaticMeshNodes.size()) gInspectorPinnedStaticMesh = -1;
        if (gInspectorPinnedCamera3D >= (int)gCamera3DNodes.size()) gInspectorPinnedCamera3D = -1;
        if (gInspectorPinnedNode3D >= (int)gNode3DNodes.size()) gInspectorPinnedNode3D = -1;

        int inspectAudio = (gSelectedAudio3D >= 0 && gSelectedAudio3D < (int)gAudio3DNodes.size()) ? gSelectedAudio3D : gInspectorPinnedAudio3D;
        int inspectStatic = (gSelectedStaticMesh >= 0 && gSelectedStaticMesh < (int)gStaticMeshNodes.size()) ? gSelectedStaticMesh : gInspectorPinnedStaticMesh;
        int inspectCamera = (gSelectedCamera3D >= 0 && gSelectedCamera3D < (int)gCamera3DNodes.size()) ? gSelectedCamera3D : gInspectorPinnedCamera3D;
        int inspectNode3D = (gSelectedNode3D >= 0 && gSelectedNode3D < (int)gNode3DNodes.size()) ? gSelectedNode3D : gInspectorPinnedNode3D;

        if ((inspectAudio >= 0 && inspectAudio < (int)gAudio3DNodes.size()) ||
            (inspectStatic >= 0 && inspectStatic < (int)gStaticMeshNodes.size()) ||
            (inspectCamera >= 0 && inspectCamera < (int)gCamera3DNodes.size()) ||
            (inspectNode3D >= 0 && inspectNode3D < (int)gNode3DNodes.size()) ||
            (gMaterialInspectorOpen && !gMaterialInspectorPath.empty()) ||
            (gNebTexInspectorOpen && !gNebTexInspectorPath.empty()) ||
            (gMaterialInspectorOpen2 && !gMaterialInspectorPath2.empty()) ||
            (gNebTexInspectorOpen2 && !gNebTexInspectorPath2.empty()))
        {
            float inspectorFullH = vp->Size.y - topBarH;
            bool hasBottomInspector =
                (gMaterialInspectorOpen2 && !gMaterialInspectorPath2.empty()) ||
                (gNebTexInspectorOpen2 && !gNebTexInspectorPath2.empty());
            float inspectorHalfH = inspectorFullH * 0.5f;
            float inspectorTopH = hasBottomInspector ? inspectorHalfH : inspectorFullH;

            ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - rightPanelWidth, vp->Pos.y + topBarH));
            ImGui::SetNextWindowSize(ImVec2(rightPanelWidth, inspectorTopH));
            ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

            ImGui::Text("Inspector");
            ImGui::SameLine();
            float closeX = ImGui::GetWindowWidth() - ImGui::GetStyle().FramePadding.x - 16.0f;
            ImGui::SetCursorPosX(closeX);
            if (ImGui::Button("x"))
            {
                gMaterialInspectorOpen = false;
                gMaterialInspectorPath.clear();
                gNebTexInspectorOpen = false;
                gNebTexInspectorPath.clear();
                gMaterialInspectorOpen2 = false;
                gMaterialInspectorPath2.clear();
                gNebTexInspectorOpen2 = false;
                gNebTexInspectorPath2.clear();
                gSelectedAudio3D = -1;
                gSelectedStaticMesh = -1;
                gSelectedCamera3D = -1;
                gSelectedNode3D = -1;
                gSelectedAssetPath.clear();
                gInspectorPinnedAudio3D = -1;
                gInspectorPinnedStaticMesh = -1;
                gInspectorPinnedCamera3D = -1;
                gInspectorPinnedNode3D = -1;
            }
            ImGui::Separator();
            ImGui::BeginChild("##InspectorTopScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);

            if (inspectAudio >= 0 && inspectAudio < (int)gAudio3DNodes.size())
            {
                auto& n = gAudio3DNodes[inspectAudio];
                const int inspectorId = inspectAudio;
                const bool inspectorChanged = (gInspectorSel != inspectorId);
                if (inspectorChanged)
                {
                    gInspectorSel = inspectorId;
                    strncpy_s(gInspectorName, n.name.c_str(), sizeof(gInspectorName) - 1);
                }

                // Live preview values for rotate
                float displayRotX = n.rotX;
                float displayRotY = n.rotY;
                float displayRotZ = n.rotZ;
                if (gTransformMode == Transform_Rotate && gHasRotatePreview && gRotatePreviewIndex == inspectAudio)
                {
                    displayRotX = gRotatePreviewX;
                    displayRotY = gRotatePreviewY;
                    displayRotZ = gRotatePreviewZ;
                }

                ImGui::Text("Audio3D Node");
                if (ImGui::InputText("Name", gInspectorName, sizeof(gInspectorName)))
                {
                    n.name = gInspectorName;
                }
                static char scriptBuf[256] = {};
                if (inspectorChanged)
                {
                    strncpy_s(scriptBuf, n.script.c_str(), sizeof(scriptBuf) - 1);
                }
                if (ImGui::InputText("Script", scriptBuf, sizeof(scriptBuf)))
                {
                    std::string s = scriptBuf;
                    if (!s.empty() && s.rfind("Scripts/", 0) != 0 && s.rfind("Scripts\\", 0) != 0)
                        s = "Scripts/" + s;
                    n.script = s;
                }
                bool scriptOk = false;
                if (!n.script.empty() && !gProjectDir.empty())
                {
                    std::filesystem::path sp = std::filesystem::path(gProjectDir) / n.script;
                    scriptOk = std::filesystem::exists(sp);
                }
                ImGui::SameLine();
                ImGui::Text("Script");
                if (ImGui::Button("Load Script"))
                {
                    if (scriptOk)
                        gViewportToast = n.script + " script validated";
                    else
                        gViewportToast = (n.script.empty() ? "(none)" : n.script) + " script unvalidated";
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                ImGui::DragFloat3("Position", &n.x, 0.1f);
                float rotArr[3] = { displayRotX, displayRotY, displayRotZ };
                ImGui::Text("Rotation"); ImGui::SameLine();
                ImGui::SetNextItemWidth(72); bool rxCh = ImGui::DragFloat("X##AudioRotX", &rotArr[0], 0.5f);
                ImGui::SameLine(); ImGui::SetNextItemWidth(72); bool ryCh = ImGui::DragFloat("Y##AudioRotY", &rotArr[1], 0.5f);
                ImGui::SameLine(); ImGui::SetNextItemWidth(72); bool rzCh = ImGui::DragFloat("Z##AudioRotZ", &rotArr[2], 0.5f);
                if (rxCh || ryCh || rzCh)
                {
                    displayRotX = rotArr[0];
                    displayRotY = rotArr[1];
                    displayRotZ = rotArr[2];
                }
                ImGui::DragFloat3("Scale", &n.scaleX, 0.01f, 0.01f, 100.0f);

                // If user edited rotation in inspector, apply back to node
                if (displayRotX != n.rotX || displayRotY != n.rotY || displayRotZ != n.rotZ)
                {
                    n.rotX = displayRotX;
                    n.rotY = displayRotY;
                    n.rotZ = displayRotZ;
                }
                ImGui::DragFloat("Inner Radius", &n.innerRadius, 0.1f, 0.1f, 1000.0f);
                ImGui::DragFloat("Outer Radius", &n.outerRadius, 0.1f, 0.1f, 2000.0f);
                ImGui::DragFloat("Base Volume", &n.baseVolume, 0.01f, 0.0f, 1.0f);
                ImGui::Text("Pan: %.2f  Volume: %.2f", n.pan, n.volume);
            }
            else if (inspectStatic >= 0 && inspectStatic < (int)gStaticMeshNodes.size())
            {
                auto& n = gStaticMeshNodes[inspectStatic];
                const int inspectorId = 10000 + inspectStatic;
                const bool inspectorChanged = (gInspectorSel != inspectorId);
                if (inspectorChanged)
                {
                    gInspectorSel = inspectorId;
                    strncpy_s(gInspectorName, n.name.c_str(), sizeof(gInspectorName) - 1);
                }

                ImGui::Text("StaticMesh3D Node");
                if (ImGui::InputText("Name", gInspectorName, sizeof(gInspectorName)))
                {
                    n.name = gInspectorName;
                }
                ImGui::Spacing();

                static char scriptBuf[256] = {};
                if (inspectorChanged)
                {
                    strncpy_s(scriptBuf, n.script.c_str(), sizeof(scriptBuf) - 1);
                }
                if (!ImGui::IsAnyItemActive() && n.script != scriptBuf)
                    strncpy_s(scriptBuf, n.script.c_str(), sizeof(scriptBuf) - 1);
                if (ImGui::InputText("##StaticScriptPath", scriptBuf, sizeof(scriptBuf)))
                {
                    std::string s = scriptBuf;
                    if (!s.empty() && s.rfind("Scripts/", 0) != 0 && s.rfind("Scripts\\", 0) != 0)
                        s = "Scripts/" + s;
                    n.script = s;
                }
                bool scriptOk = false;
                if (!n.script.empty() && !gProjectDir.empty())
                {
                    std::filesystem::path sp = std::filesystem::path(gProjectDir) / n.script;
                    scriptOk = std::filesystem::exists(sp);
                }
                ImGui::SameLine();
                if (ImGui::Button("Load Script"))
                {
                    if (scriptOk)
                        gViewportToast = n.script + " script validated";
                    else
                        gViewportToast = (n.script.empty() ? "(none)" : n.script) + " script unvalidated";
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }

                static char matBuf[kStaticMeshMaterialSlots][256] = {};
                static char nebslotBuf[256] = {};
                if (inspectorChanged)
                {
                    std::string slotPath;
                    if (!n.mesh.empty() && !gProjectDir.empty())
                    {
                        std::filesystem::path absMesh = std::filesystem::path(gProjectDir) / n.mesh;
                        slotPath = ToProjectRelativePath(GetNebSlotsPathForMesh(absMesh));
                    }
                    strncpy_s(nebslotBuf, slotPath.c_str(), sizeof(nebslotBuf) - 1);
                }

                ImGui::Spacing();
                ImGui::Text("Load Nebslot");
                if (ImGui::Button(">##NebslotLink"))
                {
                    std::filesystem::path slotPath;

                    if (!gSelectedAssetPath.empty() && gSelectedAssetPath.extension() == ".nebslots")
                    {
                        slotPath = gSelectedAssetPath;
                        if (!gProjectDir.empty())
                        {
                            std::filesystem::path rel = std::filesystem::relative(slotPath, std::filesystem::path(gProjectDir));
                            strncpy_s(nebslotBuf, rel.generic_string().c_str(), sizeof(nebslotBuf) - 1);
                        }
                    }
                    else
                    {
                        slotPath = std::filesystem::path(nebslotBuf);
                        if (!slotPath.empty() && slotPath.is_relative() && !gProjectDir.empty())
                            slotPath = std::filesystem::path(gProjectDir) / slotPath;
                    }

                    std::vector<std::string> slots;
                    if (!slotPath.empty() && LoadNebSlotsManifestFile(slotPath, slots))
                    {
                        for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                        {
                            if (si < (int)slots.size() && !slots[si].empty())
                                n.materialSlots[si] = slots[si];
                        }
                        if (!n.materialSlots[0].empty()) n.material = n.materialSlots[0];
                        for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                            strncpy_s(matBuf[si], n.materialSlots[si].c_str(), sizeof(matBuf[si]) - 1);
                        gViewportToast = "Nebslot linked + loaded";
                    }
                    else
                    {
                        gViewportToast = "Select/highlight a .nebslots in Assets";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                ImGui::SameLine();
                {
                    std::string slotPathLive;
                    if (!n.mesh.empty() && !gProjectDir.empty())
                    {
                        std::filesystem::path absMesh = std::filesystem::path(gProjectDir) / n.mesh;
                        slotPathLive = ToProjectRelativePath(GetNebSlotsPathForMesh(absMesh));
                    }
                    if (!ImGui::IsAnyItemActive() && slotPathLive != nebslotBuf)
                        strncpy_s(nebslotBuf, slotPathLive.c_str(), sizeof(nebslotBuf) - 1);
                }
                if (ImGui::InputText("##NebslotPath", nebslotBuf, sizeof(nebslotBuf)))
                {
                }

                ImGui::Spacing();
                ImGui::Text("Materials");

                if (inspectorChanged)
                {
                    if (n.materialSlots[0].empty()) n.materialSlots[0] = n.material;
                    for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                        strncpy_s(matBuf[si], n.materialSlots[si].c_str(), sizeof(matBuf[si]) - 1);
                }
                // Material Active Slot control moved to per-slot mini selectors.

                if (ImGui::Button(">##MatAssign0"))
                {
                    if (!gSelectedAssetPath.empty() && gSelectedAssetPath.extension() == ".nebmat" && !gProjectDir.empty())
                    {
                        std::filesystem::path rel = std::filesystem::relative(gSelectedAssetPath, std::filesystem::path(gProjectDir));
                        n.materialSlots[0] = rel.generic_string();
                        n.material = n.materialSlots[0];
                        strncpy_s(matBuf[0], n.materialSlots[0].c_str(), sizeof(matBuf[0]) - 1);
                        gViewportToast = "Material slot 1 assigned";
                    }
                    else
                    {
                        gViewportToast = "Select a .nebmat in Assets";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                ImGui::SameLine();
                if (!ImGui::IsAnyItemActive() && n.materialSlots[0] != matBuf[0])
                    strncpy_s(matBuf[0], n.materialSlots[0].c_str(), sizeof(matBuf[0]) - 1);
                if (ImGui::InputText("##MaterialPath0", matBuf[0], sizeof(matBuf[0])))
                {
                    std::string s = matBuf[0];
                    if (!s.empty() && s.rfind("Assets/", 0) != 0 && s.rfind("Assets\\", 0) != 0)
                        s = "Assets/" + s;
                    n.materialSlots[0] = s;
                    n.material = s;
                }
                ImGui::SameLine();
                std::string slot1Label = GetStaticMeshSlotLabel(n, 0);
                ImGui::Text("%s", slot1Label.c_str());

                for (int si = 1; si < kStaticMeshMaterialSlots; ++si)
                {
                    std::string btnId = ">##MatAssign" + std::to_string(si);
                    if (ImGui::Button(btnId.c_str()))
                    {
                        if (!gSelectedAssetPath.empty() && gSelectedAssetPath.extension() == ".nebmat" && !gProjectDir.empty())
                        {
                            std::filesystem::path rel = std::filesystem::relative(gSelectedAssetPath, std::filesystem::path(gProjectDir));
                            n.materialSlots[si] = rel.generic_string();
                            strncpy_s(matBuf[si], n.materialSlots[si].c_str(), sizeof(matBuf[si]) - 1);
                            gViewportToast = "Material slot " + std::to_string(si + 1) + " assigned";
                        }
                        else
                        {
                            gViewportToast = "Select a .nebmat in Assets";
                        }
                        gViewportToastUntil = glfwGetTime() + 2.0;
                    }
                    ImGui::SameLine();

                    std::string id = "##MaterialPath" + std::to_string(si);
                    std::string label = GetStaticMeshSlotLabel(n, si);
                    if (!ImGui::IsAnyItemActive() && n.materialSlots[si] != matBuf[si])
                        strncpy_s(matBuf[si], n.materialSlots[si].c_str(), sizeof(matBuf[si]) - 1);
                    if (ImGui::InputText(id.c_str(), matBuf[si], sizeof(matBuf[si])))
                    {
                        std::string s = matBuf[si];
                        if (!s.empty() && s.rfind("Assets/", 0) != 0 && s.rfind("Assets\\", 0) != 0)
                            s = "Assets/" + s;
                        n.materialSlots[si] = s;
                    }
                    ImGui::SameLine();
                    ImGui::Text("%s", label.c_str());
                }

                ImGui::Spacing();
                ImGui::Text("Mesh");

                static char meshBuf[256] = {};
                if (inspectorChanged)
                {
                    strncpy_s(meshBuf, n.mesh.c_str(), sizeof(meshBuf) - 1);
                }
                if (ImGui::Button(">##MeshAssign"))
                {
                    if (!gSelectedAssetPath.empty() && gSelectedAssetPath.extension() == ".nebmesh" && !gProjectDir.empty())
                    {
                        std::filesystem::path rel = std::filesystem::relative(gSelectedAssetPath, std::filesystem::path(gProjectDir));
                        n.mesh = rel.generic_string();
                        AutoAssignMaterialSlotsFromMesh(n);
                        strncpy_s(meshBuf, n.mesh.c_str(), sizeof(meshBuf) - 1);
                        for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                            strncpy_s(matBuf[si], n.materialSlots[si].c_str(), sizeof(matBuf[si]) - 1);
                        gViewportToast = "Static mesh assigned + slots auto-mapped";
                    }
                    else
                    {
                        gViewportToast = "Select a .nebmesh in Assets";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                ImGui::SameLine();
                if (!ImGui::IsAnyItemActive() && n.mesh != meshBuf)
                    strncpy_s(meshBuf, n.mesh.c_str(), sizeof(meshBuf) - 1);
                if (ImGui::InputText("##MeshPath", meshBuf, sizeof(meshBuf)))
                {
                    std::string s = meshBuf;
                    if (!s.empty() && s.rfind("Assets/", 0) != 0 && s.rfind("Assets\\", 0) != 0)
                        s = "Assets/" + s;
                    n.mesh = s;
                    AutoAssignMaterialSlotsFromMesh(n);
                    for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                        strncpy_s(matBuf[si], n.materialSlots[si].c_str(), sizeof(matBuf[si]) - 1);
                }
                std::string meshDisplayName = "(none)";
                bool meshOk = false;
                if (!n.mesh.empty())
                {
                    std::filesystem::path np = std::filesystem::path(n.mesh);
                    std::string stem = np.stem().string();
                    meshDisplayName = stem.empty() ? np.filename().string() : stem;
                    if (!gProjectDir.empty())
                    {
                        std::filesystem::path mp = std::filesystem::path(gProjectDir) / n.mesh;
                        meshOk = std::filesystem::exists(mp);
                    }
                }
                ImGui::SameLine();
                ImGui::Text("%s", meshDisplayName.c_str());

                ImGui::Text("Parent: %s", n.parent.empty() ? "(none)" : n.parent.c_str());
                ImGui::SameLine();
                if (!n.parent.empty() && ImGui::Button("Unparent##InspectorStatic")) n.parent.clear();
                ImGui::SameLine();
                if (ImGui::Button("Reset Xform (keep world)##InspectorStatic"))
                    ResetStaticMeshTransformsKeepWorld(inspectStatic);
                ImGui::Checkbox("Collision Source (Saturn floor)", &n.collisionSource);

                ImGui::DragFloat3("Position", &n.x, 0.1f);
                float rotArr[3] = { n.rotY, n.rotZ, n.rotX };
                bool rotCh = ImGui::DragFloat3("##StaticRotPacked", rotArr, 0.5f);
                ImGui::SameLine(); ImGui::Text("Rotation");
                if (rotCh)
                {
                    n.rotY = rotArr[0];
                    n.rotZ = rotArr[1];
                    n.rotX = rotArr[2];
                }
                ImGui::DragFloat3("Scale", &n.scaleX, 0.01f, 0.01f, 100.0f);
                /* Saturn sampling preview is now always on. */
            }
            else if (inspectCamera >= 0 && inspectCamera < (int)gCamera3DNodes.size())
            {
                auto& c = gCamera3DNodes[inspectCamera];
                const int inspectorId = 30000 + inspectCamera;
                const bool inspectorChanged = (gInspectorSel != inspectorId);
                if (inspectorChanged)
                {
                    gInspectorSel = inspectorId;
                    strncpy_s(gInspectorName, c.name.c_str(), sizeof(gInspectorName) - 1);
                }

                ImGui::Text("Camera3D Node");
                if (ImGui::InputText("Name", gInspectorName, sizeof(gInspectorName)))
                {
                    c.name = gInspectorName;
                }

                ImGui::DragFloat3("Position", &c.x, 0.1f);
                static char camParentBuf[256] = {};
                if (inspectorChanged)
                    strncpy_s(camParentBuf, c.parent.c_str(), sizeof(camParentBuf) - 1);
                if (!ImGui::IsAnyItemActive() && c.parent != camParentBuf)
                    strncpy_s(camParentBuf, c.parent.c_str(), sizeof(camParentBuf) - 1);
                if (ImGui::InputText("Parent##CamParent", camParentBuf, sizeof(camParentBuf)))
                    c.parent = camParentBuf;
                ImGui::SameLine();
                if (!c.parent.empty() && ImGui::Button("Unparent##InspectorCam")) c.parent.clear();
                ImGui::SameLine();
                if (ImGui::Button("Reset Xform (keep world pos)##InspectorCam"))
                {
                    float cwx, cwy, cwz, cwrx, cwry, cwrz;
                    GetCamera3DWorldTR(inspectCamera, cwx, cwy, cwz, cwrx, cwry, cwrz);
                    c.parent.clear();
                    c.x = cwx;
                    c.y = cwy;
                    c.z = cwz;
                    c.rotX = 0.0f;
                    c.rotY = 0.0f;
                    c.rotZ = 0.0f;
                    c.orbitX = 0.0f;
                    c.orbitY = 0.0f;
                    c.orbitZ = 0.0f;
                }

                ImGui::Text("Rotation"); ImGui::SameLine();
                ImGui::SetNextItemWidth(72); ImGui::DragFloat("X##CamRotX", &c.rotX, 0.5f);
                ImGui::SameLine(); ImGui::SetNextItemWidth(72); ImGui::DragFloat("Y##CamRotY", &c.rotY, 0.5f);
                ImGui::SameLine(); ImGui::SetNextItemWidth(72); ImGui::DragFloat("Z##CamRotZ", &c.rotZ, 0.5f);
                ImGui::BeginDisabled(c.parent.empty());
                ImGui::DragFloat3("Orbit", &c.orbitX, 0.05f);
                ImGui::EndDisabled();
                if (c.parent.empty()) ImGui::TextDisabled("Orbit enabled when Parent is set (parent acts as pivot)");
                ImGui::Checkbox("Perspective", &c.perspective);

                if (c.perspective)
                {
                    ImGui::DragFloat("FOV", &c.fovY, 0.1f, 5.0f, 170.0f);
                }
                else
                {
                    ImGui::DragFloat("Ortho Width", &c.orthoWidth, 0.05f, 0.01f, 5000.0f);
                    float aspectNow = (display_h > 0) ? ((float)display_w / (float)display_h) : (16.0f / 9.0f);
                    float orthoH = c.orthoWidth / aspectNow;
                    ImGui::Text("Ortho Height: %.3f", orthoH);
                }

                ImGui::DragFloat("Near", &c.nearZ, 0.01f, 0.001f, 1000.0f);
                ImGui::DragFloat("Far", &c.farZ, 1.0f, 1.0f, 50000.0f);
                ImGui::DragFloat("Priority", &c.priority, 0.1f);

                bool oldMain = c.main;
                ImGui::Checkbox("Main Camera", &c.main);
                if (!oldMain && c.main)
                {
                    for (int i = 0; i < (int)gCamera3DNodes.size(); ++i)
                    {
                        if (i != gSelectedCamera3D)
                            gCamera3DNodes[i].main = false;
                    }
                }

                if (ImGui::Button("Set View To Camera"))
                {
                    float cwx, cwy, cwz, cwrx, cwry, cwrz;
                    GetCamera3DWorldTR(inspectCamera, cwx, cwy, cwz, cwrx, cwry, cwrz);
                    Vec3 right{}, upAxis{}, forward{};
                    GetLocalAxesFromEuler(cwrx, cwry, cwrz, right, upAxis, forward);
                    orbitCenter = { cwx + forward.x * distance, cwy + forward.y * distance, cwz + forward.z * distance };

                    // Convert engine Euler-forward convention to viewport yaw/pitch convention.
                    viewYaw = atan2f(forward.z, forward.x) * 180.0f / 3.14159f;
                    float fy = std::clamp(forward.y, -1.0f, 1.0f);
                    viewPitch = asinf(fy) * 180.0f / 3.14159f;
                }
                ImGui::SameLine();
                if (ImGui::Button("Set Camera To View"))
                {
                    c.x = eye.x;
                    c.y = eye.y;
                    c.z = eye.z;

                    // Build forward from current viewport camera.
                    float yawRad = viewYaw * 3.14159f / 180.0f;
                    float pitchRad = viewPitch * 3.14159f / 180.0f;
                    Vec3 fwd = {
                        cosf(pitchRad) * cosf(yawRad),
                        sinf(pitchRad),
                        cosf(pitchRad) * sinf(yawRad)
                    };

                    // Map viewport forward into engine Euler convention (Rz*Ry*Rx), keeping roll neutral.
                    c.rotZ = 0.0f;
                    c.rotX = asinf(std::clamp(-fwd.y, -1.0f, 1.0f)) * 180.0f / 3.14159f;
                    c.rotY = atan2f(fwd.x, fwd.z) * 180.0f / 3.14159f;
                }
            }
            else if (inspectNode3D >= 0 && inspectNode3D < (int)gNode3DNodes.size())
            {
                auto& n = gNode3DNodes[inspectNode3D];
                const int inspectorId = 40000 + inspectNode3D;
                const bool inspectorChanged = (gInspectorSel != inspectorId);
                if (inspectorChanged)
                {
                    gInspectorSel = inspectorId;
                    strncpy_s(gInspectorName, n.name.c_str(), sizeof(gInspectorName) - 1);
                }

                ImGui::Text("Node3D Node");
                if (ImGui::InputText("Name", gInspectorName, sizeof(gInspectorName)))
                {
                    n.name = gInspectorName;
                }

                static char scriptBuf[256] = {};
                if (inspectorChanged)
                {
                    strncpy_s(scriptBuf, n.script.c_str(), sizeof(scriptBuf) - 1);
                }
                if (!ImGui::IsAnyItemActive() && n.script != scriptBuf)
                    strncpy_s(scriptBuf, n.script.c_str(), sizeof(scriptBuf) - 1);
                if (ImGui::Button(">##Node3DScriptAssign"))
                {
                    if (!gSelectedAssetPath.empty() && gSelectedAssetPath.extension() == ".c" && !gProjectDir.empty())
                    {
                        std::filesystem::path rel = std::filesystem::relative(gSelectedAssetPath, std::filesystem::path(gProjectDir));
                        std::string s = rel.generic_string();
                        if (!s.empty() && s.rfind("Scripts/", 0) != 0 && s.rfind("Scripts\\", 0) != 0)
                            s = "Scripts/" + s;
                        n.script = s;
                        strncpy_s(scriptBuf, n.script.c_str(), sizeof(scriptBuf) - 1);
                        gViewportToast = "Node3D script assigned";
                    }
                    else
                    {
                        gViewportToast = "Select a .c script in Assets";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                ImGui::SameLine();
                if (ImGui::InputText("##Node3DScriptPath", scriptBuf, sizeof(scriptBuf)))
                {
                    std::string s = scriptBuf;
                    if (!s.empty() && s.rfind("Scripts/", 0) != 0 && s.rfind("Scripts\\", 0) != 0)
                        s = "Scripts/" + s;
                    n.script = s;
                }
                bool scriptOk = false;
                if (!n.script.empty() && !gProjectDir.empty())
                {
                    std::filesystem::path sp = std::filesystem::path(gProjectDir) / n.script;
                    scriptOk = std::filesystem::exists(sp);
                }
                ImGui::SameLine();
                if (ImGui::Button("Load Script##Node3D"))
                {
                    if (scriptOk)
                        gViewportToast = n.script + " script validated";
                    else
                        gViewportToast = (n.script.empty() ? "(none)" : n.script) + " script unvalidated";
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }

                ImGui::Text("Primitive: %s", n.primitiveMesh.empty() ? "(none)" : n.primitiveMesh.c_str());
                ImGui::Checkbox("Collision Source (primitive bounds)", &n.collisionSource);
                ImGui::Checkbox("Physics Enabled", &n.physicsEnabled);
                ImGui::Text("Parent: %s", n.parent.empty() ? "(none)" : n.parent.c_str());
                ImGui::SameLine();
                if (!n.parent.empty() && ImGui::Button("Unparent##InspectorNode3D")) n.parent.clear();
                ImGui::DragFloat3("Position", &n.x, 0.1f);
                // Match StaticMesh3D axis mapping exactly to keep parent/child behavior consistent.
                float rotArrNode[3] = { n.rotY, n.rotZ, n.rotX };
                bool rotNodeCh = ImGui::DragFloat3("##Node3DRotPacked", rotArrNode, 0.5f);
                ImGui::SameLine(); ImGui::Text("Rotation");
                if (rotNodeCh)
                {
                    n.rotY = rotArrNode[0];
                    n.rotZ = rotArrNode[1];
                    n.rotX = rotArrNode[2];
                }
                ImGui::DragFloat3("Scale", &n.scaleX, 0.01f, 0.01f, 100.0f);
                ImGui::Separator();
                ImGui::TextUnformatted("Collision Bounds (local)");
                ImGui::DragFloat3("XYZ Extents", &n.extentX, 0.01f, 0.0f, 1000.0f);
                ImGui::DragFloat3("Bounds Position", &n.boundPosX, 0.01f);
            }
            else if (gMaterialInspectorOpen && !gMaterialInspectorPath.empty())
            {
                ImGui::Text("Material");
                std::string texPath;
                LoadMaterialTexture(gMaterialInspectorPath, texPath);
                static char texBuf[256] = {};
                strncpy_s(texBuf, texPath.c_str(), sizeof(texBuf) - 1);
                if (ImGui::Button(">##TexAssignInspector"))
                {
                    if (!gSelectedAssetPath.empty() && gSelectedAssetPath.extension() == ".nebtex" && !gProjectDir.empty())
                    {
                        std::filesystem::path rel = std::filesystem::relative(gSelectedAssetPath, std::filesystem::path(gProjectDir));
                        texPath = rel.generic_string();
                        strncpy_s(texBuf, texPath.c_str(), sizeof(texBuf) - 1);
                        SaveMaterialTexture(gMaterialInspectorPath, texPath);
                        gViewportToast = "Texture assigned";
                    }
                    else
                    {
                        gViewportToast = "Select a .nebtex texture in Assets";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                ImGui::SameLine();
                if (ImGui::InputText("Texture Assignment", texBuf, sizeof(texBuf)))
                {
                    std::string s = texBuf;
                    if (!s.empty() && s.rfind("Assets/", 0) != 0 && s.rfind("Assets\\", 0) != 0)
                        s = "Assets/" + s;
                    SaveMaterialTexture(gMaterialInspectorPath, s);
                }
                float uvScale = 0.0f;
                LoadMaterialUvScale(gMaterialInspectorPath, uvScale);
                int uvScaleStep = 0;
                if (uvScale <= -2.5f) uvScaleStep = -3;
                else if (uvScale <= -1.5f) uvScaleStep = -2;
                else if (uvScale <= -0.5f) uvScaleStep = -1;
                else uvScaleStep = 0;
                int uvIdx = (uvScaleStep == 0) ? 0 : (uvScaleStep == -1 ? 1 : (uvScaleStep == -2 ? 2 : 3));
                const char* uvScaleOptions[] = { "0", "-1", "-2", "-3" };
                if (ImGui::Combo("UV Scale", &uvIdx, uvScaleOptions, IM_ARRAYSIZE(uvScaleOptions)))
                {
                    uvScaleStep = (uvIdx == 0) ? 0 : (uvIdx == 1 ? -1 : (uvIdx == 2 ? -2 : -3));
                    SaveMaterialUvScale(gMaterialInspectorPath, (float)uvScaleStep);
                }
                bool allowUvRepeat = false;
                LoadMaterialAllowUvRepeat(gMaterialInspectorPath, allowUvRepeat);
                if (ImGui::Checkbox("Extended UV (Saturn)", &allowUvRepeat))
                {
                    SaveMaterialAllowUvRepeat(gMaterialInspectorPath, allowUvRepeat);
                }
                bool texOk = false;
                if (!texPath.empty())
                {
                    std::filesystem::path tp = std::filesystem::path(gProjectDir) / texPath;
                    texOk = std::filesystem::exists(tp);
                }
                ImGui::SameLine();
                ImGui::Text(texOk ? "OK" : "Missing");
            }
            else if (gNebTexInspectorOpen && !gNebTexInspectorPath.empty())
            {
                ImGui::Text("Texture");
                ImGui::TextWrapped("%s", gNebTexInspectorPath.filename().string().c_str());
                int texW = 0, texH = 0;
                if (ReadNebTexDimensions(gNebTexInspectorPath, texW, texH))
                    ImGui::Text("Dimensions: %d x %d", texW, texH);

                GLuint previewTex = GetNebTexture(gNebTexInspectorPath);
                if (previewTex != 0)
                {
                    ImGui::Text("Sample");
                    ImGui::Image((ImTextureID)(intptr_t)previewTex, ImVec2(128, 128));
                }

                int wrapMode = LoadNebTexWrapMode(gNebTexInspectorPath);
                int saturnNpot = LoadNebTexSaturnNpotMode(gNebTexInspectorPath); // 0=pad, 1=resample
                int filterMode = LoadNebTexFilterMode(gNebTexInspectorPath); // 0=nearest, 1=bilinear
                const char* wrapOptions[] = { "Repeat", "Extend", "Clip", "Mirror" };
                const char* saturnNpotOptions[] = { "Pad", "Resample" };
                const char* filterOptions[] = { "Nearest", "Bilinear" };
                bool flipU = false, flipV = false;
                LoadNebTexFlipOptions(gNebTexInspectorPath, flipU, flipV);
                bool changed = false;
                changed |= ImGui::Combo("Extension", &wrapMode, wrapOptions, IM_ARRAYSIZE(wrapOptions));
                changed |= ImGui::Combo("Filter", &filterMode, filterOptions, IM_ARRAYSIZE(filterOptions));
                changed |= ImGui::Combo("NPOT", &saturnNpot, saturnNpotOptions, IM_ARRAYSIZE(saturnNpotOptions));
                changed |= ImGui::Checkbox("Flip U", &flipU);
                changed |= ImGui::Checkbox("Flip V", &flipV);
                if (changed)
                {
                    SaveNebTexWrapMode(gNebTexInspectorPath, wrapMode);
                    SaveNebTexFilterMode(gNebTexInspectorPath, filterMode);
                    SaveNebTexSaturnNpotMode(gNebTexInspectorPath, saturnNpot);
                    SaveNebTexFlipOptions(gNebTexInspectorPath, flipU, flipV);
                    // Force immediate visual update: flush entire neb texture cache.
                    for (auto& kv : gNebTextureCache)
                    {
                        if (kv.second != 0) glDeleteTextures(1, &kv.second);
                    }
                    gNebTextureCache.clear();
                    gPreviewSaturnSampling = true; // make NPOT preview visible live
                    gViewportToast = "Texture compatibility updated (live)";
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
            }

            ImGui::EndChild();
            ImGui::End();

            if (hasBottomInspector)
            {
                // Bottom-half inspector pane (secondary asset inspector).
                ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - rightPanelWidth, vp->Pos.y + topBarH + inspectorHalfH));
                ImGui::SetNextWindowSize(ImVec2(rightPanelWidth, inspectorHalfH));
                ImGui::Begin("InspectorBottom", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

                ImGui::Text("Inspector");
                ImGui::SameLine();
                float closeX2 = ImGui::GetWindowWidth() - ImGui::GetStyle().FramePadding.x - 16.0f;
                ImGui::SetCursorPosX(closeX2);
                if (ImGui::Button("x##BottomInspector"))
                {
                    gMaterialInspectorOpen2 = false;
                    gMaterialInspectorPath2.clear();
                    gNebTexInspectorOpen2 = false;
                    gNebTexInspectorPath2.clear();
                }
                ImGui::Separator();
                ImGui::BeginChild("##InspectorBottomScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);

                if (gMaterialInspectorOpen2 && !gMaterialInspectorPath2.empty())
            {
                ImGui::Text("Material");
                std::string texPath;
                LoadMaterialTexture(gMaterialInspectorPath2, texPath);
                static char texBuf2[256] = {};
                strncpy_s(texBuf2, texPath.c_str(), sizeof(texBuf2) - 1);
                if (ImGui::Button(">##TexAssignInspectorB"))
                {
                    if (!gSelectedAssetPath.empty() && gSelectedAssetPath.extension() == ".nebtex" && !gProjectDir.empty())
                    {
                        std::filesystem::path rel = std::filesystem::relative(gSelectedAssetPath, std::filesystem::path(gProjectDir));
                        texPath = rel.generic_string();
                        strncpy_s(texBuf2, texPath.c_str(), sizeof(texBuf2) - 1);
                        SaveMaterialTexture(gMaterialInspectorPath2, texPath);
                        gViewportToast = "Texture assigned";
                    }
                    else
                    {
                        gViewportToast = "Select a .nebtex texture in Assets";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                ImGui::SameLine();
                if (ImGui::InputText("Texture Assignment##B", texBuf2, sizeof(texBuf2)))
                {
                    std::string s = texBuf2;
                    if (!s.empty() && s.rfind("Assets/", 0) != 0 && s.rfind("Assets\\", 0) != 0)
                        s = "Assets/" + s;
                    SaveMaterialTexture(gMaterialInspectorPath2, s);
                }
                float uvScale = 0.0f;
                LoadMaterialUvScale(gMaterialInspectorPath2, uvScale);
                int uvScaleStep = 0;
                if (uvScale <= -2.5f) uvScaleStep = -3;
                else if (uvScale <= -1.5f) uvScaleStep = -2;
                else if (uvScale <= -0.5f) uvScaleStep = -1;
                else uvScaleStep = 0;
                int uvIdx = (uvScaleStep == 0) ? 0 : (uvScaleStep == -1 ? 1 : (uvScaleStep == -2 ? 2 : 3));
                const char* uvScaleOptions[] = { "0", "-1", "-2", "-3" };
                if (ImGui::Combo("UV Scale##B", &uvIdx, uvScaleOptions, IM_ARRAYSIZE(uvScaleOptions)))
                {
                    uvScaleStep = (uvIdx == 0) ? 0 : (uvIdx == 1 ? -1 : (uvIdx == 2 ? -2 : -3));
                    SaveMaterialUvScale(gMaterialInspectorPath2, (float)uvScaleStep);
                }
                bool allowUvRepeat = false;
                LoadMaterialAllowUvRepeat(gMaterialInspectorPath2, allowUvRepeat);
                if (ImGui::Checkbox("Extended UV (Saturn)##B", &allowUvRepeat))
                {
                    SaveMaterialAllowUvRepeat(gMaterialInspectorPath2, allowUvRepeat);
                }
                bool texOk = false;
                if (!texPath.empty())
                {
                    std::filesystem::path tp = std::filesystem::path(gProjectDir) / texPath;
                    texOk = std::filesystem::exists(tp);
                }
                ImGui::SameLine();
                ImGui::Text(texOk ? "OK" : "Missing");
            }
            else if (gNebTexInspectorOpen2 && !gNebTexInspectorPath2.empty())
            {
                ImGui::Text("Texture");
                ImGui::TextWrapped("%s", gNebTexInspectorPath2.filename().string().c_str());
                int texW = 0, texH = 0;
                if (ReadNebTexDimensions(gNebTexInspectorPath2, texW, texH))
                    ImGui::Text("Dimensions: %d x %d", texW, texH);

                GLuint previewTex = GetNebTexture(gNebTexInspectorPath2);
                if (previewTex != 0)
                {
                    ImGui::Text("Sample");
                    ImGui::Image((ImTextureID)(intptr_t)previewTex, ImVec2(128, 128));
                }

                int wrapMode = LoadNebTexWrapMode(gNebTexInspectorPath2);
                int saturnNpot = LoadNebTexSaturnNpotMode(gNebTexInspectorPath2);
                int filterMode = LoadNebTexFilterMode(gNebTexInspectorPath2);
                const char* wrapOptions[] = { "Repeat", "Extend", "Clip", "Mirror" };
                const char* saturnNpotOptions[] = { "Pad", "Resample" };
                const char* filterOptions[] = { "Nearest", "Bilinear" };
                bool flipU = false, flipV = false;
                LoadNebTexFlipOptions(gNebTexInspectorPath2, flipU, flipV);
                bool changed = false;
                changed |= ImGui::Combo("Extension##B", &wrapMode, wrapOptions, IM_ARRAYSIZE(wrapOptions));
                changed |= ImGui::Combo("Filter##B", &filterMode, filterOptions, IM_ARRAYSIZE(filterOptions));
                changed |= ImGui::Combo("NPOT##B", &saturnNpot, saturnNpotOptions, IM_ARRAYSIZE(saturnNpotOptions));
                changed |= ImGui::Checkbox("Flip U##B", &flipU);
                changed |= ImGui::Checkbox("Flip V##B", &flipV);
                if (changed)
                {
                    SaveNebTexWrapMode(gNebTexInspectorPath2, wrapMode);
                    SaveNebTexFilterMode(gNebTexInspectorPath2, filterMode);
                    SaveNebTexSaturnNpotMode(gNebTexInspectorPath2, saturnNpot);
                    SaveNebTexFlipOptions(gNebTexInspectorPath2, flipU, flipV);
                    for (auto& kv : gNebTextureCache)
                    {
                        if (kv.second != 0) glDeleteTextures(1, &kv.second);
                    }
                    gNebTextureCache.clear();
                    gPreviewSaturnSampling = true; // make NPOT preview visible live
                    gViewportToast = "Texture compatibility updated (live)";
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
            }

                ImGui::EndChild();
                ImGui::End();
            }
        }

        // Scene tabs (top of viewport)
        if (!gOpenScenes.empty())
        {
            ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + leftPanelWidth, vp->Pos.y + topBarH));
            ImGui::SetNextWindowSize(ImVec2(vp->Size.x - leftPanelWidth - rightPanelWidth, 26.0f * ImGui::GetIO().FontGlobalScale));
            ImGui::Begin("##SceneTabs", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);
            ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.086f, 0.082f, 0.086f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
            if (ImGui::BeginTabBar("Scenes"))
            {
                for (int i = 0; i < (int)gOpenScenes.size(); ++i)
                {
                    bool open = true;
                    const char* label = gOpenScenes[i].name.c_str();
                    ImGuiTabItemFlags tabFlags = (gForceSelectSceneTab == i) ? ImGuiTabItemFlags_SetSelected : 0;
                    if (ImGui::BeginTabItem(label, &open, tabFlags))
                    {
                        if (gForceSelectSceneTab == -1)
                        {
                            if (gActiveScene != i) SetActiveScene(i);
                        }
                        else if (gForceSelectSceneTab == i)
                        {
                            if (gActiveScene != i) SetActiveScene(i);
                            gForceSelectSceneTab = -1;
                        }

                        if (ImGui::BeginPopupContextItem("SceneTabContext"))
                        {
                            if (ImGui::MenuItem("Set as default scene"))
                            {
                                if (!gProjectDir.empty() && i >= 0 && i < (int)gOpenScenes.size())
                                {
                                    if (SetProjectDefaultScene(std::filesystem::path(gProjectDir), gOpenScenes[i].path))
                                    {
                                        gViewportToast = "Default scene set: " + gOpenScenes[i].name;
                                    }
                                    else
                                    {
                                        gViewportToast = "Failed to set default scene";
                                    }
                                    gViewportToastUntil = glfwGetTime() + 2.0;
                                }
                            }
                            ImGui::EndPopup();
                        }

                        ImGui::EndTabItem();
                    }
                    if (!open)
                    {
                        // close tab
                        if (gActiveScene == i) gActiveScene = -1;
                        gOpenScenes.erase(gOpenScenes.begin() + i);
                        if (gActiveScene >= (int)gOpenScenes.size()) gActiveScene = (int)gOpenScenes.size() - 1;

                        if (gActiveScene >= 0)
                        {
                            gAudio3DNodes = gOpenScenes[gActiveScene].nodes;
                            gStaticMeshNodes = gOpenScenes[gActiveScene].staticMeshes;
                            gCamera3DNodes = gOpenScenes[gActiveScene].cameras;
                            gNode3DNodes = gOpenScenes[gActiveScene].node3d;
                        }
                        else
                        {
                            gAudio3DNodes.clear();
                            gStaticMeshNodes.clear();
                            gCamera3DNodes.clear();
                            gNode3DNodes.clear();
                            gSelectedAudio3D = -1;
                            gSelectedStaticMesh = -1;
                            gSelectedCamera3D = -1;
                            gSelectedNode3D = -1;
                            gInspectorPinnedAudio3D = -1;
                            gInspectorPinnedStaticMesh = -1;
                            gInspectorPinnedCamera3D = -1;
                            gInspectorPinnedNode3D = -1;
                        }
                        break;
                    }
                }
                ImGui::EndTabBar();
            }
            ImGui::PopStyleColor(3);
            ImGui::End();
        }

        if (gShowVmuTool)
        {
            const float topBarHLocal = 28.0f * ImGui::GetIO().FontGlobalScale;
            ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + topBarHLocal));
            ImGui::SetNextWindowSize(ImVec2(vp->Size.x, vp->Size.y - topBarHLocal));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.09f, 0.10f, 0.14f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.75f, 0.75f, 0.75f, 1));
            ImGui::Begin("##VmuToolSlate", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

            // Top border strip with left-side Save button.
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.94f, 0.94f, 0.94f, 1.0f));
            ImGui::BeginChild("##VmuToolTopStrip", ImVec2(0.0f, 34.0f * ImGui::GetIO().FontGlobalScale), true,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            if (ImGui::Button("Save"))
            {
                if (!gVmuHasImage)
                {
                    gViewportToast = "VMU Tool: no image to save";
                }
                else
                {
                    std::filesystem::path dstDir = std::filesystem::path(gProjectDir.empty() ? std::filesystem::current_path() : std::filesystem::path(gProjectDir)) / "Assets" / "VMU";
                    std::error_code ec;
                    std::filesystem::create_directories(dstDir, ec);

                    int idx = 1;
                    std::filesystem::path outPath;
                    do
                    {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "px%04d.png", idx++);
                        outPath = dstDir / buf;
                    } while (std::filesystem::exists(outPath, ec));

                    if (SaveVmuMonoPng(outPath, gVmuMono))
                    {
                        gVmuAssetPath = outPath.string();
                        gViewportToast = "VMU Tool: saved " + outPath.filename().string();
                    }
                    else
                    {
                        gViewportToast = "VMU Tool: save failed";
                    }
                }
                gViewportToastUntil = glfwGetTime() + 1.8;
            }
            ImGui::SameLine();
            if (ImGui::Button(gVmuLoadOnBoot ? "Load on boot: ON" : "Load on boot"))
            {
                if (!gVmuHasImage)
                {
                    gViewportToast = "VMU Tool: open a 48x32 PNG first";
                }
                else
                {
                    gVmuLoadOnBoot = !gVmuLoadOnBoot;
                    gViewportToast = gVmuLoadOnBoot ? "VMU Tool: load-on-boot enabled" : "VMU Tool: load-on-boot disabled";
                }
                gViewportToastUntil = glfwGetTime() + 1.8;
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Asset Linked"))
            {
                bool loaded = false;

                // Prefer VMUAnim when one is currently loaded/linked.
                if (!gVmuLinkedAnimPath.empty() && (gVmuCurrentLoadedType == 2 || gVmuAnimPlaying))
                {
                    if (LoadVmuFrameData(gVmuLinkedAnimPath))
                    {
                        gViewportToast = "VMU Tool: loaded linked VMUAnim";
                    }
                    else
                    {
                        gViewportToast = "VMU Tool: linked VMUAnim missing/invalid";
                    }
                    loaded = true;
                }

                if (!loaded && gVmuAnimLayerSel >= 0 && gVmuAnimLayerSel < (int)gVmuAnimLayers.size())
                {
                    const VmuAnimLayer& l = gVmuAnimLayers[(size_t)gVmuAnimLayerSel];
                    if (!l.linkedAsset.empty())
                    {
                        gVmuLinkedPngPath = l.linkedAsset;
                        std::string err;
                        if (LoadVmuPngToMono(gVmuLinkedPngPath, err))
                        {
                            gViewportToast = "VMU Tool: loaded linked PNG";
                            loaded = true;
                        }
                        else
                        {
                            gViewportToast = err;
                            loaded = true;
                        }
                    }
                }

                if (!loaded && !gVmuLinkedAnimPath.empty())
                {
                    if (LoadVmuFrameData(gVmuLinkedAnimPath)) gViewportToast = "VMU Tool: loaded linked VMUAnim";
                    else gViewportToast = "VMU Tool: linked VMUAnim missing/invalid";
                    loaded = true;
                }

                if (!loaded && !gVmuAssetPath.empty())
                {
                    // Fallback: current loaded PNG can be used as a link source even before assigning to a layer.
                    gVmuLinkedPngPath = gVmuAssetPath;
                    std::string err;
                    if (LoadVmuPngToMono(gVmuLinkedPngPath, err)) gViewportToast = "VMU Tool: loaded current PNG";
                    else gViewportToast = err;
                    loaded = true;
                }
                if (!loaded)
                {
                    gViewportToast = "VMU Tool: no linked PNG/VMUAnim";
                }
                gViewportToastUntil = glfwGetTime() + 1.8;
            }
            ImGui::SameLine();
            if (ImGui::Button(gVmuDrawMode ? "Draw: ON" : "Draw"))
            {
                gVmuDrawMode = !gVmuDrawMode;
                if (gVmuDrawMode && !gVmuHasImage)
                {
                    gVmuHasImage = true;
                    gVmuMono.fill(0);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Save FrameData"))
            {
                std::filesystem::path dstDir = std::filesystem::path(gProjectDir.empty() ? std::filesystem::current_path() : std::filesystem::path(gProjectDir)) / "Assets" / "VMU";
                std::error_code ec;
                std::filesystem::create_directories(dstDir, ec);
                int idx = 1;
                std::filesystem::path outPath;
                do
                {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "framedata%04d.vmuanim", idx++);
                    outPath = dstDir / buf;
                } while (std::filesystem::exists(outPath, ec));

                if (SaveVmuFrameData(outPath)) gViewportToast = "VMU Tool: saved " + outPath.filename().string();
                else gViewportToast = "VMU Tool: save framedata failed";
                gViewportToastUntil = glfwGetTime() + 1.8;
            }
            ImGui::SameLine();
            if (ImGui::Button("Load FrameData"))
            {
                std::string picked = PickVmuFrameDataDialog("Load VMU Frame Data");
                if (!picked.empty())
                {
                    if (LoadVmuFrameData(picked))
                    {
                        gVmuLinkedAnimPath = picked;
                        gViewportToast = "VMU Tool: frame data loaded";
                    }
                    else gViewportToast = "VMU Tool: load framedata failed";
                    gViewportToastUntil = glfwGetTime() + 1.8;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Close"))
            {
                gVmuAnimLayers = { {"Layer 1", true, 0, 0, ""} };
                gVmuAnimLayerSel = 0;
                gVmuAnimTotalFrames = 24;
                gVmuAnimPlayhead = 0;
                gVmuHasImage = true;
                gVmuMono.fill(0);
                gVmuAssetPath.clear();
                gVmuLinkedPngPath.clear();
                gVmuLinkedAnimPath.clear();
                gVmuCurrentLoadedType = 0;
                gViewportToast = "VMU Tool: frame/layers cleared";
                gViewportToastUntil = glfwGetTime() + 1.5;
            }
            float closeW = ImGui::GetFrameHeight();
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - closeW - ImGui::GetStyle().FramePadding.x * 2.0f - 6.0f);
            if (ImGui::Button("X", ImVec2(closeW, 0.0f)))
            {
                gShowVmuTool = false;
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();

            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false) && !gVmuUndoStack.empty())
            {
                gVmuMono = gVmuUndoStack.back();
                gVmuUndoStack.pop_back();
                gVmuHasImage = true;
                gViewportToast = "VMU Tool: undo";
                gViewportToastUntil = glfwGetTime() + 1.0;
            }

            ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.x > 10.0f && avail.y > 10.0f)
            {
                const float leftW = avail.x * 0.45f;
                const float splitGap = 10.0f;
                const float rightW = avail.x - leftW - splitGap;

                // Left: upper-half asset browser area.
                ImGui::BeginChild("##VmuLeftPane", ImVec2(leftW, avail.y), false);
                ImGui::BeginChild("##VmuAssetBrowser", ImVec2(0.0f, avail.y * 0.5f), true);
                if (ImGui::BeginTabBar("##VmuAssetTabs"))
                {
                    if (ImGui::BeginTabItem("Asset Browser"))
                    {
                        if (ImGui::Button("Image..."))
                        {
                            std::string picked = PickPngFileDialog("Import VMU PNG (48x32)");
                            if (!picked.empty())
                            {
                                std::string err;
                                if (LoadVmuPngToMono(picked, err))
                                {
                                    std::filesystem::path dstDir = std::filesystem::path(gProjectDir.empty() ? std::filesystem::current_path() : std::filesystem::path(gProjectDir)) / "Assets" / "VMU";
                                    std::error_code ec;
                                    std::filesystem::create_directories(dstDir, ec);
                                    std::filesystem::path srcPath(picked);
                                    std::filesystem::path outPath = dstDir / srcPath.filename();
                                    if (std::filesystem::exists(outPath, ec))
                                    {
                                        std::string stem = outPath.stem().string();
                                        std::string ext = outPath.extension().string();
                                        int n = 1;
                                        while (std::filesystem::exists(outPath, ec))
                                        {
                                            outPath = dstDir / (stem + "_" + std::to_string(n++) + ext);
                                        }
                                    }
                                    std::filesystem::copy_file(srcPath, outPath, std::filesystem::copy_options::overwrite_existing, ec);
                                    if (!ec)
                                    {
                                        gVmuAssetPath = outPath.string();
                                        gViewportToast = "VMU Tool: image imported to Assets/VMU";
                                    }
                                    else
                                    {
                                        gViewportToast = "VMU Tool: import failed";
                                    }
                                }
                                else
                                {
                                    gViewportToast = err;
                                }
                                gViewportToastUntil = glfwGetTime() + 2.0;
                            }
                        }
                        ImGui::Separator();

                        std::filesystem::path assetDir = std::filesystem::path(gProjectDir.empty() ? std::filesystem::current_path() : std::filesystem::path(gProjectDir)) / "Assets" / "VMU";
                        std::error_code ec;
                        if (std::filesystem::exists(assetDir, ec))
                        {
                            for (const auto& e : std::filesystem::directory_iterator(assetDir, ec))
                            {
                                if (ec || !e.is_regular_file()) continue;
                                std::filesystem::path p = e.path();
                                std::string ext = p.extension().string();
                                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                                if (ext != ".png" && ext != ".vmuanim") continue;
                                bool selected = (!gVmuAssetPath.empty() && std::filesystem::path(gVmuAssetPath) == p);
                                if (ImGui::Selectable(p.filename().string().c_str(), selected))
                                {
                                    if (ext == ".vmuanim")
                                    {
                                        if (LoadVmuFrameData(p))
                                        {
                                            gVmuLinkedAnimPath = p.string();
                                            gViewportToast = "VMU Tool: frame data loaded";
                                        }
                                        else gViewportToast = "VMU Tool: load framedata failed";
                                    }
                                    else
                                    {
                                        std::string err;
                                        if (LoadVmuPngToMono(p.string(), err))
                                        {
                                            gVmuLinkedPngPath = p.string(); // treat selected PNG as current linked source
                                            gViewportToast = "VMU Tool: loaded " + p.filename().string();
                                        }
                                        else gViewportToast = err;
                                    }
                                    gViewportToastUntil = glfwGetTime() + 2.0;
                                }
                                if (ext == ".png" && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                                {
                                    std::string dragPath = p.string();
                                    ImGui::SetDragDropPayload("VMU_PNG_ASSET", dragPath.c_str(), dragPath.size() + 1);
                                    ImGui::TextUnformatted(p.filename().string().c_str());
                                    ImGui::EndDragDropSource();
                                }
                                if (ImGui::BeginPopupContextItem((std::string("##vmuAssetCtx_") + p.filename().string()).c_str()))
                                {
                                    if (ImGui::MenuItem("Delete"))
                                    {
                                        std::error_code dec;
                                        bool removed = std::filesystem::remove(p, dec);
                                        if (removed)
                                        {
                                            if (!gVmuAssetPath.empty() && std::filesystem::path(gVmuAssetPath) == p)
                                                gVmuAssetPath.clear();
                                            gViewportToast = "VMU Tool: deleted " + p.filename().string();
                                        }
                                        else
                                        {
                                            gViewportToast = "VMU Tool: delete failed";
                                        }
                                        gViewportToastUntil = glfwGetTime() + 1.8;
                                    }
                                    ImGui::EndPopup();
                                }
                            }
                        }
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
                ImGui::EndChild();

                ImGui::BeginChild("##VmuLayerPanel", ImVec2(0.0f, 0.0f), true);

                if (ImGui::BeginTabBar("##VmuTimelineTabs"))
                {
                    ImGui::TabItemButton("TIMELINE", ImGuiTabItemFlags_Leading);
                    ImGui::TabItemButton("MOTION EDITOR");
                    ImGui::EndTabBar();
                }

                ImGui::SetNextItemWidth(90.0f);
                ImGui::InputInt("Frames", &gVmuAnimTotalFrames);
                if (gVmuAnimTotalFrames < 1) gVmuAnimTotalFrames = 1;
                if (gVmuAnimPlayhead < 0) gVmuAnimPlayhead = 0;
                if (gVmuAnimPlayhead >= gVmuAnimTotalFrames) gVmuAnimPlayhead = gVmuAnimTotalFrames - 1;
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90.0f);
                ImGui::SliderInt("Playhead", &gVmuAnimPlayhead, 0, gVmuAnimTotalFrames - 1);
                ImGui::SameLine();
                if (ImGui::Button("+ Layer"))
                {
                    VmuAnimLayer l;
                    l.name = "Layer " + std::to_string((int)gVmuAnimLayers.size() + 1);
                    l.visible = true;
                    l.frameStart = 0;
                    l.frameEnd = gVmuAnimTotalFrames - 1;
                    gVmuAnimLayers.push_back(l);
                    gVmuAnimLayerSel = (int)gVmuAnimLayers.size() - 1;
                }
                ImGui::SameLine();
                if (ImGui::Button("- Layer") && !gVmuAnimLayers.empty())
                {
                    if (gVmuAnimLayerSel < 0) gVmuAnimLayerSel = 0;
                    if (gVmuAnimLayerSel >= (int)gVmuAnimLayers.size()) gVmuAnimLayerSel = (int)gVmuAnimLayers.size() - 1;
                    gVmuAnimLayers.erase(gVmuAnimLayers.begin() + gVmuAnimLayerSel);
                    if (gVmuAnimLayers.empty()) gVmuAnimLayerSel = -1;
                    else if (gVmuAnimLayerSel >= (int)gVmuAnimLayers.size()) gVmuAnimLayerSel = (int)gVmuAnimLayers.size() - 1;
                }

                // Right-side playback controls.
                float speedBtnW = 86.0f;
                float loopBtnW = 78.0f;
                float playBtnW = 56.0f;
                float groupW = playBtnW + loopBtnW + speedBtnW + 12.0f;
                float groupX = ImGui::GetWindowContentRegionMax().x - groupW - 8.0f;
                if (groupX > ImGui::GetCursorPosX())
                {
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(groupX);
                }

                bool togglePlay = false;
                if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space, false))
                    togglePlay = true;
                if (ImGui::Button(gVmuAnimPlaying ? "Stop" : "Play", ImVec2(playBtnW, 0.0f)))
                    togglePlay = true;
                if (togglePlay)
                {
                    gVmuAnimPlaying = !gVmuAnimPlaying;
                    gVmuAnimAccum = 0.0;
                    if (gVmuAnimPlaying) gVmuAnimPlayhead = 0;
                }

                ImGui::SameLine();
                if (ImGui::Button(gVmuAnimLoop ? "Loop ON" : "Loop OFF", ImVec2(loopBtnW, 0.0f)))
                {
                    gVmuAnimLoop = !gVmuAnimLoop;
                    gViewportToast = gVmuAnimLoop ? "VMU Tool: loop ON" : "VMU Tool: loop OFF";
                    gViewportToastUntil = glfwGetTime() + 1.2;
                }

                ImGui::SameLine();
                const char* speedLabel = (gVmuAnimSpeedMode == 0) ? "Speed x0.5" : (gVmuAnimSpeedMode == 1) ? "Speed x1" : "Speed x2";
                if (ImGui::Button(speedLabel, ImVec2(speedBtnW, 0.0f)))
                {
                    gVmuAnimSpeedMode = (gVmuAnimSpeedMode + 1) % 3;
                }

                if (gVmuAnimPlaying)
                {
                    const double baseFps = 8.0;
                    const double mult = (gVmuAnimSpeedMode == 0) ? 0.5 : (gVmuAnimSpeedMode == 1) ? 1.0 : 2.0;
                    const double frameStep = 1.0 / (baseFps * mult);
                    gVmuAnimAccum += ImGui::GetIO().DeltaTime;
                    while (gVmuAnimAccum >= frameStep)
                    {
                        gVmuAnimAccum -= frameStep;
                        gVmuAnimPlayhead++;
                        if (gVmuAnimPlayhead >= gVmuAnimTotalFrames)
                        {
                            gVmuAnimPlayhead = 0; // snap back to frame 1
                            if (!gVmuAnimLoop)
                            {
                                gVmuAnimPlaying = false;
                                break;
                            }
                        }
                    }
                }

                ImGui::Separator();
                ImVec2 tAvail = ImGui::GetContentRegionAvail();
                const float leftColsW = 120.0f;
                const float timelineH = 46.0f;
                const float rowH = 24.0f;

                ImDrawList* tdl = ImGui::GetWindowDrawList();
                ImVec2 origin = ImGui::GetCursorScreenPos();
                float timelineX = origin.x + leftColsW;
                float timelineW = tAvail.x - leftColsW;
                if (timelineW < 60.0f) timelineW = 60.0f;
                float frameW = timelineW / (float)gVmuAnimTotalFrames;

                tdl->AddRectFilled(origin, ImVec2(origin.x + tAvail.x, origin.y + timelineH), IM_COL32(220, 220, 220, 255));
                tdl->AddRect(origin, ImVec2(origin.x + tAvail.x, origin.y + timelineH), IM_COL32(140, 140, 140, 255));
                for (int f = 0; f <= gVmuAnimTotalFrames; ++f)
                {
                    float x = timelineX + frameW * (float)f;
                    int major = (f % 5 == 0) ? 1 : 0;
                    tdl->AddLine(ImVec2(x, origin.y + (major ? 12.0f : 20.0f)), ImVec2(x, origin.y + timelineH), IM_COL32(150, 150, 150, 255));
                    if (major && f > 0)
                    {
                        char buf[16]; snprintf(buf, sizeof(buf), "%d", f);
                        tdl->AddText(ImVec2(x + 2.0f, origin.y + 1.0f), IM_COL32(40, 40, 40, 255), buf);
                    }
                }
                float phx = timelineX + frameW * (float)gVmuAnimPlayhead;
                tdl->AddLine(ImVec2(phx, origin.y), ImVec2(phx, origin.y + tAvail.y), IM_COL32(220, 30, 30, 255), 2.0f);

                ImGui::Dummy(ImVec2(tAvail.x, timelineH));

                for (int i = 0; i < (int)gVmuAnimLayers.size(); ++i)
                {
                    VmuAnimLayer& l = gVmuAnimLayers[(size_t)i];
                    ImVec2 rowPos = ImGui::GetCursorScreenPos();
                    ImGui::InvisibleButton((std::string("##layerRow") + std::to_string(i)).c_str(), ImVec2(tAvail.x, rowH));
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VMU_PNG_ASSET"))
                        {
                            const char* dropped = (const char*)payload->Data;
                            if (dropped && dropped[0])
                            {
                                l.linkedAsset = dropped;
                                gVmuLinkedPngPath = l.linkedAsset;
                                gViewportToast = "VMU Tool: linked PNG to layer";
                                gViewportToastUntil = glfwGetTime() + 1.5;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    bool rowSel = (gVmuAnimLayerSel == i);
                    if (ImGui::IsItemClicked())
                    {
                        gVmuAnimLayerSel = i;
                        if (!l.linkedAsset.empty())
                        {
                            std::string err;
                            if (!LoadVmuPngToMono(l.linkedAsset, err))
                            {
                                gViewportToast = err;
                                gViewportToastUntil = glfwGetTime() + 1.5;
                            }
                        }
                        else
                        {
                            gVmuHasImage = true;
                            gVmuMono.fill(0);
                        }
                    }

                    tdl->AddRectFilled(ImVec2(rowPos.x + 24.0f, rowPos.y), ImVec2(rowPos.x + tAvail.x, rowPos.y + rowH), rowSel ? IM_COL32(170, 205, 240, 180) : IM_COL32(235, 235, 235, 255));
                    tdl->AddRect(rowPos, ImVec2(rowPos.x + tAvail.x, rowPos.y + rowH), IM_COL32(170, 170, 170, 255));
                    ImGui::SetCursorScreenPos(ImVec2(rowPos.x + 2.0f, rowPos.y + 2.0f));
                    ImGui::PushID(i);
                    if (ImGui::SmallButton("->"))
                    {
                        if (gVmuCurrentLoadedType == 2 && !gVmuLinkedAnimPath.empty())
                        {
                            // VMUAnim link is project-persistent (global animation source for runtime/editor playback).
                            gViewportToast = "VMU Tool: linked VMUAnim";
                        }
                        else if (!gVmuAssetPath.empty())
                        {
                            l.linkedAsset = gVmuAssetPath;
                            gVmuLinkedPngPath = gVmuAssetPath;
                            gViewportToast = "VMU Tool: linked PNG to layer";
                        }
                        else
                        {
                            gViewportToast = "VMU Tool: load a PNG or VMUAnim first";
                        }
                        gViewportToastUntil = glfwGetTime() + 1.5;
                    }
                    ImGui::PopID();
                    tdl->AddText(ImVec2(rowPos.x + 26.0f, rowPos.y + 4.0f), IM_COL32(30, 30, 30, 255), l.name.c_str());

                    int fs = std::max(0, std::min(gVmuAnimTotalFrames - 1, l.frameStart));
                    int fe = std::max(fs, std::min(gVmuAnimTotalFrames - 1, l.frameEnd));
                    float x0 = timelineX + frameW * (float)fs;
                    float x1 = timelineX + frameW * (float)(fe + 1);
                    tdl->AddRectFilled(ImVec2(x0, rowPos.y + 3.0f), ImVec2(x1, rowPos.y + rowH - 3.0f), IM_COL32(80, 220, 80, 255));
                    tdl->AddRect(ImVec2(x0, rowPos.y + 3.0f), ImVec2(x1, rowPos.y + rowH - 3.0f), IM_COL32(20, 120, 20, 255));
                    if (!l.linkedAsset.empty())
                    {
                        tdl->AddRectFilled(ImVec2(x0 + 2.0f, rowPos.y + 6.0f), ImVec2(x0 + 6.0f, rowPos.y + rowH - 6.0f), IM_COL32(30, 30, 30, 255));
                    }
                }

                if (gVmuAnimLayerSel >= 0 && gVmuAnimLayerSel < (int)gVmuAnimLayers.size())
                {
                    VmuAnimLayer& l = gVmuAnimLayers[(size_t)gVmuAnimLayerSel];
                    ImGui::Separator();
                    char nameBuf[64] = {};
                    strncpy(nameBuf, l.name.c_str(), sizeof(nameBuf) - 1);
                    if (ImGui::InputText("Layer Name", nameBuf, sizeof(nameBuf))) l.name = nameBuf;
                    ImGui::Checkbox("Visible", &l.visible);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80.0f);
                    ImGui::InputInt("Start", &l.frameStart);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80.0f);
                    ImGui::InputInt("End", &l.frameEnd);
                    if (l.frameStart < 0) l.frameStart = 0;
                    if (l.frameEnd < l.frameStart) l.frameEnd = l.frameStart;
                    if (l.frameEnd >= gVmuAnimTotalFrames) l.frameEnd = gVmuAnimTotalFrames - 1;
                }

                ImGui::EndChild();
                ImGui::EndChild();

                // Auto-switch preview based on active layer range at current playhead.
                std::string activeLayerAsset;
                for (int li = 0; li < (int)gVmuAnimLayers.size(); ++li)
                {
                    const VmuAnimLayer& l = gVmuAnimLayers[(size_t)li];
                    if (!l.visible || l.linkedAsset.empty()) continue;
                    if (gVmuAnimPlayhead < l.frameStart || gVmuAnimPlayhead > l.frameEnd) continue;
                    activeLayerAsset = l.linkedAsset;
                }
                if (!activeLayerAsset.empty())
                {
                    if (activeLayerAsset != gVmuAssetPath)
                    {
                        std::string err;
                        LoadVmuPngToMono(activeLayerAsset, err);
                    }
                }
                else if (!gVmuDrawMode && gVmuCurrentLoadedType == 2)
                {
                    // No active VMUAnim layer at this frame: clear preview grid only for VMUAnim mode.
                    // PNG mode should keep showing the selected PNG.
                    gVmuHasImage = true;
                    gVmuMono.fill(0);
                }

                ImGui::SameLine(0.0f, splitGap);

                // Right: 48x32 preview grid.
                ImGui::BeginChild("##VmuGridPane", ImVec2(rightW, avail.y), false);
                ImVec2 ravail = ImGui::GetContentRegionAvail();
                const float cols = 48.0f;
                const float rows = 32.0f;
                float cell = (ravail.x / cols < ravail.y / rows) ? (ravail.x / cols) : (ravail.y / rows);
                if (cell < 2.0f) cell = 2.0f;

                ImVec2 start = ImGui::GetCursorScreenPos();
                float gridW = cols * cell;
                float gridH = rows * cell;
                start.x += (ravail.x - gridW);
                start.y += (ravail.y - gridH) * 0.5f;

                if (gVmuDrawMode)
                {
                    ImVec2 m = ImGui::GetIO().MousePos;
                    bool inGrid = (m.x >= start.x && m.y >= start.y && m.x < start.x + gridW && m.y < start.y + gridH);
                    bool mouseHeld = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right);
                    if (inGrid && mouseHeld && !gVmuStrokeActive)
                    {
                        gVmuUndoStack.push_back(gVmuMono);
                        if (gVmuUndoStack.size() > 64) gVmuUndoStack.erase(gVmuUndoStack.begin());
                        gVmuStrokeActive = true;
                    }
                    if (!mouseHeld)
                    {
                        gVmuStrokeActive = false;
                        gVmuLastDrawX = -1;
                        gVmuLastDrawY = -1;
                    }
                    if (inGrid && mouseHeld)
                    {
                        int px = (int)((m.x - start.x) / cell);
                        int py = (int)((m.y - start.y) / cell);
                        if (px >= 0 && px < 48 && py >= 0 && py < 32)
                        {
                            gVmuHasImage = true;
                            const uint8_t pen = ImGui::IsMouseDown(ImGuiMouseButton_Right) ? 0 : 1;
                            if (gVmuLastDrawX >= 0 && gVmuLastDrawY >= 0)
                            {
                                int x0 = gVmuLastDrawX, y0 = gVmuLastDrawY;
                                int x1 = px, y1 = py;
                                int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
                                int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
                                int err = dx + dy;
                                while (true)
                                {
                                    if (x0 >= 0 && x0 < 48 && y0 >= 0 && y0 < 32)
                                        gVmuMono[(size_t)y0 * 48u + (size_t)x0] = pen;
                                    if (x0 == x1 && y0 == y1) break;
                                    int e2 = err * 2;
                                    if (e2 >= dy) { err += dy; x0 += sx; }
                                    if (e2 <= dx) { err += dx; y0 += sy; }
                                }
                            }
                            else
                            {
                                gVmuMono[(size_t)py * 48u + (size_t)px] = pen;
                            }
                            gVmuLastDrawX = px;
                            gVmuLastDrawY = py;
                        }
                    }
                }

                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImU32 bgCol = IM_COL32(255, 255, 255, 255);
                ImU32 borderCol = IM_COL32(70, 70, 70, 255);
                ImU32 lineCol = IM_COL32(185, 185, 185, 255);
                ImU32 pixCol = IM_COL32(20, 20, 20, 255);
                dl->AddRectFilled(start, ImVec2(start.x + gridW, start.y + gridH), bgCol);
                if (gVmuHasImage)
                {
                    for (int py = 0; py < 32; ++py)
                    {
                        for (int px = 0; px < 48; ++px)
                        {
                            if (gVmuMono[(size_t)py * 48u + (size_t)px])
                            {
                                float x0 = start.x + px * cell;
                                float y0 = start.y + py * cell;
                                dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + cell, y0 + cell), pixCol);
                            }
                        }
                    }
                }
                dl->AddRect(start, ImVec2(start.x + gridW, start.y + gridH), borderCol, 0.0f, 0, 2.0f);
                for (int x = 1; x < 48; ++x)
                {
                    float px = start.x + x * cell;
                    dl->AddLine(ImVec2(px, start.y), ImVec2(px, start.y + gridH), lineCol, 1.0f);
                }
                for (int y = 1; y < 32; ++y)
                {
                    float py = start.y + y * cell;
                    dl->AddLine(ImVec2(start.x, py), ImVec2(start.x + gridW, py), lineCol, 1.0f);
                }
                ImGui::EndChild();
            }

            ImGui::End();
            ImGui::PopStyleColor(2);
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










