# Quickstart Tutorial

This guide walks you through your first project with the Nebula Dreamcast Engine, from building the editor to packaging a Dreamcast disc image.

## Prerequisites

- Windows 10 or later
- Visual Studio 2022 (with C++ desktop development workload)
- CMake 3.20+
- A 3D model in FBX, OBJ, or glTF format for testing

For Dreamcast builds you will also need DreamSDK installed. See [Nebula Dependencies and Paths](Nebula_Dependencies_and_Paths.md) for full dependency details.

---

## 1. Building the Editor

Open a terminal at the repository root and run:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

This generates the solution, fetches Assimp via FetchContent, and compiles the editor. Once the build succeeds, launch the editor:

```
./build/Debug/NebulaEditor.exe
```

See [Editor Core](Editor_Core.md) for an overview of the editor architecture.

---

## 2. Creating a Project

Go to **File > New Project**, choose an empty directory, and give your project a name. The editor creates a `.nebproj` file in that directory along with an `Assets/` folder for meshes, textures, and materials, and a `Scripts/` folder for gameplay code. Open an existing project at any time with **File > Open Project**. The project directory is the root for all relative asset paths.

---

## 3. Importing a Mesh

Drag and drop an FBX (or OBJ/glTF) file into the editor viewport, or use the asset import panel. The import pipeline uses Assimp to process the model and produces several Nebula-format files: a `.nebmesh` (geometry), one or more `.nebtex` (16-bit textures), a `.nebmat` (material definition), and a `.nebslots` manifest mapping material slots to textures. These files are written into your project's `Assets/` directory. Each StaticMesh supports up to 14 material slots.

See [Asset Pipeline](../assets/Asset_Pipeline.md) and [Mesh and Materials](../assets/Mesh_and_Materials.md) for details on formats and slot assignment.

---

## 4. Adding Nodes to a Scene

Right-click in the Outliner panel to create nodes. The engine provides four node types:

- **StaticMesh3DNode** -- A visible 3D mesh with material slots. Assign an imported `.nebmesh` to it.
- **Camera3DNode** -- Defines the viewpoint for play mode and Dreamcast export.
- **Node3DNode** -- An empty transform node useful for grouping, spawn points, or script-driven logic.
- **Audio3DNode** -- A positional audio source for 3D sound playback.

Nodes form a parent-child hierarchy. Select a node and use the Inspector panel to adjust its position, rotation, and scale. See [Node Types](../scene-and-nodes/Node_Types.md) and [Scene System](../scene-and-nodes/Scene_System.md) for full documentation.

---

## 5. Setting Up a Camera

Add a **Camera3DNode** to your scene by right-clicking in the Outliner. Position it so it has a clear view of your mesh. At least one Camera3DNode must exist in the scene for play mode to function and for Dreamcast export to succeed. The camera defines the initial viewpoint; scripts can reposition it at runtime using `NB_RT_SetCameraPosition` and related functions.

See [Camera System](../scene-and-nodes/Camera_System.md) for configuration options.

---

## 6. Writing Your First Script

Create a new `.c` file in your project's `Scripts/` folder (for example, `Scripts/game.c`). A minimal script looks like this:

```c
#include "nebula_game.h"

NB_SCRIPT_EXPORT void NB_Game_OnStart(void) {
    // Called once when play mode starts
}

NB_SCRIPT_EXPORT void NB_Game_OnUpdate(float dt) {
    // Called every frame; dt is the delta time in seconds
}
```

Scripts are plain C. Use `NB_RT_*` bridge functions to interact with the engine at runtime -- for example, `NB_RT_GetNodePosition` to read a node's position, `NB_RT_SetNodePosition` to move it, or `NB_RT_PlaySound` to trigger audio. You can also implement `NB_Game_OnSceneSwitch` to handle scene transitions. The `NB_SCRIPT_EXPORT` macro ensures functions are exported correctly on Windows (DLL) and Dreamcast.

See [Scripting](Scripting.md) for the full API reference and [Multi-Script Runtime](Multi_Script_Runtime.md) for working with multiple script files.

---

## 7. Running in Play Mode

Click the **Play** button in the editor toolbar. The editor compiles your script(s) into a DLL using MSVC, loads it, and enters play mode. The scene renders from your Camera3DNode's perspective, your `NB_Game_OnStart` function is called once, and then `NB_Game_OnUpdate` runs each frame. Physics, collision, and input are active during play mode. Press **Stop** to exit back to the editor.

See [Editor Play Mode](../editor/Editor_Play_Mode.md) for details on how the script DLL is built and loaded.

---

## 8. Packaging for Dreamcast

Go to **Package > Build Dreamcast** in the editor menu. This generates the `build_dreamcast/` directory containing:

- `main.c` -- the generated Dreamcast runtime (implements `NB_RT_*` bridge functions)
- A `Makefile` configured for the KOS toolchain
- `cd_root/data/` -- all assets converted to Dreamcast format with deterministic short names (`Sxxxxx` for scenes, `Mxxxxx` for meshes, `Txxxxx` for textures, `Axxxxx` for animations)

Navigate to `build_dreamcast/` and run `_nebula_build_dreamcast.bat` to compile the ELF binary and produce a `nebula_dreamcast.cdi` disc image. Test the CDI on a Dreamcast emulator (such as flycast or lxdream) or burn it to a disc for real hardware.

See [Dreamcast Export](../dreamcast/Dreamcast_Export.md) and [Dreamcast Binding API](../dreamcast/Dreamcast_Binding_API.md) for the full export pipeline and runtime API documentation.

---

## Next Steps

- Add collision volumes to your meshes -- see [Physics and Collision](../gameplay/Physics_and_Collision.md)
- Set up navmesh-based pathfinding -- see [Navigation](../gameplay/Navigation.md)
- Import vertex animations -- see [Animation System](../assets/Animation_System.md)
- Explore the full file format specifications -- see [File Formats](../assets/File_Formats.md)
