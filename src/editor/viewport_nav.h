#pragma once

#include "../math/math_types.h"

struct GLFWwindow;

// Editor viewport navigation state (orbit, pan, zoom, WASD roam).
// Separate from scene Camera3DNodes — this is the editor's own camera controller.
struct EditorViewportNav
{
    float orbitYaw = -139.75f;
    float orbitPitch = -14.3f;
    float viewYaw = -138.45f;
    float viewPitch = -12.1f;
    float distance = 3.2f;
    Vec3 orbitCenter = { 1.407f, 0.960f, 2.759f };

    // Internal drag state
    double lastX = 0.0, lastY = 0.0;
    bool dragging = false;
    bool rotating = false;
    bool viewLocked = true;
    float scrollDelta = 0.0f;

    // Play-mode snapshot (so Esc can restore editor camera)
    bool snapshotValid = false;
    float savedOrbitYaw = 0.0f;
    float savedOrbitPitch = 0.0f;
    float savedViewYaw = 0.0f;
    float savedViewPitch = 0.0f;
    float savedDistance = 10.0f;
    Vec3 savedOrbitCenter = { 0.0f, 0.0f, 0.0f };

    void Snapshot()
    {
        savedOrbitYaw = orbitYaw;
        savedOrbitPitch = orbitPitch;
        savedViewYaw = viewYaw;
        savedViewPitch = viewPitch;
        savedDistance = distance;
        savedOrbitCenter = orbitCenter;
        snapshotValid = true;
    }

    void Restore()
    {
        if (!snapshotValid) return;
        orbitYaw = savedOrbitYaw;
        orbitPitch = savedOrbitPitch;
        viewYaw = savedViewYaw;
        viewPitch = savedViewPitch;
        distance = savedDistance;
        orbitCenter = savedOrbitCenter;
    }

    // Compute eye position from current orbit state
    Vec3 ComputeEye() const;
};

// Handle MMB orbit, RMB rotate, scroll zoom, WASD roam.
// Call once per frame before computing view/projection matrices.
void TickEditorViewportNav(EditorViewportNav& nav, GLFWwindow* window, float deltaTime);
