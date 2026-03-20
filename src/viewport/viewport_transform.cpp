#include "viewport_transform.h"

#include <vector>
#include <string>
#include <functional>

#include "../editor/editor_state.h"
#include "../editor/undo.h"

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
