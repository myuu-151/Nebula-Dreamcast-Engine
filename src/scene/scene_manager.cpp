#include "scene_manager.h"
#include "scene_io.h"
#include "nodes/NodeTypes.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>

#include "../editor/editor_state.h"
#include "../editor/project.h"

// Declared in runtime/script_compile.h
extern void NotifyScriptSceneSwitch();

bool HasUnsavedProjectChanges()
{
    if (gProjectDir.empty()) return false;
    for (int i = 0; i < (int)gOpenScenes.size(); ++i)
    {
        const auto& s = gOpenScenes[i];
        const std::vector<Audio3DNode>& nodes = (i == gActiveScene) ? gAudio3DNodes : s.nodes;
        const std::vector<StaticMesh3DNode>& statics = (i == gActiveScene) ? gStaticMeshNodes : s.staticMeshes;
        const std::vector<Camera3DNode>& cameras = (i == gActiveScene) ? gCamera3DNodes : s.cameras;
        const std::vector<Node3DNode>& node3d = (i == gActiveScene) ? gNode3DNodes : s.node3d;

        std::string expected = NebulaScene::BuildSceneText(s.path, nodes, statics, cameras, node3d);
        std::ifstream in(s.path, std::ios::in | std::ios::binary);
        if (!in.is_open()) return true;
        std::string current((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (current != expected) return true;
    }
    return false;
}

void RefreshOpenSceneTabMetadataForPath(const std::filesystem::path& path)
{
    for (auto& s : gOpenScenes)
    {
        if (s.path == path)
            s.name = path.stem().string();
    }
}

bool LoadSceneFromPath(const std::filesystem::path& path, SceneData& outScene)
{
    return NebulaScene::LoadSceneFromPath(path, outScene);
}

void SetActiveScene(int index)
{
    if (index < 0 || index >= (int)gOpenScenes.size()) return;
    // save current nodes to current scene
    if (gActiveScene >= 0 && gActiveScene < (int)gOpenScenes.size())
    {
        gOpenScenes[gActiveScene].nodes = gAudio3DNodes;
        gOpenScenes[gActiveScene].staticMeshes = gStaticMeshNodes;
        gOpenScenes[gActiveScene].cameras = gCamera3DNodes;
        gOpenScenes[gActiveScene].node3d = gNode3DNodes;
        gOpenScenes[gActiveScene].navMeshes = gNavMesh3DNodes;
    }
    gActiveScene = index;
    // During play mode, reload from original scene data (matches DC behavior
    // where scenes are reloaded fresh from disc on every switch).  This
    // prevents stale runtime positions (e.g. player left at a trigger zone)
    // from causing immediate re-triggers when returning to a scene.
    if (gPlayMode && index < (int)gPlayOriginalScenes.size())
    {
        gAudio3DNodes = gPlayOriginalScenes[index].nodes;
        gStaticMeshNodes = gPlayOriginalScenes[index].staticMeshes;
        gCamera3DNodes = gPlayOriginalScenes[index].cameras;
        gNode3DNodes = gPlayOriginalScenes[index].node3d;
        gNavMesh3DNodes = gPlayOriginalScenes[index].navMeshes;
    }
    else
    {
        gAudio3DNodes = gOpenScenes[gActiveScene].nodes;
        gStaticMeshNodes = gOpenScenes[gActiveScene].staticMeshes;
        gCamera3DNodes = gOpenScenes[gActiveScene].cameras;
        gNode3DNodes = gOpenScenes[gActiveScene].node3d;
        gNavMesh3DNodes = gOpenScenes[gActiveScene].navMeshes;
    }
    gForceSelectSceneTab = index;
    NotifyScriptSceneSwitch();
}

void OpenSceneFile(const std::filesystem::path& path)
{
    SceneData scene;
    if (!LoadSceneFromPath(path, scene)) return;

    // If already open, just activate
    for (int i = 0; i < (int)gOpenScenes.size(); ++i)
    {
        if (gOpenScenes[i].path == path)
        {
            SetActiveScene(i);
            return;
        }
    }

    gOpenScenes.push_back(scene);
    SetActiveScene((int)gOpenScenes.size() - 1);
}

void SaveActiveScene()
{
    if (gActiveScene < 0 || gActiveScene >= (int)gOpenScenes.size())
    {
        gViewportToast = "No active scene to save";
        gViewportToastUntil = glfwGetTime() + 2.0;
        return;
    }
    gOpenScenes[gActiveScene].nodes = gAudio3DNodes;
    gOpenScenes[gActiveScene].staticMeshes = gStaticMeshNodes;
    gOpenScenes[gActiveScene].cameras = gCamera3DNodes;
    gOpenScenes[gActiveScene].node3d = gNode3DNodes;
    gOpenScenes[gActiveScene].navMeshes = gNavMesh3DNodes;
    NebulaScene::SaveSceneToPath(gOpenScenes[gActiveScene].path, gOpenScenes[gActiveScene].nodes, gOpenScenes[gActiveScene].staticMeshes, gOpenScenes[gActiveScene].cameras, gOpenScenes[gActiveScene].node3d, gOpenScenes[gActiveScene].navMeshes);
    RefreshOpenSceneTabMetadataForPath(gOpenScenes[gActiveScene].path);
    gViewportToast = "Saved " + gOpenScenes[gActiveScene].name;
    gViewportToastUntil = glfwGetTime() + 2.0;
}

void SaveAllProjectChanges()
{
    if (gOpenScenes.empty())
    {
        gViewportToast = "No open scenes to save";
        gViewportToastUntil = glfwGetTime() + 2.0;
        return;
    }

    // Sync active scene nodes first.
    if (gActiveScene >= 0 && gActiveScene < (int)gOpenScenes.size())
    {
        gOpenScenes[gActiveScene].nodes = gAudio3DNodes;
        gOpenScenes[gActiveScene].staticMeshes = gStaticMeshNodes;
        gOpenScenes[gActiveScene].cameras = gCamera3DNodes;
        gOpenScenes[gActiveScene].node3d = gNode3DNodes;
        gOpenScenes[gActiveScene].navMeshes = gNavMesh3DNodes;
    }

    // Save every open scene in the current project.
    gSaveAllInProgress = true;
    for (auto& s : gOpenScenes)
    {
        NebulaScene::SaveSceneToPath(s.path, s.nodes, s.staticMeshes, s.cameras, s.node3d, s.navMeshes);
        RefreshOpenSceneTabMetadataForPath(s.path);
    }
    gSaveAllInProgress = false;

    gViewportToast = "Saved all changes in current project";
    gViewportToastUntil = glfwGetTime() + 2.0;
}
