#include "NodeTypes.h"

namespace NebulaNodes
{
    bool TryGetParentByNodeName(
        const std::vector<Audio3DNode>& audioNodes,
        const std::vector<StaticMesh3DNode>& staticMeshNodes,
        const std::vector<Camera3DNode>& cameraNodes,
        const std::vector<Node3DNode>& node3DNodes,
        const std::string& name,
        std::string& outParent)
    {
        for (const auto& a : audioNodes) if (a.name == name) { outParent = a.parent; return true; }
        for (const auto& s : staticMeshNodes) if (s.name == name) { outParent = s.parent; return true; }
        for (const auto& c : cameraNodes) if (c.name == name) { outParent = c.parent; return true; }
        for (const auto& n : node3DNodes) if (n.name == name) { outParent = n.parent; return true; }
        return false;
    }

    bool WouldCreateHierarchyCycle(
        const std::vector<Audio3DNode>& audioNodes,
        const std::vector<StaticMesh3DNode>& staticMeshNodes,
        const std::vector<Camera3DNode>& cameraNodes,
        const std::vector<Node3DNode>& node3DNodes,
        const std::string& childName,
        const std::string& candidateParentName)
    {
        if (childName.empty() || candidateParentName.empty()) return false;
        if (childName == candidateParentName) return true;
        std::string p = candidateParentName;
        int guard = 0;
        while (!p.empty() && guard++ < 512)
        {
            if (p == childName) return true;
            std::string next;
            if (!TryGetParentByNodeName(audioNodes, staticMeshNodes, cameraNodes, node3DNodes, p, next)) break;
            p = next;
        }
        return false;
    }
}
