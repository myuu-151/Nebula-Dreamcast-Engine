#include "project.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <cctype>

#define NOMINMAX
#include <Windows.h>

std::string gProjectDir;
std::string gProjectFile;
std::vector<std::string> gRecentProjects;

std::string GetFolderName(const std::string& path)
{
    std::filesystem::path p(path);
    return p.filename().string();
}

void AddRecentProject(const std::string& projFile)
{
    if (projFile.empty()) return;
    gRecentProjects.erase(std::remove(gRecentProjects.begin(), gRecentProjects.end(), projFile), gRecentProjects.end());
    gRecentProjects.insert(gRecentProjects.begin(), projFile);
    if (gRecentProjects.size() > 10) gRecentProjects.resize(10);
}

std::string GetProjectDefaultScene(const std::filesystem::path& projectDir)
{
    if (projectDir.empty()) return "";
    std::ifstream in(projectDir / "Config.ini");
    if (!in.is_open()) return "";

    std::string line;
    while (std::getline(in, line))
    {
        if (line.rfind("defaultScene=", 0) == 0)
            return line.substr(13);
    }
    return "";
}

bool GetProjectVmuLoadOnBoot(const std::filesystem::path& projectDir)
{
    if (projectDir.empty()) return false;
    std::ifstream in(projectDir / "Config.ini");
    if (!in.is_open()) return false;
    std::string line;
    while (std::getline(in, line))
    {
        if (line.rfind("vmuLoadOnBoot=", 0) == 0)
            return line.substr(14) == "1";
    }
    return false;
}

bool SetProjectVmuLoadOnBoot(const std::filesystem::path& projectDir, bool enabled)
{
    if (projectDir.empty()) return false;
    std::filesystem::path cfgPath = projectDir / "Config.ini";
    std::vector<std::string> lines;
    {
        std::ifstream in(cfgPath);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
    }
    std::string entry = std::string("vmuLoadOnBoot=") + (enabled ? "1" : "0");
    bool replaced = false;
    for (auto& l : lines)
    {
        if (l.rfind("vmuLoadOnBoot=", 0) == 0) { l = entry; replaced = true; break; }
    }
    if (!replaced) lines.push_back(entry);
    std::ofstream out(cfgPath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        out << lines[i];
        if (i + 1 < lines.size()) out << "\n";
    }
    return true;
}

std::string GetProjectVmuAnim(const std::filesystem::path& projectDir)
{
    if (projectDir.empty()) return "";
    std::ifstream in(projectDir / "Config.ini");
    if (!in.is_open()) return "";
    std::string line;
    while (std::getline(in, line))
    {
        if (line.rfind("vmuLinkedAnim=", 0) == 0)
            return line.substr(14);
    }
    return "";
}

bool SetProjectVmuAnim(const std::filesystem::path& projectDir, const std::string& animPath)
{
    if (projectDir.empty()) return false;
    std::filesystem::path cfgPath = projectDir / "Config.ini";
    std::vector<std::string> lines;
    {
        std::ifstream in(cfgPath);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
    }
    std::string val = animPath;
    // Store as relative path if possible
    if (!animPath.empty())
    {
        std::error_code ec;
        std::filesystem::path rel = std::filesystem::relative(animPath, projectDir, ec);
        if (!ec && !rel.empty()) val = rel.generic_string();
    }
    std::string entry = "vmuLinkedAnim=" + val;
    bool replaced = false;
    for (auto& l : lines)
    {
        if (l.rfind("vmuLinkedAnim=", 0) == 0) { l = entry; replaced = true; break; }
    }
    if (!replaced) lines.push_back(entry);
    std::ofstream out(cfgPath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        out << lines[i];
        if (i + 1 < lines.size()) out << "\n";
    }
    return true;
}

bool SetProjectDefaultScene(const std::filesystem::path& projectDir, const std::filesystem::path& scenePath)
{
    if (projectDir.empty() || scenePath.empty()) return false;

    std::filesystem::path cfgPath = projectDir / "Config.ini";
    std::vector<std::string> lines;

    {
        std::ifstream in(cfgPath);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
    }

    std::filesystem::path outPath = scenePath;
    std::error_code ec;
    std::filesystem::path rel = std::filesystem::relative(scenePath, projectDir, ec);
    if (!ec && !rel.empty()) outPath = rel;

    std::string defaultLine = "defaultScene=" + outPath.generic_string();
    bool replaced = false;
    for (auto& l : lines)
    {
        if (l.rfind("defaultScene=", 0) == 0)
        {
            l = defaultLine;
            replaced = true;
            break;
        }
    }
    if (!replaced) lines.push_back(defaultLine);

    std::ofstream out(cfgPath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        out << lines[i];
        if (i + 1 < lines.size()) out << "\n";
    }
    return true;
}

