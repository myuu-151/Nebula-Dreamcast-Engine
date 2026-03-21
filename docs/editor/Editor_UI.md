# Editor UI Panels

This document describes the ImGui-based UI panels that make up the Nebula Dreamcast Engine editor. All source files live under `src/ui/`.

---

## 1. Layout Overview

The editor window is divided into fixed regions:

```
+-------------------------------------------------------+
|  Toolbar (top bar, full width)                         |
+------------+---------------------------+---------------+
|            |  Scene Tabs               |               |
|  Scene     +---------------------------+   Inspector   |
|  Outliner  |                           |   (right)     |
|  (top-left)|       3D Viewport         |               |
|            |                           |               |
+------------+                           +---------------+
|  Assets    |                           |               |
|  (bot-left)|                           |               |
+------------+---------------------------+---------------+
```

- **Top toolbar** -- spans the full window width. Contains menu buttons, play mode, wireframe toggle, and window controls.
- **Left panel, upper half** -- Scene Outliner. Hierarchical tree of all nodes in the active scene.
- **Left panel, lower half** -- Assets panel. File browser rooted at the project's `Assets/` folder.
- **Right panel** -- Inspector. Shows properties for the selected node or asset.
- **Scene tabs** -- horizontal tab bar between the toolbar and the viewport, spanning the viewport width.
- **Viewport** -- the remaining center area where the 3D scene is rendered (handled outside `src/ui/`).

Panel widths and heights scale with `ImGui::GetIO().FontGlobalScale` (controlled by the UI Scale preference). All panels use `ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar` so they behave like fixed docked regions rather than free-floating windows.

---

## 2. Toolbar

**Source:** `src/ui/toolbar.h`, `src/ui/toolbar.cpp`

**Entry point:** `DrawToolbar()` -- called once per frame, returns the toolbar height in pixels.

### Buttons (left to right)

| Button | Action |
|--------|--------|
| App icon | Decorative; rendered from `uiIconTex` if non-zero. |
| **File** | Opens the `FileMenu` popup (see Main Menu below). |
| **Edit** | Opens the `EditMenu` popup. |
| **VMU** | Opens the `ToolsMenu` popup (VMU Tool). |
| **Package** | Opens the `PackageMenu` popup (Build Dreamcast). |
| **Play** | Toggles play mode. On enter: snapshots the editor camera and scene state, switches to the project's default scene (matching Dreamcast boot behavior), and starts the script runtime via `BeginPlayScriptRuntime()`. If MSVC is unavailable the play attempt is silently reverted. On exit: stops the runtime, restores camera and scene state. |
| **Wireframe** | Checkbox that toggles `gWireframePreview`. |

### Window controls (right side)

Three buttons pinned to the right edge of the toolbar:

- **\_** (underscore) -- Minimizes the window (`glfwIconifyWindow`).
- **[]** -- Toggles maximize/restore.
- **X** -- Close. If the VMU tool is open, it closes the VMU tool first. Otherwise, if there are unsaved changes (`HasUnsavedProjectChanges()`), a modal popup "ConfirmQuit" appears offering "Save All + Exit" or "Cancel". If no unsaved changes exist, the window closes immediately.

### Window drag

Clicking and dragging on empty space in the toolbar (not on any button) moves the window by tracking mouse deltas via `glfwSetWindowPos`. This is implemented with a `draggingWindow` flag that activates on mouse-down over the toolbar background and deactivates on mouse-up.

---

## 3. Main Menu

**Source:** `src/ui/main_menu.h`, `src/ui/main_menu.cpp`

**Entry point:** `DrawMainMenus()` -- called from inside `DrawToolbar()` after the toolbar buttons have been submitted. It draws the popup contents for each menu and the Preferences window.

### File popup (`FileMenu`)

