# Physics and Collision System

This document explains how Nebula's physics and collision system works, from the
per-frame tick loop down to individual raycast and overlap tests. All physics
code runs identically in the desktop editor (play mode) and the Dreamcast
runtime.

---

## 1. Overview

Physics **only runs during play mode**. In the editor's edit mode, nodes sit
wherever you place them -- no gravity, no collision. When you press Play, the
engine calls `TickPlayModePhysics(dt)` once per frame. That function does three
things in order:

1. **Gravity** -- accelerates `velY` downward for physics-enabled nodes.
2. **Collision resolution** -- ground snap, slope alignment, and wall push-out
   against StaticMesh3D triangles.
3. **Node3D-vs-Node3D overlap** -- pushes apart any Node3D pairs whose AABBs
   intersect.

The frame delta `dt` is clamped by the caller before being passed in, so a lag
spike will not cause objects to tunnel through geometry.

Source: `src/runtime/physics.h`, `src/runtime/physics.cpp`

---

## 2. TickPlayModePhysics

```
TickPlayModePhysics(dt)
    for each Node3DNode:
        skip if none of { physicsEnabled, collisionSource, simpleCollision }

        if physicsEnabled:
            velY += gravity * dt          // gravity = -29.4 units/sec^2
            y    += velY * dt             // Euler integration

        if collisionSource or simpleCollision:
            ResolveNodeCollision(nodeIndex, dt)

    ResolveNode3DOverlaps()               // all-pairs AABB pass
```

Key points:

- Gravity is **-29.4 units/sec^2** (roughly 3x Earth gravity -- tuned for
  game-feel on Dreamcast).
- Gravity only applies when `physicsEnabled` is true. A node can have collision
  without gravity (e.g., a moving platform that slides but never falls).
- `ResolveNode3DOverlaps` runs once after all individual nodes have been
  resolved.

Source: `src/runtime/physics.cpp`

---

## 3. Node3D Collision Fields

Every `Node3DNode` carries these collision-related fields (defined in
`src/nodes/Node3DNode.h`):

| Field | Type | Default | Purpose |
|---|---|---|---|
| `simpleCollision` | bool | false | Enables AABB collision (ground snap + wall push-out) but no slope alignment |
| `collisionSource` | bool | false | Enables full collision: ground snap, slope alignment, and wall push-out |
| `physicsEnabled` | bool | false | Enables gravity (velY accumulation). Usually combined with one of the above |
| `extentX` | float | 0.5 | Half-extent of the collision AABB along X |
| `extentY` | float | 0.5 | Half-extent of the collision AABB along Y |
| `extentZ` | float | 0.5 | Half-extent of the collision AABB along Z |
| `boundPosX` | float | 0.0 | Local offset of the collision AABB center from the node origin, X |
| `boundPosY` | float | 0.0 | Local offset of the collision AABB center from the node origin, Y |
| `boundPosZ` | float | 0.0 | Local offset of the collision AABB center from the node origin, Z |
| `velY` | float | 0.0 | Vertical velocity in units/sec (modified by gravity and ground snap) |

The AABB center in world space is `(node.x + boundPosX, node.y + boundPosY,
node.z + boundPosZ)`. The box extends `extentX/Y/Z` in each direction from that
center. When used in wall collision and raycasting, extents are multiplied by the
node's world scale.

```
        +--- extentX ---+--- extentX ---+
        |               |               |
        |         (boundPosX/Y/Z)        |
        |           offset from          |
        |          node origin           |
        +-------------------------------+
                   2 * extentX
```

### simpleCollision vs collisionSource

- **simpleCollision** gives you ground snap and wall push-out but **no slope
  alignment**. The node stays upright regardless of the surface it stands on.
  Good for NPCs, collectibles, and simple objects.

- **collisionSource** gives you everything simpleCollision does **plus slope
  alignment**. The node tilts to match the ground surface normal. Good for the
  player character or vehicles.

Both flags participate in Node3D-vs-Node3D overlap resolution.

---

