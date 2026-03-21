# Navigation Mesh System

This document explains how navigation meshes work in the Nebula Dreamcast Engine, from building them in the editor to querying paths at runtime on the Dreamcast.

## Overview

The navigation system uses two libraries from the Recast Navigation project (vendored in `thirdparty/recastnavigation/`):

- **Recast** generates a navigation mesh (navmesh) offline from world geometry. It voxelizes triangles into a heightfield, extracts walkable regions, and produces a polygon mesh that represents where an agent can walk.
- **Detour** loads the finished navmesh and answers pathfinding queries at runtime: "find a path from A to B," "pick a random navigable point," or "snap this position to the nearest walkable surface."

The engine supports **one navmesh per scene**. The editor holds a single global navmesh in memory at any time. When you package for Dreamcast, each scene gets its own binary navmesh file so the runtime can load the correct one on scene switch.

## Source Files

| File | Role |
|------|------|
| `src/navmesh/NavMeshBuilder.h` | NavMeshParams struct, build/query/serialization API declarations |
| `src/navmesh/NavMeshBuilder.cpp` | Recast build pipeline, Detour query wrappers, binary serialization |
| `src/nodes/NavMesh3DNode.h` | NavMesh3DNode struct (bounds/negator volume definition) |
| `src/runtime/runtime_bridge.cpp` | NB_RT_NavMesh* bridge functions (editor-side implementations) |
| `src/platform/dreamcast/DetourBridge.h` | C-callable Detour wrapper declarations for Dreamcast |
| `src/platform/dreamcast/DetourBridge.cpp` | Dreamcast Detour implementation (reduced memory, xorshift PRNG) |
| `src/platform/dreamcast/dc_codegen.cpp` | Dreamcast packaging: per-scene navmesh build and NAV*.BIN export |

## NavMeshParams

The `NavMeshParams` struct in `src/navmesh/NavMeshBuilder.h` controls how Recast voxelizes and simplifies geometry. All parameters have defaults tuned for typical game-scale scenes:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `cellSize` | 0.15 | Horizontal voxel size (XZ). Smaller values produce more accurate navmeshes but increase build time and memory. |
| `cellHeight` | 0.1 | Vertical voxel size (Y). Controls height precision of the heightfield. |
| `walkableSlopeDeg` | 89.0 | Maximum slope angle (degrees) that counts as walkable. Set near 90 to capture all geometry including steep seams between connected surfaces. |
| `walkableHeight` | 2.0 | Minimum ceiling clearance for the agent, in world units. Converted to voxel units internally via `ceil(walkableHeight / cellHeight)`. |
| `walkableClimb` | 2.0 | Maximum step height the agent can climb. High values bridge height gaps at seams between connected surfaces. Converted via `floor(walkableClimb / cellHeight)`. |
| `walkableRadius` | 0.0 | Agent radius for eroding the navmesh edges. Zero means the navmesh extends to the exact geometry edges; agent radius is handled at query time instead. Converted via `ceil(walkableRadius / cellSize)`. |
| `maxEdgeLen` | 12 | Maximum edge length along the border of the navmesh, in world units. Converted via `maxEdgeLen / cellSize`. |
| `maxSimplError` | 0.5 | Maximum distance the simplified contour border can deviate from the original. Low values preserve more detail at edge seams. |
| `minRegionArea` | 4 | Minimum number of cells in a region. Small values allow tiny walkable patches to survive. |
| `mergeRegionArea` | 400 | Regions smaller than this will be merged with neighbors if possible. Equivalent to `rcSqr(20)`. |
| `maxVertsPerPoly` | 6 | Maximum vertices per polygon in the output navmesh. 6 is the Detour maximum. |
| `detailSampleDist` | 6.0 | Detail mesh sampling distance for height accuracy. |
| `detailSampleMaxErr` | 1.0 | Maximum vertical error allowed in the detail mesh. |

## Build Pipeline

The function `NavMeshBuild()` in `src/navmesh/NavMeshBuilder.cpp` takes packed float vertex and int index arrays and runs the full Recast pipeline. Here is the sequence of operations:

### 1. Bounds Computation

`rcCalcBounds()` scans the input vertices to find the axis-aligned bounding box. This determines the grid dimensions for the heightfield.

### 2. Heightfield Creation

`rcCreateHeightfield()` allocates a 2D grid of voxel columns. The grid resolution is calculated from the bounding box and `cellSize`:

