# Scene System

This document explains how the Nebula Dreamcast Engine represents, serializes, and manages scenes. It covers the in-memory data model, the `.nebscene` text format, the serialization API, multi-scene editing, play-mode scene switching, unsaved change detection, and asset reference propagation on rename.

Source files referenced throughout:

- `src/nodes/NodeTypes.h` -- SceneData struct and node type definitions
- `src/scene/scene_io.h` / `scene_io.cpp` -- serialization, token encoding, rename propagation
- `src/scene/scene_manager.h` / `scene_manager.cpp` -- multi-scene state, save/load orchestration
- `src/editor/editor_state.h` -- global node vectors and scene state variables

---

## 1. What Is a Scene

A scene is a collection of nodes that together describe one level, room, or screen of a game. In code, it is represented by the `SceneData` struct defined in `src/nodes/NodeTypes.h`:

```cpp
struct SceneData
{
    std::filesystem::path path;   // absolute path to the .nebscene file on disk
    std::string name;             // display name (derived from file stem)
    std::vector<Audio3DNode> nodes;
    std::vector<StaticMesh3DNode> staticMeshes;
    std::vector<Camera3DNode> cameras;
    std::vector<Node3DNode> node3d;
    std::vector<NavMesh3DNode> navMeshes;
};
```

A scene holds five separate vectors, one per node type:

| Vector | Node type | Purpose |
|---|---|---|
| `nodes` | `Audio3DNode` | Positional audio emitters with inner/outer radius and volume |
| `staticMeshes` | `StaticMesh3DNode` | Renderable meshes with materials, collision, and animation slots |
| `cameras` | `Camera3DNode` | Perspective/orthographic cameras with priority and orbit offsets |
| `node3d` | `Node3DNode` | Generic transform nodes used for gameplay logic, physics, collision bounds |
| `navMeshes` | `NavMesh3DNode` | Navigation mesh volumes and negators for pathfinding |

Every node carries at minimum a `name`, position (`x`, `y`, `z`), rotation (`rotX`, `rotY`, `rotZ`), scale, and an optional `parent` string that establishes a scene hierarchy.

---

## 2. Scene File Format (.nebscene)

Scene files are plain text. Each line is either a header or a node record. The format is designed for human readability and simple parsing -- no binary encoding, no JSON/XML overhead.

### Header

The first meaningful line declares the scene name:

```
scene=MyLevel
```

The name is the file stem (filename without extension). On load, `LoadSceneFromPath` reads this and stores it in `SceneData::name`.

### Node records

Each subsequent line starts with a type tag followed by space-separated fields:

```
Audio3D <name> <script> <x> <y> <z> <innerRadius> <outerRadius> <baseVolume> <rotXYZ> <scaleXYZ> <parent>
StaticMesh <name> <script> <material> <mesh> <x> <y> <z> <rotXYZ> <scaleXYZ> <materialSlot> <slots...> <parent> <flags...> <animSlots...>
Camera3D <name> <x> <y> <z> <rotXYZ> <orbitXYZ> <perspective> <fovY> <nearZ> <farZ> <orthoWidth> <priority> <main> <parent>
Node3D <name> <x> <y> <z> <rotXYZ> <scaleXYZ> <parent> <primitiveMesh> <script> <collisionSource> <physicsEnabled> <extentXYZ> <boundPosXYZ> <simpleCollision>
NavMesh3D <name> <x> <y> <z> <rotXYZ> <scaleXYZ> <extentXYZ> <navBounds> <navNegator> <cullWalls> <wallCullThreshold> <parent> <wireRGB> <wireThickness>
```

Parsing is positional, not key-value. The loader reads tokens left to right using `std::istringstream`. Some node types (notably `StaticMesh` and `Camera3D`) have grown over time and include backward-compatible detection for older formats with fewer fields.

### Token encoding: EncodeSceneToken / DecodeSceneToken

Because the format is space-delimited, empty strings would collapse and shift all subsequent fields out of position. The engine solves this with a simple encoding scheme in `src/scene/scene_io.cpp`:

```cpp
std::string EncodeSceneToken(const std::string& s)
{
    return s.empty() ? "-" : s;
}

void DecodeSceneToken(std::string& s)
{
    if (s == "-") s.clear();
}
```

When writing a scene, every string field that could be empty (script paths, material paths, parent names) is passed through `EncodeSceneToken`. A lone hyphen `-` stands for "no value." When reading, `DecodeSceneToken` converts `-` back to an empty string.

This means you will see lines like:

```
Audio3D Footsteps - 10 0 5 2 8 0.75 0 0 0 1 1 1 PlayerNode
```

Here, `-` in the script position means the node has no attached script. `PlayerNode` in the parent position means this audio node is parented to `PlayerNode`.

---

## 3. Scene Serialization

