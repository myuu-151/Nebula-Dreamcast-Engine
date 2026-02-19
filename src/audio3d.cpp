#include "audio3d.h"
#include <cmath>

std::vector<Audio3DNode> gAudio3DNodes;

void UpdateAudio3DNodes(float listenerX, float listenerY, float listenerZ)
{
    for (auto& n : gAudio3DNodes)
    {
        float dx = n.x - listenerX;
        float dy = n.y - listenerY;
        float dz = n.z - listenerZ;
        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);

        float inner = n.innerRadius;
        float outer = n.outerRadius;
        if (outer < inner) std::swap(outer, inner);
        if (outer <= 0.0f) outer = 0.001f;

        float att = 0.0f;
        if (dist <= inner)
            att = 1.0f;
        else if (dist >= outer)
            att = 0.0f;
        else
            att = 1.0f - ((dist - inner) / (outer - inner));

        float pan = dx / (outer > 0.0f ? outer : 1.0f);
        if (pan < -1.0f) pan = -1.0f;
        if (pan > 1.0f) pan = 1.0f;

        n.pan = pan;
        n.volume = n.baseVolume * att;
        if (n.volume < 0.0f) n.volume = 0.0f;
        if (n.volume > 1.0f) n.volume = 1.0f;

        bool nowInside = dist <= outer;
        n.justEntered = (!n.inside && nowInside);
        n.inside = nowInside;

        // Saturn hardware constraint: this is software mixing only.
        // Feed n.volume + n.pan into SCSP voice L/R at runtime.
        // Use n.justEntered to trigger one-shot sounds on entry if needed.
    }
}
