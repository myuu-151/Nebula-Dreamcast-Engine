# Math Reference

This document covers the math foundation of the Nebula Dreamcast Engine: the core types, coordinate conventions, matrix and quaternion operations, projection pipeline, and numerical stability strategies. All implementations live in three files:

- `src/math/math_types.h` -- Vec3, Mat4 struct definitions
- `src/math/math_utils.h` -- Quat struct definition and all function declarations
- `src/math/math_utils.cpp` -- implementations

---

## 1. Core Types

### Vec3

```cpp
struct Vec3 { float x, y, z; };
```

A 3-component float vector. Used throughout the engine for positions, directions, scales, and Euler angles. No operator overloads -- arithmetic is done inline or via helper functions.

### Mat4

```cpp
struct Mat4 { float m[16]; };
```

A 4x4 matrix stored as 16 floats in a flat array. The engine uses **column-major** layout to match OpenGL conventions. The mapping from row/column to array index is:

```
Column 0    Column 1    Column 2    Column 3
m[0]        m[4]        m[8]        m[12]
m[1]        m[5]        m[9]        m[13]
m[2]        m[6]        m[10]       m[14]
m[3]        m[7]        m[11]       m[15]
```

Translation lives in `m[12]`, `m[13]`, `m[14]`. The perspective divide flag lives in `m[11]` (set to -1 for perspective projection).

### Quat

```cpp
struct Quat { float w, x, y, z; };
```

A unit quaternion representing a rotation. `w` is the scalar (real) part, `(x, y, z)` is the vector (imaginary) part. The identity quaternion is `{1, 0, 0, 0}`.

---

## 2. Coordinate Conventions

The engine uses a **right-handed coordinate system**:

- **+X** points right
- **+Y** points up
- **+Z** points toward the viewer (out of the screen)

This matches the default OpenGL convention.

### Matrix storage

Matrices are **column-major**, meaning `m[col*4 + row]` gives the element at a given row and column. This is the layout OpenGL expects when you pass a matrix via `glLoadMatrixf` or similar calls. When you read the code, `m[12..14]` is the translation column.

### Euler rotation order

Rotations follow the **ZYX** order. The combined rotation matrix is:

```
R = Rz * Ry * Rx
```

This means: rotate around X first (pitch), then Y (yaw), then Z (roll). All public API angles are in **degrees**. Internal trig calls convert to radians using `angle * pi / 180`.

---

## 3. Matrix Operations

### Mat4Identity

Returns the 4x4 identity matrix (ones on the diagonal, zeros elsewhere).

```
| 1  0  0  0 |
| 0  1  0  0 |
| 0  0  1  0 |
| 0  0  0  1 |
```

### Mat4Multiply

Multiplies two 4x4 matrices. The inner loop computes 4 dot products per output element, iterating over 4 rows and 4 columns = 16 elements, each requiring 4 multiply-adds. That is **64 multiplications and 48 additions** total.

```
R[row][col] = sum(k=0..3) A[row][k] * B[k][col]
```

Note: the implementation indexes into the flat array as `m[row*4 + col]`, which is a row-major indexing pattern applied to column-major data. This works correctly because the multiply is a standard matrix product -- the layout convention only matters when interfacing with OpenGL or interpreting individual elements.

### Mat4Perspective

Builds a symmetric perspective projection matrix from:

- `fovyRadians` -- vertical field of view in radians
- `aspect` -- width / height
- `znear`, `zfar` -- near and far clip planes

The resulting matrix:

```
| f/aspect  0       0                          0  |
| 0         f       0                          0  |
| 0         0  (zf+zn)/(zn-zf)   (2*zf*zn)/(zn-zf) |
| 0         0      -1                          0  |
```

where `f = 1 / tan(fov/2)`. The `-1` in `m[11]` enables the perspective divide (clip.w = -eye.z).

### Mat4Orthographic

Builds an orthographic projection matrix from axis-aligned bounds. No perspective divide -- parallel lines remain parallel.

```
| 2/(r-l)     0          0       -(r+l)/(r-l) |
|    0     2/(t-b)       0       -(t+b)/(t-b) |
|    0        0       -2/(f-n)   -(f+n)/(f-n) |
|    0        0          0            1        |
```

