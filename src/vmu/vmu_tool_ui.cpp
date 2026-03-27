#include "vmu_tool_ui.h"

#include "imgui.h"
#include <GLFW/glfw3.h>

#include <vector>
#include <string>
#include <array>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdint>

#include "vmu_tool.h"
#include "../editor/project.h"                     // gProjectDir, SetProjectVmuAnim, SetProjectVmuLoadOnBoot
#include "../io/texture_io.h"           // SaveVmuMonoPng
#include "../editor/file_dialogs.h"            // PickPngFileDialog, PickVmuFrameDataDialog

#include "../editor/editor_state.h"

// Static globals (were static in main.cpp, now static here)
static bool gVmuAnimPlaying = false;
static double gVmuAnimAccum = 0.0;
static bool gVmuDrawMode = false;
static bool gVmuStrokeActive = false;
static int gVmuLastDrawX = -1;
static int gVmuLastDrawY = -1;
static std::vector<std::array<uint8_t, 48 * 32>> gVmuUndoStack;

void DrawVmuToolUI(const ImGuiViewport* vp)
{
        if (gShowVmuTool)
        {
            const float topBarHLocal = 28.0f * ImGui::GetIO().FontGlobalScale;
            ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + topBarHLocal));
            ImGui::SetNextWindowSize(ImVec2(vp->Size.x, vp->Size.y - topBarHLocal));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.09f, 0.10f, 0.14f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.75f, 0.75f, 0.75f, 1));
            ImGui::Begin("##VmuToolSlate", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

            // Top border strip with left-side Save button.
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.94f, 0.94f, 0.94f, 1.0f));
            ImGui::BeginChild("##VmuToolTopStrip", ImVec2(0.0f, 34.0f * ImGui::GetIO().FontGlobalScale), true,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            if (ImGui::Button("Save"))
            {
                if (!gVmuHasImage)
                {
                    gViewportToast = "VMU Tool: no image to save";
                }
                else
                {
                    std::filesystem::path dstDir = std::filesystem::path(gProjectDir.empty() ? std::filesystem::current_path() : std::filesystem::path(gProjectDir)) / "Assets" / "VMU";
                    std::error_code ec;
                    std::filesystem::create_directories(dstDir, ec);

                    int idx = 1;
                    std::filesystem::path outPath;
                    do
                    {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "px%04d.png", idx++);
                        outPath = dstDir / buf;
                    } while (std::filesystem::exists(outPath, ec));

                    if (SaveVmuMonoPng(outPath, gVmuMono))
                    {
                        gVmuAssetPath = outPath.string();
                        gViewportToast = "VMU Tool: saved " + outPath.filename().string();
                    }
                    else
                    {
                        gViewportToast = "VMU Tool: save failed";
                    }
                }
                gViewportToastUntil = glfwGetTime() + 1.8;
            }
            ImGui::SameLine();
            if (ImGui::Button(gVmuLoadOnBoot ? "Load on boot: ON" : "Load on boot"))
            {
                if (!gVmuHasImage)
                {
                    gViewportToast = "VMU Tool: open a 48x32 PNG first";
                }
                else
                {
                    gVmuLoadOnBoot = !gVmuLoadOnBoot;
                    if (!gProjectDir.empty())
                        SetProjectVmuLoadOnBoot(std::filesystem::path(gProjectDir), gVmuLoadOnBoot);
                    gViewportToast = gVmuLoadOnBoot ? "VMU Tool: load-on-boot enabled" : "VMU Tool: load-on-boot disabled";
                }
                gViewportToastUntil = glfwGetTime() + 1.8;
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Asset Linked"))
            {
                bool loaded = false;

                // Prefer VMUAnim when one is currently loaded/linked.
                if (!gVmuLinkedAnimPath.empty() && (gVmuCurrentLoadedType == 2 || gVmuAnimPlaying))
                {
                    if (LoadVmuFrameData(gVmuLinkedAnimPath))
                    {
                        gViewportToast = "VMU Tool: loaded linked VMUAnim";
                    }
                    else
                    {
                        gViewportToast = "VMU Tool: linked VMUAnim missing/invalid";
                    }
                    loaded = true;
                }

                if (!loaded && gVmuAnimLayerSel >= 0 && gVmuAnimLayerSel < (int)gVmuAnimLayers.size())
                {
                    const VmuAnimLayer& l = gVmuAnimLayers[(size_t)gVmuAnimLayerSel];
                    if (!l.linkedAsset.empty())
                    {
                        gVmuLinkedPngPath = l.linkedAsset;
                        std::string err;
                        if (LoadVmuPngToMono(gVmuLinkedPngPath, err))
                        {
                            gViewportToast = "VMU Tool: loaded linked PNG";
                            loaded = true;
                        }
                        else
                        {
                            gViewportToast = err;
                            loaded = true;
                        }
                    }
                }

                if (!loaded && !gVmuLinkedAnimPath.empty())
                {
                    if (LoadVmuFrameData(gVmuLinkedAnimPath)) gViewportToast = "VMU Tool: loaded linked VMUAnim";
                    else gViewportToast = "VMU Tool: linked VMUAnim missing/invalid";
                    loaded = true;
                }

                if (!loaded && !gVmuAssetPath.empty())
                {
                    // Fallback: current loaded PNG can be used as a link source even before assigning to a layer.
                    gVmuLinkedPngPath = gVmuAssetPath;
                    std::string err;
                    if (LoadVmuPngToMono(gVmuLinkedPngPath, err)) gViewportToast = "VMU Tool: loaded current PNG";
                    else gViewportToast = err;
                    loaded = true;
                }
                if (!loaded)
                {
                    gViewportToast = "VMU Tool: no linked PNG/VMUAnim";
                }
                gViewportToastUntil = glfwGetTime() + 1.8;
            }
            // Drag-drop target: drop .vmuanim onto "Load Asset Linked" to link permanently
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VMU_ANIM_ASSET"))
                {
                    std::string droppedPath((const char*)payload->Data);
                    if (LoadVmuFrameData(droppedPath))
                    {
                        gVmuLinkedAnimPath = droppedPath;
                        gVmuCurrentLoadedType = 2;
                        // Persist to project Config.ini
                        if (!gProjectDir.empty())
                            SetProjectVmuAnim(std::filesystem::path(gProjectDir), droppedPath);
                        gViewportToast = "VMU Tool: linked VMUAnim (saved to project)";
                    }
                    else
                    {
                        gViewportToast = "VMU Tool: failed to load dropped VMUAnim";
                    }
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
                ImGui::EndDragDropTarget();
            }
            // Right-click to unlink
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !gVmuLinkedAnimPath.empty())
            {
                gVmuLinkedAnimPath.clear();
                if (!gProjectDir.empty())
                    SetProjectVmuAnim(std::filesystem::path(gProjectDir), "");
                gViewportToast = "VMU Tool: unlinked VMUAnim";
                gViewportToastUntil = glfwGetTime() + 1.8;
            }
            ImGui::SameLine();
            if (ImGui::Button(gVmuDrawMode ? "Draw: ON" : "Draw"))
            {
                gVmuDrawMode = !gVmuDrawMode;
                if (gVmuDrawMode && !gVmuHasImage)
                {
                    gVmuHasImage = true;
                    gVmuMono.fill(0);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Save FrameData"))
            {
                std::filesystem::path dstDir = std::filesystem::path(gProjectDir.empty() ? std::filesystem::current_path() : std::filesystem::path(gProjectDir)) / "Assets" / "VMU";
                std::error_code ec;
                std::filesystem::create_directories(dstDir, ec);
                int idx = 1;
                std::filesystem::path outPath;
                do
                {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "framedata%04d.vmuanim", idx++);
                    outPath = dstDir / buf;
                } while (std::filesystem::exists(outPath, ec));

                if (SaveVmuFrameData(outPath)) gViewportToast = "VMU Tool: saved " + outPath.filename().string();
                else gViewportToast = "VMU Tool: save framedata failed";
                gViewportToastUntil = glfwGetTime() + 1.8;
            }
            ImGui::SameLine();
            if (ImGui::Button("Load FrameData"))
            {
                std::string picked = PickVmuFrameDataDialog("Load VMU Frame Data");
                if (!picked.empty())
                {
                    if (LoadVmuFrameData(picked))
                    {
                        gVmuLinkedAnimPath = picked;
                        gViewportToast = "VMU Tool: frame data loaded";
                    }
                    else gViewportToast = "VMU Tool: load framedata failed";
                    gViewportToastUntil = glfwGetTime() + 1.8;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Close"))
            {
                gVmuAnimLayers = { {"Layer 1", true, 0, 0, ""} };
                gVmuAnimLayerSel = 0;
                gVmuAnimTotalFrames = 24;
                gVmuAnimPlayhead = 0;
                gVmuHasImage = true;
                gVmuMono.fill(0);
                gVmuAssetPath.clear();
                gVmuLinkedPngPath.clear();
                gVmuLinkedAnimPath.clear();
                gVmuCurrentLoadedType = 0;
                gViewportToast = "VMU Tool: frame/layers cleared";
                gViewportToastUntil = glfwGetTime() + 1.5;
            }
            float closeW = ImGui::GetFrameHeight();
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - closeW - ImGui::GetStyle().FramePadding.x * 2.0f - 6.0f);
            if (ImGui::Button("X", ImVec2(closeW, 0.0f)))
            {
                gShowVmuTool = false;
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();

            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false) && !gVmuUndoStack.empty())
            {
                gVmuMono = gVmuUndoStack.back();
                gVmuUndoStack.pop_back();
                gVmuHasImage = true;
                gViewportToast = "VMU Tool: undo";
                gViewportToastUntil = glfwGetTime() + 1.0;
            }

            ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.x > 10.0f && avail.y > 10.0f)
            {
                const float leftW = avail.x * 0.45f;
                const float splitGap = 10.0f;
                const float rightW = avail.x - leftW - splitGap;

                // Left: upper-half asset browser area.
                ImGui::BeginChild("##VmuLeftPane", ImVec2(leftW, avail.y), false);
                ImGui::BeginChild("##VmuAssetBrowser", ImVec2(0.0f, avail.y * 0.5f), true);
                if (ImGui::BeginTabBar("##VmuAssetTabs"))
                {
                    if (ImGui::BeginTabItem("Asset Browser"))
                    {
                        if (ImGui::Button("Image..."))
                        {
                            std::string picked = PickPngFileDialog("Import VMU PNG (48x32)");
                            if (!picked.empty())
                            {
                                std::string err;
                                if (LoadVmuPngToMono(picked, err))
                                {
                                    std::filesystem::path dstDir = std::filesystem::path(gProjectDir.empty() ? std::filesystem::current_path() : std::filesystem::path(gProjectDir)) / "Assets" / "VMU";
                                    std::error_code ec;
                                    std::filesystem::create_directories(dstDir, ec);
                                    std::filesystem::path srcPath(picked);
                                    std::filesystem::path outPath = dstDir / srcPath.filename();
                                    if (std::filesystem::exists(outPath, ec))
                                    {
                                        std::string stem = outPath.stem().string();
                                        std::string ext = outPath.extension().string();
                                        int n = 1;
                                        while (std::filesystem::exists(outPath, ec))
                                        {
                                            outPath = dstDir / (stem + "_" + std::to_string(n++) + ext);
                                        }
                                    }
                                    std::filesystem::copy_file(srcPath, outPath, std::filesystem::copy_options::overwrite_existing, ec);
                                    if (!ec)
                                    {
                                        gVmuAssetPath = outPath.string();
                                        gViewportToast = "VMU Tool: image imported to Assets/VMU";
                                    }
                                    else
                                    {
                                        gViewportToast = "VMU Tool: import failed";
                                    }
                                }
                                else
                                {
                                    gViewportToast = err;
                                }
                                gViewportToastUntil = glfwGetTime() + 2.0;
                            }
                        }
                        ImGui::Separator();

                        std::filesystem::path assetDir = std::filesystem::path(gProjectDir.empty() ? std::filesystem::current_path() : std::filesystem::path(gProjectDir)) / "Assets" / "VMU";
                        std::error_code ec;
                        if (std::filesystem::exists(assetDir, ec))
                        {
                            for (const auto& e : std::filesystem::directory_iterator(assetDir, ec))
                            {
                                if (ec || !e.is_regular_file()) continue;
                                std::filesystem::path p = e.path();
                                std::string ext = p.extension().string();
                                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                                if (ext != ".png" && ext != ".vmuanim") continue;
                                bool selected = (!gVmuAssetPath.empty() && std::filesystem::path(gVmuAssetPath) == p);
                                if (ImGui::Selectable(p.filename().string().c_str(), selected))
                                {
                                    if (ext == ".vmuanim")
                                    {
                                        if (LoadVmuFrameData(p))
                                        {
                                            gVmuLinkedAnimPath = p.string();
                                            gVmuCurrentLoadedType = 2;
                                            // Persist link to project
                                            if (!gProjectDir.empty())
                                                SetProjectVmuAnim(std::filesystem::path(gProjectDir), p.string());
                                            gViewportToast = "VMU Tool: frame data loaded (linked)";
                                        }
                                        else gViewportToast = "VMU Tool: load framedata failed";
                                    }
                                    else
                                    {
                                        std::string err;
                                        if (LoadVmuPngToMono(p.string(), err))
                                        {
                                            gVmuLinkedPngPath = p.string(); // treat selected PNG as current linked source
                                            gViewportToast = "VMU Tool: loaded " + p.filename().string();
                                        }
                                        else gViewportToast = err;
                                    }
                                    gViewportToastUntil = glfwGetTime() + 2.0;
                                }
                                if (ext == ".png" && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                                {
                                    std::string dragPath = p.string();
                                    ImGui::SetDragDropPayload("VMU_PNG_ASSET", dragPath.c_str(), dragPath.size() + 1);
                                    ImGui::TextUnformatted(p.filename().string().c_str());
                                    ImGui::EndDragDropSource();
                                }
                                if (ext == ".vmuanim" && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                                {
                                    std::string dragPath = p.string();
                                    ImGui::SetDragDropPayload("VMU_ANIM_ASSET", dragPath.c_str(), dragPath.size() + 1);
                                    ImGui::TextUnformatted(p.filename().string().c_str());
                                    ImGui::EndDragDropSource();
                                }
                                if (ImGui::BeginPopupContextItem((std::string("##vmuAssetCtx_") + p.filename().string()).c_str()))
                                {
                                    if (ImGui::MenuItem("Delete"))
                                    {
                                        std::error_code dec;
                                        bool removed = std::filesystem::remove(p, dec);
                                        if (removed)
                                        {
                                            if (!gVmuAssetPath.empty() && std::filesystem::path(gVmuAssetPath) == p)
                                                gVmuAssetPath.clear();
                                            gViewportToast = "VMU Tool: deleted " + p.filename().string();
                                        }
                                        else
                                        {
                                            gViewportToast = "VMU Tool: delete failed";
                                        }
                                        gViewportToastUntil = glfwGetTime() + 1.8;
                                    }
                                    ImGui::EndPopup();
                                }
                            }
                        }
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
                ImGui::EndChild();

                ImGui::BeginChild("##VmuLayerPanel", ImVec2(0.0f, 0.0f), true);

                if (ImGui::BeginTabBar("##VmuTimelineTabs"))
                {
                    ImGui::TabItemButton("TIMELINE", ImGuiTabItemFlags_Leading);
                    ImGui::TabItemButton("MOTION EDITOR");
                    ImGui::EndTabBar();
                }

                ImGui::SetNextItemWidth(90.0f);
                ImGui::InputInt("Frames", &gVmuAnimTotalFrames);
                if (gVmuAnimTotalFrames < 1) gVmuAnimTotalFrames = 1;
                if (gVmuAnimPlayhead < 0) gVmuAnimPlayhead = 0;
                if (gVmuAnimPlayhead >= gVmuAnimTotalFrames) gVmuAnimPlayhead = gVmuAnimTotalFrames - 1;
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90.0f);
                ImGui::SliderInt("Playhead", &gVmuAnimPlayhead, 0, gVmuAnimTotalFrames - 1);
                ImGui::SameLine();
                if (ImGui::Button("+ Layer"))
                {
                    VmuAnimLayer l;
                    l.name = "Layer " + std::to_string((int)gVmuAnimLayers.size() + 1);
                    l.visible = true;
                    l.frameStart = 0;
                    l.frameEnd = gVmuAnimTotalFrames - 1;
                    gVmuAnimLayers.push_back(l);
                    gVmuAnimLayerSel = (int)gVmuAnimLayers.size() - 1;
                }
                ImGui::SameLine();
                if (ImGui::Button("- Layer") && !gVmuAnimLayers.empty())
                {
                    if (gVmuAnimLayerSel < 0) gVmuAnimLayerSel = 0;
                    if (gVmuAnimLayerSel >= (int)gVmuAnimLayers.size()) gVmuAnimLayerSel = (int)gVmuAnimLayers.size() - 1;
                    gVmuAnimLayers.erase(gVmuAnimLayers.begin() + gVmuAnimLayerSel);
                    if (gVmuAnimLayers.empty()) gVmuAnimLayerSel = -1;
                    else if (gVmuAnimLayerSel >= (int)gVmuAnimLayers.size()) gVmuAnimLayerSel = (int)gVmuAnimLayers.size() - 1;
                }

                // Right-side playback controls.
                float speedBtnW = 140.0f;
                float loopBtnW = 110.0f;
                float playBtnW = 70.0f;
                float groupW = playBtnW + loopBtnW + speedBtnW + 24.0f;
                float groupX = ImGui::GetWindowContentRegionMax().x - groupW - 8.0f;
                if (groupX > ImGui::GetCursorPosX())
                {
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(groupX);
                }

                bool togglePlay = false;
                if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space, false))
                    togglePlay = true;
                if (ImGui::Button(gVmuAnimPlaying ? "Stop" : "Play", ImVec2(playBtnW, 0.0f)))
                    togglePlay = true;
                if (togglePlay)
                {
                    gVmuAnimPlaying = !gVmuAnimPlaying;
                    gVmuAnimAccum = 0.0;
                    if (gVmuAnimPlaying) gVmuAnimPlayhead = 0;
                }

                ImGui::SameLine();
                if (ImGui::Button(gVmuAnimLoop ? "Loop ON" : "Loop OFF", ImVec2(loopBtnW, 0.0f)))
                {
                    gVmuAnimLoop = !gVmuAnimLoop;
                    gViewportToast = gVmuAnimLoop ? "VMU Tool: loop ON" : "VMU Tool: loop OFF";
                    gViewportToastUntil = glfwGetTime() + 1.2;
                }

                ImGui::SameLine();
                const char* speedLabel = (gVmuAnimSpeedMode == 0) ? "Speed x1" : (gVmuAnimSpeedMode == 1) ? "Speed x2" : (gVmuAnimSpeedMode == 2) ? "Speed x3" : "Speed x4";
                if (ImGui::Button(speedLabel, ImVec2(speedBtnW, 0.0f)))
                {
                    gVmuAnimSpeedMode = (gVmuAnimSpeedMode + 1) % 4;
                }

                if (gVmuAnimPlaying)
                {
                    const double baseFps = 8.0;
                    const double mult = (gVmuAnimSpeedMode == 0) ? 0.5 : (gVmuAnimSpeedMode == 1) ? 1.0 : (gVmuAnimSpeedMode == 2) ? 1.5 : 2.0;
                    const double frameStep = 1.0 / (baseFps * mult);
                    gVmuAnimAccum += ImGui::GetIO().DeltaTime;
                    while (gVmuAnimAccum >= frameStep)
                    {
                        gVmuAnimAccum -= frameStep;
                        gVmuAnimPlayhead++;
                        if (gVmuAnimPlayhead >= gVmuAnimTotalFrames)
                        {
                            gVmuAnimPlayhead = 0; // snap back to frame 1
                            if (!gVmuAnimLoop)
                            {
                                gVmuAnimPlaying = false;
                                break;
                            }
                        }
                    }
                }

                ImGui::Separator();
                ImVec2 tAvail = ImGui::GetContentRegionAvail();
                const float leftColsW = 120.0f;
                const float timelineH = 46.0f;
                const float rowH = 24.0f;

                ImDrawList* tdl = ImGui::GetWindowDrawList();
                ImVec2 origin = ImGui::GetCursorScreenPos();
                float timelineX = origin.x + leftColsW;
                float timelineW = tAvail.x - leftColsW;
                if (timelineW < 60.0f) timelineW = 60.0f;
                float frameW = timelineW / (float)gVmuAnimTotalFrames;

                tdl->AddRectFilled(origin, ImVec2(origin.x + tAvail.x, origin.y + timelineH), IM_COL32(220, 220, 220, 255));
                tdl->AddRect(origin, ImVec2(origin.x + tAvail.x, origin.y + timelineH), IM_COL32(140, 140, 140, 255));
                for (int f = 0; f <= gVmuAnimTotalFrames; ++f)
                {
                    float x = timelineX + frameW * (float)f;
                    int major = (f % 5 == 0) ? 1 : 0;
                    tdl->AddLine(ImVec2(x, origin.y + (major ? 12.0f : 20.0f)), ImVec2(x, origin.y + timelineH), IM_COL32(150, 150, 150, 255));
                    if (major && f > 0)
                    {
                        char buf[16]; snprintf(buf, sizeof(buf), "%d", f);
                        tdl->AddText(ImVec2(x + 2.0f, origin.y + 1.0f), IM_COL32(40, 40, 40, 255), buf);
                    }
                }
                float phx = timelineX + frameW * (float)gVmuAnimPlayhead;
                tdl->AddLine(ImVec2(phx, origin.y), ImVec2(phx, origin.y + tAvail.y), IM_COL32(220, 30, 30, 255), 2.0f);

                ImGui::Dummy(ImVec2(tAvail.x, timelineH));

                for (int i = 0; i < (int)gVmuAnimLayers.size(); ++i)
                {
                    VmuAnimLayer& l = gVmuAnimLayers[(size_t)i];
                    ImVec2 rowPos = ImGui::GetCursorScreenPos();
                    ImGui::InvisibleButton((std::string("##layerRow") + std::to_string(i)).c_str(), ImVec2(tAvail.x, rowH));
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VMU_PNG_ASSET"))
                        {
                            const char* dropped = (const char*)payload->Data;
                            if (dropped && dropped[0])
                            {
                                l.linkedAsset = dropped;
                                gVmuLinkedPngPath = l.linkedAsset;
                                gViewportToast = "VMU Tool: linked PNG to layer";
                                gViewportToastUntil = glfwGetTime() + 1.5;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    bool rowSel = (gVmuAnimLayerSel == i);
                    if (ImGui::IsItemClicked())
                    {
                        gVmuAnimLayerSel = i;
                        if (!l.linkedAsset.empty())
                        {
                            std::string err;
                            if (!LoadVmuPngToMono(l.linkedAsset, err))
                            {
                                gViewportToast = err;
                                gViewportToastUntil = glfwGetTime() + 1.5;
                            }
                        }
                        else
                        {
                            gVmuHasImage = true;
                            gVmuMono.fill(0);
                        }
                    }

                    tdl->AddRectFilled(ImVec2(rowPos.x + 24.0f, rowPos.y), ImVec2(rowPos.x + tAvail.x, rowPos.y + rowH), rowSel ? IM_COL32(170, 205, 240, 180) : IM_COL32(235, 235, 235, 255));
                    tdl->AddRect(rowPos, ImVec2(rowPos.x + tAvail.x, rowPos.y + rowH), IM_COL32(170, 170, 170, 255));
                    ImGui::SetCursorScreenPos(ImVec2(rowPos.x + 2.0f, rowPos.y + 2.0f));
                    ImGui::PushID(i);
                    if (ImGui::SmallButton("->"))
                    {
                        if (gVmuCurrentLoadedType == 2 && !gVmuLinkedAnimPath.empty())
                        {
                            // VMUAnim link is project-persistent (global animation source for runtime/editor playback).
                            gViewportToast = "VMU Tool: linked VMUAnim";
                        }
                        else if (!gVmuAssetPath.empty())
                        {
                            l.linkedAsset = gVmuAssetPath;
                            gVmuLinkedPngPath = gVmuAssetPath;
                            gViewportToast = "VMU Tool: linked PNG to layer";
                        }
                        else
                        {
                            gViewportToast = "VMU Tool: load a PNG or VMUAnim first";
                        }
                        gViewportToastUntil = glfwGetTime() + 1.5;
                    }
                    ImGui::PopID();
                    tdl->AddText(ImVec2(rowPos.x + 26.0f, rowPos.y + 4.0f), IM_COL32(30, 30, 30, 255), l.name.c_str());

                    int fs = std::max(0, std::min(gVmuAnimTotalFrames - 1, l.frameStart));
                    int fe = std::max(fs, std::min(gVmuAnimTotalFrames - 1, l.frameEnd));
                    float x0 = timelineX + frameW * (float)fs;
                    float x1 = timelineX + frameW * (float)(fe + 1);
                    tdl->AddRectFilled(ImVec2(x0, rowPos.y + 3.0f), ImVec2(x1, rowPos.y + rowH - 3.0f), IM_COL32(80, 220, 80, 255));
                    tdl->AddRect(ImVec2(x0, rowPos.y + 3.0f), ImVec2(x1, rowPos.y + rowH - 3.0f), IM_COL32(20, 120, 20, 255));
                    if (!l.linkedAsset.empty())
                    {
                        tdl->AddRectFilled(ImVec2(x0 + 2.0f, rowPos.y + 6.0f), ImVec2(x0 + 6.0f, rowPos.y + rowH - 6.0f), IM_COL32(30, 30, 30, 255));
                    }
                }

                if (gVmuAnimLayerSel >= 0 && gVmuAnimLayerSel < (int)gVmuAnimLayers.size())
                {
                    VmuAnimLayer& l = gVmuAnimLayers[(size_t)gVmuAnimLayerSel];
                    ImGui::Separator();
                    char nameBuf[64] = {};
                    strncpy(nameBuf, l.name.c_str(), sizeof(nameBuf) - 1);
                    if (ImGui::InputText("Layer Name", nameBuf, sizeof(nameBuf))) l.name = nameBuf;
                    ImGui::Checkbox("Visible", &l.visible);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80.0f);
                    ImGui::InputInt("Start", &l.frameStart);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80.0f);
                    ImGui::InputInt("End", &l.frameEnd);
                    if (l.frameStart < 0) l.frameStart = 0;
                    if (l.frameEnd < l.frameStart) l.frameEnd = l.frameStart;
                    if (l.frameEnd >= gVmuAnimTotalFrames) l.frameEnd = gVmuAnimTotalFrames - 1;
                }

                ImGui::EndChild();
                ImGui::EndChild();

                // Auto-switch preview based on active layer range at current playhead.
                std::string activeLayerAsset;
                for (int li = 0; li < (int)gVmuAnimLayers.size(); ++li)
                {
                    const VmuAnimLayer& l = gVmuAnimLayers[(size_t)li];
                    if (!l.visible || l.linkedAsset.empty()) continue;
                    if (gVmuAnimPlayhead < l.frameStart || gVmuAnimPlayhead > l.frameEnd) continue;
                    activeLayerAsset = l.linkedAsset;
                }
                if (!activeLayerAsset.empty())
                {
                    if (activeLayerAsset != gVmuAssetPath)
                    {
                        std::string err;
                        LoadVmuPngToMono(activeLayerAsset, err);
                    }
                }
                else if (!gVmuDrawMode && gVmuCurrentLoadedType == 2)
                {
                    // No active VMUAnim layer at this frame: clear preview grid only for VMUAnim mode.
                    // PNG mode should keep showing the selected PNG.
                    gVmuHasImage = true;
                    gVmuMono.fill(0);
                }

                ImGui::SameLine(0.0f, splitGap);

                // Right: 48x32 preview grid.
                ImGui::BeginChild("##VmuGridPane", ImVec2(rightW, avail.y), false);
                ImVec2 ravail = ImGui::GetContentRegionAvail();
                const float cols = 48.0f;
                const float rows = 32.0f;
                float cell = (ravail.x / cols < ravail.y / rows) ? (ravail.x / cols) : (ravail.y / rows);
                if (cell < 2.0f) cell = 2.0f;

                ImVec2 start = ImGui::GetCursorScreenPos();
                float gridW = cols * cell;
                float gridH = rows * cell;
                start.x += (ravail.x - gridW);
                start.y += (ravail.y - gridH) * 0.5f;

                if (gVmuDrawMode)
                {
                    ImVec2 m = ImGui::GetIO().MousePos;
                    bool inGrid = (m.x >= start.x && m.y >= start.y && m.x < start.x + gridW && m.y < start.y + gridH);
                    bool mouseHeld = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right);
                    if (inGrid && mouseHeld && !gVmuStrokeActive)
                    {
                        gVmuUndoStack.push_back(gVmuMono);
                        if (gVmuUndoStack.size() > 64) gVmuUndoStack.erase(gVmuUndoStack.begin());
                        gVmuStrokeActive = true;
                    }
                    if (!mouseHeld)
                    {
                        gVmuStrokeActive = false;
                        gVmuLastDrawX = -1;
                        gVmuLastDrawY = -1;
                    }
                    if (inGrid && mouseHeld)
                    {
                        int px = (int)((m.x - start.x) / cell);
                        int py = (int)((m.y - start.y) / cell);
                        if (px >= 0 && px < 48 && py >= 0 && py < 32)
                        {
                            gVmuHasImage = true;
                            const uint8_t pen = ImGui::IsMouseDown(ImGuiMouseButton_Right) ? 0 : 1;
                            if (gVmuLastDrawX >= 0 && gVmuLastDrawY >= 0)
                            {
                                int x0 = gVmuLastDrawX, y0 = gVmuLastDrawY;
                                int x1 = px, y1 = py;
                                int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
                                int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
                                int err = dx + dy;
                                while (true)
                                {
                                    if (x0 >= 0 && x0 < 48 && y0 >= 0 && y0 < 32)
                                        gVmuMono[(size_t)y0 * 48u + (size_t)x0] = pen;
                                    if (x0 == x1 && y0 == y1) break;
                                    int e2 = err * 2;
                                    if (e2 >= dy) { err += dy; x0 += sx; }
                                    if (e2 <= dx) { err += dx; y0 += sy; }
                                }
                            }
                            else
                            {
                                gVmuMono[(size_t)py * 48u + (size_t)px] = pen;
                            }
                            gVmuLastDrawX = px;
                            gVmuLastDrawY = py;
                        }
                    }
                }

                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImU32 bgCol = IM_COL32(255, 255, 255, 255);
                ImU32 borderCol = IM_COL32(70, 70, 70, 255);
                ImU32 lineCol = IM_COL32(185, 185, 185, 255);
                ImU32 pixCol = IM_COL32(20, 20, 20, 255);
                dl->AddRectFilled(start, ImVec2(start.x + gridW, start.y + gridH), bgCol);
                if (gVmuHasImage)
                {
                    for (int py = 0; py < 32; ++py)
                    {
                        for (int px = 0; px < 48; ++px)
                        {
                            if (gVmuMono[(size_t)py * 48u + (size_t)px])
                            {
                                float x0 = start.x + px * cell;
                                float y0 = start.y + py * cell;
                                dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + cell, y0 + cell), pixCol);
                            }
                        }
                    }
                }
                dl->AddRect(start, ImVec2(start.x + gridW, start.y + gridH), borderCol, 0.0f, 0, 2.0f);
                for (int x = 1; x < 48; ++x)
                {
                    float px = start.x + x * cell;
                    dl->AddLine(ImVec2(px, start.y), ImVec2(px, start.y + gridH), lineCol, 1.0f);
                }
                for (int y = 1; y < 32; ++y)
                {
                    float py = start.y + y * cell;
                    dl->AddLine(ImVec2(start.x, py), ImVec2(start.x + gridW, py), lineCol, 1.0f);
                }
                ImGui::EndChild();
            }

            ImGui::End();
            ImGui::PopStyleColor(2);
        }
}
