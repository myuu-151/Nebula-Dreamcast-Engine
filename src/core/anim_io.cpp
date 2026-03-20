#include "anim_io.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "mesh_io.h"
#include "editor/project.h"

#include <cmath>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <unordered_set>
#include <set>
#include <cfloat>

// Globals defined in main.cpp — accessed by RebuildNebMeshEmbeddedMapping
extern std::string gViewportToast;
extern double gViewportToastUntil;

// glfwGetTime() — declared here to avoid pulling in GLFW/Windows headers
extern "C" double glfwGetTime(void);

// ---------------------------------------------------------------------------
// Assimp animation helpers
// ---------------------------------------------------------------------------

aiMatrix4x4 AiComposeTRS(const aiVector3D& t, const aiQuaternion& r, const aiVector3D& s)
{
    aiMatrix4x4 m = aiMatrix4x4(r.GetMatrix());
    m.a1 *= s.x; m.a2 *= s.x; m.a3 *= s.x;
    m.b1 *= s.y; m.b2 *= s.y; m.b3 *= s.y;
    m.c1 *= s.z; m.c2 *= s.z; m.c3 *= s.z;
    m.a4 = t.x;
    m.b4 = t.y;
    m.c4 = t.z;
    return m;
}

aiVector3D AiInterpVec(const aiVector3D& a, const aiVector3D& b, double t)
{
    return a + (b - a) * (float)t;
}

aiQuaternion AiInterpQuat(const aiQuaternion& a, const aiQuaternion& b, double t)
{
    aiQuaternion out;
    aiQuaternion::Interpolate(out, a, b, (float)t);
    out.Normalize();
    return out;
}

std::string NormalizeAnimName(const std::string& in)
{
    std::string s;
    s.reserve(in.size());
    for (char c : in)
    {
        char lc = (char)std::tolower((unsigned char)c);
        if (lc == '\\') lc = '/';
        s.push_back(lc);
    }

    // Keep only leaf token after common namespace/path separators.
    size_t cut = std::string::npos;
    const char* seps = ":|/";
    for (const char* p = seps; *p; ++p)
    {
        size_t f = s.rfind(*p);
        if (f != std::string::npos) cut = (cut == std::string::npos) ? f : std::max(cut, f);
    }
    std::string leaf = (cut == std::string::npos) ? s : s.substr(cut + 1);
    while (!leaf.empty() && (leaf.front() == ' ' || leaf.front() == '\t')) leaf.erase(leaf.begin());
    while (!leaf.empty() && (leaf.back() == ' ' || leaf.back() == '\t')) leaf.pop_back();
    return leaf.empty() ? s : leaf;
}

void CollectSceneNodes(const aiNode* node, std::vector<const aiNode*>& out)
{
    if (!node) return;
    out.push_back(node);
    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        CollectSceneNodes(node->mChildren[i], out);
}

const aiNode* ResolveSceneNodeByNameRobust(const aiScene* scene, const aiString& rawName)
{
    if (!scene || !scene->mRootNode) return nullptr;
    const aiNode* exact = scene->mRootNode->FindNode(rawName);
    if (exact) return exact;

    const std::string targetNorm = NormalizeAnimName(rawName.C_Str());
    if (targetNorm.empty()) return nullptr;

    std::vector<const aiNode*> nodes;
    CollectSceneNodes(scene->mRootNode, nodes);

    for (const aiNode* n : nodes)
    {
        if (!n) continue;
        if (NormalizeAnimName(n->mName.C_Str()) == targetNorm)
            return n;
    }

    // Fallback suffix match on normalized leaf token.
    for (const aiNode* n : nodes)
    {
        if (!n) continue;
        std::string nodeNorm = NormalizeAnimName(n->mName.C_Str());
        if (nodeNorm.size() >= targetNorm.size() &&
            nodeNorm.compare(nodeNorm.size() - targetNorm.size(), targetNorm.size(), targetNorm) == 0)
            return n;
    }
    return nullptr;
}

const aiNodeAnim* AiFindChannel(const aiAnimation* anim, const aiString& name)
{
    if (!anim) return nullptr;
    for (unsigned int i = 0; i < anim->mNumChannels; ++i)
    {
        if (anim->mChannels[i]->mNodeName == name)
            return anim->mChannels[i];
    }

    const std::string targetNorm = NormalizeAnimName(name.C_Str());
    if (targetNorm.empty()) return nullptr;
    for (unsigned int i = 0; i < anim->mNumChannels; ++i)
    {
        const aiNodeAnim* ch = anim->mChannels[i];
        if (!ch) continue;
        std::string chNorm = NormalizeAnimName(ch->mNodeName.C_Str());
        if (chNorm == targetNorm) return ch;
    }
    for (unsigned int i = 0; i < anim->mNumChannels; ++i)
    {
        const aiNodeAnim* ch = anim->mChannels[i];
        if (!ch) continue;
        std::string chNorm = NormalizeAnimName(ch->mNodeName.C_Str());
        if (chNorm.size() >= targetNorm.size() &&
            chNorm.compare(chNorm.size() - targetNorm.size(), targetNorm.size(), targetNorm) == 0)
            return ch;
    }
    return nullptr;
}

aiVector3D AiSamplePosition(const aiNodeAnim* channel, double time)
{
    if (!channel || channel->mNumPositionKeys == 0) return aiVector3D(0, 0, 0);
    if (channel->mNumPositionKeys == 1) return channel->mPositionKeys[0].mValue;
    for (unsigned int i = 0; i + 1 < channel->mNumPositionKeys; ++i)
    {
        const auto& a = channel->mPositionKeys[i];
        const auto& b = channel->mPositionKeys[i + 1];
        if (time <= b.mTime)
        {
            double dt = (b.mTime - a.mTime);
            double t = (dt > 0.0) ? (time - a.mTime) / dt : 0.0;
            return AiInterpVec(a.mValue, b.mValue, t);
        }
    }
    return channel->mPositionKeys[channel->mNumPositionKeys - 1].mValue;
}

aiVector3D AiSampleScale(const aiNodeAnim* channel, double time)
{
    if (!channel || channel->mNumScalingKeys == 0) return aiVector3D(1, 1, 1);
    if (channel->mNumScalingKeys == 1) return channel->mScalingKeys[0].mValue;
    for (unsigned int i = 0; i + 1 < channel->mNumScalingKeys; ++i)
    {
        const auto& a = channel->mScalingKeys[i];
        const auto& b = channel->mScalingKeys[i + 1];
        if (time <= b.mTime)
        {
            double dt = (b.mTime - a.mTime);
            double t = (dt > 0.0) ? (time - a.mTime) / dt : 0.0;
            return AiInterpVec(a.mValue, b.mValue, t);
        }
    }
    return channel->mScalingKeys[channel->mNumScalingKeys - 1].mValue;
}

aiQuaternion AiSampleRotation(const aiNodeAnim* channel, double time)
{
    if (!channel || channel->mNumRotationKeys == 0) return aiQuaternion();
    if (channel->mNumRotationKeys == 1) return channel->mRotationKeys[0].mValue;
    for (unsigned int i = 0; i + 1 < channel->mNumRotationKeys; ++i)
    {
        const auto& a = channel->mRotationKeys[i];
        const auto& b = channel->mRotationKeys[i + 1];
        if (time <= b.mTime)
        {
            double dt = (b.mTime - a.mTime);
            double t = (dt > 0.0) ? (time - a.mTime) / dt : 0.0;
            return AiInterpQuat(a.mValue, b.mValue, t);
        }
    }
    return channel->mRotationKeys[channel->mNumRotationKeys - 1].mValue;
}

bool AiTryDecomposeTrs(const aiMatrix4x4& m, aiVector3D& outT, aiQuaternion& outR, aiVector3D& outS)
{
    outT = aiVector3D(0, 0, 0);
    outR = aiQuaternion();
    outS = aiVector3D(1, 1, 1);
    aiVector3D s;
    aiVector3D t;
    aiQuaternion r;
    m.Decompose(s, r, t);
    auto finite3 = [](const aiVector3D& v) -> bool {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    auto finite4 = [](const aiQuaternion& q) -> bool {
        return std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z);
    };
    if (!finite3(s) || !finite3(t) || !finite4(r))
        return false;
    outT = t;
    outR = r;
    outS = s;
    return true;
}

aiMatrix4x4 AiNodeLocalAtTimeLegacy(const aiNode* node, const aiAnimation* anim, double time)
{
    if (!node) return aiMatrix4x4();
    const aiNodeAnim* channel = AiFindChannel(anim, node->mName);
    if (!channel) return node->mTransformation;

    aiVector3D t = AiSamplePosition(channel, time);
    aiVector3D s = AiSampleScale(channel, time);
    aiQuaternion r = AiSampleRotation(channel, time);
    return AiComposeTRS(t, r, s);
}

aiMatrix4x4 AiNodeLocalAtTime(const aiNode* node, const aiAnimation* anim, double time, AiNodeTrsSample* outSample)
{
    if (outSample) *outSample = AiNodeTrsSample{};
    if (!node) return aiMatrix4x4();

    aiVector3D bindT(0, 0, 0);
    aiQuaternion bindR;
    aiVector3D bindS(1, 1, 1);
    const bool haveBindTrs = AiTryDecomposeTrs(node->mTransformation, bindT, bindR, bindS);
    if (outSample)
    {
        outSample->bindTranslation = bindT;
        outSample->bindRotation = bindR;
        outSample->bindScale = bindS;
    }

    const aiNodeAnim* channel = AiFindChannel(anim, node->mName);
    if (!channel)
    {
        if (outSample)
        {
            outSample->translation = bindT;
            outSample->rotation = bindR;
            outSample->scale = bindS;
            outSample->hasChannel = false;
            outSample->usedBindTranslation = haveBindTrs;
            outSample->usedBindRotation = haveBindTrs;
            outSample->usedBindScale = haveBindTrs;
        }
        return node->mTransformation;
    }

    aiVector3D t = (channel->mNumPositionKeys > 0) ? AiSamplePosition(channel, time) : bindT;
    aiQuaternion r = (channel->mNumRotationKeys > 0) ? AiSampleRotation(channel, time) : bindR;
    const bool useAnimatedScale = (channel->mNumScalingKeys > 0);
    aiVector3D s = useAnimatedScale ? AiSampleScale(channel, time) : bindS;
    if (outSample)
    {
        outSample->translation = t;
        outSample->rotation = r;
        outSample->scale = s;
        outSample->hasChannel = true;
        outSample->posKeys = channel->mNumPositionKeys;
        outSample->rotKeys = channel->mNumRotationKeys;
        outSample->sclKeys = channel->mNumScalingKeys;
        outSample->usedBindTranslation = (channel->mNumPositionKeys == 0);
        outSample->usedBindRotation = (channel->mNumRotationKeys == 0);
        outSample->usedBindScale = !useAnimatedScale;
    }
    return AiComposeTRS(t, r, s);
}

bool AiFindNodeGlobal(const aiNode* node, const aiAnimation* anim, double time, const aiMatrix4x4& parent, const aiNode* target, aiMatrix4x4& out)
{
    aiMatrix4x4 local = AiNodeLocalAtTime(node, anim, time);
    aiMatrix4x4 global = parent * local;
    if (node == target)
    {
        out = global;
        return true;
    }
    for (unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        if (AiFindNodeGlobal(node->mChildren[i], anim, time, global, target, out))
            return true;
    }
    return false;
}

