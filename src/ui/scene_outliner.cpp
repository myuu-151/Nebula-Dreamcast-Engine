#include "scene_outliner.h"

#include "imgui.h"

#include <string>
#include <cstring>
#include <vector>
#include <functional>
#include <unordered_set>
#include <filesystem>

#include "../nodes/NodeTypes.h"
#include "../io/mesh_io.h"
#include "../editor/project.h"
#include "../editor/editor_state.h"
#include "../editor/undo.h"
#include "../viewport/viewport_transform.h"

// Helper functions defined in main.cpp
extern bool WouldCreateHierarchyCycle(const std::string& childName, const std::string& candidateParentName);
extern void ReparentStaticMeshKeepWorldPos(int childIdx, const std::string& newParent);
extern int  FindStaticMeshByName(const std::string& name);
extern int  FindNode3DByName(const std::string& name);

// ---------------------------------------------------------------------------
void DrawSceneOutliner(const ImGuiViewport* vp, float topBarH, float leftPanelWidth, float leftPanelHeight, float assetsHeight)
{
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + topBarH));
    ImGui::SetNextWindowSize(ImVec2(leftPanelWidth, leftPanelHeight - assetsHeight));
    ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);
    ImGui::Text("Scene");
    ImGui::Separator();

    if (ImGui::BeginPopupContextWindow("SceneContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
        if (ImGui::BeginMenu("Create Node"))
        {
            if (ImGui::MenuItem("Audio3D"))
            {
                Audio3DNode node;
                node.name = "Audio3D" + std::to_string((int)gAudio3DNodes.size() + 1);
                int idx = (int)gAudio3DNodes.size();
                gAudio3DNodes.push_back(node);

                PushUndo({"Create Audio3D",
                    [idx]() { if (idx >= 0 && idx < (int)gAudio3DNodes.size()) gAudio3DNodes.erase(gAudio3DNodes.begin() + idx); },
                    [idx, node]() {
                        if (idx < 0) return;
                        int i = idx;
                        if (i > (int)gAudio3DNodes.size()) i = (int)gAudio3DNodes.size();
                        gAudio3DNodes.insert(gAudio3DNodes.begin() + i, node);
                    }
                });
            }
            if (ImGui::MenuItem("StaticMesh3D"))
            {
                StaticMesh3DNode node;
                node.name = "StaticMesh3D" + std::to_string((int)gStaticMeshNodes.size() + 1);
                if (!gProjectDir.empty())
                {
                    EnsureDefaultCubeNebmesh(std::filesystem::path(gProjectDir));
                    node.mesh = "Assets/cube_primitive.nebmesh";
                    NebulaNodes::AutoAssignMaterialSlotsFromMesh(node);
                }
                int idx = (int)gStaticMeshNodes.size();
                gStaticMeshNodes.push_back(node);

                PushUndo({"Create StaticMesh3D",
                    [idx]() { if (idx >= 0 && idx < (int)gStaticMeshNodes.size()) gStaticMeshNodes.erase(gStaticMeshNodes.begin() + idx); },
                    [idx, node]() {
                        if (idx < 0) return;
                        int i = idx;
                        if (i > (int)gStaticMeshNodes.size()) i = (int)gStaticMeshNodes.size();
                        gStaticMeshNodes.insert(gStaticMeshNodes.begin() + i, node);
                    }
                });
            }
            if (ImGui::MenuItem("Camera3D"))
            {
                Camera3DNode node;
                node.name = "Camera3D" + std::to_string((int)gCamera3DNodes.size() + 1);
                bool hasMain = false;
                for (const auto& c : gCamera3DNodes) { if (c.main) { hasMain = true; break; } }
                if (!hasMain) node.main = true;
                int idx = (int)gCamera3DNodes.size();
                gCamera3DNodes.push_back(node);

                PushUndo({"Create Camera3D",
                    [idx]() { if (idx >= 0 && idx < (int)gCamera3DNodes.size()) gCamera3DNodes.erase(gCamera3DNodes.begin() + idx); },
                    [idx, node]() {
                        if (idx < 0) return;
                        int i = idx;
                        if (i > (int)gCamera3DNodes.size()) i = (int)gCamera3DNodes.size();
                        gCamera3DNodes.insert(gCamera3DNodes.begin() + i, node);
                    }
                });
            }
            if (ImGui::MenuItem("Node3D"))
            {
                Node3DNode node;
                node.name = "Node3D" + std::to_string((int)gNode3DNodes.size() + 1);
                node.primitiveMesh = "assets/cube_primitive.nebmesh";
                int idx = (int)gNode3DNodes.size();
                gNode3DNodes.push_back(node);
                gSelectedNode3D = idx;
                gSelectedAudio3D = -1;
                gSelectedStaticMesh = -1;
                gSelectedCamera3D = -1;

                PushUndo({"Create Node3D",
                    [idx]() { if (idx >= 0 && idx < (int)gNode3DNodes.size()) gNode3DNodes.erase(gNode3DNodes.begin() + idx); },
                    [idx, node]() {
                        if (idx < 0) return;
                        int i = idx;
                        if (i > (int)gNode3DNodes.size()) i = (int)gNode3DNodes.size();
                        gNode3DNodes.insert(gNode3DNodes.begin() + i, node);
                    }
                });
            }
            if (ImGui::MenuItem("NavMesh3D"))
            {
                NavMesh3DNode node;
                node.name = "NavMesh3D" + std::to_string((int)gNavMesh3DNodes.size() + 1);
                int idx = (int)gNavMesh3DNodes.size();
                gNavMesh3DNodes.push_back(node);
                gSelectedNavMesh3D = idx;
                gSelectedAudio3D = -1;
                gSelectedStaticMesh = -1;
                gSelectedCamera3D = -1;
                gSelectedNode3D = -1;

                PushUndo({"Create NavMesh3D",
                    [idx]() { if (idx >= 0 && idx < (int)gNavMesh3DNodes.size()) gNavMesh3DNodes.erase(gNavMesh3DNodes.begin() + idx); },
                    [idx, node]() {
                        if (idx < 0) return;
                        int i = idx;
                        if (i > (int)gNavMesh3DNodes.size()) i = (int)gNavMesh3DNodes.size();
                        gNavMesh3DNodes.insert(gNavMesh3DNodes.begin() + i, node);
                    }
                });
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    // List Audio3D nodes
    auto findAudioByNameLocal = [&](const std::string& nm)->int { for (int ai=0; ai<(int)gAudio3DNodes.size(); ++ai) if (gAudio3DNodes[ai].name == nm) return ai; return -1; };
    for (int i = 0; i < (int)gAudio3DNodes.size(); ++i)
    {
        auto& n = gAudio3DNodes[i];

        // Hide descendants of collapsed Audio3D parents.
        bool skipAudioNode = false;
        {
            std::string p = n.parent;
            while (!p.empty())
            {
                int pi = findAudioByNameLocal(p);
                if (pi < 0) break;
                if (gCollapsedAudioRoots.find(p) != gCollapsedAudioRoots.end()) { skipAudioNode = true; break; }
                p = gAudio3DNodes[pi].parent;
            }
        }
        if (skipAudioNode) continue;

        ImGui::PushID(i);
        {
            bool hasAudioChild = false;
            for (int ci = 0; ci < (int)gAudio3DNodes.size(); ++ci) if (gAudio3DNodes[ci].parent == n.name) { hasAudioChild = true; break; }
            for (int ci = 0; !hasAudioChild && ci < (int)gStaticMeshNodes.size(); ++ci) if (gStaticMeshNodes[ci].parent == n.name) { hasAudioChild = true; break; }
            for (int ci = 0; !hasAudioChild && ci < (int)gCamera3DNodes.size(); ++ci) if (gCamera3DNodes[ci].parent == n.name) { hasAudioChild = true; break; }
            for (int ci = 0; !hasAudioChild && ci < (int)gNode3DNodes.size(); ++ci) if (gNode3DNodes[ci].parent == n.name) { hasAudioChild = true; break; }
            bool audioExpanded = (gCollapsedAudioRoots.find(n.name) == gCollapsedAudioRoots.end());
            if (hasAudioChild)
            {
                if (ImGui::SmallButton(audioExpanded ? "v" : ">"))
                {
                    if (audioExpanded) gCollapsedAudioRoots.insert(n.name);
                    else gCollapsedAudioRoots.erase(n.name);
                }
                ImGui::SameLine();
            }
        }

        bool selected = (gSelectedAudio3D == i) || gMultiSelectedAudio3D.count(i);
        if (ImGui::Selectable(n.name.c_str(), selected))
        {
            if (io.KeyCtrl)
            {
                if (gMultiSelectedAudio3D.count(i)) gMultiSelectedAudio3D.erase(i);
                else gMultiSelectedAudio3D.insert(i);
                // Add current single-select to multi if first Ctrl+Click
                if (gSelectedAudio3D >= 0 && !gMultiSelectedAudio3D.empty())
                { gMultiSelectedAudio3D.insert(gSelectedAudio3D); gSelectedAudio3D = -1; }
                if (gSelectedStaticMesh >= 0) { gMultiSelectedStaticMesh.insert(gSelectedStaticMesh); gSelectedStaticMesh = -1; }
                if (gSelectedCamera3D >= 0) { gMultiSelectedCamera3D.insert(gSelectedCamera3D); gSelectedCamera3D = -1; }
                if (gSelectedNode3D >= 0) { gMultiSelectedNode3D.insert(gSelectedNode3D); gSelectedNode3D = -1; }
                if (gSelectedNavMesh3D >= 0) { gMultiSelectedNavMesh3D.insert(gSelectedNavMesh3D); gSelectedNavMesh3D = -1; }
            }
            else
            {
                ClearMultiSelection();
                gSelectedAudio3D = i;
                gSelectedStaticMesh = -1;
                gSelectedCamera3D = -1;
                gSelectedNode3D = -1;
                gSelectedNavMesh3D = -1;
            }
            gTransforming = false;
            gTransformMode = Transform_None;
            gAxisLock = 0;
            gLastTransformMouseX = io.MousePos.x;
            gLastTransformMouseY = io.MousePos.y;
        }

        if (ImGui::BeginDragDropSource())
        {
            int payload = i;
            ImGui::SetDragDropPayload("SCENE_AUDIO3D", &payload, sizeof(int));
            ImGui::Text("%s", n.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget())
        {
            auto tryReparentToAudio = [&](const char* payloadType, auto&& getName, auto&& setParent)
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadType))
                {
                    int child = *(const int*)payload->Data;
                    std::string childName;
                    if (getName(child, childName) && !WouldCreateHierarchyCycle(childName, n.name))
                        setParent(child, n.name);
                }
            };
            tryReparentToAudio("SCENE_AUDIO3D",
                [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gAudio3DNodes.size() || child == i) return false; childName = gAudio3DNodes[child].name; return true; },
                [&](int child, const std::string& parent){ gAudio3DNodes[child].parent = parent; });
            tryReparentToAudio("SCENE_STATICMESH",
                [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gStaticMeshNodes.size()) return false; childName = gStaticMeshNodes[child].name; return true; },
                [&](int child, const std::string& parent){ ReparentStaticMeshKeepWorldPos(child, parent); });
            tryReparentToAudio("SCENE_CAMERA3D",
                [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gCamera3DNodes.size()) return false; childName = gCamera3DNodes[child].name; return true; },
                [&](int child, const std::string& parent){ gCamera3DNodes[child].parent = parent; });
            tryReparentToAudio("SCENE_NODE3D",
                [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gNode3DNodes.size()) return false; childName = gNode3DNodes[child].name; return true; },
                [&](int child, const std::string& parent){ gNode3DNodes[child].parent = parent; });
            ImGui::EndDragDropTarget();
        }

        bool requestDelete = false;
        if (ImGui::BeginPopupContextItem("NodeContext"))
        {
            if (ImGui::MenuItem("Rename"))
            {
                gNodeRenameIndex = i;
                gNodeRenameStatic = false;
                gNodeRenameCamera = false;
                gNodeRenameNode3D = false;
                strncpy_s(gNodeRenameBuffer, n.name.c_str(), sizeof(gNodeRenameBuffer) - 1);
                gNodeRenameOpen = true;
            }
            if (ImGui::MenuItem("Duplicate"))
            {
                Audio3DNode dup = n;
                dup.name = n.name + "_copy";
                dup.x += 1.0f;
                gAudio3DNodes.push_back(dup);
            }
            if (ImGui::MenuItem("Unlink Hierarchy"))
            {
                n.parent.clear();
                for (auto& a : gAudio3DNodes) if (a.parent == n.name) a.parent.clear();
                for (auto& s : gStaticMeshNodes) if (s.parent == n.name) s.parent.clear();
                for (auto& c : gCamera3DNodes) if (c.parent == n.name) c.parent.clear();
                for (auto& n3 : gNode3DNodes) if (n3.parent == n.name) n3.parent.clear();
            }
            if (ImGui::MenuItem("Delete"))
            {
                requestDelete = true;
            }
            ImGui::EndPopup();
        }

        if (requestDelete)
        {
            int idx = i;
            Audio3DNode deleted = n;
            gAudio3DNodes.erase(gAudio3DNodes.begin() + idx);
            for (auto& n3 : gNode3DNodes)
                if (n3.parent == deleted.name) n3.parent.clear();
            if (gSelectedAudio3D == idx) gSelectedAudio3D = -1;
            else if (gSelectedAudio3D > idx) gSelectedAudio3D--;

            PushUndo({"Delete Audio3D",
                [idx, deleted]() {
                    int i = idx;
                    if (i < 0) return;
                    if (i > (int)gAudio3DNodes.size()) i = (int)gAudio3DNodes.size();
                    gAudio3DNodes.insert(gAudio3DNodes.begin() + i, deleted);
                },
                [idx]() {
                    if (idx >= 0 && idx < (int)gAudio3DNodes.size()) gAudio3DNodes.erase(gAudio3DNodes.begin() + idx);
                }
            });

            ImGui::PopID();
            continue;
        }


        ImGui::PopID();
    }

    if (!gStaticMeshNodes.empty())
    {
        std::function<void(int,int)> drawStaticNode = [&](int i, int depth)
        {
            auto& n = gStaticMeshNodes[i];
            ImGui::PushID(10000 + i);
            if (depth > 0) ImGui::Indent(14.0f * depth);

            bool hasStaticChild = false;
            for (int ci = 0; ci < (int)gStaticMeshNodes.size(); ++ci)
                if (gStaticMeshNodes[ci].parent == n.name) { hasStaticChild = true; break; }
            for (int ci = 0; !hasStaticChild && ci < (int)gAudio3DNodes.size(); ++ci) if (gAudio3DNodes[ci].parent == n.name) { hasStaticChild = true; break; }
            for (int ci = 0; !hasStaticChild && ci < (int)gCamera3DNodes.size(); ++ci) if (gCamera3DNodes[ci].parent == n.name) { hasStaticChild = true; break; }
            for (int ci = 0; !hasStaticChild && ci < (int)gNode3DNodes.size(); ++ci) if (gNode3DNodes[ci].parent == n.name) { hasStaticChild = true; break; }
            bool staticExpanded = (gCollapsedStaticRoots.find(n.name) == gCollapsedStaticRoots.end());
            if (hasStaticChild)
            {
                if (ImGui::SmallButton(staticExpanded ? "v" : ">"))
                {
                    if (staticExpanded) gCollapsedStaticRoots.insert(n.name);
                    else gCollapsedStaticRoots.erase(n.name);
                    staticExpanded = !staticExpanded;
                }
                ImGui::SameLine();
            }
            bool selected = (gSelectedStaticMesh == i) || gMultiSelectedStaticMesh.count(i);
            if (ImGui::Selectable(n.name.c_str(), selected))
            {
                if (io.KeyCtrl)
                {
                    if (gMultiSelectedStaticMesh.count(i)) gMultiSelectedStaticMesh.erase(i);
                    else gMultiSelectedStaticMesh.insert(i);
                    if (gSelectedStaticMesh >= 0 && !gMultiSelectedStaticMesh.empty())
                    { gMultiSelectedStaticMesh.insert(gSelectedStaticMesh); gSelectedStaticMesh = -1; }
                    if (gSelectedAudio3D >= 0) { gMultiSelectedAudio3D.insert(gSelectedAudio3D); gSelectedAudio3D = -1; }
                    if (gSelectedCamera3D >= 0) { gMultiSelectedCamera3D.insert(gSelectedCamera3D); gSelectedCamera3D = -1; }
                    if (gSelectedNode3D >= 0) { gMultiSelectedNode3D.insert(gSelectedNode3D); gSelectedNode3D = -1; }
                    if (gSelectedNavMesh3D >= 0) { gMultiSelectedNavMesh3D.insert(gSelectedNavMesh3D); gSelectedNavMesh3D = -1; }
                }
                else
                {
                    ClearMultiSelection();
                    gSelectedStaticMesh = i;
                    gSelectedAudio3D = -1;
                    gSelectedCamera3D = -1;
                    gSelectedNode3D = -1;
                    gSelectedNavMesh3D = -1;
                }
                gTransforming = false;
                gTransformMode = Transform_None;
                gAxisLock = 0;
                gLastTransformMouseX = io.MousePos.x;
                gLastTransformMouseY = io.MousePos.y;
            }

            if (ImGui::BeginDragDropSource())
            {
                int payload = i;
                ImGui::SetDragDropPayload("SCENE_STATICMESH", &payload, sizeof(int));
                ImGui::Text("%s", n.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                auto tryReparentToStatic = [&](const char* payloadType, auto&& getName, auto&& setParent)
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadType))
                    {
                        int child = *(const int*)payload->Data;
                        std::string childName;
                        if (getName(child, childName) && !WouldCreateHierarchyCycle(childName, n.name))
                            setParent(child, n.name);
                    }
                };
                tryReparentToStatic("SCENE_AUDIO3D",
                    [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gAudio3DNodes.size()) return false; childName = gAudio3DNodes[child].name; return true; },
                    [&](int child, const std::string& parent){ gAudio3DNodes[child].parent = parent; });
                tryReparentToStatic("SCENE_STATICMESH",
                    [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gStaticMeshNodes.size() || child == i) return false; childName = gStaticMeshNodes[child].name; return true; },
                    [&](int child, const std::string& parent){ ReparentStaticMeshKeepWorldPos(child, parent); });
                tryReparentToStatic("SCENE_CAMERA3D",
                    [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gCamera3DNodes.size()) return false; childName = gCamera3DNodes[child].name; return true; },
                    [&](int child, const std::string& parent){ gCamera3DNodes[child].parent = parent; });
                tryReparentToStatic("SCENE_NODE3D",
                    [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gNode3DNodes.size()) return false; childName = gNode3DNodes[child].name; return true; },
                    [&](int child, const std::string& parent){ gNode3DNodes[child].parent = parent; });
                ImGui::EndDragDropTarget();
            }

            bool requestDelete = false;
            if (ImGui::BeginPopupContextItem("NodeContext"))
            {
                if (ImGui::MenuItem("Rename"))
                {
                    gNodeRenameIndex = i;
                    gNodeRenameStatic = true;
                    gNodeRenameCamera = false;
                    gNodeRenameNode3D = false;
                    strncpy_s(gNodeRenameBuffer, n.name.c_str(), sizeof(gNodeRenameBuffer) - 1);
                    gNodeRenameOpen = true;
                }
                if (ImGui::MenuItem("Duplicate"))
                {
                    StaticMesh3DNode dup = n;
                    dup.name = n.name + "_copy";
                    dup.x += 1.0f;
                    gStaticMeshNodes.push_back(dup);
                }
                if (!n.parent.empty() && ImGui::MenuItem("Unparent"))
                {
                    n.parent.clear();
                }
                if (ImGui::MenuItem("Unlink Hierarchy"))
                {
                    n.parent.clear();
                    for (auto& a : gAudio3DNodes) if (a.parent == n.name) a.parent.clear();
                    for (auto& sm : gStaticMeshNodes) if (sm.parent == n.name) sm.parent.clear();
                    for (auto& c : gCamera3DNodes) if (c.parent == n.name) c.parent.clear();
                    for (auto& n3 : gNode3DNodes) if (n3.parent == n.name) n3.parent.clear();
                }
                if (ImGui::MenuItem("Delete"))
                {
                    requestDelete = true;
                }
                ImGui::EndPopup();
            }

            if (requestDelete)
            {
                int idx = i;
                StaticMesh3DNode deleted = n;
                gStaticMeshNodes.erase(gStaticMeshNodes.begin() + idx);
                for (auto& sm : gStaticMeshNodes)
                    if (sm.parent == deleted.name) sm.parent.clear();
                for (auto& n3 : gNode3DNodes)
                    if (n3.parent == deleted.name) n3.parent.clear();
                if (gSelectedStaticMesh == idx) gSelectedStaticMesh = -1;
                else if (gSelectedStaticMesh > idx) gSelectedStaticMesh--;

                PushUndo({"Delete StaticMesh3D",
                    [idx, deleted]() {
                        int i = idx;
                        if (i < 0) return;
                        if (i > (int)gStaticMeshNodes.size()) i = (int)gStaticMeshNodes.size();
                        gStaticMeshNodes.insert(gStaticMeshNodes.begin() + i, deleted);
                    },
                    [idx]() {
                        if (idx >= 0 && idx < (int)gStaticMeshNodes.size()) gStaticMeshNodes.erase(gStaticMeshNodes.begin() + idx);
                    }
                });

                if (depth > 0) ImGui::Unindent(14.0f * depth);
                ImGui::PopID();
                return;
            }

            if (staticExpanded)
            {
                for (int ci = 0; ci < (int)gStaticMeshNodes.size(); ++ci)
                    if (gStaticMeshNodes[ci].parent == n.name) drawStaticNode(ci, depth + 1);
            }

            if (depth > 0) ImGui::Unindent(14.0f * depth);
            ImGui::PopID();
        };

        for (int i = 0; i < (int)gStaticMeshNodes.size(); ++i)
            if (gStaticMeshNodes[i].parent.empty()
                || (FindStaticMeshByName(gStaticMeshNodes[i].parent) < 0
                    && FindNode3DByName(gStaticMeshNodes[i].parent) < 0))
                drawStaticNode(i, 0);
    }

    if (!gCamera3DNodes.empty())
    {
        auto findCameraByNameLocal = [&](const std::string& nm)->int { for (int ci=0; ci<(int)gCamera3DNodes.size(); ++ci) if (gCamera3DNodes[ci].name == nm) return ci; return -1; };
        for (int i = 0; i < (int)gCamera3DNodes.size(); ++i)
        {
            auto& n = gCamera3DNodes[i];

            // If this camera is directly parented to a Node3D, it is shown under that Node3D branch.
            if (!n.parent.empty() && FindNode3DByName(n.parent) >= 0)
                continue;

            // Hide descendants of collapsed Camera3D parents.
            bool skipCameraNode = false;
            {
                std::string p = n.parent;
                while (!p.empty())
                {
                    int pi = findCameraByNameLocal(p);
                    if (pi < 0) break;
                    if (gCollapsedCameraRoots.find(p) != gCollapsedCameraRoots.end()) { skipCameraNode = true; break; }
                    p = gCamera3DNodes[pi].parent;
                }
            }
            if (skipCameraNode) continue;

            ImGui::PushID(20000 + i);
            {
                bool hasCameraChild = false;
                for (int ci = 0; ci < (int)gCamera3DNodes.size(); ++ci) if (gCamera3DNodes[ci].parent == n.name) { hasCameraChild = true; break; }
                for (int ci = 0; !hasCameraChild && ci < (int)gAudio3DNodes.size(); ++ci) if (gAudio3DNodes[ci].parent == n.name) { hasCameraChild = true; break; }
                for (int ci = 0; !hasCameraChild && ci < (int)gStaticMeshNodes.size(); ++ci) if (gStaticMeshNodes[ci].parent == n.name) { hasCameraChild = true; break; }
                for (int ci = 0; !hasCameraChild && ci < (int)gNode3DNodes.size(); ++ci) if (gNode3DNodes[ci].parent == n.name) { hasCameraChild = true; break; }
                bool cameraExpanded = (gCollapsedCameraRoots.find(n.name) == gCollapsedCameraRoots.end());
                if (hasCameraChild)
                {
                    if (ImGui::SmallButton(cameraExpanded ? "v" : ">"))
                    {
                        if (cameraExpanded) gCollapsedCameraRoots.insert(n.name);
                        else gCollapsedCameraRoots.erase(n.name);
                    }
                    ImGui::SameLine();
                }
            }

            bool selected = (gSelectedCamera3D == i) || gMultiSelectedCamera3D.count(i);
            if (ImGui::Selectable(n.name.c_str(), selected))
            {
                if (io.KeyCtrl)
                {
                    if (gMultiSelectedCamera3D.count(i)) gMultiSelectedCamera3D.erase(i);
                    else gMultiSelectedCamera3D.insert(i);
                    if (gSelectedCamera3D >= 0 && !gMultiSelectedCamera3D.empty())
                    { gMultiSelectedCamera3D.insert(gSelectedCamera3D); gSelectedCamera3D = -1; }
                    if (gSelectedAudio3D >= 0) { gMultiSelectedAudio3D.insert(gSelectedAudio3D); gSelectedAudio3D = -1; }
                    if (gSelectedStaticMesh >= 0) { gMultiSelectedStaticMesh.insert(gSelectedStaticMesh); gSelectedStaticMesh = -1; }
                    if (gSelectedNode3D >= 0) { gMultiSelectedNode3D.insert(gSelectedNode3D); gSelectedNode3D = -1; }
                    if (gSelectedNavMesh3D >= 0) { gMultiSelectedNavMesh3D.insert(gSelectedNavMesh3D); gSelectedNavMesh3D = -1; }
                }
                else
                {
                    ClearMultiSelection();
                    gSelectedCamera3D = i;
                    gSelectedAudio3D = -1;
                    gSelectedStaticMesh = -1;
                    gSelectedNode3D = -1;
                    gSelectedNavMesh3D = -1;
                }
                gTransforming = false;
                gTransformMode = Transform_None;
                gAxisLock = 0;
                gLastTransformMouseX = io.MousePos.x;
                gLastTransformMouseY = io.MousePos.y;
            }

            if (ImGui::BeginDragDropSource())
            {
                int payload = i;
                ImGui::SetDragDropPayload("SCENE_CAMERA3D", &payload, sizeof(int));
                ImGui::Text("%s", n.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                auto tryReparentToCamera = [&](const char* payloadType, auto&& getName, auto&& setParent)
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadType))
                    {
                        int child = *(const int*)payload->Data;
                        std::string childName;
                        if (getName(child, childName) && !WouldCreateHierarchyCycle(childName, n.name))
                            setParent(child, n.name);
                    }
                };
                tryReparentToCamera("SCENE_AUDIO3D",
                    [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gAudio3DNodes.size()) return false; childName = gAudio3DNodes[child].name; return true; },
                    [&](int child, const std::string& parent){ gAudio3DNodes[child].parent = parent; });
                tryReparentToCamera("SCENE_STATICMESH",
                    [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gStaticMeshNodes.size()) return false; childName = gStaticMeshNodes[child].name; return true; },
                    [&](int child, const std::string& parent){ ReparentStaticMeshKeepWorldPos(child, parent); });
                tryReparentToCamera("SCENE_CAMERA3D",
                    [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gCamera3DNodes.size() || child == i) return false; childName = gCamera3DNodes[child].name; return true; },
                    [&](int child, const std::string& parent){ gCamera3DNodes[child].parent = parent; });
                tryReparentToCamera("SCENE_NODE3D",
                    [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gNode3DNodes.size()) return false; childName = gNode3DNodes[child].name; return true; },
                    [&](int child, const std::string& parent){ gNode3DNodes[child].parent = parent; });
                ImGui::EndDragDropTarget();
            }

            bool requestDelete = false;
            if (ImGui::BeginPopupContextItem("NodeContext"))
            {
                if (ImGui::MenuItem("Rename"))
                {
                    gNodeRenameIndex = i;
                    gNodeRenameStatic = false;
                    gNodeRenameCamera = true;
                    gNodeRenameNode3D = false;
                    strncpy_s(gNodeRenameBuffer, n.name.c_str(), sizeof(gNodeRenameBuffer) - 1);
                    gNodeRenameOpen = true;
                }
                if (ImGui::MenuItem("Duplicate"))
                {
                    Camera3DNode dup = n;
                    dup.name = n.name + "_copy";
                    dup.main = false;
                    dup.x += 1.0f;
                    gCamera3DNodes.push_back(dup);
                }
                if (ImGui::MenuItem("Unlink Hierarchy"))
                {
                    n.parent.clear();
                    for (auto& a : gAudio3DNodes) if (a.parent == n.name) a.parent.clear();
                    for (auto& s : gStaticMeshNodes) if (s.parent == n.name) s.parent.clear();
                    for (auto& c : gCamera3DNodes) if (c.parent == n.name) c.parent.clear();
                    for (auto& n3 : gNode3DNodes) if (n3.parent == n.name) n3.parent.clear();
                }
                if (ImGui::MenuItem("Delete"))
                {
                    requestDelete = true;
                }
                ImGui::EndPopup();
            }

            if (requestDelete)
            {
                int idx = i;
                Camera3DNode deleted = n;
                gCamera3DNodes.erase(gCamera3DNodes.begin() + idx);
                for (auto& n3 : gNode3DNodes)
                    if (n3.parent == deleted.name) n3.parent.clear();
                if (deleted.main && !gCamera3DNodes.empty())
                {
                    bool hasMain = false;
                    for (const auto& c : gCamera3DNodes) { if (c.main) { hasMain = true; break; } }
                    if (!hasMain) gCamera3DNodes[0].main = true;
                }
                if (gSelectedCamera3D == idx) gSelectedCamera3D = -1;
                else if (gSelectedCamera3D > idx) gSelectedCamera3D--;

                PushUndo({"Delete Camera3D",
                    [idx, deleted]() {
                        int i = idx;
                        if (i < 0) return;
                        if (i > (int)gCamera3DNodes.size()) i = (int)gCamera3DNodes.size();
                        gCamera3DNodes.insert(gCamera3DNodes.begin() + i, deleted);
                    },
                    [idx]() {
                        if (idx >= 0 && idx < (int)gCamera3DNodes.size()) gCamera3DNodes.erase(gCamera3DNodes.begin() + idx);
                    }
                });

                ImGui::PopID();
                continue;
            }

            ImGui::PopID();
        }
    }

    if (!gNode3DNodes.empty())
    {
        std::vector<bool> node3dDrawn((size_t)gNode3DNodes.size(), false);
        std::function<void(int,int)> drawNode3D = [&](int i, int depth)
        {
            if (i < 0 || i >= (int)gNode3DNodes.size()) return;
            if (node3dDrawn[(size_t)i]) return;
            node3dDrawn[(size_t)i] = true;
            auto& n = gNode3DNodes[i];
            ImGui::PushID(40000 + i);
            if (depth > 0) ImGui::Indent(14.0f * depth);

            bool hasNode3DChild = false;
            for (int ci = 0; ci < (int)gNode3DNodes.size(); ++ci)
                if (gNode3DNodes[ci].parent == n.name) { hasNode3DChild = true; break; }
            for (int ci = 0; !hasNode3DChild && ci < (int)gAudio3DNodes.size(); ++ci) if (gAudio3DNodes[ci].parent == n.name) { hasNode3DChild = true; break; }
            for (int ci = 0; !hasNode3DChild && ci < (int)gStaticMeshNodes.size(); ++ci) if (gStaticMeshNodes[ci].parent == n.name) { hasNode3DChild = true; break; }
            for (int ci = 0; !hasNode3DChild && ci < (int)gCamera3DNodes.size(); ++ci) if (gCamera3DNodes[ci].parent == n.name) { hasNode3DChild = true; break; }
            bool node3DExpanded = (gCollapsedNode3DRoots.find(n.name) == gCollapsedNode3DRoots.end());
            if (hasNode3DChild)
            {
                if (ImGui::SmallButton(node3DExpanded ? "v" : ">"))
                {
                    if (node3DExpanded) gCollapsedNode3DRoots.insert(n.name);
                    else gCollapsedNode3DRoots.erase(n.name);
                    node3DExpanded = !node3DExpanded;
                }
                ImGui::SameLine();
            }
            bool selected = (gSelectedNode3D == i) || gMultiSelectedNode3D.count(i);
            if (ImGui::Selectable(n.name.c_str(), selected))
            {
                if (io.KeyCtrl)
                {
                    if (gMultiSelectedNode3D.count(i)) gMultiSelectedNode3D.erase(i);
                    else gMultiSelectedNode3D.insert(i);
                    if (gSelectedNode3D >= 0 && !gMultiSelectedNode3D.empty())
                    { gMultiSelectedNode3D.insert(gSelectedNode3D); gSelectedNode3D = -1; }
                    if (gSelectedAudio3D >= 0) { gMultiSelectedAudio3D.insert(gSelectedAudio3D); gSelectedAudio3D = -1; }
                    if (gSelectedStaticMesh >= 0) { gMultiSelectedStaticMesh.insert(gSelectedStaticMesh); gSelectedStaticMesh = -1; }
                    if (gSelectedCamera3D >= 0) { gMultiSelectedCamera3D.insert(gSelectedCamera3D); gSelectedCamera3D = -1; }
                    if (gSelectedNavMesh3D >= 0) { gMultiSelectedNavMesh3D.insert(gSelectedNavMesh3D); gSelectedNavMesh3D = -1; }
                }
                else
                {
                    ClearMultiSelection();
                    gSelectedNode3D = i;
                    gSelectedAudio3D = -1;
                    gSelectedStaticMesh = -1;
                    gSelectedCamera3D = -1;
                    gSelectedNavMesh3D = -1;
                }
                gTransforming = false;
                gTransformMode = Transform_None;
                gAxisLock = 0;
                gLastTransformMouseX = io.MousePos.x;
                gLastTransformMouseY = io.MousePos.y;
            }

            if (ImGui::BeginDragDropSource())
            {
                int payload = i;
                ImGui::SetDragDropPayload("SCENE_NODE3D", &payload, sizeof(int));
                ImGui::Text("%s", n.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                auto tryReparentToNode3D = [&](const char* payloadType, auto&& getName, auto&& setParent)
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadType))
                    {
                        int child = *(const int*)payload->Data;
                        std::string childName;
                        if (getName(child, childName) && !WouldCreateHierarchyCycle(childName, n.name))
                            setParent(child, n.name);
                    }
                };
                tryReparentToNode3D("SCENE_AUDIO3D",
                    [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gAudio3DNodes.size()) return false; childName = gAudio3DNodes[child].name; return true; },
                    [&](int child, const std::string& parent){ gAudio3DNodes[child].parent = parent; });
                tryReparentToNode3D("SCENE_STATICMESH",
                    [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gStaticMeshNodes.size()) return false; childName = gStaticMeshNodes[child].name; return true; },
                    [&](int child, const std::string& parent){ ReparentStaticMeshKeepWorldPos(child, parent); });
                tryReparentToNode3D("SCENE_CAMERA3D",
                    [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gCamera3DNodes.size()) return false; childName = gCamera3DNodes[child].name; return true; },
                    [&](int child, const std::string& parent){ gCamera3DNodes[child].parent = parent; });
                tryReparentToNode3D("SCENE_NODE3D",
                    [&](int child, std::string& childName)->bool { if (child < 0 || child >= (int)gNode3DNodes.size() || child == i) return false; childName = gNode3DNodes[child].name; return true; },
                    [&](int child, const std::string& parent){ gNode3DNodes[child].parent = parent; });
                ImGui::EndDragDropTarget();
            }

            bool requestDelete = false;
            if (ImGui::BeginPopupContextItem("NodeContext"))
            {
                if (ImGui::MenuItem("Rename"))
                {
                    gNodeRenameIndex = i;
                    gNodeRenameStatic = false;
                    gNodeRenameCamera = false;
                    gNodeRenameNode3D = true;
                    strncpy_s(gNodeRenameBuffer, n.name.c_str(), sizeof(gNodeRenameBuffer) - 1);
                    gNodeRenameOpen = true;
                }
                if (ImGui::MenuItem("Duplicate"))
                {
                    Node3DNode dup = n;
                    dup.name = n.name + "_copy";
                    dup.x += 1.0f;
                    gNode3DNodes.push_back(dup);
                }
                if (!n.parent.empty() && ImGui::MenuItem("Unparent"))
                {
                    n.parent.clear();
                }
                if (ImGui::MenuItem("Unlink Hierarchy"))
                {
                    n.parent.clear();
                    for (auto& a : gAudio3DNodes) if (a.parent == n.name) a.parent.clear();
                    for (auto& s : gStaticMeshNodes) if (s.parent == n.name) s.parent.clear();
                    for (auto& c : gCamera3DNodes) if (c.parent == n.name) c.parent.clear();
                    for (auto& nn : gNode3DNodes) if (nn.parent == n.name) nn.parent.clear();
                }
                if (ImGui::MenuItem("Delete"))
                {
                    requestDelete = true;
                }
                ImGui::EndPopup();
            }

            if (requestDelete)
            {
                int idx = i;
                Node3DNode deleted = n;
                gNode3DNodes.erase(gNode3DNodes.begin() + idx);
                for (auto& nn : gNode3DNodes)
                    if (nn.parent == deleted.name) nn.parent.clear();
                if (gSelectedNode3D == idx) gSelectedNode3D = -1;
                else if (gSelectedNode3D > idx) gSelectedNode3D--;

                PushUndo({"Delete Node3D",
                    [idx, deleted]() {
                        int i = idx;
                        if (i < 0) return;
                        if (i > (int)gNode3DNodes.size()) i = (int)gNode3DNodes.size();
                        gNode3DNodes.insert(gNode3DNodes.begin() + i, deleted);
                    },
                    [idx]() {
                        if (idx >= 0 && idx < (int)gNode3DNodes.size()) gNode3DNodes.erase(gNode3DNodes.begin() + idx);
                    }
                });

                if (depth > 0) ImGui::Unindent(14.0f * depth);
                ImGui::PopID();
                return;
            }

            if (node3DExpanded)
            {
                for (int ci = 0; ci < (int)gNode3DNodes.size(); ++ci)
                    if (gNode3DNodes[ci].parent == n.name) drawNode3D(ci, depth + 1);

                auto drawChildLeaf = [&](const std::string& label, bool selected, auto&& onSelect)
                {
                    ImGui::PushID(label.c_str());
                    if (depth + 1 > 0) ImGui::Indent(14.0f * (depth + 1));
                    if (ImGui::Selectable(label.c_str(), selected)) onSelect();
                    if (depth + 1 > 0) ImGui::Unindent(14.0f * (depth + 1));
                    ImGui::PopID();
                };

                // Show direct non-Node3D children under Node3D parents so hierarchy linking is visible in-place.
                for (int ai = 0; ai < (int)gAudio3DNodes.size(); ++ai)
                {
                    if (gAudio3DNodes[ai].parent != n.name) continue;
                    drawChildLeaf(std::string("[Audio] ") + gAudio3DNodes[ai].name, gSelectedAudio3D == ai, [&]() {
                        gSelectedAudio3D = ai;
                        gSelectedStaticMesh = -1;
                        gSelectedCamera3D = -1;
                        gSelectedNode3D = -1;
                    });
                }
                for (int si = 0; si < (int)gStaticMeshNodes.size(); ++si)
                {
                    if (gStaticMeshNodes[si].parent != n.name) continue;
                    drawChildLeaf(std::string("[StaticMesh3D] ") + gStaticMeshNodes[si].name, gSelectedStaticMesh == si, [&]() {
                        gSelectedAudio3D = -1;
                        gSelectedStaticMesh = si;
                        gSelectedCamera3D = -1;
                        gSelectedNode3D = -1;
                    });
                }
                for (int ci = 0; ci < (int)gCamera3DNodes.size(); ++ci)
                {
                    if (gCamera3DNodes[ci].parent != n.name) continue;
                    drawChildLeaf(std::string("[Camera3D] ") + gCamera3DNodes[ci].name, gSelectedCamera3D == ci, [&]() {
                        gSelectedAudio3D = -1;
                        gSelectedStaticMesh = -1;
                        gSelectedCamera3D = ci;
                        gSelectedNode3D = -1;
                    });
                }
            }

            if (depth > 0) ImGui::Unindent(14.0f * depth);
            ImGui::PopID();
        };

        for (int i = 0; i < (int)gNode3DNodes.size(); ++i)
            if (gNode3DNodes[i].parent.empty() || FindNode3DByName(gNode3DNodes[i].parent) < 0)
                drawNode3D(i, 0);
        for (int i = 0; i < (int)gNode3DNodes.size(); ++i)
            if (!node3dDrawn[(size_t)i])
                drawNode3D(i, 0);
    }

    // List NavMesh3D nodes
    for (int i = 0; i < (int)gNavMesh3DNodes.size(); ++i)
    {
        auto& n = gNavMesh3DNodes[i];
        ImGui::PushID(10000 + i);
        bool selected = (gSelectedNavMesh3D == i) || gMultiSelectedNavMesh3D.count(i);
        std::string label = n.name + (n.navNegator ? " [NEG]" : "") + (n.cullWalls ? " [CULL]" : "");
        if (ImGui::Selectable(label.c_str(), selected))
        {
            if (io.KeyCtrl)
            {
                if (gMultiSelectedNavMesh3D.count(i)) gMultiSelectedNavMesh3D.erase(i);
                else gMultiSelectedNavMesh3D.insert(i);
                if (gSelectedNavMesh3D >= 0 && !gMultiSelectedNavMesh3D.empty())
                { gMultiSelectedNavMesh3D.insert(gSelectedNavMesh3D); gSelectedNavMesh3D = -1; }
                if (gSelectedAudio3D >= 0) { gMultiSelectedAudio3D.insert(gSelectedAudio3D); gSelectedAudio3D = -1; }
                if (gSelectedStaticMesh >= 0) { gMultiSelectedStaticMesh.insert(gSelectedStaticMesh); gSelectedStaticMesh = -1; }
                if (gSelectedCamera3D >= 0) { gMultiSelectedCamera3D.insert(gSelectedCamera3D); gSelectedCamera3D = -1; }
                if (gSelectedNode3D >= 0) { gMultiSelectedNode3D.insert(gSelectedNode3D); gSelectedNode3D = -1; }
            }
            else
            {
                ClearMultiSelection();
                gSelectedNavMesh3D = i;
                gSelectedAudio3D = -1;
                gSelectedStaticMesh = -1;
                gSelectedCamera3D = -1;
                gSelectedNode3D = -1;
            }
            gTransforming = false;
            gTransformMode = Transform_None;
            gAxisLock = 0;
        }

        bool requestDelete = false;
        if (ImGui::BeginPopupContextItem("NavMesh3DContext"))
        {
            if (ImGui::MenuItem("Duplicate"))
            {
                NavMesh3DNode dup = n;
                dup.name = n.name + "_copy";
                dup.x += 1.0f;
                gNavMesh3DNodes.push_back(dup);
            }
            if (ImGui::MenuItem("Delete"))
                requestDelete = true;
            ImGui::EndPopup();
        }
        if (requestDelete)
        {
            gNavMesh3DNodes.erase(gNavMesh3DNodes.begin() + i);
            if (gSelectedNavMesh3D == i) gSelectedNavMesh3D = -1;
            else if (gSelectedNavMesh3D > i) --gSelectedNavMesh3D;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    ImGui::End();

    if (gNodeRenameOpen)
    {
        ImGui::OpenPopup("RenameNode");
    }
    if (ImGui::BeginPopupModal("RenameNode", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("New name", gNodeRenameBuffer, sizeof(gNodeRenameBuffer));
        if (ImGui::Button("OK"))
        {
            if (!gNodeRenameStatic && !gNodeRenameCamera && !gNodeRenameNode3D && gNodeRenameIndex >= 0 && gNodeRenameIndex < (int)gAudio3DNodes.size())
            {
                int idx = gNodeRenameIndex;
                std::string oldName = gAudio3DNodes[idx].name;
                std::string newName = gNodeRenameBuffer;
                gAudio3DNodes[idx].name = newName;
                for (auto& a : gAudio3DNodes) if (a.parent == oldName) a.parent = newName;
                for (auto& s : gStaticMeshNodes) if (s.parent == oldName) s.parent = newName;
                for (auto& c : gCamera3DNodes) if (c.parent == oldName) c.parent = newName;
                for (auto& n3 : gNode3DNodes) if (n3.parent == oldName) n3.parent = newName;

                PushUndo({"Rename Node",
                    [idx, oldName]() {
                        if (idx >= 0 && idx < (int)gAudio3DNodes.size())
                        {
                            std::string cur = gAudio3DNodes[idx].name;
                            gAudio3DNodes[idx].name = oldName;
                            for (auto& a : gAudio3DNodes) if (a.parent == cur) a.parent = oldName;
                            for (auto& s : gStaticMeshNodes) if (s.parent == cur) s.parent = oldName;
                            for (auto& c : gCamera3DNodes) if (c.parent == cur) c.parent = oldName;
                            for (auto& n3 : gNode3DNodes) if (n3.parent == cur) n3.parent = oldName;
                        }
                    },
                    [idx, newName]() {
                        if (idx >= 0 && idx < (int)gAudio3DNodes.size())
                        {
                            std::string cur = gAudio3DNodes[idx].name;
                            gAudio3DNodes[idx].name = newName;
                            for (auto& a : gAudio3DNodes) if (a.parent == cur) a.parent = newName;
                            for (auto& s : gStaticMeshNodes) if (s.parent == cur) s.parent = newName;
                            for (auto& c : gCamera3DNodes) if (c.parent == cur) c.parent = newName;
                            for (auto& n3 : gNode3DNodes) if (n3.parent == cur) n3.parent = newName;
                        }
                    }
                });
            }
            else if (gNodeRenameStatic && gNodeRenameIndex >= 0 && gNodeRenameIndex < (int)gStaticMeshNodes.size())
            {
                int idx = gNodeRenameIndex;
                std::string oldName = gStaticMeshNodes[idx].name;
                std::string newName = gNodeRenameBuffer;
                gStaticMeshNodes[idx].name = newName;
                for (auto& sm : gStaticMeshNodes)
                    if (sm.parent == oldName) sm.parent = newName;
                for (auto& n3 : gNode3DNodes)
                    if (n3.parent == oldName) n3.parent = newName;

                PushUndo({"Rename StaticMesh3D",
                    [idx, oldName]() {
                        if (idx >= 0 && idx < (int)gStaticMeshNodes.size())
                        {
                            std::string cur = gStaticMeshNodes[idx].name;
                            gStaticMeshNodes[idx].name = oldName;
                            for (auto& sm : gStaticMeshNodes) if (sm.parent == cur) sm.parent = oldName;
                            for (auto& n3 : gNode3DNodes) if (n3.parent == cur) n3.parent = oldName;
                        }
                    },
                    [idx, newName]() {
                        if (idx >= 0 && idx < (int)gStaticMeshNodes.size())
                        {
                            std::string cur = gStaticMeshNodes[idx].name;
                            gStaticMeshNodes[idx].name = newName;
                            for (auto& sm : gStaticMeshNodes) if (sm.parent == cur) sm.parent = newName;
                            for (auto& n3 : gNode3DNodes) if (n3.parent == cur) n3.parent = newName;
                        }
                    }
                });
            }
            else if (gNodeRenameCamera && gNodeRenameIndex >= 0 && gNodeRenameIndex < (int)gCamera3DNodes.size())
            {
                int idx = gNodeRenameIndex;
                std::string oldName = gCamera3DNodes[idx].name;
                std::string newName = gNodeRenameBuffer;
                gCamera3DNodes[idx].name = newName;
                for (auto& a : gAudio3DNodes) if (a.parent == oldName) a.parent = newName;
                for (auto& s : gStaticMeshNodes) if (s.parent == oldName) s.parent = newName;
                for (auto& c : gCamera3DNodes) if (c.parent == oldName) c.parent = newName;
                for (auto& n3 : gNode3DNodes) if (n3.parent == oldName) n3.parent = newName;

                PushUndo({"Rename Camera3D",
                    [idx, oldName]() {
                        if (idx >= 0 && idx < (int)gCamera3DNodes.size())
                        {
                            std::string cur = gCamera3DNodes[idx].name;
                            gCamera3DNodes[idx].name = oldName;
                            for (auto& a : gAudio3DNodes) if (a.parent == cur) a.parent = oldName;
                            for (auto& s : gStaticMeshNodes) if (s.parent == cur) s.parent = oldName;
                            for (auto& c : gCamera3DNodes) if (c.parent == cur) c.parent = oldName;
                            for (auto& n3 : gNode3DNodes) if (n3.parent == cur) n3.parent = oldName;
                        }
                    },
                    [idx, newName]() {
                        if (idx >= 0 && idx < (int)gCamera3DNodes.size())
                        {
                            std::string cur = gCamera3DNodes[idx].name;
                            gCamera3DNodes[idx].name = newName;
                            for (auto& a : gAudio3DNodes) if (a.parent == cur) a.parent = newName;
                            for (auto& s : gStaticMeshNodes) if (s.parent == cur) s.parent = newName;
                            for (auto& c : gCamera3DNodes) if (c.parent == cur) c.parent = newName;
                            for (auto& n3 : gNode3DNodes) if (n3.parent == cur) n3.parent = newName;
                        }
                    }
                });
            }
            else if (gNodeRenameNode3D && gNodeRenameIndex >= 0 && gNodeRenameIndex < (int)gNode3DNodes.size())
            {
                int idx = gNodeRenameIndex;
                std::string oldName = gNode3DNodes[idx].name;
                std::string newName = gNodeRenameBuffer;
                gNode3DNodes[idx].name = newName;
                for (auto& a : gAudio3DNodes) if (a.parent == oldName) a.parent = newName;
                for (auto& s : gStaticMeshNodes) if (s.parent == oldName) s.parent = newName;
                for (auto& c : gCamera3DNodes) if (c.parent == oldName) c.parent = newName;
                for (auto& n3 : gNode3DNodes) if (n3.parent == oldName) n3.parent = newName;

                PushUndo({"Rename Node3D",
                    [idx, oldName]() {
                        if (idx >= 0 && idx < (int)gNode3DNodes.size())
                        {
                            std::string cur = gNode3DNodes[idx].name;
                            gNode3DNodes[idx].name = oldName;
                            for (auto& a : gAudio3DNodes) if (a.parent == cur) a.parent = oldName;
                            for (auto& s : gStaticMeshNodes) if (s.parent == cur) s.parent = oldName;
                            for (auto& c : gCamera3DNodes) if (c.parent == cur) c.parent = oldName;
                            for (auto& n3 : gNode3DNodes) if (n3.parent == cur) n3.parent = oldName;
                        }
                    },
                    [idx, newName]() {
                        if (idx >= 0 && idx < (int)gNode3DNodes.size())
                        {
                            std::string cur = gNode3DNodes[idx].name;
                            gNode3DNodes[idx].name = newName;
                            for (auto& a : gAudio3DNodes) if (a.parent == cur) a.parent = newName;
                            for (auto& s : gStaticMeshNodes) if (s.parent == cur) s.parent = newName;
                            for (auto& c : gCamera3DNodes) if (c.parent == cur) c.parent = newName;
                            for (auto& n3 : gNode3DNodes) if (n3.parent == cur) n3.parent = newName;
                        }
                    }
                });
            }
            gNodeRenameOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            gNodeRenameOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
