# Dreamcast Binding API

This document describes the runtime binding API used by Nebula gameplay scripts and the generated Dreamcast runtime code.

## Architecture

```
  Gameplay Scripts (Scripts/*.c)
      |
      | calls NB_RT_* functions
      v
  Generated Runtime (build_dreamcast/main.c)
      |
      | calls NB_DC_*, NB_KOS_* functions
      v
  Platform Bindings (KosBindings.c, KosInput.c)
      |
      | calls KallistiOS API
      v
  Dreamcast Hardware
```

| Layer | Location | Role |
|-------|----------|------|
| Script | `Scripts/*.c` | Gameplay logic. Implements `NB_Game_OnStart`, `NB_Game_OnUpdate`, `NB_Game_OnSceneSwitch`. Calls `NB_RT_*` bridge functions |
| Generated runtime | `build_dreamcast/main.c` | Implements `NB_RT_*` bridge functions. Calls `NB_DC_*` loaders/scene APIs and `NB_KOS_*` input APIs |
| Platform bindings | `src/platform/dreamcast/*.c` | `KosBindings.c/.h` (scene/mesh/texture loading, `NB_DC_*`), `KosInput.c/.h` (controller input, `NB_KOS_*`) |

## Script Hook API

These are the entry points your gameplay script should export:

```c
NB_SCRIPT_EXPORT void NB_Game_OnStart(void);
NB_SCRIPT_EXPORT void NB_Game_OnUpdate(float dt);
NB_SCRIPT_EXPORT void NB_Game_OnSceneSwitch(const char* sceneName);
```

| Hook | When Called |
|------|------------|
| `OnStart` | Once when runtime starts |
| `OnUpdate(dt)` | Every frame (`dt` = seconds since last frame, clamped to 0.1s max) |
| `OnSceneSwitch(name)` | After runtime switches to another scene |

`NebulaGameStub.c` provides weak fallback definitions so link succeeds even if no script defines them.

## Runtime Bridge API (`NB_RT_*`)

Used by scripts to control world/camera state. Implemented in generated runtime code (`main.c`) emitted by the editor. Script files only declare and call these symbols.

### Node/Mesh Transform

```c
void NB_RT_GetMeshPosition(float outPos[3]);
void NB_RT_SetMeshPosition(float x, float y, float z);
void NB_RT_AddMeshPositionDelta(float dx, float dy, float dz);
```

Legacy/simple mesh bridge (single-mesh compatibility path).

### Node3D Transform

```c
void NB_RT_GetNode3DPosition(const char* name, float outPos[3]);
void NB_RT_SetNode3DPosition(const char* name, float x, float y, float z);
void NB_RT_GetNode3DRotation(const char* name, float outRot[3]);
void NB_RT_SetNode3DRotation(const char* name, float x, float y, float z);
```

Script-side movement/rotation for named Node3D objects. The `name` parameter must match the Node3D name in the scene exactly (case-insensitive). Multiple scripts can run simultaneously — each Node3D with a unique `script` field gets its own DLL slot at runtime.

#### Name-based lookup

On Dreamcast, the generated runtime maintains a table of all Node3D nodes (`DcNode3D gNode3Ds[]`). Each bridge function resolves `name` at runtime:

- If `name` matches the player's parent Node3D (the one parenting the player StaticMesh3D), the call routes to player globals (`gMeshPos`, `gMeshRot`, etc.)
- Otherwise, `dc_find_node3d(name)` scans the node table and routes to that node's data

```c
// Control4.c — player controller
static const char* PLAYER_NODE = "PlayerRoot";
NB_RT_SetNode3DPosition(PLAYER_NODE, x, y, z);  // moves PlayerRoot

// airoam.c — AI roaming
static const char* AI_NODE = "AINode";
NB_RT_SetNode3DPosition(AI_NODE, x, y, z);       // moves AINode (not the player)
```

Node names are set in the editor and exported automatically. Scripts never need to know the internal index — just pass the name string.

#### Collision-source rotation behavior

