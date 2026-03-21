# Mesh and Materials Pipeline

This document explains how Nebula handles meshes, materials, and textures from
import through to Dreamcast packaging. It covers the in-memory structures, binary
file formats, import/export functions, and the caching and slot systems that tie
everything together.

Source files referenced throughout:

- `src/io/mesh_io.h` / `src/io/mesh_io.cpp` -- mesh struct, binary I/O, import, topology cleanup, cache
- `src/io/meta_io.h` / `src/io/meta_io.cpp` -- material files, slot manifests, texture metadata
- `src/io/texture_io.h` / `src/io/texture_io.cpp` -- texture import (WIC), .nebtex export, TGA conversion
- `src/nodes/NodeTypes.h` / `src/nodes/StaticMesh3DNode.cpp` -- material slot resolution at the node level

---

## 1. NebMesh -- the in-memory mesh

Every mesh loaded into the editor lives as a `NebMesh` struct. It holds the
geometry data plus flags that record which optional streams are present.

```
struct NebMesh
{
    vector<Vec3>      positions;         // vertex positions (float, post-dequantize)
    vector<Vec3>      uvs;               // UV layer 0 (x=u, y=v, z unused)
    vector<Vec3>      uvs1;              // UV layer 1 (v6+, same layout)
    vector<uint16_t>  indices;           // triangle index buffer (3 per tri)
    vector<uint16_t>  faceMaterial;      // per-triangle material slot index
    vector<uint8_t>   faceVertexCounts;  // original polygon arity per source face (v4+)
    vector<NebFaceRecord> faceRecords;   // canonical authored face stream (v5+)

    bool hasUv, hasUv1;                  // which UV layers are present
    int  uvLayerCount;                   // 0, 1, or 2
    bool hasFaceMaterial;                // per-tri material indices present
    bool hasFaceTopology;                // original polygon arity stream present
    bool hasFaceRecords;                 // authored face records present
    bool valid;                          // true after successful load
};
```

### UV layers

A mesh can carry zero, one, or two UV layers. `uvs` is the primary layer
(used for diffuse texturing). `uvs1` is the secondary layer (added in format
v6), typically used for lightmaps or shading overlays. Both are stored as
`Vec3` where only x and y are meaningful.

### Face records (v5+)

`NebFaceRecord` preserves the authored face topology from the source FBX. Each
record stores polygon arity (3 for triangle, 4 for quad), a winding hint
(0=clockwise, 1=counter-clockwise), a material index, up to 4 vertex indices,
and per-corner UVs. This data survives the triangulated index buffer and is used
by Saturn-path export tools that need original quad topology.

```
struct NebFaceRecord
{
    uint8_t  arity;          // 3 or 4
    uint8_t  winding;        // 0=CW, 1=CCW
    uint16_t material;
    uint16_t indices[4];
    Vec3     uvs[4];
};
```

---

## 2. The .nebmesh binary format

`.nebmesh` is a big-endian binary file that stores mesh geometry compactly
for fast loading on both desktop and Dreamcast.

### File layout

```
Offset  Size     Field
------  -------  -----------------------------------------
0x00    4 bytes  Magic: "NEBM" (ASCII)
0x04    4 bytes  Version (uint32 BE) -- currently 6
0x08    4 bytes  Flags (uint32 BE)
0x0C    4 bytes  Vertex count (uint32 BE)
0x10    4 bytes  Index count (uint32 BE)
0x14    4 bytes  Position fractional bits (uint32 BE) -- always 8

        --- position stream ---
        vertexCount * 6 bytes: (S16 x, S16 y, S16 z) per vertex

        --- UV0 stream (if flags bit 0 set) ---
        vertexCount * 4 bytes: (S16 u, S16 v) per vertex

        --- UV1 stream (if flags bit 4 set) ---
        vertexCount * 4 bytes: (S16 u, S16 v) per vertex

        --- index stream ---
        indexCount * 2 bytes: uint16 per index

        --- face material stream (if flags bit 1 set) ---
        (indexCount/3) * 2 bytes: uint16 material index per triangle

        --- face topology stream (if flags bit 2 set) ---
        4 bytes: faceCount (uint32 BE)
        faceCount bytes: uint8 polygon arity per original face

        --- face records stream (if flags bit 3 set) ---
        4 bytes: recordCount (uint32 BE)
        per record:
            1 byte:  arity (uint8)
            1 byte:  winding (uint8)
            2 bytes: material (uint16 BE)
            4 corners * (2+2+2 bytes): index(u16) + uv_u(s16) + uv_v(s16)
```

