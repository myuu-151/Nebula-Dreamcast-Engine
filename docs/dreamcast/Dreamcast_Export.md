# Dreamcast Export Pipeline

This document explains how Nebula Dreamcast Engine exports a project from the desktop editor into a bootable Dreamcast disc image. It covers the runtime architecture, the code generation process, asset staging, platform bindings, input handling, data types, navigation mesh support, and the final build flow.

Source files referenced throughout live under `src/platform/dreamcast/`.

---

## 1. Three-Layer Runtime Architecture

The Dreamcast runtime is organized into three layers. Each layer only calls downward, never upward, which keeps gameplay code portable and the platform layer reusable across projects.

### Layer 1: Script Layer (gameplay)

Gameplay scripts are plain C files that live in your project's `Scripts/` directory. They implement three entry points that the generated runtime calls:

- `NB_Game_OnStart()` -- called once when a scene loads.
- `NB_Game_OnUpdate()` -- called every frame.
- `NB_Game_OnSceneSwitch()` -- called when transitioning between scenes.

Scripts interact with the engine exclusively through `NB_RT_*` bridge functions (e.g. `NB_RT_SetMeshPosition`, `NB_RT_PlayAnimation`, `NB_RT_NavMeshFindPath`). They never include KOS headers or call hardware APIs directly.

### Layer 2: Generated Runtime (`build_dreamcast/main.c`)

The editor generates `main.c` at export time. This file:

- Provides concrete implementations of every `NB_RT_*` function that scripts call.
- Manages scene state: loads scenes via `NB_DC_LoadScene`, iterates meshes/textures, maintains per-node transform arrays, runs collision/physics each frame.
- Calls `NB_DC_*` platform functions (layer 3) for asset loading and `NB_KOS_*` for input.
- Contains the `main()` entry point that initializes PVR, loads the default scene, and enters the game loop.

Because `main.c` is machine-generated, you should never hand-edit it. Changes belong in the editor's code generation logic (`dc_codegen.cpp`).

### Layer 3: Platform Bindings (`KosBindings.c`, `KosInput.c`, `DetourBridge.cpp`)

The lowest layer talks directly to KOS (Kallistios) and the Dreamcast hardware. It provides:

- `NB_DC_*` functions for scene, mesh, texture, and navmesh loading.
- `NB_KOS_*` functions for controller input.
- `NB_DC_Detour*` functions for navmesh pathfinding.

This layer is compiled from hand-written source files that ship with the engine. It does not change per-project.

---

## 2. GenerateDreamcastPackage Workflow

The entire export is driven by a single function, `GenerateDreamcastPackage()`, defined in `src/platform/dreamcast/dc_codegen.cpp` (header: `dc_codegen.h`). Here is what it does, step by step.

### 2.1 Validate Project

The function checks that a project directory is set (`gProjectDir`). If not, it prints an error and returns. It also validates the DreamSDK path from editor preferences (`gPrefDreamSdkHome`), defaulting to `C:\DreamSDK` if unset.

### 2.2 Create `build_dreamcast/` Directory

A `build_dreamcast/` directory is created (or reused) inside the project folder. All generated files, staged assets, and build artifacts go here.

### 2.3 Generate the Build Script

A Windows batch file `_nebula_build_dreamcast.bat` is written. This script:

- Sets environment variables for the KOS toolchain (`KOS_BASE`, `KOS_CC_BASE`, `PATH` additions for `sh-elf-*` tools).
- Invokes `make` against `Makefile.dreamcast`.
- Runs `sh-elf-objcopy` to produce a flat binary from the ELF.
- Scrambles the binary with KOS's `scramble` tool to produce `1ST_READ.BIN`.
- Generates `IP.BIN` (Dreamcast disc header) via `makeip`.
- Creates an ISO with `mkisofs` from the `cd_root/` directory.
- Converts the ISO to a `.cdi` disc image with `cdi4dc`.

### 2.4 Copy Scripts