When a Node3D has `collisionSource` enabled, the engine uses an internal quaternion for orientation and aligns the node to the ground surface normal (slope alignment):

| Operation | Behavior |
|-----------|----------|
| `SetNode3DRotation` | Only updates **yaw** (`y` parameter). Tilt from slope alignment is preserved — `x` and `z` are accepted but overridden each frame |
| `GetNode3DRotation` | Returns the script's last-set yaw in `outRot[1]`, not a quaternion extraction. Script always reads back exactly what it wrote |
| Rendering | Uses quaternion directly (converted to rotation matrix), bypassing Euler angles to avoid gimbal lock |

#### Node3D physics flags

Each Node3D has three independent collision/physics toggles:

| Flag | Behavior |
|------|----------|
| Simple Collision | Raycast ground snap only (no rotation change). Node sticks to floor but stays upright |
| Collision Source | Ground snap + slope alignment. Node tilts to match surface normal using internal quaternion |
| Gravity | Applies downward acceleration. Works with either collision toggle for falling behavior |

These flags work identically for all Node3Ds — player and non-player alike. An AI Node3D with `collisionSource` gets full slope alignment on both editor and Dreamcast. Engine gravity and ground snap always apply regardless of whether a script calls `SetNode3DPosition`.

### Camera

```c
void NB_RT_GetCameraOrbit(const char* name, float outOrbit[3]);
void NB_RT_SetCameraOrbit(const char* name, float x, float y, float z);
void NB_RT_GetCameraRotation(const char* name, float outRot[3]);
void NB_RT_SetCameraRotation(const char* name, float x, float y, float z);
void NB_RT_GetCameraWorldForward(const char* name, float outFwd[3]);
int  NB_RT_IsCameraUnderNode3D(const char* cameraName, const char* nodeName);
```

Used for orbit-style cameras, camera-relative movement, and linkage checks.

### Collision / Physics

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

| Function | Purpose |
|----------|---------|
| `CollisionBounds` | Get/set AABB half-extents (box size) of a Node3D's collision volume |
| `BoundPos` | Get/set local offset of the collision box relative to the node's origin |
| `PhysicsEnabled` | Toggle gravity per node |
| `CollisionSource` | Toggle slope alignment (ground snap + tilt to surface normal) per node |
| `SimpleCollision` | Toggle ground snap only (no slope alignment) per node |
| `VelocityY` | Read/write vertical velocity (use `SetNode3DVelocityY` for jump impulse) |
| `IsNode3DOnFloor` | Returns 1 if grounded (physics enabled, vertical velocity near zero) |
| `CheckAABBOverlap` | Returns 1 if two named Node3D bounding boxes overlap. Pure geometry test — no collision flags required. Uses position + `boundPos` + `extent`, scaled by node scale. On Dreamcast, extents are pre-scaled during export |
| `RaycastDown` | Casts vertical ray downward from (x,y,z) against collision-flagged StaticMesh3D triangles. Returns 1 if hit, writes Y of highest surface into `outHitY` |
| `RaycastDownWithNormal` | Same as `RaycastDown` but also returns the surface normal in `outNormal[3]`. Used by the engine for slope alignment |

### Engine-Level Physics (Automatic)

These behaviors run automatically each frame for Node3D nodes with the appropriate flags. Scripts do not call anything to activate them — they are configured via editor properties.

#### Wall collision (AABB vs mesh triangles)

When a Node3D has `collisionSource` or `simpleCollision` enabled, the engine tests its AABB against all triangles of every collision-source StaticMesh3D in the scene. Only wall-like triangles produce push-out.

| Step | Description |
|------|-------------|
| 1 | For each collision-source StaticMesh3D, iterate all triangles |
| 2 | Compute face normal. Skip if `abs(ny) > wallThreshold` (floor/ceiling) |
| 3 | Check Y overlap between AABB vertical range and triangle vertical range |
| 4 | Check XZ broadphase (AABB bbox vs triangle bbox) |
| 5 | Compute signed distance from AABB center to triangle plane |
| 6 | Compare against projected half-extent (`hx*|nx| + hy*|ny| + hz*|nz|`) |
| 7 | If penetrating, push node along the horizontal component of face normal |

