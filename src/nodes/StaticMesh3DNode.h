#pragma once

#include <array>
#include <string>
#include <vector>

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
    bool animPreload = true;
};
