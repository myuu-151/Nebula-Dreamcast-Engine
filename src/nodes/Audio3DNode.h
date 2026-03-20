#pragma once

#include <string>
#include <vector>

struct Audio3DNode
{
    std::string name;
    std::string parent;
    std::string script;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float innerRadius = 3.0f;  // max volume inside
    float outerRadius = 10.0f; // min volume at edge
    float baseVolume = 1.0f;
    float pan = 0.0f;    // -1..1
    float volume = 1.0f; // 0..1

    float rotX = 0.0f;
    float rotY = 0.0f;
    float rotZ = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float scaleZ = 1.0f;

    bool inside = false;      // listener inside outer radius
    bool justEntered = false; // trigger on enter
};

extern std::vector<Audio3DNode> gAudio3DNodes;

void UpdateAudio3DNodes(float listenerX, float listenerY, float listenerZ);
