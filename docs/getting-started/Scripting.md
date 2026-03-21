# Scripting System

This document covers the Nebula Dreamcast Engine scripting system end to end: how scripts are written, discovered, compiled, loaded, executed, hot-reloaded, and how the same scripts run on Dreamcast hardware.

Source files:
- `src/runtime/script_compile.h` / `script_compile.cpp` -- script compilation, DLL loading, hot reload
- `src/runtime/runtime_bridge.h` / `runtime_bridge.cpp` -- NB_RT_* bridge function declarations and implementations

---

## 1. Overview

Gameplay scripts are plain **C files** (`.c`). Each script implements up to three callback hooks that the engine calls at well-defined moments:

| Hook | Signature | When called |
|------|-----------|-------------|
| `NB_Game_OnStart` | `void NB_Game_OnStart(void)` | Once, immediately after all DLLs are loaded and play mode begins |
| `NB_Game_OnUpdate` | `void NB_Game_OnUpdate(float dt)` | Every frame, with `dt` as seconds since last frame |
| `NB_Game_OnSceneSwitch` | `void NB_Game_OnSceneSwitch(const char* sceneName)` | Each time the active scene changes |

All three hooks are optional. If a script does not export one, the engine silently skips it.

Scripts interact with the engine through **NB_RT_* bridge functions**. These are the only way a script can read or modify nodes, cameras, physics, animations, navmesh, and scenes. On Windows the bridge functions resolve at load time via `GetProcAddress` against the editor executable. On Dreamcast the same function names are compiled directly into `main.c`.

---

## 2. Quick-Start Example

Create a file at `YourProject/Scripts/player.c`:

```c
#include <stdio.h>
#include <math.h>

/* --- Platform export macro --- */
#if defined(_WIN32) && !defined(__DREAMCAST__)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define NB_SCRIPT_EXPORT __declspec(dllexport)
#else
#define NB_SCRIPT_EXPORT
#endif

/* --- Declare the bridge functions you need --- */
void NB_RT_GetNode3DPosition(const char* name, float outPos[3]);
void NB_RT_SetNode3DPosition(const char* name, float x, float y, float z);
void NB_RT_GetNode3DRotation(const char* name, float outRot[3]);
void NB_RT_SetNode3DRotation(const char* name, float x, float y, float z);
int  NB_RT_IsNode3DOnFloor(const char* name);

/* --- Hooks --- */

NB_SCRIPT_EXPORT void NB_Game_OnStart(void)
{
    printf("Player script started\n");
}

NB_SCRIPT_EXPORT void NB_Game_OnUpdate(float dt)
{
    float pos[3];
    NB_RT_GetNode3DPosition("Player", pos);

    /* Simple forward movement */
    pos[2] += 2.0f * dt;
    NB_RT_SetNode3DPosition("Player", pos[0], pos[1], pos[2]);
}

NB_SCRIPT_EXPORT void NB_Game_OnSceneSwitch(const char* sceneName)
{
    printf("Switched to scene: %s\n", sceneName);
}
```

Key points:
- The `NB_SCRIPT_EXPORT` macro is required on Windows so the DLL exports the hook symbols. On Dreamcast it expands to nothing.
- Forward-declare only the `NB_RT_*` functions you actually call. The bridge file provides their implementations at link time.
- Node names (e.g. `"Player"`) must match the node names in your scene exactly (case-sensitive).

---

## 3. Script Discovery

When play mode starts, `ResolveAllScriptPaths()` builds the list of scripts to compile and load. It collects from two sources:

1. **Node3D script fields** -- every `Node3DNode` in every open scene has an optional `script` field. If set, that path is resolved relative to the project directory.
2. **Project Scripts/ folder** -- all `.c` files under `<ProjectDir>/Scripts/` are collected recursively. This matches the Dreamcast build, which includes every `.c` file from that folder.

Deduplication is by canonical path: if the same file is referenced from a node and also lives in `Scripts/`, it appears only once. The order is: current scene nodes first, then other open scenes' nodes, then the Scripts/ folder sweep.

