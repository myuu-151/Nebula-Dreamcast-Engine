#pragma once

struct GLFWwindow;

// Run viewport click-selection for all node types.
// mouseX/mouseY: current mouse position, scaleX/scaleY: framebuffer scale.
// mouseClicked: true if LMB was just pressed this frame.
void TickViewportSelection(GLFWwindow* window, float mouseX, float mouseY,
                           float scaleX, float scaleY, bool mouseClicked);
