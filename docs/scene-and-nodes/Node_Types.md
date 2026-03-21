# Nebula Node Types

This document describes the five scene node types that make up every Nebula scene. All nodes are plain C++ data structs stored in `std::vector` containers inside `SceneData`. There are no virtual functions, no inheritance hierarchies, and no entity-component indirection -- each node type is a flat struct with public fields.

Source files live under `src/nodes/`:

| File | Purpose |
|------|---------|
| `NodeTypes.h` | Umbrella header, `SceneData` struct, `NebulaNodes` namespace |
| `Audio3DNode.h` / `.cpp` | Spatial audio node |
| `StaticMesh3DNode.h` / `.cpp` | Renderable mesh node |
| `Camera3DNode.h` | Scene camera node |
| `Node3DNode.h` / `.cpp` | Non-visual transform/collision node |
| `NavMesh3DNode.h` / `.cpp` | Navigation mesh volume node |

---

## 1. SceneData -- the container

Every loaded scene is represented by a single `SceneData` instance (defined in `NodeTypes.h`):

```cpp
struct SceneData
{
    std::filesystem::path path;
    std::string name;
    std::vector<Audio3DNode>      nodes;         // audio nodes
    std::vector<StaticMesh3DNode> staticMeshes;
    std::vector<Camera3DNode>     cameras;
    std::vector<Node3DNode>       node3d;
    std::vector<NavMesh3DNode>    navMeshes;
};
```

Each node type lives in its own flat vector. The editor indexes into these vectors directly -- there is no unified node list.

---

## 2. Common fields

Every node type shares three string fields (by convention, not inheritance):

| Field | Type | Description |
|-------|------|-------------|
| `name` | `std::string` | Unique name within the scene. Used for parent references and script lookups. |
| `parent` | `std::string` | Name of the parent node (empty string = root). Can reference a node of a different type. |
| `script` | `std::string` | Path to the gameplay script (`.c` file) attached to this node. Not present on `Camera3DNode` or `NavMesh3DNode`. |

All node types also carry position (`x`, `y`, `z`), rotation (`rotX`, `rotY`, `rotZ`), and scale (`scaleX`, `scaleY`, `scaleZ`) fields, though some types add additional rotation representations (quaternion on Node3D, orbit offsets on Camera3D).

---

## 3. Audio3DNode

**Header:** `src/nodes/Audio3DNode.h`
**Implementation:** `src/nodes/Audio3DNode.cpp`

Audio3DNode represents a spatial audio emitter in the scene. It calculates volume attenuation and stereo pan based on listener distance each frame.

### Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `name` | `string` | | Node name |
| `parent` | `string` | | Parent node name |
| `script` | `string` | | Attached script path |
| `x`, `y`, `z` | `float` | 0 | World position |
| `rotX`, `rotY`, `rotZ` | `float` | 0 | Rotation (degrees) |
| `scaleX`, `scaleY`, `scaleZ` | `float` | 1 | Scale |
| `innerRadius` | `float` | 3.0 | Distance within which volume is at maximum |
| `outerRadius` | `float` | 10.0 | Distance at which volume drops to zero |
| `baseVolume` | `float` | 1.0 | Source volume multiplier (author-set) |
| `pan` | `float` | 0.0 | Computed stereo pan, -1 (left) to 1 (right) |
| `volume` | `float` | 1.0 | Computed volume after attenuation, 0 to 1 |
| `inside` | `bool` | false | True when listener is within `outerRadius` |
| `justEntered` | `bool` | false | True for one frame when listener first enters `outerRadius` |

### Attenuation model

`UpdateAudio3DNodes(listenerX, listenerY, listenerZ)` runs once per frame over the global `gAudio3DNodes` vector. For each node:

1. Compute Euclidean distance from listener to node position.
2. If `outerRadius < innerRadius`, swap them. Clamp `outerRadius` to a minimum of 0.001.
3. Apply linear attenuation:
   - `dist <= innerRadius`: attenuation = 1.0 (full volume)
   - `dist >= outerRadius`: attenuation = 0.0 (silent)
   - Between: linear interpolation `1.0 - (dist - inner) / (outer - inner)`
