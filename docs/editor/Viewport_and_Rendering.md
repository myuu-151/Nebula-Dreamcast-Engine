# Viewport and Rendering

This document describes the 3D viewport and rendering system used by the Nebula Dreamcast Engine editor. All source files discussed live under `src/viewport/`.

---

## 1. Overview

The editor viewport is an OpenGL 2.x immediate-mode renderer. There is a single 3D viewport with a Dear ImGui overlay on top. All rendering uses the fixed-function pipeline: `glBegin`/`glEnd` calls, `glVertex3f`, `glColor3f`, `glTexCoord2f`, and the built-in modelview/projection matrix stack. There are no shaders.

The viewport draws in this order each frame:

1. Background gradient, stars, nebula clouds, grid, and axes (`background.cpp`)
2. Node gizmos for Audio3D, Camera3D, Node3D, and NavMesh3D (`node_gizmos.cpp`)
3. StaticMesh geometry with textures, lighting, and overlays (`static_mesh_render.cpp`)
4. ImGui overlay (panels, inspectors, menus) rendered by the ImGui OpenGL2 backend

Selection and transform interaction run alongside the render loop, reading back the current GL matrices to project world positions to screen space.

---

## 2. Viewport Rendering Utilities

**Source:** `src/viewport/viewport_render.h`, `src/viewport/viewport_render.cpp`

This module provides shared helpers used by every other viewport subsystem.

### Texture Loading (`.nebtex` format)

`GetNebTexture(path)` is the main entry point. It checks the global cache `gNebTextureCache` (an `std::unordered_map<std::string, GLuint>` declared as an extern in `viewport_render.cpp`) and returns a cached GL texture handle if one exists. On a cache miss it calls `LoadNebTexture`.

`LoadNebTexture` reads the `.nebtex` binary format:

- **Header:** 4-byte magic `NEBT`, then four big-endian `uint16_t` values: width, height, format, flags.
- **Pixel data:** Only format 1 (RGB555) is supported. Each pixel is a big-endian 16-bit word. The loader unpacks 5-bit R/G/B channels to 8-bit by shifting left 3 and filling the low bits (`(c5 << 3) | (c5 >> 2)`), producing a full RGBA8 buffer with alpha set to 255.
- **GL upload:** The texture is created with `glGenTextures` and uploaded via `glTexImage2D`. Filter mode (nearest or linear) and wrap mode (repeat, clamp, or mirrored repeat) are read from the `.nebtex` metadata through `NebulaAssets::LoadNebTexFilterMode` and `NebulaAssets::LoadNebTexWrapMode`.

### 3D-to-2D Projection

```cpp
bool ProjectToScreenGL(const Vec3& world, float& outX, float& outY, float scaleX, float scaleY);
```

This function reads the current GL modelview and projection matrices with `glGetFloatv`, reads the viewport with `glGetIntegerv`, and manually multiplies the world-space point through the MVP pipeline to produce NDC coordinates. It rejects points behind the camera (NDC Z outside [-1, 1]). The resulting framebuffer pixel coordinates are divided by `scaleX`/`scaleY` to convert to ImGui screen coordinates (for HiDPI displays where framebuffer pixels differ from logical pixels).

This function is used by viewport selection (to project node origins and mesh triangles to screen space) and by any code that needs to draw ImGui overlays at 3D positions.

### Quaternion to GL Matrix

```cpp
void QuatToGLMatrix(float qw, float qx, float qy, float qz, float m[16]);
```

Converts a unit quaternion to a column-major 4x4 rotation matrix suitable for `glMultMatrixf`. Used when rendering parented nodes (Node3D parents store rotation as quaternions to avoid gimbal lock).

### Other Utilities

- `CreateCircleTexture(size)` generates a procedural soft-circle texture (white with radial alpha falloff) used for particle-like effects.
- `GetLocalAxes(Audio3DNode, right, up, forward)` computes local-space axis vectors from a node's Euler angles. Used by gizmo rendering to draw orientation indicators.

---

## 3. Background

**Source:** `src/viewport/background.h`, `src/viewport/background.cpp`

`DrawViewportBackground(themeMode, playMode)` renders the full viewport background. In play mode it draws solid black. Otherwise, it draws a vertical gradient quad whose colors depend on the theme.

### Theme Modes

