#include "viewport_nav.h"

#include <cmath>
#include <GLFW/glfw3.h>
#include "imgui.h"

Vec3 EditorViewportNav::ComputeEye() const
{
    float yawRad = orbitYaw * 3.14159f / 180.0f;
    float pitchRad = orbitPitch * 3.14159f / 180.0f;
    return {
        orbitCenter.x - distance * cosf(pitchRad) * cosf(yawRad),
        orbitCenter.y - distance * sinf(pitchRad),
        orbitCenter.z - distance * cosf(pitchRad) * sinf(yawRad)
    };
}

void TickEditorViewportNav(EditorViewportNav& nav, GLFWwindow* window, float deltaTime)
{
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    int mmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE);
    int rmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);

    const float orbitSensitivity = 0.25f;
    const float rotateSensitivity = 0.10f;

    bool navMouseAllowed = !ImGui::GetIO().WantCaptureMouse;
    bool navKeyAllowed = !ImGui::GetIO().WantCaptureKeyboard;

    // MMB: orbit around center
    if (mmb == GLFW_PRESS && navMouseAllowed)
    {
        if (!nav.dragging)
        {
            nav.dragging = true;
            nav.lastX = mx;
            nav.lastY = my;

            // lock orbit center to current view target before orbiting
            float viewYawRad = nav.viewYaw * 3.14159f / 180.0f;
            float viewPitchRad = nav.viewPitch * 3.14159f / 180.0f;
            Vec3 forward = {
                cosf(viewPitchRad) * cosf(viewYawRad),
                sinf(viewPitchRad),
                cosf(viewPitchRad) * sinf(viewYawRad)
            };
            Vec3 eye = nav.ComputeEye();
            nav.orbitCenter = {
                eye.x + forward.x * nav.distance,
                eye.y + forward.y * nav.distance,
                eye.z + forward.z * nav.distance
            };
            nav.orbitYaw = nav.viewYaw;
            nav.orbitPitch = nav.viewPitch;
        }
        else
        {
            double dx = mx - nav.lastX;
            double dy = my - nav.lastY;
            nav.lastX = mx;
            nav.lastY = my;

            nav.orbitYaw   -= (float)dx * orbitSensitivity;
            nav.orbitPitch -= (float)dy * orbitSensitivity;
            if (nav.orbitPitch > 89.0f) nav.orbitPitch = 89.0f;
            if (nav.orbitPitch < -89.0f) nav.orbitPitch = -89.0f;

            // keep view aligned to orbit (look at orbit center)
            nav.viewLocked = true;
            nav.viewYaw = nav.orbitYaw;
            nav.viewPitch = nav.orbitPitch;
        }
    }
    else
    {
        nav.dragging = false;
    }

    // RMB: rotate in place (free look)
    if (rmb == GLFW_PRESS && navMouseAllowed)
    {
        if (!nav.rotating)
        {
            nav.rotating = true;
            nav.lastX = mx;
            nav.lastY = my;
        }
        else
        {
            double dx = mx - nav.lastX;
            double dy = my - nav.lastY;
            nav.lastX = mx;
            nav.lastY = my;

            nav.viewLocked = false;
            nav.viewYaw   -= (float)dx * rotateSensitivity;
            nav.viewPitch -= (float)dy * rotateSensitivity;
            if (nav.viewPitch > 89.0f) nav.viewPitch = 89.0f;
            if (nav.viewPitch < -89.0f) nav.viewPitch = -89.0f;
        }
    }
    else
    {
        nav.rotating = false;
    }

    // Scroll: dolly along view direction
    if (nav.scrollDelta != 0.0f && navMouseAllowed)
    {
        float viewYawRad = nav.viewYaw * 3.14159f / 180.0f;
        float viewPitchRad = nav.viewPitch * 3.14159f / 180.0f;
        Vec3 forward = {
            cosf(viewPitchRad) * cosf(viewYawRad),
            sinf(viewPitchRad),
            cosf(viewPitchRad) * sinf(viewYawRad)
        };
        float move = nav.scrollDelta * 0.5f;
        nav.orbitCenter.x += forward.x * move;
        nav.orbitCenter.y += forward.y * move;
        nav.orbitCenter.z += forward.z * move;
        nav.scrollDelta = 0.0f;
    }

    // WASD roaming (only while RMB held)
    if (rmb == GLFW_PRESS && navMouseAllowed && navKeyAllowed)
    {
        float moveSpeed = 5.0f; // units per second
        float move = moveSpeed * deltaTime;

        float yawRad = nav.viewYaw * 3.14159f / 180.0f;
        Vec3 forwardXZ = { cosf(yawRad), 0.0f, sinf(yawRad) };
        Vec3 rightXZ = { -sinf(yawRad), 0.0f, cosf(yawRad) };

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        {
            nav.orbitCenter.x += forwardXZ.x * move;
            nav.orbitCenter.z += forwardXZ.z * move;
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        {
            nav.orbitCenter.x -= forwardXZ.x * move;
            nav.orbitCenter.z -= forwardXZ.z * move;
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        {
            nav.orbitCenter.x += rightXZ.x * move;
            nav.orbitCenter.z += rightXZ.z * move;
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        {
            nav.orbitCenter.x -= rightXZ.x * move;
            nav.orbitCenter.z -= rightXZ.z * move;
        }
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
        {
            nav.orbitCenter.y += move;
        }
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
        {
            nav.orbitCenter.y -= move;
        }
    }
}