**Wall threshold:** Each StaticMesh3D has a `wallThreshold` property (default 0.7, range 0.0–1.0) controlling the floor vs wall cutoff. ~0.7 corresponds to roughly 45 degrees. Lower values treat more surfaces as walls. Editable per-mesh in the inspector. On Dreamcast, baked into `kSceneWallThreshold` and `CollMeshCache.wallThresh`.

**Rotated geometry:** Collision works correctly with rotated StaticMesh3D objects (including 90° rotations to create walls from floor meshes). The engine handles axis remapping internally.

#### Node3D-vs-Node3D AABB push-apart

After per-node physics (gravity, ground snap, wall collision), the engine runs pairwise AABB overlap between all physics-enabled Node3Ds:

1. Compute overlap on X, Y, Z axes independently
2. Find smallest horizontal overlap axis (X or Z). If Y is smallest, skip
3. Push both nodes apart along that axis, splitting penetration 50/50

This allows characters to block each other. O(N²), fine for typical counts (2–5 physics Node3Ds). On Dreamcast, the player Node3D participates via `gMeshPos`.

### Scene Switching

```c
void NB_RT_NextScene(void);
void NB_RT_PrevScene(void);
void NB_RT_SwitchScene(const char* name);
```

| Function | Behavior |
|----------|----------|
| `NextScene` | Advance to next scene in loaded list (wraps). Deferred to end of frame — navmesh cleared/rebuilt, `OnSceneSwitch` called |
| `PrevScene` | Switch to previous scene (wraps). Same deferred semantics |
| `SwitchScene` | Switch to scene by human-readable name (case-insensitive). No-op if not found. In editor, only works for open tabs; on Dreamcast, all packaged scenes are available |

```c
// Debounced scene advance
static int sStartHeld = 0;
if (NB_KOS_ButtonDown(NB_BTN_START))
{
    if (!sStartHeld) { NB_RT_NextScene(); sStartHeld = 1; }
}
else { sStartHeld = 0; }

// Jump to specific scene
NB_RT_SwitchScene("MyLevel2");
```

#### Trigger zone pattern

Switch scene when player walks into a Node3D's bounds:

```c
static const char* PLAYER_NODE  = "PlayerRoot";
static const char* TRIGGER_NODE = "trigger";
static const char* TARGET_SCENE = "Level2";
static int sTriggered = 0;

NB_SCRIPT_EXPORT void NB_Game_OnStart(void)
{
    sTriggered = 0;
}

NB_SCRIPT_EXPORT void NB_Game_OnUpdate(float dt)
{
    (void)dt;
    if (sTriggered) return;
    if (NB_RT_CheckAABBOverlap(PLAYER_NODE, TRIGGER_NODE))
    {
        sTriggered = 1;
        NB_RT_SwitchScene(TARGET_SCENE);
    }
}

NB_SCRIPT_EXPORT void NB_Game_OnSceneSwitch(const char* sceneName)
{
    (void)sceneName;
    sTriggered = 0;  // reset so trigger works again if player returns
}
```

The trigger Node3D needs no collision flags — `CheckAABBOverlap` is a pure geometry test. Set bounds/scale in the editor to define the activation area.

### Animation Slots

```c
void NB_RT_PlayAnimation(const char* meshName, const char* animName);
void NB_RT_StopAnimation(const char* meshName);
int  NB_RT_IsAnimationPlaying(const char* meshName);
int  NB_RT_IsAnimationFinished(const char* meshName);
void NB_RT_SetAnimationSpeed(const char* meshName, float speed);
```

Named animation slots on StaticMesh3D nodes. Each StaticMesh3D can have up to 8 named slots, each pointing to a `.nebanim` file.

