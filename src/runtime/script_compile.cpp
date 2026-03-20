#include "script_compile.h"
#include "editor/prefs.h"
#include "editor/project.h"
#include "nodes/NodeTypes.h"

#include <stdio.h>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <GLFW/glfw3.h>

#include "../editor/editor_state.h"

// ---------------------------------------------------------------------------
// Global definitions (owned by this TU)
// ---------------------------------------------------------------------------
bool                       gEnableScriptHotReload = false;
std::unordered_map<std::string, std::filesystem::file_time_type> gScriptHotReloadKnownMtimes;
std::string                gScriptHotReloadTrackedProjectDir;
double                     gScriptHotReloadNextPollAt = 0.0;
unsigned long long         gScriptHotReloadGeneration = 0;

std::vector<ScriptSlot>    gEditorScripts;
bool                       gEditorScriptActive = false;
double                     gEditorScriptNextTickLog = 0.0;
bool                       useScriptController = true;
bool                       gEnableCppPlayFallbackControls = false;

std::atomic<int>           gScriptCompileState{0};
std::atomic<int>           gScriptCompileDone{0};
int                        gScriptCompileTotal = 0;
std::thread                gScriptCompileThread;
std::vector<std::filesystem::path> gScriptCompilePaths;
std::vector<std::filesystem::path> gScriptCompileDllPaths;
std::vector<bool>          gScriptCompileResults;

ScriptHotReloadCallback    gOnScriptHotReloadEvent = nullptr;

// ---------------------------------------------------------------------------
// BuildScriptFileMtimeSnapshot
// ---------------------------------------------------------------------------
bool BuildScriptFileMtimeSnapshot(const std::filesystem::path& scriptsDir, std::unordered_map<std::string, std::filesystem::file_time_type>& outSnapshot)
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

// ---------------------------------------------------------------------------
// RunScriptHotReloadV1
// ---------------------------------------------------------------------------
void RunScriptHotReloadV1(const std::vector<std::filesystem::path>& changedFiles, bool manualTrigger)
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
        if (changedFiles.size() == 1)
            gViewportToast = "Script updated: " + changedFiles[0].filename().string();
        else
            gViewportToast = "Scripts updated: " + std::to_string((int)changedFiles.size()) + " file(s)";
        printf("[ScriptHotReload] v1 %s reload: %zu .c file(s) changed. state update only (no compile/rebind). generation=%llu\n",
               mode.c_str(), changedFiles.size(), gScriptHotReloadGeneration);
        for (const auto& c : changedFiles)
            printf("[ScriptHotReload] changed: %s\n", c.generic_string().c_str());
    }
    gViewportToastUntil = glfwGetTime() + 3.0;
}

// ---------------------------------------------------------------------------
// ForceScriptHotReloadNowV1
// ---------------------------------------------------------------------------
void ForceScriptHotReloadNowV1()
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

// ---------------------------------------------------------------------------
// PollScriptHotReloadV1
// ---------------------------------------------------------------------------
void PollScriptHotReloadV1(double now)
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

