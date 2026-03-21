# Source Directory Structure

This document describes the layout of the `src/` directory and how the engine's modules relate to each other.

## Directory Map

```
src/
  camera/          Camera math and viewport utilities
  editor/          Editor application, shared state, preferences
  io/              File format loaders and exporters
  math/            Math primitives and utility functions
  navmesh/         Navigation mesh building (Recast/Detour)
  nodes/           Scene node type definitions
  platform/
    dreamcast/     Dreamcast codegen, KOS bindings, input
  runtime/         Play-mode physics, collision, script bridge
  scene/           Scene serialization and multi-scene management
  script/          (reserved for future script tooling)
  ui/              ImGui panels and editor UI widgets
  viewport/        3D viewport rendering and transform gizmos
  vmu/             VMU (Visual Memory Unit) icon tool
```

## Module Descriptions

### `camera/`
Camera math shared by both the editor viewport and Dreamcast runtime export.

| File | Purpose |
|------|---------|
| `camera3d.h/.cpp` | `Camera3D` struct (orientation-based), basis/view/projection builders, `NebulaCamera3D` namespace for Dreamcast export |

`Camera3D` uses forward/up vectors instead of Euler angles. `BuildCamera3DFromLegacyEuler` converts from Euler (used by `Camera3DNode` in scenes) to this representation.

### `editor/`
The editor application entry point, frame loop, shared editor state, and input handling.

| File | Purpose |
|------|---------|
| `main.cpp` | Editor entry point: GLFW/ImGui init, window icon, callback wiring, main loop, shutdown |
| `frame_loop.h/.cpp` | Per-frame tick: input polling, script runtime, 3D rendering, ImGui UI dispatch. `InstallDropCallback` (drag-and-drop import) |
| `viewport_nav.h/.cpp` | Editor viewport camera controller (orbit, pan, zoom, WASD roam), `EvaluateFrameCamera` (per-frame camera evaluation), `InstallViewportScrollCallback` |
| `hotkeys.h/.cpp` | GLFW-level transform hotkeys (G/R/S/X/Y/Z), Esc (play-mode stop), Delete (node deletion), Ctrl shortcuts (undo/redo, save, copy/paste) |
| `editor_state.h/.cpp` | Shared global state: node arrays (`gStaticMeshNodes`, `gNode3DNodes`, etc.), selection, play-mode flags, scene snapshot helpers |
| `project.h/.cpp` | Project file management (`gProjectDir`, `gProjectFile`), open/save, path utilities (`GetExecutableDirectory`, `ResolveEditorAssetPath`, `ToProjectRelativePath`, `GetNebMeshMetaPath`) |
| `prefs.h/.cpp` | Editor preferences (DreamSDK path, MSVC path), persisted in `editor_prefs.ini` |
| `file_dialogs.h/.cpp` | Win32 open/save file dialogs |
| `undo.h/.cpp` | Undo/redo system |

`editor_state.h` is the central hub that most modules include to access the live scene data. It owns the global node vectors that represent the currently loaded scene.

### `io/`
Loaders and exporters for Nebula's custom file formats. These are pure data operations with no editor or UI dependencies.

| File | Purpose |
|------|---------|
| `mesh_io.h/.cpp` | `.nebmesh` load/save, Assimp mesh import, `NebMesh` struct, mesh cache |
| `meta_io.h/.cpp` | `.nebmat` material I/O, `.nebslots` manifest I/O, texture metadata (`NebulaAssets` namespace) |
| `texture_io.h/.cpp` | `.nebtex` export (16-bit BE), WIC image loading, TGA conversion for Dreamcast packaging |
| `anim_io.h/.cpp` | `.nebanim` vertex animation clips, embedded animation metadata, Assimp animation baking |

### `math/`
Math primitives used across the entire codebase.

| File | Purpose |
|------|---------|
| `math_types.h` | `Vec3`, `Mat4`, `Quat` structs |
| `math_utils.h/.cpp` | Euler/quaternion conversion, axis extraction, `QuatFromNormalAndYaw`, `SyncNode3DEulerFromQuat` |

### `navmesh/`
Navigation mesh building using Recast/Detour (vendored in `thirdparty/recastnavigation`).