### Flags bitmask

| Bit | Meaning                                  |
|-----|------------------------------------------|
|  0  | UV layer 0 present                       |
|  1  | Per-triangle material index stream        |
|  2  | Original face topology (vertex counts)    |
|  3  | Canonical face records (v5+)              |
|  4  | UV layer 1 present (v6+)                  |

### S16 8.8 fixed-point encoding

All positions and UVs are stored as signed 16-bit integers in 8.8 fixed-point
format. This gives 8 bits of integer range (-128..+127) and 8 bits of fraction
(1/256 resolution).

Encode: `int16_t stored = round(floatValue * 256.0)`
Decode: `float value = stored / 256.0`

The position range is therefore roughly -128 to +127.996 world units. Meshes
larger than this will be clamped, which triggers a "Position clamp (too large
for 8.8)" warning during export.

### Version history

| Version | Changes                                              |
|---------|------------------------------------------------------|
| 3       | Added per-triangle material index stream (flag bit 1)|
| 4       | Added face topology stream (flag bit 2)              |
| 5       | Added canonical face records stream (flag bit 3)     |
| 6       | Added second UV layer (flag bit 4)                   |

---

## 3. Mesh import from FBX via Assimp

The function `ExportNebMesh` in `src/io/mesh_io.cpp` takes an Assimp `aiScene`
(loaded from FBX or another format) and writes a `.nebmesh` binary. The key
steps are:

### a. UV channel selection

For each sub-mesh in the scene, the exporter scans all available UV channels
and picks the one with the widest UV span as the primary (UV0). If a second
channel exists, it becomes UV1.

### b. Material slot compaction

FBX material indices may be sparse. The exporter builds a compact mapping from
source material indices to contiguous slot indices (0..N-1), so the .nebmesh
faceMaterial stream uses sequential slot numbers.

### c. Vertex assembly and basis transform

Each vertex position is run through `ApplyImportBasis`, which remaps axes based
on the import basis mode (identity, Blender, or Maya convention) and then bakes
a 90-degree Y-axis rotation so meshes face the correct direction at (0,0,0).

Positions and UVs are pre-quantized to the S16 8.8 grid at export time. This
ensures that the topology cleanup pass operates on the same precision as the
stored format, preventing a second cleanup during load from finding new merges
and changing vertex order.

### d. Fan triangulation

Polygons with more than 3 vertices are triangulated as a fan from vertex 0:
`(v0, v1, v2)`, `(v0, v2, v3)`, etc. The original polygon arity is preserved
in both the faceVertexCounts stream and the faceRecords stream.

### e. Provenance mapping

The exporter optionally captures provenance: for each vertex in the final
.nebmesh, it records which source mesh and source vertex index it came from.
This mapping survives topology cleanup and is used by the animation baking
system to correlate animation frames with the final vertex order.

### f. Limits

- Maximum 2048 vertices per mesh
- Maximum 65535 indices per mesh

Exceeding either limit produces a warning string but does not prevent export.

---

## 4. Topology cleanup

`CleanupNebMeshTopology` is called both during export and during load. It
performs three operations in order:

### a. Vertex welding

Duplicate vertices are merged using a spatial hash with an epsilon of 1/512 for
both position and UV coordinates. Two vertices merge only if their positions
are within squared distance `(1/512)^2` AND their UV0 coordinates (and UV1 if
present) match within 1/512.

