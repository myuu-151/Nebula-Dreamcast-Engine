// NavMesh_AI_Roam.c
// AI script that makes a Node3D randomly roam within navmesh bounds.
// Assign this script to the Node3D you want to roam, or set AI_NODE below.

#include <math.h>
#include <stdio.h>

#if defined(_WIN32) && !defined(__DREAMCAST__)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define NB_SCRIPT_EXPORT __declspec(dllexport)
#else
#define NB_SCRIPT_EXPORT
#endif

// Runtime bridge API
void NB_RT_GetNode3DPosition(const char* name, float outPos[3]);
void NB_RT_SetNode3DPosition(const char* name, float x, float y, float z);
void NB_RT_GetNode3DRotation(const char* name, float outRot[3]);
void NB_RT_SetNode3DRotation(const char* name, float x, float y, float z);
int  NB_RT_NavMeshBuild(void);
int  NB_RT_NavMeshIsReady(void);
int  NB_RT_NavMeshFindRandomPoint(float outPos[3]);
int  NB_RT_NavMeshFindPath(float sx, float sy, float sz, float gx, float gy, float gz, float* outPath, int maxPoints);

// ---- Configuration ----
static const char* AI_NODE = "AINode";
static const float MOVE_SPEED = 3.0f;
static const float ARRIVE_DIST = 0.5f;
static const float TURN_SPEED_DEG = 360.0f;
static const float WAIT_TIME = 1.0f;
static const float EPS = 0.0001f;

// ---- State ----
static int   sNavReady = 0;
static float sGoalPos[3] = {0, 0, 0};
static int   sHasGoal = 0;
static float sPath[64 * 3];
static int   sPathLen = 0;
static int   sPathIdx = 0;
static float sWaitTimer = 0.0f;

static float WrapAngleDeg(float a)
{
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

static float Clamp(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void PickNewGoal(float cx, float cy, float cz)
{
    float rp[3] = {0, 0, 0};
    if (NB_RT_NavMeshFindRandomPoint(rp))
    {
        sPathLen = NB_RT_NavMeshFindPath(cx, cy, cz, rp[0], rp[1], rp[2], sPath, 64);
        if (sPathLen > 0)
        {
            sPathIdx = 0;
            sGoalPos[0] = rp[0];
            sGoalPos[1] = rp[1];
            sGoalPos[2] = rp[2];
            sHasGoal = 1;
            printf("[AI_Roam] New goal (%.1f, %.1f, %.1f) path=%d pts\n", rp[0], rp[1], rp[2], sPathLen);
        }
    }
}

NB_SCRIPT_EXPORT void NB_Game_OnStart(void)
{
    printf("[AI_Roam] OnStart — building navmesh...\n");
    sNavReady = NB_RT_NavMeshBuild();
    if (sNavReady)
        printf("[AI_Roam] NavMesh ready\n");
    else
        printf("[AI_Roam] NavMesh build failed\n");

    sHasGoal = 0;
    sPathLen = 0;
    sPathIdx = 0;
    sWaitTimer = 0.0f;
}

NB_SCRIPT_EXPORT void NB_Game_OnUpdate(float dt)
{
    if (dt <= 0.0f) dt = 1.0f / 60.0f;

    if (!sNavReady)
    {
        sNavReady = NB_RT_NavMeshIsReady();
        if (!sNavReady) return;
    }

    float pos[3] = {0, 0, 0};
    NB_RT_GetNode3DPosition(AI_NODE, pos);

    // Waiting between destinations
    if (sWaitTimer > 0.0f)
    {
        sWaitTimer -= dt;
        NB_RT_SetNode3DPosition(AI_NODE, pos[0], pos[1], pos[2]);
        return;
    }

    // Pick a goal if we don't have one
    if (!sHasGoal)
    {
        PickNewGoal(pos[0], pos[1], pos[2]);
        if (!sHasGoal)
        {
            NB_RT_SetNode3DPosition(AI_NODE, pos[0], pos[1], pos[2]);
            return;
        }
    }

    // Get current waypoint from path
    float tx, tz;
    if (sPathIdx < sPathLen)
    {
        tx = sPath[sPathIdx * 3 + 0];
        tz = sPath[sPathIdx * 3 + 2];
    }
    else
    {
        tx = sGoalPos[0];
        tz = sGoalPos[2];
    }

    float dx = tx - pos[0];
    float dz = tz - pos[2];
    float dist = sqrtf(dx * dx + dz * dz);

    // Arrived at current waypoint
    if (dist < ARRIVE_DIST)
    {
        sPathIdx++;
        if (sPathIdx >= sPathLen)
        {
            // Arrived at final goal — wait then pick new
            sHasGoal = 0;
            sWaitTimer = WAIT_TIME;
            printf("[AI_Roam] Arrived, waiting %.1fs\n", WAIT_TIME);
            NB_RT_SetNode3DPosition(AI_NODE, pos[0], pos[1], pos[2]);
            return;
        }
    }

    // Move toward current waypoint
    if (dist > EPS)
    {
        float nx = dx / dist;
        float nz = dz / dist;
        float step = MOVE_SPEED * dt;
        if (step > dist) step = dist;

        pos[0] += nx * step;
        pos[2] += nz * step;

        // Face movement direction
        float rot[3] = {0, 0, 0};
        NB_RT_GetNode3DRotation(AI_NODE, rot);
        float targetYaw = atan2f(nx, nz) * (180.0f / 3.14159265f);
        float dy = WrapAngleDeg(targetYaw - rot[1]);
        float maxStep = TURN_SPEED_DEG * dt;
        dy = Clamp(dy, -maxStep, maxStep);
        rot[1] = WrapAngleDeg(rot[1] + dy);
        NB_RT_SetNode3DRotation(AI_NODE, rot[0], rot[1], rot[2]);
    }

    NB_RT_SetNode3DPosition(AI_NODE, pos[0], pos[1], pos[2]);
}

NB_SCRIPT_EXPORT void NB_Game_OnSceneSwitch(const char* sceneName)
{
    (void)sceneName;
    sHasGoal = 0;
    sPathLen = 0;
    sPathIdx = 0;
    sWaitTimer = 0.0f;
}