Three functions in `src/scene/scene_io.cpp` (namespace `NebulaScene`) handle writing and reading scenes.

### BuildSceneText

```cpp
std::string BuildSceneText(
    const std::filesystem::path& path,
    const std::vector<Audio3DNode>& nodes,
    const std::vector<StaticMesh3DNode>& statics,
    const std::vector<Camera3DNode>& cameras,
    const std::vector<Node3DNode>& node3d,
    const std::vector<NavMesh3DNode>& navMeshes = {});
```

This produces the complete text content of a `.nebscene` file as a `std::string`, without writing to disk. It starts with the `scene=` header (using `path.stem()` for the name), then iterates each node vector and writes one line per node with all fields encoded.

`BuildSceneText` is also used by the unsaved-change detector (see section 6), which compares the text it would write against what is currently on disk.

### SaveSceneToPath

```cpp
void SaveSceneToPath(const std::filesystem::path& path, ...);
```

Two overloads exist. Both open the file at `path` for writing (truncating any existing content), call `BuildSceneText`, and write the result. The simpler overload takes only audio nodes (for legacy compatibility); the full overload takes all five node vectors.

### LoadSceneFromPath

```cpp
bool LoadSceneFromPath(const std::filesystem::path& path, SceneData& outScene);
```

Opens the file, clears all vectors in `outScene`, sets `outScene.path` and `outScene.name`, then reads line by line. Each line is dispatched by its type tag (`Audio3D`, `StaticMesh`, `Camera3D`, `Node3D`, `NavMesh3D`). Fields are parsed positionally, and string fields are run through `DecodeSceneToken`.

For `Node3D` nodes, the loader also calls an internal helper `SyncNode3DQuatFromEuler` to compute quaternion rotation from the Euler angles stored on disk, keeping the runtime quaternion fields in sync.

The `Camera3D` parser demonstrates the engine's approach to format evolution: it checks `toks.size()` to detect whether orbit offsets are present (18+ tokens = new format, 14+ tokens = intermediate format, fewer = legacy format) and fills defaults for missing fields.

Returns `true` on success, `false` if the file could not be opened.

---

## 4. Multi-Scene Management

The editor supports having multiple scenes open simultaneously, managed through global state in `src/editor/editor_state.h` and orchestrated by `src/scene/scene_manager.cpp`.

### Key globals

```cpp
extern std::vector<SceneData> gOpenScenes;   // all currently open scenes
extern int gActiveScene;                      // index into gOpenScenes
extern int gForceSelectSceneTab;              // UI hint to switch tab
```

The editor also maintains "working" node vectors that hold the nodes of whichever scene is currently active:

```cpp
extern std::vector<Audio3DNode>       gAudio3DNodes;
extern std::vector<StaticMesh3DNode>  gStaticMeshNodes;
extern std::vector<Camera3DNode>      gCamera3DNodes;
extern std::vector<Node3DNode>        gNode3DNodes;
extern std::vector<NavMesh3DNode>     gNavMesh3DNodes;
```

All editor UI -- the viewport renderer, the inspector, the outliner -- reads and writes these global vectors. They represent the active scene's nodes.

### SetActiveScene

When the user clicks a scene tab or a scene switch is triggered programmatically, `SetActiveScene(int index)` runs:

1. **Save current nodes back** -- copies the global node vectors into `gOpenScenes[gActiveScene]` so the outgoing scene's edits are preserved.
2. **Update gActiveScene** -- sets the new index.
3. **Load new nodes out** -- copies the incoming scene's node vectors from `gOpenScenes[index]` into the global vectors.
4. **Set gForceSelectSceneTab** -- tells the UI to visually highlight the new tab.
5. **Notify scripts** -- calls `NotifyScriptSceneSwitch()` so any running gameplay scripts know the scene changed.

This two-way sync pattern (globals <-> SceneData vectors) is central to the engine. The globals are the "live editing buffer," and the SceneData vectors in `gOpenScenes` are the "stored state."

### OpenSceneFile

```cpp
void OpenSceneFile(const std::filesystem::path& path);
```

Loads a `.nebscene` from disk. If the scene is already in `gOpenScenes`, it simply activates it. Otherwise, it appends a new `SceneData` and activates it.

### SaveActiveScene / SaveAllProjectChanges

`SaveActiveScene` syncs the global node vectors back into the active scene's `SceneData`, then calls `SaveSceneToPath` to write to disk.

`SaveAllProjectChanges` does the same for every open scene, iterating `gOpenScenes` and saving each one.

---

## 5. Play-Mode Scene Switching

When the editor enters play mode, it needs to simulate how the Dreamcast runtime handles scenes. On real hardware, switching scenes means reloading fresh data from disc -- there is no in-memory "dirty" state carried over.

The editor replicates this with `gPlayOriginalScenes`:

```cpp
extern bool gPlayMode;
extern std::vector<SceneData> gPlayOriginalScenes;
```

