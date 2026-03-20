#include "node_helpers.h"

#include <cmath>
#include <vector>
#include <string>

#include "../editor/editor_state.h"

int FindStaticMeshByName(const std::string& name)
{
    return NebulaNodes::FindStaticMeshByName(gStaticMeshNodes, name);
}

int FindNode3DByName(const std::string& name)
{
    return NebulaNodes::FindNode3DByName(gNode3DNodes, name);
}

int FindCamera3DByName(const std::string& name)
{
    if (name.empty()) return -1;
    for (int i = 0; i < (int)gCamera3DNodes.size(); ++i)
    {
        if (gCamera3DNodes[i].name == name)
            return i;
    }
    return -1;
}

bool TryGetParentByNodeName(const std::string& name, std::string& outParent)
{
    return NebulaNodes::TryGetParentByNodeName(gAudio3DNodes, gStaticMeshNodes, gCamera3DNodes, gNode3DNodes, name, outParent);
}

bool WouldCreateHierarchyCycle(const std::string& childName, const std::string& candidateParentName)
{
    return NebulaNodes::WouldCreateHierarchyCycle(gAudio3DNodes, gStaticMeshNodes, gCamera3DNodes, gNode3DNodes, childName, candidateParentName);
}

bool StaticMeshCreatesCycle(int childIdx, int candidateParentIdx)
{
    return NebulaNodes::StaticMeshCreatesCycle(gStaticMeshNodes, childIdx, candidateParentIdx);
}

bool Node3DCreatesCycle(int childIdx, int candidateParentIdx)
{
    return NebulaNodes::Node3DCreatesCycle(gNode3DNodes, childIdx, candidateParentIdx);
}

void GetStaticMeshWorldTRS(int idx, float& ox, float& oy, float& oz, float& orx, float& ory, float& orz, float& osx, float& osy, float& osz)
{
    NebulaNodes::GetStaticMeshWorldTRS(gStaticMeshNodes, gNode3DNodes, idx, ox, oy, oz, orx, ory, orz, osx, osy, osz);
}

void GetNode3DWorldTRS(int idx, float& ox, float& oy, float& oz, float& orx, float& ory, float& orz, float& osx, float& osy, float& osz)
{
    NebulaNodes::GetNode3DWorldTRS(gStaticMeshNodes, gNode3DNodes, idx, ox, oy, oz, orx, ory, orz, osx, osy, osz);
}

void GetNode3DWorldTRSQuat(int idx, float& ox, float& oy, float& oz, float& oqw, float& oqx, float& oqy, float& oqz, float& osx, float& osy, float& osz)
{
    NebulaNodes::GetNode3DWorldTRSQuat(gStaticMeshNodes, gNode3DNodes, idx, ox, oy, oz, oqw, oqx, oqy, oqz, osx, osy, osz);
}

