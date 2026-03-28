#include "hotkeys.h"
#include "editor_state.h"
#include "viewport_nav.h"
#include "undo.h"
#include "../viewport/viewport_transform.h"
#include "../nodes/NodeTypes.h"
#include "../runtime/script_compile.h"
#include "../scene/scene_manager.h"
#include "../ui/asset_browser.h"

#include "imgui.h"
#include <GLFW/glfw3.h>
#include <string>
#include <filesystem>

// Edge-detect statics for transform keys
static bool gKeyG = false;
static bool gKeyR = false;
static bool gKeyS = false;
static bool gKeyX = false;
static bool gKeyY = false;
static bool gKeyZ = false;
static bool gKeyEsc = false;
static bool gKeyDel = false;

// Copy/paste statics (multi-select aware)
static std::vector<Audio3DNode> gCopiedAudio3D;
static std::vector<StaticMesh3DNode> gCopiedStaticMesh;
static std::vector<Camera3DNode> gCopiedCamera3D;
static std::vector<Node3DNode> gCopiedNode3D;
static std::vector<NavMesh3DNode> gCopiedNavMesh3D;

static std::string IncrementName(const std::string& name)
{
    std::string base = name;
    int num = 1;
    size_t pos = base.find_last_not_of("0123456789");
    if (pos != std::string::npos && pos + 1 < base.size())
    {
        num = std::stoi(base.substr(pos + 1)) + 1;
        base = base.substr(0, pos + 1);
    }
    return base + std::to_string(num);
}