When play mode starts, the engine snapshots all open scenes into `gPlayOriginalScenes`. During play mode, `SetActiveScene` checks `gPlayMode` and, if true, loads node data from the original snapshot rather than from `gOpenScenes`:

```cpp
if (gPlayMode && index < (int)gPlayOriginalScenes.size())
{
    gAudio3DNodes = gPlayOriginalScenes[index].nodes;
    gStaticMeshNodes = gPlayOriginalScenes[index].staticMeshes;
    // ... etc
}
```

This prevents a common bug: if a player walks to a trigger zone in Scene A and switches to Scene B, then switches back to Scene A, the player would still be standing on the trigger zone with stale runtime positions, causing an immediate re-trigger. By reloading from the original snapshot, every scene switch starts fresh, matching Dreamcast disc behavior.

When play mode ends, a separate snapshot/restore mechanism (`SnapshotPlaySceneState` / `RestorePlaySceneState`, declared in `editor_state.h`) restores all editor state to what it was before play mode began.

---

## 6. Unsaved Change Detection

The function `HasUnsavedProjectChanges` in `src/scene/scene_manager.cpp` determines whether any open scene has been modified since it was last saved:

```cpp
bool HasUnsavedProjectChanges()
{
    for (int i = 0; i < (int)gOpenScenes.size(); ++i)
    {
        // For the active scene, use the live global vectors
        // For inactive scenes, use the stored SceneData vectors
        std::string expected = NebulaScene::BuildSceneText(s.path, nodes, statics, cameras, node3d);

        // Read the file currently on disk
        std::ifstream in(s.path, std::ios::in | std::ios::binary);
        std::string current(...);

        if (current != expected) return true;
    }
    return false;
}
```

The approach is straightforward: for each open scene, generate what the file would contain if saved right now (`BuildSceneText`), then compare byte-for-byte against what is on disk. If any scene differs, there are unsaved changes.

Note the special handling for the active scene: it reads from the global node vectors (the live editing buffer), not from the SceneData stored in `gOpenScenes`, because the active scene's SceneData might not reflect the latest edits until the next `SetActiveScene` or save call.

---

## 7. Asset Reference Updates on Rename

When a user renames or moves an asset file (a mesh, texture, script, or material), every scene that references that asset needs to be updated. The function `UpdateAssetReferencesForRename` in `src/scene/scene_io.cpp` handles this comprehensively.

### What it updates

The function rewrites path references in three scopes:

1. **Global node vectors** (the active scene's live editing buffer) -- `gStaticMeshNodes`, `gAudio3DNodes`, `gNode3DNodes`.
2. **All open scenes** (`gOpenScenes`) -- updates the same fields in every open scene's stored SceneData.
3. **On-disk files** in the project's `Assets/` tree -- walks every `.nebscene`, `.nebmat`, and `.nebslots` file and rewrites matching path references.

### How path matching works

The helper `RewritePathRefForRename` converts paths to project-relative form, normalizes slashes to forward slashes, and checks for exact matches (for files) or prefix matches (for directories). When a directory is renamed, any asset path that started with the old directory prefix is updated to use the new prefix.

```cpp
bool RewritePathRefForRename(std::string& ref, const std::string& oldRel,
                              const std::string& newRel, bool isDir);
```

For files: the reference must exactly match `oldRel` to be rewritten.
For directories: any reference that starts with `oldRel/` gets its prefix replaced with `newRel/`.

### Fields that are checked

For `StaticMesh3DNode`: `script`, `material`, `mesh`, `vtxAnim`, all `materialSlots[]`, and all `animSlots[].path`.
For `Audio3DNode`: `script`.
For `Node3DNode`: `script`, `primitiveMesh`.

### On-disk rewriting

For `.nebscene` files on disk that are not currently open in the editor, the function loads each one, applies the rename to all node references, and re-saves it. For `.nebmat` and `.nebslots` files (which use a simple `key=value` text format), it does an in-place line-by-line rewrite via `RewriteRefTextFileInPlace`.

The function also updates `selectedAssetPath` (the currently selected item in the asset browser) so the UI selection follows the rename.

---

## Summary of Data Flow

```
.nebscene file on disk
        |
   LoadSceneFromPath()  -->  SceneData in gOpenScenes[i]
        |
   SetActiveScene(i)    -->  global node vectors (gStaticMeshNodes, etc.)
        |                        |
   [editor UI edits]            |
        |                        |
   SetActiveScene(j)    <--  save globals back to gOpenScenes[i]
        |
   SaveSceneToPath()    -->  BuildSceneText()  -->  .nebscene file on disk
```

The global node vectors are always the "live" copy of the active scene. Switching scenes saves the live copy back and loads the new scene out. Saving writes the live copy (or the stored copy for non-active scenes) to disk through the text serializer.
