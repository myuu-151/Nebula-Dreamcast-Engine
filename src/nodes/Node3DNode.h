#pragma once

#include <string>

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
