#include "static_mesh_render.h"
#include "viewport_render.h"
#include "background.h"
#include "node_helpers.h"
#include "../editor/editor_state.h"
#include "../editor/project.h"
#include "../io/mesh_io.h"
#include "../io/meta_io.h"
#include "../io/anim_io.h"
#include "../io/texture_io.h"
#include "../ui/mesh_inspector.h"
#include "../nodes/NodeTypes.h"

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

// Cached centroid+scale for anim-to-mesh mapping (computed once per anim+mesh pair)
struct AnimPoseCache { Vec3 aC, bC; float ds; };
static std::unordered_map<std::string, AnimPoseCache> gAnimPoseCache;

// 4x4 Bayer ordered dither matrix (normalized 0-1 thresholds for 16 levels)
static const float kBayer4x4[4][4] = {
    {  0.0f/16.0f,  8.0f/16.0f,  2.0f/16.0f, 10.0f/16.0f },
    { 12.0f/16.0f,  4.0f/16.0f, 14.0f/16.0f,  6.0f/16.0f },
    {  3.0f/16.0f, 11.0f/16.0f,  1.0f/16.0f,  9.0f/16.0f },
    { 15.0f/16.0f,  7.0f/16.0f, 13.0f/16.0f,  5.0f/16.0f },
};

// Generate a 32x32 stipple pattern from a continuous threshold (0=opaque, 1=fully transparent)
// Ramp applies a power curve to the threshold for non-linear falloff.
static void GenerateStipplePattern(float threshold, float ramp, GLubyte outPattern[128])
{
    float t = std::max(0.0f, std::min(1.0f, threshold));
    if (ramp != 1.0f && ramp > 0.0f) t = powf(t, 1.0f / ramp);
    for (int row = 0; row < 32; ++row)
    {
        GLubyte rowBytes[4] = { 0, 0, 0, 0 };
        for (int col = 0; col < 32; ++col)
        {
            float bayerVal = kBayer4x4[row % 4][col % 4];
            // Pixel is drawn (bit=1) if bayer threshold >= transparency amount
            if (bayerVal >= t) rowBytes[col / 8] |= (GLubyte)(0x80 >> (col % 8));
        }
        outPattern[row * 4 + 0] = rowBytes[0];
        outPattern[row * 4 + 1] = rowBytes[1];
        outPattern[row * 4 + 2] = rowBytes[2];
        outPattern[row * 4 + 3] = rowBytes[3];
    }
}

#include <GLFW/glfw3.h>

#ifndef GL_POLYGON_OFFSET_LINE
#define GL_POLYGON_OFFSET_LINE 0x2A02
#endif

