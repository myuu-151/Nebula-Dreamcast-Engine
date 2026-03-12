#pragma once

#include <cstdint>
#include <vector>
#include "../editor/math_types.h"

// ---------------------------------------------------------------------------
// NavMeshBuilder — builds and queries a Recast/Detour navigation mesh.
//
// Usage from the editor (main.cpp):
//   1. Gather world-space triangles from collision-flagged StaticMesh3DNodes
//   2. Call NavMeshBuild() with the vertex/index arrays
//   3. Query with NavMeshFindPath / NavMeshFindRandomPoint / NavMeshFindClosestPoint
//   4. Call NavMeshClear() to free the current navmesh
//
// The module holds a single global navmesh at a time (one scene = one navmesh).
// ---------------------------------------------------------------------------

// Recast build parameters — tweak these for your game scale.
struct NavMeshParams
{
    float cellSize          = 0.3f;
    float cellHeight        = 0.2f;
    float walkableSlopeDeg  = 55.0f;
    float walkableHeight    = 2.0f;   // agent height
    float walkableClimb     = 0.9f;   // max step-up
    float walkableRadius    = 0.4f;   // agent radius
    float maxEdgeLen        = 12.0f;
    float maxSimplError     = 1.3f;
    int   minRegionArea     = 64;     // rcSqr(8)
    int   mergeRegionArea   = 400;    // rcSqr(20)
    int   maxVertsPerPoly   = 6;
    float detailSampleDist  = 6.0f;
    float detailSampleMaxErr= 1.0f;
};

// Build navmesh from world-space triangles.
// verts:   packed float array [x0,y0,z0, x1,y1,z1, ...] — (count / 3) vertices
// tris:    packed int array [i0,i1,i2, ...] — (count / 3) triangles indexing into verts
// Returns true on success.
bool NavMeshBuild(const float* verts, int vertCount,
                  const int* tris, int triCount,
                  const NavMeshParams& params = NavMeshParams{});

// Free the current navmesh.
void NavMeshClear();

// Is a navmesh currently loaded?
bool NavMeshIsReady();

// Find a path between two world-space points.
// Returns true if a path was found; outPath receives the waypoints.
bool NavMeshFindPath(const Vec3& start, const Vec3& goal,
                     std::vector<Vec3>& outPath);

// Return a random navigable point on the navmesh.
bool NavMeshFindRandomPoint(Vec3& outPoint);

// Project a world-space position onto the nearest navmesh surface.
bool NavMeshFindClosestPoint(const Vec3& pos, Vec3& outPoint);

// Serialize the built Detour navmesh to a binary blob (for Dreamcast packaging).
// Returns true on success. The blob can be loaded at runtime with NavMeshLoadBinary.
bool NavMeshSaveBinary(std::vector<uint8_t>& outBlob);

// Load a previously serialized navmesh binary blob.
bool NavMeshLoadBinary(const uint8_t* data, int dataSize);
