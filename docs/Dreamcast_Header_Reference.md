# Dreamcast Header Reference

This document describes the platform header files used by the Dreamcast runtime. These headers define the C-callable APIs that connect gameplay scripts to the Dreamcast hardware through the generated runtime layer.

---

## KosBindings.h

**Path:** `src/platform/dreamcast/KosBindings.h`
**Implemented in:** `KosBindings.c`

The main platform bindings header. Declares two categories of functions:

### NB_DC_* (Platform Layer)

Implemented directly in `KosBindings.c`. These handle all hardware-level asset I/O on the Dreamcast — reading files from the GD-ROM, parsing Nebula binary formats, and managing loaded data in memory.

| Section | Functions | Purpose |
|---------|-----------|---------|
| Scene lifecycle | `LoadScene`, `UnloadScene`, `SwitchScene` | Load/unload `.nebscene` data from disc |
| Mesh/texture loading | `LoadMesh`, `LoadTexture`, `FreeMesh`, `FreeTexture` | Parse `.nebmesh` and `.nebtex` binaries into GPU-ready structs |
| Scene metadata | `GetSceneName`, `GetSceneMeshPath`, `GetSceneTransform`, `*At` variants | Read scene node info (paths, transforms) for multi-mesh scenes |
| NavMesh asset loading | `LoadNavMesh`, `FreeNavMesh`, `NavMeshIsLoaded`, `GetNavMeshData` | Load serialized navmesh binary (`NAV00001.BIN`) from disc |

### NB_RT_* (Runtime Bridge)

Declared here but **implemented in the generated `build_dreamcast/main.c`**. KosBindings.c provides weak fallback stubs so linking succeeds even without the generated runtime.

Scripts call these functions to interact with the game world. The generated main.c implements them as reads/writes against static global variables that hold the current scene state.

| Section | Functions | Purpose |
|---------|-----------|---------|
| Transform bridge | `Get/SetMeshPosition`, `AddMeshPositionDelta` | Legacy single-mesh position control |
| Node3D bridge | `Get/SetNode3DPosition`, `Get/SetNode3DRotation` | Named Node3D transform control |
| Camera bridge | `Get/SetCameraOrbit`, `Get/SetCameraRotation`, `GetCameraWorldForward`, `IsCameraUnderNode3D` | Camera orientation and linkage queries |
| Collision/physics | `Get/SetNode3DCollisionBounds`, `Get/SetNode3DBoundPos`, `Get/SetNode3DPhysicsEnabled`, `Get/SetNode3DCollisionSource`, `Get/SetNode3DSimpleCollision`, `Get/SetNode3DVelocityY`, `IsNode3DOnFloor`, `CheckAABBOverlap` | AABB collision, gravity, ground snap, slope alignment toggles |
| Raycasting | `RaycastDown`, `RaycastDownWithNormal` | Vertical raycasts against collision geometry |
| NavMesh queries | `NavMeshBuild`, `NavMeshClear`, `NavMeshIsReady`, `NavMeshFindPath`, `NavMeshFindRandomPoint`, `NavMeshFindClosestPoint` | Pathfinding via Detour |

### Common Types

```c
NB_Vec3            — 3-float vector (x, y, z)
NB_Mesh            — Vertex/index/UV data for a loaded mesh
NB_Texture         — Pixel data + dimensions + sampling parameters
NB_KOS_RawPadState — Raw Maple controller state (low-level)
```

---

## KosInput.h

**Path:** `src/platform/dreamcast/KosInput.h`
**Implemented in:** `KosInput.c`

High-level controller input wrappers. Abstracts the raw Maple API into a clean per-frame input model.

### Usage Pattern

```c
NB_KOS_InitInput();           // once at startup
// each frame:
NB_KOS_PollInput();           // read controller state
if (NB_KOS_ButtonPressed(NB_BTN_A)) { /* jump */ }
float sx = NB_KOS_GetStickX(); // -1.0 to +1.0
```