bool AiFindNodeGlobalLegacy(const aiNode* node, const aiAnimation* anim, double time, const aiMatrix4x4& parent, const aiNode* target, aiMatrix4x4& out)
{
    aiMatrix4x4 local = AiNodeLocalAtTimeLegacy(node, anim, time);
    aiMatrix4x4 global = parent * local;
    if (node == target)
    {
        out = global;
        return true;
    }
    for (unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        if (AiFindNodeGlobalLegacy(node->mChildren[i], anim, time, global, target, out))
            return true;
    }
    return false;
}

const aiNode* AiFindNodeWithMesh(const aiNode* node, unsigned int meshIndex)
{
    if (!node) return nullptr;
    for (unsigned int i = 0; i < node->mNumMeshes; ++i)
    {
        if (node->mMeshes[i] == meshIndex)
            return node;
    }
    for (unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        const aiNode* found = AiFindNodeWithMesh(node->mChildren[i], meshIndex);
        if (found) return found;
    }
    return nullptr;
}

aiVector3D AiTransformPoint(const aiMatrix4x4& m, const aiVector3D& p)
{
    aiVector3D r;
    r.x = m.a1 * p.x + m.a2 * p.y + m.a3 * p.z + m.a4;
    r.y = m.b1 * p.x + m.b2 * p.y + m.b3 * p.z + m.b4;
    r.z = m.c1 * p.x + m.c2 * p.y + m.c3 * p.z + m.c4;
    return r;
}

float AiMaxAbs3(const aiVector3D& v)
{
    return std::max(std::fabs(v.x), std::max(std::fabs(v.y), std::fabs(v.z)));
}

float AiQuatAngularDeltaDeg(const aiQuaternion& a, const aiQuaternion& b)
{
    const double na = std::sqrt((double)a.w * a.w + (double)a.x * a.x + (double)a.y * a.y + (double)a.z * a.z);
    const double nb = std::sqrt((double)b.w * b.w + (double)b.x * b.x + (double)b.y * b.y + (double)b.z * b.z);
    if (na <= 1e-12 || nb <= 1e-12) return 0.0f;
    double dot = (a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z) / (na * nb);
    dot = std::fabs(std::max(-1.0, std::min(1.0, dot)));
    const double ang = 2.0 * std::acos(dot);
    return (float)(ang * (180.0 / 3.14159265358979323846));
}

// ---------------------------------------------------------------------------
// DumpEmbeddedAnimDiagnostics
// ---------------------------------------------------------------------------

void DumpEmbeddedAnimDiagnostics(const aiScene* scene, const aiAnimation* anim, const std::vector<unsigned int>& meshIndices, int probeFrame, float fps)
{
    if (!scene || !scene->mRootNode || !anim || meshIndices.empty()) return;

    const double tps = anim->mTicksPerSecond != 0.0 ? anim->mTicksPerSecond : 24.0;
    const double time0 = 0.0;
    const double timeProbe = ((double)std::max(0, probeFrame) / std::max(1.0f, fps)) * tps;
    std::unordered_set<const aiNode*> bonesSet;
    for (unsigned int mi : meshIndices)
    {
        if (mi >= scene->mNumMeshes || !scene->mMeshes[mi]) continue;
        const aiMesh* mesh = scene->mMeshes[mi];
        for (unsigned int bi = 0; bi < mesh->mNumBones; ++bi)
        {
            const aiBone* b = mesh->mBones[bi];
            if (!b) continue;
            const aiNode* bn = ResolveSceneNodeByNameRobust(scene, b->mName);
            if (bn) bonesSet.insert(bn);
        }
    }
    if (bonesSet.empty())
    {
        printf("[AnimDiag] no matched bones for selected mesh set\n");
        return;
    }

    std::vector<const aiNode*> bones;
    bones.reserve(bonesSet.size());
    for (const aiNode* n : bonesSet) bones.push_back(n);
    std::sort(bones.begin(), bones.end(), [](const aiNode* a, const aiNode* b) {
        const char* an = (a && a->mName.length > 0) ? a->mName.C_Str() : "";
        const char* bn = (b && b->mName.length > 0) ? b->mName.C_Str() : "";
        return std::strcmp(an, bn) < 0;
    });

    int nonUnitScale = 0;
    int unstableScale = 0;
    int legacyScaleMismatch = 0;
    int legacyRotMismatch = 0;
    int legacyPosMismatch = 0;
    int missingScaleKeysUsingBind = 0;
    const float epsScale = 1e-3f;
    const float epsPos = 1e-4f;
    const float epsRotDeg = 0.05f;

    aiMatrix4x4 identity;
    printf("[AnimDiag] clip=%s probeFrame=%d fps=%.3f tps=%.3f bones=%zu\n",
        (anim->mName.length > 0) ? anim->mName.C_Str() : "<unnamed>",
        std::max(0, probeFrame), fps, (float)tps, bones.size());

    size_t printed = 0;
    for (const aiNode* bn : bones)
    {
        AiNodeTrsSample fixed0;
        AiNodeTrsSample fixedProbe;
        AiNodeLocalAtTime(bn, anim, time0, &fixed0);
        AiNodeLocalAtTime(bn, anim, timeProbe, &fixedProbe);
        aiMatrix4x4 localLegacy0 = AiNodeLocalAtTimeLegacy(bn, anim, time0);
        aiMatrix4x4 localLegacyProbe = AiNodeLocalAtTimeLegacy(bn, anim, timeProbe);
        aiVector3D legT0, legS0, legT1, legS1;
        aiQuaternion legR0, legR1;
        AiTryDecomposeTrs(localLegacy0, legT0, legR0, legS0);
        AiTryDecomposeTrs(localLegacyProbe, legT1, legR1, legS1);

        const float nonUnit = std::max(
            AiMaxAbs3(aiVector3D(fixed0.scale.x - 1.0f, fixed0.scale.y - 1.0f, fixed0.scale.z - 1.0f)),
            AiMaxAbs3(aiVector3D(fixedProbe.scale.x - 1.0f, fixedProbe.scale.y - 1.0f, fixedProbe.scale.z - 1.0f)));
        const float unstable = AiMaxAbs3(aiVector3D(
            fixedProbe.scale.x - fixed0.scale.x,
            fixedProbe.scale.y - fixed0.scale.y,
            fixedProbe.scale.z - fixed0.scale.z));
        const float legacyScaleDelta = std::max(
            AiMaxAbs3(aiVector3D(fixed0.scale.x - legS0.x, fixed0.scale.y - legS0.y, fixed0.scale.z - legS0.z)),
            AiMaxAbs3(aiVector3D(fixedProbe.scale.x - legS1.x, fixedProbe.scale.y - legS1.y, fixedProbe.scale.z - legS1.z)));
        const float legacyPosDelta = std::max(
            AiMaxAbs3(aiVector3D(fixed0.translation.x - legT0.x, fixed0.translation.y - legT0.y, fixed0.translation.z - legT0.z)),
            AiMaxAbs3(aiVector3D(fixedProbe.translation.x - legT1.x, fixedProbe.translation.y - legT1.y, fixedProbe.translation.z - legT1.z)));
        const float legacyRotDeg = std::max(
            AiQuatAngularDeltaDeg(fixed0.rotation, legR0),
            AiQuatAngularDeltaDeg(fixedProbe.rotation, legR1));

        if (nonUnit > epsScale) nonUnitScale++;
        if (unstable > epsScale) unstableScale++;
        if (legacyScaleDelta > epsScale) legacyScaleMismatch++;
        if (legacyPosDelta > epsPos) legacyPosMismatch++;
        if (legacyRotDeg > epsRotDeg) legacyRotMismatch++;
        if (fixed0.hasChannel && fixed0.sclKeys == 0 && fixed0.usedBindScale) missingScaleKeysUsingBind++;

        bool interesting = false;
        interesting = interesting || (legacyScaleDelta > epsScale);
        interesting = interesting || (legacyPosDelta > epsPos);
        interesting = interesting || (legacyRotDeg > epsRotDeg);
        interesting = interesting || (unstable > epsScale);
        interesting = interesting || (fixed0.sclKeys == 0 && fixed0.hasChannel);
        if (interesting && printed < 20)
        {
            aiMatrix4x4 g0Fixed;
            aiMatrix4x4 g1Fixed;
            aiMatrix4x4 g0Legacy;
            aiMatrix4x4 g1Legacy;
            AiFindNodeGlobal(scene->mRootNode, anim, time0, identity, bn, g0Fixed);
            AiFindNodeGlobal(scene->mRootNode, anim, timeProbe, identity, bn, g1Fixed);
            AiFindNodeGlobalLegacy(scene->mRootNode, anim, time0, identity, bn, g0Legacy);
            AiFindNodeGlobalLegacy(scene->mRootNode, anim, timeProbe, identity, bn, g1Legacy);
            aiVector3D gtf0, gsf0, gtf1, gsf1, gtl0, gsl0, gtl1, gsl1;
            aiQuaternion grf0, grf1, grl0, grl1;
            AiTryDecomposeTrs(g0Fixed, gtf0, grf0, gsf0);
            AiTryDecomposeTrs(g1Fixed, gtf1, grf1, gsf1);
            AiTryDecomposeTrs(g0Legacy, gtl0, grl0, gsl0);
            AiTryDecomposeTrs(g1Legacy, gtl1, grl1, gsl1);

            printf("[AnimDiagBone] bone=%s ch=%d keys[p=%u r=%u s=%u] bindS=(%.6f,%.6f,%.6f) "
                "localS0=(%.6f,%.6f,%.6f) localS1=(%.6f,%.6f,%.6f) legacyS0=(%.6f,%.6f,%.6f) legacyS1=(%.6f,%.6f,%.6f) "
                "legacyPosDelta=%.6f legacyRotDeltaDeg=%.6f unstableScale=%.6f\n",
                (bn->mName.length > 0) ? bn->mName.C_Str() : "<unnamed>",
                fixed0.hasChannel ? 1 : 0,
                fixed0.posKeys, fixed0.rotKeys, fixed0.sclKeys,
                fixed0.bindScale.x, fixed0.bindScale.y, fixed0.bindScale.z,
                fixed0.scale.x, fixed0.scale.y, fixed0.scale.z,
                fixedProbe.scale.x, fixedProbe.scale.y, fixedProbe.scale.z,
                legS0.x, legS0.y, legS0.z,
                legS1.x, legS1.y, legS1.z,
                legacyPosDelta, legacyRotDeg, unstable);
            printf("[AnimDiagGlobal] bone=%s fixedGScale0=(%.6f,%.6f,%.6f) fixedGScale1=(%.6f,%.6f,%.6f) "
                "legacyGScale0=(%.6f,%.6f,%.6f) legacyGScale1=(%.6f,%.6f,%.6f)\n",
                (bn->mName.length > 0) ? bn->mName.C_Str() : "<unnamed>",
                gsf0.x, gsf0.y, gsf0.z,
                gsf1.x, gsf1.y, gsf1.z,
                gsl0.x, gsl0.y, gsl0.z,
                gsl1.x, gsl1.y, gsl1.z);
            printed++;
        }
    }

    printf("[AnimDiagSummary] nonUnitScaleBones=%d unstableScaleBones=%d missingScaleKeysUsingBind=%d "
        "legacyScaleMismatch=%d legacyPosMismatch=%d legacyRotMismatch=%d\n",
        nonUnitScale, unstableScale, missingScaleKeysUsingBind,
        legacyScaleMismatch, legacyPosMismatch, legacyRotMismatch);
}

