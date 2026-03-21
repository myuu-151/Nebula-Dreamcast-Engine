# Asset Pipeline

This document covers how assets (textures, materials, meshes, animations) are imported, stored, and exported for Dreamcast in Nebula.

---

## Texture Pipeline

### Import flow

When a mesh is imported (FBX, OBJ, etc. via Assimp):
1. Embedded or referenced textures are exported to PNG
2. Each PNG is converted to `.nebtex` format (16-bit big-endian pixel data)
3. A metadata sidecar file (`.nebtex.meta`) is created alongside each texture
4. Textures are assigned to the appropriate material slots on the StaticMesh3D

### .nebtex format

Binary format storing raw 16-bit pixel data in big-endian byte order. The Dreamcast PVR hardware expects big-endian textures, so this format can be loaded directly into VRAM without byte-swapping.

### Texture metadata (.nebtex.meta)

Each `.nebtex` file has a sidecar `.meta` file storing per-texture properties:

| Property | Values | Purpose |
|----------|--------|---------|
| **Wrap Mode** | Clamp / Repeat | UV addressing mode |
| **Filter Mode** | Nearest / Bilinear | Texture sampling filter |
| **NPOT Mode** | None / Scale / Pad | How non-power-of-two textures are handled |
| **Allow UV Repeat** | On / Off | Whether UV coordinates can tile |
| **Flip U / Flip V** | On / Off | Mirror texture on U or V axis |

These properties are editable in the texture inspector when a `.nebtex` file is selected in the asset browser.

### NPOT (Non-Power-of-Two) handling

The Dreamcast PVR requires power-of-two texture dimensions (64, 128, 256, 512, 1024). When a source image has non-power-of-two dimensions, the NPOT mode controls how it's handled:

- **None** — no conversion (may fail on hardware)
- **Scale** — resizes to nearest power of two
- **Pad** — pads with transparent pixels to next power of two

### Dreamcast staging

During export, textures are staged as `Txxxxx` short names under `build_dreamcast/cd_root/data/`. The metadata properties (wrap mode, filter, flip) are baked into the generated runtime constants and passed to the PVR at load time.

---

## Material Pipeline

### .nebmat format

Material files (`.nebmat`) store per-slot rendering properties for a StaticMesh3D. Each StaticMesh3D can have up to **14 material slots** (`kStaticMeshMaterialSlots`).

### Material properties per slot

| Property | Range | Purpose |
|----------|-------|---------|
| **Shade Mode** | Unlit / Lit | Whether the slot receives directional lighting |
| **Light Yaw** | 0-360 | Directional light horizontal angle |
| **Light Pitch** | 0-90 | Directional light vertical angle |
| **Shadow Intensity** | 0.0-1.0 | Strength of directional shadow |
| **Shading UV** | 0 / 1 | Which UV layer to use for shading |
| **UV Scale U** | float | Horizontal UV scale multiplier |
| **UV Scale V** | float | Vertical UV scale multiplier |

### Material slot assignment

When a mesh is imported, the editor auto-assigns material slots from the source model's material names. Each slot gets a `.nebmat` file named `m_<materialName>.nebmat` in the project's materials folder.

Material slots can also be manually assigned in the StaticMesh3D inspector.

### Per-scene material data

Material properties are **per-scene** — each scene has its own set of material properties for its meshes. This ensures that the same mesh can have different shading in different scenes.

On Dreamcast, material properties are baked into per-scene 3D arrays:
```
kSceneShadeMode[sceneIndex][meshIndex][slotIndex]
kSceneLightYaw[sceneIndex][meshIndex][slotIndex]
kSceneLightPitch[sceneIndex][meshIndex][slotIndex]
...
```

### .nebslots format

The slot manifest file (`.nebslots`) records which material names map to which slot indices for a given mesh. This is generated during import and used during Dreamcast export to ensure consistent slot ordering.

---

## Mesh Pipeline

### Import

Meshes are imported from FBX, OBJ, GLTF, and other Assimp-supported formats. During import:
1. Vertex positions, normals, UVs, and indices are extracted
2. Data is written to `.nebmesh` binary format
3. Up to 2 UV layers are supported
4. Material slot assignments are extracted from the source model

### .nebmesh format

