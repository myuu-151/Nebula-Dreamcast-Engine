#include "SceneIO.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

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
            for (auto& ms : n.materialSlots)
                RewritePathRefForRename(ms, oldRel, newRel, isDir);
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
                for (auto& ms : n.materialSlots)
                    RewritePathRefForRename(ms, oldRel, newRel, isDir);
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
    }

    std::string EncodeSceneToken(const std::string& s)
    {
        return s.empty() ? "-" : s;
    }

    void DecodeSceneToken(std::string& s)
    {
        if (s == "-") s.clear();
    }

    std::string BuildSceneText(const std::filesystem::path& path, const std::vector<Audio3DNode>& nodes, const std::vector<StaticMesh3DNode>& statics, const std::vector<Camera3DNode>& cameras, const std::vector<Node3DNode>& node3d)
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
            out << "\n";
        }
        for (const auto& c : cameras)
        {
            out << "Camera3D " << c.name << " "
                << c.x << " " << c.y << " " << c.z << " "
                << c.rotX << " " << c.rotY << " " << c.rotZ << " "
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
                << (n.physicsEnabled ? 1 : 0) << "\n";
        }
        return out.str();
    }

    void SaveSceneToPath(const std::filesystem::path& path, const std::vector<Audio3DNode>& nodes)
    {
        std::ofstream out(path, std::ios::out | std::ios::trunc);
        if (!out.is_open()) return;
        out << BuildSceneText(path, nodes, std::vector<StaticMesh3DNode>{}, std::vector<Camera3DNode>{}, std::vector<Node3DNode>{});
    }

    void SaveSceneToPath(const std::filesystem::path& path, const std::vector<Audio3DNode>& nodes, const std::vector<StaticMesh3DNode>& statics, const std::vector<Camera3DNode>& cameras, const std::vector<Node3DNode>& node3d)
    {
        std::ofstream out(path, std::ios::out | std::ios::trunc);
        if (!out.is_open()) return;
        out << BuildSceneText(path, nodes, statics, cameras, node3d);
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

                if (toks.size() >= 14)
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
                outScene.node3d.push_back(n);
            }
        }
        return true;
    }
}
