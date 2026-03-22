# Viewport and Rendering System

This document describes the 3D viewport rendering pipeline used by the Nebula Dreamcast Engine's desktop editor. The viewport is built on OpenGL 2 immediate-mode rendering and covers background drawing, mesh rendering, node gizmos, click-to-select picking, and interactive transform manipulation.

All source files referenced below live under `src/viewport/`.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [OpenGL 2 Immediate-Mode Rendering Pipeline](#opengl-2-immediate-mode-rendering-pipeline)
3. [Texture Cache System](#texture-cache-system)
4. [Screen Projection and Local Axes Helpers](#screen-projection-and-local-axes-helpers)
5. [Viewport Background](#viewport-background)
6. [StaticMesh3D Rendering](#staticmesh3d-rendering)
7. [Node Gizmos](#node-gizmos)
8. [Click-to-Select Node Picking](#click-to-select-node-picking)
9. [Transform Gizmo Interaction](#transform-gizmo-interaction)
10. [Node World Transform Queries and Camera Hierarchy Helpers](#node-world-transform-queries-and-camera-hierarchy-helpers)

---

## Architecture Overview

The viewport system is split across six source file pairs:

| File | Responsibility |
|------|---------------|
| `viewport_render.h/.cpp` | Texture loading, screen projection, quaternion-to-matrix conversion, local axis extraction |
| `background.h/.cpp` | Gradient background, procedural stars/nebula, grid, world axes, checker overlay texture |
| `static_mesh_render.h/.cpp` | StaticMesh3D triangle rendering, material binding, vertex animation, smooth shading, navmesh overlay, selection overlay |
| `node_gizmos.h/.cpp` | Wireframe gizmos for Audio3D, Camera3D, Node3D, and NavMesh3D nodes |
| `viewport_selection.h/.cpp` | Click-to-select node picking via screen-space distance and triangle hit tests |
| `viewport_transform.h/.cpp` | Translate/rotate/scale interaction, undo snapshots, axis-constrained manipulation |
| `node_helpers.h/.cpp` | World-space transform resolution, parent chain walking, hierarchy cycle detection, camera hierarchy queries |

The editor calls these subsystems each frame in a fixed order:

1. `DrawViewportBackground()` -- clears and draws background, grid, axes
2. `DrawNodeGizmos()` -- wireframe helpers for non-mesh node types
3. `RenderStaticMeshNodes()` -- textured/wireframe mesh rendering
4. `TickViewportSelection()` -- processes mouse clicks for picking
5. `TickTransformInteraction()` -- applies ongoing grab/rotate/scale to the selected node

---

## OpenGL 2 Immediate-Mode Rendering Pipeline

The entire viewport uses the OpenGL 2.x fixed-function pipeline. There are no shaders. All geometry is submitted through `glBegin`/`glEnd` blocks using `GL_TRIANGLES`, `GL_LINES`, `GL_QUADS`, and `GL_POINTS` primitives.

Key patterns used throughout:

- **Matrix stack manipulation.** Each renderable pushes a matrix with `glPushMatrix()`, applies translation/rotation/scale via `glTranslatef`/`glRotatef`/`glScalef`/`glMultMatrixf`, draws geometry, then pops with `glPopMatrix()`. The background temporarily replaces both the projection and modelview matrices with identity to draw a full-screen quad.

- **Immediate vertex submission.** Vertices are emitted one at a time inside `glBegin`/`glEnd` blocks. Texture coordinates are set with `glTexCoord2f()` before each vertex. Per-vertex colors are set with `glColor3f()` or `glColor4f()`.

- **State toggling.** Texturing (`GL_TEXTURE_2D`), blending (`GL_BLEND`), depth testing (`GL_DEPTH_TEST`), lighting (`GL_LIGHTING`), and polygon mode (`GL_LINE` vs `GL_FILL`) are toggled on and off as needed. The background explicitly resets all GL state at the start of each frame to prevent leakage from the previous frame's ImGui pass.

- **Stencil buffer usage.** The selected-mesh checker overlay uses the stencil buffer to mask a fullscreen textured quad to the silhouette of the selected mesh.

---

## Texture Cache System

**Source:** `viewport_render.h`, `viewport_render.cpp`

### .nebtex File Loading

The engine uses its own `.nebtex` binary texture format. `LoadNebTexture()` reads these files with the following structure:

1. **Magic bytes:** 4 bytes `NEBT`
2. **Header:** four big-endian `uint16_t` values -- width, height, format, flags
3. **Pixel data:** `width * height` big-endian `uint16_t` values in RGB555 format

Only format 1 (RGB555) is supported. Each 16-bit pixel is unpacked into 8-bit RGBA by expanding 5-bit channels to 8 bits (shift left 3, fill low bits from high bits: `(c5 << 3) | (c5 >> 2)`). Alpha is always 255 (fully opaque).

The resulting RGBA buffer is uploaded to an OpenGL texture via `glTexImage2D()`.

### Texture Parameters

Filter and wrap modes are read from the `.nebtex` file's metadata (via `NebulaAssets::LoadNebTexFilterMode` and `NebulaAssets::LoadNebTexWrapMode`):

- **Filter mode 0:** `GL_NEAREST` (pixel-art / retro look)
- **Filter mode 1+:** `GL_LINEAR` (smooth)
- **Wrap mode 0:** `GL_REPEAT` (tiling)
- **Wrap mode 1 or 2:** `GL_CLAMP` (clamped edges)
- **Wrap mode 3:** `GL_MIRRORED_REPEAT` (mirrored tiling, with fallback to `GL_REPEAT` if the GL extension is unavailable)

### Texture Cache

A global `std::unordered_map<std::string, GLuint>` named `gNebTextureCache` stores loaded textures keyed by file path. The public function `GetNebTexture(path)` checks this cache first and only calls `LoadNebTexture()` on a cache miss. This ensures each texture is loaded and uploaded to the GPU only once.

If a texture fails to load (returns `GLuint` 0), the cache still stores the zero value to avoid repeated load attempts. The mesh renderer explicitly evicts and retries zero-valued entries when the file exists on disk, handling the case where a texture file is created or updated after the initial failed load.

### Circle Texture

`CreateCircleTexture(size)` generates a procedural soft-circle texture at runtime. It produces an RGBA image where RGB is always white (255,255,255) and alpha falls off quadratically from the center. This is used for point-sprite-style effects.

---

## Screen Projection and Local Axes Helpers

**Source:** `viewport_render.h`, `viewport_render.cpp`

### ProjectToScreenGL

```cpp
bool ProjectToScreenGL(const Vec3& world, float& outX, float& outY, float scaleX, float scaleY);
```

Projects a 3D world-space point to 2D screen coordinates. This is used by the selection system and gizmo drawing.

The function:

1. Reads the current `GL_MODELVIEW_MATRIX` and `GL_PROJECTION_MATRIX` from OpenGL state
2. Multiplies `projection * modelview * worldPoint` manually (column-major multiplication)
3. Performs perspective division (`x/w`, `y/w`)
4. Checks the NDC Z is within `[-1, 1]` (rejects points behind the camera or beyond the far plane)
5. Maps NDC to viewport pixel coordinates using the current `GL_VIEWPORT`
6. Divides by `scaleX`/`scaleY` to convert framebuffer pixels to ImGui screen coordinates (handling high-DPI displays)

Returns `false` if the point is outside the view frustum or `w == 0`.

### GetLocalAxes

```cpp
void GetLocalAxes(const Audio3DNode& n, Vec3& right, Vec3& up, Vec3& forward);
```

Computes the local coordinate axes (right, up, forward) for an Audio3D node based on its Euler rotation angles. Delegates to `GetLocalAxesFromEuler()` in `math_utils`.

### QuatToGLMatrix

```cpp
void QuatToGLMatrix(float qw, float qx, float qy, float qz, float m[16]);
```

Converts a unit quaternion to a 4x4 column-major rotation matrix suitable for `glMultMatrixf()`. This is used when rendering Node3D nodes and their children, which store orientation as quaternions to avoid gimbal lock.

The conversion formula follows the standard quaternion-to-matrix derivation:

```
m[0]  = 1 - 2*(yy+zz)    m[4]  = 2*(xy-wz)        m[8]  = 2*(xz+wy)
m[1]  = 2*(xy+wz)         m[5]  = 1 - 2*(xx+zz)    m[9]  = 2*(yz-wx)
m[2]  = 2*(xz-wy)         m[6]  = 2*(yz+wx)         m[10] = 1 - 2*(xx+yy)
```

---

## Viewport Background

**Source:** `background.h`, `background.cpp`

### DrawViewportBackground

```cpp
void DrawViewportBackground(int themeMode, bool playMode);
```

Draws the complete viewport background in several layers. Called once per frame before any scene geometry.

### Step 1: GL State Reset

The function begins by resetting all GL state to a known baseline. This is critical because the previous frame's ImGui rendering pass may leave arbitrary state enabled. The reset disables depth test, alpha test, texturing, blending, lighting, color material, normalize, and all client state arrays (color, vertex, texcoord, normal). Shade model is set to `GL_SMOOTH`.

### Step 2: Gradient Background

Both the projection and modelview matrices are pushed and replaced with identity. A full-screen quad is drawn with `glBegin(GL_QUADS)` using per-vertex colors to create a vertical gradient. The color palette depends on `themeMode`:

| Theme | Bottom Color | Top Color |
|-------|-------------|-----------|
| 0 -- Space | Dark navy `#08090C` | Slightly lighter `#14161C` |
| 1 -- Slate | Blue-grey `#2A363D` | Light slate `#6D7F89` |
| 2 -- Classic | Deep teal `#162229` | Pale blue-green `#75A8B2` |
| 3 -- Grey | Medium grey `#5A595C` | Lighter grey `#7F7E83` |
| Play mode | Solid black | Solid black |
| Other | Solid black | Solid black |

After the gradient, the matrices are popped and depth testing is re-enabled.

### Step 3: Checker Overlay Texture (One-Time Init)

On the first call, the function generates a 64x64 checkerboard texture (`gCheckerOverlayTex`) with 8x8 pixel cells. Even cells are transparent; odd cells are opaque purple (120, 90, 235). This texture is stored globally and reused by the StaticMesh3D selection overlay system (see [Selected Mesh Checker Overlay](#selected-mesh-checker-overlay)).

### Step 4: Procedural Stars and Nebula (Space Theme Only)

When `themeMode == 0`, the function draws two layers of world-space point effects:

**Nebula (400 points):** Distributed uniformly on a sphere of radius 480 units. Each point has a randomized purple/blue/green tint and is drawn as a 10px `GL_POINT` at 60% opacity. These create a subtle colored haze behind the stars.

**Stars (4000 points):** Distributed uniformly on a sphere of radius 500 units. Each star has a random phase, twinkle speed, color tint (white or pale blue), and size value. Stars twinkle over time via a sinusoidal brightness modulation: `brightness = 0.3 + 0.7 * (0.5 + 0.5 * sin(time * speed + phase))`. Stars are drawn as `GL_POINTS` in three size buckets:

- Size 0-300: 3px points
- Size 300-700: 6px points
- Size 700+: 10px points

Star and nebula positions are generated once (on first call, using `rand()`) and cached in static arrays. Blending is enabled (`GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`) and `GL_POINT_SMOOTH` is used for soft point rendering.

### Step 5: Ground Grid

A 10x10 grid centered at the origin with 0.5-unit spacing, drawn as grey (0.6, 0.6, 0.6) `GL_LINES` on the Y=0 plane. The center lines (at X=0 and Z=0) are skipped so the colored axis lines do not overlap with grey grid lines.

### Step 6: World Axes

Two axis lines drawn slightly above the grid (Y=0.001 to avoid z-fighting):

- **X axis:** Red (0.8, 0.2, 0.2), extending from `-half` to `+half`
- **Z axis:** Amber (0.95, 0.65, 0.1), extending from `-half` to `+half`

---

## StaticMesh3D Rendering

**Source:** `static_mesh_render.h`, `static_mesh_render.cpp`

### RenderStaticMeshNodes

```cpp
void RenderStaticMeshNodes();
```

Iterates over all `StaticMesh3DNode` entries in the scene and renders each one. This is the most complex rendering function in the viewport system.

### Mesh Data

Each StaticMesh3D references a `.nebmesh` file. The mesh data is loaded via `GetNebMesh()` (from `io/mesh_io`) and provides:

- `positions` -- vertex positions (`std::vector<Vec3>`)
- `uvs` -- texture coordinates (`std::vector<Vec2>`)
- `indices` -- triangle indices (`std::vector<uint16_t>`)
- `faceMaterial` -- per-triangle material slot index
- `faceVertexCounts` -- original polygon vertex counts (for wireframe quad display)
- `hasFaceTopology` -- whether original polygon data is available
- `hasFaceMaterial` -- whether per-face material assignments are available
- `hasUv` -- whether UV coordinates are available

### Vertex Animation

Before rendering, the function checks whether the mesh has an active animation clip (`.nebanim` file). Animation sources are checked in priority order:

1. **Slot preview** -- if the editor is previewing a specific animation slot (`gStaticAnimPreviewNode` and `gStaticAnimPreviewSlot` match this mesh)
2. **Play-mode active slot** -- if in play mode with a runtime-test-enabled mesh, the slot from `gEditorAnimActiveSlot` is used
3. **Legacy vtxAnim** -- the mesh's single animation reference (older format)

Animation clips are cached in `gStaticAnimClipCache`. Each clip contains per-frame vertex positions. The current frame is determined by:

- In play mode with active animation: `floor(elapsedTime * fps) % frameCount`
- In play mode without explicit animation state: `floor(glfwGetTime() * fps) % frameCount`
- In preview mode: the editor's preview frame slider value

If the clip is **mesh-aligned** (`meshAligned` flag), frame positions replace the rest-pose directly. If not mesh-aligned, the system computes a bounding box alignment:

1. Compute centroids and bounding box diagonals for both the mesh rest pose and animation frame 0
2. Derive a scale ratio from the diagonal lengths
3. Transform each animated vertex: `meshCentroid + (animVertex - animCentroid) * scaleRatio`

The result is a `staticAnimPosed` vector that replaces the mesh's rest-pose positions for rendering. Vertices beyond the animation's vertex count fall through to rest-pose positions.

### Material State

Materials are resolved per-triangle via the `faceMaterial` index. Each material slot can reference either a `.nebtex` texture directly or a `.nebmat` material file. The `MatState` struct captures the resolved state for each slot:

| Field | Description |
|-------|-------------|
| `tex` | GL texture handle (0 if no texture) |
| `flipU`, `flipV` | UV axis flip flags |
| `satU`, `satV` | Saturn-sampling UV padding scale (retro accuracy mode) |
| `uvScale` | Power-of-two UV tiling exponent (`uvMul = 2^(-uvScale)`) |
| `uvScaleU`, `uvScaleV` | Per-axis UV scale multipliers |
| `shadingMode` | 0 = unlit, 1 = lit (directional) |
| `lightRotation` | Light yaw angle in degrees |
| `lightPitch` | Light pitch angle in degrees |
| `lightRoll` | Light roll angle in degrees |
| `shadowIntensity` | Shadow darkness multiplier (0-2 range) |
| `shadingUv` | UV-based shading channel index (-1 = none) |

Materials can be `.nebtex` files (texture-only, parameters read from the texture's metadata) or `.nebmat` files (material with texture reference, UV transform, and shading settings loaded through the `NebulaAssets` API).

All material slots (up to `kStaticMeshMaterialSlots`, which is 14) are pre-warmed at the start of each mesh via `getMatState()` to ensure consistent visibility regardless of triangle draw order.

### Transform Setup

For each mesh, the world-space translation, rotation, and scale are computed via `GetStaticMeshWorldTRS()`, which walks the parent chain. The GL matrix stack is set up as follows:

**Parented under Node3D:** The parent's world quaternion is obtained from `GetNode3DWorldTRSQuat()`, converted to a 4x4 matrix via `QuatToGLMatrix()`, and applied with `glMultMatrixf()`. The child's local Euler rotation is then layered on top with three `glRotatef()` calls (X, Y, Z).

**Standalone mesh:** Euler rotation is applied with an axis remap: `rotZ` maps to X-axis rotation (pitch), `rotX` maps to Y-axis rotation (yaw), `rotY` maps to Z-axis rotation (roll). This matches the engine's coordinate convention.

World scale is applied via `glScalef(wsx, wsy, wsz)` after rotation.

### Smooth Normal Computation

When any material slot uses lit shading (`shadingMode == 1` or `shadingUv >= 0`), the renderer computes smooth vertex normals through a position-welding algorithm:

1. **Adaptive weld distance:** The mesh's bounding box diagonal is computed from rest-pose positions. The weld distance is 0.5% of this diagonal, making it scale-independent.

2. **Position grouping:** Vertices whose rest-pose positions are within the weld distance are assigned to the same group. This merges vertices that were duplicated at UV seams or material boundaries. The grouping uses the rest-pose (not animated) positions so groups remain stable under animation.

3. **Face normal accumulation:** For each triangle, the cross product of its edges (using rendered/animated positions) is accumulated into each vertex's position group. The cross product is **negated** to compensate for the flipped winding order caused by X-axis negation during model import.

4. **Normalization:** Each group's accumulated normal is normalized to unit length. All vertices in the group share the same smooth normal.

5. **Camera-space transform:** The smooth normals are transformed by the current modelview matrix (retrieved via `glGetFloatv(GL_MODELVIEW_MATRIX)`) to produce camera-space normals. This gives view-dependent shading that matches the Dreamcast runtime's behavior, where light direction is relative to the camera.

### Software Lighting

Lighting is computed in software (not via GL fixed-function lighting) to exactly match the Dreamcast build. For each lit vertex, the per-vertex color is set to:

```
c = ambient + max(0, dot(normal, lightDir)) * diffuse
    - (normal.y < 0 ? -normal.y * shadowQuotient : 0)
    + (1 - |normal.z|) * 0.12
```

Where:
- `ambient` = 0.35
- `diffuse` = 0.9 - 0.25 * shadowIntensity
- `shadowQuotient` = 0.25 * shadowIntensity (darkens undersides)
- The `(1 - |normal.z|) * 0.12` term adds a subtle rim/fill light on edges facing the camera

The light direction is derived from the material's yaw and pitch angles via spherical coordinate conversion:

```
dx = sin(yaw) * cos(pitch)
dy = sin(pitch)
dz = cos(yaw) * cos(pitch)
```

When the material or light parameters change between triangles, the renderer ends the current `glBegin(GL_TRIANGLES)` block, updates the GL texture binding and lighting state, then starts a new `glBegin(GL_TRIANGLES)` block.

### Triangle Rendering

Triangles are emitted with `glBegin(GL_TRIANGLES)`. For each triangle:

1. The face material index is looked up from `mesh->faceMaterial`
2. The material state is retrieved from the pre-warmed cache
3. If the bound texture or shading mode differs from the previous triangle, the renderer batches a state change (end/begin `GL_TRIANGLES`, rebind texture)
4. UV coordinates are computed per-vertex with transforms: `u = meshU * 2^(-uvScale) * uvScaleU`, V is flipped (`1 - meshV`), then Saturn sampling and flip flags are applied
5. If lit, the per-vertex color is computed from the camera-space smooth normal and light direction
6. The vertex position (from renderPositions, which may be animated) is emitted

### Wireframe Mode

When `gWireframePreview` is enabled, the polygon mode is set to `GL_LINE`. Two paths exist:

**Quad-aware wireframe (preferred):** If the mesh has face topology data (`hasFaceTopology` and `faceVertexCounts`), polygon boundary edges are drawn from the original face data using `GL_LINES`. For each original polygon, the edge loop is reconstructed from the triangle fan indices. This avoids showing triangulation diagonals on quads and n-gons.

**Triangle wireframe (fallback):** If no face topology is available, the standard `GL_TRIANGLES` in `GL_LINE` polygon mode is used, which shows all triangle edges including internal diagonals.

Per-material-slot coloring uses a deterministic hash function: `color = 0.45 + 0.40 * |sin(matIndex * k + offset)|` with different constants for R, G, B. Selected meshes are drawn in white.

### NavMesh Overlay

For meshes flagged as `navmeshReady` (and not currently selected), a colored wireframe overlay is drawn on top of triangles that fall inside any NavMesh3D positive bounds volume:

1. All NavMesh3D nodes with `navBounds` enabled are collected. Positive (non-negator) bounds are stored as AABBs with their wire color and thickness. Cull-walls bounds are stored separately.

2. For each triangle, the three vertices are transformed to world space (scale + translate) and tested against all positive AABBs. If any vertex is inside any positive bounds, the triangle is marked for overlay.

3. **Wall culling:** If the triangle centroid falls inside a cull-walls bounds, the face normal's Y component is checked against the cull threshold. Near-vertical faces (|normal.y| < threshold) are excluded -- these are walls, not walkable surfaces.

4. Overlay triangles are drawn in `GL_LINE` polygon mode with `GL_POLYGON_OFFSET_LINE` (offset -1, -1) to prevent z-fighting. The wire color and thickness come from the NavMesh3D node.

### Selected Mesh Checker Overlay

When a StaticMesh3D is selected, a purple checkerboard overlay is drawn on top of it using the stencil buffer. This is a three-pass technique:

**Pass 1 -- Stencil write:** The selected mesh's triangles are rendered with:
- Color writes disabled (`glColorMask(GL_FALSE, ...)`)
- Depth writes disabled (`glDepthMask(GL_FALSE)`)
- Depth function set to `GL_LEQUAL` with polygon offset (-1, -1)
- Stencil set to always write 1 (`GL_ALWAYS, GL_REPLACE`)

This writes a silhouette mask of the visible mesh into the stencil buffer.

**Pass 2 -- Checker draw:** A fullscreen quad is drawn in orthographic projection (identity matrices) with:
- Stencil function set to `GL_EQUAL, 1` (only draw where stencil is 1)
- `gCheckerOverlayTex` bound with `GL_REPLACE` texture environment
- Alpha blending enabled
- Depth test disabled
- UV coordinates scaled so checker cells are always 8 pixels on screen: `uMax = viewportWidth / (checkerPx * 8.0)`, `vMax = viewportHeight / (checkerPx * 8.0)` where `checkerPx = 8.0`

**Pass 3 -- Cleanup:** Stencil test disabled, blend disabled, depth test re-enabled, texture environment reset to `GL_MODULATE`, stencil mask and function reset.

### GL State Cleanup

After all meshes are drawn, the function:
- Sets polygon mode back to `GL_FILL`
- Disables lighting, light0, color material, normalize, texturing
- Sets shade model to `GL_SMOOTH`

This prevents state from leaking into the next frame's background gradient or ImGui rendering pass.

---

## Node Gizmos

**Source:** `node_gizmos.h`, `node_gizmos.cpp`

### DrawNodeGizmos

```cpp
void DrawNodeGizmos(const Camera3DNode* activeCam);
```

Draws wireframe visual helpers for all non-mesh node types. Called once per frame after the background and before StaticMesh rendering. The `activeCam` parameter identifies the camera driving the play viewport (its helper is hidden during play mode).

When `gHideUnselectedWireframes` is true, only the selected node's gizmo is drawn for Audio3D and Camera3D nodes.

### Audio3D Range Spheres

Each Audio3D node is drawn as three concentric wireframe spheres plus a vertical marker line and local axis indicators:

- **Outer sphere** (blue 0.2, 0.6, 1.0 when unselected; white when selected) -- represents the outer falloff radius
- **Inner sphere** (green 0.1, 1.0, 0.4 when unselected) -- represents the inner/full-volume radius
- **Core sphere** (yellow 1.0, 0.9, 0.2; radius = 0.25 * scaleAvg) -- marks the node center

If `outerRadius < innerRadius`, the values are swapped before drawing.

The spheres are drawn using **12 latitude rings** and **16 longitude segments** via `GL_LINE_LOOP`. Each ring is a separate `GL_LINE_LOOP` draw call.

A **vertical marker line** extends 1 unit upward from the node position.

**Local axis indicators** are drawn from the origin using `GetLocalAxes()`: red for X (right), green for Y (up), blue for Z (forward). Axis length is 0.6 * scaleAvg.

During a **rotate transform preview**, the gizmo reads `gRotatePreviewX/Y/Z` instead of the node's committed rotation values, providing real-time visual feedback.

When the node is selected and an **axis-locked grab or rotate** is active, a long guide line (2000 units in each direction, line width 2.5) is drawn along the locked world axis through the node position. The line color matches the axis: red for X, green for Y, blue for Z.

### Camera3D Frustum Helpers

Each Camera3D node is drawn as a simple "camera cone" wireframe using `GL_LINES`:

- A forward-pointing line from origin to (0, 0, 1)
- Four corner lines from origin to near-plane corners: (+/-0.3, +/-0.2, 0.7)

The helper uses world-space position and rotation from `GetCamera3DWorldTR()`, which walks the parent chain including orbit offsets.

Colors:
- **Selected:** White (1.0, 1.0, 1.0)
- **Main camera:** Bright green (0.2, 1.0, 0.4)
- **Other cameras:** Darker green (0.1, 0.8, 0.3)

The helper for the camera currently driving play mode is hidden to avoid visual clutter.

### Node3D Bounding Boxes

Each Node3D is drawn as a wireframe cube. The rendering process:

1. World position and quaternion rotation are resolved via `GetNode3DWorldTRSQuat()`
2. Translation is applied with `glTranslatef(wx, wy, wz)`
3. The quaternion is converted to a GL matrix via `QuatToGLMatrix()` and applied with `glMultMatrixf()`
4. A local offset `(boundPosX, boundPosY, boundPosZ)` is applied via `glTranslatef` for the collision center
5. The cube is scaled by `worldScale * (2 * extent)` on each axis via `glScalef`
6. A unit cube (6 faces, each a `GL_QUADS` primitive, side length 1 centered at origin) is drawn in `GL_LINE` polygon mode

Colors: white when selected, light cyan (0.55, 0.9, 1.0) otherwise.

### NavMesh3D Bounds

Each NavMesh3D node with `navBounds` enabled is drawn as a wireframe box:

- Position is applied with `glTranslatef`
- Euler rotation is applied with three `glRotatef` calls (X, Y, Z)
- The box is scaled by `scale * extent` on each axis
- Line width is set to the node's `wireThickness` property (reset to 1.0 after drawing)

Colors:
- **Selected:** White (1.0, 1.0, 1.0)
- **Negator volume:** Red (1.0, 0.25, 0.25)
- **Positive volume:** Custom color from `(wireR, wireG, wireB)`

---

## Click-to-Select Node Picking

**Source:** `viewport_selection.h`, `viewport_selection.cpp`

### TickViewportSelection

```cpp
void TickViewportSelection(GLFWwindow* window, float mouseX, float mouseY,
                           float scaleX, float scaleY, bool mouseClicked);
```

Processes viewport mouse clicks to select nodes. Called once per frame. If `mouseClicked` is false, the function returns immediately. On click, it calls `glfwFocusWindow()` to ensure the viewport has keyboard focus.

### Selection Algorithm

The function iterates over all node types and finds the node whose screen projection is closest to the mouse cursor. It tracks a single `bestDist` value across all types, with a `clearBest` lambda that resets all candidate indices when a closer match is found.

**Audio3D, Camera3D, Node3D, NavMesh3D nodes:** Each node's world position is projected to screen coordinates via `ProjectToScreenGL()`. The 2D Euclidean pixel distance from the mouse cursor is computed.

**StaticMesh3D nodes (triangle-accurate picking):** For each mesh with loaded geometry:

1. World transform (position, rotation, scale) is resolved via `GetStaticMeshWorldTRS()`
2. The rotation matrix is built differently depending on parenting:
   - **Parented under Node3D:** Parent quaternion is converted to a 3x3 rotation matrix; child local Euler rotation is applied first, then parent rotation
   - **Standalone:** Euler angles are converted to trig values with the engine's axis remap (Z->X, X->Y, Y->Z) and applied via ZYX rotation order
3. Each triangle's three vertices are: scaled, rotated, translated to world space, then projected to screen space via `ProjectToScreenGL()`
4. A 2D **barycentric point-in-triangle test** checks if the mouse cursor is inside the projected triangle:
   ```
   u = (dot11*dot02 - dot01*dot12) / (dot00*dot11 - dot01*dot01)
   v = (dot00*dot12 - dot01*dot02) / (dot00*dot11 - dot01*dot01)
   hit = (u >= 0) && (v >= 0) && (u + v <= 1)
   ```
5. If the mouse is inside any triangle, the mesh gets a distance of **0** (highest priority). The search short-circuits on the first triangle hit.
6. If no triangle is hit, the mesh falls back to origin-point distance like other node types.

### Selection Threshold and Behavior

A node is only selected if the best distance is less than **80 pixels**. Clicking further than 80 pixels from any node deselects all nodes.

When a selection changes:
- All five selection indices (`gSelectedAudio3D`, `gSelectedStaticMesh`, `gSelectedCamera3D`, `gSelectedNode3D`, `gSelectedNavMesh3D`) are updated -- only the winning type gets a valid index; all others are set to -1
- Any active transform is cancelled (`gTransforming = false`, `gTransformMode = Transform_None`, `gAxisLock = 0`)
- The last-mouse position is reset for future transform tracking

### Interaction with Transform Mode

- If `gTransformMode` is `Transform_None`, clicking selects the nearest node normally.
- If `gTransformMode` is `Transform_Rotate` and click distance >= 80px, the transform is cancelled, the rotate preview is cleared, and all nodes are deselected.
- For `Transform_Grab` or `Transform_Scale`, clicking far from any node cancels the transform and deselects.

---

## Transform Gizmo Interaction

**Source:** `viewport_transform.h`, `viewport_transform.cpp`

### Transform Modes

The transform system uses an enum with four states:

```cpp
enum TransformMode {
    Transform_None = 0,
    Transform_Grab,    // Translation
    Transform_Rotate,  // Rotation
    Transform_Scale    // Scale
};
```

Global state variables:
- `gTransformMode` -- current active mode (set by keyboard shortcuts G/R/S in the main loop)
- `gAxisLock` -- `0` for free transform, `'X'`/`'Y'`/`'Z'` for axis-constrained
- `gTransforming` -- true while mouse movement is being tracked
- `gHasTransformSnapshot` -- true when a before-state has been captured for undo

### TransformTarget Abstraction

The internal `TransformTarget` struct provides a unified interface to the selected node's transform data:

```cpp
struct TransformTarget {
    float* x, *y, *z;           // position pointers
    float* rotX, *rotY, *rotZ;  // rotation pointers
    float* scaleX, *scaleY, *scaleZ;  // scale pointers
    int selectedId;             // unique ID for preview matching
    bool isNode3D;              // needs quaternion sync after rotation
    bool isAudio3D;             // has radius fields
    float* innerRadius;         // Audio3D inner radius (nullable)
    float* outerRadius;         // Audio3D outer radius (nullable)
};
```

`GetSelectedTransformTarget()` resolves the currently selected node (checking Audio3D, StaticMesh, Node3D, NavMesh3D in that order) and fills a `TransformTarget` with mutable pointers to the node's fields. The `selectedId` uses offset ranges to distinguish node types (0+ for Audio3D, 10000+ for StaticMesh, 40000+ for Node3D, 50000+ for NavMesh3D).

### TickTransformInteraction

```cpp
void TickTransformInteraction(const Vec3& forward, const Vec3& up, const Vec3& eye,
                              float mouseX, float mouseY, bool mouseClicked);
```

Called once per frame. On the first call after a transform begins, the current mouse position is stored as the baseline. On subsequent calls, the frame-to-frame mouse delta (`dx`, `dy`) is computed.

Camera-space right and up vectors are derived from the camera's forward and up directions via cross products. The move scale factor is `0.0015 * distanceToCam`, making transforms feel consistent regardless of zoom level.

If any non-zero mouse delta is detected and no snapshot exists, `BeginTransformSnapshot()` is called automatically.

### Grab (Translation)

Mouse delta is converted to a 3D world-space displacement:

```
delta = right * -dx * moveScale + up * -dy * moveScale
```

The negation on dx/dy makes the object follow the mouse direction intuitively.

Axis lock behavior:
- **X lock:** Only `delta.x` is applied to the node's X position
- **Y lock:** Only `delta.y` is applied to the node's Y position
- **Z lock:** Only `delta.z` is applied to the node's Z position
- **No lock:** All three components are applied

Clicking the left mouse button confirms the translation, calls `EndTransformSnapshot()`, and resets `gTransformMode` to `Transform_None`.

### Rotate (Rotation)

Rotation uses **cumulative** mouse displacement from the start point (not per-frame delta). The rotation scale is **1.5 degrees per pixel**.

When rotation begins (or the selected node changes), the current Euler angles and mouse position are saved as the start state. Each frame, the preview rotation is computed:

- **X-lock:** `previewX = startX + (mouseY - startMouseY) * 1.5`
- **Y-lock:** `previewY = startY + (mouseX - startMouseX) * 1.5`
- **Z-lock:** `previewZ = startZ + (mouseX - startMouseX) * 1.5`
- **No lock:** Y rotates with horizontal mouse, X rotates with vertical mouse

The preview values (`gRotatePreviewX/Y/Z`) are read by the node gizmo renderer to show real-time visual feedback without modifying the actual node data.

Clicking confirms the rotation: the preview values are written into the node's rotation fields. For Node3D nodes, `SyncNode3DQuatFromEuler()` is called to keep the internal quaternion synchronized. The start state is then reset.

### Scale

Scale is computed from combined horizontal and vertical mouse movement:

```
s = 1.0 + (-dy + dx) * 0.03
```

Dragging up-right enlarges, down-left shrinks. The scale factor is clamped to a minimum of 0.01 to prevent zero or negative scale.

For **Audio3D nodes**, the inner and outer radii are also scaled by `s` (independently of axis lock), with a minimum clamp of 0.01.

Axis lock constrains scaling to a single axis. Without axis lock, uniform scaling is applied to all three axes.

Clicking confirms the scale and resets the transform mode.

### Undo Snapshots

The transform system integrates with the editor's undo system through three functions:

**`BeginTransformSnapshot()`** captures the selected node's complete state before any changes begin. It identifies which node type is selected and stores the appropriate before-state struct (`gTransformBefore` for Audio3D, `gTransformBeforeStatic` for StaticMesh, etc.). Flags track which type was captured.

**`EndTransformSnapshot()`** compares the current node state to the captured snapshot using the `TransformChanged()` overloads. These compare all nine transform components (x, y, z, rotX, rotY, rotZ, scaleX, scaleY, scaleZ). If any value differs, an undo entry is pushed via `PushUndo()` with:
- A description string (e.g., "Transform StaticMesh3D")
- An undo lambda that restores the before-state
- A redo lambda that restores the after-state

Both lambdas include bounds checks to handle the case where nodes are added or removed between undo/redo operations.

**`CancelTransformSnapshot()`** restores the captured before-state into the node's fields, effectively reverting all changes made during the transform. Used when the user presses Escape or clicks in empty space during a transform.

---

## Node World Transform Queries and Camera Hierarchy Helpers

**Source:** `node_helpers.h`, `node_helpers.cpp`

These functions resolve the world-space transform of any node by walking up the parent chain. They are used by the rendering, selection, and transform systems. All functions delegate to `NebulaNodes` namespace implementations, passing in the global node arrays.

### Node Lookup

- `FindStaticMeshByName(name)` -- returns the index of a StaticMesh3D with the given name, or -1. Delegates to `NebulaNodes::FindStaticMeshByName`.
- `FindNode3DByName(name)` -- returns the index of a Node3D with the given name, or -1. Delegates to `NebulaNodes::FindNode3DByName`.
- `FindCamera3DByName(name)` -- returns the index of a Camera3D with the given name, or -1. Implemented directly with a linear search.

### World Transform Resolution

**`GetStaticMeshWorldTRS(idx, ox, oy, oz, orx, ory, orz, osx, osy, osz)`**

Returns world-space position, rotation, and scale for a StaticMesh3D by walking up its parent chain (through StaticMesh and Node3D parents). Delegates to `NebulaNodes::GetStaticMeshWorldTRS`.

**`GetNode3DWorldTRS(idx, ...)`**

Returns world-space position, rotation (Euler), and scale for a Node3D. Delegates to `NebulaNodes::GetNode3DWorldTRS`.

**`GetNode3DWorldTRSQuat(idx, ox, oy, oz, oqw, oqx, oqy, oqz, osx, osy, osz)`**

Returns world-space position, rotation as a quaternion (w, x, y, z), and scale for a Node3D. This is the preferred function for rendering and selection, as quaternion rotation avoids gimbal lock. Delegates to `NebulaNodes::GetNode3DWorldTRSQuat`.

**`GetCamera3DWorldTR(idx, ox, oy, oz, orx, ory, orz)`**

Returns world-space position and rotation for a Camera3D. This function has specialized parent-chain walking logic:

1. Start with the camera's local position. If the camera has a parent, add the orbit offset (`orbitX/Y/Z`).
2. Start with the camera's local rotation.
3. Walk up the parent chain (searching Audio3D, StaticMesh, Camera3D, and Node3D arrays by name):
   - For Audio3D parents: add the parent's position (no rotation contribution)
   - For StaticMesh, Camera3D, and Node3D parents: rotate the accumulated offset around the parent's pivot using Euler rotation math, then add the parent's position and rotation
4. A guard counter (256 iterations) prevents infinite loops from circular parent chains.

The `rotateOffsetEuler` helper function applies a 3D rotation (X then Y then Z order) to an offset vector, implementing the orbit behavior where the camera's position rotates around its parent.

**`TryGetNodeWorldPosByName(name, ox, oy, oz)`**

Looks up a node by name (StaticMesh first, then Node3D) and returns its world position. Returns false if not found.

### Hierarchy Cycle Detection

- **`TryGetParentByNodeName(name, outParent)`** -- Given any node name, returns its parent name. Searches across all node types (Audio3D, StaticMesh, Camera3D, Node3D).

- **`WouldCreateHierarchyCycle(childName, candidateParentName)`** -- Walks up the candidate parent's ancestor chain. If it reaches `childName`, the reparent would create a cycle. Returns true to block it.

- **`StaticMeshCreatesCycle(childIdx, candidateParentIdx)`** -- Checks if parenting one StaticMesh under another would create a cycle within the StaticMesh parent chain.

- **`Node3DCreatesCycle(childIdx, candidateParentIdx)`** -- Checks if parenting one Node3D under another would create a cycle within the Node3D parent chain.

### Camera Hierarchy Queries

**`IsCameraUnderNode3D(cam, nodeName)`**

Returns true if the given Camera3D is directly or transitively parented under the Node3D with the given name. Checks both the direct parent and walks up the ancestor chain via `TryGetParentByNodeName`. A 256-iteration guard prevents infinite loops.

This is used by the editor to determine which cameras are affected by a Node3D's transform changes.

### Reparenting Helpers

**`ReparentStaticMeshKeepWorldPos(childIdx, newParent)`**

Changes a StaticMesh's parent while preserving its visual world position:

1. Compute the current world position via `GetStaticMeshWorldTRS()`
2. Set the new parent name
3. If the new parent is empty (unparenting), set local position to the current world position
4. If the new parent exists, look up the new parent's world position and subtract it from the child's world position to compute the correct local offset

**`ResetStaticMeshTransformsKeepWorld(idx)`**

Un-parents a StaticMesh (clears its parent field) and copies its current world-space position, rotation, and scale into the local transform fields. This ensures the mesh stays in place visually while becoming a root-level node.