| themeMode | Name    | Bottom Color | Top Color  |
|-----------|---------|-------------|------------|
| 0         | Space   | #08090C     | #14161C    |
| 1         | Slate   | #2A363D     | #6D7F89    |
| 2         | Classic | #162229     | #75A8B2    |
| 3         | Grey    | #5A595C     | #7F7E83    |
| other     | Black   | #000000     | #000000    |

The gradient is drawn as a fullscreen quad in orthographic projection (identity matrices) using `glBegin(GL_QUADS)` with per-vertex colors.

### Procedural Starfield

On the first call, 4000 stars are initialized on a sphere of radius 500 units. Each star gets:

- A random position on the sphere (uniform spherical distribution).
- A phase offset, twinkle speed, color tint (white or blue-white), and size bucket.

Stars are only drawn when `themeMode == 0` (Space). They render as `GL_POINTS` in three size buckets (3px, 6px, 10px) based on each star's random size value. Brightness varies per frame as `0.3 + 0.7 * (0.5 + 0.5 * sin(time * speed + phase))`, creating a twinkling effect.

### Nebula Clouds

400 nebula points are initialized on a sphere of radius 480 (slightly inside the star sphere). Each gets a random purple/blue tint. They render as 10px `GL_POINTS` at 60% alpha, only in Space theme.

### Ground Grid and Axes

A grid of grey lines is drawn on the Y=0 plane, spanning 10 units in each direction at 0.5 unit spacing (skipping the center lines). Two colored axis lines are drawn through the origin:

- **X axis:** Red (0.8, 0.2, 0.2), drawn at Y=0.001 to avoid Z-fighting with the grid.
- **Z axis:** Amber (0.95, 0.65, 0.1), same slight Y offset.

### Checker Overlay Texture

`gCheckerOverlayTex` is a 64x64 checkerboard texture created lazily on the first call to `DrawViewportBackground`. The pattern uses 8x8 pixel cells. Even cells are transparent; odd cells are opaque purple (RGB 120, 90, 235). This texture is used later by `static_mesh_render.cpp` to highlight the selected mesh with a purple checker overlay.

---

## 4. StaticMesh Rendering

**Source:** `src/viewport/static_mesh_render.h`, `src/viewport/static_mesh_render.cpp`

`RenderStaticMeshNodes()` iterates over all `gStaticMeshNodes` and draws each one. This is the most involved rendering path in the editor.

### Animation Resolution

For each mesh, the renderer determines which vertex animation clip to use, in priority order:

1. **Slot preview:** If the user is previewing a specific animation slot in the inspector (`gStaticAnimPreviewNode` / `gStaticAnimPreviewSlot`), use that slot's animation path.
2. **Play-mode active slot:** If in play mode and the mesh has `runtimeTest` enabled, use the slot indicated by `gEditorAnimActiveSlot`.
3. **Legacy `vtxAnim`:** Fall back to the mesh's single `vtxAnim` field (pre-slot-system).

When an animation clip is active, the renderer loads it via `LoadNebAnimClip` (cached in `gStaticAnimClipCache`), determines the current frame, and replaces the mesh's rest-pose positions with the animated positions. If the clip is marked `meshAligned`, positions are used directly. Otherwise, the renderer computes a bounding-box-based scale/offset to align the animation data to the mesh's rest pose.

### Per-Material State

Each triangle can reference a different material slot (via `mesh->faceMaterial`). The renderer builds a `MatState` struct for each slot, which includes:

- GL texture handle (loaded via `GetNebTexture`)
- UV flip flags (flipU, flipV) and UV scale/transform values
- Shading mode (0 = unlit, 1 = lit)
- Light direction parameters (rotation, pitch, roll) and shadow intensity
- Saturn sampling UV correction (optional retro-accuracy mode)

Materials can be `.nebtex` files (texture-only) or `.nebmat` files (material with texture reference, UV transform, and shading settings). All material slots are warmed up front with `getMatState` to avoid draw-order-dependent visibility issues.

### Smooth Normal Computation

When any material slot uses lit shading, the renderer computes smooth vertex normals via position welding:

1. Compute the mesh bounding box diagonal. The weld distance is 0.5% of this diagonal, so it adapts to any model scale.
2. Group vertices whose rest-pose positions are within the weld distance. This merges vertices that were split at UV/material seams.
3. Accumulate face normals (area-weighted by the cross product) into each group.
4. Normalize each group's accumulated normal. Every vertex in the group shares the same smooth normal.

The cross product is negated to compensate for the engine's handedness flip during import (X-axis negation reverses triangle winding).

