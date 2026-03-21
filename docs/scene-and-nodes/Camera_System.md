# Camera System

This document explains the Nebula Dreamcast Engine camera system from the ground up. It covers the two camera representations, how view and projection matrices are built, how the editor viewport camera works independently of scene cameras, and how cameras export to the Dreamcast runtime.

Source files referenced throughout:

- `src/camera/Camera3D.h` / `Camera3D.cpp` -- core camera structs and matrix builders
- `src/nodes/Camera3DNode.h` -- scene-serialized camera node
- `src/editor/viewport_nav.h` / `viewport_nav.cpp` -- editor viewport navigation
- `src/viewport/node_helpers.h` / `node_helpers.cpp` -- parent-chain world transform resolution

---

## 1. Two Camera Representations

The engine uses two distinct camera types for different purposes:

### Camera3D (orientation-based, editor/runtime)

Defined in `src/camera/Camera3D.h`. This is the _working_ camera representation used by the editor viewport and the runtime matrix pipeline. Orientation is stored as **forward** and **up** vectors, which avoids gimbal lock and maps directly to view matrix construction.

```cpp
struct Camera3D
{
    std::string name;
    std::string parent;
    Vec3 position = { 0.0f, 2.0f, -6.0f };
    Vec3 forward  = { 0.0f, 0.0f, 1.0f };
    Vec3 up       = { 0.0f, 1.0f, 0.0f };
    bool  perspective = true;
    float fovY       = 70.0f;
    float nearZ      = 0.25f;
    float farZ       = 4096.0f;
    float orthoWidth = 12.8f;
    float priority   = 0.0f;
    bool  main       = false;
};
```

### Camera3DNode (Euler-based, scene persistence)

Defined in `src/nodes/Camera3DNode.h`. This is the _serialized_ camera representation that lives in `.nebscene` files. Orientation is stored as Euler angles (`rotX`, `rotY`, `rotZ`) because they are easy to inspect and edit in the UI. It also carries an **orbit offset** (`orbitX/Y/Z`) for cameras parented to other nodes.

```cpp
struct Camera3DNode
{
    std::string name;
    std::string parent;
    float x = 0, y = 2, z = -6;
    float rotX = 0, rotY = 0, rotZ = 0;
    float orbitX = 0, orbitY = 0, orbitZ = 0;
    bool  perspective = true;
    float fovY = 70, nearZ = 0.25f, farZ = 4096;
    float orthoWidth = 12.8f;
    float priority = 0;
    bool  main = false;
};
```

**Why two types?** Euler angles are human-friendly for scene authoring, but forward/up vectors produce stable orthonormal bases without gimbal lock. Whenever the engine needs to compute matrices, it converts `Camera3DNode` Euler data into a `Camera3D` via `BuildCamera3DFromLegacyEuler`, then feeds it into the matrix pipeline.

```
Camera3DNode (scene file)
    |
    | BuildCamera3DFromLegacyEuler()
    v
Camera3D (forward/up vectors)
    |
    | BuildCamera3DView / BuildCamera3DProjection
    v
View + Projection matrices
```

---

## 2. Camera3D Fields Reference

| Field | Type | Default | Purpose |
|-------|------|---------|---------|
| `name` | string | -- | Unique node identifier |
| `parent` | string | -- | Name of parent node (empty = root) |
| `position` | Vec3 | (0, 2, -6) | World-space eye position |
| `forward` | Vec3 | (0, 0, 1) | Direction the camera looks toward |
| `up` | Vec3 | (0, 1, 0) | Which way is "up" for the camera |
| `perspective` | bool | true | true = perspective projection, false = orthographic |
| `fovY` | float | 70.0 | Vertical field of view in degrees (perspective only) |
| `nearZ` | float | 0.25 | Near clip plane distance |
| `farZ` | float | 4096.0 | Far clip plane distance |
| `orthoWidth` | float | 12.8 | Half-width of the orthographic frustum |
| `priority` | float | 0.0 | Used to select among multiple cameras (higher wins) |
| `main` | bool | false | If true, this camera is a candidate for the active camera |

---

## 3. Building Camera Data

The engine does not jump directly from a `Camera3D` to matrices. Instead it builds intermediate structs that validate and organize the data. This separation keeps matrix construction clean and testable.

### Camera3DBasis -- orthonormal basis from hints

```cpp
Camera3DBasis BuildCamera3DBasis(const Vec3& forwardHint, const Vec3& upHint);
```

Takes raw `forward` and `up` vectors (which may not be orthonormal) and produces a clean right/up/forward basis.

The algorithm:

1. Normalize `forwardHint`. Fall back to (0,0,1) if degenerate.
2. Normalize `upHint`. Fall back to (0,1,0).
3. Compute `right = cross(up, forward)` and normalize.
4. If `right` is degenerate (forward and up are parallel), pick a fallback up that is not parallel to forward and recompute.
5. Recompute `up = cross(forward, right)` to ensure orthogonality.
6. Recompute `right = cross(up, forward)` one more time for a clean basis.

