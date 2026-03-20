#include "mesh_inspector.h"

#include "imgui.h"

#define NOMINMAX
#include <Windows.h>
#include <GL/gl.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "editor/project.h"
#include "asset_browser.h"
#include "core/meta_io.h"
#include "core/mesh_io.h"
#include "core/anim_io.h"
#include "nodes/NodeTypes.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_set>

#include <GLFW/glfw3.h>

// ---------------------------------------------------------------------------
// Globals owned by this module
// ---------------------------------------------------------------------------
NebMeshInspectorState gNebMeshInspectorState;
std::unordered_map<std::string, NebAnimClip> gStaticAnimClipCache;

bool gMaterialInspectorOpen2 = false;
std::filesystem::path gMaterialInspectorPath2;
bool gNebTexInspectorOpen2 = false;
std::filesystem::path gNebTexInspectorPath2;

bool gPreviewSaturnSampling = true;

// ---------------------------------------------------------------------------
// Externs for globals that live in main.cpp or other modules
// ---------------------------------------------------------------------------
extern std::string gViewportToast;
extern double gViewportToastUntil;
extern std::vector<StaticMesh3DNode> gStaticMeshNodes;

extern bool gImportPopupOpen;
extern std::string gImportPath;
extern Assimp::Importer gImportAssimp;
extern const aiScene* gImportScene;
extern std::vector<bool> gImportAnimConvert;
extern std::string gImportBaseNebMeshPath;

// Helpers defined in main.cpp — made non-static for this module
extern std::string ToProjectRelativePath(const std::filesystem::path& p);
extern std::filesystem::path GetNebMeshMetaPath(const std::filesystem::path& absMeshPath);
extern bool LoadNebMeshVtxAnimLink(const std::filesystem::path& absMeshPath, std::string& outAnimPath);

// ---------------------------------------------------------------------------
// GetMeshUvLayerCountForMaterial
// ---------------------------------------------------------------------------
int GetMeshUvLayerCountForMaterial(const std::filesystem::path& matPath)
{
    if (matPath.empty() || gProjectDir.empty()) return 1;
    std::filesystem::path relMat;
    {
        std::error_code ec;
        relMat = std::filesystem::relative(matPath, std::filesystem::path(gProjectDir), ec);
        if (ec || relMat.empty()) return 1;
    }
    std::string relMatStr = relMat.generic_string();
    // Search cached meshes first
    for (auto& kv : gNebMeshCache)
    {
        if (!kv.second.valid) continue;
        std::filesystem::path meshPath(kv.first);
        std::vector<std::string> slots;
        if (NebulaAssets::LoadNebSlotsManifest(meshPath, slots, gProjectDir))
        {
            for (const auto& sl : slots)
            {
                if (sl == relMatStr) return kv.second.uvLayerCount;
            }
        }
    }
    // Search scene static mesh nodes
    for (const auto& node : gStaticMeshNodes)
    {
        if (node.mesh.empty()) continue;
        for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
        {
            if (node.materialSlots[si] == relMatStr)
            {
                std::filesystem::path meshAbs = std::filesystem::path(gProjectDir) / node.mesh;
                if (std::filesystem::exists(meshAbs))
                {
                    const NebMesh* m = GetNebMesh(meshAbs);
                    if (m && m->valid) return m->uvLayerCount;
                    return ReadNebMeshUvLayerCount(meshAbs);
                }
            }
        }
    }
    return 1; // default: assume at least UV0
}