These smooth normals are then transformed into camera space using the current modelview matrix, producing view-dependent shading that matches the Dreamcast runtime's behavior.

### Software Lighting

Lighting is computed in software (not via GL fixed-function lighting) to match the Dreamcast build exactly. For each lit vertex, the shading value is:

```
c = ambient + max(0, dot(normal, lightDir)) * diffuse
    - (normal.y < 0 ? -normal.y * shadowQuotient : 0)
    + (1 - |normal.z|) * 0.12
```

The light direction is computed from the material's yaw/pitch angles using spherical coordinates. Shadow intensity controls the balance between ambient and diffuse terms.

### Triangle Rendering

Triangles are emitted with `glBegin(GL_TRIANGLES)`. When the active material slot changes between triangles (different texture, different shading mode, or different light parameters), the renderer issues `glEnd()`, rebinds state, and starts a new `glBegin(GL_TRIANGLES)`. UV coordinates are transformed per-vertex according to the material's flip, scale, and Saturn sampling settings.

### World Transform

Each StaticMesh is positioned via `glTranslatef` for the world position, then rotation. If parented under a Node3D, the parent's quaternion rotation is applied via `QuatToGLMatrix` + `glMultMatrixf`, followed by the child's local Euler rotation. Standalone meshes use a remapped Euler order: X <- Z (pitch), Y <- X (yaw), Z <- Y (roll).

### Quad Wireframe Path

When wireframe mode is active (`gWireframePreview`) and the mesh has original face topology data (`mesh->hasFaceTopology`), the renderer draws polygon boundary edges using `GL_LINES` instead of triangulated wireframes. This avoids showing triangulation diagonals on quads and n-gons. Each polygon's edges are reconstructed from the face vertex counts and triangle fan indices. Wire colors are deterministic per material slot (hash-based RGB).

### NavMesh Overlay

For meshes marked `navmeshReady`, the renderer draws a colored wireframe overlay on triangles that fall inside any NavMesh3D bounds volume. It tests each triangle's world-space vertices against all positive (non-negator) NavMesh3D AABBs. Near-vertical faces inside wall-cull bounds are excluded based on their face normal's Y component versus the cull threshold. The overlay uses `GL_POLYGON_OFFSET_LINE` to prevent Z-fighting with the solid mesh.

### Selected Mesh Checker Overlay (Stencil Buffer)

When a StaticMesh is selected, a purple checkerboard overlay is composited onto its visible surface using the stencil buffer:

1. **Stencil write pass:** The selected mesh's triangles are rendered with color and depth writes disabled, depth test set to `GL_LEQUAL`, and `GL_POLYGON_OFFSET_FILL` with a slight bias. Fragments that pass depth test write 1 into the stencil buffer. This creates a screen-space silhouette mask of the visible portions of the mesh.
2. **Checker draw pass:** A fullscreen quad is drawn with `gCheckerOverlayTex` bound and the stencil function set to `GL_EQUAL, 1`. The UV coordinates are scaled so the checker pattern has 8-pixel cells regardless of viewport size. Alpha blending composites the semi-transparent purple pattern over the scene.
3. **Cleanup:** Stencil test is disabled, blend is turned off, and depth function is reset.

---

## 5. Node Gizmos

**Source:** `src/viewport/node_gizmos.h`, `src/viewport/node_gizmos.cpp`

`DrawNodeGizmos(activeCam)` renders wireframe indicators for all non-StaticMesh node types. The `activeCam` parameter is the camera currently driving the play-mode viewport; its gizmo is hidden to avoid visual clutter.

### Audio3D Gizmo

Each Audio3D node is drawn as three concentric wireframe spheres:

- **Outer radius sphere:** Blue (0.2, 0.6, 1.0), latitude/longitude rings (12 latitude, 16 longitude segments).
- **Inner radius sphere:** Green (0.1, 1.0, 0.4), same ring structure.
- **Core sphere:** Yellow (1.0, 0.9, 0.2), radius is 0.25 times the average scale.

A vertical line from the node's position upward by 1 unit serves as an additional locator.

Local axis indicators are drawn as short colored lines from the origin: red for X (right), green for Y (up), blue for Z (forward). Axes are computed from the node's Euler angles via `GetLocalAxes`.

When the node is selected during a rotate transform, the gizmo uses `gRotatePreviewX/Y/Z` to show a live preview of the rotation before it is committed.

When an axis lock is active during grab or rotate, an infinite colored guide line is drawn through the node along the locked world axis (2000 units in each direction, line width 2.5).

