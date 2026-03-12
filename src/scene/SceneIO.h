#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "NodeTypes.h"

namespace NebulaScene
{
    std::string NormalizePathRef(std::string s);
    bool RewritePathRefForRename(std::string& ref, const std::string& oldRel, const std::string& newRel, bool isDir);

    void UpdateAssetReferencesForRename(
        const std::filesystem::path& oldPath,
        const std::filesystem::path& newPath,
        const std::string& projectDir,
        std::vector<Audio3DNode>& audioNodes,
        std::vector<StaticMesh3DNode>& staticMeshNodes,
        std::vector<Node3DNode>& node3DNodes,
        std::vector<SceneData>& openScenes,
        std::filesystem::path& selectedAssetPath);

    std::string EncodeSceneToken(const std::string& s);
    void DecodeSceneToken(std::string& s);
    std::string BuildSceneText(const std::filesystem::path& path, const std::vector<Audio3DNode>& nodes, const std::vector<StaticMesh3DNode>& statics, const std::vector<Camera3DNode>& cameras, const std::vector<Node3DNode>& node3d, const std::vector<NavMesh3DNode>& navMeshes = {});
    void SaveSceneToPath(const std::filesystem::path& path, const std::vector<Audio3DNode>& nodes);
    void SaveSceneToPath(const std::filesystem::path& path, const std::vector<Audio3DNode>& nodes, const std::vector<StaticMesh3DNode>& statics, const std::vector<Camera3DNode>& cameras, const std::vector<Node3DNode>& node3d, const std::vector<NavMesh3DNode>& navMeshes = {});
    bool LoadSceneFromPath(const std::filesystem::path& path, SceneData& outScene);
}
