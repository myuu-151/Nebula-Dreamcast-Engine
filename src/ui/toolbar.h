#pragma once

#include <cstdint>

struct GLFWwindow;
struct EditorViewportNav;

// Draw the top toolbar (File/Edit/VMU/Package/Play/Wireframe buttons),
// window controls (min/max/close), quit confirmation, and window drag.
// Returns the toolbar height in pixels.
float DrawToolbar(GLFWwindow* window, EditorViewportNav& nav,
                  unsigned int uiIconTex, bool& showPreferences,
                  float& uiScale, int& themeMode);
