#pragma once
#include <string>
#include <filesystem>

// User-overridable tool paths (Preferences)
extern std::string gPrefDreamSdkHome;
extern std::string gPrefVcvarsPath;

std::string GetPrefsPath();
std::filesystem::path ResolveVcvarsPathFromPreference(const std::string& pref);
void LoadPreferences(float& uiScale, int& themeMode);
void SavePreferences(float uiScale, int themeMode);