Defined in: `ResolveAllScriptPaths()` in `src/runtime/script_compile.cpp` (line 199).

---

## 4. Compilation

### The compile function

`CompileEditorScriptDLL()` compiles a single `.c` file into a Windows DLL. The compile command is:

```
cl /nologo /LD /TC /O2 /Fe:<output.dll> <script.c> <bridge.c> /link user32.lib
```

Flags:
- `/LD` -- produce a DLL
- `/TC` -- treat input as C (not C++)
- `/O2` -- optimize for speed

Before compiling, the engine writes `nb_editor_bridge.c` into `<ProjectDir>/Intermediate/EditorScript/`. This generated file contains `GetProcAddress` wrappers for every `NB_RT_*` function, so the script can call them without linking against the editor directly.

### DLL cache

Before recompiling, the engine checks whether the existing DLL is newer than the script source file. If so, compilation is skipped entirely. This makes repeated play-mode entries fast when scripts have not changed.

### Build output

All intermediate files go to `<ProjectDir>/Intermediate/EditorScript/`:
- `nb_script_0.dll`, `nb_script_1.dll`, ... (one per script slot)
- `nb_editor_bridge.c` (generated bridge)
- `nb_script_build.log` (compiler output on failure)

Defined in: `CompileEditorScriptDLL()` in `src/runtime/script_compile.cpp` (line 319).

---

## 5. MSVC Resolution

The compiler (`cl.exe`) must be available. The engine resolves it in this order:

1. **PATH** -- runs `where cl` to check if `cl.exe` is already on the system PATH. If found, it is used directly.

2. **User preference** -- if `cl.exe` is not on PATH, the engine checks `gPrefVcvarsPath` (set via File > Preferences in the editor). If the user has configured a path to `vcvarsall.bat` or `vcvars64.bat`, that is used.

3. **Auto-scan** -- if no preference is set, the engine searches a hardcoded list of candidate paths covering:
   - Visual Studio 2022 (Community, Professional, Enterprise, BuildTools)
   - Visual Studio 2019 (same editions)
   - Both `Program Files` and `Program Files (x86)` roots
   - Both `vcvarsall.bat` and `vcvars64.bat` variants

   The first existing path wins. The engine calls it with the `x64` argument (for `vcvarsall.bat`) to set up the 64-bit toolchain, then invokes `cl`.

4. **Failure** -- if none of the above finds MSVC, compilation fails with `"cl.exe not found in PATH and vcvarsall.bat not found"`. A toast notification tells the user to set the MSVC path in Preferences.

Defined in: the `vcvarsCandidates` array and surrounding logic in `CompileEditorScriptDLL()` (line 387).

---

## 6. Async Compilation

Compilation runs on a background thread so the editor viewport stays responsive.

### State machine

The global `gScriptCompileState` atomic integer tracks progress:

| Value | Meaning |
|-------|---------|
| 0 | Idle -- no compilation in progress |
| 1 | Compiling -- background thread is running |
| 2 | Done -- background thread finished, main thread should load DLLs |

### Flow

1. `BeginPlayScriptRuntime()` resolves all script paths, checks if any need recompilation, verifies MSVC is available, then launches `gScriptCompileThread`.
2. The background thread iterates over each script, calling `CompileEditorScriptDLL()` for each one. It updates `gScriptCompileDone` (an atomic counter) after each script so the UI can show a progress bar.
3. When all scripts are compiled, the thread sets `gScriptCompileState` to 2.
4. The main loop calls `PollPlayScriptCompile()` each frame. When it sees state 2, it joins the thread and proceeds to DLL loading.

Before launching the thread, `BeginPlayScriptRuntime()` does a quick pre-check: if any scripts actually need compiling (DLL missing or older than source), it verifies MSVC is reachable. If all DLLs are cached and up-to-date, the thread still runs but each `CompileEditorScriptDLL()` call returns immediately from the cache check.

Defined in: `BeginPlayScriptRuntime()` (line 554) and `PollPlayScriptCompile()` (line 634) in `src/runtime/script_compile.cpp`.

