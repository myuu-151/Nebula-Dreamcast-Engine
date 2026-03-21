#pragma once
#include <string>
#include <vector>
#include <array>
#include <filesystem>
#include <cstdint>

struct VmuAnimLayer { std::string name; bool visible = true; int frameStart = 0; int frameEnd = 0; std::string linkedAsset; };

// --- VMU state ---
extern bool gShowVmuTool;
extern bool gVmuHasImage;
extern bool gVmuLoadOnBoot;
extern std::string gVmuAssetPath;
extern std::vector<VmuAnimLayer> gVmuAnimLayers;
extern int gVmuAnimLayerSel;
extern int gVmuAnimTotalFrames;
extern int gVmuAnimPlayhead;
extern bool gVmuAnimLoop;
extern int gVmuAnimSpeedMode;
extern int gVmuCurrentLoadedType;
extern std::string gVmuLinkedPngPath;
extern std::string gVmuLinkedAnimPath;
extern std::array<uint8_t, 48 * 32> gVmuMono;

bool LoadVmuPngToMono(const std::string& path, std::string& outErr);
bool SaveVmuFrameData(const std::filesystem::path& outPath);
bool LoadVmuFrameData(const std::filesystem::path& inPath);
