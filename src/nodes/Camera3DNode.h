#pragma once

#include <string>

// ---------------------------------------------------------------------------
// Camera3DNode — scene-serialized camera data
// ---------------------------------------------------------------------------
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
