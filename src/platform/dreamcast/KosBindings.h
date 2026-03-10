#pragma once
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <stdint.h>

typedef struct NB_KOS_RawPadState {
    int has_controller;
    uint32_t buttons;
    int8_t stick_x;
    int8_t stick_y;
    uint8_t l_trigger;
    uint8_t r_trigger;
} NB_KOS_RawPadState;

void NB_KOS_BindingsInit(void);
void NB_KOS_BindingsRead(NB_KOS_RawPadState* outState);

typedef struct NB_Vec3 {
    float x;
    float y;
    float z;
} NB_Vec3;

typedef struct NB_Mesh {
    NB_Vec3* pos;
    NB_Vec3* tri_uv;
    NB_Vec3* tri_uv1;   /* second UV layer (v6+), NULL if absent */
    uint16_t* indices;
    uint16_t* tri_mat;
    int vert_count;
    int tri_count;
    int uv_layer_count;  /* total UV layers present (0, 1, or 2) */
} NB_Mesh;

typedef struct NB_Texture {
    uint16_t* pixels;
    int w;
    int h;
    float us;
    float vs;
    int filter;
} NB_Texture;

int NB_DC_LoadScene(const char* scenePath);
void NB_DC_UnloadScene(void);
int NB_DC_SwitchScene(const char* scenePath);

int NB_DC_LoadMesh(const char* meshPath, NB_Mesh* out);
int NB_DC_LoadTexture(const char* texPath, NB_Texture* out);
void NB_DC_FreeMesh(NB_Mesh* m);
void NB_DC_FreeTexture(NB_Texture* t);

const char* NB_DC_GetSceneName(void);
const char* NB_DC_GetSceneMeshPath(void);
const char* NB_DC_GetSceneTexturePath(int slotIndex);
void NB_DC_GetSceneTransform(float outPos[3], float outRot[3], float outScale[3]);
int NB_DC_GetSceneMeshCount(void);
const char* NB_DC_GetSceneMeshPathAt(int meshIndex);
const char* NB_DC_GetSceneTexturePathAt(int meshIndex, int slotIndex);
void NB_DC_GetSceneTransformAt(int meshIndex, float outPos[3], float outRot[3], float outScale[3]);

// Runtime transform bridge (implemented by generated Dreamcast runtime main.c)
void NB_RT_GetMeshPosition(float outPos[3]);
void NB_RT_SetMeshPosition(float x, float y, float z);
void NB_RT_AddMeshPositionDelta(float dx, float dy, float dz);

void NB_RT_GetNode3DPosition(const char* name, float outPos[3]);
void NB_RT_SetNode3DPosition(const char* name, float x, float y, float z);
void NB_RT_GetNode3DRotation(const char* name, float outRot[3]);
void NB_RT_SetNode3DRotation(const char* name, float x, float y, float z);
void NB_RT_GetCameraOrbit(const char* name, float outOrbit[3]);
void NB_RT_SetCameraOrbit(const char* name, float x, float y, float z);
void NB_RT_GetCameraRotation(const char* name, float outRot[3]);
void NB_RT_SetCameraRotation(const char* name, float x, float y, float z);
void NB_RT_GetCameraWorldForward(const char* name, float outFwd[3]);
int NB_RT_IsCameraUnderNode3D(const char* cameraName, const char* nodeName);
