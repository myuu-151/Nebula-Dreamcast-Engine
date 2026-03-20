#include "import_pipeline.h"

#include <cmath>
#include <cstring>
#include <cctype>
#include <cfloat>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <array>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "editor/project.h"                     // gProjectDir
#include "../io/meta_io.h"             // NebulaAssets::*, kStaticMeshMaterialSlots
#include "../io/mesh_io.h"          // ExportNebMesh, LoadNebMesh, ApplyImportBasis, NebMesh, Vec3
#include "../io/anim_io.h"         // NebMeshEmbeddedAnimMeta, BuildDefaultEmbeddedMetaFromScene, SaveNebMeshEmbeddedMeta
#include "../io/texture_io.h"       // ExportNebTexturePNG
#include "../nodes/NodeTypes.h"          // kStaticMeshMaterialSlots, StaticMesh3DNode

// ---------------------------------------------------------------------------
// Globals from main.cpp that we read but don't own
// ---------------------------------------------------------------------------
extern std::string              gViewportToast;
extern double                   gViewportToastUntil;
extern std::filesystem::path    gAssetsCurrentDir;

// glfwGetTime lives in GLFW — declared here to avoid pulling the full header.
extern "C" double glfwGetTime(void);

// ---------------------------------------------------------------------------
// Import-related globals (owned here, externed from import_pipeline.h)
// ---------------------------------------------------------------------------
bool        gImportPopupOpen           = false;
bool        gImportUseProvenanceMapping = true;
bool        gImportDeltaCompress       = false;
bool        gImportDoubleSampleRate    = false;
std::string gImportWarning;

// ---------------------------------------------------------------------------
// Internal helpers (duplicated from main.cpp to avoid cross-TU coupling)
// ---------------------------------------------------------------------------
static std::string SanitizeToken(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (char c : in)
    {
        unsigned char uc = (unsigned char)c;
        if (std::isalnum(uc) || c == '_' || c == '-') out.push_back(c);
        else if (std::isspace(uc) || c == '.') out.push_back('_');
    }
    if (out.empty()) out = "slot";
    return out;
}

static std::string ToProjectRelativePath(const std::filesystem::path& p)
{
    if (gProjectDir.empty())
        return p.filename().generic_string();

    std::error_code ec;
    std::filesystem::path rel = std::filesystem::relative(p, std::filesystem::path(gProjectDir), ec);
    if (ec) return p.filename().generic_string();
    return rel.generic_string();
}

static std::filesystem::path MakeUniqueAssetPath(const std::filesystem::path& root,
                                                  const std::string& baseName,
                                                  const std::string& ext)
{
    for (int i = 0; i < 1000; ++i)
    {
        std::string suffix = (i == 0) ? "" : std::to_string(i);
        std::filesystem::path p = root / (baseName + suffix + ext);
        if (!std::filesystem::exists(p))
            return p;
    }
    return root / (baseName + ext);
}

// ---------------------------------------------------------------------------
// ResolveMaterialTexturePath
// ---------------------------------------------------------------------------
bool ResolveMaterialTexturePath(const aiScene* scene, const aiMaterial* mat,
                                const std::filesystem::path& modelPath,
                                std::filesystem::path& outTexPath, std::string& warn)
{
    if (!scene || !mat) return false;

    aiString texName;
    bool found = false;
#if defined(aiTextureType_BASE_COLOR)
    if (!found && mat->GetTexture(aiTextureType_BASE_COLOR, 0, &texName) == AI_SUCCESS) found = true;
#endif
    if (!found && mat->GetTexture(aiTextureType_DIFFUSE, 0, &texName) == AI_SUCCESS) found = true;
    if (!found && mat->GetTexture(aiTextureType_UNKNOWN, 0, &texName) == AI_SUCCESS) found = true;
    if (!found) return false;

    std::string raw = texName.C_Str();
    if (raw.empty()) return false;
    if (raw[0] == '*')
    {
        if (!warn.empty()) warn += " | ";
        warn += "Embedded FBX textures not supported yet (" + raw + ")";
        return false;
    }

    std::filesystem::path p(raw);
    if (p.is_relative())
    {
        std::filesystem::path p1 = modelPath.parent_path() / p;
        if (std::filesystem::exists(p1)) { outTexPath = p1; return true; }

        std::filesystem::path p2 = modelPath.parent_path() / p.filename();
        if (std::filesystem::exists(p2)) { outTexPath = p2; return true; }
    }
    else if (std::filesystem::exists(p))
    {
        outTexPath = p;
        return true;
    }

    if (!warn.empty()) warn += " | ";
    warn += "Texture file missing: " + raw;
    return false;
}

