#pragma once

#include "core/math_types.h"
#include "core/mesh_io.h"
#include "core/anim_io.h"

#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>

struct ImVec2;

// ---------------------------------------------------------------------------
// Globals owned by mesh_inspector.cpp
// ---------------------------------------------------------------------------
extern NebMeshInspectorState gNebMeshInspectorState;
extern std::unordered_map<std::string, NebAnimClip> gStaticAnimClipCache;

// Secondary (bottom-half) material/texture inspector panes
extern bool gMaterialInspectorOpen2;
extern std::filesystem::path gMaterialInspectorPath2;
extern bool gNebTexInspectorOpen2;
extern std::filesystem::path gNebTexInspectorPath2;

// Preview toggle
extern bool gPreviewSaturnSampling;

// ---------------------------------------------------------------------------
// Functions extracted from main.cpp
// ---------------------------------------------------------------------------
void DrawNebMeshInspectorWindow(float deltaTime);
void OpenNebMeshInspector(const std::filesystem::path& absMeshPath);
void OpenNebAnimInspector(const std::filesystem::path& absAnimPath);
void RefreshNebMeshInspectorClipIfNeeded();
void DrawNebMeshMiniPreview(const NebMesh& mesh, const std::vector<Vec3>* posedVertices, const ImVec2& previewSize, const Vec3* helperPoint = nullptr);
int  GetMeshUvLayerCountForMaterial(const std::filesystem::path& matPath);
