# Editor Core

This document teaches the core architecture of the Nebula Dreamcast Engine's desktop editor application. It covers the entry point, frame loop, global state, project management, preferences, file dialogs, undo/redo, hotkeys, and the two key per-session structs that tie everything together.

All source files referenced live under `src/editor/`.

---

## 1. Entry Point (main.cpp)

**Source:** `src/editor/main.cpp`

The editor starts in a standard `main()` function. The startup sequence is:

1. **GLFW initialization.** `glfwInit()` is called first. If it fails, the process exits immediately.

2. **Allocate session structs.** Two stack-local structs are created:
   - `EditorViewportNav nav` -- viewport camera controller (orbit, pan, zoom, WASD).
   - `EditorFrameContext ctx` -- per-session mutable state (UI scale, theme, icon texture).

3. **Load preferences.** `LoadPreferences(ctx.uiScale, ctx.themeMode)` reads `editor_prefs.ini` and populates the context with saved UI scale and theme mode values before the window is created.

4. **Create the GLFW window.** Window hints request an OpenGL 2.1 context with an 8-bit stencil buffer. The window is created undecorated (`GLFW_DECORATED = GLFW_FALSE`) at 1280x720 with the title "Nebula":

   ```cpp
   glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
   glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
   glfwWindowHint(GLFW_STENCIL_BITS, 8);
   glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
   GLFWwindow* window = glfwCreateWindow(1280, 720, "Nebula", nullptr, nullptr);
   ```

5. **Icon loading via WIC.** The Nebula logo icon (`assets/nebula_logo.ico`) is located using `ResolveEditorAssetPath`, then loaded through `LoadImageWIC` (Windows Imaging Component). The returned BGRA pixels are swizzled to RGBA and passed to `glfwSetWindowIcon`.

6. **OpenGL + ImGui setup.** The GL context is made current, vsync is enabled (`glfwSwapInterval(1)`), and Dear ImGui is initialized with the OpenGL2 + GLFW backends. The font global scale is set from `ctx.uiScale`.

7. **Callback wiring.** Two GLFW callbacks are installed:
   - `InstallViewportScrollCallback(window)` -- feeds mouse scroll deltas into `nav.scrollDelta`.
   - `InstallDropCallback(window)` -- pushes drag-and-dropped file paths into `gPendingDroppedImports`.

   The window's user pointer is set to `&nav` so the scroll callback can access the nav struct.

8. **UI icon texture.** The same logo `.ico` is loaded a second time via `LoadTextureWIC` as an OpenGL texture for use in the toolbar (`ctx.uiIconTex`).

9. **Main loop.** A simple while loop calls `TickEditorFrame(window, nav, ctx)` each iteration until the window is closed.

10. **Shutdown.** ImGui backends, ImGui context, GLFW window, and GLFW itself are torn down in order.

---

## 2. Frame Loop (frame_loop.h / frame_loop.cpp)

**Source:** `src/editor/frame_loop.h`, `src/editor/frame_loop.cpp`

`TickEditorFrame` is the single function that runs every frame. It orchestrates the entire editor tick in a fixed sequence:

### Input and Imports

- `glfwPollEvents()` processes OS events.
- If `gPendingDroppedImports` is non-empty, the dropped file paths are moved into a local copy, the global is cleared, and `ImportAssetsToCurrentFolder` is called to import them into the active asset folder.

### Delta Time

- Delta time is computed from `glfwGetTime()` and clamped to a maximum of 0.1 seconds to prevent large jumps after stalls.

### Script Hot-Reload

- `PollScriptHotReloadV1(now)` checks for source file changes.
- If `gScriptCompileState` is non-zero, `PollPlayScriptCompile()` checks compile progress.
- `TickPlayScriptRuntime(deltaTime, now)` drives the play-mode script runtime each frame.

### Viewport Navigation

- `TickEditorViewportNav(nav, window, deltaTime)` processes MMB orbit, RMB rotate, scroll zoom, and WASD roaming.

### GL Clear

- The framebuffer size is queried and the GL viewport is set.
- Color, depth, and stencil buffers are cleared to black.
- Scissor test is disabled and all write masks are enabled to ensure a clean clear.

