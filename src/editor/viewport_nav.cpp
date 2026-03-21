#include "viewport_nav.h"
#include "editor_state.h"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <GLFW/glfw3.h>
#include "imgui.h"

#include "../camera/camera3d.h"
#include "../nodes/NodeTypes.h"
#include "../viewport/node_helpers.h"
#include "../math/math_utils.h"

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

void InstallViewportScrollCallback(GLFWwindow* window)
{
    glfwSetScrollCallback(window, [](GLFWwindow* win, double, double yoff)
    {
        auto* n = (EditorViewportNav*)glfwGetWindowUserPointer(win);
        if (n) n->scrollDelta += (float)yoff;
    });
}

FrameCameraResult EvaluateFrameCamera(EditorViewportNav& nav, float aspect, double now)
{
    FrameCameraResult result{};

    Mat4 proj = Mat4Perspective(45.0f * 3.14159f / 180.0f, aspect, 0.1f, 2000.0f);
    Vec3 eye = nav.ComputeEye();
    gEye = eye;

    Camera3DNode* activeCam = nullptr;
    if (!gCamera3DNodes.empty())
    {
        for (auto& c : gCamera3DNodes)
        {
            if (c.main && (!activeCam || c.priority > activeCam->priority))
                activeCam = &c;
        }
        if (!activeCam)
            activeCam = &gCamera3DNodes[0];
    }

    NebulaCamera3D::View playView{};
    NebulaCamera3D::Projection playProj{};
    bool hasPlayCam = false;
    if (activeCam && gPlayMode)
    {
        int activeCamIdx = (int)(activeCam - &gCamera3DNodes[0]);
        float camX, camY, camZ, camRX, camRY, camRZ;
        GetCamera3DWorldTR(activeCamIdx, camX, camY, camZ, camRX, camRY, camRZ);

        Camera3D playCam = BuildCamera3DFromLegacyEuler(
            activeCam->name,
            activeCam->parent,
            camX, camY, camZ,
            camRX, camRY, camRZ,
            activeCam->perspective,
            activeCam->fovY,
            activeCam->nearZ,
            activeCam->farZ,
            activeCam->orthoWidth,
            activeCam->priority,
            activeCam->main);

        playView = NebulaCamera3D::BuildView(playCam);
        playProj = NebulaCamera3D::BuildProjection(playCam, aspect);
        hasPlayCam = true;

        proj = NebulaCamera3D::BuildProjectionMatrix(playProj);
        eye = playView.eye;

        Vec3 playForward = playView.basis.forward;
        nav.viewYaw = atan2f(playForward.z, playForward.x) * 180.0f / 3.14159f;
        nav.viewPitch = asinf(std::clamp(playForward.y, -1.0f, 1.0f)) * 180.0f / 3.14159f;

        static double sLastParityCamLog = -10.0;
        if ((now - sLastParityCamLog) >= 1.0)
        {
            sLastParityCamLog = now;
            printf("[CameraParity][EditorPlay] eye=(%.3f,%.3f,%.3f) f=(%.3f,%.3f,%.3f) r=(%.3f,%.3f,%.3f) u=(%.3f,%.3f,%.3f)\n",
                playView.eye.x, playView.eye.y, playView.eye.z,
                playView.basis.forward.x, playView.basis.forward.y, playView.basis.forward.z,
                playView.basis.right.x, playView.basis.right.y, playView.basis.right.z,
                playView.basis.up.x, playView.basis.up.y, playView.basis.up.z);
        }
    }

    // If view is locked, keep looking at orbit center
    if (nav.viewLocked && !(activeCam && gPlayMode))
    {
        Vec3 dir = { nav.orbitCenter.x - eye.x, nav.orbitCenter.y - eye.y, nav.orbitCenter.z - eye.z };
        float dlen = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (dlen > 0.0001f)
        {
            dir.x /= dlen; dir.y /= dlen; dir.z /= dlen;
            nav.viewYaw = atan2f(dir.z, dir.x) * 180.0f / 3.14159f;
            nav.viewPitch = asinf(dir.y) * 180.0f / 3.14159f;
        }
    }

    Vec3 forward{};
    Vec3 up = { 0.0f, 1.0f, 0.0f };

    if (hasPlayCam)
    {
        up = playView.basis.up;
        forward = playView.basis.forward;
    }
    else
    {
        float viewYawRad = nav.viewYaw * 3.14159f / 180.0f;
        float viewPitchRad = nav.viewPitch * 3.14159f / 180.0f;
        forward = {
            cosf(viewPitchRad) * cosf(viewYawRad),
            sinf(viewPitchRad),
            cosf(viewPitchRad) * sinf(viewYawRad)
        };
    }

    Mat4 view = {};
    if (hasPlayCam)
    {
        view = NebulaCamera3D::BuildViewMatrix(playView);
    }
    else
    {
        Vec3 target = { eye.x + forward.x, eye.y + forward.y, eye.z + forward.z };
        view = Mat4LookAt(eye, target, up);
    }

    // Editor parity toggle: mirror runtime + viewport horizontally (right-to-left).
    const bool kMirrorViewportRTL = false;
    if (kMirrorViewportRTL)
    {
        proj.m[0] = -proj.m[0];
    }

    result.proj = proj;
    result.view = view;
    result.eye = eye;
    result.forward = forward;
    result.up = up;
    result.activeCam = activeCam;
    return result;
}
