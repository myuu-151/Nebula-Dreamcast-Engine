#pragma once

// ---------------------------------------------------------------------------
// Script compilation, hot-reload, and play-mode script runtime management.
// Extracted from main.cpp.
// ---------------------------------------------------------------------------

#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <atomic>
#include <thread>

#define NOMINMAX
#include <Windows.h>

// ---------------------------------------------------------------------------
// Type aliases
// ---------------------------------------------------------------------------
using EditorScriptStartFn       = void(*)(void);
using EditorScriptUpdateFn      = void(*)(float);
using EditorScriptSceneSwitchFn = void(*)(const char*);
using ScriptHotReloadCallback   = void(*)(const std::vector<std::filesystem::path>& changedFiles, bool manualTrigger);

// ---------------------------------------------------------------------------
// ScriptSlot — one loaded gameplay DLL
// ---------------------------------------------------------------------------
struct ScriptSlot
{
    HMODULE module = nullptr;
    std::string path;
    EditorScriptStartFn       onStart       = nullptr;
    EditorScriptUpdateFn      onUpdate      = nullptr;
    EditorScriptSceneSwitchFn onSceneSwitch = nullptr;
    bool active  = false;
    bool started = false;
};

// ---------------------------------------------------------------------------
// Globals (defined in script_compile.cpp)
// ---------------------------------------------------------------------------
extern bool                       gEnableScriptHotReload;
extern std::unordered_map<std::string, std::filesystem::file_time_type> gScriptHotReloadKnownMtimes;
extern std::string                gScriptHotReloadTrackedProjectDir;
extern double                     gScriptHotReloadNextPollAt;
extern unsigned long long         gScriptHotReloadGeneration;

extern std::vector<ScriptSlot>    gEditorScripts;
extern bool                       gEditorScriptActive;
extern double                     gEditorScriptNextTickLog;
extern bool                       useScriptController;

extern std::atomic<int>           gScriptCompileState;
extern std::atomic<int>           gScriptCompileDone;
extern int                        gScriptCompileTotal;
extern std::thread                gScriptCompileThread;
extern std::vector<std::filesystem::path> gScriptCompilePaths;
extern std::vector<std::filesystem::path> gScriptCompileDllPaths;
extern std::vector<bool>          gScriptCompileResults;

extern ScriptHotReloadCallback    gOnScriptHotReloadEvent;

// ---------------------------------------------------------------------------
// Function declarations
// ---------------------------------------------------------------------------
bool BuildScriptFileMtimeSnapshot(const std::filesystem::path& scriptsDir,
                                  std::unordered_map<std::string, std::filesystem::file_time_type>& outSnapshot);
void RunScriptHotReloadV1(const std::vector<std::filesystem::path>& changedFiles, bool manualTrigger);
void ForceScriptHotReloadNowV1();
void PollScriptHotReloadV1(double now);

std::vector<std::filesystem::path> ResolveAllScriptPaths();
std::filesystem::path ResolveGameplayScriptPath();

bool WriteEditorScriptBridgeFile(const std::filesystem::path& path);
bool CompileEditorScriptDLL(const std::filesystem::path& scriptPath,
                            std::filesystem::path& outDllPath,
                            std::string& outError, int slotIdx = 0);
void UnloadEditorScriptRuntime();
bool LoadEditorScriptSlot(const std::filesystem::path& scriptPath, int slotIdx);

bool BeginPlayScriptRuntime();
bool PollPlayScriptCompile();
void EndPlayScriptRuntime();
void TickPlayScriptRuntime(float dt, double now);
void NotifyScriptSceneSwitch();