```
width  = ceil((bmax[0] - bmin[0]) / cellSize)
height = ceil((bmax[2] - bmin[2]) / cellSize)
```

### 3. Triangle Rasterization

First, `rcMarkWalkableTriangles()` classifies each input triangle by its slope. Triangles steeper than `walkableSlopeDeg` are marked non-walkable.

**Auto winding flip:** If zero triangles are marked walkable after the initial pass, the builder assumes the winding order is inverted. It swaps the first two indices of every triangle and retries. This handles meshes exported with flipped normals without requiring manual correction.

Then `rcRasterizeTriangles()` voxelizes the triangles into the heightfield.

### 4. Obstacle Filtering

Three filters clean up the heightfield:

- `rcFilterLowHangingWalkableObstacles()` -- removes walkable spans that hang below walkable surfaces (e.g., bottom faces of platforms).
- `rcFilterLedgeSpans()` -- removes spans at ledge edges where the drop exceeds `walkableClimb`.
- `rcFilterWalkableLowHeightSpans()` -- removes spans where ceiling clearance is less than `walkableHeight`.

### 5. Compact Heightfield

`rcBuildCompactHeightfield()` converts the heightfield into a more efficient representation for region building. After compaction, `rcErodeWalkableArea()` shrinks walkable areas inward by `walkableRadius` (no erosion when radius is zero).

### 6. Distance Field and Regions

`rcBuildDistanceField()` computes the distance from each cell to the nearest border. `rcBuildRegions()` uses watershed partitioning to group cells into contiguous regions, respecting `minRegionArea` and `mergeRegionArea`.

### 7. Contours

`rcBuildContours()` traces the borders of each region into simplified polygon outlines, controlled by `maxSimplificationError` and `maxEdgeLen`.

### 8. Polygon Mesh

`rcBuildPolyMesh()` tessellates the contours into convex polygons with at most `maxVertsPerPoly` vertices. All polygons are flagged as walkable (flag = 1, area = 0).

### 9. Detail Mesh

`rcBuildPolyMeshDetail()` adds height detail to each polygon so the navmesh surface more closely follows the original geometry, controlled by `detailSampleDist` and `detailSampleMaxError`.

### 10. Detour NavMesh Creation

`dtCreateNavMeshData()` packs the polygon mesh and detail mesh into Detour's binary format with a BVH tree for fast spatial queries. The resulting `dtNavMesh` is initialized with `DT_TILE_FREE_DATA` (Detour takes ownership of the data buffer). A `dtNavMeshQuery` is allocated with a node pool of 2048 for editor-side queries.

## Query Functions

All query functions require a built navmesh (`NavMeshIsReady()` returns true). They use a default `dtQueryFilter` that includes all polygon flags and excludes none.

### Query Extents

All nearest-poly lookups use search extents of **(2.0, 4.0, 2.0)** in X, Y, Z respectively. This means the search volume extends 2 units horizontally and 4 units vertically from the query point. If the target point is farther than these extents from any navmesh polygon, the query will fail.

### NavMeshFindPath

```cpp
bool NavMeshFindPath(const Vec3& start, const Vec3& goal, std::vector<Vec3>& outPath);
```

Finds a path between two world-space points. Internally this is a two-phase process:

1. **Polygon corridor** -- `findNearestPoly()` locates the start and goal polygons, then `findPath()` finds a corridor of up to 2048 connected polygons between them.
2. **Straight path extraction** -- `findStraightPath()` converts the polygon corridor into a sequence of waypoint positions (Vec3), using `DT_STRAIGHTPATH_ALL_CROSSINGS` to include all portal crossings.

Returns true if waypoints were found; the output vector contains the ordered waypoints.

### NavMeshFindRandomPoint

```cpp
bool NavMeshFindRandomPoint(Vec3& outPoint);
```

Returns a uniformly distributed random navigable point on the navmesh. Uses `rand()` / `RAND_MAX` as the random number source on the editor side. Useful for AI patrol targets or spawn point selection.

### NavMeshFindClosestPoint

```cpp
bool NavMeshFindClosestPoint(const Vec3& pos, Vec3& outPoint);
```

Projects a world-space position onto the nearest navmesh surface. Uses `findNearestPoly()` with the standard query extents. Useful for snapping an agent back onto the navmesh after being displaced.

## Serialization

The navmesh binary format is simply the raw Detour tile data, with no additional header or wrapper.

### NavMeshSaveBinary