// ---------------------------------------------------------------------------
// FindTextureByMaterialNameFallback
// ---------------------------------------------------------------------------
bool FindTextureByMaterialNameFallback(const std::filesystem::path& modelPath,
                                       const std::string& materialName,
                                       std::filesystem::path& outTexPath)
{
    if (materialName.empty()) return false;
    std::filesystem::path dir = modelPath.parent_path();
    if (dir.empty() || !std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) return false;

    auto toLower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    };

    const std::string matRaw = toLower(materialName);
    const std::string matSan = toLower(SanitizeToken(materialName));

    for (const auto& e : std::filesystem::directory_iterator(dir))
    {
        if (!e.is_regular_file()) continue;
        std::string ext = toLower(e.path().extension().string());
        if (ext != ".png" && ext != ".tga" && ext != ".jpg" && ext != ".jpeg" && ext != ".bmp" && ext != ".gif" && ext != ".webp")
            continue;

        std::string stemRaw = toLower(e.path().stem().string());
        std::string stemSan = toLower(SanitizeToken(e.path().stem().string()));
        if (stemRaw == matRaw || stemSan == matRaw || stemRaw == matSan || stemSan == matSan)
        {
            outTexPath = e.path();
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// VtxAnimLink helpers
// ---------------------------------------------------------------------------
static std::filesystem::path GetNebMeshVtxAnimLinkPath(const std::filesystem::path& absMeshPath)
{
    return std::filesystem::path(absMeshPath.string() + ".vtxlink");
}

bool LoadNebMeshVtxAnimLink(const std::filesystem::path& absMeshPath, std::string& outAnimPath)
{
    outAnimPath.clear();
    std::ifstream in(GetNebMeshVtxAnimLinkPath(absMeshPath));
    if (!in.is_open()) return false;
    std::getline(in, outAnimPath);
    return !outAnimPath.empty();
}

bool SaveNebMeshVtxAnimLink(const std::filesystem::path& absMeshPath, const std::string& animPath)
{
    std::ofstream out(GetNebMeshVtxAnimLinkPath(absMeshPath), std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;
    out << animPath;
    return true;
}

// ---------------------------------------------------------------------------
// ImportModelTexturesAndGenerateMaterials
// ---------------------------------------------------------------------------
int ImportModelTexturesAndGenerateMaterials(const aiScene* scene,
                                            const std::filesystem::path& modelPath,
                                            const std::filesystem::path& targetDir,
                                            const std::filesystem::path& meshOut,
                                            std::string& warn)
{
    if (!scene) return 0;
    std::filesystem::create_directories(targetDir);
    std::filesystem::path texDir = targetDir / "tex";
    std::filesystem::path matDir = targetDir / "mat";
    std::filesystem::create_directories(texDir);
    std::filesystem::create_directories(matDir);

    int generated = 0;

    // Keep only materials actually referenced by meshes, in source file order.
    std::vector<unsigned char> seen(scene->mNumMaterials, 0);
    for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
    {
        const aiMesh* mesh = scene->mMeshes[m];
        if (!mesh) continue;
        if (mesh->mMaterialIndex < scene->mNumMaterials)
            seen[mesh->mMaterialIndex] = 1;
    }
    std::vector<unsigned int> used;
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i)
    {
        if (seen[i]) used.push_back(i);
    }
    std::string meshStem = SanitizeToken(meshOut.stem().string());
    std::vector<std::string> importedSlotMaterials(kStaticMeshMaterialSlots);
    for (size_t ui = 0; ui < used.size(); ++ui)
    {
        unsigned int mi = used[ui];
        const aiMaterial* mat = scene->mMaterials[mi];
        aiString matName;
        std::string matNameSafe = "material" + std::to_string((int)ui + 1);
        if (mat && mat->Get(AI_MATKEY_NAME, matName) == AI_SUCCESS && matName.length > 0)
            matNameSafe = SanitizeToken(matName.C_Str());

        float uvSu = 1.0f, uvSv = 1.0f, uvOu = 0.0f, uvOv = 0.0f, uvRotDeg = 0.0f;
        if (mat)
        {
            aiUVTransform uvT{};
#if defined(aiTextureType_BASE_COLOR)
            if (mat->Get(AI_MATKEY_UVTRANSFORM(aiTextureType_BASE_COLOR, 0), uvT) == AI_SUCCESS ||
                mat->Get(AI_MATKEY_UVTRANSFORM(aiTextureType_DIFFUSE, 0), uvT) == AI_SUCCESS)
#else
            if (mat->Get(AI_MATKEY_UVTRANSFORM(aiTextureType_DIFFUSE, 0), uvT) == AI_SUCCESS)
#endif
            {
                uvSu = uvT.mScaling.x; uvSv = uvT.mScaling.y;
                uvOu = uvT.mTranslation.x; uvOv = uvT.mTranslation.y;
                uvRotDeg = uvT.mRotation * 57.2957795f;
            }
        }

        std::filesystem::path texSrc;
        bool texFound = ResolveMaterialTexturePath(scene, mat, modelPath, texSrc, warn);
        if (!texFound)
        {
            std::string matNameRaw = matNameSafe;
            if (mat && mat->Get(AI_MATKEY_NAME, matName) == AI_SUCCESS && matName.length > 0)
                matNameRaw = matName.C_Str();
            if (FindTextureByMaterialNameFallback(modelPath, matNameRaw, texSrc))
            {
                texFound = true;
                if (!warn.empty()) warn += " | ";
                warn += "Texture fallback by material name: " + matNameRaw + " -> " + texSrc.filename().string();
            }
        }
        std::string texRel;
        if (texFound)
        {
            char midx[8];
            snprintf(midx, sizeof(midx), "%02u", (unsigned)(ui + 1));
            std::string texBase = SanitizeToken(texSrc.stem().string());
            std::filesystem::path texOut = MakeUniqueAssetPath(texDir, texBase, ".nebtex");
            std::string texWarn;
            if (ExportNebTexturePNG(texSrc, texOut, texWarn))
            {
                texRel = ToProjectRelativePath(texOut);
                if (!texWarn.empty())
                {
                    if (!warn.empty()) warn += " | ";
                    warn += texWarn;
                }
            }
            else
            {
                if (!warn.empty()) warn += " | ";
                warn += "Texture import failed: " + texSrc.string();
            }
        }

        char midx[8];
        snprintf(midx, sizeof(midx), "%02u", (unsigned)(ui + 1));
        std::string matBase = "m_" + matNameSafe;
        std::filesystem::path matOut = MakeUniqueAssetPath(matDir, matBase, ".nebmat");
        if (NebulaAssets::SaveMaterialTexture(matOut, texRel))
        {
            NebulaAssets::SaveMaterialUvTransform(matOut, uvSu, uvSv, uvOu, uvOv, uvRotDeg);
            generated++;
            if ((int)ui < kStaticMeshMaterialSlots)
                importedSlotMaterials[(int)ui] = ToProjectRelativePath(matOut);
        }
    }

    if (!NebulaAssets::SaveNebSlotsManifest(meshOut, importedSlotMaterials))
    {
        if (!warn.empty()) warn += " | ";
        warn += "Failed to write .nebslots manifest in nebslot folder";
    }

    return generated;
}

// ---------------------------------------------------------------------------
// ImportAssetsToCurrentFolder
// ---------------------------------------------------------------------------
void ImportAssetsToCurrentFolder(const std::vector<std::string>& pickedList)
{
    if (pickedList.empty()) return;

    std::filesystem::path targetDir = gAssetsCurrentDir.empty() ? (std::filesystem::path(gProjectDir) / "Assets") : gAssetsCurrentDir;
    int importedCount = 0;

    for (const auto& picked : pickedList)
    {
        std::filesystem::path inPath(picked);
        std::filesystem::path outPath = targetDir / inPath.filename();

        std::string ext = inPath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".vtxa")
        {
            std::filesystem::path animOut = outPath;
            animOut.replace_extension(".nebanim");
            std::error_code ec;
            std::filesystem::copy_file(inPath, animOut, std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec) importedCount++;
            gViewportToast = ec ? "Import failed" : ("Imported " + animOut.filename().string());
            gViewportToastUntil = glfwGetTime() + 2.0;
        }
        else if (ext == ".nebanim")
        {
            std::error_code ec;
            std::filesystem::copy_file(inPath, outPath, std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec) importedCount++;
            gViewportToast = ec ? "Import failed" : ("Imported " + outPath.filename().string());
            gViewportToastUntil = glfwGetTime() + 2.0;
        }
        else if (ext == ".png")
        {
            std::filesystem::path texOut = outPath;
            texOut.replace_extension(".nebtex");
            std::string warn;
            if (ExportNebTexturePNG(inPath, texOut, warn))
            {
                importedCount++;
                gViewportToast = "Imported " + texOut.filename().string();
                if (!warn.empty()) gImportWarning = warn;
            }
            else
            {
                gViewportToast = "PNG import failed";
            }
            gViewportToastUntil = glfwGetTime() + 2.0;
        }
        else if (ext == ".fbx")
        {
            Assimp::Importer importer;
            const aiScene* scene = importer.ReadFile(picked,
                aiProcess_JoinIdenticalVertices |
                aiProcess_PreTransformVertices |
                aiProcess_GlobalScale);
            if (!scene)
            {
                gViewportToast = "FBX import failed";
                gViewportToastUntil = glfwGetTime() + 2.0;
            }
            else
            {
                std::string warn;
                std::error_code ec;
                std::filesystem::path meshesDir = targetDir / "mesh";
                std::filesystem::create_directories(meshesDir, ec);
                std::filesystem::path animDir = targetDir / "anim";
                std::filesystem::create_directories(animDir, ec);
                std::filesystem::path meshOut = meshesDir / inPath.filename();
                meshOut.replace_extension(".nebmesh");
                std::vector<uint32_t> provMesh;
                std::vector<uint32_t> provVert;
                if (!ec && ExportNebMesh(scene, meshOut, warn, &provMesh, &provVert))
                {
                    importedCount++;
                    bool exactProvenanceReady = false;
                    // Embedded animation metadata sidecar for NebMesh Inspector.
                    NebMeshEmbeddedAnimMeta embMeta;
                    Assimp::Importer metaImporter;
                    const aiScene* metaScene = metaImporter.ReadFile(
                        picked,
                        aiProcess_JoinIdenticalVertices |
                        aiProcess_GlobalScale);
                    BuildDefaultEmbeddedMetaFromScene(metaScene, embMeta);
                    {
                        std::error_code rec;
                        if (!gProjectDir.empty())
                        {
                            std::filesystem::path rel = std::filesystem::relative(inPath, std::filesystem::path(gProjectDir), rec);
                            embMeta.sourceFbxPath = rec ? inPath.generic_string() : rel.generic_string();
                        }
                        else
                        {
                            embMeta.sourceFbxPath = inPath.generic_string();
                        }
                    }
                    embMeta.provenanceVersion = 1;
                    NebMesh finalMeshCheck;
                    const bool finalMeshOk = LoadNebMesh(meshOut, finalMeshCheck) && finalMeshCheck.valid;
                    const size_t finalNebVerts = finalMeshOk ? finalMeshCheck.positions.size() : 0;
                    std::vector<Vec3> sourcePos;
                    std::vector<Vec3> sourceUv;
                    std::vector<uint32_t> sourceMeshIdx;
                    std::vector<uint32_t> sourceVertIdx;
                    for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
                    {
                        const aiMesh* mesh = scene->mMeshes[m];
                        if (!mesh) continue;

                        int bestUvChannel = -1;
                        float bestSpan = -1.0f;
                        for (int ch = 0; ch < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++ch)
                        {
                            if (!(mesh->HasTextureCoords(ch) && mesh->mNumUVComponents[ch] >= 2 && mesh->mNumVertices > 0)) continue;
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
                                bestSpan = span;
                                bestUvChannel = ch;
                            }
                        }

                        for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
                        {
                            aiVector3D p = mesh->mVertices[v];
                            sourcePos.push_back(ApplyImportBasis(Vec3{ p.x, p.y, p.z }));
                            if (bestUvChannel >= 0)
                            {
                                aiVector3D uv = mesh->mTextureCoords[bestUvChannel][v];
                                sourceUv.push_back(Vec3{ uv.x, uv.y, 0.0f });
                            }
                            else
                            {
                                sourceUv.push_back(Vec3{ 0.0f, 0.0f, 0.0f });
                            }
                            sourceMeshIdx.push_back((uint32_t)m);
                            sourceVertIdx.push_back((uint32_t)v);
                        }
                    }

                    std::vector<uint32_t> remapProvMesh;
                    std::vector<uint32_t> remapProvVert;
                    remapProvMesh.reserve(finalNebVerts);
                    remapProvVert.reserve(finalNebVerts);
                    if (finalMeshOk && finalNebVerts > 0 && sourcePos.size() == sourceUv.size() && sourcePos.size() == sourceMeshIdx.size() && sourcePos.size() == sourceVertIdx.size())
                    {
                        std::vector<unsigned char> used(sourcePos.size(), 0);
                        const bool finalHasUv = finalMeshCheck.hasUv && finalMeshCheck.uvs.size() == finalMeshCheck.positions.size();
                        const float uvWeight = 0.1f; // Small UV term to stabilize ties across duplicated positions.
                        for (size_t i = 0; i < finalNebVerts; ++i)
                        {
                            const Vec3& p = finalMeshCheck.positions[i];
                            const Vec3 uv = finalHasUv ? finalMeshCheck.uvs[i] : Vec3{ 0.0f, 0.0f, 0.0f };
                            int best = -1;
                            float bestScore = FLT_MAX;

                            for (size_t j = 0; j < sourcePos.size(); ++j)
                            {
                                if (used[j]) continue;
                                const Vec3& q = sourcePos[j];
                                const Vec3& quv = sourceUv[j];
                                const float dx = p.x - q.x;
                                const float dy = p.y - q.y;
                                const float dz = p.z - q.z;
                                const float du = uv.x - quv.x;
                                const float dv = uv.y - quv.y;
                                const float score = (dx * dx + dy * dy + dz * dz) + uvWeight * (du * du + dv * dv);
                                if (score < bestScore) { bestScore = score; best = (int)j; }
                            }

                            if (best < 0)
                            {
                                for (size_t j = 0; j < sourcePos.size(); ++j)
                                {
                                    const Vec3& q = sourcePos[j];
                                    const Vec3& quv = sourceUv[j];
                                    const float dx = p.x - q.x;
                                    const float dy = p.y - q.y;
                                    const float dz = p.z - q.z;
                                    const float du = uv.x - quv.x;
                                    const float dv = uv.y - quv.y;
                                    const float score = (dx * dx + dy * dy + dz * dz) + uvWeight * (du * du + dv * dv);
                                    if (score < bestScore) { bestScore = score; best = (int)j; }
                                }
                            }

                            if (best >= 0)
                            {
                                used[(size_t)best] = 1;
                                remapProvMesh.push_back(sourceMeshIdx[(size_t)best]);
                                remapProvVert.push_back(sourceVertIdx[(size_t)best]);
                            }
                        }
                    }

                    embMeta.exportedVertexCount = (uint32_t)finalNebVerts;
                    const bool provCountOk =
                        finalMeshOk &&
                        remapProvMesh.size() == finalNebVerts &&
                        remapProvVert.size() == finalNebVerts &&
                        finalNebVerts > 0;
                    size_t firstInvalidProvenanceIndex = SIZE_MAX;
                    bool provenancePairsValid = provCountOk;
                    if (provenancePairsValid)
                    {
                        for (size_t i = 0; i < finalNebVerts; ++i)
                        {
                            const uint32_t sm = remapProvMesh[i];
                            const uint32_t sv = remapProvVert[i];
                            if (sm >= scene->mNumMeshes || !scene->mMeshes[sm] || sv >= scene->mMeshes[sm]->mNumVertices)
                            {
                                provenancePairsValid = false;
                                firstInvalidProvenanceIndex = i;
                                break;
                            }
                        }
                    }
                    else
                    {
                        firstInvalidProvenanceIndex = std::min(remapProvMesh.size(), remapProvVert.size());
                    }
                    const bool provenanceExact = provCountOk && provenancePairsValid;
                    printf("[AnimMapRoundtrip] sourceMergedVerts=%zu\n", sourcePos.size());
                    printf("[AnimMapRoundtrip] exportProvCount=%zu\n", std::min(provMesh.size(), provVert.size()));
                    printf("[AnimMapRoundtrip] loadFinalVerts=%zu\n", finalNebVerts);
                    printf("[AnimMapRoundtrip] remapCount=%zu\n", std::min(remapProvMesh.size(), remapProvVert.size()));
                    printf("[AnimMapRoundtrip] valid=%d\n", provenanceExact ? 1 : 0);
                    printf("[AnimMapRoundtrip] firstInvalid=%zu\n", (firstInvalidProvenanceIndex == SIZE_MAX) ? (size_t)-1 : firstInvalidProvenanceIndex);

                    if (provenanceExact)
                    {
                        embMeta.provenanceVersion = 1;
                        embMeta.provenanceMeshIndices = remapProvMesh;
                        embMeta.provenanceVertexIndices = remapProvVert;
                        embMeta.mappingQuality = "exact";
                        embMeta.mappingOk = true;
                        embMeta.mappingVerified = true;
                        exactProvenanceReady = true;
                    }
                    else
                    {
                        embMeta.provenanceVersion = 1;
                        embMeta.provenanceMeshIndices.clear();
                        embMeta.provenanceVertexIndices.clear();
                        embMeta.mappingQuality = "missing";
                        embMeta.mappingOk = false;
                        embMeta.mappingVerified = false;
                        if (!warn.empty()) warn += " | ";
                        warn += "Exact provenance unavailable: post-load provenance remap mismatch/invalid";
                        gViewportToast = "Exact provenance unavailable: missing/invalid provenance";
                    }
                    const char* writeQuality = provenanceExact ? "exact" : "missing";
                    printf("[AnimMetaWrite] mesh=%s\n", meshOut.generic_string().c_str());
                    printf("[AnimMetaWrite] finalNebVerts=%zu\n", finalNebVerts);
                    printf("[AnimMetaWrite] exportedVertexCount=%u\n", embMeta.exportedVertexCount);
                    printf("[AnimMetaWrite] provMeshCount=%zu\n", embMeta.provenanceMeshIndices.size());
                    printf("[AnimMetaWrite] provVertCount=%zu\n", embMeta.provenanceVertexIndices.size());
                    printf("[AnimMetaWrite] quality=%s\n", writeQuality);

                    printf("[AnimMap] stage=%s\n", provenanceExact ? "export_animmeta_bind" : "fail_provenance_count_mismatch");
                    printf("[AnimMap] mode=%s\n", provenanceExact ? "provenance_capture_exact" : "missing_provenance");
                    printf("[AnimMap] finalNebVerts=%zu\n", finalNebVerts);
                    printf("[AnimMap] provenanceCount=%zu\n", embMeta.provenanceMeshIndices.size());
                    printf("[AnimMap] provenanceValid=%d\n", provenanceExact ? 1 : 0);

                    if (!SaveNebMeshEmbeddedMeta(meshOut, embMeta))
                    {
                        if (!warn.empty()) warn += " | ";
                        warn += "Failed to write embedded anim metadata";
                        gViewportToast = "Exact provenance unavailable: failed to write animmeta";
                    }

                    int matCount = ImportModelTexturesAndGenerateMaterials(scene, inPath, targetDir, meshOut, warn);
                    gViewportToast = "Imported " + meshOut.filename().string();
                    if (matCount > 0)
                        gViewportToast += " + " + std::to_string(matCount) + " material slot(s)";
                    if (!exactProvenanceReady)
                        gViewportToast += " | Exact provenance unavailable";
                    if (!warn.empty())
                        gImportWarning = warn;
                }
                else
                {
                    gViewportToast = "Mesh export failed";
                }
                gViewportToastUntil = glfwGetTime() + 2.0;
            }
        }
        else
        {
            gViewportToast = "Unsupported file";
            gViewportToastUntil = glfwGetTime() + 2.0;
        }
    }

    if (pickedList.size() > 1)
    {
        gViewportToast = "Imported " + std::to_string(importedCount) + " / " + std::to_string((int)pickedList.size()) + " assets";
        gViewportToastUntil = glfwGetTime() + 2.0;
    }
}