The triple-normalize pattern guarantees an orthonormal frame even when the input vectors are nearly parallel or unnormalized.

### Camera3DView -- eye, target, and basis

```cpp
Camera3DView BuildCamera3DView(const Camera3D& camera);
```

Packs the camera's position as `eye`, builds the basis from `camera.forward` and `camera.up`, and computes `target = eye + basis.forward` (one unit ahead of the eye along the view direction).

### Camera3DProjection -- validated projection parameters

```cpp
Camera3DProjection BuildCamera3DProjection(const Camera3D& camera, float aspect);
```

Copies projection settings from the `Camera3D` with safety clamping:

- `fovYDeg` clamped to [5, 170] degrees (prevents degenerate frustums)
- `fovYRad` precomputed from the clamped degrees
- `aspect` floored to 0.0001 (prevents division by zero)
- `nearZ` floored to 0.001
- `farZ` guaranteed to be at least `nearZ + 0.01`
- `orthoWidth` floored to 0.01

These clamps mean downstream code never needs to guard against bad projection parameters.

---

## 4. Matrix Construction

### View Matrix

```cpp
Mat4 BuildCamera3DViewMatrix(const Camera3DView& view);
```

Builds a column-major 4x4 view matrix from the basis and eye position. The layout in the `Mat4::m[16]` array:

```
m[0]  = right.x     m[4]  = right.y     m[8]  = right.z     m[12] = -dot(right, eye)
m[1]  = up.x        m[5]  = up.y        m[9]  = up.z        m[13] = -dot(up, eye)
m[2]  = -forward.x  m[6]  = -forward.y  m[10] = -forward.z  m[14] = +dot(forward, eye)
m[3]  = 0           m[7]  = 0           m[11] = 0           m[15] = 1
```

Note the negated forward row -- OpenGL convention looks down the negative Z axis, so the view matrix negates the forward direction.

The translation column (m[12..14]) encodes the dot products of each basis axis with the negated eye position, which effectively transforms the eye to the origin in camera space.

### Projection Matrix

```cpp
Mat4 BuildCamera3DProjectionMatrix(const Camera3DProjection& proj);
```

Two modes:

**Perspective** -- standard symmetric frustum:

```
m[0] = f / aspect    m[5] = f    (where f = 1 / tan(fovY/2))
m[10] = (far + near) / (near - far)
m[11] = -1
m[14] = (2 * far * near) / (near - far)
```

This is a standard OpenGL-style perspective matrix with depth mapped to [-1, 1].

**Orthographic** -- symmetric box:

```
m[0]  = 2 / (2 * orthoWidth)    i.e. 1 / orthoWidth
m[5]  = 2 / (2 * orthoHeight)   where orthoHeight = orthoWidth / aspect
m[10] = -2 / (far - near)
m[14] = -(far + near) / (far - near)
```

---

## 5. Legacy Euler Conversion

```cpp
Camera3D BuildCamera3DFromLegacyEuler(
    const std::string& name, const std::string& parent,
    float x, float y, float z,
    float rotX, float rotY, float rotZ,
    bool perspective, float fovY, float nearZ, float farZ,
    float orthoWidth, float priority, bool main);
```

This function bridges the gap between `Camera3DNode` (Euler angles) and `Camera3D` (forward/up vectors). It:

1. Converts the Euler angles to a rotation matrix using **R = Rz * Ry * Rx** order.
2. Extracts the local `right`, `up`, and `forward` axes from the matrix columns.
3. Assigns `forward` and `up` into a new `Camera3D`, along with all other fields.

The rotation convention is ZYX (yaw-pitch-roll), matching the convention used elsewhere in the engine for node transforms.

This function is called every frame during play mode to convert the active `Camera3DNode`'s world-space Euler transform into a usable `Camera3D`.

---

## 6. NebulaCamera3D Namespace -- Runtime and Dreamcast Export

The `NebulaCamera3D` namespace in `src/camera/Camera3D.h` provides a parallel set of camera utilities for the runtime and Dreamcast export path. The functions mirror the free-standing builder functions:

| Free function | NebulaCamera3D equivalent |
|---------------|--------------------------|
| `BuildCamera3DBasis` | `NebulaCamera3D::BuildBasis` |
| `BuildCamera3DView` | `NebulaCamera3D::BuildView` |
| `BuildCamera3DProjection` | `NebulaCamera3D::BuildProjection` |
| `BuildCamera3DViewMatrix` | `NebulaCamera3D::BuildViewMatrix` |
| `BuildCamera3DProjectionMatrix` | `NebulaCamera3D::BuildProjectionMatrix` |