void TickTransformHotkeys(GLFWwindow* window, EditorViewportNav& nav)
{
    auto edge = [](bool now, bool& prev) { bool pressed = (now && !prev); prev = now; return pressed; };

    bool kG = (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS);
    bool kR = (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS);
    bool kS = (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS);
    bool kX = (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS);
    bool kY = (glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS);
    bool kZ = (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS);
    bool kEsc = (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS);
    bool kDel = (glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS);

    bool blockTransformKeys = ImGui::GetIO().WantTextInput || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);

    if (edge(kG, gKeyG) && !blockTransformKeys)
    {
        EndTransformSnapshot();
        if (gTransformMode == Transform_Grab) gTransformMode = Transform_None;
        else { gTransformMode = Transform_Grab; BeginTransformSnapshot(); }
        gAxisLock = 0;
    }
    if (edge(kR, gKeyR) && !blockTransformKeys)
    {
        EndTransformSnapshot();
        if (gTransformMode == Transform_Rotate) gTransformMode = Transform_None;
        else { gTransformMode = Transform_Rotate; BeginTransformSnapshot(); }
        gAxisLock = 0;
    }
    if (edge(kS, gKeyS) && !blockTransformKeys)
    {
        EndTransformSnapshot();
        if (gTransformMode == Transform_Scale) gTransformMode = Transform_None;
        else { gTransformMode = Transform_Scale; BeginTransformSnapshot(); }
        gAxisLock = 0;
    }
    if (!blockTransformKeys)
    {
        if (edge(kX, gKeyX)) gAxisLock = (gAxisLock == 'X') ? 0 : 'X';
        if (edge(kY, gKeyY)) gAxisLock = (gAxisLock == 'Y') ? 0 : 'Y';
        if (edge(kZ, gKeyZ)) gAxisLock = (gAxisLock == 'Z') ? 0 : 'Z';
    }
    else
    {
        edge(kX, gKeyX); edge(kY, gKeyY); edge(kZ, gKeyZ);
    }
    if (edge(kEsc, gKeyEsc))
    {
        if (gPlayMode)
        {
            gPlayMode = false;
            gPlayOriginalScenes.clear();
            EndPlayScriptRuntime();
            nav.Restore();
            RestorePlaySceneState();
        }
        else
        {
            CancelTransformSnapshot();
            gTransformMode = Transform_None;
            gAxisLock = 0;
            gSelectedAudio3D = -1;
            ClearMultiSelection();
            gHasRotatePreview = false;
        }
    }
    if (edge(kDel, gKeyDel))
    {
        if (gSelectedAudio3D >= 0 && gSelectedAudio3D < (int)gAudio3DNodes.size())
        {
            int idx = gSelectedAudio3D;
            Audio3DNode node = gAudio3DNodes[idx];
            gAudio3DNodes.erase(gAudio3DNodes.begin() + idx);
            gSelectedAudio3D = -1;
            gTransformMode = Transform_None;
            gAxisLock = 0;
            gHasTransformSnapshot = false;

            PushUndo({"Delete Audio3D",
                [idx, node]() {
                    int i = idx;
                    if (i < 0) return;
                    if (i > (int)gAudio3DNodes.size()) i = (int)gAudio3DNodes.size();
                    gAudio3DNodes.insert(gAudio3DNodes.begin() + i, node);
                },
                [idx]() {
                    if (idx >= 0 && idx < (int)gAudio3DNodes.size())
                        gAudio3DNodes.erase(gAudio3DNodes.begin() + idx);
                }
            });
        }
        else if (gSelectedStaticMesh >= 0 && gSelectedStaticMesh < (int)gStaticMeshNodes.size())
        {
            int idx = gSelectedStaticMesh;
            StaticMesh3DNode node = gStaticMeshNodes[idx];
            gStaticMeshNodes.erase(gStaticMeshNodes.begin() + idx);
            gSelectedStaticMesh = -1;
            gTransformMode = Transform_None;
            gAxisLock = 0;
            gHasTransformSnapshot = false;

            PushUndo({"Delete StaticMesh3D",
                [idx, node]() {
                    int i = idx;
                    if (i < 0) return;
                    if (i > (int)gStaticMeshNodes.size()) i = (int)gStaticMeshNodes.size();
                    gStaticMeshNodes.insert(gStaticMeshNodes.begin() + i, node);
                },
                [idx]() {
                    if (idx >= 0 && idx < (int)gStaticMeshNodes.size())
                        gStaticMeshNodes.erase(gStaticMeshNodes.begin() + idx);
                }
            });
        }
        else if (gSelectedNavMesh3D >= 0 && gSelectedNavMesh3D < (int)gNavMesh3DNodes.size())
        {
            int idx = gSelectedNavMesh3D;
            NavMesh3DNode node = gNavMesh3DNodes[idx];
            gNavMesh3DNodes.erase(gNavMesh3DNodes.begin() + idx);
            gSelectedNavMesh3D = -1;
            gTransformMode = Transform_None;
            gAxisLock = 0;
            gHasTransformSnapshot = false;

            PushUndo({"Delete NavMesh3D",
                [idx, node]() {
                    int i = idx;
                    if (i < 0) return;
                    if (i > (int)gNavMesh3DNodes.size()) i = (int)gNavMesh3DNodes.size();
                    gNavMesh3DNodes.insert(gNavMesh3DNodes.begin() + i, node);
                },
                [idx]() {
                    if (idx >= 0 && idx < (int)gNavMesh3DNodes.size())
                        gNavMesh3DNodes.erase(gNavMesh3DNodes.begin() + idx);
                }
            });
        }
        else if (!gSelectedAssetPath.empty() && std::filesystem::exists(gSelectedAssetPath))
        {
            gPendingDelete = gSelectedAssetPath;
            gDoDelete = true;
            gSelectedAssetPath.clear();
        }
    }
}