| Item | Shortcut | Behavior |
|------|----------|----------|
| New Project... | Ctrl+Shift+N | Opens a folder picker, then calls `CreateNebulaProject()`. |
| Open Project... | Ctrl+Shift+O | Opens a `.nebproj` file picker. Clears all scene/node state, resets undo/redo, auto-loads linked VMU animation and load-on-boot flag from the project file, adds to recent projects list. |
| Close Project | -- | Clears project and all scene state. |
| Open Recent | -- | Submenu listing `gRecentProjects`. Selecting one performs the same open logic as Open Project. Shows "(none)" when the list is empty. |
| Save | Ctrl+S | Saves the active scene (`SaveActiveScene()`). |
| Save All | Ctrl+Shift+S | Saves all open scenes and project state (`SaveAllProjectChanges()`). |
| Enable Hot Reloading | -- | Turns on script hot reload (polls for `.c` file changes). Replaced by "Reload Scripts Now" and "Disable Hot Reloading" once active. |
| Preferences... | Ctrl+, | Opens the Preferences window. |
| Exit | Alt+F4 | Placeholder menu item (actual close logic is in the toolbar X button). |

### Edit popup (`EditMenu`)

| Item | Shortcut |
|------|----------|
| Undo | Ctrl+Z |
| Redo | Ctrl+Shift+Z |
| Cut / Copy / Paste | Ctrl+X / C / V (placeholders) |

### Tools popup (`ToolsMenu`)

| Item | Behavior |
|------|----------|
| VMU Tool | Sets `gShowVmuTool = true`, opening the VMU pixel-art animation editor. |

### Package popup (`PackageMenu`)

| Item | Behavior |
|------|----------|
| Build Dreamcast (NEBULA_DREAMCAST) | Calls `GenerateDreamcastPackage()` to generate `build_dreamcast/main.c`, the Makefile, and stage all assets under `cd_root/data/`. |

### Preferences window

Opened when `showPreferences` is true. Contains:

- **UI Scale** -- slider from 0.75 to 2.5, controls `FontGlobalScale`.
- **Theme** -- combo box with five options: Space, Slate, Arctic, Classic, Black.
- **Hide unselected wireframes** -- checkbox.
- **DreamSDK** -- text input for the DreamSDK home path. Shows green "OK" or red "missing" status.
- **MSVC** -- text input for the `vcvarsall.bat` path. The value is trimmed of leading/trailing whitespace, quotes, and equals signs. Shows green "OK" or red "missing" status based on `ResolveVcvarsPathFromPreference()`.
- **Save** button -- persists preferences via `SavePreferences()` and shows a toast.

---

## 4. Scene Outliner

**Source:** `src/ui/scene_outliner.h`, `src/ui/scene_outliner.cpp`

**Entry point:** `DrawSceneOutliner()` -- draws the upper-left panel titled "Scene".

### Node listing

Nodes are listed by type in order: Audio3D, StaticMesh3D, Camera3D, Node3D, NavMesh3D. Each type uses its own selection index (e.g., `gSelectedAudio3D`, `gSelectedStaticMesh`). Selecting a node of one type clears the selection of all other types and resets the transform gizmo state.

### Hierarchical display

Nodes that have children (detected by scanning all node lists for matching `parent` names) show a collapse/expand toggle button ("v" for expanded, ">" for collapsed). Collapsed state is tracked per-name in sets like `gCollapsedStaticRoots`, `gCollapsedAudioRoots`, etc.

StaticMesh3D nodes use a recursive `drawStaticNode()` lambda that renders children indented by `14.0f * depth` pixels. Root nodes are those with an empty parent or whose parent name does not match any existing node.

Camera3D nodes parented to a Node3D are hidden at the top level and shown under their Node3D parent branch instead.

### Context menu (right-click)

**On empty space** -- "Create Node" submenu offering: Audio3D, StaticMesh3D, Camera3D, Node3D, NavMesh3D. Each creation pushes an undo entry. New StaticMesh3D nodes auto-assign `cube_primitive.nebmesh` and run material slot auto-mapping. The first Camera3D created in a scene is automatically set as the main camera.

