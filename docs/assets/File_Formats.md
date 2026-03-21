# Nebula Dreamcast Engine -- File Format Reference

This document describes every custom file format used by the Nebula Dreamcast Engine. Binary formats use big-endian byte order unless otherwise noted. Text formats use UTF-8 with `key=value` line syntax.

---

## Table of Contents

1. [.nebproj -- Project File](#nebproj----project-file)
2. [.nebscene -- Scene File](#nebscene----scene-file)
3. [.nebmesh -- Binary Mesh](#nebmesh----binary-mesh)
4. [.nebtex -- Binary Texture](#nebtex----binary-texture)
5. [.nebtex.meta -- Texture Metadata](#nebtexmeta----texture-metadata)
6. [.nebmat -- Material](#nebmat----material)
7. [.nebslots -- Slot Manifest](#nebslots----slot-manifest)
8. [.nebanim -- Binary Animation](#nebanim----binary-animation)
9. [.animmeta -- Embedded Animation Metadata](#animmeta----embedded-animation-metadata)
10. [.vmuanim -- VMU Animation](#vmuanim----vmu-animation)
11. [Config.ini -- Project Config](#configini----project-config)
12. [editor_prefs.ini -- Editor Preferences](#editor_prefsini----editor-preferences)

---

## .nebproj -- Project File

| Property | Value |
|----------|-------|
| Extension | `.nebproj` |
| Type | Plain text, single line |
| Purpose | Marks a directory as a Nebula project |
| Owner module | `src/editor/project.cpp` |
| Read function | Recognized by the editor on project open |
| Write function | `CreateNebulaProject()` |

The `.nebproj` file lives at the project root alongside `Config.ini`. Its filename follows the pattern `<ProjectName>.nebproj`.

### Structure

```
name=MyGame
```

| Field  | Description                            |
|--------|----------------------------------------|
| `name` | Human-readable project name (string).  |

### Notes

- When creating a new project, the editor also removes any legacy `.neb` file with the same stem name.
- The project directory is required to contain `Assets/`, `Scripts/`, and `Intermediate/` subdirectories, which are created automatically.

---

## .nebscene -- Scene File

| Property | Value |
|----------|-------|
| Extension | `.nebscene` |
| Type | Plain text, line-oriented, whitespace-delimited |
| Purpose | Serializes one scene's complete node hierarchy |
| Owner module | `src/scene/scene_io.cpp` (`NebulaScene` namespace) |
| Read function | `NebulaScene::LoadSceneFromPath()` |
| Write function | `NebulaScene::SaveSceneToPath()` |
| Builder | `NebulaScene::BuildSceneText()` |

### Token encoding

Empty string fields are encoded as `"-"` (a single dash) by `EncodeSceneToken()`. On load, any token equal to `"-"` is decoded back to an empty string by `DecodeSceneToken()`. This prevents whitespace-delimited parsing from collapsing empty fields.

### Header

```
scene=SceneName
```

The scene name is derived from the file's stem (filename without extension).

### Node lines

Each subsequent line begins with a type keyword followed by space-separated fields. Five node types are supported.

#### Audio3D

```
Audio3D <name> <script> <x> <y> <z> <innerRadius> <outerRadius> <baseVolume> <rotX> <rotY> <rotZ> <scaleX> <scaleY> <scaleZ> <parent>
```

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Unique node name |
| `script` | string | Project-relative path to a `.c` script, or `-` |
| `x`, `y`, `z` | float | World position |
| `innerRadius` | float | Audio inner radius |
| `outerRadius` | float | Audio outer radius |
| `baseVolume` | float | Base volume level |
| `rotX`, `rotY`, `rotZ` | float | Euler rotation in degrees |
| `scaleX`, `scaleY`, `scaleZ` | float | Non-uniform scale |
| `parent` | string | Parent node name, or `-` for root |

#### StaticMesh

```
StaticMesh <name> <script> <material> <mesh> <x> <y> <z> <rotX> <rotY> <rotZ> <scaleX> <scaleY> <scaleZ> <materialSlot> <slot1..slot14> <parent> <collisionSource> <vtxAnim> <runtimeTest> <navmeshReady> <wallThreshold> <animSlotCount> [<slotName> <slotPath> <speed> <loop>] x N
```

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Unique node name |
| `script` | string | Script path or `-` |
| `material` | string | Primary material path or `-` |
| `mesh` | string | Path to `.nebmesh` file or `-` |
| `x`, `y`, `z` | float | World position |
| `rotX`, `rotY`, `rotZ` | float | Euler rotation in degrees |
| `scaleX`, `scaleY`, `scaleZ` | float | Non-uniform scale |
| `materialSlot` | int | Active slot index (0-based, range 0..13) |
| `slot1..slot14` | string | 14 material path tokens (one per slot) |
| `parent` | string | Parent node name or `-` |
| `collisionSource` | 0/1 | Whether this mesh contributes to collision |
| `vtxAnim` | string | Path to a `.nebanim` file or `-` |
| `runtimeTest` | 0/1 | Runtime test flag |
| `navmeshReady` | 0/1 | Eligible for navmesh generation |
| `wallThreshold` | float | Wall-detection angle threshold (default 0.7) |
| `animSlotCount` | int | Number of animation slots in use (max 8) |
| Animation slots | -- | Each slot has 4 tokens: `name`, `path`, `speed` (float), `loop` (0/1). Backward-compatible with 3-token format (no loop field). |

Constants: `kStaticMeshMaterialSlots = 14`, `kStaticMeshAnimSlots = 8`.

#### Camera3D

```
Camera3D <name> <x> <y> <z> <rotX> <rotY> <rotZ> <orbitX> <orbitY> <orbitZ> <perspective> <fovY> <nearZ> <farZ> <orthoWidth> <priority> <main> <parent>
```

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Unique node name |
| `x`, `y`, `z` | float | World position |
| `rotX`, `rotY`, `rotZ` | float | Euler rotation in degrees |
| `orbitX`, `orbitY`, `orbitZ` | float | Orbit pivot offsets (new format, 18+ tokens) |
| `perspective` | 0/1 | 1 for perspective, 0 for orthographic |
| `fovY` | float | Vertical field of view in degrees |
| `nearZ` | float | Near clip plane |
| `farZ` | float | Far clip plane |
| `orthoWidth` | float | Orthographic projection width |
| `priority` | float | Camera priority for sorting |
| `main` | 0/1 | 1 if this is the main camera |
| `parent` | string | Parent node name or `-` |

Older files without orbit offsets (14-15 tokens, or 11 tokens) are accepted with adjusted field positions.

#### Node3D

```
Node3D <name> <x> <y> <z> <rotX> <rotY> <rotZ> <scaleX> <scaleY> <scaleZ> <parent> <primitiveMesh> <script> <collisionSource> <physicsEnabled> <extentX> <extentY> <extentZ> <boundPosX> <boundPosY> <boundPosZ> <simpleCollision>
```

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Unique node name |
| `x`, `y`, `z` | float | World position |
| `rotX`, `rotY`, `rotZ` | float | Euler rotation in degrees |
| `scaleX`, `scaleY`, `scaleZ` | float | Non-uniform scale |
| `parent` | string | Parent node name or `-` |
| `primitiveMesh` | string | Path to primitive mesh or `-` |
| `script` | string | Script path or `-` |
| `collisionSource` | 0/1 | Whether this node contributes to collision |
| `physicsEnabled` | 0/1 | Whether physics simulation is active |
| `extentX`, `extentY`, `extentZ` | float | AABB half-extents for collision |
| `boundPosX`, `boundPosY`, `boundPosZ` | float | AABB center offset |
| `simpleCollision` | 0/1 | Use simplified collision shape |

On load, the loader also derives quaternion rotation from the Euler angles using the Rz * Ry * Rx convention.

#### NavMesh3D

```
NavMesh3D <name> <x> <y> <z> <rotX> <rotY> <rotZ> <scaleX> <scaleY> <scaleZ> <extentX> <extentY> <extentZ> <navBounds> <navNegator> <cullWalls> <wallCullThreshold> <parent> <wireR> <wireG> <wireB> <wireThickness>
```

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Unique node name |
| `x`, `y`, `z` | float | World position |
| `rotX`, `rotY`, `rotZ` | float | Euler rotation in degrees |
| `scaleX`, `scaleY`, `scaleZ` | float | Non-uniform scale |
| `extentX`, `extentY`, `extentZ` | float | Bounding volume extents |
| `navBounds` | 0/1 | Acts as navmesh boundary |
| `navNegator` | 0/1 | Acts as navmesh exclusion zone |
| `cullWalls` | 0/1 | Enable wall culling |
| `wallCullThreshold` | float | Wall cull angle threshold |
| `parent` | string | Parent node name or `-` |
| `wireR`, `wireG`, `wireB` | float | Debug wireframe color (RGB, 0..1) |
| `wireThickness` | float | Debug wireframe line thickness |

### Asset reference renaming

When assets are renamed or moved within the project, `UpdateAssetReferencesForRename()` rewrites all path-valued fields in open scenes and persists changes to disk for `.nebscene`, `.nebmat`, and `.nebslots` files under the `Assets/` tree.

---

## .nebmesh -- Binary Mesh

| Property | Value |
|----------|-------|
| Extension | `.nebmesh` |
| Type | Binary, big-endian |
| Current version | 6 |
| Purpose | Stores triangle mesh geometry with optional UVs, materials, and face topology |
| Owner module | `src/io/mesh_io.cpp` |
| Read function | `LoadNebMesh()` |
| Write function | `ExportNebMesh()` |
| Cache | `gNebMeshCache` (keyed by path string) |

### Magic and header

```
Offset  Size   Type     Field
------  ----   ----     -----
0x00    4      char[4]  magic = "NEBM"
0x04    4      U32BE    version
0x08    4      U32BE    flags
0x0C    4      U32BE    vertexCount
0x10    4      U32BE    indexCount
0x14    4      U32BE    precisionFactor (posFracBits)
```

Total header size: 24 bytes.

### Flags bitfield

| Bit | Mask  | Name            | Description                                      |
|-----|-------|-----------------|--------------------------------------------------|
| 0   | 0x01  | hasUV           | UV layer 0 is present                            |
| 1   | 0x02  | hasFaceMaterial | Per-triangle material index stream follows        |
| 2   | 0x04  | hasFaceTopology | Original polygon arity stream follows (v4+)      |
| 3   | 0x08  | hasFaceRecords  | Canonical face records stream follows (v5+)      |
| 4   | 0x10  | hasUV1          | Second UV layer is present (v6+)                 |

### Data streams (in order)

**1. Vertex positions** -- `vertexCount * 3` values

Each vertex is 3x S16BE (x, y, z). To recover world-space coordinates:

```
float x = (int16_t)raw_x / (float)(1 << precisionFactor)
```

The default `precisionFactor` is 8 (8.8 fixed-point), giving a position range of roughly -128..+127.996 with 1/256 precision.

**2. UV layer 0** (present if `flags & 0x01`) -- `vertexCount * 2` values

Each UV is 2x S16BE (u, v) in 8.8 fixed-point:

```
float u = (int16_t)raw_u / 256.0f
```

**3. UV layer 1** (present if `flags & 0x10`) -- `vertexCount * 2` values

Same encoding as UV layer 0.

**4. Triangle indices** -- `indexCount` values

Each index is U16BE, referencing into the vertex array.

**5. Per-triangle material indices** (present if `flags & 0x02`) -- `indexCount / 3` values

Each is U16BE, identifying which material slot this triangle uses.

**6. Face topology stream** (present if `flags & 0x04`)

```
U32BE    faceCount
uint8_t  faceVertexCounts[faceCount]   (each byte = polygon arity of original source face)
```

**7. Face records stream** (present if `flags & 0x08`)

```
U32BE    recordCount
```

Each record (repeated `recordCount` times):

```
Offset  Size   Type     Field
------  ----   ----     -----
+0      1      U8       arity (3 or 4)
+1      1      U8       winding (0=CW, 1=CCW)
+2      2      U16BE    material
+4      24     --       4 corner entries (below)
```

Each of the 4 corner entries (unused corners are zeroed for triangles):

```
+0      2      U16BE    vertexIndex
+2      2      S16BE    u  (8.8 fixed)
+4      2      S16BE    v  (8.8 fixed)
```

Total per record: 2 + 2 + 4*6 = 28 bytes.

### Import pipeline

During export via `ExportNebMesh()`:

1. Assimp scene meshes are iterated. For each mesh, the best UV channel is selected by maximum UV span.
2. Vertex positions are transformed through `ApplyImportBasis()` which converts from DCC tool space (Blender, Maya) to Nebula engine space, then bakes a 90-degree Y-axis rotation.
3. Positions and UVs are pre-quantized to S16 8.8 precision before cleanup.
4. `CleanupNebMeshTopology()` merges duplicate vertices (within 1/512 position epsilon and UV epsilon), removes degenerate/duplicate triangles, and stabilizes triangle ordering by material-then-index sort.
5. FBX material indices are compacted into contiguous slot indices (0..N-1).
6. Provenance tracking maps each output vertex back to its source Assimp mesh and vertex index.
7. N-gon faces (>4 sides) are fan-triangulated. Quads and triangles are preserved as canonical face records.

### Limits

- Maximum vertices: 2048
- Maximum indices: 65535

### Version history

| Version | Changes                                       |
|---------|-----------------------------------------------|
| 3       | Added per-triangle material indices            |
| 4       | Added face topology stream (polygon arity)     |
| 5       | Added canonical face records stream            |
| 6       | Added optional second UV layer (bit 4 in flags)|

---

## .nebtex -- Binary Texture

| Property | Value |
|----------|-------|
| Extension | `.nebtex` |
| Type | Binary, big-endian |
| Purpose | Stores a 16-bit color texture for Dreamcast/Saturn targets |
| Owner module | `src/io/texture_io.cpp` |
| Read function | `ReadNebTexDimensions()` (header only), `ConvertNebTexToTga24()` (full decode) |
| Write function | `ExportNebTexturePNG()` |

### Magic and header

```
Offset  Size   Type     Field
------  ----   ----     -----
0x00    4      char[4]  magic = "NEBT"
0x04    2      U16BE    width
0x06    2      U16BE    height
0x08    2      U16BE    format (must be 1 = RGB555)
0x0A    2      U16BE    flags (reserved, currently 0)
```

Total header size: 12 bytes.

### Pixel data

`width * height` pixels, each U16BE in RGB555 encoding:

```
Bit layout:  [14..10] R (5 bits)  [9..5] G (5 bits)  [4..0] B (5 bits)

  uint16_t rgb555 = (R5 << 10) | (G5 << 5) | B5;
```

Where R5, G5, B5 are the 5 most significant bits of each 8-bit channel (i.e., `channel >> 3`).

Pixels are stored in row-major order, top-to-bottom, left-to-right.

### Decoding back to 8-bit

To reconstruct full 8-bit channels from 5-bit values:

```c
uint8_t r = (r5 << 3) | (r5 >> 2);
uint8_t g = (g5 << 3) | (g5 >> 2);
uint8_t b = (b5 << 3) | (b5 >> 2);
```

### Packaging for Dreamcast/Saturn

During packaging (`ConvertNebTexToTga24()`):

- Maximum dimensions: 256x256 (textures exceeding this are rejected).
- Non-power-of-two textures are padded or resampled to the next power of two (minimum 8), controlled by the `.nebtex.meta` `npot` setting.
- Output is an uncompressed 24-bit TGA with top-left origin for the Saturn/Dreamcast toolchain.
- `GetNebTexSaturnPadUvScale()` computes the UV scale factors needed when pad mode shrinks the effective texture area.

---

## .nebtex.meta -- Texture Metadata

| Property | Value |
|----------|-------|
| Extension | `.nebtex.meta` (sidecar, stored at `<name>.nebtex.meta`) |
| Type | Plain text, `key=value` per line |
| Purpose | Per-texture rendering and packaging settings |
| Owner module | `src/io/meta_io.cpp` (`NebulaAssets` namespace) |
| Path resolution | `NebulaAssets::GetNebTexMetaPath()` |

### Fields

| Key                      | Values                         | Default    | Description                                         |
|--------------------------|--------------------------------|------------|-----------------------------------------------------|
| `wrap`                   | `repeat`, `extend`, `clip`, `mirror` | `repeat`   | Texture wrap/addressing mode                        |
| `flip_u`                 | `0` or `1`                     | `0`        | Flip texture horizontally                            |
| `flip_v`                 | `0` or `1`                     | `0`        | Flip texture vertically                              |
| `npot`                   | `pad` or `resample`            | `pad`      | Non-power-of-two handling during packaging           |
| `saturn_allow_uv_repeat` | `0` or `1`                     | `0`        | Allow UV repeat on Saturn target                     |
| `filter`                 | `nearest` or `bilinear`        | `bilinear` | Texture filtering mode                               |

### Example

```
wrap=repeat
flip_u=0
flip_v=0
npot=resample
saturn_allow_uv_repeat=0
filter=bilinear
```

### Round-trip saves

All save functions (`SaveNebTexWrapMode`, `SaveNebTexFlipOptions`, etc.) perform a full read-modify-write: they read all existing fields from the meta file, update the target field, then rewrite the entire file. This ensures no fields are lost during partial updates.

---

## .nebmat -- Material

| Property | Value |
|----------|-------|
| Extension | `.nebmat` |
| Type | Plain text, `key=value` per line |
| Purpose | Defines material properties including texture reference, UV transform, and shading |
| Owner module | `src/io/meta_io.cpp` (`NebulaAssets` namespace) |
| Read functions | `LoadMaterialTexture()`, `LoadMaterialUvScale()`, `LoadMaterialUvTransform()`, `LoadMaterialShadingMode()`, etc. |
| Write function | `SaveMaterialAllFields()` (full write), plus per-field wrappers |

### Fields

| Key                      | Type    | Default | Description                                   |
|--------------------------|---------|---------|-----------------------------------------------|
| `texture`                | string  | (empty) | Relative path to `.nebtex` file               |
| `uv_scale`               | float   | `0`     | Legacy uniform UV scale                       |
| `saturn_allow_uv_repeat` | `0`/`1` | `0`    | Allow UV repeat on Saturn target              |
| `uv_scale_u`             | float   | `1`     | UV scale on U axis                            |
| `uv_scale_v`             | float   | `1`     | UV scale on V axis                            |
| `uv_offset_u`            | float   | `0`     | UV offset on U axis                           |
| `uv_offset_v`            | float   | `0`     | UV offset on V axis                           |
| `uv_rotate_deg`          | float   | `0`     | UV rotation in degrees                        |
| `shading`                | int     | `0`     | Shading mode (0=unlit, others engine-defined) |
| `light_rotation`         | float   | `0`     | Light source yaw in degrees                   |
| `light_pitch`            | float   | `0`     | Light source pitch in degrees                 |
| `light_roll`             | float   | `0`     | Light source roll in degrees                  |
| `shadow_intensity`       | float   | `1`     | Shadow darkening intensity (0..1)             |
| `shading_uv`             | int     | `-1`    | UV layer index for shading (-1 = auto)        |

### Example

```
texture=Assets/tex/ground.nebtex
uv_scale=0
saturn_allow_uv_repeat=0
uv_scale_u=1
uv_scale_v=1
uv_offset_u=0
uv_offset_v=0
uv_rotate_deg=0
shading=0
light_rotation=0
light_pitch=0
light_roll=0
shadow_intensity=1
shading_uv=-1
```

### Round-trip saves

All per-field save functions (e.g. `SaveMaterialTexture()`) read the full field set via `ReadAllMatFields()`, update the target field, then call `SaveMaterialAllFields()` to rewrite all 14 fields atomically.

---

## .nebslots -- Slot Manifest

| Property | Value |
|----------|-------|
| Extension | `.nebslots` |
| Type | Plain text, `key=value` per line |
| Purpose | Maps material slots to `.nebmat` file paths for a given mesh |
| Owner module | `src/io/meta_io.cpp` (`NebulaAssets` namespace) |
| Path convention | `Assets/<meshdir>/nebslot/<meshname>.nebslots` |
| Path resolution | `NebulaAssets::GetNebSlotsPathForMesh()` |
| Read function | `NebulaAssets::LoadNebSlotsManifest()` / `LoadNebSlotsManifestFile()` |
| Write function | `NebulaAssets::SaveNebSlotsManifest()` |

### Structure

```
slot1=Assets/mat/ground.nebmat
slot2=Assets/mat/wall.nebmat
slot3=
...
slot14=
```

| Key         | Description                                                |
|-------------|------------------------------------------------------------|
| `slot1`..`slot14` | Relative path to a `.nebmat` file, or empty if unused. |

Up to 14 material slots are supported (`kStaticMeshMaterialSlots = 14`). Slot numbering is 1-based in the file but 0-based in memory.

### Load behavior

- Lines not matching the `slotN=` pattern are skipped.
- Slot indices outside the range 1..14 are ignored.
- If a referenced material path does not exist relative to the project directory, a fallback lookup is attempted in the `mat/` sibling directory of the `.nebslots` file's parent.

---

## .nebanim -- Binary Animation

| Property | Value |
|----------|-------|
| Extension | `.nebanim` |
| Type | Binary, big-endian |
| Current version | 4 |
| Purpose | Stores baked per-vertex positions for each frame of a vertex animation clip |
| Owner module | `src/io/anim_io.cpp` |
| Read function | `LoadNebAnimClip()` |
| Write function | `ExportNebAnimation()` |
| Remapping | `WriteRemappedNebAnim()`, `StageRemappedNebAnim()` |

### Magic and header

```
Offset  Size   Type     Field
------  ----   ----     -----
0x00    4      char[4]  magic = "NEB0"
0x04    4      U32BE    version (2, 3, or 4)
```

Version 3+ adds the following field immediately after version:

```
0x08    4      U32BE    flags
```

Then, common to all versions:

```
        4      U32BE    vertexCount
        4      U32BE    frameCount
        4      U32BE    fps (16.16 fixed-point)
```

Version 3+ adds:

```
        4      U32BE    deltaFracBits
```

**FPS decoding:** `float fps = (int32_t)fpsFixed / 65536.0f` (clamped to minimum 1.0).

### Flags bitfield (v3+)

| Bit | Mask  | Name           | Description                                    |
|-----|-------|----------------|------------------------------------------------|
| 0   | 0x01  | deltaEncoded   | Frames 1..N use delta compression              |
| 1   | 0x02  | hasEmbeddedMap | Embedded vertex mapping follows frame data     |
| 2   | 0x04  | meshLocalSpace | Positions are in mesh-local space              |

### Frame data

For each frame (0 to `frameCount - 1`), for each vertex (0 to `vertexCount - 1`):

**Frame 0 (or all frames when not delta-encoded):**

```
S32BE    x   (16.16 fixed-point)
S32BE    y   (16.16 fixed-point)
S32BE    z   (16.16 fixed-point)
```

12 bytes per vertex per frame.

Convert to float: `float value = (int32_t)raw / 65536.0f`

**Frames 1..N (when delta-encoded, `flags & 0x01`):**

```
S16BE    dx  (scaled by 1/(1 << deltaFracBits))
S16BE    dy
S16BE    dz
```

6 bytes per vertex per frame. Each delta is added to the previous frame's value:

```
pos[f][v].x = pos[f-1][v].x + (int16_t)dx / (float)(1 << deltaFracBits)
```

### V4 trailer

After all frame data, version 4 adds:

```
U32BE    targetMeshVertexCount    (vertex count of the target .nebmesh)
U32BE    targetMeshHash           (CRC32 of the target mesh layout)
```

### Embedded vertex mapping (v4, when `flags & 0x02`)

Follows the v4 trailer:

```
U32BE    mapCount
U32BE    mapIndices[mapCount]     (FBX source vertex index per output vertex)
```

This mapping allows the runtime to reorder animation vertices to match a separately-exported `.nebmesh`.

### Export pipeline

`ExportNebAnimation()` performs the following:

1. Builds bone influence data from the Assimp scene for skinned animation.
2. Reconstructs the exact vertex array that `ExportNebMesh()` + `LoadNebMesh()` would produce, using identical S16 quantization and `CleanupNebMeshTopology()`, to guarantee a correct FBX-to-nebmesh vertex mapping via provenance tracking.
3. Samples skinned vertex positions at each frame by evaluating bone transforms through the scene hierarchy.
4. Writes frame data in either absolute (16.16 fixed) or delta-compressed (S16 deltas) form.
5. Appends the v4 trailer with target mesh vertex count and CRC32 hash, plus the embedded vertex mapping if available.

### Limits

- Maximum vertices: 4096 (load), 2048 (export)
- Maximum frames: 1000 (load), 300 (export)
- `deltaFracBits` clamped to 0..15

### Version history

| Version | Changes                                              |
|---------|------------------------------------------------------|
| 2       | Initial format: absolute S32BE positions per frame   |
| 3       | Added flags, deltaFracBits, delta compression        |
| 4       | Added target mesh info trailer and embedded mapping   |

---

## .animmeta -- Embedded Animation Metadata

| Property | Value |
|----------|-------|
| Extension | `.animmeta.animmeta` (double extension is intentional) |
| Type | Plain text, `key=value` per line, with CSV-encoded arrays |
| Purpose | Stores the FBX-to-nebmesh vertex mapping and animation clip metadata for a mesh's embedded animations |
| Owner module | `src/io/anim_io.cpp` |
| Path convention | `Assets/<meshdir>/animmeta/<meshname>.animmeta.animmeta` |
| Path resolution | `GetNebMeshEmbeddedMetaPath()` |
| Read function | `LoadNebMeshEmbeddedMeta()` |
| Write function | `SaveNebMeshEmbeddedMeta()` |

### Fields

| Key | Type | Description |
|-----|------|-------------|
| `source_fbx` | string | Path to the source FBX file used for animation |
| `exported_vertex_count` | uint32 | Total vertex count exported from the FBX |
| `mesh_indices` | CSV of uint | Comma-separated Assimp mesh indices used |
| `mapping_verified` | 0/1 | Whether the mapping has been validated |
| `mapping_ok` | 0/1 | Whether the mapping is usable |
| `mapping_quality` | string | One of: `missing`, `approx`, `exact` |
| `map_indices` | CSV of uint32 | Comma-separated FBX vertex indices for the mapping |
| `provenance_version` | uint32 | Provenance data format version (currently 1) |
| `provenance_count` | uint | Declared provenance entry count (for validation) |
| `provenance_mesh_indices` | CSV of uint32 | Per-vertex source mesh index |
| `provenance_vertex_indices` | CSV of uint32 | Per-vertex source vertex index within its mesh |
| `clip_count` | uint | Number of animation clips |
| `clip0`, `clip1`, ... | string | Animation clip names (0-indexed keys) |

### Example

```
source_fbx=Assets/models/character.fbx
exported_vertex_count=512
mesh_indices=0,1
mapping_verified=1
mapping_ok=1
mapping_quality=exact
map_indices=0,1,2,3,...
provenance_version=1
provenance_count=512
provenance_mesh_indices=0,0,0,...,1,1,1,...
provenance_vertex_indices=0,1,2,...,0,1,2,...
clip_count=2
clip0=Idle
clip1=Walk
```

### Validation

On load, the declared `provenance_count` is cross-checked against the actual sizes of `provenance_mesh_indices` and `provenance_vertex_indices`. If they do not match, provenance data is discarded and `mapping_ok` is set to false.

---

## .vmuanim -- VMU Animation

| Property | Value |
|----------|-------|
| Extension | `.vmuanim` |
| Type | Plain text, tab-separated fields |
| Purpose | Defines animation timeline data for the Dreamcast VMU (Visual Memory Unit) display |
| Owner module | `src/vmu/vmu_tool.cpp` |
| Read function | `LoadVmuFrameData()` |
| Write function | `SaveVmuFrameData()` |
| See also | `docs/VMU_Tool.md` for the VMU Tool editor documentation |

### Structure

```
VMUANIM 1
TOTAL_FRAMES	24
PLAYHEAD	0
LOOP	0
SPEED_MODE	1
LAYER_COUNT	2
LAYER	Layer 1	1	0	12	Assets/vmu/frame1.png
LAYER	Layer 2	1	5	20	Assets/vmu/frame2.png
```

### Header fields

| Key             | Type | Description                                    |
|-----------------|------|------------------------------------------------|
| `VMUANIM`       | --   | Format identifier and version (always `1`)     |
| `TOTAL_FRAMES`  | int  | Total number of frames in the timeline         |
| `PLAYHEAD`      | int  | Current playhead position (frame index)        |
| `LOOP`          | 0/1  | Whether the animation loops                    |
| `SPEED_MODE`    | int  | Playback speed preset (0=slow, 1=normal, 2=fast) |
| `LAYER_COUNT`   | int  | Number of LAYER lines that follow              |

### LAYER line format

Tab-separated fields:

```
LAYER	<name>	<visible>	<frameStart>	<frameEnd>	<linkedAsset>
```

| Field         | Type   | Description                                   |
|---------------|--------|-----------------------------------------------|
| `name`        | string | Layer display name (tabs/newlines sanitized to spaces on save) |
| `visible`     | 0/1    | Layer visibility                              |
| `frameStart`  | int    | First frame this layer is active              |
| `frameEnd`    | int    | Last frame this layer is active               |
| `linkedAsset` | string | Path to the source PNG asset for this layer   |

### Load defaults

If the file contains no layers, a default layer named "Layer 1" is created. `TOTAL_FRAMES` is clamped to a minimum of 1. `SPEED_MODE` is clamped to 0..2.

---

## Config.ini -- Project Config

| Property | Value |
|----------|-------|
| Filename | `Config.ini` |
| Location | Project root directory |
| Type | Plain text, `key=value` per line |
| Purpose | Stores project-level settings |
| Owner module | `src/editor/project.cpp` |
| Read functions | `GetProjectDefaultScene()`, `GetProjectVmuLoadOnBoot()`, `GetProjectVmuAnim()` |
| Write functions | `SetProjectDefaultScene()`, `SetProjectVmuLoadOnBoot()`, `SetProjectVmuAnim()`, `CreateNebulaProject()` |

### Fields

| Key              | Type   | Description                                         |
|------------------|--------|-----------------------------------------------------|
| `project`        | string | Project name (matches `.nebproj` name)              |
| `defaultScene`   | string | Relative path to the default scene (`.nebscene`)    |
| `vmuLoadOnBoot`  | 0/1    | Load VMU icon on Dreamcast boot                     |
| `vmuLinkedAnim`  | string | Relative path to linked `.vmuanim` file             |

### Example

```
project=MyGame
defaultScene=Assets/scenes/Main.nebscene
vmuLoadOnBoot=1
vmuLinkedAnim=Assets/vmu/boot_anim.vmuanim
```

### Write behavior

All set functions perform read-modify-write: they read all existing lines, find and replace the target key (or append it if missing), then rewrite the entire file. This preserves any unrecognized keys.

---

## editor_prefs.ini -- Editor Preferences

| Property | Value |
|----------|-------|
| Filename | `editor_prefs.ini` |
| Location | Same directory as the editor executable |
| Type | Plain text, `key=value` per line |
| Purpose | Persists user preferences across editor sessions |
| Owner module | `src/editor/prefs.cpp` |
| Read function | `LoadPreferences()` |
| Write function | `SavePreferences()` |

### Fields

| Key            | Type   | Default          | Description                                     |
|----------------|--------|------------------|-------------------------------------------------|
| `uiScale`      | float  | `2.0`            | Editor UI scale factor                          |
| `themeMode`    | int    | `0`              | Editor theme (0=Space, 1=Slate, 2=Classic, 3=Grey) |
| `dreamSdkHome` | string | `C:\DreamSDK`   | Path to DreamSDK installation                   |
| `vcvarsPath`   | string | (empty)          | Path to MSVC vcvarsall.bat or VS install root    |

### Example

```
uiScale=1.25
themeMode=0
dreamSdkHome=C:\DreamSDK
vcvarsPath=C:\Program Files\Microsoft Visual Studio\2022\Community
```

### Legacy support

The key `spaceTheme=1` is accepted as a legacy alias for `themeMode=0`, and `spaceTheme=0` maps to `themeMode=1`.

### vcvarsPath resolution

The `vcvarsPath` value is resolved flexibly by `ResolveVcvarsPathFromPreference()`:

- If it points directly to a file (e.g. `vcvarsall.bat`), that file is used.
- If it points to a VS instance root (e.g. `.../2022/Community`), the editor searches for `VC/Auxiliary/Build/vcvarsall.bat` or `vcvars64.bat` beneath it.
- If it points to the top-level Visual Studio directory, the editor iterates year/edition combinations (2022, 2019, 18, 17 crossed with Community, Professional, Enterprise, BuildTools) to find a valid `vcvarsall.bat`.

---

## Dreamcast Asset Staging

When packaging for Dreamcast, all assets are staged under `build_dreamcast/cd_root/data/` using deterministic short names:

| Asset type | Name pattern | Example |
|------------|-------------|---------|
| Scenes     | `Sxxxxx`    | `S00001` |
| Meshes     | `Mxxxxx`    | `M00042` |
| Textures   | `Txxxxx`    | `T00007` |
| Animations | `Axxxxx`    | `A00003` |

Missing textures at runtime use a fallback white texture. The staging process converts `.nebtex` to TGA and `.nebmesh`/`.nebanim` are copied or remapped as needed.

## See Also

- [Mesh and Materials](Mesh_and_Materials.md) -- mesh import pipeline and material system
- [Animation System](Animation_System.md) -- vertex animation baking and .nebanim details
- [Scene System](../scene-and-nodes/Scene_System.md) -- .nebscene serialization and scene management
- [VMU Tool](../editor/VMU_Tool.md) -- .vmuanim format and VMU icon editor
