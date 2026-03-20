// ---------------------------------------------------------------------------
// mesh_io.cpp — Mesh I/O, binary helpers, and mesh cache
// Extracted from main.cpp.
// ---------------------------------------------------------------------------

#include "mesh_io.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <cmath>
#include <algorithm>
#include <unordered_set>
#include <array>
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Extern globals defined in main.cpp
// ---------------------------------------------------------------------------
extern int gImportBasisMode;

// ---------------------------------------------------------------------------
// Global caches (definitions)
// ---------------------------------------------------------------------------
std::unordered_map<std::string, NebMesh> gNebMeshCache;
std::unordered_map<std::string, GLuint>  gNebTextureCache;

// ---------------------------------------------------------------------------
// Binary I/O helpers
// ---------------------------------------------------------------------------

void WriteU32BE(std::ofstream& out, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)((v >> 24) & 0xFF), (uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 8) & 0xFF), (uint8_t)(v & 0xFF) };
    out.write((const char*)b, 4);
}

void WriteU16BE(std::ofstream& out, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)((v >> 8) & 0xFF), (uint8_t)(v & 0xFF) };
    out.write((const char*)b, 2);
}

bool ReadU32BE(std::ifstream& in, uint32_t& outVal)
{
    uint8_t b[4];
    if (!in.read((char*)b, 4)) return false;
    outVal = (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | uint32_t(b[3]);
    return true;
}

bool ReadU16BE(std::ifstream& in, uint16_t& outVal)
{
    uint8_t b[2];
    if (!in.read((char*)b, 2)) return false;
    outVal = (uint16_t(b[0]) << 8) | uint16_t(b[1]);
    return true;
}

bool ReadS16BE(std::ifstream& in, int16_t& outVal)
{
    uint16_t v;
    if (!ReadU16BE(in, v)) return false;
    outVal = (int16_t)v;
    return true;
}

bool ReadS32BE(std::ifstream& in, int32_t& outVal)
{
    uint32_t v;
    if (!ReadU32BE(in, v)) return false;
    outVal = (int32_t)v;
    return true;
}

void WriteS32BE(std::ofstream& out, int32_t v)
{
    uint8_t b[4] = { (uint8_t)((v >> 24) & 0xFF), (uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 8) & 0xFF), (uint8_t)(v & 0xFF) };
    out.write((const char*)b, 4);
}

void WriteS16BE(std::ofstream& out, int16_t v)
{
    uint8_t b[2] = { (uint8_t)((v >> 8) & 0xFF), (uint8_t)(v & 0xFF) };
    out.write((const char*)b, 2);
}

uint16_t ToU16Clamped(uint32_t v)
{
    return (v > 0xFFFFu) ? 0xFFFFu : (uint16_t)v;
}

int16_t ToS16Fixed8_8(float v)
{
    float scaled = v * 256.0f;
    if (scaled > 32767.0f) scaled = 32767.0f;
    if (scaled < -32768.0f) scaled = -32768.0f;
    return (int16_t)std::lround(scaled);
}

int32_t ToFixed16_16(float v)
{
    const float scale = 65536.0f;
    float clamped = std::fmax(std::fmin(v, 32767.0f), -32768.0f);
    return (int32_t)std::lround(clamped * scale);
}

float FromFixed16_16(int32_t v)
{
    return (float)v / 65536.0f;
}

// ---------------------------------------------------------------------------
// Import-basis helpers
// ---------------------------------------------------------------------------

Vec3 ApplyImportBasis(const Vec3& v)
{
    Vec3 b;
    switch (gImportBasisMode)
    {
    case 1: // Blender (-Z forward, Y up) -> Nebula basis (flipped X to match viewport)
        b = Vec3{ -v.z, v.y, -v.x }; break;
    case 2: // Maya-style (+Z forward, Y up, flipped X to match viewport)
        b = Vec3{ -v.y, -v.x, v.z }; break;
    default:
        b = v; break;
    }
    // Bake 90-degree rotation matching standalone StaticMesh3D rotX=90 (GL Y-axis)
    // so FBX meshes face correct direction at 0,0,0. R_Y(90): (x,y,z) -> (z, y, -x)
    return Vec3{ b.z, b.y, -b.x };
}

uint8_t ComputeFaceWindingHint(const Vec3& a, const Vec3& b, const Vec3& c)
{
    // Sign of projected area on XY after import-basis transform.
    const float x1 = b.x - a.x;
    const float y1 = b.y - a.y;
    const float x2 = c.x - a.x;
    const float y2 = c.y - a.y;
    const float area2 = x1 * y2 - y1 * x2;
    return (area2 >= 0.0f) ? 1u : 0u;
}

// ---------------------------------------------------------------------------
// CleanupNebMeshTopology — merge duplicate vertices, remove degenerate tris
// ---------------------------------------------------------------------------