### Mat4LookAt

Builds a view matrix from eye position, target point, and up vector. Delegates to `NebulaCamera3D::BuildLookAtView` and `BuildViewMatrix` in `src/camera/camera3d.h`. The resulting matrix transforms world-space coordinates into view (eye) space.

---

## 4. Projection Pipeline

### MulMat4Vec4

Multiplies a Mat4 by a 4-component vector (x, y, z, w), producing a transformed 4-component output. Uses column-major access:

```
ox = m[0]*x + m[4]*y + m[8]*z  + m[12]*w
oy = m[1]*x + m[5]*y + m[9]*z  + m[13]*w
oz = m[2]*x + m[6]*y + m[10]*z + m[14]*w
ow = m[3]*x + m[7]*y + m[11]*z + m[15]*w
```

### ProjectToScreen

Transforms a world-space point into screen-space pixel coordinates. The pipeline has five stages:

1. **World to View**: Multiply the world position by the view matrix (w=1).
2. **View to Clip**: Multiply the view-space position by the projection matrix.
3. **Perspective divide check**: If `clip.w == 0`, the point is degenerate -- return false.
4. **Clip to NDC**: Divide x, y, z by w. If `ndc.z` is outside [-1, 1], the point is behind the camera or beyond the far plane -- return false.
5. **NDC to Screen**: Map from [-1, 1] to pixel coordinates with a Y flip:
   ```
   screenX = (ndcX * 0.5 + 0.5) * width
   screenY = (1.0 - (ndcY * 0.5 + 0.5)) * height
   ```

The Y flip converts from OpenGL's bottom-up NDC to screen coordinates where Y=0 is the top of the viewport.

---

## 5. Euler Angles and Local Axes

### GetLocalAxesFromEuler

Given Euler angles (rotX, rotY, rotZ) in degrees, computes the three local axis vectors (right, up, forward) by building the rotation matrix R = Rz * Ry * Rx and reading off its columns.

Shorthand: let `cx = cos(rotX)`, `sx = sin(rotX)`, and likewise for Y and Z. The nine matrix elements are:

```
m00 = cy*cz               m01 = cz*sx*sy - cx*sz    m02 = sx*sz + cx*cz*sy
m10 = cy*sz               m11 = cx*cz + sx*sy*sz    m12 = cx*sy*sz - cz*sx
m20 = -sy                 m21 = cy*sx               m22 = cx*cy
```

The columns of this matrix give the local axes:

- **right**   = (m00, m10, m20) -- the local X axis
- **up**      = (m01, m11, m21) -- the local Y axis
- **forward** = (m02, m12, m22) -- the local Z axis

These are used for:

- **Camera control**: extracting the direction a camera faces, computing strafe and fly directions.
- **Movement**: applying velocity along an object's forward or right vector.
- **Direction vectors**: e.g., getting a Node3D's facing direction from its rotation.

---

## 6. Quaternion Operations

### QuatFromAxisAngle

Converts an axis `(ax, ay, az)` and angle in degrees to a unit quaternion:

```
halfAngle = angleDeg * pi / 180 / 2
q.w = cos(halfAngle)
q.x = ax * sin(halfAngle)
q.y = ay * sin(halfAngle)
q.z = az * sin(halfAngle)
```

The input axis should be normalized. The function does not normalize it internally.

### QuatMultiply (Hamilton product)

Multiplies two quaternions following the Hamilton product convention:

```
(a.w + a.x*i + a.y*j + a.z*k) * (b.w + b.x*i + b.y*j + b.z*k)
```

Expanded:

```
w = a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z
x = a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y
y = a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x
z = a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w
```

Quaternion multiplication is **not commutative**: `QuatMultiply(a, b) != QuatMultiply(b, a)`. The result represents rotation `b` applied first, then rotation `a` -- matching the matrix convention `R_a * R_b`.

### QuatNormalize

Normalizes a quaternion to unit length by dividing all four components by the magnitude. Uses a threshold of **0.0001** -- if the magnitude is below this, the quaternion is left unchanged to avoid division by near-zero:

```
len = sqrt(w*w + x*x + y*y + z*z)
if (len > 0.0001) { w /= len; x /= len; y /= len; z /= len; }
```

