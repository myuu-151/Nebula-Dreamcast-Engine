<p align="center">
  <img src="assets/star.png" alt="Nebula Dreamcast Engine" width="220" />
</p>

# Nebula Dreamcast Engine

**Nebula Dreamcast Engine** is a 3D game engine + editor focused on a practical, shippable workflow for **Windows + Sega Dreamcast**.

It is built for fast iteration: edit in the desktop editor, package for Dreamcast, test quickly on emulator/hardware, repeat.

## Highlights
- **Dreamcast-first runtime path** with rapid package/rebuild loops
- **Editor-driven content workflow** (scenes, meshes, textures, materials)
- **Deterministic asset staging names** for reliable disc lookup
- **Runtime safety fallbacks** to avoid hard crashes on missing texture refs
- **Continuous controller orbit/zoom controls** in Dreamcast runtime

## Current Status
Active development / prototyping, with a working Dreamcast package flow and hardware-focused iteration.

## Dreamcast Runtime Notes
- Scene/mesh/texture staging uses short deterministic names:
  - Scenes: `Sxxxxx.*`
  - Meshes: `Mxxxxx.*`
  - Textures: `Txxxxx.*`
- Missing scene texture refs can fall back safely at runtime.
- Missing/unloadable textures use fallback white texture instead of hard exit.
- Material refs are staged to `data/materials` for packaging parity.

## Repository Layout
- `src/` - editor + runtime generator/source code
- `assets/` - project assets
- `thirdparty/` - dependencies/tooling integrations
- `build_dreamcast/` - generated Dreamcast runtime/package output (project-local)

## File Formats
- **.nebproj** - project file
- **.nebscene** - scene file
- **.nebmesh** - mesh format
- **.nebtex** - texture format
- **.nebmat** - material reference file

## Build / Package (Dreamcast)
General flow:
1. Open project in editor
2. Export/package Dreamcast build
3. Test `nebula_dreamcast.cdi` (emulator/hardware)
4. Iterate

> Exact environment/toolchain setup may vary by local DreamSDK/KOS install.

## Roadmap Direction
- Keep editor/runtime parity tight
- Harden scene/material-slot serialization consistency
- Continue exporter/import cleanup for geometry stability
- Preserve pragmatic runtime behavior over feature bloat

## License
> TODO: add license
