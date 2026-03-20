#include "viewport_transform.h"

#include <vector>
#include <string>
#include <functional>
#include <cmath>

#include "../editor/editor_state.h"
#include "../editor/undo.h"
#include "../math/math_utils.h"

TransformMode gTransformMode = Transform_None;
char gAxisLock = 0;
bool gTransforming = false;

bool gHasTransformSnapshot = false;
static bool gTransformIsStatic = false;
static bool gTransformIsNode3D = false;
static bool gTransformIsNavMesh3D = false;
static int gTransformIndex = -1;
static Audio3DNode gTransformBefore;
static StaticMesh3DNode gTransformBeforeStatic;
static Node3DNode gTransformBeforeNode3D;
static NavMesh3DNode gTransformBeforeNavMesh3D;

bool TransformChanged(const Audio3DNode& a, const Audio3DNode& b)
{
    return a.x != b.x || a.y != b.y || a.z != b.z ||
           a.rotX != b.rotX || a.rotY != b.rotY || a.rotZ != b.rotZ ||
           a.scaleX != b.scaleX || a.scaleY != b.scaleY || a.scaleZ != b.scaleZ;
}

bool TransformChanged(const StaticMesh3DNode& a, const StaticMesh3DNode& b)
{
    return a.x != b.x || a.y != b.y || a.z != b.z ||
           a.rotX != b.rotX || a.rotY != b.rotY || a.rotZ != b.rotZ ||
           a.scaleX != b.scaleX || a.scaleY != b.scaleY || a.scaleZ != b.scaleZ;
}

bool TransformChanged(const Node3DNode& a, const Node3DNode& b)
{
    return a.x != b.x || a.y != b.y || a.z != b.z ||
           a.rotX != b.rotX || a.rotY != b.rotY || a.rotZ != b.rotZ ||
           a.scaleX != b.scaleX || a.scaleY != b.scaleY || a.scaleZ != b.scaleZ;
}

bool TransformChanged(const NavMesh3DNode& a, const NavMesh3DNode& b)
{
    return a.x != b.x || a.y != b.y || a.z != b.z ||
           a.rotX != b.rotX || a.rotY != b.rotY || a.rotZ != b.rotZ ||
           a.scaleX != b.scaleX || a.scaleY != b.scaleY || a.scaleZ != b.scaleZ;
}

void BeginTransformSnapshot()
{
    if (gSelectedAudio3D >= 0 && gSelectedAudio3D < (int)gAudio3DNodes.size())
    {
        gHasTransformSnapshot = true;
        gTransformIsStatic = false;
        gTransformIsNode3D = false;
        gTransformIsNavMesh3D = false;
        gTransformIndex = gSelectedAudio3D;
        gTransformBefore = gAudio3DNodes[gSelectedAudio3D];
        return;
    }

    if (gSelectedStaticMesh >= 0 && gSelectedStaticMesh < (int)gStaticMeshNodes.size())
    {
        gHasTransformSnapshot = true;
        gTransformIsStatic = true;
        gTransformIsNode3D = false;
        gTransformIsNavMesh3D = false;
        gTransformIndex = gSelectedStaticMesh;
        gTransformBeforeStatic = gStaticMeshNodes[gSelectedStaticMesh];
        return;
    }

    if (gSelectedNode3D >= 0 && gSelectedNode3D < (int)gNode3DNodes.size())
    {
        gHasTransformSnapshot = true;
        gTransformIsStatic = false;
        gTransformIsNode3D = true;
        gTransformIsNavMesh3D = false;
        gTransformIndex = gSelectedNode3D;
        gTransformBeforeNode3D = gNode3DNodes[gSelectedNode3D];
        return;
    }

    if (gSelectedNavMesh3D >= 0 && gSelectedNavMesh3D < (int)gNavMesh3DNodes.size())
    {
        gHasTransformSnapshot = true;
        gTransformIsStatic = false;
        gTransformIsNode3D = false;
        gTransformIsNavMesh3D = true;
        gTransformIndex = gSelectedNavMesh3D;
        gTransformBeforeNavMesh3D = gNavMesh3DNodes[gSelectedNavMesh3D];
    }
}

