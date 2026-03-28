#include "KosBindings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void NB_KOS_BindingsInit(void) {}

void NB_KOS_BindingsRead(NB_KOS_RawPadState* outState) {
    if (!outState) return;
    memset(outState, 0, sizeof(*outState));

    maple_device_t* dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (!dev) return;

    cont_state_t* st = (cont_state_t*)maple_dev_status(dev);
    if (!st) return;

    outState->has_controller = 1;
    outState->buttons = st->buttons;
    outState->stick_x = st->joyx;
    outState->stick_y = st->joyy;
    outState->l_trigger = st->ltrig;
    outState->r_trigger = st->rtrig;
}

enum { NB_DC_MAX_SCENE_MESHES = 64, NB_DC_MAX_TEXTURE_SLOTS = 16 };

typedef struct NB_DC_SceneMeshState {
    char mesh_path[128];
    char texture_paths[NB_DC_MAX_TEXTURE_SLOTS][128];
    float pos[3];
    float rot[3];
    float scale[3];
} NB_DC_SceneMeshState;

typedef struct NB_DC_SceneState {
    int loaded;
    char scene_name[64];
    int mesh_count;
    NB_DC_SceneMeshState meshes[NB_DC_MAX_SCENE_MESHES];
} NB_DC_SceneState;

static NB_DC_SceneState gSceneState;

static int NB_DC_ReadU16BE(FILE* f, uint16_t* out) {
    int a = fgetc(f);
    int b = fgetc(f);
    if (a < 0 || b < 0) return 0;
    *out = (uint16_t)(((uint16_t)a << 8) | (uint16_t)b);
    return 1;
}

static int NB_DC_ReadS16BE(FILE* f, int16_t* out) {
    uint16_t t = 0;
    if (!NB_DC_ReadU16BE(f, &t)) return 0;
    *out = (int16_t)t;
    return 1;
}

static int NB_DC_ReadU32BE(FILE* f, uint32_t* out) {
    int a = fgetc(f);
    int b = fgetc(f);
    int c = fgetc(f);
    int d = fgetc(f);
    if (a < 0 || b < 0 || c < 0 || d < 0) return 0;
    *out = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
    return 1;
}

static int NB_DC_SceneTokenize(char* s, char* toks[], int maxTok) {
    int n = 0;
    char* p = strtok(s, " \t\r\n");
    while (p && n < maxTok) {
        toks[n++] = p;
        p = strtok(NULL, " \t\r\n");
    }
    return n;
}

int NB_DC_LoadScene(const char* scenePath) {
    if (!scenePath || !scenePath[0]) return 0;

    static NB_DC_SceneState nextState;
    memset(&nextState, 0, sizeof(nextState));

    FILE* f = fopen(scenePath, "r");
    if (!f) return 0;

    char line[1024];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        if (!strncmp(line, "scene=", 6)) {
            strncpy(nextState.scene_name, line + 6, sizeof(nextState.scene_name) - 1);
            nextState.scene_name[sizeof(nextState.scene_name) - 1] = 0;
            size_t ln = strlen(nextState.scene_name);
            while (ln > 0 && (nextState.scene_name[ln - 1] == '\n' || nextState.scene_name[ln - 1] == '\r'))
                nextState.scene_name[--ln] = 0;
            continue;
        }
        if (!strncmp(line, "staticmesh ", 11)) {
            char* toks[64];
            int tc = NB_DC_SceneTokenize(line, toks, 64);
            if (tc < 25) continue;
            if (nextState.mesh_count >= NB_DC_MAX_SCENE_MESHES) continue;

            NB_DC_SceneMeshState* mesh = &nextState.meshes[nextState.mesh_count];
            memset(mesh, 0, sizeof(*mesh));
            mesh->scale[0] = 1.0f;
            mesh->scale[1] = 1.0f;
            mesh->scale[2] = 1.0f;

            strncpy(mesh->mesh_path, toks[1], sizeof(mesh->mesh_path) - 1);
            mesh->mesh_path[sizeof(mesh->mesh_path) - 1] = 0;
            mesh->pos[0] = strtof(toks[2], 0);
            mesh->pos[1] = strtof(toks[3], 0);
            mesh->pos[2] = strtof(toks[4], 0);
            mesh->rot[0] = strtof(toks[5], 0);
            mesh->rot[1] = strtof(toks[6], 0);
            mesh->rot[2] = strtof(toks[7], 0);
            mesh->scale[0] = strtof(toks[8], 0);
            mesh->scale[1] = strtof(toks[9], 0);
            mesh->scale[2] = strtof(toks[10], 0);
            for (int i = 0; i < NB_DC_MAX_TEXTURE_SLOTS; ++i) {
                mesh->texture_paths[i][0] = 0;
                if (11 + i < tc && strcmp(toks[11 + i], "-") != 0) {
                    strncpy(mesh->texture_paths[i], toks[11 + i], sizeof(mesh->texture_paths[i]) - 1);
                    mesh->texture_paths[i][sizeof(mesh->texture_paths[i]) - 1] = 0;
                }
            }

            nextState.mesh_count++;
            found = 1;
            continue;
        }
    }

    fclose(f);
    if (!found) return 0;

    nextState.loaded = 1;
    gSceneState = nextState;
    return 1;
}

