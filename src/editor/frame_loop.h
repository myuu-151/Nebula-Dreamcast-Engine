#pragma once

#include <string>
#include <vector>

struct GLFWwindow;
struct EditorViewportNav;

// Per-session mutable state needed by the frame loop.
struct EditorFrameContext
{
    double lastTime = 0.0;
    bool showPreferences = false;
    bool showViewportDebugTab = false;
    float uiScale = 2.0f;
    int themeMode = 0;
    unsigned int uiIconTex = 0;
};

// Pending drag-and-drop imports (populated by GLFW drop callback, consumed each frame).
extern std::vector<std::string> gPendingDroppedImports;

// Run one editor frame: input, simulation, rendering, UI.
void TickEditorFrame(GLFWwindow* window, EditorViewportNav& nav, EditorFrameContext& ctx);

// Install GLFW drop callback that populates gPendingDroppedImports.
void InstallDropCallback(GLFWwindow* window);