void RenderStaticMeshNodes()
{
    // StaticMesh3D rendering (basic)
    glPolygonMode(GL_FRONT_AND_BACK, gWireframePreview ? GL_LINE : GL_FILL);
    for (int i = 0; i < (int)gStaticMeshNodes.size(); ++i)
    {
        const auto& s = gStaticMeshNodes[i];
        const bool selected = (gSelectedStaticMesh == i) || gMultiSelectedStaticMesh.count(i);
        if (s.mesh.empty() || gProjectDir.empty()) continue;
        std::filesystem::path meshPath = std::filesystem::path(gProjectDir) / s.mesh;
        const NebMesh* mesh = GetNebMesh(meshPath);
        if (!mesh || !mesh->valid) continue;

        std::vector<Vec3> staticAnimPosed;
        const std::vector<Vec3>* renderPositions = &mesh->positions;
        // Determine which anim to use: slot preview > play-mode active slot > legacy vtxAnim
        std::string resolvedAnimRef;
        if (i == gStaticAnimPreviewNode && gStaticAnimPreviewSlot >= 0 && gStaticAnimPreviewSlot < s.animSlotCount && !s.animSlots[gStaticAnimPreviewSlot].path.empty())
            resolvedAnimRef = s.animSlots[gStaticAnimPreviewSlot].path;
        else if (gPlayMode && s.runtimeTest && gEditorAnimActiveSlot.count((int)i) && gEditorAnimActiveSlot[(int)i] >= 0 && gEditorAnimActiveSlot[(int)i] < s.animSlotCount)
            resolvedAnimRef = s.animSlots[gEditorAnimActiveSlot[(int)i]].path;
        else if (!s.vtxAnim.empty())
            resolvedAnimRef = s.vtxAnim;
        if (!resolvedAnimRef.empty())
        {
            std::filesystem::path animPath = ResolveProjectAssetPath(resolvedAnimRef);
            if (!animPath.empty() && std::filesystem::exists(animPath))
            {
                std::string key = animPath.generic_string();
                auto it = gStaticAnimClipCache.find(key);
                if (it == gStaticAnimClipCache.end())
                {
                    NebAnimClip clip;
                    std::string err;
                    if (LoadNebAnimClip(animPath, clip, err))
                        it = gStaticAnimClipCache.emplace(key, std::move(clip)).first;
                }
                if (it != gStaticAnimClipCache.end() && it->second.valid && it->second.vertexCount <= mesh->positions.size() && !it->second.frames.empty())
                {
                    int frame = 0;
                    if (gPlayMode && s.runtimeTest && gEditorAnimPlaying.count((int)i) && gEditorAnimPlaying[(int)i])
                    {
                        float elapsed = gEditorAnimTime.count((int)i) ? gEditorAnimTime[(int)i] : 0.0f;
                        float speed = gEditorAnimSpeed.count((int)i) ? gEditorAnimSpeed[(int)i] : 1.0f;
                        const float fps = std::max(1.0f, it->second.fps);
                        int f = (int)std::floor(elapsed * fps);
                        if (it->second.frameCount > 0)
                            frame = f % (int)it->second.frameCount;
                    }
                    else if (gPlayMode && s.runtimeTest)
                    {
                        const float fps = std::max(1.0f, it->second.fps);
                        int f = (int)std::floor((float)glfwGetTime() * fps);
                        if (it->second.frameCount > 0)
                            frame = f % (int)it->second.frameCount;
                    }
                    else if (i == gStaticAnimPreviewNode)
                    {
                        frame = std::max(0, std::min(gStaticAnimPreviewFrame, (int)it->second.frameCount - 1));
                    }
                    frame = std::max(0, std::min(frame, (int)it->second.frames.size() - 1));

                    const std::vector<Vec3>& f0 = it->second.frames[0];
                    const std::vector<Vec3>& ff = it->second.frames[(size_t)frame];
                    const size_t nv = std::min(f0.size(), std::min(ff.size(), mesh->positions.size()));
                    if (nv > 0)
                    {
                        staticAnimPosed.resize(mesh->positions.size());
                        if (it->second.meshAligned)
                        {
                            // Animation already in mesh-local space — direct replacement
                            for (size_t vi = 0; vi < nv; ++vi)
                                staticAnimPosed[vi] = ff[vi];
                        }
                        else
                        {
                            // Look up or compute cached centroid+scale (once per anim+mesh pair)
                            std::string poseKey = key + "|" + meshPath.generic_string();
                            auto pcIt = gAnimPoseCache.find(poseKey);
                            if (pcIt == gAnimPoseCache.end())
                            {
                                Vec3 bMin = mesh->positions[0], bMax = mesh->positions[0], bC = {0,0,0};
                                Vec3 aMin = f0[0], aMax = f0[0], aC = {0,0,0};
                                for (size_t vi = 0; vi < nv; ++vi)
                                {
                                    const Vec3& bp = mesh->positions[vi];
                                    const Vec3& ap = f0[vi];
                                    bC.x += bp.x; bC.y += bp.y; bC.z += bp.z;
                                    aC.x += ap.x; aC.y += ap.y; aC.z += ap.z;
                                    bMin.x = std::min(bMin.x, bp.x); bMin.y = std::min(bMin.y, bp.y); bMin.z = std::min(bMin.z, bp.z);
                                    bMax.x = std::max(bMax.x, bp.x); bMax.y = std::max(bMax.y, bp.y); bMax.z = std::max(bMax.z, bp.z);
                                    aMin.x = std::min(aMin.x, ap.x); aMin.y = std::min(aMin.y, ap.y); aMin.z = std::min(aMin.z, ap.z);
                                    aMax.x = std::max(aMax.x, ap.x); aMax.y = std::max(aMax.y, ap.y); aMax.z = std::max(aMax.z, ap.z);
                                }
                                const float invN = 1.0f / (float)nv;
                                bC.x *= invN; bC.y *= invN; bC.z *= invN;
                                aC.x *= invN; aC.y *= invN; aC.z *= invN;
                                const float bDiag = sqrtf((bMax.x-bMin.x)*(bMax.x-bMin.x) + (bMax.y-bMin.y)*(bMax.y-bMin.y) + (bMax.z-bMin.z)*(bMax.z-bMin.z));
                                const float aDiag = sqrtf((aMax.x-aMin.x)*(aMax.x-aMin.x) + (aMax.y-aMin.y)*(aMax.y-aMin.y) + (aMax.z-aMin.z)*(aMax.z-aMin.z));
                                float ds = 1.0f;
                                if (aDiag > 1e-6f && bDiag > 1e-6f) ds = bDiag / aDiag;
                                pcIt = gAnimPoseCache.emplace(poseKey, AnimPoseCache{aC, bC, ds}).first;
                            }
                            const Vec3& aC = pcIt->second.aC;
                            const Vec3& bC = pcIt->second.bC;
                            const float ds = pcIt->second.ds;

                            for (size_t vi = 0; vi < nv; ++vi)
                            {
                                staticAnimPosed[vi].x = bC.x + (ff[vi].x - aC.x) * ds;
                                staticAnimPosed[vi].y = bC.y + (ff[vi].y - aC.y) * ds;
                                staticAnimPosed[vi].z = bC.z + (ff[vi].z - aC.z) * ds;
                            }
                        }
                        for (size_t vi = nv; vi < mesh->positions.size(); ++vi)
                            staticAnimPosed[vi] = mesh->positions[vi];
                        renderPositions = &staticAnimPosed;
                    }
                }
            }
        }

        struct MatState { GLuint tex = 0; bool flipU = false; bool flipV = false; float satU = 1.0f; float satV = 1.0f; float uvScale = 0.0f; float uvScaleU = 1.0f; float uvScaleV = 1.0f; int shadingMode = 0; float lightRotation = 0.0f; float lightPitch = 0.0f; float lightRoll = 0.0f; float shadowIntensity = 1.0f; int shadingUv = -1; float stipple = 0.0f; float stippleTintR = 1.0f; float stippleTintG = 1.0f; float stippleTintB = 1.0f; float stippleIntensity = 1.0f; float stippleAlpha = 1.0f; float stippleRamp = 1.0f; int cullFace = -1; float opacity = 1.0f; };
        std::unordered_map<int, MatState> matState;
        auto getMatState = [&](int matIndex) -> MatState {
            auto it = matState.find(matIndex);
            if (it != matState.end()) return it->second;

            MatState st{};
            std::string matRef = NebulaNodes::GetStaticMeshMaterialByIndex(s, matIndex);
            if (matRef.empty() && matIndex == 0) matRef = s.material; // legacy scene fallback
            if (!matRef.empty())
            {
                std::filesystem::path matPath = std::filesystem::path(gProjectDir) / matRef;
                if (matPath.extension() == ".nebtex")
                {
                    st.tex = GetNebTexture(matPath);
                    if (st.tex == 0 && std::filesystem::exists(matPath))
                    {
                        gNebTextureCache.erase(matPath.string());
                        st.tex = GetNebTexture(matPath);
                    }
                    NebulaAssets::LoadNebTexFlipOptions(matPath, st.flipU, st.flipV);
                    if (gPreviewSaturnSampling)
                        GetNebTexSaturnPadUvScale(matPath, st.satU, st.satV);
                }
                else
                {
                    NebulaAssets::LoadMaterialUvScale(matPath, st.uvScale);
                    { float su=1,sv=1,ou=0,ov=0,rd=0; NebulaAssets::LoadMaterialUvTransform(matPath,su,sv,ou,ov,rd); st.uvScaleU=su; st.uvScaleV=sv; }
                    st.shadingMode = NebulaAssets::LoadMaterialShadingMode(matPath);
                    st.lightRotation = NebulaAssets::LoadMaterialLightRotation(matPath);
                    st.lightPitch = NebulaAssets::LoadMaterialLightPitch(matPath);
                    st.lightRoll = NebulaAssets::LoadMaterialLightRoll(matPath);
                    st.shadowIntensity = NebulaAssets::LoadMaterialShadowIntensity(matPath);
                    st.shadingUv = NebulaAssets::LoadMaterialShadingUv(matPath);
                    st.stipple = NebulaAssets::LoadMaterialStipple(matPath);
                    NebulaAssets::LoadMaterialStippleTint(matPath, st.stippleTintR, st.stippleTintG, st.stippleTintB);
                    st.stippleIntensity = NebulaAssets::LoadMaterialStippleIntensity(matPath);
                    st.stippleAlpha = NebulaAssets::LoadMaterialStippleAlpha(matPath);
                    st.stippleRamp = NebulaAssets::LoadMaterialStippleRamp(matPath);
                    st.cullFace = NebulaAssets::LoadMaterialCullFace(matPath);
                    st.opacity = NebulaAssets::LoadMaterialOpacity(matPath);
                    std::string texPath;
                    if (NebulaAssets::LoadMaterialTexture(matPath, texPath) && !texPath.empty())
                    {
                        std::filesystem::path tpath = std::filesystem::path(gProjectDir) / texPath;
                        st.tex = GetNebTexture(tpath);
                        if (st.tex == 0 && std::filesystem::exists(tpath))
                        {
                            gNebTextureCache.erase(tpath.string());
                            st.tex = GetNebTexture(tpath);
                        }
                        if (tpath.extension() == ".nebtex")
                        {
                            NebulaAssets::LoadNebTexFlipOptions(tpath, st.flipU, st.flipV);
                            if (gPreviewSaturnSampling)
                                GetNebTexSaturnPadUvScale(tpath, st.satU, st.satV);
                        }
                    }
                }
            }
            matState[matIndex] = st;
            return st;
        };

        int seedMatIndex = s.materialSlot;
        for (size_t idx = 0; idx + 2 < mesh->indices.size(); idx += 3)
        {
            uint16_t i0 = mesh->indices[idx + 0];
            uint16_t i1 = mesh->indices[idx + 1];
            uint16_t i2 = mesh->indices[idx + 2];
            if (i0 >= mesh->positions.size() || i1 >= mesh->positions.size() || i2 >= mesh->positions.size())
                continue;
            int triIndex = (int)(idx / 3);
            if (mesh->hasFaceMaterial && triIndex >= 0 && triIndex < (int)mesh->faceMaterial.size())
                seedMatIndex = (int)mesh->faceMaterial[triIndex];
            break;
        }

        // Warm all slot material states up-front so slot visibility is not dependent on draw order.
        for (int si = 0; si < kStaticMeshMaterialSlots; ++si) (void)getMatState(si);

        // Check if any material slot uses lit shading
        bool anyLit = false;
        for (auto& kv : matState) { if (kv.second.shadingMode == 1 || kv.second.shadingUv >= 0) { anyLit = true; break; } }

        // Compute smooth vertex normals by position so shading is
        // always smooth across the entire geometry, even across
        // material/UV seam splits where vertices are duplicated.
        // Groups by original (rest-pose) mesh positions for stable
        // welding even under animation, computes normals from rendered
        // positions for correct lighting.
        std::vector<Vec3> smoothNormals;
        if (anyLit && renderPositions && !mesh->indices.empty())
        {
            const size_t vertCount = renderPositions->size();
            const std::vector<Vec3>& basePos = mesh->positions; // rest-pose for grouping

            // Compute adaptive weld distance from bounding box diagonal
            // so it works regardless of model scale.
            Vec3 bMin = basePos[0], bMax = basePos[0];
            for (size_t vi = 1; vi < vertCount && vi < basePos.size(); ++vi)
            {
                const Vec3& p = basePos[vi];
                if (p.x < bMin.x) bMin.x = p.x; if (p.x > bMax.x) bMax.x = p.x;
                if (p.y < bMin.y) bMin.y = p.y; if (p.y > bMax.y) bMax.y = p.y;
                if (p.z < bMin.z) bMin.z = p.z; if (p.z > bMax.z) bMax.z = p.z;
            }
            float diag = sqrtf((bMax.x-bMin.x)*(bMax.x-bMin.x) + (bMax.y-bMin.y)*(bMax.y-bMin.y) + (bMax.z-bMin.z)*(bMax.z-bMin.z));
            const float weldDist = std::max(0.01f, diag * 0.005f); // 0.5% of bounding box
            const float weldDist2 = weldDist * weldDist;
            std::vector<size_t> vertToGroup(vertCount, (size_t)-1);
            std::vector<Vec3> groupNormalAccum;
            size_t numGroups = 0;

            for (size_t vi = 0; vi < vertCount; ++vi)
            {
                if (vertToGroup[vi] != (size_t)-1) continue;
                const Vec3& pa = (vi < basePos.size()) ? basePos[vi] : (*renderPositions)[vi];
                size_t gi = numGroups++;
                vertToGroup[vi] = gi;
                groupNormalAccum.push_back({0.0f, 0.0f, 0.0f});
                // Find all other unassigned verts at the same position
                for (size_t vj = vi + 1; vj < vertCount; ++vj)
                {
                    if (vertToGroup[vj] != (size_t)-1) continue;
                    const Vec3& pb = (vj < basePos.size()) ? basePos[vj] : (*renderPositions)[vj];
                    float dx = pa.x - pb.x, dy = pa.y - pb.y, dz = pa.z - pb.z;
                    if (dx*dx + dy*dy + dz*dz <= weldDist2)
                        vertToGroup[vj] = gi;
                }
            }

            // Accumulate face normals per position group
            for (size_t idx = 0; idx + 2 < mesh->indices.size(); idx += 3)
            {
                uint16_t i0 = mesh->indices[idx + 0];
                uint16_t i1 = mesh->indices[idx + 1];
                uint16_t i2 = mesh->indices[idx + 2];
                if (i0 >= vertCount || i1 >= vertCount || i2 >= vertCount)
                    continue;
                const Vec3& p0 = (*renderPositions)[i0];
                const Vec3& p1 = (*renderPositions)[i1];
                const Vec3& p2 = (*renderPositions)[i2];
                float ex = p1.x - p0.x, ey = p1.y - p0.y, ez = p1.z - p0.z;
                float fx = p2.x - p0.x, fy = p2.y - p0.y, fz = p2.z - p0.z;
                // Negate cross product to compensate for flipped winding from
                // import basis X-negate (handedness flip reverses triangle order)
                float nx = -(ey * fz - ez * fy);
                float ny = -(ez * fx - ex * fz);
                float nz = -(ex * fy - ey * fx);
                size_t g0 = vertToGroup[i0], g1 = vertToGroup[i1], g2 = vertToGroup[i2];
                groupNormalAccum[g0].x += nx; groupNormalAccum[g0].y += ny; groupNormalAccum[g0].z += nz;
                groupNormalAccum[g1].x += nx; groupNormalAccum[g1].y += ny; groupNormalAccum[g1].z += nz;
                groupNormalAccum[g2].x += nx; groupNormalAccum[g2].y += ny; groupNormalAccum[g2].z += nz;
            }

            // Normalize group normals
            for (auto& n : groupNormalAccum)
            {
                float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
                if (len > 1e-8f) { n.x /= len; n.y /= len; n.z /= len; }
                else { n.x = 0.0f; n.y = 1.0f; n.z = 0.0f; }
            }

            // Assign the shared group normal to every vertex index
            smoothNormals.resize(vertCount);
            for (size_t vi = 0; vi < vertCount; ++vi)
                smoothNormals[vi] = groupNormalAccum[vertToGroup[vi]];

            // DEBUG: print weld stats once per mesh per frame
            static int dbgFrame = 0;
            if (dbgFrame++ < 5)
                fprintf(stderr, "[SMOOTH] verts=%zu groups=%zu weldDist=%.6f diag=%.4f\n", vertCount, numGroups, weldDist, diag);
        }

        // Set up GL lighting if any material is lit
        if (anyLit)
        {
            glEnable(GL_LIGHTING);
            glEnable(GL_LIGHT0);
            glEnable(GL_COLOR_MATERIAL);
            glEnable(GL_NORMALIZE); // renormalize after modelview scale
            glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
            glShadeModel(GL_SMOOTH);
        }

        MatState primaryState = getMatState(seedMatIndex);
        if (primaryState.tex != 0 && mesh->hasUv)
        {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, primaryState.tex);
        }
        else
        {
            glDisable(GL_TEXTURE_2D);
        }

        float wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz;
        GetStaticMeshWorldTRS(i, wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz);
        glPushMatrix();
        glTranslatef(wx, wy, wz);
        // If parented under Node3D, use quaternion rotation (avoids Euler gimbal lock)
        int parentNode3DIdx = -1;
        if (!s.parent.empty()) parentNode3DIdx = FindNode3DByName(s.parent);
        if (parentNode3DIdx >= 0)
        {
            // Parent (Node3D) rotation via quaternion matrix
            float pqw, pqx, pqy, pqz, _px, _py, _pz, _psx, _psy, _psz;
            GetNode3DWorldTRSQuat(parentNode3DIdx, _px, _py, _pz, pqw, pqx, pqy, pqz, _psx, _psy, _psz);
            { float rm[16]; QuatToGLMatrix(pqw, pqx, pqy, pqz, rm); glMultMatrixf(rm); }
            // Child local rotation on top (visual offset)
            glRotatef(s.rotX, 1.0f, 0.0f, 0.0f);
            glRotatef(s.rotY, 0.0f, 1.0f, 0.0f);
            glRotatef(s.rotZ, 0.0f, 0.0f, 1.0f);
        }
        else
        {
            // Standalone StaticMesh rotation axis remap:
            // X <- Z (pitch/back-forth), Y <- X (yaw left-right), Z <- Y (roll/spin)
            glRotatef(wrz, 1.0f, 0.0f, 0.0f);
            glRotatef(wrx, 0.0f, 1.0f, 0.0f);
            glRotatef(wry, 0.0f, 0.0f, 1.0f);
        }
        glScalef(wsx, wsy, wsz);

        // Transform smooth normals to camera space via full modelview matrix.
        // This gives view-dependent shading (light moves with camera) matching DC build.
        // Note: material light values will visually match DC when the editor camera
        // is at the same angle as the DC game camera.
        std::vector<Vec3> csNormals;
        if (!smoothNormals.empty())
        {
            float mv[16];
            glGetFloatv(GL_MODELVIEW_MATRIX, mv);
            csNormals.resize(smoothNormals.size());
            for (size_t vi = 0; vi < smoothNormals.size(); ++vi)
            {
                const Vec3& n = smoothNormals[vi];
                float tx = mv[0]*n.x + mv[4]*n.y + mv[8]*n.z;
                float ty = mv[1]*n.x + mv[5]*n.y + mv[9]*n.z;
                float tz = mv[2]*n.x + mv[6]*n.y + mv[10]*n.z;
                float len = sqrtf(tx*tx + ty*ty + tz*tz);
                if (len > 1e-8f) { csNormals[vi] = {tx/len, ty/len, tz/len}; }
                else { csNormals[vi] = {0.0f, 1.0f, 0.0f}; }
            }
        }

        auto setWireColorForMat = [&](int faceMat)
        {
            if (selected) { glColor3f(1.0f, 1.0f, 1.0f); return; }
            float r = 0.45f + 0.40f * fabsf(sinf((float)(faceMat * 1.7f + 0.3f)));
            float g = 0.45f + 0.40f * fabsf(sinf((float)(faceMat * 2.3f + 1.1f)));
            float b = 0.45f + 0.40f * fabsf(sinf((float)(faceMat * 1.1f + 2.2f)));
            glColor3f(r, g, b);
        };

        float curStipple = 0.0f;
        float curOpacity = 1.0f;
        int curCullFace = -1; // -1 = uninitialized
        if (gWireframePreview && mesh->hasFaceTopology && !mesh->faceVertexCounts.empty())
        {
            // Quad/polygon wireframe path: draw polygon boundary edges from source face topology
            // so quads do not show triangulation diagonals.
            glDisable(GL_TEXTURE_2D);
            glBegin(GL_LINES);
            int triCursor = 0;
            const int triCount = (int)(mesh->indices.size() / 3);
            for (size_t fi = 0; fi < mesh->faceVertexCounts.size() && triCursor < triCount; ++fi)
            {
                int fv = (int)mesh->faceVertexCounts[fi];
                if (fv < 3) continue;
                int faceTriCount = std::max(1, fv - 2);
                if (triCursor + faceTriCount > triCount) break;

                int faceMat = 0;
                if (mesh->hasFaceMaterial && triCursor >= 0 && triCursor < (int)mesh->faceMaterial.size())
                    faceMat = (int)mesh->faceMaterial[triCursor];
                setWireColorForMat(faceMat);

                std::vector<uint16_t> poly;
                poly.reserve((size_t)fv);
                uint16_t v0 = mesh->indices[triCursor * 3 + 0];
                uint16_t v1 = mesh->indices[triCursor * 3 + 1];
                poly.push_back(v0);
                poly.push_back(v1);
                for (int k = 0; k < faceTriCount; ++k)
                {
                    uint16_t vk = mesh->indices[(triCursor + k) * 3 + 2];
                    poly.push_back(vk);
                }
                triCursor += faceTriCount;

                for (size_t k = 0; k < poly.size(); ++k)
                {
                    uint16_t ia = poly[k];
                    uint16_t ib = poly[(k + 1) % poly.size()];
                    if (ia >= renderPositions->size() || ib >= renderPositions->size()) continue;
                    const Vec3& a = (*renderPositions)[ia];
                    const Vec3& b = (*renderPositions)[ib];
                    glVertex3f(a.x, a.y, a.z);
                    glVertex3f(b.x, b.y, b.z);
                }
            }
            glEnd();
        }
        else
        {
            if (gWireframePreview) glDisable(GL_TEXTURE_2D);
            // If no material is lit, disable lighting for this mesh's triangles
            if (!anyLit) glDisable(GL_LIGHTING);
            glBegin(GL_TRIANGLES);
            // Force first triangle to bind from its own face material slot.
            GLuint boundTex = 0xFFFFFFFFu;
            int curLit = -1; // -1 = uninitialized
            int curShadingUv = -2; // -2 = uninitialized
            float curLightRot = -999.0f;
            float curLightPit = -999.0f;
            float curLightRol = -999.0f;
            float curShadowInt = -999.0f;
            float swLx = 0.0f, swLy = -1.0f, swLz = 0.0f;
            float swAmb = 0.35f, swDif = 0.9f, swShQ = 0.0f;
            for (size_t idx = 0; idx + 2 < mesh->indices.size(); idx += 3)
            {
                uint16_t i0 = mesh->indices[idx + 0];
                uint16_t i1 = mesh->indices[idx + 1];
                uint16_t i2 = mesh->indices[idx + 2];
                if (i0 >= renderPositions->size() || i1 >= renderPositions->size() || i2 >= renderPositions->size())
                    continue;

                int triIndex = (int)(idx / 3);
                int faceMat = 0;
                if (mesh->hasFaceMaterial && triIndex >= 0 && triIndex < (int)mesh->faceMaterial.size())
                    faceMat = (int)mesh->faceMaterial[triIndex];
                MatState triState = getMatState(faceMat);
                if (gWireframePreview) setWireColorForMat(faceMat);
                else if (triState.stipple > 0.0f) { float si = triState.stippleIntensity; glColor4f(triState.stippleTintR * si, triState.stippleTintG * si, triState.stippleTintB * si, triState.stippleAlpha); }
                else if (triState.opacity < 1.0f) glColor4f(1.0f, 1.0f, 1.0f, triState.opacity);
                else glColor3f(1.0f, 1.0f, 1.0f);
                int triLit = triState.shadingMode;
                bool needRestart = (triState.tex != boundTex) || (triLit != curLit) || (triState.shadingUv != curShadingUv) || (triState.stipple != curStipple) || (triState.opacity != curOpacity) || (triState.cullFace != curCullFace) || ((triLit == 1 || triState.shadingUv >= 0) && (triState.lightRotation != curLightRot || triState.lightPitch != curLightPit || triState.lightRoll != curLightRol || triState.shadowIntensity != curShadowInt));
                if (needRestart)
                {
                    glEnd();
                    if (triLit != curLit || triState.shadingUv != curShadingUv)
                    {
                        // All shading now done in software to match DC build exactly
                        glDisable(GL_LIGHTING);
                        curLit = triLit;
                        curShadingUv = triState.shadingUv;
                    }
                    if ((triLit == 1 || triState.shadingUv >= 0) && (triState.lightRotation != curLightRot || triState.lightPitch != curLightPit || triState.lightRoll != curLightRol))
                    {
                        // Match DC build light direction: spherical coords from yaw/pitch
                        float yRad = triState.lightRotation * 3.14159265f / 180.0f;
                        float xRad = triState.lightPitch * 3.14159265f / 180.0f;
                        float dx = sinf(yRad) * cosf(xRad);
                        float dy = sinf(xRad);
                        float dz = cosf(yRad) * cosf(xRad);
                        float len = sqrtf(dx * dx + dy * dy + dz * dz);
                        if (len > 1e-8f) { dx /= len; dy /= len; dz /= len; }
                        else { dx = 0.0f; dy = -1.0f; dz = 0.0f; }
                        swLx = dx; swLy = dy; swLz = dz;
                        curLightRot = triState.lightRotation;
                        curLightPit = triState.lightPitch;
                        curLightRol = triState.lightRoll;
                    }
                    if ((triLit == 1 || triState.shadingUv >= 0) && triState.shadowIntensity != curShadowInt)
                    {
                        float si = triState.shadowIntensity;
                        if (si < 0.0f) si = 0.0f; if (si > 2.0f) si = 2.0f;
                        swAmb = 0.35f;
                        swDif = 0.9f - 0.25f * si;
                        swShQ = 0.25f * si;
                        curShadowInt = triState.shadowIntensity;
                    }
                    if (triState.tex != 0 && mesh->hasUv) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, triState.tex); }
                    else glDisable(GL_TEXTURE_2D);
                    if (triState.stipple != curStipple)
                    {
                        if (triState.stipple > 0.0f)
                        {
                            GLubyte dynPattern[128];
                            GenerateStipplePattern(triState.stipple, triState.stippleRamp, dynPattern);
                            glEnable(GL_POLYGON_STIPPLE);
                            glPolygonStipple(dynPattern);
                        }
                        else
                        {
                            glDisable(GL_POLYGON_STIPPLE);
                        }
                        // Alpha blending for stipple alpha
                        if (triState.stipple > 0.0f && triState.stippleAlpha < 1.0f)
                        {
                            glEnable(GL_BLEND);
                            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                            glColor4f(triState.stippleTintR * triState.stippleIntensity,
                                      triState.stippleTintG * triState.stippleIntensity,
                                      triState.stippleTintB * triState.stippleIntensity,
                                      triState.stippleAlpha);
                        }
                        else if (curStipple > 0.0f)
                        {
                            glDisable(GL_BLEND);
                        }
                        curStipple = triState.stipple;
                    }
                    if (triState.cullFace != curCullFace && triState.cullFace >= 0)
                    {
                        if (triState.cullFace == 0) glDisable(GL_CULL_FACE);
                        else { glEnable(GL_CULL_FACE); glCullFace(triState.cullFace == 2 ? GL_FRONT : GL_BACK); }
                        curCullFace = triState.cullFace;
                    }
                    if (triState.opacity != curOpacity)
                    {
                        if (triState.opacity < 1.0f)
                        {
                            glEnable(GL_BLEND);
                            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                            glDepthMask(GL_FALSE);
                        }
                        else if (curOpacity < 1.0f)
                        {
                            glDisable(GL_BLEND);
                            glDepthMask(GL_TRUE);
                        }
                        curOpacity = triState.opacity;
                    }
                    glBegin(GL_TRIANGLES);
                    boundTex = triState.tex;
                }

                if (triState.tex != 0 && mesh->hasUv && i0 < mesh->uvs.size()) { float u = mesh->uvs[i0].x; float v = 1.0f - mesh->uvs[i0].y; float uvMul = powf(2.0f, -triState.uvScale); u *= uvMul * triState.uvScaleU; v *= uvMul * triState.uvScaleV; if (gPreviewSaturnSampling) { u *= triState.satU; v *= triState.satV; } if (triState.flipU) u = 1.0f - u; if (triState.flipV) v = 1.0f - v; glTexCoord2f(u, v); }
                if ((triState.shadingUv >= 0 || triLit == 1) && i0 < csNormals.size()) { const Vec3& sn = csNormals[i0]; float d = sn.x * swLx + sn.y * swLy + sn.z * swLz; if (d < 0.0f) d = 0.0f; float c = swAmb + d * swDif - ((sn.y < 0.0f) ? (-sn.y * swShQ) : 0.0f) + (1.0f - fabsf(sn.z)) * 0.12f; glColor4f(c, c, c, triState.opacity); }
                const Vec3& v0 = (*renderPositions)[i0];
                glVertex3f(v0.x, v0.y, v0.z);
                if (triState.tex != 0 && mesh->hasUv && i1 < mesh->uvs.size()) { float u = mesh->uvs[i1].x; float v = 1.0f - mesh->uvs[i1].y; float uvMul = powf(2.0f, -triState.uvScale); u *= uvMul * triState.uvScaleU; v *= uvMul * triState.uvScaleV; if (gPreviewSaturnSampling) { u *= triState.satU; v *= triState.satV; } if (triState.flipU) u = 1.0f - u; if (triState.flipV) v = 1.0f - v; glTexCoord2f(u, v); }
                if ((triState.shadingUv >= 0 || triLit == 1) && i1 < csNormals.size()) { const Vec3& sn = csNormals[i1]; float d = sn.x * swLx + sn.y * swLy + sn.z * swLz; if (d < 0.0f) d = 0.0f; float c = swAmb + d * swDif - ((sn.y < 0.0f) ? (-sn.y * swShQ) : 0.0f) + (1.0f - fabsf(sn.z)) * 0.12f; glColor4f(c, c, c, triState.opacity); }
                const Vec3& v1 = (*renderPositions)[i1];
                glVertex3f(v1.x, v1.y, v1.z);
                if (triState.tex != 0 && mesh->hasUv && i2 < mesh->uvs.size()) { float u = mesh->uvs[i2].x; float v = 1.0f - mesh->uvs[i2].y; float uvMul = powf(2.0f, -triState.uvScale); u *= uvMul * triState.uvScaleU; v *= uvMul * triState.uvScaleV; if (gPreviewSaturnSampling) { u *= triState.satU; v *= triState.satV; } if (triState.flipU) u = 1.0f - u; if (triState.flipV) v = 1.0f - v; glTexCoord2f(u, v); }
                if ((triState.shadingUv >= 0 || triLit == 1) && i2 < csNormals.size()) { const Vec3& sn = csNormals[i2]; float d = sn.x * swLx + sn.y * swLy + sn.z * swLz; if (d < 0.0f) d = 0.0f; float c = swAmb + d * swDif - ((sn.y < 0.0f) ? (-sn.y * swShQ) : 0.0f) + (1.0f - fabsf(sn.z)) * 0.12f; glColor4f(c, c, c, triState.opacity); }
                const Vec3& v2 = (*renderPositions)[i2];
                glVertex3f(v2.x, v2.y, v2.z);
            }
            glEnd();
        }

        // Restore all per-material GL state after mesh rendering
        glDisable(GL_POLYGON_STIPPLE);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glDisable(GL_CULL_FACE);

        // Restore lighting state after mesh rendering
        if (anyLit)
        {
            glDisable(GL_LIGHTING);
            glDisable(GL_LIGHT0);
            glDisable(GL_COLOR_MATERIAL);
            glDisable(GL_NORMALIZE);
            glShadeModel(GL_FLAT);
        }

        // Green wireframe overlay for navmesh-ready triangles inside a NavMesh3D bounds
        if (s.navmeshReady && !selected && !gNavMesh3DNodes.empty())
        {
            // Collect positive (non-negator) bounds AABBs + wireframe style
            struct NavBounds { float minX, minY, minZ, maxX, maxY, maxZ; float r, g, b, thick; };
            struct CullBounds { float minX, minY, minZ, maxX, maxY, maxZ; float threshold; };
            std::vector<NavBounds> posBounds;
            std::vector<CullBounds> cullBounds;
            for (int ni = 0; ni < (int)gNavMesh3DNodes.size(); ++ni)
            {
                const auto& nm = gNavMesh3DNodes[ni];
                if (!nm.navBounds) continue;
                float hx = nm.scaleX * nm.extentX * 0.5f;
                float hy = nm.scaleY * nm.extentY * 0.5f;
                float hz = nm.scaleZ * nm.extentZ * 0.5f;
                if (!nm.navNegator)
                    posBounds.push_back({ nm.x - hx, nm.y - hy, nm.z - hz, nm.x + hx, nm.y + hy, nm.z + hz, nm.wireR, nm.wireG, nm.wireB, nm.wireThickness });
                if (nm.cullWalls)
                    cullBounds.push_back({ nm.x - hx, nm.y - hy, nm.z - hz, nm.x + hx, nm.y + hy, nm.z + hz, nm.wallCullThreshold });
            }

            if (!posBounds.empty())
            {
                // Use color/thickness from first positive bounds
                float overlayR = posBounds[0].r, overlayG = posBounds[0].g, overlayB = posBounds[0].b;
                float overlayThick = posBounds[0].thick;

                bool anyDrawn = false;
                for (size_t idx = 0; idx + 2 < mesh->indices.size(); idx += 3)
                {
                    uint16_t i0 = mesh->indices[idx + 0];
                    uint16_t i1 = mesh->indices[idx + 1];
                    uint16_t i2 = mesh->indices[idx + 2];
                    if (i0 >= renderPositions->size() || i1 >= renderPositions->size() || i2 >= renderPositions->size())
                        continue;
                    const Vec3& lv0 = (*renderPositions)[i0];
                    const Vec3& lv1 = (*renderPositions)[i1];
                    const Vec3& lv2 = (*renderPositions)[i2];

                    // World-space positions (scale + translate)
                    float wp[3][3] = {
                        { wx + lv0.x * wsx, wy + lv0.y * wsy, wz + lv0.z * wsz },
                        { wx + lv1.x * wsx, wy + lv1.y * wsy, wz + lv1.z * wsz },
                        { wx + lv2.x * wsx, wy + lv2.y * wsy, wz + lv2.z * wsz }
                    };

                    bool triInside = false;
                    for (int vi = 0; vi < 3 && !triInside; ++vi)
                    {
                        for (int bi = 0; bi < (int)posBounds.size() && !triInside; ++bi)
                        {
                            const auto& b = posBounds[bi];
                            if (wp[vi][0] >= b.minX && wp[vi][0] <= b.maxX &&
                                wp[vi][1] >= b.minY && wp[vi][1] <= b.maxY &&
                                wp[vi][2] >= b.minZ && wp[vi][2] <= b.maxZ)
                            {
                                triInside = true;
                                overlayR = b.r; overlayG = b.g; overlayB = b.b;
                                overlayThick = b.thick;
                            }
                        }
                    }

                    // Wall cull check: skip near-vertical faces inside cull-walls bounds
                    if (triInside && !cullBounds.empty())
                    {
                        float cx = (wp[0][0] + wp[1][0] + wp[2][0]) / 3.0f;
                        float cy = (wp[0][1] + wp[1][1] + wp[2][1]) / 3.0f;
                        float cz = (wp[0][2] + wp[1][2] + wp[2][2]) / 3.0f;
                        for (int ci = 0; ci < (int)cullBounds.size(); ++ci)
                        {
                            const auto& cb = cullBounds[ci];
                            if (cx >= cb.minX && cx <= cb.maxX &&
                                cy >= cb.minY && cy <= cb.maxY &&
                                cz >= cb.minZ && cz <= cb.maxZ)
                            {
                                // Compute face normal from world-space triangle
                                float e1x = wp[1][0] - wp[0][0], e1y = wp[1][1] - wp[0][1], e1z = wp[1][2] - wp[0][2];
                                float e2x = wp[2][0] - wp[0][0], e2y = wp[2][1] - wp[0][1], e2z = wp[2][2] - wp[0][2];
                                float nx = e1y * e2z - e1z * e2y;
                                float ny = e1z * e2x - e1x * e2z;
                                float nz = e1x * e2y - e1y * e2x;
                                float nlen = sqrtf(nx * nx + ny * ny + nz * nz);
                                if (nlen > 1e-8f)
                                {
                                    ny /= nlen;
                                    // Cull near-vertical faces (wall-like): normal.y close to 0
                                    if (fabsf(ny) < cb.threshold)
                                        triInside = false;
                                }
                                break;
                            }
                        }
                    }

                    if (triInside)
                    {
                        if (!anyDrawn)
                        {
                            glDisable(GL_TEXTURE_2D);
                            glDisable(GL_LIGHTING);
                            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                            glEnable(GL_POLYGON_OFFSET_LINE);
                            glPolygonOffset(-1.0f, -1.0f);
                            glLineWidth(overlayThick);
                            glColor3f(overlayR, overlayG, overlayB);
                            glBegin(GL_TRIANGLES);
                            anyDrawn = true;
                        }
                        glVertex3f(lv0.x, lv0.y, lv0.z);
                        glVertex3f(lv1.x, lv1.y, lv1.z);
                        glVertex3f(lv2.x, lv2.y, lv2.z);
                    }
                }
                if (anyDrawn)
                {
                    glEnd();
                    glDisable(GL_POLYGON_OFFSET_LINE);
                    glPolygonOffset(0.0f, 0.0f);
                    glLineWidth(1.0f);
                    glPolygonMode(GL_FRONT_AND_BACK, gWireframePreview ? GL_LINE : GL_FILL);
                }
            }
        }

        // Selected checker overlay: fullscreen texture masked by selected mesh shape
        if (selected && gCheckerOverlayTex != 0)
        {
            const float checkerPx = 8.0f;

            // 1) Write selected mesh silhouette into stencil (respect depth)
            glEnable(GL_STENCIL_TEST);
            glDisable(GL_SCISSOR_TEST);
            glClearStencil(0);
            glClear(GL_STENCIL_BUFFER_BIT);
            glStencilMask(0xFF);
            glStencilFunc(GL_ALWAYS, 1, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glDepthMask(GL_FALSE);
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.0f, -1.0f);
            glDepthFunc(GL_LEQUAL);
            glDisable(GL_TEXTURE_2D);

            glBegin(GL_TRIANGLES);
            for (size_t idx = 0; idx + 2 < mesh->indices.size(); idx += 3)
            {
                uint16_t i0 = mesh->indices[idx + 0];
                uint16_t i1 = mesh->indices[idx + 1];
                uint16_t i2 = mesh->indices[idx + 2];
                if (i0 >= renderPositions->size() || i1 >= renderPositions->size() || i2 >= renderPositions->size())
                    continue;
                const Vec3& v0 = (*renderPositions)[i0];
                const Vec3& v1 = (*renderPositions)[i1];
                const Vec3& v2 = (*renderPositions)[i2];
                glVertex3f(v0.x, v0.y, v0.z);
                glVertex3f(v1.x, v1.y, v1.z);
                glVertex3f(v2.x, v2.y, v2.z);
            }
            glEnd();

            // 2) Draw fullscreen checker only where stencil matches
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            glDisable(GL_POLYGON_OFFSET_FILL);

            glStencilMask(0x00);
            glStencilFunc(GL_EQUAL, 1, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

            GLint vp[4] = { 0, 0, 1, 1 };
            glGetIntegerv(GL_VIEWPORT, vp);
            const float uMax = (float)vp[2] / (checkerPx * 8.0f);
            const float vMax = (float)vp[3] / (checkerPx * 8.0f);

            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, gCheckerOverlayTex);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

            glMatrixMode(GL_PROJECTION);
            glPushMatrix();
            glLoadIdentity();
            glMatrixMode(GL_MODELVIEW);
            glPushMatrix();
            glLoadIdentity();

            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
            glTexCoord2f(uMax, 0.0f); glVertex3f( 1.0f, -1.0f, 0.0f);
            glTexCoord2f(uMax, vMax); glVertex3f( 1.0f,  1.0f, 0.0f);
            glTexCoord2f(0.0f, vMax); glVertex3f(-1.0f,  1.0f, 0.0f);
            glEnd();

            glPopMatrix();
            glMatrixMode(GL_PROJECTION);
            glPopMatrix();
            glMatrixMode(GL_MODELVIEW);

            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            glStencilMask(0xFF);
            glStencilFunc(GL_ALWAYS, 0, 0xFF);
            glDisable(GL_STENCIL_TEST);
        }

        glPopMatrix();

        glDisable(GL_TEXTURE_2D);
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Clean up all GL state that mesh rendering may have enabled so it
    // never leaks into the next frame's gradient or ImGui pass.
    glDisable(GL_LIGHTING);
    glDisable(GL_LIGHT0);
    glDisable(GL_COLOR_MATERIAL);
    glDisable(GL_NORMALIZE);
    glDisable(GL_TEXTURE_2D);
    glShadeModel(GL_SMOOTH);
}