---

## 7. DLL Loading

DLL loading happens on the **main thread** inside `PollPlayScriptCompile()`, after the compile thread finishes. For each successfully compiled script:

1. `LoadLibraryA()` loads the DLL.
2. `GetProcAddress()` resolves three symbols: `NB_Game_OnStart`, `NB_Game_OnUpdate`, `NB_Game_OnSceneSwitch`. Any that are missing become NULL (not an error).
3. A `ScriptSlot` struct is created and pushed into `gEditorScripts`.
4. After all slots are loaded, every slot with a non-NULL `onStart` has it called immediately.

The `ScriptSlot` struct holds:
```cpp
struct ScriptSlot
{
    HMODULE module;                       // DLL handle
    std::string path;                     // source .c path
    EditorScriptStartFn onStart;          // void(*)(void)
    EditorScriptUpdateFn onUpdate;        // void(*)(float)
    EditorScriptSceneSwitchFn onSceneSwitch; // void(*)(const char*)
    bool active;
    bool started;
};
```

When play mode ends, `EndPlayScriptRuntime()` calls `FreeLibrary()` on every loaded DLL and clears the slot list.

---

## 8. Runtime Tick

Once scripts are loaded and play mode is active, the engine calls two functions each frame:

### Per-frame update

`TickPlayScriptRuntime(float dt, double now)` iterates over all active `ScriptSlot` entries and calls `slot.onUpdate(dt)` on each one. It only runs when `gPlayMode`, `gEditorScriptActive`, and `useScriptController` are all true. A diagnostic log line is printed once per second.

### Scene switch notification

`NotifyScriptSceneSwitch()` is called whenever the active scene changes (via `NB_RT_NextScene`, `NB_RT_PrevScene`, `NB_RT_SwitchScene`, or editor scene tab switching during play mode). It passes the new scene's name to every slot's `onSceneSwitch` callback.

---

## 9. Hot Reload

The engine can detect when `.c` files in `<ProjectDir>/Scripts/` change on disk and notify you with a toast.

### Polling

`PollScriptHotReloadV1(double now)` runs every frame when hot reload is enabled (`gEnableScriptHotReload`). It checks the clock and only does real work every **0.75 seconds**. Each poll:

1. Takes a snapshot of all `.c` file modification times in the Scripts/ folder.
2. Compares against the previously known snapshot.
3. If any files were added, removed, or modified, it calls `RunScriptHotReloadV1()` with the list of changed files.

### Notification

`RunScriptHotReloadV1()` increments the hot reload generation counter and shows a viewport toast (e.g. "Script updated: player.c" or "Scripts updated: 3 file(s)"). It also fires the `gOnScriptHotReloadEvent` callback if one is registered.

### Manual trigger

`ForceScriptHotReloadNowV1()` can be called from the editor UI to force an immediate check, bypassing the 0.75s timer. This is useful when you have just saved a file and want to confirm the engine sees the change.

Note: Hot reload v1 detects changes and notifies, but does not automatically recompile and re-inject DLLs mid-play. To pick up script changes, stop and restart play mode.

---

## 10. NB_RT_* API Reference

These are all the bridge functions available to gameplay scripts. They work identically in the editor (Windows) and on Dreamcast.

### Node3D Transform

| Function | Description |
|----------|-------------|
| `void NB_RT_GetNode3DPosition(const char* name, float outPos[3])` | Get world position of a Node3D |
| `void NB_RT_SetNode3DPosition(const char* name, float x, float y, float z)` | Set world position of a Node3D |
| `void NB_RT_GetNode3DRotation(const char* name, float outRot[3])` | Get Euler rotation (degrees) of a Node3D |
| `void NB_RT_SetNode3DRotation(const char* name, float x, float y, float z)` | Set Euler rotation (degrees) of a Node3D. Syncs quaternion internally. If physics is enabled, only yaw is applied; tilt is preserved. |

### Camera

