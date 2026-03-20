#pragma once

struct GLFWwindow;

// Play-mode fallback controls when script runtime is unavailable.
// Handles WASD movement, IJKL camera look, camera-relative 8-way movement,
// and CameraPivot snapping for PlayerRoot.
void TickFallbackControls(GLFWwindow* window, float deltaTime, bool navKeyAllowed);
