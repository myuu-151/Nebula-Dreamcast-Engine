#pragma once

// Per-node type headers
#include "Audio3DNode.h"
#include "Camera3DNode.h"
#include "../camera/camera3d.h"
#include "NavMesh3DNode.h"
#include "Node3DNode.h"
#include "StaticMesh3DNode.h"

#include <filesystem>
#include <string>
#include <vector>

struct SceneData
{
    std::filesystem::path path;
    std::string name;
    std::vector<Audio3DNode> nodes;
    std::vector<StaticMesh3DNode> staticMeshes;
    std::vector<Camera3DNode> cameras;
    std::vector<Node3DNode> node3d;
    std::vector<NavMesh3DNode> navMeshes;
};

namespace NebulaNodes
{
    int FindStaticMeshByName(const std::vector<StaticMesh3DNode>& staticMeshNodes, const std::string& name);
    int FindNode3DByName(const std::vector<Node3DNode>& node3DNodes, const std::string& name);

    bool TryGetParentByNodeName(
        const std::vector<Audio3DNode>& audioNodes,
        const std::vector<StaticMesh3DNode>& staticMeshNodes,
        const std::vector<Camera3DNode>& cameraNodes,
        const std::vector<Node3DNode>& node3DNodes,
        const std::string& name,
        std::string& outParent);
    bool WouldCreateHierarchyCycle(
        const std::vector<Audio3DNode>& audioNodes,
        const std::vector<StaticMesh3DNode>& staticMeshNodes,
        const std::vector<Camera3DNode>& cameraNodes,
        const std::vector<Node3DNode>& node3DNodes,
        const std::string& childName,
        const std::string& candidateParentName);
    bool StaticMeshCreatesCycle(const std::vector<StaticMesh3DNode>& staticMeshNodes, int childIdx, int candidateParentIdx);
    bool Node3DCreatesCycle(const std::vector<Node3DNode>& node3DNodes, int childIdx, int candidateParentIdx);

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
        float& osz);
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
        float& osz);
    // Quaternion variant — outputs world rotation as quaternion (no Euler gimbal lock)
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
        float& osz);

    std::string GetStaticMeshPrimaryMaterial(const StaticMesh3DNode& n);
    std::string GetStaticMeshMaterialByIndex(const StaticMesh3DNode& n, int matIndex);
    std::string GetStaticMeshSlotLabel(const StaticMesh3DNode& n, int slotIndex, const std::string& projectDir);
    void AutoAssignMaterialSlotsFromMesh(StaticMesh3DNode& n);
}