### VMU Tool Branch

- If `gShowVmuTool` is true, the background is re-cleared to white and execution jumps directly to the ImGui-only rendering section via `goto RenderImGuiOnly`. This skips all 3D viewport rendering, because the VMU pixel editor has its own full-window UI.

### Camera Evaluation

- `EvaluateFrameCamera(nav, aspect, now)` returns a `FrameCameraResult` containing projection and view matrices, eye position, forward/up vectors, and a pointer to the active `Camera3DNode`. In play mode, it overrides the editor camera with the scene's main camera.

### Physics

- In play mode (when scripts are not compiling), `TickPlayModePhysics(dt)` runs Node3D gravity, per-triangle ground collision, wall collision, and slope alignment.

### Animation Tick

- In play mode, per-node animation playback is advanced. For each playing node, `gEditorAnimTime` is incremented by `dt * speed`. When the frame count is exceeded, the animation either loops (wrapping time back) or clamps at the last frame and stops, depending on `gEditorAnimLoop`.

### Transform Interaction

- `TickTransformInteraction` handles mouse-driven grab/rotate/scale of selected nodes using the camera's forward, up, and eye vectors.

### 3D Rendering

- GL projection and modelview matrices are loaded from the camera result.
- `DrawViewportBackground` renders the gradient, stars, nebula, grid, and axes (theme-dependent; suppressed in play mode).
- `DrawNodeGizmos` draws audio spheres, camera helpers, Node3D boxes, and NavMesh3D bounds.
- `RenderStaticMeshNodes` draws all StaticMesh3D geometry.

### ImGui Panels

After `ImGui::NewFrame()`:

- `TickCtrlShortcuts()` handles Ctrl+Z/Y, Ctrl+S, Ctrl+C/V.
- `DrawToolbar` renders the top toolbar with window controls, play button, and preferences toggle.
- `DrawSceneOutliner` renders the left-side scene hierarchy.
- `DrawAssetsPanel` renders the bottom-left asset browser.
- `DrawInspectorPanel` renders the right-side property inspector. Viewport camera bridge globals (`gViewYaw`, `gViewPitch`, `gViewDistance`, `gOrbitCenter`) are written from `nav` before the call and read back after, because the inspector can modify camera state (e.g., focus-on-selection).
- `DrawSceneTabs` renders scene tab bar between the left and right panels.
- `DrawVmuToolUI` renders the VMU pixel editor when active.
- `DrawNebMeshInspectorWindow` renders the mesh inspector overlay.

### Progress Bar

- When `gScriptCompileState == 1`, a centered progress bar is drawn on the foreground draw list showing "Compiling scripts... N / M".

### Toast Notifications

- If `gViewportToast` is non-empty and the current time is before `gViewportToastUntil`, a small text overlay is drawn in the top-left of the viewport area.

### Swap

- `ImGui::Render()` and `ImGui_ImplOpenGL2_RenderDrawData` finalize the ImGui frame, then `glfwSwapBuffers` presents the result.

---

## 3. Editor State (editor_state.h / editor_state.cpp)

**Source:** `src/editor/editor_state.h`, `src/editor/editor_state.cpp`

This module is the central global state hub. It declares `extern` globals that virtually every other editor module reads or writes. There are no classes or singletons -- it is a flat set of global variables organized by category.

### Node Vectors

Five vectors hold all nodes for the active scene:

| Vector | Node type |
|--------|-----------|
| `gAudio3DNodes` | `Audio3DNode` (defined in `nodes/Audio3D.cpp`) |
| `gStaticMeshNodes` | `StaticMesh3DNode` |
| `gCamera3DNodes` | `Camera3DNode` |
| `gNode3DNodes` | `Node3DNode` |
| `gNavMesh3DNodes` | `NavMesh3DNode` |

When the user switches scenes, these vectors are swapped with the data stored in `gOpenScenes[gActiveScene]`.

### Scene State