The namespace also adds `BuildLookAtView`, which constructs a view from explicit eye/target/up rather than from a `Camera3D` struct.

### DreamcastExport Struct

```cpp
struct DreamcastExport
{
    View       view;
    Projection projection;
    float focalX = 0.0f;
    float focalY = 0.0f;
    float viewW  = 640.0f;
    float viewH  = 480.0f;
};
```

This struct packages everything the Dreamcast runtime needs to set up its camera via the KOS `pvr_*` API.

### BuildDreamcastExport

```cpp
DreamcastExport BuildDreamcastExport(
    const Camera3D& camera, float aspect, const Vec3& targetOffset);
```

This function does three things beyond the standard view/projection build:

1. **Coordinate mirroring.** The Dreamcast runtime uses a coordinate system where X and Z are negated relative to the editor. `BuildDreamcastExport` mirrors the eye position:
   ```
   eye.x = -eye.x
   eye.z = -eye.z
   ```
   Then it rebuilds the basis to look from the mirrored eye toward the (unmirrored) target, which produces correct rendering on DC hardware.

2. **Target offset.** An optional `targetOffset` is added to the view target before mirroring. This allows gameplay code to shift the look-at point (e.g., for camera tracking).

3. **Focal length computation.** The Dreamcast PVR hardware needs focal lengths in pixel units rather than a projection matrix. The conversion for a 640x480 framebuffer:
   ```
   focalY = (viewH / 2) / tan(fovY / 2)
   focalX = focalY * aspect
   ```

---

## 7. Editor Viewport Camera (EditorViewportNav)

Defined in `src/editor/viewport_nav.h` and `viewport_nav.cpp`.

The editor viewport has its own camera controller, **completely independent of scene Camera3DNodes**. This means you can orbit around your scene without affecting any camera node. The editor camera is an orbit-style controller with four interaction modes.

### State

```cpp
struct EditorViewportNav
{
    float orbitYaw, orbitPitch;   // Orbit angles around orbitCenter
    float viewYaw, viewPitch;    // View direction (may differ from orbit when free-looking)
    float distance;              // Distance from orbitCenter to eye
    Vec3  orbitCenter;           // Point being orbited
    bool  viewLocked;            // true = view always points at orbitCenter
    // ... drag state, play-mode snapshot ...
};
```

The eye position is derived from orbit state:

```cpp
Vec3 EditorViewportNav::ComputeEye() const
{
    float yawRad   = orbitYaw * PI / 180;
    float pitchRad = orbitPitch * PI / 180;
    return {
        orbitCenter.x - distance * cos(pitchRad) * cos(yawRad),
        orbitCenter.y - distance * sin(pitchRad),
        orbitCenter.z - distance * cos(pitchRad) * sin(yawRad)
    };
}
```

### Interaction Modes

**MMB (middle mouse button) -- Orbit.** Dragging with MMB orbits the camera around `orbitCenter`. On the first frame of a drag, the orbit center is recalculated to sit one `distance` unit ahead of the current eye along the view direction. This ensures orbiting feels natural even after free-look rotation. Orbit mode sets `viewLocked = true`, which forces the view direction to track the orbit center.

**RMB (right mouse button) -- Free look.** Dragging with RMB rotates the view direction without moving the eye. This sets `viewLocked = false`, decoupling the view direction from the orbit center.

**Scroll wheel -- Dolly.** Scrolling moves `orbitCenter` forward/backward along the current view direction by 0.5 units per scroll tick. This effectively dollies the camera in and out.

**WASD + Q/E -- Roam (RMB must be held).** While holding RMB:
- **W/S** move the orbit center forward/backward along the XZ plane
- **A/D** strafe left/right
- **E/Q** move up/down along the Y axis
- Movement speed is 5 units/second, scaled by `deltaTime`

### viewLocked Mode

When `viewLocked` is true, `EvaluateFrameCamera` overrides `viewYaw` and `viewPitch` each frame so the camera always looks at `orbitCenter`. When false, the view direction is independent -- the user can look around freely while the orbit center stays put.

### Play-Mode Snapshot

When entering play mode, the editor calls `nav.Snapshot()` to save the current orbit state. When exiting (Esc), `nav.Restore()` returns the editor camera to where it was. This prevents play-mode camera movement from losing the editor's viewport position.

---

## 8. EvaluateFrameCamera -- Per-Frame Camera Selection

```cpp
FrameCameraResult EvaluateFrameCamera(EditorViewportNav& nav, float aspect, double now);
```

Defined in `src/editor/viewport_nav.cpp`. Called once per frame to decide which camera to use and build the final view/projection matrices. Returns a `FrameCameraResult` containing the matrices, eye position, forward/up vectors, and a pointer to the active `Camera3DNode`.

### Active Camera Selection

