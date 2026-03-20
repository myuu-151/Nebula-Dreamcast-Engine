#pragma once

#include "../math/math_types.h"
#include "mesh_io.h"

#include <assimp/scene.h>
#include <assimp/Importer.hpp>

#include <string>
#include <vector>
#include <filesystem>
#include <cstdint>
#include <unordered_map>

// ---------------------------------------------------------------------------
// AnimBakeDiagnostics
// ---------------------------------------------------------------------------
struct AnimBakeDiagnostics
{
    int totalBones = 0;
    int matchedBones = 0;
    int unmatchedBones = 0;
    int channelsFound = 0;
    float maxVertexDeltaFromFrame0 = 0.0f;
};

// ---------------------------------------------------------------------------
// AiNodeTrsSample — per-node TRS sample output from AiNodeLocalAtTime
// ---------------------------------------------------------------------------
struct AiNodeTrsSample
{
    aiVector3D translation = aiVector3D(0, 0, 0);
    aiQuaternion rotation = aiQuaternion();
    aiVector3D scale = aiVector3D(1, 1, 1);
    aiVector3D bindTranslation = aiVector3D(0, 0, 0);
    aiQuaternion bindRotation = aiQuaternion();
    aiVector3D bindScale = aiVector3D(1, 1, 1);
    bool hasChannel = false;
    bool usedBindTranslation = false;
    bool usedBindRotation = false;
    bool usedBindScale = false;
    unsigned int posKeys = 0;
    unsigned int rotKeys = 0;
    unsigned int sclKeys = 0;
};

// ---------------------------------------------------------------------------
// VtxAnimPlaybackState — lightweight playback state for vertex animation preview
// ---------------------------------------------------------------------------
struct VtxAnimPlaybackState
{
    bool playing = false;
    bool loop = true;
    float currentTimeSec = 0.0f;
    int currentFrame = 0;
    float fps = 12.0f;
};

// ---------------------------------------------------------------------------
// Inspector enums
// ---------------------------------------------------------------------------
enum class InspectorPlaybackMode : int
{
    EmbeddedExact = 0,
    EmbeddedApprox = 1,
    ExternalLegacy = 2,
};

enum class InspectorMappingQuality : int
{
    Missing = 0,
    Approx = 1,
    Exact = 2,
};

// ---------------------------------------------------------------------------
// InspectorPlaybackDiagnostics / InspectorPlaybackState
// ---------------------------------------------------------------------------
struct InspectorPlaybackDiagnostics
{
    InspectorMappingQuality mappingQuality = InspectorMappingQuality::Missing;
    AnimBakeDiagnostics skinning = {};
    bool skinningValid = false;
    std::string previewReason;
};

struct InspectorPlaybackState
{
    int selectedClip = 0;
    int currentFrame = 0;
    float currentTimeSec = 0.0f;
    InspectorPlaybackDiagnostics diagnostics;
};

// ---------------------------------------------------------------------------
// NebMeshEmbeddedAnimMeta — metadata for embedded animation mapping
// ---------------------------------------------------------------------------
struct NebMeshEmbeddedAnimMeta
{
    std::string sourceFbxPath;
    std::vector<std::string> clipNames;
    std::vector<unsigned int> meshIndices;
    std::vector<uint32_t> mapIndices;
    uint32_t provenanceVersion = 1;
    std::vector<uint32_t> provenanceMeshIndices;
    std::vector<uint32_t> provenanceVertexIndices;
    uint32_t exportedVertexCount = 0;
    bool mappingVerified = false;
    bool mappingOk = false;
    std::string mappingQuality = "missing"; // missing|approx|exact
};

