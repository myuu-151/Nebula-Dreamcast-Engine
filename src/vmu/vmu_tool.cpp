#include "vmu_tool.h"
#include "../io/texture_io.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdlib>
#include <cstdint>

// --- VMU state ---
bool gShowVmuTool = false;
bool gVmuHasImage = false;
bool gVmuLoadOnBoot = false;
std::string gVmuAssetPath;
std::vector<VmuAnimLayer> gVmuAnimLayers = { {"Layer 1", true, 0, 0} };
int gVmuAnimLayerSel = 0;
int gVmuAnimTotalFrames = 24;
int gVmuAnimPlayhead = 0;
bool gVmuAnimLoop = false;
int gVmuAnimSpeedMode = 1;
int gVmuCurrentLoadedType = 0;
std::string gVmuLinkedPngPath;
std::string gVmuLinkedAnimPath;
std::array<uint8_t, 48 * 32> gVmuMono = {};

bool LoadVmuPngToMono(const std::string& path, std::string& outErr)
{
    unsigned int iw = 0, ih = 0;
    std::vector<unsigned char> bgra;
    std::wstring w = std::filesystem::path(path).wstring();
    if (!LoadImageWIC(w.c_str(), iw, ih, bgra))
    {
        outErr = "VMU Tool: failed to load PNG";
        return false;
    }
    if (iw != 48 || ih != 32)
    {
        outErr = "VMU Tool: PNG must be exactly 48x32";
        return false;
    }

    for (unsigned int y = 0; y < 32; ++y)
    {
        for (unsigned int x = 0; x < 48; ++x)
        {
            size_t i = ((size_t)y * (size_t)iw + (size_t)x) * 4u;
            unsigned char b = bgra[i + 0];
            unsigned char g = bgra[i + 1];
            unsigned char r = bgra[i + 2];
            unsigned char a = bgra[i + 3];
            unsigned char lum = (unsigned char)((30u * r + 59u * g + 11u * b) / 100u);
            gVmuMono[(size_t)y * 48u + (size_t)x] = (a > 127 && lum < 128) ? 1 : 0;
        }
    }

    gVmuHasImage = true;
    gVmuAssetPath = path;
    gVmuCurrentLoadedType = 1;
    return true;
}

bool SaveVmuFrameData(const std::filesystem::path& outPath)
{
    std::ofstream out(outPath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;
    out << "VMUANIM 1\n";
    out << "TOTAL_FRAMES\t" << gVmuAnimTotalFrames << "\n";
    out << "PLAYHEAD\t" << gVmuAnimPlayhead << "\n";
    out << "LOOP\t" << (gVmuAnimLoop ? 1 : 0) << "\n";
    out << "SPEED_MODE\t" << gVmuAnimSpeedMode << "\n";
    out << "LAYER_COUNT\t" << gVmuAnimLayers.size() << "\n";
    for (const auto& l : gVmuAnimLayers)
    {
        std::string nm = l.name;
        for (char& c : nm) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
        out << "LAYER\t" << nm << "\t" << (l.visible ? 1 : 0) << "\t" << l.frameStart << "\t" << l.frameEnd << "\t" << l.linkedAsset << "\n";
    }
    return true;
}

bool LoadVmuFrameData(const std::filesystem::path& inPath)
{
    std::ifstream in(inPath);
    if (!in.is_open()) return false;

    std::vector<VmuAnimLayer> loadedLayers;
    int loadedFrames = 24;
    int loadedPlayhead = 0;
    int loadedLoop = 0;
    int loadedSpeedMode = 1;
    std::string line;
    bool headerOk = false;

    while (std::getline(in, line))
    {
        if (line.rfind("VMUANIM ", 0) == 0) { headerOk = true; continue; }
        if (line.rfind("TOTAL_FRAMES\t", 0) == 0) { loadedFrames = atoi(line.c_str() + 13); continue; }
        if (line.rfind("PLAYHEAD\t", 0) == 0) { loadedPlayhead = atoi(line.c_str() + 9); continue; }
        if (line.rfind("LOOP\t", 0) == 0) { loadedLoop = atoi(line.c_str() + 5); continue; }
        if (line.rfind("SPEED_MODE\t", 0) == 0) { loadedSpeedMode = atoi(line.c_str() + 11); continue; }
        if (line.rfind("LAYER\t", 0) == 0)
        {
            std::vector<std::string> parts;
            size_t start = 0;
            while (start <= line.size())
            {
                size_t p = line.find('\t', start);
                if (p == std::string::npos) { parts.push_back(line.substr(start)); break; }
                parts.push_back(line.substr(start, p - start));
                start = p + 1;
            }
            if (parts.size() >= 6)
            {
                VmuAnimLayer l;
                l.name = parts[1];
                l.visible = (atoi(parts[2].c_str()) != 0);
                l.frameStart = atoi(parts[3].c_str());
                l.frameEnd = atoi(parts[4].c_str());
                l.linkedAsset = parts[5];
                loadedLayers.push_back(l);
            }
        }
    }

    if (!headerOk) return false;
    if (loadedFrames < 1) loadedFrames = 1;
    if (loadedLayers.empty()) loadedLayers.push_back({ "Layer 1", true, 0, 0, "" });

    gVmuAnimTotalFrames = loadedFrames;
    gVmuAnimPlayhead = std::max(0, std::min(loadedPlayhead, gVmuAnimTotalFrames - 1));
    gVmuAnimLoop = (loadedLoop != 0);
    gVmuAnimSpeedMode = std::max(0, std::min(loadedSpeedMode, 2));
    gVmuAnimLayers = loadedLayers;
    gVmuAnimLayerSel = 0;
    gVmuCurrentLoadedType = 2;
    return true;
}
