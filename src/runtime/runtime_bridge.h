#pragma once

// ---------------------------------------------------------------------------
// NB_RT_* bridge function declarations
// These are the editor-side implementations of the Dreamcast runtime API.
// Exported as extern "C" so gameplay scripts can link against them.
// ---------------------------------------------------------------------------

#if defined(_WIN32)
#define NB_RT_EXPORT extern "C" __declspec(dllexport)
#else
#define NB_RT_EXPORT extern "C"
#endif

// --- Node3D position/rotation ---
NB_RT_EXPORT void  NB_RT_GetNode3DPosition(const char* name, float outPos[3]);
NB_RT_EXPORT void  NB_RT_SetNode3DPosition(const char* name, float x, float y, float z);
NB_RT_EXPORT void  NB_RT_GetNode3DRotation(const char* name, float outRot[3]);
NB_RT_EXPORT void  NB_RT_SetNode3DRotation(const char* name, float x, float y, float z);

// --- Camera ---
NB_RT_EXPORT void  NB_RT_GetCameraWorldForward(const char* name, float outFwd[3]);
NB_RT_EXPORT void  NB_RT_GetCameraOrbit(const char* name, float outOrbit[3]);
NB_RT_EXPORT void  NB_RT_SetCameraOrbit(const char* name, float x, float y, float z);
NB_RT_EXPORT void  NB_RT_GetCameraRotation(const char* name, float outRot[3]);
NB_RT_EXPORT void  NB_RT_SetCameraRotation(const char* name, float x, float y, float z);
NB_RT_EXPORT int   NB_RT_IsCameraUnderNode3D(const char* cameraName, const char* nodeName);

// --- Node3D collision/physics ---
NB_RT_EXPORT void  NB_RT_GetNode3DCollisionBounds(const char* name, float outExtents[3]);
NB_RT_EXPORT void  NB_RT_SetNode3DCollisionBounds(const char* name, float ex, float ey, float ez);
NB_RT_EXPORT void  NB_RT_GetNode3DBoundPos(const char* name, float outPos[3]);
NB_RT_EXPORT void  NB_RT_SetNode3DBoundPos(const char* name, float bx, float by, float bz);
NB_RT_EXPORT int   NB_RT_GetNode3DPhysicsEnabled(const char* name);
NB_RT_EXPORT void  NB_RT_SetNode3DPhysicsEnabled(const char* name, int enabled);
NB_RT_EXPORT int   NB_RT_GetNode3DCollisionSource(const char* name);
NB_RT_EXPORT void  NB_RT_SetNode3DCollisionSource(const char* name, int enabled);
NB_RT_EXPORT int   NB_RT_GetNode3DSimpleCollision(const char* name);
NB_RT_EXPORT void  NB_RT_SetNode3DSimpleCollision(const char* name, int enabled);
NB_RT_EXPORT float NB_RT_GetNode3DVelocityY(const char* name);
NB_RT_EXPORT void  NB_RT_SetNode3DVelocityY(const char* name, float vy);
NB_RT_EXPORT int   NB_RT_IsNode3DOnFloor(const char* name);
NB_RT_EXPORT int   NB_RT_CheckAABBOverlap(const char* name1, const char* name2);

// --- NavMesh ---
NB_RT_EXPORT int   NB_RT_NavMeshBuild(void);
NB_RT_EXPORT void  NB_RT_NavMeshClear(void);
NB_RT_EXPORT int   NB_RT_NavMeshIsReady(void);
NB_RT_EXPORT int   NB_RT_NavMeshFindPath(float sx, float sy, float sz,
                                          float gx, float gy, float gz,
                                          float* outPath, int maxPoints);
NB_RT_EXPORT int   NB_RT_NavMeshFindRandomPoint(float outPos[3]);
NB_RT_EXPORT int   NB_RT_NavMeshFindClosestPoint(float px, float py, float pz, float outPos[3]);

// --- Scene switching ---
NB_RT_EXPORT void  NB_RT_NextScene(void);
NB_RT_EXPORT void  NB_RT_PrevScene(void);
NB_RT_EXPORT void  NB_RT_SwitchScene(const char* name);

// --- Animation slots ---
NB_RT_EXPORT void  NB_RT_PlayAnimation(const char* meshName, const char* animName);
NB_RT_EXPORT void  NB_RT_StopAnimation(const char* meshName);
NB_RT_EXPORT int   NB_RT_IsAnimationPlaying(const char* meshName);
NB_RT_EXPORT int   NB_RT_IsAnimationFinished(const char* meshName);
NB_RT_EXPORT void  NB_RT_SetAnimationSpeed(const char* meshName, float speed);

// --- Raycasting ---
NB_RT_EXPORT int   NB_RT_RaycastDown(float rx, float ry, float rz, float* outHitY);
NB_RT_EXPORT int   NB_RT_RaycastDownWithNormal(float rx, float ry, float rz, float* outHitY, float outNormal[3]);

// ---------------------------------------------------------------------------
// Helper function declarations shared between main.cpp and runtime_bridge.cpp
// These are defined in main.cpp and linked externally by runtime_bridge.cpp.
// ---------------------------------------------------------------------------
#include <string>

int  FindNode3DByName(const std::string& name);
int  FindCamera3DByName(const std::string& name);
int  FindStaticMeshByName(const std::string& name);
