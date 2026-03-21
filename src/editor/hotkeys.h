#pragma once

struct GLFWwindow;
struct EditorViewportNav;

// Process GLFW-level transform hotkeys (G/R/S/X/Y/Z), Esc (play-mode stop +
// transform cancel), and Delete (node deletion).
// Call once per frame BEFORE ImGui::NewFrame().
void TickTransformHotkeys(GLFWwindow* window, EditorViewportNav& nav);

// Process Ctrl shortcuts (undo/redo, save, copy/paste).
// Call once per frame AFTER ImGui::NewFrame().
void TickCtrlShortcuts();