Unselected gizmos can be hidden via `gHideUnselectedWireframes`.

### Camera3D Gizmo

Each Camera3D node is drawn as a forward-pointing pyramid marker using `GL_LINES`:

- A line from the origin to (0, 0, 1) representing the view direction.
- Four lines from the origin to the corners of a rectangle at Z=0.7, offset by +/-0.3 in X and +/-0.2 in Y.

The marker is positioned and rotated using the camera's world transform from `GetCamera3DWorldTR`. Selected cameras are white; the main camera is brighter green (0.2, 1.0, 0.4); others are dimmer green (0.1, 0.8, 0.3).

### Node3D Gizmo

Each Node3D is drawn as a wireframe box (`GL_QUADS` in `GL_LINE` polygon mode). The box dimensions come from the node's collision extents (`extentX/Y/Z * 2.0`), offset by `boundPosX/Y/Z`. The transform uses quaternion rotation via `QuatToGLMatrix` to avoid gimbal lock, then applies world scale. Selected nodes are white; unselected are light cyan (0.55, 0.9, 1.0).

### NavMesh3D Gizmo

NavMesh3D bounds are drawn as wireframe boxes when `navBounds` is true. The box size is `scaleX * extentX` by `scaleY * extentY` by `scaleZ * extentZ`. Colors are: white when selected, red (1.0, 0.25, 0.25) for negator volumes, or the node's custom wire color (`wireR/G/B`) for positive volumes. Line thickness comes from `wireThickness`.

---

## 6. Viewport Selection

**Source:** `src/viewport/viewport_selection.h`, `src/viewport/viewport_selection.cpp`

`TickViewportSelection(window, mouseX, mouseY, scaleX, scaleY, mouseClicked)` runs the click-to-select logic once per frame. It only acts when `mouseClicked` is true (LMB just pressed).

### Selection Algorithm

The function iterates over all node types, computing a screen-space distance from the mouse cursor to each node. It tracks the closest candidate across all types.

**Audio3D, Camera3D, Node3D, NavMesh3D:** These use origin-point distance. The node's world position is projected to screen space via `ProjectToScreenGL`, and the Euclidean pixel distance to the mouse is computed.

**StaticMesh3D:** Uses triangle-based picking for precise selection. For each mesh:

1. Load the mesh geometry and compute its world transform (position, rotation, scale).
2. Build the rotation matrix. For parented meshes, this includes the parent Node3D's quaternion rotation followed by the child's local Euler rotation. For standalone meshes, Euler angles are applied with the engine's axis remap.
3. For each triangle, transform all three vertices to world space (scale, rotate, translate), project them to screen space, then run a 2D barycentric coordinate point-in-triangle test against the mouse position.
4. If the mouse falls inside any triangle, the mesh is selected with distance 0 (highest priority). The search short-circuits on the first hit.
5. If no triangle is hit, the mesh falls back to origin-point distance like other node types.

### Distance Threshold

If the best candidate's screen distance is under 80 pixels, it becomes selected. If 80 pixels or more, everything is deselected. When a selection changes, any active transform is cancelled and the last-mouse position is reset.

If a transform is already in progress (`gTransformMode != Transform_None`), clicking to select in empty space (>= 80px) cancels the transform and deselects.

---

## 7. Transform Interaction

**Source:** `src/viewport/viewport_transform.h`, `src/viewport/viewport_transform.cpp`

### Transform Modes

The `TransformMode` enum defines four states:

- `Transform_None` -- no active transform.
- `Transform_Grab` -- translate the selected node.
- `Transform_Rotate` -- rotate the selected node.
- `Transform_Scale` -- scale the selected node.

The mode is activated by keyboard shortcuts (G/R/S) in the main loop and is stored in `gTransformMode`. An axis lock (`gAxisLock`: 0, 'X', 'Y', or 'Z') constrains the transform to a single world axis.

### Mouse Delta Calculation

`TickTransformInteraction(forward, up, eye, mouseX, mouseY, mouseClicked)` runs each frame. It computes frame-to-frame mouse deltas (`dx`, `dy`) and builds camera-space right and up vectors from the camera's forward and up directions (via cross products). The move scale is proportional to the distance from the camera to the node (`0.0015 * distToCam`), so transforms feel consistent regardless of zoom level.

### Grab Mode

The mouse delta is projected onto the camera-space right and up axes to produce a 3D world-space translation delta. With an axis lock, only the locked component is applied. Clicking LMB commits the transform.