void CleanupNebMeshTopology(
    NebMesh& mesh,
    std::vector<uint32_t>* provenanceMeshIndices,
    std::vector<uint32_t>* provenanceVertexIndices)
{
    if (mesh.positions.empty())
    {
        if (provenanceMeshIndices) provenanceMeshIndices->clear();
        if (provenanceVertexIndices) provenanceVertexIndices->clear();
        return;
    }

    const float posEps = 1.0f / 512.0f;
    const float uvEps = 1.0f / 512.0f;
    const float posEps2 = posEps * posEps;

    auto quant = [](float v, float e) -> int32_t { return (int32_t)std::lround(v / e); };
    auto pack3 = [](int32_t x, int32_t y, int32_t z) -> uint64_t {
        uint64_t ux = (uint32_t)x;
        uint64_t uy = (uint32_t)y;
        uint64_t uz = (uint32_t)z;
        uint64_t h = 1469598103934665603ull;
        h ^= ux + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h ^= uy + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h ^= uz + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        return h;
    };

    std::unordered_map<uint64_t, std::vector<uint16_t>> buckets;
    buckets.reserve(mesh.positions.size() * 2 + 1);

    std::vector<Vec3> newPos;
    std::vector<Vec3> newUv;
    std::vector<Vec3> newUv1;
    newPos.reserve(mesh.positions.size());
    if (mesh.hasUv && mesh.uvs.size() == mesh.positions.size()) newUv.reserve(mesh.uvs.size());
    if (mesh.hasUv1 && mesh.uvs1.size() == mesh.positions.size()) newUv1.reserve(mesh.uvs1.size());

    std::vector<uint16_t> remap(mesh.positions.size(), 0);

    const bool trackProvenance =
        provenanceMeshIndices &&
        provenanceVertexIndices &&
        provenanceMeshIndices->size() == mesh.positions.size() &&
        provenanceVertexIndices->size() == mesh.positions.size();
    std::vector<uint32_t> newProvMesh;
    std::vector<uint32_t> newProvVert;
    if (trackProvenance)
    {
        newProvMesh.reserve(mesh.positions.size());
        newProvVert.reserve(mesh.positions.size());
    }

    for (size_t i = 0; i < mesh.positions.size(); ++i)
    {
        const Vec3& p = mesh.positions[i];
        const int32_t qx = quant(p.x, posEps);
        const int32_t qy = quant(p.y, posEps);
        const int32_t qz = quant(p.z, posEps);
        const uint64_t key = pack3(qx, qy, qz);

        uint16_t chosen = 0xFFFFu;
        auto it = buckets.find(key);
        if (it != buckets.end())
        {
            for (uint16_t cand : it->second)
            {
                const Vec3& cp = newPos[cand];
                float dx = cp.x - p.x, dy = cp.y - p.y, dz = cp.z - p.z;
                if ((dx * dx + dy * dy + dz * dz) > posEps2) continue;

                if (mesh.hasUv && mesh.uvs.size() == mesh.positions.size() && cand < newUv.size())
                {
                    const Vec3& u0 = mesh.uvs[i];
                    const Vec3& u1 = newUv[cand];
                    if (std::fabs(u0.x - u1.x) > uvEps || std::fabs(u0.y - u1.y) > uvEps)
                        continue;
                }

                if (mesh.hasUv1 && mesh.uvs1.size() == mesh.positions.size() && cand < newUv1.size())
                {
                    const Vec3& u0 = mesh.uvs1[i];
                    const Vec3& u1 = newUv1[cand];
                    if (std::fabs(u0.x - u1.x) > uvEps || std::fabs(u0.y - u1.y) > uvEps)
                        continue;
                }

                chosen = cand;
                break;
            }
        }

        if (chosen == 0xFFFFu)
        {
            chosen = (uint16_t)newPos.size();
            newPos.push_back(p);
            if (mesh.hasUv && mesh.uvs.size() == mesh.positions.size()) newUv.push_back(mesh.uvs[i]);
            if (mesh.hasUv1 && mesh.uvs1.size() == mesh.positions.size()) newUv1.push_back(mesh.uvs1[i]);
            buckets[key].push_back(chosen);
            if (trackProvenance)
            {
                newProvMesh.push_back((*provenanceMeshIndices)[i]);
                newProvVert.push_back((*provenanceVertexIndices)[i]);
            }
        }

        remap[i] = chosen;
    }

    if (!newPos.empty() && newPos.size() < mesh.positions.size())
    {
        mesh.positions.swap(newPos);
        if (mesh.hasUv && mesh.uvs.size() == remap.size()) mesh.uvs.swap(newUv);
        if (mesh.hasUv1 && mesh.uvs1.size() == remap.size()) mesh.uvs1.swap(newUv1);
    }

    if (trackProvenance)
    {
        provenanceMeshIndices->swap(newProvMesh);
        provenanceVertexIndices->swap(newProvVert);
    }

    for (size_t i = 0; i < mesh.indices.size(); ++i)
    {
        uint16_t idx = mesh.indices[i];
        if (idx < remap.size()) mesh.indices[i] = remap[idx];
    }

    if (mesh.hasFaceRecords)
    {
        for (auto& fr : mesh.faceRecords)
        {
            const int ar = (fr.arity >= 3 && fr.arity <= 4) ? (int)fr.arity : 3;
            for (int ci = 0; ci < ar; ++ci)
            {
                uint16_t idx = fr.indices[ci];
                if (idx < remap.size()) fr.indices[ci] = remap[idx];
            }
        }
    }

    // Remove exact duplicate/degenerate triangles after weld (preserve first occurrence).
    auto triKey = [](uint16_t a, uint16_t b, uint16_t c, uint16_t mat) -> uint64_t {
        if (a > b) std::swap(a, b);
        if (b > c) std::swap(b, c);
        if (a > b) std::swap(a, b);
        uint64_t h = 1469598103934665603ull;
        h ^= (uint64_t)a + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h ^= (uint64_t)b + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h ^= (uint64_t)c + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h ^= (uint64_t)mat + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        return h;
    };

    std::vector<uint16_t> newIdx;
    std::vector<uint16_t> newMat;
    newIdx.reserve(mesh.indices.size());
    if (mesh.hasFaceMaterial) newMat.reserve(mesh.faceMaterial.size());

    std::unordered_set<uint64_t> seenTri;
    seenTri.reserve(mesh.indices.size() / 3 + 1);

    const size_t triCount = mesh.indices.size() / 3;
    for (size_t t = 0; t < triCount; ++t)
    {
        uint16_t a = mesh.indices[t * 3 + 0];
        uint16_t b = mesh.indices[t * 3 + 1];
        uint16_t c = mesh.indices[t * 3 + 2];
        if (a == b || b == c || c == a) continue;

        uint16_t mat = (mesh.hasFaceMaterial && t < mesh.faceMaterial.size()) ? mesh.faceMaterial[t] : 0;
        uint64_t key = triKey(a, b, c, mat);
        if (!seenTri.insert(key).second) continue;

        newIdx.push_back(a); newIdx.push_back(b); newIdx.push_back(c);
        if (mesh.hasFaceMaterial) newMat.push_back(mat);
    }

    if (!newIdx.empty() && newIdx.size() < mesh.indices.size())
    {
        mesh.indices.swap(newIdx);
        if (mesh.hasFaceMaterial) mesh.faceMaterial.swap(newMat);
    }

    // Stabilize triangle stream ordering to reduce frame-to-frame draw conflicts.
    {
        const size_t triN = mesh.indices.size() / 3;
        if (triN > 1)
        {
            std::vector<size_t> order(triN);
            for (size_t i = 0; i < triN; ++i) order[i] = i;

            auto triSortKey = [&](size_t t) {
                uint16_t a = mesh.indices[t * 3 + 0];
                uint16_t b = mesh.indices[t * 3 + 1];
                uint16_t c = mesh.indices[t * 3 + 2];
                uint16_t m = (mesh.hasFaceMaterial && t < mesh.faceMaterial.size()) ? mesh.faceMaterial[t] : 0;
                uint16_t lo = std::min<uint16_t>(a, std::min<uint16_t>(b, c));
                uint16_t hi = std::max<uint16_t>(a, std::max<uint16_t>(b, c));
                uint16_t mid = (uint16_t)(a + b + c - lo - hi);
                return std::array<uint16_t, 4>{ m, lo, mid, hi };
            };

            std::stable_sort(order.begin(), order.end(), [&](size_t l, size_t r) {
                auto kl = triSortKey(l);
                auto kr = triSortKey(r);
                return kl < kr;
            });

            std::vector<uint16_t> sortedIdx;
            std::vector<uint16_t> sortedMat;
            sortedIdx.reserve(mesh.indices.size());
            if (mesh.hasFaceMaterial) sortedMat.reserve(mesh.faceMaterial.size());

            for (size_t oi = 0; oi < triN; ++oi)
            {
                size_t t = order[oi];
                sortedIdx.push_back(mesh.indices[t * 3 + 0]);
                sortedIdx.push_back(mesh.indices[t * 3 + 1]);
                sortedIdx.push_back(mesh.indices[t * 3 + 2]);
                if (mesh.hasFaceMaterial)
                {
                    uint16_t m = (t < mesh.faceMaterial.size()) ? mesh.faceMaterial[t] : 0;
                    sortedMat.push_back(m);
                }
            }

            mesh.indices.swap(sortedIdx);
            if (mesh.hasFaceMaterial) mesh.faceMaterial.swap(sortedMat);
        }
    }

    if (mesh.hasFaceRecords && !mesh.faceRecords.empty())
    {
        std::vector<NebFaceRecord> newRecs;
        newRecs.reserve(mesh.faceRecords.size());
        std::unordered_set<uint64_t> seenRecTri;
        seenRecTri.reserve(mesh.faceRecords.size() + 1);

        for (auto fr : mesh.faceRecords)
        {
            int ar = (fr.arity >= 3 && fr.arity <= 4) ? (int)fr.arity : 3;
            if (ar == 3)
            {
                uint16_t a = fr.indices[0], b = fr.indices[1], c = fr.indices[2];
                if (a == b || b == c || c == a) continue;
                uint64_t key = triKey(a, b, c, fr.material);
                if (!seenRecTri.insert(key).second) continue;
            }
            newRecs.push_back(fr);
        }

        if (!newRecs.empty() && newRecs.size() < mesh.faceRecords.size())
            mesh.faceRecords.swap(newRecs);
    }
}

