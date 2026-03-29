#include "scene_io.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace
{
    static void SyncNode3DQuatFromEuler(Node3DNode& n)
    {
        float rx = n.rotX * 3.14159265f / 180.0f * 0.5f;
        float ry = n.rotY * 3.14159265f / 180.0f * 0.5f;
        float rz = n.rotZ * 3.14159265f / 180.0f * 0.5f;
        float cx = cosf(rx), sx = sinf(rx);
        float cy = cosf(ry), sy = sinf(ry);
        float cz = cosf(rz), sz = sinf(rz);
        // R = Rz * Ry * Rx → q = qZ * qY * qX
        n.qw = cz*cy*cx + sz*sy*sx;
        n.qx = cz*cy*sx - sz*sy*cx;
        n.qy = cz*sy*cx + sz*cy*sx;
        n.qz = sz*cy*cx - cz*sy*sx;
    }
}

namespace NebulaScene
{
    std::string NormalizePathRef(std::string s)
    {
        std::replace(s.begin(), s.end(), '\\', '/');
        return s;
    }

    bool RewritePathRefForRename(std::string& ref, const std::string& oldRel, const std::string& newRel, bool isDir)
    {
        if (ref.empty() || oldRel.empty() || newRel.empty()) return false;
        std::string cur = NormalizePathRef(ref);
        std::string oldN = NormalizePathRef(oldRel);
        std::string newN = NormalizePathRef(newRel);

        if (isDir)
        {
            if (cur == oldN)
            {
                ref = newN;
                return true;
            }
            std::string oldPrefix = oldN;
            if (!oldPrefix.empty() && oldPrefix.back() != '/') oldPrefix.push_back('/');
            if (cur.rfind(oldPrefix, 0) == 0)
            {
                ref = newN + cur.substr(oldPrefix.size() - 1);
                return true;
            }
            return false;
        }

        if (cur == oldN)
        {
            ref = newN;
            return true;
        }
        return false;
    }

