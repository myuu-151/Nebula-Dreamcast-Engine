#include "math_utils.h"
#include "../camera/camera3d.h"
#include <cmath>
#include <cfloat>
#include <algorithm>

// ── Matrix utilities ──

Mat4 Mat4Identity()
{
    Mat4 r = {};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

Mat4 Mat4Multiply(const Mat4& a, const Mat4& b)
{
    Mat4 r = {};
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            r.m[row * 4 + col] =
                a.m[row * 4 + 0] * b.m[0 * 4 + col] +
                a.m[row * 4 + 1] * b.m[1 * 4 + col] +
                a.m[row * 4 + 2] * b.m[2 * 4 + col] +
                a.m[row * 4 + 3] * b.m[3 * 4 + col];
        }
    }
    return r;
}

Mat4 Mat4Perspective(float fovyRadians, float aspect, float znear, float zfar)
{
    float f = 1.0f / tanf(fovyRadians * 0.5f);
    Mat4 r = {};
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zfar + znear) / (znear - zfar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zfar * znear) / (znear - zfar);
    return r;
}

Mat4 Mat4Orthographic(float left, float right, float bottom, float top, float znear, float zfar)
{
    Mat4 r = Mat4Identity();
    r.m[0] = 2.0f / (right - left);
    r.m[5] = 2.0f / (top - bottom);
    r.m[10] = -2.0f / (zfar - znear);
    r.m[12] = -(right + left) / (right - left);
    r.m[13] = -(top + bottom) / (top - bottom);
    r.m[14] = -(zfar + znear) / (zfar - znear);
    return r;
}

Mat4 Mat4LookAt(const Vec3& eye, const Vec3& target, const Vec3& up)
{
    return NebulaCamera3D::BuildViewMatrix(NebulaCamera3D::BuildLookAtView(eye, target, up));
}

// ── Matrix-vector multiply ──

void MulMat4Vec4(const Mat4& m, float x, float y, float z, float w, float& ox, float& oy, float& oz, float& ow)
{
    // Column-major (OpenGL)
    ox = m.m[0] * x + m.m[4] * y + m.m[8]  * z + m.m[12] * w;
    oy = m.m[1] * x + m.m[5] * y + m.m[9]  * z + m.m[13] * w;
    oz = m.m[2] * x + m.m[6] * y + m.m[10] * z + m.m[14] * w;
    ow = m.m[3] * x + m.m[7] * y + m.m[11] * z + m.m[15] * w;
}

// ── Projection ──

bool ProjectToScreen(const Vec3& world, const Mat4& view, const Mat4& proj, int w, int h, float& outX, float& outY)
{
    float vx, vy, vz, vw;
    MulMat4Vec4(view, world.x, world.y, world.z, 1.0f, vx, vy, vz, vw);
    float px, py, pz, pw;
    MulMat4Vec4(proj, vx, vy, vz, 1.0f, px, py, pz, pw);
    if (pw == 0.0f) return false;
    float ndcX = px / pw;
    float ndcY = py / pw;
    float ndcZ = pz / pw;
    if (ndcZ < -1.0f || ndcZ > 1.0f) return false;
    outX = (ndcX * 0.5f + 0.5f) * (float)w;
    outY = (1.0f - (ndcY * 0.5f + 0.5f)) * (float)h;
    return true;
}

// ── Euler / axis helpers ──

void GetLocalAxesFromEuler(float rotX, float rotY, float rotZ, Vec3& right, Vec3& up, Vec3& forward)
{
    float rx = rotX * 3.14159f / 180.0f;
    float ry = rotY * 3.14159f / 180.0f;
    float rz = rotZ * 3.14159f / 180.0f;

    float cx = cosf(rx), sx = sinf(rx);
    float cy = cosf(ry), sy = sinf(ry);
    float cz = cosf(rz), sz = sinf(rz);

    // R = Rz * Ry * Rx
    float m00 = cy * cz;
    float m01 = cz * sx * sy - cx * sz;
    float m02 = sx * sz + cx * cz * sy;

    float m10 = cy * sz;
    float m11 = cx * cz + sx * sy * sz;
    float m12 = cx * sy * sz - cz * sx;

    float m20 = -sy;
    float m21 = cy * sx;
    float m22 = cx * cy;

    right   = { m00, m10, m20 };
    up      = { m01, m11, m21 };
    forward = { m02, m12, m22 };
}

// ── Quaternion utilities ──

Quat QuatFromAxisAngle(float ax, float ay, float az, float angleDeg)
{
    float half = angleDeg * 3.14159265f / 180.0f * 0.5f;
    float s = sinf(half);
    return { cosf(half), ax * s, ay * s, az * s };
}

Quat QuatMultiply(Quat a, Quat b)
{
    return {
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z,
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w
    };
}

void QuatNormalize(Quat& q)
{
    float len = sqrtf(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z);
    if (len > 0.0001f) { q.w /= len; q.x /= len; q.y /= len; q.z /= len; }
}

