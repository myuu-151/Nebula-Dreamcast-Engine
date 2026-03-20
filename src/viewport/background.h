#pragma once

#define NOMINMAX
#include <Windows.h>
#include <GL/gl.h>

// Checker overlay texture created on first call to DrawViewportBackground.
// Used by StaticMesh rendering for selection overlay.
extern GLuint gCheckerOverlayTex;

// Draw background gradient, procedural stars, nebula, grid and axes.
// themeMode: 0=Space, 1=Slate, 2=Classic, 3=Grey, else=Black.
void DrawViewportBackground(int themeMode, bool playMode);