4. Compute `volume = baseVolume * attenuation`, clamped to [0, 1].
5. Compute `pan = dx / outerRadius`, clamped to [-1, 1], where `dx` is the X-axis distance from the listener to the node.

### Entry/exit tracking

The `inside` flag tracks whether the listener is within `outerRadius`. The `justEntered` flag is true for exactly one frame when the listener transitions from outside to inside. This enables one-shot trigger behavior (e.g., play a sound effect when the player enters an area).

---

## 4. StaticMesh3DNode

**Header:** `src/nodes/StaticMesh3DNode.h`
**Implementation:** `src/nodes/StaticMesh3DNode.cpp`

StaticMesh3DNode is the primary renderable node. It references a `.nebmesh` file and supports multiple material slots and animation slots.

### Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `name` | `string` | | Node name |
| `parent` | `string` | | Parent node name |
| `script` | `string` | | Attached script path |
| `material` | `string` | | Legacy single-material path (kept for backward compat) |
| `materialSlots` | `array<string, 14>` | all empty | Up to 14 material paths, one per submesh |
| `materialSlot` | `int` | 0 | Currently active/primary material slot index |
| `mesh` | `string` | | Path to the `.nebmesh` file |
| `vtxAnim` | `string` | | Legacy single vertex animation path |
| `animSlots` | `array<AnimSlot, 8>` | | Up to 8 named animation slots |
| `animSlotCount` | `int` | 0 | Number of populated animation slots |
| `x`, `y`, `z` | `float` | 0 | Position |
| `rotX`, `rotY`, `rotZ` | `float` | 0 | Euler rotation (degrees) |
| `scaleX`, `scaleY`, `scaleZ` | `float` | 1 | Scale |
| `collisionSource` | `bool` | false | If true, this mesh's geometry is used as collision source for wall/ground tests |
| `runtimeTest` | `bool` | false | If true, runtime collision tests are performed against this mesh |
| `navmeshReady` | `bool` | false | If true, this mesh has been preprocessed for navigation mesh building |
| `wallThreshold` | `float` | 0.7 | Normal dot-product threshold for classifying faces as walls vs. floors (used in collision) |

### Material slots

The constant `kStaticMeshMaterialSlots = 14` defines the maximum number of material slots per mesh. Each slot maps to a submesh index and holds a path to a `.nebmat` file.

The `NebulaNodes` namespace provides helpers for working with material slots:

- **`GetStaticMeshPrimaryMaterial(n)`** -- Returns the material path for the active slot. Falls back to slot 0, then to the legacy `material` field.
- **`GetStaticMeshMaterialByIndex(n, index)`** -- Returns the material path at a specific slot index.
- **`GetStaticMeshSlotLabel(n, slotIndex, projectDir)`** -- Derives a human-readable label for a slot by checking the `mat/` directory naming convention, then the `.nebslots` manifest, then the slot path stem.
- **`AutoAssignMaterialSlotsFromMesh(n)`** -- Syncs the legacy `material` field with `materialSlots[0]` in both directions.

### Animation slots (AnimSlot)

```cpp
constexpr int kStaticMeshAnimSlots = 8;

struct AnimSlot
{
    std::string name;      // Slot name (e.g., "idle", "walk")
    std::string path;      // Path to .nebanim file
    float speed = 1.0f;    // Playback speed multiplier
    bool loop = true;      // Whether the animation loops
};
```

Each `StaticMesh3DNode` can hold up to 8 animation slots. The `animSlotCount` field tracks how many are actually populated. Animation slots allow scripts to switch between named animations at runtime (e.g., `NB_RT_PlayAnimSlot`).

---

## 5. Camera3DNode

**Header:** `src/nodes/Camera3DNode.h`

Camera3DNode defines a scene camera. There is no `.cpp` file -- the struct is pure data, and the editor and runtime handle camera logic elsewhere.

### Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `name` | `string` | | Node name |
| `parent` | `string` | | Parent node name |
| `x`, `y`, `z` | `float` | 0, 2, -6 | Position (note the default offset) |
| `rotX`, `rotY`, `rotZ` | `float` | 0 | Euler rotation (degrees) |
| `orbitX`, `orbitY`, `orbitZ` | `float` | 0 | Local orbit offset around the inherited/root pivot |
| `perspective` | `bool` | true | True for perspective projection, false for orthographic |
| `fovY` | `float` | 70.0 | Vertical field of view in degrees (perspective mode) |
| `nearZ` | `float` | 0.25 | Near clipping plane |
| `farZ` | `float` | 4096.0 | Far clipping plane |
| `orthoWidth` | `float` | 12.8 | Orthographic viewport width (ortho mode) |
| `priority` | `float` | 0.0 | Priority for active camera selection (highest wins) |
| `main` | `bool` | false | If true, this camera is the scene's designated main camera |

### Projection modes

When `perspective` is true, the camera uses a standard perspective projection with `fovY` controlling the vertical field of view. When false, it uses orthographic projection with `orthoWidth` controlling the visible width.

### Priority system

Multiple cameras can exist in a scene. The active camera is selected by checking the `main` flag first, then falling back to the camera with the highest `priority` value. This allows scripts to dynamically switch cameras by adjusting priority values at runtime.

### Orbit offsets

The `orbitX`/`orbitY`/`orbitZ` fields provide a local position offset that is applied relative to the camera's inherited pivot point (from parenting). This is useful for third-person camera setups where the camera orbits around a character node without modifying the base transform.

### Note on missing fields

Camera3DNode does not have a `script` field. Camera behavior is typically controlled by the script attached to the node it is parented to, or by direct runtime API calls.

---

## 6. Node3DNode

**Header:** `src/nodes/Node3DNode.h`
**Implementation:** `src/nodes/Node3DNode.cpp`

Node3DNode is a non-visual transform node used for hierarchy organization, collision volumes, and physics. It is the workhorse for gameplay entities -- player characters, enemies, trigger zones, and any object that needs collision or scripted behavior but does not directly render a mesh.

### Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `name` | `string` | | Node name |
| `parent` | `string` | | Parent node name |
| `script` | `string` | | Attached script path |
| `primitiveMesh` | `string` | `"assets/cube_primitive.nebmesh"` | Debug visualization mesh |
| `x`, `y`, `z` | `float` | 0 | Position |
| `rotX`, `rotY`, `rotZ` | `float` | 0 | Cached Euler rotation (degrees), kept in sync with quaternion |
| `qw`, `qx`, `qy`, `qz` | `float` | 1,0,0,0 | Quaternion orientation (authoritative source of truth) |
| `scaleX`, `scaleY`, `scaleZ` | `float` | 1 | Scale |
| `simpleCollision` | `bool` | false | Enable simple AABB collision testing |
| `collisionSource` | `bool` | false | This node acts as a collision source |
| `physicsEnabled` | `bool` | false | Enable gravity/physics simulation |
| `extentX`, `extentY`, `extentZ` | `float` | 0.5 | Collision AABB half-extents |
| `boundPosX`, `boundPosY`, `boundPosZ` | `float` | 0 | Local offset of the collision bounds from the node origin |
| `velY` | `float` | 0 | Vertical velocity (used by physics/gravity) |

### Dual rotation representation

Node3DNode maintains two rotation representations:

1. **Quaternion (`qw`, `qx`, `qy`, `qz`)** -- the authoritative orientation. Used for slope alignment, parent chain accumulation, and any operation that must avoid gimbal lock.
2. **Euler angles (`rotX`, `rotY`, `rotZ`)** -- cached for the editor UI, scene serialization, and script compatibility. These are derived from the quaternion and may not round-trip perfectly through extreme orientations.

The quaternion representation exists because Node3D nodes are often used for characters that align to terrain slopes. Euler angles suffer from gimbal lock in those cases, while quaternion multiplication composes rotations cleanly through the parent chain.

### Collision system

The collision fields (`simpleCollision`, `collisionSource`, `extentX/Y/Z`, `boundPosX/Y/Z`) define an axis-aligned bounding box (AABB) for collision testing. The extents define the half-size of the box, and `boundPos` offsets the box center from the node's position. This separation allows the collision volume to be positioned independently of the visual transform (e.g., raising a collision box above the ground plane).

When `physicsEnabled` is true, the engine applies gravity each frame, accumulating into `velY` and updating the node's `y` position. Ground snapping and wall collision use the collision AABB against meshes marked as `collisionSource`.

---

## 7. NavMesh3DNode

