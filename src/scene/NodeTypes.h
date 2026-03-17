#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include "../audio3d.h"

constexpr int kStaticMeshMaterialSlots = 14;
constexpr int kStaticMeshAnimSlots = 8;

struct AnimSlot
{
    std::string name;
    std::string path;
    float speed = 1.0f;
    bool loop = true;
};

struct StaticMesh3DNode
{
    std::string name;
    std::string parent;
    std::string script;
    std::string material;
    std::array<std::string, kStaticMeshMaterialSlots> materialSlots{};
    int materialSlot = 0;
    std::string mesh;
    std::string vtxAnim;
    std::array<AnimSlot, kStaticMeshAnimSlots> animSlots{};
    int animSlotCount = 0;
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
    bool runtimeTest = false;
    bool navmeshReady = false;
    float wallThreshold = 0.7f;
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
    // Local orbit offset around inherited/root pivot (position offset, not transform parenting)
    float orbitX = 0.0f;
    float orbitY = 0.0f;
    float orbitZ = 0.0f;
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
    // Quaternion orientation (source of truth for slope alignment).
    // rotX/rotY/rotZ are cached Euler for UI, serialization, and script compat.
    float qw = 1.0f, qx = 0.0f, qy = 0.0f, qz = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float scaleZ = 1.0f;
    bool simpleCollision = false;
    bool collisionSource = false;
    bool physicsEnabled = false;
    // Collision-only bounds extents (does not affect transform hierarchy)
    float extentX = 0.5f;
    float extentY = 0.5f;
    float extentZ = 0.5f;
    // Collision-only local bounds offset (does not affect transform hierarchy)
    float boundPosX = 0.0f;
    float boundPosY = 0.0f;
    float boundPosZ = 0.0f;
    float velY = 0.0f;
};

struct NavMesh3DNode
{
    std::string name;
    std::string parent;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float rotX = 0.0f;
    float rotY = 0.0f;
    float rotZ = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float scaleZ = 1.0f;
    // Extents of the bounding box (full size, not half-extents)
    float extentX = 10.0f;
    float extentY = 10.0f;
    float extentZ = 10.0f;
    bool navBounds = true;
    bool navNegator = false;
    bool cullWalls = false;
    float wallCullThreshold = 0.2f;
    float wireR = 0.1f;
    float wireG = 1.0f;
    float wireB = 0.25f;
    float wireThickness = 1.0f;
};

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