| Function | Description |
|----------|-------------|
| `void NB_RT_GetCameraWorldForward(const char* name, float outFwd[3])` | Get the camera's world-space forward direction vector |
| `void NB_RT_GetCameraOrbit(const char* name, float outOrbit[3])` | Get camera orbit offset (x, y, z) |
| `void NB_RT_SetCameraOrbit(const char* name, float x, float y, float z)` | Set camera orbit offset |
| `void NB_RT_GetCameraRotation(const char* name, float outRot[3])` | Get camera Euler rotation |
| `void NB_RT_SetCameraRotation(const char* name, float x, float y, float z)` | Set camera Euler rotation |
| `int NB_RT_IsCameraUnderNode3D(const char* cameraName, const char* nodeName)` | Returns 1 if the camera is parented under the named Node3D |

### Collision and Physics

| Function | Description |
|----------|-------------|
| `void NB_RT_GetNode3DCollisionBounds(const char* name, float outExtents[3])` | Get AABB half-extents |
| `void NB_RT_SetNode3DCollisionBounds(const char* name, float ex, float ey, float ez)` | Set AABB half-extents |
| `void NB_RT_GetNode3DBoundPos(const char* name, float outPos[3])` | Get collision bound center offset |
| `void NB_RT_SetNode3DBoundPos(const char* name, float bx, float by, float bz)` | Set collision bound center offset |
| `int NB_RT_GetNode3DPhysicsEnabled(const char* name)` | Returns 1 if physics (gravity/ground snap) is enabled |
| `void NB_RT_SetNode3DPhysicsEnabled(const char* name, int enabled)` | Enable or disable physics |
| `int NB_RT_GetNode3DCollisionSource(const char* name)` | Returns 1 if the node is a collision source |
| `void NB_RT_SetNode3DCollisionSource(const char* name, int enabled)` | Set collision source flag |
| `int NB_RT_GetNode3DSimpleCollision(const char* name)` | Returns 1 if simple (AABB) collision is enabled |
| `void NB_RT_SetNode3DSimpleCollision(const char* name, int enabled)` | Set simple collision flag |
| `float NB_RT_GetNode3DVelocityY(const char* name)` | Get vertical velocity |
| `void NB_RT_SetNode3DVelocityY(const char* name, float vy)` | Set vertical velocity (for jumping) |
| `int NB_RT_IsNode3DOnFloor(const char* name)` | Returns 1 if the node has landed (physics enabled, velY near zero) |
| `int NB_RT_CheckAABBOverlap(const char* name1, const char* name2)` | Returns 1 if two nodes' AABBs overlap. Accounts for bound offsets and node scale. |

### Animation

| Function | Description |
|----------|-------------|
| `void NB_RT_PlayAnimation(const char* meshName, const char* animName)` | Start playing a named animation slot on a StaticMesh3D. Resets time to 0. |
| `void NB_RT_StopAnimation(const char* meshName)` | Stop animation playback on a StaticMesh3D |
| `int NB_RT_IsAnimationPlaying(const char* meshName)` | Returns 1 if an animation is currently playing |
| `int NB_RT_IsAnimationFinished(const char* meshName)` | Returns 1 if the current animation has finished (non-looping only) |
| `void NB_RT_SetAnimationSpeed(const char* meshName, float speed)` | Override playback speed (1.0 = normal) |

### NavMesh

| Function | Description |
|----------|-------------|
| `int NB_RT_NavMeshBuild(void)` | Build the navigation mesh from all StaticMesh3D geometry within NavMesh3D bounds. Returns 1 on success. |
| `void NB_RT_NavMeshClear(void)` | Destroy the current nav mesh |
| `int NB_RT_NavMeshIsReady(void)` | Returns 1 if a nav mesh has been built and is available |
| `int NB_RT_NavMeshFindPath(float sx, sy, sz, float gx, gy, gz, float* outPath, int maxPoints)` | Find a path from start to goal. Returns number of waypoints written to `outPath` (3 floats per point). |
| `int NB_RT_NavMeshFindRandomPoint(float outPos[3])` | Pick a random reachable point on the nav mesh |
| `int NB_RT_NavMeshFindClosestPoint(float px, py, pz, float outPos[3])` | Snap a position to the nearest point on the nav mesh |