// ---------------------------------------------------------------------------
// SanitizeName
// ---------------------------------------------------------------------------

std::string SanitizeName(const std::string& name)
{
    std::string out;
    out.reserve(name.size());
    for (char c : name)
    {
        if (std::isalnum((unsigned char)c) || c == '_' || c == '-') out.push_back(c);
        else if (c == ' ') out.push_back('_');
    }
    if (out.empty()) out = "Anim";
    return out;
}

// ---------------------------------------------------------------------------
// ResolveProjectAssetPath
// ---------------------------------------------------------------------------

std::filesystem::path ResolveProjectAssetPath(const std::string& relOrAbs)
{
    if (relOrAbs.empty()) return {};
    std::filesystem::path p(relOrAbs);
    if (p.is_absolute()) return p;
    if (gProjectDir.empty()) return {};
    return std::filesystem::path(gProjectDir) / p;
}

// ---------------------------------------------------------------------------
// Embedded animation meta path
// ---------------------------------------------------------------------------

std::filesystem::path GetNebMeshEmbeddedMetaPath(const std::filesystem::path& absMeshPath)
{
    return absMeshPath.parent_path() / "animmeta" / (absMeshPath.stem().string() + ".animmeta.animmeta");
}

// ---------------------------------------------------------------------------
// CSV helpers
// ---------------------------------------------------------------------------

std::string JoinUIntCsv(const std::vector<unsigned int>& values)
{
    std::string out;
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (i > 0) out += ",";
        out += std::to_string(values[i]);
    }
    return out;
}

bool ParseUIntCsv(const std::string& s, std::vector<unsigned int>& outValues)
{
    outValues.clear();
    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        if (token.empty()) continue;
        unsigned int v = (unsigned int)strtoul(token.c_str(), nullptr, 10);
        outValues.push_back(v);
    }
    return !outValues.empty();
}

std::string JoinU32Csv(const std::vector<uint32_t>& values)
{
    std::string out;
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (i > 0) out += ",";
        out += std::to_string(values[i]);
    }
    return out;
}

bool ParseU32Csv(const std::string& s, std::vector<uint32_t>& outValues)
{
    outValues.clear();
    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        if (token.empty()) continue;
        uint32_t v = (uint32_t)strtoul(token.c_str(), nullptr, 10);
        outValues.push_back(v);
    }
    return !outValues.empty();
}

// ---------------------------------------------------------------------------
// BuildDefaultEmbeddedMetaFromScene
// ---------------------------------------------------------------------------

void BuildDefaultEmbeddedMetaFromScene(const aiScene* scene, NebMeshEmbeddedAnimMeta& outMeta)
{
    outMeta.clipNames.clear();
    outMeta.meshIndices.clear();
    outMeta.mapIndices.clear();
    outMeta.provenanceVersion = 1;
    outMeta.provenanceMeshIndices.clear();
    outMeta.provenanceVertexIndices.clear();
    outMeta.exportedVertexCount = 0;
    outMeta.mappingVerified = false;
    outMeta.mappingOk = false;
    outMeta.mappingQuality = "missing";
    if (!scene) return;

    for (unsigned int i = 0; i < scene->mNumAnimations; ++i)
    {
        const aiAnimation* anim = scene->mAnimations[i];
        if (!anim) continue;
        std::string clipName = anim->mName.length > 0 ? anim->mName.C_Str() : ("Anim" + std::to_string(i + 1));
        outMeta.clipNames.push_back(clipName);
    }
    for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi)
    {
        const aiMesh* m = scene->mMeshes[mi];
        if (!m) continue;
        outMeta.meshIndices.push_back(mi);
        outMeta.exportedVertexCount += m->mNumVertices;
    }
}

// ---------------------------------------------------------------------------
// SaveNebMeshEmbeddedMeta / LoadNebMeshEmbeddedMeta
// ---------------------------------------------------------------------------

