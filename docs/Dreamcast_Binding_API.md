# Dreamcast Binding API (Nebula)

This document explains the runtime binding API used by Nebula gameplay scripts and generated Dreamcast runtime code.

---

## Architecture (quick view)

There are three layers:

1. **Gameplay script layer** (`Scripts/*.c`)
   - Implements `NB_Game_OnStart`, `NB_Game_OnUpdate`, `NB_Game_OnSceneSwitch`.
   - Calls `NB_RT_*` bridge functions.

2. **Generated runtime layer** (`build_dreamcast/main.c`)
   - Implements many `NB_RT_*` bridge functions.
   - Calls `NB_DC_*` loaders/scene APIs and `NB_KOS_*` input APIs.

3. **Platform bindings layer** (`src/platform/dreamcast/*.c`)
   - `KosBindings.c/.h`: scene/mesh/texture loading + scene metadata bridge (`NB_DC_*`).
   - `KosInput.c/.h`: controller input wrappers (`NB_KOS_*`).

---

## Script Hook API (entry points)

These are the functions your script should export:

```c
NB_SCRIPT_EXPORT void NB_Game_OnStart(void);
NB_SCRIPT_EXPORT void NB_Game_OnUpdate(float dt);
NB_SCRIPT_EXPORT void NB_Game_OnSceneSwitch(const char* sceneName);
```

### Behavior
- **OnStart**: called once when runtime starts.
- **OnUpdate(dt)**: called every frame.
- **OnSceneSwitch(name)**: called after runtime switches to another scene.

`NebulaGameStub.c` provides weak fallback definitions so link succeeds even if no script defines them.

---

## Runtime Bridge API (`NB_RT_*`)

Used by scripts to control world/camera state.

### Node/mesh transform bridge

```c
void NB_RT_GetMeshPosition(float outPos[3]);
void NB_RT_SetMeshPosition(float x, float y, float z);
void NB_RT_AddMeshPositionDelta(float dx, float dy, float dz);
```

Legacy/simple mesh bridge (mainly single-mesh compatibility path).

### Node3D bridge

```c
void NB_RT_GetNode3DPosition(const char* name, float outPos[3]);
void NB_RT_SetNode3DPosition(const char* name, float x, float y, float z);
void NB_RT_GetNode3DRotation(const char* name, float outRot[3]);
void NB_RT_SetNode3DRotation(const char* name, float x, float y, float z);
```

Script-side movement/rotation for named Node3D objects.

**Physics-enabled rotation behavior:** When a Node3D has `physicsEnabled`, the engine uses an internal quaternion for orientation and automatically aligns the node to the ground surface normal (slope alignment). In this mode:
- `SetNode3DRotation` only updates **yaw** (the `y` parameter). Tilt (pitch/roll from slope alignment) is preserved automatically — the `x` and `z` parameters are accepted but slope alignment overrides them each frame.
- `GetNode3DRotation` returns the script's last-set yaw in `outRot[1]`, not a value extracted from the internal quaternion. This ensures the script always reads back exactly what it wrote, with no drift or snapping at extreme angles.
- Rendering uses the quaternion directly (converted to a rotation matrix), bypassing Euler angles entirely to avoid gimbal lock.

### Camera bridge

```c
void NB_RT_GetCameraOrbit(const char* name, float outOrbit[3]);
void NB_RT_SetCameraOrbit(const char* name, float x, float y, float z);
void NB_RT_GetCameraRotation(const char* name, float outRot[3]);
void NB_RT_SetCameraRotation(const char* name, float x, float y, float z);
void NB_RT_GetCameraWorldForward(const char* name, float outFwd[3]);
int  NB_RT_IsCameraUnderNode3D(const char* cameraName, const char* nodeName);
```

Used for orbit-style cameras, camera-relative movement, and linkage checks.

### Collision / physics bridge

```c
void  NB_RT_GetNode3DCollisionBounds(const char* name, float outExtents[3]);
void  NB_RT_SetNode3DCollisionBounds(const char* name, float ex, float ey, float ez);
void  NB_RT_GetNode3DBoundPos(const char* name, float outPos[3]);
void  NB_RT_SetNode3DBoundPos(const char* name, float bx, float by, float bz);
int   NB_RT_GetNode3DPhysicsEnabled(const char* name);
void  NB_RT_SetNode3DPhysicsEnabled(const char* name, int enabled);
float NB_RT_GetNode3DVelocityY(const char* name);
void  NB_RT_SetNode3DVelocityY(const char* name, float vy);
int   NB_RT_IsNode3DOnFloor(const char* name);
int   NB_RT_CheckAABBOverlap(const char* name1, const char* name2);
int   NB_RT_RaycastDown(float x, float y, float z, float* outHitY);
```

