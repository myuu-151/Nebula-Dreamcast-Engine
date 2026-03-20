#pragma once

#include <string>

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