**Header:** `src/nodes/NavMesh3DNode.h`
**Implementation:** `src/nodes/NavMesh3DNode.cpp`

NavMesh3DNode defines a volume that controls navigation mesh generation. It does not render geometry at runtime -- it is an editor-only construct that feeds into the Recast/Detour navmesh builder.

### Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `name` | `string` | | Node name |
| `parent` | `string` | | Parent node name |
| `x`, `y`, `z` | `float` | 0 | Position |
| `rotX`, `rotY`, `rotZ` | `float` | 0 | Rotation (degrees) |
| `scaleX`, `scaleY`, `scaleZ` | `float` | 1 | Scale |
| `extentX`, `extentY`, `extentZ` | `float` | 10 | Bounding box full-size extents |
| `navBounds` | `bool` | true | If true, this volume defines the navmesh generation area |
| `navNegator` | `bool` | false | If true, this volume carves out (subtracts from) the navmesh |
| `cullWalls` | `bool` | false | If true, wall faces within this volume are culled from navmesh generation |
| `wallCullThreshold` | `float` | 0.2 | Normal threshold for wall culling |
| `wireR`, `wireG`, `wireB` | `float` | 0.1, 1.0, 0.25 | Wireframe visualization color (editor only) |
| `wireThickness` | `float` | 1.0 | Wireframe line thickness (editor only) |

### Volume roles

A NavMesh3DNode serves one of three roles:

- **Bounds (`navBounds = true`):** Defines the region where the navmesh is built. Only geometry inside this volume is considered.
- **Negator (`navNegator = true`):** Subtracts from the navmesh. Geometry inside this volume is excluded, creating holes or blocked areas.
- **Wall culler (`cullWalls = true`):** Removes wall-facing triangles from the navmesh input based on `wallCullThreshold`, preventing agents from pathing up steep surfaces.

### Note on missing fields

NavMesh3DNode does not have a `script` field. It is a purely declarative volume used during navmesh baking, not a runtime gameplay object.

---

## 8. NebulaNodes namespace utilities

The `NebulaNodes` namespace (declared in `NodeTypes.h`, implemented across `Audio3DNode.cpp`, `StaticMesh3DNode.cpp`, and `Node3DNode.cpp`) provides cross-type utility functions.

### Lookup

| Function | Description |
|----------|-------------|
| `FindStaticMeshByName(vec, name)` | Returns the index of the named StaticMesh3DNode, or -1 |
| `FindNode3DByName(vec, name)` | Returns the index of the named Node3DNode, or -1 |

### Hierarchy cycle detection

| Function | Description |
|----------|-------------|
| `TryGetParentByNodeName(...)` | Searches all four node vectors for a node with the given name and returns its parent string. Used to walk parent chains across node types. |
| `WouldCreateHierarchyCycle(...)` | Cross-type cycle check. Walks up from `candidateParentName` through all node types, checking if `childName` would be encountered. Uses a 512-iteration guard. |
| `StaticMeshCreatesCycle(vec, childIdx, parentIdx)` | Same-type cycle check for StaticMesh nodes (256-iteration guard). |
| `Node3DCreatesCycle(vec, childIdx, parentIdx)` | Same-type cycle check for Node3D nodes (256-iteration guard). |

### World transform computation

| Function | Description |
|----------|-------------|
| `GetStaticMeshWorldTRS(...)` | Computes world position, rotation, and scale for a StaticMesh3DNode by walking its parent chain. |
| `GetNode3DWorldTRS(...)` | Computes world position, Euler rotation, and scale for a Node3DNode by walking its parent chain using quaternion math internally. |
| `GetNode3DWorldTRSQuat(...)` | Same as above but outputs world rotation as a quaternion instead of Euler angles, avoiding gimbal lock. |

### Material slot helpers

| Function | Description |
|----------|-------------|
| `GetStaticMeshPrimaryMaterial(n)` | Returns the effective primary material path (active slot -> slot 0 -> legacy field). |
| `GetStaticMeshMaterialByIndex(n, idx)` | Returns the material path at a specific slot index. |
| `GetStaticMeshSlotLabel(n, idx, projectDir)` | Derives a display label for a material slot from naming conventions or the `.nebslots` manifest. |
| `AutoAssignMaterialSlotsFromMesh(n)` | Syncs the legacy `material` field with `materialSlots[0]`. |

