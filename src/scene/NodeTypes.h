#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include "../audio3d.h"

constexpr int kStaticMeshMaterialSlots = 14;

struct StaticMesh3DNode
{
    std::string name;
    std::string parent;
    std::string script;
    std::string material;
    std::array<std::string, kStaticMeshMaterialSlots> materialSlots{};
    int materialSlot = 0;
    std::string mesh;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float rotX = 0.0f;
    float rotY = 0.0f;
    float rotZ = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float scaleZ = 1.0f;
    bool collisionSource = false;
};

struct Camera3DNode
{
    std::string name;
    std::string parent;
    float x = 0.0f;
    float y = 2.0f;
    float z = -6.0f;
    float rotX = 0.0f;
    float rotY = 0.0f;
    float rotZ = 0.0f;
    bool perspective = true;
    float fovY = 70.0f;
    float nearZ = 0.25f;
    float farZ = 4096.0f;
    float orthoWidth = 12.8f;
    float priority = 0.0f;
    bool main = false;
};

struct Node3DNode
{
    std::string name;
    std::string parent;
    std::string script;
    std::string primitiveMesh = "assets/cube_primitive.nebmesh";
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float rotX = 0.0f;
    float rotY = 0.0f;
    float rotZ = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float scaleZ = 1.0f;
    bool collisionSource = false;
    bool physicsEnabled = false;
    float velY = 0.0f;
};

struct SceneData
{
    std::filesystem::path path;
    std::string name;
    std::vector<Audio3DNode> nodes;
    std::vector<StaticMesh3DNode> staticMeshes;
    std::vector<Camera3DNode> cameras;
    std::vector<Node3DNode> node3d;
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

    std::string GetStaticMeshPrimaryMaterial(const StaticMesh3DNode& n);
    std::string GetStaticMeshMaterialByIndex(const StaticMesh3DNode& n, int matIndex);
    std::string GetStaticMeshSlotLabel(const StaticMesh3DNode& n, int slotIndex, const std::string& projectDir);
    void AutoAssignMaterialSlotsFromMesh(StaticMesh3DNode& n);
}