### Rotate Mode

Rotation uses a preview system. When rotation begins, the node's current Euler angles and the mouse position are saved as the start state. Each frame, the cumulative mouse offset from the start position is scaled by 1.5 and applied to the preview angles (`gRotatePreviewX/Y/Z`). Gizmos read these preview values to show the rotation in real time without modifying the actual node data.

- No axis lock: horizontal mouse motion rotates around Y, vertical rotates around X.
- X lock: vertical mouse motion rotates around X.
- Y lock: horizontal mouse motion rotates around Y.
- Z lock: horizontal mouse motion rotates around Z.

Clicking LMB writes the preview values into the actual node rotation and commits the transform. For Node3D nodes, `SyncNode3DQuatFromEuler` is called to keep the internal quaternion in sync with the Euler angles.

### Scale Mode

The scale factor is `1.0 + ((-dy + dx) * 0.03)`, clamped to a minimum of 0.01. With an axis lock, only the locked axis is scaled. For Audio3D nodes, scaling also multiplies the inner and outer radius values. Clicking LMB commits.

### Snapshot and Undo

Before any transform modifies data, `BeginTransformSnapshot()` saves a copy of the selected node's state. When the transform is committed, `EndTransformSnapshot()` compares the before and after states. If they differ, an undo entry is pushed via `PushUndo` with lambdas that restore/reapply the node data. `CancelTransformSnapshot()` restores the saved state, discarding the in-progress transform.

The snapshot system tracks which node type is being transformed (Audio3D, StaticMesh, Node3D, or NavMesh3D) and stores the corresponding before-state struct.

---

## 8. Node Helpers

**Source:** `src/viewport/node_helpers.h`, `src/viewport/node_helpers.cpp`

This module provides world-transform computation and hierarchy utilities used throughout the viewport subsystem. All functions delegate to the `NebulaNodes` namespace implementations, passing in the global node arrays (`gStaticMeshNodes`, `gNode3DNodes`, etc.).

### World Transform Computation

- `GetStaticMeshWorldTRS(idx, ...)` -- Computes the world-space translate/rotate/scale of a StaticMesh by walking up its parent chain.
- `GetNode3DWorldTRS(idx, ...)` -- Same for Node3D, returning Euler angles.
- `GetNode3DWorldTRSQuat(idx, ...)` -- Same for Node3D, returning a quaternion rotation instead of Euler angles. Used by rendering and selection to avoid gimbal lock.
- `GetCamera3DWorldTR(idx, ...)` -- Computes a Camera3D's world position and rotation. Handles the special case of orbit offset: when the camera has a parent, its `orbitX/Y/Z` offset is added to its local position before walking the parent chain. Each parent's rotation is applied to orbit the camera's offset around the parent pivot using Euler rotation math. The parent walk supports chains up to 256 nodes deep (cycle guard).
- `TryGetNodeWorldPosByName(name, ...)` -- Looks up a node by name (StaticMesh or Node3D) and returns its world position.

### Hierarchy Cycle Detection

- `WouldCreateHierarchyCycle(childName, candidateParentName)` -- Walks from the candidate parent up through the hierarchy. If it reaches the child name, the reparent would create a cycle. Returns true to block it.
- `StaticMeshCreatesCycle(childIdx, candidateParentIdx)` -- Checks if parenting one StaticMesh under another would create a cycle.
- `Node3DCreatesCycle(childIdx, candidateParentIdx)` -- Same for Node3D nodes.

### Node Lookup

- `FindStaticMeshByName(name)` -- Returns the index of the first StaticMesh with the given name, or -1.
- `FindNode3DByName(name)` -- Same for Node3D.
- `FindCamera3DByName(name)` -- Same for Camera3D (implemented directly, not delegated).

### Reparent with World Position Preservation

- `ReparentStaticMeshKeepWorldPos(childIdx, newParent)` -- Changes a StaticMesh's parent while adjusting its local position so its world position stays the same. If the new parent is empty (unparented), the local position is set to the current world position. Otherwise, the new parent's world position is subtracted from the child's world position to compute the new local offset.
- `ResetStaticMeshTransformsKeepWorld(idx)` -- Unparents a StaticMesh and writes its current world-space TRS values directly into its local fields.

### IsCameraUnderNode3D

`IsCameraUnderNode3D(cam, nodeName)` walks up the camera's parent chain to determine if it is a descendant of the named node. Used to determine if moving a Node3D should also affect the viewport camera.
