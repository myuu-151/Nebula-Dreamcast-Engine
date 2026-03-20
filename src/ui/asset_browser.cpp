#include "asset_browser.h"
#include "editor/project.h"
#include "core/meta_io.h"
#include "core/anim_io.h"
#include "scene/scene_io.h"
#include "nodes/NodeTypes.h"
#include "core/texture_io.h"
#include "mesh_inspector.h"
#include "imgui.h"

#include <cstring>
#include <fstream>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <memory>

#include <GLFW/glfw3.h>

// --- Globals owned by asset_browser ---
std::filesystem::path gRenamePath;
char gRenameBuffer[256] = {};
std::filesystem::path gInlineRenamePath;
char gInlineRenameBuffer[256] = {};
bool gInlineRenameFocus = false;
std::filesystem::path gPendingDelete;
bool gDoDelete = false;
bool gRenameModalOpen = false;
std::filesystem::path gAssetsCurrentDir;
std::filesystem::path gSelectedAssetPath;
double gSelectedAssetPathSetTime = 0.0;

#include "editor/editor_state.h"

extern std::string gImportWarning;

#include "editor/undo.h"

extern void OpenSceneFile(const std::filesystem::path& path);
extern void RefreshOpenSceneTabMetadataForPath(const std::filesystem::path& path);

// ---------------------------------------------------------------------------
// Asset path helpers
// ---------------------------------------------------------------------------

std::filesystem::path MakeUniqueAssetPath(const std::filesystem::path& root, const std::string& baseName, const std::string& ext)
{
    for (int i = 0; i < 1000; ++i)
    {
        std::string suffix = (i == 0) ? "" : std::to_string(i);
        std::filesystem::path p = root / (baseName + suffix + ext);
        if (!std::filesystem::exists(p))
            return p;
    }
    return root / (baseName + ext);
}

void CreateSceneAssetAt(const std::filesystem::path& scenePath)
{
    std::filesystem::create_directories(scenePath.parent_path());
    std::ofstream out(scenePath, std::ios::out | std::ios::trunc);
    if (out.is_open())
    {
        out << "name=" << scenePath.stem().string() << "\n";
    }
}

std::filesystem::path CreateSceneAsset(const std::filesystem::path& assetsRoot)
{
    std::filesystem::create_directories(assetsRoot);
    std::filesystem::path scenePath = MakeUniqueAssetPath(assetsRoot, "NewScene", ".nebscene");
    CreateSceneAssetAt(scenePath);
    return scenePath;
}

void CreateAssetFolderAt(const std::filesystem::path& folder)
{
    std::filesystem::create_directories(folder);
}

std::filesystem::path CreateAssetFolder(const std::filesystem::path& assetsRoot)
{
    std::filesystem::create_directories(assetsRoot);
    std::filesystem::path folder = MakeUniqueAssetPath(assetsRoot, "NewFolder", "");
    CreateAssetFolderAt(folder);
    return folder;
}

// ---------------------------------------------------------------------------
// Asset management
// ---------------------------------------------------------------------------

void DeleteAssetPath(const std::filesystem::path& p)
{
    if (!std::filesystem::exists(p)) return;
    if (std::filesystem::is_directory(p))
        std::filesystem::remove_all(p);
    else
        std::filesystem::remove(p);
}

