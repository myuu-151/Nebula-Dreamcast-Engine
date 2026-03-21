# Animation System

This document covers the Nebula Dreamcast Engine's vertex animation system end to end: how skeletal FBX animations are baked into per-frame vertex positions, how the binary `.nebanim` format stores them, and how playback works in both the desktop editor and the Dreamcast runtime.

## 1. Overview

Nebula uses **vertex animation**, not skeletal animation, at runtime. The engine imports FBX files that contain skeletal rigs and keyframed bone animations via Assimp, but at export time it **evaluates the skeleton at each sample frame** and records the final world-space vertex positions. The result is a flat array of per-frame vertex positions stored in a `.nebanim` file. This approach trades memory for simplicity: the Dreamcast runtime never needs a skeleton, bone hierarchy, or skinning shader. It just swaps vertex buffers each frame.

The baking pipeline lives in `src/io/anim_io.cpp` and `src/io/anim_io.h`. The in-memory clip representation and the mesh it operates on are defined in `src/io/mesh_io.h`.

## 2. AnimSlot

Each `StaticMesh3DNode` can hold up to **8 animation slots** (`kStaticMeshAnimSlots`). A slot is defined in `src/nodes/StaticMesh3DNode.h`:

```cpp
struct AnimSlot
{
    std::string name;   // user-facing label, e.g. "Walk", "Idle"
    std::string path;   // project-relative path to a .nebanim file
    float speed = 1.0f; // playback speed multiplier
    bool loop = true;   // whether this slot loops or clamps at the last frame
};
```

The `StaticMesh3DNode` stores the slots in a fixed-size array:

```cpp
std::array<AnimSlot, kStaticMeshAnimSlots> animSlots{};
int animSlotCount = 0;
```

Slots are serialized into `.nebscene` files as part of each StaticMesh3D node line. The scene I/O code in `src/scene/scene_io.cpp` writes each slot as four tokens: `name path speed loop_flag`. On load, it auto-detects whether the loop flag is present for backward compatibility (older scenes used 3 tokens per slot).

At runtime on the Dreamcast, gameplay scripts select a slot by name via `NB_RT_PlayAnimation(meshName, animName)`, which looks up the slot by name and starts playback. The per-slot loop flag is respected: looping slots wrap around, non-looping slots stop at the last frame and report finished.

## 3. NebAnimClip

The in-memory representation of a loaded `.nebanim` is `NebAnimClip`, defined in `src/io/mesh_io.h`:

```cpp
struct NebAnimClip
{
    uint32_t version = 0;            // file format version (currently 4)
    uint32_t flags = 0;              // bitfield: bit 0 = delta compressed,
                                     //           bit 1 = has embedded map,
                                     //           bit 2 = mesh-local space
    uint32_t vertexCount = 0;        // vertices per frame
    uint32_t frameCount = 0;         // total frames in the clip
    float fps = 12.0f;               // sample rate (default 12 fps)
    uint32_t deltaFracBits = 8;      // fractional bits for delta encoding
    uint32_t targetMeshVertexCount = 0; // expected mesh vertex count (v4+)
    uint32_t targetMeshHash = 0;     // CRC32 of the target mesh layout (v4+)
    bool hasEmbeddedMap = false;     // whether an FBX-to-nebmesh index map is embedded
    bool meshAligned = false;        // whether vertices are already in mesh order
    std::vector<uint32_t> embeddedMapIndices; // FBX->nebmesh vertex remap table
    bool valid = false;
    std::vector<std::vector<Vec3>> frames; // frames[frameIndex][vertexIndex] = position
};
```

Key fields:

- **`frames`** is the core data: a vector of frames, where each frame is a vector of `Vec3` positions, one per vertex. `frames[f][v]` gives you the world-space position of vertex `v` at frame `f`.
- **`fps`** controls playback rate. Stored in the binary as fixed-point 16.16.
- **`deltaFracBits`** controls the precision of delta-compressed frames. With the default value of 8, each delta unit represents 1/256 of a world unit.
- **`embeddedMapIndices`** (v4+) stores the index remapping table that maps output vertex order (matching the .nebmesh) back to the flat FBX vertex order used during baking.

## 4. .nebanim Binary Format