- `gOpenScenes` (`std::vector<SceneData>`) -- all currently open scenes.
- `gActiveScene` (`int`, default -1) -- index into `gOpenScenes` for the scene being edited.
- `gForceSelectSceneTab` -- used to programmatically switch the scene tab bar.
- `gPlayMode` (`bool`) -- true when the editor is in play mode.
- `gPlayOriginalScenes` -- backup of scene data taken when play mode starts, so changes made during play can be discarded.
- `gRequestDreamcastGenerate` -- flag to trigger Dreamcast code generation from the UI thread.
- `gSaveAllInProgress` -- flag to prevent re-entrant save operations.

### Selection Indices

Per-node-type selection indices (`gSelectedAudio3D`, `gSelectedStaticMesh`, `gSelectedCamera3D`, `gSelectedNode3D`, `gSelectedNavMesh3D`), all defaulting to -1 (nothing selected). Companion "pinned" indices (`gInspectorPinnedAudio3D`, etc.) allow the inspector to keep showing a node even after the outliner selection changes. `gInspectorSel` and `gInspectorName` track which node the inspector is currently displaying.

### Node Rename State

- `gNodeRenameIndex` / `gNodeRenameStatic` / `gNodeRenameCamera` / `gNodeRenameNode3D` -- which node and type is being renamed.
- `gNodeRenameBuffer[256]` -- editable name text.
- `gNodeRenameOpen` -- whether the rename popup is active.

### Transform Mouse State

`gLastTransformMouseX` / `gLastTransformMouseY` store the mouse position from the previous frame, used for computing deltas during grab/rotate/scale operations.

### Viewport Camera Bridge

Because the inspector panel can modify camera parameters (e.g., via focus-on-selection), a set of bridge globals shuttle camera state between `EditorViewportNav` and the ImGui panels:

- `gViewYaw`, `gViewPitch`, `gViewDistance`, `gOrbitCenter` -- copied from nav before drawing the inspector, read back after.
- `gEye` -- the computed eye position, set each frame by `EvaluateFrameCamera`.
- `gDisplayW`, `gDisplayH` -- framebuffer dimensions.

### Viewport Toast

- `gViewportToast` (`std::string`) -- text to display.
- `gViewportToastUntil` (`double`) -- GLFW time at which the toast expires.

### Rotate Preview

Temporary state for the rotation gizmo preview: `gHasRotatePreview`, `gRotatePreviewIndex`, and the preview Euler angles `gRotatePreviewX/Y/Z`.

### Animation Preview

Editor-time (non-play-mode) animation preview state for the inspector:
- `gStaticAnimPreviewPlay/Loop` -- play and loop toggles.
- `gStaticAnimPreviewTimeSec/Frame` -- current playback time and frame.
- `gStaticAnimPreviewNode/Slot/LastNode` -- which mesh and animation slot is being previewed.

### Play-Mode Animation Maps

Six `std::unordered_map<int, ...>` maps keyed by node index store per-node animation state during play mode:
- `gEditorAnimActiveSlot` -- which animation slot is playing.
- `gEditorAnimTime` -- elapsed playback time.
- `gEditorAnimSpeed` -- playback speed multiplier.
- `gEditorAnimPlaying` -- whether the animation is actively playing.
- `gEditorAnimFinished` -- whether the animation reached its end.
- `gEditorAnimLoop` -- whether to loop or clamp.

### Outliner Collapse Sets

Four `std::unordered_set<std::string>` sets track which root nodes are collapsed in the scene outliner: `gCollapsedAudioRoots`, `gCollapsedStaticRoots`, `gCollapsedCameraRoots`, `gCollapsedNode3DRoots`.

### Material/Texture Inspectors

- `gMaterialInspectorOpen` / `gMaterialInspectorPath` -- controls the material inspector popup.
- `gNebTexInspectorOpen` / `gNebTexInspectorPath` -- controls the texture inspector popup.

### Wireframe Flags

- `gWireframePreview` -- render meshes in wireframe mode.
- `gHideUnselectedWireframes` -- only show wireframe for the selected mesh.

### Play-Mode Scene Snapshot