void EndTransformSnapshot()
{
    if (!gHasTransformSnapshot) return;

    if (gTransformIsStatic)
    {
        if (gTransformIndex < 0 || gTransformIndex >= (int)gStaticMeshNodes.size())
        {
            gHasTransformSnapshot = false;
            return;
        }

        StaticMesh3DNode before = gTransformBeforeStatic;
        StaticMesh3DNode after = gStaticMeshNodes[gTransformIndex];
        if (TransformChanged(before, after))
        {
            int idx = gTransformIndex;
            PushUndo({"Transform StaticMesh3D",
                [idx, before]() { if (idx >= 0 && idx < (int)gStaticMeshNodes.size()) gStaticMeshNodes[idx] = before; },
                [idx, after]() { if (idx >= 0 && idx < (int)gStaticMeshNodes.size()) gStaticMeshNodes[idx] = after; }
            });
        }
    }
    else if (gTransformIsNode3D)
    {
        if (gTransformIndex < 0 || gTransformIndex >= (int)gNode3DNodes.size())
        {
            gHasTransformSnapshot = false;
            return;
        }

        Node3DNode before = gTransformBeforeNode3D;
        Node3DNode after = gNode3DNodes[gTransformIndex];
        if (TransformChanged(before, after))
        {
            int idx = gTransformIndex;
            PushUndo({"Transform Node3D",
                [idx, before]() { if (idx >= 0 && idx < (int)gNode3DNodes.size()) gNode3DNodes[idx] = before; },
                [idx, after]() { if (idx >= 0 && idx < (int)gNode3DNodes.size()) gNode3DNodes[idx] = after; }
            });
        }
    }
    else if (gTransformIsNavMesh3D)
    {
        if (gTransformIndex < 0 || gTransformIndex >= (int)gNavMesh3DNodes.size())
        {
            gHasTransformSnapshot = false;
            return;
        }

        NavMesh3DNode before = gTransformBeforeNavMesh3D;
        NavMesh3DNode after = gNavMesh3DNodes[gTransformIndex];
        if (TransformChanged(before, after))
        {
            int idx = gTransformIndex;
            PushUndo({"Transform NavMesh3D",
                [idx, before]() { if (idx >= 0 && idx < (int)gNavMesh3DNodes.size()) gNavMesh3DNodes[idx] = before; },
                [idx, after]() { if (idx >= 0 && idx < (int)gNavMesh3DNodes.size()) gNavMesh3DNodes[idx] = after; }
            });
        }
    }
    else
    {
        if (gTransformIndex < 0 || gTransformIndex >= (int)gAudio3DNodes.size())
        {
            gHasTransformSnapshot = false;
            return;
        }

        Audio3DNode before = gTransformBefore;
        Audio3DNode after = gAudio3DNodes[gTransformIndex];
        if (TransformChanged(before, after))
        {
            int idx = gTransformIndex;
            PushUndo({"Transform Node",
                [idx, before]() { if (idx >= 0 && idx < (int)gAudio3DNodes.size()) gAudio3DNodes[idx] = before; },
                [idx, after]() { if (idx >= 0 && idx < (int)gAudio3DNodes.size()) gAudio3DNodes[idx] = after; }
            });
        }
    }

    gHasTransformSnapshot = false;
}

void CancelTransformSnapshot()
{
    if (!gHasTransformSnapshot) return;

    if (gTransformIsStatic && gTransformIndex >= 0 && gTransformIndex < (int)gStaticMeshNodes.size())
        gStaticMeshNodes[gTransformIndex] = gTransformBeforeStatic;
    else if (gTransformIsNode3D && gTransformIndex >= 0 && gTransformIndex < (int)gNode3DNodes.size())
        gNode3DNodes[gTransformIndex] = gTransformBeforeNode3D;
    else if (gTransformIsNavMesh3D && gTransformIndex >= 0 && gTransformIndex < (int)gNavMesh3DNodes.size())
        gNavMesh3DNodes[gTransformIndex] = gTransformBeforeNavMesh3D;
    else if (gTransformIndex >= 0 && gTransformIndex < (int)gAudio3DNodes.size())
        gAudio3DNodes[gTransformIndex] = gTransformBefore;

    gHasTransformSnapshot = false;
}

// ---------------------------------------------------------------------------
// Unified transform interaction
// ---------------------------------------------------------------------------

