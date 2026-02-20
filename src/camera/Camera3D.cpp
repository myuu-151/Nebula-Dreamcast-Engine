#include "Camera3D.h"

#include <algorithm>
#include <cmath>

namespace
{
    static Vec3 NormalizeVec3Safe(const Vec3& v, const Vec3& fallback)
    {
        float lenSq = v.x * v.x + v.y * v.y + v.z * v.z;
        if (lenSq <= 1.0e-12f) return fallback;
        float invLen = 1.0f / std::sqrt(lenSq);
        return Vec3{ v.x * invLen, v.y * invLen, v.z * invLen };
    }

    static Vec3 CrossVec3(const Vec3& a, const Vec3& b)
    {
        return Vec3{
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    static Mat4 Mat4Identity()
    {
        Mat4 r = {};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }
}

namespace NebulaCamera3D
{
    Basis BuildBasis(const Vec3& forwardHint, const Vec3& upHint)
    {
        Basis basis{};
        basis.forward = NormalizeVec3Safe(forwardHint, Vec3{ 0.0f, 0.0f, 1.0f });
        basis.up = NormalizeVec3Safe(upHint, Vec3{ 0.0f, 1.0f, 0.0f });
        basis.right = NormalizeVec3Safe(CrossVec3(basis.up, basis.forward), Vec3{ 1.0f, 0.0f, 0.0f });
        if (std::fabs(basis.right.x) < 1e-5f && std::fabs(basis.right.y) < 1e-5f && std::fabs(basis.right.z) < 1e-5f)
        {
            Vec3 fallbackUp = (std::fabs(basis.forward.y) < 0.99f) ? Vec3{ 0.0f, 1.0f, 0.0f } : Vec3{ 1.0f, 0.0f, 0.0f };
            basis.right = NormalizeVec3Safe(CrossVec3(fallbackUp, basis.forward), Vec3{ 1.0f, 0.0f, 0.0f });
        }
        basis.up = NormalizeVec3Safe(CrossVec3(basis.forward, basis.right), Vec3{ 0.0f, 1.0f, 0.0f });
        basis.right = NormalizeVec3Safe(CrossVec3(basis.up, basis.forward), Vec3{ 1.0f, 0.0f, 0.0f });
        return basis;
    }

    View BuildView(const Camera3DV2& camera)
    {
        View view{};
        view.eye = camera.position;
        view.basis = BuildBasis(camera.forward, camera.up);
        view.target = { view.eye.x + view.basis.forward.x, view.eye.y + view.basis.forward.y, view.eye.z + view.basis.forward.z };
        return view;
    }

    View BuildLookAtView(const Vec3& eye, const Vec3& target, const Vec3& up)
    {
        View view{};
        view.eye = eye;
        view.basis = BuildBasis(Vec3{ target.x - eye.x, target.y - eye.y, target.z - eye.z }, up);
        view.target = { view.eye.x + view.basis.forward.x, view.eye.y + view.basis.forward.y, view.eye.z + view.basis.forward.z };
        return view;
    }

    Projection BuildProjection(const Camera3DV2& camera, float aspect)
    {
        Projection proj{};
        proj.perspective = camera.perspective;
        proj.fovYDeg = std::clamp(camera.fovY, 5.0f, 170.0f);
        proj.fovYRad = proj.fovYDeg * 3.14159f / 180.0f;
        proj.aspect = std::max(aspect, 1.0e-4f);
        proj.nearZ = std::max(camera.nearZ, 0.001f);
        proj.farZ = std::max(camera.farZ, proj.nearZ + 0.01f);
        proj.orthoWidth = std::max(camera.orthoWidth, 0.01f);
        return proj;
    }

    Mat4 BuildViewMatrix(const View& view)
    {
        Mat4 r = Mat4Identity();
        const Basis& b = view.basis;
        r.m[0] = b.right.x; r.m[1] = b.up.x; r.m[2] = -b.forward.x;
        r.m[4] = b.right.y; r.m[5] = b.up.y; r.m[6] = -b.forward.y;
        r.m[8] = b.right.z; r.m[9] = b.up.z; r.m[10] = -b.forward.z;
        r.m[12] = -(b.right.x * view.eye.x + b.right.y * view.eye.y + b.right.z * view.eye.z);
        r.m[13] = -(b.up.x * view.eye.x + b.up.y * view.eye.y + b.up.z * view.eye.z);
        r.m[14] = (b.forward.x * view.eye.x + b.forward.y * view.eye.y + b.forward.z * view.eye.z);
        return r;
    }

    Mat4 BuildProjectionMatrix(const Projection& proj)
    {
        Mat4 r = {};
        if (proj.perspective)
        {
            float f = 1.0f / std::tanf(proj.fovYRad * 0.5f);
            r.m[0] = f / proj.aspect;
            r.m[5] = f;
            r.m[10] = (proj.farZ + proj.nearZ) / (proj.nearZ - proj.farZ);
            r.m[11] = -1.0f;
            r.m[14] = (2.0f * proj.farZ * proj.nearZ) / (proj.nearZ - proj.farZ);
        }
        else
        {
            float orthoHeight = proj.orthoWidth / proj.aspect;
            r = Mat4Identity();
            r.m[0] = 1.0f / proj.orthoWidth;
            r.m[5] = 1.0f / orthoHeight;
            r.m[10] = -2.0f / (proj.farZ - proj.nearZ);
            r.m[14] = -(proj.farZ + proj.nearZ) / (proj.farZ - proj.nearZ);
        }
        return r;
    }

    DreamcastExport BuildDreamcastExport(const Camera3DV2& camera, float aspect, const Vec3& targetOffset)
    {
        DreamcastExport out{};
        out.projection = BuildProjection(camera, aspect);
        out.view = BuildView(camera);
        out.view.target = {
            out.view.target.x + targetOffset.x,
            out.view.target.y + targetOffset.y,
            out.view.target.z + targetOffset.z
        };

        // Dreamcast runtime parity: mirror camera eye in X/Z, then rebuild basis toward target.
        out.view.eye.x = -out.view.eye.x;
        out.view.eye.z = -out.view.eye.z;

        out.view.basis = BuildBasis(
            Vec3{
                out.view.target.x - out.view.eye.x,
                out.view.target.y - out.view.eye.y,
                out.view.target.z - out.view.eye.z
            },
            out.view.basis.up);

        out.focalY = (out.viewH * 0.5f) / std::max(1.0e-4f, std::tanf(out.projection.fovYRad * 0.5f));
        out.focalX = out.focalY * out.projection.aspect;
        return out;
    }
}
