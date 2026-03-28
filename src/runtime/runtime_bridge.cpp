// ---------------------------------------------------------------------------
// runtime_bridge.cpp — NB_RT_* bridge function implementations
// Extracted from main.cpp. These are the editor-side implementations of the
// Dreamcast runtime API, exported so gameplay scripts can call them.
// ---------------------------------------------------------------------------

#include "runtime_bridge.h"
#include "io/mesh_io.h"
#include "math/math_types.h"
#include "nodes/NodeTypes.h"
#include "navmesh/NavMeshBuilder.h"
#include "editor/project.h"
#include "math/math_utils.h"

#include <vector>
#include <string>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "editor/editor_state.h"

// ---------------------------------------------------------------------------
// Helper functions defined in main.cpp, linked externally.
// FindNode3DByName, FindCamera3DByName, FindStaticMeshByName are declared
// in runtime_bridge.h.
// ---------------------------------------------------------------------------
extern void GetStaticMeshWorldTRS(int idx, float& ox, float& oy, float& oz,
                                  float& orx, float& ory, float& orz,
                                  float& osx, float& osy, float& osz);
extern void GetCamera3DWorldTR(int idx, float& ox, float& oy, float& oz,
                                float& orx, float& ory, float& orz);
extern bool IsCameraUnderNode3D(const Camera3DNode& cam, const std::string& nodeName);
extern void SetActiveScene(int index);
extern bool LoadSceneFromPath(const std::filesystem::path& path, SceneData& outScene);
extern void NotifyScriptSceneSwitch();

// ===========================================================================
// NB_RT_* implementations
// ===========================================================================

NB_RT_EXPORT void NB_RT_GetNode3DPosition(const char* name, float outPos[3])
{
    if (!outPos)
        return;
    outPos[0] = outPos[1] = outPos[2] = 0.0f;
    if (!name)
        return;
    int idx = FindNode3DByName(name);
    if (idx < 0)
        return;
    const auto& n = gNode3DNodes[idx];
    outPos[0] = n.x;
    outPos[1] = n.y;
    outPos[2] = n.z;
}

NB_RT_EXPORT void NB_RT_SetNode3DPosition(const char* name, float x, float y, float z)
{
    if (!name)
        return;
    int idx = FindNode3DByName(name);
    if (idx < 0)
        return;
    auto& n = gNode3DNodes[idx];
    n.x = x;
    n.y = y;
    n.z = z;
}

NB_RT_EXPORT void NB_RT_GetNode3DRotation(const char* name, float outRot[3])
{
    if (!outRot)
        return;
    outRot[0] = outRot[1] = outRot[2] = 0.0f;
    if (!name)
        return;
    int idx = FindNode3DByName(name);
    if (idx < 0)
        return;
    const auto& n = gNode3DNodes[idx];
    outRot[0] = n.rotX;
    outRot[1] = n.rotY;
    outRot[2] = n.rotZ;
}