```cpp
bool NavMeshSaveBinary(std::vector<uint8_t>& outBlob);
```

Extracts the tile data from the single-tile navmesh at position (0, 0, 0). The output blob is a direct copy of `tile->data` with size `tile->dataSize`. This is the format written to `NAV*.BIN` files for Dreamcast.

### NavMeshLoadBinary

```cpp
bool NavMeshLoadBinary(const uint8_t* data, int dataSize);
```

Reconstructs a navmesh from a previously saved binary blob. The data is copied into a `dtAlloc`'d buffer because `dtNavMesh::init()` takes ownership of the pointer when initialized with `DT_TILE_FREE_DATA`. After loading, a `dtNavMeshQuery` is allocated with a 2048-node pool and all query functions become available.

## NavMesh3DNode

Defined in `src/nodes/NavMesh3DNode.h`, this node type acts as a spatial volume that controls which geometry participates in navmesh generation. It does not contain mesh data itself -- it is an axis-aligned bounding box that selects or excludes triangles from nearby StaticMesh3D nodes.

### Fields

| Field | Default | Description |
|-------|---------|-------------|
| `extentX/Y/Z` | 10.0 | Full size of the bounding box (not half-extents). The volume spans from `position - extent/2` to `position + extent/2`. |
| `navBounds` | true | When true, this volume defines a region where geometry should be included in the navmesh. |
| `navNegator` | false | When true, this volume carves out (excludes) geometry from the navmesh. Negators take priority over bounds. |
| `cullWalls` | false | Enables wall culling within this volume. |
| `wallCullThreshold` | 0.2 | Threshold for wall culling classification. |
| `wireR/G/B` | 0.1, 1.0, 0.25 | Wireframe display color in the editor viewport. |
| `wireThickness` | 1.0 | Wireframe line thickness in the editor viewport. |

### How Bounds and Negators Interact

When `NB_RT_NavMeshBuild()` runs, it collects all NavMesh3D nodes in the current scene. For each input triangle, it tests whether at least one vertex falls inside any `navBounds` volume AND does not fall inside any `navNegator` volume. This lets you define walkable regions precisely:

- Place a `navBounds` volume over a large area to include its floor geometry.
- Place a `navNegator` inside it to cut out a hole (for example, around a pit or restricted zone).
- If no NavMesh3D nodes exist in the scene, all StaticMesh3D triangles are included.

## Editor Integration

The editor-side navmesh build is triggered by calling `NB_RT_NavMeshBuild()` from a gameplay script (or internally during Dreamcast packaging). The implementation lives in `src/runtime/runtime_bridge.cpp`.

### Triangle Collection Process

1. **Collect nav volumes** -- All NavMesh3DNode entries in the current scene are converted to axis-aligned min/max bounding boxes, tagged as bounds or negator.

2. **Iterate StaticMesh3D nodes** -- For each StaticMesh3D in the scene, the builder opens the `.nebmesh` file and reads vertex positions (fixed-point s16 big-endian) and indices (u16 big-endian).

3. **World-space transform** -- Each vertex is transformed to world space using the StaticMesh3D's position, rotation, and scale. The rotation axis mapping depends on whether the mesh has a Node3D parent (standard axes) or is standalone (axis remap: X from Z, Y from X, Z from Y to match rendering conventions).

4. **Bounds clipping** -- Each triangle is tested against the nav volumes. A triangle is included if at least one of its three vertices passes the bounds/negator test. If no nav volumes exist, all triangles are included.

5. **Build** -- The collected vertices and indices are passed to `NavMeshBuild()`.

Note that the editor build collects triangles from **all** StaticMesh3D nodes in the scene, not just those marked as `collisionSource`. The `collisionSource` flag is used by the physics/collision system, not the navmesh builder.

### Script API

Gameplay scripts can call these bridge functions on both editor and Dreamcast:

```c
int  NB_RT_NavMeshBuild(void);                                           // Build from scene geometry
void NB_RT_NavMeshClear(void);                                           // Free current navmesh
int  NB_RT_NavMeshIsReady(void);                                         // Check if navmesh exists
int  NB_RT_NavMeshFindPath(float sx, float sy, float sz,
                           float gx, float gy, float gz,
                           float* outPath, int maxPoints);               // Returns waypoint count
int  NB_RT_NavMeshFindRandomPoint(float outPos[3]);                      // Random navigable point
int  NB_RT_NavMeshFindClosestPoint(float px, float py, float pz,
                                   float outPos[3]);                     // Nearest surface point
```

