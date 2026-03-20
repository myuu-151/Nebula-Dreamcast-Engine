// ---------------------------------------------------------------------------
// physics.cpp — Play-mode physics tick.
// Gravity only. Collision (ground snap, slope alignment, wall push-out,
// Node3D overlap) is handled by collision.cpp.
// ---------------------------------------------------------------------------

#include "physics.h"
#include "collision.h"
#include "nodes/NodeTypes.h"

#include "../editor/editor_state.h"

// ---------------------------------------------------------------------------
// TickPlayModePhysics — one frame of play-mode physics for all Node3D nodes.
// ---------------------------------------------------------------------------
void TickPlayModePhysics(float dt)
{
    const float gravity = -29.4f;

    for (int ni = 0; ni < (int)gNode3DNodes.size(); ++ni)
    {
        auto& n3 = gNode3DNodes[ni];
        if (!n3.physicsEnabled && !n3.collisionSource && !n3.simpleCollision) continue;

        // Gravity (physicsEnabled only)
        if (n3.physicsEnabled)
        {
            n3.velY += gravity * dt;
            n3.y += n3.velY * dt;
        }

        // Ground snap, slope alignment, wall collision
        if (n3.collisionSource || n3.simpleCollision)
            ResolveNodeCollision(ni, dt);
    }

    // Node3D-vs-Node3D AABB collision
    ResolveNode3DOverlaps();
}