// ---------------------------------------------------------------------------
// ResolveAllScriptPaths
// ---------------------------------------------------------------------------
std::vector<std::filesystem::path> ResolveAllScriptPaths()
{
    std::vector<std::filesystem::path> result;
    if (gProjectDir.empty())
        return result;

    printf("[ScriptRuntime] ProjectDir=%s\n", gProjectDir.c_str());

    auto resolvePath = [](const std::string& rel) -> std::filesystem::path
    {
        if (rel.empty()) return {};
        std::filesystem::path p(rel);
        if (p.is_absolute())
            return p;
        return std::filesystem::path(gProjectDir) / p;
    };

    // Collect unique script paths from all open scenes' Node3D nodes + Scripts folder
    std::vector<std::string> seen;
    auto addScript = [&](const std::filesystem::path& p)
    {
        if (p.empty() || !std::filesystem::exists(p)) return;
        std::string canonical = p.generic_string();
        for (const auto& s : seen)
            if (s == canonical) return;
        seen.push_back(canonical);
        result.push_back(p);
        printf("[ScriptRuntime] ResolvedScript=%s\n", p.string().c_str());
    };
    // From current scene's nodes
    for (const auto& n : gNode3DNodes)
        addScript(resolvePath(n.script));
    // From all other open scenes
    for (int i = 0; i < (int)gOpenScenes.size(); ++i)
    {
        if (i == gActiveScene) continue;
        for (const auto& n : gOpenScenes[i].node3d)
            addScript(resolvePath(n.script));
    }
    // From project Scripts/ folder (matches DC build — all .c files)
    {
        std::filesystem::path scriptsDir = std::filesystem::path(gProjectDir) / "Scripts";
        std::error_code ec2;
        for (auto& e : std::filesystem::recursive_directory_iterator(scriptsDir, ec2))
        {
            if (!e.is_regular_file()) continue;
            if (e.path().extension() == ".c")
                addScript(e.path());
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// ResolveGameplayScriptPath
// ---------------------------------------------------------------------------
std::filesystem::path ResolveGameplayScriptPath()
{
    auto all = ResolveAllScriptPaths();
    return all.empty() ? std::filesystem::path{} : all[0];
}

// ---------------------------------------------------------------------------
// WriteEditorScriptBridgeFile
// ---------------------------------------------------------------------------
bool WriteEditorScriptBridgeFile(const std::filesystem::path& path)
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
    out << "void NB_RT_GetNode3DCollisionBounds(const char* name, float outExtents[3]){ typedef void(*Fn)(const char*, float*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_GetNode3DCollisionBounds\"); if(fn) fn(name,outExtents); }\n";
    out << "void NB_RT_SetNode3DCollisionBounds(const char* name, float ex, float ey, float ez){ typedef void(*Fn)(const char*, float, float, float); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_SetNode3DCollisionBounds\"); if(fn) fn(name,ex,ey,ez); }\n";
    out << "void NB_RT_GetNode3DBoundPos(const char* name, float outPos[3]){ typedef void(*Fn)(const char*, float*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_GetNode3DBoundPos\"); if(fn) fn(name,outPos); }\n";
    out << "void NB_RT_SetNode3DBoundPos(const char* name, float bx, float by, float bz){ typedef void(*Fn)(const char*, float, float, float); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_SetNode3DBoundPos\"); if(fn) fn(name,bx,by,bz); }\n";
    out << "int NB_RT_GetNode3DPhysicsEnabled(const char* name){ typedef int(*Fn)(const char*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_GetNode3DPhysicsEnabled\"); return fn ? fn(name) : 0; }\n";
    out << "void NB_RT_SetNode3DPhysicsEnabled(const char* name, int enabled){ typedef void(*Fn)(const char*, int); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_SetNode3DPhysicsEnabled\"); if(fn) fn(name,enabled); }\n";
    out << "int NB_RT_GetNode3DCollisionSource(const char* name){ typedef int(*Fn)(const char*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_GetNode3DCollisionSource\"); return fn ? fn(name) : 0; }\n";
    out << "void NB_RT_SetNode3DCollisionSource(const char* name, int enabled){ typedef void(*Fn)(const char*, int); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_SetNode3DCollisionSource\"); if(fn) fn(name,enabled); }\n";
    out << "int NB_RT_GetNode3DSimpleCollision(const char* name){ typedef int(*Fn)(const char*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_GetNode3DSimpleCollision\"); return fn ? fn(name) : 0; }\n";
    out << "void NB_RT_SetNode3DSimpleCollision(const char* name, int enabled){ typedef void(*Fn)(const char*, int); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_SetNode3DSimpleCollision\"); if(fn) fn(name,enabled); }\n";
    out << "float NB_RT_GetNode3DVelocityY(const char* name){ typedef float(*Fn)(const char*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_GetNode3DVelocityY\"); return fn ? fn(name) : 0.0f; }\n";
    out << "void NB_RT_SetNode3DVelocityY(const char* name, float vy){ typedef void(*Fn)(const char*, float); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_SetNode3DVelocityY\"); if(fn) fn(name,vy); }\n";
    out << "int NB_RT_IsNode3DOnFloor(const char* name){ typedef int(*Fn)(const char*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_IsNode3DOnFloor\"); return fn ? fn(name) : 0; }\n";
    out << "int NB_RT_CheckAABBOverlap(const char* name1, const char* name2){ typedef int(*Fn)(const char*, const char*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_CheckAABBOverlap\"); return fn ? fn(name1,name2) : 0; }\n";
    out << "int NB_RT_RaycastDown(float x, float y, float z, float* outHitY){ typedef int(*Fn)(float,float,float,float*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_RaycastDown\"); return fn ? fn(x,y,z,outHitY) : 0; }\n";
    out << "int NB_RT_RaycastDownWithNormal(float x, float y, float z, float* outHitY, float outNormal[3]){ typedef int(*Fn)(float,float,float,float*,float*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_RaycastDownWithNormal\"); return fn ? fn(x,y,z,outHitY,outNormal) : 0; }\n";
    out << "int NB_RT_NavMeshBuild(void){ typedef int(*Fn)(void); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_NavMeshBuild\"); return fn ? fn() : 0; }\n";
    out << "void NB_RT_NavMeshClear(void){ typedef void(*Fn)(void); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_NavMeshClear\"); if(fn) fn(); }\n";
    out << "int NB_RT_NavMeshIsReady(void){ typedef int(*Fn)(void); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_NavMeshIsReady\"); return fn ? fn() : 0; }\n";
    out << "int NB_RT_NavMeshFindPath(float sx, float sy, float sz, float gx, float gy, float gz, float* outPath, int maxPoints){ typedef int(*Fn)(float,float,float,float,float,float,float*,int); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_NavMeshFindPath\"); return fn ? fn(sx,sy,sz,gx,gy,gz,outPath,maxPoints) : 0; }\n";
    out << "int NB_RT_NavMeshFindRandomPoint(float outPos[3]){ typedef int(*Fn)(float*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_NavMeshFindRandomPoint\"); return fn ? fn(outPos) : 0; }\n";
    out << "int NB_RT_NavMeshFindClosestPoint(float px, float py, float pz, float outPos[3]){ typedef int(*Fn)(float,float,float,float*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_NavMeshFindClosestPoint\"); return fn ? fn(px,py,pz,outPos) : 0; }\n";
    out << "void NB_RT_NextScene(void){ typedef void(*Fn)(void); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_NextScene\"); if(fn) fn(); }\n";
    out << "void NB_RT_PrevScene(void){ typedef void(*Fn)(void); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_PrevScene\"); if(fn) fn(); }\n";
    out << "void NB_RT_SwitchScene(const char* name){ typedef void(*Fn)(const char*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_SwitchScene\"); if(fn) fn(name); }\n";
    out << "void NB_RT_PlayAnimation(const char* meshName, const char* animName){ typedef void(*Fn)(const char*,const char*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_PlayAnimation\"); if(fn) fn(meshName,animName); }\n";
    out << "void NB_RT_StopAnimation(const char* meshName){ typedef void(*Fn)(const char*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_StopAnimation\"); if(fn) fn(meshName); }\n";
    out << "int NB_RT_IsAnimationPlaying(const char* meshName){ typedef int(*Fn)(const char*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_IsAnimationPlaying\"); return fn ? fn(meshName) : 0; }\n";
    out << "int NB_RT_IsAnimationFinished(const char* meshName){ typedef int(*Fn)(const char*); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_IsAnimationFinished\"); return fn ? fn(meshName) : 0; }\n";
    out << "void NB_RT_SetAnimationSpeed(const char* meshName, float speed){ typedef void(*Fn)(const char*,float); static Fn fn=0; if(!fn) fn=(Fn)nb_get(\"NB_RT_SetAnimationSpeed\"); if(fn) fn(meshName,speed); }\n";
    return true;
}

// ---------------------------------------------------------------------------
// CompileEditorScriptDLL
// ---------------------------------------------------------------------------
bool CompileEditorScriptDLL(const std::filesystem::path& scriptPath, std::filesystem::path& outDllPath, std::string& outError, int slotIdx)
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

    std::string dllName = "nb_script_" + std::to_string(slotIdx);
    outDllPath = outDir / (dllName + ".dll");
    std::filesystem::path bridgePath = outDir / "nb_editor_bridge.c";

    // Skip recompilation if the DLL already exists and is newer than the
    // script source.  Check BEFORE writing the bridge file so its timestamp
    // doesn't invalidate the cache every time.
    if (std::filesystem::exists(outDllPath, ec))
    {
        auto dllTime = std::filesystem::last_write_time(outDllPath, ec);
        auto srcTime = std::filesystem::last_write_time(scriptPath, ec);
        if (!ec && dllTime > srcTime)
        {
            printf("[ScriptRuntime] DLL up-to-date, skipping compile for %s\n", scriptPath.string().c_str());
            return true;
        }
    }

    if (!WriteEditorScriptBridgeFile(bridgePath))
    {
        outError = "failed to write editor bridge file";
        return false;
    }

    std::filesystem::remove(outDllPath, ec);
    std::filesystem::remove(buildLogPath, ec);
    std::filesystem::remove(outDir / (dllName + ".exp"), ec);
    std::filesystem::remove(outDir / (dllName + ".lib"), ec);
    std::filesystem::remove(outDir / (dllName + ".pdb"), ec);

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
            std::filesystem::path userVcvars = ResolveVcvarsPathFromPreference(gPrefVcvarsPath);
            if (!userVcvars.empty())
            {
                vcvarsPath = userVcvars;
                gViewportToast = std::string("Using MSVC from Preferences: ") + userVcvars.string();
                gViewportToastUntil = glfwGetTime() + 2.0;
            }
            else
            {
                gViewportToast = std::string("MSVC path not found (Preferences): ") + gPrefVcvarsPath;
                gViewportToastUntil = glfwGetTime() + 2.5;
            }
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

// ---------------------------------------------------------------------------
// UnloadEditorScriptRuntime
// ---------------------------------------------------------------------------
void UnloadEditorScriptRuntime()
{
    for (auto& slot : gEditorScripts)
    {
        if (slot.module)
        {
            FreeLibrary(slot.module);
            slot.module = nullptr;
        }
    }
    gEditorScripts.clear();
    gEditorScriptActive = false;
}

// ---------------------------------------------------------------------------
// LoadEditorScriptSlot
// ---------------------------------------------------------------------------
bool LoadEditorScriptSlot(const std::filesystem::path& scriptPath, int slotIdx)
{
    std::filesystem::path dllPath;
    std::string err;
    if (!CompileEditorScriptDLL(scriptPath, dllPath, err, slotIdx))
    {
        printf("[ScriptRuntime] compile failed [%d]: %s\n", slotIdx, err.c_str());
        gViewportToast = std::string("Script runtime failed: ") + err;
        gViewportToastUntil = glfwGetTime() + 2.5;
        return false;
    }

    HMODULE mod = LoadLibraryA(dllPath.string().c_str());
    if (!mod)
    {
        printf("[ScriptRuntime] LoadLibrary failed for %s\n", dllPath.string().c_str());
        gViewportToast = "Script runtime failed: LoadLibrary";
        gViewportToastUntil = glfwGetTime() + 2.5;
        return false;
    }

    ScriptSlot slot;
    slot.module = mod;
    slot.path = scriptPath.string();
    slot.onStart = (EditorScriptStartFn)GetProcAddress(mod, "NB_Game_OnStart");
    slot.onUpdate = (EditorScriptUpdateFn)GetProcAddress(mod, "NB_Game_OnUpdate");
    slot.onSceneSwitch = (EditorScriptSceneSwitchFn)GetProcAddress(mod, "NB_Game_OnSceneSwitch");
    slot.active = true;
    slot.started = false;

    printf("[ScriptRuntime] loaded [%d] %s\n", slotIdx, scriptPath.string().c_str());
    printf("[ScriptRuntime] DLL=%s\n", dllPath.string().c_str());
    printf("[ScriptRuntime] symbols: start=%s update=%s scene=%s\n",
        slot.onStart ? "yes" : "no",
        slot.onUpdate ? "yes" : "no",
        slot.onSceneSwitch ? "yes" : "no");

    gEditorScripts.push_back(slot);
    return true;
}

// ---------------------------------------------------------------------------
// BeginPlayScriptRuntime
// ---------------------------------------------------------------------------
bool BeginPlayScriptRuntime()
{
    if (!useScriptController)
    {
        gEditorScriptActive = false;
        printf("[ScriptRuntime] skipped (engine-owned controls mode)\n");
        return true;
    }

    std::vector<std::filesystem::path> scriptPaths = ResolveAllScriptPaths();
    if (scriptPaths.empty())
    {
        printf("[ScriptRuntime] no gameplay scripts resolved\n");
        gViewportToast = "Script runtime failed: no scripts";
        gViewportToastUntil = glfwGetTime() + 2.5;
        return true;
    }

    UnloadEditorScriptRuntime();

    // Quick check: do any scripts actually need compiling?  If so, verify
    // MSVC is available before launching the compile thread.
    {
        bool anyNeedCompile = false;
        std::filesystem::path outDir = std::filesystem::path(gProjectDir) / "Intermediate" / "EditorScript";
        std::error_code ec;
        for (int i = 0; i < (int)scriptPaths.size(); ++i)
        {
            std::filesystem::path dllPath = outDir / ("nb_script_" + std::to_string(i) + ".dll");
            if (!std::filesystem::exists(dllPath, ec))
            { anyNeedCompile = true; break; }
            auto dllTime = std::filesystem::last_write_time(dllPath, ec);
            auto srcTime = std::filesystem::last_write_time(scriptPaths[i], ec);
            if (ec || dllTime <= srcTime)
            { anyNeedCompile = true; break; }
        }
        if (anyNeedCompile)
        {
            int hasCl = system("cmd /c \"where cl >nul 2>nul\"");
            if (hasCl != 0 && ResolveVcvarsPathFromPreference(gPrefVcvarsPath).empty())
            {
                gViewportToast = "MSVC not found! Set PATH in File > Preferences";
                gViewportToastUntil = glfwGetTime() + 3.0;
                return false;
            }
        }
    }

    // Set up async compilation state
    gScriptCompilePaths = scriptPaths;
    gScriptCompileTotal = (int)scriptPaths.size();
    gScriptCompileDone.store(0);
    gScriptCompileDllPaths.assign(gScriptCompileTotal, std::filesystem::path());
    gScriptCompileResults.assign(gScriptCompileTotal, false);
    gScriptCompileState.store(1);

    // Launch compilation on a background thread so the UI stays responsive
    gScriptCompileThread = std::thread([]()
    {
        for (int i = 0; i < gScriptCompileTotal; ++i)
        {
            std::filesystem::path dllPath;
            std::string err;
            bool ok = CompileEditorScriptDLL(gScriptCompilePaths[i], dllPath, err, i);
            gScriptCompileDllPaths[i] = ok ? dllPath : std::filesystem::path();
            gScriptCompileResults[i] = ok;
            if (!ok)
                printf("[ScriptRuntime] compile failed [%d]: %s\n", i, err.c_str());
            gScriptCompileDone.store(i + 1);
        }
        gScriptCompileState.store(2);
    });
    return true;
}

// ---------------------------------------------------------------------------
// PollPlayScriptCompile
// Called from the main loop each frame while gScriptCompileState != 0.
// Returns true once compilation is finished and scripts are loaded.
// ---------------------------------------------------------------------------
bool PollPlayScriptCompile()
{
    int state = gScriptCompileState.load();
    if (state != 2) return false;

    // Join the compile thread
    if (gScriptCompileThread.joinable())
        gScriptCompileThread.join();

    gScriptCompileState.store(0);

    // Load the compiled DLLs on the main thread
    int loaded = 0;
    for (int i = 0; i < gScriptCompileTotal; ++i)
    {
        if (!gScriptCompileResults[i]) continue;

        HMODULE mod = LoadLibraryA(gScriptCompileDllPaths[i].string().c_str());
        if (!mod)
        {
            printf("[ScriptRuntime] LoadLibrary failed for %s\n", gScriptCompileDllPaths[i].string().c_str());
            continue;
        }

        ScriptSlot slot;
        slot.module = mod;
        slot.path = gScriptCompilePaths[i].string();
        slot.onStart = (EditorScriptStartFn)GetProcAddress(mod, "NB_Game_OnStart");
        slot.onUpdate = (EditorScriptUpdateFn)GetProcAddress(mod, "NB_Game_OnUpdate");
        slot.onSceneSwitch = (EditorScriptSceneSwitchFn)GetProcAddress(mod, "NB_Game_OnSceneSwitch");
        slot.active = true;
        slot.started = false;

        printf("[ScriptRuntime] loaded [%d] %s\n", i, gScriptCompilePaths[i].string().c_str());
        printf("[ScriptRuntime] symbols: start=%s update=%s scene=%s\n",
            slot.onStart ? "yes" : "no",
            slot.onUpdate ? "yes" : "no",
            slot.onSceneSwitch ? "yes" : "no");

        gEditorScripts.push_back(slot);
        ++loaded;
    }

    if (loaded == 0)
    {
        gViewportToast = "Script runtime failed: no scripts compiled";
        gViewportToastUntil = glfwGetTime() + 2.5;
        return true;
    }

    gEditorScriptActive = true;
    gEditorScriptNextTickLog = 0.0;

    for (auto& slot : gEditorScripts)
    {
        if (slot.onStart)
        {
            slot.onStart();
            slot.started = true;
            printf("[ScriptRuntime] OnStart called for %s\n", slot.path.c_str());
        }
    }

    gViewportToast = "Script runtime active: " + std::to_string(loaded) + " script(s)";
    gViewportToastUntil = glfwGetTime() + 2.5;
    return true;
}

// ---------------------------------------------------------------------------
// EndPlayScriptRuntime
// ---------------------------------------------------------------------------
void EndPlayScriptRuntime()
{
    // Wait for any in-progress compile thread before unloading
    if (gScriptCompileThread.joinable())
        gScriptCompileThread.join();
    gScriptCompileState.store(0);
    UnloadEditorScriptRuntime();
}

// ---------------------------------------------------------------------------
// TickPlayScriptRuntime
// ---------------------------------------------------------------------------
void TickPlayScriptRuntime(float dt, double now)
{
    if (!gPlayMode || !gEditorScriptActive || !useScriptController)
        return;

    // Clear per-frame script-managed flags before scripts run

    for (auto& slot : gEditorScripts)
    {
        if (slot.active && slot.onUpdate)
            slot.onUpdate(dt);
    }
    if (now >= gEditorScriptNextTickLog)
    {
        gEditorScriptNextTickLog = now + 1.0;
        printf("[ScriptRuntime] OnUpdate tick (%d scripts)\n", (int)gEditorScripts.size());
    }
}

// ---------------------------------------------------------------------------
// NotifyScriptSceneSwitch
// ---------------------------------------------------------------------------
void NotifyScriptSceneSwitch()
{
    if (!gPlayMode || !gEditorScriptActive || !useScriptController)
        return;
    if (gActiveScene < 0 || gActiveScene >= (int)gOpenScenes.size())
        return;
    const char* sceneName = gOpenScenes[gActiveScene].name.c_str();
    for (auto& slot : gEditorScripts)
    {
        if (slot.active && slot.onSceneSwitch)
            slot.onSceneSwitch(sceneName);
    }
    printf("[ScriptRuntime] OnSceneSwitch: %s\n", sceneName);
}
