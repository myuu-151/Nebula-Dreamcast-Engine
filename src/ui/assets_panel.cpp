#include "assets_panel.h"
#include "asset_browser.h"
#include "import_pipeline.h"
#include "../editor/editor_state.h"
#include "../editor/undo.h"
#include "../editor/file_dialogs.h"
#include "../editor/project.h"
#include "../io/anim_io.h"
#include "../io/mesh_io.h"
#include "../scene/scene_manager.h"

#include "imgui.h"
#include <GLFW/glfw3.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cmath>

// Import state (previously in main.cpp, extern'd by mesh_inspector.cpp)
std::string gImportPath;
Assimp::Importer gImportAssimp;
const aiScene* gImportScene = nullptr;
std::vector<bool> gImportAnimConvert;
std::string gImportBaseNebMeshPath;
int gImportBasisMode = 1;

void DrawAssetsPanel(ImGuiViewport* vp, float topBarH, float leftPanelWidth, float assetsHeight)
{
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + topBarH + (vp->Size.y - topBarH - assetsHeight)));
    ImGui::SetNextWindowSize(ImVec2(leftPanelWidth, assetsHeight));
    ImGui::Begin("Assets", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);
    ImGui::Text("Assets");
    ImGui::Separator();

    if (!gProjectDir.empty())
    {
        std::filesystem::path assetsRoot = std::filesystem::path(gProjectDir) / "Assets";

        if (ImGui::BeginPopupContextWindow("AssetsContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            std::filesystem::path targetDir = gAssetsCurrentDir.empty() ? assetsRoot : gAssetsCurrentDir;

            if (ImGui::BeginMenu("Create Asset"))
            {
                if (ImGui::MenuItem("Scene"))
                {
                    std::filesystem::path created = CreateSceneAsset(targetDir);
                    PushUndo({"Create Scene",
                        [created]() { DeleteAssetPath(created); },
                        [created]() { CreateSceneAssetAt(created); }
                    });
                }
                if (ImGui::MenuItem("Material"))
                {
                    std::filesystem::path created = CreateMaterialAsset(targetDir);
                    PushUndo({"Create Material",
                        [created]() { DeleteAssetPath(created); },
                        [created]() { CreateMaterialAsset(created.parent_path()); }
                    });
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Import Asset"))
            {
                auto pickedList = PickImportAssetDialog("Import Asset");
                ImportAssetsToCurrentFolder(pickedList);
            }
            if (ImGui::MenuItem("Convert Asset"))
            {
                gImportBaseNebMeshPath.clear();
                std::string picked = PickFbxFileDialog("Convert FBX");
                if (!picked.empty())
                {
                    gImportPath = picked;
                    gImportScene = gImportAssimp.ReadFile(gImportPath,
                        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);
                    gImportAnimConvert.clear();
                    if (gImportScene && gImportScene->mNumAnimations > 0)
                        gImportAnimConvert.resize(gImportScene->mNumAnimations, false);
                    gImportPopupOpen = true;
                }
            }
            if (ImGui::MenuItem("New Folder"))
            {
                std::filesystem::path created = CreateAssetFolder(targetDir);
                PushUndo({"Create Folder",
                    [created]() { DeleteAssetPath(created); },
                    [created]() { CreateAssetFolderAt(created); }
                });
            }
            ImGui::EndPopup();
        }

        DrawAssetsBrowser(assetsRoot);

        if (gRenameModalOpen)
        {
            ImGui::OpenPopup("RenameItem");
        }
        if (ImGui::BeginPopupModal("RenameItem", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("New name", gRenameBuffer, sizeof(gRenameBuffer));
            if (ImGui::Button("OK"))
            {
                if (!gRenamePath.empty())
                {
                    std::filesystem::path oldPath = gRenamePath;
                    std::filesystem::path newPath = RenameAssetPath(gRenamePath, gRenameBuffer);
                    if (!newPath.empty())
                    {
                        PushUndo({"Rename Asset",
                            [oldPath, newPath]() { if (std::filesystem::exists(newPath)) std::filesystem::rename(newPath, oldPath); },
                            [oldPath, newPath]() { if (std::filesystem::exists(oldPath)) std::filesystem::rename(oldPath, newPath); }
                        });

                        UpdateAssetReferencesForRename(oldPath, newPath);

                        if (oldPath.extension() == ".nebscene" && newPath.extension() == ".nebscene")
                        {
                            for (auto& s : gOpenScenes)
                            {
                                if (s.path == oldPath)
                                {
                                    s.path = newPath;
                                    s.name = newPath.stem().string();
                                }
                            }
                        }
                    }
                    gRenamePath.clear();
                }
                gRenameModalOpen = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                gRenameModalOpen = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (gImportPopupOpen)
        {
            ImGui::OpenPopup("Convert Asset");
        }
        if (ImGui::BeginPopupModal("Convert Asset", &gImportPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("FBX: %s", gImportPath.c_str());
            if (!gImportScene)
            {
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Failed to load FBX.");
            }
            else
            {
                if (gImportScene->mNumAnimations == 0)
                {
                    ImGui::Text("(no animations)");
                }
                else if (ImGui::TreeNode("Animations"))
                {
                    for (unsigned int i = 0; i < gImportScene->mNumAnimations; ++i)
                    {
                        const aiAnimation* anim = gImportScene->mAnimations[i];
                        std::string animName = anim->mName.length > 0 ? anim->mName.C_Str() : ("Anim" + std::to_string(i + 1));
                        bool checked = gImportAnimConvert[i];
                        if (ImGui::Checkbox(animName.c_str(), &checked))
                            gImportAnimConvert[i] = checked;
                    }
                    ImGui::TreePop();
                }
                ImGui::Checkbox("Delta compress (store frame deltas)", &gImportDeltaCompress);
                ImGui::Checkbox("Duplicate sample rate (2x)", &gImportDoubleSampleRate);
                ImGui::Checkbox("Include animation provenance mapping", &gImportUseProvenanceMapping);
                if (!gImportBaseNebMeshPath.empty())
                    ImGui::Text("Target mesh: %s", gImportBaseNebMeshPath.c_str());
                ImGui::Text("Export: .nebanim BE, 16.16 fixed, %.0f fps (pos only)", gImportDoubleSampleRate ? 24.0f : 12.0f);
                ImGui::Text("Mesh import: .nebmesh BE, 8.8 fixed, indexed (UV0 if present)");
                if (gImportDeltaCompress)
                    ImGui::Text("Delta: int16 8.8 per component (smaller)");
                if (!gImportWarning.empty())
                    ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1), "%s", gImportWarning.c_str());
            }

            if (ImGui::Button("Convert Animations"))
            {
                int exported = 0;
                gImportWarning.clear();
                if (gImportScene && gImportScene->mNumAnimations > 0)
                {
                    std::filesystem::path inPath(gImportPath);
                    std::string base = inPath.stem().string();
                    std::filesystem::path dir = inPath.parent_path();

                    std::filesystem::path animDir = dir / "anim";

                    if (!gImportBaseNebMeshPath.empty())
                    {
                        std::filesystem::path nebPath = std::filesystem::path(gImportBaseNebMeshPath);
                        if (nebPath.is_relative())
                        {
                            if (!gProjectDir.empty()) nebPath = std::filesystem::path(gProjectDir) / nebPath;
                            else nebPath = dir / nebPath;
                        }
                        std::filesystem::path meshDir = nebPath.parent_path();
                        if (!meshDir.empty())
                        {
                            std::string leaf = meshDir.filename().string();
                            std::transform(leaf.begin(), leaf.end(), leaf.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                            if (leaf == "mesh")
                                animDir = meshDir.parent_path() / "anim";
                            else
                                animDir = meshDir / "anim";
                        }
                    }

                    std::error_code animEc;
                    std::filesystem::create_directories(animDir, animEc);
                    if (!std::filesystem::exists(animDir))
                    {
                        if (!gImportWarning.empty()) gImportWarning += " | ";
                        gImportWarning += "Failed to create anim/ folder; exporting beside source";
                        animDir = dir;
                    }

                    uint32_t targetNebMeshVerts = 0;
                    NebMesh targetNebMesh;
                    if (!gImportBaseNebMeshPath.empty())
                    {
                        std::filesystem::path nebPath = std::filesystem::path(gImportBaseNebMeshPath);
                        if (nebPath.is_relative())
                        {
                            if (!gProjectDir.empty()) nebPath = std::filesystem::path(gProjectDir) / nebPath;
                            else nebPath = dir / nebPath;
                        }
                        if (LoadNebMesh(nebPath, targetNebMesh) && targetNebMesh.valid)
                        {
                            targetNebMeshVerts = (uint32_t)targetNebMesh.positions.size();
                        }
                        else
                        {
                            if (!gImportWarning.empty()) gImportWarning += " | ";
                            gImportWarning += "Target .nebmesh load failed";
                        }
                    }

                    for (unsigned int i = 0; i < gImportScene->mNumAnimations; ++i)
                    {
                        if (!gImportAnimConvert[i]) continue;
                        const aiAnimation* anim = gImportScene->mAnimations[i];
                        std::string animName = anim->mName.length > 0 ? anim->mName.C_Str() : ("Anim" + std::to_string(i + 1));
                        animName = SanitizeName(animName);

                        std::vector<unsigned int> meshIndices;
                        uint32_t mergedVerts = 0;
                        for (unsigned int mi = 0; mi < gImportScene->mNumMeshes; ++mi)
                        {
                            const aiMesh* m = gImportScene->mMeshes[mi];
                            if (!m) continue;
                            meshIndices.push_back(mi);
                            mergedVerts += (uint32_t)m->mNumVertices;
                        }

                        if (meshIndices.empty())
                        {
                            if (!gImportWarning.empty()) gImportWarning += " | ";
                            gImportWarning += animName + ": no valid FBX mesh found";
                            continue;
                        }

                        std::filesystem::path outPath = animDir / (base + "_" + animName + ".nebanim");
                        std::string warn;
                        if (ExportNebAnimation(gImportScene, anim, meshIndices, outPath, warn,
                            gImportDeltaCompress, targetNebMeshVerts,
                            targetNebMesh.valid ? &targetNebMesh : nullptr, nullptr,
                            gImportDoubleSampleRate ? 2.0f : 1.0f, false))
                        {
                            exported++;
                            if (!warn.empty())
                            {
                                if (!gImportWarning.empty()) gImportWarning += " | ";
                                gImportWarning += animName + ": " + warn;
                            }
                        }
                    }
                }
                gViewportToast = exported > 0 ? ("Exported " + std::to_string(exported) + " .nebanim") : "No animations exported";
                gViewportToastUntil = glfwGetTime() + 2.0;
                gImportPopupOpen = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                gImportPopupOpen = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
    else
    {
        ImGui::Text("(no project selected)");
    }

    ImGui::End();
}