The file is **big-endian** throughout (matching the Dreamcast's SH4 byte order). The loader is `LoadNebAnimClip()` in `src/io/anim_io.cpp`.

### Header

| Offset | Size | Field | Notes |
|--------|------|-------|-------|
| 0 | 4 | Magic | `NEB0` (ASCII) |
| 4 | 4 | Version | u32, currently 4. Versions 2-4 supported. |
| 8 | 4 | Flags | u32 (v3+). Bit 0: delta compressed. Bit 1: has embedded map. Bit 2: mesh-local space. |
| 12 | 4 | VertexCount | u32. Vertices per frame. Max 4096. |
| 16 | 4 | FrameCount | u32. Total frames. Max 1000. |
| 20 | 4 | FPS | s32 fixed-point 16.16. |
| 24 | 4 | DeltaFracBits | u32 (v3+). Fractional bits for delta encoding (default 8). |

### Frame Data

Immediately after the header, frame data follows. For each frame, for each vertex:

- **Frame 0 (or non-delta mode):** Three `s32` values in fixed-point 16.16 format (x, y, z). Each vertex costs 12 bytes.
- **Delta frames (frames 1+ when delta-compressed):** Three `s16` values representing the delta from the previous frame's vertex position. Each delta is scaled by `2^deltaFracBits`. Each vertex costs 6 bytes, halving the per-frame storage.

Delta compression is optional. When enabled (flag bit 0), frame 0 is always stored as full 32-bit fixed-point values, and subsequent frames store 16-bit deltas. If a delta exceeds the s16 range, it is clamped and a warning is emitted during export.

### v4 Trailer

After all frame data, version 4 files include:

| Field | Size | Notes |
|-------|------|-------|
| TargetMeshVertexCount | u32 | Expected vertex count of the matching .nebmesh |
| TargetMeshHash | u32 | CRC32 hash of the target mesh layout |

If the embedded-map flag (bit 1) is set, the trailer continues:

| Field | Size | Notes |
|-------|------|-------|
| MapCount | u32 | Number of entries in the remap table |
| MapIndices | MapCount * u32 | For each output vertex, the flat FBX source index |

## 5. Animation Baking from FBX

The baking pipeline converts skeletal animation data from an Assimp-loaded FBX scene into per-frame vertex positions. The key functions are in `src/io/anim_io.cpp`.

### SampleMergedFbxVertices

This is the core sampling function. Given an Assimp scene, an animation, a set of mesh indices, and a time in ticks, it evaluates the skeleton and returns the skinned vertex positions for all specified meshes concatenated into a single flat array.

The process for each mesh:

1. **Collect bone data.** For each bone in the mesh, resolve its scene node using `ResolveSceneNodeByNameRobust()` and store its offset matrix and per-vertex weights.
2. **Evaluate bone transforms.** At the given time, walk the scene hierarchy to compute each bone's global transform using `AiFindNodeGlobal()`, then multiply by the bone's offset matrix to get the final bone-to-world matrix.
3. **Skin each vertex.** For each vertex, accumulate the weighted sum of bone transforms applied to the bind-pose position. Vertices with no bone weights fall back to the mesh node's global transform.
4. **Apply import basis.** The result is transformed through `ApplyImportBasis()` to match Nebula's coordinate conventions.

### Assimp TRS Interpolation

The engine provides its own interpolation helpers rather than relying on Assimp's built-in evaluation:

- **`AiSamplePosition()`** / **`AiSampleScale()`** -- Linear interpolation between keyframes for position and scale channels.
- **`AiSampleRotation()`** -- Spherical linear interpolation (slerp) between rotation quaternion keyframes.
- **`AiNodeLocalAtTime()`** -- Computes a node's local transform at a given time by sampling its animation channel's TRS components and composing them via `AiComposeTRS()`. Falls back to the node's static transform if no channel exists.

### Bone Channel Matching

The function `AiFindChannel()` implements a three-tier matching strategy to find the animation channel for a bone:

1. **Exact match** -- Direct `aiString` comparison against channel node names.
2. **Normalized name match** -- Both names are normalized via `NormalizeAnimName()` (lowercased, path separators stripped, leaf token extracted after `:`, `|`, `/`), then compared.
3. **Suffix match** -- If the normalized target name appears as a suffix of the normalized channel name, it matches. This handles cases where FBX exporters prepend namespace prefixes.

The same three-tier strategy is used by `ResolveSceneNodeByNameRobust()` when mapping bone names to scene nodes.

### ExportNebAnimation

This is the main export function. It:

1. Builds the same virtual mesh that `ExportNebMesh` would produce, including S16-snapped positions and UV selection, with full provenance tracking.
2. Runs `CleanupNebMeshTopology()` on the virtual mesh to get the same deduplicated vertex order as the actual .nebmesh file.
3. Uses the provenance arrays to build a guaranteed-correct FBX-to-nebmesh index mapping (`sourceIndexForOutput`).
4. Samples vertex positions at each frame (default 12 fps), applies bone skinning, and writes the binary .nebanim file with the mapping embedded in the v4 trailer.

## 6. Embedded Animation Metadata

When an FBX file is imported as a mesh, the engine can store animation metadata alongside the .nebmesh file. This is managed by `NebMeshEmbeddedAnimMeta` (defined in `src/io/anim_io.h`):

```cpp
struct NebMeshEmbeddedAnimMeta
{
    std::string sourceFbxPath;                    // project-relative path to source FBX
    std::vector<std::string> clipNames;           // names of animation clips in the FBX
    std::vector<unsigned int> meshIndices;         // which Assimp mesh indices to include
    std::vector<uint32_t> mapIndices;             // FBX-to-nebmesh vertex remap table
    uint32_t provenanceVersion = 1;
    std::vector<uint32_t> provenanceMeshIndices;  // for each nebmesh vertex: source mesh ref index
    std::vector<uint32_t> provenanceVertexIndices;// for each nebmesh vertex: source vertex index
    uint32_t exportedVertexCount = 0;
    bool mappingVerified = false;
    bool mappingOk = false;
    std::string mappingQuality = "missing";       // "missing", "approx", or "exact"
};
```

The metadata is stored as a plain-text `.nebmeshmeta` sidecar file (path derived by `GetNebMeshEmbeddedMetaPath()`). It records:

- The source FBX path so the editor can re-open it for re-baking or preview.
- The list of clip names found in the FBX.
- The mesh indices that were included during export.
- Provenance data mapping each nebmesh vertex back to its source FBX mesh and vertex index.

`BuildDefaultEmbeddedMetaFromScene()` populates a meta struct from a freshly-loaded Assimp scene, collecting all animation clip names and all mesh indices.

## 7. Provenance Mapping

When an FBX is exported to .nebmesh, the mesh undergoes topology cleanup (`CleanupNebMeshTopology`): duplicate vertices are merged, degenerate triangles are removed, and the vertex order may change. This means the flat FBX vertex array and the final .nebmesh vertex array have different orderings and potentially different counts.

The **provenance arrays** (`provenanceMeshIndices` and `provenanceVertexIndices`) track, for each vertex in the final .nebmesh, which Assimp mesh ref and which vertex within that mesh it originated from. This allows the animation exporter to write vertex positions in the exact order the .nebmesh expects.

There are three quality levels for this mapping:

- **exact** -- Provenance data is present and verified. Each nebmesh vertex maps to a specific FBX source vertex. Animation playback will be pixel-perfect.
- **approx** -- No provenance data, but a nearest-position fallback mapping was built. Works for simple meshes but may produce artifacts on complex geometry with overlapping vertices.
- **missing** -- No mapping available. Animation cannot be previewed in embedded mode.

The function `RebuildNebMeshEmbeddedMapping()` in `src/io/anim_io.cpp` attempts to rebuild the mapping by re-importing the source FBX via Assimp and correlating vertices. It checks for provenance data first (exact path), falls back to position-based matching (approx path), and reports the quality level.

## 8. Playback in the Editor

Editor animation playback state is stored in global maps keyed by mesh index (the index into `gStaticMeshNodes`). These are declared in `src/editor/editor_state.h`:

```cpp
extern std::unordered_map<int, int>   gEditorAnimActiveSlot;  // which slot is active
extern std::unordered_map<int, float> gEditorAnimTime;        // current time in seconds
extern std::unordered_map<int, float> gEditorAnimSpeed;       // playback speed
extern std::unordered_map<int, bool>  gEditorAnimPlaying;     // is playing
extern std::unordered_map<int, bool>  gEditorAnimFinished;    // has reached end
extern std::unordered_map<int, bool>  gEditorAnimLoop;        // should loop
```

The animation tick happens every frame in `TickEditorFrame()` (in `src/editor/frame_loop.cpp`) during play mode:

1. For each mesh with `gEditorAnimPlaying[idx] == true`, advance `gEditorAnimTime[idx]` by `deltaTime * speed`.
2. Look up the active animation slot and resolve its .nebanim path.
3. Load the clip from `gStaticAnimClipCache` (a global cache of loaded NebAnimClip structs, declared in `src/ui/mesh_inspector.h`).
4. Compute the current frame: `frame = floor(time * fps)`.
5. If the frame exceeds `frameCount`:
   - If looping: wrap the time by subtracting the clip duration until it falls back in range.
   - If not looping: stop playback and clamp to the last frame.

The rendering code in `src/viewport/static_mesh_render.cpp` reads the current frame from the clip cache and substitutes the animated vertex positions when drawing.

## 9. Playback on the Dreamcast

During the Dreamcast packaging step (`src/platform/dreamcast/dc_codegen.cpp`), the editor:

1. Collects all animation slot paths across all scenes and stages the .nebanim files to the disc image under deterministic short names (`Axxxxx`).
2. Emits per-scene, per-mesh animation slot metadata into the generated `main.c`, including slot name, disc path, speed multiplier, and loop flag.
3. Generates the `NB_RT_PlayAnimation()` bridge function, which:
   - Finds the mesh by name using `dc_find_mesh_by_name()`.
   - Searches the mesh's slot table for a matching animation name.
   - Loads the .nebanim data from the disc if not already cached.
   - Sets the mesh's animation state to playing with the slot's speed and loop settings.

The runtime bridge API (declared in `src/platform/dreamcast/KosBindings.h`) provides:

```c
void NB_RT_PlayAnimation(const char* meshName, const char* animName);
void NB_RT_StopAnimation(const char* meshName);
int  NB_RT_IsAnimationPlaying(const char* meshName);
int  NB_RT_IsAnimationFinished(const char* meshName);
void NB_RT_SetAnimationSpeed(const char* meshName, float speed);
```

Gameplay scripts (plain C, compiled separately) call these functions. The generated runtime code handles frame advancement each update tick, applying the per-slot loop flag: looping slots wrap, non-looping slots clamp at the last frame and report finished via `NB_RT_IsAnimationFinished()`.

## 10. Mesh Inspector

The mesh inspector panel (`src/ui/mesh_inspector.h`, `src/ui/mesh_inspector.cpp`) provides animation preview and diagnostics. Its state is held in `NebMeshInspectorState` (defined in `src/io/anim_io.h`).

### Preview Modes

The inspector supports three playback modes (`InspectorPlaybackMode`):

- **EmbeddedExact** -- Re-opens the source FBX via Assimp and evaluates the skeleton in real time using the exact provenance mapping. Requires the source FBX to be accessible and provenance data to be present. This is the highest-fidelity preview.
- **EmbeddedApprox** -- Same as above but uses approximate position-based mapping when exact provenance is unavailable.
- **ExternalLegacy** -- Loads a standalone .nebanim file and applies its vertex positions directly. Does not require the source FBX. Used for inspecting pre-exported clips.

### Mapping Quality Diagnostics

The inspector reports mapping quality as one of three levels (`InspectorMappingQuality`):

- **Exact** -- Provenance-based mapping verified successfully.
- **Approx** -- Nearest-position fallback mapping in use.
- **Missing** -- No mapping available; preview unavailable in embedded mode.

The `AnimBakeDiagnostics` struct reports skinning statistics: total bones, matched bones, unmatched bones, animation channels found, and the maximum vertex displacement from frame 0 (useful for detecting near-static or broken animations).

### Mini 3D Preview

The function `DrawNebMeshMiniPreview()` renders a small wireframe 3D viewport within the inspector panel. It shows the mesh with the current animation frame's vertex positions applied, and supports orbit camera controls (yaw, pitch, zoom stored in the inspector state). This allows quick visual verification without needing to place the mesh in a scene.

## Source File Reference

| File | Role |
|------|------|
| `src/io/anim_io.h` | All animation structs, Assimp helper declarations, inspector state |
| `src/io/anim_io.cpp` | Baking, sampling, export, clip loading, embedded meta I/O, mapping rebuild |
| `src/io/mesh_io.h` | `NebAnimClip` and `NebMesh` struct definitions, binary I/O helpers |
| `src/nodes/StaticMesh3DNode.h` | `AnimSlot` struct, `kStaticMeshAnimSlots` constant |
| `src/editor/frame_loop.cpp` | Editor play-mode animation tick logic |
| `src/editor/editor_state.h` | Editor animation playback state globals |
| `src/ui/mesh_inspector.h` | Mesh inspector globals and function declarations |
| `src/ui/mesh_inspector.cpp` | Inspector UI rendering and animation preview |
| `src/viewport/static_mesh_render.cpp` | Viewport rendering with animated vertex substitution |
| `src/scene/scene_io.cpp` | Scene serialization of animation slots |
| `src/platform/dreamcast/dc_codegen.cpp` | Dreamcast code generation for animation slots and bridge functions |
| `src/platform/dreamcast/KosBindings.h` | Runtime animation bridge API declarations |
| `src/platform/dreamcast/KosBindings.c` | Weak fallback stubs for animation bridge functions |