When play mode starts, `SnapshotPlaySceneState()` copies all five node vectors, `gOpenScenes`, and `gActiveScene` into `playSaved*` variables. When play mode ends (Esc), `RestorePlaySceneState()` writes them back, discarding any changes made during play. The `playSceneSnapshotValid` flag guards against restoring uninitialized data.

---

## 4. Project Management (project.h / project.cpp)

**Source:** `src/editor/project.h`, `src/editor/project.cpp`

### Global Project State

- `gProjectDir` (`std::string`) -- absolute path to the open project's root directory.
- `gProjectFile` (`std::string`) -- absolute path to the `.nebproj` file.
- `gRecentProjects` (`std::vector<std::string>`) -- most-recently-opened project files, up to 10.

### CreateNebulaProject

`CreateNebulaProject(folder)` initializes a new project at the given folder path:

1. Creates the folder itself via `std::filesystem::create_directories`.
2. Extracts the project name from the folder name, stripping any accidental `.nebproj` or `.neb` extension suffix.
3. Creates three subdirectories: `Assets/`, `Scripts/`, `Intermediate/`.
4. Writes `Config.ini` with a `project=` line.
5. Writes a `.nebproj` file with a `name=` line. If a legacy `.neb` file exists, it is deleted.
6. Sets `gProjectDir` and `gProjectFile`, and adds the project to the recent list.

### Recent Projects

`AddRecentProject(projFile)` inserts the path at the front of `gRecentProjects`, removes any duplicate, and caps the list at 10 entries.

### Path Utilities

- **`GetExecutableDirectory()`** -- returns the directory containing the running `.exe`, using `GetModuleFileNameA`. Falls back to `std::filesystem::current_path()`.

- **`ResolveEditorAssetPath(relPath)`** -- locates an editor asset (like the logo icon) by searching up to 7 parent directories from both the current working directory and the executable directory. It also tries case-swapped variants of the `assets/` / `Assets/` prefix. Returns the first existing match, or an empty path if nothing is found.

- **`ToProjectRelativePath(p)`** -- converts an absolute path to a project-relative path using `std::filesystem::relative`. If `gProjectDir` is empty, falls back to just the filename.

- **`GetNebMeshMetaPath(absMeshPath)`** -- returns the animation metadata path for a mesh: `<meshDir>/animmeta/<meshStem>.animmeta.animmeta`.

### Config.ini Accessors

Project-level settings are stored in `Config.ini` as simple `key=value` lines:

- **Default scene:** `GetProjectDefaultScene` / `SetProjectDefaultScene` -- reads/writes the `defaultScene=` line. The setter converts absolute paths to project-relative before writing.

- **VMU config:** `GetProjectVmuLoadOnBoot` / `SetProjectVmuLoadOnBoot` -- the `vmuLoadOnBoot=` flag (0 or 1). `GetProjectVmuAnim` / `SetProjectVmuAnim` -- the `vmuLinkedAnim=` path.

All setters use a read-modify-write pattern: read all lines, find and replace the target line (or append if missing), then write all lines back.

---

## 5. Preferences (prefs.h / prefs.cpp)

**Source:** `src/editor/prefs.h`, `src/editor/prefs.cpp`

Editor preferences are stored in `editor_prefs.ini`, located next to the editor executable (not in the project directory). The path is resolved once via `GetModuleFileNameA` and cached.

### Globals

- `gPrefDreamSdkHome` (`std::string`, default `"C:\\DreamSDK"`) -- path to the DreamSDK installation.
- `gPrefVcvarsPath` (`std::string`) -- path to a Visual Studio `vcvarsall.bat` or `vcvars64.bat`.

### LoadPreferences / SavePreferences

`LoadPreferences(uiScale, themeMode)` reads the ini file line by line and parses four keys:
- `uiScale=` -- float, controls ImGui font scaling.
- `themeMode=` -- int, selects the viewport background theme (0 = space, 1 = solid, etc.).
- `spaceTheme=` -- legacy bool, mapped to themeMode for backward compatibility.
- `dreamSdkHome=` -- populates `gPrefDreamSdkHome`.
- `vcvarsPath=` -- populates `gPrefVcvarsPath` after trimming whitespace and quotes.

