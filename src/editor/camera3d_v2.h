#pragma once

#include <string>

#include "math_types.h"

struct Camera3DV2
{
    std::string name;
    std::string parent;

    Vec3 position{ 0.0f, 2.0f, -6.0f };
    Vec3 forward{ 0.0f, 0.0f, 1.0f };
    Vec3 up{ 0.0f, 1.0f, 0.0f };

    bool perspective = true;
    float fovY = 70.0f;
    float nearZ = 0.25f;
    float farZ = 4096.0f;
    float orthoWidth = 12.8f;
    float priority = 0.0f;
    bool main = false;
};

struct Camera3DV2Basis
{
    Vec3 right{};
    Vec3 up{};
    Vec3 forward{};
};

struct Camera3DV2View
{
    Vec3 eye{};
    Vec3 target{};
    Camera3DV2Basis basis{};
};

struct Camera3DV2Projection
{
    bool perspective = true;
    float fovYDeg = 70.0f;
    float fovYRad = 70.0f * 3.14159f / 180.0f;
    float aspect = 1.0f;
    float nearZ = 0.25f;
    float farZ = 4096.0f;
    float orthoWidth = 12.8f;
};

// Camera3D v2 convention (canonical):
// - Right-handed basis in world space.
// - +Y is world up, +Z is camera forward.
// - Cross order is fixed:
//   right = normalize(cross(up, forward))
//   up    = normalize(cross(forward, right))
// Euler order (legacy migration only): R = Rz * Ry * Rx.
Camera3DV2Basis BuildCamera3DV2Basis(const Vec3& forwardHint, const Vec3& upHint);
Camera3DV2View BuildCamera3DV2View(const Camera3DV2& camera);
Camera3DV2Projection BuildCamera3DV2Projection(const Camera3DV2& camera, float aspect);
Mat4 BuildCamera3DV2ViewMatrix(const Camera3DV2View& view);
Mat4 BuildCamera3DV2ProjectionMatrix(const Camera3DV2Projection& proj);

// Legacy Camera3D migration helper (Euler order Rz * Ry * Rx).
Camera3DV2 BuildCamera3DV2FromLegacyEuler(
    const std::string& name,
    const std::string& parent,
    float x, float y, float z,
    float rotX, float rotY, float rotZ,
    bool perspective,
    float fovY,
    float nearZ,
    float farZ,
    float orthoWidth,
    float priority,
    bool main);
