#pragma once

#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <cstdint>

#include "../math/math_types.h"
#include "../nodes/NodeTypes.h"
#include "../platform/dreamcast/dc_codegen.h"

// --- Node vectors ---
extern std::vector<Audio3DNode>       gAudio3DNodes;
extern std::vector<StaticMesh3DNode>  gStaticMeshNodes;
extern std::vector<Camera3DNode>      gCamera3DNodes;
extern std::vector<Node3DNode>        gNode3DNodes;
extern std::vector<NavMesh3DNode>     gNavMesh3DNodes;

// --- Scene state ---
extern std::vector<SceneData> gOpenScenes;
extern int gActiveScene;
extern int gForceSelectSceneTab;
extern bool gPlayMode;
extern std::vector<SceneData> gPlayOriginalScenes;
extern bool gRequestDreamcastGenerate;
extern bool gSaveAllInProgress;

// --- Selection ---
extern int gSelectedAudio3D;
extern int gSelectedStaticMesh;
extern int gSelectedCamera3D;
extern int gSelectedNode3D;
extern int gSelectedNavMesh3D;
extern int gInspectorPinnedAudio3D;
extern int gInspectorPinnedStaticMesh;
extern int gInspectorPinnedCamera3D;
extern int gInspectorPinnedNode3D;
extern int gInspectorPinnedNavMesh3D;
extern int gInspectorSel;
extern char gInspectorName[256];

// --- Node rename ---
extern int gNodeRenameIndex;
extern bool gNodeRenameStatic;
extern bool gNodeRenameCamera;
extern bool gNodeRenameNode3D;
extern char gNodeRenameBuffer[256];
extern bool gNodeRenameOpen;

// --- Transform mouse state ---
extern double gLastTransformMouseX;
extern double gLastTransformMouseY;

// --- Viewport toast ---
extern std::string gViewportToast;
extern double gViewportToastUntil;

// --- Viewport camera bridge ---
extern float gViewYaw;
extern float gViewPitch;
extern float gViewDistance;
extern Vec3  gOrbitCenter;
extern Vec3  gEye;
extern int   gDisplayW;
extern int   gDisplayH;

// --- Rotate preview ---
extern bool  gHasRotatePreview;
extern int   gRotatePreviewIndex;
extern float gRotatePreviewX;
extern float gRotatePreviewY;
extern float gRotatePreviewZ;

// --- Animation preview ---
extern bool  gStaticAnimPreviewPlay;
extern bool  gStaticAnimPreviewLoop;
extern float gStaticAnimPreviewTimeSec;
extern int   gStaticAnimPreviewFrame;
extern int   gStaticAnimPreviewLastNode;
extern int   gStaticAnimPreviewNode;
extern int   gStaticAnimPreviewSlot;

// --- Editor play-mode animation state ---
extern std::unordered_map<int, int>   gEditorAnimActiveSlot;
extern std::unordered_map<int, float> gEditorAnimTime;
extern std::unordered_map<int, float> gEditorAnimSpeed;
extern std::unordered_map<int, bool>  gEditorAnimPlaying;
extern std::unordered_map<int, bool>  gEditorAnimFinished;
extern std::unordered_map<int, bool>  gEditorAnimLoop;

// --- VMU state ---
extern bool gShowVmuTool;
extern bool gVmuHasImage;
extern bool gVmuLoadOnBoot;
extern std::string gVmuAssetPath;
extern std::vector<VmuAnimLayer> gVmuAnimLayers;
extern int gVmuAnimLayerSel;
extern int gVmuAnimTotalFrames;
extern int gVmuAnimPlayhead;
extern bool gVmuAnimLoop;
extern int gVmuAnimSpeedMode;
extern int gVmuCurrentLoadedType;
extern std::string gVmuLinkedPngPath;
extern std::string gVmuLinkedAnimPath;
extern std::array<uint8_t, 48 * 32> gVmuMono;

// --- Outliner collapse state ---
extern std::unordered_set<std::string> gCollapsedAudioRoots;
extern std::unordered_set<std::string> gCollapsedStaticRoots;
extern std::unordered_set<std::string> gCollapsedCameraRoots;
extern std::unordered_set<std::string> gCollapsedNode3DRoots;

// --- Inspector material/texture ---
extern bool gMaterialInspectorOpen;
extern std::filesystem::path gMaterialInspectorPath;
extern bool gNebTexInspectorOpen;
extern std::filesystem::path gNebTexInspectorPath;

// --- Wireframe ---
extern bool gWireframePreview;
extern bool gHideUnselectedWireframes;
