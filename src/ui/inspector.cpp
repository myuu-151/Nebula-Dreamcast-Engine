#include "inspector.h"

#include "imgui.h"
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <cmath>
#include <algorithm>

#include <cstring>
#include <cstdint>
#define NOMINMAX
#include <Windows.h>
#include <GL/gl.h>
#include <GLFW/glfw3.h>

#include "math/math_types.h"
#include "editor/project.h"
#include "ui/asset_browser.h"
#include "ui/mesh_inspector.h"
#include "nodes/NodeTypes.h"
#include "nodes/Audio3DNode.h"
#include "io/meta_io.h"
#include "io/mesh_io.h"
#include "io/anim_io.h"
#include "math/math_utils.h"
#include "editor/editor_state.h"
#include "viewport/viewport_transform.h"
#include "runtime/script_compile.h"

// Functions defined in main.cpp
void GetCamera3DWorldTR(int idx, float& ox, float& oy, float& oz, float& orx, float& ory, float& orz);
void ResetStaticMeshTransformsKeepWorld(int idx);
GLuint GetNebTexture(const std::filesystem::path& path);

// TransformMode enum now in viewport/viewport_transform.h

void DrawInspectorPanel(const ImGuiViewport* vp, float topBarH, float rightPanelWidth)
{
        // Right panel: Inspector (selection is pinned until closed with X)
        {
            bool topHasAssetInspector =
                (gMaterialInspectorOpen && !gMaterialInspectorPath.empty()) ||
                (gNebTexInspectorOpen && !gNebTexInspectorPath.empty());
            bool hasNodeSelection =
                (gSelectedAudio3D >= 0 && gSelectedAudio3D < (int)gAudio3DNodes.size()) ||
                (gSelectedStaticMesh >= 0 && gSelectedStaticMesh < (int)gStaticMeshNodes.size()) ||
                (gSelectedCamera3D >= 0 && gSelectedCamera3D < (int)gCamera3DNodes.size()) ||
                (gSelectedNode3D >= 0 && gSelectedNode3D < (int)gNode3DNodes.size());

            // If an asset inspector is occupying top and user selects a node, stack asset inspector to bottom.
            if (topHasAssetInspector && hasNodeSelection)
            {
                if (gMaterialInspectorOpen && !gMaterialInspectorPath.empty())
                {
                    gMaterialInspectorOpen2 = true;
                    gMaterialInspectorPath2 = gMaterialInspectorPath;
                    gNebTexInspectorOpen2 = false;
                    gNebTexInspectorPath2.clear();
                }
                else if (gNebTexInspectorOpen && !gNebTexInspectorPath.empty())
                {
                    gNebTexInspectorOpen2 = true;
                    gNebTexInspectorPath2 = gNebTexInspectorPath;
                    gMaterialInspectorOpen2 = false;
                    gMaterialInspectorPath2.clear();
                }

                gMaterialInspectorOpen = false;
                gMaterialInspectorPath.clear();
                gNebTexInspectorOpen = false;
                gNebTexInspectorPath.clear();
            }
        }

        if (gSelectedAudio3D >= 0 && gSelectedAudio3D < (int)gAudio3DNodes.size())
        {
            gInspectorPinnedAudio3D = gSelectedAudio3D;
            gInspectorPinnedStaticMesh = -1;
            gInspectorPinnedCamera3D = -1;
            gInspectorPinnedNode3D = -1;
            gInspectorPinnedNavMesh3D = -1;
        }
        else if (gSelectedStaticMesh >= 0 && gSelectedStaticMesh < (int)gStaticMeshNodes.size())
        {
            gInspectorPinnedStaticMesh = gSelectedStaticMesh;
            gInspectorPinnedAudio3D = -1;
            gInspectorPinnedCamera3D = -1;
            gInspectorPinnedNode3D = -1;
            gInspectorPinnedNavMesh3D = -1;
        }
        else if (gSelectedCamera3D >= 0 && gSelectedCamera3D < (int)gCamera3DNodes.size())
        {
            gInspectorPinnedCamera3D = gSelectedCamera3D;
            gInspectorPinnedAudio3D = -1;
            gInspectorPinnedStaticMesh = -1;
            gInspectorPinnedNode3D = -1;
            gInspectorPinnedNavMesh3D = -1;
        }
        else if (gSelectedNode3D >= 0 && gSelectedNode3D < (int)gNode3DNodes.size())
        {
            gInspectorPinnedNode3D = gSelectedNode3D;
            gInspectorPinnedAudio3D = -1;
            gInspectorPinnedStaticMesh = -1;
            gInspectorPinnedCamera3D = -1;
            gInspectorPinnedNavMesh3D = -1;
        }
        else if (gSelectedNavMesh3D >= 0 && gSelectedNavMesh3D < (int)gNavMesh3DNodes.size())
        {
            gInspectorPinnedNavMesh3D = gSelectedNavMesh3D;
            gInspectorPinnedAudio3D = -1;
            gInspectorPinnedStaticMesh = -1;
            gInspectorPinnedCamera3D = -1;
            gInspectorPinnedNode3D = -1;
        }

        if (gInspectorPinnedAudio3D >= (int)gAudio3DNodes.size()) gInspectorPinnedAudio3D = -1;
        if (gInspectorPinnedStaticMesh >= (int)gStaticMeshNodes.size()) gInspectorPinnedStaticMesh = -1;
        if (gInspectorPinnedCamera3D >= (int)gCamera3DNodes.size()) gInspectorPinnedCamera3D = -1;
        if (gInspectorPinnedNode3D >= (int)gNode3DNodes.size()) gInspectorPinnedNode3D = -1;
        if (gInspectorPinnedNavMesh3D >= (int)gNavMesh3DNodes.size()) gInspectorPinnedNavMesh3D = -1;

        int inspectAudio = (gSelectedAudio3D >= 0 && gSelectedAudio3D < (int)gAudio3DNodes.size()) ? gSelectedAudio3D : gInspectorPinnedAudio3D;
        int inspectStatic = (gSelectedStaticMesh >= 0 && gSelectedStaticMesh < (int)gStaticMeshNodes.size()) ? gSelectedStaticMesh : gInspectorPinnedStaticMesh;
        int inspectCamera = (gSelectedCamera3D >= 0 && gSelectedCamera3D < (int)gCamera3DNodes.size()) ? gSelectedCamera3D : gInspectorPinnedCamera3D;
        int inspectNode3D = (gSelectedNode3D >= 0 && gSelectedNode3D < (int)gNode3DNodes.size()) ? gSelectedNode3D : gInspectorPinnedNode3D;
        int inspectNavMesh3D = (gSelectedNavMesh3D >= 0 && gSelectedNavMesh3D < (int)gNavMesh3DNodes.size()) ? gSelectedNavMesh3D : gInspectorPinnedNavMesh3D;

        if ((inspectAudio >= 0 && inspectAudio < (int)gAudio3DNodes.size()) ||
            (inspectStatic >= 0 && inspectStatic < (int)gStaticMeshNodes.size()) ||
            (inspectCamera >= 0 && inspectCamera < (int)gCamera3DNodes.size()) ||
            (inspectNode3D >= 0 && inspectNode3D < (int)gNode3DNodes.size()) ||
            (inspectNavMesh3D >= 0 && inspectNavMesh3D < (int)gNavMesh3DNodes.size()) ||
            (gMaterialInspectorOpen && !gMaterialInspectorPath.empty()) ||
            (gNebTexInspectorOpen && !gNebTexInspectorPath.empty()) ||
            (gMaterialInspectorOpen2 && !gMaterialInspectorPath2.empty()) ||
            (gNebTexInspectorOpen2 && !gNebTexInspectorPath2.empty()))
        {
            float inspectorFullH = vp->Size.y - topBarH;
            bool hasBottomInspector =
                (gMaterialInspectorOpen2 && !gMaterialInspectorPath2.empty()) ||
                (gNebTexInspectorOpen2 && !gNebTexInspectorPath2.empty());
            float inspectorHalfH = inspectorFullH * 0.5f;
            float inspectorTopH = hasBottomInspector ? inspectorHalfH : inspectorFullH;

            ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - rightPanelWidth, vp->Pos.y + topBarH));
            ImGui::SetNextWindowSize(ImVec2(rightPanelWidth, inspectorTopH));
            ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

            ImGui::Text("Inspector");
            ImGui::SameLine();
            float closeX = ImGui::GetWindowWidth() - ImGui::GetStyle().FramePadding.x - 16.0f;
            ImGui::SetCursorPosX(closeX);
            if (ImGui::Button("x"))
            {
                gMaterialInspectorOpen = false;
                gMaterialInspectorPath.clear();
                gNebTexInspectorOpen = false;
                gNebTexInspectorPath.clear();
                gMaterialInspectorOpen2 = false;
                gMaterialInspectorPath2.clear();
                gNebTexInspectorOpen2 = false;
                gNebTexInspectorPath2.clear();
                gSelectedAudio3D = -1;
                gSelectedStaticMesh = -1;
                gSelectedCamera3D = -1;
                gSelectedNode3D = -1;
                gSelectedNavMesh3D = -1;
                gSelectedAssetPath.clear();
                gInspectorPinnedAudio3D = -1;
                gInspectorPinnedStaticMesh = -1;
                gInspectorPinnedCamera3D = -1;
                gInspectorPinnedNode3D = -1;
                gInspectorPinnedNavMesh3D = -1;
            }
            ImGui::Separator();
            ImGui::BeginChild("##InspectorTopScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);

            if (inspectAudio >= 0 && inspectAudio < (int)gAudio3DNodes.size())
            {
                auto& n = gAudio3DNodes[inspectAudio];
                const int inspectorId = inspectAudio;
                const bool inspectorChanged = (gInspectorSel != inspectorId);
                if (inspectorChanged)
                {
                    gInspectorSel = inspectorId;
                    strncpy_s(gInspectorName, n.name.c_str(), sizeof(gInspectorName) - 1);
                }

                // Live preview values for rotate
                float displayRotX = n.rotX;
                float displayRotY = n.rotY;
                float displayRotZ = n.rotZ;
                if (gTransformMode == Transform_Rotate && gHasRotatePreview && gRotatePreviewIndex == inspectAudio)
                {
                    displayRotX = gRotatePreviewX;
                    displayRotY = gRotatePreviewY;
                    displayRotZ = gRotatePreviewZ;
                }

                ImGui::Text("Audio3D Node");
                if (ImGui::InputText("Name", gInspectorName, sizeof(gInspectorName)))
                {
                    n.name = gInspectorName;
                }
                static char scriptBuf[256] = {};
                if (inspectorChanged)
                {
                    strncpy_s(scriptBuf, n.script.c_str(), sizeof(scriptBuf) - 1);
                }
                if (ImGui::InputText("Script", scriptBuf, sizeof(scriptBuf)))
                {
                    std::string s = scriptBuf;
                    if (!s.empty() && s.rfind("Scripts/", 0) != 0 && s.rfind("Scripts\\", 0) != 0)
                        s = "Scripts/" + s;
                    n.script = s;
                }
                bool scriptOk = false;
                if (!n.script.empty() && !gProjectDir.empty())
                {
                    std::filesystem::path sp = std::filesystem::path(gProjectDir) / n.script;
                    scriptOk = std::filesystem::exists(sp);
                }
                ImGui::SameLine();
                ImGui::Text("Script");
                if (ImGui::Button("Load Script"))
                {
                    if (scriptOk)
                        gViewportToast = n.script + " script validated";
                    else
                        gViewportToast = (n.script.empty() ? "(none)" : n.script) + " script unvalidated";
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                if (ImGui::Button("Deinit Script##Audio"))
                {
                    n.script.clear();
                    scriptBuf[0] = '\0';
                    if (!gProjectDir.empty())
                    {
                        std::filesystem::path outDir = std::filesystem::path(gProjectDir) / "Intermediate" / "EditorScript";
                        std::error_code ec;
                        if (std::filesystem::exists(outDir, ec))
                        {
                            for (auto& e : std::filesystem::directory_iterator(outDir, ec))
                            {
                                if (e.is_regular_file() && e.path().extension() == ".dll")
                                    std::filesystem::remove(e.path(), ec);
                            }
                        }
                    }
                    UnloadEditorScriptRuntime();
                    gViewportToast = "Script hook removed";
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                ImGui::DragFloat3("Position", &n.x, 0.1f);
                float rotArr[3] = { displayRotX, displayRotY, displayRotZ };
                ImGui::Text("Rotation"); ImGui::SameLine();
                ImGui::SetNextItemWidth(72); bool rxCh = ImGui::DragFloat("X##AudioRotX", &rotArr[0], 0.5f);
                ImGui::SameLine(); ImGui::SetNextItemWidth(72); bool ryCh = ImGui::DragFloat("Y##AudioRotY", &rotArr[1], 0.5f);
                ImGui::SameLine(); ImGui::SetNextItemWidth(72); bool rzCh = ImGui::DragFloat("Z##AudioRotZ", &rotArr[2], 0.5f);
                if (rxCh || ryCh || rzCh)
                {
                    displayRotX = rotArr[0];
                    displayRotY = rotArr[1];
                    displayRotZ = rotArr[2];
                }
                ImGui::DragFloat3("Scale", &n.scaleX, 0.01f, 0.01f, 100.0f);

                // If user edited rotation in inspector, apply back to node
                if (displayRotX != n.rotX || displayRotY != n.rotY || displayRotZ != n.rotZ)
                {
                    n.rotX = displayRotX;
                    n.rotY = displayRotY;
                    n.rotZ = displayRotZ;
                }
                ImGui::DragFloat("Inner Radius", &n.innerRadius, 0.1f, 0.1f, 1000.0f);
                ImGui::DragFloat("Outer Radius", &n.outerRadius, 0.1f, 0.1f, 2000.0f);
                ImGui::DragFloat("Base Volume", &n.baseVolume, 0.01f, 0.0f, 1.0f);
                ImGui::Text("Pan: %.2f  Volume: %.2f", n.pan, n.volume);
            }
            else if (inspectStatic >= 0 && inspectStatic < (int)gStaticMeshNodes.size())
            {
                auto& n = gStaticMeshNodes[inspectStatic];
                const int inspectorId = 10000 + inspectStatic;
                const bool inspectorChanged = (gInspectorSel != inspectorId);
                if (inspectorChanged)
                {
                    gInspectorSel = inspectorId;
                    strncpy_s(gInspectorName, n.name.c_str(), sizeof(gInspectorName) - 1);
                }

                ImGui::Text("StaticMesh3D Node");
                if (ImGui::InputText("Name", gInspectorName, sizeof(gInspectorName)))
                {
                    n.name = gInspectorName;
                }
                ImGui::Spacing();

                static char scriptBuf[256] = {};
                if (inspectorChanged)
                {
                    strncpy_s(scriptBuf, n.script.c_str(), sizeof(scriptBuf) - 1);
                }
                if (!ImGui::IsAnyItemActive() && n.script != scriptBuf)
                    strncpy_s(scriptBuf, n.script.c_str(), sizeof(scriptBuf) - 1);
                if (ImGui::InputText("##StaticScriptPath", scriptBuf, sizeof(scriptBuf)))
                {
                    std::string s = scriptBuf;
                    if (!s.empty() && s.rfind("Scripts/", 0) != 0 && s.rfind("Scripts\\", 0) != 0)
                        s = "Scripts/" + s;
                    n.script = s;
                }
                bool scriptOk = false;
                if (!n.script.empty() && !gProjectDir.empty())
                {
                    std::filesystem::path sp = std::filesystem::path(gProjectDir) / n.script;
                    scriptOk = std::filesystem::exists(sp);
                }
                ImGui::SameLine();
                if (ImGui::Button("Load Script"))
                {
                    if (scriptOk)
                        gViewportToast = n.script + " script validated";
                    else
                        gViewportToast = (n.script.empty() ? "(none)" : n.script) + " script unvalidated";
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                if (ImGui::Button("Deinit Script##Static"))
                {
                    n.script.clear();
                    scriptBuf[0] = '\0';
                    if (!gProjectDir.empty())
                    {
                        std::filesystem::path outDir = std::filesystem::path(gProjectDir) / "Intermediate" / "EditorScript";
                        std::error_code ec;
                        if (std::filesystem::exists(outDir, ec))
                        {
                            for (auto& e : std::filesystem::directory_iterator(outDir, ec))
                            {
                                if (e.is_regular_file() && e.path().extension() == ".dll")
                                    std::filesystem::remove(e.path(), ec);
                            }
                        }
                    }
                    UnloadEditorScriptRuntime();
                    gViewportToast = "Script hook removed";
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }

                static char matBuf[kStaticMeshMaterialSlots][256] = {};
                static char nebslotBuf[256] = {};
                if (inspectorChanged)
                {
                    nebslotBuf[0] = '\0';
                }

                ImGui::Spacing();
                ImGui::Text("Load Nebslot");
                if (ImGui::Button(">##NebslotLink"))
                {
                    std::filesystem::path slotPath;

                    if (!gSelectedAssetPath.empty() && gSelectedAssetPath.extension() == ".nebslots")
                    {
                        slotPath = gSelectedAssetPath;
                        if (!gProjectDir.empty())
                        {
                            std::filesystem::path rel = std::filesystem::relative(slotPath, std::filesystem::path(gProjectDir));
                            strncpy_s(nebslotBuf, rel.generic_string().c_str(), sizeof(nebslotBuf) - 1);
                        }
                    }
                    else
                    {
                        slotPath = std::filesystem::path(nebslotBuf);
                        if (!slotPath.empty() && slotPath.is_relative() && !gProjectDir.empty())
                            slotPath = std::filesystem::path(gProjectDir) / slotPath;
                    }

                    std::vector<std::string> slots;
                    if (!slotPath.empty() && NebulaAssets::LoadNebSlotsManifestFile(slotPath, slots, gProjectDir))
                    {
                        for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                        {
                            if (si < (int)slots.size() && !slots[si].empty())
                                n.materialSlots[si] = slots[si];
                        }
                        if (!n.materialSlots[0].empty()) n.material = n.materialSlots[0];
                        for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                            strncpy_s(matBuf[si], n.materialSlots[si].c_str(), sizeof(matBuf[si]) - 1);
                        gViewportToast = "Nebslot linked + loaded";
                    }
                    else
                    {
                        gViewportToast = "Select/highlight a .nebslots in Assets";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                ImGui::SameLine();
                if (ImGui::InputText("##NebslotPath", nebslotBuf, sizeof(nebslotBuf)))
                {
                }

                ImGui::Spacing();
                ImGui::Text("Materials");

                if (inspectorChanged)
                {
                    if (n.materialSlots[0].empty()) n.materialSlots[0] = n.material;
                    for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                        strncpy_s(matBuf[si], n.materialSlots[si].c_str(), sizeof(matBuf[si]) - 1);
                }
                // Material Active Slot control moved to per-slot mini selectors.

                if (ImGui::Button(">##MatAssign0"))
                {
                    if (!gSelectedAssetPath.empty() && gSelectedAssetPath.extension() == ".nebmat" && !gProjectDir.empty())
                    {
                        std::filesystem::path rel = std::filesystem::relative(gSelectedAssetPath, std::filesystem::path(gProjectDir));
                        n.materialSlots[0] = rel.generic_string();
                        n.material = n.materialSlots[0];
                        strncpy_s(matBuf[0], n.materialSlots[0].c_str(), sizeof(matBuf[0]) - 1);
                        gViewportToast = "Material slot 1 assigned";
                    }
                    else
                    {
                        gViewportToast = "Select a .nebmat in Assets";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                ImGui::SameLine();
                if (!ImGui::IsAnyItemActive() && n.materialSlots[0] != matBuf[0])
                    strncpy_s(matBuf[0], n.materialSlots[0].c_str(), sizeof(matBuf[0]) - 1);
                if (ImGui::InputText("##MaterialPath0", matBuf[0], sizeof(matBuf[0])))
                {
                    std::string s = matBuf[0];
                    if (!s.empty() && s.rfind("Assets/", 0) != 0 && s.rfind("Assets\\", 0) != 0)
                        s = "Assets/" + s;
                    n.materialSlots[0] = s;
                    n.material = s;
                }
                ImGui::SameLine();
                std::string slot1Label = NebulaNodes::GetStaticMeshSlotLabel(n, 0, gProjectDir);
                ImGui::Text("%s", slot1Label.c_str());

                for (int si = 1; si < kStaticMeshMaterialSlots; ++si)
                {
                    std::string btnId = ">##MatAssign" + std::to_string(si);
                    if (ImGui::Button(btnId.c_str()))
                    {
                        if (!gSelectedAssetPath.empty() && gSelectedAssetPath.extension() == ".nebmat" && !gProjectDir.empty())
                        {
                            std::filesystem::path rel = std::filesystem::relative(gSelectedAssetPath, std::filesystem::path(gProjectDir));
                            n.materialSlots[si] = rel.generic_string();
                            strncpy_s(matBuf[si], n.materialSlots[si].c_str(), sizeof(matBuf[si]) - 1);
                            gViewportToast = "Material slot " + std::to_string(si + 1) + " assigned";
                        }
                        else
                        {
                            gViewportToast = "Select a .nebmat in Assets";
                        }
                        gViewportToastUntil = glfwGetTime() + 2.0;
                    }
                    ImGui::SameLine();

                    std::string id = "##MaterialPath" + std::to_string(si);
                    std::string label = NebulaNodes::GetStaticMeshSlotLabel(n, si, gProjectDir);
                    if (!ImGui::IsAnyItemActive() && n.materialSlots[si] != matBuf[si])
                        strncpy_s(matBuf[si], n.materialSlots[si].c_str(), sizeof(matBuf[si]) - 1);
                    if (ImGui::InputText(id.c_str(), matBuf[si], sizeof(matBuf[si])))
                    {
                        std::string s = matBuf[si];
                        if (!s.empty() && s.rfind("Assets/", 0) != 0 && s.rfind("Assets\\", 0) != 0)
                            s = "Assets/" + s;
                        n.materialSlots[si] = s;
                    }
                    ImGui::SameLine();
                    ImGui::Text("%s", label.c_str());
                }

                ImGui::Spacing();
                ImGui::Text("Mesh");

                static char meshBuf[256] = {};
                if (inspectorChanged)
                {
                    strncpy_s(meshBuf, n.mesh.c_str(), sizeof(meshBuf) - 1);
                }
                if (ImGui::Button(">##MeshAssign"))
                {
                    if (!gSelectedAssetPath.empty() && gSelectedAssetPath.extension() == ".nebmesh" && !gProjectDir.empty())
                    {
                        std::filesystem::path rel = std::filesystem::relative(gSelectedAssetPath, std::filesystem::path(gProjectDir));
                        n.mesh = rel.generic_string();
                        NebulaNodes::AutoAssignMaterialSlotsFromMesh(n);
                        strncpy_s(meshBuf, n.mesh.c_str(), sizeof(meshBuf) - 1);
                        for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                            strncpy_s(matBuf[si], n.materialSlots[si].c_str(), sizeof(matBuf[si]) - 1);
                        gViewportToast = "Static mesh assigned + slots auto-mapped";
                    }
                    else
                    {
                        gViewportToast = "Select a .nebmesh in Assets";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                ImGui::SameLine();
                if (!ImGui::IsAnyItemActive() && n.mesh != meshBuf)
                    strncpy_s(meshBuf, n.mesh.c_str(), sizeof(meshBuf) - 1);
                if (ImGui::InputText("##MeshPath", meshBuf, sizeof(meshBuf)))
                {
                    std::string s = meshBuf;
                    if (!s.empty() && s.rfind("Assets/", 0) != 0 && s.rfind("Assets\\", 0) != 0)
                        s = "Assets/" + s;
                    n.mesh = s;
                    NebulaNodes::AutoAssignMaterialSlotsFromMesh(n);
                    for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                        strncpy_s(matBuf[si], n.materialSlots[si].c_str(), sizeof(matBuf[si]) - 1);
                }
                std::string meshDisplayName = "(none)";
                bool meshOk = false;
                if (!n.mesh.empty())
                {
                    std::filesystem::path np = std::filesystem::path(n.mesh);
                    std::string stem = np.stem().string();
                    meshDisplayName = stem.empty() ? np.filename().string() : stem;
                    if (!gProjectDir.empty())
                    {
                        std::filesystem::path mp = std::filesystem::path(gProjectDir) / n.mesh;
                        meshOk = std::filesystem::exists(mp);
                    }
                }
                ImGui::SameLine();
                ImGui::Text("%s", meshDisplayName.c_str());

                ImGui::Spacing();
                ImGui::Text("Load NebAnim (legacy)");
                static char animBuf[256] = {};
                if (inspectorChanged)
                    strncpy_s(animBuf, n.vtxAnim.c_str(), sizeof(animBuf) - 1);
                if (ImGui::Button(">##StaticAnimAssign"))
                {
                    if (!gSelectedAssetPath.empty() && gSelectedAssetPath.extension() == ".nebanim" && !gProjectDir.empty())
                    {
                        std::filesystem::path rel = std::filesystem::relative(gSelectedAssetPath, std::filesystem::path(gProjectDir));
                        n.vtxAnim = rel.generic_string();
                        strncpy_s(animBuf, n.vtxAnim.c_str(), sizeof(animBuf) - 1);
                        gViewportToast = "Static mesh anim assigned";
                    }
                    else
                    {
                        gViewportToast = "Select a .nebanim in Assets";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                ImGui::SameLine();
                if (!ImGui::IsAnyItemActive() && n.vtxAnim != animBuf)
                    strncpy_s(animBuf, n.vtxAnim.c_str(), sizeof(animBuf) - 1);
                if (ImGui::InputText("##StaticAnimPath", animBuf, sizeof(animBuf)))
                {
                    std::string s = animBuf;
                    if (!s.empty() && s.rfind("Assets/", 0) != 0 && s.rfind("Assets\\", 0) != 0)
                        s = "Assets/" + s;
                    n.vtxAnim = s;
                }

                ImGui::Checkbox("Runtime test", &n.runtimeTest);

                // --- Animation Slots ---
                ImGui::Spacing();
                if (ImGui::CollapsingHeader("Animation Slots", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    static char animSlotNameBuf[kStaticMeshAnimSlots][64] = {};
                    static char animSlotPathBuf[kStaticMeshAnimSlots][256] = {};
                    static int animSlotLastNode = -1;
                    if (animSlotLastNode != inspectStatic || inspectorChanged)
                    {
                        animSlotLastNode = inspectStatic;
                        for (int si = 0; si < kStaticMeshAnimSlots; ++si)
                        {
                            strncpy_s(animSlotNameBuf[si], n.animSlots[si].name.c_str(), sizeof(animSlotNameBuf[si]) - 1);
                            strncpy_s(animSlotPathBuf[si], n.animSlots[si].path.c_str(), sizeof(animSlotPathBuf[si]) - 1);
                        }
                    }

                    int removeIdx = -1;
                    for (int si = 0; si < n.animSlotCount; ++si)
                    {
                        ImGui::PushID(si);
                        ImGui::Text("Slot %d", si + 1);
                        ImGui::SameLine();
                        if (ImGui::Button("X##AnimSlotRemove"))
                            removeIdx = si;

                        // Name field
                        std::string nameLabel = "##AnimSlotName" + std::to_string(si);
                        ImGui::SetNextItemWidth(120.0f);
                        if (!ImGui::IsAnyItemActive() && n.animSlots[si].name != animSlotNameBuf[si])
                            strncpy_s(animSlotNameBuf[si], n.animSlots[si].name.c_str(), sizeof(animSlotNameBuf[si]) - 1);
                        if (ImGui::InputText(nameLabel.c_str(), animSlotNameBuf[si], sizeof(animSlotNameBuf[si])))
                            n.animSlots[si].name = animSlotNameBuf[si];
                        ImGui::SameLine();
                        ImGui::Text("Name");

                        // Path assign button + field
                        std::string btnId = ">##AnimSlotAssign" + std::to_string(si);
                        if (ImGui::Button(btnId.c_str()))
                        {
                            if (!gSelectedAssetPath.empty() && gSelectedAssetPath.extension() == ".nebanim" && !gProjectDir.empty())
                            {
                                std::filesystem::path rel = std::filesystem::relative(gSelectedAssetPath, std::filesystem::path(gProjectDir));
                                n.animSlots[si].path = rel.generic_string();
                                strncpy_s(animSlotPathBuf[si], n.animSlots[si].path.c_str(), sizeof(animSlotPathBuf[si]) - 1);
                                gViewportToast = "Anim slot " + std::to_string(si + 1) + " assigned";
                            }
                            else
                            {
                                gViewportToast = "Select a .nebanim in Assets";
                            }
                            gViewportToastUntil = glfwGetTime() + 2.0;
                        }
                        ImGui::SameLine();
                        std::string pathLabel = "##AnimSlotPath" + std::to_string(si);
                        if (!ImGui::IsAnyItemActive() && n.animSlots[si].path != animSlotPathBuf[si])
                            strncpy_s(animSlotPathBuf[si], n.animSlots[si].path.c_str(), sizeof(animSlotPathBuf[si]) - 1);
                        if (ImGui::InputText(pathLabel.c_str(), animSlotPathBuf[si], sizeof(animSlotPathBuf[si])))
                        {
                            std::string s = animSlotPathBuf[si];
                            if (!s.empty() && s.rfind("Assets/", 0) != 0 && s.rfind("Assets\\", 0) != 0)
                                s = "Assets/" + s;
                            n.animSlots[si].path = s;
                        }

                        // Play button per slot
                        std::filesystem::path slotAnimAbs = ResolveProjectAssetPath(n.animSlots[si].path);
                        NebAnimClip* slotClip = nullptr;
                        if (!slotAnimAbs.empty() && std::filesystem::exists(slotAnimAbs))
                        {
                            std::string key = slotAnimAbs.generic_string();
                            auto it = gStaticAnimClipCache.find(key);
                            if (it == gStaticAnimClipCache.end())
                            {
                                NebAnimClip clip;
                                std::string err;
                                if (LoadNebAnimClip(slotAnimAbs, clip, err))
                                    it = gStaticAnimClipCache.emplace(key, std::move(clip)).first;
                            }
                            if (it != gStaticAnimClipCache.end() && it->second.valid)
                                slotClip = &it->second;
                        }
                        if (slotClip && slotClip->frameCount > 0)
                        {
                            std::string playLabel = (gStaticAnimPreviewPlay && gStaticAnimPreviewNode == inspectStatic && gStaticAnimPreviewSlot == si) ? "Stop##AnimSlotPlay" : "Play##AnimSlotPlay";
                            playLabel += std::to_string(si);
                            if (ImGui::Button(playLabel.c_str()))
                            {
                                if (gStaticAnimPreviewPlay && gStaticAnimPreviewNode == inspectStatic && gStaticAnimPreviewSlot == si)
                                {
                                    gStaticAnimPreviewPlay = false;
                                }
                                else
                                {
                                    gStaticAnimPreviewNode = inspectStatic;
                                    gStaticAnimPreviewSlot = si;
                                    gStaticAnimPreviewFrame = 0;
                                    gStaticAnimPreviewTimeSec = 0.0f;
                                    gStaticAnimPreviewPlay = true;
                                }
                            }
                            ImGui::SameLine();
                            ImGui::Text("%d frames, %.1f fps", (int)slotClip->frameCount, slotClip->fps);
                        }
                        else if (!n.animSlots[si].path.empty())
                        {
                            ImGui::TextDisabled("(invalid .nebanim)");
                        }

                        // Speed slider per slot (0 stopped .. 1 normal .. 2 fast)
                        {
                            std::string speedLabel = "Speed##AnimSlotSpeed" + std::to_string(si);
                            ImGui::SetNextItemWidth(200.0f);
                            if (ImGui::SliderFloat(speedLabel.c_str(), &n.animSlots[si].speed, 0.0f, 2.0f, "%.2f"))
                            {
                                // Live-update: apply speed to active preview or play-mode animation
                                if (gStaticAnimPreviewPlay && gStaticAnimPreviewNode == inspectStatic && gStaticAnimPreviewSlot == si)
                                {
                                    // Preview uses the slot speed directly via the clip fps, nothing extra to set
                                }
                                if (gEditorAnimPlaying.count(inspectStatic) && gEditorAnimPlaying[inspectStatic] && gEditorAnimActiveSlot.count(inspectStatic) && gEditorAnimActiveSlot[inspectStatic] == si)
                                {
                                    gEditorAnimSpeed[inspectStatic] = n.animSlots[si].speed;
                                }
                            }
                        }

                        // Loop toggle
                        {
                            std::string loopLabel = "Loop##AnimSlotLoop" + std::to_string(si);
                            ImGui::Checkbox(loopLabel.c_str(), &n.animSlots[si].loop);
                        }

                        ImGui::Separator();
                        ImGui::PopID();
                    }

                    // Remove slot
                    if (removeIdx >= 0 && removeIdx < n.animSlotCount)
                    {
                        for (int si = removeIdx; si < n.animSlotCount - 1; ++si)
                            n.animSlots[si] = n.animSlots[si + 1];
                        n.animSlots[n.animSlotCount - 1] = AnimSlot{};
                        n.animSlotCount--;
                        animSlotLastNode = -1; // force buffer resync
                    }

                    // Add slot button
                    if (n.animSlotCount < kStaticMeshAnimSlots)
                    {
                        if (ImGui::Button("+ Add Animation Slot"))
                        {
                            n.animSlots[n.animSlotCount].name = "Anim" + std::to_string(n.animSlotCount + 1);
                            n.animSlots[n.animSlotCount].path.clear();
                            n.animSlotCount++;
                            animSlotLastNode = -1; // force buffer resync
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("Max %d animation slots", kStaticMeshAnimSlots);
                    }
                }

                // Dreamcast animation loading mode
                if (n.animSlotCount > 0)
                {
                    ImGui::Separator();
                    ImGui::Text("DC Anim Loading:");
                    bool preload = n.animPreload;
                    bool discIO = !n.animPreload;
                    if (ImGui::RadioButton("RAM Preload", preload))
                        n.animPreload = true;
                    ImGui::SameLine();
                    if (ImGui::RadioButton("Disc I/O", discIO))
                        n.animPreload = false;
                }

                // Animation preview tick (for slot-based preview)
                if (gStaticAnimPreviewLastNode != inspectStatic)
                {
                    gStaticAnimPreviewLastNode = inspectStatic;
                    gStaticAnimPreviewNode = inspectStatic;
                    gStaticAnimPreviewPlay = false;
                    gStaticAnimPreviewTimeSec = 0.0f;
                    gStaticAnimPreviewFrame = 0;
                    gStaticAnimPreviewSlot = -1;
                }

                if (gStaticAnimPreviewPlay && gStaticAnimPreviewNode == inspectStatic && gStaticAnimPreviewSlot >= 0 && gStaticAnimPreviewSlot < n.animSlotCount)
                {
                    std::filesystem::path previewAnimAbs = ResolveProjectAssetPath(n.animSlots[gStaticAnimPreviewSlot].path);
                    NebAnimClip* previewClip = nullptr;
                    if (!previewAnimAbs.empty() && std::filesystem::exists(previewAnimAbs))
                    {
                        std::string key = previewAnimAbs.generic_string();
                        auto it = gStaticAnimClipCache.find(key);
                        if (it != gStaticAnimClipCache.end() && it->second.valid)
                            previewClip = &it->second;
                    }
                    if (previewClip && previewClip->frameCount > 0)
                    {
                        float fps = std::max(1.0f, previewClip->fps);
                        const int maxFrame = (int)previewClip->frameCount - 1;
                        float slotSpeed = (gStaticAnimPreviewSlot >= 0 && gStaticAnimPreviewSlot < n.animSlotCount) ? n.animSlots[gStaticAnimPreviewSlot].speed : 1.0f;
                        gStaticAnimPreviewTimeSec += ImGui::GetIO().DeltaTime * slotSpeed;
                        if (gStaticAnimPreviewTimeSec < 0.0f) gStaticAnimPreviewTimeSec = 0.0f;
                        int nextFrame = (int)std::floor(gStaticAnimPreviewTimeSec * fps);
                        if (gStaticAnimPreviewLoop)
                        {
                            if (maxFrame > 0)
                                nextFrame = nextFrame % (maxFrame + 1);
                        }
                        else if (nextFrame > maxFrame)
                        {
                            nextFrame = maxFrame;
                            gStaticAnimPreviewPlay = false;
                        }
                        gStaticAnimPreviewFrame = std::max(0, std::min(nextFrame, maxFrame));
                    }
                }
                // Legacy vtxAnim preview (when no slot preview is active)
                else if (!n.vtxAnim.empty() && (gStaticAnimPreviewSlot < 0 || !gStaticAnimPreviewPlay))
                {
                    std::filesystem::path animAbs = ResolveProjectAssetPath(n.vtxAnim);
                    NebAnimClip* previewClip = nullptr;
                    if (!animAbs.empty() && std::filesystem::exists(animAbs))
                    {
                        std::string key = animAbs.generic_string();
                        auto it = gStaticAnimClipCache.find(key);
                        if (it == gStaticAnimClipCache.end())
                        {
                            NebAnimClip clip;
                            std::string err;
                            if (LoadNebAnimClip(animAbs, clip, err))
                                it = gStaticAnimClipCache.emplace(key, std::move(clip)).first;
                        }
                    }
                }

                ImGui::Text("Parent: %s", n.parent.empty() ? "(none)" : n.parent.c_str());
                ImGui::SameLine();
                if (!n.parent.empty() && ImGui::Button("Unparent##InspectorStatic")) n.parent.clear();
                ImGui::SameLine();
                if (ImGui::Button("Reset Xform (keep world)##InspectorStatic"))
                    ResetStaticMeshTransformsKeepWorld(inspectStatic);
                ImGui::Checkbox("Collision Source (Saturn floor)", &n.collisionSource);
                if (n.collisionSource)
                    ImGui::SliderFloat("Wall Threshold", &n.wallThreshold, 0.0f, 1.0f, "%.2f");
                ImGui::Checkbox("Navmesh Ready", &n.navmeshReady);

                ImGui::DragFloat3("Position", &n.x, 0.1f);
                float rotArr[3] = { n.rotY, n.rotZ, n.rotX };
                bool rotCh = ImGui::DragFloat3("##StaticRotPacked", rotArr, 0.5f);
                ImGui::SameLine(); ImGui::Text("Rotation");
                if (rotCh)
                {
                    n.rotY = rotArr[0];
                    n.rotZ = rotArr[1];
                    n.rotX = rotArr[2];
                }
                ImGui::DragFloat3("Scale", &n.scaleX, 0.01f, 0.01f, 100.0f);
                /* Saturn sampling preview is now always on. */
            }
            else if (inspectCamera >= 0 && inspectCamera < (int)gCamera3DNodes.size())
            {
                auto& c = gCamera3DNodes[inspectCamera];
                const int inspectorId = 30000 + inspectCamera;
                const bool inspectorChanged = (gInspectorSel != inspectorId);
                if (inspectorChanged)
                {
                    gInspectorSel = inspectorId;
                    strncpy_s(gInspectorName, c.name.c_str(), sizeof(gInspectorName) - 1);
                }

                ImGui::Text("Camera3D Node");
                if (ImGui::InputText("Name", gInspectorName, sizeof(gInspectorName)))
                {
                    c.name = gInspectorName;
                }

                ImGui::DragFloat3("Position", &c.x, 0.1f);
                static char camParentBuf[256] = {};
                if (inspectorChanged)
                    strncpy_s(camParentBuf, c.parent.c_str(), sizeof(camParentBuf) - 1);
                if (!ImGui::IsAnyItemActive() && c.parent != camParentBuf)
                    strncpy_s(camParentBuf, c.parent.c_str(), sizeof(camParentBuf) - 1);
                if (ImGui::InputText("Parent##CamParent", camParentBuf, sizeof(camParentBuf)))
                    c.parent = camParentBuf;
                ImGui::SameLine();
                if (!c.parent.empty() && ImGui::Button("Unparent##InspectorCam")) c.parent.clear();
                ImGui::SameLine();
                if (ImGui::Button("Reset Xform (keep world pos)##InspectorCam"))
                {
                    float cwx, cwy, cwz, cwrx, cwry, cwrz;
                    GetCamera3DWorldTR(inspectCamera, cwx, cwy, cwz, cwrx, cwry, cwrz);
                    c.parent.clear();
                    c.x = cwx;
                    c.y = cwy;
                    c.z = cwz;
                    c.rotX = 0.0f;
                    c.rotY = 0.0f;
                    c.rotZ = 0.0f;
                    c.orbitX = 0.0f;
                    c.orbitY = 0.0f;
                    c.orbitZ = 0.0f;
                }

                ImGui::Text("Rotation"); ImGui::SameLine();
                ImGui::SetNextItemWidth(72); ImGui::DragFloat("X##CamRotX", &c.rotX, 0.5f);
                ImGui::SameLine(); ImGui::SetNextItemWidth(72); ImGui::DragFloat("Y##CamRotY", &c.rotY, 0.5f);
                ImGui::SameLine(); ImGui::SetNextItemWidth(72); ImGui::DragFloat("Z##CamRotZ", &c.rotZ, 0.5f);
                ImGui::BeginDisabled(c.parent.empty());
                ImGui::DragFloat3("Orbit", &c.orbitX, 0.05f);
                ImGui::EndDisabled();
                if (c.parent.empty()) ImGui::TextDisabled("Orbit enabled when Parent is set (parent acts as pivot)");
                ImGui::Checkbox("Perspective", &c.perspective);

                if (c.perspective)
                {
                    ImGui::DragFloat("FOV", &c.fovY, 0.1f, 5.0f, 170.0f);
                }
                else
                {
                    ImGui::DragFloat("Ortho Width", &c.orthoWidth, 0.05f, 0.01f, 5000.0f);
                    float aspectNow = (gDisplayH > 0) ? ((float)gDisplayW / (float)gDisplayH) : (16.0f / 9.0f);
                    float orthoH = c.orthoWidth / aspectNow;
                    ImGui::Text("Ortho Height: %.3f", orthoH);
                }

                ImGui::DragFloat("Near", &c.nearZ, 0.01f, 0.001f, 1000.0f);
                ImGui::DragFloat("Far", &c.farZ, 1.0f, 1.0f, 50000.0f);
                ImGui::DragFloat("Priority", &c.priority, 0.1f);

                bool oldMain = c.main;
                ImGui::Checkbox("Main Camera", &c.main);
                if (!oldMain && c.main)
                {
                    for (int i = 0; i < (int)gCamera3DNodes.size(); ++i)
                    {
                        if (i != gSelectedCamera3D)
                            gCamera3DNodes[i].main = false;
                    }
                }

                if (ImGui::Button("Set View To Camera"))
                {
                    float cwx, cwy, cwz, cwrx, cwry, cwrz;
                    GetCamera3DWorldTR(inspectCamera, cwx, cwy, cwz, cwrx, cwry, cwrz);
                    Vec3 right{}, upAxis{}, forward{};
                    GetLocalAxesFromEuler(cwrx, cwry, cwrz, right, upAxis, forward);
                    gOrbitCenter = { cwx + forward.x * gViewDistance, cwy + forward.y * gViewDistance, cwz + forward.z * gViewDistance };

                    // Convert engine Euler-forward convention to viewport yaw/pitch convention.
                    gViewYaw = atan2f(forward.z, forward.x) * 180.0f / 3.14159f;
                    float fy = std::clamp(forward.y, -1.0f, 1.0f);
                    gViewPitch = asinf(fy) * 180.0f / 3.14159f;
                }
                ImGui::SameLine();
                if (ImGui::Button("Set Camera To View"))
                {
                    c.x = gEye.x;
                    c.y = gEye.y;
                    c.z = gEye.z;

                    // Build forward from current viewport camera.
                    float yawRad = gViewYaw * 3.14159f / 180.0f;
                    float pitchRad = gViewPitch * 3.14159f / 180.0f;
                    Vec3 fwd = {
                        cosf(pitchRad) * cosf(yawRad),
                        sinf(pitchRad),
                        cosf(pitchRad) * sinf(yawRad)
                    };

                    // Map viewport forward into engine Euler convention (Rz*Ry*Rx), keeping roll neutral.
                    c.rotZ = 0.0f;
                    c.rotX = asinf(std::clamp(-fwd.y, -1.0f, 1.0f)) * 180.0f / 3.14159f;
                    c.rotY = atan2f(fwd.x, fwd.z) * 180.0f / 3.14159f;
                }
            }
            else if (inspectNode3D >= 0 && inspectNode3D < (int)gNode3DNodes.size())
            {
                auto& n = gNode3DNodes[inspectNode3D];
                const int inspectorId = 40000 + inspectNode3D;
                const bool inspectorChanged = (gInspectorSel != inspectorId);
                if (inspectorChanged)
                {
                    gInspectorSel = inspectorId;
                    strncpy_s(gInspectorName, n.name.c_str(), sizeof(gInspectorName) - 1);
                }

                ImGui::Text("Node3D Node");
                if (ImGui::InputText("Name", gInspectorName, sizeof(gInspectorName)))
                {
                    n.name = gInspectorName;
                }

                static char scriptBuf[256] = {};
                if (inspectorChanged)
                {
                    strncpy_s(scriptBuf, n.script.c_str(), sizeof(scriptBuf) - 1);
                }
                if (!ImGui::IsAnyItemActive() && n.script != scriptBuf)
                    strncpy_s(scriptBuf, n.script.c_str(), sizeof(scriptBuf) - 1);
                if (ImGui::Button(">##Node3DScriptAssign"))
                {
                    if (!gSelectedAssetPath.empty() && gSelectedAssetPath.extension() == ".c" && !gProjectDir.empty())
                    {
                        std::filesystem::path rel = std::filesystem::relative(gSelectedAssetPath, std::filesystem::path(gProjectDir));
                        std::string s = rel.generic_string();
                        if (!s.empty() && s.rfind("Scripts/", 0) != 0 && s.rfind("Scripts\\", 0) != 0)
                            s = "Scripts/" + s;
                        n.script = s;
                        strncpy_s(scriptBuf, n.script.c_str(), sizeof(scriptBuf) - 1);
                        gViewportToast = "Node3D script assigned";
                    }
                    else
                    {
                        gViewportToast = "Select a .c script in Assets";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                ImGui::SameLine();
                if (ImGui::InputText("##Node3DScriptPath", scriptBuf, sizeof(scriptBuf)))
                {
                    std::string s = scriptBuf;
                    if (!s.empty() && s.rfind("Scripts/", 0) != 0 && s.rfind("Scripts\\", 0) != 0)
                        s = "Scripts/" + s;
                    n.script = s;
                }
                bool scriptOk = false;
                if (!n.script.empty() && !gProjectDir.empty())
                {
                    std::filesystem::path sp = std::filesystem::path(gProjectDir) / n.script;
                    scriptOk = std::filesystem::exists(sp);
                }
                ImGui::SameLine();
                if (ImGui::Button("Load Script##Node3D"))
                {
                    if (scriptOk)
                        gViewportToast = n.script + " script validated";
                    else
                        gViewportToast = (n.script.empty() ? "(none)" : n.script) + " script unvalidated";
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                if (ImGui::Button("Deinit Script##Node3D"))
                {
                    n.script.clear();
                    scriptBuf[0] = '\0';
                    if (!gProjectDir.empty())
                    {
                        std::filesystem::path outDir = std::filesystem::path(gProjectDir) / "Intermediate" / "EditorScript";
                        std::error_code ec;
                        if (std::filesystem::exists(outDir, ec))
                        {
                            for (auto& e : std::filesystem::directory_iterator(outDir, ec))
                            {
                                if (e.is_regular_file() && e.path().extension() == ".dll")
                                    std::filesystem::remove(e.path(), ec);
                            }
                        }
                    }
                    UnloadEditorScriptRuntime();
                    gViewportToast = "Script hook removed";
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }

                ImGui::Text("Primitive: %s", n.primitiveMesh.empty() ? "(none)" : n.primitiveMesh.c_str());
                ImGui::Checkbox("Simple Collision", &n.simpleCollision);
                ImGui::Checkbox("Collision Source (slope alignment)", &n.collisionSource);
                ImGui::Checkbox("Gravity", &n.physicsEnabled);
                ImGui::Text("Parent: %s", n.parent.empty() ? "(none)" : n.parent.c_str());
                ImGui::SameLine();
                if (!n.parent.empty() && ImGui::Button("Unparent##InspectorNode3D")) n.parent.clear();
                ImGui::DragFloat3("Position", &n.x, 0.1f);
                // Match StaticMesh3D axis mapping exactly to keep parent/child behavior consistent.
                float rotArrNode[3] = { n.rotY, n.rotZ, n.rotX };
                bool rotNodeCh = ImGui::DragFloat3("##Node3DRotPacked", rotArrNode, 0.5f);
                ImGui::SameLine(); ImGui::Text("Rotation");
                if (rotNodeCh)
                {
                    n.rotY = rotArrNode[0];
                    n.rotZ = rotArrNode[1];
                    n.rotX = rotArrNode[2];
                    SyncNode3DQuatFromEuler(n);
                }
                ImGui::DragFloat3("Scale", &n.scaleX, 0.01f, 0.01f, 100.0f);
                ImGui::Separator();
                ImGui::TextUnformatted("Collision Bounds (local)");
                ImGui::DragFloat3("XYZ Extents", &n.extentX, 0.01f, 0.0f, 1000.0f);
                ImGui::DragFloat3("Bounds Position", &n.boundPosX, 0.01f);

            }
            else if (inspectNavMesh3D >= 0 && inspectNavMesh3D < (int)gNavMesh3DNodes.size())
            {
                auto& n = gNavMesh3DNodes[inspectNavMesh3D];
                ImGui::Text("NavMesh3D");
                ImGui::Separator();

                static char navNameBuf[256] = {};
                static int lastNavInspected = -1;
                bool navInspectorChanged = (lastNavInspected != inspectNavMesh3D);
                lastNavInspected = inspectNavMesh3D;
                if (navInspectorChanged)
                    strncpy_s(navNameBuf, n.name.c_str(), sizeof(navNameBuf) - 1);
                if (ImGui::InputText("Name", navNameBuf, sizeof(navNameBuf)))
                    n.name = navNameBuf;

                ImGui::DragFloat3("Position", &n.x, 0.1f);
                float navRotArr[3] = { n.rotY, n.rotZ, n.rotX };
                if (ImGui::DragFloat3("##NavMesh3DRotPacked", navRotArr, 0.5f))
                {
                    n.rotY = navRotArr[0];
                    n.rotZ = navRotArr[1];
                    n.rotX = navRotArr[2];
                }
                ImGui::SameLine(); ImGui::Text("Rotation");
                ImGui::DragFloat3("Scale", &n.scaleX, 0.01f, 0.01f, 100.0f);
                ImGui::Separator();
                ImGui::DragFloat3("Extents", &n.extentX, 0.1f, 0.1f, 1000.0f);
                ImGui::Checkbox("Nav Bounds", &n.navBounds);
                ImGui::Checkbox("Nav Negator", &n.navNegator);
                ImGui::Checkbox("Cull Walls", &n.cullWalls);
                if (n.cullWalls)
                    ImGui::DragFloat("Wall Cull Threshold", &n.wallCullThreshold, 0.01f, 0.0f, 1.0f);
                ImGui::Separator();
                ImGui::ColorEdit3("Wireframe Color", &n.wireR);
                ImGui::DragFloat("Wireframe Thickness", &n.wireThickness, 0.1f, 0.5f, 10.0f);
            }
            else if (gMaterialInspectorOpen && !gMaterialInspectorPath.empty())
            {
                ImGui::Text("Material");
                std::string texPath;
                NebulaAssets::LoadMaterialTexture(gMaterialInspectorPath, texPath);
                static char texBuf[256] = {};
                strncpy_s(texBuf, texPath.c_str(), sizeof(texBuf) - 1);
                if (ImGui::Button(">##TexAssignInspector"))
                {
                    if (!gSelectedAssetPath.empty() && gSelectedAssetPath.extension() == ".nebtex" && !gProjectDir.empty())
                    {
                        std::filesystem::path rel = std::filesystem::relative(gSelectedAssetPath, std::filesystem::path(gProjectDir));
                        texPath = rel.generic_string();
                        strncpy_s(texBuf, texPath.c_str(), sizeof(texBuf) - 1);
                        NebulaAssets::SaveMaterialTexture(gMaterialInspectorPath, texPath);
                        gViewportToast = "Texture assigned";
                    }
                    else
                    {
                        gViewportToast = "Select a .nebtex texture in Assets";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                ImGui::SameLine();
                if (ImGui::InputText("Texture Assignment", texBuf, sizeof(texBuf)))
                {
                    std::string s = texBuf;
                    if (!s.empty() && s.rfind("Assets/", 0) != 0 && s.rfind("Assets\\", 0) != 0)
                        s = "Assets/" + s;
                    NebulaAssets::SaveMaterialTexture(gMaterialInspectorPath, s);
                }
                {
                    float su=1,sv=1,ou=0,ov=0,rd=0;
                    NebulaAssets::LoadMaterialUvTransform(gMaterialInspectorPath,su,sv,ou,ov,rd);
                    bool changed = false;
                    if (ImGui::DragFloat("U Scale", &su, 0.01f, 0.01f, 100.0f, "%.3f")) changed = true;
                    if (ImGui::DragFloat("V Scale", &sv, 0.01f, 0.01f, 100.0f, "%.3f")) changed = true;
                    if (changed) NebulaAssets::SaveMaterialUvTransform(gMaterialInspectorPath,su,sv,ou,ov,rd);
                }
                int shadingMode = NebulaAssets::LoadMaterialShadingMode(gMaterialInspectorPath);
                const char* shadingOptions[] = { "Unlit", "Lit" };
                if (ImGui::Combo("Shading", &shadingMode, shadingOptions, IM_ARRAYSIZE(shadingOptions)))
                    NebulaAssets::SaveMaterialShadingMode(gMaterialInspectorPath, shadingMode);
                if (shadingMode == 1)
                {
                    float lightRot = NebulaAssets::LoadMaterialLightRotation(gMaterialInspectorPath);
                    if (ImGui::DragFloat("Light X", &lightRot, 1.0f, -360.0f, 360.0f, "%.0f deg"))
                        NebulaAssets::SaveMaterialLightRotation(gMaterialInspectorPath, lightRot);
                    float lightPit = NebulaAssets::LoadMaterialLightPitch(gMaterialInspectorPath);
                    if (ImGui::DragFloat("Light Y", &lightPit, 1.0f, -360.0f, 360.0f, "%.0f deg"))
                        NebulaAssets::SaveMaterialLightPitch(gMaterialInspectorPath, lightPit);
                    float shadInt = NebulaAssets::LoadMaterialShadowIntensity(gMaterialInspectorPath);
                    if (ImGui::SliderFloat("Shadow Intensity", &shadInt, 0.0f, 1.0f, "%.2f"))
                        NebulaAssets::SaveMaterialShadowIntensity(gMaterialInspectorPath, shadInt);
                    int shadingUv = NebulaAssets::LoadMaterialShadingUv(gMaterialInspectorPath);
                    int meshUvCount = GetMeshUvLayerCountForMaterial(gMaterialInspectorPath);
                    int shadingUvCombo = shadingUv + 1;
                    const char* shadingUvOptions2[] = { "None", "UV0", "UV1" };
                    int optCount = 1 + meshUvCount; // "None" + one per UV layer
                    if (optCount > 3) optCount = 3;
                    if (ImGui::Combo("Vertex Shading UVs", &shadingUvCombo, shadingUvOptions2, optCount))
                        NebulaAssets::SaveMaterialShadingUv(gMaterialInspectorPath, shadingUvCombo - 1);
                }
                bool texOk = false;
                if (!texPath.empty())
                {
                    std::filesystem::path tp = std::filesystem::path(gProjectDir) / texPath;
                    texOk = std::filesystem::exists(tp);
                }
                ImGui::SameLine();
                ImGui::Text(texOk ? "OK" : "Missing");
            }
            else if (gNebTexInspectorOpen && !gNebTexInspectorPath.empty())
            {
                ImGui::Text("Texture");
                ImGui::TextWrapped("%s", gNebTexInspectorPath.filename().string().c_str());
                int texW = 0, texH = 0;
                if (NebulaAssets::ReadNebTexDimensions(gNebTexInspectorPath, texW, texH))
                    ImGui::Text("Dimensions: %d x %d", texW, texH);

                GLuint previewTex = GetNebTexture(gNebTexInspectorPath);
                if (previewTex != 0)
                {
                    ImGui::Text("Sample");
                    ImGui::Image((ImTextureID)(intptr_t)previewTex, ImVec2(128, 128));
                }

                int wrapMode = NebulaAssets::LoadNebTexWrapMode(gNebTexInspectorPath);
                int saturnNpot = NebulaAssets::LoadNebTexSaturnNpotMode(gNebTexInspectorPath); // 0=pad, 1=resample
                int filterMode = NebulaAssets::LoadNebTexFilterMode(gNebTexInspectorPath); // 0=nearest, 1=bilinear
                const char* wrapOptions[] = { "Repeat", "Extend" };
                const char* saturnNpotOptions[] = { "Pad", "Resample" };
                const char* filterOptions[] = { "Nearest", "Bilinear" };
                bool flipU = false, flipV = false;
                NebulaAssets::LoadNebTexFlipOptions(gNebTexInspectorPath, flipU, flipV);
                bool changed = false;
                if (wrapMode > 1) wrapMode = 0;
                changed |= ImGui::Combo("Wrap Mode", &wrapMode, wrapOptions, IM_ARRAYSIZE(wrapOptions));
                changed |= ImGui::Combo("Filter", &filterMode, filterOptions, IM_ARRAYSIZE(filterOptions));
                changed |= ImGui::Combo("NPOT", &saturnNpot, saturnNpotOptions, IM_ARRAYSIZE(saturnNpotOptions));
                changed |= ImGui::Checkbox("Flip U", &flipU);
                changed |= ImGui::Checkbox("Flip V", &flipV);
                if (changed)
                {
                    NebulaAssets::SaveNebTexWrapMode(gNebTexInspectorPath, wrapMode);
                    NebulaAssets::SaveNebTexFilterMode(gNebTexInspectorPath, filterMode);
                    NebulaAssets::SaveNebTexSaturnNpotMode(gNebTexInspectorPath, saturnNpot);
                    NebulaAssets::SaveNebTexFlipOptions(gNebTexInspectorPath, flipU, flipV);
                    // Force immediate visual update: flush entire neb texture cache.
                    for (auto& kv : gNebTextureCache)
                    {
                        if (kv.second != 0) glDeleteTextures(1, &kv.second);
                    }
                    gNebTextureCache.clear();
                    gPreviewSaturnSampling = true; // make NPOT preview visible live
                    gViewportToast = "Texture compatibility updated (live)";
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
            }

            ImGui::EndChild();
            ImGui::End();

            if (hasBottomInspector)
            {
                // Bottom-half inspector pane (secondary asset inspector).
                ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - rightPanelWidth, vp->Pos.y + topBarH + inspectorHalfH));
                ImGui::SetNextWindowSize(ImVec2(rightPanelWidth, inspectorHalfH));
                ImGui::Begin("InspectorBottom", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

                ImGui::Text("Inspector");
                ImGui::SameLine();
                float closeX2 = ImGui::GetWindowWidth() - ImGui::GetStyle().FramePadding.x - 16.0f;
                ImGui::SetCursorPosX(closeX2);
                if (ImGui::Button("x##BottomInspector"))
                {
                    gMaterialInspectorOpen2 = false;
                    gMaterialInspectorPath2.clear();
                    gNebTexInspectorOpen2 = false;
                    gNebTexInspectorPath2.clear();
                }
                ImGui::Separator();
                ImGui::BeginChild("##InspectorBottomScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);

                if (gMaterialInspectorOpen2 && !gMaterialInspectorPath2.empty())
            {
                ImGui::Text("Material");
                std::string texPath;
                NebulaAssets::LoadMaterialTexture(gMaterialInspectorPath2, texPath);
                static char texBuf2[256] = {};
                strncpy_s(texBuf2, texPath.c_str(), sizeof(texBuf2) - 1);
                if (ImGui::Button(">##TexAssignInspectorB"))
                {
                    if (!gSelectedAssetPath.empty() && gSelectedAssetPath.extension() == ".nebtex" && !gProjectDir.empty())
                    {
                        std::filesystem::path rel = std::filesystem::relative(gSelectedAssetPath, std::filesystem::path(gProjectDir));
                        texPath = rel.generic_string();
                        strncpy_s(texBuf2, texPath.c_str(), sizeof(texBuf2) - 1);
                        NebulaAssets::SaveMaterialTexture(gMaterialInspectorPath2, texPath);
                        gViewportToast = "Texture assigned";
                    }
                    else
                    {
                        gViewportToast = "Select a .nebtex texture in Assets";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                ImGui::SameLine();
                if (ImGui::InputText("Texture Assignment##B", texBuf2, sizeof(texBuf2)))
                {
                    std::string s = texBuf2;
                    if (!s.empty() && s.rfind("Assets/", 0) != 0 && s.rfind("Assets\\", 0) != 0)
                        s = "Assets/" + s;
                    NebulaAssets::SaveMaterialTexture(gMaterialInspectorPath2, s);
                }
                {
                    float su=1,sv=1,ou=0,ov=0,rd=0;
                    NebulaAssets::LoadMaterialUvTransform(gMaterialInspectorPath2,su,sv,ou,ov,rd);
                    bool changed = false;
                    if (ImGui::DragFloat("U Scale##B", &su, 0.01f, 0.01f, 100.0f, "%.3f")) changed = true;
                    if (ImGui::DragFloat("V Scale##B", &sv, 0.01f, 0.01f, 100.0f, "%.3f")) changed = true;
                    if (changed) NebulaAssets::SaveMaterialUvTransform(gMaterialInspectorPath2,su,sv,ou,ov,rd);
                }
                int shadingMode = NebulaAssets::LoadMaterialShadingMode(gMaterialInspectorPath2);
                const char* shadingOptions[] = { "Unlit", "Lit" };
                if (ImGui::Combo("Shading##B", &shadingMode, shadingOptions, IM_ARRAYSIZE(shadingOptions)))
                    NebulaAssets::SaveMaterialShadingMode(gMaterialInspectorPath2, shadingMode);
                if (shadingMode == 1)
                {
                    float lightRot = NebulaAssets::LoadMaterialLightRotation(gMaterialInspectorPath2);
                    if (ImGui::DragFloat("Light X##B", &lightRot, 1.0f, -360.0f, 360.0f, "%.0f deg"))
                        NebulaAssets::SaveMaterialLightRotation(gMaterialInspectorPath2, lightRot);
                    float lightPit = NebulaAssets::LoadMaterialLightPitch(gMaterialInspectorPath2);
                    if (ImGui::DragFloat("Light Y##B", &lightPit, 1.0f, -360.0f, 360.0f, "%.0f deg"))
                        NebulaAssets::SaveMaterialLightPitch(gMaterialInspectorPath2, lightPit);
                    float shadInt = NebulaAssets::LoadMaterialShadowIntensity(gMaterialInspectorPath2);
                    if (ImGui::SliderFloat("Shadow Intensity##B", &shadInt, 0.0f, 1.0f, "%.2f"))
                        NebulaAssets::SaveMaterialShadowIntensity(gMaterialInspectorPath2, shadInt);
                    int shadingUv = NebulaAssets::LoadMaterialShadingUv(gMaterialInspectorPath2);
                    int meshUvCount = GetMeshUvLayerCountForMaterial(gMaterialInspectorPath2);
                    int shadingUvCombo = shadingUv + 1;
                    const char* shadingUvOptions2[] = { "None", "UV0", "UV1" };
                    int optCount = 1 + meshUvCount;
                    if (optCount > 3) optCount = 3;
                    if (ImGui::Combo("Vertex Shading UVs##B", &shadingUvCombo, shadingUvOptions2, optCount))
                        NebulaAssets::SaveMaterialShadingUv(gMaterialInspectorPath2, shadingUvCombo - 1);
                }
                bool texOk = false;
                if (!texPath.empty())
                {
                    std::filesystem::path tp = std::filesystem::path(gProjectDir) / texPath;
                    texOk = std::filesystem::exists(tp);
                }
                ImGui::SameLine();
                ImGui::Text(texOk ? "OK" : "Missing");
            }
            else if (gNebTexInspectorOpen2 && !gNebTexInspectorPath2.empty())
            {
                ImGui::Text("Texture");
                ImGui::TextWrapped("%s", gNebTexInspectorPath2.filename().string().c_str());
                int texW = 0, texH = 0;
                if (NebulaAssets::ReadNebTexDimensions(gNebTexInspectorPath2, texW, texH))
                    ImGui::Text("Dimensions: %d x %d", texW, texH);

                GLuint previewTex = GetNebTexture(gNebTexInspectorPath2);
                if (previewTex != 0)
                {
                    ImGui::Text("Sample");
                    ImGui::Image((ImTextureID)(intptr_t)previewTex, ImVec2(128, 128));
                }

                int wrapMode = NebulaAssets::LoadNebTexWrapMode(gNebTexInspectorPath2);
                int saturnNpot = NebulaAssets::LoadNebTexSaturnNpotMode(gNebTexInspectorPath2);
                int filterMode = NebulaAssets::LoadNebTexFilterMode(gNebTexInspectorPath2);
                const char* wrapOptions[] = { "Repeat", "Extend" };
                const char* saturnNpotOptions[] = { "Pad", "Resample" };
                const char* filterOptions[] = { "Nearest", "Bilinear" };
                bool flipU = false, flipV = false;
                NebulaAssets::LoadNebTexFlipOptions(gNebTexInspectorPath2, flipU, flipV);
                bool changed = false;
                if (wrapMode > 1) wrapMode = 0;
                changed |= ImGui::Combo("Wrap Mode##B", &wrapMode, wrapOptions, IM_ARRAYSIZE(wrapOptions));
                changed |= ImGui::Combo("Filter##B", &filterMode, filterOptions, IM_ARRAYSIZE(filterOptions));
                changed |= ImGui::Combo("NPOT##B", &saturnNpot, saturnNpotOptions, IM_ARRAYSIZE(saturnNpotOptions));
                changed |= ImGui::Checkbox("Flip U##B", &flipU);
                changed |= ImGui::Checkbox("Flip V##B", &flipV);
                if (changed)
                {
                    NebulaAssets::SaveNebTexWrapMode(gNebTexInspectorPath2, wrapMode);
                    NebulaAssets::SaveNebTexFilterMode(gNebTexInspectorPath2, filterMode);
                    NebulaAssets::SaveNebTexSaturnNpotMode(gNebTexInspectorPath2, saturnNpot);
                    NebulaAssets::SaveNebTexFlipOptions(gNebTexInspectorPath2, flipU, flipV);
                    for (auto& kv : gNebTextureCache)
                    {
                        if (kv.second != 0) glDeleteTextures(1, &kv.second);
                    }
                    gNebTextureCache.clear();
                    gPreviewSaturnSampling = true; // make NPOT preview visible live
                    gViewportToast = "Texture compatibility updated (live)";
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
            }

                ImGui::EndChild();
                ImGui::End();
            }
        }
}