static float sRotateStartX = 0.0f;
static float sRotateStartY = 0.0f;
static float sRotateStartZ = 0.0f;
static float sRotateStartMouseX = 0.0f;
static float sRotateStartMouseY = 0.0f;

struct TransformTarget
{
    float* x;
    float* y;
    float* z;
    float* rotX;
    float* rotY;
    float* rotZ;
    float* scaleX;
    float* scaleY;
    float* scaleZ;
    int selectedId;
    bool isNode3D;
    bool isAudio3D;
    float* innerRadius;
    float* outerRadius;
};

static bool GetSelectedTransformTarget(TransformTarget& t)
{
    t = {};
    t.innerRadius = nullptr;
    t.outerRadius = nullptr;
    t.isNode3D = false;
    t.isAudio3D = false;

    if (gSelectedAudio3D >= 0 && gSelectedAudio3D < (int)gAudio3DNodes.size())
    {
        auto& n = gAudio3DNodes[gSelectedAudio3D];
        t = { &n.x, &n.y, &n.z, &n.rotX, &n.rotY, &n.rotZ,
              &n.scaleX, &n.scaleY, &n.scaleZ,
              gSelectedAudio3D, false, true, &n.innerRadius, &n.outerRadius };
        return true;
    }
    if (gSelectedStaticMesh >= 0 && gSelectedStaticMesh < (int)gStaticMeshNodes.size())
    {
        auto& n = gStaticMeshNodes[gSelectedStaticMesh];
        t = { &n.x, &n.y, &n.z, &n.rotX, &n.rotY, &n.rotZ,
              &n.scaleX, &n.scaleY, &n.scaleZ,
              10000 + gSelectedStaticMesh, false, false, nullptr, nullptr };
        return true;
    }
    if (gSelectedNode3D >= 0 && gSelectedNode3D < (int)gNode3DNodes.size())
    {
        auto& n = gNode3DNodes[gSelectedNode3D];
        t = { &n.x, &n.y, &n.z, &n.rotX, &n.rotY, &n.rotZ,
              &n.scaleX, &n.scaleY, &n.scaleZ,
              40000 + gSelectedNode3D, true, false, nullptr, nullptr };
        return true;
    }
    if (gSelectedNavMesh3D >= 0 && gSelectedNavMesh3D < (int)gNavMesh3DNodes.size())
    {
        auto& n = gNavMesh3DNodes[gSelectedNavMesh3D];
        t = { &n.x, &n.y, &n.z, &n.rotX, &n.rotY, &n.rotZ,
              &n.scaleX, &n.scaleY, &n.scaleZ,
              50000 + gSelectedNavMesh3D, false, false, nullptr, nullptr };
        return true;
    }
    return false;
}

