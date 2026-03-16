# Nebula Script Formatting, Syntax, and Semantics

This guide explains how to write C gameplay scripts for Nebula so they run correctly in:
- Windows editor/runtime (DLL path), and
- Dreamcast runtime (compiled into ELF/CDI).

---

## 1) Script file basics

- Script files are plain **C** source files (`.c`).
- Recommended location: `Scripts/` (project) and/or `build_dreamcast/scripts/` (staged build).
- Keep one main gameplay owner script per build unless you intentionally split logic across multiple files.

Example header skeleton:

```c
#include <math.h>
#include <stdio.h>

#if defined(__DREAMCAST__)
#include <kos/dbgio.h>
#include "KosInput.h"
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(_WIN32) && !defined(__DREAMCAST__)
#define NB_SCRIPT_EXPORT __declspec(dllexport)
#else
#define NB_SCRIPT_EXPORT
#endif
```

---

## 2) Required exported hooks

These functions are your script entry points:

```c
NB_SCRIPT_EXPORT void NB_Game_OnStart(void);
NB_SCRIPT_EXPORT void NB_Game_OnUpdate(float dt);
NB_SCRIPT_EXPORT void NB_Game_OnSceneSwitch(const char* sceneName);
```

### Semantics
- **OnStart**: called once after runtime init.
- **OnUpdate(dt)**: called every frame; `dt` is frame delta seconds.
- **OnSceneSwitch(name)**: called when runtime changes scene.

If your script does not export a hook, weak fallback hooks may run instead (no-op behavior).

---

## 3) Runtime bridge calls (`NB_RT_*`)

Declare the runtime bridge symbols in script:

```c
void NB_RT_GetNode3DPosition(const char* name, float outPos[3]);
void NB_RT_SetNode3DPosition(const char* name, float x, float y, float z);
void NB_RT_GetNode3DRotation(const char* name, float outRot[3]);
void NB_RT_SetNode3DRotation(const char* name, float x, float y, float z);
void NB_RT_GetCameraOrbit(const char* name, float outOrbit[3]);
void NB_RT_SetCameraOrbit(const char* name, float x, float y, float z);
void NB_RT_GetCameraRotation(const char* name, float outRot[3]);
void NB_RT_SetCameraRotation(const char* name, float x, float y, float z);
void NB_RT_GetCameraWorldForward(const char* name, float outFwd[3]);
void NB_RT_NextScene(void);
void NB_RT_PrevScene(void);
```

**Scene switching:** `NB_RT_NextScene()` and `NB_RT_PrevScene()` cycle through loaded scenes. The switch is deferred to the end of the current frame — navmesh is automatically rebuilt and `NB_Game_OnSceneSwitch` is called. Use debounce logic to avoid rapid cycling (see `Control4.c` for an example).

### Naming semantics
- `name` must match scene node names (e.g. `"PlayerRoot"`, `"Camera3D1"`, `"AINode"`). Matching is case-insensitive.
- Each script should define its target node name as a constant and pass it to all `NB_RT_*` calls:
  ```c
  static const char* PLAYER_NODE = "PlayerRoot";
  NB_RT_GetNode3DPosition(PLAYER_NODE, pos);
  NB_RT_SetNode3DPosition(PLAYER_NODE, pos[0], pos[1], pos[2]);
  ```
- On Dreamcast, the runtime maintains a name-indexed table of all Node3D nodes. The player's parent Node3D routes to the player globals (physics, rendering, camera). All other names route to independent per-node data in the table.
- If `name` doesn't match any Node3D in the scene, get functions return zeros and set functions are no-ops.

---

## 4) Input semantics (Windows vs Dreamcast)

### Windows
Use `GetAsyncKeyState` for quick polling:

```c
if (GetAsyncKeyState('W') & 0x8000) { /* pressed */ }
```

### Dreamcast
Use `KosInput` wrappers (recommended), not raw Maple bit logic:

```c
if (NB_KOS_HasController()) {
    if (NB_KOS_ButtonDown(NB_BTN_DPAD_UP)) { /* pressed */ }
    float ax = NB_KOS_GetStickX();
    float ay = NB_KOS_GetStickY();
}
```

Why: wrappers normalize button semantics and avoid active-low/raw-state mistakes.

> **Note:** `NB_KOS_PollInput()` is called once per frame by the DC runtime before `NB_Game_OnUpdate(dt)`. Scripts do **not** need to call it themselves — just use the query functions (`NB_KOS_HasController`, `NB_KOS_GetStickX`, `NB_KOS_ButtonDown`, etc.) directly.

### Platform guard and `__DREAMCAST__`

The generated Dreamcast Makefile defines `-D__DREAMCAST__` in CFLAGS. Scripts should use this to guard platform-specific input:

```c
#if defined(__DREAMCAST__)
    // DC input: NB_KOS_HasController(), NB_KOS_GetStickX(), etc.
#elif defined(_WIN32)
    // Editor input: GetAsyncKeyState(), etc.
#endif
```