| Function | Behavior |
|----------|----------|
| `PlayAnimation(mesh, anim)` | Start playing named slot on the StaticMesh3D (case-insensitive match). Resets time to 0, uses slot's configured speed. On Dreamcast, clip is lazy-loaded from disc on first play |
| `StopAnimation(mesh)` | Stop playback. Last frame remains visible |
| `IsAnimationPlaying(mesh)` | Returns 1 if currently playing |
| `IsAnimationFinished(mesh)` | Returns 1 if past last frame. Looping: flag set but playback continues. Play-once: stops on last frame |
| `SetAnimationSpeed(mesh, speed)` | Override speed at runtime (0.0–2.0). Does not change saved editor value |

#### Editor slot setup

In the StaticMesh3D inspector, expand "Animation Slots" and click **+** to add slots:

| Property | Description |
|----------|-------------|
| Name | String scripts use to reference the animation (e.g. `"walk"`, `"wait"`) |
| Path | `.nebanim` file (click `>` to pick from project assets) |
| Speed | Playback speed multiplier (0.0–2.0, default 1.0). Persists to Dreamcast export |
| Loop | When checked (default), wraps continuously. Unchecked = play once, stop on last frame. Persists to export |
| Play/Stop | Preview button for editor viewport |

The StaticMesh3D must have **Runtime test** checked in the inspector for play-mode animation to work.

#### Usage example (AI roaming)

```c
static const char* AI_NODE = "AINode";
static const char* AI_MESH = "chao";
static int sIsWalking = 0;

NB_SCRIPT_EXPORT void NB_Game_OnStart(void)
{
    NB_RT_PlayAnimation(AI_MESH, "wait");
    sIsWalking = 0;
}

NB_SCRIPT_EXPORT void NB_Game_OnUpdate(float dt)
{
    // ... movement logic ...
    if (dist < ARRIVE_DIST)
    {
        NB_RT_PlayAnimation(AI_MESH, "wait");
        sIsWalking = 0;
        return;
    }
    if (!sIsWalking)
    {
        NB_RT_PlayAnimation(AI_MESH, "walk");
        sIsWalking = 1;
    }
}
```

#### Dreamcast export

Animation slots are baked into per-scene constant arrays (`kSceneAnimSlotCount`, `kSceneAnimSlotName`, `kSceneAnimSlotDisk`, `kSceneAnimSlotSpeed`). `.nebanim` files are staged as `Axxxxx` short names under `cd_root/data/animations/`. At runtime, `PlayAnimation` lazy-loads the disc file, freeing the previous clip (one active clip per mesh). Serialization is backward compatible — scenes saved before animation slots load with `animSlotCount = 0`.

### NavMesh

```c
int  NB_RT_NavMeshBuild(void);
void NB_RT_NavMeshClear(void);
int  NB_RT_NavMeshIsReady(void);
int  NB_RT_NavMeshFindPath(float sx, float sy, float sz, float gx, float gy, float gz, float* outPath, int maxPoints);
int  NB_RT_NavMeshFindRandomPoint(float outPos[3]);
int  NB_RT_NavMeshFindClosestPoint(float px, float py, float pz, float outPos[3]);
```

| Function | Behavior |
|----------|----------|
| `NavMeshBuild` | Build navmesh from StaticMesh3D geometry within NavMesh3D bounding volumes. Only triangles inside `navBounds` (and not inside `navNegator`) are included. Returns 1 on success |
| `NavMeshClear` | Free current navmesh data |
| `NavMeshIsReady` | Returns 1 if navmesh is built and available |
| `NavMeshFindPath` | Find path between two world-space points. Writes up to `maxPoints` xyz waypoints into `outPath`. Returns waypoint count, 0 if no path |
| `NavMeshFindRandomPoint` | Pick random navigable point on navmesh. Returns 1 on success |
| `NavMeshFindClosestPoint` | Project world position onto nearest navmesh surface. Returns 1 on success |

