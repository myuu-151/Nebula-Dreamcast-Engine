#pragma once

#include "math_types.h"
#include <string>

// ---------------------------------------------------------------------------
// Camera3D — editor viewport camera (orientation-based, forward/up vectors)
// ---------------------------------------------------------------------------
struct Camera3D
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

struct Camera3DBasis
{
    Vec3 right = { 1.0f, 0.0f, 0.0f };
    Vec3 up = { 0.0f, 1.0f, 0.0f };
    Vec3 forward = { 0.0f, 0.0f, 1.0f };
};

struct Camera3DView
{
    Vec3 eye = {};
    Vec3 target = {};
    Camera3DBasis basis;
};

struct Camera3DProjection
{
    bool perspective = true;
    float fovYDeg = 70.0f;
    float fovYRad = 0.0f;
    float aspect = 1.0f;
    float nearZ = 0.25f;
    float farZ = 4096.0f;
    float orthoWidth = 12.8f;
};

Camera3DBasis BuildCamera3DBasis(const Vec3& forwardHint, const Vec3& upHint);
Camera3DView BuildCamera3DView(const Camera3D& camera);
Camera3DProjection BuildCamera3DProjection(const Camera3D& camera, float aspect);
Mat4 BuildCamera3DViewMatrix(const Camera3DView& view);
Mat4 BuildCamera3DProjectionMatrix(const Camera3DProjection& proj);

Camera3D BuildCamera3DFromLegacyEuler(
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
    using Basis = Camera3DBasis;
    using View = Camera3DView;
    using Projection = Camera3DProjection;

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
    View BuildView(const Camera3D& camera);
    View BuildLookAtView(const Vec3& eye, const Vec3& target, const Vec3& up);
    Projection BuildProjection(const Camera3D& camera, float aspect);
    Mat4 BuildViewMatrix(const View& view);
    Mat4 BuildProjectionMatrix(const Projection& proj);
    DreamcastExport BuildDreamcastExport(const Camera3D& camera, float aspect, const Vec3& targetOffset);
}