NB_RT_EXPORT void NB_RT_SetNode3DRotation(const char* name, float x, float y, float z)
{
    if (!name)
        return;
    int idx = FindNode3DByName(name);
    if (idx < 0)
        return;
    auto& n = gNode3DNodes[idx];
    bool didPhysicsYaw = false;
    if (n.physicsEnabled)
    {
        // Physics owns tilt — only update yaw, preserve current up vector
        float curUpX = 2.0f * (n.qx * n.qy - n.qw * n.qz);
        float curUpY = 1.0f - 2.0f * (n.qx * n.qx + n.qz * n.qz);
        float curUpZ = 2.0f * (n.qy * n.qz + n.qw * n.qx);
        float uLen = sqrtf(curUpX*curUpX + curUpY*curUpY + curUpZ*curUpZ);
        if (uLen > 0.0001f) { curUpX /= uLen; curUpY /= uLen; curUpZ /= uLen; }
        else { curUpX = 0.0f; curUpY = 1.0f; curUpZ = 0.0f; }
        // Build quat from preserved tilt + new yaw
        float yawRad = y * 3.14159265f / 180.0f;
        float fwdX = sinf(yawRad), fwdZ = cosf(yawRad);
        // Project forward onto plane perpendicular to up
        float dot = fwdX * curUpX + fwdZ * curUpZ;
        float pfx = fwdX - dot * curUpX;
        float pfy = -dot * curUpY;
        float pfz = fwdZ - dot * curUpZ;
        float pfLen = sqrtf(pfx*pfx + pfy*pfy + pfz*pfz);
        if (pfLen > 0.0001f)
        {
            pfx /= pfLen; pfy /= pfLen; pfz /= pfLen;
            float rx = curUpY * pfz - curUpZ * pfy;
            float ry = curUpZ * pfx - curUpX * pfz;
            float rz = curUpX * pfy - curUpY * pfx;
            float rLen = sqrtf(rx*rx + ry*ry + rz*rz);
            if (rLen > 0.0001f) { rx /= rLen; ry /= rLen; rz /= rLen; }
            // Matrix [right, up, forward] -> quaternion
            float m00=rx, m01=curUpX, m02=pfx;
            float m10=ry, m11=curUpY, m12=pfy;
            float m20=rz, m21=curUpZ, m22=pfz;
            float trace = m00 + m11 + m22;
            if (trace > 0)
            {
                float s = 0.5f / sqrtf(trace + 1.0f);
                n.qw = 0.25f / s;
                n.qx = (m21 - m12) * s;
                n.qy = (m02 - m20) * s;
                n.qz = (m10 - m01) * s;
            }
            else if (m00 > m11 && m00 > m22)
            {
                float s = 2.0f * sqrtf(1.0f + m00 - m11 - m22);
                n.qw = (m21 - m12) / s;
                n.qx = 0.25f * s;
                n.qy = (m01 + m10) / s;
                n.qz = (m02 + m20) / s;
            }
            else if (m11 > m22)
            {
                float s = 2.0f * sqrtf(1.0f + m11 - m00 - m22);
                n.qw = (m02 - m20) / s;
                n.qx = (m01 + m10) / s;
                n.qy = 0.25f * s;
                n.qz = (m12 + m21) / s;
            }
            else
            {
                float s = 2.0f * sqrtf(1.0f + m22 - m00 - m11);
                n.qw = (m10 - m01) / s;
                n.qx = (m02 + m20) / s;
                n.qy = (m12 + m21) / s;
                n.qz = 0.25f * s;
            }
            didPhysicsYaw = true;
        }
    }
    if (!didPhysicsYaw)
    {
        // No physics or degenerate case — full Euler->quat sync
        float hrx = x * 3.14159265f / 180.0f * 0.5f;
        float hry = y * 3.14159265f / 180.0f * 0.5f;
        float hrz = z * 3.14159265f / 180.0f * 0.5f;
        float cx = cosf(hrx), sx = sinf(hrx);
        float cy = cosf(hry), sy = sinf(hry);
        float cz = cosf(hrz), sz = sinf(hrz);
        n.qw = cz*cy*cx + sz*sy*sx;
        n.qx = cz*cy*sx - sz*sy*cx;
        n.qy = cz*sy*cx + sz*cy*sx;
        n.qz = sz*cy*cx - cz*sy*sx;
    }
    n.rotX = x;
    n.rotY = y;
    n.rotZ = z;
}

NB_RT_EXPORT void NB_RT_GetCameraWorldForward(const char* name, float outFwd[3])
{
    if (!outFwd)
        return;
    outFwd[0] = 0.0f;
    outFwd[1] = 0.0f;
    outFwd[2] = 1.0f;
    if (!name)
        return;
    int idx = FindCamera3DByName(name);
    if (idx < 0)
        return;
    float wx, wy, wz, wrx, wry, wrz;
    GetCamera3DWorldTR(idx, wx, wy, wz, wrx, wry, wrz);
    Vec3 right{}, up{}, forward{};
    GetLocalAxesFromEuler(wrx, wry, wrz, right, up, forward);
    outFwd[0] = forward.x;
    outFwd[1] = forward.y;
    outFwd[2] = forward.z;
}

NB_RT_EXPORT void NB_RT_GetCameraOrbit(const char* name, float outOrbit[3])
{
    if (!outOrbit)
        return;
    outOrbit[0] = outOrbit[1] = outOrbit[2] = 0.0f;
    if (!name)
        return;
    int idx = FindCamera3DByName(name);
    if (idx < 0)
        return;
    const auto& c = gCamera3DNodes[idx];
    outOrbit[0] = c.orbitX;
    outOrbit[1] = c.orbitY;
    outOrbit[2] = c.orbitZ;
}

NB_RT_EXPORT void NB_RT_SetCameraOrbit(const char* name, float x, float y, float z)
{
    if (!name)
        return;
    int idx = FindCamera3DByName(name);
    if (idx < 0)
        return;
    auto& c = gCamera3DNodes[idx];
    c.orbitX = x;
    c.orbitY = y;
    c.orbitZ = z;
}

NB_RT_EXPORT void NB_RT_GetCameraRotation(const char* name, float outRot[3])
{
    if (!outRot)
        return;
    outRot[0] = outRot[1] = outRot[2] = 0.0f;
    if (!name)
        return;
    int idx = FindCamera3DByName(name);
    if (idx < 0)
        return;
    const auto& c = gCamera3DNodes[idx];
    outRot[0] = c.rotX;
    outRot[1] = c.rotY;
    outRot[2] = c.rotZ;
}

NB_RT_EXPORT void NB_RT_SetCameraRotation(const char* name, float x, float y, float z)
{
    if (!name)
        return;
    int idx = FindCamera3DByName(name);
    if (idx < 0)
        return;
    auto& c = gCamera3DNodes[idx];
    c.rotX = x;
    c.rotY = y;
    c.rotZ = z;
}