The algorithm quantizes each position to a grid cell, hashes the cell, and
checks existing vertices in the same bucket for exact proximity. A remap table
is built so all index references can be updated.

### b. Degenerate triangle removal

After welding, any triangle where two or more indices are identical is removed.
Exact duplicate triangles (same sorted index triple and same material) are also
removed, keeping only the first occurrence.

### c. Triangle ordering stabilization

The remaining triangles are sorted by `(material, lowestIndex, midIndex,
highestIndex)` using a stable sort. This produces a deterministic draw order
that reduces frame-to-frame draw conflicts.

If provenance arrays are passed in, they are kept in sync through all three
operations.

---

## 5. Mesh caching

The editor maintains a global cache to avoid reloading meshes from disk:

```cpp
// Global cache: path string -> loaded NebMesh
std::unordered_map<std::string, NebMesh> gNebMeshCache;
```

The cache-aware loader is `GetNebMesh`:

```cpp
const NebMesh* GetNebMesh(const std::filesystem::path& path);
```

It returns a pointer to the cached mesh if one exists, otherwise calls
`LoadNebMesh` and inserts the result. If loading fails, a `NebMesh` with
`valid = false` is cached (so it will not retry on every frame).

A similar cache exists for GL textures: `gNebTextureCache` maps path strings
to `GLuint` texture IDs.

---

## 6. Material system

### .nebmat text format

Materials are stored as plain-text key=value files with the `.nebmat` extension.
Each line is one field. A complete material file looks like this:

```
texture=Assets/textures/brick.nebtex
uv_scale=1
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

### Field reference

| Field                   | Type   | Description                                       |
|-------------------------|--------|---------------------------------------------------|
| `texture`               | string | Relative path to the .nebtex texture               |
| `uv_scale`              | float  | Uniform UV scale (legacy, still read)              |
| `saturn_allow_uv_repeat`| 0/1    | Allow UV repeat on Saturn/Dreamcast                |
| `uv_scale_u`, `uv_scale_v` | float | Per-axis UV scale                              |
| `uv_offset_u`, `uv_offset_v` | float | Per-axis UV offset                            |
| `uv_rotate_deg`         | float  | UV rotation in degrees                             |
| `shading`               | int    | Shading mode (0 = unlit, other modes TBD)          |
| `light_rotation`        | float  | Directional light yaw                              |
| `light_pitch`           | float  | Directional light pitch                            |
| `light_roll`            | float  | Directional light roll                             |
| `shadow_intensity`      | float  | Shadow darkness (1.0 = full)                       |
| `shading_uv`            | int    | Which UV layer to use for shading (-1 = default)   |

### Per-field read-modify-write pattern

To change a single field without disturbing the others, the code uses a
read-modify-write pattern. For example, `SaveMaterialTexture` does this:

1. Read all fields from disk via `ReadAllMatFields` (a private helper that
   calls every `LoadMaterial*` function).
2. Replace the one field being changed.
3. Rewrite the entire file via `SaveMaterialAllFields`.

This ensures the file always contains all fields in a consistent order, even if
a field was previously missing.

All material I/O lives in the `NebulaAssets` namespace in `src/io/meta_io.cpp`.

---

## 7. Material slots

### Slot architecture

Each `StaticMesh3DNode` can hold up to 14 material assignments, one per slot.
The constant is:

```cpp
constexpr int kStaticMeshMaterialSlots = 14;
```

Each triangle in the mesh has a `faceMaterial` index that maps to one of these
slots. Slot 0 is the default.

### .nebslots manifest

The slot-to-material mapping is persisted as a `.nebslots` file alongside the
mesh. The file lives at:

```
<mesh_dir>/nebslot/<mesh_stem>.nebslots
```

For example, `Assets/models/house.nebmesh` would have its slots at
`Assets/models/nebslot/house.nebslots`.

The file format is plain text:

```
slot1=Assets/mat/house_wall.nebmat
slot2=Assets/mat/house_roof.nebmat
slot3=
slot4=
...
slot14=
```

Slot numbering in the file is 1-based; the code converts to 0-based internally.
Empty slots are written as empty values.

### GetStaticMeshPrimaryMaterial priority

When the editor needs a single "primary" material for a mesh node, it uses
this priority order (from `src/nodes/StaticMesh3DNode.cpp`):

1. The material in `materialSlots[node.materialSlot]` (the node's selected slot)
2. The material in `materialSlots[0]` (slot 0 fallback)
3. The legacy `node.material` field (backwards compatibility)

---

## 8. Texture pipeline

### .nebtex binary format

Textures are stored as `.nebtex` files -- a compact big-endian binary with
RGB555 pixel data.

```
Offset  Size     Field
------  -------  ---------------------------
0x00    4 bytes  Magic: "NEBT" (ASCII)
0x04    2 bytes  Width (uint16 BE)
0x06    2 bytes  Height (uint16 BE)
0x08    2 bytes  Format (uint16 BE) -- 1 = RGB555
0x0A    2 bytes  Flags (uint16 BE) -- reserved, currently 0

        --- pixel data ---
        width * height * 2 bytes: RGB555 pixels (big-endian)
