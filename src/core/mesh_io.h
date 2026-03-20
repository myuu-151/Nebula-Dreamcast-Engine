#pragma once

#include "math_types.h"

#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <filesystem>
#include <fstream>

// GLuint is just unsigned int on all platforms — avoid pulling in GL/gl.h (requires Windows.h first)
using GLuint = unsigned int;

// Forward-declare Assimp types used by ExportNebMesh / EnsureDefaultCubeNebmesh
struct aiScene;

// ---------------------------------------------------------------------------
// NebFaceRecord — per-face authored record (v5+)
// ---------------------------------------------------------------------------
struct NebFaceRecord
{
    uint8_t arity = 3;            // 3 or 4 for Saturn path
    uint8_t winding = 0;          // 0=clockwise, 1=counter-clockwise (author-space hint)
    uint16_t material = 0;
    uint16_t indices[4] = { 0, 0, 0, 0 };
    Vec3 uvs[4] = {};
};

// ---------------------------------------------------------------------------
// NebMesh — in-memory representation of a .nebmesh binary
// ---------------------------------------------------------------------------
struct NebMesh
{
    std::vector<Vec3> positions;
    std::vector<Vec3> uvs;
    std::vector<Vec3> uvs1;           // second UV layer (v6+)
    std::vector<uint16_t> indices;
    std::vector<uint16_t> faceMaterial; // per-triangle material index
    std::vector<uint8_t> faceVertexCounts; // original polygon arity per source face (v4+)
    std::vector<NebFaceRecord> faceRecords; // canonical authored face stream (v5+)
    bool hasUv = false;
    bool hasUv1 = false;              // second UV layer present
    int uvLayerCount = 0;             // total UV layers (0, 1, or 2)
    bool hasFaceMaterial = false;
    bool hasFaceTopology = false;
    bool hasFaceRecords = false;
    bool valid = false;
};

// ---------------------------------------------------------------------------
// NebAnimClip — in-memory representation of a .nebanim binary
// ---------------------------------------------------------------------------
struct NebAnimClip
{
    uint32_t version = 0;
    uint32_t flags = 0;
    uint32_t vertexCount = 0;
    uint32_t frameCount = 0;
    float fps = 12.0f;
    uint32_t deltaFracBits = 8;
    uint32_t targetMeshVertexCount = 0;
    uint32_t targetMeshHash = 0;
    bool hasEmbeddedMap = false;
    bool meshAligned = false;
    std::vector<uint32_t> embeddedMapIndices;
    bool valid = false;
    std::vector<std::vector<Vec3>> frames;
};

// ---------------------------------------------------------------------------
// Global caches
// ---------------------------------------------------------------------------
extern std::unordered_map<std::string, NebMesh> gNebMeshCache;
extern std::unordered_map<std::string, GLuint>  gNebTextureCache;

// ---------------------------------------------------------------------------
// Binary I/O helpers (big-endian)
// ---------------------------------------------------------------------------
void WriteU32BE(std::ofstream& out, uint32_t v);
void WriteU16BE(std::ofstream& out, uint16_t v);
bool ReadU32BE(std::ifstream& in, uint32_t& outVal);
bool ReadU16BE(std::ifstream& in, uint16_t& outVal);
bool ReadS16BE(std::ifstream& in, int16_t& outVal);
bool ReadS32BE(std::ifstream& in, int32_t& outVal);
void WriteS32BE(std::ofstream& out, int32_t v);
void WriteS16BE(std::ofstream& out, int16_t v);

uint16_t ToU16Clamped(uint32_t v);
int16_t  ToS16Fixed8_8(float v);
int32_t  ToFixed16_16(float v);
float    FromFixed16_16(int32_t v);

// ---------------------------------------------------------------------------
// Import-basis helpers (used by ExportNebMesh)
// ---------------------------------------------------------------------------
Vec3    ApplyImportBasis(const Vec3& v);
uint8_t ComputeFaceWindingHint(const Vec3& a, const Vec3& b, const Vec3& c);

// ---------------------------------------------------------------------------
// Mesh I/O functions
// ---------------------------------------------------------------------------
bool LoadNebMesh(const std::filesystem::path& path, NebMesh& outMesh);
const NebMesh* GetNebMesh(const std::filesystem::path& path);

bool ExportNebMesh(
    const aiScene* scene,
    const std::filesystem::path& outPath,
    std::string& warning,
    std::vector<uint32_t>* outProvenanceMeshIndices = nullptr,
    std::vector<uint32_t>* outProvenanceVertexIndices = nullptr);

void CleanupNebMeshTopology(
    NebMesh& mesh,
    std::vector<uint32_t>* outProvMeshIndices = nullptr,
    std::vector<uint32_t>* outProvVertexIndices = nullptr);

int ReadNebMeshUvLayerCount(const std::filesystem::path& path);

uint32_t HashNebMeshLayoutCrc32(const NebMesh& mesh);

bool EnsureDefaultCubeNebmesh(const std::filesystem::path& projectDir);