| File | Purpose |
|------|---------|
| `NavMeshBuilder.h/.cpp` | Builds nav mesh from scene geometry, pathfinding queries |

### `nodes/`
Scene node type definitions. Each node type has its own header; `NodeTypes.h` is the umbrella include.

| File | Purpose |
|------|---------|
| `NodeTypes.h` | Umbrella header, `SceneData` struct, `NebulaNodes` namespace (hierarchy, world transforms) |
| `Camera3DNode.h` | `Camera3DNode` struct (Euler-based scene camera) |
| `StaticMesh3DNode.h/.cpp` | `StaticMesh3DNode` struct, material slot helpers |
| `Node3DNode.h/.cpp` | `Node3DNode` struct (generic 3D node with physics/collision fields) |
| `Audio3DNode.h/.cpp` | `Audio3DNode` struct |
| `NavMesh3DNode.h/.cpp` | `NavMesh3DNode` struct |

Node types are plain data structs. They hold serializable fields (position, rotation, scale, parent name, flags) and are stored in vectors in `editor_state.h`.

### `platform/dreamcast/`
Dreamcast-specific code generation and KOS platform bindings.

| File | Purpose |
|------|---------|
| `dc_codegen.h/.cpp` | Generates `build_dreamcast/main.c` and Makefile from editor scene data |
| `KosBindings.h/.c` | KOS platform layer: scene/mesh/texture loading on Dreamcast hardware |
| `KosInput.h/.c` | Dreamcast controller input wrappers |
| `DetourBridge.h/.cpp` | Detour nav mesh integration for Dreamcast runtime |
| `build_helpers.h/.cpp` | Disc image detection and build utilities |

`dc_codegen` is the bridge between the editor and Dreamcast runtime. It reads the current scene data and emits C source code that implements the `NB_RT_*` API using `NB_DC_*` and `NB_KOS_*` calls.

### `runtime/`
Play-mode simulation systems. These run in the editor to preview gameplay behavior.

| File | Purpose |
|------|---------|
| `physics.h/.cpp` | Gravity tick (applies `velY` to all physics-enabled nodes) |
| `collision.h/.cpp` | Ground snap, slope alignment, wall push-out (AABB vs triangles), Node3D-vs-Node3D overlap |
| `runtime_bridge.h/.cpp` | Editor-side `NB_RT_*` function implementations (the same API that gameplay scripts call on Dreamcast) |
| `script_compile.h/.cpp` | Script compilation (MSVC), DLL hot-reload, play-mode script lifecycle |

The physics/collision split: `physics.cpp` only applies gravity, then calls `ResolveNodeCollision()` and `ResolveNode3DOverlaps()` from `collision.cpp` for all collision response.

### `scene/`
Scene file serialization and multi-scene tab management.

| File | Purpose |
|------|---------|
| `scene_io.h/.cpp` | `.nebscene` read/write, token encoding/decoding |
| `scene_manager.h/.cpp` | Multi-scene tab state, scene switching, save tracking |

### `ui/`
ImGui editor panels. Each panel is a self-contained widget.

| File | Purpose |
|------|---------|
| `toolbar.h/.cpp` | Top toolbar (File/Edit/VMU/Package/Play buttons, window controls, quit confirmation) |
| `main_menu.h/.cpp` | Popup menus for toolbar (File, Edit, Tools, Package), preferences dialog |
| `inspector.h/.cpp` | Property inspector for selected nodes and assets |
| `scene_outliner.h/.cpp` | Scene hierarchy tree view |
| `scene_tabs.h/.cpp` | Multi-scene tab bar |
| `assets_panel.h/.cpp` | Assets panel (context menu, rename, FBX convert, import) |
| `asset_browser.h/.cpp` | Project asset browser utilities (create scene/material/folder, rename, delete) |
| `mesh_inspector.h/.cpp` | Mesh/animation preview and inspection |
| `import_pipeline.h/.cpp` | Asset import dialog (mesh, texture, animation) |

### `viewport/`
3D viewport rendering and interactive transform tools.