### Scene Switching

| Function | Description |
|----------|-------------|
| `void NB_RT_NextScene(void)` | Switch to the next scene in the open scene list (wraps around) |
| `void NB_RT_PrevScene(void)` | Switch to the previous scene (wraps around) |
| `void NB_RT_SwitchScene(const char* name)` | Switch to a scene by name. If not already open, searches the project folder for a matching `.nebscene` file and loads it. |

### Raycasting

| Function | Description |
|----------|-------------|
| `int NB_RT_RaycastDown(float x, float y, float z, float* outHitY)` | Cast a ray straight down from (x,y,z). Returns 1 if a collision-source floor triangle was hit, writes the Y position to `outHitY`. |
| `int NB_RT_RaycastDownWithNormal(float x, y, z, float* outHitY, float outNormal[3])` | Same as RaycastDown but also returns the surface normal of the hit triangle. Useful for slope alignment. |

---

## 11. Dreamcast Side

On Dreamcast, the scripting system works differently at the implementation level but identically from the script author's perspective.

### How it works

1. The editor's Dreamcast export/package step collects all `.c` files from the project's `Scripts/` folder.
2. The editor generates `build_dreamcast/main.c`, which contains:
   - Direct implementations of every `NB_RT_*` function (no DLL, no `GetProcAddress` -- just normal C functions).
   - A main loop that calls `NB_Game_OnStart()` once at boot, `NB_Game_OnUpdate(dt)` each frame, and `NB_Game_OnSceneSwitch(name)` on scene transitions.
3. All script `.c` files are compiled alongside `main.c` by the KOS (Kallistios) toolchain into a single Dreamcast binary.
4. On Dreamcast, `NB_RT_*` functions call into `NB_DC_*` and `NB_KOS_*` platform APIs defined in `src/platform/dreamcast/KosBindings.c`.

### What this means for script authors

- Write your script once. It runs in the editor and on Dreamcast without changes.
- The `NB_SCRIPT_EXPORT` macro expands to `__declspec(dllexport)` on Windows and to nothing on Dreamcast, so it is safe to use on both platforms.
- If you need platform-specific behavior, use `#if defined(__DREAMCAST__)` / `#else` / `#endif`.
- Do not `#include <windows.h>` unconditionally -- guard it with `#if defined(_WIN32) && !defined(__DREAMCAST__)`.

---

## 12. Troubleshooting

**"Script runtime failed: no scripts"** -- No `.c` files were found. Check that your project has a `Scripts/` folder with at least one `.c` file, or that a Node3D in your scene has its `script` field set to a valid path.

**"cl.exe not found in PATH and vcvarsall.bat not found"** -- The engine cannot find MSVC. Either add `cl.exe` to your PATH, or set the MSVC path in File > Preferences.

**"compile failed (see ...build.log)"** -- The C compiler returned an error. Open `<ProjectDir>/Intermediate/EditorScript/nb_script_build.log` to see the full compiler output.

**Script hooks not called** -- Make sure your hook functions are marked with `NB_SCRIPT_EXPORT` and have the exact signatures shown above. Missing the export macro means `GetProcAddress` cannot find the symbols in the DLL.

**Node not found at runtime** -- Node name strings are case-sensitive and must match the scene exactly. Check for trailing spaces or typos.

## See Also

- [Script Formatting Syntax Semantics](Script_Formatting_Syntax_Semantics.md) -- script file conventions and syntax rules
- [Multi Script Runtime](Multi_Script_Runtime.md) -- running multiple scripts simultaneously
- [Dreamcast Binding API v2](Dreamcast_Binding_API_v2.md) -- complete NB_RT_* function reference
- [Physics and Collision](../gameplay/Physics_and_Collision.md) -- runtime collision and physics system
- [Navigation](../gameplay/Navigation.md) -- navmesh pathfinding system