- **CollisionBounds**: get/set the AABB half-extents (box size) of a Node3D's collision volume.
- **BoundPos**: get/set the local offset of the collision box relative to the node's origin.
- **PhysicsEnabled**: toggle gravity and floor collision per node.
- **VelocityY**: read/write vertical velocity (use `SetNode3DVelocityY` to apply jump impulse).
- **IsNode3DOnFloor**: returns 1 if the node is grounded (physics enabled, vertical velocity near zero).
- **CheckAABBOverlap**: returns 1 if two named Node3D collision boxes overlap (useful for hit detection, triggers).
- **RaycastDown**: casts a vertical ray downward from (x,y,z) against collision-flagged StaticMesh3D triangles. Returns 1 if hit, writing the Y coordinate of the highest surface below the ray origin into `outHitY`. Useful for slope following and ground snapping.

### NavMesh bridge

```c
int  NB_RT_NavMeshBuild(void);
void NB_RT_NavMeshClear(void);
int  NB_RT_NavMeshIsReady(void);
int  NB_RT_NavMeshFindPath(float sx, float sy, float sz, float gx, float gy, float gz, float* outPath, int maxPoints);
int  NB_RT_NavMeshFindRandomPoint(float outPos[3]);
int  NB_RT_NavMeshFindClosestPoint(float px, float py, float pz, float outPos[3]);
```

- **NavMeshBuild**: builds the navmesh from all StaticMesh3D geometry in the current scene. Returns 1 on success.
- **NavMeshClear**: frees the current navmesh data.
- **NavMeshIsReady**: returns 1 if a navmesh has been built and is available for queries.
- **NavMeshFindPath**: finds a path between two world-space points. Writes up to `maxPoints` waypoints into `outPath` (packed xyz). Returns the number of waypoints, or 0 if no path found.
- **NavMeshFindRandomPoint**: picks a random navigable point on the navmesh. Returns 1 on success.
- **NavMeshFindClosestPoint**: projects a world position onto the nearest navmesh surface. Returns 1 on success.

> **Note:** On Dreamcast, navmesh functions are currently stubbed (return 0). The editor-side implementations use Recast/Detour and are fully functional for in-editor script testing.

### Where `NB_RT_*` is implemented

- Implemented in generated runtime code (`main.c`) emitted from `src/main.cpp`.
- Script files only declare and call these symbols.

---

## Dreamcast Scene/Asset Binding API (`NB_DC_*`)

Declared in `src/platform/dreamcast/KosBindings.h`, implemented in `KosBindings.c`.

### Scene lifecycle

```c
int  NB_DC_LoadScene(const char* scenePath);
void NB_DC_UnloadScene(void);
int  NB_DC_SwitchScene(const char* scenePath);
```

### Common types

```c
typedef struct NB_Vec3 {
    float x;
    float y;
    float z;
} NB_Vec3;
```

### Mesh/texture loading

```c
typedef struct NB_Mesh {
    NB_Vec3* pos;
    NB_Vec3* tri_uv;
    NB_Vec3* tri_uv1;       /* second UV layer (v6+), NULL if absent */
    uint16_t* indices;
    uint16_t* tri_mat;
    int vert_count;
    int tri_count;
    int uv_layer_count;      /* total UV layers present (0, 1, or 2) */
} NB_Mesh;

typedef struct NB_Texture {
    uint16_t* pixels;
    int w;
    int h;
    float us;
    float vs;
    int filter;
    int wrapMode;
    int flipU;
    int flipV;
} NB_Texture;

int  NB_DC_LoadMesh(const char* meshPath, NB_Mesh* out);
int  NB_DC_LoadTexture(const char* texPath, NB_Texture* out);
void NB_DC_FreeMesh(NB_Mesh* m);
void NB_DC_FreeTexture(NB_Texture* t);
```

### Scene metadata (single-mesh compatibility)

```c
const char* NB_DC_GetSceneName(void);
const char* NB_DC_GetSceneMeshPath(void);
const char* NB_DC_GetSceneTexturePath(int slotIndex);
void NB_DC_GetSceneTransform(float outPos[3], float outRot[3], float outScale[3]);
```

### Scene metadata (multi-mesh)

```c
int         NB_DC_GetSceneMeshCount(void);
const char* NB_DC_GetSceneMeshPathAt(int meshIndex);
const char* NB_DC_GetSceneTexturePathAt(int meshIndex, int slotIndex);
void        NB_DC_GetSceneTransformAt(int meshIndex, float outPos[3], float outRot[3], float outScale[3]);
```

Use `*At` variants for modern multi-StaticMesh scenes.

### NavMesh asset loading

```c
int         NB_DC_LoadNavMesh(const char* navPath);
void        NB_DC_FreeNavMesh(void);
int         NB_DC_NavMeshIsLoaded(void);
const void* NB_DC_GetNavMeshData(int* outSize);
```

- **LoadNavMesh**: loads a serialized navmesh binary (`.BIN`) from disc into memory. Returns 1 on success.
- **FreeNavMesh**: releases the loaded navmesh data.
- **NavMeshIsLoaded**: returns 1 if navmesh data is currently in memory.
- **GetNavMeshData**: returns a pointer to the raw navmesh blob and its size. Used by future Detour integration for pathfinding queries on DC.