void NB_DC_UnloadScene(void) {
    memset(&gSceneState, 0, sizeof(gSceneState));
}

int NB_DC_SwitchScene(const char* scenePath) {
    NB_DC_UnloadScene();
    return NB_DC_LoadScene(scenePath);
}

int NB_DC_LoadMesh(const char* meshPath, NB_Mesh* out) {
    if (!meshPath || !meshPath[0] || !out) return 0;
    memset(out, 0, sizeof(*out));

    FILE* f = fopen(meshPath, "rb");
    if (!f) return 0;

    char m[4];
    if (fread(m, 1, 4, f) != 4 || m[0] != 'N' || m[1] != 'E' || m[2] != 'B' || m[3] != 'M') {
        fclose(f);
        return 0;
    }

    uint32_t ver = 0;
    uint32_t flags = 0;
    uint32_t vc = 0;
    uint32_t ic = 0;
    uint32_t pf = 0;
    if (!NB_DC_ReadU32BE(f, &ver) || !NB_DC_ReadU32BE(f, &flags) || !NB_DC_ReadU32BE(f, &vc) ||
        !NB_DC_ReadU32BE(f, &ic) || !NB_DC_ReadU32BE(f, &pf)) {
        fclose(f);
        return 0;
    }
    if (vc == 0 || ic < 3 || vc > 65535 || ic > 262144) {
        fclose(f);
        return 0;
    }

    NB_Vec3* pos = (NB_Vec3*)malloc(sizeof(NB_Vec3) * vc);
    uint16_t* idx = (uint16_t*)malloc(sizeof(uint16_t) * ic);
    if (!pos || !idx) {
        free(pos);
        free(idx);
        fclose(f);
        return 0;
    }

    float inv = (pf < 31) ? (1.0f / (float)(1u << pf)) : (1.0f / 256.0f);
    for (uint32_t i = 0; i < vc; ++i) {
        int16_t x = 0;
        int16_t y = 0;
        int16_t z = 0;
        if (!NB_DC_ReadS16BE(f, &x) || !NB_DC_ReadS16BE(f, &y) || !NB_DC_ReadS16BE(f, &z)) {
            free(pos);
            free(idx);
            fclose(f);
            return 0;
        }
        pos[i].x = x * inv;
        pos[i].y = y * inv;
        pos[i].z = z * inv;
    }

    int hasUv = (flags & 1u) != 0;
    int hasUv1 = (flags & 16u) != 0;
    NB_Vec3* uv = (NB_Vec3*)malloc(sizeof(NB_Vec3) * vc);
    if (!uv) {
        free(pos);
        free(idx);
        fclose(f);
        return 0;
    }
    for (uint32_t i = 0; i < vc; ++i) {
        uv[i].x = 0.0f;
        uv[i].y = 0.0f;
        uv[i].z = 0.0f;
    }
    if (hasUv) {
        for (uint32_t i = 0; i < vc; ++i) {
            int16_t u = 0;
            int16_t v = 0;
            if (!NB_DC_ReadS16BE(f, &u) || !NB_DC_ReadS16BE(f, &v)) {
                free(uv);
                free(pos);
                free(idx);
                fclose(f);
                return 0;
            }
            uv[i].x = (float)u / 256.0f;
            uv[i].y = (float)v / 256.0f;
        }
    }

    NB_Vec3* uv1 = NULL;
    if (hasUv1) {
        uv1 = (NB_Vec3*)malloc(sizeof(NB_Vec3) * vc);
        if (!uv1) {
            free(uv);
            free(pos);
            free(idx);
            fclose(f);
            return 0;
        }
        for (uint32_t i = 0; i < vc; ++i) {
            int16_t u = 0;
            int16_t v = 0;
            if (!NB_DC_ReadS16BE(f, &u) || !NB_DC_ReadS16BE(f, &v)) {
                free(uv1);
                free(uv);
                free(pos);
                free(idx);
                fclose(f);
                return 0;
            }
            uv1[i].x = (float)u / 256.0f;
            uv1[i].y = (float)v / 256.0f;
            uv1[i].z = 0.0f;
        }
    }

    for (uint32_t i = 0; i < ic; ++i) {
        uint16_t t = 0;
        if (!NB_DC_ReadU16BE(f, &t)) {
            free(uv);
            free(pos);
            free(idx);
            fclose(f);
            return 0;
        }
        idx[i] = t;
    }

    uint32_t tc = ic / 3u;
    uint16_t* triMat = (uint16_t*)malloc(sizeof(uint16_t) * tc);
    if (!triMat) {
        free(uv);
        free(pos);
        free(idx);
        fclose(f);
        return 0;
    }
    for (uint32_t t = 0; t < tc; ++t) triMat[t] = 0;
    if (flags & 2u) {
        for (uint32_t t = 0; t < tc; ++t) {
            if (!NB_DC_ReadU16BE(f, &triMat[t])) break;
        }
    }

    NB_Vec3* triUv = (NB_Vec3*)malloc(sizeof(NB_Vec3) * ic);
    if (!triUv) {
        free(uv1);
        free(uv);
        free(pos);
        free(idx);
        free(triMat);
        fclose(f);
        return 0;
    }
    for (uint32_t i = 0; i < ic; ++i) {
        uint16_t vi = idx[i];
        if (vi < vc)
            triUv[i] = uv[vi];
        else {
            triUv[i].x = 0.0f;
            triUv[i].y = 0.0f;
            triUv[i].z = 0.0f;
        }
    }

    NB_Vec3* triUv1 = NULL;
    if (uv1) {
        triUv1 = (NB_Vec3*)malloc(sizeof(NB_Vec3) * ic);
        if (triUv1) {
            for (uint32_t i = 0; i < ic; ++i) {
                uint16_t vi = idx[i];
                if (vi < vc)
                    triUv1[i] = uv1[vi];
                else {
                    triUv1[i].x = 0.0f;
                    triUv1[i].y = 0.0f;
                    triUv1[i].z = 0.0f;
                }
            }
        }
        free(uv1);
    }

    free(uv);
    fclose(f);

    out->pos = pos;
    out->indices = idx;
    out->tri_uv = triUv;
    out->tri_uv1 = triUv1;
    out->tri_mat = triMat;
    out->vert_count = (int)vc;
    out->tri_count = (int)tc;
    out->uv_layer_count = (hasUv ? 1 : 0) + (hasUv1 ? 1 : 0);
    return 1;
}

