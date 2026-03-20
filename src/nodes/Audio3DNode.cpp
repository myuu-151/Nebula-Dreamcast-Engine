#include "NodeTypes.h"
#include <cmath>

std::vector<Audio3DNode> gAudio3DNodes;

void UpdateAudio3DNodes(float listenerX, float listenerY, float listenerZ)
{
    for (auto& n : gAudio3DNodes)
    {
        float dx = n.x - listenerX;
        float dy = n.y - listenerY;
        float dz = n.z - listenerZ;
        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);

        float inner = n.innerRadius;
        float outer = n.outerRadius;
        if (outer < inner) std::swap(outer, inner);
        if (outer <= 0.0f) outer = 0.001f;

        float att = 0.0f;
        if (dist <= inner)
            att = 1.0f;
        else if (dist >= outer)
            att = 0.0f;
        else
            att = 1.0f - ((dist - inner) / (outer - inner));

        float pan = dx / (outer > 0.0f ? outer : 1.0f);
        if (pan < -1.0f) pan = -1.0f;
        if (pan > 1.0f) pan = 1.0f;

        n.pan = pan;
        n.volume = n.baseVolume * att;
        if (n.volume < 0.0f) n.volume = 0.0f;
        if (n.volume > 1.0f) n.volume = 1.0f;

        bool nowInside = dist <= outer;
        n.justEntered = (!n.inside && nowInside);
        n.inside = nowInside;
    }
}

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
