# Multi-Script Runtime

Nebula supports running multiple gameplay scripts simultaneously. Each Node3D with a unique `script` field gets its own DLL at runtime.

---

## How it works

1. When play mode starts, the engine scans all Node3D nodes and collects unique script paths from their `script` fields.
2. Each unique script is compiled to its own DLL (`nb_script_0.dll`, `nb_script_1.dll`, etc.) in the project's `Intermediate/EditorScript/` folder.
3. All DLLs are loaded and their `NB_Game_OnStart`, `NB_Game_OnUpdate`, `NB_Game_OnSceneSwitch` symbols are resolved.
4. Every frame, `OnUpdate(dt)` is called on all active scripts in order.

If no Node3D has a `script` field set, the engine falls back to `Scripts/WASD_Node3D_Nav.c`.

---

## Setting up multiple scripts

1. Create your script files in your project's `Scripts/` folder (e.g. `Scripts/player.c`, `Scripts/airoam.c`).
2. In the editor, select a Node3D and set its `script` field to the relative path (e.g. `Scripts/player.c`).
3. Multiple Node3D nodes can reference the same script — it only gets compiled and loaded once.
4. Different Node3D nodes can reference different scripts — each unique script gets its own DLL.

---

## Script structure

Every script must be plain C and export its hooks with `NB_SCRIPT_EXPORT`:

```c
#include <stdio.h>

#if defined(_WIN32) && !defined(__DREAMCAST__)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define NB_SCRIPT_EXPORT __declspec(dllexport)
#else
#define NB_SCRIPT_EXPORT
#endif

// Declare bridge functions you need
void NB_RT_GetNode3DPosition(const char* name, float outPos[3]);
void NB_RT_SetNode3DPosition(const char* name, float x, float y, float z);

NB_SCRIPT_EXPORT void NB_Game_OnStart(void)
{
    printf("Script started\n");
}

NB_SCRIPT_EXPORT void NB_Game_OnUpdate(float dt)
{
    // Your per-frame logic here
}

NB_SCRIPT_EXPORT void NB_Game_OnSceneSwitch(const char* sceneName)
{
    (void)sceneName;
}
```

---

## Which node does a script control?

Scripts reference nodes by name string, not by which Node3D they are assigned to. The `script` field on a Node3D just tells the engine to load that script — the script itself decides which nodes to read/write via the `NB_RT_*` name parameter.

For example, `airoam.c` controls `"AINode"` by name:

```c
static const char* AI_NODE = "AINode";

NB_SCRIPT_EXPORT void NB_Game_OnUpdate(float dt)
{
    float pos[3];
    NB_RT_GetNode3DPosition(AI_NODE, pos);
    // move it...
    NB_RT_SetNode3DPosition(AI_NODE, pos[0], pos[1], pos[2]);
}
```

A single script can control multiple nodes, and multiple scripts can read the same node.

---

## Execution order

1. All scripts' `OnStart` is called in load order (order of first appearance in the node list).
2. Each frame, all scripts' `OnUpdate(dt)` is called in the same order.
3. After scripts run, the engine applies physics (gravity, ground snap, slope alignment) to all physics/collision-enabled Node3Ds.

---

## Limitations

- Scripts are plain C, not C++. No classes or templates.
- Each script has its own static state — globals in one script are not visible to another.
- Scripts cannot directly call functions in other scripts.
- On Windows, scripts are compiled with MSVC (`cl.exe`) at play-mode start. The MSVC path is configured in editor preferences.
- On Dreamcast, all scripts are compiled together into the final binary — the multi-DLL system is editor-only.