NB_RT_EXPORT int NB_RT_IsCameraUnderNode3D(const char* cameraName, const char* nodeName)
{
    if (!cameraName || !nodeName)
        return 0;
    int camIdx = FindCamera3DByName(cameraName);
    if (camIdx < 0)
        return 0;
    return IsCameraUnderNode3D(gCamera3DNodes[camIdx], nodeName) ? 1 : 0;
}

NB_RT_EXPORT void NB_RT_GetNode3DCollisionBounds(const char* name, float outExtents[3])
{
    if (!outExtents) return;
    outExtents[0] = outExtents[1] = outExtents[2] = 0.5f;
    if (!name) return;
    int idx = FindNode3DByName(name);
    if (idx < 0) return;
    const auto& n = gNode3DNodes[idx];
    outExtents[0] = n.extentX;
    outExtents[1] = n.extentY;
    outExtents[2] = n.extentZ;
}

NB_RT_EXPORT void NB_RT_SetNode3DCollisionBounds(const char* name, float ex, float ey, float ez)
{
    if (!name) return;
    int idx = FindNode3DByName(name);
    if (idx < 0) return;
    auto& n = gNode3DNodes[idx];
    n.extentX = ex;
    n.extentY = ey;
    n.extentZ = ez;
}

NB_RT_EXPORT void NB_RT_GetNode3DBoundPos(const char* name, float outPos[3])
{
    if (!outPos) return;
    outPos[0] = outPos[1] = outPos[2] = 0.0f;
    if (!name) return;
    int idx = FindNode3DByName(name);
    if (idx < 0) return;
    const auto& n = gNode3DNodes[idx];
    outPos[0] = n.boundPosX;
    outPos[1] = n.boundPosY;
    outPos[2] = n.boundPosZ;
}

NB_RT_EXPORT void NB_RT_SetNode3DBoundPos(const char* name, float bx, float by, float bz)
{
    if (!name) return;
    int idx = FindNode3DByName(name);
    if (idx < 0) return;
    auto& n = gNode3DNodes[idx];
    n.boundPosX = bx;
    n.boundPosY = by;
    n.boundPosZ = bz;
}

NB_RT_EXPORT int NB_RT_GetNode3DPhysicsEnabled(const char* name)
{
    if (!name) return 0;
    int idx = FindNode3DByName(name);
    if (idx < 0) return 0;
    return gNode3DNodes[idx].physicsEnabled ? 1 : 0;
}

NB_RT_EXPORT void NB_RT_SetNode3DPhysicsEnabled(const char* name, int enabled)
{
    if (!name) return;
    int idx = FindNode3DByName(name);
    if (idx < 0) return;
    gNode3DNodes[idx].physicsEnabled = (enabled != 0);
}

NB_RT_EXPORT int NB_RT_GetNode3DCollisionSource(const char* name)
{
    if (!name) return 0;
    int idx = FindNode3DByName(name);
    if (idx < 0) return 0;
    return gNode3DNodes[idx].collisionSource ? 1 : 0;
}

NB_RT_EXPORT void NB_RT_SetNode3DCollisionSource(const char* name, int enabled)
{
    if (!name) return;
    int idx = FindNode3DByName(name);
    if (idx < 0) return;
    gNode3DNodes[idx].collisionSource = (enabled != 0);
}

NB_RT_EXPORT int NB_RT_GetNode3DSimpleCollision(const char* name)
{
    if (!name) return 0;
    int idx = FindNode3DByName(name);
    if (idx < 0) return 0;
    return gNode3DNodes[idx].simpleCollision ? 1 : 0;
}

NB_RT_EXPORT void NB_RT_SetNode3DSimpleCollision(const char* name, int enabled)
{
    if (!name) return;
    int idx = FindNode3DByName(name);
    if (idx < 0) return;
    gNode3DNodes[idx].simpleCollision = (enabled != 0);
}

NB_RT_EXPORT float NB_RT_GetNode3DVelocityY(const char* name)
{
    if (!name) return 0.0f;
    int idx = FindNode3DByName(name);
    if (idx < 0) return 0.0f;
    return gNode3DNodes[idx].velY;
}

NB_RT_EXPORT void NB_RT_SetNode3DVelocityY(const char* name, float vy)
{
    if (!name) return;
    int idx = FindNode3DByName(name);
    if (idx < 0) return;
    gNode3DNodes[idx].velY = vy;
}

NB_RT_EXPORT int NB_RT_IsNode3DOnFloor(const char* name)
{
    if (!name) return 0;
    int idx = FindNode3DByName(name);
    if (idx < 0) return 0;
    const auto& n = gNode3DNodes[idx];
    // On floor if physics enabled and vertical velocity is zero (landed)
    return (n.physicsEnabled && n.velY >= 0.0f && n.velY < 0.01f) ? 1 : 0;
}

