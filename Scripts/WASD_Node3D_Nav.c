// WASD_Node3D_Nav.c
// Script-owned controller: SA1 camera-relative 8-way WASD + camera orbit controls.

#include <math.h>
#include <stdio.h>

#if defined(_WIN32) && !defined(__DREAMCAST__)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static const char* PLAYER_NODE = "PlayerRoot";
static const char* CAMERA_PIVOT_NODE = "CameraPivot";
static const char* CAMERA_NODE = "Camera3D1";

static const float MOVE_SPEED = 5.0f;
static const float TURN_SPEED_DEG = 360.0f;
static const float LOOK_YAW_SPEED_DEG = 120.0f;
static const float LOOK_PITCH_SPEED_DEG = 90.0f;
static const float PITCH_LIMIT_RAD = 1.39626f;
static const float DEADZONE = 0.05f;
static const float EPS = 0.0001f;

static int kEnableDebugLog = 0;
static float sLogTimer = 0.0f;

void NB_RT_GetNode3DPosition(const char* name, float outPos[3]);
void NB_RT_SetNode3DPosition(const char* name, float x, float y, float z);
void NB_RT_GetNode3DRotation(const char* name, float outRot[3]);
void NB_RT_SetNode3DRotation(const char* name, float x, float y, float z);
void NB_RT_GetCameraOrbit(const char* name, float outOrbit[3]);
void NB_RT_SetCameraOrbit(const char* name, float x, float y, float z);
void NB_RT_GetCameraRotation(const char* name, float outRot[3]);
void NB_RT_SetCameraRotation(const char* name, float x, float y, float z);
void NB_RT_GetCameraWorldForward(const char* name, float outFwd[3]);

