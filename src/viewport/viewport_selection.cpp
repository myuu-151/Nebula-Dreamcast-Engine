#include "viewport_selection.h"
#include "viewport_render.h"
#include "viewport_transform.h"
#include "node_helpers.h"
#include "../editor/editor_state.h"
#include "../editor/project.h"
#include "../io/mesh_io.h"
#include "../nodes/NodeTypes.h"

#include <cmath>
#include <algorithm>
#include <filesystem>

#include "imgui.h"
#include <GLFW/glfw3.h>

void TickViewportSelection(GLFWwindow* window, float mouseX, float mouseY,
                           float scaleX, float scaleY, bool mouseClicked)
{
    if (!mouseClicked) return;

    glfwFocusWindow(window);
    bool ctrlHeld = ImGui::GetIO().KeyCtrl;
    float bestDist = 1e9f; // px
    int bestAudioIndex = -1;
    int bestStaticIndex = -1;
    int bestCameraIndex = -1;
    int bestNode3DIndex = -1;
    int bestNavMeshIndex = -1;

    auto pickNearest = [&](float sx, float sy) -> float {
        float dx = mouseX - sx;
        float dy = mouseY - sy;
        return sqrtf(dx*dx + dy*dy);
    };
    auto clearBest = [&]() { bestAudioIndex = -1; bestStaticIndex = -1; bestCameraIndex = -1; bestNode3DIndex = -1; bestNavMeshIndex = -1; };

    for (int i = 0; i < (int)gAudio3DNodes.size(); ++i)
    {
        float sx, sy;
        if (ProjectToScreenGL({ gAudio3DNodes[i].x, gAudio3DNodes[i].y, gAudio3DNodes[i].z }, sx, sy, scaleX, scaleY))
        {
            float d = pickNearest(sx, sy);
            if (d < bestDist) { bestDist = d; clearBest(); bestAudioIndex = i; }
        }
    }
    // StaticMesh3D: test mouse against projected triangles for accurate selection.
    // Falls back to origin-point distance if mesh data is unavailable.
    for (int i = 0; i < (int)gStaticMeshNodes.size(); ++i)
    {
        const auto& s = gStaticMeshNodes[i];
        bool hitMesh = false;

        // Try triangle-based selection if mesh geometry is loaded
        if (!s.mesh.empty() && !gProjectDir.empty())
        {
            std::filesystem::path meshPath = std::filesystem::path(gProjectDir) / s.mesh;
            const NebMesh* mesh = GetNebMesh(meshPath);
            if (mesh && mesh->valid && !mesh->positions.empty() && mesh->indices.size() >= 3)
            {
                float wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz;
                GetStaticMeshWorldTRS(i, wx, wy, wz, wrx, wry, wrz, wsx, wsy, wsz);

                // Build rotation matrix (same axis remap as rendering)
                int pickParentN3D = (!s.parent.empty()) ? FindNode3DByName(s.parent) : -1;
                bool parentedNode3D = (pickParentN3D >= 0);
                // Parent quaternion rotation matrix (3x3, row-major) for Node3D parents
                float pr00=1,pr01=0,pr02=0, pr10=0,pr11=1,pr12=0, pr20=0,pr21=0,pr22=1;
                // Euler trig for standalone meshes
                float cx1=1,sx1=0,cy1=1,sy1=0,cz1=1,sz1=0;
                if (parentedNode3D)
                {
                    float pqw2, pqx2, pqy2, pqz2, _px2, _py2, _pz2, _psx2, _psy2, _psz2;
                    GetNode3DWorldTRSQuat(pickParentN3D, _px2, _py2, _pz2, pqw2, pqx2, pqy2, pqz2, _psx2, _psy2, _psz2);
                    float xx2=pqx2*pqx2, yy2=pqy2*pqy2, zz2=pqz2*pqz2;
                    float xy2=pqx2*pqy2, xz2=pqx2*pqz2, yz2=pqy2*pqz2;
                    float wx2=pqw2*pqx2, wy2=pqw2*pqy2, wz2=pqw2*pqz2;
                    pr00=1-2*(yy2+zz2); pr01=2*(xy2-wz2); pr02=2*(xz2+wy2);
                    pr10=2*(xy2+wz2); pr11=1-2*(xx2+zz2); pr12=2*(yz2-wx2);
                    pr20=2*(xz2-wy2); pr21=2*(yz2+wx2); pr22=1-2*(xx2+yy2);
                }
                else
                {
                    float arx = wrz, ary = wrx, arz = wry;
                    float rrx = arx * 3.14159f / 180.0f, rry = ary * 3.14159f / 180.0f, rrz = arz * 3.14159f / 180.0f;
                    cx1 = cosf(rrx); sx1 = sinf(rrx); cy1 = cosf(rry); sy1 = sinf(rry); cz1 = cosf(rrz); sz1 = sinf(rrz);
                }

                // Child local rotation for parented meshes
                float ccx = 1, csx2 = 0, ccy = 1, csy2 = 0, ccz = 1, csz2 = 0;
                if (parentedNode3D)
                {
                    float crx = s.rotX * 3.14159f / 180.0f, cry = s.rotY * 3.14159f / 180.0f, crz = s.rotZ * 3.14159f / 180.0f;
                    ccx = cosf(crx); csx2 = sinf(crx); ccy = cosf(cry); csy2 = sinf(cry); ccz = cosf(crz); csz2 = sinf(crz);
                }

                float mx = mouseX, my = mouseY;

                for (size_t idx = 0; idx + 2 < mesh->indices.size(); idx += 3)
                {
                    float svx[3], svy[3];
                    bool allOk = true;
                    for (int vi = 0; vi < 3; ++vi)
                    {
                        uint16_t ii = mesh->indices[idx + vi];
                        if (ii >= mesh->positions.size()) { allOk = false; break; }
                        Vec3 v = mesh->positions[ii];
                        v.x *= wsx; v.y *= wsy; v.z *= wsz;

                        if (parentedNode3D)
                        {
                            // Child rotation first
                            float t;
                            t = v.x*ccz - v.y*csz2; v.y = v.x*csz2 + v.y*ccz; v.x = t;
                            t = v.x*ccy + v.z*csy2; v.z = -v.x*csy2 + v.z*ccy; v.x = t;
                            t = v.y*ccx - v.z*csx2; v.z = v.y*csx2 + v.z*ccx; v.y = t;
                            // Parent rotation via quaternion matrix
                            float vx2 = pr00*v.x + pr01*v.y + pr02*v.z;
                            float vy2 = pr10*v.x + pr11*v.y + pr12*v.z;
                            float vz2 = pr20*v.x + pr21*v.y + pr22*v.z;
                            v.x = vx2; v.y = vy2; v.z = vz2;
                        }
                        else
                        {
                            // Standalone rotation via Euler
                            float t;
                            t = v.x*cz1 - v.y*sz1; v.y = v.x*sz1 + v.y*cz1; v.x = t;
                            t = v.x*cy1 + v.z*sy1; v.z = -v.x*sy1 + v.z*cy1; v.x = t;
                            t = v.y*cx1 - v.z*sx1; v.z = v.y*sx1 + v.z*cx1; v.y = t;
                        }

                        v.x += wx; v.y += wy; v.z += wz;

                        if (!ProjectToScreenGL(v, svx[vi], svy[vi], scaleX, scaleY))
                        { allOk = false; break; }
                    }
                    if (!allOk) continue;

                    // Point-in-triangle test (2D barycentric)
                    float d0x = svx[1] - svx[0], d0y = svy[1] - svy[0];
                    float d1x = svx[2] - svx[0], d1y = svy[2] - svy[0];
                    float d2x = mx - svx[0], d2y = my - svy[0];
                    float dot00 = d0x*d0x + d0y*d0y;
                    float dot01 = d0x*d1x + d0y*d1y;
                    float dot02 = d0x*d2x + d0y*d2y;
                    float dot11 = d1x*d1x + d1y*d1y;
                    float dot12 = d1x*d2x + d1y*d2y;
                    float invDenom = dot00*dot11 - dot01*dot01;
                    if (fabsf(invDenom) < 1e-10f) continue;
                    invDenom = 1.0f / invDenom;
                    float u = (dot11*dot02 - dot01*dot12) * invDenom;
                    float v2 = (dot00*dot12 - dot01*dot02) * invDenom;
                    if (u >= 0.0f && v2 >= 0.0f && (u + v2) <= 1.0f)
                    {
                        hitMesh = true;
                        bestDist = 0.0f;
                        clearBest();
                        bestStaticIndex = i;
                        break;
                    }
                }
            }
        }

        // Fallback to origin-point distance if no triangle hit
        if (!hitMesh)
        {
            float sx, sy;
            if (ProjectToScreenGL({ s.x, s.y, s.z }, sx, sy, scaleX, scaleY))
            {
                float d = pickNearest(sx, sy);
                if (d < bestDist) { bestDist = d; clearBest(); bestStaticIndex = i; }
            }
        }
    }
    for (int i = 0; i < (int)gCamera3DNodes.size(); ++i)
    {
        float sx, sy;
        if (ProjectToScreenGL({ gCamera3DNodes[i].x, gCamera3DNodes[i].y, gCamera3DNodes[i].z }, sx, sy, scaleX, scaleY))
        {
            float d = pickNearest(sx, sy);
            if (d < bestDist) { bestDist = d; clearBest(); bestCameraIndex = i; }
        }
    }
    for (int i = 0; i < (int)gNode3DNodes.size(); ++i)
    {
        float sx, sy;
        if (ProjectToScreenGL({ gNode3DNodes[i].x, gNode3DNodes[i].y, gNode3DNodes[i].z }, sx, sy, scaleX, scaleY))
        {
            float d = pickNearest(sx, sy);
            if (d < bestDist) { bestDist = d; clearBest(); bestNode3DIndex = i; }
        }
    }
    for (int i = 0; i < (int)gNavMesh3DNodes.size(); ++i)
    {
        float sx, sy;
        if (ProjectToScreenGL({ gNavMesh3DNodes[i].x, gNavMesh3DNodes[i].y, gNavMesh3DNodes[i].z }, sx, sy, scaleX, scaleY))
        {
            float d = pickNearest(sx, sy);
            if (d < bestDist) { bestDist = d; clearBest(); bestNavMeshIndex = i; }
        }
    }

    auto deselectAll = [&]() {
        gSelectedAudio3D = -1; gSelectedStaticMesh = -1;
        gSelectedCamera3D = -1; gSelectedNode3D = -1; gSelectedNavMesh3D = -1;
    };

    if (gTransformMode == Transform_None)
    {
        int prevAudioSel = gSelectedAudio3D;
        int prevStaticSel = gSelectedStaticMesh;
        int prevCameraSel = gSelectedCamera3D;
        int prevNode3DSel = gSelectedNode3D;
        int prevNavMeshSel = gSelectedNavMesh3D;
        if (bestDist < 80.0f)
        {
            if (ctrlHeld)
            {
                // Migrate current single-select into multi-select sets
                if (gSelectedAudio3D >= 0) { gMultiSelectedAudio3D.insert(gSelectedAudio3D); gSelectedAudio3D = -1; }
                if (gSelectedStaticMesh >= 0) { gMultiSelectedStaticMesh.insert(gSelectedStaticMesh); gSelectedStaticMesh = -1; }
                if (gSelectedCamera3D >= 0) { gMultiSelectedCamera3D.insert(gSelectedCamera3D); gSelectedCamera3D = -1; }
                if (gSelectedNode3D >= 0) { gMultiSelectedNode3D.insert(gSelectedNode3D); gSelectedNode3D = -1; }
                if (gSelectedNavMesh3D >= 0) { gMultiSelectedNavMesh3D.insert(gSelectedNavMesh3D); gSelectedNavMesh3D = -1; }
                // Toggle the clicked object in multi-select
                if (bestAudioIndex >= 0) { if (gMultiSelectedAudio3D.count(bestAudioIndex)) gMultiSelectedAudio3D.erase(bestAudioIndex); else gMultiSelectedAudio3D.insert(bestAudioIndex); }
                if (bestStaticIndex >= 0) { if (gMultiSelectedStaticMesh.count(bestStaticIndex)) gMultiSelectedStaticMesh.erase(bestStaticIndex); else gMultiSelectedStaticMesh.insert(bestStaticIndex); }
                if (bestCameraIndex >= 0) { if (gMultiSelectedCamera3D.count(bestCameraIndex)) gMultiSelectedCamera3D.erase(bestCameraIndex); else gMultiSelectedCamera3D.insert(bestCameraIndex); }
                if (bestNode3DIndex >= 0) { if (gMultiSelectedNode3D.count(bestNode3DIndex)) gMultiSelectedNode3D.erase(bestNode3DIndex); else gMultiSelectedNode3D.insert(bestNode3DIndex); }
                if (bestNavMeshIndex >= 0) { if (gMultiSelectedNavMesh3D.count(bestNavMeshIndex)) gMultiSelectedNavMesh3D.erase(bestNavMeshIndex); else gMultiSelectedNavMesh3D.insert(bestNavMeshIndex); }
            }
            else
            {
                ClearMultiSelection();
                gSelectedAudio3D = bestAudioIndex;
                gSelectedStaticMesh = bestStaticIndex;
                gSelectedCamera3D = bestCameraIndex;
                gSelectedNode3D = bestNode3DIndex;
                gSelectedNavMesh3D = bestNavMeshIndex;
            }
        }
        else
        {
            if (!ctrlHeld)
            {
                ClearMultiSelection();
                deselectAll();
            }
        }

        if (gSelectedAudio3D != prevAudioSel || gSelectedStaticMesh != prevStaticSel ||
            gSelectedCamera3D != prevCameraSel || gSelectedNode3D != prevNode3DSel ||
            gSelectedNavMesh3D != prevNavMeshSel || HasMultiSelection())
        {
            gTransforming = false;
            gTransformMode = Transform_None;
            gAxisLock = 0;
            gLastTransformMouseX = mouseX;
            gLastTransformMouseY = mouseY;
        }
    }
    else if (gTransformMode == Transform_Rotate)
    {
        if (bestDist >= 80.0f)
        {
            deselectAll();
            gTransformMode = Transform_None;
            gAxisLock = 0;
            gHasRotatePreview = false;
        }
    }
    else
    {
        if (bestDist >= 80.0f)
        {
            deselectAll();
            gTransformMode = Transform_None;
            gAxisLock = 0;
        }
    }
}
