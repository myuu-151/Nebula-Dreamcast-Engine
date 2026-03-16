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

Script-side movement/rotation for named Node3D objects. The `name` parameter must match the Node3D name in the scene exactly (case-insensitive). Multiple scripts can run simultaneously — each Node3D with a unique `script` field gets its own DLL slot at runtime.

**Multi-node name-based lookup:** On Dreamcast, the generated runtime maintains a table of all Node3D nodes from the scene (`DcNode3D gNode3Ds[]`). Each bridge function resolves the `name` parameter at runtime:
- If `name` matches the player's parent Node3D (the Node3D that parents the player StaticMesh3D), the call routes to the player globals (`gMeshPos`, `gMeshRot`, etc.) which drive rendering, physics, and camera.
- Otherwise, `dc_find_node3d(name)` scans the node table and routes to that node's data. This allows AI scripts, camera pivots, and other non-player Node3Ds to be controlled independently.

**Example — two scripts controlling different nodes:**

```c
// Control4.c — player controller
static const char* PLAYER_NODE = "PlayerRoot";
NB_RT_SetNode3DPosition(PLAYER_NODE, x, y, z);  // moves PlayerRoot

// airoam.c — AI roaming
static const char* AI_NODE = "AINode";
NB_RT_SetNode3DPosition(AI_NODE, x, y, z);       // moves AINode (not the player)
```

Node names are set in the editor and exported into the generated runtime automatically. Scripts never need to know the internal index — just pass the name string.

**Collision-source rotation behavior:** When a Node3D has `collisionSource` enabled, the engine uses an internal quaternion for orientation and automatically aligns the node to the ground surface normal (slope alignment). In this mode:
- `SetNode3DRotation` only updates **yaw** (the `y` parameter). Tilt (pitch/roll from slope alignment) is preserved automatically — the `x` and `z` parameters are accepted but slope alignment overrides them each frame.
- `GetNode3DRotation` returns the script's last-set yaw in `outRot[1]`, not a value extracted from the internal quaternion. This ensures the script always reads back exactly what it wrote, with no drift or snapping at extreme angles.
- Rendering uses the quaternion directly (converted to a rotation matrix), bypassing Euler angles entirely to avoid gimbal lock.

**Node3D physics toggles:** Each Node3D has three independent collision/physics flags:
- **Simple Collision** — raycast ground snap only (no rotation change). The node sticks to the floor but stays upright.
- **Collision Source** — ground snap + slope alignment. The node tilts to match the surface normal.
- **Gravity** — applies downward acceleration. Works alongside either collision toggle for falling behavior. Scripts that implement their own gravity (e.g. with `RaycastDown`) can leave this off.