// ---------------------------------------------------------------------------
// LoadNebMesh — loads .nebmesh binary into NebMesh struct
// ---------------------------------------------------------------------------

bool LoadNebMesh(const std::filesystem::path& path, NebMesh& outMesh)
{
    std::ifstream in(path, std::ios::binary | std::ios::in);
    if (!in.is_open()) return false;

    char magic[4];
    if (!in.read(magic, 4)) return false;
    if (!(magic[0] == 'N' && magic[1] == 'E' && magic[2] == 'B' && magic[3] == 'M')) return false;

    uint32_t version = 0, flags = 0, vertexCount = 0, indexCount = 0, posFracBits = 8;
    if (!ReadU32BE(in, version)) return false;
    if (!ReadU32BE(in, flags)) return false;
    if (!ReadU32BE(in, vertexCount)) return false;
    if (!ReadU32BE(in, indexCount)) return false;
    if (!ReadU32BE(in, posFracBits)) return false;

    outMesh.positions.resize(vertexCount);
    outMesh.uvs.clear();
    outMesh.uvs1.clear();
    outMesh.indices.resize(indexCount);
    outMesh.faceMaterial.clear();
    outMesh.faceVertexCounts.clear();
    outMesh.faceRecords.clear();
    outMesh.hasUv = (flags & 1u) != 0;
    outMesh.hasUv1 = (flags & 16u) != 0;
    outMesh.hasFaceMaterial = (flags & 2u) != 0;
    outMesh.hasFaceTopology = (flags & 4u) != 0;
    outMesh.hasFaceRecords = (flags & 8u) != 0;

    const float invScale = 1.0f / (float)(1 << posFracBits);
    for (uint32_t v = 0; v < vertexCount; ++v)
    {
        int16_t x, y, z;
        if (!ReadS16BE(in, x) || !ReadS16BE(in, y) || !ReadS16BE(in, z)) return false;
        outMesh.positions[v] = { x * invScale, y * invScale, z * invScale };
    }

    if (outMesh.hasUv)
    {
        outMesh.uvs.resize(vertexCount);
        const float uvInv = 1.0f / 256.0f;
        for (uint32_t v = 0; v < vertexCount; ++v)
        {
            int16_t u, vcoord;
            if (!ReadS16BE(in, u) || !ReadS16BE(in, vcoord)) return false;
            outMesh.uvs[v] = { u * uvInv, vcoord * uvInv, 0.0f };
        }
    }

    if (outMesh.hasUv1)
    {
        outMesh.uvs1.resize(vertexCount);
        const float uvInv = 1.0f / 256.0f;
        for (uint32_t v = 0; v < vertexCount; ++v)
        {
            int16_t u, vcoord;
            if (!ReadS16BE(in, u) || !ReadS16BE(in, vcoord)) return false;
            outMesh.uvs1[v] = { u * uvInv, vcoord * uvInv, 0.0f };
        }
    }

    for (uint32_t i = 0; i < indexCount; ++i)
    {
        uint16_t idx;
        if (!ReadU16BE(in, idx)) return false;
        outMesh.indices[i] = idx;
    }

    if (outMesh.hasFaceMaterial)
    {
        uint32_t triCount = indexCount / 3u;
        outMesh.faceMaterial.resize(triCount, 0);
        for (uint32_t t = 0; t < triCount; ++t)
        {
            uint16_t fm = 0;
            if (!ReadU16BE(in, fm)) { outMesh.faceMaterial.clear(); outMesh.hasFaceMaterial = false; break; }
            outMesh.faceMaterial[t] = fm;
        }
    }

    if (outMesh.hasFaceTopology)
    {
        uint32_t faceCount = 0;
        if (!ReadU32BE(in, faceCount))
        {
            outMesh.faceVertexCounts.clear();
            outMesh.hasFaceTopology = false;
        }
        else if (faceCount > 0)
        {
            outMesh.faceVertexCounts.resize(faceCount, 3);
            if (!in.read((char*)outMesh.faceVertexCounts.data(), (std::streamsize)faceCount))
            {
                outMesh.faceVertexCounts.clear();
                outMesh.hasFaceTopology = false;
            }
        }
    }

    if (outMesh.hasFaceRecords && in.peek() != EOF)
    {
        uint32_t recCount = 0;
        if (!ReadU32BE(in, recCount))
        {
            outMesh.faceRecords.clear();
            outMesh.hasFaceRecords = false;
        }
        else if (recCount > 0)
        {
            outMesh.faceRecords.resize(recCount);
            bool ok = true;
            const float uvInv = 1.0f / 256.0f;
            for (uint32_t ri = 0; ri < recCount && ok; ++ri)
            {
                int a = in.get();
                int w = in.get();
                if (a == EOF || w == EOF) { ok = false; break; }
                outMesh.faceRecords[ri].arity = (uint8_t)a;
                outMesh.faceRecords[ri].winding = (uint8_t)w;
                if (!ReadU16BE(in, outMesh.faceRecords[ri].material)) { ok = false; break; }
                for (int ci = 0; ci < 4; ++ci)
                {
                    int16_t u = 0, v = 0;
                    if (!ReadU16BE(in, outMesh.faceRecords[ri].indices[ci]) || !ReadS16BE(in, u) || !ReadS16BE(in, v))
                    {
                        ok = false;
                        break;
                    }
                    outMesh.faceRecords[ri].uvs[ci] = Vec3{ u * uvInv, v * uvInv, 0.0f };
                }
            }
            if (!ok)
            {
                outMesh.faceRecords.clear();
                outMesh.hasFaceRecords = false;
            }
        }
    }

    CleanupNebMeshTopology(outMesh);

    outMesh.uvLayerCount = (outMesh.hasUv ? 1 : 0) + (outMesh.hasUv1 ? 1 : 0);
    outMesh.valid = true;
    return true;
}

