#pragma once

// Draw wireframe gizmos for Audio3D, Camera3D, Node3D and NavMesh3D nodes.
// Call once per frame after grid/axes and before StaticMesh rendering.
// `activeCam` is the camera currently driving the play viewport (its helper is hidden).
struct Camera3DNode;
void DrawNodeGizmos(const Camera3DNode* activeCam);
