#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DetourBridge — C-callable wrapper around Detour navmesh queries for Dreamcast.
 *
 * The editor builds a navmesh binary (NAV00001.BIN) using Recast offline.
 * At runtime on DC, the binary is loaded via NB_DC_LoadNavMesh() and then
 * passed to NB_DC_DetourInit() which creates the dtNavMesh + dtNavMeshQuery
 * objects needed for pathfinding.
 */

/* Initialize Detour from a serialized navmesh blob (the raw .BIN bytes).
 * Returns 1 on success, 0 on failure. */
int NB_DC_DetourInit(const void* navData, int navDataSize);

/* Tear down Detour state (frees dtNavMesh and dtNavMeshQuery). */
void NB_DC_DetourFree(void);

/* Returns 1 if Detour is initialized and ready for queries. */
int NB_DC_DetourIsReady(void);

/* Find a path between two world-space points.
 * Writes up to maxPoints xyz waypoints into outPath (packed: x0,y0,z0, x1,y1,z1, ...).
 * Returns the number of waypoints written, or 0 if no path found. */
int NB_DC_DetourFindPath(float sx, float sy, float sz,
                         float gx, float gy, float gz,
                         float* outPath, int maxPoints);

/* Pick a random navigable point on the navmesh.
 * Writes xyz into outPos. Returns 1 on success, 0 on failure. */
int NB_DC_DetourFindRandomPoint(float outPos[3]);

/* Project a world position onto the nearest navmesh surface.
 * Writes xyz into outPos. Returns 1 on success, 0 on failure. */
int NB_DC_DetourFindClosestPoint(float px, float py, float pz, float outPos[3]);

#ifdef __cplusplus
}
#endif
