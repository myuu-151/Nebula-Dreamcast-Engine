#pragma once

#include <string>
#include <vector>
#include <filesystem>

struct aiScene;
struct aiMaterial;

// Import-related globals (owned by import_pipeline.cpp)
extern bool        gImportPopupOpen;
extern bool        gImportUseProvenanceMapping;
extern bool        gImportDeltaCompress;
extern bool        gImportDoubleSampleRate;
extern std::string gImportWarning;

// Resolve the texture path referenced by an aiMaterial (BASE_COLOR, DIFFUSE, UNKNOWN).
bool ResolveMaterialTexturePath(const aiScene* scene, const aiMaterial* mat,
                                const std::filesystem::path& modelPath,
                                std::filesystem::path& outTexPath, std::string& warn);

// Fallback: search the model's directory for a texture whose stem matches the material name.
bool FindTextureByMaterialNameFallback(const std::filesystem::path& modelPath,
                                       const std::string& materialName,
                                       std::filesystem::path& outTexPath);

// Import textures referenced by the scene's materials and generate .nebmat + .nebslots.
int ImportModelTexturesAndGenerateMaterials(const aiScene* scene,
                                            const std::filesystem::path& modelPath,
                                            const std::filesystem::path& targetDir,
                                            const std::filesystem::path& meshOut,
                                            std::string& warn);

// Load the .vtxlink sidecar beside a .nebmesh to get the linked animation path.
bool LoadNebMeshVtxAnimLink(const std::filesystem::path& absMeshPath, std::string& outAnimPath);

// Save the .vtxlink sidecar beside a .nebmesh.
bool SaveNebMeshVtxAnimLink(const std::filesystem::path& absMeshPath, const std::string& animPath);

// Import a list of asset files (.fbx, .png, .nebanim, .vtxa) into the current asset folder.
void ImportAssetsToCurrentFolder(const std::vector<std::string>& pickedList);