// ---------------------------------------------------------------------------
// GetNebMesh — cache lookup + load
// ---------------------------------------------------------------------------

const NebMesh* GetNebMesh(const std::filesystem::path& path)
{
    std::string key = path.string();
    auto it = gNebMeshCache.find(key);
    if (it != gNebMeshCache.end()) return &it->second;

    NebMesh mesh;
    if (!LoadNebMesh(path, mesh))
    {
        mesh.valid = false;
    }
    gNebMeshCache[key] = std::move(mesh);
    return &gNebMeshCache[key];
}

// ---------------------------------------------------------------------------
// ReadNebMeshUvLayerCount — reads UV layer count from header only
// ---------------------------------------------------------------------------

int ReadNebMeshUvLayerCount(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::in);
    if (!in.is_open()) return 0;
    char magic[4];
    if (!in.read(magic, 4)) return 0;
    if (!(magic[0] == 'N' && magic[1] == 'E' && magic[2] == 'B' && magic[3] == 'M')) return 0;
    uint32_t version = 0, flags = 0;
    if (!ReadU32BE(in, version)) return 0;
    if (!ReadU32BE(in, flags)) return 0;
    int count = 0;
    if (flags & 1u) ++count;  // UV0
    if (flags & 16u) ++count; // UV1
    return count;
}