bool SaveNebMeshEmbeddedMeta(const std::filesystem::path& absMeshPath, const NebMeshEmbeddedAnimMeta& meta)
{
    std::filesystem::path metaPath = GetNebMeshEmbeddedMetaPath(absMeshPath);
    std::filesystem::create_directories(metaPath.parent_path());
    std::ofstream out(metaPath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;
    out << "source_fbx=" << meta.sourceFbxPath << "\n";
    out << "exported_vertex_count=" << meta.exportedVertexCount << "\n";
    out << "mesh_indices=" << JoinUIntCsv(meta.meshIndices) << "\n";
    out << "mapping_verified=" << (meta.mappingVerified ? 1 : 0) << "\n";
    out << "mapping_ok=" << (meta.mappingOk ? 1 : 0) << "\n";
    out << "mapping_quality=" << meta.mappingQuality << "\n";
    out << "map_indices=" << JoinU32Csv(meta.mapIndices) << "\n";
    out << "provenance_version=" << meta.provenanceVersion << "\n";
    out << "provenance_count=" << meta.provenanceMeshIndices.size() << "\n";
    out << "provenance_mesh_indices=" << JoinU32Csv(meta.provenanceMeshIndices) << "\n";
    out << "provenance_vertex_indices=" << JoinU32Csv(meta.provenanceVertexIndices) << "\n";
    out << "clip_count=" << meta.clipNames.size() << "\n";
    for (size_t i = 0; i < meta.clipNames.size(); ++i)
        out << "clip" << i << "=" << meta.clipNames[i] << "\n";
    return true;
}

bool LoadNebMeshEmbeddedMeta(const std::filesystem::path& absMeshPath, NebMeshEmbeddedAnimMeta& outMeta)
{
    outMeta = NebMeshEmbeddedAnimMeta{};
    std::ifstream in(GetNebMeshEmbeddedMetaPath(absMeshPath));
    if (!in.is_open()) return false;

    std::string line;
    size_t clipCount = 0;
    size_t provenanceCountDeclared = 0;
    bool provenanceCountSeen = false;
    std::vector<std::string> loadedClips;
    while (std::getline(in, line))
    {
        if (line.rfind("source_fbx=", 0) == 0) outMeta.sourceFbxPath = line.substr(11);
        else if (line.rfind("exported_vertex_count=", 0) == 0) outMeta.exportedVertexCount = (uint32_t)strtoul(line.substr(22).c_str(), nullptr, 10);
        else if (line.rfind("mesh_indices=", 0) == 0) ParseUIntCsv(line.substr(13), outMeta.meshIndices);
        else if (line.rfind("mapping_verified=", 0) == 0) outMeta.mappingVerified = (line.substr(17) == "1");
        else if (line.rfind("mapping_ok=", 0) == 0) outMeta.mappingOk = (line.substr(11) == "1");
        else if (line.rfind("mapping_quality=", 0) == 0) outMeta.mappingQuality = line.substr(16);
        else if (line.rfind("map_indices=", 0) == 0) ParseU32Csv(line.substr(12), outMeta.mapIndices);
        else if (line.rfind("provenance_version=", 0) == 0) outMeta.provenanceVersion = (uint32_t)strtoul(line.substr(19).c_str(), nullptr, 10);
        else if (line.rfind("provenance_count=", 0) == 0) { provenanceCountDeclared = (size_t)strtoull(line.substr(17).c_str(), nullptr, 10); provenanceCountSeen = true; }
        else if (line.rfind("provenance_mesh_indices=", 0) == 0) ParseU32Csv(line.substr(24), outMeta.provenanceMeshIndices);
        else if (line.rfind("provenance_vertex_indices=", 0) == 0) ParseU32Csv(line.substr(26), outMeta.provenanceVertexIndices);
        else if (line.rfind("clip_count=", 0) == 0) clipCount = (size_t)strtoul(line.substr(11).c_str(), nullptr, 10);
        else if (line.rfind("clip", 0) == 0)
        {
            size_t eq = line.find('=');
            if (eq != std::string::npos && eq > 4)
                loadedClips.push_back(line.substr(eq + 1));
        }
    }
    outMeta.clipNames = loadedClips;
    if (clipCount > 0 && outMeta.clipNames.size() > clipCount)
        outMeta.clipNames.resize(clipCount);
    if (outMeta.mappingQuality.empty())
        outMeta.mappingQuality = outMeta.mappingOk ? "approx" : "missing";
    if (outMeta.provenanceVersion == 0)
        outMeta.provenanceVersion = 1;
    if (provenanceCountSeen)
    {
        const bool provCountOk =
            (provenanceCountDeclared == outMeta.provenanceMeshIndices.size()) &&
            (provenanceCountDeclared == outMeta.provenanceVertexIndices.size());
        if (!provCountOk)
        {
            printf("[AnimMap] stage=fail_animmeta_provenance_count_mismatch\n");
            printf("[AnimMap] mode=missing_provenance\n");
            printf("[AnimMap] provenanceCountDeclared=%zu\n", provenanceCountDeclared);
            printf("[AnimMap] provenanceMeshCount=%zu\n", outMeta.provenanceMeshIndices.size());
            printf("[AnimMap] provenanceVertCount=%zu\n", outMeta.provenanceVertexIndices.size());
            outMeta.provenanceMeshIndices.clear();
            outMeta.provenanceVertexIndices.clear();
            outMeta.mappingOk = false;
            outMeta.mappingQuality = "missing";
            outMeta.mappingVerified = false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// FindAnimByNameOrIndex
// ---------------------------------------------------------------------------

const aiAnimation* FindAnimByNameOrIndex(const aiScene* scene, const std::string& clipName, int clipIndex)
{
    if (!scene || scene->mNumAnimations == 0) return nullptr;
    if (!clipName.empty())
    {
        for (unsigned int i = 0; i < scene->mNumAnimations; ++i)
        {
            const aiAnimation* anim = scene->mAnimations[i];
            if (!anim) continue;
            std::string nm = anim->mName.length > 0 ? anim->mName.C_Str() : ("Anim" + std::to_string(i + 1));
            if (nm == clipName) return anim;
        }
    }
    if (clipIndex >= 0 && clipIndex < (int)scene->mNumAnimations)
        return scene->mAnimations[clipIndex];
    return scene->mAnimations[0];
}

// ---------------------------------------------------------------------------
// Inspector mapping/playback helpers
// ---------------------------------------------------------------------------

InspectorMappingQuality ParseMappingQuality(const std::string& q)
{
    if (q == "exact") return InspectorMappingQuality::Exact;
    if (q == "approx") return InspectorMappingQuality::Approx;
    return InspectorMappingQuality::Missing;
}

const char* MappingQualityLabel(InspectorMappingQuality q)
{
    if (q == InspectorMappingQuality::Exact) return "Exact";
    if (q == InspectorMappingQuality::Approx) return "Approx";
    return "Missing";
}

const char* PlaybackModeLabel(InspectorPlaybackMode m)
{
    if (m == InspectorPlaybackMode::EmbeddedExact) return "EmbeddedExact";
    if (m == InspectorPlaybackMode::EmbeddedApprox) return "EmbeddedApprox";
    return "ExternalLegacy";
}

// ---------------------------------------------------------------------------
// PackSourceVertexKey / BuildMergedSourceIndexTable
// ---------------------------------------------------------------------------

uint64_t PackSourceVertexKey(uint32_t meshIndex, uint32_t vertexIndex)
{
    return ((uint64_t)meshIndex << 32ull) | (uint64_t)vertexIndex;
}

bool BuildMergedSourceIndexTable(
    const aiScene* scene,
    const std::vector<unsigned int>& meshIndices,
    std::unordered_map<uint64_t, uint32_t>& outMergedIndex,
    uint32_t& outMergedVertexCount)
{
    outMergedIndex.clear();
    outMergedVertexCount = 0;
    if (!scene) return false;

    outMergedIndex.reserve(4096);
    uint32_t runningOffset = 0;
    for (unsigned int mi : meshIndices)
    {
        if (mi >= scene->mNumMeshes || !scene->mMeshes[mi]) continue;
        const aiMesh* mesh = scene->mMeshes[mi];
        for (uint32_t vi = 0; vi < mesh->mNumVertices; ++vi)
        {
            outMergedIndex[PackSourceVertexKey(mi, vi)] = runningOffset + vi;
        }
        runningOffset += (uint32_t)mesh->mNumVertices;
    }
    outMergedVertexCount = runningOffset;
    return outMergedVertexCount > 0;
}

// ---------------------------------------------------------------------------
// SampleEmbeddedMergedVerticesCached
// ---------------------------------------------------------------------------

bool SampleEmbeddedMergedVerticesCached(
    NebMeshInspectorState& st,
    const aiAnimation* anim,
    int selectedClip,
    int frame,
    float fps,
    std::vector<Vec3>& outMerged)
{
    outMerged.clear();
    if (!st.embeddedScene || !anim) return false;
    if (st.embeddedCacheValid &&
        st.embeddedCacheClipIndex == selectedClip &&
        st.embeddedCacheFrame == frame)
    {
        outMerged = st.embeddedCacheMergedVertices;
        return !outMerged.empty();
    }

    const double tps = anim->mTicksPerSecond != 0.0 ? anim->mTicksPerSecond : 24.0;
    const double timeTicks = ((double)frame / std::max(1.0f, fps)) * tps;
    if (!SampleMergedFbxVertices(st.embeddedScene, anim, st.embeddedMeta.meshIndices, timeTicks, outMerged, nullptr))
        return false;

    st.embeddedCacheMergedVertices = outMerged;
    st.embeddedCacheClipIndex = selectedClip;
    st.embeddedCacheFrame = frame;
    st.embeddedCacheValid = true;
    return true;
}

// ---------------------------------------------------------------------------
// SampleMergedFbxVertices
// ---------------------------------------------------------------------------

bool SampleMergedFbxVertices(
    const aiScene* scene,
    const aiAnimation* anim,
    const std::vector<unsigned int>& meshIndices,
    double timeTicks,
    std::vector<Vec3>& outVerts,
    AnimBakeDiagnostics* outDiag)
{
    outVerts.clear();
    if (outDiag) *outDiag = AnimBakeDiagnostics{};
    if (!scene || meshIndices.empty()) return false;

    struct BoneInfluence { uint16_t bone = 0; float weight = 0.0f; };
    struct AnimMeshRef {
        const aiMesh* mesh = nullptr;
        const aiNode* node = nullptr;
        std::vector<const aiNode*> boneNodes;
        std::vector<aiMatrix4x4> boneOffsets;
        std::vector<std::vector<BoneInfluence>> vertexWeights;
    };
    std::vector<AnimMeshRef> refs;
    refs.reserve(meshIndices.size());
    for (unsigned int mi : meshIndices)
    {
        if (mi >= scene->mNumMeshes) continue;
        const aiMesh* mesh = scene->mMeshes[mi];
        if (!mesh) continue;
        AnimMeshRef ref;
        ref.mesh = mesh;
        ref.node = AiFindNodeWithMesh(scene->mRootNode, mi);
        if (!ref.node) ref.node = scene->mRootNode;
        ref.vertexWeights.resize(mesh->mNumVertices);
        ref.boneNodes.resize(mesh->mNumBones, nullptr);
        ref.boneOffsets.resize(mesh->mNumBones);
        for (unsigned int bi = 0; bi < mesh->mNumBones; ++bi)
        {
            const aiBone* b = mesh->mBones[bi];
            if (!b) continue;
            ref.boneNodes[bi] = ResolveSceneNodeByNameRobust(scene, b->mName);
            ref.boneOffsets[bi] = b->mOffsetMatrix;
            if (outDiag)
            {
                outDiag->totalBones++;
                if (ref.boneNodes[bi]) outDiag->matchedBones++;
                else outDiag->unmatchedBones++;
            }
            for (unsigned int wi = 0; wi < b->mNumWeights; ++wi)
            {
                const aiVertexWeight& vw = b->mWeights[wi];
                if (vw.mVertexId >= mesh->mNumVertices) continue;
                BoneInfluence inf;
                inf.bone = (uint16_t)bi;
                inf.weight = vw.mWeight;
                ref.vertexWeights[vw.mVertexId].push_back(inf);
            }
        }
        refs.push_back(std::move(ref));
    }
    if (refs.empty()) return false;

    aiMatrix4x4 identity;
    if (outDiag && anim)
    {
        std::unordered_set<std::string> chset;
        for (const auto& r : refs)
        {
            for (const aiNode* bn : r.boneNodes)
            {
                if (!bn) continue;
                if (AiFindChannel(anim, bn->mName))
                    chset.insert(NormalizeAnimName(bn->mName.C_Str()));
            }
        }
        outDiag->channelsFound = (int)chset.size();
    }
    for (const auto& r : refs)
    {
        aiMatrix4x4 meshGlobal;
        if (r.node && !AiFindNodeGlobal(scene->mRootNode, anim, timeTicks, identity, r.node, meshGlobal))
            meshGlobal = aiMatrix4x4();

        std::vector<aiMatrix4x4> boneMats;
        boneMats.resize(r.boneOffsets.size(), meshGlobal);
        for (size_t bi = 0; bi < r.boneOffsets.size(); ++bi)
        {
            aiMatrix4x4 bg;
            const aiNode* bn = r.boneNodes[bi];
            if (bn && AiFindNodeGlobal(scene->mRootNode, anim, timeTicks, identity, bn, bg))
                boneMats[bi] = bg * r.boneOffsets[bi];
            else
                boneMats[bi] = meshGlobal;
        }

        for (unsigned int v = 0; v < r.mesh->mNumVertices; ++v)
        {
            aiVector3D p = r.mesh->mVertices[v];
            aiVector3D tp(0, 0, 0);
            float wsum = 0.0f;
            if (v < r.vertexWeights.size() && !r.vertexWeights[v].empty())
            {
                for (const auto& inf : r.vertexWeights[v])
                {
                    if (inf.bone >= boneMats.size() || inf.weight <= 0.0f) continue;
                    aiVector3D bp = AiTransformPoint(boneMats[inf.bone], p);
                    tp += bp * inf.weight;
                    wsum += inf.weight;
                }
                if (wsum > 0.00001f) tp = tp * (1.0f / wsum);
                else tp = AiTransformPoint(meshGlobal, p);
            }
            else
            {
                tp = AiTransformPoint(meshGlobal, p);
            }

            Vec3 tv = ApplyImportBasis(Vec3{ tp.x, tp.y, tp.z });
            outVerts.push_back(tv);
        }
    }

    return !outVerts.empty();
}

// ---------------------------------------------------------------------------
// ComputeEmbeddedClipDiagnostics
// ---------------------------------------------------------------------------

bool ComputeEmbeddedClipDiagnostics(
    const aiScene* scene,
    const aiAnimation* anim,
    const std::vector<unsigned int>& meshIndices,
    float sampleFps,
    AnimBakeDiagnostics& outDiag)
{
    outDiag = AnimBakeDiagnostics{};
    if (!scene || !anim) return false;

    std::vector<Vec3> frame0;
    AnimBakeDiagnostics baseDiag;
    if (!SampleMergedFbxVertices(scene, anim, meshIndices, 0.0, frame0, &baseDiag))
        return false;
    outDiag = baseDiag;
    if (frame0.empty()) return false;

    const double tps = anim->mTicksPerSecond != 0.0 ? anim->mTicksPerSecond : 24.0;
    const double durationSec = (tps > 0.0) ? (anim->mDuration / tps) : 0.0;
    const uint32_t frameCount = (uint32_t)std::max(1.0, std::floor(durationSec * sampleFps + 0.5) + 1.0);
    float maxDelta = 0.0f;
    std::vector<Vec3> fr;
    for (uint32_t f = 1; f < frameCount; ++f)
    {
        const double timeTicks = ((double)f / std::max(1.0f, sampleFps)) * tps;
        if (!SampleMergedFbxVertices(scene, anim, meshIndices, timeTicks, fr, nullptr))
            continue;
        const size_t n = std::min(frame0.size(), fr.size());
        for (size_t i = 0; i < n; ++i)
        {
            const float dx = fr[i].x - frame0[i].x;
            const float dy = fr[i].y - frame0[i].y;
            const float dz = fr[i].z - frame0[i].z;
            const float d = sqrtf(dx * dx + dy * dy + dz * dz);
            if (d > maxDelta) maxDelta = d;
        }
    }
    outDiag.maxVertexDeltaFromFrame0 = maxDelta;
    return true;
}

// ---------------------------------------------------------------------------
// BuildMergedFbxBindData
// ---------------------------------------------------------------------------

bool BuildMergedFbxBindData(
    const aiScene* scene,
    const std::vector<unsigned int>& meshIndices,
    std::vector<Vec3>& outPositions,
    std::vector<Vec3>& outUvs)
{
    outPositions.clear();
    outUvs.clear();
    if (!scene) return false;

    for (unsigned int mi : meshIndices)
    {
        if (mi >= scene->mNumMeshes) continue;
        const aiMesh* mesh = scene->mMeshes[mi];
        if (!mesh) continue;

        int bestUvChannel = -1;
        float bestSpan = -1.0f;
        for (int ch = 0; ch < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++ch)
        {
            if (!(mesh->HasTextureCoords(ch) && mesh->mNumUVComponents[ch] >= 2)) continue;
            float minU = mesh->mTextureCoords[ch][0].x, maxU = minU;
            float minV = mesh->mTextureCoords[ch][0].y, maxV = minV;
            for (unsigned int v = 1; v < mesh->mNumVertices; ++v)
            {
                aiVector3D uv = mesh->mTextureCoords[ch][v];
                minU = std::min(minU, uv.x); maxU = std::max(maxU, uv.x);
                minV = std::min(minV, uv.y); maxV = std::max(maxV, uv.y);
            }
            float span = (maxU - minU) + (maxV - minV);
            if (span > bestSpan)
            {
                bestSpan = span;
                bestUvChannel = ch;
            }
        }

        for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
        {
            aiVector3D p = mesh->mVertices[v];
            Vec3 pv = ApplyImportBasis(Vec3{ p.x, p.y, p.z });
            outPositions.push_back(pv);
            if (bestUvChannel >= 0)
            {
                aiVector3D uv = mesh->mTextureCoords[bestUvChannel][v];
                outUvs.push_back(Vec3{ uv.x, uv.y, 0.0f });
            }
            else
            {
                outUvs.push_back(Vec3{ 0.0f, 0.0f, 0.0f });
            }
        }
    }
    return !outPositions.empty() && outPositions.size() == outUvs.size();
}

// ---------------------------------------------------------------------------
// LoadNebAnimClip
// ---------------------------------------------------------------------------

bool LoadNebAnimClip(const std::filesystem::path& path, NebAnimClip& outClip, std::string& outError)
{
    outClip = NebAnimClip{};
    outError.clear();

    std::ifstream in(path, std::ios::binary | std::ios::in);
    if (!in.is_open())
    {
        outError = "Missing .nebanim";
        return false;
    }

    char magic[4] = {};
    if (!in.read(magic, 4) || !(magic[0] == 'N' && magic[1] == 'E' && magic[2] == 'B' && magic[3] == '0'))
    {
        outError = "Invalid .nebanim header";
        return false;
    }

    uint32_t version = 0;
    uint32_t flags = 0;
    uint32_t vertexCount = 0;
    uint32_t frameCount = 0;
    uint32_t fpsFixed = 0;
    uint32_t deltaFracBits = 8;
    if (!ReadU32BE(in, version) || (version < 2 || version > 4))
    {
        outError = "Unsupported .nebanim version";
        return false;
    }
    if (version >= 3 && !ReadU32BE(in, flags))
    {
        outError = "Corrupt .nebanim flags";
        return false;
    }
    if (!ReadU32BE(in, vertexCount) || !ReadU32BE(in, frameCount) || !ReadU32BE(in, fpsFixed))
    {
        outError = "Corrupt .nebanim header";
        return false;
    }
    if (version >= 3 && !ReadU32BE(in, deltaFracBits))
    {
        outError = "Corrupt .nebanim delta config";
        return false;
    }

    if (vertexCount == 0 || frameCount == 0 || vertexCount > 4096 || frameCount > 1000)
    {
        outError = "Unsupported .nebanim dimensions";
        return false;
    }

    outClip.version = version;
    outClip.flags = flags;
    outClip.vertexCount = vertexCount;
    outClip.frameCount = frameCount;
    outClip.fps = std::max(1.0f, FromFixed16_16((int32_t)fpsFixed));
    outClip.deltaFracBits = std::min<uint32_t>(deltaFracBits, 15u);
    outClip.frames.assign(frameCount, std::vector<Vec3>(vertexCount, Vec3{ 0.0f, 0.0f, 0.0f }));

    const bool deltaEncoded = (version >= 3) && ((flags & 1u) != 0);
    const float deltaInv = 1.0f / (float)(1u << outClip.deltaFracBits);
    std::vector<Vec3> prev(vertexCount, Vec3{ 0.0f, 0.0f, 0.0f });
    for (uint32_t f = 0; f < frameCount; ++f)
    {
        for (uint32_t v = 0; v < vertexCount; ++v)
        {
            Vec3 value = { 0.0f, 0.0f, 0.0f };
            if (!deltaEncoded || f == 0)
            {
                int32_t x = 0, y = 0, z = 0;
                if (!ReadS32BE(in, x) || !ReadS32BE(in, y) || !ReadS32BE(in, z))
                {
                    outError = "Corrupt .nebanim frame payload";
                    return false;
                }
                value = { FromFixed16_16(x), FromFixed16_16(y), FromFixed16_16(z) };
            }
            else
            {
                int16_t dx = 0, dy = 0, dz = 0;
                if (!ReadS16BE(in, dx) || !ReadS16BE(in, dy) || !ReadS16BE(in, dz))
                {
                    outError = "Corrupt .nebanim delta payload";
                    return false;
                }
                value.x = prev[v].x + (float)dx * deltaInv;
                value.y = prev[v].y + (float)dy * deltaInv;
                value.z = prev[v].z + (float)dz * deltaInv;
            }

            outClip.frames[f][v] = value;
            prev[v] = value;
        }
    }

    if (version >= 4)
    {
        if (!ReadU32BE(in, outClip.targetMeshVertexCount) || !ReadU32BE(in, outClip.targetMeshHash))
        {
            outError = "Corrupt .nebanim v4 trailer";
            return false;
        }

        const bool hasMap = (flags & 2u) != 0;
        outClip.hasEmbeddedMap = hasMap;
        outClip.meshAligned = (flags & 4u) != 0;
        if (hasMap)
        {
            uint32_t mapCount = 0;
            if (!ReadU32BE(in, mapCount) || mapCount == 0 || mapCount > 65536)
            {
                outError = "Corrupt .nebanim map header";
                return false;
            }
            outClip.embeddedMapIndices.resize(mapCount);
            for (uint32_t i = 0; i < mapCount; ++i)
            {
                if (!ReadU32BE(in, outClip.embeddedMapIndices[i]))
                {
                    outError = "Corrupt .nebanim map payload";
                    return false;
                }
            }
        }
    }

    outClip.valid = true;
    return true;
}

// ---------------------------------------------------------------------------
// RebuildNebMeshEmbeddedMapping
// ---------------------------------------------------------------------------

bool RebuildNebMeshEmbeddedMapping(NebMeshInspectorState& st)
{
    st.embeddedSourceOk = false;
    st.embeddedMappingOk = false;
    st.embeddedVertexMap.clear();
    st.embeddedStatusMessage.clear();
    st.embeddedCacheValid = false;
    st.loggedCurrentPreviewState = false;
    st.lastPreviewReason.clear();

    const std::filesystem::path meshPath = st.targetMeshPath;
    const std::filesystem::path metaPath = meshPath.empty() ? std::filesystem::path() : GetNebMeshEmbeddedMetaPath(meshPath);
    const bool metaExists = (!metaPath.empty() && std::filesystem::exists(metaPath));
    const std::string fbxRaw = st.embeddedMeta.sourceFbxPath;
    std::filesystem::path absFbx = ResolveProjectAssetPath(fbxRaw);
    const bool fbxExists = (!absFbx.empty() && std::filesystem::exists(absFbx));
    size_t baseNebVerts = 0;
    size_t fbxMeshCount = 0;
    size_t mergedFbxVerts = 0;
    size_t candidateSetCount = 0;
    size_t outputMapSize = 0;
    int provenancePresent = 0;
    size_t provenanceCount = 0;
    int provenanceValid = 0;
    size_t firstInvalidProvenanceIndex = SIZE_MAX;
    std::string exactUnavailableReason;

    auto emitDiag = [&](const char* stage, const char* mode) {
        printf("[AnimMap] stage=%s\n", stage ? stage : "(null)");
        printf("[AnimMap] mode=%s\n", mode ? mode : "(null)");
        printf("[AnimMap] mesh=%s\n", meshPath.generic_string().c_str());
        printf("[AnimMap] metaPath=%s exists=%d\n", metaPath.generic_string().c_str(), metaExists ? 1 : 0);
        printf("[AnimMap] fbxPathRaw=%s\n", fbxRaw.c_str());
        printf("[AnimMap] fbxPathResolved=%s exists=%d\n", absFbx.generic_string().c_str(), fbxExists ? 1 : 0);
        printf("[AnimMap] baseNebVerts=%zu\n", baseNebVerts);
        printf("[AnimMap] finalNebVerts=%zu\n", baseNebVerts);
        printf("[AnimMap] fbxMeshCount=%zu\n", fbxMeshCount);
        printf("[AnimMap] mergedFbxVerts=%zu\n", mergedFbxVerts);
        printf("[AnimMap] candidateSetCount=%zu\n", candidateSetCount);
        printf("[AnimMap] outputMapSize=%zu expected=%zu\n", outputMapSize, baseNebVerts);
        printf("[AnimMap] provenancePresent=%d\n", provenancePresent);
        printf("[AnimMap] provenanceCount=%zu\n", provenanceCount);
        printf("[AnimMap] provenanceValid=%d\n", provenanceValid);
        printf("[AnimMap] firstInvalidProvenanceIndex=%zu\n", (firstInvalidProvenanceIndex == SIZE_MAX) ? (size_t)-1 : firstInvalidProvenanceIndex);
        printf("[AnimMap] invalidIndex=%zu\n", (firstInvalidProvenanceIndex == SIZE_MAX) ? (size_t)-1 : firstInvalidProvenanceIndex);
        if (!exactUnavailableReason.empty())
            printf("[AnimMap] reason=%s\n", exactUnavailableReason.c_str());
    };

    auto fail = [&](const char* stage, const std::string& statusMsg, const std::string& toastMsg, bool persistMetaState) -> bool {
        st.embeddedStatusMessage = statusMsg;
        st.embeddedDiagDirty = true;
        st.nearStaticWarned = false;
        exactUnavailableReason = statusMsg;
        if (!toastMsg.empty())
        {
            gViewportToast = "Exact provenance unavailable: " + toastMsg;
            gViewportToastUntil = glfwGetTime() + 2.0;
        }
        st.embeddedMeta.mappingVerified = true;
        st.embeddedMeta.mappingOk = false;
        st.embeddedMeta.mappingQuality = "missing";
        st.embeddedMeta.mapIndices.clear();
        emitDiag(stage, stage);
        if (persistMetaState && !meshPath.empty())
        {
            if (!SaveNebMeshEmbeddedMeta(meshPath, st.embeddedMeta))
            {
                gViewportToast = "Exact provenance unavailable: map write failure";
                gViewportToastUntil = glfwGetTime() + 2.0;
                emitDiag("fail_map_write", "fail_map_write");
            }
        }
        return false;
    };

    if (meshPath.empty())
        return fail("fail_no_mesh", "No mesh selected", "invalid .nebmesh", false);

    const NebMesh* mesh = GetNebMesh(meshPath);
    if (!mesh || !mesh->valid)
        return fail("fail_invalid_mesh", "Target .nebmesh missing/invalid", "invalid .nebmesh", true);
    baseNebVerts = mesh->positions.size();
    provenancePresent = (!st.embeddedMeta.provenanceMeshIndices.empty() &&
                         st.embeddedMeta.provenanceMeshIndices.size() == st.embeddedMeta.provenanceVertexIndices.size()) ? 1 : 0;
    provenanceCount = st.embeddedMeta.provenanceMeshIndices.size();
    provenanceValid = 0;
    firstInvalidProvenanceIndex = SIZE_MAX;

    if (!st.embeddedMetaLoaded || fbxRaw.empty())
        return fail("fail_missing_metadata", "Missing embedded metadata", "missing metadata", true);

    if (absFbx.empty() || !std::filesystem::exists(absFbx))
        return fail("fail_missing_fbx", "Embedded FBX source missing", "missing source FBX", true);

    if (st.embeddedLoadedFbxPath != absFbx.generic_string() || st.embeddedScene == nullptr)
    {
        st.embeddedScene = st.embeddedImporter.ReadFile(
            absFbx.string(),
            aiProcess_JoinIdenticalVertices |
            aiProcess_GlobalScale);
        st.embeddedLoadedFbxPath = absFbx.generic_string();
    }
    if (!st.embeddedScene)
        return fail("fail_load_fbx", "Failed to load embedded FBX", "failed to load source FBX", true);

    st.embeddedSourceOk = true;
    fbxMeshCount = st.embeddedScene->mNumMeshes;

    std::vector<unsigned int> meshIndices = st.embeddedMeta.meshIndices;
    if (meshIndices.empty())
    {
        for (unsigned int mi = 0; mi < st.embeddedScene->mNumMeshes; ++mi)
        {
            if (st.embeddedScene->mMeshes[mi])
                meshIndices.push_back(mi);
        }
    }
    if (meshIndices.empty())
        return fail("fail_no_candidates", "No candidate FBX meshes", "no candidate meshes", true);

    // Deterministic exact path: provenance-driven map from export-time source IDs.
    if (provenancePresent && provenanceCount == baseNebVerts)
    {
        std::unordered_map<uint64_t, uint32_t> mergedIndex;
        uint32_t runningOffset = 0;
        BuildMergedSourceIndexTable(st.embeddedScene, meshIndices, mergedIndex, runningOffset);
        mergedFbxVerts = (size_t)runningOffset;
        candidateSetCount = mergedFbxVerts;

        std::vector<uint32_t> provMap;
        provMap.reserve(baseNebVerts);
        bool provOk = true;
        for (size_t i = 0; i < baseNebVerts; ++i)
        {
            const uint32_t sm = st.embeddedMeta.provenanceMeshIndices[i];
            const uint32_t sv = st.embeddedMeta.provenanceVertexIndices[i];
            if (sm >= st.embeddedScene->mNumMeshes || !st.embeddedScene->mMeshes[sm]) { provOk = false; firstInvalidProvenanceIndex = i; break; }
            if (sv >= st.embeddedScene->mMeshes[sm]->mNumVertices) { provOk = false; firstInvalidProvenanceIndex = i; break; }
            const auto it = mergedIndex.find(PackSourceVertexKey(sm, sv));
            if (it == mergedIndex.end()) { provOk = false; firstInvalidProvenanceIndex = i; break; }
            uint32_t merged = it->second;
            if (merged >= runningOffset) { provOk = false; firstInvalidProvenanceIndex = i; break; }
            provMap.push_back(merged);
        }
        outputMapSize = provMap.size();
        if (provOk && provMap.size() == baseNebVerts)
        {
            provenanceValid = 1;
            st.embeddedVertexMap = provMap;
            st.embeddedMappingOk = true;
            st.embeddedMeta.mappingVerified = true;
            st.embeddedMeta.mappingOk = true;
            st.embeddedMeta.mappingQuality = "exact";
            st.embeddedMeta.meshIndices = meshIndices;
            st.embeddedMeta.mapIndices = provMap;
            st.embeddedMeta.exportedVertexCount = (uint32_t)baseNebVerts;
            st.embeddedStatusMessage = "Embedded mapping exact (provenance)";
            st.embeddedDiagDirty = true;
            st.nearStaticWarned = false;
            st.lastEmbeddedClipIndex = -1;
            st.embeddedCacheValid = false;
            st.loggedCurrentPreviewState = false;
            if (!SaveNebMeshEmbeddedMeta(meshPath, st.embeddedMeta))
            {
                st.embeddedMappingOk = false;
                st.embeddedMeta.mappingOk = false;
                st.embeddedMeta.mappingQuality = "missing";
                st.embeddedStatusMessage = "Map write failed";
                gViewportToast = "Exact provenance unavailable: map write failure";
                gViewportToastUntil = glfwGetTime() + 2.0;
                emitDiag("fail_map_write", "fail_map_write");
                return false;
            }
            emitDiag("success_exact", "provenance_exact");
            gViewportToast = "Exact provenance mapping rebuilt";
            gViewportToastUntil = glfwGetTime() + 2.0;
            return true;
        }
        exactUnavailableReason = (firstInvalidProvenanceIndex == SIZE_MAX)
            ? "Invalid provenance"
            : ("Invalid provenance at index " + std::to_string(firstInvalidProvenanceIndex));
    }
    else if (provenancePresent && provenanceCount != baseNebVerts)
    {
        exactUnavailableReason = "Provenance count mismatch";
    }
    else
    {
        exactUnavailableReason = "Provenance missing";
    }

    const aiAnimation* diagAnim = nullptr;
    if (st.inspectorPlayback.selectedClip >= 0 && st.inspectorPlayback.selectedClip < (int)st.embeddedMeta.clipNames.size())
        diagAnim = FindAnimByNameOrIndex(st.embeddedScene, st.embeddedMeta.clipNames[(size_t)st.inspectorPlayback.selectedClip], st.inspectorPlayback.selectedClip);
    else
        diagAnim = FindAnimByNameOrIndex(st.embeddedScene, std::string(), 0);
    if (diagAnim)
    {
        AnimBakeDiagnostics bdiag;
        if (ComputeEmbeddedClipDiagnostics(st.embeddedScene, diagAnim, meshIndices, 12.0f, bdiag))
        {
            printf("[AnimBake] totalBones=%d matchedBones=%d unmatchedBones=%d channelsFound=%d maxDelta=%.6f\n",
                bdiag.totalBones, bdiag.matchedBones, bdiag.unmatchedBones, bdiag.channelsFound, bdiag.maxVertexDeltaFromFrame0);
            const float kNearStaticEps = 1e-4f;
            if (bdiag.maxVertexDeltaFromFrame0 < kNearStaticEps)
            {
                gViewportToast = "Embedded clip sampled but produced near-static deformation (bone/channel mismatch)";
                gViewportToastUntil = glfwGetTime() + 2.5;
            }
        }
    }

    std::vector<Vec3> bindVerts;
    std::vector<Vec3> bindUvs;
    if (!BuildMergedFbxBindData(st.embeddedScene, meshIndices, bindVerts, bindUvs))
        return fail("fail_build_candidates", "Failed to build candidate sets", "no candidate meshes", true);

    mergedFbxVerts = bindVerts.size();
    candidateSetCount = bindVerts.size();
    if (bindVerts.empty())
        return fail("fail_empty_candidates", "Candidate set empty", "no candidate meshes", true);

    std::vector<uint32_t> outMap;
    outMap.reserve(baseNebVerts);
    // Legacy fallback: weighted position+UV nearest assignment with one-to-one preference.
    std::vector<unsigned char> used(bindVerts.size(), 0);
    const float uvWeight = 0.1f;
    for (size_t i = 0; i < mesh->positions.size(); ++i)
    {
        const Vec3& p = mesh->positions[i];
        const Vec3 uv = (mesh->hasUv && i < mesh->uvs.size()) ? mesh->uvs[i] : Vec3{ 0.0f, 0.0f, 0.0f };
        int best = -1;
        float bestScore = FLT_MAX;

        for (size_t j = 0; j < bindVerts.size(); ++j)
        {
            if (used[j]) continue;
            const Vec3& q = bindVerts[j];
            const Vec3 quv = (j < bindUvs.size()) ? bindUvs[j] : Vec3{ 0.0f, 0.0f, 0.0f };
            const float dx = p.x - q.x;
            const float dy = p.y - q.y;
            const float dz = p.z - q.z;
            const float du = uv.x - quv.x;
            const float dv = uv.y - quv.y;
            const float score = (dx * dx + dy * dy + dz * dz) + uvWeight * (du * du + dv * dv);
            if (score < bestScore) { bestScore = score; best = (int)j; }
        }

        if (best < 0)
        {
            // Allow reuse if source set is smaller than destination.
            for (size_t j = 0; j < bindVerts.size(); ++j)
            {
                const Vec3& q = bindVerts[j];
                const Vec3 quv = (j < bindUvs.size()) ? bindUvs[j] : Vec3{ 0.0f, 0.0f, 0.0f };
                const float dx = p.x - q.x;
                const float dy = p.y - q.y;
                const float dz = p.z - q.z;
                const float du = uv.x - quv.x;
                const float dv = uv.y - quv.y;
                const float score = (dx * dx + dy * dy + dz * dz) + uvWeight * (du * du + dv * dv);
                if (score < bestScore) { bestScore = score; best = (int)j; }
            }
        }

        if (best >= 0)
        {
            used[(size_t)best] = 1;
            outMap.push_back((uint32_t)best);
        }
    }

    outputMapSize = outMap.size();
    if (outMap.empty() || outMap.size() != baseNebVerts)
        return fail("fail_map_empty", "Map build empty", "map build empty", true);

    st.embeddedVertexMap = outMap;
    st.embeddedMappingOk = true;
    st.embeddedMeta.mappingVerified = true;
    st.embeddedMeta.mappingOk = true;
    st.embeddedMeta.mappingQuality = "approx";
    st.embeddedMeta.meshIndices = meshIndices;
    st.embeddedMeta.mapIndices = outMap;
    st.embeddedMeta.exportedVertexCount = (uint32_t)bindVerts.size();
    st.embeddedStatusMessage = provenancePresent ? "Embedded mapping approximate (provenance invalid)" : "Embedded mapping approximate (provenance missing)";
    st.embeddedDiagDirty = true;
    st.nearStaticWarned = false;
    st.lastEmbeddedClipIndex = -1;
    st.embeddedCacheValid = false;
    st.loggedCurrentPreviewState = false;

    if (!SaveNebMeshEmbeddedMeta(meshPath, st.embeddedMeta))
    {
        st.embeddedMappingOk = false;
        st.embeddedMeta.mappingOk = false;
        st.embeddedMeta.mappingQuality = "missing";
        st.embeddedStatusMessage = "Map write failed";
        gViewportToast = "Exact provenance unavailable: map write failure";
        gViewportToastUntil = glfwGetTime() + 2.0;
        emitDiag("fail_map_write", "fail_map_write");
        return false;
    }

    gViewportToast = "Exact provenance unavailable: " + (exactUnavailableReason.empty() ? std::string("approx fallback active") : exactUnavailableReason);
    gViewportToastUntil = glfwGetTime() + 2.0;
    emitDiag("success_approx", "approx_fallback");
    return true;
}

// ---------------------------------------------------------------------------
// ExportNebAnimation
// ---------------------------------------------------------------------------

bool ExportNebAnimation(const aiScene* scene, const aiAnimation* anim, const std::vector<unsigned int>& meshIndices, const std::filesystem::path& outPath, std::string& warning, bool deltaCompress, uint32_t forcedVertexCount, const NebMesh* forcedTargetMesh, const std::vector<uint32_t>* forcedMapIndices, float sampleRateMultiplier, bool meshLocalSpace)
{
    if (!scene || !anim || scene->mNumMeshes == 0 || meshIndices.empty()) return false;

    struct BoneInfluence { uint16_t bone = 0; float weight = 0.0f; };
    struct AnimMeshRef {
        const aiMesh* mesh = nullptr;
        const aiNode* node = nullptr;
        std::vector<const aiNode*> boneNodes;
        std::vector<aiMatrix4x4> boneOffsets;
        std::vector<std::vector<BoneInfluence>> vertexWeights;
    };
    std::vector<AnimMeshRef> refs;
    refs.reserve(meshIndices.size());

    uint32_t totalVerts = 0;
    for (unsigned int mi : meshIndices)
    {
        if (mi >= scene->mNumMeshes) continue;
        const aiMesh* mesh = scene->mMeshes[mi];
        if (!mesh) continue;

        AnimMeshRef ref;
        ref.mesh = mesh;
        ref.node = AiFindNodeWithMesh(scene->mRootNode, mi);
        if (!ref.node) ref.node = scene->mRootNode;

        ref.vertexWeights.resize(mesh->mNumVertices);
        ref.boneNodes.resize(mesh->mNumBones, nullptr);
        ref.boneOffsets.resize(mesh->mNumBones);
        for (unsigned int bi = 0; bi < mesh->mNumBones; ++bi)
        {
            const aiBone* b = mesh->mBones[bi];
            if (!b) continue;
            ref.boneNodes[bi] = ResolveSceneNodeByNameRobust(scene, b->mName);
            ref.boneOffsets[bi] = b->mOffsetMatrix;
            for (unsigned int wi = 0; wi < b->mNumWeights; ++wi)
            {
                const aiVertexWeight& vw = b->mWeights[wi];
                if (vw.mVertexId >= mesh->mNumVertices) continue;
                BoneInfluence inf;
                inf.bone = (uint16_t)bi;
                inf.weight = vw.mWeight;
                ref.vertexWeights[vw.mVertexId].push_back(inf);
            }
        }

        refs.push_back(std::move(ref));
        totalVerts += (uint32_t)mesh->mNumVertices;
    }
    if (refs.empty() || totalVerts == 0) return false;

    // Rebuild the exact vertex array that ExportNebMesh + LoadNebMesh produces,
    // using the same UV channel selection, S16 quantization, and cleanup. The cleanup's
    // provenance tracking gives us the guaranteed-correct FBX-to-nebmesh mapping.
    auto s16snap = [](float v) -> float {
        float scaled = v * 256.0f;
        if (scaled > 32767.0f) scaled = 32767.0f;
        if (scaled < -32768.0f) scaled = -32768.0f;
        return (float)((int16_t)std::lround(scaled)) / 256.0f;
    };

    // Select best UV channel per mesh (same logic as ExportNebMesh).
    std::vector<int> meshUvChannel(scene->mNumMeshes, -1);
    bool anyUv = false;
    for (unsigned int mi : meshIndices)
    {
        if (mi >= scene->mNumMeshes) continue;
        const aiMesh* mesh = scene->mMeshes[mi];
        if (!mesh) continue;
        int bestCh = -1;
        float bestSpan = -1.0f;
        for (int ch = 0; ch < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++ch)
        {
            if (!(mesh->HasTextureCoords(ch) && mesh->mNumUVComponents[ch] >= 2)) continue;
            float minU = mesh->mTextureCoords[ch][0].x, maxU = minU;
            float minV = mesh->mTextureCoords[ch][0].y, maxV = minV;
            for (unsigned int v = 1; v < mesh->mNumVertices; ++v)
            {
                aiVector3D uv = mesh->mTextureCoords[ch][v];
                minU = std::min(minU, uv.x); maxU = std::max(maxU, uv.x);
                minV = std::min(minV, uv.y); maxV = std::max(maxV, uv.y);
            }
            float span = (maxU - minU) + (maxV - minV);
            if (span > bestSpan) { bestSpan = span; bestCh = ch; }
        }
        meshUvChannel[mi] = bestCh;
        if (bestCh >= 0) anyUv = true;
    }

    // Build virtual nebmesh with S16-snapped positions + UVs and provenance.
    NebMesh virtualMesh;
    virtualMesh.hasUv = anyUv;
    std::vector<uint32_t> provMeshIdx, provVertIdx;
    std::vector<uint32_t> meshStartInFlat;
    {
        uint32_t flatStart = 0;
        size_t refI = 0;
        for (unsigned int mi : meshIndices)
        {
            if (mi >= scene->mNumMeshes) continue;
            const aiMesh* mesh = scene->mMeshes[mi];
            if (!mesh) continue;
            meshStartInFlat.push_back(flatStart);
            int uvCh = meshUvChannel[mi];
            bool meshHasUv = (uvCh >= 0 && mesh->HasTextureCoords(uvCh) && mesh->mNumUVComponents[uvCh] >= 2);
            for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
            {
                aiVector3D p = mesh->mVertices[v];
                Vec3 bp = ApplyImportBasis(Vec3{ p.x, p.y, p.z });
                virtualMesh.positions.push_back(Vec3{ s16snap(bp.x), s16snap(bp.y), s16snap(bp.z) });
                if (anyUv)
                {
                    if (meshHasUv)
                    {
                        aiVector3D uv = mesh->mTextureCoords[uvCh][v];
                        virtualMesh.uvs.push_back(Vec3{ s16snap(uv.x), s16snap(uv.y), 0.0f });
                    }
                    else
                        virtualMesh.uvs.push_back(Vec3{ 0.0f, 0.0f, 0.0f });
                }
                provMeshIdx.push_back(refI);
                provVertIdx.push_back(v);
            }
            flatStart += mesh->mNumVertices;
            ++refI;
        }
    }

    // Run the same cleanup as ExportNebMesh + LoadNebMesh to get deduplicated vertex order.
    CleanupNebMeshTopology(virtualMesh, &provMeshIdx, &provVertIdx);

    double tps = anim->mTicksPerSecond != 0.0 ? anim->mTicksPerSecond : 24.0;
    double durationTicks = anim->mDuration;
    double durationSec = (tps > 0.0) ? (durationTicks / tps) : 0.0;
    const float fps = 12.0f * std::max(1.0f, sampleRateMultiplier);
    unsigned int frameCount = (unsigned int)std::max(1.0, std::floor(durationSec * fps + 0.5) + 1.0);

    const uint32_t maxVerts = 2048;
    const uint32_t maxFrames = 300;
    if (totalVerts > maxVerts)
        warning = "Vertex limit exceeded (" + std::to_string(totalVerts) + ">" + std::to_string(maxVerts) + ")";
    if (frameCount > maxFrames)
    {
        if (!warning.empty()) warning += "; ";
        warning += "Frame limit exceeded (" + std::to_string(frameCount) + ">" + std::to_string(maxFrames) + ")";
    }

    std::ofstream out(outPath, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;

    const char magic[4] = { 'N','E','B','0' };
    uint32_t version = 4;
    uint32_t vertexCount = (forcedVertexCount > 0) ? forcedVertexCount : totalVerts;

    // Use the virtual mesh rebuild + provenance to compute the exact FBX->nebmesh mapping.
    // This is guaranteed correct because it uses the same code path as ExportNebMesh.
    std::vector<uint32_t> sourceIndexForOutput;
    if (!virtualMesh.positions.empty() && !provMeshIdx.empty() &&
        provMeshIdx.size() == virtualMesh.positions.size())
    {
        // Convert provenance (ref index + local vertex) to flat FBX bind-pose index.
        sourceIndexForOutput.resize(virtualMesh.positions.size());
        for (uint32_t i = 0; i < (uint32_t)virtualMesh.positions.size(); ++i)
        {
            uint32_t ri = provMeshIdx[i];
            uint32_t vi = provVertIdx[i];
            uint32_t flatIdx = (ri < meshStartInFlat.size()) ? (meshStartInFlat[ri] + vi) : vi;
            sourceIndexForOutput[i] = flatIdx;
        }
        vertexCount = (uint32_t)virtualMesh.positions.size();
        printf("[AnimExport] Rebuild-provenance: %u vertices mapped (from %u FBX verts)\n",
               vertexCount, totalVerts);
    }

    // Fallback: use explicit map indices if rebuild didn't succeed.
    if (sourceIndexForOutput.empty() && forcedMapIndices && forcedMapIndices->size() == vertexCount)
        sourceIndexForOutput = *forcedMapIndices;

    const bool hasEmbeddedMap = !sourceIndexForOutput.empty() && sourceIndexForOutput.size() == vertexCount;

    aiMatrix4x4 identity;

    std::vector<aiMatrix4x4> invMeshGlobalStatic(refs.size());
    if (meshLocalSpace)
    {
        for (size_t ri = 0; ri < refs.size(); ++ri)
        {
            aiMatrix4x4 mg;
            if (AiFindNodeGlobal(scene->mRootNode, anim, 0.0, identity, refs[ri].node, mg))
            {
                if (std::fabs(mg.Determinant()) < 1e-8f)
                {
                    meshLocalSpace = false;
                    break;
                }
                invMeshGlobalStatic[ri] = mg;
                invMeshGlobalStatic[ri].Inverse();
            }
        }
    }

    uint32_t flags = (deltaCompress ? 1u : 0u) | (hasEmbeddedMap ? 2u : 0u) | (meshLocalSpace ? 4u : 0u);
    uint32_t frames = frameCount;
    uint32_t fpsFixed = ToFixed16_16(fps);
    const uint32_t deltaFracBits = 8;

    out.write(magic, 4);
    WriteU32BE(out, version);
    if (version >= 3) WriteU32BE(out, flags);
    WriteU32BE(out, vertexCount);
    WriteU32BE(out, frames);
    WriteU32BE(out, fpsFixed);
    if (version >= 3) WriteU32BE(out, deltaFracBits);

    std::vector<aiVector3D> prev;
    prev.resize(vertexCount);

    bool anyDeltaClamp = false;

    for (unsigned int f = 0; f < frameCount; ++f)
    {
        double timeSec = (double)f / (double)fps;
        double timeTicks = timeSec * tps;
        if (timeTicks > durationTicks) timeTicks = durationTicks;

        std::vector<aiVector3D> frameVerts;
        frameVerts.reserve(totalVerts);

        for (size_t refIdx = 0; refIdx < refs.size(); ++refIdx)
        {
            const auto& r = refs[refIdx];
            aiMatrix4x4 meshGlobal;
            AiFindNodeGlobal(scene->mRootNode, anim, timeTicks, identity, r.node, meshGlobal);

            std::vector<aiMatrix4x4> boneMats;
            boneMats.resize(r.boneOffsets.size(), meshGlobal);
            for (size_t bi = 0; bi < r.boneOffsets.size(); ++bi)
            {
                aiMatrix4x4 bg;
                const aiNode* bn = r.boneNodes[bi];
                if (bn && AiFindNodeGlobal(scene->mRootNode, anim, timeTicks, identity, bn, bg))
                    boneMats[bi] = bg * r.boneOffsets[bi];
                else
                    boneMats[bi] = meshGlobal;
            }

            for (unsigned int v = 0; v < r.mesh->mNumVertices; ++v)
            {
                aiVector3D p = r.mesh->mVertices[v];
                aiVector3D tp(0, 0, 0);
                float wsum = 0.0f;

                if (v < r.vertexWeights.size() && !r.vertexWeights[v].empty())
                {
                    for (const auto& inf : r.vertexWeights[v])
                    {
                        if (inf.bone >= boneMats.size() || inf.weight <= 0.0f) continue;
                        aiVector3D bp = AiTransformPoint(boneMats[inf.bone], p);
                        tp += bp * inf.weight;
                        wsum += inf.weight;
                    }
                    if (wsum > 0.00001f)
                        tp = tp * (1.0f / wsum);
                    else
                        tp = AiTransformPoint(meshGlobal, p);
                }
                else
                {
                    tp = AiTransformPoint(meshGlobal, p);
                }

                if (meshLocalSpace) tp = AiTransformPoint(invMeshGlobalStatic[refIdx], tp);
                Vec3 tv = ApplyImportBasis(Vec3{ tp.x, tp.y, tp.z });
                frameVerts.push_back(aiVector3D(tv.x, tv.y, tv.z));
            }
        }

        if (frameVerts.empty())
            frameVerts.push_back(aiVector3D(0, 0, 0));
        while (frameVerts.size() < vertexCount)
            frameVerts.push_back(frameVerts.back());

        for (uint32_t vi = 0; vi < vertexCount; ++vi)
        {
            uint32_t srcIndex = vi;
            if (!sourceIndexForOutput.empty() && vi < sourceIndexForOutput.size())
                srcIndex = sourceIndexForOutput[vi];
            if (srcIndex >= frameVerts.size()) srcIndex = (uint32_t)frameVerts.size() - 1;
            aiVector3D ta = frameVerts[srcIndex];
            if (!deltaCompress || f == 0)
            {
                WriteS32BE(out, ToFixed16_16(ta.x));
                WriteS32BE(out, ToFixed16_16(ta.y));
                WriteS32BE(out, ToFixed16_16(ta.z));
            }
            else
            {
                aiVector3D d = ta - prev[vi];
                float scale = (float)(1 << deltaFracBits);
                auto toDelta = [&](float val) -> int16_t {
                    float scaled = val * scale;
                    if (scaled > 32767.0f) { anyDeltaClamp = true; scaled = 32767.0f; }
                    if (scaled < -32768.0f) { anyDeltaClamp = true; scaled = -32768.0f; }
                    return (int16_t)std::lround(scaled);
                };
                WriteS16BE(out, toDelta(d.x));
                WriteS16BE(out, toDelta(d.y));
                WriteS16BE(out, toDelta(d.z));
            }
            prev[vi] = ta;
        }
    }

    // v4 trailer: optional mesh safety info + optional embedded map payload.
    uint32_t targetMeshVertexCount = 0;
    uint32_t targetMeshHash = 0;
    if (forcedTargetMesh && forcedTargetMesh->valid)
    {
        targetMeshVertexCount = (uint32_t)forcedTargetMesh->positions.size();
        targetMeshHash = HashNebMeshLayoutCrc32(*forcedTargetMesh);
    }
    else if (forcedVertexCount > 0)
    {
        targetMeshVertexCount = forcedVertexCount;
    }
    WriteU32BE(out, targetMeshVertexCount);
    WriteU32BE(out, targetMeshHash);

    if (hasEmbeddedMap)
    {
        WriteU32BE(out, (uint32_t)sourceIndexForOutput.size());
        for (uint32_t idx : sourceIndexForOutput)
            WriteU32BE(out, idx);
    }

    if (anyDeltaClamp)
    {
        if (!warning.empty()) warning += "; ";
        warning += "Delta clamp (movement too large)";
    }

    return true;
}

// ---------------------------------------------------------------------------
// WriteRemappedNebAnim
// ---------------------------------------------------------------------------

bool WriteRemappedNebAnim(
    const std::filesystem::path& outPath,
    const NebAnimClip& clip,
    const std::vector<int>& remap,
    uint32_t newVertexCount)
{
    if (!clip.valid || clip.frames.empty() || newVertexCount == 0) return false;
    std::ofstream out(outPath, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;

    const bool deltaCompress = (clip.flags & 1u) != 0;
    const uint32_t deltaFracBits = std::min<uint32_t>(clip.deltaFracBits, 15u);
    const char magic[4] = { 'N','E','B','0' };
    uint32_t version = 4;
    uint32_t flags = deltaCompress ? 1u : 0u; // no embedded map needed — already in mesh order
    uint32_t frameCount = clip.frameCount;
    uint32_t fpsFixed = ToFixed16_16(clip.fps);

    out.write(magic, 4);
    WriteU32BE(out, version);
    WriteU32BE(out, flags);
    WriteU32BE(out, newVertexCount);
    WriteU32BE(out, frameCount);
    WriteU32BE(out, fpsFixed);
    WriteU32BE(out, deltaFracBits);

    std::vector<Vec3> prev(newVertexCount, Vec3{0,0,0});
    for (uint32_t f = 0; f < frameCount; ++f)
    {
        const std::vector<Vec3>& srcFrame = (f < clip.frames.size()) ? clip.frames[f] : clip.frames[0];
        for (uint32_t vi = 0; vi < newVertexCount; ++vi)
        {
            Vec3 pos = {0,0,0};
            int srcIdx = (vi < remap.size()) ? remap[vi] : -1;
            if (srcIdx >= 0 && srcIdx < (int)srcFrame.size())
                pos = srcFrame[srcIdx];
            else if (!srcFrame.empty())
                pos = srcFrame[0];

            if (!deltaCompress || f == 0)
            {
                WriteS32BE(out, ToFixed16_16(pos.x));
                WriteS32BE(out, ToFixed16_16(pos.y));
                WriteS32BE(out, ToFixed16_16(pos.z));
            }
            else
            {
                float dx = pos.x - prev[vi].x;
                float dy = pos.y - prev[vi].y;
                float dz = pos.z - prev[vi].z;
                float scale = (float)(1u << deltaFracBits);
                auto toDelta = [&](float val) -> int16_t {
                    float scaled = val * scale;
                    if (scaled > 32767.0f) scaled = 32767.0f;
                    if (scaled < -32768.0f) scaled = -32768.0f;
                    return (int16_t)std::lround(scaled);
                };
                WriteS16BE(out, toDelta(dx));
                WriteS16BE(out, toDelta(dy));
                WriteS16BE(out, toDelta(dz));
            }
            prev[vi] = pos;
        }
    }

    // v4 trailer (no embedded map)
    WriteU32BE(out, newVertexCount);
    WriteU32BE(out, 0u);
    return true;
}

// ---------------------------------------------------------------------------
// StageRemappedNebAnim
// ---------------------------------------------------------------------------

bool StageRemappedNebAnim(
    const std::filesystem::path& srcAnimPath,
    const std::filesystem::path& dstAnimPath,
    const std::filesystem::path& meshAbsPath)
{
    NebAnimClip clip;
    std::string err;
    if (!LoadNebAnimClip(srcAnimPath, clip, err) || !clip.valid || clip.frames.empty())
        return false;

    if (clip.meshAligned)
    {
        printf("[DreamcastBuild]   anim remap: mesh-aligned (no remap needed)\n");
        return false; // plain copy is fine — already in mesh-local space + vertex order
    }

    NebMesh mesh;
    if (!LoadNebMesh(meshAbsPath, mesh) || !mesh.valid || mesh.positions.empty())
        return false;

    uint32_t meshVC = (uint32_t)mesh.positions.size();
    uint32_t animVC = clip.vertexCount;
    if (animVC == 0 || meshVC == 0)
        return false;

    // If counts match, identity remap (no change needed).
    if (meshVC == animVC)
    {
        printf("[DreamcastBuild]   anim remap: identity (mesh==anim==%d)\n", (int)meshVC);
        return false; // plain copy is fine
    }

    // Need the v4 embedded map to do provenance-based alignment.
    if (!clip.hasEmbeddedMap || clip.embeddedMapIndices.size() != animVC)
    {
        printf("[DreamcastBuild]   anim remap: no embedded map, falling back to copy\n");
        return false;
    }

    // Build inverse map: FBX source index -> anim vertex index
    // anim_fbx[ai] = embeddedMapIndices[ai] (FBX source for each anim vertex)
    const std::vector<uint32_t>& animFbx = clip.embeddedMapIndices;
    uint32_t maxFbxSrc = 0;
    for (uint32_t ai = 0; ai < animVC; ++ai)
        if (animFbx[ai] > maxFbxSrc) maxFbxSrc = animFbx[ai];
    const uint32_t totalFbxVerts = maxFbxSrc + 1;

    std::vector<int> fbxToAnim(totalFbxVerts, -1);
    for (uint32_t ai = 0; ai < animVC; ++ai)
        fbxToAnim[animFbx[ai]] = (int)ai;

    // Find the FBX source indices that the ANIMATION excludes (its gaps).
    std::vector<uint32_t> animGaps;
    for (uint32_t s = 0; s < totalFbxVerts; ++s)
        if (fbxToAnim[s] < 0) animGaps.push_back(s);

    // The mesh has meshVC vertices from totalFbxVerts FBX sources.
    // meshVC = totalFbxVerts - meshGapCount, so meshGapCount = totalFbxVerts - meshVC.
    uint32_t meshGapCount = totalFbxVerts - meshVC;
    if (meshGapCount > animGaps.size() + 10)
    {
        printf("[DreamcastBuild]   anim remap: gap count mismatch (meshGaps=%d animGaps=%zu), falling back\n",
            (int)meshGapCount, animGaps.size());
        return false;
    }

    auto buildRemap = [&](const std::set<uint32_t>& meshGapSet) -> std::vector<int>
    {
        std::vector<int> remap(meshVC, -1);
        uint32_t mi = 0, ai = 0;
        for (uint32_t fbxSrc = 0; fbxSrc < totalFbxVerts && mi < meshVC; ++fbxSrc)
        {
            bool inMesh = (meshGapSet.find(fbxSrc) == meshGapSet.end());
            bool inAnim = (fbxToAnim[fbxSrc] >= 0);
            if (inMesh && inAnim)
            {
                remap[mi] = fbxToAnim[fbxSrc];
                ++mi;
                ++ai;
            }
            else if (inMesh && !inAnim)
            {
                remap[mi] = (ai > 0) ? (int)(ai - 1) : 0;
                ++mi;
            }
            else if (!inMesh && inAnim)
            {
                ++ai;
            }
        }
        return remap;
    };

    auto countIdentity = [](const std::vector<int>& remap) -> int
    {
        int c = 0;
        for (size_t i = 0; i < remap.size(); ++i)
            if (remap[i] == (int)i) ++c;
        return c;
    };

    std::vector<int> bestRemap;
    int bestIdentity = -1;
    uint32_t bestExtra = UINT32_MAX;

    if (meshGapCount == animGaps.size())
    {
        std::set<uint32_t> gapSet(animGaps.begin(), animGaps.end());
        bestRemap = buildRemap(gapSet);
        bestIdentity = countIdentity(bestRemap);
    }
    else if (meshGapCount < animGaps.size())
    {
        uint32_t extraCount = (uint32_t)(animGaps.size() - meshGapCount);
        if (extraCount <= 3 && animGaps.size() <= 20)
        {
            for (size_t skip = 0; skip < animGaps.size(); ++skip)
            {
                if (extraCount == 1)
                {
                    std::set<uint32_t> meshGapSet;
                    for (size_t g = 0; g < animGaps.size(); ++g)
                        if (g != skip) meshGapSet.insert(animGaps[g]);
                    auto remap = buildRemap(meshGapSet);
                    int identity = countIdentity(remap);
                    if (identity > bestIdentity)
                    {
                        bestRemap = remap;
                        bestIdentity = identity;
                        bestExtra = animGaps[skip];
                    }
                }
            }
        }
        if (bestRemap.empty())
        {
            std::set<uint32_t> meshGapSet(animGaps.begin(), animGaps.begin() + meshGapCount);
            bestRemap = buildRemap(meshGapSet);
            bestIdentity = countIdentity(bestRemap);
        }
    }
    else
    {
        printf("[DreamcastBuild]   anim remap: mesh has more gaps than anim, falling back\n");
        return false;
    }

    if (bestRemap.empty() || bestRemap.size() != meshVC)
    {
        printf("[DreamcastBuild]   anim remap: failed to build remap\n");
        return false;
    }

    printf("[DreamcastBuild]   anim remap: provenance-aligned mesh=%d anim=%d identity=%d/%d extra_fbx=%u\n",
        (int)meshVC, (int)animVC, bestIdentity, (int)meshVC, bestExtra);

    return WriteRemappedNebAnim(dstAnimPath, clip, bestRemap, meshVC);
}