void TickCtrlShortcuts()
{
    ImGuiIO& io = ImGui::GetIO();

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))
    {
        if (io.KeyShift) DoRedo();
        else DoUndo();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
    {
        if (io.KeyShift) SaveAllProjectChanges();
        else SaveActiveScene();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C) && !io.WantTextInput)
    {
        gCopiedAudio3D.clear();
        gCopiedStaticMesh.clear();
        gCopiedCamera3D.clear();
        gCopiedNode3D.clear();
        gCopiedNavMesh3D.clear();

        // Copy from multi-selection
        for (int idx : gMultiSelectedAudio3D)
            if (idx >= 0 && idx < (int)gAudio3DNodes.size()) gCopiedAudio3D.push_back(gAudio3DNodes[idx]);
        for (int idx : gMultiSelectedStaticMesh)
            if (idx >= 0 && idx < (int)gStaticMeshNodes.size()) gCopiedStaticMesh.push_back(gStaticMeshNodes[idx]);
        for (int idx : gMultiSelectedCamera3D)
            if (idx >= 0 && idx < (int)gCamera3DNodes.size()) gCopiedCamera3D.push_back(gCamera3DNodes[idx]);
        for (int idx : gMultiSelectedNode3D)
            if (idx >= 0 && idx < (int)gNode3DNodes.size()) gCopiedNode3D.push_back(gNode3DNodes[idx]);
        for (int idx : gMultiSelectedNavMesh3D)
            if (idx >= 0 && idx < (int)gNavMesh3DNodes.size()) gCopiedNavMesh3D.push_back(gNavMesh3DNodes[idx]);

        // Also copy from single-selection if no multi-select
        if (gCopiedAudio3D.empty() && gCopiedStaticMesh.empty() && gCopiedCamera3D.empty() && gCopiedNode3D.empty() && gCopiedNavMesh3D.empty())
        {
            if (gSelectedAudio3D >= 0 && gSelectedAudio3D < (int)gAudio3DNodes.size())
                gCopiedAudio3D.push_back(gAudio3DNodes[gSelectedAudio3D]);
            else if (gSelectedStaticMesh >= 0 && gSelectedStaticMesh < (int)gStaticMeshNodes.size())
                gCopiedStaticMesh.push_back(gStaticMeshNodes[gSelectedStaticMesh]);
            else if (gSelectedCamera3D >= 0 && gSelectedCamera3D < (int)gCamera3DNodes.size())
                gCopiedCamera3D.push_back(gCamera3DNodes[gSelectedCamera3D]);
            else if (gSelectedNode3D >= 0 && gSelectedNode3D < (int)gNode3DNodes.size())
                gCopiedNode3D.push_back(gNode3DNodes[gSelectedNode3D]);
            else if (gSelectedNavMesh3D >= 0 && gSelectedNavMesh3D < (int)gNavMesh3DNodes.size())
                gCopiedNavMesh3D.push_back(gNavMesh3DNodes[gSelectedNavMesh3D]);
        }
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) && !io.WantTextInput)
    {
        bool hasCopied = !gCopiedAudio3D.empty() || !gCopiedStaticMesh.empty() ||
                         !gCopiedCamera3D.empty() || !gCopiedNode3D.empty() || !gCopiedNavMesh3D.empty();
        if (hasCopied)
        {
            ClearMultiSelection();
            gSelectedAudio3D = -1;
            gSelectedStaticMesh = -1;
            gSelectedCamera3D = -1;
            gSelectedNode3D = -1;
            gSelectedNavMesh3D = -1;

            for (const auto& src : gCopiedAudio3D)
            {
                Audio3DNode node = src;
                node.name = IncrementName(node.name);
                node.x += 1.0f;
                int idx = (int)gAudio3DNodes.size();
                gAudio3DNodes.push_back(node);
                gMultiSelectedAudio3D.insert(idx);
            }
            for (const auto& src : gCopiedStaticMesh)
            {
                StaticMesh3DNode node = src;
                node.name = IncrementName(node.name);
                node.x += 1.0f;
                int idx = (int)gStaticMeshNodes.size();
                gStaticMeshNodes.push_back(node);
                gMultiSelectedStaticMesh.insert(idx);
            }
            for (const auto& src : gCopiedCamera3D)
            {
                Camera3DNode node = src;
                node.name = IncrementName(node.name);
                node.x += 1.0f;
                int idx = (int)gCamera3DNodes.size();
                gCamera3DNodes.push_back(node);
                gMultiSelectedCamera3D.insert(idx);
            }
            for (const auto& src : gCopiedNode3D)
            {
                Node3DNode node = src;
                node.name = IncrementName(node.name);
                node.x += 1.0f;
                int idx = (int)gNode3DNodes.size();
                gNode3DNodes.push_back(node);
                gMultiSelectedNode3D.insert(idx);
            }
            for (const auto& src : gCopiedNavMesh3D)
            {
                NavMesh3DNode node = src;
                node.name = IncrementName(node.name);
                node.x += 1.0f;
                int idx = (int)gNavMesh3DNodes.size();
                gNavMesh3DNodes.push_back(node);
                gMultiSelectedNavMesh3D.insert(idx);
            }
            PushUndo({"Paste nodes", nullptr, nullptr});
        }
    }
}