int NB_DC_LoadTexture(const char* texPath, NB_Texture* out) {
    if (!texPath || !texPath[0] || !out) return 0;
    memset(out, 0, sizeof(*out));

    FILE* f = fopen(texPath, "rb");
    if (!f) return 0;
    char m[4];
    if (fread(m, 1, 4, f) != 4 || m[0] != 'N' || m[1] != 'E' || m[2] != 'B' || m[3] != 'T') {
        fclose(f);
        return 0;
    }

    uint16_t w = 0;
    uint16_t h = 0;
    uint16_t fmt = 0;
    uint16_t flg = 0;
    if (!NB_DC_ReadU16BE(f, &w) || !NB_DC_ReadU16BE(f, &h) || !NB_DC_ReadU16BE(f, &fmt) || !NB_DC_ReadU16BE(f, &flg)) {
        fclose(f);
        return 0;
    }
    if (fmt != 1 || w == 0 || h == 0) {
        fclose(f);
        return 0;
    }

    int tw = 1;
    int th = 1;
    while (tw < w && tw < 1024) tw <<= 1;
    while (th < h && th < 1024) th <<= 1;
    if (tw <= 0 || th <= 0) {
        fclose(f);
        return 0;
    }

    uint16_t* pix = (uint16_t*)malloc((size_t)tw * (size_t)th * 2u);
    if (!pix) {
        fclose(f);
        return 0;
    }
    memset(pix, 0, (size_t)tw * (size_t)th * 2u);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint16_t p = 0;
            if (!NB_DC_ReadU16BE(f, &p)) {
                free(pix);
                fclose(f);
                return 0;
            }
            uint16_t r5 = (p >> 10) & 31;
            uint16_t g5 = (p >> 5) & 31;
            uint16_t b5 = p & 31;
            uint16_t g6 = (uint16_t)((g5 << 1) | (g5 >> 4));
            pix[(size_t)y * (size_t)tw + (size_t)x] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
        }
    }

    /* Extension chunk: filter(1B) wrapMode(1B) flipU(1B) flipV(1B) */
    int filter = (flg & 1u) ? 1 : 0;
    int wrapMode = 0, flipU = 0, flipV = 0;
    int b0 = fgetc(f);
    if (b0 != EOF) {
        if (b0 == 0 || b0 == 1) filter = b0;
        int b1 = fgetc(f);
        if (b1 != EOF) { wrapMode = b1; }
        int b2 = fgetc(f);
        if (b2 != EOF) { flipU = b2; }
        int b3 = fgetc(f);
        if (b3 != EOF) { flipV = b3; }
    }
    fclose(f);

    out->pixels = pix;
    out->w = tw;
    out->h = th;
    out->us = (float)w / (float)tw;
    out->vs = (float)h / (float)th;
    out->filter = filter;
    out->wrapMode = wrapMode;
    out->flipU = flipU;
    out->flipV = flipV;
    return 1;
}