The editor automatically builds and packages the navmesh binary (`NAV00001.BIN`) into `cd_root/data/navmesh/` during Dreamcast export.

---

## Dreamcast Input Binding API (`NB_KOS_*`)

Declared in `src/platform/dreamcast/KosInput.h`, implemented in `KosInput.c`.

```c
void  NB_KOS_InitInput(void);
void  NB_KOS_PollInput(void);
int   NB_KOS_HasController(void);
float NB_KOS_GetStickX(void);
float NB_KOS_GetStickY(void);
float NB_KOS_GetLTrigger(void);
float NB_KOS_GetRTrigger(void);

int      NB_KOS_ButtonDown(int btn);
int      NB_KOS_ButtonPressed(int btn);
int      NB_KOS_ButtonReleased(int btn);
uint32_t NB_KOS_GetRawButtons(void);
```

- **InitInput**: one-time initialization (called by runtime before main loop).
- **PollInput**: reads controller state; call once per frame before any button/stick queries.
- **HasController**: returns 1 if a controller is connected on port 0.
- **GetStickX / GetStickY**: analog stick, normalized to approximately -1.0 .. +1.0.
- **GetLTrigger / GetRTrigger**: analog triggers, normalized to 0.0 .. 1.0.
- **ButtonDown**: returns 1 while the button is held.
- **ButtonPressed**: returns 1 on the frame the button was first pressed.
- **ButtonReleased**: returns 1 on the frame the button was released.
- **GetRawButtons**: returns the raw Maple button bitmask (advanced use).

### Button IDs (`NB_BTN_*`)

```c
#define NB_BTN_A          CONT_A
#define NB_BTN_B          CONT_B
#define NB_BTN_X          CONT_X
#define NB_BTN_Y          CONT_Y
#define NB_BTN_START      CONT_START
#define NB_BTN_DPAD_UP    CONT_DPAD_UP
#define NB_BTN_DPAD_DOWN  CONT_DPAD_DOWN
#define NB_BTN_DPAD_LEFT  CONT_DPAD_LEFT
#define NB_BTN_DPAD_RIGHT CONT_DPAD_RIGHT
#define NB_BTN_Z          CONT_Z
#define NB_BTN_D          CONT_D
```

### Low-level pad access

```c
typedef struct NB_KOS_RawPadState {
    int has_controller;
    uint32_t buttons;
    int8_t stick_x;
    int8_t stick_y;
    uint8_t l_trigger;
    uint8_t r_trigger;
} NB_KOS_RawPadState;

void NB_KOS_BindingsInit(void);
void NB_KOS_BindingsRead(NB_KOS_RawPadState* outState);
```

These are the raw Maple layer underneath `NB_KOS_*`. Most scripts should use the higher-level wrappers instead.

### Why use wrappers instead of raw Maple bits

`NB_KOS_*` normalizes button semantics (including active-low behavior) and keeps script input consistent with runtime expectations.

---

## Runtime Timing

The generated Dreamcast runtime uses `timer_us_gettime64()` to measure real delta time each frame. The `dt` value passed to `NB_Game_OnUpdate(dt)` reflects actual elapsed time in seconds (clamped to 0.1s max to prevent spiral-of-death on long frames).

The render loop is paced by `pvr_wait_ready()` which syncs to the Dreamcast VBlank at 60 Hz (NTSC). No additional sleep is used, so the runtime targets a full 60 fps.

---

## Minimal Script Example

```c
NB_SCRIPT_EXPORT void NB_Game_OnUpdate(float dt)
{
    float pos[3] = {0};
    NB_RT_GetNode3DPosition("PlayerRoot", pos);

#if defined(__DREAMCAST__)
    NB_KOS_PollInput();
    if (NB_KOS_HasController() && NB_KOS_ButtonDown(NB_BTN_DPAD_UP)) {
        pos[2] += 5.0f * dt;
    }
#endif

    NB_RT_SetNode3DPosition("PlayerRoot", pos[0], pos[1], pos[2]);
}
```

---

## Build/Link Notes

- Dreamcast Makefile should include script sources (e.g. `scripts/*.c`) so your hook symbols are linked.
- If controls work in editor but not Dreamcast, verify:
  1. script object is in link line,
  2. script exports `NB_Game_OnUpdate`,
  3. runtime is calling `NB_Game_OnUpdate(dt)` each frame,
  4. input path uses `NB_KOS_*` wrappers.

---

## Source-of-Truth Files

- Generator/runtime emit logic: `src/main.cpp`
- Runtime output used by build: `build_dreamcast/main.c`
- Scene/asset bindings: `src/platform/dreamcast/KosBindings.c/.h`
- Input bindings: `src/platform/dreamcast/KosInput.c/.h`
- Script examples/templates: `Scripts/`
