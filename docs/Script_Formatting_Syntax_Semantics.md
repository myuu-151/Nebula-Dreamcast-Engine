# Nebula Script Formatting, Syntax, and Semantics

This guide explains how to write C gameplay scripts for Nebula so they run correctly in:
- Windows editor/runtime (DLL path), and
- Dreamcast runtime (compiled into ELF/CDI).

---

## 1) Script file basics

- Script files are plain **C** source files (`.c`).
- Recommended location: `Scripts/` (project) and/or `build_dreamcast/scripts/` (staged build).
- Keep one main gameplay owner script per build unless you intentionally split logic across multiple files.

Recommended header skeleton:

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
```

### Naming semantics
- `name` must match scene node names exactly (e.g. `"PlayerRoot"`, `"Camera3D1"`).
- If name mismatch occurs, behavior depends on runtime implementation (often no-op/fallback).

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
NB_KOS_PollInput();
if (NB_KOS_HasController()) {
    if (NB_KOS_ButtonDown(NB_BTN_DPAD_UP)) { /* pressed */ }
    float ax = NB_KOS_GetStickX();
    float ay = NB_KOS_GetStickY();
}
```

Why: wrappers normalize button semantics and avoid active-low/raw-state mistakes.

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
    NB_KOS_PollInput();
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

If multiple `.c` scripts are auto-compiled in one build:
- Avoid implementing the same hook in multiple files unless intentional.
- If multiple define `NB_Game_OnUpdate`, linker behavior may fail or choose one unexpectedly.
- Recommended: one primary gameplay hook file, others as utility modules.

---

## 10) Practical checklist before testing

- [ ] Hook functions exported with `NB_SCRIPT_EXPORT`
- [ ] Node names match scene exactly
- [ ] Dreamcast path uses `NB_KOS_*` wrappers
- [ ] Script file included in Dreamcast `SOURCES`
- [ ] Build uses updated script copy (not stale staged copy)
- [ ] Runtime logging confirms `OnStart` and `OnUpdate` are executing