void CreateNebulaProject(const std::string& folder)
{
    if (folder.empty()) return;

    std::filesystem::path projDir(folder);
    std::filesystem::create_directories(projDir);

    std::string projName = GetFolderName(folder);
    if (projName.empty()) projName = "NebulaProject";

    // Normalize accidental extension in folder name (e.g. "MyGame.neb")
    auto endsWithNoCase = [](const std::string& s, const std::string& suf)
    {
        if (s.size() < suf.size()) return false;
        for (size_t i = 0; i < suf.size(); ++i)
        {
            char a = (char)tolower((unsigned char)s[s.size() - suf.size() + i]);
            char b = (char)tolower((unsigned char)suf[i]);
            if (a != b) return false;
        }
        return true;
    };
    if (endsWithNoCase(projName, ".nebproj"))
    {
        projName.resize(projName.size() - 8);
    }
    else if (endsWithNoCase(projName, ".neb"))
    {
        projName.resize(projName.size() - 4);
    }
    if (projName.empty()) projName = "NebulaProject";

    std::filesystem::create_directories(projDir / "Assets");
    std::filesystem::create_directories(projDir / "Scripts");
    std::filesystem::create_directories(projDir / "Intermediate");

    // Config.ini (project name)
    {
        std::ofstream cfg(projDir / "Config.ini", std::ios::out | std::ios::trunc);
        if (cfg.is_open())
        {
            cfg << "project=" << projName;
        }
    }

    // Project file (.nebproj)
    {
        std::filesystem::path projPath = projDir / (projName + ".nebproj");
        std::ofstream proj(projPath, std::ios::out | std::ios::trunc);
        if (proj.is_open())
        {
            proj << "name=" << projName;
        }

        // Cleanup legacy project filename if present
        std::filesystem::path legacyProjPath = projDir / (projName + ".neb");
        if (std::filesystem::exists(legacyProjPath))
        {
            std::error_code ec;
            std::filesystem::remove(legacyProjPath, ec);
        }

        gProjectFile = projPath.string();
        AddRecentProject(gProjectFile);
    }

    gProjectDir = projDir.string();
}

std::filesystem::path GetExecutableDirectory()
{
    char pathBuf[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(NULL, pathBuf, (DWORD)MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return std::filesystem::current_path();
    std::filesystem::path p(pathBuf);
    return p.parent_path();
}

std::filesystem::path ResolveEditorAssetPath(const std::filesystem::path& relPath)
{
    std::vector<std::filesystem::path> roots;
    auto addRootChain = [&](std::filesystem::path base)
    {
        std::error_code ec;
        for (int i = 0; i < 7 && !base.empty(); ++i)
        {
            if (std::filesystem::exists(base, ec) && !ec)
                roots.push_back(base);
            std::filesystem::path parent = base.parent_path();
            if (parent == base) break;
            base = parent;
        }
    };

    addRootChain(std::filesystem::current_path());
    addRootChain(GetExecutableDirectory());

    std::vector<std::filesystem::path> relCandidates;
    relCandidates.push_back(relPath);

    auto relGeneric = relPath.generic_string();
    if (relGeneric.rfind("assets/", 0) == 0)
        relCandidates.push_back(std::filesystem::path("Assets") / relPath.lexically_relative("assets"));
    else if (relGeneric.rfind("Assets/", 0) == 0)
        relCandidates.push_back(std::filesystem::path("assets") / relPath.lexically_relative("Assets"));

    std::error_code ec;
    for (const auto& root : roots)
    {
        for (const auto& rel : relCandidates)
        {
            std::filesystem::path p = root / rel;
            if (std::filesystem::exists(p, ec) && !ec)
                return p;
        }
    }
    return {};
}

std::string ToProjectRelativePath(const std::filesystem::path& p)
{
    if (gProjectDir.empty())
        return p.filename().generic_string();

    std::error_code ec;
    std::filesystem::path rel = std::filesystem::relative(p, std::filesystem::path(gProjectDir), ec);
    if (ec) return p.filename().generic_string();
    return rel.generic_string();
}

std::filesystem::path GetNebMeshMetaPath(const std::filesystem::path& absMeshPath)
{
    return absMeshPath.parent_path() / "animmeta" / (absMeshPath.stem().string() + ".animmeta.animmeta");
}
