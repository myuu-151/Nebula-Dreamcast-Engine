#include "viewport_render.h"

#include <vector>
#include <fstream>
#include <cmath>
#include <string>
#include <unordered_map>

#include "../io/mesh_io.h"
#include "../io/meta_io.h"
#include "../math/math_utils.h"

extern std::unordered_map<std::string, GLuint> gNebTextureCache;

GLuint CreateCircleTexture(int size)
{
    std::vector<unsigned char> pixels(size * size * 4);
    float c = (size - 1) * 0.5f;
    float radius = c;
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            float dx = x - c;
            float dy = y - c;
            float d = sqrtf(dx * dx + dy * dy);
            float a = 1.0f - (d / radius);
            if (a < 0.0f) a = 0.0f;
            float alpha = a * a;
            int i = (y * size + x) * 4;
            pixels[i + 0] = 255;
            pixels[i + 1] = 255;
            pixels[i + 2] = 255;
            pixels[i + 3] = (unsigned char)(alpha * 255.0f);
        }
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return tex;
}

static GLuint LoadNebTexture(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::in);
    if (!in.is_open()) return 0;

    char magic[4];
    if (!in.read(magic, 4)) return 0;
    if (!(magic[0] == 'N' && magic[1] == 'E' && magic[2] == 'B' && magic[3] == 'T')) return 0;

    uint16_t w = 0, h = 0, format = 0, flags = 0;
    if (!ReadU16BE(in, w) || !ReadU16BE(in, h) || !ReadU16BE(in, format) || !ReadU16BE(in, flags)) return 0;
    if (format != 1) return 0; // RGB555

    std::vector<unsigned char> rgba(w * h * 4);
    for (uint32_t i = 0; i < (uint32_t)w * (uint32_t)h; ++i)
    {
        uint16_t rgb;
        if (!ReadU16BE(in, rgb)) return 0;
        uint8_t r5 = (rgb >> 10) & 0x1F;
        uint8_t g5 = (rgb >> 5) & 0x1F;
        uint8_t b5 = (rgb) & 0x1F;
        uint8_t r = (r5 << 3) | (r5 >> 2);
        uint8_t g = (g5 << 3) | (g5 >> 2);
        uint8_t b = (b5 << 3) | (b5 >> 2);
        rgba[i * 4 + 0] = r;
        rgba[i * 4 + 1] = g;
        rgba[i * 4 + 2] = b;
        rgba[i * 4 + 3] = 255;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    int filterMode = NebulaAssets::LoadNebTexFilterMode(path);
    GLint glFilter = (filterMode == 0) ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glFilter);
    int wrapMode = NebulaAssets::LoadNebTexWrapMode(path);
    GLint wrapS = GL_REPEAT;
    GLint wrapT = GL_REPEAT;
    if (wrapMode == 1 || wrapMode == 2)
    {
        wrapS = GL_CLAMP;
        wrapT = GL_CLAMP;
    }
    else if (wrapMode == 3)
    {
#ifdef GL_MIRRORED_REPEAT
        wrapS = GL_MIRRORED_REPEAT;
        wrapT = GL_MIRRORED_REPEAT;
#else
        wrapS = GL_REPEAT;
        wrapT = GL_REPEAT;
#endif
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    return tex;
}

GLuint GetNebTexture(const std::filesystem::path& path)
{
    std::string key = path.string();
    auto it = gNebTextureCache.find(key);
    if (it != gNebTextureCache.end()) return it->second;
    GLuint tex = LoadNebTexture(path);
    gNebTextureCache[key] = tex;
    return tex;
}

bool ProjectToScreenGL(const Vec3& world, float& outX, float& outY, float scaleX, float scaleY)
{
    float model[16], proj[16];
    int vp[4];
    glGetFloatv(GL_MODELVIEW_MATRIX, model);
    glGetFloatv(GL_PROJECTION_MATRIX, proj);
    glGetIntegerv(GL_VIEWPORT, vp);

    // Multiply proj * model * vec (column-major)
    float x = world.x, y = world.y, z = world.z;
    float mvx = model[0]*x + model[4]*y + model[8]*z + model[12];
    float mvy = model[1]*x + model[5]*y + model[9]*z + model[13];
    float mvz = model[2]*x + model[6]*y + model[10]*z + model[14];
    float mvw = model[3]*x + model[7]*y + model[11]*z + model[15];

    float px = proj[0]*mvx + proj[4]*mvy + proj[8]*mvz + proj[12]*mvw;
    float py = proj[1]*mvx + proj[5]*mvy + proj[9]*mvz + proj[13]*mvw;
    float pz = proj[2]*mvx + proj[6]*mvy + proj[10]*mvz + proj[14]*mvw;
    float pw = proj[3]*mvx + proj[7]*mvy + proj[11]*mvz + proj[15]*mvw;

    if (pw == 0.0f) return false;
    float ndcX = px / pw;
    float ndcY = py / pw;
    float ndcZ = pz / pw;
    if (ndcZ < -1.0f || ndcZ > 1.0f) return false;

    float sx = vp[0] + (ndcX * 0.5f + 0.5f) * (float)vp[2];
    float sy = vp[1] + (1.0f - (ndcY * 0.5f + 0.5f)) * (float)vp[3];

    // Convert framebuffer pixels to ImGui screen coords
    outX = sx / scaleX;
    outY = sy / scaleY;
    return true;
}

void GetLocalAxes(const Audio3DNode& n, Vec3& right, Vec3& up, Vec3& forward)
{
    GetLocalAxesFromEuler(n.rotX, n.rotY, n.rotZ, right, up, forward);
}

void QuatToGLMatrix(float qw, float qx, float qy, float qz, float m[16])
{
    float xx = qx*qx, yy = qy*qy, zz = qz*qz;
    float xy = qx*qy, xz = qx*qz, yz = qy*qz;
    float wx = qw*qx, wy = qw*qy, wz = qw*qz;
    // Column-major for OpenGL
    m[ 0] = 1 - 2*(yy+zz); m[ 1] = 2*(xy+wz);     m[ 2] = 2*(xz-wy);     m[ 3] = 0;
    m[ 4] = 2*(xy-wz);     m[ 5] = 1 - 2*(xx+zz); m[ 6] = 2*(yz+wx);     m[ 7] = 0;
    m[ 8] = 2*(xz+wy);     m[ 9] = 2*(yz-wx);     m[10] = 1 - 2*(xx+yy); m[11] = 0;
    m[12] = 0;              m[13] = 0;              m[14] = 0;              m[15] = 1;
}
