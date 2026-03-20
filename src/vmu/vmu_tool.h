#pragma once
#include <string>
#include <filesystem>

bool LoadVmuPngToMono(const std::string& path, std::string& outErr);
bool SaveVmuFrameData(const std::filesystem::path& outPath);
bool LoadVmuFrameData(const std::filesystem::path& inPath);