**On a node** -- Rename, Duplicate, Unparent (if parented), Unlink Hierarchy (clears own parent and all children's parents), Delete. Delete operations push undo entries and clean up parent references on remaining nodes.

### Drag-and-drop reparenting

Every node emits a drag-drop source payload tagged with its type (e.g., `SCENE_STATICMESH`, `SCENE_AUDIO3D`). Every node also acts as a drag-drop target that accepts payloads from all node types. Before applying a reparent, the code calls `WouldCreateHierarchyCycle()` to prevent circular parent chains. StaticMesh3D reparenting uses `ReparentStaticMeshKeepWorldPos()` to preserve world-space position when the parent changes.

---

## 5. Assets Panel

**Source:** `src/ui/assets_panel.h`, `src/ui/assets_panel.cpp`

**Entry point:** `DrawAssetsPanel()` -- draws the lower-left panel titled "Assets".

This panel is the container that hosts the asset browser (Section 6) and manages two modal popups: the rename modal and the FBX convert modal.

### Context menu (right-click on empty space)

| Item | Behavior |
|------|----------|
| Create Asset > Scene | Creates a new `.nebscene` file in the current directory. Pushes an undo entry. |
| Create Asset > Material | Creates a new `.nebmat` file with default properties (unlit, UV scale 1, no texture). Pushes an undo entry. |
| Import Asset | Opens a multi-file picker for `.fbx`, `.png`, `.nebanim`, `.vtxa` files. Calls `ImportAssetsToCurrentFolder()`. |
| Convert Asset | Opens a single-file picker for `.fbx`. Loads the FBX via Assimp and opens the Convert Asset popup. |
| New Folder | Creates a new folder. Pushes an undo entry. |

### Rename modal

A popup modal titled "RenameItem" with a text input and OK/Cancel buttons. On OK, calls `RenameAssetPath()`, pushes an undo entry, and calls `UpdateAssetReferencesForRename()` to fix up all material/mesh/scene references across open scenes and inspector paths. If the renamed file is a `.nebscene`, open scene tabs are updated.

### FBX Convert popup

A popup modal titled "Convert Asset" showing:

- The source FBX path.
- An expandable tree of animations found in the FBX, each with a checkbox to include/exclude.
- **Delta compress** checkbox -- stores frame deltas as int16 8.8 values (smaller files).
- **Duplicate sample rate (2x)** checkbox -- exports at 24 fps instead of 12 fps.
- **Include animation provenance mapping** checkbox -- enables exact vertex-to-bone mapping for preview.
- Target mesh path (if set from the mesh inspector).
- Format summary: `.nebanim` big-endian, 16.16 fixed-point, position-only frames.
- "Convert Animations" button -- exports checked animations to an `anim/` subfolder beside the mesh, with warnings for missing meshes or failed exports.

---

## 6. Asset Browser

**Source:** `src/ui/asset_browser.h`, `src/ui/asset_browser.cpp`

**Entry point:** `DrawAssetsBrowser()` -- called from `DrawAssetsPanel()`, renders the file listing.

### Directory navigation

The browser is rooted at `<project>/Assets/`. A `[..]` entry appears when the user has navigated into a subdirectory, clicking it moves up one level (clamped to the assets root). Directories are shown with a trailing `/` and can be double-clicked to enter. `.meta` sidecar files and `.fbx` source files are hidden.

### File display and selection

Files display their stem (without extension). Single-click selects the asset (sets `gSelectedAssetPath`). Double-click behavior depends on file type:

- `.nebscene` -- opens the scene in a new tab via `OpenSceneFile()`.
- `.nebmat` -- opens the material inspector. If the top inspector is already occupied by a node, the material opens in the bottom-half inspector instead.
- `.nebtex` -- opens the texture inspector, with the same top/bottom split logic.

### Default scene indicator

Scene files that match the project's default scene (set in `.nebproj`) display a yellow asterisk (*) prefix in the file listing.

### Inline rename

`BeginInlineAssetRename()` replaces the selectable label with an `InputText` widget. Pressing Enter or clicking away commits the rename via `CommitInlineAssetRename()`, which calls `RenameAssetPath()`, pushes an undo entry, and updates all asset references. Pressing Escape cancels.

### Per-item context menu

| Item | Condition | Behavior |
|------|-----------|----------|
| Open Scene | `.nebscene` only | Opens the scene. |
| Save Scene | `.nebscene` only | Saves the scene if it is the active scene. |
| Set as default scene | `.nebscene` only | Writes the default scene path into the project file. |
| Rename | All | Begins inline rename. |
| Duplicate | All | Creates a `_copy` suffixed duplicate. |
| Inspector | `.nebmat`, `.nebtex`, `.nebmesh`, `.nebanim` | Opens the appropriate inspector. |
| Save as .nebtex | `.png` only | Converts PNG to 16-bit big-endian `.nebtex`. |
| Save .nebtex | `.nebtex` only | Touch/write-through re-save. |
| Generate Material | `.nebtex` only | Creates a `.nebmat` file referencing this texture. |
| Delete | All | Moves the file to a temporary trash folder (`nebula_trash` in the system temp directory). Pushes an undo entry that can restore the file from trash. |

### Asset management functions

The module also exports utility functions used elsewhere:

- `DeleteAssetPath()` -- permanent deletion (used by undo/redo).
- `MoveAssetToTrash()` -- moves to temp trash with rename-then-copy fallback.
- `DuplicateAssetPath()` -- creates a `_copy` suffixed clone.
- `RenameAssetPath()` -- renames preserving extension.
- `UpdateAssetReferencesForRename()` -- walks all open scenes, node lists, inspector paths, and the current directory to rewrite stale references.
- `CreateSceneAsset()`, `CreateMaterialAsset()`, `CreateAssetFolder()` -- asset creation with unique name generation via `MakeUniqueAssetPath()`.

---

## 7. Inspector

**Source:** `src/ui/inspector.h`, `src/ui/inspector.cpp`

**Entry point:** `DrawInspectorPanel()` -- draws the right panel.

The inspector uses a "pinned selection" model: selecting a node pins it in the inspector until the user clicks the "x" close button or selects a different node. The close button clears all selections, all pinned indices, and all asset inspector state.

When both a node and an asset (material or texture) are selected simultaneously, the inspector splits into two halves: the node occupies the top half and the asset occupies the bottom half, each in its own scrollable child region.

### Audio3D node inspector

- **Name** -- editable text input.
- **Script** -- path to a `.c` script file, auto-prefixed with `Scripts/` if missing. "Load Script" button validates the path exists on disk.
- **Position** -- DragFloat3.
- **Rotation** -- three separate DragFloat fields (X, Y, Z). Supports live preview during rotate transform gizmo operations.
- **Scale** -- DragFloat3.
- **Inner Radius / Outer Radius** -- distance-based spatial audio falloff.
- **Base Volume** -- 0.0 to 1.0.
- **Pan / Volume** -- read-only computed values.

### StaticMesh3D node inspector

- **Name** and **Script** -- same as Audio3D.
- **Load Nebslot** -- button (">") that loads a `.nebslots` manifest from the currently selected asset, populating all material slots at once.
- **Materials** -- 14 slots (`kStaticMeshMaterialSlots`). Each slot has a ">" button to assign the currently selected `.nebmat` asset and a text input for manual path entry. Slot labels show the material stem name resolved from the project directory.
- **Mesh** -- ">" button to assign a `.nebmesh` from the current selection. Auto-assigns material slots from the mesh's `.nebslots` manifest when a mesh is assigned.
- **Load NebAnim (legacy)** -- assigns a `.nebanim` for legacy vertex animation.
- **Runtime test** -- checkbox.
- **Animation Slots** -- collapsible section with up to 8 slots (`kStaticMeshAnimSlots`). Each slot has:
  - Name text input.
  - Path assign button + text input for `.nebanim` path.
  - Play/Stop button with frame count and fps display.
  - Speed slider (0.0 to 2.0).
  - Loop checkbox.
  - "X" button to remove the slot.
  - "+ Add Animation Slot" button at the bottom.
- **Parent** -- display with Unparent button and "Reset Xform (keep world)" button.
- **Collision Source** -- checkbox, enables wall collision. When active, a Wall Threshold slider (0.0 to 1.0) appears.
- **Navmesh Ready** -- checkbox.
- **Position / Rotation / Scale** -- DragFloat3 fields.

### Camera3D node inspector

- **Name** -- editable.
- **Position** -- DragFloat3.
- **Parent** -- text input + Unparent button + "Reset Xform (keep world pos)" button.
- **Rotation** -- X, Y, Z DragFloat fields.
- **Orbit** -- DragFloat3, only enabled when a parent is set (parent acts as pivot point).
- **Perspective** -- checkbox. When perspective: FOV slider (5-170 degrees). When orthographic: Ortho Width drag + computed Ortho Height display.
- **Near / Far** -- clip plane distances.
- **Priority** -- float for camera sorting.
- **Main Camera** -- checkbox. Setting this to true clears it on all other cameras.
- **Set View To Camera** / **Set Camera To View** -- buttons that sync between the editor viewport camera and this Camera3D node's transform.

### Node3D node inspector

- **Name** and **Script** -- same pattern, with a ">" button to assign a `.c` script from the current asset selection.
- **Primitive** -- read-only display of the primitive mesh path.
- **Simple Collision** -- checkbox.
- **Collision Source (slope alignment)** -- checkbox.
- **Gravity** -- checkbox (`physicsEnabled`).
- **Parent** -- display + Unparent button.
- **Position / Rotation / Scale** -- DragFloat3 fields.
- **Collision Bounds (local)** -- XYZ Extents and Bounds Position DragFloat3 fields.

### NavMesh3D node inspector

- **Name** -- editable.
- **Position / Rotation / Scale** -- DragFloat3 fields.
- **Extents** -- DragFloat3 for the bounding volume.
- **Nav Bounds / Nav Negator** -- checkboxes.
- **Cull Walls** -- checkbox, with a Wall Cull Threshold slider when active.
- **Wireframe Color** -- ColorEdit3.
- **Wireframe Thickness** -- DragFloat.

### Material sub-inspector

Appears when a `.nebmat` is selected (either in the top or bottom half):

- **Texture Assignment** -- ">" button to assign a `.nebtex` from selection + text input. Shows "OK" or "Missing" status.
- **U Scale / V Scale** -- DragFloat fields for UV transform.
- **Shading** -- combo: Unlit or Lit. When Lit:
  - Light X / Light Y -- directional light rotation/pitch in degrees.
  - Shadow Intensity -- slider 0.0 to 1.0.
  - Vertex Shading UVs -- combo: None, UV0, UV1 (options limited by the mesh's actual UV layer count).

### Texture sub-inspector

Appears when a `.nebtex` is selected:

- Filename and dimensions display.
- 128x128 texture preview image.
- **Wrap Mode** -- Repeat or Extend.
- **Filter** -- Nearest or Bilinear.
- **NPOT** -- Pad or Resample (non-power-of-two handling for Dreamcast).
- **Flip U / Flip V** -- checkboxes.
- Changing any setting immediately flushes the texture cache and updates the viewport preview.

---

## 8. Scene Tabs

**Source:** `src/ui/scene_tabs.h`, `src/ui/scene_tabs.cpp`

**Entry point:** `DrawSceneTabs()` -- draws the horizontal tab bar above the viewport.

Each open scene gets a tab labeled with its name. Tab behavior:

- **Click** -- switches to that scene by calling `SetActiveScene()`, which swaps all node lists (audio, static mesh, camera, Node3D, NavMesh3D) to the clicked scene's stored data.
- **Close** (the small X on the tab) -- removes the scene from `gOpenScenes`. If it was the active scene, the active scene index is reset. If no scenes remain, all node lists and selection indices are cleared.
- **Right-click > "Set as default scene"** -- calls `SetProjectDefaultScene()` to write the scene path into the project file. This scene will be loaded first on Dreamcast boot and when entering play mode.

A `gForceSelectSceneTab` mechanism allows other code (such as the play-mode scene switch) to programmatically activate a specific tab.

Tab colors use a custom dark style matching the toolbar buttons.

---

## 9. Mesh Inspector

**Source:** `src/ui/mesh_inspector.h`, `src/ui/mesh_inspector.cpp`

**Entry point:** `DrawNebMeshInspectorWindow()` -- a free-floating ImGui window (not docked), opened via `OpenNebMeshInspector()` or `OpenNebAnimInspector()`.

This inspector provides detailed mesh and animation analysis, distinct from the right-panel inspector.

### Mesh display

- Mesh path input with ">" assign button.
- Metadata paths: `.meta` sidecar path and `.animmeta` embedded animation metadata path.
- Mesh stats from the loaded `NebMesh`: vertex count, index count, UV layer count.

### Embedded animation source

When `.animmeta` exists alongside the mesh, the inspector loads it and attempts to locate the source FBX file:

- Shows source FBX path and "OK" / "Missing" status.
- Mapping quality indicator: "Exact (Provenance) [OK]", "Approximate [WARN]", or "Missing [ERR]". Approximate mapping is disabled by default to prevent corrupted previews.
- **Embedded Clip** combo -- selects which animation clip to preview.
- **Rebuild Animation Mapping** button -- recomputes vertex-to-bone provenance mapping.
- **Convert Animations** button -- opens the FBX convert popup pre-filled with this mesh's source FBX.

### Playback modes

A "Playback Mode" combo with three options:

- **EmbeddedExact** -- uses provenance mapping from the `.animmeta` for accurate skeletal animation baking.
- **EmbeddedApprox** -- uses approximate vertex matching (can be force-enabled with a checkbox).
- **ExternalLegacy** -- uses a `.nebanim` clip directly (frame-by-frame vertex positions).

### Playback controls

- Play/Pause/Stop buttons.
- Frame slider and frame counter.
- Loop toggle.
- FPS display (default 12 fps).

### Bone diagnostics

When using embedded playback:

- Debug animation diagnostics toggle with probe frame input and "Dump now" button.
- Diagnostic output: total bones, matched bones, unmatched bones, channels found, max vertex delta from frame 0.
- Warning toast when a clip produces near-static deformation (indicating bone/channel mismatch).

### Mini 3D wireframe preview

`DrawNebMeshMiniPreview()` renders an interactive wireframe view of the mesh inside an ImGui canvas:

- Dark background rectangle with a border.
- Wireframe edges drawn by projecting 3D vertices to 2D screen space using a simple perspective projection with yaw/pitch rotation.
- **Right-drag** rotates the view (yaw and pitch, pitch clamped to +/-1.3 radians).
- **Mouse wheel** zooms in/out (zoom factor 0.2 to 5.0).
- Optionally displays posed vertices from animation playback.
- Can render an optional helper point (e.g., for debugging collision bounds).

### Other functions

- `OpenNebMeshInspector()` -- opens the inspector for a `.nebmesh` file, loading the mesh and its embedded metadata.
- `OpenNebAnimInspector()` -- opens the inspector targeting a `.nebanim` file, loading the linked mesh via `.vtxlink` sidecar.
- `RefreshNebMeshInspectorClipIfNeeded()` -- reloads the animation clip when the source path changes.
- `GetMeshUvLayerCountForMaterial()` -- looks up how many UV layers the mesh that uses a given material has (used by the material inspector's "Vertex Shading UVs" combo).

## See Also

- [Editor Core](../getting-started/Editor_Core.md) -- editor setup and project management
- [Viewport and Rendering](Viewport_and_Rendering.md) -- 3D viewport and rendering pipeline
- [Scene System](../scene-and-nodes/Scene_System.md) -- scene serialization and multi-scene management
- [Mesh and Materials](../assets/Mesh_and_Materials.md) -- mesh import, material slots, and textures

---

## 10. Import Pipeline

**Source:** `src/ui/import_pipeline.h`, `src/ui/import_pipeline.cpp`

This module handles FBX import via Assimp and the generation of Nebula-format assets.

### Core import flow

`ImportAssetsToCurrentFolder()` accepts a list of file paths from the file picker and processes them based on extension:

- `.fbx` -- full mesh + material + texture import pipeline.
- `.png` / image files -- converted to `.nebtex` via `ExportNebTexturePNG()`.
- `.nebanim` / `.vtxa` -- copied directly into the current asset folder.

### FBX mesh import

When an FBX file is imported:

1. Assimp loads it with `aiProcess_Triangulate | aiProcess_JoinIdenticalVertices`.
2. The mesh is exported as `.nebmesh` (big-endian, 8.8 fixed-point, indexed, UV0 if present) via `ExportNebMesh()`.
3. `ImportModelTexturesAndGenerateMaterials()` is called to process materials.

### Material and texture generation

`ImportModelTexturesAndGenerateMaterials()` iterates over all materials in the Assimp scene:

1. For each material, `ResolveMaterialTexturePath()` attempts to find the texture file by checking (in order): `aiTextureType_BASE_COLOR`, `aiTextureType_DIFFUSE`, `aiTextureType_UNKNOWN`. Embedded FBX textures (paths starting with `*`) are not supported and produce a warning.
2. If the texture property lookup fails, `FindTextureByMaterialNameFallback()` searches the model's directory for an image file whose stem matches the material name (case-insensitive, with sanitization).
3. Found textures are converted to `.nebtex` format.
4. A `.nebmat` material file is generated for each material, referencing the converted texture.
5. A `.nebslots` manifest is generated listing all material slot assignments in order, enabling one-click material slot population in the inspector.

### Provenance mapping

When `gImportUseProvenanceMapping` is enabled (default: true), the import pipeline records which FBX mesh index and vertex index each output vertex came from. This provenance data is stored in the `.animmeta` sidecar alongside the `.nebmesh` and enables exact skeletal animation baking in the mesh inspector's embedded playback mode. Without provenance, animation preview falls back to approximate vertex matching which can produce incorrect deformations.

### .vtxlink sidecar files

- `LoadNebMeshVtxAnimLink()` -- reads a `.vtxlink` text file beside a `.nebmesh` to get the path to a linked `.nebanim`.
- `SaveNebMeshVtxAnimLink()` -- writes the `.vtxlink` sidecar.

These sidecars allow the mesh inspector to automatically locate the correct animation clip when opening a mesh for inspection.

### Animation export options

Animations are exported via `ExportNebAnimation()` with these configurable parameters:

- **Delta compression** (`gImportDeltaCompress`) -- stores per-frame vertex deltas as int16 8.8 values instead of absolute positions. Produces smaller files.
- **Double sample rate** (`gImportDoubleSampleRate`) -- exports at 24 fps instead of the default 12 fps. Useful for fast-moving animations.
- **Provenance mapping** (`gImportUseProvenanceMapping`) -- includes vertex-to-bone mapping data for exact animation preview.

Output format: `.nebanim`, big-endian, 16.16 fixed-point, position-only vertex frames.