// ---------------------------------------------------------------------------
// ExportNebMesh — exports Assimp scene to .nebmesh binary
// ---------------------------------------------------------------------------

bool ExportNebMesh(
    const aiScene* scene,
    const std::filesystem::path& outPath,
    std::string& warning,
    std::vector<uint32_t>* outProvenanceMeshIndices,
    std::vector<uint32_t>* outProvenanceVertexIndices)
{
    if (!scene || scene->mNumMeshes == 0) return false;

    const uint32_t maxVerts = 2048;
    const uint32_t maxIndices = 65535;

    bool hasUv0 = false;
    bool hasUv1 = false;
    std::vector<int> meshUvChannel(scene->mNumMeshes, -1);
    std::vector<int> meshUvChannel1(scene->mNumMeshes, -1); // second UV channel
    uint32_t srcVertexCount = 0;
    uint32_t srcIndexCount = 0;
    for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
    {
        const aiMesh* mesh = scene->mMeshes[m];
        if (!mesh) continue;
        srcVertexCount += mesh->mNumVertices;
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
        {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices >= 3)
                srcIndexCount += (face.mNumIndices - 2) * 3; // fan triangulation
        }
        int bestUvChannel = -1;
        float bestSpan = -1.0f;
        int secondUvChannel = -1;
        float secondSpan = -1.0f;
        for (int ch = 0; ch < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++ch)
        {
            if (!(mesh->HasTextureCoords(ch) && mesh->mNumUVComponents[ch] >= 2)) continue;
            float minU = mesh->mTextureCoords[ch][0].x, maxU = minU;
            float minV = mesh->mTextureCoords[ch][0].y, maxV = minV;
            for (unsigned int v = 1; v < mesh->mNumVertices; ++v)
            {
                aiVector3D uv = mesh->mTextureCoords[ch][v];
                minU = std::min(minU, uv.x); maxU = std::max(maxU, uv.x);
                minV = std::min(minV, uv.y); maxV = std::max(maxV, uv.y);
            }
            float span = (maxU - minU) + (maxV - minV);
            if (span > bestSpan)
            {
                secondSpan = bestSpan;
                secondUvChannel = bestUvChannel;
                bestSpan = span;
                bestUvChannel = ch;
            }
            else if (span > secondSpan)
            {
                secondSpan = span;
                secondUvChannel = ch;
            }
        }
        meshUvChannel[m] = bestUvChannel;
        meshUvChannel1[m] = secondUvChannel;
        if (bestUvChannel >= 0) hasUv0 = true;
        if (secondUvChannel >= 0) hasUv1 = true;
    }

    NebMesh exportMesh;
    exportMesh.hasUv = hasUv0;
    exportMesh.hasUv1 = hasUv1;
    exportMesh.uvLayerCount = (hasUv0 ? 1 : 0) + (hasUv1 ? 1 : 0);
    exportMesh.hasFaceMaterial = true;
    exportMesh.hasFaceTopology = true;
    exportMesh.hasFaceRecords = true;
    exportMesh.positions.reserve(srcVertexCount);
    if (hasUv0) exportMesh.uvs.reserve(srcVertexCount);
    if (hasUv1) exportMesh.uvs1.reserve(srcVertexCount);
    exportMesh.indices.reserve(srcIndexCount);
    exportMesh.faceMaterial.reserve(srcIndexCount / 3u);
    exportMesh.faceVertexCounts.reserve(srcIndexCount / 3u);
    exportMesh.faceRecords.reserve(srcIndexCount / 3u);

    std::vector<uint32_t> provenanceMeshIndices;
    std::vector<uint32_t> provenanceVertexIndices;
    provenanceMeshIndices.reserve(srcVertexCount);
    provenanceVertexIndices.reserve(srcVertexCount);

    // Compact used FBX material indices into contiguous slot indices (0..N-1).
    std::vector<unsigned char> usedMat(scene->mNumMaterials, 0);
    for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
    {
        const aiMesh* mesh = scene->mMeshes[m];
        if (!mesh) continue;
        if (mesh->mMaterialIndex < scene->mNumMaterials)
            usedMat[mesh->mMaterialIndex] = 1;
    }
    std::vector<int> matToSlot(scene->mNumMaterials, -1);
    int nextSlot = 0;
    for (unsigned int mi = 0; mi < scene->mNumMaterials; ++mi)
        if (usedMat[mi]) matToSlot[mi] = nextSlot++;

    uint32_t baseVertex = 0;
    for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
    {
        const aiMesh* mesh = scene->mMeshes[m];
        if (!mesh) continue;

        int uvCh = ((int)m < (int)meshUvChannel.size()) ? meshUvChannel[m] : -1;
        const bool meshHasUv = (uvCh >= 0 && mesh->HasTextureCoords(uvCh) && mesh->mNumUVComponents[uvCh] >= 2);
        int uvCh1 = ((int)m < (int)meshUvChannel1.size()) ? meshUvChannel1[m] : -1;
        const bool meshHasUv1 = (uvCh1 >= 0 && mesh->HasTextureCoords(uvCh1) && mesh->mNumUVComponents[uvCh1] >= 2);

        for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
        {
            aiVector3D p = mesh->mVertices[v];
            Vec3 pv{ p.x, p.y, p.z };
            Vec3 bp = ApplyImportBasis(pv);
            // Pre-quantize to S16 8.8 grid so CleanupNebMeshTopology operates on
            // the same precision as the stored format. This prevents LoadNebMesh's
            // second cleanup from finding new merges and changing vertex order.
            exportMesh.positions.push_back(Vec3{
                (float)ToS16Fixed8_8(bp.x) / 256.0f,
                (float)ToS16Fixed8_8(bp.y) / 256.0f,
                (float)ToS16Fixed8_8(bp.z) / 256.0f });
            if (exportMesh.hasUv)
            {
                aiVector3D uv = meshHasUv ? mesh->mTextureCoords[uvCh][v] : aiVector3D(0, 0, 0);
                exportMesh.uvs.push_back(Vec3{
                    (float)ToS16Fixed8_8(uv.x) / 256.0f,
                    (float)ToS16Fixed8_8(uv.y) / 256.0f,
                    0.0f });
            }
            if (exportMesh.hasUv1)
            {
                aiVector3D uv1 = meshHasUv1 ? mesh->mTextureCoords[uvCh1][v] : aiVector3D(0, 0, 0);
                exportMesh.uvs1.push_back(Vec3{
                    (float)ToS16Fixed8_8(uv1.x) / 256.0f,
                    (float)ToS16Fixed8_8(uv1.y) / 256.0f,
                    0.0f });
            }
            provenanceMeshIndices.push_back((uint32_t)m);
            provenanceVertexIndices.push_back((uint32_t)v);
        }

        unsigned int sourceMat = mesh->mMaterialIndex;
        uint16_t matIdx = 0;
        if (sourceMat < matToSlot.size() && matToSlot[sourceMat] >= 0)
            matIdx = (uint16_t)std::min<int>(matToSlot[sourceMat], 65535);
        else
            matIdx = (uint16_t)std::min<unsigned int>(sourceMat, 65535u);

        auto getUv = [&](unsigned int localIdx)->Vec3 {
            if (!(uvCh >= 0 && mesh->HasTextureCoords(uvCh) && mesh->mNumUVComponents[uvCh] >= 2)) return Vec3{ 0,0,0 };
            if (localIdx >= mesh->mNumVertices) return Vec3{ 0,0,0 };
            aiVector3D uv = mesh->mTextureCoords[uvCh][localIdx];
            return Vec3{ uv.x, uv.y, 0.0f };
        };
        auto getPos = [&](unsigned int localIdx)->Vec3 {
            if (localIdx >= mesh->mNumVertices) return Vec3{ 0,0,0 };
            aiVector3D p = mesh->mVertices[localIdx];
            return ApplyImportBasis(Vec3{ p.x, p.y, p.z });
        };

        for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
        {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices < 3) continue;

            exportMesh.faceVertexCounts.push_back((uint8_t)std::min<unsigned int>(face.mNumIndices, 255u));

            if (face.mNumIndices == 3 || face.mNumIndices == 4)
            {
                NebFaceRecord rec{};
                rec.arity = (uint8_t)face.mNumIndices;
                rec.material = matIdx;
                for (unsigned int ci = 0; ci < face.mNumIndices; ++ci)
                {
                    unsigned int li = face.mIndices[ci];
                    rec.indices[ci] = ToU16Clamped(li + baseVertex);
                    rec.uvs[ci] = getUv(li);
                }
                Vec3 pa = getPos(face.mIndices[0]);
                Vec3 pb = getPos(face.mIndices[1]);
                Vec3 pc = getPos(face.mIndices[2]);
                rec.winding = ComputeFaceWindingHint(pa, pb, pc);
                exportMesh.faceRecords.push_back(rec);
            }
            else
            {
                // Saturn-safe subset: split n-gons into authored fan triangles and serialize explicitly.
                for (unsigned int i = 1; i + 1 < face.mNumIndices; ++i)
                {
                    NebFaceRecord rec{};
                    rec.arity = 3;
                    rec.material = matIdx;
                    unsigned int li0 = face.mIndices[0];
                    unsigned int li1 = face.mIndices[i];
                    unsigned int li2 = face.mIndices[i + 1];
                    rec.indices[0] = ToU16Clamped(li0 + baseVertex);
                    rec.indices[1] = ToU16Clamped(li1 + baseVertex);
                    rec.indices[2] = ToU16Clamped(li2 + baseVertex);
                    rec.uvs[0] = getUv(li0);
                    rec.uvs[1] = getUv(li1);
                    rec.uvs[2] = getUv(li2);
                    rec.winding = ComputeFaceWindingHint(getPos(li0), getPos(li1), getPos(li2));
                    exportMesh.faceRecords.push_back(rec);
                }
            }

            // Triangulate polygon face as fan: (0, i, i+1)
            for (unsigned int i = 1; i + 1 < face.mNumIndices; ++i)
            {
                exportMesh.indices.push_back(ToU16Clamped(face.mIndices[0] + baseVertex));
                exportMesh.indices.push_back(ToU16Clamped(face.mIndices[i] + baseVertex));
                exportMesh.indices.push_back(ToU16Clamped(face.mIndices[i + 1] + baseVertex));
                exportMesh.faceMaterial.push_back(matIdx);
            }
        }

        baseVertex += mesh->mNumVertices;
    }

    const size_t preCleanupVerts = exportMesh.positions.size();
    const size_t provCountBefore = std::min(provenanceMeshIndices.size(), provenanceVertexIndices.size());
    printf("[AnimMapExport] preCleanupVerts=%zu\n", preCleanupVerts);
    printf("[AnimMapExport] provCountBefore=%zu\n", provCountBefore);

    // Final exported stream must match runtime/topology-cleaned vertex order.
    CleanupNebMeshTopology(exportMesh, &provenanceMeshIndices, &provenanceVertexIndices);

    const uint32_t vertexCount = (uint32_t)exportMesh.positions.size();
    const uint32_t indexCount = (uint32_t)exportMesh.indices.size();
    const size_t postCleanupVerts = exportMesh.positions.size();
    const size_t provCountAfter = std::min(provenanceMeshIndices.size(), provenanceVertexIndices.size());
    printf("[AnimMapExport] postCleanupVerts=%zu\n", postCleanupVerts);
    printf("[AnimMapExport] provCountAfter=%zu\n", provCountAfter);

    if (vertexCount > maxVerts)
        warning = "Vertex limit exceeded (" + std::to_string(vertexCount) + ">" + std::to_string(maxVerts) + ")";
    if (indexCount > maxIndices)
    {
        if (!warning.empty()) warning += "; ";
        warning += "Index limit exceeded (" + std::to_string(indexCount) + ">" + std::to_string(maxIndices) + ")";
    }

    size_t firstInvalidProvenanceIndex = SIZE_MAX;
    bool provenanceValid = (provenanceMeshIndices.size() == vertexCount &&
                            provenanceVertexIndices.size() == vertexCount);
    if (provenanceValid)
    {
        for (size_t i = 0; i < provenanceMeshIndices.size(); ++i)
        {
            const uint32_t sm = provenanceMeshIndices[i];
            const uint32_t sv = provenanceVertexIndices[i];
            if (sm >= scene->mNumMeshes || !scene->mMeshes[sm] || sv >= scene->mMeshes[sm]->mNumVertices)
            {
                provenanceValid = false;
                firstInvalidProvenanceIndex = i;
                break;
            }
        }
    }
    else
    {
        firstInvalidProvenanceIndex = std::min(provenanceMeshIndices.size(), provenanceVertexIndices.size());
    }
    printf("[AnimMapExport] firstInvalidProvIndex=%zu\n", (firstInvalidProvenanceIndex == SIZE_MAX) ? (size_t)-1 : firstInvalidProvenanceIndex);

    const char* exportMode = provenanceValid ? "provenance_capture_exact" : "provenance_capture_invalid";
    printf("[AnimMap] stage=%s\n", provenanceValid ? "export_provenance" : "fail_provenance_invalid");
    printf("[AnimMap] mode=%s\n", exportMode);
    printf("[AnimMap] finalNebVerts=%u\n", vertexCount);
    printf("[AnimMap] provenanceCount=%zu\n", provenanceMeshIndices.size());
    printf("[AnimMap] provenanceValid=%d\n", provenanceValid ? 1 : 0);
    printf("[AnimMap] firstInvalidProvenanceIndex=%zu\n", (firstInvalidProvenanceIndex == SIZE_MAX) ? (size_t)-1 : firstInvalidProvenanceIndex);

    if (!provenanceValid)
    {
        if (!warning.empty()) warning += "; ";
        warning += "Provenance capture failed: final vertex stream has invalid/missing provenance";
        return false;
    }

    std::ofstream out(outPath, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;

    const char magic[4] = { 'N','E','B','M' };
    uint32_t version = 6; // v6 adds second UV layer (optional via flags)
    uint32_t flags = 0u;
    if (exportMesh.hasUv && exportMesh.uvs.size() == exportMesh.positions.size()) flags |= 1u; // bit0 = UV0
    flags |= 2u; // bit1 = face material index stream
    flags |= 4u; // bit2 = original face topology stream (vertex count per face)
    flags |= 8u; // bit3 = canonical face records stream
    if (exportMesh.hasUv1 && exportMesh.uvs1.size() == exportMesh.positions.size()) flags |= 16u; // bit4 = UV1
    uint32_t posFracBits = 8; // 8.8 fixed

    out.write(magic, 4);
    WriteU32BE(out, version);
    WriteU32BE(out, flags);
    WriteU32BE(out, vertexCount);
    WriteU32BE(out, indexCount);
    WriteU32BE(out, posFracBits);

    bool clampWarn = false;
    const float scale = (float)(1 << posFracBits);
    auto toPos = [&](float val) -> int16_t {
        float scaled = val * scale;
        if (scaled > 32767.0f) { clampWarn = true; scaled = 32767.0f; }
        if (scaled < -32768.0f) { clampWarn = true; scaled = -32768.0f; }
        return (int16_t)std::lround(scaled);
    };

    for (const Vec3& p : exportMesh.positions)
    {
        WriteS16BE(out, toPos(p.x));
        WriteS16BE(out, toPos(p.y));
        WriteS16BE(out, toPos(p.z));
    }

    if ((flags & 1u) != 0)
    {
        for (const Vec3& uv : exportMesh.uvs)
        {
            WriteS16BE(out, ToS16Fixed8_8(uv.x));
            WriteS16BE(out, ToS16Fixed8_8(uv.y));
        }
    }

    if ((flags & 16u) != 0)
    {
        for (const Vec3& uv : exportMesh.uvs1)
        {
            WriteS16BE(out, ToS16Fixed8_8(uv.x));
            WriteS16BE(out, ToS16Fixed8_8(uv.y));
        }
    }

    for (uint16_t idx : exportMesh.indices)
        WriteU16BE(out, idx);

    // v3+ optional stream: one uint16 material index per triangle face.
    for (uint16_t fm : exportMesh.faceMaterial)
        WriteU16BE(out, fm);

    // v4 optional stream: source polygon arity for each original face.
    WriteU32BE(out, (uint32_t)exportMesh.faceVertexCounts.size());
    if (!exportMesh.faceVertexCounts.empty())
        out.write((const char*)exportMesh.faceVertexCounts.data(), (std::streamsize)exportMesh.faceVertexCounts.size());

    // v5 optional stream: canonical authored face records (tri/quad corner order + UVs + winding hint + material).
    WriteU32BE(out, (uint32_t)exportMesh.faceRecords.size());
    for (const auto& fr : exportMesh.faceRecords)
    {
        out.put((char)fr.arity);
        out.put((char)fr.winding);
        WriteU16BE(out, fr.material);
        for (int ci = 0; ci < 4; ++ci)
        {
            WriteU16BE(out, fr.indices[ci]);
            WriteS16BE(out, ToS16Fixed8_8(fr.uvs[ci].x));
            WriteS16BE(out, ToS16Fixed8_8(fr.uvs[ci].y));
        }
    }

    if (clampWarn)
    {
        if (!warning.empty()) warning += "; ";
        warning += "Position clamp (too large for 8.8)";
    }

    if (outProvenanceMeshIndices) *outProvenanceMeshIndices = provenanceMeshIndices;
    if (outProvenanceVertexIndices) *outProvenanceVertexIndices = provenanceVertexIndices;

    return true;
}