## 4. Ground Snap (Raycasting)

Ground snap prevents physics-enabled nodes from falling through the floor. It
works by casting a vertical ray downward from just above the bottom of the
node's collision AABB.

### How it works

1. Compute the ray origin:
   - X = world X + boundPosX
   - Y = world Y + boundPosY - extentY*scaleY + 0.5 (slightly above AABB bottom)
   - Z = world Z + boundPosZ

2. Call `NB_RT_RaycastDownWithNormal`, which tests this ray against every
   triangle of every `collisionSource`-flagged StaticMesh3D in the scene.

3. For each triangle, the raycast performs a **2D barycentric test in the XZ
   plane**. Because the ray is purely vertical (pointing straight down), the
   problem reduces to checking whether the ray's XZ position falls inside the
   triangle's XZ projection:

```
    Top-down view (XZ plane):

         A
        / \
       /   \         * Ray (rx, rz)
      /  *  \        Falls inside triangle ABC
     /       \       => compute hitY via barycentric interpolation
    B---------C
```

4. The hit Y is computed by barycentric interpolation:
   `hitY = Ay + (By - Ay) * u + (Cy - Ay) * v`

5. Only hits **below** the ray origin are accepted (`hitY <= ry`). Wall-like
   triangles (where the face normal's Y component is negative after
   normalization) are skipped -- only floor surfaces are valid ground.

6. The **highest** qualifying hit is selected as the ground surface.

### Snap behavior

Once a ground hit is found, the node is snapped:

```
groundY = hitY - boundPosY + extentY * scaleY
if (node.y <= groundY):
    node.y = groundY
    if (velY < 0): velY = 0     // stop falling
```

This places the bottom of the AABB on the ground surface. The `velY = 0` reset
prevents the node from accumulating downward velocity while resting on the
floor.

Source: `src/runtime/collision.cpp` (ResolveNodeCollision), `src/runtime/runtime_bridge.cpp` (NB_RT_RaycastDown, NB_RT_RaycastDownWithNormal)

---

## 5. Slope Alignment

When `collisionSource` is enabled (not just `simpleCollision`), the node
smoothly tilts to match the ground surface normal. This gives a natural look
when a character walks over uneven terrain.

### Algorithm

1. Extract the node's current "up" vector from its quaternion orientation.

2. Compute a **target normal**: if the ground hit normal points upward
   (`ny > 0`), use it directly. Otherwise, target the flat-upright vector
   `(0, 1, 0)`.

3. **Exponential decay interpolation** blends the current up toward the target:
   ```
   t = 1.0 - pow(0.0001, dt)
   smoothed = current + (target - current) * t
   normalize(smoothed)
   ```
   The `pow(0.0001, dt)` factor makes the blend frame-rate-independent. At 60
   fps, `t` is roughly 0.85, so the node reaches the target orientation quickly
   but not instantaneously.

4. Build a new quaternion via `QuatFromNormalAndYaw(smoothedNormal, savedYaw)`.
   This constructs an orientation where:
   - The node's local up axis aligns with the smoothed surface normal.
   - The node's yaw (horizontal facing direction) is preserved exactly as the
     script set it.

5. Sync Euler angles back from the quaternion for serialization and UI display,
   then restore the original yaw value to prevent drift.

When there is **no ground hit** (the node is in the air), the same interpolation
runs but targets the upright normal `(0, 1, 0)`, gradually returning the node to
a level orientation.

Source: `src/runtime/collision.cpp` (ResolveNodeCollision), `src/math/math_utils.h` (QuatFromNormalAndYaw)

---

## 6. Wall Collision

`WallCollideAABB` tests a node's AABB against the triangles of all
`collisionSource`-flagged StaticMesh3D nodes, but only considers **wall
triangles** -- triangles whose face normal is mostly horizontal.

### Wall threshold

Each StaticMesh3D has a `wallThreshold` field (default 0.7, range 0.0 to 1.0).
A triangle is classified as a wall if:

```
abs(faceNormal.y) <= wallThreshold
```

