#pragma once

#include "math_types.h"
#include "../nodes/NodeTypes.h"

// ── Quaternion type ──

struct Quat { float w, x, y, z; };

// ── Matrix utilities ──

Mat4 Mat4Identity();
Mat4 Mat4Multiply(const Mat4& a, const Mat4& b);
Mat4 Mat4Perspective(float fovyRadians, float aspect, float znear, float zfar);
Mat4 Mat4Orthographic(float left, float right, float bottom, float top, float znear, float zfar);
Mat4 Mat4LookAt(const Vec3& eye, const Vec3& target, const Vec3& up);

// ── Matrix-vector multiply ──

void MulMat4Vec4(const Mat4& m, float x, float y, float z, float w, float& ox, float& oy, float& oz, float& ow);

// ── Projection ──

bool ProjectToScreen(const Vec3& world, const Mat4& view, const Mat4& proj, int w, int h, float& outX, float& outY);

// ── Euler / axis helpers ──

void GetLocalAxesFromEuler(float rotX, float rotY, float rotZ, Vec3& right, Vec3& up, Vec3& forward);

// ── Quaternion utilities ──

Quat QuatFromAxisAngle(float ax, float ay, float az, float angleDeg);
Quat QuatMultiply(Quat a, Quat b);
void QuatNormalize(Quat& q);
void QuatToEuler(float qw, float qx, float qy, float qz, float& rotX, float& rotY, float& rotZ);
Quat EulerToQuat(float rotX, float rotY, float rotZ);
void SyncNode3DQuatFromEuler(Node3DNode& n);
void SyncNode3DEulerFromQuat(Node3DNode& n);
float QuatYawDeg(float qw, float qx, float qy, float qz);
Quat QuatFromNormalAndYaw(float nx, float ny, float nz, float yawDeg);
void QuatNlerp(Quat& cur, const Quat& target, float t);
