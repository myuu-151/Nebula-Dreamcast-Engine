#pragma once

#include "../editor/math_types.h"
#include "../editor/camera3d_v2.h"

namespace NebulaCamera3D
{
    struct Basis
    {
        Vec3 right{};
        Vec3 up{};
        Vec3 forward{};
    };

    struct View
    {
        Vec3 eye{};
        Vec3 target{};
        Basis basis{};
    };

    struct Projection
    {
        bool perspective = true;
        float fovYDeg = 70.0f;
        float fovYRad = 70.0f * 3.14159f / 180.0f;
        float aspect = 1.0f;
        float nearZ = 0.25f;
        float farZ = 4096.0f;
        float orthoWidth = 12.8f;
    };

    struct DreamcastExport
    {
        View view{};
        Projection projection{};
        float viewW = 640.0f;
        float viewH = 570.0f;
        float focalX = 420.0f;
        float focalY = 420.0f;
    };

    Basis BuildBasis(const Vec3& forwardHint, const Vec3& upHint);
    View BuildView(const Camera3DV2& camera);
    View BuildLookAtView(const Vec3& eye, const Vec3& target, const Vec3& up);
    Projection BuildProjection(const Camera3DV2& camera, float aspect);
    Mat4 BuildViewMatrix(const View& view);
    Mat4 BuildProjectionMatrix(const Projection& proj);
    DreamcastExport BuildDreamcastExport(const Camera3DV2& camera, float aspect, const Vec3& targetOffset);
}
