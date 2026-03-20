#include "scene_tabs.h"

#include "imgui.h"
#include <GLFW/glfw3.h>

#include <vector>
#include <string>
#include <filesystem>

#include "../nodes/NodeTypes.h"
#include "../scene/scene_manager.h"
#include "../editor/project.h"

#include "../editor/editor_state.h"

void DrawSceneTabs(const ImGuiViewport* vp, float topBarH, float leftPanelWidth, float rightPanelWidth)
{
        // Scene tabs (top of viewport)
        if (!gOpenScenes.empty())
        {
            ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + leftPanelWidth, vp->Pos.y + topBarH));
            ImGui::SetNextWindowSize(ImVec2(vp->Size.x - leftPanelWidth - rightPanelWidth, 26.0f * ImGui::GetIO().FontGlobalScale));
            ImGui::Begin("##SceneTabs", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);
            ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.086f, 0.082f, 0.086f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
            if (ImGui::BeginTabBar("Scenes"))
            {
                for (int i = 0; i < (int)gOpenScenes.size(); ++i)
                {
                    bool open = true;
                    const char* label = gOpenScenes[i].name.c_str();
                    ImGuiTabItemFlags tabFlags = (gForceSelectSceneTab == i) ? ImGuiTabItemFlags_SetSelected : 0;
                    if (ImGui::BeginTabItem(label, &open, tabFlags))
                    {
                        if (gForceSelectSceneTab == -1)
                        {
                            if (gActiveScene != i) SetActiveScene(i);
                        }
                        else if (gForceSelectSceneTab == i)
                        {
                            if (gActiveScene != i) SetActiveScene(i);
                            gForceSelectSceneTab = -1;
                        }

                        if (ImGui::BeginPopupContextItem("SceneTabContext"))
                        {
                            if (ImGui::MenuItem("Set as default scene"))
                            {
                                if (!gProjectDir.empty() && i >= 0 && i < (int)gOpenScenes.size())
                                {
                                    if (SetProjectDefaultScene(std::filesystem::path(gProjectDir), gOpenScenes[i].path))
                                    {
                                        gViewportToast = "Default scene set: " + gOpenScenes[i].name;
                                    }
                                    else
                                    {
                                        gViewportToast = "Failed to set default scene";
                                    }
                                    gViewportToastUntil = glfwGetTime() + 2.0;
                                }
                            }
                            ImGui::EndPopup();
                        }

                        ImGui::EndTabItem();
                    }
                    if (!open)
                    {
                        // close tab
                        if (gActiveScene == i) gActiveScene = -1;
                        gOpenScenes.erase(gOpenScenes.begin() + i);
                        if (gActiveScene >= (int)gOpenScenes.size()) gActiveScene = (int)gOpenScenes.size() - 1;

                        if (gActiveScene >= 0)
                        {
                            gAudio3DNodes = gOpenScenes[gActiveScene].nodes;
                            gStaticMeshNodes = gOpenScenes[gActiveScene].staticMeshes;
                            gCamera3DNodes = gOpenScenes[gActiveScene].cameras;
                            gNode3DNodes = gOpenScenes[gActiveScene].node3d;
    gNavMesh3DNodes = gOpenScenes[gActiveScene].navMeshes;
                        }
                        else
                        {
                            gAudio3DNodes.clear();
                            gStaticMeshNodes.clear();
                            gCamera3DNodes.clear();
                            gNode3DNodes.clear();
                            gNavMesh3DNodes.clear();
                            gSelectedAudio3D = -1;
                            gSelectedStaticMesh = -1;
                            gSelectedCamera3D = -1;
                            gSelectedNode3D = -1;
                            gSelectedNavMesh3D = -1;
                            gInspectorPinnedAudio3D = -1;
                            gInspectorPinnedStaticMesh = -1;
                            gInspectorPinnedCamera3D = -1;
                            gInspectorPinnedNode3D = -1;
                            gInspectorPinnedNavMesh3D = -1;
                        }
                        break;
                    }
                }
                ImGui::EndTabBar();
            }
            ImGui::PopStyleColor(3);
            ImGui::End();
        }
}