### QuatToEuler

Extracts ZYX Euler angles (in degrees) from a quaternion. The conversion reconstructs the relevant rotation matrix elements from quaternion components:

```
m20 = 2*(qx*qz - qw*qy)    -> rotY = asin(-m20)
m21 = 2*(qy*qz + qw*qx)    -> rotX = atan2(m21, m22)
m22 = 1 - 2*(qx*qx + qy*qy)
m10 = 2*(qx*qy + qw*qz)    -> rotZ = atan2(m10, m00)
m00 = 1 - 2*(qy*qy + qz*qz)
```

The sine value for `rotY` is clamped to [-0.9999, 0.9999] before calling `asinf` to prevent NaN at gimbal lock (see Section 9).

### EulerToQuat

Converts Euler angles (degrees) to a quaternion by composing three axis-angle quaternions matching the ZYX convention:

```
qX = QuatFromAxisAngle(1, 0, 0, rotX)
qY = QuatFromAxisAngle(0, 1, 0, rotY)
qZ = QuatFromAxisAngle(0, 0, 1, rotZ)
result = qZ * qY * qX
```

The multiplication order matches R = Rz * Ry * Rx: X is applied first, Z last.

---

## 7. Advanced Quaternion Operations

### QuatFromNormalAndYaw

Builds an orientation quaternion that aligns an object to a surface normal while preserving a desired yaw (heading). This is the most involved math function in the engine. Used for things like aligning a character to sloped ground.

**Algorithm:**

1. **Normalize the surface normal** to get the desired "up" direction `(ux, uy, uz)`. If the normal is degenerate (length < 0.0001), return the identity quaternion.

2. **Compute the forward direction** from the yaw angle on the XZ plane:
   ```
   fwdX = sin(yawRad)
   fwdZ = cos(yawRad)
   ```

3. **Project forward onto the surface plane** using tangent plane projection. Remove the component of the forward vector along the surface normal:
   ```
   F' = F - dot(F, N) * N
   ```
   Then normalize `F'`. If the projected forward is degenerate (the yaw direction points directly along the normal), fall back to `EulerToQuat(0, yawDeg, 0)`.

4. **Compute the right vector** as the cross product `right = cross(up, forward)` and normalize it.

5. **Build a 3x3 rotation matrix** from columns [right, up, forward].

6. **Convert the rotation matrix to a quaternion** using **Shepperd's method** (see Section 9). This method selects the optimal extraction path based on which diagonal element is largest, avoiding numerical instability when the trace is near zero.

7. **Normalize** the resulting quaternion.

### QuatNlerp (Normalized Linear Interpolation)

Interpolates between a current quaternion and a target quaternion by fraction `t`, producing a normalized result. This is a cheaper alternative to Slerp that gives visually acceptable results for small to moderate angular differences.

**Steps:**

1. **Shortest path check**: Compute `dot(cur, target)`. If negative, negate the target to ensure the interpolation takes the short way around the 4D sphere. Without this, the interpolation could rotate 270 degrees instead of 90.

2. **Linear blend**: For each component:
   ```
   cur.w += (target.w - cur.w) * t
   ```
   This is equivalent to `lerp(cur, target, t)` but written as an in-place update.

3. **Normalize**: The linear blend does not preserve unit length, so normalize afterward.

The function modifies `cur` in place.

### QuatYawDeg

Extracts the yaw angle (heading, rotation around Y) from a quaternion, in degrees. Works by rotating the forward vector (0, 0, 1) by the quaternion and computing the yaw from the resulting XZ components:

```
fx = 2*(qx*qz + qw*qy)
fz = 1 - 2*(qx*qx + qy*qy)
yaw = atan2(fx, fz) * (180 / pi)
```

This correctly ignores pitch and roll, giving a clean heading value.

---

## 8. Node3D Sync Functions

Node3D objects store rotation in two representations simultaneously: Euler angles (`rotX`, `rotY`, `rotZ`) and quaternion (`qw`, `qx`, `qy`, `qz`). Both fields live on the `Node3DNode` struct. Keeping them in sync is critical because different systems prefer different representations:

- The **editor UI** displays and edits Euler angles (intuitive for artists).
- The **runtime** and advanced math (surface alignment, interpolation) use quaternions (no gimbal lock, composable).

### SyncNode3DQuatFromEuler

Called after the editor modifies Euler angles. Converts the node's `(rotX, rotY, rotZ)` to a quaternion and writes it to `(qw, qx, qy, qz)`:

```cpp
Quat q = EulerToQuat(n.rotX, n.rotY, n.rotZ);
n.qw = q.w; n.qx = q.x; n.qy = q.y; n.qz = q.z;
```

### SyncNode3DEulerFromQuat

Called after the runtime modifies the quaternion (e.g., after a QuatNlerp or surface alignment). Converts the node's `(qw, qx, qy, qz)` back to Euler angles and writes `(rotX, rotY, rotZ)`:

```cpp
QuatToEuler(n.qw, n.qx, n.qy, n.qz, n.rotX, n.rotY, n.rotZ);
```

**Rule of thumb:** Always call the appropriate sync function after modifying either representation. Forgetting to sync will cause the editor display and runtime behavior to disagree.

---

## 9. Numerical Stability

Several functions include guards against edge cases that would otherwise produce NaN, infinity, or wildly incorrect results.

### Normalization threshold (0.0001)

`QuatNormalize` and `QuatFromNormalAndYaw` both skip division when a vector's magnitude falls below 0.0001. This prevents division-by-zero when a quaternion or normal is degenerate (e.g., all components near zero). The threshold is deliberately conservative -- a truly unit-length quaternion has magnitude 1.0, so anything below 0.0001 indicates garbage data.

### Sine clamping in QuatToEuler

The `asinf` function is only defined for inputs in [-1, 1]. Due to floating-point arithmetic, the reconstructed `sin(rotY)` value `(-m20)` can slightly exceed this range. The code clamps it to [-0.9999, 0.9999]:

```cpp
if (sy > 0.9999f) sy = 0.9999f;
if (sy < -0.9999f) sy = -0.9999f;
```

This also prevents gimbal lock from producing extreme angle values. At exactly +/-90 degrees pitch, the X and Z axes align and Euler representation becomes ambiguous. The 0.9999 clamp keeps the output in a numerically stable regime.

### Degenerate vector fallbacks

In `QuatFromNormalAndYaw`, two degenerate cases are handled:

1. **Degenerate surface normal** (length < 0.0001): Returns identity quaternion `{1, 0, 0, 0}` instead of attempting to build an orientation from a zero-length vector.

2. **Degenerate projected forward** (length < 0.0001): This happens when the desired forward direction is parallel to the surface normal (e.g., looking straight down a vertical wall). Falls back to a simple yaw-only Euler rotation `EulerToQuat(0, yawDeg, 0)`.

### Shepperd's method for matrix-to-quaternion

The naive formula for extracting a quaternion from a rotation matrix uses the trace (`m00 + m11 + m22`). When the trace is near zero, the naive formula produces catastrophic cancellation. Shepperd's method avoids this by checking which of four possible extraction paths will be most numerically stable:

- If `trace > 0`: use the standard trace-based formula.
- If `m00` is the largest diagonal element: extract `q.x` first.
- If `m11` is the largest diagonal element: extract `q.y` first.
- If `m22` is the largest diagonal element: extract `q.z` first.

Each branch computes one component from a square root of a positive quantity (guaranteed by the branch condition), then derives the other three components from ratios. This ensures no square root of a negative number and minimal floating-point error regardless of the rotation's orientation.

---

## Summary of Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| Column-major Mat4 | Direct compatibility with OpenGL without transposing |
| ZYX Euler order | Matches common 3D convention; yaw (Y) is the primary gameplay rotation axis |
| Degrees in public API | More intuitive for editor UI and script authors |
| Dual Euler + Quat on Node3D | Euler for human editing, quaternions for runtime math; sync functions bridge them |
| Nlerp instead of Slerp | Cheaper, sufficient for per-frame interpolation with small time steps |
| Shepperd's method | Robust matrix-to-quaternion for arbitrary orientations (surface alignment) |
| 0.0001 thresholds | Prevent NaN/Inf from degenerate inputs without rejecting valid near-zero values |