The function walks `<Project>/Scripts/` recursively and copies every `.c` file into `build_dreamcast/scripts/`, preserving subdirectory structure. C++ files (`.cpp`, `.cc`, `.cxx`) are intentionally skipped -- gameplay scripts must be plain C. The relative paths are collected for inclusion in the Makefile.

### 2.5 Resolve Camera and Primary Mesh

The export logic loads the project's default scene (configured in the `.nebproj` file) and selects:

- **Camera**: Prefers the main camera with the highest priority, then any highest-priority camera, then a legacy-named camera (`Camera3D1`, etc.). Falls back to a default position if no camera exists.
- **Primary mesh**: Prefers a StaticMesh3D parented under a Node3D (typically the player mesh), then the first non-cube mesh, then the first mesh.

Camera world transforms are resolved by walking the parent chain, applying rotation and translation at each ancestor, mirroring the editor's play-mode logic.

### 2.6 Build Vertex/Index/UV/Material Arrays

For the primary mesh, the exporter reads the `.nebmesh` binary and builds runtime arrays:

- Vertex positions (`Vec3`).
- UV coordinates per vertex or per-face (face records override per-vertex UVs when present).
- Index buffer (`uint16_t`).
- Per-triangle material slot IDs (`uint16_t`), used to select which texture slot to bind during rendering.

Quads in face records are triangulated with parity correction.

### 2.7 Generate Output Files

The function writes four key files:

| File | Purpose |
|---|---|
| `main.c` | Generated runtime: `NB_RT_*` implementations, game loop, PVR setup |
| `entry.c` | Minimal KOS entry point that calls `main()` |
| `Makefile.dreamcast` | Build rules for `sh-elf-gcc`/`sh-elf-g++`, links KOS libs, includes script sources |
| `NebulaGameStub.c` | Weak-symbol stubs for `NB_Game_OnStart/OnUpdate/OnSceneSwitch` so the build links even without scripts |

### 2.8 Stage Assets to `cd_root/data/`

All project assets are copied into `cd_root/data/` with deterministic short names (see section 3). Texture extension chunks (filter, wrap, flip) are appended during staging. Collision detection ensures no two assets map to the same short name.

---

## 3. Asset Staging -- Deterministic Short Names

The Dreamcast filesystem (ISO 9660) works best with short, uppercase filenames. The exporter assigns each asset a deterministic name using a prefix letter and a five-digit ordinal:

| Asset type | Prefix | Example | Subdirectory |
|---|---|---|---|
| Scene | `S` | `S00001.NEBSCENE` | `cd_root/data/scenes/` |
| Mesh | `M` | `M00001.NEBMESH` | `cd_root/data/meshes/` |
| Texture | `T` | `T00001.NEBTEX` | `cd_root/data/textures/` |
| Material | `MAT` | `MAT00001.NEBMAT` | `cd_root/data/materials/` |
| Animation | `A` | `A00001.NEBANIM` | `cd_root/data/animations/` |
| NavMesh | `NAV` | `NAV00001.BIN` | `cd_root/data/navmesh/` |
| VMU data | -- | -- | `cd_root/data/vmu/` |

The naming function (`stageShortDiskNameFromAbsKey`) takes the asset's original file extension, uppercases it, and prepends the prefix + ordinal:

```
prefix + "%05d" % ordinal + UPPERCASE(extension)
```

Ordinals start at 1 and increment per asset type. The generated `main.c` contains the mapping from logical asset names to these staged filenames, so the runtime knows which short name to load.

If two different source files would produce the same short name (a collision), the export aborts with an error message in the build log.

---

## 4. KosBindings -- Platform Asset Loading

**Files:** `src/platform/dreamcast/KosBindings.h`, `src/platform/dreamcast/KosBindings.c`

KosBindings implements the `NB_DC_*` platform layer and provides weak fallback stubs for all `NB_RT_*` functions.

### 4.1 Scene Loading