### Functions

| Function | Returns | Purpose |
|----------|---------|---------|
| `NB_KOS_InitInput` | void | One-time initialization |
| `NB_KOS_PollInput` | void | Read controller state (call once per frame) |
| `NB_KOS_HasController` | int | 1 if controller connected on port 0 |
| `NB_KOS_GetStickX/Y` | float | Analog stick, normalized -1.0 to +1.0 |
| `NB_KOS_GetLTrigger/RTrigger` | float | Analog triggers, normalized 0.0 to 1.0 |
| `NB_KOS_ButtonDown` | int | 1 while button is held |
| `NB_KOS_ButtonPressed` | int | 1 on the frame button was first pressed |
| `NB_KOS_ButtonReleased` | int | 1 on the frame button was released |
| `NB_KOS_GetRawButtons` | uint32_t | Raw Maple button bitmask |

### Button IDs

| Define | Maps to |
|--------|---------|
| `NB_BTN_A` | `CONT_A` |
| `NB_BTN_B` | `CONT_B` |
| `NB_BTN_X` | `CONT_X` |
| `NB_BTN_Y` | `CONT_Y` |
| `NB_BTN_START` | `CONT_START` |
| `NB_BTN_DPAD_UP/DOWN/LEFT/RIGHT` | `CONT_DPAD_*` |
| `NB_BTN_Z` | `CONT_Z` |
| `NB_BTN_D` | `CONT_D` |

---

## DetourBridge.h

**Path:** `src/platform/dreamcast/DetourBridge.h`
**Implemented in:** `DetourBridge.cpp`

C-callable wrapper around the Detour navmesh query library, cross-compiled for SH4. The generated main.c calls these internally — scripts never call `NB_DC_Detour*` directly.

### Functions

| Function | Returns | Purpose |
|----------|---------|---------|
| `NB_DC_DetourInit` | int | Create dtNavMesh + dtNavMeshQuery from raw navmesh blob. Node pool: 512, poly cap: 256 |
| `NB_DC_DetourFree` | void | Tear down Detour state |
| `NB_DC_DetourIsReady` | int | 1 if initialized and ready for queries |
| `NB_DC_DetourFindPath` | int | Find straight-line path between two points. Returns waypoint count |
| `NB_DC_DetourFindRandomPoint` | int | Pick a random navigable point on the navmesh |
| `NB_DC_DetourFindClosestPoint` | int | Project a world position onto nearest navmesh surface |

### How It Connects

```
Script calls NB_RT_NavMeshBuild()
  -> generated main.c calls NB_DC_LoadNavMesh()     [KosBindings.c — reads .BIN from disc]
  -> generated main.c calls NB_DC_DetourInit()       [DetourBridge.cpp — creates query objects]

Script calls NB_RT_NavMeshFindPath()
  -> generated main.c calls NB_DC_DetourFindPath()   [DetourBridge.cpp — runs Detour query]
```

### Build Notes

During Dreamcast export, the editor copies 6 Detour source files and 9 headers into `build_dreamcast/detour/`. DetourBridge.h and DetourBridge.cpp are copied into `build_dreamcast/`. The generated Makefile compiles everything with `sh-elf-g++` using `-fno-exceptions -fno-rtti` and links with `kos-c++`.

---

## Source-of-Truth Files

| File | Role |
|------|------|
| `src/platform/dreamcast/KosBindings.c` | NB_DC_* implementations + weak NB_RT_* stubs |
| `src/platform/dreamcast/KosInput.c` | NB_KOS_* input implementations |
| `src/platform/dreamcast/DetourBridge.cpp` | NB_DC_Detour* navmesh query implementations |
| `build_dreamcast/main.c` | Generated runtime — real NB_RT_* implementations, physics loop, render loop |
| `src/main.cpp` | Editor monolith that generates main.c and the Makefile |