`SavePreferences(uiScale, themeMode)` writes all four keys to the ini file, truncating and overwriting.

### ResolveVcvarsPathFromPreference

Given a user-provided preference string, this function tries to locate a valid `vcvarsall.bat` or `vcvars64.bat`:

1. If the string is a path to an existing regular file, return it directly.
2. If it is a directory, check for `VC/Auxiliary/Build/vcvarsall.bat` and `VC/Auxiliary/Build/vcvars64.bat` underneath it.
3. If neither is found, search through 24 combinations of Visual Studio year (`2022`, `2019`, `18`, `17`) and edition (`Community`, `Professional`, `Enterprise`, `BuildTools`) as subdirectories.
4. Returns the first match found, or an empty path.

---

## 6. File Dialogs (file_dialogs.h / file_dialogs.cpp)

**Source:** `src/editor/file_dialogs.h`, `src/editor/file_dialogs.cpp`

All file dialogs use the Win32 COM `IFileDialog` API (not the legacy `GetOpenFileName`). Each function initializes COM with `CoInitializeEx(COINIT_APARTMENTTHREADED)`, creates a `CLSID_FileOpenDialog` instance, configures options and filters, calls `Show(nullptr)`, extracts the result path via `IShellItem::GetDisplayName(SIGDN_FILESYSPATH)`, converts from wide string to UTF-8 via `WideCharToMultiByte`, and cleans up COM objects. On non-Windows platforms, all functions return empty.

### Available Dialogs

| Function | Purpose | Filter |
|----------|---------|--------|
| `PickFolderDialog` | Choose a directory | `FOS_PICKFOLDERS` |
| `PickProjectFileDialog` | Open a `.nebproj` file | `*.nebproj` |
| `PickFbxFileDialog` | Open an FBX model | `*.fbx` + all files |
| `PickPngFileDialog` | Open a PNG image | `*.png` + all files |
| `PickVmuFrameDataDialog` | Open VMU frame data | `*.vmuanim` + all files |
| `PickImportAssetDialog` | Multi-select import | `*.fbx;*.nebanim;*.vtxa;*.png` + individual type filters + all files |

`PickImportAssetDialog` is the only multi-select dialog. It uses `FOS_ALLOWMULTISELECT`, queries `IFileOpenDialog::GetResults` for an `IShellItemArray`, and iterates all items to return a `std::vector<std::string>`.

---

## 7. Undo/Redo (undo.h / undo.cpp)

**Source:** `src/editor/undo.h`, `src/editor/undo.cpp`

### UndoAction Struct

Each undoable operation is represented by:

```cpp
struct UndoAction
{
    std::string label;              // Human-readable description (e.g. "Delete Audio3D")
    std::function<void()> undo;     // Lambda that reverses the action
    std::function<void()> redo;     // Lambda that re-applies the action
};
```

### Stacks

- `gUndoStack` (`std::vector<UndoAction>`) -- history of performed actions.
- `gRedoStack` (`std::vector<UndoAction>`) -- actions that have been undone and can be redone.

### Operations

- **`PushUndo(action)`** -- appends the action to `gUndoStack` and clears `gRedoStack`. This means performing any new action invalidates the redo history, which is standard undo/redo behavior.

- **`DoUndo()`** -- if a transform is currently active (`gTransformMode != Transform_None` or `gHasTransformSnapshot`), it finalizes the transform first by calling `EndTransformSnapshot()` and resetting `gTransformMode` and `gAxisLock`. Then it pops the last action from `gUndoStack`, calls its `undo` lambda, and pushes it onto `gRedoStack`.

- **`DoRedo()`** -- same finalization logic for active transforms, then pops from `gRedoStack`, calls the `redo` lambda, and pushes onto `gUndoStack`.

The finalization step is important: it ensures that if the user is mid-transform (e.g., dragging a node) and presses Ctrl+Z, the in-progress transform is committed before the undo stack is consulted.

---

## 8. Hotkeys (hotkeys.h / hotkeys.cpp)

**Source:** `src/editor/hotkeys.h`, `src/editor/hotkeys.cpp`

