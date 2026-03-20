#pragma once

#include <string>
#include "../nodes/NodeTypes.h"

int FindStaticMeshByName(const std::string& name);
int FindNode3DByName(const std::string& name);
int FindCamera3DByName(const std::string& name);

bool TryGetParentByNodeName(const std::string& name, std::string& outParent);
bool WouldCreateHierarchyCycle(const std::string& childName, const std::string& candidateParentName);
bool StaticMeshCreatesCycle(int childIdx, int candidateParentIdx);
bool Node3DCreatesCycle(int childIdx, int candidateParentIdx);

void GetStaticMeshWorldTRS(int idx, float& ox, float& oy, float& oz, float& orx, float& ory, float& orz, float& osx, float& osy, float& osz);
void GetNode3DWorldTRS(int idx, float& ox, float& oy, float& oz, float& orx, float& ory, float& orz, float& osx, float& osy, float& osz);
void GetNode3DWorldTRSQuat(int idx, float& ox, float& oy, float& oz, float& oqw, float& oqx, float& oqy, float& oqz, float& osx, float& osy, float& osz);
void GetCamera3DWorldTR(int idx, float& ox, float& oy, float& oz, float& orx, float& ory, float& orz);

bool TryGetNodeWorldPosByName(const std::string& name, float& ox, float& oy, float& oz);
bool IsCameraUnderNode3D(const Camera3DNode& cam, const std::string& nodeName);

void ReparentStaticMeshKeepWorldPos(int childIdx, const std::string& newParent);
void ResetStaticMeshTransformsKeepWorld(int idx);