NB_RT_EXPORT int NB_RT_CheckAABBOverlap(const char* name1, const char* name2)
{
    if (!name1 || !name2) return 0;
    int i1 = FindNode3DByName(name1);
    int i2 = FindNode3DByName(name2);
    if (i1 < 0 || i2 < 0) return 0;

    const auto& a = gNode3DNodes[i1];
    const auto& b = gNode3DNodes[i2];

    float ax = a.x, ay = a.y, az = a.z;
    float bx = b.x, by = b.y, bz = b.z;

    // Apply bound offsets
    ax += a.boundPosX; ay += a.boundPosY; az += a.boundPosZ;
    bx += b.boundPosX; by += b.boundPosY; bz += b.boundPosZ;

    // Scale extents by node scale
    float aex = a.extentX * a.scaleX, aey = a.extentY * a.scaleY, aez = a.extentZ * a.scaleZ;
    float bex = b.extentX * b.scaleX, bey = b.extentY * b.scaleY, bez = b.extentZ * b.scaleZ;

    // AABB overlap test
    if (ax + aex < bx - bex || ax - aex > bx + bex) return 0;
    if (ay + aey < by - bey || ay - aey > by + bey) return 0;
    if (az + aez < bz - bez || az - aez > bz + bez) return 0;
    return 1;
}