    static bool RewriteRefTextFileInPlace(const std::filesystem::path& filePath, const std::string& oldRel, const std::string& newRel, bool isDir)
    {
        std::ifstream in(filePath);
        if (!in.is_open()) return false;

        std::vector<std::string> lines;
        std::string line;
        bool changed = false;
        while (std::getline(in, line))
        {
            std::string out = line;
            size_t eq = line.find('=');
            if (eq != std::string::npos)
            {
                std::string key = line.substr(0, eq);
                std::string val = line.substr(eq + 1);
                std::string newVal = val;
                if (RewritePathRefForRename(newVal, oldRel, newRel, isDir))
                {
                    out = key + "=" + newVal;
                    changed = true;
                }
            }
            lines.push_back(out);
        }
        in.close();

        if (!changed) return false;

        std::ofstream out(filePath, std::ios::out | std::ios::trunc);
        if (!out.is_open()) return false;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            out << lines[i];
            if (i + 1 < lines.size()) out << "\n";
        }
        return true;
    }

    void UpdateAssetReferencesForRename(
        const std::filesystem::path& oldPath,
        const std::filesystem::path& newPath,
        const std::string& projectDir,
        std::vector<Audio3DNode>& audioNodes,
        std::vector<StaticMesh3DNode>& staticMeshNodes,
        std::vector<Node3DNode>& node3DNodes,
        std::vector<SceneData>& openScenes,
        std::filesystem::path& selectedAssetPath)
    {
        if (projectDir.empty()) return;
        std::error_code ec;
        auto proj = std::filesystem::path(projectDir);
        auto oldRelP = std::filesystem::relative(oldPath, proj, ec);
        if (ec) return;
        ec.clear();
        auto newRelP = std::filesystem::relative(newPath, proj, ec);
        if (ec) return;

        std::string oldRel = oldRelP.generic_string();
        std::string newRel = newRelP.generic_string();
        bool isDir = std::filesystem::is_directory(newPath, ec) && !ec;

        for (auto& n : staticMeshNodes)
        {
            RewritePathRefForRename(n.script, oldRel, newRel, isDir);
            RewritePathRefForRename(n.material, oldRel, newRel, isDir);
            RewritePathRefForRename(n.mesh, oldRel, newRel, isDir);
            RewritePathRefForRename(n.vtxAnim, oldRel, newRel, isDir);
            for (auto& ms : n.materialSlots)
                RewritePathRefForRename(ms, oldRel, newRel, isDir);
            for (int si = 0; si < n.animSlotCount; ++si)
                RewritePathRefForRename(n.animSlots[si].path, oldRel, newRel, isDir);
        }
        for (auto& n : audioNodes)
            RewritePathRefForRename(n.script, oldRel, newRel, isDir);
        for (auto& n : node3DNodes)
        {
            RewritePathRefForRename(n.script, oldRel, newRel, isDir);
            RewritePathRefForRename(n.primitiveMesh, oldRel, newRel, isDir);
        }

        for (auto& sc : openScenes)
        {
            for (auto& n : sc.nodes)
                RewritePathRefForRename(n.script, oldRel, newRel, isDir);
            for (auto& n : sc.staticMeshes)
            {
                RewritePathRefForRename(n.script, oldRel, newRel, isDir);
                RewritePathRefForRename(n.material, oldRel, newRel, isDir);
                RewritePathRefForRename(n.mesh, oldRel, newRel, isDir);
                RewritePathRefForRename(n.vtxAnim, oldRel, newRel, isDir);
                for (auto& ms : n.materialSlots)
                    RewritePathRefForRename(ms, oldRel, newRel, isDir);
                for (int si = 0; si < n.animSlotCount; ++si)
                    RewritePathRefForRename(n.animSlots[si].path, oldRel, newRel, isDir);
            }
            for (auto& n : sc.node3d)
            {
                RewritePathRefForRename(n.script, oldRel, newRel, isDir);
                RewritePathRefForRename(n.primitiveMesh, oldRel, newRel, isDir);
            }
        }

        if (!selectedAssetPath.empty())
        {
            std::filesystem::path sel = selectedAssetPath;
            auto selS = sel.generic_string();
            auto oldS = oldPath.generic_string();
            if (selS == oldS || selS.rfind(oldS + "/", 0) == 0)
            {
                std::string tail = selS.substr(oldS.size());
                selectedAssetPath = std::filesystem::path(newPath.generic_string() + tail);
            }
        }

        // Persist rename rewrite across on-disk asset metadata/scenes in the project Assets tree.
        std::error_code fec;
        std::filesystem::path assetsRoot = std::filesystem::path(projectDir) / "Assets";
        if (std::filesystem::exists(assetsRoot, fec) && !fec)
        {
            for (auto it = std::filesystem::recursive_directory_iterator(assetsRoot, fec);
                 !fec && it != std::filesystem::recursive_directory_iterator();
                 it.increment(fec))
            {
                if (fec) break;
                if (!it->is_regular_file()) continue;
                const auto& fp = it->path();
                auto ext = fp.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });

                if (ext == ".nebscene")
                {
                    SceneData diskScene;
                    if (!LoadSceneFromPath(fp, diskScene)) continue;
                    bool diskChanged = false;
                    for (auto& n : diskScene.nodes)
                        diskChanged |= RewritePathRefForRename(n.script, oldRel, newRel, isDir);
                    for (auto& n : diskScene.staticMeshes)
                    {
                        diskChanged |= RewritePathRefForRename(n.script, oldRel, newRel, isDir);
                        diskChanged |= RewritePathRefForRename(n.material, oldRel, newRel, isDir);
                        diskChanged |= RewritePathRefForRename(n.mesh, oldRel, newRel, isDir);
                        diskChanged |= RewritePathRefForRename(n.vtxAnim, oldRel, newRel, isDir);
                        for (auto& ms : n.materialSlots)
                            diskChanged |= RewritePathRefForRename(ms, oldRel, newRel, isDir);
                        for (int si = 0; si < n.animSlotCount; ++si)
                            diskChanged |= RewritePathRefForRename(n.animSlots[si].path, oldRel, newRel, isDir);
                    }
                    for (auto& n : diskScene.node3d)
                    {
                        diskChanged |= RewritePathRefForRename(n.script, oldRel, newRel, isDir);
                        diskChanged |= RewritePathRefForRename(n.primitiveMesh, oldRel, newRel, isDir);
                    }
                    if (diskChanged)
                        SaveSceneToPath(fp, diskScene.nodes, diskScene.staticMeshes, diskScene.cameras, diskScene.node3d);
                }
                else if (ext == ".nebmat" || ext == ".nebslots")
                {
                    RewriteRefTextFileInPlace(fp, oldRel, newRel, isDir);
                }
            }
        }
    }

    std::string EncodeSceneToken(const std::string& s)
    {
        return s.empty() ? "-" : s;
    }

    void DecodeSceneToken(std::string& s)
    {
        if (s == "-") s.clear();
    }

    std::string BuildSceneText(const std::filesystem::path& path, const std::vector<Audio3DNode>& nodes, const std::vector<StaticMesh3DNode>& statics, const std::vector<Camera3DNode>& cameras, const std::vector<Node3DNode>& node3d, const std::vector<NavMesh3DNode>& navMeshes)
    {
        std::ostringstream out;
        out << "scene=" << path.stem().string() << "\n";
        for (const auto& n : nodes)
        {
            out << "Audio3D " << n.name << " " << EncodeSceneToken(n.script) << " "
                << n.x << " " << n.y << " " << n.z << " "
                << n.innerRadius << " " << n.outerRadius << " "
                << n.baseVolume << " "
                << n.rotX << " " << n.rotY << " " << n.rotZ << " "
                << n.scaleX << " " << n.scaleY << " " << n.scaleZ << " "
                << EncodeSceneToken(n.parent) << "\n";
        }
        for (const auto& n : statics)
        {
            out << "StaticMesh " << n.name << " " << EncodeSceneToken(n.script) << " " << EncodeSceneToken(n.material) << " " << EncodeSceneToken(n.mesh) << " "
                << n.x << " " << n.y << " " << n.z << " "
                << n.rotX << " " << n.rotY << " " << n.rotZ << " "
                << n.scaleX << " " << n.scaleY << " " << n.scaleZ;
            out << " " << n.materialSlot;
            for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                out << " " << EncodeSceneToken(n.materialSlots[si]);
            out << " " << EncodeSceneToken(n.parent);
            out << " " << (n.collisionSource ? 1 : 0);
            out << " " << EncodeSceneToken(n.vtxAnim);
            out << " " << (n.runtimeTest ? 1 : 0);
            out << " " << (n.navmeshReady ? 1 : 0);
            out << " " << n.wallThreshold;
            out << " " << n.animSlotCount;
            for (int si = 0; si < kStaticMeshAnimSlots; ++si)
                out << " " << EncodeSceneToken(n.animSlots[si].name) << " " << EncodeSceneToken(n.animSlots[si].path) << " " << n.animSlots[si].speed << " " << (n.animSlots[si].loop ? 1 : 0);
            out << " " << (n.animPreload ? 1 : 0);
            out << " " << (n.collisionWalls ? 1 : 0);
            out << "\n";
        }
        for (const auto& c : cameras)
        {
            out << "Camera3D " << c.name << " "
                << c.x << " " << c.y << " " << c.z << " "
                << c.rotX << " " << c.rotY << " " << c.rotZ << " "
                << c.orbitX << " " << c.orbitY << " " << c.orbitZ << " "
                << (c.perspective ? 1 : 0) << " "
                << c.fovY << " " << c.nearZ << " " << c.farZ << " "
                << c.orthoWidth << " " << c.priority << " "
                << (c.main ? 1 : 0) << " "
                << EncodeSceneToken(c.parent) << "\n";
        }
        for (const auto& n : node3d)
        {
            out << "Node3D " << n.name << " "
                << n.x << " " << n.y << " " << n.z << " "
                << n.rotX << " " << n.rotY << " " << n.rotZ << " "
                << n.scaleX << " " << n.scaleY << " " << n.scaleZ << " "
                << EncodeSceneToken(n.parent) << " "
                << EncodeSceneToken(n.primitiveMesh) << " "
                << EncodeSceneToken(n.script) << " "
                << (n.collisionSource ? 1 : 0) << " "
                << (n.physicsEnabled ? 1 : 0) << " "
                << n.extentX << " " << n.extentY << " " << n.extentZ << " "
                << n.boundPosX << " " << n.boundPosY << " " << n.boundPosZ << " "
                << (n.simpleCollision ? 1 : 0) << "\n";
        }
        for (const auto& n : navMeshes)
        {
            out << "NavMesh3D " << n.name << " "
                << n.x << " " << n.y << " " << n.z << " "
                << n.rotX << " " << n.rotY << " " << n.rotZ << " "
                << n.scaleX << " " << n.scaleY << " " << n.scaleZ << " "
                << n.extentX << " " << n.extentY << " " << n.extentZ << " "
                << (n.navBounds ? 1 : 0) << " "
                << (n.navNegator ? 1 : 0) << " "
                << (n.cullWalls ? 1 : 0) << " "
                << n.wallCullThreshold << " "
                << EncodeSceneToken(n.parent) << " "
                << n.wireR << " " << n.wireG << " " << n.wireB << " "
                << n.wireThickness << "\n";
        }
        return out.str();
    }

    void SaveSceneToPath(const std::filesystem::path& path, const std::vector<Audio3DNode>& nodes)
    {
        std::ofstream out(path, std::ios::out | std::ios::trunc);
        if (!out.is_open()) return;
        out << BuildSceneText(path, nodes, std::vector<StaticMesh3DNode>{}, std::vector<Camera3DNode>{}, std::vector<Node3DNode>{});
    }

    void SaveSceneToPath(const std::filesystem::path& path, const std::vector<Audio3DNode>& nodes, const std::vector<StaticMesh3DNode>& statics, const std::vector<Camera3DNode>& cameras, const std::vector<Node3DNode>& node3d, const std::vector<NavMesh3DNode>& navMeshes)
    {
        std::ofstream out(path, std::ios::out | std::ios::trunc);
        if (!out.is_open()) return;
        out << BuildSceneText(path, nodes, statics, cameras, node3d, navMeshes);
    }

    bool LoadSceneFromPath(const std::filesystem::path& path, SceneData& outScene)
    {
        std::ifstream in(path);
        if (!in.is_open()) return false;

        outScene.path = path;
        outScene.name = path.stem().string();
        outScene.nodes.clear();
        outScene.staticMeshes.clear();
        outScene.cameras.clear();
        outScene.node3d.clear();
        outScene.navMeshes.clear();

        std::string line;
        while (std::getline(in, line))
        {
            if (line.rfind("scene=", 0) == 0)
            {
                outScene.name = line.substr(6);
                continue;
            }
            std::istringstream ss(line);
            std::string type;
            ss >> type;
            if (type == "Audio3D")
            {
                Audio3DNode n;
                ss >> n.name >> n.script >> n.x >> n.y >> n.z >> n.innerRadius >> n.outerRadius >> n.baseVolume >> n.rotX >> n.rotY >> n.rotZ >> n.scaleX >> n.scaleY >> n.scaleZ;
                ss >> n.parent;
                DecodeSceneToken(n.script);
                DecodeSceneToken(n.parent);
                outScene.nodes.push_back(n);
            }
            else if (type == "StaticMesh")
            {
                StaticMesh3DNode n;
                ss >> n.name >> n.script >> n.material >> n.mesh >> n.x >> n.y >> n.z >> n.rotX >> n.rotY >> n.rotZ >> n.scaleX >> n.scaleY >> n.scaleZ;
                DecodeSceneToken(n.script);
                DecodeSceneToken(n.material);
                DecodeSceneToken(n.mesh);
                std::vector<std::string> extra;
                std::string tok;
                while (ss >> tok) extra.push_back(tok);

                size_t startSlot = 0;
                if (extra.size() >= (size_t)kStaticMeshMaterialSlots + 1)
                {
                    n.materialSlot = atoi(extra[0].c_str());
                    startSlot = 1;
                }

                for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                {
                    size_t ei = startSlot + (size_t)si;
                    if (ei >= extra.size()) break;
                    std::string v = extra[ei];
                    DecodeSceneToken(v);
                    n.materialSlots[si] = v;
                }
                size_t parentIdx = startSlot + (size_t)kStaticMeshMaterialSlots;
                if (parentIdx < extra.size())
                {
                    n.parent = extra[parentIdx];
                    DecodeSceneToken(n.parent);
                }
                size_t collisionIdx = parentIdx + 1;
                if (collisionIdx < extra.size())
                    n.collisionSource = (atoi(extra[collisionIdx].c_str()) != 0);
                size_t animIdx = collisionIdx + 1;
                if (animIdx < extra.size())
                {
                    n.vtxAnim = extra[animIdx];
                    DecodeSceneToken(n.vtxAnim);
                }
                size_t runtimeTestIdx = animIdx + 1;
                if (runtimeTestIdx < extra.size())
                    n.runtimeTest = (atoi(extra[runtimeTestIdx].c_str()) != 0);
                size_t navmeshReadyIdx = runtimeTestIdx + 1;
                if (navmeshReadyIdx < extra.size())
                    n.navmeshReady = (atoi(extra[navmeshReadyIdx].c_str()) != 0);
                size_t wallThreshIdx = navmeshReadyIdx + 1;
                if (wallThreshIdx < extra.size())
                    n.wallThreshold = (float)atof(extra[wallThreshIdx].c_str());
                size_t animSlotCountIdx = wallThreshIdx + 1;
                if (animSlotCountIdx < extra.size())
                {
                    n.animSlotCount = atoi(extra[animSlotCountIdx].c_str());
                    if (n.animSlotCount < 0) n.animSlotCount = 0;
                    if (n.animSlotCount > kStaticMeshAnimSlots) n.animSlotCount = kStaticMeshAnimSlots;
                    // Detect tokens-per-slot: 4 if loop token present, else 3 (backward compat)
                    int tokensPerSlot = 3;
                    if (n.animSlotCount > 0)
                    {
                        // Check if 4th token of first slot looks like a loop flag (0 or 1)
                        size_t loopCheck = animSlotCountIdx + 1 + 3;
                        if (loopCheck < extra.size())
                        {
                            const std::string& lv = extra[loopCheck];
                            if (lv == "0" || lv == "1") tokensPerSlot = 4;
                        }
                    }
                    for (int si = 0; si < kStaticMeshAnimSlots; ++si)
                    {
                        size_t nameIdx = animSlotCountIdx + 1 + (size_t)si * tokensPerSlot;
                        size_t pathIdx = nameIdx + 1;
                        size_t speedIdx = nameIdx + 2;
                        size_t loopIdx = nameIdx + 3;
                        if (nameIdx < extra.size())
                        {
                            n.animSlots[si].name = extra[nameIdx];
                            DecodeSceneToken(n.animSlots[si].name);
                        }
                        if (pathIdx < extra.size())
                        {
                            n.animSlots[si].path = extra[pathIdx];
                            DecodeSceneToken(n.animSlots[si].path);
                        }
                        if (speedIdx < extra.size())
                            n.animSlots[si].speed = (float)atof(extra[speedIdx].c_str());
                        if (tokensPerSlot >= 4 && loopIdx < extra.size())
                            n.animSlots[si].loop = (atoi(extra[loopIdx].c_str()) != 0);
                    }
                    size_t animPreloadIdx = animSlotCountIdx + 1 + (size_t)kStaticMeshAnimSlots * tokensPerSlot;
                    if (animPreloadIdx < extra.size())
                        n.animPreload = (atoi(extra[animPreloadIdx].c_str()) != 0);
                    size_t collisionWallsIdx = animPreloadIdx + 1;
                    if (collisionWallsIdx < extra.size())
                        n.collisionWalls = (atoi(extra[collisionWallsIdx].c_str()) != 0);
                }
                if (n.materialSlot < 0 || n.materialSlot >= kStaticMeshMaterialSlots) n.materialSlot = 0;
                if (n.materialSlots[0].empty()) n.materialSlots[0] = n.material;
                outScene.staticMeshes.push_back(n);
            }
            else if (type == "Camera3D")
            {
                Camera3DNode c;
                std::vector<std::string> toks;
                std::string tok;
                while (ss >> tok) toks.push_back(tok);
                if (toks.size() < 11) continue;

                c.name = toks[0];
                auto F = [&](size_t i) -> float { return (i < toks.size()) ? (float)atof(toks[i].c_str()) : 0.0f; };

                c.x = F(1);
                c.y = F(2);
                c.z = F(3);
                c.rotX = F(4);
                c.rotY = F(5);
                c.rotZ = F(6);

                // New format includes orbit offsets at [7..9].
                if (toks.size() >= 18)
                {
                    c.orbitX = F(7);
                    c.orbitY = F(8);
                    c.orbitZ = F(9);
                    c.perspective = (F(10) != 0.0f);
                    c.fovY = F(11);
                    c.nearZ = F(12);
                    c.farZ = F(13);
                    c.orthoWidth = F(14);
                    c.priority = F(15);
                    c.main = (F(16) != 0.0f);
                    c.parent = toks[17];
                    DecodeSceneToken(c.parent);
                }
                else if (toks.size() >= 14)
                {
                    c.perspective = (F(7) != 0.0f);
                    c.fovY = F(8);
                    c.nearZ = F(9);
                    c.farZ = F(10);
                    c.orthoWidth = F(11);
                    c.priority = F(12);
                    c.main = (F(13) != 0.0f);
                    if (toks.size() >= 15)
                    {
                        c.parent = toks[14];
                        DecodeSceneToken(c.parent);
                    }
                }
                else
                {
                    c.perspective = true;
                    c.fovY = F(7);
                    c.nearZ = F(8);
                    c.farZ = F(9);
                    c.main = (F(10) != 0.0f);
                }

                outScene.cameras.push_back(c);
            }
            else if (type == "Node3D")
            {
                Node3DNode n;
                std::vector<std::string> toks;
                std::string tok;
                while (ss >> tok) toks.push_back(tok);
                if (toks.size() < 10) continue;
                auto F = [&](size_t i) -> float { return (i < toks.size()) ? (float)atof(toks[i].c_str()) : 0.0f; };

                n.name = toks[0];
                n.x = F(1);
                n.y = F(2);
                n.z = F(3);
                n.rotX = F(4);
                n.rotY = F(5);
                n.rotZ = F(6);
                n.scaleX = F(7);
                n.scaleY = F(8);
                n.scaleZ = F(9);
                if (toks.size() >= 11)
                {
                    n.parent = toks[10];
                    DecodeSceneToken(n.parent);
                }
                if (toks.size() >= 12)
                {
                    n.primitiveMesh = toks[11];
                    DecodeSceneToken(n.primitiveMesh);
                }
                if (toks.size() >= 13)
                {
                    n.script = toks[12];
                    DecodeSceneToken(n.script);
                }
                if (toks.size() >= 14)
                    n.collisionSource = (atoi(toks[13].c_str()) != 0);
                if (toks.size() >= 15)
                    n.physicsEnabled = (atoi(toks[14].c_str()) != 0);
                if (toks.size() >= 18)
                {
                    n.extentX = (float)atof(toks[15].c_str());
                    n.extentY = (float)atof(toks[16].c_str());
                    n.extentZ = (float)atof(toks[17].c_str());
                }
                if (toks.size() >= 21)
                {
                    n.boundPosX = (float)atof(toks[18].c_str());
                    n.boundPosY = (float)atof(toks[19].c_str());
                    n.boundPosZ = (float)atof(toks[20].c_str());
                }
                if (toks.size() >= 22)
                    n.simpleCollision = (atoi(toks[21].c_str()) != 0);
                SyncNode3DQuatFromEuler(n);
                outScene.node3d.push_back(n);
            }
            else if (type == "NavMesh3D")
            {
                NavMesh3DNode n;
                std::vector<std::string> toks;
                std::string tok;
                while (ss >> tok) toks.push_back(tok);
                if (toks.size() < 12) continue;
                auto F = [&](size_t i) -> float { return (i < toks.size()) ? (float)atof(toks[i].c_str()) : 0.0f; };

                n.name = toks[0];
                n.x = F(1); n.y = F(2); n.z = F(3);
                n.rotX = F(4); n.rotY = F(5); n.rotZ = F(6);
                n.scaleX = F(7); n.scaleY = F(8); n.scaleZ = F(9);
                n.extentX = F(10); n.extentY = F(11); n.extentZ = F(12);
                if (toks.size() >= 14) n.navBounds = (atoi(toks[13].c_str()) != 0);
                if (toks.size() >= 15) n.navNegator = (atoi(toks[14].c_str()) != 0);
                if (toks.size() >= 16) n.cullWalls = (atoi(toks[15].c_str()) != 0);
                if (toks.size() >= 17) n.wallCullThreshold = F(16);
                if (toks.size() >= 18)
                {
                    n.parent = toks[17];
                    DecodeSceneToken(n.parent);
                }
                if (toks.size() >= 19) n.wireR = F(18);
                if (toks.size() >= 20) n.wireG = F(19);
                if (toks.size() >= 21) n.wireB = F(20);
                if (toks.size() >= 22) n.wireThickness = F(21);
                outScene.navMeshes.push_back(n);
            }
        }
        return true;
    }
}