> On Dreamcast, navmesh queries use Detour compiled for SH4. The editor packages per-scene binaries (`NAV00001.BIN`, etc.) into `cd_root/data/navmesh/`. At runtime, `NavMeshBuild` loads the binary for the current scene and initializes Detour. Scene switches automatically clear and rebuild. The Detour node pool is reduced to 512 (vs 2048 on desktop) to fit DC memory.

## Dreamcast Scene/Asset API (`NB_DC_*`)

Declared in `src/platform/dreamcast/KosBindings.h`, implemented in `KosBindings.c`.

### Scene Lifecycle

```c
int  NB_DC_LoadScene(const char* scenePath);
void NB_DC_UnloadScene(void);
int  NB_DC_SwitchScene(const char* scenePath);
```

### Common Types

```c
typedef struct NB_Vec3 {
    float x, y, z;
} NB_Vec3;
```

### Mesh/Texture Loading

```c
typedef struct NB_Mesh {
    NB_Vec3*  pos;
    NB_Vec3*  tri_uv;
    NB_Vec3*  tri_uv1;       // second UV layer (v6+), NULL if absent
    uint16_t* indices;
    uint16_t* tri_mat;
    int vert_count;
    int tri_count;
    int uv_layer_count;      // total UV layers present (0, 1, or 2)
} NB_Mesh;

typedef struct NB_Texture {
    uint16_t* pixels;
    int w, h;
    float us, vs;
    int filter;
    int wrapMode;
    int flipU, flipV;
} NB_Texture;

int  NB_DC_LoadMesh(const char* meshPath, NB_Mesh* out);
int  NB_DC_LoadTexture(const char* texPath, NB_Texture* out);
void NB_DC_FreeMesh(NB_Mesh* m);
void NB_DC_FreeTexture(NB_Texture* t);
```

### Scene Metadata

```c
// Single-mesh (compatibility)
const char* NB_DC_GetSceneName(void);
const char* NB_DC_GetSceneMeshPath(void);
const char* NB_DC_GetSceneTexturePath(int slotIndex);
void        NB_DC_GetSceneTransform(float outPos[3], float outRot[3], float outScale[3]);

// Multi-mesh (modern)
int         NB_DC_GetSceneMeshCount(void);
const char* NB_DC_GetSceneMeshPathAt(int meshIndex);
const char* NB_DC_GetSceneTexturePathAt(int meshIndex, int slotIndex);
void        NB_DC_GetSceneTransformAt(int meshIndex, float outPos[3], float outRot[3], float outScale[3]);
```

Use `*At` variants for multi-StaticMesh scenes. Material properties (shade mode, light yaw/pitch, shadow intensity, shading UV layer, UV scale) are baked into per-scene 3D arrays indexed by `[sceneIndex][meshIndex][slotIndex]`.

### NavMesh Asset Loading

```c
int         NB_DC_LoadNavMesh(const char* navPath);
void        NB_DC_FreeNavMesh(void);
int         NB_DC_NavMeshIsLoaded(void);
const void* NB_DC_GetNavMeshData(int* outSize);
```

| Function | Purpose |
|----------|---------|
| `LoadNavMesh` | Load serialized navmesh binary (`.BIN`) from disc. Returns 1 on success |
| `FreeNavMesh` | Release loaded navmesh data |
| `NavMeshIsLoaded` | Returns 1 if navmesh data is in memory |
| `GetNavMeshData` | Returns pointer to raw navmesh blob and its size. Used by `DetourInit` |

### Detour Queries

Declared in `src/platform/dreamcast/DetourBridge.h`, implemented in `DetourBridge.cpp`.

```c
int  NB_DC_DetourInit(const void* navData, int navDataSize);
void NB_DC_DetourFree(void);
int  NB_DC_DetourIsReady(void);
int  NB_DC_DetourFindPath(float sx, float sy, float sz, float gx, float gy, float gz, float* outPath, int maxPoints);
int  NB_DC_DetourFindRandomPoint(float outPos[3]);
int  NB_DC_DetourFindClosestPoint(float px, float py, float pz, float outPos[3]);
```