// ---------------------------------------------------------------------------
// NavMesh runtime bridge (editor-side)
// ---------------------------------------------------------------------------
NB_RT_EXPORT int NB_RT_NavMeshBuild(void)
{
    // Inline BE readers (LoadNebMesh helpers are defined later in the file)
    auto readU32BE = [](std::ifstream& f, uint32_t& v) -> bool {
        uint8_t b[4]; if (!f.read((char*)b, 4)) return false;
        v = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
        return true;
    };
    auto readS16BE = [](std::ifstream& f, int16_t& v) -> bool {
        uint8_t b[2]; if (!f.read((char*)b, 2)) return false;
        v = (int16_t)(((uint16_t)b[0] << 8) | b[1]);
        return true;
    };
    auto readU16BE = [](std::ifstream& f, uint16_t& v) -> bool {
        uint8_t b[2]; if (!f.read((char*)b, 2)) return false;
        v = ((uint16_t)b[0] << 8) | b[1];
        return true;
    };

    // Collect NavMesh3D bounding volumes
    struct NavBounds { float minX, minY, minZ, maxX, maxY, maxZ; bool negator; };
    std::vector<NavBounds> navVolumes;
    for (const auto& nm : gNavMesh3DNodes)
    {
        if (!nm.navBounds && !nm.navNegator) continue;
        float hx = nm.extentX * 0.5f;
        float hy = nm.extentY * 0.5f;
        float hz = nm.extentZ * 0.5f;
        navVolumes.push_back({
            nm.x - hx, nm.y - hy, nm.z - hz,
            nm.x + hx, nm.y + hy, nm.z + hz,
            nm.navNegator
        });
    }
    printf("[NavMesh] %d nav volumes\n", (int)navVolumes.size());

    // Test if a point is inside any navBounds and not inside any navNegator
    auto pointInNavBounds = [&](float px, float py, float pz) -> bool {
        bool inBounds = false;
        for (const auto& vol : navVolumes)
        {
            if (vol.negator) continue;
            if (px >= vol.minX && px <= vol.maxX &&
                py >= vol.minY && py <= vol.maxY &&
                pz >= vol.minZ && pz <= vol.maxZ)
            { inBounds = true; break; }
        }
        if (!inBounds) return false;
        for (const auto& vol : navVolumes)
        {
            if (!vol.negator) continue;
            if (px >= vol.minX && px <= vol.maxX &&
                py >= vol.minY && py <= vol.maxY &&
                pz >= vol.minZ && pz <= vol.maxZ)
                return false;
        }
        return true;
    };

    // Gather world-space triangles from all StaticMesh3D nodes, clipped to nav bounds
    std::vector<float> verts;
    std::vector<int>   tris;
    std::vector<unsigned char> triFlags;
    printf("[NavMesh] Building from %d static meshes (project=%s)\n", (int)gStaticMeshNodes.size(), gProjectDir.c_str());
    for (int si = 0; si < (int)gStaticMeshNodes.size(); ++si)
    {
        const auto& sm = gStaticMeshNodes[si];
        if (sm.mesh.empty() || gProjectDir.empty()) continue;
        if (!sm.collisionSource) continue;

        std::filesystem::path meshPath = std::filesystem::path(gProjectDir) / sm.mesh;
        std::ifstream mf(meshPath, std::ios::binary);
        if (!mf.is_open()) { printf("[NavMesh]   [%d] %s — file not found\n", si, sm.name.c_str()); continue; }

        // Read NEBM header (4-byte magic, then BE u32: version, flags, vertexCount, indexCount, posFracBits)
        char magic[4];
        if (!mf.read(magic, 4) || magic[0] != 'N' || magic[1] != 'E' || magic[2] != 'B' || magic[3] != 'M')
        { printf("[NavMesh]   [%d] %s — bad magic\n", si, sm.name.c_str()); continue; }

        uint32_t version = 0, flags = 0, vertexCount = 0, indexCount = 0, posFracBits = 8;
        if (!readU32BE(mf, version) || !readU32BE(mf, flags) || !readU32BE(mf, vertexCount) ||
            !readU32BE(mf, indexCount) || !readU32BE(mf, posFracBits))
        { printf("[NavMesh]   [%d] %s — header read failed\n", si, sm.name.c_str()); continue; }

        // Read positions (fixed-point s16 BE)
        float invScale = 1.0f / (float)(1 << posFracBits);
        std::vector<Vec3> positions(vertexCount);
        for (uint32_t v = 0; v < vertexCount; ++v)
        {
            int16_t x, y, z;
            if (!readS16BE(mf, x) || !readS16BE(mf, y) || !readS16BE(mf, z)) break;
            positions[v] = { x * invScale, y * invScale, z * invScale };
        }

        // Skip UVs
        bool hasUv = (flags & 1u) != 0;
        bool hasUv1 = (flags & 16u) != 0;
        if (hasUv) mf.seekg(vertexCount * 4, std::ios::cur);  // 2x s16 per vertex
        if (hasUv1) mf.seekg(vertexCount * 4, std::ios::cur);

        // Read indices (u16 BE)
        std::vector<uint16_t> indices(indexCount);
        for (uint32_t i = 0; i < indexCount; ++i)
        {
            if (!readU16BE(mf, indices[i])) break;
        }

        if (positions.empty() || indices.empty()) continue;

        // Get world transform
        float wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz;
        GetStaticMeshWorldTRS(si, wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz);

        // Standalone StaticMesh3D uses axis remap (X<-Z, Y<-X, Z<-Y) to match rendering
        Vec3 right, up, forward;
        bool hasN3DParent = !sm.parent.empty() && FindNode3DByName(sm.parent) >= 0;
        if (hasN3DParent)
            GetLocalAxesFromEuler(wrx, wry, wrz, right, up, forward);
        else
            GetLocalAxesFromEuler(wrz, wrx, wry, right, up, forward);

        // Transform all vertices to world space
        std::vector<Vec3> worldPos(vertexCount);
        for (uint32_t v = 0; v < vertexCount; ++v)
        {
            float lx = positions[v].x * wsx;
            float ly = positions[v].y * wsy;
            float lz = positions[v].z * wsz;
            worldPos[v].x = wx + right.x * lx + up.x * ly + forward.x * lz;
            worldPos[v].y = wy + right.y * lx + up.y * ly + forward.y * lz;
            worldPos[v].z = wz + right.z * lx + up.z * ly + forward.z * lz;
        }

        // Only include triangles where at least one vertex is inside nav bounds
        int addedTris = 0;
        for (uint32_t t = 0; t + 2 < indexCount; t += 3)
        {
            uint16_t i0 = indices[t], i1 = indices[t + 1], i2 = indices[t + 2];
            if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) continue;
            const Vec3& v0 = worldPos[i0];
            const Vec3& v1 = worldPos[i1];
            const Vec3& v2 = worldPos[i2];
            bool inside = navVolumes.empty() ||
                          pointInNavBounds(v0.x, v0.y, v0.z) ||
                          pointInNavBounds(v1.x, v1.y, v1.z) ||
                          pointInNavBounds(v2.x, v2.y, v2.z);
            if (!inside) continue;

            // Mark ALL faces on collisionWalls meshes as non-walkable obstacles
            unsigned char flag = sm.collisionWalls ? 1 : 0;

            int baseVert = (int)(verts.size() / 3);
            verts.push_back(v0.x); verts.push_back(v0.y); verts.push_back(v0.z);
            verts.push_back(v1.x); verts.push_back(v1.y); verts.push_back(v1.z);
            verts.push_back(v2.x); verts.push_back(v2.y); verts.push_back(v2.z);
            tris.push_back(baseVert);
            tris.push_back(baseVert + 1);
            tris.push_back(baseVert + 2);
            triFlags.push_back(flag);
            ++addedTris;
        }
        { int wf = 0; for (size_t fi = triFlags.size() - addedTris; fi < triFlags.size(); ++fi) if (triFlags[fi] & 1) ++wf;
          printf("[NavMesh]   [%d] %s — %d/%u tris (in bounds), collisionWalls=%d, wallFlags=%d\n", si, sm.name.c_str(), addedTris, indexCount / 3, sm.collisionWalls ? 1 : 0, wf); }
    }

    int wallFlagCount = 0;
    for (size_t i = 0; i < triFlags.size(); ++i)
        if (triFlags[i] & 1) ++wallFlagCount;
    printf("[NavMesh] Total: %d verts, %d tris (%d wall-flagged)\n", (int)(verts.size() / 3), (int)(tris.size() / 3), wallFlagCount);
    if (verts.empty() || tris.empty())
    {
        printf("[NavMesh] No geometry — build failed\n");
        return 0;
    }
    int ok = NavMeshBuild(verts.data(), (int)(verts.size() / 3), tris.data(), (int)(tris.size() / 3), NavMeshParams{}, triFlags.data()) ? 1 : 0;
    printf("[NavMesh] Build result: %s\n", ok ? "success" : "failed");
    return ok;
}

