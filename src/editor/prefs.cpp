#include "prefs.h"

#include <fstream>
#include <sstream>
#include <algorithm>

#define NOMINMAX
#include <Windows.h>

std::string gPrefDreamSdkHome = "C:\\DreamSDK";
std::string gPrefVcvarsPath;

std::string GetPrefsPath()
{
    // Store preferences next to the editor executable so the path is
    // stable regardless of the working directory at launch time.
    static std::string cached;
    if (cached.empty())
    {
        char buf[MAX_PATH] = {0};
        GetModuleFileNameA(nullptr, buf, MAX_PATH);
        std::filesystem::path p(buf);
        cached = (p.parent_path() / "editor_prefs.ini").string();
    }
    return cached;
}

std::filesystem::path ResolveVcvarsPathFromPreference(const std::string& pref)
{
    if (pref.empty()) return {};
    std::string s = pref;
    while (!s.empty() && (s[0] == '=' || s[0] == ' ' || s[0] == '\t' || s[0] == '"')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '"')) s.pop_back();
    if (s.empty()) return {};
    std::filesystem::path p = s;
    std::error_code ec;
    if (std::filesystem::is_regular_file(p, ec) && !ec) return p;
    ec.clear();
    if (std::filesystem::is_directory(p, ec) && !ec)
    {
        auto tryCandidate = [](const std::filesystem::path& base) -> std::filesystem::path
        {
            const char* rels[] = {
                "VC/Auxiliary/Build/vcvarsall.bat",
                "VC/Auxiliary/Build/vcvars64.bat"
            };
            for (const char* rel : rels)
            {
                std::filesystem::path cand = base / rel;
                if (std::filesystem::exists(cand)) return cand;
            }
            return {};
        };

        // If user already points at a VS instance root (e.g. .../2022/Community)
        if (auto direct = tryCandidate(p); !direct.empty()) return direct;

        // If user points at top-level Visual Studio folder (e.g. C:/Program Files/Microsoft Visual Studio)
        const char* years[] = { "2022", "2019", "18", "17" };
        const char* editions[] = { "Community", "Professional", "Enterprise", "BuildTools" };
        for (const char* y : years)
        {
            for (const char* e : editions)
            {
                std::filesystem::path inst = p / y / e;
                if (auto hit = tryCandidate(inst); !hit.empty()) return hit;
            }
        }
    }
    return {};
}

void LoadPreferences(float& uiScale, int& themeMode)
{
    std::ifstream in(GetPrefsPath());
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line))
    {
        if (line.rfind("uiScale=", 0) == 0)
        {
            uiScale = std::stof(line.substr(8));
        }
        else if (line.rfind("themeMode=", 0) == 0)
        {
            themeMode = std::stoi(line.substr(10));
        }
        else if (line.rfind("spaceTheme=", 0) == 0)
        {
            // legacy support
            themeMode = (line.substr(11) == "1") ? 0 : 1;
        }
        else if (line.rfind("dreamSdkHome=", 0) == 0)
        {
            gPrefDreamSdkHome = line.substr(13);
        }
        else if (line.rfind("vcvarsPath=", 0) == 0)
        {
            gPrefVcvarsPath = line.substr(10);
            while (!gPrefVcvarsPath.empty() && (gPrefVcvarsPath[0] == '=' || gPrefVcvarsPath[0] == ' ' || gPrefVcvarsPath[0] == '\t' || gPrefVcvarsPath[0] == '"'))
                gPrefVcvarsPath.erase(gPrefVcvarsPath.begin());
            while (!gPrefVcvarsPath.empty() && (gPrefVcvarsPath.back() == ' ' || gPrefVcvarsPath.back() == '\t' || gPrefVcvarsPath.back() == '"'))
                gPrefVcvarsPath.pop_back();
        }
    }
}

void SavePreferences(float uiScale, int themeMode)
{
    std::ofstream out(GetPrefsPath(), std::ios::out | std::ios::trunc);
    if (!out.is_open()) return;
    out << "uiScale=" << uiScale << "\n";
    out << "themeMode=" << themeMode << "\n";
    out << "dreamSdkHome=" << gPrefDreamSdkHome << "\n";
    out << "vcvarsPath=" << gPrefVcvarsPath << "\n";
}
