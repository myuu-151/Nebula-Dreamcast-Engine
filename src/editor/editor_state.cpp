#include "editor_state.h"

// --- Node vectors ---
// gAudio3DNodes — defined in nodes/Audio3D.cpp
std::vector<StaticMesh3DNode>  gStaticMeshNodes;
std::vector<Camera3DNode>      gCamera3DNodes;
std::vector<Node3DNode>        gNode3DNodes;
std::vector<NavMesh3DNode>     gNavMesh3DNodes;

// --- Scene state ---
std::vector<SceneData> gOpenScenes;
int gActiveScene = -1;
int gForceSelectSceneTab = -1;
bool gPlayMode = false;
std::vector<SceneData> gPlayOriginalScenes;
bool gRequestDreamcastGenerate = false;
bool gSaveAllInProgress = false;

// --- Selection ---
int gSelectedAudio3D = -1;
int gSelectedStaticMesh = -1;
int gSelectedCamera3D = -1;
int gSelectedNode3D = -1;
int gSelectedNavMesh3D = -1;
int gInspectorPinnedAudio3D = -1;
int gInspectorPinnedStaticMesh = -1;
int gInspectorPinnedCamera3D = -1;
int gInspectorPinnedNode3D = -1;
int gInspectorPinnedNavMesh3D = -1;
int gInspectorSel = -1;
char gInspectorName[256] = {};

// --- Node rename ---
int gNodeRenameIndex = -1;
bool gNodeRenameStatic = false;
bool gNodeRenameCamera = false;
bool gNodeRenameNode3D = false;
char gNodeRenameBuffer[256] = {};
bool gNodeRenameOpen = false;

// --- Transform mouse state ---
double gLastTransformMouseX = 0.0;
double gLastTransformMouseY = 0.0;

// --- Viewport toast ---
std::string gViewportToast;
double gViewportToastUntil = 0.0;

// --- Viewport camera bridge ---
float gViewYaw = 0.0f;
float gViewPitch = 0.0f;
float gViewDistance = 0.0f;
Vec3  gOrbitCenter = {};
Vec3  gEye = {};
int   gDisplayW = 0;
int   gDisplayH = 0;

// --- Rotate preview ---
bool  gHasRotatePreview = false;
int   gRotatePreviewIndex = -1;
float gRotatePreviewX = 0.0f;
float gRotatePreviewY = 0.0f;
float gRotatePreviewZ = 0.0f;

// --- Animation preview ---
bool  gStaticAnimPreviewPlay = false;
bool  gStaticAnimPreviewLoop = true;
float gStaticAnimPreviewTimeSec = 0.0f;
int   gStaticAnimPreviewFrame = 0;
int   gStaticAnimPreviewLastNode = -1;
int   gStaticAnimPreviewNode = -1;
int   gStaticAnimPreviewSlot = -1;

// --- Editor play-mode animation state ---
std::unordered_map<int, int>   gEditorAnimActiveSlot;
std::unordered_map<int, float> gEditorAnimTime;
std::unordered_map<int, float> gEditorAnimSpeed;
std::unordered_map<int, bool>  gEditorAnimPlaying;
std::unordered_map<int, bool>  gEditorAnimFinished;
std::unordered_map<int, bool>  gEditorAnimLoop;

// --- Outliner collapse state ---
std::unordered_set<std::string> gCollapsedAudioRoots;
std::unordered_set<std::string> gCollapsedStaticRoots;
std::unordered_set<std::string> gCollapsedCameraRoots;
std::unordered_set<std::string> gCollapsedNode3DRoots;

// --- Inspector material/texture ---
bool gMaterialInspectorOpen = false;
std::filesystem::path gMaterialInspectorPath;
bool gNebTexInspectorOpen = false;
std::filesystem::path gNebTexInspectorPath;

// --- Wireframe ---
bool gWireframePreview = false;
bool gHideUnselectedWireframes = false;

// --- Play-mode scene snapshot ---
bool playSceneSnapshotValid = false;
int playSavedActiveScene = -1;
std::vector<SceneData> playSavedOpenScenes;
std::vector<Audio3DNode> playSavedAudio3DNodes;
std::vector<StaticMesh3DNode> playSavedStaticMeshNodes;
std::vector<Camera3DNode> playSavedCamera3DNodes;
std::vector<Node3DNode> playSavedNode3DNodes;
std::vector<NavMesh3DNode> playSavedNavMesh3DNodes;

void SnapshotPlaySceneState()
{
    playSavedActiveScene = gActiveScene;
    playSavedOpenScenes = gOpenScenes;
    playSavedAudio3DNodes = gAudio3DNodes;
    playSavedStaticMeshNodes = gStaticMeshNodes;
    playSavedCamera3DNodes = gCamera3DNodes;
    playSavedNode3DNodes = gNode3DNodes;
    playSavedNavMesh3DNodes = gNavMesh3DNodes;
    playSceneSnapshotValid = true;
}

void RestorePlaySceneState()
{
    if (!playSceneSnapshotValid) return;
    gOpenScenes = playSavedOpenScenes;
    gActiveScene = playSavedActiveScene;
    gAudio3DNodes = playSavedAudio3DNodes;
    gStaticMeshNodes = playSavedStaticMeshNodes;
    gCamera3DNodes = playSavedCamera3DNodes;
    gNode3DNodes = playSavedNode3DNodes;
    gNavMesh3DNodes = playSavedNavMesh3DNodes;
}
