#include "build_helpers.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace NebulaDreamcastBuild
{
    int RunCommand(const char* cmd)
    {
        if (!cmd) return -1;
        printf("[Package] %s\n", cmd);
        return system(cmd);
    }

    bool IsDiscImageFilePath(const std::filesystem::path& p)
    {
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext == ".bin" || ext == ".iso";
    }

    bool IsLikelySaturnImageBin(const std::filesystem::path& p)
    {
        if (!IsDiscImageFilePath(p)) return false;

        std::string name = p.filename().string();
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);

        if (name.find("cmakedeterminecompilerabi") != std::string::npos) return false;
        if (name.find("backup") != std::string::npos) return false;
        if (name.find("memcard") != std::string::npos) return false;
        if (name.find("smpc") != std::string::npos) return false;

        return true;
    }

    bool GenerateCueForBuild(const std::filesystem::path& buildDir, const std::filesystem::path& projectDir, std::filesystem::path& outCue)
    {
        outCue.clear();
        if (buildDir.empty()) return false;

        std::vector<std::filesystem::path> roots;
        if (std::filesystem::exists(buildDir)) roots.push_back(buildDir);
        if (!projectDir.empty() && std::filesystem::exists(projectDir)) roots.push_back(projectDir);

        for (const auto& root : roots)
        {
            for (auto& e : std::filesystem::recursive_directory_iterator(root))
            {
                if (!e.is_regular_file()) continue;
                std::string ext = e.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".cue")
                {
                    outCue = e.path();
                    return true;
                }
            }
        }

        std::filesystem::path bestBin;
        uintmax_t bestSize = 0;
        for (const auto& root : roots)
        {
            for (auto& e : std::filesystem::recursive_directory_iterator(root))
            {
                if (!e.is_regular_file()) continue;
                const auto& p = e.path();
                if (!IsLikelySaturnImageBin(p)) continue;

                std::error_code ec;
                uintmax_t sz = std::filesystem::file_size(p, ec);
                if (ec) continue;

                if (sz > bestSize)
                {
                    bestSize = sz;
                    bestBin = p;
                }
            }
        }

        if (bestBin.empty()) return false;

        outCue = buildDir / "game.cue";
        std::ofstream cue(outCue, std::ios::out | std::ios::trunc);
        if (!cue.is_open()) return false;

        std::filesystem::path relBin = std::filesystem::relative(bestBin, outCue.parent_path());
        std::string relBinStr = relBin.generic_string();

        cue << "FILE \"" << relBinStr << "\" BINARY\n";
        cue << "  TRACK 01 MODE1/2048\n";
        cue << "    INDEX 01 00:00:00\n";
        cue.close();

        return true;
    }
}