void NB_DC_FreeMesh(NB_Mesh* m) {
    if (!m) return;
    free(m->pos);
    free(m->indices);
    free(m->tri_uv);
    free(m->tri_uv1);
    free(m->tri_mat);
    memset(m, 0, sizeof(*m));
}

void NB_DC_FreeTexture(NB_Texture* t) {
    if (!t) return;
    free(t->pixels);
    memset(t, 0, sizeof(*t));
}

const char* NB_DC_GetSceneName(void) {
    return gSceneState.scene_name;
}

const char* NB_DC_GetSceneMeshPath(void) {
    if (gSceneState.mesh_count <= 0) return "";
    return gSceneState.meshes[0].mesh_path;
}

const char* NB_DC_GetSceneTexturePath(int slotIndex) {
    if (gSceneState.mesh_count <= 0) return "";
    if (slotIndex < 0 || slotIndex >= NB_DC_MAX_TEXTURE_SLOTS) return "";
    return gSceneState.meshes[0].texture_paths[slotIndex];
}

void NB_DC_GetSceneTransform(float outPos[3], float outRot[3], float outScale[3]) {
    if (gSceneState.mesh_count <= 0) {
        if (outPos) outPos[0] = outPos[1] = outPos[2] = 0.0f;
        if (outRot) outRot[0] = outRot[1] = outRot[2] = 0.0f;
        if (outScale) outScale[0] = outScale[1] = outScale[2] = 1.0f;
        return;
    }
    NB_DC_GetSceneTransformAt(0, outPos, outRot, outScale);
}

int NB_DC_GetSceneMeshCount(void) {
    return gSceneState.mesh_count;
}

const char* NB_DC_GetSceneMeshPathAt(int meshIndex) {
    if (meshIndex < 0 || meshIndex >= gSceneState.mesh_count) return "";
    return gSceneState.meshes[meshIndex].mesh_path;
}

const char* NB_DC_GetSceneTexturePathAt(int meshIndex, int slotIndex) {
    if (meshIndex < 0 || meshIndex >= gSceneState.mesh_count) return "";
    if (slotIndex < 0 || slotIndex >= NB_DC_MAX_TEXTURE_SLOTS) return "";
    return gSceneState.meshes[meshIndex].texture_paths[slotIndex];
}