// ---------------------------------------------------------------------------
// NebMeshInspectorState — full state for the mesh/animation inspector panel
// ---------------------------------------------------------------------------
struct NebMeshInspectorState
{
    bool open = false;
    std::filesystem::path targetMeshPath;
    char meshInput[512] = {};
    std::string vtxAnimPath;
    std::string loadedVtxAnimPath;
    char vtxAnimInput[512] = {};
    NebAnimClip clip;
    VtxAnimPlaybackState playback;
    InspectorPlaybackState inspectorPlayback;
    NebMeshEmbeddedAnimMeta embeddedMeta;
    bool embeddedMetaLoaded = false;
    InspectorPlaybackMode playbackMode = InspectorPlaybackMode::EmbeddedExact;
    bool forceEnableApproxPreview = false;
    bool driveNebAnimOnly = false;
    bool embeddedSourceOk = false;
    bool embeddedMappingOk = false;
    std::string embeddedStatusMessage;
    std::vector<uint32_t> embeddedVertexMap;
    std::string embeddedLoadedFbxPath;
    Assimp::Importer embeddedImporter;
    const aiScene* embeddedScene = nullptr;
    int lastEmbeddedClipIndex = -1;
    bool embeddedDiagDirty = true;
    bool nearStaticWarned = false;
    bool embeddedCacheValid = false;
    int embeddedCacheClipIndex = -1;
    int embeddedCacheFrame = -1;
    std::vector<Vec3> embeddedCacheMergedVertices;
    bool loggedCurrentPreviewState = false;
    int loggedPreviewFrame = -1;
    InspectorPlaybackMode loggedPreviewMode = InspectorPlaybackMode::EmbeddedExact;
    std::string lastPreviewReason;
    std::string selectedHelperBoneName;
    bool animDebugDumpEnabled = false;
    int animDebugProbeFrame = 10;
    int animDebugLastClip = -1;
    int animDebugLastProbeFrame = -1;
    bool animDebugForceDump = false;

    // Mini-preview camera controls
    float previewYaw = 0.62f;
    float previewPitch = 0.28f;
    float previewZoom = 1.0f;
};

// ---------------------------------------------------------------------------
// Assimp animation helpers
// ---------------------------------------------------------------------------
aiMatrix4x4 AiComposeTRS(const aiVector3D& t, const aiQuaternion& r, const aiVector3D& s);
aiVector3D AiInterpVec(const aiVector3D& a, const aiVector3D& b, double t);
aiQuaternion AiInterpQuat(const aiQuaternion& a, const aiQuaternion& b, double t);
std::string NormalizeAnimName(const std::string& in);
void CollectSceneNodes(const aiNode* node, std::vector<const aiNode*>& out);
const aiNode* ResolveSceneNodeByNameRobust(const aiScene* scene, const aiString& rawName);
const aiNodeAnim* AiFindChannel(const aiAnimation* anim, const aiString& name);
aiVector3D AiSamplePosition(const aiNodeAnim* channel, double time);
aiVector3D AiSampleScale(const aiNodeAnim* channel, double time);
aiQuaternion AiSampleRotation(const aiNodeAnim* channel, double time);
bool AiTryDecomposeTrs(const aiMatrix4x4& m, aiVector3D& outT, aiQuaternion& outR, aiVector3D& outS);
aiMatrix4x4 AiNodeLocalAtTimeLegacy(const aiNode* node, const aiAnimation* anim, double time);
aiMatrix4x4 AiNodeLocalAtTime(const aiNode* node, const aiAnimation* anim, double time, AiNodeTrsSample* outSample = nullptr);
bool AiFindNodeGlobal(const aiNode* node, const aiAnimation* anim, double time, const aiMatrix4x4& parent, const aiNode* target, aiMatrix4x4& out);
bool AiFindNodeGlobalLegacy(const aiNode* node, const aiAnimation* anim, double time, const aiMatrix4x4& parent, const aiNode* target, aiMatrix4x4& out);
const aiNode* AiFindNodeWithMesh(const aiNode* node, unsigned int meshIndex);
aiVector3D AiTransformPoint(const aiMatrix4x4& m, const aiVector3D& v);
float AiMaxAbs3(const aiVector3D& v);
float AiQuatAngularDeltaDeg(const aiQuaternion& a, const aiQuaternion& b);

// ---------------------------------------------------------------------------
// Animation diagnostics
// ---------------------------------------------------------------------------
void DumpEmbeddedAnimDiagnostics(const aiScene* scene, const aiAnimation* anim, const std::vector<unsigned int>& meshIndices, int probeFrame, float fps);