**Important:** The DC cross-compiler (`sh-elf-gcc`) does not define `_WIN32` or `__DREAMCAST__` on its own — the engine's generated Makefile provides `-D__DREAMCAST__`. If neither macro is defined, both input blocks are skipped and the script receives zero input every frame (compiles without errors but produces no movement). This is the most common cause of "script works in editor but does nothing on hardware."

---

## 5) Formatting conventions (recommended)

### 5.1 Structure
1. Includes
2. Platform macros/export macro
3. Runtime bridge declarations
4. Constants/tunables
5. Utility helpers (`Clamp`, angle wrap, etc.)
6. Hook implementations (`OnStart`, `OnUpdate`, `OnSceneSwitch`)

### 5.2 Naming
- Constants: `UPPER_SNAKE_CASE`
- Static locals/helpers: `lowerCamelCase` or `PascalCase` consistently
- Node labels: `static const char* PLAYER_NODE = "PlayerRoot";`

### 5.3 Safety
- Guard tiny denominators with epsilon (`EPS`) before divide/normalize.
- Clamp pitch/angles/speeds to avoid exploding transforms.
- If `dt <= 0`, set fallback `dt = 1.0f / 60.0f`.

### 5.4 Logging
- Windows: `printf`
- Dreamcast: `dbgio_printf`
- Throttle logs in `OnUpdate` (e.g. once per 30–60 frames).

---

## 6) Common syntax pitfalls

- Missing `NB_SCRIPT_EXPORT` on hooks (Windows symbol not exported).
- C++ syntax in `.c` files (`//` is fine, but no templates/classes).
- Using unavailable headers per platform branch.
- Forgetting to include `KosInput.h` when using `NB_KOS_*`.
- Defining duplicate global symbols across multiple script files.

---

## 7) Common semantic pitfalls

- **Script compiles but no gameplay movement**:
  - Hook not linked (script object not in Makefile)
  - Hook overridden by fallback (link/symbol issue)
  - Wrong node names passed to `NB_RT_*`
  - Input read path wrong for target platform

- **Works in editor, fails on Dreamcast**:
  - `__DREAMCAST__` not defined — DC input block skipped silently (most common cause)
  - Windows-only input path accidentally used
  - Build is compiling stale script copy
  - Dreamcast Makefile not including script file(s)

---

## 8) Minimal semantic template

```c
NB_SCRIPT_EXPORT void NB_Game_OnUpdate(float dt)
{
    if (dt <= 0.0f) dt = 1.0f / 60.0f;

    float inX = 0.0f, inY = 0.0f;

#if defined(_WIN32) && !defined(__DREAMCAST__)
    if (GetAsyncKeyState('A') & 0x8000) inX -= 1.0f;
    if (GetAsyncKeyState('D') & 0x8000) inX += 1.0f;
#elif defined(__DREAMCAST__)
    if (NB_KOS_HasController()) {
        if (NB_KOS_ButtonDown(NB_BTN_DPAD_LEFT))  inX -= 1.0f;
        if (NB_KOS_ButtonDown(NB_BTN_DPAD_RIGHT)) inX += 1.0f;
    }
#endif

    float pos[3] = {0,0,0};
    NB_RT_GetNode3DPosition("PlayerRoot", pos);
    pos[0] += inX * 5.0f * dt;
    NB_RT_SetNode3DPosition("PlayerRoot", pos[0], pos[1], pos[2]);
}
```

---

## 9) Multi-script semantics

Multiple scripts can run simultaneously, each controlling different Node3D nodes by name:

```c
// Control4.c — assigned to PlayerRoot's child mesh
static const char* PLAYER_NODE = "PlayerRoot";
NB_RT_SetNode3DPosition(PLAYER_NODE, x, y, z);

// airoam.c — assigned to AINode's child mesh
static const char* AI_NODE = "AINode";
NB_RT_SetNode3DPosition(AI_NODE, x, y, z);
```

Each script defines `NB_Game_OnStart`, `NB_Game_OnUpdate`, and `NB_Game_OnSceneSwitch`. On Windows, each unique script compiles to its own DLL. On Dreamcast, the export pipeline auto-renames hooks to avoid symbol collisions (see `docs/Multi_Script_Runtime.md`).

**Key rules:**
- Each script controls nodes by name string — it does not automatically control the Node3D it's assigned to. The `script` field on a Node3D just tells the engine to load that script.
- Multiple scripts can read the same node. A single script can control multiple nodes.
- Scripts share the same navmesh and raycast data — `NB_RT_NavMeshBuild()` only needs to be called once (by any script).

---

## 10) Practical checklist before testing

- [ ] Hook functions exported with `NB_SCRIPT_EXPORT`
- [ ] Node names match scene exactly
- [ ] Dreamcast path uses `NB_KOS_*` wrappers inside `#if defined(__DREAMCAST__)`
- [ ] Generated Makefile includes `-D__DREAMCAST__` in CFLAGS
- [ ] Script file included in Dreamcast `SOURCES`
- [ ] Build uses updated script copy (not stale staged copy)
- [ ] Runtime logging confirms `OnStart` and `OnUpdate` are executing
