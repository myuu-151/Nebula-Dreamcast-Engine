<p align="center">
  <img src="docs/nebula-logo.png" alt="Nebula Saturn Engine" width="320" />
</p>

# Nebula Saturn Engine

**Nebula Saturn Engine** is a 3D game engine + editor targeting **Windows and Sega Saturn**, built on JO Engine.

## Highlights
- **Dual-target workflow** (Windows + Saturn)
- **Editor-first tooling** for scenes and assets
- **Lightweight codebase**

## Current Status
Early development / active prototyping.

## Saturn Rendering/Runtime Targets
- **Dual SH-2 utilization** is a roadmap target (explicit workload split, not single-CPU-only flow).
- **VDP1 + VDP2 pipeline split** is a roadmap target:
  - VDP1 for 3D mesh/polygon submission
  - VDP2 for layered backgrounds/scene planes and display composition
- Keep Saturn-safe constraints enforced with warnings (texture size/format/slot/VRAM pressure) during rollout.

## Repository Layout
- `src/` - editor/runtime code
- `assets/` - project assets
- `thirdparty/` - vendored deps (JO Engine, GLFW, ImGui)

## Formats
- **.nebproj** - Project files (**new canonical project extension**)
- **.nebscene** - Scene files
- **.nebmesh** - Static mesh (NEBM, indexed, 8.8 fixed, optional UV0)
- **.nebtex** - Texture (NEBT, RGB555, big-endian)
- **.nebmat** - Material (text file, `texture=<Assets/...>`)
- **.nebslots** - StaticMesh material-slot manifest (text file, typically under `Assets/.../nebslot/`, `slotN=<Assets/.../.nebmat>`)
- **.nebanim** - Vertex animation (NEB0, big-endian, fixed-point, 12 fps; optional delta compression)

## Build (Windows)
> TODO: add build steps

## Build (Saturn)
> TODO: add build steps

## License
> TODO: add license