## Dreamcast Runtime

On Dreamcast, the navmesh is not built at runtime. Instead, the editor builds it during packaging and writes it to disc as a binary file. The runtime loads and queries it through a C-callable wrapper.

### DetourBridge

`src/platform/dreamcast/DetourBridge.h` and `DetourBridge.cpp` provide the Dreamcast-side Detour wrapper. The API mirrors the editor's `NB_RT_NavMesh*` functions but is designed for the Dreamcast's constraints:

| Function | Description |
|----------|-------------|
| `NB_DC_DetourInit(navData, size)` | Load navmesh from binary blob. Copies data via `dtAlloc` (Detour takes ownership). Returns 1 on success. |
| `NB_DC_DetourFree()` | Tear down navmesh and query objects. |
| `NB_DC_DetourIsReady()` | Returns 1 if initialized. |
| `NB_DC_DetourFindPath(sx,sy,sz, gx,gy,gz, outPath, maxPoints)` | Find path, write packed xyz waypoints into outPath. Returns waypoint count. |
| `NB_DC_DetourFindRandomPoint(outPos)` | Pick random navigable point. Returns 1 on success. |
| `NB_DC_DetourFindClosestPoint(px,py,pz, outPos)` | Snap to nearest navmesh surface. Returns 1 on success. |

### Memory Constraints

The Dreamcast has 16 MB of total RAM, so DetourBridge uses reduced pool sizes:

- **Node pool:** 512 (down from the editor's 2048). This limits the complexity of paths that can be computed in a single query.
- **Polygon path cap:** 256 (down from the editor's 2048). This is the maximum number of polygons in a path corridor.

Both the path corridor and straight path extraction share the 256-poly limit, so output waypoint arrays are bounded to 256 entries.

### PRNG

The `<random>` header is not available on KOS (the Dreamcast OS), so `DetourBridge.cpp` implements a **xorshift32** PRNG for `NB_DC_DetourFindRandomPoint`:

```cpp
static unsigned int sRandState = 1;
static float DetourRandFloat(void)
{
    sRandState ^= sRandState << 13;
    sRandState ^= sRandState >> 17;
    sRandState ^= sRandState << 5;
    return (float)(sRandState & 0x7FFFFFFF) / (float)0x7FFFFFFF;
}
```

This produces a deterministic sequence seeded at 1. The same seed is used across all calls within a session.

### Disc Layout

During Dreamcast packaging (`src/platform/dreamcast/dc_codegen.cpp`), the engine builds one navmesh per scene. Each scene's navmesh is serialized and written to:

```
build_dreamcast/cd_root/data/navmesh/NAVxxxxx.BIN
```

Where `xxxxx` is the 1-based scene index, zero-padded to 5 digits. For example, scene 1 produces `NAV00001.BIN`, scene 2 produces `NAV00002.BIN`, and so on. At runtime, `KosBindings.c` loads the appropriate BIN file when a scene is activated, then passes the raw bytes to `NB_DC_DetourInit()`.

## Typical Workflow

1. **Place geometry.** Add StaticMesh3D nodes to your scene with the floor, ramps, and platforms your agent will walk on.

2. **Define walkable regions.** Add one or more NavMesh3D nodes with `navBounds = true`. Size and position their extents to cover the areas where you want navigation. Optionally add `navNegator` volumes to carve out holes.

3. **Build the navmesh.** Call `NB_RT_NavMeshBuild()` from a script, or let the Dreamcast packaging step build it automatically. The editor console prints diagnostic output showing grid size, walkable triangle counts, region counts, and polygon counts.

4. **Query paths.** Use `NB_RT_NavMeshFindPath()` to get a series of waypoints from start to goal. Move your agent along the waypoints each frame.

5. **Package for Dreamcast.** The packaging step in `dc_codegen.cpp` builds a navmesh for each scene and writes the BIN files to the disc image. No manual export step is needed.

## See Also

- [Node Types](../scene-and-nodes/Node_Types.md) -- NavMesh3DNode volume definitions
- [Physics and Collision](Physics_and_Collision.md) -- runtime collision and ground snap system
- [Scripting](../getting-started/Scripting.md) -- NB_RT_NavMesh* bridge functions
- [Dreamcast Export](../dreamcast/Dreamcast_Export.md) -- per-scene navmesh binary export
