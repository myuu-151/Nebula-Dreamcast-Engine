# Learning Path

A recommended reading order for programmers new to the engine.

## 1. Get oriented

| Doc | What you will learn |
|-----|---------------------|
| [Source_Directory_Structure.md](getting-started/Source_Directory_Structure.md) | Repository layout, where every file lives |
| [Editor_Core.md](getting-started/Editor_Core.md) | How the desktop editor is structured |
| [Nebula_Dependencies_and_Paths.md](getting-started/Nebula_Dependencies_and_Paths.md) | Build dependencies, SDK paths, CMake options |

## 2. Understand scenes and nodes

| Doc | What you will learn |
|-----|---------------------|
| [Node_Types.md](scene-and-nodes/Node_Types.md) | StaticMesh3D, Camera3D, Node3D, Audio3D node types |
| [Scene_System.md](scene-and-nodes/Scene_System.md) | Scene hierarchy, serialization, parent chains |

## 3. Write gameplay scripts

| Doc | What you will learn |
|-----|---------------------|
| [Scripting.md](getting-started/Scripting.md) | Script entry points, NB_SCRIPT_EXPORT, basic API |
| [Script_Formatting_Syntax_Semantics.md](getting-started/Script_Formatting_Syntax_Semantics.md) | Script syntax rules and formatting requirements |
| [Multi_Script_Runtime.md](getting-started/Multi_Script_Runtime.md) | Running multiple scripts, per-scene script assignment |

## 4. Assets and rendering

| Doc | What you will learn |
|-----|---------------------|
| [Mesh_and_Materials.md](assets/Mesh_and_Materials.md) | Mesh import, material slots, texture assignment |
| [Animation_System.md](assets/Animation_System.md) | Vertex animation baking, animation slots, loop toggles |
| [Camera_System.md](scene-and-nodes/Camera_System.md) | Editor and runtime camera behavior |
| [Asset_Pipeline.md](assets/Asset_Pipeline.md) | End-to-end asset flow from source files to Dreamcast |

## 5. Gameplay systems

| Doc | What you will learn |
|-----|---------------------|
| [Physics_and_Collision.md](gameplay/Physics_and_Collision.md) | Wall collision, ground snap, AABB overlap checks |
| [Navigation.md](gameplay/Navigation.md) | NavMesh building and Detour pathfinding |

## 6. Dreamcast target

| Doc | What you will learn |
|-----|---------------------|
| [Dreamcast_Export.md](dreamcast/Dreamcast_Export.md) | Packaging a disc image, build flow, staging layout |
| [Dreamcast_Binding_API_v2.md](getting-started/Dreamcast_Binding_API_v2.md) | NB_RT_* / NB_DC_* / NB_KOS_* function reference |
| [Dreamcast_Header_Reference.md](dreamcast/Dreamcast_Header_Reference.md) | Generated header structs and constants |

## 7. Reference

| Doc | What you will learn |
|-----|---------------------|
| [File_Formats.md](assets/File_Formats.md) | .nebproj, .nebscene, .nebmesh, .nebtex, .nebmat, .nebanim specs |
| [Math_Reference.md](reference/Math_Reference.md) | Vector/matrix types, coordinate conventions |
| [Editor_UI.md](editor/Editor_UI.md) | Panel layout, inspector, property editing |
| [Viewport_and_Rendering.md](editor/Viewport_and_Rendering.md) | Viewport controls, OpenGL rendering pipeline |