At the default of 0.7, surfaces steeper than roughly 45 degrees from vertical
are treated as walls. Lower values make more surfaces count as walls.

### Push-out computation

For each wall triangle that overlaps the AABB:

1. **Transform** the triangle vertices to world space (scale, rotate, translate).
2. **Compute the face normal** via cross product of two edges.
3. **Filter**: skip if `abs(normalY) > wallThreshold` (this is a floor/ceiling,
   not a wall).
4. **Bounding box overlap**: early-out if the triangle's axis-aligned bounds
   do not overlap the AABB in Y, X, or Z.
5. **Signed distance**: compute the signed distance `d` from the AABB center to
   the triangle plane, and the AABB's projected extent `projDist` along the
   normal. Penetration is `projDist - abs(d)`.
6. **Horizontal push**: project the face normal onto the XZ plane (discard the Y
   component), normalize, and push the node out by the penetration amount along
   that direction.

```
    Side view:
                        Wall triangle
                       /|
                      / |
    +------+         /  |
    | AABB | ---->  /   |     Node pushes away from wall
    +------+       /    |     along horizontal normal
                  /     |
```

The push is accumulated across all penetrating wall triangles, then applied to
the node's X and Z position. Y is never modified by wall collision.

Source: `src/runtime/collision.cpp` (WallCollideAABB)

---

## 7. Node3D-vs-Node3D Overlap

`ResolveNode3DOverlaps` runs a brute-force all-pairs test on every Node3D that
has `collisionSource` or `simpleCollision` enabled. It pushes overlapping pairs
apart along the axis of least penetration in the XZ plane.

### Algorithm

For each pair (A, B):

1. Compute AABB centers with bound offsets applied:
   `center = (x + boundPosX, y + boundPosY, z + boundPosZ)`

2. Compute overlap on each axis:
   ```
   overlapX = (A.extentX + B.extentX) - abs(A.centerX - B.centerX)
   overlapY = (A.extentY + B.extentY) - abs(A.centerY - B.centerY)
   overlapZ = (A.extentZ + B.extentZ) - abs(A.centerZ - B.centerZ)
   ```

3. If any overlap is zero or negative, the AABBs do not intersect -- skip.

4. Find the **smallest penetration among X and Z only**. Y overlap is required
   for intersection but is never used as the push axis (nodes do not push each
   other vertically -- that would fight gravity).

5. Push each node by half the penetration in opposite directions along the
   chosen axis:
   ```
   half = minPenetration * 0.5
   A moves +half along axis
   B moves -half along axis
   ```

```
    Top-down view (XZ plane):

    Before:                       After:
    +-----+                       +-----+
    |  A  |--+                    |  A  |  +-----+
    +-----+  |                    +-----+  |  B  |
         +-----+                           +-----+
         |  B  |
         +-----+
         <---->
        overlapX (smallest) => push apart on X
```

Source: `src/runtime/collision.cpp` (ResolveNode3DOverlaps)

---

## 8. NB_RT_CheckAABBOverlap (Script API)

```c
int NB_RT_CheckAABBOverlap(const char* name1, const char* name2);
```

A script-callable function that tests whether two named Node3D nodes' AABBs
overlap. Unlike `ResolveNode3DOverlaps`, this function does **not** push anything
apart -- it is a pure query.

It accounts for:
- **Bound offsets** (`boundPosX/Y/Z`) added to each node's position.
- **Scale**: extents are multiplied by each node's `scaleX/Y/Z`.

Returns 1 if the AABBs overlap on all three axes, 0 otherwise.

### Typical usage in a gameplay script

```c
void NB_Game_OnUpdate(float dt)
{
    // Check if the player touched a pickup
    if (NB_RT_CheckAABBOverlap("Player", "HealthPickup"))
    {
        // Handle pickup...
    }
}
```

Source: `src/runtime/runtime_bridge.cpp` (NB_RT_CheckAABBOverlap)

---

## 9. NB_RT_IsNode3DOnFloor (Script API)

