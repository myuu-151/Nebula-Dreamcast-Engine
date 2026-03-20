// ---------------------------------------------------------------------------
// collision.cpp — AABB collision detection and resolution.
// Wall collision against StaticMesh3D triangles, ground snap, slope
// alignment, and Node3D-vs-Node3D overlap.
// ---------------------------------------------------------------------------

#include "collision.h"
#include "runtime_bridge.h"
#include "io/mesh_io.h"
#include "math/math_utils.h"
#include "nodes/NodeTypes.h"
#include "editor/project.h"

#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <filesystem>

#include "../editor/editor_state.h"

extern void GetStaticMeshWorldTRS(int idx, float& ox, float& oy, float& oz,
                                  float& orx, float& ory, float& orz,
                                  float& osx, float& osy, float& osz);
extern void GetNode3DWorldTRS(int idx, float& ox, float& oy, float& oz,
                              float& orx, float& ory, float& orz,
                              float& osx, float& osy, float& osz);

// ---------------------------------------------------------------------------
// WallCollideAABB — horizontal AABB push-out against wall triangles.
// ---------------------------------------------------------------------------
void WallCollideAABB(float cx, float cy, float cz,
                     float hx, float hy, float hz,
                     float* outPushX, float* outPushZ)
{
    *outPushX = 0.0f;
    *outPushZ = 0.0f;
    for (int si = 0; si < (int)gStaticMeshNodes.size(); ++si)
    {
        const auto& s = gStaticMeshNodes[si];
        if (s.mesh.empty() || gProjectDir.empty()) continue;
        if (!s.collisionSource) continue;
        float wt = s.wallThreshold;

        std::filesystem::path meshPath = std::filesystem::path(gProjectDir) / s.mesh;
        const NebMesh* mesh = GetNebMesh(meshPath);
        if (!mesh || !mesh->valid || mesh->positions.empty() || mesh->indices.empty()) continue;

        float wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz;
        GetStaticMeshWorldTRS(si, wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz);

        Vec3 right, up, forward;
        bool hasN3DParent = !s.parent.empty() && FindNode3DByName(s.parent) >= 0;
        if (hasN3DParent)
            GetLocalAxesFromEuler(wrx, wry, wrz, right, up, forward);
        else
            GetLocalAxesFromEuler(wrz, wrx, wry, right, up, forward);

        for (size_t t = 0; t + 2 < mesh->indices.size(); t += 3)
        {
            const auto& p0 = mesh->positions[mesh->indices[t]];
            const auto& p1 = mesh->positions[mesh->indices[t + 1]];
            const auto& p2 = mesh->positions[mesh->indices[t + 2]];
            float s0x = p0.x * wsx, s0y = p0.y * wsy, s0z = p0.z * wsz;
            float s1x = p1.x * wsx, s1y = p1.y * wsy, s1z = p1.z * wsz;
            float s2x = p2.x * wsx, s2y = p2.y * wsy, s2z = p2.z * wsz;
            float ax = wx + right.x * s0x + up.x * s0y + forward.x * s0z;
            float ay = wy + right.y * s0x + up.y * s0y + forward.y * s0z;
            float az = wz + right.z * s0x + up.z * s0y + forward.z * s0z;
            float bx = wx + right.x * s1x + up.x * s1y + forward.x * s1z;
            float by = wy + right.y * s1x + up.y * s1y + forward.y * s1z;
            float bz = wz + right.z * s1x + up.z * s1y + forward.z * s1z;
            float tx = wx + right.x * s2x + up.x * s2y + forward.x * s2z;
            float ty = wy + right.y * s2x + up.y * s2y + forward.y * s2z;
            float tz = wz + right.z * s2x + up.z * s2y + forward.z * s2z;

            float ex1 = bx - ax, ey1 = by - ay, ez1 = bz - az;
            float ex2 = tx - ax, ey2 = ty - ay, ez2 = tz - az;
            float fnx = ey1 * ez2 - ez1 * ey2;
            float fny = ez1 * ex2 - ex1 * ez2;
            float fnz = ex1 * ey2 - ey1 * ex2;
            float nl = sqrtf(fnx * fnx + fny * fny + fnz * fnz);
            if (nl < 1e-8f) continue;
            float nInv = 1.0f / nl;
            fnx *= nInv; fny *= nInv; fnz *= nInv;

            if (fny > wt || fny < -wt) continue;

            float triMinY = ay < by ? (ay < ty ? ay : ty) : (by < ty ? by : ty);
            float triMaxY = ay > by ? (ay > ty ? ay : ty) : (by > ty ? by : ty);
            if (cy - hy >= triMaxY || cy + hy <= triMinY) continue;

            float triMinX = ax < bx ? (ax < tx ? ax : tx) : (bx < tx ? bx : tx);
            float triMaxX = ax > bx ? (ax > tx ? ax : tx) : (bx > tx ? bx : tx);
            float triMinZ = az < bz ? (az < tz ? az : tz) : (bz < tz ? bz : tz);
            float triMaxZ = az > bz ? (az > tz ? az : tz) : (bz > tz ? bz : tz);
            if (cx + hx < triMinX || cx - hx > triMaxX) continue;
            if (cz + hz < triMinZ || cz - hz > triMaxZ) continue;

            float d = fnx * (cx - ax) + fny * (cy - ay) + fnz * (cz - az);
            float projDist = hx * fabsf(fnx) + hy * fabsf(fny) + hz * fabsf(fnz);
            float pen = projDist - fabsf(d);
            if (pen <= 0.0f) continue;

            float hnx = fnx, hnz = fnz;
            float hLen = sqrtf(hnx * hnx + hnz * hnz);
            if (hLen < 1e-6f) continue;
            hnx /= hLen;
            hnz /= hLen;

            float sign = (d >= 0.0f) ? 1.0f : -1.0f;
            *outPushX += sign * hnx * pen;
            *outPushZ += sign * hnz * pen;
        }
    }
}

