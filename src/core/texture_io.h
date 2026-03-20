#pragma once

#include <string>
#include <vector>
#include <array>
#include <filesystem>
#include <cstdint>

// GLuint forward
using GLuint = unsigned int;

bool LoadImageWIC(const wchar_t* path, unsigned int& outW, unsigned int& outH, std::vector<unsigned char>& outPixelsBGRA);
GLuint LoadTextureWIC(const wchar_t* path);
bool ExportNebTexturePNG(const std::filesystem::path& pngPath, const std::filesystem::path& outPath, std::string& warning);
bool SaveVmuMonoPng(const std::filesystem::path& outPath, const std::array<uint8_t, 48 * 32>& mono);

// --- TGA conversion (Dreamcast packaging) ---
bool ConvertNebTexToTga24(const std::filesystem::path& nebtexPath, const std::filesystem::path& tgaOutPath, std::string& warn);
bool ConvertTgaToJoSafeTga24(const std::filesystem::path& tgaInPath, const std::filesystem::path& tgaOutPath, std::string& warn);
bool GetNebTexSaturnPadUvScale(const std::filesystem::path& nebtexPath, float& outU, float& outV);
