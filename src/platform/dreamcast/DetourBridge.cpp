/*
 * DetourBridge.cpp — Detour navmesh query wrapper for Dreamcast (SH4/KOS).
 *
 * Compiled with kos-c++ (sh-elf-g++).  All public functions are extern "C"
 * so the C-based generated runtime (main.c) can call them directly.
 *
 * Memory notes:
 *   - Dreamcast has 16 MB RAM total.  We use a reduced node pool (512) and
 *     path poly cap (256) to keep Detour's working set small.
 *   - The navmesh binary is already loaded by KosBindings.c (NB_DC_LoadNavMesh).
 *     We duplicate it here because dtNavMesh takes ownership of the buffer.
 */

#include "DetourBridge.h"

#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "DetourCommon.h"
#include "DetourStatus.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---- internal state ---- */
static dtNavMesh*      sNavMesh  = nullptr;
static dtNavMeshQuery* sQuery    = nullptr;

static const int kMaxNodePool  = 512;   /* reduced from default 2048 */
static const int kMaxPathPolys = 256;   /* max polys in a single path query */

/* ---- helpers ---- */

/* Simple xorshift32 PRNG (no <random> on KOS). */
static unsigned int sRandState = 1;
static float DetourRandFloat(void)
{
    sRandState ^= sRandState << 13;
    sRandState ^= sRandState >> 17;
    sRandState ^= sRandState << 5;
    return (float)(sRandState & 0x7FFFFFFF) / (float)0x7FFFFFFF;
}

/* ---- public API ---- */

extern "C" int NB_DC_DetourInit(const void* navData, int navDataSize)
{
    NB_DC_DetourFree();

    if (!navData || navDataSize <= 0) return 0;

    /* dtNavMesh::init() takes ownership of the data pointer and will free it
       with dtFree().  We must give it a dtAlloc'd copy. */
    unsigned char* copy = (unsigned char*)dtAlloc(navDataSize, DT_ALLOC_PERM);
    if (!copy) return 0;
    memcpy(copy, navData, (size_t)navDataSize);

    sNavMesh = dtAllocNavMesh();
    if (!sNavMesh) { dtFree(copy); return 0; }

    dtStatus st = sNavMesh->init(copy, navDataSize, DT_TILE_FREE_DATA);
    if (dtStatusFailed(st))
    {
        dtFreeNavMesh(sNavMesh);
        sNavMesh = nullptr;
        return 0;
    }

    sQuery = dtAllocNavMeshQuery();
    if (!sQuery)
    {
        dtFreeNavMesh(sNavMesh);
        sNavMesh = nullptr;
        return 0;
    }

    st = sQuery->init(sNavMesh, kMaxNodePool);
    if (dtStatusFailed(st))
    {
        dtFreeNavMeshQuery(sQuery);
        sQuery = nullptr;
        dtFreeNavMesh(sNavMesh);
        sNavMesh = nullptr;
        return 0;
    }

    return 1;
}

extern "C" void NB_DC_DetourFree(void)
{
    if (sQuery) { dtFreeNavMeshQuery(sQuery); sQuery = nullptr; }
    if (sNavMesh) { dtFreeNavMesh(sNavMesh); sNavMesh = nullptr; }
}

extern "C" int NB_DC_DetourIsReady(void)
{
    return (sNavMesh && sQuery) ? 1 : 0;
}

extern "C" int NB_DC_DetourFindPath(float sx, float sy, float sz,
                                     float gx, float gy, float gz,
                                     float* outPath, int maxPoints)
{
    if (!sQuery || !outPath || maxPoints <= 0) return 0;

    const float halfExt[3] = { 2.0f, 4.0f, 2.0f };
    dtQueryFilter filter;

    float startPos[3] = { sx, sy, sz };
    float goalPos[3]  = { gx, gy, gz };
    dtPolyRef startRef = 0, goalRef = 0;
    float nearStart[3], nearGoal[3];

    sQuery->findNearestPoly(startPos, halfExt, &filter, &startRef, nearStart);
    sQuery->findNearestPoly(goalPos,  halfExt, &filter, &goalRef,  nearGoal);

    if (!startRef || !goalRef) return 0;

    /* Find polygon corridor */
    dtPolyRef polyPath[kMaxPathPolys];
    int polyCount = 0;
    sQuery->findPath(startRef, goalRef, nearStart, nearGoal, &filter,
                     polyPath, &polyCount, kMaxPathPolys);
    if (polyCount <= 0) return 0;

    /* Convert polygon corridor to straight-line waypoints */
    float straightPath[kMaxPathPolys * 3];
    unsigned char straightFlags[kMaxPathPolys];
    dtPolyRef straightPolys[kMaxPathPolys];
    int straightCount = 0;

    sQuery->findStraightPath(nearStart, nearGoal, polyPath, polyCount,
                             straightPath, straightFlags, straightPolys,
                             &straightCount, kMaxPathPolys, 0);

    if (straightCount <= 0) return 0;

    int outCount = straightCount < maxPoints ? straightCount : maxPoints;
    memcpy(outPath, straightPath, (size_t)outCount * 3 * sizeof(float));
    return outCount;
}

extern "C" int NB_DC_DetourFindRandomPoint(float outPos[3])
{
    if (!sQuery) return 0;

    dtQueryFilter filter;
    dtPolyRef randRef = 0;
    float pt[3];

    dtStatus st = sQuery->findRandomPoint(&filter, DetourRandFloat, &randRef, pt);
    if (dtStatusFailed(st) || !randRef) return 0;

    outPos[0] = pt[0];
    outPos[1] = pt[1];
    outPos[2] = pt[2];
    return 1;
}

extern "C" int NB_DC_DetourFindClosestPoint(float px, float py, float pz, float outPos[3])
{
    if (!sQuery) return 0;

    const float halfExt[3] = { 2.0f, 4.0f, 2.0f };
    dtQueryFilter filter;
    dtPolyRef ref = 0;
    float nearest[3];
    float pos[3] = { px, py, pz };

    sQuery->findNearestPoly(pos, halfExt, &filter, &ref, nearest);
    if (!ref) return 0;

    outPos[0] = nearest[0];
    outPos[1] = nearest[1];
    outPos[2] = nearest[2];
    return 1;
}