Hotkey processing is split into two functions that run at different points in the frame.

### TickTransformHotkeys (before ImGui::NewFrame)

Called before ImGui processes input, so it operates on raw GLFW key state. It uses edge detection (press on this frame, not held from previous frame) via static booleans for each key.

**Transform keys (G / R / S):**
- On edge-press, the current transform is finalized (`EndTransformSnapshot`).
- If the same mode is already active, it toggles off. Otherwise, the new mode is activated and a transform snapshot is begun (`BeginTransformSnapshot`).
- The axis lock is cleared on mode change.
- These keys are blocked when ImGui wants text input or when the mouse is over an ImGui window (`blockTransformKeys`).

**Axis lock keys (X / Y / Z):**
- Toggle the `gAxisLock` character between 0 and the pressed axis. Pressing X when already locked to X unlocks it; pressing X when locked to Y switches to X.

**Escape:**
- In play mode: exits play mode, clears `gPlayOriginalScenes`, calls `EndPlayScriptRuntime()`, restores the viewport camera via `nav.Restore()`, and restores the scene snapshot via `RestorePlaySceneState()`.
- Outside play mode: cancels the current transform (`CancelTransformSnapshot`), resets transform mode and axis lock, deselects audio nodes, and clears the rotate preview.

**Delete:**
- Deletes the currently selected node (checking Audio3D, StaticMesh3D, NavMesh3D, and asset browser selection in priority order). Each deletion pushes an `UndoAction` whose undo lambda re-inserts the node at its original index and whose redo lambda re-deletes it.

### TickCtrlShortcuts (after ImGui::NewFrame)

Called after ImGui processes input, so it uses `ImGui::IsKeyPressed` and `ImGuiIO::KeyCtrl` / `KeyShift`.

| Shortcut | Action |
|----------|--------|
| Ctrl+Z | `DoUndo()` |
| Ctrl+Shift+Z | `DoRedo()` |
| Ctrl+S | `SaveActiveScene()` |
| Ctrl+Shift+S | `SaveAllProjectChanges()` |
| Ctrl+C | Copy selected node (Audio3D or StaticMesh3D) into a static local |
| Ctrl+V | Paste the copied node with an incremented numeric suffix, push undo action |

Copy/paste uses static locals (`gHasCopiedNode`, `gCopiedNode`, `gHasCopiedStatic`, `gCopiedStatic`). The name increment logic finds the trailing digits in the node name, parses them as an integer, adds 1, and appends the new number. For example, pasting "Enemy3" produces "Enemy4".

---

## 9. EditorFrameContext Struct

**Source:** `src/editor/frame_loop.h`

`EditorFrameContext` holds per-session state that persists across frames but is not part of the scene or project:

```cpp
struct EditorFrameContext
{
    double lastTime = 0.0;              // Previous frame's glfwGetTime(), for delta time
    bool showPreferences = false;       // Whether the preferences window is open
    bool showViewportDebugTab = false;  // Whether the viewport debug overlay is shown
    float uiScale = 2.0f;              // ImGui font global scale
    int themeMode = 0;                  // Viewport background theme index
    unsigned int uiIconTex = 0;         // GL texture ID for the Nebula logo in the toolbar
};
```

This struct is allocated on the stack in `main()` and passed by reference to `TickEditorFrame` each frame. It is intentionally small and self-contained -- it does not hold scene data.

The default `uiScale` of 2.0 is overridden immediately by `LoadPreferences` if an `editor_prefs.ini` exists.

---

## 10. EditorViewportNav Struct

**Source:** `src/editor/viewport_nav.h`, `src/editor/viewport_nav.cpp`

`EditorViewportNav` is the editor's own camera controller, separate from any in-scene `Camera3DNode`. It supports orbit, pan, zoom, and WASD roaming.

### Fields

**Orientation and distance:**
- `orbitYaw`, `orbitPitch` -- angles for orbit mode (MMB drag).
- `viewYaw`, `viewPitch` -- angles for the actual view direction. In orbit mode these are kept equal to the orbit angles. In free-look mode (RMB drag) they diverge.
- `distance` -- distance from `orbitCenter` to the eye.
- `orbitCenter` -- the 3D point the camera orbits around.

