#pragma once
#include <string>
#include <vector>
#include <filesystem>

extern std::string gProjectDir;
extern std::string gProjectFile;
extern std::vector<std::string> gRecentProjects;

std::string GetFolderName(const std::string& path);
void AddRecentProject(const std::string& projFile);
std::string GetProjectDefaultScene(const std::filesystem::path& projectDir);
bool GetProjectVmuLoadOnBoot(const std::filesystem::path& projectDir);
bool SetProjectVmuLoadOnBoot(const std::filesystem::path& projectDir, bool enabled);
std::string GetProjectVmuAnim(const std::filesystem::path& projectDir);
bool SetProjectVmuAnim(const std::filesystem::path& projectDir, const std::string& animPath);
bool SetProjectDefaultScene(const std::filesystem::path& projectDir, const std::filesystem::path& scenePath);
void CreateNebulaProject(const std::string& folder);

std::filesystem::path GetExecutableDirectory();
std::filesystem::path ResolveEditorAssetPath(const std::filesystem::path& relPath);
std::string ToProjectRelativePath(const std::filesystem::path& p);
std::filesystem::path GetNebMeshMetaPath(const std::filesystem::path& absMeshPath);
