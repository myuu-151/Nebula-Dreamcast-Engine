#include "NodeTypes.h"
#include <cmath>

namespace
{
    struct Q4 { float w, x, y, z; };

    static Q4 Q4Multiply(Q4 a, Q4 b)
    {
        return {
            a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z,
            a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
            a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
            a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w
        };
    }

    static Q4 Q4FromAxisAngle(float ax, float ay, float az, float angleDeg)
    {
        float half = angleDeg * 3.14159265f / 180.0f * 0.5f;
        float s = sinf(half);
        return { cosf(half), ax * s, ay * s, az * s };
    }

    static Q4 EulerToQ4(float rotX, float rotY, float rotZ)
    {
        // R = Rz * Ry * Rx
        Q4 qX = Q4FromAxisAngle(1, 0, 0, rotX);
        Q4 qY = Q4FromAxisAngle(0, 1, 0, rotY);
        Q4 qZ = Q4FromAxisAngle(0, 0, 1, rotZ);
        return Q4Multiply(Q4Multiply(qZ, qY), qX);
    }

    static void Q4ToEuler(Q4 q, float& rotX, float& rotY, float& rotZ)
    {
        const float kDeg = 180.0f / 3.14159265f;
        float m20 = 2.0f * (q.x * q.z - q.w * q.y);
        float sy = -m20;
        if (sy > 0.9999f) sy = 0.9999f;
        if (sy < -0.9999f) sy = -0.9999f;
        rotY = asinf(sy) * kDeg;
        float m21 = 2.0f * (q.y * q.z + q.w * q.x);
        float m22 = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
        rotX = atan2f(m21, m22) * kDeg;
        float m10 = 2.0f * (q.x * q.y + q.w * q.z);
        float m00 = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
        rotZ = atan2f(m10, m00) * kDeg;
    }
}

namespace NebulaNodes
{
    int FindNode3DByName(const std::vector<Node3DNode>& node3DNodes, const std::string& name)
    {
        for (int i = 0; i < (int)node3DNodes.size(); ++i)
            if (node3DNodes[i].name == name) return i;
        return -1;
    }

    bool Node3DCreatesCycle(const std::vector<Node3DNode>& node3DNodes, int childIdx, int candidateParentIdx)
    {
        if (childIdx < 0 || candidateParentIdx < 0) return false;
        if (childIdx == candidateParentIdx) return true;
        std::string p = node3DNodes[candidateParentIdx].parent;
        int guard = 0;
        while (!p.empty() && guard++ < 256)
        {
            int pi = FindNode3DByName(node3DNodes, p);
            if (pi < 0) break;
            if (pi == childIdx) return true;
            p = node3DNodes[pi].parent;
        }
        return false;
    }

    void GetNode3DWorldTRS(
        const std::vector<StaticMesh3DNode>& staticMeshNodes,
        const std::vector<Node3DNode>& node3DNodes,
        int idx,
        float& ox,
        float& oy,
        float& oz,
        float& orx,
        float& ory,
        float& orz,
        float& osx,
        float& osy,
        float& osz)
    {
        if (idx < 0 || idx >= (int)node3DNodes.size()) { ox=oy=oz=orx=ory=orz=0.0f; osx=osy=osz=1.0f; return; }
        const auto& n = node3DNodes[idx];
        ox = n.x; oy = n.y; oz = n.z;
        Q4 q = { n.qw, n.qx, n.qy, n.qz };
        osx = n.scaleX; osy = n.scaleY; osz = n.scaleZ;
        std::string p = n.parent;
        int guard = 0;
        while (!p.empty() && guard++ < 256)
        {
            int ni = FindNode3DByName(node3DNodes, p);
            if (ni >= 0)
            {
                const auto& pn = node3DNodes[ni];
                ox += pn.x; oy += pn.y; oz += pn.z;
                Q4 pq = { pn.qw, pn.qx, pn.qy, pn.qz };
                q = Q4Multiply(pq, q);
                osx *= pn.scaleX; osy *= pn.scaleY; osz *= pn.scaleZ;
                p = pn.parent;
                continue;
            }

            int si = FindStaticMeshByName(staticMeshNodes, p);
            if (si >= 0)
            {
                const auto& pn = staticMeshNodes[si];
                ox += pn.x; oy += pn.y; oz += pn.z;
                Q4 pq = EulerToQ4(pn.rotX, pn.rotY, pn.rotZ);
                q = Q4Multiply(pq, q);
                osx *= pn.scaleX; osy *= pn.scaleY; osz *= pn.scaleZ;
                p = pn.parent;
                continue;
            }
            break;
        }
        Q4ToEuler(q, orx, ory, orz);
    }

    void GetNode3DWorldTRSQuat(
        const std::vector<StaticMesh3DNode>& staticMeshNodes,
        const std::vector<Node3DNode>& node3DNodes,
        int idx,
        float& ox,
        float& oy,
        float& oz,
        float& oqw,
        float& oqx,
        float& oqy,
        float& oqz,
        float& osx,
        float& osy,
        float& osz)
    {
        if (idx < 0 || idx >= (int)node3DNodes.size()) { ox=oy=oz=0.0f; oqw=1.0f; oqx=oqy=oqz=0.0f; osx=osy=osz=1.0f; return; }
        const auto& n = node3DNodes[idx];
        ox = n.x; oy = n.y; oz = n.z;
        Q4 q = { n.qw, n.qx, n.qy, n.qz };
        osx = n.scaleX; osy = n.scaleY; osz = n.scaleZ;
        std::string p = n.parent;
        int guard = 0;
        while (!p.empty() && guard++ < 256)
        {
            int ni = FindNode3DByName(node3DNodes, p);
            if (ni >= 0)
            {
                const auto& pn = node3DNodes[ni];
                ox += pn.x; oy += pn.y; oz += pn.z;
                Q4 pq = { pn.qw, pn.qx, pn.qy, pn.qz };
                q = Q4Multiply(pq, q);
                osx *= pn.scaleX; osy *= pn.scaleY; osz *= pn.scaleZ;
                p = pn.parent;
                continue;
            }

            int si = FindStaticMeshByName(staticMeshNodes, p);
            if (si >= 0)
            {
                const auto& pn = staticMeshNodes[si];
                ox += pn.x; oy += pn.y; oz += pn.z;
                Q4 pq = EulerToQ4(pn.rotX, pn.rotY, pn.rotZ);
                q = Q4Multiply(pq, q);
                osx *= pn.scaleX; osy *= pn.scaleY; osz *= pn.scaleZ;
                p = pn.parent;
                continue;
            }
            break;
        }
        oqw = q.w; oqx = q.x; oqy = q.y; oqz = q.z;
    }
}