`NB_DC_LoadScene(const char* scenePath)` opens a `.nebscene` text file and parses it line by line:

- `scene=<name>` sets the scene name.
- `staticmesh <meshPath> <px> <py> <pz> <rx> <ry> <rz> <sx> <sy> <sz> <tex0> <tex1> ... <tex15>` defines a mesh instance with position, rotation, scale, and up to 16 texture slot paths. A `-` token means "no texture in this slot."

The parser tokenizes each line on whitespace. A `staticmesh` line must have at least 25 tokens (keyword + mesh path + 9 transform floats + texture slots to parse).

Scene state is stored in a static `NB_DC_SceneState` struct that holds up to 64 meshes (`NB_DC_MAX_SCENE_MESHES = 64`), each with up to 16 texture slots (`NB_DC_MAX_TEXTURE_SLOTS = 16`). Each mesh entry (`NB_DC_SceneMeshState`) stores:

- `mesh_path[128]` -- path to the `.nebmesh` file on disc.
- `texture_paths[16][128]` -- paths to texture files for each material slot.
- `pos[3]`, `rot[3]`, `scale[3]` -- transform.

`NB_DC_SwitchScene` unloads the current scene and loads a new one. `NB_DC_UnloadScene` zeroes the global state.

Query functions (`NB_DC_GetSceneMeshCount`, `NB_DC_GetSceneMeshPathAt`, `NB_DC_GetSceneTexturePathAt`, `NB_DC_GetSceneTransformAt`) let the generated runtime iterate all meshes and their textures.

### 4.2 Mesh Loading

`NB_DC_LoadMesh(const char* meshPath, NB_Mesh* out)` reads a `.nebmesh` binary file. The format is big-endian:

1. **Magic**: 4 bytes, `NEBM`.
2. **Header**: version (u32), flags (u32), vertex count (u32), index count (u32), position fraction bits (u32).
3. **Vertices**: each vertex is 3x `int16_t` (x, y, z). Dequantized by multiplying by `1.0 / (1 << fractionBits)`.
4. **UV layer 0** (if `flags & 1`): per-vertex `int16_t` pairs (u, v), divided by 256.0 to get float UVs.
5. **UV layer 1** (if `flags & 16`): same format as UV layer 0.
6. **Indices**: `uint16_t` array.
7. **Per-triangle material IDs** (if `flags & 2`): `uint16_t` per triangle.

After reading, the loader expands per-vertex UVs into per-index (per-corner) UVs in `tri_uv` and `tri_uv1` arrays, so the renderer can index them directly by triangle corner.

Limits: vertex count <= 65535, index count <= 262144.

### 4.3 Texture Loading

`NB_DC_LoadTexture(const char* texPath, NB_Texture* out)` reads a `.nebtex` binary file:

1. **Magic**: 4 bytes, `NEBT`.
2. **Header**: width (u16), height (u16), format (u16, must be 1 = RGB555), flags (u16).
3. **Pixel data**: `width * height` big-endian `uint16_t` values in RGB555 format.
4. **Extension chunk** (optional, 4 bytes): filter mode, wrap mode, flipU, flipV.

The loader converts RGB555 to RGB565 on the fly:

```
R5 = (pixel >> 10) & 31
G5 = (pixel >> 5) & 31
B5 = pixel & 31
G6 = (G5 << 1) | (G5 >> 4)      // expand 5-bit green to 6-bit
RGB565 = (R5 << 11) | (G6 << 5) | B5
```

Non-power-of-two textures are padded to the next power of two (up to 1024). The `us` and `vs` fields in `NB_Texture` store the UV scale factor (`original_dimension / padded_dimension`) so the renderer can adjust UVs to avoid sampling the padding.

### 4.4 NavMesh Loading

`NB_DC_LoadNavMesh(const char* navPath)` reads a raw binary blob (the serialized Detour navmesh) into a heap buffer. The data is later passed to `NB_DC_DetourInit()` for pathfinding initialization.