**Drag state:**
- `lastX`, `lastY` -- cursor position from the previous frame.
- `dragging` -- true while MMB is held.
- `rotating` -- true while RMB is held.
- `viewLocked` -- when true, the view direction is computed to look at `orbitCenter`. Set to true on MMB orbit, set to false on RMB free-look.
- `scrollDelta` -- accumulated scroll wheel delta, consumed each frame to dolly the camera along the view direction.

**Play-mode snapshot:**
- `snapshotValid`, `savedOrbitYaw`, `savedOrbitPitch`, `savedViewYaw`, `savedViewPitch`, `savedDistance`, `savedOrbitCenter` -- a full copy of the camera state taken when play mode starts.
- `Snapshot()` copies current values into the saved fields.
- `Restore()` writes saved values back, so pressing Esc returns the editor camera to its pre-play-mode position.

### ComputeEye

Returns the eye position from the orbit parameters using spherical coordinates:

```
eye.x = orbitCenter.x - distance * cos(pitch) * cos(yaw)
eye.y = orbitCenter.y - distance * sin(pitch)
eye.z = orbitCenter.z - distance * cos(pitch) * sin(yaw)
```

### TickEditorViewportNav

Called once per frame. Handles:

- **MMB orbit:** On initial press, the orbit center is re-projected to where the view direction hits at `distance` from the eye. While held, mouse deltas rotate `orbitYaw/orbitPitch`, and `viewYaw/viewPitch` are locked to match.
- **RMB free-look:** Mouse deltas rotate `viewYaw/viewPitch` independently. `viewLocked` is set to false so the camera no longer auto-points at the orbit center.
- **Scroll zoom:** Dolly the orbit center forward/backward along the view direction by `scrollDelta * 0.5`.
- **WASD roaming (RMB held):** W/S move forward/backward on the XZ plane, A/D strafe left/right, E/Q move up/down. Move speed is 5 units/second, scaled by delta time.

Pitch is clamped to +/-89 degrees in both orbit and free-look modes.

### EvaluateFrameCamera

Called once per frame after `TickEditorViewportNav`. Returns a `FrameCameraResult`:

1. Builds a default 45-degree perspective projection.
2. Computes the eye from orbit parameters.
3. Finds the active `Camera3DNode` (highest priority with `main == true`, or the first camera if none is marked main).
4. In play mode with an active camera, builds the view and projection from the camera node's world-space transform, and overrides `nav.viewYaw/viewPitch` to match the camera's forward direction.
5. If `viewLocked` is true (and not in play-mode camera override), recomputes `viewYaw/viewPitch` to look at `orbitCenter`.
6. Builds the final view matrix (either from `NebulaCamera3D::BuildViewMatrix` in play mode or from `Mat4LookAt` in edit mode).

### InstallViewportScrollCallback

A one-line GLFW scroll callback that accumulates `yoffset` into `nav->scrollDelta`. It retrieves the nav pointer from `glfwGetWindowUserPointer`.

---

## How These Pieces Fit Together

The data flow each frame is:

1. `main()` calls `TickEditorFrame(window, nav, ctx)`.
2. `TickEditorFrame` polls input, advances time, ticks scripts.
3. `TickEditorViewportNav` updates `nav` from mouse/keyboard.
4. `EvaluateFrameCamera` reads `nav` + `gCamera3DNodes` + `gPlayMode` to produce matrices.
5. 3D rendering uses those matrices.
6. ImGui panels read/write `editor_state` globals and the camera bridge globals.
7. `TickCtrlShortcuts` handles undo/redo/save/copy/paste via the undo stack and scene manager.
8. Preferences are loaded once at startup and saved when the user closes the preferences window.
9. Project state (`gProjectDir`, `gProjectFile`) is set when a project is created or opened, and persists for the session.
10. Play mode uses `SnapshotPlaySceneState` + `nav.Snapshot()` on entry and `RestorePlaySceneState` + `nav.Restore()` on exit, so the editor returns to its pre-play state cleanly.