void TickTransformInteraction(const Vec3& forward, const Vec3& up, const Vec3& eye,
                              float mouseX, float mouseY, bool mouseClicked)
{
    TransformTarget t;
    if (!GetSelectedTransformTarget(t))
    {
        gTransforming = false;
        return;
    }

    if (gTransformMode == Transform_None)
    {
        gTransforming = false;
        return;
    }

    float dx = 0.0f;
    float dy = 0.0f;
    if (!gTransforming)
    {
        gTransforming = true;
        gLastTransformMouseX = mouseX;
        gLastTransformMouseY = mouseY;
    }
    else
    {
        dx = mouseX - (float)gLastTransformMouseX;
        dy = mouseY - (float)gLastTransformMouseY;
        gLastTransformMouseX = mouseX;
        gLastTransformMouseY = mouseY;
    }

    if ((dx != 0.0f || dy != 0.0f) && !gHasTransformSnapshot)
    {
        BeginTransformSnapshot();
    }

    // Build camera-space right/up axes
    Vec3 right, upAxis;
    Vec3 forwardAxis = forward;
    right = { forwardAxis.y * up.z - forwardAxis.z * up.y,
              forwardAxis.z * up.x - forwardAxis.x * up.z,
              forwardAxis.x * up.y - forwardAxis.y * up.x };
    float rlen = sqrtf(right.x*right.x + right.y*right.y + right.z*right.z);
    if (rlen > 0.0001f) { right.x /= rlen; right.y /= rlen; right.z /= rlen; }
    upAxis = { right.y * forwardAxis.z - right.z * forwardAxis.y,
               right.z * forwardAxis.x - right.x * forwardAxis.z,
               right.x * forwardAxis.y - right.y * forwardAxis.x };
    float ulen = sqrtf(upAxis.x*upAxis.x + upAxis.y*upAxis.y + upAxis.z*upAxis.z);
    if (ulen > 0.0001f) { upAxis.x /= ulen; upAxis.y /= ulen; upAxis.z /= ulen; }

    float distToCam = sqrtf((*t.x - eye.x)*(*t.x - eye.x) +
                            (*t.y - eye.y)*(*t.y - eye.y) +
                            (*t.z - eye.z)*(*t.z - eye.z));
    float moveScale = 0.0015f * distToCam;

    if (gTransformMode == Transform_Grab)
    {
        Vec3 delta = { right.x * -dx * moveScale + upAxis.x * -dy * moveScale,
                       right.y * -dx * moveScale + upAxis.y * -dy * moveScale,
                       right.z * -dx * moveScale + upAxis.z * -dy * moveScale };

        if (gAxisLock == 'X') { *t.x += delta.x; }
        else if (gAxisLock == 'Y') { *t.y += delta.y; }
        else if (gAxisLock == 'Z') { *t.z += delta.z; }
        else { *t.x += delta.x; *t.y += delta.y; *t.z += delta.z; }

        if (mouseClicked)
        {
            if (!gHasTransformSnapshot) BeginTransformSnapshot();
            EndTransformSnapshot();
            gTransformMode = Transform_None;
        }
    }
    else if (gTransformMode == Transform_Rotate)
    {
        float rotScale = 1.5f;
        if (!gHasRotatePreview || gRotatePreviewIndex != t.selectedId)
        {
            gHasRotatePreview = true;
            gRotatePreviewIndex = t.selectedId;
            sRotateStartX = *t.rotX;
            sRotateStartY = *t.rotY;
            sRotateStartZ = *t.rotZ;
            sRotateStartMouseX = mouseX;
            sRotateStartMouseY = mouseY;
        }

        float rdx = mouseX - sRotateStartMouseX;
        float rdy = mouseY - sRotateStartMouseY;
        gRotatePreviewX = sRotateStartX;
        gRotatePreviewY = sRotateStartY;
        gRotatePreviewZ = sRotateStartZ;

        if (gAxisLock == 'X') gRotatePreviewX += rdy * rotScale;
        else if (gAxisLock == 'Y') gRotatePreviewY += rdx * rotScale;
        else if (gAxisLock == 'Z') gRotatePreviewZ += rdx * rotScale;
        else { gRotatePreviewY += rdx * rotScale; gRotatePreviewX += rdy * rotScale; }

        if (mouseClicked)
        {
            *t.rotX = gRotatePreviewX;
            *t.rotY = gRotatePreviewY;
            *t.rotZ = gRotatePreviewZ;
            if (t.isNode3D)
                SyncNode3DQuatFromEuler(gNode3DNodes[gSelectedNode3D]);
            sRotateStartX = *t.rotX;
            sRotateStartY = *t.rotY;
            sRotateStartZ = *t.rotZ;
            sRotateStartMouseX = mouseX;
            sRotateStartMouseY = mouseY;
            gHasRotatePreview = false;
            if (!gHasTransformSnapshot) BeginTransformSnapshot();
            EndTransformSnapshot();
            gTransformMode = Transform_None;
        }
    }
    else if (gTransformMode == Transform_Scale)
    {
        float s = 1.0f + ((-dy + dx) * 0.03f);
        if (s < 0.01f) s = 0.01f;

        if (t.isAudio3D && t.innerRadius && t.outerRadius)
        {
            *t.innerRadius *= s;
            *t.outerRadius *= s;
            if (*t.innerRadius < 0.01f) *t.innerRadius = 0.01f;
            if (*t.outerRadius < 0.01f) *t.outerRadius = 0.01f;
        }

        if (gAxisLock == 'X') *t.scaleX *= s;
        else if (gAxisLock == 'Y') *t.scaleY *= s;
        else if (gAxisLock == 'Z') *t.scaleZ *= s;
        else { *t.scaleX *= s; *t.scaleY *= s; *t.scaleZ *= s; }

        if (mouseClicked)
        {
            if (!gHasTransformSnapshot) BeginTransformSnapshot();
            EndTransformSnapshot();
            gTransformMode = Transform_None;
        }
    }
}
