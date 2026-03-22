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
    const float kMaxStep = 1.0f / 60.0f; // sub-step if frame dips below 60fps
    const int kMaxSubSteps = 4;           // cap to avoid spiral of death

    // Determine sub-step count
    int steps = 1;
    float stepDt = dt;
    if (dt > kMaxStep)
    {
        steps = (int)ceilf(dt / kMaxStep);
        if (steps > kMaxSubSteps) steps = kMaxSubSteps;
        stepDt = dt / (float)steps;
    }

    for (int step = 0; step < steps; ++step)
    {
        for (int ni = 0; ni < (int)gNode3DNodes.size(); ++ni)
        {
            auto& n3 = gNode3DNodes[ni];
            if (!n3.physicsEnabled && !n3.collisionSource && !n3.simpleCollision) continue;

            // Gravity (physicsEnabled only)
            if (n3.physicsEnabled)
            {
                n3.velY += gravity * stepDt;
                n3.y += n3.velY * stepDt;
            }

            // Ground snap, slope alignment, wall collision
            if (n3.collisionSource || n3.simpleCollision)
                ResolveNodeCollision(ni, stepDt);
        }

        // Node3D-vs-Node3D AABB collision
        ResolveNode3DOverlaps();
    }
}