Binary format storing:
- Vertex positions (float xyz)
- UV coordinates per triangle (up to 2 layers)
- Triangle indices (uint16)
- Per-triangle material slot index (uint16)
- Vertex count, triangle count, UV layer count

### Dreamcast staging

Meshes are staged as `Mxxxxx` short names under `build_dreamcast/cd_root/data/`. The binary format is loaded directly by `NB_DC_LoadMesh()` on hardware.

---

## Animation Pipeline

### Animation sources

Nebula supports two animation sources:

1. **Embedded FBX animations** — clips baked from the source FBX file's animation data. These are the primary source.
2. **Legacy .nebanim files** — external vertex animation clips. These override embedded animations when the "Drive .nebanim" toggle is enabled on a StaticMesh3D.

### .nebanim format

Binary vertex animation format:
- Magic: `NEB0`
- Version: 2, 3, or 4
- Per-frame vertex position data
- Frame count, vertex count

### Animation slots

Each StaticMesh3D can have up to **8 animation slots**. Each slot has:

| Property | Purpose |
|----------|---------|
| **Name** | String identifier used by scripts (e.g. `"walk"`, `"idle"`) |
| **Path** | Path to the `.nebanim` file |
| **Speed** | Playback speed multiplier (0.0 - 2.0, default 1.0) |
| **Loop** | When checked, animation wraps continuously. When unchecked, plays once and stops on last frame |

Slots are configured in the StaticMesh3D inspector under "Animation Slots."

### Animation baking

When a mesh with embedded FBX animations is imported, the editor can bake the animation data into `.nebanim` files. This converts skeletal/keyframe animation into per-frame vertex positions suitable for the Dreamcast's vertex animation system.

### Dreamcast export

Animation slots are baked into per-scene constant arrays:
```c
kSceneAnimSlotCount[sceneIndex][meshIndex]      // slots per mesh
kSceneAnimSlotName[sceneIndex][meshIndex][slot]  // slot name string
kSceneAnimSlotDisk[sceneIndex][meshIndex][slot]  // staged filename (Axxxxx)
kSceneAnimSlotSpeed[sceneIndex][meshIndex][slot] // speed multiplier
kSceneAnimSlotLoop[sceneIndex][meshIndex][slot]  // loop flag (0 or 1)
```

`.nebanim` files are staged as `Axxxxx` short names under `build_dreamcast/cd_root/data/animations/`.

At runtime, `PlayAnimation` lazy-loads the disc file matching the slot's short name. Only one animation clip per mesh is kept in memory at a time — starting a new slot frees the previous clip first.

---

## Scene Pipeline

### .nebscene format

Text-based scene file storing:
- Scene name and metadata
- All node types: StaticMesh3D, Camera3D, Node3D, Audio3D, NavMesh3D
- Per-node properties (transform, physics flags, collision bounds, parent, script path)
- Per-StaticMesh3D animation slot data
- Token-encoded strings for serialization safety

### .nebproj format

Project file storing:
- Project name
- Default scene name (used for Dreamcast boot and editor play-mode start)
- Project-level settings

### Dreamcast scene staging

Scenes are staged as `Sxxxxx` short names. Each scene's meshes, textures, materials, and animations are collected and staged with deterministic short names. The generated `main.c` contains arrays mapping scene indices to all their asset data.

### Asset name mapping

| Asset Type | Short Name Pattern | Staging Location |
|------------|-------------------|------------------|
| Scenes | `Sxxxxx` | `cd_root/data/` |
| Meshes | `Mxxxxx` | `cd_root/data/` |
| Textures | `Txxxxx` | `cd_root/data/` |
| Animations | `Axxxxx` | `cd_root/data/animations/` |
| NavMeshes | `NAVxxxxx.BIN` | `cd_root/data/navmesh/` |

Names are assigned sequentially during export. The mapping is deterministic — the same project always produces the same short names.

---

## Missing Asset Handling

### Missing textures

If a material slot references a texture that doesn't exist at export time, the runtime uses a **fallback white texture**. This prevents crashes on hardware — the mesh renders with flat white on that slot instead.

### Missing animations

If an animation slot references a `.nebanim` file that doesn't exist, the slot is excluded from export. `PlayAnimation` calls for that slot will be no-ops at runtime.
