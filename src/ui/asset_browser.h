#pragma once
#include <string>
#include <filesystem>

// --- Globals owned by asset_browser ---
extern std::filesystem::path gRenamePath;
extern char gRenameBuffer[256];
extern std::filesystem::path gInlineRenamePath;
extern char gInlineRenameBuffer[256];
extern bool gInlineRenameFocus;
extern std::filesystem::path gPendingDelete;
extern bool gDoDelete;
extern bool gRenameModalOpen;
extern std::filesystem::path gAssetsCurrentDir;
extern std::filesystem::path gSelectedAssetPath;
extern double gSelectedAssetPathSetTime;

// --- Asset management functions ---
void DeleteAssetPath(const std::filesystem::path& p);
std::filesystem::path MoveAssetToTrash(const std::filesystem::path& p);
std::filesystem::path RenameAssetPath(const std::filesystem::path& p, const std::string& newName);
std::filesystem::path DuplicateAssetPath(const std::filesystem::path& p);
bool RewritePathRefForRename(std::string& ref, const std::string& oldRel, const std::string& newRel, bool isDir);
void UpdateAssetReferencesForRename(const std::filesystem::path& oldPath, const std::filesystem::path& newPath);
void BeginInlineAssetRename(const std::filesystem::path& p, const std::string& displayName);
void CommitInlineAssetRename();

// --- Asset creation ---
void CreateSceneAssetAt(const std::filesystem::path& scenePath);
std::filesystem::path CreateSceneAsset(const std::filesystem::path& assetsRoot);
void CreateAssetFolderAt(const std::filesystem::path& folder);
std::filesystem::path CreateAssetFolder(const std::filesystem::path& assetsRoot);
std::filesystem::path MakeUniqueAssetPath(const std::filesystem::path& root, const std::string& baseName, const std::string& ext);

// --- Main asset browser UI ---
void DrawAssetsBrowser(const std::filesystem::path& root);
