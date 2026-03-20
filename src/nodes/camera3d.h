#pragma once

#include "../core/math_types.h"
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

// ---------------------------------------------------------------------------
// Camera3DV2 — editor viewport camera
// ---------------------------------------------------------------------------
struct Camera3DV2
{
    std::string name;
    std::string parent;
    Vec3 position = { 0.0f, 2.0f, -6.0f };
    Vec3 forward = { 0.0f, 0.0f, 1.0f };
    Vec3 up = { 0.0f, 1.0f, 0.0f };
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
    Vec3 right = { 1.0f, 0.0f, 0.0f };
    Vec3 up = { 0.0f, 1.0f, 0.0f };
    Vec3 forward = { 0.0f, 0.0f, 1.0f };
};

struct Camera3DV2View
{
    Vec3 eye = {};
    Vec3 target = {};
    Camera3DV2Basis basis;
};

struct Camera3DV2Projection
{
    bool perspective = true;
    float fovYDeg = 70.0f;
    float fovYRad = 0.0f;
    float aspect = 1.0f;
    float nearZ = 0.25f;
    float farZ = 4096.0f;
    float orthoWidth = 12.8f;
};

Camera3DV2Basis BuildCamera3DV2Basis(const Vec3& forwardHint, const Vec3& upHint);
Camera3DV2View BuildCamera3DV2View(const Camera3DV2& camera);
Camera3DV2Projection BuildCamera3DV2Projection(const Camera3DV2& camera, float aspect);
Mat4 BuildCamera3DV2ViewMatrix(const Camera3DV2View& view);
Mat4 BuildCamera3DV2ProjectionMatrix(const Camera3DV2Projection& proj);

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

// ---------------------------------------------------------------------------
// NebulaCamera3D — runtime / Dreamcast export camera utilities
// ---------------------------------------------------------------------------
namespace NebulaCamera3D
{
    using Basis = Camera3DV2Basis;
    using View = Camera3DV2View;
    using Projection = Camera3DV2Projection;

    struct DreamcastExport
    {
        View view;
        Projection projection;
        float focalX = 0.0f;
        float focalY = 0.0f;
        float viewW = 640.0f;
        float viewH = 480.0f;
    };

    Basis BuildBasis(const Vec3& forwardHint, const Vec3& upHint);
    View BuildView(const Camera3DV2& camera);
    View BuildLookAtView(const Vec3& eye, const Vec3& target, const Vec3& up);
    Projection BuildProjection(const Camera3DV2& camera, float aspect);
    Mat4 BuildViewMatrix(const View& view);
    Mat4 BuildProjectionMatrix(const Projection& proj);
    DreamcastExport BuildDreamcastExport(const Camera3DV2& camera, float aspect, const Vec3& targetOffset);
}
