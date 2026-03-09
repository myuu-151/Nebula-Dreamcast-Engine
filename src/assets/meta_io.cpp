#include "meta_io.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>

namespace NebulaAssets
{
    bool LoadMaterialTexture(const std::filesystem::path& matPath, std::string& outTex)
    {
        std::ifstream in(matPath);
        if (!in.is_open()) return false;
        std::string line;
        while (std::getline(in, line))
        {
            if (line.rfind("texture=", 0) == 0)
            {
                outTex = line.substr(8);
                return true;
            }
        }
        return false;
    }

    bool LoadMaterialUvScale(const std::filesystem::path& matPath, float& outUvScale)
    {
        outUvScale = 0.0f;
        std::ifstream in(matPath);
        if (!in.is_open()) return false;
        std::string line;
        while (std::getline(in, line))
        {
            if (line.rfind("uv_scale=", 0) == 0)
            {
                outUvScale = (float)atof(line.substr(9).c_str());
                return true;
            }
        }
        return false;
    }

    bool LoadMaterialAllowUvRepeat(const std::filesystem::path& matPath, bool& outAllowUvRepeat)
    {
        outAllowUvRepeat = false;
        std::ifstream in(matPath);
        if (!in.is_open()) return false;
        std::string line;
        while (std::getline(in, line))
        {
            if (line.rfind("saturn_allow_uv_repeat=", 0) == 0)
            {
                outAllowUvRepeat = (line.substr(23) == "1");
                return true;
            }
        }
        return false;
    }

    void LoadMaterialUvTransform(const std::filesystem::path& matPath, float& su, float& sv, float& ou, float& ov, float& rotDeg)
    {
        su = 1.0f;
        sv = 1.0f;
        ou = 0.0f;
        ov = 0.0f;
        rotDeg = 0.0f;
        std::ifstream in(matPath);
        if (!in.is_open()) return;
        std::string line;
        while (std::getline(in, line))
        {
            if (line.rfind("uv_scale_u=", 0) == 0) su = (float)atof(line.substr(11).c_str());
            else if (line.rfind("uv_scale_v=", 0) == 0) sv = (float)atof(line.substr(11).c_str());
            else if (line.rfind("uv_offset_u=", 0) == 0) ou = (float)atof(line.substr(12).c_str());
            else if (line.rfind("uv_offset_v=", 0) == 0) ov = (float)atof(line.substr(12).c_str());
            else if (line.rfind("uv_rotate_deg=", 0) == 0) rotDeg = (float)atof(line.substr(14).c_str());
        }
    }

    static float LoadMaterialFloatField(const std::filesystem::path& matPath, const char* key, float defaultVal)
    {
        std::ifstream in(matPath);
        if (!in.is_open()) return defaultVal;
        std::string prefix = std::string(key) + "=";
        std::string line;
        while (std::getline(in, line))
        {
            if (line.rfind(prefix, 0) == 0)
                return (float)atof(line.substr(prefix.size()).c_str());
        }
        return defaultVal;
    }

    static int LoadMaterialIntField(const std::filesystem::path& matPath, const char* key, int defaultVal)
    {
        std::ifstream in(matPath);
        if (!in.is_open()) return defaultVal;
        std::string prefix = std::string(key) + "=";
        std::string line;
        while (std::getline(in, line))
        {
            if (line.rfind(prefix, 0) == 0)
                return atoi(line.substr(prefix.size()).c_str());
        }
        return defaultVal;
    }

    int LoadMaterialShadingMode(const std::filesystem::path& matPath)
    {
        return LoadMaterialIntField(matPath, "shading", 0);
    }

    float LoadMaterialLightRotation(const std::filesystem::path& matPath)
    {
        return LoadMaterialFloatField(matPath, "light_rotation", 0.0f);
    }

    float LoadMaterialLightPitch(const std::filesystem::path& matPath)
    {
        return LoadMaterialFloatField(matPath, "light_pitch", 0.0f);
    }

    float LoadMaterialLightRoll(const std::filesystem::path& matPath)
    {
        return LoadMaterialFloatField(matPath, "light_roll", 0.0f);
    }

    float LoadMaterialShadowIntensity(const std::filesystem::path& matPath)
    {
        return LoadMaterialFloatField(matPath, "shadow_intensity", 1.0f);
    }