// ---------------------------------------------------------------------------
// ResolveNodeCollision — ground snap, slope alignment, wall push-out for
// a single Node3D node.
// ---------------------------------------------------------------------------
void ResolveNodeCollision(int nodeIndex, float dt)
{
    auto& n3 = gNode3DNodes[nodeIndex];

    float pwx, pwy, pwz, pwrx, pwry, pwrz, pwsx, pwsy, pwsz;
    GetNode3DWorldTRS(nodeIndex, pwx, pwy, pwz, pwrx, pwry, pwrz, pwsx, pwsy, pwsz);
    float hy = std::max(0.0f, n3.extentY * pwsy);
    float castY = pwy + n3.boundPosY - hy + 0.5f;
    float hitY = 0.0f;
    float hitNormal[3] = {0.0f, 1.0f, 0.0f};
    bool groundHit = NB_RT_RaycastDownWithNormal(pwx + n3.boundPosX, castY, pwz + n3.boundPosZ, &hitY, hitNormal);

    if (groundHit)
    {
        // Ground snap
        float groundY = hitY - n3.boundPosY + hy;
        if (n3.y <= groundY)
        {
            n3.y = groundY;
            if (n3.velY < 0.0f) n3.velY = 0.0f;
        }

        // Slope alignment (collisionSource only, not simpleCollision)
        if (n3.collisionSource)
        {
            float hnx = hitNormal[0], hny = hitNormal[1], hnz = hitNormal[2];
            float savedYaw = n3.rotY;

            float curUpX = 2.0f * (n3.qx * n3.qy - n3.qw * n3.qz);
            float curUpY = 1.0f - 2.0f * (n3.qx * n3.qx + n3.qz * n3.qz);
            float curUpZ = 2.0f * (n3.qy * n3.qz + n3.qw * n3.qx);

            float t = 1.0f - powf(0.0001f, dt);
            float targetNX = (hny > 0.0f) ? hnx : 0.0f;
            float targetNY = (hny > 0.0f) ? hny : 1.0f;
            float targetNZ = (hny > 0.0f) ? hnz : 0.0f;
            float smX = curUpX + (targetNX - curUpX) * t;
            float smY = curUpY + (targetNY - curUpY) * t;
            float smZ = curUpZ + (targetNZ - curUpZ) * t;
            float smLen = sqrtf(smX*smX + smY*smY + smZ*smZ);
            if (smLen > 0.0001f) { smX /= smLen; smY /= smLen; smZ /= smLen; }

            Quat result = QuatFromNormalAndYaw(smX, smY, smZ, savedYaw);
            n3.qw = result.w; n3.qx = result.x; n3.qy = result.y; n3.qz = result.z;
            SyncNode3DEulerFromQuat(n3);
            n3.rotY = savedYaw;
        }
    }
    else if (n3.collisionSource)
    {
        // No ground hit — smooth tilt back to upright
        float savedYaw = n3.rotY;
        float curUpX = 2.0f * (n3.qx * n3.qy - n3.qw * n3.qz);
        float curUpY = 1.0f - 2.0f * (n3.qx * n3.qx + n3.qz * n3.qz);
        float curUpZ = 2.0f * (n3.qy * n3.qz + n3.qw * n3.qx);
        float t = 1.0f - powf(0.0001f, dt);
        float smX = curUpX + (0.0f - curUpX) * t;
        float smY = curUpY + (1.0f - curUpY) * t;
        float smZ = curUpZ + (0.0f - curUpZ) * t;
        float smLen = sqrtf(smX*smX + smY*smY + smZ*smZ);
        if (smLen > 0.0001f) { smX /= smLen; smY /= smLen; smZ /= smLen; }
        Quat result = QuatFromNormalAndYaw(smX, smY, smZ, savedYaw);
        n3.qw = result.w; n3.qx = result.x; n3.qy = result.y; n3.qz = result.z;
        SyncNode3DEulerFromQuat(n3);
        n3.rotY = savedYaw;
    }

    // Wall collision (horizontal push-out)
    {
        float wcx = pwx + n3.boundPosX;
        float wcy = pwy + n3.boundPosY;
        float wcz = pwz + n3.boundPosZ;
        float whx = std::max(0.01f, n3.extentX * pwsx);
        float why = std::max(0.01f, n3.extentY * pwsy);
        float whz = std::max(0.01f, n3.extentZ * pwsz);
        float pushX = 0.0f, pushZ = 0.0f;
        WallCollideAABB(wcx, wcy, wcz, whx, why, whz, &pushX, &pushZ);
        if (pushX != 0.0f || pushZ != 0.0f)
        {
            n3.x += pushX;
            n3.z += pushZ;
        }
    }
}

