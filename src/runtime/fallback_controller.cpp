#include "fallback_controller.h"
#include "script_compile.h"
#include "../viewport/node_helpers.h"
#include "../editor/editor_state.h"
#include "../nodes/NodeTypes.h"

#include <cmath>
#include <cstdio>

#include <GLFW/glfw3.h>

void TickFallbackControls(GLFWwindow* window, float deltaTime, bool navKeyAllowed)
{
    if (!gEnableCppPlayFallbackControls || !gPlayMode || !navKeyAllowed ||
        (gEditorScriptActive && useScriptController))
        return;

    const float moveStep = 5.0f * deltaTime;
    const float lookYawStep = 120.0f * deltaTime;
    const float lookPitchStep = 90.0f * deltaTime;
    const float turnSpeed = 360.0f * deltaTime;
    static double sFallbackLogTime = 0.0;
    static double sFreezeLogTime = 0.0;

    float inX = 0.0f, inY = 0.0f;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) inX += 1.0f;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) inX -= 1.0f;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) inY += 1.0f;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) inY -= 1.0f;
    float inLen = sqrtf(inX * inX + inY * inY);
    if (inLen > 1.0f)
    {
        inX /= inLen;
        inY /= inLen;
        inLen = 1.0f;
    }
    float lookYaw = 0.0f, lookPitch = 0.0f;
    if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS) lookYaw += 1.0f;
    if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS) lookYaw -= 1.0f;
    if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS) lookPitch += 1.0f;
    if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS) lookPitch -= 1.0f;

    // Hard fallback sync: keep CameraPivot at PlayerRoot position in C++ when script runtime is unavailable.
    {
        int playerIdx = FindNode3DByName("PlayerRoot");
        int pivotIdx = FindNode3DByName("CameraPivot");
        if (playerIdx >= 0 && pivotIdx >= 0 && playerIdx < (int)gNode3DNodes.size() && pivotIdx < (int)gNode3DNodes.size())
        {
            gNode3DNodes[pivotIdx].x = gNode3DNodes[playerIdx].x;
            gNode3DNodes[pivotIdx].y = gNode3DNodes[playerIdx].y;
            gNode3DNodes[pivotIdx].z = gNode3DNodes[playerIdx].z;
        }
    }

    for (int ni = 0; ni < (int)gNode3DNodes.size(); ++ni)
    {
        auto& n3 = gNode3DNodes[ni];
        if (n3.name != "PlayerRoot") continue;

        const bool lookActive = (fabsf(lookYaw) > 0.0001f || fabsf(lookPitch) > 0.0001f);
        for (auto& cam : gCamera3DNodes)
        {
            const bool camLinked = IsCameraUnderNode3D(cam, n3.name)
                || (n3.name == "PlayerRoot" && cam.parent == "CameraPivot");
            if (!camLinked) continue;
            if (!lookActive) continue; // hard-freeze camera unless J/L/I/K is pressed

            bool migratedOrbit = false;
            if (fabsf(cam.orbitX) < 0.0001f && fabsf(cam.orbitZ) < 0.0001f)
            {
                // Seed orbit from local camera offset so J/L has visible effect immediately.
                cam.orbitX = cam.x;
                cam.orbitZ = cam.z;
                cam.x = 0.0f;
                cam.z = 0.0f;
                migratedOrbit = true;
            }

            if (!migratedOrbit && fabsf(lookYaw) > 0.0001f)
            {
                const float yawRad = (-lookYaw * lookYawStep) * 3.14159f / 180.0f;
                const float sn = sinf(yawRad), cs = cosf(yawRad);
                float ox = cam.orbitX, oz = cam.orbitZ;
                cam.orbitX = ox * cs - oz * sn;
                cam.orbitZ = ox * sn + oz * cs;
            }
            if (fabsf(lookPitch) > 0.0001f)
            {
                float ox = cam.orbitX, oy = cam.orbitY, oz = cam.orbitZ;
                float horiz = sqrtf(ox * ox + oz * oz);
                float radius = sqrtf(horiz * horiz + oy * oy);
                if (radius > 0.0001f)
                {
                    float pitch = atan2f(oy, (horiz > 0.0001f ? horiz : 0.0001f));
                    pitch += (lookPitch * lookPitchStep) * 3.14159f / 180.0f;
                    const float lim = 1.39626f;
                    if (pitch > lim) pitch = lim;
                    if (pitch < -lim) pitch = -lim;
                    float newHoriz = cosf(pitch) * radius;
                    cam.orbitY = sinf(pitch) * radius;
                    if (horiz > 0.0001f)
                    {
                        float s = newHoriz / horiz;
                        cam.orbitX = ox * s;
                        cam.orbitZ = oz * s;
                    }
                    else
                    {
                        cam.orbitX = 0.0f;
                        cam.orbitZ = -newHoriz;
                    }
                }
            }

            // Keep camera looking toward pivot from orbit offset (only during explicit look input).
            {
                float fx = -cam.orbitX, fy = -cam.orbitY, fz = -cam.orbitZ;
                float fl = sqrtf(fx * fx + fy * fy + fz * fz);
                if (fl > 0.0001f)
                {
                    fx /= fl; fy /= fl; fz /= fl;
                    cam.rotY = atan2f(fx, fz) * 180.0f / 3.14159f;
                    cam.rotX = -atan2f(fy, sqrtf(fx * fx + fz * fz)) * 180.0f / 3.14159f;
                    cam.rotZ = 0.0f;
                }
            }
        }

        // SA1 camera-relative 8-way fallback (mode-locked):
        // use camera->pivot direction on XZ so W is away from camera, S is toward camera.
        float camToNodeX = 0.0f, camToNodeZ = 1.0f;
        bool gotFwd = false;
        bool gotCamForFreeze = false;
        float freezeOrbitX = 0.0f, freezeOrbitY = 0.0f, freezeOrbitZ = 0.0f;
        float freezeRotX = 0.0f, freezeRotY = 0.0f, freezeRotZ = 0.0f;
        for (const auto& cam : gCamera3DNodes)
        {
            const bool camLinked = IsCameraUnderNode3D(cam, n3.name)
                || (n3.name == "PlayerRoot" && cam.parent == "CameraPivot");
            if (!camLinked) continue;
            int ci = FindCamera3DByName(cam.name);
            if (ci < 0) continue;

            float cwx, cwy, cwz, cwrx, cwry, cwrz;
            GetCamera3DWorldTR(ci, cwx, cwy, cwz, cwrx, cwry, cwrz);

            float nwx, nwy, nwz, nwrx, nwry, nwrz, nwsx, nwsy, nwsz;
            GetNode3DWorldTRS(ni, nwx, nwy, nwz, nwrx, nwry, nwrz, nwsx, nwsy, nwsz);

            camToNodeX = nwx - cwx;
            camToNodeZ = nwz - cwz;
            gotFwd = true;
            gotCamForFreeze = true;
            freezeOrbitX = cam.orbitX;
            freezeOrbitY = cam.orbitY;
            freezeOrbitZ = cam.orbitZ;
            freezeRotX = cam.rotX;
            freezeRotY = cam.rotY;
            freezeRotZ = cam.rotZ;
            break;
        }
        if (!gotFwd)
        {
            float yawRad = n3.rotY * 3.14159f / 180.0f;
            camToNodeX = sinf(yawRad);
            camToNodeZ = cosf(yawRad);
        }

        float fLen = sqrtf(camToNodeX * camToNodeX + camToNodeZ * camToNodeZ);
        if (fLen < 0.0001f)
        {
            // Stable fallback when camera forward collapses (prevents spin jitter).
            float yawRad = n3.rotY * 3.14159f / 180.0f;
            camToNodeX = sinf(yawRad);
            camToNodeZ = cosf(yawRad);
            fLen = 1.0f;
        }
        camToNodeX /= fLen;
        camToNodeZ /= fLen;
        float camRightX = camToNodeZ;
        float camRightZ = -camToNodeX;

        // Build desired move heading from stable camera basis + WASD direction.
        float moveX = camRightX * inX + camToNodeX * inY;
        float moveZ = camRightZ * inX + camToNodeZ * inY;
        float moveLen = sqrtf(moveX * moveX + moveZ * moveZ);
        const float deadzone = 0.05f;
        float targetYaw = n3.rotY;
        float dy = 0.0f;
        if (moveLen > deadzone)
        {
            moveX /= moveLen;
            moveZ /= moveLen;

            n3.x += moveX * moveStep;
            n3.z += moveZ * moveStep;

            targetYaw = atan2f(moveX, moveZ) * 180.0f / 3.14159f;
            dy = targetYaw - n3.rotY;
            while (dy > 180.0f) dy -= 360.0f;
            while (dy < -180.0f) dy += 360.0f;
            if (dy > turnSpeed) dy = turnSpeed;
            if (dy < -turnSpeed) dy = -turnSpeed;
            n3.rotY += dy;
        }

        // Keep CameraPivot snapped to PlayerRoot after movement, same frame.
        if (n3.name == "PlayerRoot")
        {
            int pivotIdx = FindNode3DByName("CameraPivot");
            if (pivotIdx >= 0 && pivotIdx < (int)gNode3DNodes.size())
            {
                gNode3DNodes[pivotIdx].x = n3.x;
                gNode3DNodes[pivotIdx].y = n3.y;
                gNode3DNodes[pivotIdx].z = n3.z;
            }
        }

        if (inLen > 0.0001f)
        {
            double now = glfwGetTime();
            if (now - sFallbackLogTime >= 1.0)
            {
                sFallbackLogTime = now;
                printf("[Fallback8Way] node=%s in=(%.2f,%.2f) camToNode=(%.2f,%.2f,%.2f) move=(%.2f,%.2f,%.2f) yaw=%.2f target=%.2f dy=%.2f\n",
                    n3.name.c_str(),
                    inX, inY,
                    camToNodeX, camToNodeZ, fLen,
                    moveX, moveZ, moveLen,
                    n3.rotY, targetYaw, dy);
            }
        }

        if (!lookActive && inLen > 0.0001f && gotCamForFreeze)
        {
            double now = glfwGetTime();
            if (now - sFreezeLogTime >= 1.0)
            {
                sFreezeLogTime = now;
                printf("[FallbackCamFreeze] lookActive=0 camOrbit=(%.3f,%.3f,%.3f) camRot=(%.3f,%.3f,%.3f)\n",
                    freezeOrbitX, freezeOrbitY, freezeOrbitZ,
                    freezeRotX, freezeRotY, freezeRotZ);
            }
        }
    }
}