NB_RT_EXPORT void NB_RT_NavMeshClear(void)
{
    NavMeshClear();
}

NB_RT_EXPORT int NB_RT_NavMeshIsReady(void)
{
    return NavMeshIsReady() ? 1 : 0;
}

NB_RT_EXPORT int NB_RT_NavMeshFindPath(float sx, float sy, float sz,
                                        float gx, float gy, float gz,
                                        float* outPath, int maxPoints)
{
    Vec3 start = {sx, sy, sz};
    Vec3 goal  = {gx, gy, gz};
    std::vector<Vec3> path;
    if (!NavMeshFindPath(start, goal, path)) return 0;
    int count = (int)path.size();
    if (count > maxPoints) count = maxPoints;
    for (int i = 0; i < count; ++i)
    {
        outPath[i * 3 + 0] = path[i].x;
        outPath[i * 3 + 1] = path[i].y;
        outPath[i * 3 + 2] = path[i].z;
    }
    return count;
}

NB_RT_EXPORT int NB_RT_NavMeshFindRandomPoint(float outPos[3])
{
    Vec3 pt;
    if (!NavMeshFindRandomPoint(pt)) return 0;
    outPos[0] = pt.x; outPos[1] = pt.y; outPos[2] = pt.z;
    return 1;
}

NB_RT_EXPORT int NB_RT_NavMeshFindClosestPoint(float px, float py, float pz, float outPos[3])
{
    Vec3 pos = {px, py, pz};
    Vec3 closest;
    if (!NavMeshFindClosestPoint(pos, closest)) return 0;
    outPos[0] = closest.x; outPos[1] = closest.y; outPos[2] = closest.z;
    return 1;
}

// ---------------------------------------------------------------------------
// Scene switching
// ---------------------------------------------------------------------------
NB_RT_EXPORT void NB_RT_NextScene(void)
{
    if (gOpenScenes.size() <= 1) return;
    int next = (gActiveScene + 1) % (int)gOpenScenes.size();
    SetActiveScene(next);
}

NB_RT_EXPORT void NB_RT_PrevScene(void)
{
    if (gOpenScenes.size() <= 1) return;
    int prev = (gActiveScene - 1 + (int)gOpenScenes.size()) % (int)gOpenScenes.size();
    SetActiveScene(prev);
}