### 4.5 Weak NB_RT_* Stubs

KosBindings.c provides `__attribute__((weak))` stubs for every `NB_RT_*` function. These are no-ops or return safe defaults (zero vectors, `0` for booleans). They exist so that the platform library links successfully even if the generated `main.c` is absent or incomplete. When `main.c` is present, its strong definitions override the weak stubs.

Categories of weak stubs:
- Transform: `NB_RT_GetNode3DPosition`, `NB_RT_SetNode3DPosition`, rotation, camera orbit/rotation/forward.
- Collision/physics: bounds, bound position, physics enable, collision source, simple collision, velocity, floor check, AABB overlap, raycast.
- Animation: play, stop, is-playing, is-finished, speed.
- NavMesh: build, clear, is-ready, find-path, random-point, closest-point.

---

## 5. KosInput -- Controller Input

**Files:** `src/platform/dreamcast/KosInput.h`, `src/platform/dreamcast/KosInput.c`

KosInput wraps the raw Maple controller API into a clean per-frame input system.

### 5.1 Setup

Call `NB_KOS_InitInput()` once at startup. This initializes the Maple binding layer and takes an initial controller snapshot so the first frame does not produce spurious "pressed" events.

Call `NB_KOS_PollInput()` once per frame, before any input queries. This snapshots the previous frame's state and reads the current controller state via `NB_KOS_BindingsRead()`, which enumerates the first Maple controller (`maple_enum_type(0, MAPLE_FUNC_CONTROLLER)`) and copies its button, stick, and trigger values into an `NB_KOS_RawPadState`.

### 5.2 Button Queries

Three functions provide digital button state:

| Function | Returns true when... |
|---|---|
| `NB_KOS_ButtonDown(mask)` | Button is held this frame |
| `NB_KOS_ButtonPressed(mask)` | Button was just pressed (not held last frame) |
| `NB_KOS_ButtonReleased(mask)` | Button was just released (held last frame, not this frame) |

Edge detection compares `gCurrState.buttons` against `gPrevState.buttons`.

### 5.3 Button Macros

Button masks map directly to KOS `CONT_*` constants:

```c
#define NB_BTN_A       CONT_A
#define NB_BTN_B       CONT_B
#define NB_BTN_X       CONT_X
#define NB_BTN_Y       CONT_Y
#define NB_BTN_START   CONT_START
#define NB_BTN_DPAD_UP    CONT_DPAD_UP
#define NB_BTN_DPAD_DOWN  CONT_DPAD_DOWN
#define NB_BTN_DPAD_LEFT  CONT_DPAD_LEFT
#define NB_BTN_DPAD_RIGHT CONT_DPAD_RIGHT
#define NB_BTN_Z       CONT_Z
#define NB_BTN_D       CONT_D
```

### 5.4 Analog Input

- `NB_KOS_GetStickX()` / `NB_KOS_GetStickY()`: Normalize the raw `int8_t` stick value by dividing by 127.0 and clamping to [-1.0, 1.0].
- `NB_KOS_GetLTrigger()` / `NB_KOS_GetRTrigger()`: Normalize the raw `uint8_t` trigger value by dividing by 255.0, producing a [0.0, 1.0] range.

### 5.5 Utility

- `NB_KOS_HasController()`: Returns 1 if a controller was detected on the last poll.
- `NB_KOS_GetRawButtons()`: Returns the raw `uint32_t` button bitmask for advanced use.

---

## 6. Data Types

These structs are defined in `KosBindings.h` and used across all three runtime layers.

### NB_Vec3

```c
typedef struct NB_Vec3 {
    float x, y, z;
} NB_Vec3;
```

General-purpose 3D vector. Used for vertex positions and UV coordinates (z is typically unused for UVs).

### NB_Mesh