void QuatToEuler(float qw, float qx, float qy, float qz, float& rotX, float& rotY, float& rotZ)
{
    // ZYX Euler extraction matching R = Rz * Ry * Rx convention
    const float kDeg = 180.0f / 3.14159265f;
    float m20 = 2.0f * (qx * qz - qw * qy);
    float sy = -m20;
    if (sy > 0.9999f) sy = 0.9999f;
    if (sy < -0.9999f) sy = -0.9999f;
    rotY = asinf(sy) * kDeg;
    float m21 = 2.0f * (qy * qz + qw * qx);
    float m22 = 1.0f - 2.0f * (qx * qx + qy * qy);
    rotX = atan2f(m21, m22) * kDeg;
    float m10 = 2.0f * (qx * qy + qw * qz);
    float m00 = 1.0f - 2.0f * (qy * qy + qz * qz);
    rotZ = atan2f(m10, m00) * kDeg;
}

Quat EulerToQuat(float rotX, float rotY, float rotZ)
{
    // R = Rz * Ry * Rx -> q = qZ * qY * qX
    Quat qX = QuatFromAxisAngle(1, 0, 0, rotX);
    Quat qY = QuatFromAxisAngle(0, 1, 0, rotY);
    Quat qZ = QuatFromAxisAngle(0, 0, 1, rotZ);
    return QuatMultiply(QuatMultiply(qZ, qY), qX);
}

void SyncNode3DQuatFromEuler(Node3DNode& n)
{
    Quat q = EulerToQuat(n.rotX, n.rotY, n.rotZ);
    n.qw = q.w; n.qx = q.x; n.qy = q.y; n.qz = q.z;
}

void SyncNode3DEulerFromQuat(Node3DNode& n)
{
    // Standard ZYX Euler extraction -- matches glRotatef Rz*Ry*Rx convention
    QuatToEuler(n.qw, n.qx, n.qy, n.qz, n.rotX, n.rotY, n.rotZ);
}

float QuatYawDeg(float qw, float qx, float qy, float qz)
{
    // Rotate (0,0,1) by the quaternion to get the forward vector,
    // then extract yaw = atan2(forward.x, forward.z).
    // forward = q * (0,0,1) * q^-1
    float fx = 2.0f * (qx * qz + qw * qy);
    float fz = 1.0f - 2.0f * (qx * qx + qy * qy);
    return atan2f(fx, fz) * (180.0f / 3.14159265f);
}

Quat QuatFromNormalAndYaw(float nx, float ny, float nz, float yawDeg)
{
    // Build orientation from surface normal (desired up) and yaw angle
    float len = sqrtf(nx*nx + ny*ny + nz*nz);
    if (len < 0.0001f) return {1, 0, 0, 0};
    float ux = nx/len, uy = ny/len, uz = nz/len;

    // Yaw direction on XZ plane
    float yawRad = yawDeg * 3.14159265f / 180.0f;
    float fwdX = sinf(yawRad);
    float fwdZ = cosf(yawRad);

    // Project forward onto surface plane: F' = F - dot(F, N)*N
    float dot = fwdX * ux + fwdZ * uz;
    float pfx = fwdX - dot * ux;
    float pfy = -dot * uy;
    float pfz = fwdZ - dot * uz;
    float pfLen = sqrtf(pfx*pfx + pfy*pfy + pfz*pfz);
    if (pfLen < 0.0001f)
        return EulerToQuat(0.0f, yawDeg, 0.0f);
    pfx /= pfLen; pfy /= pfLen; pfz /= pfLen;

    // Right = cross(up, forward)
    float rx = uy * pfz - uz * pfy;
    float ry = uz * pfx - ux * pfz;
    float rz = ux * pfy - uy * pfx;
    float rLen = sqrtf(rx*rx + ry*ry + rz*rz);
    if (rLen > 0.0001f) { rx /= rLen; ry /= rLen; rz /= rLen; }

    // Rotation matrix columns: [right, up, forward]
    float m00=rx, m01=ux, m02=pfx;
    float m10=ry, m11=uy, m12=pfy;
    float m20=rz, m21=uz, m22=pfz;

    // Matrix to quaternion (Shepperd's method)
    float trace = m00 + m11 + m22;
    Quat q;
    if (trace > 0)
    {
        float s = 0.5f / sqrtf(trace + 1.0f);
        q.w = 0.25f / s;
        q.x = (m21 - m12) * s;
        q.y = (m02 - m20) * s;
        q.z = (m10 - m01) * s;
    }
    else if (m00 > m11 && m00 > m22)
    {
        float s = 2.0f * sqrtf(1.0f + m00 - m11 - m22);
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    }
    else if (m11 > m22)
    {
        float s = 2.0f * sqrtf(1.0f + m11 - m00 - m22);
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    }
    else
    {
        float s = 2.0f * sqrtf(1.0f + m22 - m00 - m11);
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }
    QuatNormalize(q);
    return q;
}

void QuatNlerp(Quat& cur, const Quat& target, float t)
{
    // Ensure shortest path
    float dot = cur.w*target.w + cur.x*target.x + cur.y*target.y + cur.z*target.z;
    float tw = target.w, tx = target.x, ty = target.y, tz = target.z;
    if (dot < 0) { tw = -tw; tx = -tx; ty = -ty; tz = -tz; }
    cur.w += (tw - cur.w) * t;
    cur.x += (tx - cur.x) * t;
    cur.y += (ty - cur.y) * t;
    cur.z += (tz - cur.z) * t;
    QuatNormalize(cur);
}