Engine gravity and ground snap always apply regardless of whether a script calls `SetNode3DPosition` — there is no "script-managed" override.

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
int   NB_RT_GetNode3DCollisionSource(const char* name);
void  NB_RT_SetNode3DCollisionSource(const char* name, int enabled);
int   NB_RT_GetNode3DSimpleCollision(const char* name);
void  NB_RT_SetNode3DSimpleCollision(const char* name, int enabled);
float NB_RT_GetNode3DVelocityY(const char* name);
void  NB_RT_SetNode3DVelocityY(const char* name, float vy);
int   NB_RT_IsNode3DOnFloor(const char* name);
int   NB_RT_CheckAABBOverlap(const char* name1, const char* name2);
int   NB_RT_RaycastDown(float x, float y, float z, float* outHitY);
int   NB_RT_RaycastDownWithNormal(float x, float y, float z, float* outHitY, float outNormal[3]);
```

- **CollisionBounds**: get/set the AABB half-extents (box size) of a Node3D's collision volume.
- **BoundPos**: get/set the local offset of the collision box relative to the node's origin.
- **PhysicsEnabled**: toggle gravity per node.
- **CollisionSource**: toggle slope alignment (ground snap + tilt to surface normal) per node.
- **SimpleCollision**: toggle ground snap only (no slope alignment) per node.
- **VelocityY**: read/write vertical velocity (use `SetNode3DVelocityY` to apply jump impulse).
- **IsNode3DOnFloor**: returns 1 if the node is grounded (physics enabled, vertical velocity near zero).
- **CheckAABBOverlap**: returns 1 if two named Node3D bounding boxes overlap (pure geometry test — does **not** require any collision flags enabled). Useful for hit detection, trigger zones, and scene-switch triggers. The test uses each node's position + `boundPos` offset + `extent` half-extents.
- **RaycastDown**: casts a vertical ray downward from (x,y,z) against collision-flagged StaticMesh3D triangles. Returns 1 if hit, writing the Y coordinate of the highest surface below the ray origin into `outHitY`. Useful for ground snapping.
- **RaycastDownWithNormal**: same as `RaycastDown` but also returns the surface normal of the hit triangle in `outNormal[3]`. Used by the engine for slope alignment; scripts can use it for custom orientation logic.

### Engine-level physics (automatic, not script-callable)

The following physics behaviors run automatically each frame for Node3D nodes with the appropriate flags enabled. Scripts do not need to call anything to activate them — they are configured via editor properties.

#### Wall collision (AABB vs mesh triangles)

When a Node3D has `collisionSource` or `simpleCollision` enabled, the engine tests its AABB against all triangles of every `collisionSource`-flagged StaticMesh3D in the scene. Triangles whose face normal is mostly vertical (floor/ceiling) are skipped; only wall-like triangles produce a horizontal push-out.

**Algorithm:**
1. For each collision-source StaticMesh3D, iterate all triangles.
2. Compute the face normal. Skip if `abs(ny) > wallThreshold` (floor/ceiling).
3. Check Y overlap between the AABB vertical range and the triangle's vertical range.
4. Check XZ broadphase (AABB bbox vs triangle bbox).
5. Compute signed distance from AABB center to the triangle plane.
6. Compare against projected AABB half-extent (`hx*|nx| + hy*|ny| + hz*|nz|`).
7. If penetrating, push the node along the horizontal component of the face normal.

**Per-mesh wall threshold:** Each StaticMesh3D has a `wallThreshold` property (default 0.7, range 0.0–1.0) that controls the floor/ceiling vs wall cutoff angle. A threshold of 0.7 corresponds to roughly 45 degrees — triangles steeper than this are treated as walls. Lower values treat more surfaces as walls; higher values treat more as floors. This is editable per-mesh in the editor inspector (shown when `Collision Source` is checked).

On Dreamcast, the wall threshold is baked into a per-scene per-mesh array (`kSceneWallThreshold`) and stored in the collision mesh cache (`CollMeshCache.wallThresh`).

#### Node3D-vs-Node3D AABB push-apart

After all per-node physics (gravity, ground snap, wall collision), the engine runs a pairwise AABB overlap test between all physics-enabled Node3D nodes. If two AABBs overlap:
1. Compute overlap on X, Y, and Z axes independently.
2. Find the smallest horizontal overlap axis (X or Z). If Y is smallest, skip (vertical overlap doesn't push).
3. Push both nodes apart along that axis, splitting the penetration 50/50.

This allows characters to block each other (e.g., player blocking AI, or AI nodes colliding with each other). The check is O(N^2) which is fine for typical scene counts (2–5 physics Node3Ds).

On Dreamcast, the player Node3D (identified via `kSceneMeshParentN3D`) participates in the same pairwise check, with push applied to `gMeshPos` (the player rendering position).

### Scene switching bridge

```c
void NB_RT_NextScene(void);
void NB_RT_PrevScene(void);
void NB_RT_SwitchScene(const char* name);
```

- **NextScene**: advances to the next scene in the loaded scene list (wraps around). The switch happens at the end of the current frame — navmesh is automatically cleared and rebuilt for the new scene, and `NB_Game_OnSceneSwitch` is called.
- **PrevScene**: switches to the previous scene (wraps around). Same deferred semantics as `NextScene`.
- **SwitchScene**: switches to a specific scene by its human-readable name (case-insensitive match). Same deferred semantics — the switch happens at end of frame with navmesh rebuild and `OnSceneSwitch` callback. If no scene matches the name, the call is a no-op.

**Usage pattern** (with debounce to avoid rapid cycling):

```c
static int sStartHeld = 0;

// Dreamcast
if (NB_KOS_ButtonDown(NB_BTN_START))
{
    if (!sStartHeld) { NB_RT_NextScene(); sStartHeld = 1; }
}
else { sStartHeld = 0; }