static float Clamp(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float WrapAngleDeg(float a) {
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

void NB_Game_OnStart(void)
{
}

void NB_Game_OnUpdate(float dt)
{
    if (dt <= 0.0f) dt = 1.0f / 60.0f;

    float playerPos[3] = {0, 0, 0};
    float playerRot[3] = {0, 0, 0};
    NB_RT_GetNode3DPosition(PLAYER_NODE, playerPos);
    NB_RT_GetNode3DRotation(PLAYER_NODE, playerRot);

    NB_RT_SetNode3DPosition(CAMERA_PIVOT_NODE, playerPos[0], playerPos[1], playerPos[2]);

    float inX = 0.0f;
    float inY = 0.0f;
    float lookYaw = 0.0f;
    float lookPitch = 0.0f;

#if defined(_WIN32) && !defined(__DREAMCAST__)
    if (GetAsyncKeyState('A') & 0x8000) inX -= 1.0f;
    if (GetAsyncKeyState('D') & 0x8000) inX += 1.0f;
    if (GetAsyncKeyState('W') & 0x8000) inY += 1.0f;
    if (GetAsyncKeyState('S') & 0x8000) inY -= 1.0f;

    if (GetAsyncKeyState('J') & 0x8000) lookYaw += 1.0f;
    if (GetAsyncKeyState('L') & 0x8000) lookYaw -= 1.0f;
    if (GetAsyncKeyState('I') & 0x8000) lookPitch += 1.0f;
    if (GetAsyncKeyState('K') & 0x8000) lookPitch -= 1.0f;
#endif

    float inLen = sqrtf(inX * inX + inY * inY);
    if (inLen > 1.0f) {
        inX /= inLen;
        inY /= inLen;
        inLen = 1.0f;
    }

    float orbit[3] = {0, 0, 0};
    NB_RT_GetCameraOrbit(CAMERA_NODE, orbit);

    float ox = orbit[0];
    float oy = orbit[1];
    float oz = orbit[2];
    int orbitChanged = 0;

    if (fabsf(lookYaw) > EPS) {
        float yawRad = (-lookYaw * LOOK_YAW_SPEED_DEG * dt) * (3.14159265f / 180.0f);
        float sn = sinf(yawRad);
        float cs = cosf(yawRad);
        float nx = ox * cs - oz * sn;
        float nz = ox * sn + oz * cs;
        ox = nx;
        oz = nz;
        orbitChanged = 1;
    }

    if (fabsf(lookPitch) > EPS) {
        float horiz = sqrtf(ox * ox + oz * oz);
        float radius = sqrtf(horiz * horiz + oy * oy);
        if (radius > EPS) {
            float pitch = atan2f(oy, (horiz > EPS ? horiz : EPS));
            pitch += (lookPitch * LOOK_PITCH_SPEED_DEG * dt) * (3.14159265f / 180.0f);
            pitch = Clamp(pitch, -PITCH_LIMIT_RAD, PITCH_LIMIT_RAD);
            float newHoriz = cosf(pitch) * radius;
            oy = sinf(pitch) * radius;
            if (horiz > EPS) {
                float s = newHoriz / horiz;
                ox *= s;
                oz *= s;
            } else {
                ox = 0.0f;
                oz = -newHoriz;
            }
            orbitChanged = 1;
        }
    }

    if (orbitChanged) {
        NB_RT_SetCameraOrbit(CAMERA_NODE, ox, oy, oz);
    }

    float fx = -ox;
    float fy = -oy;
    float fz = -oz;
    float fLen = sqrtf(fx * fx + fy * fy + fz * fz);
    if (fLen > EPS) {
        float inv = 1.0f / fLen;
        fx *= inv;
        fy *= inv;
        fz *= inv;
        float yawDeg = atan2f(fx, fz) * (180.0f / 3.14159265f);
        float pitchDeg = -atan2f(fy, sqrtf(fx * fx + fz * fz)) * (180.0f / 3.14159265f);
        NB_RT_SetCameraRotation(CAMERA_NODE, pitchDeg, yawDeg, 0.0f);
    }

    float camToNodeX = 0.0f;
    float camToNodeZ = 1.0f;
    int haveBasis = 0;

    if (fLen > EPS) {
        camToNodeX = fx;
        camToNodeZ = fz;
        haveBasis = 1;
    } else {
        float fwd[3] = {0, 0, 1};
        NB_RT_GetCameraWorldForward(CAMERA_NODE, fwd);
        float fwdLen = sqrtf(fwd[0] * fwd[0] + fwd[2] * fwd[2]);
        if (fwdLen > EPS) {
            camToNodeX = fwd[0] / fwdLen;
            camToNodeZ = fwd[2] / fwdLen;
            haveBasis = 1;
        }
    }

    if (!haveBasis) {
        float yawRad = playerRot[1] * (3.14159265f / 180.0f);
        camToNodeX = sinf(yawRad);
        camToNodeZ = cosf(yawRad);
    }

    float camRightX = camToNodeZ;
    float camRightZ = -camToNodeX;

    float moveX = camRightX * inX + camToNodeX * inY;
    float moveZ = camRightZ * inX + camToNodeZ * inY;
    float moveLen = sqrtf(moveX * moveX + moveZ * moveZ);

    if (moveLen > DEADZONE) {
        moveX /= moveLen;
        moveZ /= moveLen;

        playerPos[0] += moveX * MOVE_SPEED * dt;
        playerPos[2] += moveZ * MOVE_SPEED * dt;

        float targetYaw = atan2f(moveX, moveZ) * (180.0f / 3.14159265f);
        float dy = WrapAngleDeg(targetYaw - playerRot[1]);
        float maxStep = TURN_SPEED_DEG * dt;
        dy = Clamp(dy, -maxStep, maxStep);
        playerRot[1] = WrapAngleDeg(playerRot[1] + dy);
    }

    NB_RT_SetNode3DPosition(PLAYER_NODE, playerPos[0], playerPos[1], playerPos[2]);
    NB_RT_SetNode3DRotation(PLAYER_NODE, playerRot[0], playerRot[1], playerRot[2]);

    NB_RT_SetNode3DPosition(CAMERA_PIVOT_NODE, playerPos[0], playerPos[1], playerPos[2]);

    if (kEnableDebugLog) {
        sLogTimer += dt;
        if (sLogTimer >= 1.0f) {
            sLogTimer = 0.0f;
            printf("[ScriptController] pos=(%.2f,%.2f,%.2f) rotY=%.2f in=(%.2f,%.2f) orbit=(%.2f,%.2f,%.2f)\n",
                playerPos[0], playerPos[1], playerPos[2], playerRot[1], inX, inY, ox, oy, oz);
        }
    }
}

void NB_Game_OnSceneSwitch(const char* sceneName)
{
    (void)sceneName;
}