    int LoadMaterialShadingUv(const std::filesystem::path& matPath)
    {
        return LoadMaterialIntField(matPath, "shading_uv", -1);
    }

    bool SaveMaterialAllFields(const std::filesystem::path& matPath, const std::string& tex, float uvScale, bool allowUvRepeat, float su, float sv, float ou, float ov, float rotDeg, int shadingMode, float lightRotation, float lightPitch, float lightRoll, float shadowIntensity, int shadingUv)
    {
        std::ofstream out(matPath, std::ios::out | std::ios::trunc);
        if (!out.is_open()) return false;
        out << "texture=" << tex << "\n";
        out << "uv_scale=" << uvScale << "\n";
        out << "saturn_allow_uv_repeat=" << (allowUvRepeat ? 1 : 0) << "\n";
        out << "uv_scale_u=" << su << "\n";
        out << "uv_scale_v=" << sv << "\n";
        out << "uv_offset_u=" << ou << "\n";
        out << "uv_offset_v=" << ov << "\n";
        out << "uv_rotate_deg=" << rotDeg << "\n";
        out << "shading=" << shadingMode << "\n";
        out << "light_rotation=" << lightRotation << "\n";
        out << "light_pitch=" << lightPitch << "\n";
        out << "light_roll=" << lightRoll << "\n";
        out << "shadow_intensity=" << shadowIntensity << "\n";
        out << "shading_uv=" << shadingUv << "\n";
        return true;
    }

    // Helper: read all material fields for round-trip save
    struct MatFields {
        std::string tex;
        float uvScale = 0.0f;
        bool allowUvRepeat = false;
        float su = 1.0f, sv = 1.0f, ou = 0.0f, ov = 0.0f, rotDeg = 0.0f;
        int shadingMode = 0;
        float lightRotation = 0.0f, lightPitch = 0.0f, lightRoll = 0.0f, shadowIntensity = 1.0f;
        int shadingUv = -1;
    };
    static MatFields ReadAllMatFields(const std::filesystem::path& matPath)
    {
        MatFields f;
        LoadMaterialTexture(matPath, f.tex);
        LoadMaterialUvScale(matPath, f.uvScale);
        LoadMaterialAllowUvRepeat(matPath, f.allowUvRepeat);
        LoadMaterialUvTransform(matPath, f.su, f.sv, f.ou, f.ov, f.rotDeg);
        f.shadingMode = LoadMaterialShadingMode(matPath);
        f.lightRotation = LoadMaterialLightRotation(matPath);
        f.lightPitch = LoadMaterialLightPitch(matPath);
        f.lightRoll = LoadMaterialLightRoll(matPath);
        f.shadowIntensity = LoadMaterialShadowIntensity(matPath);
        f.shadingUv = LoadMaterialShadingUv(matPath);
        return f;
    }

    bool SaveMaterialTexture(const std::filesystem::path& matPath, const std::string& tex)
    {
        auto f = ReadAllMatFields(matPath);
        return SaveMaterialAllFields(matPath, tex, f.uvScale, f.allowUvRepeat, f.su, f.sv, f.ou, f.ov, f.rotDeg, f.shadingMode, f.lightRotation, f.lightPitch, f.lightRoll, f.shadowIntensity, f.shadingUv);
    }

    bool SaveMaterialUvScale(const std::filesystem::path& matPath, float uvScale)
    {
        auto f = ReadAllMatFields(matPath);
        return SaveMaterialAllFields(matPath, f.tex, uvScale, f.allowUvRepeat, f.su, f.sv, f.ou, f.ov, f.rotDeg, f.shadingMode, f.lightRotation, f.lightPitch, f.lightRoll, f.shadowIntensity, f.shadingUv);
    }

    bool SaveMaterialAllowUvRepeat(const std::filesystem::path& matPath, bool allowUvRepeat)
    {
        auto f = ReadAllMatFields(matPath);
        return SaveMaterialAllFields(matPath, f.tex, f.uvScale, allowUvRepeat, f.su, f.sv, f.ou, f.ov, f.rotDeg, f.shadingMode, f.lightRotation, f.lightPitch, f.lightRoll, f.shadowIntensity, f.shadingUv);
    }

