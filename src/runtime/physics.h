#pragma once

// Run one physics tick for all Node3D nodes that have physics/collision
// enabled. Call once per frame while play-mode is active and scripts
// have finished compiling. |dt| is the frame delta (clamped by caller).
void TickPlayModePhysics(float dt);