// Windows
if (GetAsyncKeyState(VK_RETURN) & 0x8000)
{
    if (!sStartHeld) { NB_RT_NextScene(); sStartHeld = 1; }
}
else { sStartHeld = 0; }

// Jump to a specific scene by name
NB_RT_SwitchScene("MyLevel2");
```

### NavMesh bridge

```c
int  NB_RT_NavMeshBuild(void);
void NB_RT_NavMeshClear(void);
int  NB_RT_NavMeshIsReady(void);
int  NB_RT_NavMeshFindPath(float sx, float sy, float sz, float gx, float gy, float gz, float* outPath, int maxPoints);
int  NB_RT_NavMeshFindRandomPoint(float outPos[3]);
int  NB_RT_NavMeshFindClosestPoint(float px, float py, float pz, float outPos[3]);
```

- **NavMeshBuild**: builds the navmesh from StaticMesh3D geometry within NavMesh3D bounding volumes. Only triangles with at least one vertex inside a `navBounds` volume (and not inside a `navNegator` volume) are included. Returns 1 on success.
- **NavMeshClear**: frees the current navmesh data.
- **NavMeshIsReady**: returns 1 if a navmesh has been built and is available for queries.
- **NavMeshFindPath**: finds a path between two world-space points. Writes up to `maxPoints` waypoints into `outPath` (packed xyz). Returns the number of waypoints, or 0 if no path found.
- **NavMeshFindRandomPoint**: picks a random navigable point on the navmesh. Returns 1 on success.
- **NavMeshFindClosestPoint**: projects a world position onto the nearest navmesh surface. Returns 1 on success.

> **Note:** On Dreamcast, navmesh queries use Detour compiled for SH4. The editor builds and packages per-scene navmesh binaries (`NAV00001.BIN`, `NAV00002.BIN`, etc.) during export into `cd_root/data/navmesh/`. At runtime, `NB_RT_NavMeshBuild` loads the binary matching the current scene index from disc and initializes Detour for pathfinding. When switching scenes, the navmesh is automatically cleared and rebuilt for the new scene. The Detour node pool is reduced to 512 (vs 2048 on desktop) to fit DC memory constraints.

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
- **GetNavMeshData**: returns a pointer to the raw navmesh blob and its size. Used by `NB_DC_DetourInit` to create the Detour query objects.

The editor automatically builds and packages per-scene navmesh binaries (`NAVxxxxx.BIN`) into `cd_root/data/navmesh/` during Dreamcast export.

### Detour navmesh queries

Declared in `src/platform/dreamcast/DetourBridge.h`, implemented in `DetourBridge.cpp`.

```c
int  NB_DC_DetourInit(const void* navData, int navDataSize);
void NB_DC_DetourFree(void);
int  NB_DC_DetourIsReady(void);
int  NB_DC_DetourFindPath(float sx, float sy, float sz, float gx, float gy, float gz, float* outPath, int maxPoints);
int  NB_DC_DetourFindRandomPoint(float outPos[3]);
int  NB_DC_DetourFindClosestPoint(float px, float py, float pz, float outPos[3]);
```

- **DetourInit**: creates a `dtNavMesh` and `dtNavMeshQuery` from a raw navmesh blob (the bytes loaded by `NB_DC_LoadNavMesh`). Uses a reduced node pool of 512 and max path poly cap of 256 to fit DC memory. Returns 1 on success.
- **DetourFree**: tears down the Detour state.
- **DetourIsReady**: returns 1 if Detour is initialized and ready for queries.
- **DetourFindPath**: finds a straight-line path between two world-space points. Writes up to `maxPoints` packed xyz waypoints into `outPath`. Returns the number of waypoints, or 0 if no path found.
- **DetourFindRandomPoint**: picks a random navigable point on the navmesh. Returns 1 on success.
- **DetourFindClosestPoint**: projects a world position onto the nearest navmesh surface. Returns 1 on success.

The generated Dreamcast runtime calls these automatically — scripts use `NB_RT_NavMesh*` and never call `NB_DC_Detour*` directly. The Detour library (6 source files from `thirdparty/recastnavigation/Detour/`) is cross-compiled for SH4 with `-fno-exceptions -fno-rtti` and linked via `kos-c++`.

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