    bool SaveMaterialUvTransform(const std::filesystem::path& matPath, float su, float sv, float ou, float ov, float rotDeg)
    {
        auto f = ReadAllMatFields(matPath);
        return SaveMaterialAllFields(matPath, f.tex, f.uvScale, f.allowUvRepeat, su, sv, ou, ov, rotDeg, f.shadingMode, f.lightRotation, f.lightPitch, f.lightRoll, f.shadowIntensity, f.shadingUv);
    }

    bool SaveMaterialShadingMode(const std::filesystem::path& matPath, int mode)
    {
        auto f = ReadAllMatFields(matPath);
        return SaveMaterialAllFields(matPath, f.tex, f.uvScale, f.allowUvRepeat, f.su, f.sv, f.ou, f.ov, f.rotDeg, mode, f.lightRotation, f.lightPitch, f.lightRoll, f.shadowIntensity, f.shadingUv);
    }

    bool SaveMaterialLightRotation(const std::filesystem::path& matPath, float rotation)
    {
        auto f = ReadAllMatFields(matPath);
        return SaveMaterialAllFields(matPath, f.tex, f.uvScale, f.allowUvRepeat, f.su, f.sv, f.ou, f.ov, f.rotDeg, f.shadingMode, rotation, f.lightPitch, f.lightRoll, f.shadowIntensity, f.shadingUv);
    }

    bool SaveMaterialLightPitch(const std::filesystem::path& matPath, float pitch)
    {
        auto f = ReadAllMatFields(matPath);
        return SaveMaterialAllFields(matPath, f.tex, f.uvScale, f.allowUvRepeat, f.su, f.sv, f.ou, f.ov, f.rotDeg, f.shadingMode, f.lightRotation, pitch, f.lightRoll, f.shadowIntensity, f.shadingUv);
    }

    bool SaveMaterialLightRoll(const std::filesystem::path& matPath, float roll)
    {
        auto f = ReadAllMatFields(matPath);
        return SaveMaterialAllFields(matPath, f.tex, f.uvScale, f.allowUvRepeat, f.su, f.sv, f.ou, f.ov, f.rotDeg, f.shadingMode, f.lightRotation, f.lightPitch, roll, f.shadowIntensity, f.shadingUv);
    }

    bool SaveMaterialShadowIntensity(const std::filesystem::path& matPath, float intensity)
    {
        auto f = ReadAllMatFields(matPath);
        return SaveMaterialAllFields(matPath, f.tex, f.uvScale, f.allowUvRepeat, f.su, f.sv, f.ou, f.ov, f.rotDeg, f.shadingMode, f.lightRotation, f.lightPitch, f.lightRoll, intensity, f.shadingUv);
    }

    bool SaveMaterialShadingUv(const std::filesystem::path& matPath, int uvIndex)
    {
        auto f = ReadAllMatFields(matPath);
        return SaveMaterialAllFields(matPath, f.tex, f.uvScale, f.allowUvRepeat, f.su, f.sv, f.ou, f.ov, f.rotDeg, f.shadingMode, f.lightRotation, f.lightPitch, f.lightRoll, f.shadowIntensity, uvIndex);
    }

    std::filesystem::path GetNebSlotsPathForMesh(const std::filesystem::path& absMeshPath)
    {
        return absMeshPath.parent_path() / "nebslot" / (absMeshPath.stem().string() + ".nebslots");
    }

