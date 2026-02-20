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

typedef struct NB_DC_SceneState {
    int loaded;
    char scene_name[64];
    char mesh_path[128];
    char texture_paths[16][128];
    float pos[3];
    float rot[3];
    float scale[3];
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

    NB_DC_SceneState nextState;
    memset(&nextState, 0, sizeof(nextState));
    nextState.scale[0] = 1.0f;
    nextState.scale[1] = 1.0f;
    nextState.scale[2] = 1.0f;

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
            strncpy(nextState.mesh_path, toks[1], sizeof(nextState.mesh_path) - 1);
            nextState.mesh_path[sizeof(nextState.mesh_path) - 1] = 0;
            nextState.pos[0] = strtof(toks[2], 0);
            nextState.pos[1] = strtof(toks[3], 0);
            nextState.pos[2] = strtof(toks[4], 0);
            nextState.rot[0] = strtof(toks[5], 0);
            nextState.rot[1] = strtof(toks[6], 0);
            nextState.rot[2] = strtof(toks[7], 0);
            nextState.scale[0] = strtof(toks[8], 0);
            nextState.scale[1] = strtof(toks[9], 0);
            nextState.scale[2] = strtof(toks[10], 0);
            for (int i = 0; i < 16; ++i) {
                nextState.texture_paths[i][0] = 0;
                if (11 + i < tc && strcmp(toks[11 + i], "-") != 0) {
                    strncpy(nextState.texture_paths[i], toks[11 + i], sizeof(nextState.texture_paths[i]) - 1);
                    nextState.texture_paths[i][sizeof(nextState.texture_paths[i]) - 1] = 0;
                }
            }
            found = 1;
            break;
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

    free(uv);
    fclose(f);

    out->pos = pos;
    out->indices = idx;
    out->tri_uv = triUv;
    out->tri_mat = triMat;
    out->vert_count = (int)vc;
    out->tri_count = (int)tc;
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

    int tail = fgetc(f);
    int filter = (flg & 1u) ? 1 : 0;
    if (tail == 0 || tail == 1) filter = tail;
    fclose(f);

    out->pixels = pix;
    out->w = tw;
    out->h = th;
    out->us = (float)w / (float)tw;
    out->vs = (float)h / (float)th;
    out->filter = filter;
    return 1;
}

void NB_DC_FreeMesh(NB_Mesh* m) {
    if (!m) return;
    free(m->pos);
    free(m->indices);
    free(m->tri_uv);
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
    return gSceneState.mesh_path;
}

const char* NB_DC_GetSceneTexturePath(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= 16) return "";
    return gSceneState.texture_paths[slotIndex];
}

void NB_DC_GetSceneTransform(float outPos[3], float outRot[3], float outScale[3]) {
    if (outPos) {
        outPos[0] = gSceneState.pos[0];
        outPos[1] = gSceneState.pos[1];
        outPos[2] = gSceneState.pos[2];
    }
    if (outRot) {
        outRot[0] = gSceneState.rot[0];
        outRot[1] = gSceneState.rot[1];
        outRot[2] = gSceneState.rot[2];
    }
    if (outScale) {
        outScale[0] = gSceneState.scale[0];
        outScale[1] = gSceneState.scale[1];
        outScale[2] = gSceneState.scale[2];
    }
}