void NB_DC_GetSceneTransformAt(int meshIndex, float outPos[3], float outRot[3], float outScale[3]) {
    if (meshIndex < 0 || meshIndex >= gSceneState.mesh_count) {
        if (outPos) outPos[0] = outPos[1] = outPos[2] = 0.0f;
        if (outRot) outRot[0] = outRot[1] = outRot[2] = 0.0f;
        if (outScale) outScale[0] = outScale[1] = outScale[2] = 1.0f;
        return;
    }
    const NB_DC_SceneMeshState* mesh = &gSceneState.meshes[meshIndex];
    if (outPos) {
        outPos[0] = mesh->pos[0];
        outPos[1] = mesh->pos[1];
        outPos[2] = mesh->pos[2];
    }
    if (outRot) {
        outRot[0] = mesh->rot[0];
        outRot[1] = mesh->rot[1];
        outRot[2] = mesh->rot[2];
    }
    if (outScale) {
        outScale[0] = mesh->scale[0];
        outScale[1] = mesh->scale[1];
        outScale[2] = mesh->scale[2];
    }
}

void __attribute__((weak)) NB_RT_GetNode3DPosition(const char* name, float outPos[3]) {
    (void)name;
    if (outPos) {
        outPos[0] = 0.0f;
        outPos[1] = 0.0f;
        outPos[2] = 0.0f;
    }
}

void __attribute__((weak)) NB_RT_SetNode3DPosition(const char* name, float x, float y, float z) {
    (void)name;
    (void)x;
    (void)y;
    (void)z;
}

void __attribute__((weak)) NB_RT_GetNode3DRotation(const char* name, float outRot[3]) {
    (void)name;
    if (outRot) {
        outRot[0] = 0.0f;
        outRot[1] = 0.0f;
        outRot[2] = 0.0f;
    }
}

void __attribute__((weak)) NB_RT_SetNode3DRotation(const char* name, float x, float y, float z) {
    (void)name;
    (void)x;
    (void)y;
    (void)z;
}

void __attribute__((weak)) NB_RT_GetCameraOrbit(const char* name, float outOrbit[3]) {
    (void)name;
    if (outOrbit) {
        outOrbit[0] = 0.0f;
        outOrbit[1] = 0.0f;
        outOrbit[2] = 0.0f;
    }
}

void __attribute__((weak)) NB_RT_SetCameraOrbit(const char* name, float x, float y, float z) {
    (void)name;
    (void)x;
    (void)y;
    (void)z;
}

void __attribute__((weak)) NB_RT_GetCameraRotation(const char* name, float outRot[3]) {
    (void)name;
    if (outRot) {
        outRot[0] = 0.0f;
        outRot[1] = 0.0f;
        outRot[2] = 0.0f;
    }
}

void __attribute__((weak)) NB_RT_SetCameraRotation(const char* name, float x, float y, float z) {
    (void)name;
    (void)x;
    (void)y;
    (void)z;
}

void __attribute__((weak)) NB_RT_GetCameraWorldForward(const char* name, float outFwd[3]) {
    (void)name;
    if (outFwd) {
        outFwd[0] = 0.0f;
        outFwd[1] = 0.0f;
        outFwd[2] = 1.0f;
    }
}

int __attribute__((weak)) NB_RT_IsCameraUnderNode3D(const char* cameraName, const char* nodeName) {
    (void)cameraName;
    (void)nodeName;
    return 0;
}

int __attribute__((weak)) NB_RT_RaycastDown(float x, float y, float z, float* outHitY) {
    (void)x; (void)y; (void)z; (void)outHitY;
    return 0;
}

int __attribute__((weak)) NB_RT_RaycastDownWithNormal(float x, float y, float z, float* outHitY, float outNormal[3]) {
    (void)x; (void)y; (void)z; (void)outHitY; (void)outNormal;
    return 0;
}

