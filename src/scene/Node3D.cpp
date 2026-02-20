#include "NodeTypes.h"

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
        orx = n.rotX; ory = n.rotY; orz = n.rotZ;
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
                orx += pn.rotX; ory += pn.rotY; orz += pn.rotZ;
                osx *= pn.scaleX; osy *= pn.scaleY; osz *= pn.scaleZ;
                p = pn.parent;
                continue;
            }

            int si = FindStaticMeshByName(staticMeshNodes, p);
            if (si >= 0)
            {
                const auto& pn = staticMeshNodes[si];
                ox += pn.x; oy += pn.y; oz += pn.z;
                orx += pn.rotX; ory += pn.rotY; orz += pn.rotZ;
                osx *= pn.scaleX; osy *= pn.scaleY; osz *= pn.scaleZ;
                p = pn.parent;
                continue;
            }
            break;
        }
    }
}
