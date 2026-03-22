#pragma once

// Horizontal AABB push-out against wall triangles of all collisionSource
// StaticMesh3D nodes. Outputs total XZ push to resolve penetration.
void WallCollideAABB(float cx, float cy, float cz,
                     float hx, float hy, float hz,
                     float* outPushX, float* outPushZ);

// "Squeezable" wall collision — same as WallCollideAABB but clamps push per
// triangle, allowing the player to slowly squeeze through thin geometry.
// Useful for soft barriers or foliage. NOT used by the runtime by default.
void WallCollideAABBSqueezable(float cx, float cy, float cz,
                               float hx, float hy, float hz,
                               float* outPushX, float* outPushZ);

// Node3D-vs-Node3D AABB overlap resolution. Pushes overlapping pairs
// apart along the axis with the smallest XZ penetration.
void ResolveNode3DOverlaps();

// Ground snap, slope alignment, and wall collision for a single Node3D.
// Called per-node after gravity has been applied.
void ResolveNodeCollision(int nodeIndex, float dt);