// ---------------------------------------------------------------------------
// DrawNebMeshInspectorWindow
// ---------------------------------------------------------------------------
void DrawNebMeshInspectorWindow(float deltaTime)
{
    if (!gNebMeshInspectorState.open)
        return;

    NebMeshInspectorState& st = gNebMeshInspectorState;
    RefreshNebMeshInspectorClipIfNeeded();

    bool open = st.open;
    ImGui::SetNextWindowSize(ImVec2(460.0f, 430.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("NebMesh Inspector", &open))
    {
        st.open = open;
        ImGui::End();
        return;
    }
    st.open = open;

    std::filesystem::path meshPath = st.targetMeshPath;
    ImGui::TextUnformatted("Mesh");
    if (ImGui::Button(">##NebMeshPickForAnimInspector"))
    {
        if (!gSelectedAssetPath.empty() && gSelectedAssetPath.extension() == ".nebmesh")
        {
            meshPath = gSelectedAssetPath;
            OpenNebMeshInspector(meshPath);
        }
    }
    ImGui::SameLine();
    if (!ImGui::IsAnyItemActive() && !meshPath.empty())
        strncpy_s(st.meshInput, ToProjectRelativePath(meshPath).c_str(), sizeof(st.meshInput) - 1);
    if (ImGui::InputText("##NebMeshPathForAnimInspector", st.meshInput, sizeof(st.meshInput)))
    {
        std::filesystem::path rp = ResolveProjectAssetPath(st.meshInput);
        if (!rp.empty() && rp.extension() == ".nebmesh")
            OpenNebMeshInspector(rp);
    }

    meshPath = st.targetMeshPath;
    if (meshPath.empty())
        ImGui::TextDisabled("No .nebmesh selected (pick one to preview animation)");
    else
    {
        ImGui::TextWrapped("%s", meshPath.filename().string().c_str());
        ImGui::Text("Meta: %s", ToProjectRelativePath(GetNebMeshMetaPath(meshPath)).c_str());
        ImGui::Text("AnimMeta: %s", ToProjectRelativePath(GetNebMeshEmbeddedMetaPath(meshPath)).c_str());
    }

    const NebMesh* mesh = meshPath.empty() ? nullptr : GetNebMesh(meshPath);
    if (st.embeddedMetaLoaded && !st.embeddedMeta.mappingVerified)
        RebuildNebMeshEmbeddedMapping(st);
    if (st.embeddedMetaLoaded && st.embeddedScene == nullptr)
    {
        std::filesystem::path absFbx = ResolveProjectAssetPath(st.embeddedMeta.sourceFbxPath);
        if (!absFbx.empty() && std::filesystem::exists(absFbx))
        {
            st.embeddedScene = st.embeddedImporter.ReadFile(
                absFbx.string(),
                aiProcess_JoinIdenticalVertices |
                aiProcess_GlobalScale);
            st.embeddedLoadedFbxPath = absFbx.generic_string();
        }
        st.embeddedSourceOk = (st.embeddedScene != nullptr);
    }

    const InspectorMappingQuality mapQuality = ParseMappingQuality(st.embeddedMeta.mappingQuality);
    st.inspectorPlayback.diagnostics.mappingQuality = mapQuality;

    ImGui::Separator();
    ImGui::TextUnformatted("Animation Source");
    ImGui::TextDisabled("Embedded (FBX) primary; External .nebanim is legacy fallback.");
    if (st.embeddedMetaLoaded)
    {
        ImGui::TextWrapped("Source FBX: %s", st.embeddedMeta.sourceFbxPath.c_str());
        ImGui::Text("Embedded source: %s", st.embeddedSourceOk ? "OK" : "Missing");
        if (st.driveNebAnimOnly)
            ImGui::TextDisabled("Embedded clips disabled (driven by .nebanim)");
        ImGui::Text("Mapping: %s", (mapQuality == InspectorMappingQuality::Exact) ? "Exact (Provenance) [OK]" :
            (mapQuality == InspectorMappingQuality::Approx) ? "Approximate [WARN]" : "Missing [ERR]");
        if (mapQuality == InspectorMappingQuality::Approx)
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "Approx mapping disabled to prevent corrupted preview");
        if (!st.embeddedStatusMessage.empty())
            ImGui::TextDisabled("%s", st.embeddedStatusMessage.c_str());

        if (!st.embeddedMeta.clipNames.empty())
        {
            if (st.inspectorPlayback.selectedClip < 0) st.inspectorPlayback.selectedClip = 0;
            if (st.inspectorPlayback.selectedClip >= (int)st.embeddedMeta.clipNames.size()) st.inspectorPlayback.selectedClip = (int)st.embeddedMeta.clipNames.size() - 1;
            std::vector<const char*> labels;
            labels.reserve(st.embeddedMeta.clipNames.size());
            for (const auto& n : st.embeddedMeta.clipNames) labels.push_back(n.c_str());
            int prevClip = st.inspectorPlayback.selectedClip;
            if (ImGui::Combo("Embedded Clip", &st.inspectorPlayback.selectedClip, labels.data(), (int)labels.size()) &&
                st.inspectorPlayback.selectedClip != prevClip)
            {
                st.inspectorPlayback.currentFrame = 0;
                st.inspectorPlayback.currentTimeSec = 0.0f;
                st.embeddedDiagDirty = true;
                st.nearStaticWarned = false;
                st.embeddedCacheValid = false;
                st.loggedCurrentPreviewState = false;
            }
        }
        else
        {
            ImGui::TextDisabled("No embedded clips listed");
        }

        if (ImGui::Button("Rebuild Animation Mapping"))
            RebuildNebMeshEmbeddedMapping(st);
        ImGui::SameLine();
        if (ImGui::Button("Convert Animations"))
        {
            std::filesystem::path absFbx = ResolveProjectAssetPath(st.embeddedMeta.sourceFbxPath);
            if (!absFbx.empty() && std::filesystem::exists(absFbx))
            {
                gImportPath = absFbx.generic_string();
                gImportBaseNebMeshPath = ToProjectRelativePath(meshPath);
                gImportScene = gImportAssimp.ReadFile(gImportPath,
                    aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);
                gImportAnimConvert.clear();
                if (gImportScene && gImportScene->mNumAnimations > 0)
                {
                    gImportAnimConvert.resize(gImportScene->mNumAnimations, true);
                    gImportPopupOpen = true;
                }
                else
                {
                    gViewportToast = "No animations found in source FBX";
                    gViewportToastUntil = glfwGetTime() + 2.0;
                }
            }
            else
            {
                gViewportToast = "Embedded source FBX missing";
                gViewportToastUntil = glfwGetTime() + 2.0;
            }
        }
    }
    else
    {
        ImGui::TextDisabled("No embedded metadata found (.animmeta missing)");
    }

    const char* modeLabels[] = { "EmbeddedExact", "EmbeddedApprox", "ExternalLegacy" };
    int modeInt = (int)st.playbackMode;
    if (ImGui::Combo("Playback Mode", &modeInt, modeLabels, 3))
    {
        st.playbackMode = (InspectorPlaybackMode)modeInt;
        if (st.playbackMode != InspectorPlaybackMode::ExternalLegacy)
            st.driveNebAnimOnly = false;
        st.inspectorPlayback.currentFrame = 0;
        st.inspectorPlayback.currentTimeSec = 0.0f;
        st.embeddedCacheValid = false;
        st.loggedCurrentPreviewState = false;
    }
    if (st.driveNebAnimOnly)
    {
        if (st.playbackMode != InspectorPlaybackMode::ExternalLegacy)
            st.playbackMode = InspectorPlaybackMode::ExternalLegacy;
    }

    if (st.playbackMode == InspectorPlaybackMode::EmbeddedApprox)
        ImGui::Checkbox("Force-enable approx deformation (advanced)", &st.forceEnableApproxPreview);

    uint32_t activeFrameCount = 0;
    float activeFps = 12.0f;
    bool usingEmbeddedPlayback = false;
    const aiAnimation* selectedEmbeddedAnim = nullptr;
    if (st.playbackMode == InspectorPlaybackMode::ExternalLegacy)
    {
        if (st.clip.valid && st.clip.frameCount > 0)
        {
            activeFrameCount = st.clip.frameCount;
            activeFps = std::max(1.0f, st.clip.fps);
        }
    }
    else if (mesh && mesh->valid && st.embeddedMetaLoaded && st.embeddedScene)
    {
        if (st.inspectorPlayback.selectedClip >= 0 && st.inspectorPlayback.selectedClip < (int)st.embeddedMeta.clipNames.size())
            selectedEmbeddedAnim = FindAnimByNameOrIndex(st.embeddedScene, st.embeddedMeta.clipNames[(size_t)st.inspectorPlayback.selectedClip], st.inspectorPlayback.selectedClip);
        else
            selectedEmbeddedAnim = FindAnimByNameOrIndex(st.embeddedScene, std::string(), 0);
        if (selectedEmbeddedAnim)
        {
            const double tps = selectedEmbeddedAnim->mTicksPerSecond != 0.0 ? selectedEmbeddedAnim->mTicksPerSecond : 24.0;
            const double durationSec = (tps > 0.0) ? (selectedEmbeddedAnim->mDuration / tps) : 0.0;
            activeFrameCount = (uint32_t)std::max(1.0, std::floor(durationSec * activeFps + 0.5) + 1.0);
            usingEmbeddedPlayback = activeFrameCount > 0;
        }
    }

    if (usingEmbeddedPlayback && selectedEmbeddedAnim)
    {
        ImGui::Checkbox("Debug anim diagnostics (log)", &st.animDebugDumpEnabled);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0f);
        if (ImGui::InputInt("Probe frame##AnimDiagProbe", &st.animDebugProbeFrame))
            st.animDebugLastProbeFrame = -1;
        if (st.animDebugProbeFrame < 0) st.animDebugProbeFrame = 0;
        if (activeFrameCount > 0 && st.animDebugProbeFrame >= (int)activeFrameCount) st.animDebugProbeFrame = (int)activeFrameCount - 1;
        ImGui::SameLine();
        if (ImGui::Button("Dump now##AnimDiagDump"))
            st.animDebugForceDump = true;
    }

    if (usingEmbeddedPlayback && selectedEmbeddedAnim &&
        (st.embeddedDiagDirty || st.lastEmbeddedClipIndex != st.inspectorPlayback.selectedClip))
    {
        AnimBakeDiagnostics diag;
        if (ComputeEmbeddedClipDiagnostics(st.embeddedScene, selectedEmbeddedAnim, st.embeddedMeta.meshIndices, 12.0f, diag))
        {
            printf("[AnimBake] totalBones=%d matchedBones=%d unmatchedBones=%d channelsFound=%d maxDelta=%.6f\n",
                diag.totalBones, diag.matchedBones, diag.unmatchedBones, diag.channelsFound, diag.maxVertexDeltaFromFrame0);
            st.inspectorPlayback.diagnostics.skinning = diag;
            st.inspectorPlayback.diagnostics.skinningValid = true;
            if (diag.maxVertexDeltaFromFrame0 < 1e-4f && !st.nearStaticWarned)
            {
                gViewportToast = "Embedded clip sampled but produced near-static deformation (bone/channel mismatch)";
                gViewportToastUntil = glfwGetTime() + 2.5;
                st.nearStaticWarned = true;
            }
        }
        else
        {
            st.inspectorPlayback.diagnostics.skinningValid = false;
        }
        st.embeddedDiagDirty = false;
        st.lastEmbeddedClipIndex = st.inspectorPlayback.selectedClip;
    }

    if (usingEmbeddedPlayback && selectedEmbeddedAnim)
    {
        const int probeFrame = std::max(0, std::min(st.animDebugProbeFrame, (int)activeFrameCount - 1));
        const bool autoDump = st.animDebugDumpEnabled &&
            (st.animDebugLastClip != st.inspectorPlayback.selectedClip || st.animDebugLastProbeFrame != probeFrame);
        if (autoDump || st.animDebugForceDump)
        {
            DumpEmbeddedAnimDiagnostics(st.embeddedScene, selectedEmbeddedAnim, st.embeddedMeta.meshIndices, probeFrame, activeFps);
            st.animDebugLastClip = st.inspectorPlayback.selectedClip;
            st.animDebugLastProbeFrame = probeFrame;
            st.animDebugForceDump = false;
        }
    }
    st.playback.fps = activeFps;

    if (activeFrameCount > 0 && st.playback.playing)
    {
        st.inspectorPlayback.currentTimeSec += std::max(0.0f, deltaTime);
        const float maxTime = (float)(activeFrameCount - 1) / std::max(1.0f, activeFps);
        if (st.playback.loop && maxTime > 0.0f)
        {
            while (st.inspectorPlayback.currentTimeSec > maxTime)
                st.inspectorPlayback.currentTimeSec -= maxTime;
        }
        else if (st.inspectorPlayback.currentTimeSec > maxTime)
        {
            st.inspectorPlayback.currentTimeSec = maxTime;
            st.playback.playing = false;
        }
        int frame = (int)std::floor(st.inspectorPlayback.currentTimeSec * activeFps + 0.5f);
        st.inspectorPlayback.currentFrame = std::max(0, std::min(frame, (int)activeFrameCount - 1));
    }
    if (activeFrameCount > 0 && st.inspectorPlayback.currentFrame >= (int)activeFrameCount)
        st.inspectorPlayback.currentFrame = (int)activeFrameCount - 1;

    std::vector<Vec3> embeddedPosed;
    const std::vector<Vec3>* posed = nullptr;
    bool previewApplied = false;
    std::string previewReason = "No playable animation source";
    const bool invalidProvenance = (!st.embeddedMeta.provenanceMeshIndices.empty() &&
                                    st.embeddedMeta.provenanceMeshIndices.size() == st.embeddedMeta.provenanceVertexIndices.size() &&
                                    mapQuality != InspectorMappingQuality::Exact);

    if (!mesh || !mesh->valid)
    {
        previewReason = "Mesh missing or invalid";
    }
    else if (st.playbackMode == InspectorPlaybackMode::ExternalLegacy)
    {
        if (!st.clip.valid || st.clip.frameCount == 0) previewReason = "No external legacy clip";
        else if (st.clip.vertexCount != (uint32_t)mesh->positions.size()) previewReason = "External clip vertex count mismatch";
        else if (st.inspectorPlayback.currentFrame < 0 || st.inspectorPlayback.currentFrame >= (int)st.clip.frames.size()) previewReason = "Clip sampling failed";
        else
        {
            posed = &st.clip.frames[(size_t)st.inspectorPlayback.currentFrame];
            previewApplied = (posed->size() == mesh->positions.size());
            previewReason = previewApplied ? "OK" : "Clip sampling failed";
        }
    }
    else
    {
        if (!st.embeddedMetaLoaded || !st.embeddedSourceOk || !st.embeddedScene) previewReason = "Embedded source missing";
        else if (!usingEmbeddedPlayback || !selectedEmbeddedAnim) previewReason = "Clip sampling failed";
        else if (st.playbackMode == InspectorPlaybackMode::EmbeddedExact && mapQuality != InspectorMappingQuality::Exact)
            previewReason = invalidProvenance ? "Invalid provenance" : "Exact mapping unavailable";
        else if (st.playbackMode == InspectorPlaybackMode::EmbeddedApprox && mapQuality == InspectorMappingQuality::Approx && !st.forceEnableApproxPreview)
            previewReason = "Approx mapping disabled to prevent corrupted preview";
        else if (!st.embeddedMappingOk || st.embeddedVertexMap.size() != mesh->positions.size())
            previewReason = "Exact mapping unavailable";
        else
        {
            std::vector<Vec3> merged;
            if (!SampleEmbeddedMergedVerticesCached(st, selectedEmbeddedAnim, st.inspectorPlayback.selectedClip, st.inspectorPlayback.currentFrame, activeFps, merged))
                previewReason = "Clip sampling failed";
            else if (st.embeddedVertexMap.size() != mesh->positions.size())
                previewReason = "Exact mapping unavailable";
            else
            {
                embeddedPosed.resize(mesh->positions.size());
                bool badSrc = false;
                for (size_t i = 0; i < st.embeddedVertexMap.size(); ++i)
                {
                    const uint32_t src = st.embeddedVertexMap[i];
                    if (src >= merged.size()) { badSrc = true; break; }
                    embeddedPosed[i] = merged[src];
                }
                if (badSrc)
                    previewReason = "Invalid provenance";
                else
                {
                    posed = &embeddedPosed;
                    previewApplied = (embeddedPosed.size() == mesh->positions.size());
                    previewReason = previewApplied ? "OK" : "Exact mapping unavailable";
                }
            }
        }
    }

    st.inspectorPlayback.diagnostics.previewReason = previewReason;
    if (!st.loggedCurrentPreviewState ||
        st.loggedPreviewFrame != st.inspectorPlayback.currentFrame ||
        st.loggedPreviewMode != st.playbackMode ||
        st.lastPreviewReason != previewReason)
    {
        printf("[AnimPreview] applied=%d reason=%s mode=%s frame=%d\n",
            previewApplied ? 1 : 0,
            previewReason.c_str(),
            PlaybackModeLabel(st.playbackMode),
            st.inspectorPlayback.currentFrame);
        st.loggedCurrentPreviewState = true;
        st.loggedPreviewFrame = st.inspectorPlayback.currentFrame;
        st.loggedPreviewMode = st.playbackMode;
        st.lastPreviewReason = previewReason;
    }

    ImGui::TextUnformatted("Diagnostics");
    ImGui::Text("Playback mode: %s", PlaybackModeLabel(st.playbackMode));
    ImGui::Text("Mapping quality: %s", MappingQualityLabel(mapQuality));
    if (st.inspectorPlayback.diagnostics.skinningValid)
    {
        const AnimBakeDiagnostics& d = st.inspectorPlayback.diagnostics.skinning;
        ImGui::Text("Bones matched: %d/%d  unmatched: %d  channels: %d",
            d.matchedBones, d.totalBones, d.unmatchedBones, d.channelsFound);

        if (st.embeddedScene && ImGui::CollapsingHeader("Bone match hierarchy", ImGuiTreeNodeFlags_None))
        {
            std::unordered_set<const aiNode*> matchedBones;
            std::unordered_set<const aiNode*> visibleNodes;
            for (unsigned int mi : st.embeddedMeta.meshIndices)
            {
                if (mi >= st.embeddedScene->mNumMeshes || !st.embeddedScene->mMeshes[mi]) continue;
                const aiMesh* m = st.embeddedScene->mMeshes[mi];
                for (unsigned int bi = 0; bi < m->mNumBones; ++bi)
                {
                    const aiBone* b = m->mBones[bi];
                    if (!b) continue;
                    const aiNode* bn = ResolveSceneNodeByNameRobust(st.embeddedScene, b->mName);
                    if (!bn) continue;
                    matchedBones.insert(bn);
                    for (const aiNode* p = bn; p; p = p->mParent)
                        visibleNodes.insert(p);
                }
            }

            std::function<void(const aiNode*)> drawNode = [&](const aiNode* n)
            {
                if (!n || visibleNodes.find(n) == visibleNodes.end()) return;

                bool hasVisibleChild = false;
                for (unsigned int ci = 0; ci < n->mNumChildren; ++ci)
                {
                    if (visibleNodes.find(n->mChildren[ci]) != visibleNodes.end())
                    {
                        hasVisibleChild = true;
                        break;
                    }
                }

                const bool isMatchedBone = matchedBones.find(n) != matchedBones.end();
                const bool hasChannel = (selectedEmbeddedAnim && AiFindChannel(selectedEmbeddedAnim, n->mName));
                std::string label = n->mName.length > 0 ? n->mName.C_Str() : "<unnamed>";
                if (isMatchedBone)
                    label += hasChannel ? "  [bone|ch]" : "  [bone]";

                ImGuiTreeNodeFlags flags = hasVisibleChild ? 0 : (ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
                if (n->mName.length > 0 && st.selectedHelperBoneName == n->mName.C_Str())
                    flags |= ImGuiTreeNodeFlags_Selected;
                bool open = ImGui::TreeNodeEx((void*)n, flags, "%s", label.c_str());
                if (ImGui::IsItemClicked() && n->mName.length > 0)
                    st.selectedHelperBoneName = n->mName.C_Str();
                if (hasVisibleChild && open)
                {
                    for (unsigned int ci = 0; ci < n->mNumChildren; ++ci)
                        drawNode(n->mChildren[ci]);
                    ImGui::TreePop();
                }
            };

            if (!matchedBones.empty())
            {
                std::vector<std::string> selectableBones;
                std::function<void(const aiNode*)> collectSelectable = [&](const aiNode* n)
                {
                    if (!n || visibleNodes.find(n) == visibleNodes.end()) return;
                    if (matchedBones.find(n) != matchedBones.end() && n->mName.length > 0)
                        selectableBones.push_back(n->mName.C_Str());
                    for (unsigned int ci = 0; ci < n->mNumChildren; ++ci)
                        collectSelectable(n->mChildren[ci]);
                };
                collectSelectable(st.embeddedScene->mRootNode);

                if (!selectableBones.empty() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput)
                {
                    int sel = -1;
                    for (int i = 0; i < (int)selectableBones.size(); ++i)
                    {
                        if (st.selectedHelperBoneName == selectableBones[(size_t)i]) { sel = i; break; }
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
                    {
                        if (sel < 0) sel = 0;
                        else sel = (sel - 1 + (int)selectableBones.size()) % (int)selectableBones.size();
                        st.selectedHelperBoneName = selectableBones[(size_t)sel];
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
                    {
                        if (sel < 0) sel = 0;
                        else sel = (sel + 1) % (int)selectableBones.size();
                        st.selectedHelperBoneName = selectableBones[(size_t)sel];
                    }
                }

                if (!st.selectedHelperBoneName.empty())
                {
                    ImGui::Text("Selected helper: %s", st.selectedHelperBoneName.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear helper"))
                        st.selectedHelperBoneName.clear();
                }
                drawNode(st.embeddedScene->mRootNode);
                ImGui::TextDisabled("[bone] matched bone, [bone|ch] matched with animation channel");
            }
            else
            {
                ImGui::TextDisabled("No matched bones found for selected embedded mesh set.");
            }
        }
    }
    if (!previewApplied)
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.45f, 1.0f), "%s", previewReason.c_str());

    Vec3 helperPoint = { 0.0f, 0.0f, 0.0f };
    bool helperPointValid = false;
    if (!st.selectedHelperBoneName.empty() && st.embeddedScene && selectedEmbeddedAnim)
    {
        aiString q;
        q.Set(st.selectedHelperBoneName);
        const aiNode* helperNode = ResolveSceneNodeByNameRobust(st.embeddedScene, q);
        if (helperNode)
        {
            const double tps = selectedEmbeddedAnim->mTicksPerSecond != 0.0 ? selectedEmbeddedAnim->mTicksPerSecond : 24.0;
            const double timeTicks = ((double)st.inspectorPlayback.currentFrame / std::max(1.0f, activeFps)) * tps;
            aiMatrix4x4 global;
            aiMatrix4x4 identity;
            if (AiFindNodeGlobal(st.embeddedScene->mRootNode, selectedEmbeddedAnim, timeTicks, identity, helperNode, global))
            {
                aiVector3D h = AiTransformPoint(global, aiVector3D(0, 0, 0));
                helperPoint = ApplyImportBasis(Vec3{ h.x, h.y, h.z });
                helperPointValid = true;
            }
        }
    }

    ImGui::TextUnformatted("Preview");
    if (mesh && mesh->valid)
    {
        DrawNebMeshMiniPreview(*mesh, posed, ImVec2(-1.0f, 180.0f), helperPointValid ? &helperPoint : nullptr);
        if (!previewApplied)
            ImGui::TextDisabled("Previewing base mesh");
    }
    else
    {
        static const NebMesh kEmptyPreviewMesh;
        DrawNebMeshMiniPreview(kEmptyPreviewMesh, nullptr, ImVec2(-1.0f, 180.0f), nullptr);
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Mesh missing or invalid");
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Legacy External .nebanim (advanced)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextDisabled("Legacy path is optional fallback only.");
        if (ImGui::Button(">##NebAnimPickLegacy"))
        {
            if (!gSelectedAssetPath.empty() && gSelectedAssetPath.extension() == ".nebanim")
            {
                st.vtxAnimPath = ToProjectRelativePath(gSelectedAssetPath);
                strncpy_s(st.vtxAnimInput, st.vtxAnimPath.c_str(), sizeof(st.vtxAnimInput) - 1);
                st.loadedVtxAnimPath.clear();
                RefreshNebMeshInspectorClipIfNeeded();
            }
        }
        ImGui::SameLine();
        if (ImGui::InputText("##LegacyNebAnimPath", st.vtxAnimInput, sizeof(st.vtxAnimInput)))
        {
            st.vtxAnimPath = st.vtxAnimInput;
            st.loadedVtxAnimPath.clear();
            RefreshNebMeshInspectorClipIfNeeded();
        }
        ImGui::Text("Clip: %s", st.clip.valid ? "Loaded" : "Missing");
        if (ImGui::Button(st.driveNebAnimOnly ? "Drive .nebanim: ON" : "Drive .nebanim: OFF"))
        {
            st.driveNebAnimOnly = !st.driveNebAnimOnly;
            st.playbackMode = st.driveNebAnimOnly ? InspectorPlaybackMode::ExternalLegacy : InspectorPlaybackMode::EmbeddedExact;
            st.inspectorPlayback.currentFrame = 0;
            st.inspectorPlayback.currentTimeSec = 0.0f;
            st.loggedCurrentPreviewState = false;
        }
    }

    if (activeFrameCount > 0)
    {
        if (ImGui::Button(st.playback.playing ? "Pause" : "Play"))
            st.playback.playing = !st.playback.playing;
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &st.playback.loop);

        int frame = st.inspectorPlayback.currentFrame;
        if (ImGui::SliderInt("Timeline", &frame, 0, (int)activeFrameCount - 1))
        {
            st.inspectorPlayback.currentFrame = frame;
            st.inspectorPlayback.currentTimeSec = (float)frame / std::max(1.0f, st.playback.fps);
            st.loggedCurrentPreviewState = false;
        }
        const float progress = (activeFrameCount > 1)
            ? (float)st.inspectorPlayback.currentFrame / (float)(activeFrameCount - 1)
            : 0.0f;
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 6.0f));
        ImGui::Text("Frame %d / %u  (%.2f fps) [%s]",
            st.inspectorPlayback.currentFrame + 1,
            activeFrameCount,
            st.playback.fps,
            PlaybackModeLabel(st.playbackMode));
    }
    else
    {
        ImGui::TextUnformatted("Timeline");
        float none = 0.0f;
        ImGui::SliderFloat("##TimelinePlaceholder", &none, 0.0f, 1.0f, "", ImGuiSliderFlags_NoInput);
        ImGui::TextDisabled("No playable animation source");
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// OpenNebMeshInspector
// ---------------------------------------------------------------------------
void OpenNebMeshInspector(const std::filesystem::path& absMeshPath)
{
    gNebMeshInspectorState.open = true;
    if (gNebMeshInspectorState.targetMeshPath == absMeshPath)
        return;

    gNebMeshInspectorState.targetMeshPath = absMeshPath;
    strncpy_s(gNebMeshInspectorState.meshInput, ToProjectRelativePath(absMeshPath).c_str(), sizeof(gNebMeshInspectorState.meshInput) - 1);
    gNebMeshInspectorState.vtxAnimPath.clear();
    gNebMeshInspectorState.loadedVtxAnimPath.clear();
    gNebMeshInspectorState.clip = NebAnimClip{};
    gNebMeshInspectorState.playback = VtxAnimPlaybackState{};
    gNebMeshInspectorState.inspectorPlayback = InspectorPlaybackState{};
    gNebMeshInspectorState.embeddedMeta = NebMeshEmbeddedAnimMeta{};
    gNebMeshInspectorState.embeddedMetaLoaded = LoadNebMeshEmbeddedMeta(absMeshPath, gNebMeshInspectorState.embeddedMeta);
    gNebMeshInspectorState.playbackMode = InspectorPlaybackMode::EmbeddedExact;
    gNebMeshInspectorState.driveNebAnimOnly = false;
    gNebMeshInspectorState.forceEnableApproxPreview = false;
    gNebMeshInspectorState.embeddedSourceOk = gNebMeshInspectorState.embeddedMetaLoaded;
    gNebMeshInspectorState.embeddedMappingOk = gNebMeshInspectorState.embeddedMetaLoaded && gNebMeshInspectorState.embeddedMeta.mappingOk;
    gNebMeshInspectorState.embeddedStatusMessage.clear();
    gNebMeshInspectorState.embeddedVertexMap = gNebMeshInspectorState.embeddedMeta.mapIndices;
    gNebMeshInspectorState.embeddedLoadedFbxPath.clear();
    gNebMeshInspectorState.embeddedScene = nullptr;
    gNebMeshInspectorState.lastEmbeddedClipIndex = -1;
    gNebMeshInspectorState.embeddedDiagDirty = true;
    gNebMeshInspectorState.nearStaticWarned = false;
    gNebMeshInspectorState.embeddedCacheValid = false;
    gNebMeshInspectorState.embeddedCacheClipIndex = -1;
    gNebMeshInspectorState.embeddedCacheFrame = -1;
    gNebMeshInspectorState.embeddedCacheMergedVertices.clear();
    gNebMeshInspectorState.loggedCurrentPreviewState = false;
    gNebMeshInspectorState.loggedPreviewFrame = -1;
    gNebMeshInspectorState.loggedPreviewMode = InspectorPlaybackMode::EmbeddedExact;
    gNebMeshInspectorState.lastPreviewReason.clear();
    LoadNebMeshVtxAnimLink(absMeshPath, gNebMeshInspectorState.vtxAnimPath);
    strncpy_s(gNebMeshInspectorState.vtxAnimInput, gNebMeshInspectorState.vtxAnimPath.c_str(), sizeof(gNebMeshInspectorState.vtxAnimInput) - 1);

    if (gNebMeshInspectorState.embeddedMetaLoaded && gNebMeshInspectorState.embeddedVertexMap.empty() && gNebMeshInspectorState.embeddedMeta.mappingOk)
    {
        const NebMesh* nm = GetNebMesh(absMeshPath);
        if (nm && nm->valid)
        {
            gNebMeshInspectorState.embeddedVertexMap.resize(nm->positions.size());
            for (size_t i = 0; i < nm->positions.size(); ++i) gNebMeshInspectorState.embeddedVertexMap[i] = (uint32_t)i;
        }
    }

    if (gNebMeshInspectorState.embeddedMetaLoaded && !gNebMeshInspectorState.embeddedMeta.mappingVerified)
        RebuildNebMeshEmbeddedMapping(gNebMeshInspectorState);
}

// ---------------------------------------------------------------------------
// OpenNebAnimInspector
// ---------------------------------------------------------------------------
void OpenNebAnimInspector(const std::filesystem::path& absAnimPath)
{
    gNebMeshInspectorState.open = true;
    gNebMeshInspectorState.vtxAnimPath = ToProjectRelativePath(absAnimPath);
    if (gNebMeshInspectorState.vtxAnimPath.empty())
        gNebMeshInspectorState.vtxAnimPath = absAnimPath.generic_string();
    strncpy_s(gNebMeshInspectorState.vtxAnimInput, gNebMeshInspectorState.vtxAnimPath.c_str(), sizeof(gNebMeshInspectorState.vtxAnimInput) - 1);
    gNebMeshInspectorState.loadedVtxAnimPath.clear();
    gNebMeshInspectorState.driveNebAnimOnly = true;
    gNebMeshInspectorState.playbackMode = InspectorPlaybackMode::ExternalLegacy;
    RefreshNebMeshInspectorClipIfNeeded();
}

// ---------------------------------------------------------------------------
// RefreshNebMeshInspectorClipIfNeeded
// ---------------------------------------------------------------------------
void RefreshNebMeshInspectorClipIfNeeded()
{
    if (!gNebMeshInspectorState.open)
        return;

    const std::string animRef = gNebMeshInspectorState.vtxAnimPath;
    if (animRef == gNebMeshInspectorState.loadedVtxAnimPath)
        return;

    gNebMeshInspectorState.loadedVtxAnimPath = animRef;
    gNebMeshInspectorState.clip = NebAnimClip{};
    gNebMeshInspectorState.playback.currentFrame = 0;
    gNebMeshInspectorState.playback.currentTimeSec = 0.0f;
    gNebMeshInspectorState.inspectorPlayback.currentFrame = 0;
    gNebMeshInspectorState.inspectorPlayback.currentTimeSec = 0.0f;

    if (animRef.empty())
        return;

    std::filesystem::path absAnim = ResolveProjectAssetPath(animRef);
    std::string err;
    if (!LoadNebAnimClip(absAnim, gNebMeshInspectorState.clip, err))
    {
        gViewportToast = "NebMesh Inspector: " + err;
        gViewportToastUntil = glfwGetTime() + 2.0;
        return;
    }

    gNebMeshInspectorState.playback.fps = gNebMeshInspectorState.clip.fps;
}

// ---------------------------------------------------------------------------
// DrawNebMeshMiniPreview
// ---------------------------------------------------------------------------
void DrawNebMeshMiniPreview(const NebMesh& mesh, const std::vector<Vec3>* posedVertices, const ImVec2& previewSize, const Vec3* helperPoint)
{
    ImVec2 drawSize = previewSize;
    if (drawSize.x <= 0.0f)
        drawSize.x = std::max(8.0f, ImGui::GetContentRegionAvail().x);
    if (drawSize.y <= 0.0f)
        drawSize.y = 160.0f;

    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = ImVec2(p0.x + drawSize.x, p0.y + drawSize.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(20, 22, 28, 255), 3.0f);
    dl->AddRect(p0, p1, IM_COL32(70, 74, 82, 255), 3.0f, 0, 1.0f);
    ImGui::InvisibleButton("##NebMeshMiniPreview", drawSize);

    // Interactive mini-preview controls: right-drag rotate, wheel zoom.
    if (ImGui::IsItemHovered())
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f)
        {
            gNebMeshInspectorState.previewZoom *= (io.MouseWheel > 0.0f) ? 1.1f : 0.9f;
            gNebMeshInspectorState.previewZoom = std::max(0.2f, std::min(gNebMeshInspectorState.previewZoom, 5.0f));
        }
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            gNebMeshInspectorState.previewYaw += io.MouseDelta.x * 0.01f;
            gNebMeshInspectorState.previewPitch += io.MouseDelta.y * 0.01f;
            gNebMeshInspectorState.previewPitch = std::max(-1.3f, std::min(gNebMeshInspectorState.previewPitch, 1.3f));
        }
    }

    const std::vector<Vec3>& src = (posedVertices && posedVertices->size() == mesh.positions.size()) ? *posedVertices : mesh.positions;
    if (src.empty() || mesh.indices.size() < 3)
        return;

    const float yaw = gNebMeshInspectorState.previewYaw;
    const float pitch = gNebMeshInspectorState.previewPitch;
    const float cy = cosf(yaw), sy = sinf(yaw);
    const float cp = cosf(pitch), sp = sinf(pitch);
    std::vector<Vec3> tverts(src.size());
    Vec3 center = { 0.0f, 0.0f, 0.0f };
    for (size_t i = 0; i < src.size(); ++i)
    {
        const Vec3 v = src[i];
        const float x1 = cy * v.x + sy * v.z;
        const float z1 = -sy * v.x + cy * v.z;
        const float y2 = cp * v.y - sp * z1;
        const float z2 = sp * v.y + cp * z1;
        tverts[i] = { x1, y2, z2 };
        center.x += x1;
        center.y += y2;
        center.z += z2;
    }
    center.x /= (float)tverts.size();
    center.y /= (float)tverts.size();
    center.z /= (float)tverts.size();

    float radius = 0.001f;
    for (const Vec3& v : tverts)
    {
        const float dx = v.x - center.x;
        const float dy = v.y - center.y;
        const float dz = v.z - center.z;
        radius = std::max(radius, sqrtf(dx * dx + dy * dy + dz * dz));
    }

    const float dist = (radius * 3.0f + 0.001f) / std::max(0.2f, gNebMeshInspectorState.previewZoom);
    const float scale = std::min(drawSize.x, drawSize.y) * 0.82f * std::max(0.2f, gNebMeshInspectorState.previewZoom);
    const float cx = (p0.x + p1.x) * 0.5f;
    const float cy2 = (p0.y + p1.y) * 0.5f;

    std::vector<ImVec2> projected(tverts.size(), ImVec2(cx, cy2));
    for (size_t i = 0; i < tverts.size(); ++i)
    {
        Vec3 q = tverts[i];
        q.x -= center.x;
        q.y -= center.y;
        q.z -= center.z;
        const float denom = q.z + dist;
        if (fabsf(denom) < 1e-5f)
            continue;
        projected[i].x = cx + (q.x / denom) * scale;
        projected[i].y = cy2 - (q.y / denom) * scale;
    }

    bool hasHelperProjected = false;
    ImVec2 helperProj = ImVec2(cx, cy2);
    if (helperPoint)
    {
        const Vec3 v = *helperPoint;
        const float x1 = cy * v.x + sy * v.z;
        const float z1 = -sy * v.x + cy * v.z;
        const float y2 = cp * v.y - sp * z1;
        const float z2 = sp * v.y + cp * z1;
        Vec3 q = { x1 - center.x, y2 - center.y, z2 - center.z };
        const float denom = q.z + dist;
        if (fabsf(denom) >= 1e-5f)
        {
            helperProj.x = cx + (q.x / denom) * scale;
            helperProj.y = cy2 - (q.y / denom) * scale;
            hasHelperProjected = true;
        }
    }

    const size_t triCount = mesh.indices.size() / 3;
    const size_t maxTri = std::min<size_t>(triCount, 3000);
    for (size_t t = 0; t < maxTri; ++t)
    {
        const uint16_t i0 = mesh.indices[t * 3 + 0];
        const uint16_t i1 = mesh.indices[t * 3 + 1];
        const uint16_t i2 = mesh.indices[t * 3 + 2];
        if (i0 >= projected.size() || i1 >= projected.size() || i2 >= projected.size())
            continue;
        dl->AddLine(projected[i0], projected[i1], IM_COL32(172, 206, 255, 255), 1.0f);
        dl->AddLine(projected[i1], projected[i2], IM_COL32(172, 206, 255, 255), 1.0f);
        dl->AddLine(projected[i2], projected[i0], IM_COL32(172, 206, 255, 255), 1.0f);
    }

    if (hasHelperProjected)
    {
        dl->AddCircleFilled(helperProj, 4.0f, IM_COL32(255, 232, 48, 255));
        dl->AddCircle(helperProj, 6.0f, IM_COL32(255, 196, 0, 255), 16, 1.0f);
    }
}