| Function | Purpose |
|----------|---------|
| `DetourInit` | Create `dtNavMesh` + `dtNavMeshQuery` from raw blob. Node pool 512, max path poly 256 (DC memory). Returns 1 on success |
| `DetourFree` | Tear down Detour state |
| `DetourIsReady` | Returns 1 if initialized |
| `DetourFindPath` | Straight-line path between two points. Writes up to `maxPoints` packed xyz waypoints. Returns count, 0 if no path |
| `DetourFindRandomPoint` | Random navigable point. Returns 1 on success |
| `DetourFindClosestPoint` | Project position onto nearest navmesh surface. Returns 1 on success |

Scripts use `NB_RT_NavMesh*` and never call `NB_DC_Detour*` directly. Detour (6 source files from `thirdparty/recastnavigation/Detour/`) is cross-compiled for SH4 with `-fno-exceptions -fno-rtti`.

## Dreamcast Input API (`NB_KOS_*`)

Declared in `src/platform/dreamcast/KosInput.h`, implemented in `KosInput.c`.

### High-Level Input

```c
void  NB_KOS_InitInput(void);
void  NB_KOS_PollInput(void);
int   NB_KOS_HasController(void);
float NB_KOS_GetStickX(void);
float NB_KOS_GetStickY(void);
float NB_KOS_GetLTrigger(void);
float NB_KOS_GetRTrigger(void);
int   NB_KOS_ButtonDown(int btn);
int   NB_KOS_ButtonPressed(int btn);
int   NB_KOS_ButtonReleased(int btn);
uint32_t NB_KOS_GetRawButtons(void);
```

| Function | Purpose |
|----------|---------|
| `InitInput` | One-time init (called before main loop) |
| `PollInput` | Read controller state — call once per frame before queries |
| `HasController` | Returns 1 if controller connected on port 0 |
| `GetStickX / GetStickY` | Analog stick, normalized ~-1.0 to +1.0 |
| `GetLTrigger / GetRTrigger` | Analog triggers, 0.0 to 1.0 |
| `ButtonDown` | Returns 1 while button is held |
| `ButtonPressed` | Returns 1 on the frame button was first pressed |
| `ButtonReleased` | Returns 1 on the frame button was released |
| `GetRawButtons` | Raw Maple button bitmask (advanced use) |

### Button IDs

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

### Low-Level Pad Access

```c
typedef struct NB_KOS_RawPadState {
    int      has_controller;
    uint32_t buttons;
    int8_t   stick_x, stick_y;
    uint8_t  l_trigger, r_trigger;
} NB_KOS_RawPadState;

void NB_KOS_BindingsInit(void);
void NB_KOS_BindingsRead(NB_KOS_RawPadState* outState);
```

Raw Maple layer underneath `NB_KOS_*`. Most scripts should use the high-level wrappers instead — they normalize button semantics (including active-low behavior) and keep input consistent with runtime expectations.

## Runtime Timing

The generated Dreamcast runtime uses `timer_us_gettime64()` for real delta time each frame. `dt` passed to `OnUpdate` reflects actual elapsed seconds (clamped to 0.1s max). The render loop syncs to VBlank at 60 Hz (NTSC) via `pvr_wait_ready()`.

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

## Build/Link Notes

- Dreamcast Makefile should include script sources (`scripts/*.c`) so hook symbols are linked
- If controls work in editor but not Dreamcast, verify:
  1. Script object is in link line
  2. Script exports `NB_Game_OnUpdate`
  3. Runtime calls `NB_Game_OnUpdate(dt)` each frame
  4. Input path uses `NB_KOS_*` wrappers

## Source-of-Truth Files

| File | Role |
|------|------|
| `src/platform/dreamcast/dc_codegen.cpp` | Generator/runtime emit logic |
| `build_dreamcast/main.c` | Runtime output used by build |
| `src/platform/dreamcast/KosBindings.c/.h` | Scene/asset bindings |
| `src/platform/dreamcast/KosInput.c/.h` | Input bindings |
| `Scripts/` | Script examples/templates |
