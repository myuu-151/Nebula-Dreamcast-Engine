#include "frame_loop.h"
#include "viewport_nav.h"
#include "hotkeys.h"
#include "editor_state.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#define NOMINMAX
#include <Windows.h>
#include <GL/gl.h>
#include <GLFW/glfw3.h>

#include "nodes/NodeTypes.h"
#include "runtime/script_compile.h"
#include "runtime/physics.h"
#include "io/anim_io.h"
#include "ui/toolbar.h"
#include "ui/assets_panel.h"
#include "ui/scene_outliner.h"
#include "ui/inspector.h"
#include "ui/scene_tabs.h"
#include "ui/mesh_inspector.h"
#include "ui/import_pipeline.h"
#include "vmu/vmu_tool_ui.h"
#include "viewport/viewport_render.h"
#include "viewport/viewport_transform.h"
#include "viewport/viewport_selection.h"
#include "viewport/node_gizmos.h"
#include "viewport/background.h"
#include "viewport/static_mesh_render.h"
#include "viewport/node_helpers.h"

std::vector<std::string> gPendingDroppedImports;

void TickEditorFrame(GLFWwindow* window, EditorViewportNav& nav, EditorFrameContext& ctx)
{
    glfwPollEvents();

    if (!gPendingDroppedImports.empty())
    {
        auto dropped = gPendingDroppedImports;
        gPendingDroppedImports.clear();
        ImportAssetsToCurrentFolder(dropped);
    }

    double now = glfwGetTime();
    float deltaTime = (float)(now - ctx.lastTime);
    ctx.lastTime = now;
    if (deltaTime > 0.1f) deltaTime = 0.1f;

    PollScriptHotReloadV1(now);
    if (gScriptCompileState.load() != 0)
        PollPlayScriptCompile();
    TickPlayScriptRuntime(deltaTime, now);

    TickEditorViewportNav(nav, window, deltaTime);

    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    ImGuiIO& io = ImGui::GetIO();

    if (gShowVmuTool)
    {
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        goto RenderImGuiOnly;
    }

    {
        float aspect = (float)display_w / (float)display_h;
        FrameCameraResult cam = EvaluateFrameCamera(nav, aspect, now);

        UpdateAudio3DNodes(cam.eye.x, cam.eye.y, cam.eye.z);

        TickTransformHotkeys(window, nav);

        // Node3D gravity + per-triangle ground collision + slope alignment in play mode.
        if (gPlayMode && gScriptCompileState.load() == 0)
        {
            float dt = io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f);
            if (dt > 0.1f) dt = 0.1f;
            TickPlayModePhysics(dt);
        }

        // Tick editor animation playback time
        if (gPlayMode)
        {
            float dt = io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f);
            for (auto& [idx, playing] : gEditorAnimPlaying)
            {
                if (!playing) continue;
                float speed = gEditorAnimSpeed.count(idx) ? gEditorAnimSpeed[idx] : 1.0f;
                gEditorAnimTime[idx] += dt * speed;
                int slot = gEditorAnimActiveSlot.count(idx) ? gEditorAnimActiveSlot[idx] : -1;
                if (slot >= 0 && idx >= 0 && idx < (int)gStaticMeshNodes.size() && slot < gStaticMeshNodes[idx].animSlotCount)
                {
                    std::filesystem::path ap = ResolveProjectAssetPath(gStaticMeshNodes[idx].animSlots[slot].path);
                    if (!ap.empty())
                    {
                        auto it = gStaticAnimClipCache.find(ap.generic_string());
                        if (it != gStaticAnimClipCache.end() && it->second.valid && it->second.frameCount > 0)
                        {
                            float fps = std::max(1.0f, it->second.fps);
                            int f = (int)std::floor(gEditorAnimTime[idx] * fps);
                            if (f >= (int)it->second.frameCount)
                            {
                                gEditorAnimFinished[idx] = true;
                                bool shouldLoop = gEditorAnimLoop.count(idx) ? gEditorAnimLoop[idx] : true;
                                if (shouldLoop)
                                {
                                    float dur = (float)it->second.frameCount / fps;
                                    if (dur > 0.0f) { while (gEditorAnimTime[idx] >= dur) gEditorAnimTime[idx] -= dur; }
                                    else gEditorAnimTime[idx] = 0.0f;
                                }
                                else
                                {
                                    gEditorAnimPlaying[idx] = false;
                                    gEditorAnimTime[idx] = (float)(it->second.frameCount - 1) / fps;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Transform interaction (unified handler for all node types)
        TickTransformInteraction(cam.forward, cam.up, cam.eye, io.MousePos.x, io.MousePos.y, ImGui::IsMouseClicked(0));

        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(cam.proj.m);
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(cam.view.m);

        // Background gradient, stars, nebula, grid, axes
        DrawViewportBackground(ctx.themeMode, gPlayMode);

        // Node gizmos (Audio3D spheres, Camera3D helpers, Node3D boxes, NavMesh3D bounds)
        DrawNodeGizmos(cam.activeCam);

        // StaticMesh3D rendering
        RenderStaticMeshNodes();
    }

RenderImGuiOnly:
    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    TickCtrlShortcuts();

    ImGuiViewport* vp = ImGui::GetMainViewport();
    DrawToolbar(window, nav, ctx.uiIconTex, ctx.showPreferences, ctx.uiScale, ctx.themeMode);

    const float leftPanelWidth = 220.0f * ImGui::GetIO().FontGlobalScale;
    const float rightPanelWidth = 260.0f * ImGui::GetIO().FontGlobalScale;
    const float topBarH = 28.0f * ImGui::GetIO().FontGlobalScale;
    const float leftPanelHeight = vp->Size.y - topBarH;
    const float assetsHeight = leftPanelHeight * 0.5f;

    // Viewport selection bounds
    float vpMinX = vp->Pos.x + leftPanelWidth;
    float vpMinY = vp->Pos.y + topBarH;
    float vpMaxX = vp->Pos.x + vp->Size.x - rightPanelWidth;
    float vpMaxY = vp->Pos.y + vp->Size.y;
    bool mouseInViewport = (io.MousePos.x >= vpMinX && io.MousePos.x <= vpMaxX &&
                            io.MousePos.y >= vpMinY && io.MousePos.y <= vpMaxY);

    if (ctx.showViewportDebugTab)
    {
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + 8.0f, vp->Pos.y + topBarH + 8.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::Begin("Debug", &ctx.showViewportDebugTab,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
        ImGui::Text("Viewport");
        ImGui::Text("min: %.1f, %.1f", vpMinX, vpMinY);
        ImGui::Text("max: %.1f, %.1f", vpMaxX, vpMaxY);
        ImGui::Text("size: %.1f x %.1f", vpMaxX - vpMinX, vpMaxY - vpMinY);
        ImGui::Separator();
        ImGui::Text("Editor Camera");
        ImGui::Text("pos: %.3f, %.3f, %.3f", gEye.x, gEye.y, gEye.z);
        ImGui::Text("view rot(yaw/pitch): %.2f, %.2f", nav.viewYaw, nav.viewPitch);
        ImGui::Text("orbit rot(yaw/pitch): %.2f, %.2f", nav.orbitYaw, nav.orbitPitch);
        ImGui::Text("orbit center: %.3f, %.3f, %.3f", nav.orbitCenter.x, nav.orbitCenter.y, nav.orbitCenter.z);
        ImGui::End();
    }

    TickViewportSelection(window, io.MousePos.x, io.MousePos.y,
                        io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y,
                        ImGui::IsMouseClicked(0) && mouseInViewport);
    DrawSceneOutliner(vp, topBarH, leftPanelWidth, leftPanelHeight, assetsHeight);

    DrawAssetsPanel(vp, topBarH, leftPanelWidth, assetsHeight);

    // Right panel: Inspector
    gViewYaw = nav.viewYaw;
    gViewPitch = nav.viewPitch;
    gViewDistance = nav.distance;
    gOrbitCenter = nav.orbitCenter;
    gDisplayW = display_w;
    gDisplayH = display_h;
    DrawInspectorPanel(vp, topBarH, rightPanelWidth);
    nav.viewYaw = gViewYaw;
    nav.viewPitch = gViewPitch;
    nav.distance = gViewDistance;
    nav.orbitCenter = gOrbitCenter;

    DrawSceneTabs(vp, topBarH, leftPanelWidth, rightPanelWidth);

    DrawVmuToolUI(vp);

    DrawNebMeshInspectorWindow(ImGui::GetIO().DeltaTime);

    // Script compile progress bar
    if (gScriptCompileState.load() == 1)
    {
        int done = gScriptCompileDone.load();
        int total = gScriptCompileTotal;
        float pct = (total > 0) ? (float)done / (float)total : 0.0f;

        char label[128];
        snprintf(label, sizeof(label), "Compiling scripts... %d / %d", done, total);
        ImVec2 textSize = ImGui::CalcTextSize(label);

        float barW = 300.0f;
        if (barW < textSize.x + 24.0f) barW = textSize.x + 24.0f;
        float barH = 32.0f;

        float vpCenterX = vp->Pos.x + leftPanelWidth + (vp->Size.x - leftPanelWidth - rightPanelWidth) * 0.5f;
        float vpCenterY = vp->Pos.y + topBarH + (vp->Size.y - topBarH) * 0.5f;
        ImVec2 pos(vpCenterX - barW * 0.5f, vpCenterY - barH * 0.5f);

        auto* dl = ImGui::GetForegroundDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + barW, pos.y + barH), IM_COL32(20, 20, 20, 220), 6.0f);
        float fillW = (barW - 4.0f) * pct;
        if (fillW > 0.0f)
            dl->AddRectFilled(ImVec2(pos.x + 2.0f, pos.y + 2.0f), ImVec2(pos.x + 2.0f + fillW, pos.y + barH - 2.0f), IM_COL32(80, 160, 255, 255), 4.0f);
        dl->AddRect(pos, ImVec2(pos.x + barW, pos.y + barH), IM_COL32(100, 100, 100, 200), 6.0f);
        dl->AddText(ImVec2(pos.x + (barW - textSize.x) * 0.5f, pos.y + (barH - textSize.y) * 0.5f), IM_COL32(255, 255, 255, 255), label);
    }

    // Save toast
    if (!gViewportToast.empty() && glfwGetTime() < gViewportToastUntil)
    {
        ImVec2 textSize = ImGui::CalcTextSize(gViewportToast.c_str());
        ImVec2 pos(vp->Pos.x + leftPanelWidth + 12.0f, vp->Pos.y + topBarH + 36.0f);
        ImGui::GetForegroundDrawList()->AddRectFilled(pos, ImVec2(pos.x + textSize.x + 12.0f, pos.y + textSize.y + 8.0f), IM_COL32(0, 0, 0, 180), 4.0f);
        ImGui::GetForegroundDrawList()->AddText(ImVec2(pos.x + 6.0f, pos.y + 4.0f), IM_COL32(255, 255, 255, 255), gViewportToast.c_str());
    }

    ImGui::Render();
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
}
