// ScriptTemplate.c
// Cross-platform gameplay script template for Nebula (Windows editor + Dreamcast runtime)
//
// Copy this file, rename it, and implement NB_Game_OnStart / NB_Game_OnUpdate.
// Any .c in build_dreamcast/scripts can be auto-compiled into Dreamcast runtime
// (depending on your generated Makefile settings).

#include <math.h>
#include <stdio.h>

#if defined(__DREAMCAST__)
#include <kos/dbgio.h>
#include "KosInput.h"
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(_WIN32) && !defined(__DREAMCAST__)
#define NB_SCRIPT_EXPORT __declspec(dllexport)
#else
#define NB_SCRIPT_EXPORT
#endif

// -----------------------------------------------------------------------------
// Runtime bridge API (implemented by generated runtime main.c)
// -----------------------------------------------------------------------------
void NB_RT_GetNode3DPosition(const char* name, float outPos[3]);
void NB_RT_SetNode3DPosition(const char* name, float x, float y, float z);
void NB_RT_GetNode3DRotation(const char* name, float outRot[3]);
void NB_RT_SetNode3DRotation(const char* name, float x, float y, float z);
void NB_RT_GetCameraOrbit(const char* name, float outOrbit[3]);
void NB_RT_SetCameraOrbit(const char* name, float x, float y, float z);
void NB_RT_GetCameraRotation(const char* name, float outRot[3]);
void NB_RT_SetCameraRotation(const char* name, float x, float y, float z);
void NB_RT_GetCameraWorldForward(const char* name, float outFwd[3]);

// -----------------------------------------------------------------------------
// Script-configurable node names
// -----------------------------------------------------------------------------
static const char* PLAYER_NODE = "PlayerRoot";
static const char* CAMERA_NODE = "Camera3D1";

// -----------------------------------------------------------------------------
// Tunables
// -----------------------------------------------------------------------------
static const float MOVE_SPEED = 5.0f;
static const float EPS = 0.0001f;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static float Clamp(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// -----------------------------------------------------------------------------
// Required exported hooks
// -----------------------------------------------------------------------------
NB_SCRIPT_EXPORT void NB_Game_OnStart(void)
{
#if defined(__DREAMCAST__)
    dbgio_printf("[ScriptTemplate] OnStart\n");
#else
    printf("[ScriptTemplate] OnStart\n");
#endif

    // Example: read current player position
    float pos[3] = {0, 0, 0};
    NB_RT_GetNode3DPosition(PLAYER_NODE, pos);

    // Example: keep as-is (no movement on start)
    NB_RT_SetNode3DPosition(PLAYER_NODE, pos[0], pos[1], pos[2]);
}

NB_SCRIPT_EXPORT void NB_Game_OnUpdate(float dt)
{
    if (dt <= 0.0f) dt = 1.0f / 60.0f;

    float inX = 0.0f;
    float inY = 0.0f;

#if defined(_WIN32) && !defined(__DREAMCAST__)
    // Editor/runtime keyboard input
    if (GetAsyncKeyState('A') & 0x8000) inX -= 1.0f;
    if (GetAsyncKeyState('D') & 0x8000) inX += 1.0f;
    if (GetAsyncKeyState('W') & 0x8000) inY += 1.0f;
    if (GetAsyncKeyState('S') & 0x8000) inY -= 1.0f;
#elif defined(__DREAMCAST__)
    // Dreamcast input through KosInput wrappers
    NB_KOS_PollInput();
    if (NB_KOS_HasController())
    {
        if (NB_KOS_ButtonDown(NB_BTN_DPAD_LEFT))  inX -= 1.0f;
        if (NB_KOS_ButtonDown(NB_BTN_DPAD_RIGHT)) inX += 1.0f;
        if (NB_KOS_ButtonDown(NB_BTN_DPAD_UP))    inY += 1.0f;
        if (NB_KOS_ButtonDown(NB_BTN_DPAD_DOWN))  inY -= 1.0f;

        const float ax = NB_KOS_GetStickX();
        const float ay = NB_KOS_GetStickY();
        if (fabsf(ax) > 0.15f) inX += ax;
        if (fabsf(ay) > 0.15f) inY += -ay;
    }
#endif

    const float len = sqrtf(inX * inX + inY * inY);
    if (len > EPS)
    {
        float nx = inX / len;
        float ny = inY / len;

        float pos[3] = {0, 0, 0};
        NB_RT_GetNode3DPosition(PLAYER_NODE, pos);

        pos[0] += nx * MOVE_SPEED * dt;
        pos[2] += ny * MOVE_SPEED * dt;

        NB_RT_SetNode3DPosition(PLAYER_NODE, pos[0], pos[1], pos[2]);
    }

    // Optional: camera API usage example
    // float orbit[3] = {0,0,0};
    // NB_RT_GetCameraOrbit(CAMERA_NODE, orbit);
    // NB_RT_SetCameraOrbit(CAMERA_NODE, orbit[0], Clamp(orbit[1], -10.0f, 10.0f), orbit[2]);
}

NB_SCRIPT_EXPORT void NB_Game_OnSceneSwitch(const char* sceneName)
{
    (void)sceneName;
#if defined(__DREAMCAST__)
    dbgio_printf("[ScriptTemplate] OnSceneSwitch\n");
#else
    printf("[ScriptTemplate] OnSceneSwitch\n");
#endif
}
