#pragma once
#include <string>
#include <vector>

std::string PickFolderDialog(const char* title);
std::string PickProjectFileDialog(const char* title);
std::string PickFbxFileDialog(const char* title);
std::string PickPngFileDialog(const char* title);
std::string PickVmuFrameDataDialog(const char* title);
std::vector<std::string> PickImportAssetDialog(const char* title);