// ---------------------------------------------------------------------------
// HashNebMeshLayoutCrc32 — CRC32 hash of mesh layout
// ---------------------------------------------------------------------------

uint32_t HashNebMeshLayoutCrc32(const NebMesh& mesh)
{
    uint32_t crc = 0xFFFFFFFFu;
    auto feedByte = [&](uint8_t b)
    {
        crc ^= b;
        for (int i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    };

    auto feedU32 = [&](uint32_t v)
    {
        feedByte((uint8_t)(v & 0xFFu));
        feedByte((uint8_t)((v >> 8) & 0xFFu));
        feedByte((uint8_t)((v >> 16) & 0xFFu));
        feedByte((uint8_t)((v >> 24) & 0xFFu));
    };

    feedU32((uint32_t)mesh.positions.size());
    for (const Vec3& p : mesh.positions)
    {
        uint32_t bx = 0, by = 0, bz = 0;
        static_assert(sizeof(float) == sizeof(uint32_t), "float size mismatch");
        memcpy(&bx, &p.x, sizeof(uint32_t));
        memcpy(&by, &p.y, sizeof(uint32_t));
        memcpy(&bz, &p.z, sizeof(uint32_t));
        feedU32(bx); feedU32(by); feedU32(bz);
    }

    feedU32((uint32_t)mesh.indices.size());
    for (uint16_t idx : mesh.indices)
    {
        feedByte((uint8_t)(idx & 0xFFu));
        feedByte((uint8_t)((idx >> 8) & 0xFFu));
    }

    return ~crc;
}

// ---------------------------------------------------------------------------
// EnsureDefaultCubeNebmesh — creates default cube mesh from FBX
// ---------------------------------------------------------------------------

bool EnsureDefaultCubeNebmesh(const std::filesystem::path& projectDir)
{
    if (projectDir.empty()) return false;
    std::filesystem::path assetsDir = projectDir / "Assets";
    std::filesystem::path fbx = assetsDir / "cube_primitive.fbx";
    std::filesystem::path nebmesh = assetsDir / "cube_primitive.nebmesh";
    if (std::filesystem::exists(nebmesh)) return true;
    if (!std::filesystem::exists(fbx)) return false;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(fbx.string(), aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);
    if (!scene) return false;
    std::string warn;
    return ExportNebMesh(scene, nebmesh, warn);
}
