# Nebula Dreamcast Engine -- Documentation Index

Nebula is a 3D game engine and editor for building games that target the Sega Dreamcast.
You author content in a Windows desktop editor (ImGui + OpenGL 2), then export a packaged
disc image that runs on real Dreamcast hardware or an emulator.

---

## Architecture Overview

```
+=========================================================+
|                   DESKTOP EDITOR                         |
|             (ImGui + OpenGL 2, src/main.cpp)             |
|                                                          |
|  Import FBX/PNG/WAV --> edit scenes --> export to DC     |
+---------------------------------------------------------+
|                     Source Modules                        |
|                                                          |
|  math/    camera/   io/       nodes/    scene/           |
|  editor/  ui/       viewport/ runtime/  vmu/             |
+---------------------------------------------------------+

                  ASSET PIPELINE
  FBX/PNG/WAV                        Dreamcast staging
  ----------                         ------------------
  .fbx  -----> editor import ------> .nebmesh  (Mxxxxx)
  .png  -----> texture convert -----> .nebtex   (Txxxxx)
  .fbx  -----> animation bake -----> .nebanim  (Axxxxx)
  scene -----> scene serialize -----> .nebscene (Sxxxxx)

           DREAMCAST THREE-LAYER RUNTIME

  +---------------------------------------------------+
  |  Scripts/*.c                                       |
  |  NB_Game_OnStart / OnUpdate / OnSceneSwitch        |
  |  Calls NB_RT_* bridge functions                    |
  +---------------------------------------------------+
                        |
                        v
  +---------------------------------------------------+
  |  Generated Runtime  (build_dreamcast/main.c)       |
  |  Implements NB_RT_* functions                      |
  |  Calls NB_DC_* and NB_KOS_* APIs                  |
  +---------------------------------------------------+
                        |
                        v
  +---------------------------------------------------+
  |  Platform Bindings  (src/platform/dreamcast/)      |
  |  KosBindings.c/h   KosInput.c/h                   |
  +---------------------------------------------------+
                        |
                        v
  +---------------------------------------------------+
  |  Dreamcast Hardware  (SH4 + PowerVR2 + AICA)      |
  +---------------------------------------------------+
```

---

## Full Directory Listing

### `getting-started/` -- First steps and core concepts

| File | Description |
|------|-------------|
| [Quickstart_Tutorial.md](getting-started/Quickstart_Tutorial.md) | Step-by-step: build, create a project, write a script, run on Dreamcast |
| [Source_Directory_Structure.md](getting-started/Source_Directory_Structure.md) | Map of the repository: every folder and its purpose |
| [Editor_Core.md](getting-started/Editor_Core.md) | Architecture of the ImGui editor monolith (src/main.cpp) |
| [Nebula_Dependencies_and_Paths.md](getting-started/Nebula_Dependencies_and_Paths.md) | Build dependencies, DreamSDK paths, CMake configuration |
| [Scripting.md](getting-started/Scripting.md) | Writing gameplay scripts in C for the Dreamcast runtime |
| [Script_Formatting_Syntax_Semantics.md](getting-started/Script_Formatting_Syntax_Semantics.md) | Script syntax rules, formatting constraints, and semantics |
| [Multi_Script_Runtime.md](getting-started/Multi_Script_Runtime.md) | Multi-script support: per-scene assignment and execution model |
| [Dreamcast_Binding_API_v2.md](getting-started/Dreamcast_Binding_API_v2.md) | Complete NB_RT_* / NB_DC_* / NB_KOS_* function reference |

### `scene-and-nodes/` -- Scene graph and node types

| File | Description |
|------|-------------|
| [Node_Types.md](scene-and-nodes/Node_Types.md) | StaticMesh3DNode, Camera3DNode, Node3DNode, Audio3DNode definitions |
| [Scene_System.md](scene-and-nodes/Scene_System.md) | Scene serialization, hierarchy, parent chains, cycle detection |
| [Camera_System.md](scene-and-nodes/Camera_System.md) | Editor viewport camera and runtime camera behavior |

### `assets/` -- Asset pipeline and file formats

| File | Description |
|------|-------------|
| [Asset_Pipeline.md](assets/Asset_Pipeline.md) | End-to-end flow from source assets to Dreamcast disc |
| [Mesh_and_Materials.md](assets/Mesh_and_Materials.md) | Mesh import (Assimp), material slots (up to 14), texture binding |
| [Animation_System.md](assets/Animation_System.md) | Vertex animation baking, per-slot loop toggles, .nebanim format |
| [File_Formats.md](assets/File_Formats.md) | Binary specifications for all .neb* file formats |

### `gameplay/` -- Runtime gameplay systems

| File | Description |
|------|-------------|
| [Physics_and_Collision.md](gameplay/Physics_and_Collision.md) | Wall collision, ground snapping, Node3D-vs-Node3D AABB overlap |
| [Navigation.md](gameplay/Navigation.md) | NavMesh building with Recast and pathfinding with Detour |

### `dreamcast/` -- Dreamcast-specific documentation

| File | Description |
|------|-------------|
| [Dreamcast_Export.md](dreamcast/Dreamcast_Export.md) | Packaging workflow, staging layout (S/M/T/A prefixes), disc build |
| [Dreamcast_Binding_API.md](dreamcast/Dreamcast_Binding_API.md) | Complete NB_RT_*, NB_DC_*, NB_KOS_* function reference |
| [Dreamcast_Header_Reference.md](dreamcast/Dreamcast_Header_Reference.md) | Generated header structs, constants, and type definitions |

### `editor/` -- Editor-specific documentation

| File | Description |
|------|-------------|
| [Editor_UI.md](editor/Editor_UI.md) | Panel layout, inspector panel, property editing workflows |
| [Editor_Play_Mode.md](editor/Editor_Play_Mode.md) | Play-mode scene switching and runtime simulation in the editor |
| [Viewport_and_Rendering.md](editor/Viewport_and_Rendering.md) | Viewport navigation, OpenGL 2 rendering, draw pipeline |
| [VMU_Tool.md](editor/VMU_Tool.md) | VMU LCD image tool for Dreamcast Visual Memory Unit |

### `reference/` -- API and math reference

| File | Description |
|------|-------------|
| [Math_Reference.md](reference/Math_Reference.md) | Vector, matrix, and quaternion types; coordinate system conventions |

---

## Learning Path

New to the engine? See the [Learning Path](Learning_Path.md) for a recommended reading order.
