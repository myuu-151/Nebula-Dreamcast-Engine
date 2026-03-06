<p align="center">
  <img src="docs/nebula-logo.png" alt="Nebula Dreamcast Engine" width="320" />
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

## Dependencies

### Windows Editor / Tooling
- **Visual Studio 2022 + Native Tools x64**
  - Required for MSVC/`cl.exe` build flows.
  - Install "Desktop development with C++" workload.
- **CMake + CMake GUI**
  - Used to configure/generate Visual Studio solution files.
- **Git**
  - Source sync and branch workflows.

### Dreamcast Build Toolchain
- **DreamSDK (KallistiOS toolchain)**
  - Provides Dreamcast compile/link/package tools (e.g. `sh-elf-gcc`, `kos-cc`, `mkisofs`, CDI tooling).
  - Required for building `nebula_dreamcast.elf` and packaging CDI output.

### Quick checks
```bat
where cl
cmake --version
where sh-elf-gcc
where kos-cc
```

## Dreamcast Runtime Notes
- Runtime staged data is packaged under:
  - `build_dreamcast/cd_root/data/materials`
  - `build_dreamcast/cd_root/data/meshes`
  - `build_dreamcast/cd_root/data/scenes`
  - `build_dreamcast/cd_root/data/textures`
  - `build_dreamcast/cd_root/data/vmu`
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
- **.nebslots** - StaticMesh material-slot manifest

## Compile (Windows Editor) — CMake GUI + Visual Studio

### Prerequisites
- Visual Studio 2022 (Desktop development with C++)
- CMake (and **CMake GUI**)
- Git

### CMake GUI configure/generate
1. Open **CMake GUI**.
2. Set:
   - **Where is the source code:** `<repo>/` (this folder)
   - **Where to build the binaries:** `<repo>/build`
3. Click **Configure**.
4. Choose generator:
   - `Visual Studio 17 2022`
   - Platform: `x64`
5. Let configure finish (fix any missing dependency prompts if shown).
6. Click **Generate**.
7. Click **Open Project** (opens the generated `.sln` in Visual Studio).

### Compile in Visual Studio
1. Set **NebulaEditor** as the startup project (Solution Explorer → right-click `NebulaEditor` → **Set as Startup Project**).
2. Set configuration to **Debug** or **Release**.
3. Set platform to **x64**.
4. Build:
   - **Build → Build Solution** (`Ctrl+Shift+B`)
5. Run from Visual Studio (or run built exe from `build/...`).

### Command-line alternative (optional)
```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Build / Package (Dreamcast)
General flow:
1. Open project in editor
2. Export/package Dreamcast build
3. Run `_nebula_build_dreamcast.bat` in `build_dreamcast`
4. Test `nebula_dreamcast.cdi` (emulator/hardware)
5. Iterate

Quick analysis of what `_nebula_build_dreamcast.bat` runs:
- Cleans prior ELF output (`rm -f nebula_dreamcast.elf`)
- Compiles C sources to objects (`main.c`, bindings/input files, and script `.c` files)
- Links objects into `nebula_dreamcast.elf` with `kos-cc`
- Builds disc image data (`mkisofs`)
- Generates final CDI image for emulator/hardware testing (`cdi4dc`)

> Exact environment/toolchain setup may vary by local DreamSDK/KOS install.

## Roadmap Direction
- Keep editor/runtime parity tight
- Harden scene/material-slot serialization consistency
- Continue exporter/import cleanup for geometry stability
- Preserve pragmatic runtime behavior over feature bloat

## License
> TODO: add license
