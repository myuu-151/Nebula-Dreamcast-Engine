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
```

- **CollisionBounds**: get/set the AABB half-extents (box size) of a Node3D's collision volume.
- **BoundPos**: get/set the local offset of the collision box relative to the node's origin.
- **PhysicsEnabled**: toggle gravity and floor collision per node.
- **VelocityY**: read/write vertical velocity (use `SetNode3DVelocityY` to apply jump impulse).
- **IsNode3DOnFloor**: returns 1 if the node is grounded (physics enabled, vertical velocity near zero).
- **CheckAABBOverlap**: returns 1 if two named Node3D collision boxes overlap (useful for hit detection, triggers).

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

---

## Dreamcast Input Binding API (`NB_KOS_*`)

Declared in `src/platform/dreamcast/KosInput.h`, implemented in `KosInput.c`.

```c
void  NB_KOS_PollInput(void);
int   NB_KOS_HasController(void);
float NB_KOS_GetStickX(void);
float NB_KOS_GetStickY(void);
float NB_KOS_GetLTrigger(void);
float NB_KOS_GetRTrigger(void);

int NB_KOS_ButtonDown(int btn);
int NB_KOS_ButtonPressed(int btn);
int NB_KOS_ButtonReleased(int btn);
```

Button IDs are `NB_BTN_*` enums/macros from `KosInput.h`.

### Why use wrappers instead of raw Maple bits

`NB_KOS_*` normalizes button semantics (including active-low behavior) and keeps script input consistent with runtime expectations.

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