void GetCamera3DWorldTR(int idx, float& ox, float& oy, float& oz, float& orx, float& ory, float& orz)
{
    if (idx < 0 || idx >= (int)gCamera3DNodes.size())
    {
        ox = oy = oz = 0.0f;
        orx = ory = orz = 0.0f;
        return;
    }

    auto rotateOffsetEuler = [](float& x, float& y, float& z, float rxDeg, float ryDeg, float rzDeg)
    {
        const float rx = rxDeg * 3.14159f / 180.0f;
        const float ry = ryDeg * 3.14159f / 180.0f;
        const float rz = rzDeg * 3.14159f / 180.0f;

        const float sx = sinf(rx), cx = cosf(rx);
        const float sy = sinf(ry), cy = cosf(ry);
        const float sz = sinf(rz), cz = cosf(rz);

        float ty = y * cx - z * sx;
        float tz = y * sx + z * cx;
        y = ty; z = tz;

        float tx = x * cy + z * sy;
        tz = -x * sy + z * cy;
        x = tx; z = tz;

        tx = x * cz - y * sz;
        ty = x * sz + y * cz;
        x = tx; y = ty;
    };

    const auto& c = gCamera3DNodes[idx];
    // Base camera local offset. Orbit offset is conditional on having a parent pivot.
    ox = c.x; oy = c.y; oz = c.z;
    if (!c.parent.empty())
    {
        ox += c.orbitX;
        oy += c.orbitY;
        oz += c.orbitZ;
    }
    orx = c.rotX; ory = c.rotY; orz = c.rotZ;

    std::string p = c.parent;
    int guard = 0;
    while (!p.empty() && guard++ < 256)
    {
        bool found = false;
        for (const auto& a : gAudio3DNodes)
        {
            if (a.name == p)
            {
                ox += a.x; oy += a.y; oz += a.z;
                p = a.parent;
                found = true;
                break;
            }
        }
        if (found) continue;
        for (const auto& s : gStaticMeshNodes)
        {
            if (s.name == p)
            {
                // Parent rotation should orbit the camera offset around parent pivot.
                rotateOffsetEuler(ox, oy, oz, s.rotX, s.rotY, s.rotZ);
                ox += s.x; oy += s.y; oz += s.z;
                orx += s.rotX; ory += s.rotY; orz += s.rotZ;
                p = s.parent;
                found = true;
                break;
            }
        }
        if (found) continue;
        for (const auto& pc : gCamera3DNodes)
        {
            if (pc.name == p)
            {
                rotateOffsetEuler(ox, oy, oz, pc.rotX, pc.rotY, pc.rotZ);
                ox += pc.x; oy += pc.y; oz += pc.z;
                orx += pc.rotX; ory += pc.rotY; orz += pc.rotZ;
                p = pc.parent;
                found = true;
                break;
            }
        }
        if (found) continue;
        for (const auto& n : gNode3DNodes)
        {
            if (n.name == p)
            {
                rotateOffsetEuler(ox, oy, oz, n.rotX, n.rotY, n.rotZ);
                ox += n.x; oy += n.y; oz += n.z;
                orx += n.rotX; ory += n.rotY; orz += n.rotZ;
                p = n.parent;
                found = true;
                break;
            }
        }
        if (!found) break;
    }
}

bool TryGetNodeWorldPosByName(const std::string& name, float& ox, float& oy, float& oz)
{
    if (name.empty()) return false;

    int si = FindStaticMeshByName(name);
    if (si >= 0)
    {
        float rx, ry, rz, sx, sy, sz;
        GetStaticMeshWorldTRS(si, ox, oy, oz, rx, ry, rz, sx, sy, sz);
        return true;
    }

    int ni = FindNode3DByName(name);
    if (ni >= 0)
    {
        float rx, ry, rz, sx, sy, sz;
        GetNode3DWorldTRS(ni, ox, oy, oz, rx, ry, rz, sx, sy, sz);
        return true;
    }

    return false;
}

bool IsCameraUnderNode3D(const Camera3DNode& cam, const std::string& nodeName)
{
    if (nodeName.empty()) return false;
    if (cam.parent == nodeName) return true;

    std::string cur = cam.name;
    std::string p;
    int guard = 0;
    while (guard++ < 256 && TryGetParentByNodeName(cur, p))
    {
        if (p == nodeName) return true;
        if (p.empty() || p == cur) break;
        cur = p;
    }
    return false;
}

void ReparentStaticMeshKeepWorldPos(int childIdx, const std::string& newParent)
{
    if (childIdx < 0 || childIdx >= (int)gStaticMeshNodes.size()) return;

    float wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz;
    GetStaticMeshWorldTRS(childIdx, wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz);

    gStaticMeshNodes[childIdx].parent = newParent;

    if (newParent.empty())
    {
        gStaticMeshNodes[childIdx].x = wx;
        gStaticMeshNodes[childIdx].y = wy;
        gStaticMeshNodes[childIdx].z = wz;
        return;
    }

    float px = 0.0f, py = 0.0f, pz = 0.0f;
    if (TryGetNodeWorldPosByName(newParent, px, py, pz))
    {
        gStaticMeshNodes[childIdx].x = wx - px;
        gStaticMeshNodes[childIdx].y = wy - py;
        gStaticMeshNodes[childIdx].z = wz - pz;
    }
}

void ResetStaticMeshTransformsKeepWorld(int idx)
{
    if (idx < 0 || idx >= (int)gStaticMeshNodes.size()) return;
    float wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz;
    GetStaticMeshWorldTRS(idx, wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz);
    auto& s = gStaticMeshNodes[idx];
    s.parent.clear();
    s.x = wx; s.y = wy; s.z = wz;
    s.rotX = wrx; s.rotY = wry; s.rotZ = wrz;
    s.scaleX = wsx; s.scaleY = wsy; s.scaleZ = wsz;
}