| File | Purpose |
|------|---------|
| `viewport_render.h/.cpp` | OpenGL2 texture cache, screen projection, local axes |
| `viewport_transform.h/.cpp` | Translate/rotate/scale gizmo interaction |
| `viewport_selection.h/.cpp` | Click-to-select node picking in the 3D viewport |
| `node_helpers.h/.cpp` | Node world transform queries, camera hierarchy helpers |
| `node_gizmos.h/.cpp` | Audio3D spheres, Camera3D helpers, Node3D boxes, NavMesh3D bounds |
| `background.h/.cpp` | Viewport background (gradient, stars, nebula, grid, axes) |
| `static_mesh_render.h/.cpp` | StaticMesh3D node rendering |

### `vmu/`
Sega Dreamcast VMU (Visual Memory Unit) icon creation tool. Owns all VMU state (`gVmu*` globals) and the `VmuAnimLayer` struct.

| File | Purpose |
|------|---------|
| `vmu_tool.h/.cpp` | VMU state globals, `VmuAnimLayer` struct, icon data manipulation (48x32 monochrome bitmaps), animation frame load/save |
| `vmu_tool_ui.h/.cpp` | ImGui VMU icon editor panel |

## Dependency Flow

```
math/  <--  io/  <--  nodes/  <--  editor/
  ^          ^          ^            |
  |          |          |            v
  +--- camera/ ----+   +------> runtime/
                   |               |
                   +------> platform/dreamcast/
                               |
                               v
                           (generated C)
```

- **`math/`** has zero internal dependencies (only standard library)
- **`io/`** depends on `math/` for `Vec3`/`Mat4` in mesh and animation data
- **`camera/`** depends on `math/` for vector/matrix types
- **`nodes/`** depends on `math/` (via `math_types.h`) and `camera/` (via `camera3d.h`)
- **`editor/`** depends on everything above, plus `ui/`, `viewport/`, `runtime/`
- **`runtime/`** depends on `math/`, `io/`, `nodes/`, `editor/editor_state.h`
- **`platform/dreamcast/`** depends on `math/`, `io/`, `camera/`, `nodes/`

## Custom File Formats

| Extension | Description | Module |
|-----------|-------------|--------|
| `.nebproj` | Project file | `editor/project.cpp` |
| `.nebscene` | Scene file (nodes, hierarchy, properties) | `scene/scene_io.cpp` |
| `.nebmesh` | Binary mesh (positions, normals, UVs, indices) | `io/mesh_io.cpp` |
| `.nebtex` | Texture (16-bit big-endian pixel data) | `io/texture_io.cpp` |
| `.nebmat` | Material reference file | `io/meta_io.cpp` |
| `.nebslots` | StaticMesh material-slot manifest | `io/meta_io.cpp` |
| `.nebanim` | Vertex animation clip | `io/anim_io.cpp` |

## Dreamcast Asset Staging

When packaging for Dreamcast, assets are staged under `build_dreamcast/cd_root/data/` with deterministic short names:

| Asset Type | Prefix | Example |
|------------|--------|---------|
| Scene | `S` | `S00001.nebscene` |
| Mesh | `M` | `M00001.nebmesh` |
| Texture | `T` | `T00001.nebtex` |
| Animation | `A` | `A00001.nebanim` |

This naming scheme avoids filesystem path issues on the Dreamcast's ISO9660 disc format. The mapping between original asset paths and staged names is maintained by `dc_codegen`.

## Three-Layer Dreamcast Runtime

```
  Gameplay Scripts (Scripts/*.c)
      |
      | calls NB_RT_* functions
      v
  Generated Runtime (build_dreamcast/main.c)
      |
      | calls NB_DC_*, NB_KOS_* functions
      v
  Platform Bindings (KosBindings.c, KosInput.c)
      |
      | calls KallistiOS API
      v
  Dreamcast Hardware
```

1. **Script layer** -- Plain C gameplay scripts. Implement `NB_Game_OnStart()`, `NB_Game_OnUpdate(float dt)`, `NB_Game_OnSceneSwitch(const char* name)`.
2. **Generated runtime** -- `dc_codegen` emits this from the editor's scene data. Implements all `NB_RT_*` bridge functions using scene-specific arrays and lookups.
3. **Platform bindings** -- Hand-written KOS wrappers for scene loading, mesh rendering, texture management, and controller input.