// ---------------------------------------------------------------------------
// Skinned vertex sampling
// ---------------------------------------------------------------------------
bool SampleMergedFbxVertices(const aiScene* scene, const aiAnimation* anim, const std::vector<unsigned int>& meshIndices, double timeTicks, std::vector<Vec3>& outVerts, AnimBakeDiagnostics* outDiag = nullptr);
bool ComputeEmbeddedClipDiagnostics(const aiScene* scene, const aiAnimation* anim, const std::vector<unsigned int>& meshIndices, float sampleFps, AnimBakeDiagnostics& outDiag);
bool BuildMergedFbxBindData(const aiScene* scene, const std::vector<unsigned int>& meshIndices, std::vector<Vec3>& outPositions, std::vector<Vec3>& outUvs);
bool SampleEmbeddedMergedVerticesCached(NebMeshInspectorState& st, const aiAnimation* anim, int selectedClip, int frame, float fps, std::vector<Vec3>& outMerged);

// ---------------------------------------------------------------------------
// Embedded animation meta I/O
// ---------------------------------------------------------------------------
std::filesystem::path GetNebMeshEmbeddedMetaPath(const std::filesystem::path& absMeshPath);
void BuildDefaultEmbeddedMetaFromScene(const aiScene* scene, NebMeshEmbeddedAnimMeta& outMeta);
bool SaveNebMeshEmbeddedMeta(const std::filesystem::path& absMeshPath, const NebMeshEmbeddedAnimMeta& meta);
bool LoadNebMeshEmbeddedMeta(const std::filesystem::path& absMeshPath, NebMeshEmbeddedAnimMeta& outMeta);

// ---------------------------------------------------------------------------
// Clip lookup and inspector helpers
// ---------------------------------------------------------------------------
const aiAnimation* FindAnimByNameOrIndex(const aiScene* scene, const std::string& clipName, int clipIndex);
InspectorMappingQuality ParseMappingQuality(const std::string& q);
const char* MappingQualityLabel(InspectorMappingQuality q);
const char* PlaybackModeLabel(InspectorPlaybackMode m);

// ---------------------------------------------------------------------------
// Mapping rebuild / index tables
// ---------------------------------------------------------------------------
uint64_t PackSourceVertexKey(uint32_t meshIndex, uint32_t vertexIndex);
bool BuildMergedSourceIndexTable(const aiScene* scene, const std::vector<unsigned int>& meshIndices, std::unordered_map<uint64_t, uint32_t>& outMergedIndex, uint32_t& outMergedVertexCount);
bool RebuildNebMeshEmbeddedMapping(NebMeshInspectorState& st);

// ---------------------------------------------------------------------------
// Animation loading
// ---------------------------------------------------------------------------
bool LoadNebAnimClip(const std::filesystem::path& path, NebAnimClip& outClip, std::string& outError);

// ---------------------------------------------------------------------------
// Animation export / staging
// ---------------------------------------------------------------------------
bool ExportNebAnimation(const aiScene* scene, const aiAnimation* anim, const std::vector<unsigned int>& meshIndices, const std::filesystem::path& outPath, std::string& warning, bool deltaCompress, uint32_t forcedVertexCount = 0, const NebMesh* forcedTargetMesh = nullptr, const std::vector<uint32_t>* forcedMapIndices = nullptr, float sampleRateMultiplier = 1.0f, bool meshLocalSpace = false);
bool WriteRemappedNebAnim(const std::filesystem::path& outPath, const NebAnimClip& clip, const std::vector<int>& remap, uint32_t newVertexCount);
bool StageRemappedNebAnim(const std::filesystem::path& srcAnimPath, const std::filesystem::path& dstAnimPath, const std::filesystem::path& meshAbsPath);

// ---------------------------------------------------------------------------
// CSV helpers (used by embedded meta I/O)
// ---------------------------------------------------------------------------
std::string JoinUIntCsv(const std::vector<unsigned int>& values);
bool ParseUIntCsv(const std::string& s, std::vector<unsigned int>& outValues);
std::string JoinU32Csv(const std::vector<uint32_t>& values);
bool ParseU32Csv(const std::string& s, std::vector<uint32_t>& outValues);

// ---------------------------------------------------------------------------
// Name sanitization helper
// ---------------------------------------------------------------------------
std::string SanitizeName(const std::string& name);

// ---------------------------------------------------------------------------
// Project-relative path resolution
// ---------------------------------------------------------------------------
std::filesystem::path ResolveProjectAssetPath(const std::string& relOrAbs);