    bool LoadNebSlotsManifestFile(const std::filesystem::path& slotFilePath, std::vector<std::string>& outSlots, const std::string& projectDir)
    {
        outSlots.clear();
        std::ifstream in(slotFilePath);
        if (!in.is_open()) return false;

        std::string line;
        while (std::getline(in, line))
        {
            if (line.rfind("slot", 0) != 0) continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos || eq <= 4) continue;
            int idx = atoi(line.substr(4, eq - 4).c_str());
            if (idx < 1 || idx > kStaticMeshMaterialSlots) continue;
            std::string val = line.substr(eq + 1);

            if (!val.empty() && !projectDir.empty())
            {
                std::filesystem::path absMat = std::filesystem::path(projectDir) / val;
                if (!std::filesystem::exists(absMat))
                {
                    std::filesystem::path fallback = slotFilePath.parent_path().parent_path() / "mat" / std::filesystem::path(val).filename();
                    if (std::filesystem::exists(fallback))
                    {
                        std::error_code ec;
                        std::filesystem::path rel = std::filesystem::relative(fallback, std::filesystem::path(projectDir), ec);
                        val = ec ? fallback.filename().generic_string() : rel.generic_string();
                    }
                }
            }

            if ((int)outSlots.size() < idx) outSlots.resize(idx);
            outSlots[idx - 1] = val;
        }
        return !outSlots.empty();
    }

    bool LoadNebSlotsManifest(const std::filesystem::path& absMeshPath, std::vector<std::string>& outSlots, const std::string& projectDir)
    {
        return LoadNebSlotsManifestFile(GetNebSlotsPathForMesh(absMeshPath), outSlots, projectDir);
    }

    bool SaveNebSlotsManifest(const std::filesystem::path& absMeshPath, const std::vector<std::string>& slotMaterials)
    {
        std::filesystem::path slotPath = GetNebSlotsPathForMesh(absMeshPath);
        std::filesystem::create_directories(slotPath.parent_path());
        std::ofstream out(slotPath, std::ios::out | std::ios::trunc);
        if (!out.is_open()) return false;

        for (int i = 0; i < kStaticMeshMaterialSlots; ++i)
        {
            std::string v = (i < (int)slotMaterials.size()) ? slotMaterials[i] : std::string();
            out << "slot" << (i + 1) << "=" << v << "\n";
        }
        return true;
    }

    std::filesystem::path GetNebTexMetaPath(const std::filesystem::path& nebtexPath)
    {
        return std::filesystem::path(nebtexPath.string() + ".meta");
    }

    int LoadNebTexWrapMode(const std::filesystem::path& nebtexPath)
    {
        std::ifstream in(GetNebTexMetaPath(nebtexPath));
        if (!in.is_open()) return 0;
        std::string line;
        while (std::getline(in, line))
        {
            if (line.rfind("wrap=", 0) == 0)
            {
                std::string v = line.substr(5);
                std::transform(v.begin(), v.end(), v.begin(), ::tolower);
                if (v == "repeat") return 0;
                if (v == "extend") return 1;
                if (v == "clip") return 2;
                if (v == "mirror") return 3;
            }
        }
        return 0;
    }

    int LoadNebTexSaturnNpotMode(const std::filesystem::path& nebtexPath)
    {
        std::ifstream in(GetNebTexMetaPath(nebtexPath));
        if (!in.is_open()) return 0;
        std::string line;
        while (std::getline(in, line))
        {
            if (line.rfind("npot=", 0) == 0)
            {
                std::string v = line.substr(5);
                std::transform(v.begin(), v.end(), v.begin(), ::tolower);
                if (v == "pad") return 0;
                if (v == "resample") return 1;
            }
        }
        return 0;
    }

    bool LoadNebTexAllowUvRepeat(const std::filesystem::path& nebtexPath)
    {
        std::ifstream in(GetNebTexMetaPath(nebtexPath));
        if (!in.is_open()) return false;
        std::string line;
        while (std::getline(in, line))
        {
            if (line.rfind("saturn_allow_uv_repeat=", 0) == 0)
                return (line.substr(23) == "1");
        }
        return false;
    }

    int LoadNebTexFilterMode(const std::filesystem::path& nebtexPath)
    {
        std::ifstream in(GetNebTexMetaPath(nebtexPath));
        if (!in.is_open()) return 1;
        std::string line;
        while (std::getline(in, line))
        {
            if (line.rfind("filter=", 0) == 0)
            {
                std::string v = line.substr(7);
                std::transform(v.begin(), v.end(), v.begin(), ::tolower);
                if (v == "nearest") return 0;
                if (v == "bilinear" || v == "linear") return 1;
            }
        }
        return 1;
    }

    bool SaveNebTexWrapMode(const std::filesystem::path& nebtexPath, int mode)
    {
        const char* names[] = { "repeat", "extend", "clip", "mirror" };
        if (mode < 0 || mode > 3) mode = 0;

        bool flipU = false;
        bool flipV = false;
        int saturnNpot = 1;
        bool allowUvRepeat = false;
        int filterMode = 1;
        std::ifstream in(GetNebTexMetaPath(nebtexPath));
        std::string line;
        while (std::getline(in, line))
        {
            if (line.rfind("flip_u=", 0) == 0) flipU = (line.substr(7) == "1");
            if (line.rfind("flip_v=", 0) == 0) flipV = (line.substr(7) == "1");
            if (line.rfind("npot=", 0) == 0)
            {
                std::string v = line.substr(5);
                std::transform(v.begin(), v.end(), v.begin(), ::tolower);
                saturnNpot = (v == "pad") ? 0 : 1;
            }
            if (line.rfind("saturn_allow_uv_repeat=", 0) == 0)
                allowUvRepeat = (line.substr(23) == "1");
            if (line.rfind("filter=", 0) == 0)
            {
                std::string v = line.substr(7);
                std::transform(v.begin(), v.end(), v.begin(), ::tolower);
                filterMode = (v == "nearest") ? 0 : 1;
            }
        }

        std::ofstream out(GetNebTexMetaPath(nebtexPath), std::ios::out | std::ios::trunc);
        if (!out.is_open()) return false;
        out << "wrap=" << names[mode] << "\n";
        out << "flip_u=" << (flipU ? 1 : 0) << "\n";
        out << "flip_v=" << (flipV ? 1 : 0) << "\n";
        out << "npot=" << (saturnNpot == 0 ? "pad" : "resample") << "\n";
        out << "saturn_allow_uv_repeat=" << (allowUvRepeat ? 1 : 0) << "\n";
        out << "filter=" << (filterMode == 0 ? "nearest" : "bilinear") << "\n";
        return true;
    }

    void LoadNebTexFlipOptions(const std::filesystem::path& nebtexPath, bool& flipU, bool& flipV)
    {
        flipU = false;
        flipV = false;
        std::ifstream in(GetNebTexMetaPath(nebtexPath));
        if (!in.is_open()) return;
        std::string line;
        while (std::getline(in, line))
        {
            if (line.rfind("flip_u=", 0) == 0) flipU = (line.substr(7) == "1");
            else if (line.rfind("flip_v=", 0) == 0) flipV = (line.substr(7) == "1");
        }
    }

    bool ReadNebTexDimensions(const std::filesystem::path& nebtexPath, int& outW, int& outH)
    {
        outW = 0;
        outH = 0;
        std::ifstream in(nebtexPath, std::ios::binary | std::ios::in);
        if (!in.is_open()) return false;

        char magic[4];
        if (!in.read(magic, 4)) return false;
        if (!(magic[0] == 'N' && magic[1] == 'E' && magic[2] == 'B' && magic[3] == 'T')) return false;

        auto readU16BE_local = [&](uint16_t& out) -> bool {
            unsigned char b[2];
            if (!in.read((char*)b, 2)) return false;
            out = (uint16_t)((b[0] << 8) | b[1]);
            return true;
        };

        uint16_t w = 0;
        uint16_t h = 0;
        uint16_t format = 0;
        uint16_t flags = 0;
        if (!readU16BE_local(w) || !readU16BE_local(h) || !readU16BE_local(format) || !readU16BE_local(flags)) return false;
        outW = (int)w;
        outH = (int)h;
        return (outW > 0 && outH > 0);
    }

    bool SaveNebTexSaturnNpotMode(const std::filesystem::path& nebtexPath, int mode)
    {
        int wrapMode = LoadNebTexWrapMode(nebtexPath);
        bool flipU = false;
        bool flipV = false;
        bool allowUvRepeat = LoadNebTexAllowUvRepeat(nebtexPath);
        int filterMode = LoadNebTexFilterMode(nebtexPath);
        LoadNebTexFlipOptions(nebtexPath, flipU, flipV);
        const char* wrapNames[] = { "repeat", "extend", "clip", "mirror" };
        if (wrapMode < 0 || wrapMode > 3) wrapMode = 0;
        if (mode < 0 || mode > 1) mode = 1;

        std::ofstream out(GetNebTexMetaPath(nebtexPath), std::ios::out | std::ios::trunc);
        if (!out.is_open()) return false;
        out << "wrap=" << wrapNames[wrapMode] << "\n";
        out << "flip_u=" << (flipU ? 1 : 0) << "\n";
        out << "flip_v=" << (flipV ? 1 : 0) << "\n";
        out << "npot=" << (mode == 0 ? "pad" : "resample") << "\n";
        out << "saturn_allow_uv_repeat=" << (allowUvRepeat ? 1 : 0) << "\n";
        out << "filter=" << (filterMode == 0 ? "nearest" : "bilinear") << "\n";
        return true;
    }

    bool SaveNebTexFlipOptions(const std::filesystem::path& nebtexPath, bool flipU, bool flipV)
    {
        int wrapMode = LoadNebTexWrapMode(nebtexPath);
        int saturnNpot = LoadNebTexSaturnNpotMode(nebtexPath);
        bool allowUvRepeat = LoadNebTexAllowUvRepeat(nebtexPath);
        int filterMode = LoadNebTexFilterMode(nebtexPath);
        const char* names[] = { "repeat", "extend", "clip", "mirror" };
        if (wrapMode < 0 || wrapMode > 3) wrapMode = 0;
        std::ofstream out(GetNebTexMetaPath(nebtexPath), std::ios::out | std::ios::trunc);
        if (!out.is_open()) return false;
        out << "wrap=" << names[wrapMode] << "\n";
        out << "flip_u=" << (flipU ? 1 : 0) << "\n";
        out << "flip_v=" << (flipV ? 1 : 0) << "\n";
        out << "npot=" << (saturnNpot == 0 ? "pad" : "resample") << "\n";
        out << "saturn_allow_uv_repeat=" << (allowUvRepeat ? 1 : 0) << "\n";
        out << "filter=" << (filterMode == 0 ? "nearest" : "bilinear") << "\n";
        return true;
    }

    bool SaveNebTexAllowUvRepeat(const std::filesystem::path& nebtexPath, bool allowUvRepeat)
    {
        int wrapMode = LoadNebTexWrapMode(nebtexPath);
        int saturnNpot = LoadNebTexSaturnNpotMode(nebtexPath);
        int filterMode = LoadNebTexFilterMode(nebtexPath);
        bool flipU = false;
        bool flipV = false;
        LoadNebTexFlipOptions(nebtexPath, flipU, flipV);
        const char* names[] = { "repeat", "extend", "clip", "mirror" };
        if (wrapMode < 0 || wrapMode > 3) wrapMode = 0;

        std::ofstream out(GetNebTexMetaPath(nebtexPath), std::ios::out | std::ios::trunc);
        if (!out.is_open()) return false;
        out << "wrap=" << names[wrapMode] << "\n";
        out << "flip_u=" << (flipU ? 1 : 0) << "\n";
        out << "flip_v=" << (flipV ? 1 : 0) << "\n";
        out << "npot=" << (saturnNpot == 0 ? "pad" : "resample") << "\n";
        out << "saturn_allow_uv_repeat=" << (allowUvRepeat ? 1 : 0) << "\n";
        out << "filter=" << (filterMode == 0 ? "nearest" : "bilinear") << "\n";
        return true;
    }

    bool SaveNebTexFilterMode(const std::filesystem::path& nebtexPath, int filterMode)
    {
        int wrapMode = LoadNebTexWrapMode(nebtexPath);
        int saturnNpot = LoadNebTexSaturnNpotMode(nebtexPath);
        bool allowUvRepeat = LoadNebTexAllowUvRepeat(nebtexPath);
        bool flipU = false;
        bool flipV = false;
        LoadNebTexFlipOptions(nebtexPath, flipU, flipV);
        const char* names[] = { "repeat", "extend", "clip", "mirror" };
        if (wrapMode < 0 || wrapMode > 3) wrapMode = 0;
        if (filterMode < 0 || filterMode > 1) filterMode = 1;

        std::ofstream out(GetNebTexMetaPath(nebtexPath), std::ios::out | std::ios::trunc);
        if (!out.is_open()) return false;
        out << "wrap=" << names[wrapMode] << "\n";
        out << "flip_u=" << (flipU ? 1 : 0) << "\n";
        out << "flip_v=" << (flipV ? 1 : 0) << "\n";
        out << "npot=" << (saturnNpot == 0 ? "pad" : "resample") << "\n";
        out << "saturn_allow_uv_repeat=" << (allowUvRepeat ? 1 : 0) << "\n";
        out << "filter=" << (filterMode == 0 ? "nearest" : "bilinear") << "\n";
        return true;
    }
}