// ---------------------------------------------------------------------------
// ResolveNode3DOverlaps — push apart overlapping Node3D AABB pairs.
// ---------------------------------------------------------------------------
void ResolveNode3DOverlaps()
{
    for (int ni = 0; ni < (int)gNode3DNodes.size(); ++ni)
    {
        auto& a = gNode3DNodes[ni];
        if (!a.collisionSource && !a.simpleCollision) continue;
        for (int nj = ni + 1; nj < (int)gNode3DNodes.size(); ++nj)
        {
            auto& b = gNode3DNodes[nj];
            if (!b.collisionSource && !b.simpleCollision) continue;

            float acx = a.x + a.boundPosX, acy = a.y + a.boundPosY, acz = a.z + a.boundPosZ;
            float bcx = b.x + b.boundPosX, bcy = b.y + b.boundPosY, bcz = b.z + b.boundPosZ;

            float overX = (a.extentX + b.extentX) - fabsf(acx - bcx);
            if (overX <= 0.0f) continue;
            float overY = (a.extentY + b.extentY) - fabsf(acy - bcy);
            if (overY <= 0.0f) continue;
            float overZ = (a.extentZ + b.extentZ) - fabsf(acz - bcz);
            if (overZ <= 0.0f) continue;

            float minPen = overX;
            int axis = 0;
            if (overZ < minPen) { minPen = overZ; axis = 1; }
            if (overY < minPen) continue;

            float half = minPen * 0.5f;
            if (axis == 0)
            {
                float dir = (acx >= bcx) ? 1.0f : -1.0f;
                a.x += dir * half;
                b.x -= dir * half;
            }
            else
            {
                float dir = (acz >= bcz) ? 1.0f : -1.0f;
                a.z += dir * half;
                b.z -= dir * half;
            }
        }
    }
}
