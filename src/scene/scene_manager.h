#pragma once

#include <filesystem>

struct SceneData;

bool HasUnsavedProjectChanges();
void RefreshOpenSceneTabMetadataForPath(const std::filesystem::path& path);
bool LoadSceneFromPath(const std::filesystem::path& path, SceneData& outScene);
void SetActiveScene(int index);
void OpenSceneFile(const std::filesystem::path& path);
void SaveActiveScene();
void SaveAllProjectChanges();
