#include "main_menu.h"

#include "imgui.h"
#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include <GLFW/glfw3.h>

#include "editor/prefs.h"
#include "editor/project.h"
#include "../vmu/vmu_tool.h"
#include "editor/file_dialogs.h"
#include "scene/scene_manager.h"
#include "platform/dreamcast/dc_codegen.h"
#include "nodes/NodeTypes.h"
#include "runtime/script_compile.h"
#include "editor/editor_state.h"
#include "editor/undo.h"
#include "viewport/viewport_transform.h"

extern std::filesystem::path    gAssetsCurrentDir;

// ---------------------------------------------------------------------------
// DrawMainMenus
// ---------------------------------------------------------------------------
void DrawMainMenus(bool& showPreferences, float& uiScale, int& themeMode)
{
        if (ImGui::BeginPopup("FileMenu"))
        {
            if (ImGui::MenuItem("New Project...", "Ctrl+Shift+N"))
            {
                std::string picked = PickFolderDialog("Select project folder");
                if (!picked.empty())
                {
                    CreateNebulaProject(picked);
                }
            }
            if (ImGui::MenuItem("Open Project...", "Ctrl+Shift+O"))
            {
                std::string projFile = PickProjectFileDialog("Open Nebula project");
                if (!projFile.empty())
                {
                    gProjectFile = projFile;
                    gProjectDir = std::filesystem::path(projFile).parent_path().string();
                    gAssetsCurrentDir.clear();

                    gOpenScenes.clear();
                    gActiveScene = -1;
                    gAudio3DNodes.clear();
                    gStaticMeshNodes.clear();
                    gCamera3DNodes.clear();
                    gNode3DNodes.clear();
                    gNode3DNodes.clear();
                    gSelectedAudio3D = -1;
                    gSelectedStaticMesh = -1;
                    gSelectedCamera3D = -1;
                    gTransformMode = Transform_None;
                    gTransforming = false;
                    gAxisLock = 0;
                    gUndoStack.clear();
                    gRedoStack.clear();

                    // Auto-load linked VMU anim and load-on-boot from project
                    {
                        gVmuLoadOnBoot = GetProjectVmuLoadOnBoot(std::filesystem::path(gProjectDir));
                        std::string vmuAnim = GetProjectVmuAnim(std::filesystem::path(gProjectDir));
                        if (!vmuAnim.empty())
                        {
                            std::filesystem::path vmuPath(vmuAnim);
                            if (vmuPath.is_relative()) vmuPath = std::filesystem::path(gProjectDir) / vmuPath;
                            if (std::filesystem::exists(vmuPath) && LoadVmuFrameData(vmuPath))
                            {
                                gVmuLinkedAnimPath = vmuPath.string();
                                gShowVmuTool = true;
                            }
                        }
                    }

                    AddRecentProject(gProjectFile);
                }
            }
            if (ImGui::MenuItem("Close Project"))
            {
                gProjectFile.clear();
                gProjectDir.clear();
                gAssetsCurrentDir.clear();

                gOpenScenes.clear();
                gActiveScene = -1;
                gAudio3DNodes.clear();
                gStaticMeshNodes.clear();
                gCamera3DNodes.clear();
                gSelectedAudio3D = -1;
                gSelectedStaticMesh = -1;
                gSelectedCamera3D = -1;
                gTransformMode = Transform_None;
                gTransforming = false;
                gAxisLock = 0;
                gUndoStack.clear();
                gRedoStack.clear();
            }
            if (ImGui::BeginMenu("Open Recent"))
            {
                if (gRecentProjects.empty())
                {
                    ImGui::MenuItem("(none)", nullptr, false, false);
                }
                else
                {
                    for (const auto& p : gRecentProjects)
                    {
                        if (ImGui::MenuItem(p.c_str()))
                        {
                            gProjectFile = p;
                            gProjectDir = std::filesystem::path(p).parent_path().string();

                            gOpenScenes.clear();
                            gActiveScene = -1;
                            gAudio3DNodes.clear();
                            gStaticMeshNodes.clear();
                            gCamera3DNodes.clear();
                            gNode3DNodes.clear();
                            gSelectedAudio3D = -1;
                            gSelectedStaticMesh = -1;
                            gSelectedCamera3D = -1;
                            gTransformMode = Transform_None;
                            gTransforming = false;
                            gAxisLock = 0;
                            gUndoStack.clear();
                            gRedoStack.clear();

                            // Auto-load linked VMU anim and load-on-boot from project
                            {
                                gVmuLoadOnBoot = GetProjectVmuLoadOnBoot(std::filesystem::path(gProjectDir));
                                std::string vmuAnim = GetProjectVmuAnim(std::filesystem::path(gProjectDir));
                                if (!vmuAnim.empty())
                                {
                                    std::filesystem::path vmuPath(vmuAnim);
                                    if (vmuPath.is_relative()) vmuPath = std::filesystem::path(gProjectDir) / vmuPath;
                                    if (std::filesystem::exists(vmuPath) && LoadVmuFrameData(vmuPath))
                                    {
                                        gVmuLinkedAnimPath = vmuPath.string();
                                        gShowVmuTool = true;
                                    }
                                }
                            }

                            AddRecentProject(gProjectFile);
                        }
                    }
                }
                ImGui::EndMenu();
            }

            // Open File disabled
            if (ImGui::MenuItem("Save", "Ctrl+S")) SaveActiveScene();
            if (ImGui::MenuItem("Save All", "Ctrl+Shift+S")) SaveAllProjectChanges();
            if (!gEnableScriptHotReload)
            {
                if (ImGui::MenuItem("Enable Hot Reloading"))
                {
                    gEnableScriptHotReload = true;
                    gScriptHotReloadKnownMtimes.clear();
                    gScriptHotReloadTrackedProjectDir.clear();
                    gScriptHotReloadNextPollAt = 0.0;
                    gViewportToast = "Script Hot Reload enabled";
                    gViewportToastUntil = glfwGetTime() + 2.5;
                }
            }
            else
            {
                if (ImGui::MenuItem("Reload Scripts Now"))
                {
                    ForceScriptHotReloadNowV1();
                }
                if (ImGui::MenuItem("Disable Hot Reloading"))
                {
                    gEnableScriptHotReload = false;
                    gScriptHotReloadKnownMtimes.clear();
                    gScriptHotReloadTrackedProjectDir.clear();
                    gViewportToast = "Script Hot Reload v1 disabled";
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences...", "Ctrl+," ))
            {
                showPreferences = true;
            }
            ImGui::Separator();
            ImGui::MenuItem("Exit", "Alt+F4");
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("EditMenu"))
        {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) DoUndo();
            if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z")) DoRedo();
            ImGui::Separator();
            ImGui::MenuItem("Cut", "Ctrl+X");
            ImGui::MenuItem("Copy", "Ctrl+C");
            ImGui::MenuItem("Paste", "Ctrl+V");
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("ToolsMenu"))
        {
            if (ImGui::MenuItem("VMU Tool"))
            {
                gShowVmuTool = true;
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("PackageMenu"))
        {
            if (ImGui::MenuItem("Build Dreamcast (NEBULA_DREAMCAST)", "") || gRequestDreamcastGenerate)
            {
                gRequestDreamcastGenerate = false;
                GenerateDreamcastPackage();
            }
            ImGui::EndPopup();
        }

        if (showPreferences)
        {
            ImGui::Begin("Preferences", &showPreferences);
            if (ImGui::SliderFloat("UI Scale", &uiScale, 0.75f, 2.5f, "%.2f"))
            {
                ImGui::GetIO().FontGlobalScale = uiScale;
            }
            const char* themes[] = { "Space", "Slate (6D7F89 -> 2A363D)", "Arctic (75A8B2 -> 162229)", "Classic (7F7E83 -> 5A595C)", "Black" };
            ImGui::Combo("Theme", &themeMode, themes, IM_ARRAYSIZE(themes));
            ImGui::Checkbox("Hide unselected wireframes", &gHideUnselectedWireframes);

            static char dreamSdkBuf[512] = {0};
            static char vcvarsBuf[512] = {0};
            static bool prefPathInit = false;
            if (!prefPathInit)
            {
                strncpy(dreamSdkBuf, gPrefDreamSdkHome.c_str(), sizeof(dreamSdkBuf) - 1);
                strncpy(vcvarsBuf, gPrefVcvarsPath.c_str(), sizeof(vcvarsBuf) - 1);
                prefPathInit = true;
            }
            ImGui::InputText("DreamSDK", dreamSdkBuf, sizeof(dreamSdkBuf));
            ImGui::InputText("MSVC", vcvarsBuf, sizeof(vcvarsBuf));
            gPrefDreamSdkHome = dreamSdkBuf;
            gPrefVcvarsPath = vcvarsBuf;
            while (!gPrefVcvarsPath.empty() && (gPrefVcvarsPath[0] == '=' || gPrefVcvarsPath[0] == ' ' || gPrefVcvarsPath[0] == '\t' || gPrefVcvarsPath[0] == '"'))
                gPrefVcvarsPath.erase(gPrefVcvarsPath.begin());
            while (!gPrefVcvarsPath.empty() && (gPrefVcvarsPath.back() == ' ' || gPrefVcvarsPath.back() == '\t' || gPrefVcvarsPath.back() == '"'))
                gPrefVcvarsPath.pop_back();

            const bool dreamSdkOk = gPrefDreamSdkHome.empty() ? false : std::filesystem::exists(std::filesystem::path(gPrefDreamSdkHome));
            const bool vcvarsOk = !ResolveVcvarsPathFromPreference(gPrefVcvarsPath).empty();
            ImGui::TextColored(dreamSdkOk ? ImVec4(0.55f, 0.95f, 0.55f, 1.0f) : ImVec4(0.95f, 0.55f, 0.55f, 1.0f),
                dreamSdkOk ? "DreamSDK path: OK" : "DreamSDK path: missing/not set");
            ImGui::TextColored(vcvarsOk ? ImVec4(0.55f, 0.95f, 0.55f, 1.0f) : ImVec4(0.95f, 0.55f, 0.55f, 1.0f),
                vcvarsOk ? "VC vars path: OK" : "VC vars path: missing/not set");

            ImGui::Separator();
            if (ImGui::Button("Save"))
            {
                SavePreferences(uiScale, themeMode);
                gViewportToast = std::string("Preferences saved (DreamSDK: ") + (dreamSdkOk ? "OK" : "MISSING") + ", VC vars: " + (vcvarsOk ? "OK" : "MISSING") + ")";
                gViewportToastUntil = glfwGetTime() + 2.5;
            }
            ImGui::End();
        }
}
