#pragma once

#include <filesystem>
#include "../math/math_types.h"
#include "../nodes/NodeTypes.h"

#define NOMINMAX
#include <Windows.h>
#include <GL/gl.h>

GLuint CreateCircleTexture(int size);
GLuint GetNebTexture(const std::filesystem::path& path);
bool ProjectToScreenGL(const Vec3& world, float& outX, float& outY, float scaleX, float scaleY);
void GetLocalAxes(const Audio3DNode& n, Vec3& right, Vec3& up, Vec3& forward);
void QuatToGLMatrix(float qw, float qx, float qy, float qz, float m[16]);