```

### RGB555 encoding

Each pixel is a 16-bit value packed as:

```
Bit:  15  14 13 12 11 10   9  8  7  6  5   4  3  2  1  0
       0 |  R (5 bits)  |  G (5 bits)   |  B (5 bits)   |
```

Encode from 8-bit: `r5 = r >> 3; g5 = g >> 3; b5 = b >> 3;`
Pack: `rgb555 = (r5 << 10) | (g5 << 5) | b5`

### PNG import via LoadImageWIC

On Windows, `LoadImageWIC` (in `src/io/texture_io.cpp`) uses the Windows
Imaging Component (WIC) to decode PNG (or any WIC-supported format) into a
32-bit BGRA pixel buffer. This buffer is then used by:

- `LoadTextureWIC` -- uploads to an OpenGL texture for editor preview
- `ExportNebTexturePNG` -- converts BGRA to RGB555 and writes a .nebtex file

### ExportNebTexturePNG conversion

The function reads the PNG via WIC, then for each pixel:
1. Extract R, G, B from the BGRA buffer (B at offset+0, G at +1, R at +2)
2. Shift each channel from 8-bit to 5-bit: `r5 = r >> 3`
3. Pack into RGB555
4. Write as big-endian uint16

---

## 9. Texture metadata

Each `.nebtex` file can have a sidecar metadata file at `<path>.meta`
(for example, `brick.nebtex.meta`). This is a plain-text key=value file.

### .nebtex.meta fields

```
wrap=repeat
flip_u=0
flip_v=0
npot=resample
saturn_allow_uv_repeat=0
filter=bilinear
```

| Field                   | Values                       | Description                              |
|-------------------------|------------------------------|------------------------------------------|
| `wrap`                  | repeat, extend, clip, mirror | Texture wrap mode                        |
| `flip_u`                | 0, 1                        | Flip U coordinates                       |
| `flip_v`                | 0, 1                        | Flip V coordinates                       |
| `npot`                  | pad, resample                | How to handle non-power-of-two textures  |
| `saturn_allow_uv_repeat`| 0, 1                        | Allow UV repeat on Saturn/Dreamcast      |
| `filter`                | nearest, bilinear            | Texture filtering mode                   |

### NPOT handling modes

The Dreamcast (and Saturn path) require power-of-two texture dimensions. When
a texture is not power-of-two, the metadata `npot` field controls the strategy:

- **pad** (npot=pad, mode 0): The original pixels are placed in the top-left
  corner of a power-of-two canvas. The remaining space is filled with black.
  UV coordinates must be scaled down to address only the used portion. The
  function `GetNebTexSaturnPadUvScale` computes these scale factors:
  `outU = originalWidth / paddedWidth`, `outV = originalHeight / paddedHeight`.

- **resample** (npot=resample, mode 1): The texture is nearest-neighbor
  resampled to the next power-of-two size. UVs remain in normalized [0,1]
  range since the entire canvas is image data.

### Metadata read-modify-write

Like materials, texture metadata uses a read-modify-write pattern. Each
`SaveNebTex*` function reads all existing fields, changes one, and rewrites
the entire file. This keeps all fields present and in a consistent order.

### ReadNebTexDimensions

For tools that need only the image size without loading pixel data,
`ReadNebTexDimensions` reads just the .nebtex header (magic + width + height)
and returns the dimensions.

---

## 10. TGA conversion for Dreamcast

When packaging for Dreamcast, textures are converted from `.nebtex` to 24-bit
TGA files that the Dreamcast toolchain can process. This is handled by
`ConvertNebTexToTga24` in `src/io/texture_io.cpp`.

### Conversion steps

1. Read the .nebtex header and verify magic "NEBT" and format=1 (RGB555).
2. Enforce a 256x256 maximum -- textures larger than this are rejected.
3. Decode each RGB555 pixel back to 8-bit BGR:
   ```
   r = (r5 << 3) | (r5 >> 2)   // expand 5-bit to 8-bit with rounding
   g = (g5 << 3) | (g5 >> 2)
   b = (b5 << 3) | (b5 >> 2)
   ```
4. Enforce power-of-two dimensions (minimum 8, maximum 256) using the NPOT
   mode from the texture's .meta file:
   - **pad mode**: copy original pixels into top-left of padded canvas
   - **resample mode**: nearest-neighbor scale to power-of-two size
5. Write an uncompressed 24-bit TGA with top-left origin (descriptor byte
   0x20).

### UV scale adjustment for padding

When pad mode is used, UVs in the mesh no longer map 1:1 to the padded texture.
The function `GetNebTexSaturnPadUvScale` returns the scale factors that the
Dreamcast codegen must apply:

```
float scaleU = originalWidth  / paddedWidth;   // e.g. 100/128 = 0.78125
float scaleV = originalHeight / paddedHeight;  // e.g. 50/64   = 0.78125
```

In resample mode, these factors are both 1.0 since the entire texture is
meaningful data.

### ConvertTgaToJoSafeTga24

This secondary function takes an existing TGA file (potentially bottom-left
origin, 24 or 32 bpp) and normalizes it to a top-left-origin 24-bit TGA with
power-of-two dimensions. It uses nearest-neighbor resampling when the
dimensions need adjustment.

---

## Putting it all together

The typical workflow from FBX to Dreamcast disc image:

```
FBX file
  |
  v
