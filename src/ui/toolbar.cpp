#include "toolbar.h"
#include "main_menu.h"
#include "../editor/editor_state.h"
#include "../editor/viewport_nav.h"
#include "../editor/project.h"
#include "../runtime/script_compile.h"
#include "../scene/scene_manager.h"

#include "imgui.h"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <string>

static bool gQuitConfirmOpen = false;
static bool draggingWindow = false;

float DrawToolbar(GLFWwindow* window, EditorViewportNav& nav,
                  unsigned int uiIconTex, bool& showPreferences,
                  float& uiScale, int& themeMode)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float barH = 28.0f * ImGui::GetIO().FontGlobalScale;
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, barH));
    ImGui::Begin("##TopBar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.086f, 0.082f, 0.086f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.086f, 0.082f, 0.086f, 1.0f));

    if (uiIconTex != 0)
    {
        float iconSize = ImGui::GetFontSize() + 8.0f;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
        ImGui::Image((ImTextureID)(intptr_t)uiIconTex, ImVec2(iconSize, iconSize));
        ImGui::SameLine(0.0f, 6.0f);
    }
    float baseY = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(baseY + 4.0f);
    if (ImGui::Button("File"))
        ImGui::OpenPopup("FileMenu");
    ImGui::SameLine();
    ImGui::SetCursorPosY(baseY + 4.0f);
    if (ImGui::Button("Edit"))
        ImGui::OpenPopup("EditMenu");
    ImGui::SameLine();
    ImGui::SetCursorPosY(baseY + 4.0f);
    if (ImGui::Button("VMU"))
        ImGui::OpenPopup("ToolsMenu");
    ImGui::SameLine();
    ImGui::SetCursorPosY(baseY + 4.0f);
    if (ImGui::Button("Package"))
        ImGui::OpenPopup("PackageMenu");
    ImGui::SameLine();
    ImGui::SetCursorPosY(baseY + 4.0f);
    if (ImGui::Button("Play"))
    {
        if (!gPlayMode)
        {
            nav.Snapshot();
            SnapshotPlaySceneState();
            gPlayOriginalScenes = gOpenScenes;
            gPlayMode = true;

            // Switch to default scene (matches DC boot behavior).
            if (!gProjectDir.empty())
            {
                std::string defCfg = GetProjectDefaultScene(std::filesystem::path(gProjectDir));
                if (!defCfg.empty())
                {
                    std::filesystem::path defPath(defCfg);
                    if (defPath.is_relative())
                        defPath = std::filesystem::path(gProjectDir) / defPath;
                    std::error_code ec;
                    auto defCanon = std::filesystem::weakly_canonical(defPath, ec);
                    for (int i = 0; i < (int)gOpenScenes.size(); ++i)
                    {
                        auto scnCanon = std::filesystem::weakly_canonical(gOpenScenes[i].path, ec);
                        if (scnCanon == defCanon)
                        {
                            SetActiveScene(i);
                            break;
                        }
                    }
                }
            }

            if (!BeginPlayScriptRuntime())
            {
                // MSVC not available — revert play mode entirely
                gPlayMode = false;
                gPlayOriginalScenes.clear();
                RestorePlaySceneState();
            }
        }
        else
        {
            gPlayMode = false;
            gPlayOriginalScenes.clear();
            EndPlayScriptRuntime();
            nav.Restore();
            RestorePlaySceneState();
        }
    }
    ImGui::SameLine();
    ImGui::SetCursorPosY(baseY + 4.0f);
    ImGui::Checkbox("Wireframe", &gWireframePreview);
    ImGui::SetCursorPosY(baseY + 4.0f);
    ImGui::PopStyleColor(3);

    // Window controls (right side)
    float barW = ImGui::GetWindowWidth();
    float btnW = ImGui::GetFontSize() * 1.6f;
    float btnH = ImGui::GetFrameHeight();
    float pad = ImGui::GetStyle().FramePadding.x;
    ImVec2 btnPos(barW - (btnW * 3.0f + pad * 2.0f) - 12.0f, baseY + 0.0f);
    ImGui::SetCursorPos(btnPos);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.086f, 0.082f, 0.086f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
    if (ImGui::Button("_", ImVec2(btnW, btnH))) glfwIconifyWindow(window);
    ImGui::SameLine();
    if (ImGui::Button("[]", ImVec2(btnW, btnH)))
    {
        if (glfwGetWindowAttrib(window, GLFW_MAXIMIZED)) glfwRestoreWindow(window);
        else glfwMaximizeWindow(window);
    }
    ImGui::SameLine();
    if (ImGui::Button("X", ImVec2(btnW, btnH)))
    {
        if (gShowVmuTool)
        {
            gShowVmuTool = false;
        }
        else
        {
            if (HasUnsavedProjectChanges()) gQuitConfirmOpen = true;
            else glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }
    ImGui::PopStyleColor(3);

    if (gQuitConfirmOpen)
        ImGui::OpenPopup("ConfirmQuit");
    if (ImGui::BeginPopupModal("ConfirmQuit", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Unsaved changes detected.");
        ImGui::Text("Save All and Exit?");
        if (ImGui::Button("Save All + Exit"))
        {
            SaveAllProjectChanges();
            gQuitConfirmOpen = false;
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            gQuitConfirmOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Drag window by top bar (empty area)
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
    {
        if (ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered())
            draggingWindow = true;
    }
    if (draggingWindow)
    {
        if (ImGui::IsMouseDown(0))
        {
            int wx, wy;
            glfwGetWindowPos(window, &wx, &wy);
            glfwSetWindowPos(window, wx + (int)io.MouseDelta.x, wy + (int)io.MouseDelta.y);
        }
        else
        {
            draggingWindow = false;
        }
    }

    DrawMainMenus(showPreferences, uiScale, themeMode);

    ImGui::End();

    return barH;
}
