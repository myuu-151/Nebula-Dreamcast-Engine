#pragma once

#include "../nodes/NodeTypes.h"

enum TransformMode
{
    Transform_None = 0,
    Transform_Grab,
    Transform_Rotate,
    Transform_Scale
};

extern TransformMode gTransformMode;
extern char gAxisLock;
extern bool gTransforming;
extern bool gHasTransformSnapshot;

bool TransformChanged(const Audio3DNode& a, const Audio3DNode& b);
bool TransformChanged(const StaticMesh3DNode& a, const StaticMesh3DNode& b);
bool TransformChanged(const Node3DNode& a, const Node3DNode& b);
bool TransformChanged(const NavMesh3DNode& a, const NavMesh3DNode& b);

void BeginTransformSnapshot();
void EndTransformSnapshot();
void CancelTransformSnapshot();