---

## 9. Hierarchy system

Nebula uses a **string-based parent chain** for its scene hierarchy. Each node stores the name of its parent as a plain string in the `parent` field. An empty string means the node is at the root of the scene.

### Cross-type parenting

Parenting is not restricted to same-type nodes. A StaticMesh3DNode can be parented to a Node3DNode, or vice versa. The world transform functions handle this by searching across node type vectors when walking the parent chain:

- `GetStaticMeshWorldTRS` first looks for a parent in `staticMeshNodes`, then in `node3DNodes`.
- `GetNode3DWorldTRS` first looks for a parent in `node3DNodes`, then in `staticMeshNodes`.

This means a StaticMesh can inherit its position from a Node3D (e.g., a weapon mesh parented to a character node), and a Node3D can inherit from a StaticMesh (e.g., a collision volume parented to a platform mesh).

### Cycle detection

Because parents are stored as strings and can reference any node type, cycles must be detected when the user assigns a new parent. The engine provides two levels of cycle detection:

1. **Same-type checks** (`StaticMeshCreatesCycle`, `Node3DCreatesCycle`) -- fast, single-vector traversal with a 256-iteration guard.
2. **Cross-type check** (`WouldCreateHierarchyCycle`) -- walks through all four node type vectors with a 512-iteration guard. This is the general-purpose check used by the editor UI when assigning parents.

The iteration guards prevent infinite loops in the (unlikely) event that corrupted data creates an actual cycle in the parent chain.

---

## 10. World transform computation

### StaticMesh3DNode world transform

`GetStaticMeshWorldTRS` accumulates the world transform by walking up the parent chain:

- **Position:** Additive. Each ancestor's position is added to the child's.
- **Rotation:** Additive for StaticMesh parents (`orx += parent.rotX`). When a Node3D parent is encountered, the parent's rotation **replaces** the accumulated rotation entirely (the child's local rotation is applied separately during rendering).
- **Scale:** Multiplicative. Each ancestor's scale multiplies the child's.

The walk terminates when a parent name is empty, not found in either vector, or the 256-iteration guard is reached.

### Node3DNode world transform

`GetNode3DWorldTRS` and `GetNode3DWorldTRSQuat` use quaternion math internally to avoid gimbal lock:

1. Start with the node's own quaternion (`qw`, `qx`, `qy`, `qz`).
2. Walk up the parent chain. For each parent:
   - If it is a Node3DNode, compose quaternions: `q = parent_q * q`.
   - If it is a StaticMesh3DNode, convert the parent's Euler angles to a quaternion first, then compose.
3. Position is accumulated additively, scale multiplicatively (same as StaticMesh).
4. `GetNode3DWorldTRS` converts the final quaternion back to Euler angles for the output. `GetNode3DWorldTRSQuat` outputs the quaternion directly.

The quaternion approach is essential for Node3D because these nodes are used for characters that align to terrain slopes. Euler angle accumulation through a parent chain would produce incorrect results at extreme orientations due to gimbal lock.

### Internal quaternion helpers

`Node3DNode.cpp` contains an anonymous namespace with quaternion utilities used by the world transform functions:

- `Q4Multiply(a, b)` -- Hamilton product of two quaternions.
- `Q4FromAxisAngle(ax, ay, az, angleDeg)` -- Constructs a quaternion from an axis and angle in degrees.
- `EulerToQ4(rotX, rotY, rotZ)` -- Converts Euler angles to a quaternion using the `Rz * Ry * Rx` convention.
- `Q4ToEuler(q, rotX, rotY, rotZ)` -- Extracts Euler angles from a quaternion, with clamping to avoid singularities near the poles.

## See Also

- [Scene System](Scene_System.md) -- scene serialization and multi-scene management
- [Camera System](Camera_System.md) -- camera nodes, projection, and viewport navigation
- [Mesh and Materials](../assets/Mesh_and_Materials.md) -- mesh import, material slots, and textures
- [Physics and Collision](../gameplay/Physics_and_Collision.md) -- runtime collision and physics system
- [Navigation](../gameplay/Navigation.md) -- navmesh building and pathfinding