1. Scan all `gCamera3DNodes` for nodes with `main == true`.
2. Among those, pick the one with the highest `priority`.
3. If no camera has `main == true`, fall back to `gCamera3DNodes[0]`.
4. If there are no camera nodes at all, `activeCam` is null and the editor viewport camera is used directly.

### Edit Mode vs Play Mode

**Edit mode** (normal editing): The editor viewport camera (`EditorViewportNav`) drives everything. `activeCam` is tracked but not used for rendering -- it is available so the UI can highlight which camera is "active."

**Play mode** (`gPlayMode == true` and `activeCam` is non-null): The active `Camera3DNode` drives rendering. The pipeline:

1. Call `GetCamera3DWorldTR` to resolve the camera's world-space position and rotation (walking the parent chain).
2. Convert to a `Camera3D` via `BuildCamera3DFromLegacyEuler`.
3. Build `NebulaCamera3D::View` and `NebulaCamera3D::Projection`.
4. Build matrices using `NebulaCamera3D::BuildViewMatrix` and `NebulaCamera3D::BuildProjectionMatrix`.
5. Override `nav.viewYaw` and `nav.viewPitch` to match the play camera's forward direction (so returning to edit mode resumes from a similar angle).

---

## 9. Camera Hierarchy -- GetCamera3DWorldTR

```cpp
void GetCamera3DWorldTR(int idx, float& ox, float& oy, float& oz,
                        float& orx, float& ory, float& orz);
```

Defined in `src/viewport/node_helpers.cpp`. Resolves a `Camera3DNode`'s world-space position and rotation by walking up the parent chain.

### Algorithm

1. Start with the camera's local position (`x`, `y`, `z`) and rotation (`rotX`, `rotY`, `rotZ`).
2. If the camera has a parent, add the orbit offset (`orbitX/Y/Z`) to the local position. Orbit offsets are only applied when the camera is parented -- they represent a positional offset from the parent's pivot, not a transform-space offset.
3. Walk the parent chain (up to 256 levels, to guard against cycles):
   - For each parent node found (audio, static mesh, camera, or Node3D):
     - If the parent has rotation (static mesh, camera, Node3D), **rotate the accumulated offset** around the parent's pivot using the parent's Euler angles, then add the parent's position.
     - Accumulate the parent's rotation into the output rotation.
     - Continue to the parent's parent.

The rotation of the offset uses the Euler convention **Rx then Ry then Rz** applied sequentially, which orbits the child position around the parent's origin before translating by the parent's position. This means a camera parented to a rotating mesh will orbit around that mesh.

### Example

Consider a camera parented to a static mesh node:

```
StaticMesh "Turret"   pos=(5, 0, 0)  rot=(0, 45, 0)
  Camera3DNode "Cam"  pos=(0, 2, -3) orbit=(0, 0, 0)
```

The camera's world position is computed as:
1. Start: offset = (0, 2, -3)
2. Rotate offset by parent rotation (0, 45, 0): offset becomes roughly (2.12, 2, -2.12)
3. Add parent position: final = (7.12, 2, -2.12)
4. World rotation = camera rot + parent rot = (0, 45, 0)

---

## Summary: Data Flow Diagram

```
Scene File (.nebscene)
    |
    | Load
    v
Camera3DNode (Euler angles, per-scene)
    |
    |--- Edit mode: displayed in inspector, not used for rendering
    |
    |--- Play mode:
    |       |
    |       | GetCamera3DWorldTR() -- walk parent chain
    |       v
    |    World-space pos + Euler rot
    |       |
    |       | BuildCamera3DFromLegacyEuler()
    |       v
    |    Camera3D (forward/up vectors)
    |       |
    |       | BuildView() + BuildProjection()
    |       v
    |    Camera3DView + Camera3DProjection
    |       |
    |       | BuildViewMatrix() + BuildProjectionMatrix()
    |       v
    |    Mat4 view + Mat4 proj --> OpenGL rendering
    |
    |--- Dreamcast export:
            |
            | BuildDreamcastExport()
            v
         DreamcastExport (mirrored eye, focal lengths for 640x480)
            |
            v
         KOS pvr_* API on Dreamcast hardware

Editor Viewport (independent)
    |
    | EditorViewportNav (orbit/pan/zoom/WASD)
    |       |
    |       | ComputeEye() + yaw/pitch angles
    |       v
    |    eye + forward + up
    |       |
    |       | Mat4LookAt() / Mat4Perspective()
    |       v
    |    Mat4 view + Mat4 proj --> OpenGL rendering (edit mode only)
```

## See Also

- [Node Types](Node_Types.md) -- scene node definitions and hierarchy
- [Viewport and Rendering](../editor/Viewport_and_Rendering.md) -- 3D viewport and rendering pipeline
- [Dreamcast Export](../dreamcast/Dreamcast_Export.md) -- camera export and coordinate mirroring for Dreamcast