NB_RT_EXPORT void NB_RT_SwitchScene(const char* name)
{
    if (!name || !name[0]) return;
    // Check already-open scenes first
    for (int i = 0; i < (int)gOpenScenes.size(); ++i)
    {
        if (_stricmp(gOpenScenes[i].name.c_str(), name) == 0)
        {
            SetActiveScene(i);
            return;
        }
    }
    // Scene not open — search project folder and auto-load it
    if (!gProjectDir.empty())
    {
        std::error_code ec;
        for (auto& e : std::filesystem::recursive_directory_iterator(gProjectDir, ec))
        {
            if (!e.is_regular_file() || e.path().extension() != ".nebscene") continue;
            if (_stricmp(e.path().stem().string().c_str(), name) == 0)
            {
                SceneData scene;
                if (LoadSceneFromPath(e.path(), scene))
                {
                    // Save current scene nodes before adding new one
                    if (gActiveScene >= 0 && gActiveScene < (int)gOpenScenes.size())
                    {
                        gOpenScenes[gActiveScene].nodes = gAudio3DNodes;
                        gOpenScenes[gActiveScene].staticMeshes = gStaticMeshNodes;
                        gOpenScenes[gActiveScene].cameras = gCamera3DNodes;
                        gOpenScenes[gActiveScene].node3d = gNode3DNodes;
                        gOpenScenes[gActiveScene].navMeshes = gNavMesh3DNodes;
                    }
                    gOpenScenes.push_back(scene);
                    if (gPlayMode)
                        gPlayOriginalScenes.push_back(scene);
                    int newIdx = (int)gOpenScenes.size() - 1;
                    gActiveScene = newIdx;
                    gForceSelectSceneTab = newIdx;
                    gAudio3DNodes = gOpenScenes[newIdx].nodes;
                    gStaticMeshNodes = gOpenScenes[newIdx].staticMeshes;
                    gCamera3DNodes = gOpenScenes[newIdx].cameras;
                    gNode3DNodes = gOpenScenes[newIdx].node3d;
                    gNavMesh3DNodes = gOpenScenes[newIdx].navMeshes;
                    NotifyScriptSceneSwitch();
                }
                return;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Animation slot bridge functions (editor side)
// ---------------------------------------------------------------------------
NB_RT_EXPORT void NB_RT_PlayAnimation(const char* meshName, const char* animName)
{
    if (!meshName || !animName) return;
    int idx = FindStaticMeshByName(meshName);
    if (idx < 0) return;
    const auto& n = gStaticMeshNodes[idx];
    for (int si = 0; si < n.animSlotCount; ++si)
    {
        if (_stricmp(n.animSlots[si].name.c_str(), animName) == 0)
        {
            gEditorAnimActiveSlot[idx] = si;
            gEditorAnimTime[idx] = 0.0f;
            gEditorAnimSpeed[idx] = n.animSlots[si].speed;
            gEditorAnimLoop[idx] = n.animSlots[si].loop;
            gEditorAnimPlaying[idx] = true;
            gEditorAnimFinished[idx] = false;
            return;
        }
    }
}

NB_RT_EXPORT void NB_RT_StopAnimation(const char* meshName)
{
    if (!meshName) return;
    int idx = FindStaticMeshByName(meshName);
    if (idx < 0) return;
    gEditorAnimPlaying[idx] = false;
}

NB_RT_EXPORT int NB_RT_IsAnimationPlaying(const char* meshName)
{
    if (!meshName) return 0;
    int idx = FindStaticMeshByName(meshName);
    if (idx < 0) return 0;
    return gEditorAnimPlaying.count(idx) && gEditorAnimPlaying[idx] ? 1 : 0;
}

NB_RT_EXPORT int NB_RT_IsAnimationFinished(const char* meshName)
{
    if (!meshName) return 0;
    int idx = FindStaticMeshByName(meshName);
    if (idx < 0) return 0;
    return gEditorAnimFinished.count(idx) && gEditorAnimFinished[idx] ? 1 : 0;
}

NB_RT_EXPORT void NB_RT_SetAnimationSpeed(const char* meshName, float speed)
{
    if (!meshName) return;
    int idx = FindStaticMeshByName(meshName);
    if (idx < 0) return;
    gEditorAnimSpeed[idx] = speed;
}

// ---------------------------------------------------------------------------
// NB_RT_PlayVmuLayer — editor-side no-op (only meaningful on DC runtime)
// ---------------------------------------------------------------------------
NB_RT_EXPORT void NB_RT_PlayVmuLayer(int layer) { (void)layer; }

// ---------------------------------------------------------------------------
// NB_RT_RaycastDown — cast a vertical ray downward from (rx,ry,rz) and find
// the highest collision-flagged StaticMesh3D triangle surface below that point.
// ---------------------------------------------------------------------------
NB_RT_EXPORT int NB_RT_RaycastDown(float rx, float ry, float rz, float* outHitY)
{
    if (!outHitY) return 0;
    int hit = 0;
    float bestY = -1e30f;
    for (int si = 0; si < (int)gStaticMeshNodes.size(); ++si)
    {
        const auto& s = gStaticMeshNodes[si];
        if (s.mesh.empty() || gProjectDir.empty()) continue;
        if (!s.collisionSource) continue;

        std::filesystem::path meshPath = std::filesystem::path(gProjectDir) / s.mesh;
        const NebMesh* mesh = GetNebMesh(meshPath);
        if (!mesh || !mesh->valid || mesh->positions.empty() || mesh->indices.empty()) continue;

        float wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz;
        GetStaticMeshWorldTRS(si, wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz);

        // Standalone StaticMesh3D uses axis remap (X<-Z, Y<-X, Z<-Y) to match rendering
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
            // Scale then rotate then translate
            float s0x = p0.x * wsx, s0y = p0.y * wsy, s0z = p0.z * wsz;
            float s1x = p1.x * wsx, s1y = p1.y * wsy, s1z = p1.z * wsz;
            float s2x = p2.x * wsx, s2y = p2.y * wsy, s2z = p2.z * wsz;
            float ax = wx + right.x * s0x + up.x * s0y + forward.x * s0z;
            float ay = wy + right.y * s0x + up.y * s0y + forward.y * s0z;
            float az = wz + right.z * s0x + up.z * s0y + forward.z * s0z;
            float bx = wx + right.x * s1x + up.x * s1y + forward.x * s1z;
            float by = wy + right.y * s1x + up.y * s1y + forward.y * s1z;
            float bz = wz + right.z * s1x + up.z * s1y + forward.z * s1z;
            float cx = wx + right.x * s2x + up.x * s2y + forward.x * s2z;
            float cy = wy + right.y * s2x + up.y * s2y + forward.y * s2z;
            float cz = wz + right.z * s2x + up.z * s2y + forward.z * s2z;

            // 2D barycentric test in XZ plane (ray is vertical)
            float e1x = bx - ax, e1z = bz - az;
            float e2x = cx - ax, e2z = cz - az;
            float det = e1x * e2z - e2x * e1z;
            if (det > -1e-8f && det < 1e-8f) continue;
            float inv = 1.0f / det;
            float dx = rx - ax, dz = rz - az;
            float u = (dx * e2z - dz * e2x) * inv;
            if (u < 0.0f || u > 1.0f) continue;
            float v = (e1x * dz - e1z * dx) * inv;
            if (v < 0.0f || u + v > 1.0f) continue;
            float hitY = ay + (by - ay) * u + (cy - ay) * v;
            if (hitY <= ry && hitY > bestY)
            {
                // Skip wall-like triangles — only accept floor surfaces
                float ex1 = bx - ax, ey1 = by - ay, ez1 = bz - az;
                float ex2 = cx - ax, ey2 = cy - ay, ez2 = cz - az;
                float fnx = ey1 * ez2 - ez1 * ey2;
                float fny = ez1 * ex2 - ex1 * ez2;
                float fnz = ex1 * ey2 - ey1 * ex2;
                float nLen = sqrtf(fnx * fnx + fny * fny + fnz * fnz);
                if (nLen > 1e-8f)
                {
                    fny = (fny < 0.0f) ? -fny : fny;
                    if (fny / nLen < 0.0f) continue;
                }
                bestY = hitY;
                hit = 1;
            }
        }
    }
    if (hit) *outHitY = bestY;
    return hit;
}

// NB_RT_RaycastDownWithNormal — same as RaycastDown but also returns the
// surface normal of the hit triangle so scripts can align to slopes.
// ---------------------------------------------------------------------------
NB_RT_EXPORT int NB_RT_RaycastDownWithNormal(float rx, float ry, float rz, float* outHitY, float outNormal[3])
{
    if (!outHitY) return 0;
    int hit = 0;
    float bestY = -1e30f;
    float bestNx = 0.0f, bestNy = 1.0f, bestNz = 0.0f;
    for (int si = 0; si < (int)gStaticMeshNodes.size(); ++si)
    {
        const auto& s = gStaticMeshNodes[si];
        if (s.mesh.empty() || gProjectDir.empty()) continue;
        if (!s.collisionSource) continue;

        std::filesystem::path meshPath = std::filesystem::path(gProjectDir) / s.mesh;
        const NebMesh* mesh = GetNebMesh(meshPath);
        if (!mesh || !mesh->valid || mesh->positions.empty() || mesh->indices.empty()) continue;

        float wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz;
        GetStaticMeshWorldTRS(si, wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz);

        // Standalone StaticMesh3D uses axis remap (X<-Z, Y<-X, Z<-Y) to match rendering
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
            float cx = wx + right.x * s2x + up.x * s2y + forward.x * s2z;
            float cy = wy + right.y * s2x + up.y * s2y + forward.y * s2z;
            float cz = wz + right.z * s2x + up.z * s2y + forward.z * s2z;

            float e1x = bx - ax, e1z = bz - az;
            float e2x = cx - ax, e2z = cz - az;
            float det = e1x * e2z - e2x * e1z;
            if (det > -1e-8f && det < 1e-8f) continue;
            float inv = 1.0f / det;
            float dx = rx - ax, dz = rz - az;
            float u = (dx * e2z - dz * e2x) * inv;
            if (u < 0.0f || u > 1.0f) continue;
            float v = (e1x * dz - e1z * dx) * inv;
            if (v < 0.0f || u + v > 1.0f) continue;
            float hitY = ay + (by - ay) * u + (cy - ay) * v;
            if (hitY <= ry && hitY > bestY)
            {
                // Compute face normal via cross product of triangle edges
                float ex1 = bx - ax, ey1 = by - ay, ez1 = bz - az;
                float ex2 = cx - ax, ey2 = cy - ay, ez2 = cz - az;
                float fnx = ey1 * ez2 - ez1 * ey2;
                float fny = ez1 * ex2 - ex1 * ez2;
                float fnz = ex1 * ey2 - ey1 * ex2;
                float nLen = sqrtf(fnx * fnx + fny * fny + fnz * fnz);
                if (nLen > 1e-8f)
                {
                    float nInv = 1.0f / nLen;
                    fnx *= nInv; fny *= nInv; fnz *= nInv;
                    if (fny < 0.0f) { fnx = -fnx; fny = -fny; fnz = -fnz; }
                    // Skip wall-like triangles — only accept floor surfaces
                    if (fny < 0.0f) continue;
                    bestY = hitY;
                    hit = 1;
                    bestNx = fnx; bestNy = fny; bestNz = fnz;
                }
            }
        }
    }
    if (hit)
    {
        *outHitY = bestY;
        if (outNormal) { outNormal[0] = bestNx; outNormal[1] = bestNy; outNormal[2] = bestNz; }
    }
    return hit;
}
