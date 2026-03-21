#pragma once

struct ImGuiViewport;

// Draw the Assets panel (bottom-left): asset browser, context menu, rename modal,
// FBX convert popup.
void DrawAssetsPanel(ImGuiViewport* vp, float topBarH, float leftPanelWidth, float assetsHeight);