```c
typedef struct NB_Mesh {
    NB_Vec3*  pos;        // vertex positions [vert_count]
    NB_Vec3*  tri_uv;     // per-corner UVs, layer 0 [tri_count * 3]
    NB_Vec3*  tri_uv1;    // per-corner UVs, layer 1 (NULL if absent)
    uint16_t* indices;    // triangle index buffer [tri_count * 3]
    uint16_t* tri_mat;    // per-triangle material slot ID [tri_count]
    int vert_count;
    int tri_count;
    int uv_layer_count;   // 0, 1, or 2
} NB_Mesh;
```

Loaded by `NB_DC_LoadMesh`. The `tri_mat` array tells the renderer which texture slot to bind for each triangle, enabling multi-material meshes.

### NB_Texture

```c
typedef struct NB_Texture {
    uint16_t* pixels;   // RGB565 pixel data [w * h]
    int w, h;           // power-of-two dimensions
    float us, vs;       // UV scale: original_size / padded_size
    int filter;         // 0 = nearest, 1 = bilinear
    int wrapMode;       // wrap mode ID
    int flipU, flipV;   // UV flip flags
} NB_Texture;
```

Loaded by `NB_DC_LoadTexture`. Dimensions are always power-of-two. When the source image is NPOT, the texture is padded and `us`/`vs` are set to less than 1.0 so UVs stay within the valid region.

### NB_KOS_RawPadState

```c
typedef struct NB_KOS_RawPadState {
    int      has_controller;
    uint32_t buttons;
    int8_t   stick_x, stick_y;
    uint8_t  l_trigger, r_trigger;
} NB_KOS_RawPadState;
```

Raw Maple controller snapshot. Populated by `NB_KOS_BindingsRead`, consumed by KosInput's normalization functions.

---

## 7. DetourBridge -- NavMesh Pathfinding

**Files:** `src/platform/dreamcast/DetourBridge.h`, `src/platform/dreamcast/DetourBridge.cpp`

DetourBridge is a C-callable wrapper around the Recast/Detour library, compiled with `sh-elf-g++` but exposing all functions as `extern "C"`.

### 7.1 Initialization

`NB_DC_DetourInit(const void* navData, int navDataSize)` takes the raw navmesh binary (loaded by `NB_DC_LoadNavMesh`) and:

1. Allocates a `dtAlloc` copy of the data (Detour takes ownership and will `dtFree` it).
2. Creates a `dtNavMesh` via `dtAllocNavMesh()` and initializes it with `DT_TILE_FREE_DATA`.
3. Creates a `dtNavMeshQuery` via `dtAllocNavMeshQuery()`.
4. Initializes the query with a node pool of 512 (`kMaxNodePool`), reduced from the default 2048 to fit in Dreamcast's 16 MB RAM.

Returns 1 on success, 0 on failure. Cleans up partially-created objects on failure.

### 7.2 Pathfinding

`NB_DC_DetourFindPath(sx, sy, sz, gx, gy, gz, outPath, maxPoints)`:

1. Finds the nearest navmesh polygons to the start and goal positions using a half-extent search box of (2.0, 4.0, 2.0).
2. Computes a polygon corridor with `findPath()`, limited to 256 polygons (`kMaxPathPolys`).
3. Converts the corridor to straight-line waypoints with `findStraightPath()`.
4. Copies up to `maxPoints` waypoints into `outPath` as packed xyz floats.
5. Returns the number of waypoints, or 0 if no path was found.

### 7.3 Random Point and Closest Point

- `NB_DC_DetourFindRandomPoint(outPos)`: Picks a random navigable point using a custom xorshift32 PRNG (KOS does not provide `<random>`). The PRNG state is seeded at 1 and advances with each call.
- `NB_DC_DetourFindClosestPoint(px, py, pz, outPos)`: Projects a world position onto the nearest navmesh surface.

### 7.4 Asset Format

The navmesh binary is exported as `NAV00001.BIN` in `cd_root/data/navmesh/`. It contains the serialized `dtNavMesh` tile data produced by Recast during the editor's offline bake.

