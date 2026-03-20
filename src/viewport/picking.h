#pragma once

struct GLFWwindow;

// Run viewport click-picking for all node types.
// mouseX/mouseY: current mouse position, scaleX/scaleY: framebuffer scale.
// mouseClicked: true if LMB was just pressed this frame.
void TickViewportPicking(GLFWwindow* window, float mouseX, float mouseY,
                         float scaleX, float scaleY, bool mouseClicked);
