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
9. [.vmuanim -- VMU Animation](#vmuanim----vmu-animation)
10. [Config.ini -- Project Config](#configini----project-config)
11. [editor_prefs.ini -- Editor Preferences](#editor_prefsini----editor-preferences)

---

## .nebproj -- Project File

**Type:** Plain text, single line.

The `.nebproj` file marks a directory as a Nebula project. It lives at the project root alongside `Config.ini`.

### Structure

```
name=MyGame
```

| Field  | Description                            |
|--------|----------------------------------------|
| `name` | Human-readable project name (string).  |

The filename itself is `<ProjectName>.nebproj`. The editor creates this file when a new project is initialized via `CreateNebulaProject`.

---

## .nebscene -- Scene File

**Type:** Plain text, line-oriented.

Each `.nebscene` file describes one scene's node hierarchy. The first line is a header; subsequent lines each define one node.

### Token encoding

Empty string fields are encoded as `"-"` (a single dash) by `EncodeSceneToken`. On load, any token equal to `"-"` is decoded back to an empty string by `DecodeSceneToken`. This prevents whitespace-delimited parsing from collapsing empty fields.

### Header

```
scene=SceneName
```

### Node lines

Each line begins with a type keyword followed by space-separated fields.

#### Audio3D

```
Audio3D <name> <script> <x> <y> <z> <innerRadius> <outerRadius> <baseVolume> <rotX> <rotY> <rotZ> <scaleX> <scaleY> <scaleZ> <parent>
```

#### StaticMesh

```
StaticMesh <name> <script> <material> <mesh> <x> <y> <z> <rotX> <rotY> <rotZ> <scaleX> <scaleY> <scaleZ> <materialSlot> <slot1..slot14> <parent> <collisionSource> <vtxAnim> <runtimeTest> <navmeshReady> <wallThreshold> <animSlotCount> [<slotName> <slotPath> <speed> <loop>] x N
```

- `materialSlot` -- active slot index (0-based, range 0..13).
- `slot1..slot14` -- 14 material path tokens (one per slot).
- `collisionSource` -- 0 or 1, whether this mesh contributes to collision.
- `vtxAnim` -- path to a `.nebanim` file (or `-` if none).
- `runtimeTest` -- 0 or 1.
- `navmeshReady` -- 0 or 1.
- `wallThreshold` -- float, wall-detection angle threshold.
- `animSlotCount` -- number of animation slots in use (max 8).
- Each animation slot has 4 tokens: `name`, `path`, `speed` (float), `loop` (0 or 1).

#### Camera3D

```
Camera3D <name> <x> <y> <z> <rotX> <rotY> <rotZ> <orbitX> <orbitY> <orbitZ> <perspective> <fovY> <nearZ> <farZ> <orthoWidth> <priority> <main> <parent>
```

- `perspective` -- 1 for perspective, 0 for orthographic.
- `main` -- 1 if this is the main camera.
- Older files without orbit offsets are also accepted (fields shift accordingly).

#### Node3D

```
Node3D <name> <x> <y> <z> <rotX> <rotY> <rotZ> <scaleX> <scaleY> <scaleZ> <parent> <primitiveMesh> <script> <collisionSource> <physicsEnabled> <extentX> <extentY> <extentZ> <boundPosX> <boundPosY> <boundPosZ> <simpleCollision>
```

- `extentX/Y/Z` -- AABB half-extents for collision.
- `boundPosX/Y/Z` -- AABB center offset.
- `simpleCollision` -- 0 or 1.

#### NavMesh3D

```
NavMesh3D <name> <x> <y> <z> <rotX> <rotY> <rotZ> <scaleX> <scaleY> <scaleZ> <extentX> <extentY> <extentZ> <navBounds> <navNegator> <cullWalls> <wallCullThreshold> <parent> <wireR> <wireG> <wireB> <wireThickness>
```

---

## .nebmesh -- Binary Mesh

**Type:** Binary, big-endian. Current version: 6.

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

### Version history

| Version | Changes                                       |
|---------|-----------------------------------------------|
| 3       | Added per-triangle material indices            |
| 4       | Added face topology stream (polygon arity)     |
| 5       | Added canonical face records stream            |
| 6       | Added optional second UV layer (bit 4 in flags)|

---

## .nebtex -- Binary Texture

**Type:** Binary, big-endian.

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
Bit layout:  [15..11] unused/zero  [14..10] R (5 bits)  [9..5] G (5 bits)  [4..0] B (5 bits)

  uint16_t rgb555 = (R5 << 10) | (G5 << 5) | B5;
```

Where R5, G5, B5 are the 5 most significant bits of each 8-bit channel (i.e., `channel >> 3`).

Pixels are stored in row-major order, top-to-bottom, left-to-right.

### Notes

- Maximum dimensions for Dreamcast/Saturn packaging: 256x256.
- Non-power-of-two textures are auto-padded or resampled to the next power of two (minimum 8) during packaging, controlled by the `.nebtex.meta` `npot` setting.

---

## .nebtex.meta -- Texture Metadata

**Type:** Plain text, `key=value` per line. Sidecar file stored at `<name>.nebtex.meta`.

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

---

## .nebmat -- Material

**Type:** Plain text, `key=value` per line.

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

---

## .nebslots -- Slot Manifest

**Type:** Plain text, `key=value` per line.

Maps material slots (1-based) to `.nebmat` file paths for a given mesh. Stored under `Assets/<meshdir>/nebslot/<meshname>.nebslots`.

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

Up to 14 material slots are supported (`kStaticMeshMaterialSlots = 14`).

---

## .nebanim -- Binary Animation

**Type:** Binary, big-endian. Current version: 4.

Stores baked per-vertex positions for each frame of a vertex animation clip.

### Magic and header

```
Offset  Size   Type     Field
------  ----   ----     -----
0x00    4      char[4]  magic = "NEB0"
0x04    4      U32BE    version (2, 3, or 4)
```

Version 3+ adds the following fields immediately after version:

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

### Version history

| Version | Changes                                              |
|---------|------------------------------------------------------|
| 2       | Initial format: absolute S32BE positions per frame   |
| 3       | Added flags, deltaFracBits, delta compression        |
| 4       | Added target mesh info trailer and embedded mapping   |

---

## .vmuanim -- VMU Animation

**Type:** Plain text, tab-separated fields.

Defines animation timeline data for the Dreamcast VMU (Visual Memory Unit) display.

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
| `name`        | string | Layer display name                            |
| `visible`     | 0/1    | Layer visibility                              |
| `frameStart`  | int    | First frame this layer is active              |
| `frameEnd`    | int    | Last frame this layer is active               |
| `linkedAsset` | string | Path to the source PNG asset for this layer   |

---

## Config.ini -- Project Config

**Type:** Plain text, `key=value` per line. Located at the project root directory.

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

---

## editor_prefs.ini -- Editor Preferences

**Type:** Plain text, `key=value` per line. Stored next to the editor executable.

### Fields

| Key            | Type   | Default          | Description                                     |
|----------------|--------|------------------|-------------------------------------------------|
| `uiScale`      | float  | `1.0`            | Editor UI scale factor                          |
| `themeMode`    | int    | `0`              | Editor theme (0=space/dark, 1=light)            |
| `dreamSdkHome` | string | `C:\DreamSDK`   | Path to DreamSDK installation                   |
| `vcvarsPath`   | string | (empty)          | Path to MSVC vcvarsall.bat or VS install root    |

### Example

```
uiScale=1.25
themeMode=0
dreamSdkHome=C:\DreamSDK
vcvarsPath=C:\Program Files\Microsoft Visual Studio\2022\Community
```

The `vcvarsPath` is resolved flexibly: it can point directly to `vcvarsall.bat`, to a VS instance root, or to the top-level Visual Studio directory. The editor searches for `VC/Auxiliary/Build/vcvarsall.bat` or `vcvars64.bat` under common edition subdirectories (Community, Professional, Enterprise, BuildTools) for years 2022, 2019, 18, and 17.
