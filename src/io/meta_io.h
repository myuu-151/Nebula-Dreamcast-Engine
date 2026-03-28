#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace NebulaAssets
{
    constexpr int kStaticMeshMaterialSlots = 14;

    bool LoadMaterialTexture(const std::filesystem::path& matPath, std::string& outTex);
    bool LoadMaterialUvScale(const std::filesystem::path& matPath, float& outUvScale);
    bool LoadMaterialAllowUvRepeat(const std::filesystem::path& matPath, bool& outAllowUvRepeat);
    void LoadMaterialUvTransform(const std::filesystem::path& matPath, float& su, float& sv, float& ou, float& ov, float& rotDeg);

    int LoadMaterialShadingMode(const std::filesystem::path& matPath);
    float LoadMaterialLightRotation(const std::filesystem::path& matPath);
    float LoadMaterialLightPitch(const std::filesystem::path& matPath);
    float LoadMaterialLightRoll(const std::filesystem::path& matPath);
    float LoadMaterialShadowIntensity(const std::filesystem::path& matPath);
    int LoadMaterialShadingUv(const std::filesystem::path& matPath);
    float LoadMaterialStipple(const std::filesystem::path& matPath);
    void LoadMaterialStippleTint(const std::filesystem::path& matPath, float& r, float& g, float& b);
    float LoadMaterialStippleIntensity(const std::filesystem::path& matPath);
    float LoadMaterialStippleAlpha(const std::filesystem::path& matPath);
    float LoadMaterialStippleRamp(const std::filesystem::path& matPath);
    int LoadMaterialCullFace(const std::filesystem::path& matPath);
    float LoadMaterialOpacity(const std::filesystem::path& matPath);

    // Consolidated save — all individual saves round-trip through this
    bool SaveMaterialAllFieldsV2(const std::filesystem::path& matPath);
    bool SaveMaterialTexture(const std::filesystem::path& matPath, const std::string& tex);
    bool SaveMaterialUvScale(const std::filesystem::path& matPath, float uvScale);
    bool SaveMaterialAllowUvRepeat(const std::filesystem::path& matPath, bool allowUvRepeat);
    bool SaveMaterialUvTransform(const std::filesystem::path& matPath, float su, float sv, float ou, float ov, float rotDeg);
    bool SaveMaterialShadingMode(const std::filesystem::path& matPath, int mode);
    bool SaveMaterialLightRotation(const std::filesystem::path& matPath, float rotation);
    bool SaveMaterialLightPitch(const std::filesystem::path& matPath, float pitch);
    bool SaveMaterialLightRoll(const std::filesystem::path& matPath, float roll);
    bool SaveMaterialShadowIntensity(const std::filesystem::path& matPath, float intensity);
    bool SaveMaterialShadingUv(const std::filesystem::path& matPath, int uvIndex);
    bool SaveMaterialStipple(const std::filesystem::path& matPath, float stipple);
    bool SaveMaterialStippleTint(const std::filesystem::path& matPath, float r, float g, float b);
    bool SaveMaterialStippleIntensity(const std::filesystem::path& matPath, float intensity);
    bool SaveMaterialStippleAlpha(const std::filesystem::path& matPath, float alpha);
    bool SaveMaterialStippleRamp(const std::filesystem::path& matPath, float ramp);
    bool SaveMaterialCullFace(const std::filesystem::path& matPath, int cull);
    bool SaveMaterialOpacity(const std::filesystem::path& matPath, float opacity);

    std::filesystem::path GetNebSlotsPathForMesh(const std::filesystem::path& absMeshPath);
    bool LoadNebSlotsManifestFile(const std::filesystem::path& slotFilePath, std::vector<std::string>& outSlots, const std::string& projectDir);
    bool LoadNebSlotsManifest(const std::filesystem::path& absMeshPath, std::vector<std::string>& outSlots, const std::string& projectDir);
    bool SaveNebSlotsManifest(const std::filesystem::path& absMeshPath, const std::vector<std::string>& slotMaterials);

    std::filesystem::path GetNebTexMetaPath(const std::filesystem::path& nebtexPath);
    int LoadNebTexWrapMode(const std::filesystem::path& nebtexPath);
    int LoadNebTexSaturnNpotMode(const std::filesystem::path& nebtexPath);
    bool LoadNebTexAllowUvRepeat(const std::filesystem::path& nebtexPath);
    int LoadNebTexFilterMode(const std::filesystem::path& nebtexPath);
    bool SaveNebTexWrapMode(const std::filesystem::path& nebtexPath, int mode);
    void LoadNebTexFlipOptions(const std::filesystem::path& nebtexPath, bool& flipU, bool& flipV);
    bool ReadNebTexDimensions(const std::filesystem::path& nebtexPath, int& outW, int& outH);
    bool SaveNebTexSaturnNpotMode(const std::filesystem::path& nebtexPath, int mode);
    bool SaveNebTexFlipOptions(const std::filesystem::path& nebtexPath, bool flipU, bool flipV);
    bool SaveNebTexAllowUvRepeat(const std::filesystem::path& nebtexPath, bool allowUvRepeat);
    bool SaveNebTexFilterMode(const std::filesystem::path& nebtexPath, int filterMode);
}