std::filesystem::path MoveAssetToTrash(const std::filesystem::path& p)
{
    std::error_code ec;
    if (!std::filesystem::exists(p, ec) || ec) return {};

    std::filesystem::path trash = std::filesystem::temp_directory_path(ec) / "nebula_trash";
    if (ec) return {};
    std::filesystem::create_directories(trash, ec);
    if (ec) return {};

    std::filesystem::path base = trash / p.filename();
    std::filesystem::path dst = base;
    for (int i = 0; i < 1000; ++i)
    {
        if (!std::filesystem::exists(dst, ec) || ec) break;
        dst = trash / (base.stem().string() + "_" + std::to_string(i) + base.extension().string());
    }

    ec.clear();
    std::filesystem::rename(p, dst, ec);
    if (!ec) return dst;

    // Fallback when rename fails (e.g., access denied/cross-device): try copy + remove.
    ec.clear();
    if (std::filesystem::is_directory(p, ec) && !ec)
    {
        std::filesystem::copy(p, dst, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) return {};
        std::filesystem::remove_all(p, ec);
        if (ec) return {};
        return dst;
    }

    ec.clear();
    std::filesystem::copy_file(p, dst, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return {};
    std::filesystem::remove(p, ec);
    if (ec) return {};
    return dst;
}

std::filesystem::path RenameAssetPath(const std::filesystem::path& p, const std::string& newName)
{
    if (newName.empty()) return {};
    std::filesystem::path target = p.parent_path() / newName;
    if (p.has_extension())
        target.replace_extension(p.extension());
    std::filesystem::rename(p, target);
    return target;
}

std::filesystem::path DuplicateAssetPath(const std::filesystem::path& p)
{
    if (!std::filesystem::exists(p)) return {};

    std::filesystem::path parent = p.parent_path();
    std::string stem = p.stem().string();
    std::string ext = p.extension().string();

    std::filesystem::path candidate;
    for (int i = 1; i < 1000; ++i)
    {
        std::string suffix = (i == 1) ? "_copy" : ("_copy" + std::to_string(i));
        if (std::filesystem::is_directory(p))
            candidate = parent / (p.filename().string() + suffix);
        else
            candidate = parent / (stem + suffix + ext);

        if (!std::filesystem::exists(candidate))
            break;
    }

    if (candidate.empty()) return {};

    std::error_code ec;
    if (std::filesystem::is_directory(p))
        std::filesystem::copy(p, candidate, std::filesystem::copy_options::recursive, ec);
    else
        std::filesystem::copy_file(p, candidate, std::filesystem::copy_options::overwrite_existing, ec);

    if (ec) return {};
    return candidate;
}

bool RewritePathRefForRename(std::string& ref, const std::string& oldRel, const std::string& newRel, bool isDir)
{
    return NebulaScene::RewritePathRefForRename(ref, oldRel, newRel, isDir);
}

void UpdateAssetReferencesForRename(const std::filesystem::path& oldPath, const std::filesystem::path& newPath)
{
    NebulaScene::UpdateAssetReferencesForRename(
        oldPath,
        newPath,
        gProjectDir,
        gAudio3DNodes,
        gStaticMeshNodes,
        gNode3DNodes,
        gOpenScenes,
        gSelectedAssetPath);

    auto remapPath = [&](std::filesystem::path& p)
    {
        if (p.empty()) return;
        std::string cur = p.generic_string();
        std::string oldS = oldPath.generic_string();
        if (cur == oldS || cur.rfind(oldS + "/", 0) == 0)
        {
            std::string tail = cur.substr(oldS.size());
            p = std::filesystem::path(newPath.generic_string() + tail);
        }
    };

    remapPath(gAssetsCurrentDir);
    remapPath(gMaterialInspectorPath);
    remapPath(gMaterialInspectorPath2);
    remapPath(gNebTexInspectorPath);
    remapPath(gNebTexInspectorPath2);
    remapPath(gNebMeshInspectorState.targetMeshPath);

    if (gInlineRenamePath == oldPath)
        gInlineRenamePath = newPath;
}

void BeginInlineAssetRename(const std::filesystem::path& p, const std::string& displayName)
{
    gInlineRenamePath = p;
    strncpy_s(gInlineRenameBuffer, displayName.c_str(), sizeof(gInlineRenameBuffer) - 1);
    gInlineRenameFocus = true;
}

void CommitInlineAssetRename()
{
    if (gInlineRenamePath.empty()) return;
    std::filesystem::path oldPath = gInlineRenamePath;
    std::string newName = gInlineRenameBuffer;
    gInlineRenamePath.clear();
    gInlineRenameFocus = false;

    if (newName.empty()) return;
    std::filesystem::path newPath = RenameAssetPath(oldPath, newName);
    if (!newPath.empty() && newPath != oldPath)
    {
        PushUndo({"Rename Asset",
            [oldPath, newPath]() { if (std::filesystem::exists(newPath)) std::filesystem::rename(newPath, oldPath); },
            [oldPath, newPath]() { if (std::filesystem::exists(oldPath)) std::filesystem::rename(oldPath, newPath); }
        });
        UpdateAssetReferencesForRename(oldPath, newPath);
        gSelectedAssetPath = newPath;
        gSelectedAssetPathSetTime = ImGui::GetTime();
        gViewportToast = "Renamed " + oldPath.stem().string();
    }
    else
    {
        gViewportToast = "Rename failed";
    }
    gViewportToastUntil = glfwGetTime() + 2.0;
}

// ---------------------------------------------------------------------------
// DrawAssetsBrowser
// ---------------------------------------------------------------------------

void DrawAssetsBrowser(const std::filesystem::path& root)
{
    if (!std::filesystem::exists(root))
    {
        ImGui::Text("(Assets folder missing)");
        return;
    }

    if (gAssetsCurrentDir.empty())
        gAssetsCurrentDir = root;

    // Back button
    if (gAssetsCurrentDir != root)
    {
        if (ImGui::Selectable("[..]"))
        {
            gAssetsCurrentDir = gAssetsCurrentDir.parent_path();
            if (gAssetsCurrentDir.string().find(root.string()) != 0)
                gAssetsCurrentDir = root;
        }
    }

    for (auto& entry : std::filesystem::directory_iterator(gAssetsCurrentDir))
    {
        const auto& p = entry.path();
        if (entry.is_regular_file() && p.extension() == ".meta")
            continue; // internal sidecar files (e.g. nebtex compatibility metadata)
        if (entry.is_regular_file() && p.extension() == ".fbx")
            continue; // hide source interchange files in normal asset browsing
        // .nebslots manifests are visible in Assets for manual inspection/selection.

        std::string name = p.filename().string();
        if (entry.is_regular_file())
        {
            name = p.stem().string();
        }

        std::string itemId = p.string();
        ImGui::PushID(itemId.c_str());
        if (entry.is_directory())
        {
            bool selected = (gSelectedAssetPath == p);
            if (gInlineRenamePath == p)
            {
                if (gInlineRenameFocus)
                {
                    ImGui::SetKeyboardFocusHere();
                    gInlineRenameFocus = false;
                }
                ImGuiInputTextFlags rf = ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue;
                bool submit = ImGui::InputText("##inline_rename", gInlineRenameBuffer, sizeof(gInlineRenameBuffer), rf);
                if (submit || ImGui::IsItemDeactivatedAfterEdit()) CommitInlineAssetRename();
                if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape)) { gInlineRenamePath.clear(); gInlineRenameFocus = false; }
            }
            else if (ImGui::Selectable((name + "/").c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
            {
                const bool isDouble = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                if (!selected)
                {
                    gSelectedAssetPath = p;
                    gSelectedAssetPathSetTime = ImGui::GetTime();
                }

                if (isDouble)
                {
                    gSelectedAssetPath = p;
                    gSelectedAssetPathSetTime = ImGui::GetTime();
                    gAssetsCurrentDir = p;
                    ImGui::PopID();
                    break;
                }
            }
        }
        else if (entry.is_regular_file())
        {
            bool selected = (gSelectedAssetPath == p);
            if (gInlineRenamePath == p)
            {
                if (gInlineRenameFocus)
                {
                    ImGui::SetKeyboardFocusHere();
                    gInlineRenameFocus = false;
                }
                ImGuiInputTextFlags rf = ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue;
                bool submit = ImGui::InputText("##inline_rename", gInlineRenameBuffer, sizeof(gInlineRenameBuffer), rf);
                if (submit || ImGui::IsItemDeactivatedAfterEdit()) CommitInlineAssetRename();
                if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape)) { gInlineRenamePath.clear(); gInlineRenameFocus = false; }
            }
            else
            {
                // Check if this scene is the project default scene (yellow star indicator)
                bool isDefaultScene = false;
                if (p.extension() == ".nebscene" && !gProjectDir.empty())
                {
                    std::string defScene = GetProjectDefaultScene(std::filesystem::path(gProjectDir));
                    if (!defScene.empty())
                    {
                        std::filesystem::path defPath(defScene);
                        if (defPath.is_relative())
                            defPath = std::filesystem::path(gProjectDir) / defPath;
                        std::error_code ec;
                        isDefaultScene = std::filesystem::equivalent(p, defPath, ec);
                    }
                }

                if (isDefaultScene)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.0f, 1.0f));
                    ImGui::Text("*");
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0, 4);
                }

            if (ImGui::Selectable(name.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
            {
                const bool isDouble = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                if (!selected)
                {
                    gSelectedAssetPath = p;
                    gSelectedAssetPathSetTime = ImGui::GetTime();
                }

                if (isDouble)
                {
                    gSelectedAssetPath = p;
                    gSelectedAssetPathSetTime = ImGui::GetTime();
                    if (p.extension() == ".nebscene")
                    {
                        OpenSceneFile(p);
                    }
                    else
                    {
                        bool topOccupied =
                            (gSelectedAudio3D >= 0) || (gSelectedStaticMesh >= 0) || (gSelectedCamera3D >= 0) || (gSelectedNavMesh3D >= 0) ||
                            (gInspectorPinnedAudio3D >= 0) || (gInspectorPinnedStaticMesh >= 0) || (gInspectorPinnedCamera3D >= 0) || (gInspectorPinnedNode3D >= 0) || (gInspectorPinnedNavMesh3D >= 0) ||
                            (gMaterialInspectorOpen && !gMaterialInspectorPath.empty()) ||
                            (gNebTexInspectorOpen && !gNebTexInspectorPath.empty());

                        if (!topOccupied)
                        {
                            // No top inspector active: replace top inspector focus.
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
                            gInspectorSel = -1;

                            gMaterialInspectorOpen = false;
                            gMaterialInspectorPath.clear();
                            gNebTexInspectorOpen = false;
                            gNebTexInspectorPath.clear();

                            if (p.extension() == ".nebmat")
                            {
                                gMaterialInspectorOpen = true;
                                gMaterialInspectorPath = p;
                            }
                            else if (p.extension() == ".nebtex")
                            {
                                gNebTexInspectorOpen = true;
                                gNebTexInspectorPath = p;
                            }
                        }
                        else
                        {
                            // Top already occupied: open asset inspector in bottom half.
                            if (p.extension() == ".nebmat")
                            {
                                gMaterialInspectorOpen2 = true;
                                gMaterialInspectorPath2 = p;
                                gNebTexInspectorOpen2 = false;
                                gNebTexInspectorPath2.clear();
                            }
                            else if (p.extension() == ".nebtex")
                            {
                                gNebTexInspectorOpen2 = true;
                                gNebTexInspectorPath2 = p;
                                gMaterialInspectorOpen2 = false;
                                gMaterialInspectorPath2.clear();
                            }
                        }
                    }
                }
            }
            } // end else (default scene star + selectable)
        }

        if (ImGui::BeginPopupContextItem("AssetItem"))
        {
            if (p.extension() == ".nebscene")
            {
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.086f, 0.082f, 0.086f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.086f, 0.082f, 0.086f, 1.0f));
                if (ImGui::MenuItem("Open Scene"))
                {
                    OpenSceneFile(p);
                }
                ImGui::PopStyleColor(3);

                if (ImGui::MenuItem("Save Scene"))
                {
                    bool canSave = gSaveAllInProgress || (gActiveScene >= 0 && gActiveScene < (int)gOpenScenes.size() && gOpenScenes[gActiveScene].path == p);
                    if (canSave)
                    {
                        NebulaScene::SaveSceneToPath(p, gAudio3DNodes, gStaticMeshNodes, gCamera3DNodes, gNode3DNodes, gNavMesh3DNodes);
                        RefreshOpenSceneTabMetadataForPath(p);
                        gViewportToast = "Saved " + p.stem().string();
                    }
                    else
                    {
                        gViewportToast = "cannot save outside of active scene";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                if (ImGui::MenuItem("Set as default scene"))
                {
                    if (!gProjectDir.empty() && SetProjectDefaultScene(std::filesystem::path(gProjectDir), p))
                    {
                        gViewportToast = "Default scene set: " + p.stem().string();
                    }
                    else
                    {
                        gViewportToast = "Failed to set default scene";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                ImGui::Separator();
            }
            if (ImGui::MenuItem("Rename"))
            {
                BeginInlineAssetRename(p, name);
            }
            if (ImGui::MenuItem("Duplicate"))
            {
                std::filesystem::path dup = DuplicateAssetPath(p);
                if (!dup.empty())
                {
                    gViewportToast = "Duplicated " + p.stem().string();
                    gSelectedAssetPath = dup;
                }
                else
                {
                    gViewportToast = "Duplicate failed";
                }
                gViewportToastUntil = glfwGetTime() + 2.0;
            }
            if (p.extension() == ".nebmat")
            {
                if (ImGui::MenuItem("Inspector"))
                {
                    gMaterialInspectorOpen = true;
                    gMaterialInspectorPath = p;
                }
            }
            if (p.extension() == ".png")
            {
                if (ImGui::MenuItem("Save as .nebtex"))
                {
                    std::filesystem::path texOut = p;
                    texOut.replace_extension(".nebtex");
                    std::string warn;
                    if (ExportNebTexturePNG(p, texOut, warn))
                    {
                        gViewportToast = "Saved " + texOut.filename().string();
                        if (!warn.empty()) gImportWarning = warn;
                    }
                    else
                    {
                        gViewportToast = "Save .nebtex failed";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
            }
            if (p.extension() == ".nebtex")
            {
                if (ImGui::MenuItem("Save .nebtex"))
                {
                    // touch/write-through option for quick explicit save from dropdown
                    std::ifstream in(p, std::ios::binary);
                    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                    if (!bytes.empty())
                    {
                        std::ofstream out(p, std::ios::binary | std::ios::trunc);
                        out.write(bytes.data(), (std::streamsize)bytes.size());
                        gViewportToast = "Saved " + p.filename().string();
                    }
                    else
                    {
                        gViewportToast = "Save .nebtex failed";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                if (ImGui::MenuItem("Generate Material"))
                {
                    std::filesystem::path matPath = p.parent_path() / ("m_" + p.stem().string() + ".nebmat");
                    if (std::filesystem::exists(matPath))
                    {
                        int n = 1;
                        std::filesystem::path base = matPath;
                        while (std::filesystem::exists(matPath))
                        {
                            matPath = base.parent_path() / (base.stem().string() + "_" + std::to_string(n++) + base.extension().string());
                        }
                    }

                    std::string texRef = p.string();
                    if (!gProjectDir.empty())
                    {
                        std::error_code ec;
                        std::filesystem::path rel = std::filesystem::relative(p, std::filesystem::path(gProjectDir), ec);
                        if (!ec) texRef = rel.generic_string();
                    }

                    if (NebulaAssets::SaveMaterialTexture(matPath, texRef))
                    {
                        gViewportToast = "Generated " + matPath.filename().string();
                        gSelectedAssetPath = matPath;
                    }
                    else
                    {
                        gViewportToast = "Generate material failed";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                if (ImGui::MenuItem("Inspector"))
                {
                    gNebTexInspectorOpen = true;
                    gNebTexInspectorPath = p;
                    gMaterialInspectorOpen = false;
                    gMaterialInspectorPath.clear();
                    gSelectedAudio3D = -1;
                    gSelectedStaticMesh = -1;
                    gSelectedCamera3D = -1;
                    gSelectedNode3D = -1;
                    gInspectorPinnedAudio3D = -1;
                    gInspectorPinnedStaticMesh = -1;
                    gInspectorPinnedCamera3D = -1;
                    gInspectorPinnedNode3D = -1;
                    gInspectorPinnedNavMesh3D = -1;
                }
            }
            if (p.extension() == ".nebmesh")
            {
                if (ImGui::MenuItem("Inspector"))
                {
                    OpenNebMeshInspector(p);
                }
            }
            if (p.extension() == ".nebanim")
            {
                if (ImGui::MenuItem("Inspector"))
                {
                    OpenNebAnimInspector(p);
                }
            }
            if (ImGui::MenuItem("Delete"))
            {
                gPendingDelete = p;
                gDoDelete = true;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    if (!gInlineRenamePath.empty() &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsAnyItemHovered())
    {
        gInlineRenamePath.clear();
        gInlineRenameFocus = false;
    }

    if (gDoDelete && !gPendingDelete.empty())
    {
        std::filesystem::path original = gPendingDelete;
        auto trashPath = std::make_shared<std::filesystem::path>(MoveAssetToTrash(original));
        if (!trashPath->empty())
        {
            PushUndo({"Delete Asset",
                [original, trashPath]() {
                    std::error_code ec;
                    if (!trashPath->empty() && std::filesystem::exists(*trashPath, ec) && !ec)
                        std::filesystem::rename(*trashPath, original, ec);
                },
                [original, trashPath]() {
                    std::error_code ec;
                    if (std::filesystem::exists(original, ec) && !ec)
                        *trashPath = MoveAssetToTrash(original);
                }
            });
        }
        else
        {
            gViewportToast = "Delete failed (file/folder in use or access denied)";
            gViewportToastUntil = glfwGetTime() + 2.0;
        }
        gPendingDelete.clear();
        gDoDelete = false;
    }
}