void __attribute__((weak)) NB_RT_GetNode3DCollisionBounds(const char* name, float outExtents[3]) {
    (void)name; if (outExtents) { outExtents[0]=0.5f; outExtents[1]=0.5f; outExtents[2]=0.5f; }
}
void __attribute__((weak)) NB_RT_SetNode3DCollisionBounds(const char* name, float ex, float ey, float ez) {
    (void)name; (void)ex; (void)ey; (void)ez;
}
void __attribute__((weak)) NB_RT_GetNode3DBoundPos(const char* name, float outPos[3]) {
    (void)name; if (outPos) { outPos[0]=0; outPos[1]=0; outPos[2]=0; }
}
void __attribute__((weak)) NB_RT_SetNode3DBoundPos(const char* name, float bx, float by, float bz) {
    (void)name; (void)bx; (void)by; (void)bz;
}
int __attribute__((weak)) NB_RT_GetNode3DPhysicsEnabled(const char* name) { (void)name; return 0; }
void __attribute__((weak)) NB_RT_SetNode3DPhysicsEnabled(const char* name, int enabled) { (void)name; (void)enabled; }
int __attribute__((weak)) NB_RT_GetNode3DCollisionSource(const char* name) { (void)name; return 0; }
void __attribute__((weak)) NB_RT_SetNode3DCollisionSource(const char* name, int enabled) { (void)name; (void)enabled; }
int __attribute__((weak)) NB_RT_GetNode3DSimpleCollision(const char* name) { (void)name; return 0; }
void __attribute__((weak)) NB_RT_SetNode3DSimpleCollision(const char* name, int enabled) { (void)name; (void)enabled; }
float __attribute__((weak)) NB_RT_GetNode3DVelocityY(const char* name) { (void)name; return 0.0f; }
void __attribute__((weak)) NB_RT_SetNode3DVelocityY(const char* name, float vy) { (void)name; (void)vy; }
int __attribute__((weak)) NB_RT_IsNode3DOnFloor(const char* name) { (void)name; return 0; }
int __attribute__((weak)) NB_RT_CheckAABBOverlap(const char* name1, const char* name2) { (void)name1; (void)name2; return 0; }

/* ---- NavMesh DC asset storage ---- */

static uint8_t* gNavMeshData = NULL;
static int      gNavMeshDataSize = 0;

int NB_DC_LoadNavMesh(const char* navPath) {
    NB_DC_FreeNavMesh();
    if (!navPath || !navPath[0]) return 0;

    FILE* f = fopen(navPath, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return 0; }

    gNavMeshData = (uint8_t*)malloc((size_t)sz);
    if (!gNavMeshData) { fclose(f); return 0; }

    if ((long)fread(gNavMeshData, 1, (size_t)sz, f) != sz) {
        free(gNavMeshData);
        gNavMeshData = NULL;
        fclose(f);
        return 0;
    }
    fclose(f);
    gNavMeshDataSize = (int)sz;
    return 1;
}

void NB_DC_FreeNavMesh(void) {
    free(gNavMeshData);
    gNavMeshData = NULL;
    gNavMeshDataSize = 0;
}

int NB_DC_NavMeshIsLoaded(void) {
    return (gNavMeshData != NULL && gNavMeshDataSize > 0) ? 1 : 0;
}

const void* NB_DC_GetNavMeshData(int* outSize) {
    if (outSize) *outSize = gNavMeshDataSize;
    return gNavMeshData;
}

/* ---- Weak fallback stubs for NB_RT_Animation ---- */

void __attribute__((weak)) NB_RT_PlayAnimation(const char* meshName, const char* animName) { (void)meshName; (void)animName; }
void __attribute__((weak)) NB_RT_StopAnimation(const char* meshName) { (void)meshName; }
int  __attribute__((weak)) NB_RT_IsAnimationPlaying(const char* meshName) { (void)meshName; return 0; }
int  __attribute__((weak)) NB_RT_IsAnimationFinished(const char* meshName) { (void)meshName; return 0; }
void __attribute__((weak)) NB_RT_SetAnimationSpeed(const char* meshName, float speed) { (void)meshName; (void)speed; }

/* ---- Weak fallback stub for NB_RT_PlayVmuLayer ---- */

void __attribute__((weak)) NB_RT_PlayVmuLayer(int layer) { (void)layer; }

/* ---- Weak fallback stubs for NB_RT_NavMesh ---- */

int __attribute__((weak)) NB_RT_NavMeshBuild(void) { return 0; }
void __attribute__((weak)) NB_RT_NavMeshClear(void) {}
int __attribute__((weak)) NB_RT_NavMeshIsReady(void) { return 0; }
int __attribute__((weak)) NB_RT_NavMeshFindPath(float sx, float sy, float sz, float gx, float gy, float gz, float* outPath, int maxPoints) {
    (void)sx; (void)sy; (void)sz; (void)gx; (void)gy; (void)gz; (void)outPath; (void)maxPoints;
    return 0;
}
int __attribute__((weak)) NB_RT_NavMeshFindRandomPoint(float outPos[3]) { (void)outPos; return 0; }
int __attribute__((weak)) NB_RT_NavMeshFindClosestPoint(float px, float py, float pz, float outPos[3]) {
    (void)px; (void)py; (void)pz; (void)outPos;
    return 0;
}