### 7.5 Cleanup

`NB_DC_DetourFree()` frees both the `dtNavMeshQuery` and `dtNavMesh`. `NB_DC_DetourIsReady()` returns whether both objects exist.

---

## 8. Build Helpers

**Files:** `src/platform/dreamcast/build_helpers.h`, `src/platform/dreamcast/build_helpers.cpp`

The `NebulaDreamcastBuild` namespace provides utility functions used during packaging.

### RunCommand

`RunCommand(const char* cmd)` prints the command to stdout (prefixed with `[Package]`) and executes it via `system()`. Returns the process exit code.

### Disc Image Detection

`IsDiscImageFilePath(path)` returns true for `.bin` or `.iso` extensions.

`IsLikelySaturnImageBin(path)` filters out false positives that are not actual disc images: files containing `cmakedeterminecompilerabi`, `backup`, `memcard`, or `smpc` in their name are excluded. This prevents CMake build artifacts from being mistaken for disc images.

### CUE Sheet Generation

`GenerateCueForBuild(buildDir, projectDir, outCue)` searches for an existing `.cue` file in the build and project directories. If none is found, it locates the largest `.bin`/`.iso` file (filtered through `IsLikelySaturnImageBin`) and generates a `game.cue` file pointing to it. The CUE sheet uses `MODE1/2048` track format, which is standard for Dreamcast data tracks. This CUE file allows emulators that prefer CUE+BIN over CDI to load the game.

---

## 9. Build Flow

After `GenerateDreamcastPackage()` writes all files and stages all assets, the user runs `_nebula_build_dreamcast.bat` from the `build_dreamcast/` directory. The flow is:

1. **Environment setup**: The batch script sets `KOS_BASE`, `KOS_CC_BASE`, and adds `sh-elf-*` tools to `PATH`. It converts Windows paths to MSYS-style paths for the KOS makefiles.

2. **Compile**: `make -f Makefile.dreamcast` compiles `main.c`, `entry.c`, `NebulaGameStub.c`, all script `.c` files, `KosBindings.c`, `KosInput.c`, and `DetourBridge.cpp` using `sh-elf-gcc` / `sh-elf-g++`. Links against KOS libraries (`libkallisti.a`, etc.).

3. **Extract binary**: `sh-elf-objcopy -O binary nebula_dreamcast.elf nebula_dreamcast.bin` converts the ELF to a flat binary.

4. **Scramble**: KOS's `scramble` tool scrambles the binary into `1ST_READ.BIN`. This is required by the Dreamcast BIOS boot sequence. If the scramble tool is not found, the binary is copied as-is (useful for some emulators).

5. **IP.BIN generation**: `makeip` creates the Dreamcast disc header from `ip.txt` (auto-generated with default metadata if absent). The IP.BIN contains hardware ID, product number, boot filename, and game title.

6. **ISO creation**: `mkisofs` builds an ISO 9660 image from the `cd_root/` directory tree, embedding `IP.BIN` as the boot sector (`-G IP.BIN`). The volume label is `NEBULA_DC`.

7. **CDI creation**: `cdi4dc` converts the ISO into a `.cdi` disc image (`nebula_dreamcast.cdi`), which is the standard format for Dreamcast emulators and disc burning tools.

The final output, `nebula_dreamcast.cdi`, can be loaded directly in an emulator (e.g. lxdream, Flycast, redream) or burned to a CD-R for real hardware.

## See Also

- [Scripting](../getting-started/Scripting.md) -- gameplay script system and NB_RT_* bridge functions
- [Dreamcast Binding API](Dreamcast_Binding_API.md) -- platform binding layer reference
- [Dreamcast Header Reference](Dreamcast_Header_Reference.md) -- KosBindings and KosInput header details
- [File Formats](../assets/File_Formats.md) -- binary and text format specifications
- [VMU Tool](../editor/VMU_Tool.md) -- VMU icon and animation editor