```c
int NB_RT_IsNode3DOnFloor(const char* name);
```

Returns 1 if the named node is considered "on the floor". The test is:

```
physicsEnabled  AND  velY >= 0.0  AND  velY < 0.01
```

In other words: the node must have physics enabled, and its vertical velocity
must be essentially zero (landed). This catches both the exact-zero case after
ground snap resets `velY` and any tiny floating-point residual.

Use this to gate jump input -- only allow the player to jump when on the floor.

Source: `src/runtime/runtime_bridge.cpp` (NB_RT_IsNode3DOnFloor)

---

## 10. Raycasting API (Script API)

Two raycast functions are available to scripts. Both cast a vertical ray
**downward** from a given point and find the highest `collisionSource`
StaticMesh3D surface below it.

### NB_RT_RaycastDown

```c
int NB_RT_RaycastDown(float rx, float ry, float rz, float* outHitY);
```

- Ray origin: `(rx, ry, rz)`
- Returns 1 on hit, 0 on miss.
- On hit, `*outHitY` is set to the Y coordinate of the surface.

### NB_RT_RaycastDownWithNormal

```c
int NB_RT_RaycastDownWithNormal(float rx, float ry, float rz,
                                 float* outHitY, float outNormal[3]);
```

Same as `NB_RT_RaycastDown` but also outputs the surface normal of the hit
triangle in `outNormal[3]`. The normal is guaranteed to point upward (`ny > 0`)
-- if the raw cross product points downward, it is flipped.

### How both functions work internally

1. Iterate every StaticMesh3D node where `collisionSource` is true.
2. Load the mesh data (`.nebmesh` file via cached mesh loader).
3. Transform each triangle to world space using the mesh's TRS (scale, rotation
   via local axes, translation).
4. For each triangle, perform a 2D barycentric test in XZ (see section 4).
5. Compute the hit Y via interpolation. Accept only if `hitY <= ry` (below the
   ray origin) and the triangle is floor-like (upward-facing normal).
6. Track the **highest** valid hit across all meshes and triangles.

### Usage example

```c
void NB_Game_OnUpdate(float dt)
{
    float pos[3];
    NB_RT_GetNode3DPosition("Player", pos);

    float groundY;
    if (NB_RT_RaycastDown(pos[0], pos[1] + 1.0f, pos[2], &groundY))
    {
        // groundY is the floor surface directly below the player
    }

    // Or with surface normal for custom slope logic:
    float normal[3];
    float hitY;
    if (NB_RT_RaycastDownWithNormal(pos[0], pos[1] + 1.0f, pos[2], &hitY, normal))
    {
        // normal[0], normal[1], normal[2] = surface normal
    }
}
```

Source: `src/runtime/runtime_bridge.cpp` (NB_RT_RaycastDown, NB_RT_RaycastDownWithNormal)

---

## Source File Summary

| File | Contents |
|---|---|
| `src/runtime/physics.h` | `TickPlayModePhysics` declaration |
| `src/runtime/physics.cpp` | Per-frame gravity loop, calls collision functions |
| `src/runtime/collision.h` | `WallCollideAABB`, `ResolveNode3DOverlaps`, `ResolveNodeCollision` declarations |
| `src/runtime/collision.cpp` | Wall collision, ground snap, slope alignment, Node3D overlap |
| `src/runtime/runtime_bridge.h` | `NB_RT_*` function declarations (script API) |
| `src/runtime/runtime_bridge.cpp` | Raycast, AABB overlap, on-floor, and collision property get/set implementations |
| `src/nodes/Node3DNode.h` | `Node3DNode` struct with collision fields |
| `src/math/math_utils.h` | `QuatFromNormalAndYaw`, `SyncNode3DEulerFromQuat`, and other math helpers |

## See Also

- [Node Types](../scene-and-nodes/Node_Types.md) -- Node3DNode collision fields and hierarchy
- [Scripting](../getting-started/Scripting.md) -- NB_RT_* bridge functions for collision and physics
- [Navigation](Navigation.md) -- navmesh pathfinding system
