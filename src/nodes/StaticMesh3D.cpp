#include "NodeTypes.h"
#include "../core/meta_io.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace NebulaNodes
{
    int FindStaticMeshByName(const std::vector<StaticMesh3DNode>& staticMeshNodes, const std::string& name)
    {
        for (int i = 0; i < (int)staticMeshNodes.size(); ++i)
            if (staticMeshNodes[i].name == name) return i;
        return -1;
    }

    bool StaticMeshCreatesCycle(const std::vector<StaticMesh3DNode>& staticMeshNodes, int childIdx, int candidateParentIdx)
    {
        if (childIdx < 0 || candidateParentIdx < 0) return false;
        if (childIdx == candidateParentIdx) return true;
        std::string p = staticMeshNodes[candidateParentIdx].parent;
        int guard = 0;
        while (!p.empty() && guard++ < 256)
        {
            int pi = FindStaticMeshByName(staticMeshNodes, p);
            if (pi < 0) break;
            if (pi == childIdx) return true;
            p = staticMeshNodes[pi].parent;
        }
        return false;
    }

    void GetStaticMeshWorldTRS(
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
        if (idx < 0 || idx >= (int)staticMeshNodes.size()) { ox=oy=oz=orx=ory=orz=0.0f; osx=osy=osz=1.0f; return; }
        const auto& n = staticMeshNodes[idx];
        ox = n.x; oy = n.y; oz = n.z;
        // Always keep child local rotation, then add inherited parent rotation on top.
        orx = n.rotX; ory = n.rotY; orz = n.rotZ;
        osx = n.scaleX; osy = n.scaleY; osz = n.scaleZ;
        std::string p = n.parent;
        int guard = 0;
        while (!p.empty() && guard++ < 256)
        {
            int pi = FindStaticMeshByName(staticMeshNodes, p);
            if (pi >= 0)
            {
                const auto& pn = staticMeshNodes[pi];
                ox += pn.x; oy += pn.y; oz += pn.z;
                orx += pn.rotX; ory += pn.rotY; orz += pn.rotZ;
                osx *= pn.scaleX; osy *= pn.scaleY; osz *= pn.scaleZ;
                p = pn.parent;
                continue;
            }

            int ni = FindNode3DByName(node3DNodes, p);
            if (ni >= 0)
            {
                const auto& pn = node3DNodes[ni];
                ox += pn.x; oy += pn.y; oz += pn.z;
                // Node3D parent drives rotation entirely; child local rotation
                // is applied separately during rendering for visual orientation only.
                orx = pn.rotX; ory = pn.rotY; orz = pn.rotZ;
                osx *= pn.scaleX; osy *= pn.scaleY; osz *= pn.scaleZ;
                p = pn.parent;
                continue;
            }
            break;
        }
    }

    std::string GetStaticMeshPrimaryMaterial(const StaticMesh3DNode& n)
    {
        int si = n.materialSlot;
        if (si < 0 || si >= kStaticMeshMaterialSlots) si = 0;
        if (!n.materialSlots[si].empty()) return n.materialSlots[si];
        if (!n.materialSlots[0].empty()) return n.materialSlots[0];
        return n.material;
    }

    std::string GetStaticMeshMaterialByIndex(const StaticMesh3DNode& n, int matIndex)
    {
        if (matIndex >= 0 && matIndex < kStaticMeshMaterialSlots)
            return n.materialSlots[matIndex];
        return std::string();
    }

    std::string GetStaticMeshSlotLabel(const StaticMesh3DNode& n, int slotIndex, const std::string& projectDir)
    {
        if (slotIndex < 0 || slotIndex >= kStaticMeshMaterialSlots)
            return "material";

        if (!projectDir.empty() && !n.mesh.empty())
        {
            std::filesystem::path meshPath = std::filesystem::path(projectDir) / n.mesh;

            // Try mat/ directory naming convention first
            std::filesystem::path matDir = meshPath.parent_path() / "mat";
            std::string meshStem = meshPath.stem().string();
            for (char& c : meshStem)
            {
                unsigned char uc = (unsigned char)c;
                if (!(std::isalnum(uc) || c == '_' || c == '-')) c = '_';
            }
            char num[8];
            snprintf(num, sizeof(num), "%02d", slotIndex + 1);
            std::string prefix = "m_" + meshStem + "_" + std::string(num) + "_";

            if (std::filesystem::exists(matDir) && std::filesystem::is_directory(matDir))
            {
                for (const auto& e : std::filesystem::directory_iterator(matDir))
                {
                    if (!e.is_regular_file() || e.path().extension() != ".nebmat") continue;
                    std::string stem = e.path().stem().string();
                    if (stem.rfind(prefix, 0) == 0 && stem.size() > prefix.size())
                        return stem.substr(prefix.size());
                }
            }

            // Fall back to nebslots manifest for slot names
            std::vector<std::string> slots;
            if (NebulaAssets::LoadNebSlotsManifest(meshPath, slots, projectDir))
            {
                if (slotIndex < (int)slots.size() && !slots[slotIndex].empty())
                {
                    std::string stem = std::filesystem::path(slots[slotIndex]).stem().string();
                    if (stem.rfind("m_", 0) == 0 && stem.size() > 2) return stem.substr(2);
                    return stem.empty() ? "material" : stem;
                }
            }
        }

        const std::string& p = n.materialSlots[slotIndex];
        if (p.empty()) return "material";
        std::string stem = std::filesystem::path(p).stem().string();
        if (stem.rfind("m_", 0) == 0 && stem.size() > 2) return stem.substr(2);
        return stem.empty() ? "material" : stem;
    }

    void AutoAssignMaterialSlotsFromMesh(StaticMesh3DNode& n)
    {
        if (n.materialSlots[0].empty() && !n.material.empty())
            n.materialSlots[0] = n.material;
        if (!n.materialSlots[0].empty() && n.material.empty())
            n.material = n.materialSlots[0];
    }
}