Assimp loads aiScene
  |
  v
ExportNebMesh()
  - ApplyImportBasis (axis remap + 90-deg bake)
  - Pre-quantize to S16 8.8 grid
  - Fan triangulation, material slot compaction
  - CleanupNebMeshTopology (weld, dedup, stabilize)
  - Write .nebmesh binary
  |
  v
Editor assigns materials
  - .nebmat files (texture path, UV params, shading)
  - .nebslots manifest (slot1..slot14 assignments)
  |
  v
PNG textures imported
  - LoadImageWIC decodes to BGRA
  - ExportNebTexturePNG converts to RGB555 .nebtex
  - .nebtex.meta sidecar for wrap/filter/NPOT settings
  |
  v
Dreamcast packaging
  - ConvertNebTexToTga24 (RGB555 -> 24-bit TGA, POT enforcement)
  - GetNebTexSaturnPadUvScale for UV fixup
  - Deterministic short names: Mxxxxx, Txxxxx, Sxxxxx
  - Build disc image
```

## See Also

- [Node Types](../scene-and-nodes/Node_Types.md) -- StaticMesh3DNode fields and material slot resolution
- [Animation System](Animation_System.md) -- vertex animation baking and playback
- [File Formats](File_Formats.md) -- binary format specifications for .nebmesh, .nebtex, .nebmat
- [Dreamcast Export](../dreamcast/Dreamcast_Export.md) -- asset staging and texture conversion for Dreamcast
