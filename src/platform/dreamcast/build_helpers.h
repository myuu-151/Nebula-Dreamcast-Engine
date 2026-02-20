#pragma once

#include <filesystem>

namespace NebulaDreamcastBuild
{
    int RunCommand(const char* cmd);
    bool IsDiscImageFilePath(const std::filesystem::path& p);
    bool IsLikelySaturnImageBin(const std::filesystem::path& p);
    bool GenerateCueForBuild(const std::filesystem::path& buildDir, const std::filesystem::path& projectDir, std::filesystem::path& outCue);
}
