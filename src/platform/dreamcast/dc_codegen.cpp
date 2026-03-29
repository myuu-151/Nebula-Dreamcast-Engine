#include "dc_codegen.h"

#include "../../editor/project.h"
#include "../../editor/prefs.h"
#include "../../io/meta_io.h"
#include "../../io/mesh_io.h"
#include "../../io/anim_io.h"
#include "../../scene/scene_io.h"
#include "../../nodes/NodeTypes.h"
#include "build_helpers.h"
#include "../../navmesh/NavMeshBuilder.h"
#include "../../math/math_types.h"
#include "../../camera/camera3d.h"
#include "../../math/math_utils.h"
#include "../../io/texture_io.h"

#include <GLFW/glfw3.h>

#include <stdio.h>
#include <cmath>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <functional>

#include "../../editor/editor_state.h"

extern std::string gPrefDreamSdkHome;

extern bool LoadSceneFromPath(const std::filesystem::path& path, SceneData& outScene);
extern bool LoadVmuPngToMono(const std::string& path, std::string& outErr);
extern bool LoadVmuFrameData(const std::filesystem::path& inPath);

void GenerateDreamcastPackage()
{
if (gProjectDir.empty())
{
    printf("[Package] No project selected. Use File -> New Project first.\n");
}
else
{
    bool useLegacyDreamcastBuilder = false;
    if (!useLegacyDreamcastBuilder)
    {
        std::string projectDirFixed = gProjectDir;
        {
            auto replaceAll = [](std::string& s, const std::string& from, const std::string& to)
            {
                size_t pos = 0;
                while ((pos = s.find(from, pos)) != std::string::npos)
                {
                    s.replace(pos, from.size(), to);
                    pos += to.size();
                }
            };
            // Recover accidental escape-decoding in Windows paths (e.g. "\\n" -> newline)
            replaceAll(projectDirFixed, "\n", "\\n");
            replaceAll(projectDirFixed, "\r", "\\r");
            replaceAll(projectDirFixed, "\t", "\\t");
            // Also fix actual control chars that may already be present in the path.
            std::replace(projectDirFixed.begin(), projectDirFixed.end(), '\n', '\\');
            projectDirFixed.erase(std::remove(projectDirFixed.begin(), projectDirFixed.end(), '\r'), projectDirFixed.end());
            std::replace(projectDirFixed.begin(), projectDirFixed.end(), '\t', '\\');
        }

        std::filesystem::path buildDir = std::filesystem::path(projectDirFixed) / "build_dreamcast";
        std::filesystem::create_directories(buildDir);
        std::filesystem::path logPath = buildDir / "package.log";

        std::filesystem::path scriptPath = buildDir / "_nebula_build_dreamcast.bat";

        {
            auto normWinPath = [](std::string s) {
                std::replace(s.begin(), s.end(), '/', '\\');
                return s;
            };
            std::string dreamSdkHome = gPrefDreamSdkHome.empty() ? std::string("C:\\DreamSDK") : normWinPath(gPrefDreamSdkHome);
            if (std::filesystem::exists(std::filesystem::path(dreamSdkHome)))
            {
                gViewportToast = std::string("DreamSDK path hook OK: ") + dreamSdkHome;
                gViewportToastUntil = glfwGetTime() + 2.0;
            }
            else
            {
                gViewportToast = std::string("DreamSDK path missing (Preferences): ") + dreamSdkHome;
                gViewportToastUntil = glfwGetTime() + 2.8;
            }

            std::ofstream bs(scriptPath, std::ios::out | std::ios::trunc);
            if (bs.is_open())
            {
                bs << "@echo off\n";
                bs << "setlocal\n";
                bs << "set BUILD_DIR=%~dp0\n";
                bs << "set DREAMSDK_HOME=" << dreamSdkHome << "\n";
                bs << "set KOS_BASE=%DREAMSDK_HOME%\\opt\\toolchains\\dc\\kos\n";
                bs << "set KOS_CC_BASE=%DREAMSDK_HOME%\\opt\\toolchains\\dc\n";
                bs << "set MAKE_EXE=%DREAMSDK_HOME%\\usr\\bin\\make.exe\n";
                bs << "set SCRAMBLE_EXE=%DREAMSDK_HOME%\\opt\\toolchains\\dc\\kos\\utils\\scramble\\scramble.exe\n";
                bs << "set MAKEIP_EXE=%DREAMSDK_HOME%\\opt\\toolchains\\dc\\kos\\utils\\makeip\\makeip.exe\n";
                bs << "set MKISOFS_EXE=%DREAMSDK_HOME%\\usr\\bin\\mkisofs.exe\n";
                bs << "set CDI4DC_EXE=%DREAMSDK_HOME%\\usr\\bin\\cdi4dc.exe\n";
                bs << "if not exist \"%MAKE_EXE%\" ( echo [DreamcastBuild] Missing DreamSDK make: %MAKE_EXE% & exit /b 1 )\n";
                bs << "if not exist \"%KOS_BASE%\\include\\kos.h\" ( echo [DreamcastBuild] Missing DreamSDK KOS headers: %KOS_BASE%\\include\\kos.h & exit /b 1 )\n";
                bs << "if not exist \"%KOS_BASE%\\lib\\dreamcast\\libkallisti.a\" ( echo [DreamcastBuild] Missing DreamSDK KOS runtime lib: %KOS_BASE%\\lib\\dreamcast\\libkallisti.a & exit /b 1 )\n";
                bs << "if not exist \"%DREAMSDK_HOME%\\tmp\" mkdir \"%DREAMSDK_HOME%\\tmp\"\n";
                bs << "set TMP=%DREAMSDK_HOME%\\tmp\n";
                bs << "set TEMP=%DREAMSDK_HOME%\\tmp\n";
                bs << "set PATH=%DREAMSDK_HOME%\\opt\\toolchains\\dc\\sh-elf\\bin;%DREAMSDK_HOME%\\usr\\bin;%DREAMSDK_HOME%\\mingw64\\bin;%PATH%\n";
                bs << "set DREAMSDK_HOME_MSYS=%DREAMSDK_HOME:\\=/%\n";
                bs << "if \"%DREAMSDK_HOME_MSYS:~1,1%\"==\":\" set DREAMSDK_HOME_MSYS=/%DREAMSDK_HOME_MSYS:~0,1%%DREAMSDK_HOME_MSYS:~2%\n";
                bs << "set KOS_BASE_MSYS=%DREAMSDK_HOME_MSYS%/opt/toolchains/dc/kos\n";
                bs << "set KOS_CC_BASE_MSYS=%DREAMSDK_HOME_MSYS%/opt/toolchains/dc\n";
                bs << "pushd \"%BUILD_DIR%\"\n";
                bs << "if exist \"*.o\" del /q *.o >nul 2>nul\n";
                bs << "if exist \"scripts\" for /r scripts %%f in (*.o) do del /q \"%%f\" >nul 2>nul\n";
                bs << "\"%MAKE_EXE%\" -f Makefile.dreamcast KOS_BASE=\"%KOS_BASE_MSYS%\" KOS_CC_BASE=\"%KOS_CC_BASE_MSYS%\" TMP=\"%TMP%\" TEMP=\"%TEMP%\"\n";
                bs << "if errorlevel 1 exit /b 1\n";
                bs << "sh-elf-objcopy -O binary nebula_dreamcast.elf nebula_dreamcast.bin\n";
                bs << "if errorlevel 1 exit /b 1\n";
                bs << "if exist \"%SCRAMBLE_EXE%\" (\n";
                bs << "  \"%SCRAMBLE_EXE%\" nebula_dreamcast.bin 1ST_READ.BIN\n";
                bs << ") else (\n";
                bs << "  copy /Y nebula_dreamcast.bin 1ST_READ.BIN >nul\n";
                bs << ")\n";
                bs << "if errorlevel 1 exit /b 1\n";
                bs << "if not exist ip.txt (\n";
                bs << "  >ip.txt echo Hardware ID   : SEGA SEGAKATANA\n";
                bs << "  >>ip.txt echo Maker ID      : SEGA ENTERPRISES\n";
                bs << "  >>ip.txt echo Device Info   : CD-ROM1/1\n";
                bs << "  >>ip.txt echo Area Symbols  : JUE\n";
                bs << "  >>ip.txt echo Peripherals   : E000F10\n";
                bs << "  >>ip.txt echo Product No    : T-00000\n";
                bs << "  >>ip.txt echo Version       : V1.000\n";
                bs << "  >>ip.txt echo Release Date  : 20260218\n";
                bs << "  >>ip.txt echo Boot Filename : 1ST_READ.BIN\n";
                bs << "  >>ip.txt echo SW Maker Name : NEBULA\n";
                bs << "  >>ip.txt echo Game Title    : NEBULA DREAMCAST\n";
                bs << ")\n";
                bs << "if exist \"%MAKEIP_EXE%\" \"%MAKEIP_EXE%\" -f ip.txt IP.BIN\n";
                bs << "if not exist cd_root mkdir cd_root\n";
                bs << "copy /Y 1ST_READ.BIN cd_root\\1ST_READ.BIN >nul\n";
                bs << "if not exist cd_root\\data mkdir cd_root\\data\n";
                bs << "if not exist cd_root\\data\\meshes mkdir cd_root\\data\\meshes\n";
                bs << "if not exist cd_root\\data\\textures mkdir cd_root\\data\\textures\n";
                bs << "if not exist cd_root\\data\\scenes mkdir cd_root\\data\\scenes\n";
                bs << "if exist \"%MKISOFS_EXE%\" if exist IP.BIN \"%MKISOFS_EXE%\" -C 0,11702 -V NEBULA_DC -G IP.BIN -l -o nebula_dreamcast.iso cd_root\n";
                bs << "if exist \"%CDI4DC_EXE%\" if exist nebula_dreamcast.iso \"%CDI4DC_EXE%\" nebula_dreamcast.iso nebula_dreamcast.cdi\n";
                bs << "popd\n";
                bs << "exit /b 0\n";
            }
        }

        if (std::filesystem::exists(scriptPath))
        {
            std::filesystem::path makefilePath = buildDir / "Makefile.dreamcast";
            std::filesystem::path runtimeCPath = buildDir / "main.c";
            std::filesystem::path entryCPath = buildDir / "entry.c";
            std::filesystem::path gameStubPath = buildDir / "NebulaGameStub.c";

            // Dreamcast script policy: compile project-local C scripts only.
            // Source root: <Project>/Scripts (recursive), staged under build_dreamcast/scripts.
            std::filesystem::path projectScriptsDir = std::filesystem::path(gProjectDir) / "Scripts";
            std::filesystem::path buildScriptsDir = buildDir / "scripts";
            std::vector<std::string> scriptSourcesForMake;
            int scriptDiscoveredC = 0;
            int scriptCopiedC = 0;
            int scriptIgnoredCpp = 0;
            {
                std::error_code recEc;
                std::filesystem::remove_all(buildScriptsDir, recEc);
                std::filesystem::create_directories(buildScriptsDir, recEc);
                if (std::filesystem::exists(projectScriptsDir))
                {
                    std::filesystem::recursive_directory_iterator it(projectScriptsDir, recEc), end;
                    for (; !recEc && it != end; it.increment(recEc))
                    {
                        if (recEc) break;
                        const auto& srcPath = it->path();
                        std::error_code stEc;
                        if (!std::filesystem::is_regular_file(srcPath, stEc)) continue;

                        std::string ext = srcPath.extension().string();
                        for (char& ch : ext) ch = (char)std::tolower((unsigned char)ch);
                        if (ext == ".c")
                        {
                            ++scriptDiscoveredC;
                            std::error_code relEc;
                            std::filesystem::path rel = std::filesystem::relative(srcPath, projectScriptsDir, relEc);
                            if (relEc) continue;
                            std::filesystem::path dst = buildScriptsDir / rel;
                            std::error_code mkEc;
                            std::filesystem::create_directories(dst.parent_path(), mkEc);
                            std::error_code cpEc;
                            std::filesystem::copy_file(srcPath, dst, std::filesystem::copy_options::overwrite_existing, cpEc);
                            if (!cpEc)
                            {
                                std::string relSrc = (std::filesystem::path("scripts") / rel).string();
                                std::replace(relSrc.begin(), relSrc.end(), '\\', '/');
                                scriptSourcesForMake.push_back(relSrc);
                                ++scriptCopiedC;
                            }
                        }
                        else if (ext == ".cpp" || ext == ".cc" || ext == ".cxx")
                        {
                            ++scriptIgnoredCpp;
                        }
                    }
                }
            }

            std::vector<Audio3DNode> exportNodes = gAudio3DNodes;
            std::vector<StaticMesh3DNode> exportStatics = gStaticMeshNodes;
            std::vector<Camera3DNode> exportCameras = gCamera3DNodes;
            std::string cameraSourceScene = "(editor)";
            if (gActiveScene >= 0 && gActiveScene < (int)gOpenScenes.size() && !gOpenScenes[gActiveScene].path.empty())
                cameraSourceScene = gOpenScenes[gActiveScene].path.lexically_normal().generic_string();

            if (gActiveScene >= 0 && gActiveScene < (int)gOpenScenes.size())
            {
                gOpenScenes[gActiveScene].nodes = gAudio3DNodes;
                gOpenScenes[gActiveScene].staticMeshes = gStaticMeshNodes;
                gOpenScenes[gActiveScene].cameras = gCamera3DNodes;
                gOpenScenes[gActiveScene].node3d = gNode3DNodes;
                gOpenScenes[gActiveScene].navMeshes = gNavMesh3DNodes;
            }

            std::string defaultSceneCfg = GetProjectDefaultScene(std::filesystem::path(gProjectDir));
            if (!defaultSceneCfg.empty())
            {
                std::filesystem::path defaultScenePath(defaultSceneCfg);
                if (defaultScenePath.is_relative())
                    defaultScenePath = std::filesystem::path(gProjectDir) / defaultScenePath;

                bool foundOpen = false;
                for (const auto& s : gOpenScenes)
                {
                    if (s.path == defaultScenePath)
                    {
                        exportNodes = s.nodes;
                        exportStatics = s.staticMeshes;
                        exportCameras = s.cameras;
                        cameraSourceScene = defaultScenePath.lexically_normal().generic_string();
                        foundOpen = true;
                        break;
                    }
                }

                if (!foundOpen)
                {
                    SceneData loaded{};
                    if (LoadSceneFromPath(defaultScenePath, loaded))
                    {
                        exportNodes = loaded.nodes;
                        exportStatics = loaded.staticMeshes;
                        exportCameras = loaded.cameras;
                        cameraSourceScene = defaultScenePath.lexically_normal().generic_string();
                    }
                }
            }

            Camera3DNode camSrc{};
            bool haveCam = false;
            // Match editor selection logic as closely as possible:
            // 1) prefer main camera with highest priority
            // 2) otherwise highest priority camera
            // 3) legacy-name fallback (Camera3D1/Camera3D*)
            int bestMainPri = -2147483647;
            int bestAnyPri = -2147483647;
            int bestMainIdx = -1;
            int bestAnyIdx = -1;
            int legacyIdx = -1;
            for (int i = 0; i < (int)exportCameras.size(); ++i)
            {
                const auto& c = exportCameras[i];
                if (c.main && c.priority > bestMainPri) { bestMainPri = c.priority; bestMainIdx = i; }
                if (c.priority > bestAnyPri) { bestAnyPri = c.priority; bestAnyIdx = i; }
                if (legacyIdx < 0 && (c.name == "Camera3D1" || c.name == "Camera3D" || c.name.rfind("Camera3D", 0) == 0)) legacyIdx = i;
            }
            int chosenCamIdx = (bestMainIdx >= 0) ? bestMainIdx : ((bestAnyIdx >= 0) ? bestAnyIdx : legacyIdx);
            if (chosenCamIdx >= 0)
            {
                camSrc = exportCameras[chosenCamIdx];
                haveCam = true;
            }
            if (!haveCam && !exportCameras.empty()) { camSrc = exportCameras[0]; haveCam = true; }
            if (!haveCam) { camSrc.x = 0.f; camSrc.y = 2.f; camSrc.z = -6.f; camSrc.rotX = 0.f; camSrc.rotY = 0.f; camSrc.rotZ = 0.f; }
            printf("[DreamcastBuild] Camera selection: haveCam=%d exportCameras=%d chosenIdx=%d\n", haveCam?1:0, (int)exportCameras.size(), chosenCamIdx);
            printf("[DreamcastBuild]   camSrc: name='%s' pos=(%.3f,%.3f,%.3f) rot=(%.3f,%.3f,%.3f) orbit=(%.3f,%.3f,%.3f) fov=%.1f main=%d pri=%d parent='%s'\n",
                camSrc.name.c_str(), camSrc.x, camSrc.y, camSrc.z,
                camSrc.rotX, camSrc.rotY, camSrc.rotZ,
                camSrc.orbitX, camSrc.orbitY, camSrc.orbitZ,
                camSrc.fovY, camSrc.main?1:0, camSrc.priority, camSrc.parent.c_str());

            StaticMesh3DNode meshSrc{};
            bool haveMesh = false;
            // Runtime currently renders one primary StaticMesh3D.
            // Prefer mesh parented under a Node3D (player mesh), then first non-cube, then first mesh.
            for (const auto& s : exportStatics)
            {
                if (!s.parent.empty() && s.mesh.find("cube_primitive") == std::string::npos)
                {
                    meshSrc = s;
                    haveMesh = true;
                    break;
                }
            }
            if (!haveMesh)
            {
                for (const auto& s : exportStatics)
                {
                    if (s.mesh.find("cube_primitive") == std::string::npos)
                    {
                        meshSrc = s;
                        haveMesh = true;
                        break;
                    }
                }
            }
            if (!haveMesh && !exportStatics.empty()) { meshSrc = exportStatics[0]; haveMesh = true; }
            if (!haveMesh) { meshSrc.x = 0.f; meshSrc.y = 0.f; meshSrc.z = 0.f; meshSrc.scaleX = 1.f; meshSrc.scaleY = 1.f; meshSrc.scaleZ = 1.f; }

            // Resolve camera world transform using the same logic as the editor play mode
            // (GetCamera3DWorldTR), but operating on export data instead of globals.
            auto getCamera3DWorldTR_export = [&](const Camera3DNode& cam,
                float& ox, float& oy, float& oz,
                float& orx, float& ory, float& orz)
            {
                auto rotateOffsetEuler = [](float& x, float& y, float& z, float rxDeg, float ryDeg, float rzDeg)
                {
                    const float rx = rxDeg * 3.14159f / 180.0f;
                    const float ry = ryDeg * 3.14159f / 180.0f;
                    const float rz = rzDeg * 3.14159f / 180.0f;
                    const float sx = sinf(rx), cx = cosf(rx);
                    const float sy = sinf(ry), cy = cosf(ry);
                    const float sz = sinf(rz), cz = cosf(rz);
                    float ty = y * cx - z * sx;
                    float tz = y * sx + z * cx;
                    y = ty; z = tz;
                    float tx = x * cy + z * sy;
                    tz = -x * sy + z * cy;
                    x = tx; z = tz;
                    tx = x * cz - y * sz;
                    ty = x * sz + y * cz;
                    x = tx; y = ty;
                };

                ox = cam.x; oy = cam.y; oz = cam.z;
                if (!cam.parent.empty())
                {
                    ox += cam.orbitX;
                    oy += cam.orbitY;
                    oz += cam.orbitZ;
                }
                orx = cam.rotX; ory = cam.rotY; orz = cam.rotZ;

                std::string p = cam.parent;
                int guard = 0;
                while (!p.empty() && guard++ < 256)
                {
                    bool found = false;
                    for (const auto& a : gAudio3DNodes)
                    {
                        if (a.name == p)
                        {
                            ox += a.x; oy += a.y; oz += a.z;
                            p = a.parent;
                            found = true;
                            break;
                        }
                    }
                    if (found) continue;
                    for (const auto& s : exportStatics)
                    {
                        if (s.name == p)
                        {
                            rotateOffsetEuler(ox, oy, oz, s.rotX, s.rotY, s.rotZ);
                            ox += s.x; oy += s.y; oz += s.z;
                            orx += s.rotX; ory += s.rotY; orz += s.rotZ;
                            p = s.parent;
                            found = true;
                            break;
                        }
                    }
                    if (found) continue;
                    for (const auto& pc : exportCameras)
                    {
                        if (pc.name == p)
                        {
                            rotateOffsetEuler(ox, oy, oz, pc.rotX, pc.rotY, pc.rotZ);
                            ox += pc.x; oy += pc.y; oz += pc.z;
                            orx += pc.rotX; ory += pc.rotY; orz += pc.rotZ;
                            p = pc.parent;
                            found = true;
                            break;
                        }
                    }
                    if (found) continue;
                    for (const auto& n : gNode3DNodes)
                    {
                        if (n.name == p)
                        {
                            rotateOffsetEuler(ox, oy, oz, n.rotX, n.rotY, n.rotZ);
                            ox += n.x; oy += n.y; oz += n.z;
                            orx += n.rotX; ory += n.rotY; orz += n.rotZ;
                            p = n.parent;
                            found = true;
                            break;
                        }
                    }
                    if (!found) break;
                }
            };

            float dcWorldX, dcWorldY, dcWorldZ, dcWorldRX, dcWorldRY, dcWorldRZ;
            getCamera3DWorldTR_export(camSrc, dcWorldX, dcWorldY, dcWorldZ, dcWorldRX, dcWorldRY, dcWorldRZ);

            Camera3D dcCam = BuildCamera3DFromLegacyEuler(
                camSrc.name,
                camSrc.parent,
                dcWorldX, dcWorldY, dcWorldZ,
                dcWorldRX, dcWorldRY, dcWorldRZ,
                camSrc.perspective,
                camSrc.fovY,
                camSrc.nearZ,
                camSrc.farZ,
                camSrc.orthoWidth,
                camSrc.priority,
                camSrc.main);

            NebulaCamera3D::View dcView = NebulaCamera3D::BuildView(dcCam);
            NebulaCamera3D::Projection dcProj = NebulaCamera3D::BuildProjection(dcCam, 640.0f / 570.0f);

            const float dcViewW = 640.0f;
            const float dcViewH = 570.0f;
            const float dcFocalY = (dcViewH * 0.5f) / std::max(1.0e-4f, std::tanf(dcProj.fovYRad * 0.5f));
            const float dcFocalX = dcFocalY * dcProj.aspect;

            printf("[DreamcastBuild] Camera: eye=(%.3f,%.3f,%.3f) target=(%.3f,%.3f,%.3f) fwd=(%.3f,%.3f,%.3f) up=(%.3f,%.3f,%.3f)\n",
                dcView.eye.x, dcView.eye.y, dcView.eye.z,
                dcView.target.x, dcView.target.y, dcView.target.z,
                dcView.basis.forward.x, dcView.basis.forward.y, dcView.basis.forward.z,
                dcView.basis.up.x, dcView.basis.up.y, dcView.basis.up.z);
            printf("[DreamcastBuild] Projection: fovY=%.1f aspect=%.3f near=%.3f far=%.1f viewW=%.0f viewH=%.0f focalX=%.1f focalY=%.1f\n",
                dcProj.fovYDeg, dcProj.aspect, dcProj.nearZ, dcProj.farZ,
                dcViewW, dcViewH, dcFocalX, dcFocalY);

            std::vector<Vec3> runtimeVerts;
            std::vector<Vec3> runtimeUvs;
            std::vector<uint16_t> runtimeIndices;
            std::vector<Vec3> runtimeTriUvs;
            std::vector<uint16_t> runtimeTriMat;
            {
                std::filesystem::path meshAbs;
                if (!meshSrc.mesh.empty()) meshAbs = std::filesystem::path(gProjectDir) / meshSrc.mesh;

                const NebMesh* nm = meshAbs.empty() ? nullptr : GetNebMesh(meshAbs);
                if (nm && !nm->positions.empty() && nm->indices.size() >= 3)
                {
                    runtimeVerts = nm->positions;
                    runtimeIndices = nm->indices;
                    runtimeUvs.resize(runtimeVerts.size(), Vec3{ 0.0f, 0.0f, 0.0f });
                    if (nm->uvs.size() == nm->positions.size())
                    {
                        for (size_t i = 0; i < runtimeVerts.size(); ++i) runtimeUvs[i] = nm->uvs[i];
                    }

                    if (nm->hasFaceRecords && !nm->faceRecords.empty())
                    {
                        runtimeIndices.clear();
                        runtimeTriUvs.clear();
                        for (const auto& fr : nm->faceRecords)
                        {
                            int ar = (fr.arity >= 3 && fr.arity <= 4) ? (int)fr.arity : 3;
                            if (ar == 3)
                            {
                                runtimeIndices.push_back(fr.indices[0]);
                                runtimeIndices.push_back(fr.indices[1]);
                                runtimeIndices.push_back(fr.indices[2]);
                                runtimeTriUvs.push_back(fr.uvs[0]);
                                runtimeTriUvs.push_back(fr.uvs[1]);
                                runtimeTriUvs.push_back(fr.uvs[2]);
                                runtimeTriMat.push_back(fr.material);
                            }
                            else
                            {
                                uint16_t q[4] = { fr.indices[0], fr.indices[1], fr.indices[2], fr.indices[3] };
                                Vec3 uv[4] = { fr.uvs[0], fr.uvs[1], fr.uvs[2], fr.uvs[3] };

                                // Mirror parity correction + phase alignment (ported from Saturn canonical quad handling).
                                bool mirrored = false;
                                if (q[0] < runtimeVerts.size() && q[1] < runtimeVerts.size() && q[2] < runtimeVerts.size() && q[3] < runtimeVerts.size())
                                {
                                    const Vec3& p0 = runtimeVerts[q[0]];
                                    const Vec3& p1 = runtimeVerts[q[1]];
                                    const Vec3& p2 = runtimeVerts[q[2]];
                                    const Vec3& p3 = runtimeVerts[q[3]];
                                    Vec3 e10{ p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
                                    Vec3 e30{ p3.x - p0.x, p3.y - p0.y, p3.z - p0.z };
                                    Vec3 e20{ p2.x - p0.x, p2.y - p0.y, p2.z - p0.z };
                                    Vec3 n{ e10.y * e20.z - e10.z * e20.y, e10.z * e20.x - e10.x * e20.z, e10.x * e20.y - e10.y * e20.x };
                                    Vec3 cx{ e10.y * e30.z - e10.z * e30.y, e10.z * e30.x - e10.x * e30.z, e10.x * e30.y - e10.y * e30.x };
                                    float geomDet = cx.x * n.x + cx.y * n.y + cx.z * n.z;

                                    float du1 = uv[1].x - uv[0].x;
                                    float dv1 = uv[1].y - uv[0].y;
                                    float du2 = uv[3].x - uv[0].x;
                                    float dv2 = uv[3].y - uv[0].y;
                                    float uvDet = du1 * dv2 - dv1 * du2;
                                    mirrored = ((geomDet * uvDet) < 0.0f);
                                }

                                if (mirrored)
                                {
                                    std::swap(q[0], q[1]);
                                    std::swap(q[2], q[3]);
                                    std::swap(uv[0], uv[1]);
                                    std::swap(uv[2], uv[3]);
                                }

                                float uMin = uv[0].x, vMin = uv[0].y;
                                for (int k = 1; k < 4; ++k) { uMin = std::min(uMin, uv[k].x); vMin = std::min(vMin, uv[k].y); }
                                int bestRot = 0; float bestScore = 1e30f;
                                for (int r = 0; r < 4; ++r)
                                {
                                    float du = uv[r].x - uMin;
                                    float dv = uv[r].y - vMin;
                                    float score = du * du + dv * dv;
                                    if (score < bestScore) { bestScore = score; bestRot = r; }
                                }

                                uint16_t rq[4] = { q[bestRot & 3], q[(bestRot + 1) & 3], q[(bestRot + 2) & 3], q[(bestRot + 3) & 3] };
                                Vec3 ruv[4] = { uv[bestRot & 3], uv[(bestRot + 1) & 3], uv[(bestRot + 2) & 3], uv[(bestRot + 3) & 3] };

                                const int fan[2][3] = { {0,1,2}, {0,2,3} };
                                for (int f = 0; f < 2; ++f)
                                {
                                    int i0 = fan[f][0], i1 = fan[f][1], i2 = fan[f][2];
                                    runtimeIndices.push_back(rq[i0]);
                                    runtimeIndices.push_back(rq[i1]);
                                    runtimeIndices.push_back(rq[i2]);
                                    runtimeTriUvs.push_back(ruv[i0]);
                                    runtimeTriUvs.push_back(ruv[i1]);
                                    runtimeTriUvs.push_back(ruv[i2]);
                                    runtimeTriMat.push_back(fr.material);
                                }
                            }
                        }
                    }
                }
                else
                {
                    runtimeVerts = {
                        {-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},{0.5f,0.5f,-0.5f},{-0.5f,0.5f,-0.5f},
                        {-0.5f,-0.5f, 0.5f},{0.5f,-0.5f, 0.5f},{0.5f,0.5f, 0.5f},{-0.5f,0.5f, 0.5f}
                    };
                    runtimeIndices = {0,1,2, 0,2,3, 4,7,6, 4,6,5, 0,4,5, 0,5,1, 3,2,6, 3,6,7, 1,5,6, 1,6,2, 0,3,7, 0,7,4};
                    runtimeUvs = {
                        {0,1,0},{1,1,0},{1,0,0},{0,0,0},
                        {0,1,0},{1,1,0},{1,0,0},{0,0,0}
                    };
                }

                if (runtimeUvs.size() != runtimeVerts.size())
                    runtimeUvs.resize(runtimeVerts.size(), Vec3{ 0.0f, 0.0f, 0.0f });

                if (runtimeTriUvs.size() != runtimeIndices.size())
                {
                    runtimeTriUvs.clear();
                    runtimeTriUvs.reserve(runtimeIndices.size());
                    for (size_t ii = 0; ii < runtimeIndices.size(); ++ii)
                    {
                        uint16_t vi = runtimeIndices[ii];
                        runtimeTriUvs.push_back((vi < runtimeUvs.size()) ? runtimeUvs[vi] : Vec3{ 0.0f, 0.0f, 0.0f });
                    }
                }

                const size_t triCountLocal = runtimeIndices.size() / 3;
                if (runtimeTriMat.size() != triCountLocal)
                {
                    runtimeTriMat.clear();
                    runtimeTriMat.resize(triCountLocal, 0);
                    if (!meshAbs.empty())
                    {
                        const NebMesh* nm2 = GetNebMesh(meshAbs);
                        if (nm2 && nm2->faceMaterial.size() == triCountLocal)
                        {
                            for (size_t ti = 0; ti < triCountLocal; ++ti) runtimeTriMat[ti] = nm2->faceMaterial[ti];
                        }
                    }
                }

                // Safety fallback: some imported meshes collapse face-corner UVs to unit-square corners
                // (0/1 only) which causes severe per-triangle checker distortion. If UV diversity is implausibly
                // low for a larger indexed mesh, prefer indexed vertex UVs.
                if (runtimeIndices.size() >= 96 && runtimeTriUvs.size() == runtimeIndices.size())
                {
                    std::set<uint32_t> uvKeys;
                    for (const auto& uv : runtimeTriUvs)
                    {
                        int u = (int)std::lround(uv.x * 256.0f);
                        int v = (int)std::lround(uv.y * 256.0f);
                        uint32_t key = ((uint32_t)(u & 0xFFFF) << 16) | (uint32_t)(v & 0xFFFF);
                        uvKeys.insert(key);
                        if (uvKeys.size() > 8) break;
                    }
                    if (uvKeys.size() <= 4)
                    {
                        // Collapsed UV fallback: generate per-triangle dominant-axis planar UVs.
                        float minX = runtimeVerts.empty() ? 0.0f : runtimeVerts[0].x;
                        float maxX = minX;
                        float minY = runtimeVerts.empty() ? 0.0f : runtimeVerts[0].y;
                        float maxY = minY;
                        float minZ = runtimeVerts.empty() ? 0.0f : runtimeVerts[0].z;
                        float maxZ = minZ;
                        for (const auto& p : runtimeVerts)
                        {
                            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
                            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
                            minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z);
                        }
                        float invX = (maxX - minX) > 1e-6f ? (1.0f / (maxX - minX)) : 1.0f;
                        float invY = (maxY - minY) > 1e-6f ? (1.0f / (maxY - minY)) : 1.0f;
                        float invZ = (maxZ - minZ) > 1e-6f ? (1.0f / (maxZ - minZ)) : 1.0f;

                        runtimeTriUvs.clear();
                        runtimeTriUvs.reserve(runtimeIndices.size());
                        for (size_t ii = 0; ii + 2 < runtimeIndices.size(); ii += 3)
                        {
                            uint16_t ia = runtimeIndices[ii + 0];
                            uint16_t ib = runtimeIndices[ii + 1];
                            uint16_t ic = runtimeIndices[ii + 2];
                            Vec3 pa = (ia < runtimeVerts.size()) ? runtimeVerts[ia] : Vec3{ 0,0,0 };
                            Vec3 pb = (ib < runtimeVerts.size()) ? runtimeVerts[ib] : Vec3{ 0,0,0 };
                            Vec3 pc = (ic < runtimeVerts.size()) ? runtimeVerts[ic] : Vec3{ 0,0,0 };

                            Vec3 e1{ pb.x - pa.x, pb.y - pa.y, pb.z - pa.z };
                            Vec3 e2{ pc.x - pa.x, pc.y - pa.y, pc.z - pa.z };
                            Vec3 n{ e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
                            float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);

                            auto uvFrom = [&](const Vec3& p)->Vec3 {
                                if (ax >= ay && ax >= az) return Vec3{ (p.z - minZ) * invZ, 1.0f - (p.y - minY) * invY, 0.0f };
                                if (ay >= az)             return Vec3{ (p.x - minX) * invX, 1.0f - (p.z - minZ) * invZ, 0.0f };
                                return Vec3{ (p.x - minX) * invX, 1.0f - (p.y - minY) * invY, 0.0f };
                            };

                            runtimeTriUvs.push_back(uvFrom(pa));
                            runtimeTriUvs.push_back(uvFrom(pb));
                            runtimeTriUvs.push_back(uvFrom(pc));
                        }
                    }
                }
            }

            // Normalize triangle material ids to dense slot indices (0..N-1)
            // so imported meshes with sparse/original material ids map correctly to
            // editor-assigned material slot rows.
            {
                // Build stable mapping by ascending material id so slot rows match
                // imported slot ordering (slot1->mat0, slot2->mat1, ...).
                std::set<int> uniqueMats;
                for (uint16_t tm : runtimeTriMat) uniqueMats.insert((int)tm);

                std::unordered_map<int, int> triMatToSlot;
                int nextSlot = 0;
                for (int tm : uniqueMats)
                {
                    int assigned = nextSlot < kStaticMeshMaterialSlots ? nextSlot : (kStaticMeshMaterialSlots - 1);
                    triMatToSlot[tm] = assigned;
                    if (nextSlot < kStaticMeshMaterialSlots) nextSlot++;
                }

                for (size_t ti = 0; ti < runtimeTriMat.size(); ++ti)
                {
                    int tm = (int)runtimeTriMat[ti];
                    auto it = triMatToSlot.find(tm);
                    runtimeTriMat[ti] = (uint16_t)((it != triMatToSlot.end()) ? it->second : 0);
                }
            }

            int runtimeSlotCount = 1;
            for (uint16_t tm : runtimeTriMat) runtimeSlotCount = std::max(runtimeSlotCount, (int)tm + 1);
            runtimeSlotCount = std::max(1, std::min(runtimeSlotCount, kStaticMeshMaterialSlots));

            std::vector<int> runtimeSlotW((size_t)runtimeSlotCount, 64);
            std::vector<int> runtimeSlotH((size_t)runtimeSlotCount, 64);
            std::vector<float> runtimeSlotUScale((size_t)runtimeSlotCount, 1.0f);
            std::vector<float> runtimeSlotVScale((size_t)runtimeSlotCount, 1.0f);
            std::vector<int> runtimeSlotFilter((size_t)runtimeSlotCount, 1); // 0=nearest, 1=bilinear
            std::vector<std::string> runtimeSlotDiskName((size_t)runtimeSlotCount);
            // KOS strict: no per-slot material UV transform state in generated runtime.
            std::vector<std::vector<uint16_t>> runtimeSlotTex((size_t)runtimeSlotCount);
            auto fillCheckerSlot = [&](int si)
            {
                runtimeSlotW[(size_t)si] = 64;
                runtimeSlotH[(size_t)si] = 64;
                runtimeSlotUScale[(size_t)si] = 1.0f;
                runtimeSlotVScale[(size_t)si] = 1.0f;
                runtimeSlotFilter[(size_t)si] = 0; // checker/debug textures look best nearest
                // KOS strict: no material UV transform state.
                runtimeSlotTex[(size_t)si].assign(64 * 64, 0);
                int r = 180 + (si * 29) % 60;
                int g = 180 + (si * 47) % 60;
                int b = 180 + (si * 13) % 60;
                for (int y = 0; y < 64; ++y)
                {
                    for (int x = 0; x < 64; ++x)
                    {
                        int c = ((x >> 3) ^ (y >> 3)) & 1;
                        int rr = c ? 255 : r;
                        int gg = c ? 255 : g;
                        int bb = c ? 255 : b;
                        runtimeSlotTex[(size_t)si][(size_t)y * 64 + (size_t)x] = (uint16_t)(((rr >> 3) << 11) | ((gg >> 2) << 5) | (bb >> 3));
                    }
                }
            };

            auto loadNebTexNative = [&](const std::filesystem::path& texAbs, int& outW, int& outH, float& outUS, float& outVS, std::vector<uint16_t>& outPix)->bool
            {
                std::ifstream tin(texAbs, std::ios::binary | std::ios::in);
                if (!tin.is_open()) return false;
                char mag[4];
                if (!tin.read(mag, 4)) return false;
                if (!(mag[0] == 'N' && mag[1] == 'E' && mag[2] == 'B' && mag[3] == 'T')) return false;
                uint16_t w = 0, h = 0, fmt = 0, flg = 0;
                if (!ReadU16BE(tin, w) || !ReadU16BE(tin, h) || !ReadU16BE(tin, fmt) || !ReadU16BE(tin, flg)) return false;
                if (fmt != 1 || w == 0 || h == 0) return false;

                auto nextPow2 = [](int v) { int p = 1; while (p < v && p < 1024) p <<= 1; return p; };
                int tw = nextPow2((int)w);
                int th = nextPow2((int)h);
                if (tw <= 0 || th <= 0 || tw > 1024 || th > 1024) return false;

                std::vector<uint16_t> src((size_t)w * (size_t)h);
                for (size_t i = 0; i < src.size(); ++i)
                {
                    uint16_t p = 0;
                    if (!ReadU16BE(tin, p)) return false;
                    uint8_t r5 = (uint8_t)((p >> 10) & 0x1F);
                    uint8_t g5 = (uint8_t)((p >> 5) & 0x1F);
                    uint8_t b5 = (uint8_t)(p & 0x1F);
                    uint16_t g6 = (uint16_t)((g5 << 1) | (g5 >> 4));
                    uint16_t rgb565 = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
                    src[i] = rgb565;
                }

                int npotMode = NebulaAssets::LoadNebTexSaturnNpotMode(texAbs); // 0=pad, 1=resample
                outW = tw; outH = th;
                outPix.assign((size_t)tw * (size_t)th, 0);

                if (npotMode == 1 && (w != tw || h != th))
                {
                    // Resample source into full POT texture domain.
                    outUS = 1.0f;
                    outVS = 1.0f;
                    for (int y = 0; y < th; ++y)
                    {
                        int sy = std::min((int)h - 1, (int)((int64_t)y * (int64_t)h / std::max(1, th)));
                        for (int x = 0; x < tw; ++x)
                        {
                            int sx = std::min((int)w - 1, (int)((int64_t)x * (int64_t)w / std::max(1, tw)));
                            outPix[(size_t)y * (size_t)tw + (size_t)x] = src[(size_t)sy * (size_t)w + (size_t)sx];
                        }
                    }
                }
                else
                {
                    // Pad mode: preserve original texels, only fill top-left authored region.
                    outUS = (float)w / (float)tw;
                    outVS = (float)h / (float)th;
                    for (int y = 0; y < (int)h; ++y)
                    {
                        for (int x = 0; x < (int)w; ++x)
                        {
                            outPix[(size_t)y * (size_t)tw + (size_t)x] = src[(size_t)y * (size_t)w + (size_t)x];
                        }
                    }
                }
                return true;
            };

            for (int si = 0; si < runtimeSlotCount; ++si)
            {
                bool loadedSlot = false;
                std::string matRef;
                if (si >= 0 && si < kStaticMeshMaterialSlots) matRef = meshSrc.materialSlots[si];
                if (matRef.empty() && si == 0) matRef = meshSrc.material;

                if (!matRef.empty())
                {
                    std::filesystem::path matAbs = std::filesystem::path(gProjectDir) / matRef;
                    std::filesystem::path texAbs;
                    if (matAbs.extension() == ".nebmat")
                    {
                        // KOS strict branch: ignore editor/Saturn-era UV transform fields in .nebmat.
                        std::string texRel;
                        if (NebulaAssets::LoadMaterialTexture(matAbs, texRel) && !texRel.empty()) texAbs = std::filesystem::path(gProjectDir) / texRel;
                    }
                    else if (matAbs.extension() == ".nebtex")
                    {
                        texAbs = matAbs;
                    }

                    if (!texAbs.empty() && std::filesystem::exists(texAbs))
                    {
                        loadedSlot = loadNebTexNative(texAbs, runtimeSlotW[(size_t)si], runtimeSlotH[(size_t)si], runtimeSlotUScale[(size_t)si], runtimeSlotVScale[(size_t)si], runtimeSlotTex[(size_t)si]);
                        if (texAbs.extension() == ".nebtex")
                        {
                            runtimeSlotFilter[(size_t)si] = NebulaAssets::LoadNebTexFilterMode(texAbs);
                            runtimeSlotDiskName[(size_t)si] = texAbs.filename().string();
                        }
                    }
                }

                if (!loadedSlot) fillCheckerSlot(si);

                // slot mapping debug intentionally omitted in this path
            }

            std::vector<int> runtimeSlotFmt((size_t)runtimeSlotCount, 0); // 0=twiddled, 1=nontwiddled
            std::vector<std::vector<uint16_t>> runtimeSlotTexUpload((size_t)runtimeSlotCount);
            auto isPow2 = [](int v)->bool { return v > 0 && (v & (v - 1)) == 0; };
            for (int si = 0; si < runtimeSlotCount; ++si)
            {
                int w = runtimeSlotW[(size_t)si], h = runtimeSlotH[(size_t)si];
                const auto& src = runtimeSlotTex[(size_t)si];
                auto& dst = runtimeSlotTexUpload[(size_t)si];
                (void)isPow2; (void)w; (void)h;
                runtimeSlotFmt[(size_t)si] = 1; // KOS strict validation path: force linear/nontwiddled for all slots
                dst = src; // keep linear source order
            }

            std::string runtimeMeshDiskName;
            if (!meshSrc.mesh.empty())
                runtimeMeshDiskName = std::filesystem::path(meshSrc.mesh).filename().string();

            std::filesystem::path cdDataDir = buildDir / "cd_root" / "data";
            std::filesystem::path cdScenesDir = cdDataDir / "scenes";
            std::filesystem::path cdMeshesDir = cdDataDir / "meshes";
            std::filesystem::path cdTexturesDir = cdDataDir / "textures";
            std::filesystem::path cdMatDir = cdDataDir / "materials";
            std::filesystem::path cdAnimDir = cdDataDir / "animations";
            std::filesystem::path cdVmuDir = cdDataDir / "vmu";
            std::filesystem::path cdNavDir = cdDataDir / "navmesh";
            std::error_code stageEc;
            std::filesystem::remove_all(cdScenesDir, stageEc);
            std::filesystem::remove_all(cdMeshesDir, stageEc);
            std::filesystem::remove_all(cdTexturesDir, stageEc);
            std::filesystem::remove_all(cdMatDir, stageEc);
            std::filesystem::remove_all(cdAnimDir, stageEc);
            std::filesystem::remove_all(cdVmuDir, stageEc);
            std::filesystem::remove_all(cdNavDir, stageEc);
            std::filesystem::create_directories(cdScenesDir);
            std::filesystem::create_directories(cdMeshesDir);
            std::filesystem::create_directories(cdTexturesDir);
            std::filesystem::create_directories(cdMatDir);
            std::filesystem::create_directories(cdAnimDir);
            std::filesystem::create_directories(cdVmuDir);
            std::filesystem::create_directories(cdNavDir);

            auto normalizeAbsKey = [](const std::filesystem::path& in)->std::string
            {
                std::error_code ec;
                std::filesystem::path p = std::filesystem::weakly_canonical(in, ec);
                if (ec) p = std::filesystem::absolute(in, ec);
                if (ec) p = in;
                return p.lexically_normal().generic_string();
            };
            auto stageUpperDiskNameFromAbsKey = [](const std::string& absKey)->std::string
            {
                std::string out = std::filesystem::path(absKey).filename().string();
                if (out.empty()) out = "ASSET";
                std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::toupper(c); });
                return out;
            };
            auto stageShortDiskNameFromAbsKey = [](const std::string& absKey, const char* prefix, int ordinal)->std::string
            {
                std::string ext = std::filesystem::path(absKey).extension().string();
                if (ext.empty()) ext = ".BIN";
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::toupper(c); });
                char stem[16];
                snprintf(stem, sizeof(stem), "%s%05d", (prefix && *prefix) ? prefix : "A", std::max(0, ordinal));
                return std::string(stem) + ext;
            };

            std::vector<std::filesystem::path> sourceScenes;
            {
                std::set<std::string> dedup;
                if (!defaultSceneCfg.empty())
                {
                    std::filesystem::path p(defaultSceneCfg);
                    if (p.is_relative()) p = std::filesystem::path(gProjectDir) / p;
                    std::string k = std::filesystem::weakly_canonical(p).generic_string();
                    dedup.insert(k);
                    sourceScenes.push_back(p);
                }
                std::filesystem::path scenesRoot = std::filesystem::path(gProjectDir) / "Assets" / "Scenes";
                if (std::filesystem::exists(scenesRoot))
                {
                    for (const auto& e : std::filesystem::recursive_directory_iterator(scenesRoot))
                    {
                        if (!e.is_regular_file() || e.path().extension() != ".nebscene") continue;
                        std::string k = std::filesystem::weakly_canonical(e.path()).generic_string();
                        if (dedup.insert(k).second) sourceScenes.push_back(e.path());
                    }
                }
            }
            if (sourceScenes.empty())
            {
                std::filesystem::path fallbackScene = buildDir / "_runtime_default.nebscene";
                NebulaScene::SaveSceneToPath(fallbackScene, exportNodes, exportStatics, exportCameras, gNode3DNodes);
                sourceScenes.push_back(fallbackScene);
            }

            struct LoadedSceneForStage
            {
                std::filesystem::path sourcePath;
                SceneData data;
            };
            std::vector<LoadedSceneForStage> loadedScenes;
            loadedScenes.reserve(sourceScenes.size());
            for (const auto& scenePath : sourceScenes)
            {
                LoadedSceneForStage ls{};
                ls.sourcePath = scenePath;
                if (!LoadSceneFromPath(scenePath, ls.data)) continue;
                loadedScenes.push_back(std::move(ls));
            }

            // Overlay in-memory editor scene data so unsaved/dirty mesh changes are included in DC build.
            // This keeps staged .NEBSCENE content aligned with what user currently sees in editor.
            {
                auto normPathKey = [](const std::filesystem::path& p) -> std::string
                {
                    std::error_code ec;
                    auto c = std::filesystem::weakly_canonical(p, ec);
                    std::string k = (ec ? p.lexically_normal() : c).generic_string();
                    std::transform(k.begin(), k.end(), k.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
                    return k;
                };

                std::unordered_map<std::string, int> loadedIdxByKey;
                for (int i = 0; i < (int)loadedScenes.size(); ++i)
                {
                    loadedIdxByKey[normPathKey(loadedScenes[i].sourcePath)] = i;
                }

                for (const auto& os : gOpenScenes)
                {
                    if (os.path.empty()) continue;
                    std::string key = normPathKey(os.path);
                    auto it = loadedIdxByKey.find(key);
                    if (it != loadedIdxByKey.end())
                    {
                        loadedScenes[it->second].data = os;
                        loadedScenes[it->second].sourcePath = os.path;
                    }
                }
            }

            // Filter staged scripts to only those referenced by nodes in
            // loaded scenes.  Prevents multiple-definition linker errors
            // when Scripts/ contains several .c files that each define
            // NB_Game_OnStart / OnUpdate / OnSceneSwitch.
            {
                std::set<std::string> referencedScripts;
                for (const auto& ls : loadedScenes)
                {
                    for (const auto& sm : ls.data.staticMeshes)
                    {
                        if (!sm.script.empty())
                        {
                            std::string fn = std::filesystem::path(sm.script).filename().string();
                            referencedScripts.insert(fn);
                        }
                    }
                    for (const auto& nd : ls.data.node3d)
                    {
                        if (!nd.script.empty())
                        {
                            std::string fn = std::filesystem::path(nd.script).filename().string();
                            referencedScripts.insert(fn);
                        }
                    }
                }
                if (!referencedScripts.empty())
                {
                    std::vector<std::string> filtered;
                    for (const auto& src : scriptSourcesForMake)
                    {
                        std::string fn = std::filesystem::path(src).filename().string();
                        if (referencedScripts.count(fn))
                            filtered.push_back(src);
                    }
                    scriptSourcesForMake = filtered;
                    // Remove unreferenced script files from build dir
                    std::error_code rmEc;
                    if (std::filesystem::exists(buildScriptsDir))
                    {
                        for (const auto& e : std::filesystem::directory_iterator(buildScriptsDir, rmEc))
                        {
                            if (rmEc) break;
                            if (!e.is_regular_file()) continue;
                            std::string fn = e.path().filename().string();
                            if (!referencedScripts.count(fn))
                                std::filesystem::remove(e.path(), rmEc);
                        }
                    }
                }
            }

            // DC multi-script: rename NB_Game_On* hooks to indexed versions
            // so multiple scripts can coexist in one binary without symbol collisions.
            // Each script gets NB_Game_OnStart_N, NB_Game_OnUpdate_N, NB_Game_OnSceneSwitch_N.
            int dcScriptCount = (int)scriptSourcesForMake.size();
            if (dcScriptCount > 1)
            {
                for (int si = 0; si < dcScriptCount; ++si)
                {
                    std::filesystem::path srcFile = buildDir / scriptSourcesForMake[si];
                    if (!std::filesystem::exists(srcFile)) continue;
                    std::ifstream ifs(srcFile);
                    if (!ifs.is_open()) continue;
                    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                    ifs.close();
                    // Prepend #define redirects to rename hooks
                    std::string prefix = "/* Auto-generated hook rename for DC multi-script */\n"
                        "#define NB_Game_OnStart NB_Game_OnStart_" + std::to_string(si) + "\n"
                        "#define NB_Game_OnUpdate NB_Game_OnUpdate_" + std::to_string(si) + "\n"
                        "#define NB_Game_OnSceneSwitch NB_Game_OnSceneSwitch_" + std::to_string(si) + "\n\n";
                    std::ofstream ofs(srcFile, std::ios::out | std::ios::trunc);
                    if (ofs.is_open())
                    {
                        ofs << prefix << content;
                    }
                }
            }

            // Bake parent hierarchy into world transforms for DC export.
            // The DC runtime applies transforms flat (no parent chain walking),
            // so we must flatten here to match what the editor renders.
            for (auto& ls : loadedScenes)
            {
                auto findStaticByName = [&](const std::string& nm) -> int {
                    for (int i = 0; i < (int)ls.data.staticMeshes.size(); ++i)
                        if (ls.data.staticMeshes[i].name == nm) return i;
                    return -1;
                };
                auto findNode3DByName = [&](const std::string& nm) -> int {
                    for (int i = 0; i < (int)ls.data.node3d.size(); ++i)
                        if (ls.data.node3d[i].name == nm) return i;
                    return -1;
                };
                for (int i = 0; i < (int)ls.data.staticMeshes.size(); ++i)
                {
                    auto& sm = ls.data.staticMeshes[i];
                    float wx = sm.x, wy = sm.y, wz = sm.z;
                    float wrx = sm.rotX, wry = sm.rotY, wrz = sm.rotZ;
                    float wsx = sm.scaleX, wsy = sm.scaleY, wsz = sm.scaleZ;
                    std::string p = sm.parent;
                    int guard = 0;
                    while (!p.empty() && guard++ < 256)
                    {
                        int pi = findStaticByName(p);
                        if (pi >= 0)
                        {
                            const auto& pn = ls.data.staticMeshes[pi];
                            wx += pn.x; wy += pn.y; wz += pn.z;
                            wrx += pn.rotX; wry += pn.rotY; wrz += pn.rotZ;
                            wsx *= pn.scaleX; wsy *= pn.scaleY; wsz *= pn.scaleZ;
                            p = pn.parent;
                            continue;
                        }
                        int ni = findNode3DByName(p);
                        if (ni >= 0)
                        {
                            const auto& pn = ls.data.node3d[ni];
                            wx += pn.x; wy += pn.y; wz += pn.z;
                            wrx += pn.rotX; wry += pn.rotY; wrz += pn.rotZ;
                            wsx *= pn.scaleX; wsy *= pn.scaleY; wsz *= pn.scaleZ;
                            p = pn.parent;
                            continue;
                        }
                        break;
                    }
                    sm.x = wx; sm.y = wy; sm.z = wz;
                    sm.rotX = wrx; sm.rotY = wry; sm.rotZ = wrz;
                    sm.scaleX = wsx; sm.scaleY = wsy; sm.scaleZ = wsz;
                }
            }

            std::set<std::string> sortedMeshAbs;
            std::set<std::string> sortedTexAbs;
            std::set<std::string> sortedMatAbs;
            std::set<std::string> sortedAnimAbs;
            std::unordered_map<std::string, std::string> meshLogicalByAbs;
            std::unordered_map<std::string, std::string> texLogicalByAbs;
            std::unordered_map<std::string, std::string> animLogicalByAbs;
            std::unordered_map<std::string, std::string> animMeshAbsByAnimAbs; // animKey -> meshKey
            printf("[DreamcastBuild] loadedScenes count: %d\n", (int)loadedScenes.size());
            for (const auto& ls : loadedScenes)
            {
                printf("[DreamcastBuild]   scene '%s': %d staticMeshes, %d node3d\n",
                    ls.sourcePath.string().c_str(),
                    (int)ls.data.staticMeshes.size(),
                    (int)ls.data.node3d.size());
                for (const auto& sm : ls.data.staticMeshes)
                {
                    if (!sm.mesh.empty())
                    {
                        std::filesystem::path meshAbs = std::filesystem::path(gProjectDir) / sm.mesh;
                        if (std::filesystem::exists(meshAbs))
                        {
                            std::string key = normalizeAbsKey(meshAbs);
                            sortedMeshAbs.insert(key);
                            if (meshLogicalByAbs.find(key) == meshLogicalByAbs.end())
                            {
                                std::string logical = std::filesystem::path(sm.mesh).filename().string();
                                if (logical.empty()) logical = std::filesystem::path(key).filename().string();
                                meshLogicalByAbs[key] = logical;
                            }
                        }
                    }
                    if (!sm.vtxAnim.empty())
                    {
                        std::filesystem::path animAbs = std::filesystem::path(gProjectDir) / sm.vtxAnim;
                        if (std::filesystem::exists(animAbs))
                        {
                            std::string akey = normalizeAbsKey(animAbs);
                            sortedAnimAbs.insert(akey);
                            if (!sm.mesh.empty() && animMeshAbsByAnimAbs.find(akey) == animMeshAbsByAnimAbs.end())
                            {
                                std::filesystem::path mAbs = std::filesystem::path(gProjectDir) / sm.mesh;
                                if (std::filesystem::exists(mAbs))
                                    animMeshAbsByAnimAbs[akey] = normalizeAbsKey(mAbs);
                            }
                            if (animLogicalByAbs.find(akey) == animLogicalByAbs.end())
                            {
                                std::string logical = std::filesystem::path(sm.vtxAnim).filename().string();
                                if (logical.empty()) logical = std::filesystem::path(akey).filename().string();
                                animLogicalByAbs[akey] = logical;
                            }
                        }
                    }
                    // Collect animation slot paths
                    for (int asi = 0; asi < sm.animSlotCount; ++asi)
                    {
                        if (sm.animSlots[asi].path.empty()) continue;
                        std::filesystem::path animAbs = std::filesystem::path(gProjectDir) / sm.animSlots[asi].path;
                        if (!std::filesystem::exists(animAbs)) continue;
                        std::string akey = normalizeAbsKey(animAbs);
                        sortedAnimAbs.insert(akey);
                        if (!sm.mesh.empty() && animMeshAbsByAnimAbs.find(akey) == animMeshAbsByAnimAbs.end())
                        {
                            std::filesystem::path mAbs = std::filesystem::path(gProjectDir) / sm.mesh;
                            if (std::filesystem::exists(mAbs))
                                animMeshAbsByAnimAbs[akey] = normalizeAbsKey(mAbs);
                        }
                        if (animLogicalByAbs.find(akey) == animLogicalByAbs.end())
                        {
                            std::string logical = std::filesystem::path(sm.animSlots[asi].path).filename().string();
                            if (logical.empty()) logical = std::filesystem::path(akey).filename().string();
                            animLogicalByAbs[akey] = logical;
                        }
                    }
                    for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                    {
                        std::string matRef = sm.materialSlots[si];
                        if (matRef.empty() && si == 0) matRef = sm.material;
                        if (matRef.empty()) continue;

                        std::filesystem::path matAbs = std::filesystem::path(gProjectDir) / matRef;
                        std::filesystem::path texAbs;
                        if (matAbs.extension() == ".nebmat")
                        {
                            if (std::filesystem::exists(matAbs))
                            {
                                std::string mkey = normalizeAbsKey(matAbs);
                                sortedMatAbs.insert(mkey);
                            }
                            std::string texRel;
                            if (NebulaAssets::LoadMaterialTexture(matAbs, texRel) && !texRel.empty())
                                texAbs = std::filesystem::path(gProjectDir) / texRel;
                        }
                        else if (matAbs.extension() == ".nebtex")
                        {
                            texAbs = matAbs;
                        }
                        if (!texAbs.empty() && std::filesystem::exists(texAbs))
                        {
                            std::string key = normalizeAbsKey(texAbs);
                            sortedTexAbs.insert(key);
                            if (texLogicalByAbs.find(key) == texLogicalByAbs.end())
                            {
                                std::string logical = texAbs.filename().string();
                                if (logical.empty()) logical = std::filesystem::path(key).filename().string();
                                texLogicalByAbs[key] = logical;
                            }
                        }
                    }
                }
            }

            std::unordered_map<std::string, std::string> stagedMeshByAbs;
            std::unordered_map<std::string, std::string> stagedTexByAbs;
            std::unordered_map<std::string, std::string> stagedAnimByAbs;
            std::unordered_map<std::string, std::string> stagedSceneByAbs;
            std::vector<std::pair<std::string, std::string>> meshRefMapEntries;
            std::vector<std::pair<std::string, std::string>> texRefMapEntries;
            std::vector<std::pair<std::string, std::string>> animStageMapEntries;
            bool stagingNameCollision = false;
            std::string stagingNameCollisionMessage;
            {
                std::unordered_map<std::string, std::string> meshAbsByOutName;
                int meshOrdinal = 1;
                for (const auto& key : sortedMeshAbs)
                {
                    std::string outName = stageShortDiskNameFromAbsKey(key, "M", meshOrdinal++);
                    auto hit = meshAbsByOutName.find(outName);
                    if (hit != meshAbsByOutName.end() && hit->second != key)
                    {
                        stagingNameCollision = true;
                        stagingNameCollisionMessage = std::string("[DreamcastBuild] ERROR: mesh staging filename collision: ") + outName +
                            " <= " + hit->second + " | " + key;
                        break;
                    }
                    meshAbsByOutName[outName] = key;
                    std::error_code ec;
                    std::filesystem::copy_file(std::filesystem::path(key), cdMeshesDir / outName, std::filesystem::copy_options::overwrite_existing, ec);
                    if (ec) continue;
                    stagedMeshByAbs[key] = outName;
                    meshRefMapEntries.push_back({ meshLogicalByAbs[key], outName });
                    printf("[DreamcastBuild]   staged mesh: %s -> %s\n", meshLogicalByAbs[key].c_str(), outName.c_str());
                }
                printf("[DreamcastBuild] Total meshes collected: %d, staged: %d\n", (int)sortedMeshAbs.size(), (int)stagedMeshByAbs.size());
                if (!stagingNameCollision)
                {
                    std::unordered_map<std::string, std::string> texAbsByOutName;
                    int texOrdinal = 1;
                    for (const auto& key : sortedTexAbs)
                    {
                        std::string outName = stageShortDiskNameFromAbsKey(key, "T", texOrdinal++);
                        auto hit = texAbsByOutName.find(outName);
                        if (hit != texAbsByOutName.end() && hit->second != key)
                        {
                            stagingNameCollision = true;
                            stagingNameCollisionMessage = std::string("[DreamcastBuild] ERROR: texture staging filename collision: ") + outName +
                                " <= " + hit->second + " | " + key;
                            break;
                        }
                        texAbsByOutName[outName] = key;
                        std::error_code ec;
                        std::filesystem::copy_file(std::filesystem::path(key), cdTexturesDir / outName, std::filesystem::copy_options::overwrite_existing, ec);
                        if (ec) continue;
                        // Append extension chunk (filter, wrap, flipU, flipV) from .nebtex.meta
                        {
                            std::filesystem::path srcTex(key);
                            int extFilter = NebulaAssets::LoadNebTexFilterMode(srcTex);
                            int extWrap = NebulaAssets::LoadNebTexWrapMode(srcTex);
                            bool extFlipU = false, extFlipV = false;
                            NebulaAssets::LoadNebTexFlipOptions(srcTex, extFlipU, extFlipV);
                            std::ofstream appF(cdTexturesDir / outName, std::ios::binary | std::ios::app);
                            if (appF.is_open())
                            {
                                uint8_t ext[4] = { (uint8_t)extFilter, (uint8_t)extWrap, (uint8_t)(extFlipU ? 1 : 0), (uint8_t)(extFlipV ? 1 : 0) };
                                appF.write(reinterpret_cast<const char*>(ext), 4);
                            }
                        }
                        stagedTexByAbs[key] = outName;
                        texRefMapEntries.push_back({ texLogicalByAbs[key], outName });
                    }
                }
                if (!stagingNameCollision)
                {
                    std::unordered_map<std::string, std::string> matAbsByOutName;
                    int matOrdinal = 1;
                    for (const auto& key : sortedMatAbs)
                    {
                        std::string outName = stageShortDiskNameFromAbsKey(key, "MAT", matOrdinal++);
                        auto hit = matAbsByOutName.find(outName);
                        if (hit != matAbsByOutName.end() && hit->second != key)
                        {
                            stagingNameCollision = true;
                            stagingNameCollisionMessage = std::string("[DreamcastBuild] ERROR: material staging filename collision: ") + outName +
                                " <= " + hit->second + " | " + key;
                            break;
                        }
                        matAbsByOutName[outName] = key;
                        std::error_code ec;
                        std::filesystem::copy_file(std::filesystem::path(key), cdMatDir / outName, std::filesystem::copy_options::overwrite_existing, ec);
                        if (ec) continue;
                    }
                }
                if (!stagingNameCollision)
                {
                    std::unordered_map<std::string, std::string> animAbsByOutName;
                    int animOrdinal = 1;
                    for (const auto& key : sortedAnimAbs)
                    {
                        std::string outName = stageShortDiskNameFromAbsKey(key, "A", animOrdinal++);
                        auto hit = animAbsByOutName.find(outName);
                        if (hit != animAbsByOutName.end() && hit->second != key)
                        {
                            stagingNameCollision = true;
                            stagingNameCollisionMessage = std::string("[DreamcastBuild] ERROR: animation staging filename collision: ") + outName +
                                " <= " + hit->second + " | " + key;
                            break;
                        }
                        animAbsByOutName[outName] = key;
                        std::filesystem::path dstPath = cdAnimDir / outName;
                        bool remapped = false;
                        auto meshIt = animMeshAbsByAnimAbs.find(key);
                        if (meshIt != animMeshAbsByAnimAbs.end())
                        {
                            remapped = StageRemappedNebAnim(
                                std::filesystem::path(key), dstPath,
                                std::filesystem::path(meshIt->second));
                            if (remapped)
                                printf("[DreamcastBuild]   staged anim (remapped): %s -> %s\n", key.c_str(), outName.c_str());
                        }
                        if (!remapped)
                        {
                            std::error_code ec;
                            std::filesystem::copy_file(std::filesystem::path(key), dstPath, std::filesystem::copy_options::overwrite_existing, ec);
                            if (ec) continue;
                            printf("[DreamcastBuild]   staged anim (copy): %s -> %s\n", key.c_str(), outName.c_str());
                        }
                        stagedAnimByAbs[key] = outName;
                        animStageMapEntries.push_back({ key, outName });
                    }
                }
                // Stage navmesh binary — build from all scene meshes and serialize
                if (!stagingNameCollision)
                {
                    // Build navmesh from all meshes in all loaded scenes
                    // Uses same NEBM binary format reader as editor NavMeshBuild (line ~2274)
                    // BE reader helpers (same as LoadNebMesh)
                    auto dcReadU32BE = [](std::ifstream& f, uint32_t& v) -> bool {
                        uint8_t b[4]; if (!f.read((char*)b, 4)) return false;
                        v = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
                        return true;
                    };
                    auto dcReadS16BE = [](std::ifstream& f, int16_t& v) -> bool {
                        uint8_t b[2]; if (!f.read((char*)b, 2)) return false;
                        v = (int16_t)(((uint16_t)b[0] << 8) | b[1]);
                        return true;
                    };
                    auto dcReadU16BE = [](std::ifstream& f, uint16_t& v) -> bool {
                        uint8_t b[2]; if (!f.read((char*)b, 2)) return false;
                        v = ((uint16_t)b[0] << 8) | b[1];
                        return true;
                    };

                    // Build one navmesh per scene so scene switches can load the right one
                    struct DCNavBounds { float minX, minY, minZ, maxX, maxY, maxZ; bool negator; };
                    for (int navSceneIdx = 0; navSceneIdx < (int)loadedScenes.size(); ++navSceneIdx)
                    {
                    std::vector<float> navVerts;
                    std::vector<int>   navTris;
                    std::vector<unsigned char> navTriFlags;
                    std::vector<DCNavBounds> dcNavVolumes;
                    {
                        const auto& ls = loadedScenes[navSceneIdx];
                        printf("[DreamcastBuild] navmesh: scene[%d] has %d NavMesh3D nodes\n", navSceneIdx, (int)ls.data.navMeshes.size());
                        for (const auto& nm : ls.data.navMeshes)
                        {
                            printf("[DreamcastBuild] navmesh:   NavMesh3D '%s' navBounds=%d navNeg=%d ext=(%.1f,%.1f,%.1f) pos=(%.1f,%.1f,%.1f)\n",
                                nm.name.c_str(), nm.navBounds ? 1 : 0, nm.navNegator ? 1 : 0,
                                nm.extentX, nm.extentY, nm.extentZ, nm.x, nm.y, nm.z);
                            if (!nm.navBounds && !nm.navNegator) continue;
                            float hx = nm.extentX * 0.5f;
                            float hy = nm.extentY * 0.5f;
                            float hz = nm.extentZ * 0.5f;
                            dcNavVolumes.push_back({
                                nm.x - hx, nm.y - hy, nm.z - hz,
                                nm.x + hx, nm.y + hy, nm.z + hz,
                                nm.navNegator
                            });
                        }
                    }
                    auto dcPointInNavBounds = [&](float px, float py, float pz) -> bool {
                        bool inBounds = false;
                        for (const auto& vol : dcNavVolumes)
                        {
                            if (vol.negator) continue;
                            if (px >= vol.minX && px <= vol.maxX &&
                                py >= vol.minY && py <= vol.maxY &&
                                pz >= vol.minZ && pz <= vol.maxZ)
                            { inBounds = true; break; }
                        }
                        if (!inBounds) return false;
                        for (const auto& vol : dcNavVolumes)
                        {
                            if (!vol.negator) continue;
                            if (px >= vol.minX && px <= vol.maxX &&
                                py >= vol.minY && py <= vol.maxY &&
                                pz >= vol.minZ && pz <= vol.maxZ)
                                return false;
                        }
                        return true;
                    };

                    printf("[DreamcastBuild] navmesh export: scene[%d], %d nav volumes\n", navSceneIdx, (int)dcNavVolumes.size());
                    {
                        const auto& ls = loadedScenes[navSceneIdx];
                        printf("[DreamcastBuild] navmesh: scene[%d] has %d static meshes\n", navSceneIdx, (int)ls.data.staticMeshes.size());
                        for (const auto& sm : ls.data.staticMeshes)
                        {
                            if (sm.mesh.empty()) continue;
                            std::filesystem::path meshAbs = sm.mesh;
                            if (meshAbs.is_relative())
                                meshAbs = std::filesystem::path(gProjectDir) / meshAbs;
                            std::ifstream mf(meshAbs, std::ios::binary);
                            if (!mf.is_open()) { printf("[DreamcastBuild] navmesh: FAILED to open '%s'\n", meshAbs.string().c_str()); continue; }

                            // Read NEBM header: 4-byte magic, then BE u32: version, flags, vertexCount, indexCount, posFracBits
                            char magic[4];
                            if (!mf.read(magic, 4) || magic[0] != 'N' || magic[1] != 'E' || magic[2] != 'B' || magic[3] != 'M')
                            { printf("[DreamcastBuild] navmesh: bad magic in '%s'\n", meshAbs.string().c_str()); continue; }

                            uint32_t version = 0, flags = 0, vertexCount = 0, indexCount = 0, posFracBits = 8;
                            if (!dcReadU32BE(mf, version) || !dcReadU32BE(mf, flags) || !dcReadU32BE(mf, vertexCount) ||
                                !dcReadU32BE(mf, indexCount) || !dcReadU32BE(mf, posFracBits))
                            { printf("[DreamcastBuild] navmesh: header read failed '%s'\n", meshAbs.string().c_str()); continue; }

                            // Read positions (fixed-point s16 BE)
                            float invScale = 1.0f / (float)(1 << posFracBits);
                            std::vector<float> positions(vertexCount * 3);
                            for (uint32_t v = 0; v < vertexCount; ++v)
                            {
                                int16_t px, py, pz;
                                if (!dcReadS16BE(mf, px) || !dcReadS16BE(mf, py) || !dcReadS16BE(mf, pz)) break;
                                positions[v * 3 + 0] = px * invScale;
                                positions[v * 3 + 1] = py * invScale;
                                positions[v * 3 + 2] = pz * invScale;
                            }

                            // Skip UV layers
                            bool hasUv = (flags & 1u) != 0;
                            bool hasUv1 = (flags & 16u) != 0;
                            if (hasUv) mf.seekg(vertexCount * 4, std::ios::cur);
                            if (hasUv1) mf.seekg(vertexCount * 4, std::ios::cur);

                            // Read indices (u16 BE)
                            std::vector<uint16_t> indices(indexCount);
                            for (uint32_t i = 0; i < indexCount; ++i)
                            {
                                if (!dcReadU16BE(mf, indices[i])) break;
                            }

                            if (vertexCount == 0 || indexCount == 0) continue;

                            // Transform vertices to world space
                            float sx = sm.scaleX, sy = sm.scaleY, sz = sm.scaleZ;
                            float rx = sm.rotX * 3.14159265f / 180.0f;
                            float ry = sm.rotY * 3.14159265f / 180.0f;
                            float rz = sm.rotZ * 3.14159265f / 180.0f;
                            float cxr = cosf(rx), snxr = sinf(rx);
                            float cyr = cosf(ry), snyr = sinf(ry);
                            float czr = cosf(rz), snzr = sinf(rz);

                            std::vector<float> worldPos(vertexCount * 3);
                            for (uint32_t v = 0; v < vertexCount; ++v)
                            {
                                float lx = positions[v*3+0]*sx, ly = positions[v*3+1]*sy, lz = positions[v*3+2]*sz;
                                float t1x=lx, t1y=ly*cxr-lz*snxr, t1z=ly*snxr+lz*cxr;
                                float t2x=t1x*cyr+t1z*snyr, t2y=t1y, t2z=-t1x*snyr+t1z*cyr;
                                float t3x=t2x*czr-t2y*snzr, t3y=t2x*snzr+t2y*czr, t3z=t2z;
                                worldPos[v*3+0] = t3x + sm.x;
                                worldPos[v*3+1] = t3y + sm.y;
                                worldPos[v*3+2] = t3z + sm.z;
                            }

                            // Only include triangles where at least one vertex is inside nav bounds
                            // If collisionWalls is set, skip near-vertical (wall) faces using wallThreshold
                            int addedTris = 0;
                            for (uint32_t t = 0; t + 2 < indexCount; t += 3)
                            {
                                uint16_t i0 = indices[t], i1 = indices[t + 1], i2 = indices[t + 2];
                                if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) continue;
                                float v0x = worldPos[i0*3+0], v0y = worldPos[i0*3+1], v0z = worldPos[i0*3+2];
                                float v1x = worldPos[i1*3+0], v1y = worldPos[i1*3+1], v1z = worldPos[i1*3+2];
                                float v2x = worldPos[i2*3+0], v2y = worldPos[i2*3+1], v2z = worldPos[i2*3+2];
                                bool inside = dcNavVolumes.empty() ||
                                              dcPointInNavBounds(v0x, v0y, v0z) ||
                                              dcPointInNavBounds(v1x, v1y, v1z) ||
                                              dcPointInNavBounds(v2x, v2y, v2z);
                                if (!inside) continue;

                                // Mark ALL faces on collisionWalls meshes as non-walkable obstacles
                                unsigned char flag = sm.collisionWalls ? 1 : 0;

                                int baseV = (int)(navVerts.size() / 3);
                                navVerts.push_back(v0x); navVerts.push_back(v0y); navVerts.push_back(v0z);
                                navVerts.push_back(v1x); navVerts.push_back(v1y); navVerts.push_back(v1z);
                                navVerts.push_back(v2x); navVerts.push_back(v2y); navVerts.push_back(v2z);
                                navTris.push_back(baseV);
                                navTris.push_back(baseV + 1);
                                navTris.push_back(baseV + 2);
                                navTriFlags.push_back(flag);
                                ++addedTris;
                            }
                            printf("[DreamcastBuild] navmesh:   %s — %d verts, %d/%u tris\n", sm.name.c_str(), (int)vertexCount, addedTris, indexCount / 3);
                        }
                    }
                    printf("[DreamcastBuild] navmesh: collected %d verts, %d tris\n", (int)(navVerts.size()/3), (int)(navTris.size()/3));
                    if (!navVerts.empty() && !navTris.empty())
                    {
                        bool buildOk = NavMeshBuild(navVerts.data(), (int)(navVerts.size() / 3), navTris.data(), (int)(navTris.size() / 3), NavMeshParams{}, navTriFlags.data());
                        printf("[DreamcastBuild] navmesh: NavMeshBuild returned %s\n", buildOk ? "SUCCESS" : "FAILED");
                        if (buildOk)
                        {
                            std::vector<uint8_t> blob;
                            if (NavMeshSaveBinary(blob) && !blob.empty())
                            {
                                char navFileName[32];
                                snprintf(navFileName, sizeof(navFileName), "NAV%05d.BIN", navSceneIdx + 1);
                                std::filesystem::path navOut = cdNavDir / navFileName;
                                std::ofstream nf(navOut, std::ios::binary);
                                if (nf.is_open())
                                {
                                    nf.write(reinterpret_cast<const char*>(blob.data()), blob.size());
                                    printf("[DreamcastBuild]   staged navmesh: %s (%d bytes)\n", navFileName, (int)blob.size());
                                }
                            }
                            NavMeshClear();
                        }
                    }
                } } // end per-scene navmesh loop
                if (!stagingNameCollision)
                {
                    std::unordered_map<std::string, std::string> sceneAbsByOutName;
                    int sceneOrdinal = 1;
                    for (const auto& ls : loadedScenes)
                    {
                        std::string key = normalizeAbsKey(ls.sourcePath);
                        std::string outName = stageShortDiskNameFromAbsKey(key, "S", sceneOrdinal++);
                        auto hit = sceneAbsByOutName.find(outName);
                        if (hit != sceneAbsByOutName.end() && hit->second != key)
                        {
                            stagingNameCollision = true;
                            stagingNameCollisionMessage = std::string("[DreamcastBuild] ERROR: scene staging filename collision: ") + outName +
                                " <= " + hit->second + " | " + key;
                            break;
                        }
                        sceneAbsByOutName[outName] = key;
                        stagedSceneByAbs[key] = outName;
                    }
                }
            }
            if (!meshSrc.mesh.empty())
            {
                std::string k = normalizeAbsKey(std::filesystem::path(gProjectDir) / meshSrc.mesh);
                auto it = stagedMeshByAbs.find(k);
                if (it != stagedMeshByAbs.end()) runtimeMeshDiskName = it->second;
            }

            std::vector<std::string> runtimeSceneFiles;
            std::vector<std::string> runtimeSceneNames;
            std::vector<std::vector<std::string>> runtimeSceneAnimDiskByMesh;
            std::vector<std::vector<std::string>> runtimeSceneMeshNameByMesh;
            std::vector<std::vector<uint8_t>> runtimeSceneRuntimeTestByMesh;
            std::vector<std::vector<uint8_t>> runtimeSceneCollisionSourceByMesh;
            std::vector<std::vector<float>> runtimeSceneWallThresholdByMesh;
            std::vector<std::vector<uint8_t>> runtimeSceneCollisionWallsByMesh;
            // Per-scene per-mesh material property arrays (shade/light/UV per slot)
            std::vector<std::vector<std::array<int, kStaticMeshMaterialSlots>>> runtimeSceneShadeModeByMesh;
            std::vector<std::vector<std::array<float, kStaticMeshMaterialSlots>>> runtimeSceneLightYawByMesh;
            std::vector<std::vector<std::array<float, kStaticMeshMaterialSlots>>> runtimeSceneLightPitchByMesh;
            std::vector<std::vector<std::array<float, kStaticMeshMaterialSlots>>> runtimeSceneShadowIntensityByMesh;
            std::vector<std::vector<std::array<int, kStaticMeshMaterialSlots>>> runtimeSceneShadingUvByMesh;
            std::vector<std::vector<std::array<float, kStaticMeshMaterialSlots>>> runtimeSceneUvScaleUByMesh;
            std::vector<std::vector<std::array<float, kStaticMeshMaterialSlots>>> runtimeSceneUvScaleVByMesh;
            std::vector<std::vector<std::array<float, kStaticMeshMaterialSlots>>> runtimeSceneOpacityByMesh;
            // Per-scene parent Node3D transform for the player mesh (drives movement, not visual).
            struct ScenePlayerParent { float pos[3] = {0,0,0}; float rot[3] = {0,0,0}; };
            std::vector<ScenePlayerParent> runtimeScenePlayerParent;
            // Per-scene per-mesh animation slot data
            struct AnimSlotExport { std::string name; std::string disk; float speed = 1.0f; bool loop = true; };
            std::vector<std::vector<std::vector<AnimSlotExport>>> runtimeSceneAnimSlotsByMesh; // [scene][mesh][slot]
            struct Node3DExport { std::string name; float pos[3]; float rot[3]; float scale[3]; float extent[3]; float boundPos[3]; int physicsEnabled; int collisionSource; int simpleCollision; };
            std::vector<std::vector<Node3DExport>> runtimeSceneNode3Ds;
            // Per-scene: for each mesh index, the Node3D index that is its parent (-1 if none)
            std::vector<std::vector<int>> runtimeSceneMeshParentN3D;
            std::string defaultSceneRuntimeFile;
            for (const auto& ls : loadedScenes)
            {
                const auto& scenePath = ls.sourcePath;
                const auto& stagedScene = ls.data;

                std::string sceneOutName;
                {
                    std::string sceneKey = normalizeAbsKey(scenePath);
                    auto it = stagedSceneByAbs.find(sceneKey);
                    if (it != stagedSceneByAbs.end()) sceneOutName = it->second;
                    if (sceneOutName.empty()) sceneOutName = stageUpperDiskNameFromAbsKey(sceneKey);
                }
                std::filesystem::path sceneOutPath = cdScenesDir / sceneOutName;
                std::ofstream so(sceneOutPath, std::ios::out | std::ios::trunc);
                if (!so.is_open()) continue;

                so << "scene=" << stagedScene.name << "\n";
                int sceneWrittenMeshes = 0;
                int sceneSkippedMeshes = 0;
                std::vector<std::string> sceneAnimDiskByMesh;
                std::vector<std::vector<AnimSlotExport>> sceneAnimSlotsByMesh;
                std::vector<std::string> sceneMeshNameByMesh;
                std::vector<uint8_t> sceneRuntimeTestByMesh;
                std::vector<uint8_t> sceneCollisionSourceByMesh;
                std::vector<float> sceneWallThresholdByMesh;
                std::vector<uint8_t> sceneCollisionWallsByMesh;
                std::vector<std::array<int, kStaticMeshMaterialSlots>> sceneShadeModeByMesh;
                std::vector<std::array<float, kStaticMeshMaterialSlots>> sceneLightYawByMesh;
                std::vector<std::array<float, kStaticMeshMaterialSlots>> sceneLightPitchByMesh;
                std::vector<std::array<float, kStaticMeshMaterialSlots>> sceneShadowIntensityByMesh;
                std::vector<std::array<int, kStaticMeshMaterialSlots>> sceneShadingUvByMesh;
                std::vector<std::array<float, kStaticMeshMaterialSlots>> sceneUvScaleUByMesh;
                std::vector<std::array<float, kStaticMeshMaterialSlots>> sceneUvScaleVByMesh;
                std::vector<std::array<float, kStaticMeshMaterialSlots>> sceneOpacityByMesh;
                for (const auto& sm : stagedScene.staticMeshes)
                {
                    std::filesystem::path meshAbs = std::filesystem::path(gProjectDir) / sm.mesh;
                    std::string stagedMesh;
                    {
                        std::string key = normalizeAbsKey(meshAbs);
                        auto it = stagedMeshByAbs.find(key);
                        if (it != stagedMeshByAbs.end()) stagedMesh = it->second;
                    }
                    if (stagedMesh.empty()) { printf("[DreamcastBuild]   SKIP mesh '%s' (not staged)\n", sm.name.c_str()); ++sceneSkippedMeshes; continue; }

                    std::array<std::string, kStaticMeshMaterialSlots> slotTexNames{};
                    for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                    {
                        std::string matRef = sm.materialSlots[si];
                        if (matRef.empty() && si == 0) matRef = sm.material;
                        if (matRef.empty()) continue;

                        std::filesystem::path matAbs = std::filesystem::path(gProjectDir) / matRef;
                        std::filesystem::path texAbs;
                        if (matAbs.extension() == ".nebmat")
                        {
                            std::string texRel;
                            if (NebulaAssets::LoadMaterialTexture(matAbs, texRel) && !texRel.empty())
                                texAbs = std::filesystem::path(gProjectDir) / texRel;
                        }
                        else if (matAbs.extension() == ".nebtex")
                        {
                            texAbs = matAbs;
                        }
                        if (!texAbs.empty())
                        {
                            std::string key = normalizeAbsKey(texAbs);
                            auto it = stagedTexByAbs.find(key);
                            if (it != stagedTexByAbs.end()) slotTexNames[(size_t)si] = it->second;
                        }
                    }

                    so << "staticmesh " << stagedMesh << " "
                       << sm.x << " " << sm.y << " " << sm.z << " "
                       << sm.rotX << " " << sm.rotY << " " << sm.rotZ << " "
                       << sm.scaleX << " " << sm.scaleY << " " << sm.scaleZ;
                    for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                        so << " " << (slotTexNames[(size_t)si].empty() ? "-" : slotTexNames[(size_t)si]);
                    so << "\n";
                    std::string animDisk;
                    if (!sm.vtxAnim.empty())
                    {
                        std::string akey = normalizeAbsKey(std::filesystem::path(gProjectDir) / sm.vtxAnim);
                        auto ait = stagedAnimByAbs.find(akey);
                        if (ait != stagedAnimByAbs.end()) animDisk = ait->second;
                    }
                    sceneAnimDiskByMesh.push_back(animDisk);
                    // Collect animation slots for this mesh
                    {
                        std::vector<AnimSlotExport> meshSlots;
                        for (int asi = 0; asi < sm.animSlotCount; ++asi)
                        {
                            AnimSlotExport ase;
                            ase.name = sm.animSlots[asi].name;
                            ase.speed = sm.animSlots[asi].speed;
                            ase.loop = sm.animSlots[asi].loop;
                            if (!sm.animSlots[asi].path.empty())
                            {
                                std::string akey = normalizeAbsKey(std::filesystem::path(gProjectDir) / sm.animSlots[asi].path);
                                auto ait = stagedAnimByAbs.find(akey);
                                if (ait != stagedAnimByAbs.end()) ase.disk = ait->second;
                            }
                            meshSlots.push_back(ase);
                        }
                        sceneAnimSlotsByMesh.push_back(meshSlots);
                    }
                    sceneMeshNameByMesh.push_back(sm.name);
                    sceneRuntimeTestByMesh.push_back(sm.runtimeTest ? 1u : 0u);
                    sceneCollisionSourceByMesh.push_back(sm.collisionSource ? 1u : 0u);
                    sceneWallThresholdByMesh.push_back(sm.wallThreshold);
                    sceneCollisionWallsByMesh.push_back(sm.collisionWalls ? 1u : 0u);
                    // Collect per-slot material properties for this mesh
                    {
                        std::array<int, kStaticMeshMaterialSlots> mShade{}; mShade.fill(0);
                        std::array<float, kStaticMeshMaterialSlots> mLYaw{}; mLYaw.fill(0.0f);
                        std::array<float, kStaticMeshMaterialSlots> mLPitch{}; mLPitch.fill(35.0f);
                        std::array<float, kStaticMeshMaterialSlots> mShadow{}; mShadow.fill(1.0f);
                        std::array<int, kStaticMeshMaterialSlots> mShadUv{}; mShadUv.fill(-1);
                        std::array<float, kStaticMeshMaterialSlots> mUvU{}; mUvU.fill(1.0f);
                        std::array<float, kStaticMeshMaterialSlots> mUvV{}; mUvV.fill(1.0f);
                        std::array<float, kStaticMeshMaterialSlots> mOpacity{}; mOpacity.fill(1.0f);
                        if (!gProjectDir.empty())
                        {
                            for (int msi = 0; msi < kStaticMeshMaterialSlots; ++msi)
                            {
                                std::string matRef = sm.materialSlots[msi];
                                if (matRef.empty() && msi == 0) matRef = sm.material;
                                if (matRef.empty()) continue;
                                std::filesystem::path matPath = std::filesystem::path(gProjectDir) / matRef;
                                mShade[msi] = NebulaAssets::LoadMaterialShadingMode(matPath);
                                mLYaw[msi] = NebulaAssets::LoadMaterialLightRotation(matPath);
                                mLPitch[msi] = NebulaAssets::LoadMaterialLightPitch(matPath);
                                mShadow[msi] = NebulaAssets::LoadMaterialShadowIntensity(matPath);
                                mShadUv[msi] = NebulaAssets::LoadMaterialShadingUv(matPath);
                                { float su=1,sv=1,ou=0,ov=0,rd=0; NebulaAssets::LoadMaterialUvTransform(matPath,su,sv,ou,ov,rd); mUvU[msi]=su; mUvV[msi]=sv; }
                                mOpacity[msi] = NebulaAssets::LoadMaterialOpacity(matPath);
                            }
                        }
                        sceneShadeModeByMesh.push_back(mShade);
                        sceneLightYawByMesh.push_back(mLYaw);
                        sceneLightPitchByMesh.push_back(mLPitch);
                        sceneShadowIntensityByMesh.push_back(mShadow);
                        sceneShadingUvByMesh.push_back(mShadUv);
                        sceneUvScaleUByMesh.push_back(mUvU);
                        sceneUvScaleVByMesh.push_back(mUvV);
                        sceneOpacityByMesh.push_back(mOpacity);
                    }
                    ++sceneWrittenMeshes;
                }
                printf("[DreamcastBuild] Scene '%s' -> %s: %d meshes written, %d skipped\n",
                    stagedScene.name.c_str(), sceneOutName.c_str(), sceneWrittenMeshes, sceneSkippedMeshes);
                runtimeSceneFiles.push_back(sceneOutName);
                runtimeSceneNames.push_back(stagedScene.name);
                runtimeSceneAnimDiskByMesh.push_back(std::move(sceneAnimDiskByMesh));
                runtimeSceneAnimSlotsByMesh.push_back(std::move(sceneAnimSlotsByMesh));
                runtimeSceneMeshNameByMesh.push_back(std::move(sceneMeshNameByMesh));
                runtimeSceneRuntimeTestByMesh.push_back(std::move(sceneRuntimeTestByMesh));
                runtimeSceneCollisionSourceByMesh.push_back(std::move(sceneCollisionSourceByMesh));
                runtimeSceneWallThresholdByMesh.push_back(std::move(sceneWallThresholdByMesh));
                runtimeSceneCollisionWallsByMesh.push_back(std::move(sceneCollisionWallsByMesh));
                runtimeSceneShadeModeByMesh.push_back(std::move(sceneShadeModeByMesh));
                runtimeSceneLightYawByMesh.push_back(std::move(sceneLightYawByMesh));
                runtimeSceneLightPitchByMesh.push_back(std::move(sceneLightPitchByMesh));
                runtimeSceneShadowIntensityByMesh.push_back(std::move(sceneShadowIntensityByMesh));
                runtimeSceneShadingUvByMesh.push_back(std::move(sceneShadingUvByMesh));
                runtimeSceneUvScaleUByMesh.push_back(std::move(sceneUvScaleUByMesh));
                runtimeSceneUvScaleVByMesh.push_back(std::move(sceneUvScaleVByMesh));
                runtimeSceneOpacityByMesh.push_back(std::move(sceneOpacityByMesh));
                // Find the player mesh's parent Node3D transform for this scene.
                {
                    ScenePlayerParent pp{};
                    for (const auto& sm : stagedScene.staticMeshes)
                    {
                        if (sm.mesh == meshSrc.mesh)
                        {
                            for (const auto& n3 : stagedScene.node3d)
                            {
                                if (n3.name == sm.parent)
                                {
                                    pp.pos[0] = n3.x; pp.pos[1] = n3.y; pp.pos[2] = n3.z;
                                    pp.rot[0] = n3.rotX; pp.rot[1] = n3.rotY; pp.rot[2] = n3.rotZ;
                                    break;
                                }
                            }
                            break;
                        }
                    }
                    runtimeScenePlayerParent.push_back(pp);
                }
                // Collect all Node3D data for this scene (for DC multi-Node3D physics)
                {
                    std::vector<Node3DExport> sceneNode3Ds;
                    // Build name→Node3D index map
                    std::unordered_map<std::string, int> n3dNameToIdx;
                    for (size_t ni = 0; ni < stagedScene.node3d.size(); ++ni)
                    {
                        const auto& n3 = stagedScene.node3d[ni];
                        Node3DExport ne{};
                        ne.name = n3.name;
                        ne.pos[0] = n3.x; ne.pos[1] = n3.y; ne.pos[2] = n3.z;
                        ne.rot[0] = n3.rotX; ne.rot[1] = n3.rotY; ne.rot[2] = n3.rotZ;
                        ne.scale[0] = n3.scaleX; ne.scale[1] = n3.scaleY; ne.scale[2] = n3.scaleZ;
                        ne.extent[0] = n3.extentX * n3.scaleX; ne.extent[1] = n3.extentY * n3.scaleY; ne.extent[2] = n3.extentZ * n3.scaleZ;
                        ne.boundPos[0] = n3.boundPosX; ne.boundPos[1] = n3.boundPosY; ne.boundPos[2] = n3.boundPosZ;
                        ne.physicsEnabled = n3.physicsEnabled ? 1 : 0;
                        ne.collisionSource = n3.collisionSource ? 1 : 0;
                        ne.simpleCollision = n3.simpleCollision ? 1 : 0;
                        n3dNameToIdx[n3.name] = (int)ni;
                        sceneNode3Ds.push_back(ne);
                    }
                    // Build per-mesh parent Node3D index (-1 if none)
                    std::vector<int> meshParentN3D;
                    {
                        int meshIdx = 0;
                        for (const auto& sm : stagedScene.staticMeshes)
                        {
                            std::string meshKey = normalizeAbsKey(std::filesystem::path(gProjectDir) / sm.mesh);
                            auto it = stagedMeshByAbs.find(meshKey);
                            if (it == stagedMeshByAbs.end() || it->second.empty()) continue; // skipped mesh
                            int parentIdx = -1;
                            if (!sm.parent.empty())
                            {
                                auto pit = n3dNameToIdx.find(sm.parent);
                                if (pit != n3dNameToIdx.end()) parentIdx = pit->second;
                            }
                            meshParentN3D.push_back(parentIdx);
                            ++meshIdx;
                        }
                    }
                    runtimeSceneNode3Ds.push_back(std::move(sceneNode3Ds));
                    runtimeSceneMeshParentN3D.push_back(std::move(meshParentN3D));
                }
                if (defaultSceneRuntimeFile.empty()) defaultSceneRuntimeFile = sceneOutName;
            }
            if (runtimeSceneFiles.empty())
            {
                runtimeSceneFiles.push_back("DEFAULT.NEBSCENE");
                runtimeSceneNames.push_back("Default");
                runtimeSceneAnimDiskByMesh.push_back({});
                runtimeSceneAnimSlotsByMesh.push_back({});
                runtimeSceneMeshNameByMesh.push_back({});
                runtimeSceneRuntimeTestByMesh.push_back({});
                runtimeSceneCollisionSourceByMesh.push_back({});
                runtimeSceneWallThresholdByMesh.push_back({});
                runtimeSceneCollisionWallsByMesh.push_back({});
                runtimeScenePlayerParent.push_back(ScenePlayerParent{});
                runtimeSceneNode3Ds.push_back({});
                runtimeSceneMeshParentN3D.push_back({});
                std::ofstream so(cdScenesDir / "DEFAULT.NEBSCENE", std::ios::out | std::ios::trunc);
                if (so.is_open())
                {
                    so << "scene=Default\n";
                    for (const auto& ls : loadedScenes)
                    {
                        for (const auto& sm : ls.data.staticMeshes)
                        {
                            std::filesystem::path meshAbs = std::filesystem::path(gProjectDir) / sm.mesh;
                            std::string stagedMesh;
                            {
                                std::string key = normalizeAbsKey(meshAbs);
                                auto it = stagedMeshByAbs.find(key);
                                if (it != stagedMeshByAbs.end()) stagedMesh = it->second;
                            }
                            if (stagedMesh.empty()) continue;

                            std::array<std::string, kStaticMeshMaterialSlots> slotTexNames{};
                            for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                            {
                                std::string matRef = sm.materialSlots[si];
                                if (matRef.empty() && si == 0) matRef = sm.material;
                                if (matRef.empty()) continue;

                                std::filesystem::path matAbs = std::filesystem::path(gProjectDir) / matRef;
                                std::filesystem::path texAbs;
                                if (matAbs.extension() == ".nebmat")
                                {
                                    std::string texRel;
                                    if (NebulaAssets::LoadMaterialTexture(matAbs, texRel) && !texRel.empty())
                                        texAbs = std::filesystem::path(gProjectDir) / texRel;
                                }
                                else if (matAbs.extension() == ".nebtex")
                                {
                                    texAbs = matAbs;
                                }
                                if (!texAbs.empty())
                                {
                                    std::string key = normalizeAbsKey(texAbs);
                                    auto it = stagedTexByAbs.find(key);
                                    if (it != stagedTexByAbs.end()) slotTexNames[(size_t)si] = it->second;
                                }
                            }

                            so << "staticmesh " << stagedMesh << " "
                               << sm.x << " " << sm.y << " " << sm.z << " "
                               << sm.rotX << " " << sm.rotY << " " << sm.rotZ << " "
                               << sm.scaleX << " " << sm.scaleY << " " << sm.scaleZ;
                            for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
                                so << " " << (slotTexNames[(size_t)si].empty() ? "-" : slotTexNames[(size_t)si]);
                            so << "\n";
                        }
                    }
                }
                defaultSceneRuntimeFile = "DEFAULT.NEBSCENE";
            }

            std::ofstream mc(runtimeCPath, std::ios::out | std::ios::trunc);
            if (mc.is_open())
            {
                std::array<uint8_t, 48 * 32 / 8> vmuBootPacked{};
                std::array<uint8_t, 48 * 32> vmuSourceMono = gVmuMono;
                bool vmuSourceReady = gVmuHasImage;
                if (gVmuLoadOnBoot)
                {
                    // Runtime source selection from persistent links: PNG link first, then linked FrameData layer range.
                    std::string vmuErr;
                    if (!gVmuLinkedPngPath.empty())
                    {
                        if (LoadVmuPngToMono(gVmuLinkedPngPath, vmuErr))
                        {
                            vmuSourceMono = gVmuMono;
                            vmuSourceReady = gVmuHasImage;
                        }
                    }
                    else
                    {
                        if (!gVmuLinkedAnimPath.empty())
                        {
                            LoadVmuFrameData(gVmuLinkedAnimPath);
                        }
                        std::string activeLayerAsset;
                        for (int li = 0; li < (int)gVmuAnimLayers.size(); ++li)
                        {
                            const VmuAnimLayer& l = gVmuAnimLayers[(size_t)li];
                            if (!l.visible || l.linkedAsset.empty()) continue;
                            if (gVmuAnimPlayhead < l.frameStart || gVmuAnimPlayhead > l.frameEnd) continue;
                            activeLayerAsset = l.linkedAsset;
                        }
                        if (!activeLayerAsset.empty() && LoadVmuPngToMono(activeLayerAsset, vmuErr))
                        {
                            vmuSourceMono = gVmuMono;
                            vmuSourceReady = gVmuHasImage;
                        }
                    }
                }

                // Build baked VMU animation frames from linked layer ranges for Dreamcast runtime.
                std::vector<std::array<uint8_t, 48 * 32 / 8>> vmuAnimPacked;
                int vmuAnimEnabled = 0;
                int vmuAnimFrameCount = std::max(1, gVmuAnimTotalFrames);
                int vmuAnimLoopEnabled = gVmuAnimLoop ? 1 : 0;
                int vmuAnimSpeedCode = std::max(0, std::min(gVmuAnimSpeedMode, 3));
                {
                    auto savedMono = gVmuMono;
                    bool savedHas = gVmuHasImage;
                    std::string savedPath = gVmuAssetPath;

                    bool hasAnyLinkedLayer = false;
                    for (const auto& l : gVmuAnimLayers)
                        if (!l.linkedAsset.empty() && l.visible) { hasAnyLinkedLayer = true; break; }

                    if (gVmuLoadOnBoot && hasAnyLinkedLayer)
                    {
                        vmuAnimEnabled = 1;
                        vmuAnimPacked.resize((size_t)vmuAnimFrameCount);
                        for (int f = 0; f < vmuAnimFrameCount; ++f)
                        {
                            std::array<uint8_t, 48 * 32 / 8> out{};
                            std::string activeAsset;
                            for (int li = 0; li < (int)gVmuAnimLayers.size(); ++li)
                            {
                                const VmuAnimLayer& l = gVmuAnimLayers[(size_t)li];
                                if (!l.visible || l.linkedAsset.empty()) continue;
                                if (f < l.frameStart || f > l.frameEnd) continue;
                                activeAsset = l.linkedAsset;
                            }

                            if (!activeAsset.empty())
                            {
                                std::string err;
                                if (LoadVmuPngToMono(activeAsset, err))
                                {
                                    for (int py = 0; py < 32; ++py)
                                    {
                                        for (int px = 0; px < 48; ++px)
                                        {
                                            if (gVmuMono[(size_t)py * 48u + (size_t)px])
                                            {
                                                const int dstY = 31 - py;
                                                const int dstX = 47 - px;
                                                const int bi = dstY * 6 + (dstX >> 3);
                                                const int bit = 7 - (dstX & 7);
                                                out[(size_t)bi] |= (uint8_t)(1u << bit);
                                            }
                                        }
                                    }
                                }
                            }
                            vmuAnimPacked[(size_t)f] = out;
                        }
                    }

                    gVmuMono = savedMono;
                    gVmuHasImage = savedHas;
                    gVmuAssetPath = savedPath;
                }

                const int vmuBootEnabled = (gVmuLoadOnBoot && vmuSourceReady) ? 1 : 0;
                if (vmuBootEnabled)
                {
                    for (int py = 0; py < 32; ++py)
                    {
                        for (int px = 0; px < 48; ++px)
                        {
                            if (vmuSourceMono[(size_t)py * 48u + (size_t)px])
                            {
                                const int dstY = 31 - py; // VMU LCD memory is vertically inverted vs editor preview.
                                const int dstX = 47 - px; // VMU LCD horizontal orientation fix.
                                const int bi = dstY * 6 + (dstX >> 3);
                                const int bit = 7 - (dstX & 7);
                                vmuBootPacked[(size_t)bi] |= (uint8_t)(1u << bit);
                            }
                        }
                    }
                }

                // Disk-only VMU payload staging for runtime reads.
                {
                    std::filesystem::path vmuBootPath = cdVmuDir / "vmu_boot.bin";
                    std::ofstream vb(vmuBootPath, std::ios::binary | std::ios::out | std::ios::trunc);
                    if (vb.is_open()) vb.write((const char*)vmuBootPacked.data(), (std::streamsize)vmuBootPacked.size());

                    std::filesystem::path vmuAnimPath = cdVmuDir / "vmu_anim.bin";
                    std::ofstream va(vmuAnimPath, std::ios::binary | std::ios::out | std::ios::trunc);
                    if (va.is_open() && !vmuAnimPacked.empty())
                    {
                        for (const auto& fr : vmuAnimPacked)
                            va.write((const char*)fr.data(), (std::streamsize)fr.size());
                    }
                }

                mc << "#include <kos.h>\n";
                mc << "#include <dc/pvr.h>\n";
                mc << "#include <dc/maple.h>\n";
                mc << "#include <dc/maple/vmu.h>\n";
                mc << "#include <math.h>\n";
                mc << "#include <stdio.h>\n";
                mc << "#include <stdlib.h>\n";
                mc << "#include <string.h>\n";
                mc << "#include <stdint.h>\n";
                mc << "#include \"KosInput.h\"\n";
                mc << "#include \"KosBindings.h\"\n";
                mc << "#include \"DetourBridge.h\"\n";
                mc << "\n";
                if (dcScriptCount > 1)
                {
                    for (int si = 0; si < dcScriptCount; ++si)
                    {
                        mc << "extern void NB_Game_OnStart_" << si << "(void);\n";
                        mc << "extern void NB_Game_OnUpdate_" << si << "(float dt);\n";
                        mc << "extern void NB_Game_OnSceneSwitch_" << si << "(const char* sceneName);\n";
                    }
                    mc << "static void NB_Game_OnStart(void) {";
                    for (int si = 0; si < dcScriptCount; ++si)
                        mc << " NB_Game_OnStart_" << si << "();";
                    mc << " }\n";
                    mc << "static void NB_Game_OnUpdate(float dt) {";
                    for (int si = 0; si < dcScriptCount; ++si)
                        mc << " NB_Game_OnUpdate_" << si << "(dt);";
                    mc << " }\n";
                    mc << "static void NB_Game_OnSceneSwitch(const char* sceneName) {";
                    for (int si = 0; si < dcScriptCount; ++si)
                        mc << " NB_Game_OnSceneSwitch_" << si << "(sceneName);";
                    mc << " }\n";
                }
                else
                {
                    mc << "extern void NB_Game_OnStart(void);\n";
                    mc << "extern void NB_Game_OnUpdate(float dt);\n";
                    mc << "extern void NB_Game_OnSceneSwitch(const char* sceneName);\n";
                }
                mc << "\n";
                mc << "KOS_INIT_FLAGS(INIT_DEFAULT);\n";
                mc << "\n";
                mc << "typedef NB_Vec3 V3;\n";
                mc << "typedef struct { float x,y,z,u,v; } SV;\n";
                mc << "typedef NB_Mesh RuntimeMesh;\n";
                mc << "typedef NB_Texture RuntimeTex;\n";
                mc << "static inline unsigned twid(unsigned x, unsigned y) { unsigned z=0; for (unsigned b=0; b<16; ++b) { z |= ((x>>b)&1u) << (2u*b); z |= ((y>>b)&1u) << (2u*b+1u); } return z; }\n";
                mc << "static char dc_uc(char c){ return (c>='a'&&c<='z')?(char)(c-('a'-'A')):c; }\n";
                mc << "static int dc_eq_nocase(const char* a, const char* b){ if(!a||!b) return 0; while(*a&&*b){ if(dc_uc(*a)!=dc_uc(*b)) return 0; ++a; ++b; } return (*a==0&&*b==0)?1:0; }\n";
                mc << "static FILE* dc_try_open_rb(const char* cand, char* outPath, int outPathSize){ FILE* f=fopen(cand,\"rb\"); if(f&&outPath&&outPathSize>0){ strncpy(outPath,cand,(size_t)outPathSize-1u); outPath[(size_t)outPathSize-1u]=0; } return f; }\n";
                mc << "static FILE* dc_fopen_iso_compat(const char* path, char* outPath, int outPathSize){ if(!path||!path[0]) return 0; char cand[512]; FILE* f=0; strncpy(cand,path,sizeof(cand)-1u); cand[sizeof(cand)-1u]=0; f=dc_try_open_rb(cand,outPath,outPathSize); if(f) return f; strncpy(cand,path,sizeof(cand)-1u); cand[sizeof(cand)-1u]=0; for(size_t i=0;cand[i];++i) cand[i]=dc_uc(cand[i]); f=dc_try_open_rb(cand,outPath,outPathSize); if(f) return f; strncpy(cand,path,sizeof(cand)-1u); cand[sizeof(cand)-1u]=0; char* slash=strrchr(cand,'/'); char* bname=slash?slash+1:cand; char* dot=strrchr(bname,'.'); if(dot&&dc_eq_nocase(dot,\".nebmesh\")){ for(char* p=bname;p<dot;++p) *p=dc_uc(*p); strcpy(dot,\".NEBMESH\"); f=dc_try_open_rb(cand,outPath,outPathSize); if(f) return f; strcpy(dot,\".nebmesh\"); f=dc_try_open_rb(cand,outPath,outPathSize); if(f) return f; } else if(dot&&dc_eq_nocase(dot,\".nebtex\")){ for(char* p=bname;p<dot;++p) *p=dc_uc(*p); strcpy(dot,\".NEBTEX\"); f=dc_try_open_rb(cand,outPath,outPathSize); if(f) return f; strcpy(dot,\".nebtex\"); f=dc_try_open_rb(cand,outPath,outPathSize); if(f) return f; } else if(dot&&dc_eq_nocase(dot,\".nebanim\")){ for(char* p=bname;p<dot;++p) *p=dc_uc(*p); strcpy(dot,\".NEBANIM\"); f=dc_try_open_rb(cand,outPath,outPathSize); if(f) return f; strcpy(dot,\".nebanim\"); f=dc_try_open_rb(cand,outPath,outPathSize); if(f) return f; } return 0; }\n";
                mc << "static int dc_try_load_nebmesh(const char* path, RuntimeMesh* out){ char resolved[512]; FILE* fp=dc_fopen_iso_compat(path,resolved,(int)sizeof(resolved)); if(!fp) return 0; fclose(fp); return NB_DC_LoadMesh(resolved, out); }\n";
                mc << "static void dc_free_mesh(RuntimeMesh* m){ NB_DC_FreeMesh(m); }\n";
                mc << "static int dc_try_load_nebtex(const char* path, RuntimeTex* out){ char resolved[512]; FILE* fp=dc_fopen_iso_compat(path,resolved,(int)sizeof(resolved)); if(!fp) return 0; fclose(fp); return NB_DC_LoadTexture(resolved, out); }\n";
                mc << "static void dc_free_tex(RuntimeTex* t){ NB_DC_FreeTexture(t); }\n";
                mc << "\n";
                auto fstr = [](float v) { return std::to_string(v) + "f"; };
                mc << "/* Dreamcast camera defaults come from scene camera export (+ optional neutral calibration offsets). */\n";
                mc << "static const float kCamPosInit[3] = {" << fstr(dcView.eye.x) << "," << fstr(dcView.eye.y) << "," << fstr(dcView.eye.z) << "};\n";
                mc << "static const float kCamTargetInit[3] = {" << fstr(dcView.target.x) << "," << fstr(dcView.target.y) << "," << fstr(dcView.target.z) << "};\n";
                mc << "static const float kCamOrbitInit[3] = {" << fstr(camSrc.orbitX) << "," << fstr(camSrc.orbitY) << "," << fstr(camSrc.orbitZ) << "};\n";
                mc << "static float gPivotOffset[3] = {0.0f,1.2f,0.0f};\n";
                mc << "static float gCamPos[3] = {" << fstr(dcView.eye.x) << "," << fstr(dcView.eye.y) << "," << fstr(dcView.eye.z) << "};\n";
                mc << "static float gCamForward[3] = {" << fstr(dcView.basis.forward.x) << "," << fstr(dcView.basis.forward.y) << "," << fstr(dcView.basis.forward.z) << "};\n";
                mc << "static float gCamRight[3] = {" << fstr(dcView.basis.right.x) << "," << fstr(dcView.basis.right.y) << "," << fstr(dcView.basis.right.z) << "};\n";
                mc << "static float gCamUp[3] = {" << fstr(dcView.basis.up.x) << "," << fstr(dcView.basis.up.y) << "," << fstr(dcView.basis.up.z) << "};\n";
                mc << "static const float kProjFovYDeg = " << fstr(dcProj.fovYDeg) << ";\n";
                mc << "static const float kProjAspect = " << fstr(dcProj.aspect) << ";\n";
                mc << "static const float kProjNear = " << fstr(dcProj.nearZ) << ";\n";
                mc << "static const float kProjFar = " << fstr(dcProj.farZ) << ";\n";
                mc << "static const float kProjViewW = " << fstr(dcViewW) << ";\n";
                mc << "static const float kProjViewH = " << fstr(dcViewH) << ";\n";
                mc << "static const float kProjFocalX = " << fstr(dcFocalX) << ";\n";
                mc << "static const float kProjFocalY = " << fstr(dcFocalY) << ";\n";
                mc << "static const float kCamRot[3] = {" << fstr(camSrc.rotX) << "," << fstr(camSrc.rotY) << "," << fstr(camSrc.rotZ) << "};\n";
                // Initialize gMeshPos/gMeshRot from the parent Node3D (drives movement),
                // NOT from the child StaticMesh3D (visual offset only).
                {
                    float initPosX = meshSrc.x, initPosY = meshSrc.y, initPosZ = meshSrc.z;
                    float initRotX = meshSrc.rotX, initRotY = meshSrc.rotY, initRotZ = meshSrc.rotZ;
                    float initScX = meshSrc.scaleX, initScY = meshSrc.scaleY, initScZ = meshSrc.scaleZ;
                    float initQw = 1.0f, initQx = 0.0f, initQy = 0.0f, initQz = 0.0f;
                    if (!meshSrc.parent.empty())
                    {
                        for (const auto& n3 : gNode3DNodes)
                        {
                            if (n3.name == meshSrc.parent)
                            {
                                initPosX = n3.x; initPosY = n3.y; initPosZ = n3.z;
                                initRotX = n3.rotX; initRotY = n3.rotY; initRotZ = n3.rotZ;
                                initScX = n3.scaleX; initScY = n3.scaleY; initScZ = n3.scaleZ;
                                initQw = n3.qw; initQx = n3.qx; initQy = n3.qy; initQz = n3.qz;
                                break;
                            }
                        }
                    }
                    mc << "static float gMeshPos[3] = {" << fstr(initPosX) << "," << fstr(initPosY) << "," << fstr(initPosZ) << "};\n";
                    mc << "static float gMeshRot[3] = {" << fstr(initRotX) << "," << fstr(initRotY) << "," << fstr(initRotZ) << "};\n";
                    mc << "static float gMeshScale[3] = {" << fstr(initScX) << "," << fstr(initScY) << "," << fstr(initScZ) << "};\n";
                    mc << "static float gQw = " << fstr(initQw) << ", gQx = " << fstr(initQx) << ", gQy = " << fstr(initQy) << ", gQz = " << fstr(initQz) << ";\n";
                }
                mc << "static const float kPlayerChildRot[3] = {" << fstr(meshSrc.rotX) << "," << fstr(meshSrc.rotY) << "," << fstr(meshSrc.rotZ) << "};\n";
                mc << "static const char kPlayerMeshDisk[] = \"" << runtimeMeshDiskName << "\";\n";
                mc << "static int gPlayerMeshIdx = -1;\n";
                mc << "void NB_RT_GetMeshPosition(float outPos[3]){ if(!outPos) return; outPos[0]=gMeshPos[0]; outPos[1]=gMeshPos[1]; outPos[2]=gMeshPos[2]; }\n";
                mc << "void NB_RT_SetMeshPosition(float x,float y,float z){ gMeshPos[0]=x; gMeshPos[1]=y; gMeshPos[2]=z; }\n";
                mc << "void NB_RT_AddMeshPositionDelta(float dx,float dy,float dz){ gMeshPos[0]+=dx; gMeshPos[1]+=dy; gMeshPos[2]+=dz; }\n";
                mc << "\n";
                // Emit DcNode3D struct, array, and lookup BEFORE bridge functions that use them
                {
                    int earlyMaxN3D = 0;
                    for (const auto& sn : runtimeSceneNode3Ds)
                        if ((int)sn.size() > earlyMaxN3D) earlyMaxN3D = (int)sn.size();
                    if (earlyMaxN3D < 1) earlyMaxN3D = 1;
                    mc << "#define MAX_NODE3D " << earlyMaxN3D << "\n";
                    mc << "typedef struct { const char* name; float pos[3]; float rot[3]; float velY; int onFloor; float extent[3]; float boundPos[3]; int physEnabled; int collisionSource; int simpleCollision; float qw,qx,qy,qz; } DcNode3D;\n";
                    mc << "static DcNode3D gNode3Ds[MAX_NODE3D];\n";
                    mc << "static int gNode3DCount = 0;\n";
                    mc << "static int dc_find_node3d(const char* name){ if(!name||!name[0]) return -1; for(int i=0;i<gNode3DCount;i++){ if(gNode3Ds[i].name && dc_eq_nocase(name,gNode3Ds[i].name)) return i; } return -1; }\n";
                }
                mc << "/* Script runtime bridge — name-based Node3D lookup for multi-node support */\n";
                mc << "static const char kPlayerParentName[] = \"" << meshSrc.parent << "\";\n";
                mc << "static float gRtOrbit[3] = {0.0f, 0.0f, 0.0f};\n";
                mc << "static float gRtCamRot[3] = {0.0f, 0.0f, 0.0f};\n";
                mc << "static int gOrbitInited = 0;\n";
                mc << "static int gFollowAlign = 0;\n";
                mc << "static const int kDcDebug = 0;\n";
                mc << "static int dc_is_player_node(const char* name){ return (name && kPlayerParentName[0] && dc_eq_nocase(name, kPlayerParentName)); }\n";
                mc << "void NB_RT_GetNode3DPosition(const char* name, float outPos[3]){ if(!outPos) return; if(dc_is_player_node(name)){ outPos[0]=gMeshPos[0]; outPos[1]=gMeshPos[1]; outPos[2]=gMeshPos[2]; return; } int idx=dc_find_node3d(name); if(idx>=0){ outPos[0]=gNode3Ds[idx].pos[0]; outPos[1]=gNode3Ds[idx].pos[1]; outPos[2]=gNode3Ds[idx].pos[2]; } else { outPos[0]=outPos[1]=outPos[2]=0; } }\n";
                mc << "void NB_RT_SetNode3DPosition(const char* name, float x, float y, float z){ if(dc_is_player_node(name)){ gMeshPos[0]=x; gMeshPos[1]=y; gMeshPos[2]=z; return; } int idx=dc_find_node3d(name); if(idx>=0){ gNode3Ds[idx].pos[0]=x; gNode3Ds[idx].pos[1]=y; gNode3Ds[idx].pos[2]=z; } }\n";
                mc << "void NB_RT_GetNode3DRotation(const char* name, float outRot[3]){ if(!outRot) return; if(dc_is_player_node(name)){ outRot[0]=gMeshRot[0]; outRot[1]=gMeshRot[1]; outRot[2]=gMeshRot[2]; return; } int idx=dc_find_node3d(name); if(idx>=0){ outRot[0]=gNode3Ds[idx].rot[0]; outRot[1]=gNode3Ds[idx].rot[1]; outRot[2]=gNode3Ds[idx].rot[2]; } else { outRot[0]=outRot[1]=outRot[2]=0; } }\n";
                mc << "void NB_RT_SetNode3DRotation(const char* name, float x, float y, float z){ if(dc_is_player_node(name)){ gMeshRot[0]=x; gMeshRot[1]=y; gMeshRot[2]=z; return; } int idx=dc_find_node3d(name); if(idx>=0){ gNode3Ds[idx].rot[0]=x; gNode3Ds[idx].rot[1]=y; gNode3Ds[idx].rot[2]=z; } }\n";
                mc << "void NB_RT_GetCameraOrbit(const char* name, float outOrbit[3]){ (void)name; if(!outOrbit) return; outOrbit[0]=gRtOrbit[0]; outOrbit[1]=gRtOrbit[1]; outOrbit[2]=gRtOrbit[2]; }\n";
                mc << "void NB_RT_SetCameraOrbit(const char* name, float x, float y, float z){ (void)name; gRtOrbit[0]=x; gRtOrbit[1]=y; gRtOrbit[2]=z; gOrbitInited=1; { float tx=gMeshPos[0], ty=gMeshPos[1]+1.2f, tz=gMeshPos[2]; gCamPos[0]=tx + gRtOrbit[0]; gCamPos[1]=ty + gRtOrbit[1]; gCamPos[2]=tz + gRtOrbit[2]; { float fx=tx-gCamPos[0], fy=ty-gCamPos[1], fz=tz-gCamPos[2]; float fl=sqrtf(fx*fx+fy*fy+fz*fz); if(fl<1e-6f) fl=1.0f; fx/=fl; fy/=fl; fz/=fl; gCamForward[0]=fx; gCamForward[1]=fy; gCamForward[2]=fz; gCamUp[0]=0.0f; gCamUp[1]=1.0f; gCamUp[2]=0.0f; { float rx=-fz, ry=0.0f, rz=fx; float rl=sqrtf(rx*rx+ry*ry+rz*rz); if(rl<1e-6f){ rx=1.0f; ry=0.0f; rz=0.0f; rl=1.0f; } gCamRight[0]=rx/rl; gCamRight[1]=ry/rl; gCamRight[2]=rz/rl; } } } }\n";
                mc << "void NB_RT_GetCameraRotation(const char* name, float outRot[3]){ (void)name; if(!outRot) return; outRot[0]=gRtCamRot[0]; outRot[1]=gRtCamRot[1]; outRot[2]=gRtCamRot[2]; }\n";
                mc << "void NB_RT_SetCameraRotation(const char* name, float x, float y, float z){ (void)name; gRtCamRot[0]=x; gRtCamRot[1]=y; gRtCamRot[2]=z; { float rx=x*0.0174532925f, ry=y*0.0174532925f; float cx=cosf(rx), sx=sinf(rx), cy=cosf(ry), sy=sinf(ry); float fx=sy*cx, fy=-sx, fz=cy*cx; float fl=sqrtf(fx*fx+fy*fy+fz*fz); if(fl<1e-6f) fl=1.0f; fx/=fl; fy/=fl; fz/=fl; gCamForward[0]=fx; gCamForward[1]=fy; gCamForward[2]=fz; gCamUp[0]=0.0f; gCamUp[1]=1.0f; gCamUp[2]=0.0f; float rxv = gCamUp[1]*fz - gCamUp[2]*fy; float ryv = gCamUp[2]*fx - gCamUp[0]*fz; float rzv = gCamUp[0]*fy - gCamUp[1]*fx; float rl=sqrtf(rxv*rxv+ryv*ryv+rzv*rzv); if(rl<1e-6f){ rxv=1.0f; ryv=0.0f; rzv=0.0f; rl=1.0f; } gCamRight[0]=rxv/rl; gCamRight[1]=ryv/rl; gCamRight[2]=rzv/rl; } }\n";
                mc << "void NB_RT_GetCameraWorldForward(const char* name, float outFwd[3]){ (void)name; if(!outFwd) return; outFwd[0]=gCamForward[0]; outFwd[1]=gCamForward[1]; outFwd[2]=gCamForward[2]; }\n";
                mc << "int NB_RT_IsCameraUnderNode3D(const char* cameraName, const char* nodeName){ (void)cameraName; (void)nodeName; return 1; }\n";
                mc << "/* Collision / physics bridge */\n";
                // Find the player's parent Node3D to export its physics settings
                {
                    float initExtX = 0.5f, initExtY = 0.5f, initExtZ = 0.5f;
                    float initBpX = 0.0f, initBpY = 0.0f, initBpZ = 0.0f;
                    int initPhysics = 0;
                    int initCollisionSource = 0;
                    int initSimpleCollision = 0;
                    std::string parentName = meshSrc.parent;
                    for (const auto& n3 : gNode3DNodes)
                    {
                        if (n3.name == parentName)
                        {
                            initExtX = n3.extentX; initExtY = n3.extentY; initExtZ = n3.extentZ;
                            initBpX = n3.boundPosX; initBpY = n3.boundPosY; initBpZ = n3.boundPosZ;
                            initPhysics = n3.physicsEnabled ? 1 : 0;
                            initCollisionSource = n3.collisionSource ? 1 : 0;
                            initSimpleCollision = n3.simpleCollision ? 1 : 0;
                            break;
                        }
                    }
                    mc << "static float gCollExtent[3] = {" << fstr(initExtX) << "," << fstr(initExtY) << "," << fstr(initExtZ) << "};\n";
                    mc << "static int gPhysicsEnabled = " << initPhysics << ";\n";
                    mc << "static int gCollisionSource = " << initCollisionSource << ";\n";
                    mc << "static int gSimpleCollision = " << initSimpleCollision << ";\n";
                }
                mc << "static float gVelY = 0.0f;\n";
                mc << "static int gOnFloor = 0;\n";
                mc << "void NB_RT_GetNode3DCollisionBounds(const char* name, float outExtents[3]){ if(!outExtents) return; if(dc_is_player_node(name)){ outExtents[0]=gCollExtent[0]; outExtents[1]=gCollExtent[1]; outExtents[2]=gCollExtent[2]; return; } int idx=dc_find_node3d(name); if(idx>=0){ outExtents[0]=gNode3Ds[idx].extent[0]; outExtents[1]=gNode3Ds[idx].extent[1]; outExtents[2]=gNode3Ds[idx].extent[2]; } else { outExtents[0]=outExtents[1]=outExtents[2]=0.5f; } }\n";
                mc << "void NB_RT_SetNode3DCollisionBounds(const char* name, float ex, float ey, float ez){ if(dc_is_player_node(name)){ gCollExtent[0]=ex; gCollExtent[1]=ey; gCollExtent[2]=ez; return; } int idx=dc_find_node3d(name); if(idx>=0){ gNode3Ds[idx].extent[0]=ex; gNode3Ds[idx].extent[1]=ey; gNode3Ds[idx].extent[2]=ez; } }\n";
                {
                    float bpX = 0.0f, bpY = 0.0f, bpZ = 0.0f;
                    std::string parentName = meshSrc.parent;
                    for (const auto& n3 : gNode3DNodes)
                    {
                        if (n3.name == parentName)
                        {
                            bpX = n3.boundPosX; bpY = n3.boundPosY; bpZ = n3.boundPosZ;
                            break;
                        }
                    }
                    mc << "static float gBoundPos[3] = {" << fstr(bpX) << "," << fstr(bpY) << "," << fstr(bpZ) << "};\n";
                }
                mc << "void NB_RT_GetNode3DBoundPos(const char* name, float outPos[3]){ if(!outPos) return; if(dc_is_player_node(name)){ outPos[0]=gBoundPos[0]; outPos[1]=gBoundPos[1]; outPos[2]=gBoundPos[2]; return; } int idx=dc_find_node3d(name); if(idx>=0){ outPos[0]=gNode3Ds[idx].boundPos[0]; outPos[1]=gNode3Ds[idx].boundPos[1]; outPos[2]=gNode3Ds[idx].boundPos[2]; } else { outPos[0]=outPos[1]=outPos[2]=0; } }\n";
                mc << "void NB_RT_SetNode3DBoundPos(const char* name, float bx, float by, float bz){ if(dc_is_player_node(name)){ gBoundPos[0]=bx; gBoundPos[1]=by; gBoundPos[2]=bz; return; } int idx=dc_find_node3d(name); if(idx>=0){ gNode3Ds[idx].boundPos[0]=bx; gNode3Ds[idx].boundPos[1]=by; gNode3Ds[idx].boundPos[2]=bz; } }\n";
                mc << "int NB_RT_GetNode3DPhysicsEnabled(const char* name){ if(dc_is_player_node(name)) return gPhysicsEnabled; int idx=dc_find_node3d(name); return (idx>=0)?gNode3Ds[idx].physEnabled:0; }\n";
                mc << "void NB_RT_SetNode3DPhysicsEnabled(const char* name, int enabled){ if(dc_is_player_node(name)){ gPhysicsEnabled=enabled; return; } int idx=dc_find_node3d(name); if(idx>=0) gNode3Ds[idx].physEnabled=enabled; }\n";
                mc << "int NB_RT_GetNode3DCollisionSource(const char* name){ if(dc_is_player_node(name)) return gCollisionSource; int idx=dc_find_node3d(name); return (idx>=0)?gNode3Ds[idx].collisionSource:0; }\n";
                mc << "void NB_RT_SetNode3DCollisionSource(const char* name, int enabled){ if(dc_is_player_node(name)){ gCollisionSource=enabled; return; } int idx=dc_find_node3d(name); if(idx>=0) gNode3Ds[idx].collisionSource=enabled; }\n";
                mc << "int NB_RT_GetNode3DSimpleCollision(const char* name){ if(dc_is_player_node(name)) return gSimpleCollision; int idx=dc_find_node3d(name); return (idx>=0)?gNode3Ds[idx].simpleCollision:0; }\n";
                mc << "void NB_RT_SetNode3DSimpleCollision(const char* name, int enabled){ if(dc_is_player_node(name)){ gSimpleCollision=enabled; return; } int idx=dc_find_node3d(name); if(idx>=0) gNode3Ds[idx].simpleCollision=enabled; }\n";
                mc << "float NB_RT_GetNode3DVelocityY(const char* name){ if(dc_is_player_node(name)) return gVelY; int idx=dc_find_node3d(name); return (idx>=0)?gNode3Ds[idx].velY:0.0f; }\n";
                mc << "void NB_RT_SetNode3DVelocityY(const char* name, float vy){ if(dc_is_player_node(name)){ gVelY=vy; return; } int idx=dc_find_node3d(name); if(idx>=0) gNode3Ds[idx].velY=vy; }\n";
                mc << "int NB_RT_IsNode3DOnFloor(const char* name){ if(dc_is_player_node(name)) return gOnFloor; int idx=dc_find_node3d(name); return (idx>=0)?gNode3Ds[idx].onFloor:0; }\n";
                mc << "int NB_RT_CheckAABBOverlap(const char* name1, const char* name2){\n";
                mc << "  float ax,ay,az,aex,aey,aez, bx,by,bz,bex,bey,bez;\n";
                mc << "  int gotA=0, gotB=0;\n";
                mc << "  if(dc_is_player_node(name1)){ ax=gMeshPos[0]+gBoundPos[0]; ay=gMeshPos[1]+gBoundPos[1]; az=gMeshPos[2]+gBoundPos[2]; aex=gCollExtent[0]; aey=gCollExtent[1]; aez=gCollExtent[2]; gotA=1; }\n";
                mc << "  else { int i=dc_find_node3d(name1); if(i>=0){ DcNode3D* n=&gNode3Ds[i]; ax=n->pos[0]+n->boundPos[0]; ay=n->pos[1]+n->boundPos[1]; az=n->pos[2]+n->boundPos[2]; aex=n->extent[0]; aey=n->extent[1]; aez=n->extent[2]; gotA=1; } }\n";
                mc << "  if(dc_is_player_node(name2)){ bx=gMeshPos[0]+gBoundPos[0]; by=gMeshPos[1]+gBoundPos[1]; bz=gMeshPos[2]+gBoundPos[2]; bex=gCollExtent[0]; bey=gCollExtent[1]; bez=gCollExtent[2]; gotB=1; }\n";
                mc << "  else { int i=dc_find_node3d(name2); if(i>=0){ DcNode3D* n=&gNode3Ds[i]; bx=n->pos[0]+n->boundPos[0]; by=n->pos[1]+n->boundPos[1]; bz=n->pos[2]+n->boundPos[2]; bex=n->extent[0]; bey=n->extent[1]; bez=n->extent[2]; gotB=1; } }\n";
                mc << "  if(!gotA||!gotB) return 0;\n";
                mc << "  if(ax+aex<bx-bex||ax-aex>bx+bex) return 0;\n";
                mc << "  if(ay+aey<by-bey||ay-aey>by+bey) return 0;\n";
                mc << "  if(az+aez<bz-bez||az-aez>bz+bez) return 0;\n";
                mc << "  return 1;\n";
                mc << "}\n";
                // NB_RT_RaycastDown emitted after MAX_MESHES/gSceneMeshes declarations
                mc << "static int gNavMeshLoaded = 0;\n";
                mc << "static int gSceneMetaIndex = 0;\n";
                mc << "int NB_RT_NavMeshBuild(void){\n";
                mc << "  if (!gNavMeshLoaded) {\n";
                mc << "    char navPath[64]; snprintf(navPath,sizeof(navPath),\"/cd/data/navmesh/NAV%05d.BIN\",gSceneMetaIndex+1);\n";
                mc << "    gNavMeshLoaded = NB_DC_LoadNavMesh(navPath);\n";
                mc << "    if (gNavMeshLoaded) {\n";
                mc << "      int blobSize = 0;\n";
                mc << "      const void* blob = NB_DC_GetNavMeshData(&blobSize);\n";
                mc << "      if (!NB_DC_DetourInit(blob, blobSize)) { gNavMeshLoaded = 0; }\n";
                mc << "    }\n";
                mc << "  }\n";
                mc << "  return gNavMeshLoaded;\n";
                mc << "}\n";
                mc << "void NB_RT_NavMeshClear(void){ NB_DC_DetourFree(); NB_DC_FreeNavMesh(); gNavMeshLoaded=0; }\n";
                mc << "int NB_RT_NavMeshIsReady(void){ return NB_DC_DetourIsReady(); }\n";
                mc << "int NB_RT_NavMeshFindPath(float sx, float sy, float sz, float gx, float gy, float gz, float* outPath, int maxPoints){ return NB_DC_DetourFindPath(sx,sy,sz,gx,gy,gz,outPath,maxPoints); }\n";
                mc << "int NB_RT_NavMeshFindRandomPoint(float outPos[3]){ return NB_DC_DetourFindRandomPoint(outPos); }\n";
                mc << "int NB_RT_NavMeshFindClosestPoint(float px, float py, float pz, float outPos[3]){ return NB_DC_DetourFindClosestPoint(px,py,pz,outPos); }\n";
                mc << "static int gSceneSwitchReq = 0;\n";
                mc << "static char gSceneSwitchName[64] = {0};\n";
                mc << "void NB_RT_NextScene(void){ gSceneSwitchReq = 1; gSceneSwitchName[0]=0; }\n";
                mc << "void NB_RT_PrevScene(void){ gSceneSwitchReq = -1; gSceneSwitchName[0]=0; }\n";
                mc << "void NB_RT_SwitchScene(const char* name){ if(!name||!name[0]) return; gSceneSwitchReq = 2; strncpy(gSceneSwitchName,name,sizeof(gSceneSwitchName)-1); gSceneSwitchName[sizeof(gSceneSwitchName)-1]=0; }\n";
                mc << "static int gMirrorX = 1;\n";
                mc << "static int gMirrorY = 1;\n";
                mc << "static int gMirrorZ = 1;\n";
                mc << "static int gMirrorLrIndex = 0;\n";
                mc << "static const int kVmuLoadOnBoot = " << vmuBootEnabled << ";\n";
                mc << "static const uint8_t kVmuBootPng[192] = {";
                for (size_t vb = 0; vb < vmuBootPacked.size(); ++vb)
                {
                    mc << (int)vmuBootPacked[vb];
                    if (vb + 1 < vmuBootPacked.size()) mc << ",";
                }
                mc << "};\n";
                mc << "static const int kVmuAnimEnabled = " << vmuAnimEnabled << ";\n";
                mc << "static const int kVmuAnimLoop = " << vmuAnimLoopEnabled << ";\n";
                mc << "static const int kVmuAnimSpeedCode = " << vmuAnimSpeedCode << ";\n";
                mc << "static const int kVmuAnimFrameCount = " << vmuAnimFrameCount << ";\n";
                mc << "static const uint8_t kVmuAnimFrames[] = {";
                if (!vmuAnimPacked.empty())
                {
                    for (size_t fi = 0; fi < vmuAnimPacked.size(); ++fi)
                    {
                        for (size_t bi = 0; bi < vmuAnimPacked[fi].size(); ++bi)
                        {
                            mc << (int)vmuAnimPacked[fi][bi];
                            if (!(fi + 1 == vmuAnimPacked.size() && bi + 1 == vmuAnimPacked[fi].size())) mc << ",";
                        }
                    }
                }
                mc << "};\n";
                // Emit per-layer frame range table
                mc << "static const int kVmuLayerCount = " << (int)gVmuAnimLayers.size() << ";\n";
                mc << "static const int kVmuLayerStart[] = {";
                for (size_t li = 0; li < gVmuAnimLayers.size(); ++li)
                {
                    mc << gVmuAnimLayers[li].frameStart;
                    if (li + 1 < gVmuAnimLayers.size()) mc << ",";
                }
                mc << "};\n";
                mc << "static const int kVmuLayerEnd[] = {";
                for (size_t li = 0; li < gVmuAnimLayers.size(); ++li)
                {
                    mc << gVmuAnimLayers[li].frameEnd;
                    if (li + 1 < gVmuAnimLayers.size()) mc << ",";
                }
                mc << "};\n";
                mc << "static int gVmuActiveLayer = -1;\n";
                mc << "static int gVmuLayerTriggered = 0;\n";
                mc << "static void NB_TryLoadVmuBootImage(void){\n";
                mc << "  static uintptr_t sLastVmu = 0;\n";
                mc << "  if(!kVmuLoadOnBoot) return;\n";
                mc << "  maple_device_t* vmu = 0;\n";
                mc << "  for(int i=0;i<8;++i){ maple_device_t* d = maple_enum_type(i, MAPLE_FUNC_LCD); if(d){ vmu = d; break; } }\n";
                mc << "  uintptr_t cur = (uintptr_t)vmu;\n";
                mc << "  if(cur == sLastVmu) return;\n";
                mc << "  sLastVmu = cur;\n";
                mc << "  if(!vmu){ dbgio_printf(\"[VMU] VMU LCD disconnected\\n\"); return; }\n";
                mc << "  uint8_t diskBoot[192];\n";
                mc << "  const uint8_t* src = kVmuBootPng;\n";
                mc << "  FILE* fb = fopen(\"/cd/data/vmu/vmu_boot.bin\",\"rb\");\n";
                mc << "  if(fb){ size_t n=fread(diskBoot,1,sizeof(diskBoot),fb); fclose(fb); if(n==sizeof(diskBoot)) src=diskBoot; }\n";
                mc << "  if(kVmuAnimEnabled && kVmuAnimFrameCount > 0) src = &kVmuAnimFrames[0];\n";
                mc << "  int rc = vmu_draw_lcd(vmu, (void*)src);\n";
                mc << "  dbgio_printf(\"[VMU] load-on-boot draw rc=%d\\n\", rc);\n";
                mc << "}\n";
                mc << "static void NB_UpdateVmuAnim(float dt){\n";
                mc << "  static maple_device_t* sVmu = 0;\n";
                mc << "  static float sAccum = 0.0f;\n";
                mc << "  static int sFrame = 0;\n";
                mc << "  static int sDoneOneShot = 0;\n";
                mc << "  static uint8_t* sAnimDisk = 0;\n";
                mc << "  static int sAnimDiskFrames = 0;\n";
                mc << "  static int sAnimTriedLoad = 0;\n";
                mc << "  if(!kVmuLoadOnBoot || !kVmuAnimEnabled || kVmuAnimFrameCount <= 0) return;\n";
                mc << "  if(!sAnimTriedLoad){\n";
                mc << "    sAnimTriedLoad = 1;\n";
                mc << "    FILE* fa=fopen(\"/cd/data/vmu/vmu_anim.bin\",\"rb\");\n";
                mc << "    if(fa){ fseek(fa,0,SEEK_END); long sz=ftell(fa); fseek(fa,0,SEEK_SET); if(sz>=192){ sAnimDisk=(uint8_t*)malloc((size_t)sz); if(sAnimDisk){ size_t n=fread(sAnimDisk,1,(size_t)sz,fa); if((long)n==sz) sAnimDiskFrames=(int)(sz/192); else { free(sAnimDisk); sAnimDisk=0; } } } fclose(fa); }\n";
                mc << "  }\n";
                mc << "  if(!sVmu){ for(int i=0;i<8;++i){ maple_device_t* d=maple_enum_type(i, MAPLE_FUNC_LCD); if(d){ sVmu=d; break; } } }\n";
                mc << "  if(!sVmu) return;\n";
                mc << "  // Handle layer trigger: reset playhead to layer start\n";
                mc << "  if(gVmuLayerTriggered){\n";
                mc << "    gVmuLayerTriggered = 0;\n";
                mc << "    sAccum = 0.0f;\n";
                mc << "    sDoneOneShot = 0;\n";
                mc << "    if(gVmuActiveLayer >= 0 && gVmuActiveLayer < kVmuLayerCount){\n";
                mc << "      sFrame = kVmuLayerStart[gVmuActiveLayer];\n";
                mc << "    } else { sFrame = 0; }\n";
                mc << "  }\n";
                mc << "  // Determine frame range from active layer or full range\n";
                mc << "  int rangeStart = 0;\n";
                mc << "  int rangeEnd = kVmuAnimFrameCount - 1;\n";
                mc << "  if(gVmuActiveLayer >= 0 && gVmuActiveLayer < kVmuLayerCount){\n";
                mc << "    rangeStart = kVmuLayerStart[gVmuActiveLayer];\n";
                mc << "    rangeEnd = kVmuLayerEnd[gVmuActiveLayer];\n";
                mc << "  }\n";
                mc << "  int frameCount = (sAnimDiskFrames > 0) ? sAnimDiskFrames : kVmuAnimFrameCount;\n";
                mc << "  if(rangeEnd >= frameCount) rangeEnd = frameCount - 1;\n";
                mc << "  if(rangeStart > rangeEnd) rangeStart = rangeEnd;\n";
                mc << "  if(!kVmuAnimLoop && sDoneOneShot && gVmuActiveLayer < 0) return;\n";
                mc << "  float fps = 8.0f; if(kVmuAnimSpeedCode==0) fps = 4.0f; else if(kVmuAnimSpeedCode==2) fps = 12.0f; else if(kVmuAnimSpeedCode==3) fps = 16.0f;\n";
                mc << "  float step = 1.0f / fps;\n";
                mc << "  sAccum += dt;\n";
                mc << "  while(sAccum >= step){\n";
                mc << "    sAccum -= step;\n";
                mc << "    const uint8_t* frame = (sAnimDiskFrames > 0) ? (sAnimDisk + (size_t)sFrame * 192u) : (&kVmuAnimFrames[(size_t)sFrame * 192u]);\n";
                mc << "    vmu_draw_lcd(sVmu, (void*)frame);\n";
                mc << "    sFrame++;\n";
                mc << "    if(sFrame > rangeEnd){\n";
                mc << "      if(gVmuActiveLayer >= 0){\n";
                mc << "        // Layer finished: return to layer 0 (idle)\n";
                mc << "        gVmuActiveLayer = -1;\n";
                mc << "        sFrame = 0;\n";
                mc << "      } else {\n";
                mc << "        sFrame = rangeStart;\n";
                mc << "        if(!kVmuAnimLoop){ sDoneOneShot = 1; break; }\n";
                mc << "      }\n";
                mc << "    }\n";
                mc << "  }\n";
                mc << "}\n";
                mc << "void NB_RT_PlayVmuLayer(int layer){\n";
                mc << "  if(layer < 0 || layer >= kVmuLayerCount) return;\n";
                mc << "  gVmuActiveLayer = layer;\n";
                mc << "  gVmuLayerTriggered = 1;\n";
                mc << "}\n";
                mc << "static void NB_SetMirrorFromIndex(int idx){\n";
                mc << "  idx &= 7;\n";
                mc << "  gMirrorX = (idx & 1) ? -1 : 1;\n";
                mc << "  gMirrorY = (idx & 2) ? -1 : 1;\n";
                mc << "  gMirrorZ = (idx & 4) ? -1 : 1;\n";
                mc << "  gMirrorLrIndex = idx;\n";
                mc << "  dbgio_printf(\"[Mirror] idx=%d => X=%d Y=%d Z=%d\\n\", gMirrorLrIndex, gMirrorX, gMirrorY, gMirrorZ);\n";
                mc << "}\n";
                mc << "enum { MAX_SLOT = 16, MAX_MESHES = 64 };\n";
                mc << "enum { MAX_ANIM_SLOTS = 8 };\n";
                mc << "typedef struct { char meshDisk[128]; char meshLogical[128]; char meshName[64]; float pos[3]; float rot[3]; float scale[3]; char texDisk[MAX_SLOT][128]; char texLogical[MAX_SLOT][128]; char animDisk[128]; uint8_t runtimeTest; uint8_t collisionSource; int animSlotCount; char animSlotName[MAX_ANIM_SLOTS][32]; char animSlotDisk[MAX_ANIM_SLOTS][128]; float animSlotSpeed[MAX_ANIM_SLOTS]; uint8_t animSlotLoop[MAX_ANIM_SLOTS]; int animActiveSlot; int animLoop; float animTime; float animSpeed; int animPlaying; int animFinished; } SceneMeshMeta;\n";
                mc << "static SceneMeshMeta gSceneMeshes[MAX_MESHES];\n";
                mc << "static int gSceneMeshCount = 0;\n";
                mc << "static char gSceneName[64] = \"Default\";\n";
                mc << "static int gSceneIndex = 0;\n";
                mc << "static const char* gSceneFiles[] = {";
                for (size_t si = 0; si < runtimeSceneFiles.size(); ++si)
                {
                    mc << "\"" << runtimeSceneFiles[si] << "\"";
                    if (si + 1 < runtimeSceneFiles.size()) mc << ",";
                }
                mc << "};\n";
                mc << "static const int gSceneCount = " << (int)runtimeSceneFiles.size() << ";\n";
                mc << "static const char* gSceneNames[] = {";
                for (size_t si = 0; si < runtimeSceneNames.size(); ++si)
                {
                    mc << "\"" << runtimeSceneNames[si] << "\"";
                    if (si + 1 < runtimeSceneNames.size()) mc << ",";
                }
                mc << "};\n";
                mc << "static const char* kDefaultSceneFile = \"" << defaultSceneRuntimeFile << "\";\n";
                mc << "typedef struct { const char* logical; const char* staged; } NB_RefMap;\n";
                mc << "static const NB_RefMap kMeshRefMap[] = {";
                for (size_t mi = 0; mi < meshRefMapEntries.size(); ++mi)
                {
                    mc << "{\"" << meshRefMapEntries[mi].first << "\",\"" << meshRefMapEntries[mi].second << "\"}";
                    if (mi + 1 < meshRefMapEntries.size()) mc << ",";
                }
                mc << "};\n";
                mc << "static const int kMeshRefMapCount = " << (int)meshRefMapEntries.size() << ";\n";
                mc << "static const NB_RefMap kTexRefMap[] = {";
                for (size_t ti = 0; ti < texRefMapEntries.size(); ++ti)
                {
                    mc << "{\"" << texRefMapEntries[ti].first << "\",\"" << texRefMapEntries[ti].second << "\"}";
                    if (ti + 1 < texRefMapEntries.size()) mc << ",";
                }
                mc << "};\n";
                mc << "static const int kTexRefMapCount = " << (int)texRefMapEntries.size() << ";\n";
                mc << "static const char* kSceneAnimDisk[" << (int)runtimeSceneFiles.size() << "][MAX_MESHES] = {\n";
                for (size_t si = 0; si < runtimeSceneFiles.size(); ++si)
                {
                    mc << "{";
                    for (int mi = 0; mi < 64; ++mi)
                    {
                        std::string animDisk = (mi < (int)runtimeSceneAnimDiskByMesh[si].size()) ? runtimeSceneAnimDiskByMesh[si][mi] : "";
                        mc << "\"" << animDisk << "\"";
                        if (mi + 1 < 64) mc << ",";
                    }
                    mc << "}";
                    if (si + 1 < runtimeSceneFiles.size()) mc << ",";
                    mc << "\n";
                }
                mc << "};\n";
                // Emit per-scene animation slot count, names, and disk paths
                {
                    int sceneCount = (int)runtimeSceneFiles.size();
                    mc << "static const int kSceneAnimSlotCount[" << sceneCount << "][MAX_MESHES] = {\n";
                    for (int si = 0; si < sceneCount; ++si)
                    {
                        mc << "{";
                        for (int mi = 0; mi < 64; ++mi)
                        {
                            int cnt = 0;
                            if (mi < (int)runtimeSceneAnimSlotsByMesh[si].size())
                                cnt = (int)runtimeSceneAnimSlotsByMesh[si][mi].size();
                            mc << cnt;
                            if (mi + 1 < 64) mc << ",";
                        }
                        mc << "}";
                        if (si + 1 < sceneCount) mc << ",";
                        mc << "\n";
                    }
                    mc << "};\n";
                    mc << "static const char* kSceneAnimSlotName[" << sceneCount << "][MAX_MESHES][MAX_ANIM_SLOTS] = {\n";
                    for (int si = 0; si < sceneCount; ++si)
                    {
                        mc << "{";
                        for (int mi = 0; mi < 64; ++mi)
                        {
                            mc << "{";
                            for (int ai = 0; ai < 8; ++ai)
                            {
                                std::string nm;
                                if (mi < (int)runtimeSceneAnimSlotsByMesh[si].size() && ai < (int)runtimeSceneAnimSlotsByMesh[si][mi].size())
                                    nm = runtimeSceneAnimSlotsByMesh[si][mi][ai].name;
                                mc << "\"" << nm << "\"";
                                if (ai + 1 < 8) mc << ",";
                            }
                            mc << "}";
                            if (mi + 1 < 64) mc << ",";
                        }
                        mc << "}";
                        if (si + 1 < sceneCount) mc << ",";
                        mc << "\n";
                    }
                    mc << "};\n";
                    mc << "static const char* kSceneAnimSlotDisk[" << sceneCount << "][MAX_MESHES][MAX_ANIM_SLOTS] = {\n";
                    for (int si = 0; si < sceneCount; ++si)
                    {
                        mc << "{";
                        for (int mi = 0; mi < 64; ++mi)
                        {
                            mc << "{";
                            for (int ai = 0; ai < 8; ++ai)
                            {
                                std::string dk;
                                if (mi < (int)runtimeSceneAnimSlotsByMesh[si].size() && ai < (int)runtimeSceneAnimSlotsByMesh[si][mi].size())
                                    dk = runtimeSceneAnimSlotsByMesh[si][mi][ai].disk;
                                mc << "\"" << dk << "\"";
                                if (ai + 1 < 8) mc << ",";
                            }
                            mc << "}";
                            if (mi + 1 < 64) mc << ",";
                        }
                        mc << "}";
                        if (si + 1 < sceneCount) mc << ",";
                        mc << "\n";
                    }
                    mc << "};\n";
                    // Per-slot speed
                    mc << "static const float kSceneAnimSlotSpeed[" << sceneCount << "][MAX_MESHES][MAX_ANIM_SLOTS] = {\n";
                    for (int si = 0; si < sceneCount; ++si)
                    {
                        mc << "{";
                        for (int mi = 0; mi < 64; ++mi)
                        {
                            mc << "{";
                            for (int ai = 0; ai < 8; ++ai)
                            {
                                float spd = 1.0f;
                                if (mi < (int)runtimeSceneAnimSlotsByMesh[si].size() && ai < (int)runtimeSceneAnimSlotsByMesh[si][mi].size())
                                    spd = runtimeSceneAnimSlotsByMesh[si][mi][ai].speed;
                                char buf[32]; snprintf(buf, sizeof(buf), "%.4ff", spd);
                                mc << buf;
                                if (ai + 1 < 8) mc << ",";
                            }
                            mc << "}";
                            if (mi + 1 < 64) mc << ",";
                        }
                        mc << "}";
                        if (si + 1 < sceneCount) mc << ",";
                        mc << "\n";
                    }
                    mc << "};\n";
                    // Per-slot loop flag
                    mc << "static const uint8_t kSceneAnimSlotLoop[" << sceneCount << "][MAX_MESHES][MAX_ANIM_SLOTS] = {\n";
                    for (int si = 0; si < sceneCount; ++si)
                    {
                        mc << "{";
                        for (int mi = 0; mi < 64; ++mi)
                        {
                            mc << "{";
                            for (int ai = 0; ai < 8; ++ai)
                            {
                                int lp = 1;
                                if (mi < (int)runtimeSceneAnimSlotsByMesh[si].size() && ai < (int)runtimeSceneAnimSlotsByMesh[si][mi].size())
                                    lp = runtimeSceneAnimSlotsByMesh[si][mi][ai].loop ? 1 : 0;
                                mc << lp;
                                if (ai + 1 < 8) mc << ",";
                            }
                            mc << "}";
                            if (mi + 1 < 64) mc << ",";
                        }
                        mc << "}";
                        if (si + 1 < sceneCount) mc << ",";
                        mc << "\n";
                    }
                    mc << "};\n";
                }
                mc << "static const uint8_t kSceneRuntimeTest[" << (int)runtimeSceneFiles.size() << "][MAX_MESHES] = {\n";
                for (size_t si = 0; si < runtimeSceneFiles.size(); ++si)
                {
                    mc << "{";
                    for (int mi = 0; mi < 64; ++mi)
                    {
                        int rt = (mi < (int)runtimeSceneRuntimeTestByMesh[si].size()) ? (runtimeSceneRuntimeTestByMesh[si][mi] ? 1 : 0) : 0;
                        mc << rt;
                        if (mi + 1 < 64) mc << ",";
                    }
                    mc << "}";
                    if (si + 1 < runtimeSceneFiles.size()) mc << ",";
                    mc << "\n";
                }
                mc << "};\n";
                // Per-scene per-mesh node name (for script name-based lookup)
                mc << "static const char* kSceneMeshName[" << (int)runtimeSceneFiles.size() << "][MAX_MESHES] = {\n";
                for (size_t si = 0; si < runtimeSceneFiles.size(); ++si)
                {
                    mc << "{";
                    for (int mi = 0; mi < 64; ++mi)
                    {
                        if (mi < (int)runtimeSceneMeshNameByMesh[si].size() && !runtimeSceneMeshNameByMesh[si][mi].empty())
                            mc << "\"" << runtimeSceneMeshNameByMesh[si][mi] << "\"";
                        else
                            mc << "\"\"";
                        if (mi + 1 < 64) mc << ",";
                    }
                    mc << "}";
                    if (si + 1 < runtimeSceneFiles.size()) mc << ",";
                    mc << "\n";
                }
                mc << "};\n";
                mc << "static const uint8_t kSceneCollisionSource[" << (int)runtimeSceneFiles.size() << "][MAX_MESHES] = {\n";
                for (size_t si = 0; si < runtimeSceneFiles.size(); ++si)
                {
                    mc << "{";
                    for (int mi = 0; mi < 64; ++mi)
                    {
                        int cs = (mi < (int)runtimeSceneCollisionSourceByMesh[si].size()) ? (runtimeSceneCollisionSourceByMesh[si][mi] ? 1 : 0) : 0;
                        mc << cs;
                        if (mi + 1 < 64) mc << ",";
                    }
                    mc << "}";
                    if (si + 1 < runtimeSceneFiles.size()) mc << ",";
                    mc << "\n";
                }
                mc << "};\n";
                mc << "static const float kSceneWallThreshold[" << (int)runtimeSceneFiles.size() << "][MAX_MESHES] = {\n";
                for (size_t si = 0; si < runtimeSceneFiles.size(); ++si)
                {
                    mc << "{";
                    for (int mi = 0; mi < 64; ++mi)
                    {
                        float wt = (mi < (int)runtimeSceneWallThresholdByMesh[si].size()) ? runtimeSceneWallThresholdByMesh[si][mi] : 0.7f;
                        char wtBuf[32]; snprintf(wtBuf, sizeof(wtBuf), "%.2ff", wt); mc << wtBuf;
                        if (mi + 1 < 64) mc << ",";
                    }
                    mc << "}";
                    if (si + 1 < runtimeSceneFiles.size()) mc << ",";
                    mc << "\n";
                }
                mc << "};\n";
                mc << "static const uint8_t kSceneCollisionWalls[" << (int)runtimeSceneFiles.size() << "][MAX_MESHES] = {\n";
                for (size_t si = 0; si < runtimeSceneFiles.size(); ++si)
                {
                    mc << "{";
                    for (int mi = 0; mi < 64; ++mi)
                    {
                        int cw = (mi < (int)runtimeSceneCollisionWallsByMesh[si].size()) ? (runtimeSceneCollisionWallsByMesh[si][mi] ? 1 : 0) : 0;
                        mc << cw;
                        if (mi + 1 < 64) mc << ",";
                    }
                    mc << "}";
                    if (si + 1 < runtimeSceneFiles.size()) mc << ",";
                    mc << "\n";
                }
                mc << "};\n";
                // Per-scene parent Node3D transform for the player mesh (drives movement).
                mc << "static const float kScenePlayerParentPos[" << (int)runtimeSceneFiles.size() << "][3] = {\n";
                for (size_t si = 0; si < runtimeSceneFiles.size(); ++si)
                {
                    const auto& pp = runtimeScenePlayerParent[si];
                    mc << "{" << fstr(pp.pos[0]) << "," << fstr(pp.pos[1]) << "," << fstr(pp.pos[2]) << "}";
                    if (si + 1 < runtimeSceneFiles.size()) mc << ",";
                    mc << "\n";
                }
                mc << "};\n";
                mc << "static const float kScenePlayerParentRot[" << (int)runtimeSceneFiles.size() << "][3] = {\n";
                for (size_t si = 0; si < runtimeSceneFiles.size(); ++si)
                {
                    const auto& pp = runtimeScenePlayerParent[si];
                    mc << "{" << fstr(pp.rot[0]) << "," << fstr(pp.rot[1]) << "," << fstr(pp.rot[2]) << "}";
                    if (si + 1 < runtimeSceneFiles.size()) mc << ",";
                    mc << "\n";
                }
                mc << "};\n";
                // --- Multi-Node3D physics: emit per-scene Node3D constant arrays ---
                {
                    int sceneCount = (int)runtimeSceneFiles.size();
                    int maxN3D = 0;
                    for (const auto& sn : runtimeSceneNode3Ds)
                        if ((int)sn.size() > maxN3D) maxN3D = (int)sn.size();
                    if (maxN3D < 1) maxN3D = 1; // at least 1 to avoid zero-size arrays
                    // MAX_NODE3D, DcNode3D, gNode3Ds, dc_find_node3d already emitted above bridge functions
                    // Node3D count per scene
                    mc << "static const int kSceneNode3DCount[" << sceneCount << "] = {";
                    for (int si = 0; si < sceneCount; ++si)
                    {
                        mc << (int)runtimeSceneNode3Ds[si].size();
                        if (si + 1 < sceneCount) mc << ",";
                    }
                    mc << "};\n";
                    // Node3D names [scene][node3d] — for script name-based lookup
                    mc << "static const char* kSceneNode3DNames[" << sceneCount << "][" << maxN3D << "] = {\n";
                    for (int si = 0; si < sceneCount; ++si)
                    {
                        mc << "{";
                        for (int ni = 0; ni < maxN3D; ++ni)
                        {
                            if (ni < (int)runtimeSceneNode3Ds[si].size())
                                mc << "\"" << runtimeSceneNode3Ds[si][ni].name << "\"";
                            else
                                mc << "\"\"";
                            if (ni + 1 < maxN3D) mc << ",";
                        }
                        mc << "}";
                        if (si + 1 < sceneCount) mc << ",";
                        mc << "\n";
                    }
                    mc << "};\n";
                    // Node3D initial positions [scene][node3d][3]
                    mc << "static const float kSceneNode3DPos[" << sceneCount << "][" << maxN3D << "][3] = {\n";
                    for (int si = 0; si < sceneCount; ++si)
                    {
                        mc << "{";
                        for (int ni = 0; ni < maxN3D; ++ni)
                        {
                            if (ni < (int)runtimeSceneNode3Ds[si].size())
                            {
                                const auto& ne = runtimeSceneNode3Ds[si][ni];
                                mc << "{" << fstr(ne.pos[0]) << "," << fstr(ne.pos[1]) << "," << fstr(ne.pos[2]) << "}";
                            }
                            else mc << "{0,0,0}";
                            if (ni + 1 < maxN3D) mc << ",";
                        }
                        mc << "}";
                        if (si + 1 < sceneCount) mc << ",";
                        mc << "\n";
                    }
                    mc << "};\n";
                    // Node3D extents [scene][node3d][3]
                    mc << "static const float kSceneNode3DExt[" << sceneCount << "][" << maxN3D << "][3] = {\n";
                    for (int si = 0; si < sceneCount; ++si)
                    {
                        mc << "{";
                        for (int ni = 0; ni < maxN3D; ++ni)
                        {
                            if (ni < (int)runtimeSceneNode3Ds[si].size())
                            {
                                const auto& ne = runtimeSceneNode3Ds[si][ni];
                                mc << "{" << fstr(ne.extent[0]) << "," << fstr(ne.extent[1]) << "," << fstr(ne.extent[2]) << "}";
                            }
                            else mc << "{0.5,0.5,0.5}";
                            if (ni + 1 < maxN3D) mc << ",";
                        }
                        mc << "}";
                        if (si + 1 < sceneCount) mc << ",";
                        mc << "\n";
                    }
                    mc << "};\n";
                    // Node3D bound positions [scene][node3d][3]
                    mc << "static const float kSceneNode3DBndPos[" << sceneCount << "][" << maxN3D << "][3] = {\n";
                    for (int si = 0; si < sceneCount; ++si)
                    {
                        mc << "{";
                        for (int ni = 0; ni < maxN3D; ++ni)
                        {
                            if (ni < (int)runtimeSceneNode3Ds[si].size())
                            {
                                const auto& ne = runtimeSceneNode3Ds[si][ni];
                                mc << "{" << fstr(ne.boundPos[0]) << "," << fstr(ne.boundPos[1]) << "," << fstr(ne.boundPos[2]) << "}";
                            }
                            else mc << "{0,0,0}";
                            if (ni + 1 < maxN3D) mc << ",";
                        }
                        mc << "}";
                        if (si + 1 < sceneCount) mc << ",";
                        mc << "\n";
                    }
                    mc << "};\n";
                    // Node3D physics enabled flags [scene][node3d]
                    mc << "static const int kSceneNode3DPhys[" << sceneCount << "][" << maxN3D << "] = {\n";
                    for (int si = 0; si < sceneCount; ++si)
                    {
                        mc << "{";
                        for (int ni = 0; ni < maxN3D; ++ni)
                        {
                            mc << ((ni < (int)runtimeSceneNode3Ds[si].size()) ? runtimeSceneNode3Ds[si][ni].physicsEnabled : 0);
                            if (ni + 1 < maxN3D) mc << ",";
                        }
                        mc << "}";
                        if (si + 1 < sceneCount) mc << ",";
                        mc << "\n";
                    }
                    mc << "};\n";
                    // Node3D collisionSource flags [scene][node3d]
                    mc << "static const int kSceneNode3DCollSrc[" << sceneCount << "][" << maxN3D << "] = {\n";
                    for (int si = 0; si < sceneCount; ++si)
                    {
                        mc << "{";
                        for (int ni = 0; ni < maxN3D; ++ni)
                        {
                            mc << ((ni < (int)runtimeSceneNode3Ds[si].size()) ? runtimeSceneNode3Ds[si][ni].collisionSource : 0);
                            if (ni + 1 < maxN3D) mc << ",";
                        }
                        mc << "}";
                        if (si + 1 < sceneCount) mc << ",";
                        mc << "\n";
                    }
                    mc << "};\n";
                    // Node3D simpleCollision flags [scene][node3d]
                    mc << "static const int kSceneNode3DSimColl[" << sceneCount << "][" << maxN3D << "] = {\n";
                    for (int si = 0; si < sceneCount; ++si)
                    {
                        mc << "{";
                        for (int ni = 0; ni < maxN3D; ++ni)
                        {
                            mc << ((ni < (int)runtimeSceneNode3Ds[si].size()) ? runtimeSceneNode3Ds[si][ni].simpleCollision : 0);
                            if (ni + 1 < maxN3D) mc << ",";
                        }
                        mc << "}";
                        if (si + 1 < sceneCount) mc << ",";
                        mc << "\n";
                    }
                    mc << "};\n";
                    // Per-mesh parent Node3D index (-1 = no parent) [scene][mesh]
                    mc << "static const int kSceneMeshParentN3D[" << sceneCount << "][MAX_MESHES] = {\n";
                    for (int si = 0; si < sceneCount; ++si)
                    {
                        mc << "{";
                        const auto& mp = runtimeSceneMeshParentN3D[si];
                        for (int mi = 0; mi < 64; ++mi) // MAX_MESHES=64
                        {
                            mc << ((mi < (int)mp.size()) ? mp[mi] : -1);
                            if (mi + 1 < 64) mc << ",";
                        }
                        mc << "}";
                        if (si + 1 < sceneCount) mc << ",";
                        mc << "\n";
                    }
                    mc << "};\n";
                    // DcNode3D struct/array/lookup already emitted above bridge functions
                    mc << "static void NB_InitNode3Ds(void){\n";
                    mc << "  int si = gSceneMetaIndex; if(si<0) si=0; if(si>=" << sceneCount << ") si=0;\n";
                    mc << "  gNode3DCount = kSceneNode3DCount[si]; if(gNode3DCount>MAX_NODE3D) gNode3DCount=MAX_NODE3D;\n";
                    mc << "  for(int ni=0; ni<gNode3DCount; ni++){\n";
                    mc << "    DcNode3D* nd = &gNode3Ds[ni];\n";
                    mc << "    nd->name=kSceneNode3DNames[si][ni];\n";
                    mc << "    nd->pos[0]=kSceneNode3DPos[si][ni][0]; nd->pos[1]=kSceneNode3DPos[si][ni][1]; nd->pos[2]=kSceneNode3DPos[si][ni][2];\n";
                    mc << "    nd->rot[0]=0; nd->rot[1]=0; nd->rot[2]=0;\n";
                    mc << "    nd->velY=0; nd->onFloor=0;\n";
                    mc << "    nd->extent[0]=kSceneNode3DExt[si][ni][0]; nd->extent[1]=kSceneNode3DExt[si][ni][1]; nd->extent[2]=kSceneNode3DExt[si][ni][2];\n";
                    mc << "    nd->boundPos[0]=kSceneNode3DBndPos[si][ni][0]; nd->boundPos[1]=kSceneNode3DBndPos[si][ni][1]; nd->boundPos[2]=kSceneNode3DBndPos[si][ni][2];\n";
                    mc << "    nd->physEnabled=kSceneNode3DPhys[si][ni];\n";
                    mc << "    nd->collisionSource=kSceneNode3DCollSrc[si][ni]; nd->simpleCollision=kSceneNode3DSimColl[si][ni];\n";
                    mc << "    nd->qw=1.0f; nd->qx=0.0f; nd->qy=0.0f; nd->qz=0.0f;\n";
                    mc << "  }\n";
                    mc << "}\n";
                }
                mc << "static const char* NB_ResolveMappedRef(const char* logical, const NB_RefMap* map, int count){ if(!logical||!logical[0]||!map||count<=0) return logical; for(int i=0;i<count;++i){ if((map[i].logical&&dc_eq_nocase(logical,map[i].logical))||(map[i].staged&&dc_eq_nocase(logical,map[i].staged))) return map[i].staged; } const char* slash=strrchr(logical,'/'); const char* name=slash?slash+1:logical; for(int i=0;i<count;++i){ if((map[i].logical&&dc_eq_nocase(name,map[i].logical))||(map[i].staged&&dc_eq_nocase(name,map[i].staged))) return map[i].staged; } return logical; }\n";
                mc << "static int NB_FindSceneMetaIndex(const char* sceneFile){ if(!sceneFile||!sceneFile[0]) return gSceneIndex; for(int i=0;i<gSceneCount;++i){ if(dc_eq_nocase(sceneFile,gSceneFiles[i])) return i; } const char* slash=strrchr(sceneFile,'/'); const char* name=slash?slash+1:sceneFile; for(int i=0;i<gSceneCount;++i){ if(dc_eq_nocase(name,gSceneFiles[i])) return i; } return gSceneIndex; }\n";
                // NB_ApplyLoadedSceneState: use parent Node3D transform for gMeshPos/gMeshRot (drives movement),
                // NOT the child StaticMesh3D transform (which is visual offset only).
                mc << "static int NB_ApplyLoadedSceneState(void){ int meshCount=NB_DC_GetSceneMeshCount(); if(meshCount<=0) return 0; if(meshCount>MAX_MESHES) meshCount=MAX_MESHES; gSceneMeshCount=meshCount; for(int mi=0; mi<gSceneMeshCount; ++mi){ SceneMeshMeta* sm=&gSceneMeshes[mi]; memset(sm,0,sizeof(*sm)); { const char* mesh=NB_DC_GetSceneMeshPathAt(mi); if(mesh&&mesh[0]){ strncpy(sm->meshLogical,mesh,sizeof(sm->meshLogical)-1); const char* rm=NB_ResolveMappedRef(mesh,kMeshRefMap,kMeshRefMapCount); strncpy(sm->meshDisk,rm?rm:mesh,sizeof(sm->meshDisk)-1); } } { float p[3]={0}, r[3]={0}, s[3]={1,1,1}; NB_DC_GetSceneTransformAt(mi,p,r,s); sm->pos[0]=p[0]; sm->pos[1]=p[1]; sm->pos[2]=p[2]; sm->rot[0]=r[0]; sm->rot[1]=r[1]; sm->rot[2]=r[2]; sm->scale[0]=s[0]; sm->scale[1]=s[1]; sm->scale[2]=s[2]; } for(int i=0;i<MAX_SLOT;++i){ const char* tp=NB_DC_GetSceneTexturePathAt(mi,i); if(tp&&tp[0]){ strncpy(sm->texLogical[i],tp,127); const char* rt=NB_ResolveMappedRef(tp,kTexRefMap,kTexRefMapCount); strncpy(sm->texDisk[i],rt?rt:tp,127); } } if(mi<MAX_MESHES){ const char* ap=kSceneAnimDisk[gSceneMetaIndex][mi]; if(ap&&ap[0]) strncpy(sm->animDisk,ap,sizeof(sm->animDisk)-1); sm->runtimeTest=kSceneRuntimeTest[gSceneMetaIndex][mi]?1:0; sm->collisionSource=kSceneCollisionSource[gSceneMetaIndex][mi]?1:0; { const char* mn=kSceneMeshName[gSceneMetaIndex][mi]; if(mn&&mn[0]){ strncpy(sm->meshName,mn,63); sm->meshName[63]=0; } else { sm->meshName[0]=0; } } sm->animSlotCount=kSceneAnimSlotCount[gSceneMetaIndex][mi]; sm->animActiveSlot=-1; sm->animTime=0.0f; sm->animSpeed=1.0f; sm->animPlaying=0; sm->animFinished=0; for(int ai=0;ai<MAX_ANIM_SLOTS;++ai){ const char* sn=kSceneAnimSlotName[gSceneMetaIndex][mi][ai]; const char* sd=kSceneAnimSlotDisk[gSceneMetaIndex][mi][ai]; if(sn&&sn[0]) strncpy(sm->animSlotName[ai],sn,31); else sm->animSlotName[ai][0]=0; if(sd&&sd[0]) strncpy(sm->animSlotDisk[ai],sd,127); else sm->animSlotDisk[ai][0]=0; sm->animSlotSpeed[ai]=kSceneAnimSlotSpeed[gSceneMetaIndex][mi][ai]; sm->animSlotLoop[ai]=kSceneAnimSlotLoop[gSceneMetaIndex][mi][ai]; } } } { const char* nm=NB_DC_GetSceneName(); if(nm&&nm[0]){ strncpy(gSceneName,nm,sizeof(gSceneName)-1); gSceneName[sizeof(gSceneName)-1]=0; } } "
                   "/* Use parent Node3D transform for movement (pos/rot), not child mesh. */ "
                   "{ int si=gSceneMetaIndex; if(si<0) si=0; if(si>=" << (int)runtimeSceneFiles.size() << ") si=0; "
                   "gMeshPos[0]=kScenePlayerParentPos[si][0]; gMeshPos[1]=kScenePlayerParentPos[si][1]; gMeshPos[2]=kScenePlayerParentPos[si][2]; "
                   "gMeshRot[0]=kScenePlayerParentRot[si][0]; gMeshRot[1]=kScenePlayerParentRot[si][1]; gMeshRot[2]=kScenePlayerParentRot[si][2]; "
                   "} "
                   "if(gSceneMeshCount>0){ gMeshScale[0]=gSceneMeshes[0].scale[0]; gMeshScale[1]=gSceneMeshes[0].scale[1]; gMeshScale[2]=gSceneMeshes[0].scale[2]; } "
                   "NB_InitNode3Ds(); return 1; }\n";
                mc << "static int NB_LoadScene(const char* sceneFile){ if(!sceneFile||!sceneFile[0]) return 0; gSceneMetaIndex=NB_FindSceneMetaIndex(sceneFile); gSceneIndex=gSceneMetaIndex; char path[256]; snprintf(path,sizeof(path),\"/cd/data/scenes/%s\",sceneFile); if(!NB_DC_LoadScene(path)) return 0; return NB_ApplyLoadedSceneState(); }\n";
                mc << "static int NB_LoadSceneIndex(int idx){ if(gSceneCount<=0) return 0; while(idx<0) idx+=gSceneCount; idx%=gSceneCount; gSceneIndex=idx; gSceneMetaIndex=idx; char path[256]; snprintf(path,sizeof(path),\"/cd/data/scenes/%s\",gSceneFiles[idx]); if(!NB_DC_SwitchScene(path)) return 0; if(!NB_ApplyLoadedSceneState()) return 0; return 1; }\n";
                mc << "static int NB_NextScene(void){ return NB_LoadSceneIndex(gSceneIndex+1); }\n";
                mc << "static int NB_PrevScene(void){ return NB_LoadSceneIndex(gSceneIndex-1); }\n";
                // Collision mesh cache — loaded once on first raycast, used every frame
                mc << "typedef struct { NB_Vec3* pos; uint16_t* idx; int vc; int tc; float wallThresh; uint8_t collisionWalls; } CollMeshCache;\n";
                mc << "static CollMeshCache gCollCache[MAX_MESHES];\n";
                mc << "static int gCollCacheCount = 0;\n";
                mc << "static int gCollCacheReady = 0;\n";
                mc << "static void NB_RT_BuildCollCache(void){\n";
                mc << "  if(gCollCacheReady) return;\n";
                mc << "  gCollCacheCount=0;\n";
                mc << "  for(int mi=0;mi<gSceneMeshCount&&mi<MAX_MESHES;mi++){\n";
                mc << "    if(!gSceneMeshes[mi].collisionSource) continue;\n";
                mc << "    if(!gSceneMeshes[mi].meshDisk[0]) continue;\n";
                mc << "    char mp[256]; snprintf(mp,sizeof(mp),\"/cd/data/meshes/%s\",gSceneMeshes[mi].meshDisk);\n";
                mc << "    NB_Mesh m; if(!NB_DC_LoadMesh(mp,&m)){ dbgio_printf(\"[NEBULA][DC] CollCache: failed to load %s\\n\",mp); continue; }\n";
                mc << "    CollMeshCache* c=&gCollCache[gCollCacheCount];\n";
                mc << "    c->pos=m.pos; c->idx=m.indices; c->vc=m.vert_count; c->tc=m.tri_count;\n";
                mc << "    { int si=gSceneMetaIndex; if(si<0) si=0; c->wallThresh=kSceneWallThreshold[si][mi]; c->collisionWalls=kSceneCollisionWalls[si][mi]; }\n";
                mc << "    /* Pre-transform vertices to world space: Scale -> Rotate -> Translate */\n";
                mc << "    { SceneMeshMeta* sm=&gSceneMeshes[mi];\n";
                mc << "      int _rsi=gSceneMetaIndex; if(_rsi<0) _rsi=0;\n";
                mc << "      int hasN3D=(mi<MAX_MESHES)?(kSceneMeshParentN3D[_rsi][mi]>=0):0;\n";
                mc << "      /* Standalone StaticMesh axis remap: X<-Z, Y<-X, Z<-Y (matches rendering) */\n";
                mc << "      float rxr=(hasN3D?sm->rot[0]:sm->rot[2])*0.0174532925f;\n";
                mc << "      float ryr=(hasN3D?sm->rot[1]:sm->rot[0])*0.0174532925f;\n";
                mc << "      float rzr=(hasN3D?sm->rot[2]:sm->rot[1])*0.0174532925f;\n";
                mc << "      float _sx=sinf(rxr),_cx=cosf(rxr),_sy=sinf(ryr),_cy=cosf(ryr),_sz=sinf(rzr),_cz=cosf(rzr);\n";
                mc << "      /* R = Rz*Ry*Rx */\n";
                mc << "      float rm00=_cy*_cz, rm01=_cz*_sx*_sy-_cx*_sz, rm02=_sx*_sz+_cx*_cz*_sy;\n";
                mc << "      float rm10=_cy*_sz, rm11=_cx*_cz+_sx*_sy*_sz, rm12=_cx*_sy*_sz-_cz*_sx;\n";
                mc << "      float rm20=-_sy,    rm21=_cy*_sx,              rm22=_cx*_cy;\n";
                mc << "      float px=sm->pos[0], py=sm->pos[1], pz=sm->pos[2];\n";
                mc << "      float scx=sm->scale[0], scy=sm->scale[1], scz=sm->scale[2];\n";
                mc << "      for(int vi=0;vi<c->vc;vi++){\n";
                mc << "        float vx=c->pos[vi].x*scx, vy=c->pos[vi].y*scy, vz=c->pos[vi].z*scz;\n";
                mc << "        c->pos[vi].x=rm00*vx+rm01*vy+rm02*vz+px;\n";
                mc << "        c->pos[vi].y=rm10*vx+rm11*vy+rm12*vz+py;\n";
                mc << "        c->pos[vi].z=rm20*vx+rm21*vy+rm22*vz+pz;\n";
                mc << "      }\n";
                mc << "    }\n";
                mc << "    free(m.tri_uv); free(m.tri_uv1); free(m.tri_mat);\n";
                mc << "    gCollCacheCount++;\n";
                mc << "  }\n";
                mc << "  gCollCacheReady=1;\n";
                mc << "  dbgio_printf(\"[NEBULA][DC] Collision cache: %d meshes\\n\", gCollCacheCount);\n";
                mc << "}\n";
                mc << "int NB_RT_RaycastDown(float rx, float ry, float rz, float* outHitY){\n";
                mc << "  if(!outHitY) return 0;\n";
                mc << "  if(!gCollCacheReady) NB_RT_BuildCollCache();\n";
                mc << "  int hit=0; float bestY=-1e30f;\n";
                mc << "  for(int ci=0;ci<gCollCacheCount;ci++){\n";
                mc << "    CollMeshCache* c=&gCollCache[ci];\n";
                mc << "    for(int t=0;t<c->tc;t++){\n";
                mc << "      int i0=c->idx[t*3],i1=c->idx[t*3+1],i2=c->idx[t*3+2];\n";
                mc << "      float ax=c->pos[i0].x, ay=c->pos[i0].y, az=c->pos[i0].z;\n";
                mc << "      float bx=c->pos[i1].x, by=c->pos[i1].y, bz=c->pos[i1].z;\n";
                mc << "      float cx=c->pos[i2].x, cy=c->pos[i2].y, cz=c->pos[i2].z;\n";
                mc << "      float e1x=bx-ax,e1z=bz-az, e2x=cx-ax,e2z=cz-az;\n";
                mc << "      float det=e1x*e2z-e2x*e1z; if(det>-1e-8f&&det<1e-8f) continue;\n";
                mc << "      float inv=1.0f/det;\n";
                mc << "      float dx=rx-ax, dz=rz-az;\n";
                mc << "      float u=(dx*e2z-dz*e2x)*inv; if(u<0.0f||u>1.0f) continue;\n";
                mc << "      float v=(e1x*dz-e1z*dx)*inv; if(v<0.0f||u+v>1.0f) continue;\n";
                mc << "      float hitY=ay+(by-ay)*u+(cy-ay)*v;\n";
                mc << "      if(hitY<=ry&&hitY>bestY){bestY=hitY; hit=1;}\n";
                mc << "    }\n";
                mc << "  }\n";
                mc << "  if(hit) *outHitY=bestY;\n";
                mc << "  return hit;\n";
                mc << "}\n";
                mc << "int NB_RT_RaycastDownWithNormal(float rx, float ry, float rz, float* outHitY, float outNormal[3]){\n";
                mc << "  if(!outHitY) return 0;\n";
                mc << "  if(!gCollCacheReady) NB_RT_BuildCollCache();\n";
                mc << "  int hit=0; float bestY=-1e30f; float bnx=0,bny=1,bnz=0;\n";
                mc << "  for(int ci=0;ci<gCollCacheCount;ci++){\n";
                mc << "    CollMeshCache* c=&gCollCache[ci];\n";
                mc << "    for(int t=0;t<c->tc;t++){\n";
                mc << "      int i0=c->idx[t*3],i1=c->idx[t*3+1],i2=c->idx[t*3+2];\n";
                mc << "      float ax=c->pos[i0].x, ay=c->pos[i0].y, az=c->pos[i0].z;\n";
                mc << "      float bx=c->pos[i1].x, by=c->pos[i1].y, bz=c->pos[i1].z;\n";
                mc << "      float cx=c->pos[i2].x, cy=c->pos[i2].y, cz=c->pos[i2].z;\n";
                mc << "      float e1x=bx-ax,e1z=bz-az, e2x=cx-ax,e2z=cz-az;\n";
                mc << "      float det=e1x*e2z-e2x*e1z; if(det>-1e-8f&&det<1e-8f) continue;\n";
                mc << "      float inv=1.0f/det;\n";
                mc << "      float dx=rx-ax, dz=rz-az;\n";
                mc << "      float u=(dx*e2z-dz*e2x)*inv; if(u<0.0f||u>1.0f) continue;\n";
                mc << "      float v=(e1x*dz-e1z*dx)*inv; if(v<0.0f||u+v>1.0f) continue;\n";
                mc << "      float hitY=ay+(by-ay)*u+(cy-ay)*v;\n";
                mc << "      if(hitY<=ry&&hitY>bestY){\n";
                mc << "        bestY=hitY; hit=1;\n";
                mc << "        float ex1=bx-ax,ey1=by-ay,ez1=bz-az, ex2=cx-ax,ey2=cy-ay,ez2=cz-az;\n";
                mc << "        float nx=ey1*ez2-ez1*ey2, ny=ez1*ex2-ex1*ez2, nz=ex1*ey2-ey1*ex2;\n";
                mc << "        float nl=sqrtf(nx*nx+ny*ny+nz*nz);\n";
                mc << "        if(nl>1e-8f){ float ni=1.0f/nl; bnx=nx*ni; bny=ny*ni; bnz=nz*ni; if(bny<0){bnx=-bnx;bny=-bny;bnz=-bnz;} }\n";
                mc << "      }\n";
                mc << "    }\n";
                mc << "  }\n";
                mc << "  if(hit){ *outHitY=bestY; if(outNormal){outNormal[0]=bnx;outNormal[1]=bny;outNormal[2]=bnz;} }\n";
                mc << "  return hit;\n";
                mc << "}\n";
                mc << "/* Closest point on triangle (Ericson, Real-Time Collision Detection 5.1.5) */\n";
                mc << "static void dc_closest_pt_tri(float px,float py,float pz, float ax,float ay,float az, float bx,float by,float bz, float tcx,float tcy,float tcz, float*ox,float*oy,float*oz){\n";
                mc << "  float abx=bx-ax,aby=by-ay,abz=bz-az, acx=tcx-ax,acy=tcy-ay,acz=tcz-az;\n";
                mc << "  float apx=px-ax,apy=py-ay,apz=pz-az;\n";
                mc << "  float d1=abx*apx+aby*apy+abz*apz, d2=acx*apx+acy*apy+acz*apz;\n";
                mc << "  if(d1<=0.0f&&d2<=0.0f){*ox=ax;*oy=ay;*oz=az;return;}\n";
                mc << "  float bpx=px-bx,bpy=py-by,bpz=pz-bz;\n";
                mc << "  float d3=abx*bpx+aby*bpy+abz*bpz, d4=acx*bpx+acy*bpy+acz*bpz;\n";
                mc << "  if(d3>=0.0f&&d4<=d3){*ox=bx;*oy=by;*oz=bz;return;}\n";
                mc << "  float vc=d1*d4-d3*d2;\n";
                mc << "  if(vc<=0.0f&&d1>=0.0f&&d3<=0.0f){float v=d1/(d1-d3);*ox=ax+v*abx;*oy=ay+v*aby;*oz=az+v*abz;return;}\n";
                mc << "  float cpx=px-tcx,cpy=py-tcy,cpz=pz-tcz;\n";
                mc << "  float d5=abx*cpx+aby*cpy+abz*cpz, d6=acx*cpx+acy*cpy+acz*cpz;\n";
                mc << "  if(d6>=0.0f&&d5<=d6){*ox=tcx;*oy=tcy;*oz=tcz;return;}\n";
                mc << "  float vb=d5*d2-d1*d6;\n";
                mc << "  if(vb<=0.0f&&d2>=0.0f&&d6<=0.0f){float w=d2/(d2-d6);*ox=ax+w*acx;*oy=ay+w*acy;*oz=az+w*acz;return;}\n";
                mc << "  float va=d3*d6-d5*d4;\n";
                mc << "  if(va<=0.0f&&(d4-d3)>=0.0f&&(d5-d6)>=0.0f){float w=(d4-d3)/((d4-d3)+(d5-d6));*ox=bx+w*(tcx-bx);*oy=by+w*(tcy-by);*oz=bz+w*(tcz-bz);return;}\n";
                mc << "  float dn=1.0f/(va+vb+vc); float v=vb*dn, w=vc*dn;\n";
                mc << "  *ox=ax+abx*v+acx*w; *oy=ay+aby*v+acy*w; *oz=az+abz*v+acz*w;\n";
                mc << "}\n";
                mc << "/* Wall collision: closest-point + axis-aligned push-out */\n";
                mc << "static void dc_wall_collide(float cx, float cy, float cz, float hx, float hy, float hz, float* outPX, float* outPZ){\n";
                mc << "  *outPX=0; *outPZ=0;\n";
                mc << "  float mpPX=0,mpNX=0,mpPZ=0,mpNZ=0;\n";
                mc << "  if(!gCollCacheReady) NB_RT_BuildCollCache();\n";
                mc << "  for(int ci=0;ci<gCollCacheCount;ci++){\n";
                mc << "    CollMeshCache* c=&gCollCache[ci];\n";
                mc << "    if(c->collisionWalls) continue;\n";
                mc << "    for(int t=0;t<c->tc;t++){\n";
                mc << "      int i0=c->idx[t*3],i1=c->idx[t*3+1],i2=c->idx[t*3+2];\n";
                mc << "      float ax=c->pos[i0].x, ay=c->pos[i0].y, az=c->pos[i0].z;\n";
                mc << "      float bx=c->pos[i1].x, by=c->pos[i1].y, bz=c->pos[i1].z;\n";
                mc << "      float tx=c->pos[i2].x, ty=c->pos[i2].y, tz=c->pos[i2].z;\n";
                mc << "      float ex1=bx-ax,ey1=by-ay,ez1=bz-az, ex2=tx-ax,ey2=ty-ay,ez2=tz-az;\n";
                mc << "      float nx=ey1*ez2-ez1*ey2, ny=ez1*ex2-ex1*ez2, nz=ex1*ey2-ey1*ex2;\n";
                mc << "      float nl=sqrtf(nx*nx+ny*ny+nz*nz); if(nl<1e-8f) continue;\n";
                mc << "      float ni=1.0f/nl; nx*=ni; ny*=ni; nz*=ni;\n";
                mc << "      if(ny>c->wallThresh || ny<-c->wallThresh) continue;\n";
                mc << "      float tminy=ay; if(by<tminy) tminy=by; if(ty<tminy) tminy=ty;\n";
                mc << "      float tmaxy=ay; if(by>tmaxy) tmaxy=by; if(ty>tmaxy) tmaxy=ty;\n";
                mc << "      if(cy-hy>=tmaxy || cy+hy<=tminy) continue;\n";
                mc << "      float tminx=ax; if(bx<tminx) tminx=bx; if(tx<tminx) tminx=tx;\n";
                mc << "      float tmaxx=ax; if(bx>tmaxx) tmaxx=bx; if(tx>tmaxx) tmaxx=tx;\n";
                mc << "      float tminz=az; if(bz<tminz) tminz=bz; if(tz<tminz) tminz=tz;\n";
                mc << "      float tmaxz=az; if(bz>tmaxz) tmaxz=bz; if(tz>tmaxz) tmaxz=tz;\n";
                mc << "      if(cx+hx<tminx || cx-hx>tmaxx) continue;\n";
                mc << "      if(cz+hz<tminz || cz-hz>tmaxz) continue;\n";
                mc << "      float cpX,cpY,cpZ;\n";
                mc << "      dc_closest_pt_tri(cx,cy,cz, ax,ay,az, bx,by,bz, tx,ty,tz, &cpX,&cpY,&cpZ);\n";
                mc << "      if(cpX<cx-hx||cpX>cx+hx||cpY<cy-hy||cpY>cy+hy||cpZ<cz-hz||cpZ>cz+hz) continue;\n";
                mc << "      float overX=hx-fabsf(cpX-cx), overZ=hz-fabsf(cpZ-cz);\n";
                mc << "      if(overX<=0.0f||overZ<=0.0f) continue;\n";
                mc << "      const float kSkin=0.01f;\n";
                mc << "      float px,pz;\n";
                mc << "      if(overX<overZ){ float dir=(cx>=cpX)?1.0f:-1.0f; px=dir*(overX+kSkin); pz=0; }\n";
                mc << "      else{ px=0; float dir=(cz>=cpZ)?1.0f:-1.0f; pz=dir*(overZ+kSkin); }\n";
                mc << "      if(px>0&&px>mpPX) mpPX=px; if(px<0&&px<mpNX) mpNX=px;\n";
                mc << "      if(pz>0&&pz>mpPZ) mpPZ=pz; if(pz<0&&pz<mpNZ) mpNZ=pz;\n";
                mc << "    }\n";
                mc << "  }\n";
                mc << "  *outPX=mpPX+mpNX; *outPZ=mpPZ+mpNZ;\n";
                mc << "}\n";
                mc << "\n";
                mc << "static inline float deg2rad(float d){ return d*0.0174532925f; }\n";
                mc << "\n";
                mc << "static V3 rot_xyz(V3 v, float rx, float ry, float rz) {\n";
                mc << "  float sx=sinf(rx), cx=cosf(rx), sy=sinf(ry), cy=cosf(ry), sz=sinf(rz), cz=cosf(rz);\n";
                mc << "  float x=v.x*cz - v.y*sz, y=v.x*sz + v.y*cz; v.x=x; v.y=y;\n";
                mc << "  x=v.x*cy + v.z*sy; float z=-v.x*sy + v.z*cy; v.x=x; v.z=z;\n";
                mc << "  y=v.y*cx - v.z*sx; z=v.y*sx + v.z*cx; v.y=y; v.z=z;\n";
                mc << "  return v;\n";
                mc << "}\n";
                mc << "\n";
                mc << "static float dot3(V3 a, V3 b){ return a.x*b.x + a.y*b.y + a.z*b.z; }\n";
                mc << "static V3 sub3(V3 a, V3 b){ V3 r={a.x-b.x,a.y-b.y,a.z-b.z}; return r; }\n";
                mc << "static V3 cross3(V3 a, V3 b){ V3 r={a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; return r; }\n";
                mc << "/* SH4 fsrra + Newton-Raphson: fast 1/sqrt(x) with near-full precision */\n";
                mc << "static inline float sh4_rsqrt(float x){ float e; __asm__ __volatile__(\"fsrra %0\" : \"=f\"(e) : \"0\"(x)); e=e*(1.5f-0.5f*x*e*e); return e; }\n";
                mc << "static V3 norm3(V3 v){ float d=v.x*v.x+v.y*v.y+v.z*v.z; if(d<1e-12f){V3 z={0,0,1}; return z;} float inv=sh4_rsqrt(d); V3 r={v.x*inv,v.y*inv,v.z*inv}; return r; }\n";
                mc << "\n";
                // QuatFromNormalAndYaw + slope alignment for DC
                mc << "/* Slope alignment: quaternion from surface normal + yaw */\n";
                mc << "static void dc_quat_from_normal_yaw(float nx, float ny, float nz, float yawDeg, float* oqw, float* oqx, float* oqy, float* oqz){\n";
                mc << "  float len=sqrtf(nx*nx+ny*ny+nz*nz);\n";
                mc << "  if(len<0.0001f){*oqw=1;*oqx=0;*oqy=0;*oqz=0;return;}\n";
                mc << "  float ux=nx/len, uy=ny/len, uz=nz/len;\n";
                mc << "  float yawRad=yawDeg*0.0174532925f;\n";
                mc << "  float fwdX=sinf(yawRad), fwdZ=cosf(yawRad);\n";
                mc << "  float dot=fwdX*ux+fwdZ*uz;\n";
                mc << "  float pfx=fwdX-dot*ux, pfy=-dot*uy, pfz=fwdZ-dot*uz;\n";
                mc << "  float pfLen=sqrtf(pfx*pfx+pfy*pfy+pfz*pfz);\n";
                mc << "  if(pfLen<0.0001f){\n";
                mc << "    /* Degenerate — use pure yaw quat */\n";
                mc << "    float hy=yawRad*0.5f; *oqw=cosf(hy); *oqx=0; *oqy=sinf(hy); *oqz=0; return;\n";
                mc << "  }\n";
                mc << "  pfx/=pfLen; pfy/=pfLen; pfz/=pfLen;\n";
                mc << "  float rx=uy*pfz-uz*pfy, ry=uz*pfx-ux*pfz, rz=ux*pfy-uy*pfx;\n";
                mc << "  float rLen=sqrtf(rx*rx+ry*ry+rz*rz);\n";
                mc << "  if(rLen>0.0001f){rx/=rLen;ry/=rLen;rz/=rLen;}\n";
                mc << "  float m00=rx,m01=ux,m02=pfx, m10=ry,m11=uy,m12=pfy, m20=rz,m21=uz,m22=pfz;\n";
                mc << "  float trace=m00+m11+m22;\n";
                mc << "  float qw,qx,qy,qz;\n";
                mc << "  if(trace>0){float s=0.5f/sqrtf(trace+1.0f);qw=0.25f/s;qx=(m21-m12)*s;qy=(m02-m20)*s;qz=(m10-m01)*s;}\n";
                mc << "  else if(m00>m11&&m00>m22){float s=2.0f*sqrtf(1.0f+m00-m11-m22);qw=(m21-m12)/s;qx=0.25f*s;qy=(m01+m10)/s;qz=(m02+m20)/s;}\n";
                mc << "  else if(m11>m22){float s=2.0f*sqrtf(1.0f+m11-m00-m22);qw=(m02-m20)/s;qx=(m01+m10)/s;qy=0.25f*s;qz=(m12+m21)/s;}\n";
                mc << "  else{float s=2.0f*sqrtf(1.0f+m22-m00-m11);qw=(m10-m01)/s;qx=(m02+m20)/s;qy=(m12+m21)/s;qz=0.25f*s;}\n";
                mc << "  float ql=sqrtf(qw*qw+qx*qx+qy*qy+qz*qz);\n";
                mc << "  if(ql>0.0001f){qw/=ql;qx/=ql;qy/=ql;qz/=ql;}\n";
                mc << "  *oqw=qw;*oqx=qx;*oqy=qy;*oqz=qz;\n";
                mc << "}\n";
                mc << "static void dc_slope_align(float hnx, float hny, float hnz, float savedYaw, float dt){\n";
                mc << "  float curUpX=2.0f*(gQx*gQy-gQw*gQz);\n";
                mc << "  float curUpY=1.0f-2.0f*(gQx*gQx+gQz*gQz);\n";
                mc << "  float curUpZ=2.0f*(gQy*gQz+gQw*gQx);\n";
                mc << "  float t=1.0f-powf(0.0001f,dt);\n";
                mc << "  float tNX=(hny>0.0f)?hnx:0.0f, tNY=(hny>0.0f)?hny:1.0f, tNZ=(hny>0.0f)?hnz:0.0f;\n";
                mc << "  float smX=curUpX+(tNX-curUpX)*t;\n";
                mc << "  float smY=curUpY+(tNY-curUpY)*t;\n";
                mc << "  float smZ=curUpZ+(tNZ-curUpZ)*t;\n";
                mc << "  float smLen=sqrtf(smX*smX+smY*smY+smZ*smZ);\n";
                mc << "  if(smLen>0.0001f){smX/=smLen;smY/=smLen;smZ/=smLen;}\n";
                mc << "  dc_quat_from_normal_yaw(smX,smY,smZ,savedYaw,&gQw,&gQx,&gQy,&gQz);\n";
                mc << "}\n";
                mc << "/* Slope alignment for non-player Node3Ds (uses nd->qw/qx/qy/qz) */\n";
                mc << "static void dc_slope_align_nd(DcNode3D* nd, float hnx, float hny, float hnz, float savedYaw, float dt){\n";
                mc << "  float curUpX=2.0f*(nd->qx*nd->qy-nd->qw*nd->qz);\n";
                mc << "  float curUpY=1.0f-2.0f*(nd->qx*nd->qx+nd->qz*nd->qz);\n";
                mc << "  float curUpZ=2.0f*(nd->qy*nd->qz+nd->qw*nd->qx);\n";
                mc << "  float t=1.0f-powf(0.0001f,dt);\n";
                mc << "  float tNX=(hny>0.0f)?hnx:0.0f, tNY=(hny>0.0f)?hny:1.0f, tNZ=(hny>0.0f)?hnz:0.0f;\n";
                mc << "  float smX=curUpX+(tNX-curUpX)*t;\n";
                mc << "  float smY=curUpY+(tNY-curUpY)*t;\n";
                mc << "  float smZ=curUpZ+(tNZ-curUpZ)*t;\n";
                mc << "  float smLen=sqrtf(smX*smX+smY*smY+smZ*smZ);\n";
                mc << "  if(smLen>0.0001f){smX/=smLen;smY/=smLen;smZ/=smLen;}\n";
                mc << "  dc_quat_from_normal_yaw(smX,smY,smZ,savedYaw,&nd->qw,&nd->qx,&nd->qy,&nd->qz);\n";
                mc << "}\n";
                mc << "\n";
                mc << "/* Camera basis (cached per frame) */\n";
                mc << "static V3 gBF,gBR,gBU,gBE;\n";
                mc << "static void cam_update_basis(void){\n";
                mc << "  gBE=(V3){gCamPos[0],gCamPos[1],gCamPos[2]};\n";
                mc << "  gBF=norm3((V3){gCamForward[0],gCamForward[1],gCamForward[2]});\n";
                mc << "  gBU=norm3((V3){gCamUp[0],gCamUp[1],gCamUp[2]});\n";
                mc << "  gBR=norm3(cross3(gBU,gBF));\n";
                mc << "  if(fabsf(dot3(gBR,gBR))<1e-6f) gBR=norm3((V3){gCamRight[0],gCamRight[1],gCamRight[2]});\n";
                mc << "  if(fabsf(dot3(gBR,gBR))<1e-6f) gBR=norm3(cross3((V3){0,1,0},gBF));\n";
                mc << "  gBU=norm3(cross3(gBF,gBR));\n";
                mc << "}\n";
                mc << "static V3 w2c(V3 w){ V3 d=sub3(w,gBE); return (V3){dot3(d,gBR),dot3(d,gBU),dot3(d,gBF)}; }\n";
                mc << "static const float kClipNear = 0.05f;\n";
                mc << "/* Frustum side-plane slopes (with 10% margin to avoid edge popping) */\n";
                mc << "static const float kFrustSlopeX = (kProjViewW * 0.5f * 1.1f) / kProjFocalX;\n";
                mc << "static const float kFrustSlopeY = (kProjViewH * 0.5f * 1.1f) / kProjFocalY;\n";
                mc << "static void proj_sv(float cx, float cy, float cz, float u, float v, SV *out){\n";
                mc << "  out->x=(kProjViewW*0.5f)+(cx/cz)*kProjFocalX;\n";
                mc << "  out->y=(kProjViewH*0.5f)-(cy/cz)*kProjFocalY;\n";
                mc << "  out->z=1.0f/cz; out->u=u; out->v=v;\n";
                mc << "}\n";
                mc << "static int proj_cs(V3 cp, SV *out){\n";
                mc << "  if(cp.z<kClipNear) return 0;\n";
                mc << "  out->x=(kProjViewW*0.5f)+(cp.x/cp.z)*kProjFocalX;\n";
                mc << "  out->y=(kProjViewH*0.5f)-(cp.y/cp.z)*kProjFocalY;\n";
                mc << "  out->z=1.0f/cp.z;\n";
                mc << "  return 1;\n";
                mc << "}\n";
                mc << "static int project_point(V3 wp, SV *out){ return proj_cs(w2c(wp),out); }\n";
                mc << "/* Sutherland-Hodgman clip: clip polygon against plane nx*x+ny*y+nz*z+nd>=0 */\n";
                mc << "typedef struct { float x,y,z,u,v,lit; } CV;\n";
                mc << "static int clip_poly(CV* in, int n, CV* out, float nx, float ny, float nz, float nd){\n";
                mc << "  if(n<3) return 0;\n";
                mc << "  int m=0;\n";
                mc << "  for(int i=0;i<n;i++){\n";
                mc << "    CV *a=&in[i], *b=&in[(i+1)%n];\n";
                mc << "    float da=a->x*nx+a->y*ny+a->z*nz+nd;\n";
                mc << "    float db=b->x*nx+b->y*ny+b->z*nz+nd;\n";
                mc << "    if(da>=0) out[m++]=*a;\n";
                mc << "    if((da>=0)!=(db>=0)){\n";
                mc << "      float t=da/(da-db);\n";
                mc << "      out[m].x=a->x+t*(b->x-a->x); out[m].y=a->y+t*(b->y-a->y); out[m].z=a->z+t*(b->z-a->z);\n";
                mc << "      out[m].u=a->u+t*(b->u-a->u); out[m].v=a->v+t*(b->v-a->v); out[m].lit=a->lit+t*(b->lit-a->lit); m++;\n";
                mc << "    }\n";
                mc << "  }\n";
                mc << "  return m;\n";
                mc << "}\n";
                mc << "\n";
                mc << "static void draw_tri(pvr_poly_hdr_t *hdr, SV a, SV b, SV c, uint32 argb) {\n";
                mc << "  pvr_vertex_t v;\n";
                mc << "  pvr_prim(hdr, sizeof(*hdr));\n";
                mc << "  v.flags = PVR_CMD_VERTEX; v.x=a.x; v.y=a.y; v.z=a.z; v.u=a.u; v.v=a.v; v.argb=argb; v.oargb=0; pvr_prim(&v,sizeof(v));\n";
                mc << "  v.flags = PVR_CMD_VERTEX; v.x=b.x; v.y=b.y; v.z=b.z; v.u=b.u; v.v=b.v; v.argb=argb; v.oargb=0; pvr_prim(&v,sizeof(v));\n";
                mc << "  v.flags = PVR_CMD_VERTEX_EOL; v.x=c.x; v.y=c.y; v.z=c.z; v.u=c.u; v.v=c.v; v.argb=argb; v.oargb=0; pvr_prim(&v,sizeof(v));\n";
                mc << "}\n";
                mc << "static void draw_tri_vc(pvr_poly_hdr_t *hdr, SV a, SV b, SV c, uint32 ca, uint32 cb, uint32 cc) {\n";
                mc << "  pvr_vertex_t v;\n";
                mc << "  pvr_prim(hdr, sizeof(*hdr));\n";
                mc << "  v.flags = PVR_CMD_VERTEX; v.x=a.x; v.y=a.y; v.z=a.z; v.u=a.u; v.v=a.v; v.argb=ca; v.oargb=0; pvr_prim(&v,sizeof(v));\n";
                mc << "  v.flags = PVR_CMD_VERTEX; v.x=b.x; v.y=b.y; v.z=b.z; v.u=b.u; v.v=b.v; v.argb=cb; v.oargb=0; pvr_prim(&v,sizeof(v));\n";
                mc << "  v.flags = PVR_CMD_VERTEX_EOL; v.x=c.x; v.y=c.y; v.z=c.z; v.u=c.u; v.v=c.v; v.argb=cc; v.oargb=0; pvr_prim(&v,sizeof(v));\n";
                mc << "}\n";
                mc << "typedef struct { int loaded; int vertCount; int frameCount; float fps; V3* frames; V3* posed; int16_t* remap; int remapCount; int meshAligned; } RuntimeAnimClip;\n";
                mc << "static int rd_u32be(FILE* f, uint32_t* out){ uint8_t b[4]; if(fread(b,1,4,f)!=4) return 0; *out=((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|(uint32_t)b[3]; return 1; }\n";
                mc << "static int rd_s32be(FILE* f, int32_t* out){ uint32_t u=0; if(!rd_u32be(f,&u)) return 0; *out=(int32_t)u; return 1; }\n";
                mc << "static int rd_s16be(FILE* f, int16_t* out){ uint8_t b[2]; if(fread(b,1,2,f)!=2) return 0; *out=(int16_t)(((uint16_t)b[0]<<8)|(uint16_t)b[1]); return 1; }\n";
                mc << "static void runtime_anim_free(RuntimeAnimClip* a){ if(!a) return; if(a->frames){ free(a->frames); a->frames=0; } if(a->posed){ free(a->posed); a->posed=0; } if(a->remap){ free(a->remap); a->remap=0; } a->loaded=0; a->vertCount=0; a->frameCount=0; a->fps=0.0f; a->remapCount=0; a->meshAligned=0; }\n";
                mc << "static int runtime_anim_load(const char* path, RuntimeAnimClip* out){ if(!path||!path[0]||!out) return 0; FILE* f=fopen(path,\"rb\"); if(!f) return 0; char m[4]; if(fread(m,1,4,f)!=4 || m[0]!='N'||m[1]!='E'||m[2]!='B'||m[3]!='0'){ fclose(f); return 0; } uint32_t ver=0,flags=0,vc=0,fc=0,fpsFx=0,frac=8; if(!rd_u32be(f,&ver)){ fclose(f); return 0; } if(ver>=3){ if(!rd_u32be(f,&flags)){ fclose(f); return 0; } } if(!rd_u32be(f,&vc)||!rd_u32be(f,&fc)||!rd_u32be(f,&fpsFx)){ fclose(f); return 0; } if(ver>=3){ if(!rd_u32be(f,&frac)){ fclose(f); return 0; } } if(vc==0||fc==0||vc>65535||fc>4096){ fclose(f); return 0; } V3* frames=(V3*)malloc((size_t)vc*(size_t)fc*sizeof(V3)); V3* posed=(V3*)malloc((size_t)vc*sizeof(V3)); if(!frames||!posed){ if(frames) free(frames); if(posed) free(posed); fclose(f); return 0; } const float inv16=1.0f/65536.0f; const float dinv=1.0f/(float)(1u<<(frac>24?24:frac)); int denc=(flags&1u)?1:0; for(uint32_t fr=0; fr<fc; ++fr){ for(uint32_t i=0;i<vc;++i){ V3 v={0,0,0}; if(!denc||fr==0){ int32_t x=0,y=0,z=0; if(!rd_s32be(f,&x)||!rd_s32be(f,&y)||!rd_s32be(f,&z)){ free(frames); free(posed); fclose(f); return 0; } v.x=(float)x*inv16; v.y=(float)y*inv16; v.z=(float)z*inv16; } else { int16_t dx=0,dy=0,dz=0; if(!rd_s16be(f,&dx)||!rd_s16be(f,&dy)||!rd_s16be(f,&dz)){ free(frames); free(posed); fclose(f); return 0; } V3 p=frames[(size_t)(fr-1u)*(size_t)vc+(size_t)i]; v.x=p.x+(float)dx*dinv; v.y=p.y+(float)dy*dinv; v.z=p.z+(float)dz*dinv; } frames[(size_t)fr*(size_t)vc+(size_t)i]=v; } } fclose(f); out->loaded=1; out->vertCount=(int)vc; out->frameCount=(int)fc; out->fps=(fpsFx>0)?((float)(int32_t)fpsFx*inv16):12.0f; if(out->fps<=0.001f) out->fps=12.0f; out->frames=frames; out->posed=posed; memcpy(out->posed, out->frames, (size_t)vc*sizeof(V3)); out->meshAligned=(flags&4u)?1:0; return 1; }\n";
                mc << "\n";
                // RuntimeSceneMesh typedef and meshRt at file scope (needed by animation bridge)
                mc << "typedef struct { RuntimeMesh diskMesh; RuntimeMesh activeMesh; int kVertCount; int kTriCount; V3* base; uint16_t* trisNormal; V3* triUvNormal; uint16_t* triMatNormal; uint16_t* trisFlipped; V3* triUvFlipped; pvr_poly_hdr_t hdrSlot[MAX_SLOT]; pvr_poly_hdr_t hdrSlotTr[MAX_SLOT]; float slotOpacity[MAX_SLOT]; pvr_ptr_t slotTx[MAX_SLOT]; RuntimeTex diskTex[MAX_SLOT]; uint16_t slotW[MAX_SLOT]; uint16_t slotH[MAX_SLOT]; float slotUS[MAX_SLOT]; float slotVS[MAX_SLOT]; float slotUvScaleU[MAX_SLOT]; float slotUvScaleV[MAX_SLOT]; float slotHalfU[MAX_SLOT]; float slotHalfV[MAX_SLOT]; uint8_t slotFilter[MAX_SLOT]; uint8_t slotReady[MAX_SLOT]; uint8_t slotWrap[MAX_SLOT]; uint8_t slotFlipU[MAX_SLOT]; uint8_t slotFlipV[MAX_SLOT]; uint8_t hasExtUv; int slotCount; int loaded; int* weldGroups; int nWeldGroups; V3* weldNrmBuf; RuntimeAnimClip anim; } RuntimeSceneMesh;\n";
                mc << "static RuntimeSceneMesh meshRt[MAX_MESHES];\n";
                mc << "\n";
                // Animation slot bridge functions (after all types/globals are defined)
                mc << "static int dc_find_mesh_by_name(const char* name){ if(!name||!name[0]) return -1; for(int mi=0;mi<gSceneMeshCount&&mi<MAX_MESHES;++mi){ if(gSceneMeshes[mi].meshName[0]&&dc_eq_nocase(name,gSceneMeshes[mi].meshName)) return mi; } return -1; }\n";
                mc << "void NB_RT_PlayAnimation(const char* meshName, const char* animName){\n";
                mc << "  int mi=dc_find_mesh_by_name(meshName); if(mi<0) return;\n";
                mc << "  SceneMeshMeta* sm=&gSceneMeshes[mi];\n";
                mc << "  for(int si=0;si<sm->animSlotCount&&si<MAX_ANIM_SLOTS;++si){\n";
                mc << "    if(dc_eq_nocase(animName,sm->animSlotName[si])){\n";
                mc << "      if(sm->animActiveSlot!=si){\n";
                mc << "        runtime_anim_free(&meshRt[mi].anim);\n";
                mc << "        if(sm->animSlotDisk[si][0]){\n";
                mc << "          char ap[256]; snprintf(ap,sizeof(ap),\"/cd/data/animations/%s\",sm->animSlotDisk[si]);\n";
                mc << "          { char resolved[512]; FILE* fp=dc_fopen_iso_compat(ap,resolved,(int)sizeof(resolved)); if(fp){ fclose(fp); strncpy(ap,resolved,sizeof(ap)-1); ap[sizeof(ap)-1]=0; } }\n";
                mc << "          runtime_anim_load(ap,&meshRt[mi].anim);\n";
                mc << "          if(meshRt[mi].anim.vertCount<meshRt[mi].kVertCount){\n";
                mc << "            V3* np=(V3*)realloc(meshRt[mi].anim.posed,(size_t)meshRt[mi].kVertCount*sizeof(V3));\n";
                mc << "            if(np){ for(int vi=meshRt[mi].anim.vertCount;vi<meshRt[mi].kVertCount;++vi) np[vi]=meshRt[mi].base[vi]; meshRt[mi].anim.posed=np; }\n";
                mc << "          }\n";
                mc << "        }\n";
                mc << "        sm->animActiveSlot=si;\n";
                mc << "      }\n";
                mc << "      sm->animTime=0.0f; sm->animSpeed=sm->animSlotSpeed[si]; sm->animLoop=sm->animSlotLoop[si]; sm->animPlaying=1; sm->animFinished=0;\n";
                mc << "      return;\n";
                mc << "    }\n";
                mc << "  }\n";
                mc << "}\n";
                mc << "void NB_RT_StopAnimation(const char* meshName){\n";
                mc << "  int mi=dc_find_mesh_by_name(meshName); if(mi<0) return;\n";
                mc << "  gSceneMeshes[mi].animPlaying=0;\n";
                mc << "}\n";
                mc << "int NB_RT_IsAnimationPlaying(const char* meshName){\n";
                mc << "  int mi=dc_find_mesh_by_name(meshName); if(mi<0) return 0;\n";
                mc << "  return gSceneMeshes[mi].animPlaying;\n";
                mc << "}\n";
                mc << "int NB_RT_IsAnimationFinished(const char* meshName){\n";
                mc << "  int mi=dc_find_mesh_by_name(meshName); if(mi<0) return 0;\n";
                mc << "  return gSceneMeshes[mi].animFinished;\n";
                mc << "}\n";
                mc << "void NB_RT_SetAnimationSpeed(const char* meshName, float speed){\n";
                mc << "  int mi=dc_find_mesh_by_name(meshName); if(mi<0) return;\n";
                mc << "  gSceneMeshes[mi].animSpeed=speed;\n";
                mc << "}\n";
                mc << "\n";
                mc << "int main(int argc, char **argv) {\n";
                mc << "  (void)argc; (void)argv;\n";
                mc << "  pvr_init_defaults();\n";
                mc << "  dbgio_dev_select(\"fb\");\n";
                mc << "  dbgio_printf(\"Nebula Dreamcast Runtime (Scene Export v2)\\n\");\n";
                mc << "  dbgio_printf(\"[NEBULA][DC] Loading scene: %s (sceneCount=%d)\\n\", kDefaultSceneFile, gSceneCount);\n";
                mc << "  if (!NB_LoadScene(kDefaultSceneFile)) {\n";
                mc << "    if (!NB_LoadSceneIndex(0)) {\n";
                mc << "      dbgio_printf(\"[NEBULA][DC] Scene load failed: %s\\n\", kDefaultSceneFile);\n";
                mc << "      return 1;\n";
                mc << "    }\n";
                mc << "  }\n";
                mc << "  dbgio_printf(\"[NEBULA][DC] Scene loaded: meshCount=%d sceneName=%s\\n\", gSceneMeshCount, gSceneName);\n";
                mc << "  for(int mi=0; mi<gSceneMeshCount && mi<MAX_MESHES; ++mi) { dbgio_printf(\"[NEBULA][DC]   mesh[%d] disk=%s anim=%s rt=%d pos=(%.2f,%.2f,%.2f)\\n\", mi, gSceneMeshes[mi].meshDisk, gSceneMeshes[mi].animDisk, gSceneMeshes[mi].runtimeTest, gSceneMeshes[mi].pos[0], gSceneMeshes[mi].pos[1], gSceneMeshes[mi].pos[2]); }\n";
                mc << "  gPlayerMeshIdx = -1;\n";
                mc << "  for(int mi=0; mi<gSceneMeshCount && mi<MAX_MESHES; ++mi) { if(strcmp(gSceneMeshes[mi].meshDisk, kPlayerMeshDisk)==0){ gPlayerMeshIdx=mi; break; } }\n";
                mc << "  dbgio_printf(\"[NEBULA][DC] Player mesh idx=%d disk=%s\\n\", gPlayerMeshIdx, kPlayerMeshDisk);\n";
                mc << "  { V3 f=norm3((V3){gCamForward[0],gCamForward[1],gCamForward[2]}); V3 u=norm3((V3){gCamUp[0],gCamUp[1],gCamUp[2]}); V3 r=norm3(cross3(u,f)); if (fabsf(dot3(r,r)) < 1e-6f) r=norm3((V3){gCamRight[0],gCamRight[1],gCamRight[2]}); if (fabsf(dot3(r,r)) < 1e-6f) r=norm3(cross3((V3){0,1,0},f)); u=norm3(cross3(f,r)); dbgio_printf(\"[CameraParity][Runtime] eye=(%.3f,%.3f,%.3f) f=(%.3f,%.3f,%.3f) r=(%.3f,%.3f,%.3f) u=(%.3f,%.3f,%.3f)\\n\", gCamPos[0],gCamPos[1],gCamPos[2],f.x,f.y,f.z,r.x,r.y,r.z,u.x,u.y,u.z); }\n";
                mc << "\n";
                mc << "\n";
                mc << "  static const int kVertCountEmbedded = " << runtimeVerts.size() << ";\n";
                mc << "  static const V3 baseEmbedded[] = {\n";
                for (size_t vi = 0; vi < runtimeVerts.size(); ++vi)
                {
                    const Vec3& v = runtimeVerts[vi];
                    mc << "    {" << fstr(v.x) << "," << fstr(v.y) << "," << fstr(v.z) << "}";
                    if (vi + 1 < runtimeVerts.size()) mc << ",";
                    mc << "\n";
                }
                mc << "  };\n";
                mc << "  static const int kTriCountEmbedded = " << (runtimeIndices.size() / 3) << ";\n";
                mc << "  static const V3 triUvEmbedded[] = {\n";
                for (size_t ui = 0; ui < runtimeTriUvs.size(); ++ui)
                {
                    const Vec3& uv = runtimeTriUvs[ui];
                    mc << "    {" << fstr(uv.x) << "," << fstr(uv.y) << ",0.0f}";
                    if (ui + 1 < runtimeTriUvs.size()) mc << ",";
                    mc << "\n";
                }
                mc << "  };\n";
                mc << "  static const uint16_t triMatEmbedded[] = {";
                for (size_t ti = 0; ti < runtimeTriMat.size(); ++ti)
                {
                    mc << runtimeTriMat[ti];
                    if (ti + 1 < runtimeTriMat.size()) mc << ",";
                }
                mc << "};\n";
                mc << "  static const uint16_t trisEmbedded[] = {";
                for (size_t ii = 0; ii < runtimeIndices.size(); ++ii)
                {
                    mc << runtimeIndices[ii];
                    if (ii + 1 < runtimeIndices.size()) mc << ",";
                }
                mc << "};\n";

                // Per-scene material property arrays — emit as [NUM_SCENES][MAX_MESHES][MAX_SLOT]
                {
                    int numScenes = (int)runtimeSceneFiles.size();
                    auto emitPerSceneSlotArrayU8 = [&](const char* name, const char* type,
                        const std::vector<std::vector<std::array<int, kStaticMeshMaterialSlots>>>& data, int defaultVal)
                    {
                        mc << "  static const " << type << " " << name << "[" << numScenes << "][MAX_MESHES][MAX_SLOT] = {\n";
                        for (int si = 0; si < numScenes; ++si)
                        {
                            mc << "  {";
                            for (int mi = 0; mi < 64; ++mi)
                            {
                                mc << "{";
                                for (int s = 0; s < kStaticMeshMaterialSlots; ++s)
                                {
                                    int v = (si < (int)data.size() && mi < (int)data[si].size()) ? data[si][mi][s] : defaultVal;
                                    mc << v;
                                    if (s + 1 < kStaticMeshMaterialSlots) mc << ",";
                                }
                                mc << "}";
                                if (mi + 1 < 64) mc << ",";
                            }
                            mc << "}";
                            if (si + 1 < numScenes) mc << ",";
                            mc << "\n";
                        }
                        mc << "  };\n";
                    };
                    auto emitPerSceneSlotArrayFloat = [&](const char* name,
                        const std::vector<std::vector<std::array<float, kStaticMeshMaterialSlots>>>& data, float defaultVal)
                    {
                        mc << "  static const float " << name << "[" << numScenes << "][MAX_MESHES][MAX_SLOT] = {\n";
                        for (int si = 0; si < numScenes; ++si)
                        {
                            mc << "  {";
                            for (int mi = 0; mi < 64; ++mi)
                            {
                                mc << "{";
                                for (int s = 0; s < kStaticMeshMaterialSlots; ++s)
                                {
                                    float v = (si < (int)data.size() && mi < (int)data[si].size()) ? data[si][mi][s] : defaultVal;
                                    mc << fstr(v);
                                    if (s + 1 < kStaticMeshMaterialSlots) mc << ",";
                                }
                                mc << "}";
                                if (mi + 1 < 64) mc << ",";
                            }
                            mc << "}";
                            if (si + 1 < numScenes) mc << ",";
                            mc << "\n";
                        }
                        mc << "  };\n";
                    };
                    emitPerSceneSlotArrayU8("kSceneShadeMode", "uint8_t", runtimeSceneShadeModeByMesh, 0);
                    emitPerSceneSlotArrayFloat("kSceneLightYaw", runtimeSceneLightYawByMesh, 0.0f);
                    emitPerSceneSlotArrayFloat("kSceneLightPitch", runtimeSceneLightPitchByMesh, 35.0f);
                    emitPerSceneSlotArrayFloat("kSceneShadowIntensity", runtimeSceneShadowIntensityByMesh, 1.0f);
                    emitPerSceneSlotArrayU8("kSceneShadingUv", "int8_t", runtimeSceneShadingUvByMesh, -1);
                    emitPerSceneSlotArrayFloat("kSceneUvScaleU", runtimeSceneUvScaleUByMesh, 1.0f);
                    emitPerSceneSlotArrayFloat("kSceneUvScaleV", runtimeSceneUvScaleVByMesh, 1.0f);
                    emitPerSceneSlotArrayFloat("kSceneOpacity", runtimeSceneOpacityByMesh, 1.0f);
                }
                mc << "  memset(meshRt, 0, sizeof(meshRt));\n";
                mc << "  static const uint16_t kFallbackWhite2x2[4] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };\n";
                mc << "  for(int mi=0; mi<gSceneMeshCount && mi<MAX_MESHES; ++mi){ RuntimeSceneMesh* rm=&meshRt[mi]; SceneMeshMeta* sm=&gSceneMeshes[mi]; if(!sm->meshDisk[0]) continue; char mp[256]; snprintf(mp,sizeof(mp),\"/cd/data/meshes/%s\",sm->meshDisk); if(!dc_try_load_nebmesh(mp,&rm->diskMesh)) continue; rm->activeMesh=rm->diskMesh; rm->kVertCount=rm->activeMesh.vert_count; rm->kTriCount=rm->activeMesh.tri_count; rm->base=rm->activeMesh.pos; rm->trisNormal=rm->activeMesh.indices; rm->triUvNormal=rm->activeMesh.tri_uv; rm->triMatNormal=rm->activeMesh.tri_mat; rm->trisFlipped=(uint16_t*)malloc((size_t)rm->kTriCount*3u*sizeof(uint16_t)); rm->triUvFlipped=(V3*)malloc((size_t)rm->kTriCount*3u*sizeof(V3)); if(rm->trisFlipped&&rm->triUvFlipped){ for(int t=0;t<rm->kTriCount;++t){ rm->trisFlipped[t*3+0]=rm->trisNormal[t*3+0]; rm->trisFlipped[t*3+1]=rm->trisNormal[t*3+2]; rm->trisFlipped[t*3+2]=rm->trisNormal[t*3+1]; rm->triUvFlipped[t*3+0]=rm->triUvNormal[t*3+0]; rm->triUvFlipped[t*3+1]=rm->triUvNormal[t*3+2]; rm->triUvFlipped[t*3+2]=rm->triUvNormal[t*3+1]; } } else { free(rm->trisFlipped); free(rm->triUvFlipped); rm->trisFlipped=0; rm->triUvFlipped=0; } rm->hasExtUv=0; if(rm->triUvNormal){ for(int ti=0;ti<rm->kTriCount*3;++ti){ float ux=rm->triUvNormal[ti].x, uy=rm->triUvNormal[ti].y; if(ux<-0.001f||ux>1.001f||uy<-0.001f||uy>1.001f){rm->hasExtUv=1; break;} } } rm->slotCount=MAX_SLOT; for(int s=0;s<MAX_SLOT;++s){ const uint16_t* buf=kFallbackWhite2x2; int tw=2,th=2; float us=1.0f,vs=1.0f,hu=0.25f,hv=0.25f; int filt=0; if(sm->texDisk[s][0]){ char tp[256]; snprintf(tp,sizeof(tp),\"/cd/data/textures/%s\",sm->texDisk[s]); if(dc_try_load_nebtex(tp,&rm->diskTex[s])){ buf=rm->diskTex[s].pixels; tw=rm->diskTex[s].w; th=rm->diskTex[s].h; us=rm->diskTex[s].us; vs=rm->diskTex[s].vs; hu=0.5f/(float)(tw>0?tw:1); hv=0.5f/(float)(th>0?th:1); filt=rm->diskTex[s].filter; rm->slotWrap[s]=(uint8_t)rm->diskTex[s].wrapMode; rm->slotFlipU[s]=(uint8_t)rm->diskTex[s].flipU; rm->slotFlipV[s]=(uint8_t)rm->diskTex[s].flipV; } } rm->slotW[s]=(uint16_t)tw; rm->slotH[s]=(uint16_t)th; rm->slotUS[s]=us; rm->slotVS[s]=vs; rm->slotHalfU[s]=hu; rm->slotHalfV[s]=hv; rm->slotFilter[s]=(uint8_t)filt; rm->slotUvScaleU[s]=(mi<MAX_MESHES)?kSceneUvScaleU[gSceneMetaIndex][mi][s]:1.0f; rm->slotUvScaleV[s]=(mi<MAX_MESHES)?kSceneUvScaleV[gSceneMetaIndex][mi][s]:1.0f; rm->slotTx[s]=pvr_mem_malloc(tw*th*2); if(!rm->slotTx[s]) continue; pvr_txr_load_ex((void*)buf,rm->slotTx[s],tw,th,PVR_TXRLOAD_16BPP); pvr_poly_cxt_t cxt; int isPow2W=(tw>0)&&((tw&(tw-1))==0); int isPow2H=(th>0)&&((th&(th-1))==0); uint32 layoutFmt=(isPow2W&&isPow2H)?PVR_TXRFMT_TWIDDLED:PVR_TXRFMT_NONTWIDDLED; uint32 strideFmt=(layoutFmt==PVR_TXRFMT_TWIDDLED)?PVR_TXRFMT_POW2_STRIDE:PVR_TXRFMT_X32_STRIDE; uint32 fmt=PVR_TXRFMT_RGB565|PVR_TXRFMT_VQ_DISABLE|strideFmt|layoutFmt; pvr_filter_mode_t f=rm->slotFilter[s]?PVR_FILTER_BILINEAR:PVR_FILTER_NONE; pvr_poly_cxt_txr(&cxt,PVR_LIST_OP_POLY,fmt,tw,th,rm->slotTx[s],f); cxt.gen.culling=PVR_CULLING_NONE; cxt.depth.comparison=PVR_DEPTHCMP_GREATER; cxt.depth.write=PVR_DEPTHWRITE_ENABLE; if(rm->slotWrap[s]==1||rm->slotWrap[s]==2){cxt.txr.uv_clamp=PVR_UVCLAMP_UV;} else {cxt.txr.uv_clamp=PVR_UVCLAMP_NONE;} pvr_poly_compile(&rm->hdrSlot[s],&cxt); rm->slotOpacity[s]=(mi<MAX_MESHES)?kSceneOpacity[gSceneMetaIndex][mi][s]:1.0f; if(rm->slotOpacity[s]<1.0f){ pvr_poly_cxt_t cxtTr; pvr_poly_cxt_txr(&cxtTr,PVR_LIST_TR_POLY,fmt,tw,th,rm->slotTx[s],f); cxtTr.gen.culling=PVR_CULLING_NONE; cxtTr.depth.comparison=PVR_DEPTHCMP_GREATER; cxtTr.depth.write=PVR_DEPTHWRITE_DISABLE; cxtTr.blend.src=PVR_BLEND_SRCALPHA; cxtTr.blend.dst=PVR_BLEND_INVSRCALPHA; if(rm->slotWrap[s]==1||rm->slotWrap[s]==2){cxtTr.txr.uv_clamp=PVR_UVCLAMP_UV;} else {cxtTr.txr.uv_clamp=PVR_UVCLAMP_NONE;} pvr_poly_compile(&rm->hdrSlotTr[s],&cxtTr); } rm->slotReady[s]=1; } rm->loaded=(rm->kVertCount>0&&rm->kTriCount>0&&rm->base&&rm->trisNormal&&rm->triUvNormal&&rm->triMatNormal)?1:0; }\n";
                mc << "  for(int mi=0; mi<gSceneMeshCount && mi<MAX_MESHES; ++mi){ RuntimeSceneMesh* rm=&meshRt[mi]; SceneMeshMeta* sm=&gSceneMeshes[mi]; if(!rm->loaded){ dbgio_printf(\"[NEBULA][DC][ANIM] mesh[%d] name=%s skipped: mesh not loaded\\n\",mi,sm->meshLogical); continue; } if(!sm->runtimeTest){ if(sm->animDisk[0]) dbgio_printf(\"[NEBULA][DC][ANIM] mesh[%d] name=%s anim=%s skipped: runtimeTest=0\\n\",mi,sm->meshLogical,sm->animDisk); continue; } if(!sm->animDisk[0]){ dbgio_printf(\"[NEBULA][DC][ANIM] mesh[%d] name=%s skipped: no anim assigned\\n\",mi,sm->meshLogical); continue; } char ap[256]; snprintf(ap,sizeof(ap),\"/cd/data/animations/%s\",sm->animDisk); { char resolved[512]; FILE* fp=dc_fopen_iso_compat(ap,resolved,(int)sizeof(resolved)); if(fp){ fclose(fp); strncpy(ap,resolved,sizeof(ap)-1); ap[sizeof(ap)-1]=0; } } dbgio_printf(\"[NEBULA][DC][ANIM] mesh[%d] name=%s try load %s\\n\",mi,sm->meshLogical,ap); if(!runtime_anim_load(ap,&rm->anim)){ dbgio_printf(\"[NEBULA][DC][ANIM] mesh[%d] name=%s anim=%s load failed (missing/unreadable/invalid)\\n\",mi,sm->meshLogical,sm->animDisk); runtime_anim_free(&rm->anim); continue; } if(rm->anim.vertCount>rm->kVertCount){ dbgio_printf(\"[NEBULA][DC][ANIM] mesh[%d] name=%s anim=%s vert mismatch clip=%d mesh=%d -> fallback static\\n\",mi,sm->meshLogical,sm->animDisk,rm->anim.vertCount,rm->kVertCount); runtime_anim_free(&rm->anim); continue; } if(rm->anim.vertCount<rm->kVertCount){ V3* np=(V3*)realloc(rm->anim.posed,(size_t)rm->kVertCount*sizeof(V3)); if(!np){ runtime_anim_free(&rm->anim); continue; } for(int vi=rm->anim.vertCount;vi<rm->kVertCount;++vi) np[vi]=rm->base[vi]; rm->anim.posed=np; dbgio_printf(\"[NEBULA][DC][ANIM] mesh[%d] padded posed %d->%d\\n\",mi,rm->anim.vertCount,rm->kVertCount); } dbgio_printf(\"[NEBULA][DC][ANIM] mesh[%d] name=%s anim=%s loaded frames=%d fps=%.2f verts=%d\\n\",mi,sm->meshLogical,sm->animDisk,rm->anim.frameCount,rm->anim.fps,rm->anim.vertCount); }\n";
                mc << "  int maxVertCount=0; for(int mi=0;mi<gSceneMeshCount&&mi<MAX_MESHES;++mi){ if(meshRt[mi].loaded&&meshRt[mi].kVertCount>maxVertCount) maxVertCount=meshRt[mi].kVertCount; }\n";
                mc << "  SV* gSv=NULL; uint8_t* gOk=NULL; V3* gCs=NULL; V3* gNrm=NULL;\n";
                mc << "  if(maxVertCount>0){ gSv=(SV*)malloc((size_t)maxVertCount*sizeof(SV)); gOk=(uint8_t*)malloc((size_t)maxVertCount); gCs=(V3*)malloc((size_t)maxVertCount*sizeof(V3)); gNrm=(V3*)malloc((size_t)maxVertCount*sizeof(V3)); if(!gSv||!gOk||!gCs||!gNrm){ free(gSv); free(gOk); free(gCs); free(gNrm); gSv=NULL; gOk=NULL; gCs=NULL; gNrm=NULL; maxVertCount=0; } }\n";
                mc << "  NB_KOS_InitInput();\n";
                mc << "  NB_TryLoadVmuBootImage();\n";
                mc << "  NB_SetMirrorFromIndex(gMirrorLrIndex);\n";
                mc << "  int sceneReady = 1;\n";
                mc << "  gSceneSwitchReq = 0;\n";
                mc << "  /* legacy orbit priming removed; unified control owns orbit state. */\n";
                mc << "  NB_Game_OnStart();\n";
                mc << "  /* Compute pivot offset: world camera position WITHOUT orbit minus mesh position. */\n";
                mc << "  /* kCamPosInit already includes orbit; subtract orbit to get the target/pivot world pos. */\n";
                mc << "  gPivotOffset[0] = (kCamPosInit[0] - kCamOrbitInit[0]) - gMeshPos[0];\n";
                mc << "  gPivotOffset[1] = (kCamPosInit[1] - kCamOrbitInit[1]) - gMeshPos[1];\n";
                mc << "  gPivotOffset[2] = (kCamPosInit[2] - kCamOrbitInit[2]) - gMeshPos[2];\n";
                mc << "  {\n";
                mc << "    gFollowAlign = 0;\n";
                mc << "    gRtOrbit[0] = kCamOrbitInit[0]; gRtOrbit[1] = kCamOrbitInit[1]; gRtOrbit[2] = kCamOrbitInit[2];\n";
                mc << "    gOrbitInited = 1;\n";
                mc << "    if (kDcDebug) dbgio_printf(\"[NEBULA][DC] cameraInit followAlign=%d orbit=(%.3f,%.3f,%.3f)\\n\", gFollowAlign, gRtOrbit[0], gRtOrbit[1], gRtOrbit[2]);\n";
                mc << "  }\n";
                mc << "\n";
                mc << "  float sRuntimeClock = 0.0f;\n";
                mc << "  uint64 sLastUs = timer_us_gettime64();\n";
                mc << "  for (;;) {\n";
                mc << "    uint64 sNowUs = timer_us_gettime64();\n";
                mc << "    float dt = (float)(sNowUs - sLastUs) * 0.000001f;\n";
                mc << "    sLastUs = sNowUs;\n";
                mc << "    if (dt > 0.1f) dt = 0.1f;\n";
                mc << "    if (dt < 0.0001f) dt = 0.016f;\n";
                mc << "    sRuntimeClock += dt;\n";
                mc << "    NB_KOS_PollInput();\n";
                mc << "    NB_TryLoadVmuBootImage();\n";
                mc << "    NB_UpdateVmuAnim(dt);\n";
                mc << "\n";
                mc << "    /* Script drives all input, movement, and camera orbit — no built-in handling */\n";
                mc << "    NB_Game_OnUpdate(dt);\n";
                mc << "\n";
                mc << "    /* Camera position from orbit (script sets gRtOrbit/gMeshPos via NB_RT_* calls) */\n";
                mc << "    if (!gOrbitInited) { gRtOrbit[0] = kCamOrbitInit[0]; gRtOrbit[1] = kCamOrbitInit[1]; gRtOrbit[2] = kCamOrbitInit[2]; gOrbitInited = 1; }\n";
                mc << "    { V3 pivot = { gMeshPos[0] + gPivotOffset[0], gMeshPos[1] + gPivotOffset[1], gMeshPos[2] + gPivotOffset[2] };\n";
                mc << "    gCamPos[0] = pivot.x + gRtOrbit[0];\n";
                mc << "    gCamPos[1] = pivot.y + gRtOrbit[1];\n";
                mc << "    gCamPos[2] = pivot.z + gRtOrbit[2];\n";
                mc << "    {\n";
                mc << "      V3 nf = norm3((V3){pivot.x - gCamPos[0], pivot.y - gCamPos[1], pivot.z - gCamPos[2]});\n";
                mc << "      V3 storedUp = {0,1,0};\n";
                mc << "      V3 nr = norm3(cross3(storedUp, nf));\n";
                mc << "      if (fabsf(dot3(nr,nr)) < 1e-6f) nr = norm3((V3){gCamRight[0], gCamRight[1], gCamRight[2]});\n";
                mc << "      if (fabsf(dot3(nr,nr)) < 1e-6f) nr = (V3){1,0,0};\n";
                mc << "      gCamForward[0]=nf.x; gCamForward[1]=nf.y; gCamForward[2]=nf.z;\n";
                mc << "      gCamRight[0]=nr.x; gCamRight[1]=nr.y; gCamRight[2]=nr.z;\n";
                mc << "      gCamUp[0]=storedUp.x; gCamUp[1]=storedUp.y; gCamUp[2]=storedUp.z;\n";
                mc << "    }}\n";
                mc << "\n";
                mc << "    if (kDcDebug) {\n";
                mc << "      static int sDbg = 0;\n";
                mc << "      if ((sDbg++ % 60) == 0) {\n";
                mc << "        dbgio_printf(\"[NEBULA][DC] player pos=(%.3f,%.3f,%.3f) rot=(%.3f,%.3f,%.3f)\\n\", gMeshPos[0], gMeshPos[1], gMeshPos[2], gMeshRot[0], gMeshRot[1], gMeshRot[2]);\n";
                mc << "        dbgio_printf(\"[NEBULA][DC] cam pos=(%.3f,%.3f,%.3f) orbit=(%.3f,%.3f,%.3f)\\n\", gCamPos[0], gCamPos[1], gCamPos[2], gRtOrbit[0], gRtOrbit[1], gRtOrbit[2]);\n";
                mc << "      }\n";
                mc << "    }\n";
                mc << "\n";
                mc << "    /* Physics tick: gravity + raycast ground snap + slope alignment */\n";
                mc << "    if (gPhysicsEnabled || gCollisionSource || gSimpleCollision) {\n";
                mc << "      if (gPhysicsEnabled) {\n";
                mc << "        gVelY += -29.4f * dt;\n";
                mc << "        gMeshPos[1] += gVelY * dt;\n";
                mc << "      }\n";
                mc << "      gOnFloor = 0;\n";
                mc << "      if (gCollisionSource || gSimpleCollision) {\n";
                mc << "        float hy = gCollExtent[1];\n";
                mc << "        float castX = gMeshPos[0] + gBoundPos[0];\n";
                mc << "        float castY = gMeshPos[1] + gBoundPos[1] - hy + 0.5f;\n";
                mc << "        float castZ = gMeshPos[2] + gBoundPos[2];\n";
                mc << "        float hitY = 0.0f;\n";
                mc << "        float hitN[3] = {0.0f, 1.0f, 0.0f};\n";
                mc << "        int groundHit = NB_RT_RaycastDownWithNormal(castX, castY, castZ, &hitY, hitN);\n";
                mc << "        if (groundHit) {\n";
                mc << "          float groundY = hitY - gBoundPos[1] + hy;\n";
                mc << "          if (gMeshPos[1] <= groundY) {\n";
                mc << "            gMeshPos[1] = groundY;\n";
                mc << "            if (gVelY < 0.0f) gVelY = 0.0f;\n";
                mc << "            gOnFloor = 1;\n";
                mc << "          }\n";
                mc << "          if (gCollisionSource) {\n";
                mc << "            dc_slope_align(hitN[0], hitN[1], hitN[2], gMeshRot[1], dt);\n";
                mc << "          }\n";
                mc << "        } else if (gCollisionSource) {\n";
                mc << "          /* No ground — smooth tilt back to upright */\n";
                mc << "          float uprightN[3] = {0.0f, 1.0f, 0.0f};\n";
                mc << "          dc_slope_align(uprightN[0], uprightN[1], uprightN[2], gMeshRot[1], dt);\n";
                mc << "        }\n";
                mc << "        /* Wall collision (horizontal push-out) */\n";
                mc << "        { float wpx=0,wpz=0;\n";
                mc << "          dc_wall_collide(gMeshPos[0]+gBoundPos[0], gMeshPos[1]+gBoundPos[1], gMeshPos[2]+gBoundPos[2], gCollExtent[0], gCollExtent[1], gCollExtent[2], &wpx, &wpz);\n";
                mc << "          gMeshPos[0]+=wpx; gMeshPos[2]+=wpz; }\n";
                mc << "      }\n";
                mc << "    }\n";
                // --- Multi-Node3D physics loop ---
                mc << "    /* Node3D physics: gravity + AABB floor collision for all physics-enabled Node3Ds */\n";
                mc << "    { int si=gSceneMetaIndex; if(si<0) si=0; if(si>=" << (int)runtimeSceneFiles.size() << ") si=0;\n";
                mc << "    int playerParentNi=(gPlayerMeshIdx>=0)?kSceneMeshParentN3D[si][gPlayerMeshIdx]:-1;\n";
                mc << "    for(int ni=0; ni<gNode3DCount; ni++){\n";
                mc << "      DcNode3D* nd=&gNode3Ds[ni];\n";
                mc << "      if(!nd->physEnabled) continue;\n";
                mc << "      if(ni==playerParentNi) continue; /* player has its own physics loop */\n";
                mc << "      float preGravY=nd->pos[1];\n";
                mc << "      nd->velY += -29.4f * dt;\n";
                mc << "      nd->pos[1] += nd->velY * dt;\n";
                mc << "      nd->onFloor=0;\n";
                mc << "      float nHy=nd->extent[1];\n";
                mc << "      if(nd->collisionSource || nd->simpleCollision){\n";
                mc << "        float castX=nd->pos[0]+nd->boundPos[0];\n";
                mc << "        float castY=nd->pos[1]+nd->boundPos[1]-nHy+0.5f;\n";
                mc << "        float castZ=nd->pos[2]+nd->boundPos[2];\n";
                mc << "        float hitY=0.0f; float hitN[3]={0.0f,1.0f,0.0f};\n";
                mc << "        int groundHit=NB_RT_RaycastDownWithNormal(castX,castY,castZ,&hitY,hitN);\n";
                mc << "        if(groundHit){\n";
                mc << "          float groundY=hitY-nd->boundPos[1]+nHy;\n";
                mc << "          if(nd->pos[1]<=groundY){\n";
                mc << "            nd->pos[1]=groundY;\n";
                mc << "            if(nd->velY<0.0f) nd->velY=0.0f;\n";
                mc << "            nd->onFloor=1;\n";
                mc << "          }\n";
                mc << "          if(nd->collisionSource){\n";
                mc << "            dc_slope_align_nd(nd, hitN[0], hitN[1], hitN[2], nd->rot[1], dt);\n";
                mc << "          }\n";
                mc << "        } else if(nd->collisionSource){\n";
                mc << "          float uprightN[3]={0.0f,1.0f,0.0f};\n";
                mc << "          dc_slope_align_nd(nd, uprightN[0], uprightN[1], uprightN[2], nd->rot[1], dt);\n";
                mc << "        }\n";
                mc << "      }\n";
                mc << "      /* Wall collision for non-player Node3D */\n";
                mc << "      { float wpx=0,wpz=0;\n";
                mc << "        dc_wall_collide(nd->pos[0]+nd->boundPos[0], nd->pos[1]+nd->boundPos[1], nd->pos[2]+nd->boundPos[2], nd->extent[0], nd->extent[1], nd->extent[2], &wpx, &wpz);\n";
                mc << "        nd->pos[0]+=wpx; nd->pos[2]+=wpz; }\n";
                mc << "      /* Update child mesh position and rotation to follow this Node3D */\n";
                mc << "      for(int mi=0; mi<gSceneMeshCount && mi<MAX_MESHES; ++mi){\n";
                mc << "        if(kSceneMeshParentN3D[si][mi]==ni){\n";
                mc << "          gSceneMeshes[mi].pos[0]=nd->pos[0];\n";
                mc << "          gSceneMeshes[mi].pos[1]=nd->pos[1];\n";
                mc << "          gSceneMeshes[mi].pos[2]=nd->pos[2];\n";
                mc << "          gSceneMeshes[mi].rot[0]=nd->rot[0];\n";
                mc << "          gSceneMeshes[mi].rot[1]=nd->rot[1];\n";
                mc << "          gSceneMeshes[mi].rot[2]=nd->rot[2];\n";
                mc << "        }\n";
                mc << "      }\n";
                mc << "    }}\n";
                mc << "    /* Node3D-vs-Node3D AABB collision (push apart on overlap) */\n";
                mc << "    { int ppNi=(gPlayerMeshIdx>=0)?kSceneMeshParentN3D[gSceneMetaIndex<0?0:gSceneMetaIndex][gPlayerMeshIdx]:-1;\n";
                mc << "    for(int ni=0; ni<gNode3DCount; ni++){\n";
                mc << "      float acx,acy,acz,ahx,ahy,ahz;\n";
                mc << "      if(ni==ppNi){ acx=gMeshPos[0]+gBoundPos[0]; acy=gMeshPos[1]+gBoundPos[1]; acz=gMeshPos[2]+gBoundPos[2]; ahx=gCollExtent[0]; ahy=gCollExtent[1]; ahz=gCollExtent[2]; }\n";
                mc << "      else { DcNode3D* na=&gNode3Ds[ni]; if(!na->physEnabled) continue; acx=na->pos[0]+na->boundPos[0]; acy=na->pos[1]+na->boundPos[1]; acz=na->pos[2]+na->boundPos[2]; ahx=na->extent[0]; ahy=na->extent[1]; ahz=na->extent[2]; }\n";
                mc << "      for(int nj=ni+1; nj<gNode3DCount; nj++){\n";
                mc << "        float bcx,bcy,bcz,bhx,bhy,bhz;\n";
                mc << "        if(nj==ppNi){ bcx=gMeshPos[0]+gBoundPos[0]; bcy=gMeshPos[1]+gBoundPos[1]; bcz=gMeshPos[2]+gBoundPos[2]; bhx=gCollExtent[0]; bhy=gCollExtent[1]; bhz=gCollExtent[2]; }\n";
                mc << "        else { DcNode3D* nb=&gNode3Ds[nj]; if(!nb->physEnabled) continue; bcx=nb->pos[0]+nb->boundPos[0]; bcy=nb->pos[1]+nb->boundPos[1]; bcz=nb->pos[2]+nb->boundPos[2]; bhx=nb->extent[0]; bhy=nb->extent[1]; bhz=nb->extent[2]; }\n";
                mc << "        float ox=(ahx+bhx)-fabsf(acx-bcx); if(ox<=0) continue;\n";
                mc << "        float oy=(ahy+bhy)-fabsf(acy-bcy); if(oy<=0) continue;\n";
                mc << "        float oz=(ahz+bhz)-fabsf(acz-bcz); if(oz<=0) continue;\n";
                mc << "        float mp=ox; int ax=0; if(oz<mp){mp=oz;ax=1;} if(oy<mp) continue;\n";
                mc << "        float half=mp*0.5f;\n";
                mc << "        if(ax==0){ float dir=(acx>=bcx)?1.0f:-1.0f;\n";
                mc << "          if(ni==ppNi){gMeshPos[0]+=dir*half;}else{gNode3Ds[ni].pos[0]+=dir*half;}\n";
                mc << "          if(nj==ppNi){gMeshPos[0]-=dir*half;}else{gNode3Ds[nj].pos[0]-=dir*half;}\n";
                mc << "        } else { float dir=(acz>=bcz)?1.0f:-1.0f;\n";
                mc << "          if(ni==ppNi){gMeshPos[2]+=dir*half;}else{gNode3Ds[ni].pos[2]+=dir*half;}\n";
                mc << "          if(nj==ppNi){gMeshPos[2]-=dir*half;}else{gNode3Ds[nj].pos[2]-=dir*half;}\n";
                mc << "        }\n";
                mc << "    }}}\n";
                mc << "    if (gSceneSwitchReq != 0) {\n";
                mc << "      int metaOk = 0;\n";
                mc << "      if (gSceneSwitchReq == 2 && gSceneSwitchName[0]) { for(int si=0;si<gSceneCount;++si){ if(dc_eq_nocase(gSceneSwitchName,gSceneNames[si])){ metaOk=NB_LoadSceneIndex(si); break; } } gSceneSwitchName[0]=0; }\n";
                mc << "      else { metaOk = (gSceneSwitchReq > 0) ? NB_NextScene() : NB_PrevScene(); }\n";
                mc << "      gSceneSwitchReq = 0;\n";
                mc << "      sceneReady = 0;\n";
                mc << "      gCollCacheReady = 0;\n";
                mc << "      for(int mi=0; mi<MAX_MESHES; ++mi){ RuntimeSceneMesh* rm=&meshRt[mi]; for(int s=0;s<MAX_SLOT;++s){ if(rm->slotTx[s]){ pvr_mem_free(rm->slotTx[s]); rm->slotTx[s]=0; } dc_free_tex(&rm->diskTex[s]); } if(rm->trisFlipped){ free(rm->trisFlipped); rm->trisFlipped=0; } if(rm->triUvFlipped){ free(rm->triUvFlipped); rm->triUvFlipped=0; } runtime_anim_free(&rm->anim); dc_free_mesh(&rm->diskMesh); free(rm->weldGroups); free(rm->weldNrmBuf); memset(rm,0,sizeof(*rm)); }\n";
                mc << "      if (!metaOk) { dbgio_printf(\"[NEBULA][DC] Scene metadata switch failed\\n\"); }\n";
                mc << "      else { for(int mi=0; mi<gSceneMeshCount && mi<MAX_MESHES; ++mi){ RuntimeSceneMesh* rm=&meshRt[mi]; SceneMeshMeta* sm=&gSceneMeshes[mi]; if(!sm->meshDisk[0]) continue; char mp[256]; snprintf(mp,sizeof(mp),\"/cd/data/meshes/%s\",sm->meshDisk); if(!dc_try_load_nebmesh(mp,&rm->diskMesh)) continue; rm->activeMesh=rm->diskMesh; rm->kVertCount=rm->activeMesh.vert_count; rm->kTriCount=rm->activeMesh.tri_count; rm->base=rm->activeMesh.pos; rm->trisNormal=rm->activeMesh.indices; rm->triUvNormal=rm->activeMesh.tri_uv; rm->triMatNormal=rm->activeMesh.tri_mat; rm->trisFlipped=(uint16_t*)malloc((size_t)rm->kTriCount*3u*sizeof(uint16_t)); rm->triUvFlipped=(V3*)malloc((size_t)rm->kTriCount*3u*sizeof(V3)); if(rm->trisFlipped&&rm->triUvFlipped){ for(int t=0;t<rm->kTriCount;++t){ rm->trisFlipped[t*3+0]=rm->trisNormal[t*3+0]; rm->trisFlipped[t*3+1]=rm->trisNormal[t*3+2]; rm->trisFlipped[t*3+2]=rm->trisNormal[t*3+1]; rm->triUvFlipped[t*3+0]=rm->triUvNormal[t*3+0]; rm->triUvFlipped[t*3+1]=rm->triUvNormal[t*3+2]; rm->triUvFlipped[t*3+2]=rm->triUvNormal[t*3+1]; } } else { free(rm->trisFlipped); free(rm->triUvFlipped); rm->trisFlipped=0; rm->triUvFlipped=0; } rm->hasExtUv=0; if(rm->triUvNormal){ for(int ti=0;ti<rm->kTriCount*3;++ti){ float ux=rm->triUvNormal[ti].x, uy=rm->triUvNormal[ti].y; if(ux<-0.001f||ux>1.001f||uy<-0.001f||uy>1.001f){rm->hasExtUv=1; break;} } } rm->slotCount=MAX_SLOT; for(int s=0;s<MAX_SLOT;++s){ const uint16_t* buf=kFallbackWhite2x2; int tw=2,th=2; float us=1.0f,vs=1.0f,hu=0.25f,hv=0.25f; int filt=0; if(sm->texDisk[s][0]){ char tp[256]; snprintf(tp,sizeof(tp),\"/cd/data/textures/%s\",sm->texDisk[s]); if(dc_try_load_nebtex(tp,&rm->diskTex[s])){ buf=rm->diskTex[s].pixels; tw=rm->diskTex[s].w; th=rm->diskTex[s].h; us=rm->diskTex[s].us; vs=rm->diskTex[s].vs; hu=0.5f/(float)(tw>0?tw:1); hv=0.5f/(float)(th>0?th:1); filt=rm->diskTex[s].filter; rm->slotWrap[s]=(uint8_t)rm->diskTex[s].wrapMode; rm->slotFlipU[s]=(uint8_t)rm->diskTex[s].flipU; rm->slotFlipV[s]=(uint8_t)rm->diskTex[s].flipV; } } rm->slotW[s]=(uint16_t)tw; rm->slotH[s]=(uint16_t)th; rm->slotUS[s]=us; rm->slotVS[s]=vs; rm->slotHalfU[s]=hu; rm->slotHalfV[s]=hv; rm->slotFilter[s]=(uint8_t)filt; rm->slotUvScaleU[s]=(mi<MAX_MESHES)?kSceneUvScaleU[gSceneMetaIndex][mi][s]:1.0f; rm->slotUvScaleV[s]=(mi<MAX_MESHES)?kSceneUvScaleV[gSceneMetaIndex][mi][s]:1.0f; rm->slotTx[s]=pvr_mem_malloc(tw*th*2); if(!rm->slotTx[s]) continue; pvr_txr_load_ex((void*)buf,rm->slotTx[s],tw,th,PVR_TXRLOAD_16BPP); pvr_poly_cxt_t cxt; int isPow2W=(tw>0)&&((tw&(tw-1))==0); int isPow2H=(th>0)&&((th&(th-1))==0); uint32 layoutFmt=(isPow2W&&isPow2H)?PVR_TXRFMT_TWIDDLED:PVR_TXRFMT_NONTWIDDLED; uint32 strideFmt=(layoutFmt==PVR_TXRFMT_TWIDDLED)?PVR_TXRFMT_POW2_STRIDE:PVR_TXRFMT_X32_STRIDE; uint32 fmt=PVR_TXRFMT_RGB565|PVR_TXRFMT_VQ_DISABLE|strideFmt|layoutFmt; pvr_filter_mode_t f=rm->slotFilter[s]?PVR_FILTER_BILINEAR:PVR_FILTER_NONE; pvr_poly_cxt_txr(&cxt,PVR_LIST_OP_POLY,fmt,tw,th,rm->slotTx[s],f); cxt.gen.culling=PVR_CULLING_NONE; cxt.depth.comparison=PVR_DEPTHCMP_GREATER; cxt.depth.write=PVR_DEPTHWRITE_ENABLE; if(rm->slotWrap[s]==1||rm->slotWrap[s]==2){cxt.txr.uv_clamp=PVR_UVCLAMP_UV;} else {cxt.txr.uv_clamp=PVR_UVCLAMP_NONE;} pvr_poly_compile(&rm->hdrSlot[s],&cxt); rm->slotReady[s]=1; } rm->loaded=(rm->kVertCount>0&&rm->kTriCount>0&&rm->base&&rm->trisNormal&&rm->triUvNormal&&rm->triMatNormal)?1:0; if(rm->loaded) sceneReady=1; } for(int mi=0; mi<gSceneMeshCount && mi<MAX_MESHES; ++mi){ RuntimeSceneMesh* rm=&meshRt[mi]; SceneMeshMeta* sm=&gSceneMeshes[mi]; if(!rm->loaded){ dbgio_printf(\"[NEBULA][DC][ANIM] mesh[%d] name=%s skipped: mesh not loaded\\n\",mi,sm->meshLogical); continue; } if(!sm->runtimeTest){ if(sm->animDisk[0]) dbgio_printf(\"[NEBULA][DC][ANIM] mesh[%d] name=%s anim=%s skipped: runtimeTest=0\\n\",mi,sm->meshLogical,sm->animDisk); continue; } if(!sm->animDisk[0]){ dbgio_printf(\"[NEBULA][DC][ANIM] mesh[%d] name=%s skipped: no anim assigned\\n\",mi,sm->meshLogical); continue; } char ap[256]; snprintf(ap,sizeof(ap),\"/cd/data/animations/%s\",sm->animDisk); { char resolved[512]; FILE* fp=dc_fopen_iso_compat(ap,resolved,(int)sizeof(resolved)); if(fp){ fclose(fp); strncpy(ap,resolved,sizeof(ap)-1); ap[sizeof(ap)-1]=0; } } dbgio_printf(\"[NEBULA][DC][ANIM] mesh[%d] name=%s try load %s\\n\",mi,sm->meshLogical,ap); if(!runtime_anim_load(ap,&rm->anim)){ dbgio_printf(\"[NEBULA][DC][ANIM] mesh[%d] name=%s anim=%s load failed (missing/unreadable/invalid)\\n\",mi,sm->meshLogical,sm->animDisk); runtime_anim_free(&rm->anim); continue; } if(rm->anim.vertCount>rm->kVertCount){ dbgio_printf(\"[NEBULA][DC][ANIM] mesh[%d] name=%s anim=%s vert mismatch clip=%d mesh=%d -> fallback static\\n\",mi,sm->meshLogical,sm->animDisk,rm->anim.vertCount,rm->kVertCount); runtime_anim_free(&rm->anim); continue; } if(rm->anim.vertCount<rm->kVertCount){ V3* np=(V3*)realloc(rm->anim.posed,(size_t)rm->kVertCount*sizeof(V3)); if(!np){ runtime_anim_free(&rm->anim); continue; } for(int vi=rm->anim.vertCount;vi<rm->kVertCount;++vi) np[vi]=rm->base[vi]; rm->anim.posed=np; dbgio_printf(\"[NEBULA][DC][ANIM] mesh[%d] padded posed %d->%d\\n\",mi,rm->anim.vertCount,rm->kVertCount); } dbgio_printf(\"[NEBULA][DC][ANIM] mesh[%d] name=%s anim=%s loaded frames=%d fps=%.2f verts=%d\\n\",mi,sm->meshLogical,sm->animDisk,rm->anim.frameCount,rm->anim.fps,rm->anim.vertCount); } { int newMax=0; for(int mi=0;mi<gSceneMeshCount&&mi<MAX_MESHES;++mi){ if(meshRt[mi].loaded&&meshRt[mi].kVertCount>newMax) newMax=meshRt[mi].kVertCount; } if(newMax>maxVertCount){ free(gSv); free(gOk); free(gCs); free(gNrm); maxVertCount=newMax; gSv=(SV*)malloc((size_t)maxVertCount*sizeof(SV)); gOk=(uint8_t*)malloc((size_t)maxVertCount); gCs=(V3*)malloc((size_t)maxVertCount*sizeof(V3)); gNrm=(V3*)malloc((size_t)maxVertCount*sizeof(V3)); if(!gSv||!gOk||!gCs||!gNrm){ free(gSv); free(gOk); free(gCs); free(gNrm); gSv=NULL; gOk=NULL; gCs=NULL; gNrm=NULL; maxVertCount=0; } } } if(sceneReady){ gPlayerMeshIdx=-1; for(int mi=0;mi<gSceneMeshCount&&mi<MAX_MESHES;++mi){ if(strcmp(gSceneMeshes[mi].meshDisk,kPlayerMeshDisk)==0){ gPlayerMeshIdx=mi; break; } } NB_RT_NavMeshClear(); NB_RT_NavMeshBuild(); NB_Game_OnSceneSwitch(gSceneName); } }\n";
                mc << "    }\n";
                mc << "    if (!sceneReady) {\n";
                mc << "      pvr_wait_ready(); pvr_scene_begin(); pvr_list_begin(PVR_LIST_OP_POLY); pvr_list_finish(); pvr_scene_finish(); thd_sleep(16); continue;\n";
                mc << "    }\n";
                mc << "    cam_update_basis();\n";
                mc << "    pvr_wait_ready();\n";
                mc << "    pvr_scene_begin();\n";
                mc << "    for(int _trPass=0;_trPass<2;++_trPass){\n";
                mc << "    if(_trPass==0) pvr_list_begin(PVR_LIST_OP_POLY); else pvr_list_begin(PVR_LIST_TR_POLY);\n";
                mc << "    for (int mi=0; mi<gSceneMeshCount && mi<MAX_MESHES; ++mi) {\n";
                mc << "      RuntimeSceneMesh* rm = &meshRt[mi]; SceneMeshMeta* sm = &gSceneMeshes[mi];\n";
                mc << "      if (!rm->loaded || rm->kVertCount <= 0 || rm->kTriCount <= 0 || !rm->base || !rm->trisNormal || !rm->triUvNormal || !rm->triMatNormal) continue;\n";
                mc << "      if (!gSv || !gOk || !gCs || !gNrm || rm->kVertCount > maxVertCount) continue;\n";
                mc << "      SV* sv = gSv; uint8_t* ok = gOk; V3* cs = gCs; V3* nrm = gNrm;\n";
                mc << "      float smPos[3] = {sm->pos[0], sm->pos[1], sm->pos[2]};\n";
                mc << "      float smRot[3] = {sm->rot[0], sm->rot[1], sm->rot[2]};\n";
                mc << "      /* Child local rotation (visual offset only, zero for non-player meshes). */\n";
                mc << "      float cRx = 0, cRy = 0, cRz = 0;\n";
                mc << "      /* StaticMesh rotation axis remap: X<-Z, Y<-X, Z<-Y (matches editor OpenGL convention). */\n";
                mc << "      /* Meshes parented under a Node3D use identity remap — parent drives rotation. */\n";
                mc << "      int _rsi=gSceneMetaIndex; if(_rsi<0) _rsi=0; if(_rsi>=" << (int)runtimeSceneFiles.size() << ") _rsi=0;\n";
                mc << "      int hasN3DParent = (mi<MAX_MESHES) ? (kSceneMeshParentN3D[_rsi][mi]>=0) : 0;\n";
                mc << "      int useQuat = 0;\n";
                mc << "      float qRm[9] = {1,0,0, 0,1,0, 0,0,1}; /* quaternion rotation matrix (row-major) */\n";
                mc << "      if (mi == gPlayerMeshIdx) { cRx = deg2rad(kPlayerChildRot[0]); cRy = deg2rad(kPlayerChildRot[1]); cRz = deg2rad(kPlayerChildRot[2]); smPos[0]=gMeshPos[0]; smPos[1]=gMeshPos[1]; smPos[2]=gMeshPos[2]; smRot[0]=gMeshRot[0]; smRot[1]=gMeshRot[1]; smRot[2]=gMeshRot[2];\n";
                mc << "        if (gCollisionSource) {\n";
                mc << "          useQuat = 1;\n";
                mc << "          float w=gQw,x=gQx,y=gQy,z=gQz;\n";
                mc << "          qRm[0]=1-2*(y*y+z*z); qRm[1]=2*(x*y-w*z);   qRm[2]=2*(x*z+w*y);\n";
                mc << "          qRm[3]=2*(x*y+w*z);   qRm[4]=1-2*(x*x+z*z); qRm[5]=2*(y*z-w*x);\n";
                mc << "          qRm[6]=2*(x*z-w*y);   qRm[7]=2*(y*z+w*x);   qRm[8]=1-2*(x*x+y*y);\n";
                mc << "        }\n";
                mc << "      } else if (hasN3DParent) {\n";
                mc << "        /* Non-player mesh parented under Node3D: use Node3D quaternion if collisionSource */\n";
                mc << "        int _pni = kSceneMeshParentN3D[_rsi][mi];\n";
                mc << "        if (_pni >= 0 && _pni < gNode3DCount && gNode3Ds[_pni].collisionSource) {\n";
                mc << "          useQuat = 1;\n";
                mc << "          float w=gNode3Ds[_pni].qw, x=gNode3Ds[_pni].qx, y=gNode3Ds[_pni].qy, z=gNode3Ds[_pni].qz;\n";
                mc << "          qRm[0]=1-2*(y*y+z*z); qRm[1]=2*(x*y-w*z);   qRm[2]=2*(x*z+w*y);\n";
                mc << "          qRm[3]=2*(x*y+w*z);   qRm[4]=1-2*(x*x+z*z); qRm[5]=2*(y*z-w*x);\n";
                mc << "          qRm[6]=2*(x*z-w*y);   qRm[7]=2*(y*z+w*x);   qRm[8]=1-2*(x*x+y*y);\n";
                mc << "        }\n";
                mc << "      }\n";
                mc << "      float rxr = (mi == gPlayerMeshIdx || hasN3DParent) ? deg2rad(smRot[0]) : deg2rad(smRot[2]);\n";
                mc << "      float ryr = (mi == gPlayerMeshIdx || hasN3DParent) ? deg2rad(smRot[1]) : deg2rad(smRot[0]);\n";
                mc << "      float rzr = (mi == gPlayerMeshIdx || hasN3DParent) ? deg2rad(smRot[2]) : deg2rad(smRot[1]);\n";
                mc << "      /* Tick slot-based animation time */\n";
                mc << "      if(sm->animPlaying && rm->anim.loaded && rm->anim.frames && rm->anim.frameCount>0){\n";
                mc << "        sm->animTime += dt * sm->animSpeed;\n";
                mc << "        int af=(int)floorf(sm->animTime * rm->anim.fps);\n";
                mc << "        if(af>=(int)rm->anim.frameCount){ sm->animFinished=1; if(sm->animLoop){ float dur=(float)rm->anim.frameCount/rm->anim.fps; if(dur>0.0f){ while(sm->animTime>=dur) sm->animTime-=dur; } else { sm->animTime=0.0f; } } else { sm->animPlaying=0; sm->animTime=(float)(rm->anim.frameCount-1)/rm->anim.fps; } }\n";
                mc << "      }\n";
                mc << "      const V3* srcBase = rm->base; if((sm->animPlaying||sm->animFinished||(sm->runtimeTest)) && rm->anim.loaded && rm->anim.frames && rm->anim.posed && rm->anim.frameCount>0){ int af; if(sm->animPlaying||sm->animFinished){ af=(int)floorf(sm->animTime*rm->anim.fps); if(rm->anim.frameCount>0) af=af%rm->anim.frameCount; if(af<0) af=0; } else { af=(int)floorf(sRuntimeClock * rm->anim.fps); if(rm->anim.frameCount>0) af%=rm->anim.frameCount; if(af<0) af=0; } const V3* fr=&rm->anim.frames[(size_t)af*(size_t)rm->anim.vertCount]; int nv=rm->anim.vertCount; memcpy(rm->anim.posed,fr,(size_t)nv*sizeof(V3)); for(int vi=nv;vi<rm->kVertCount;++vi) rm->anim.posed[vi]=rm->base[vi]; srcBase=rm->anim.posed; }\n";
                mc << "      float _sx=sinf(rxr),_cx=cosf(rxr),_sy=sinf(ryr),_cy=cosf(ryr),_sz=sinf(rzr),_cz=cosf(rzr);\n";
                mc << "      /* Child visual rotation sin/cos (identity when cRx/cRy/cRz are 0). */\n";
                mc << "      float _csx=sinf(cRx),_ccx=cosf(cRx),_csy=sinf(cRy),_ccy=cosf(cRy),_csz=sinf(cRz),_ccz=cosf(cRz);\n";
                mc << "      float _scx=sm->scale[0]*(float)gMirrorX, _scy=sm->scale[1]*(float)gMirrorY, _scz=sm->scale[2]*(float)gMirrorZ;\n";
                mc << "      for (int i=0;i<rm->kVertCount;++i){ V3 v=srcBase[i]; v.x*=_scx; v.y*=_scy; v.z*=_scz; float t; ";
                mc << "t=v.x*_ccz-v.y*_csz; v.y=v.x*_csz+v.y*_ccz; v.x=t; t=v.x*_ccy+v.z*_csy; v.z=-v.x*_csy+v.z*_ccy; v.x=t; t=v.y*_ccx-v.z*_csx; v.z=v.y*_csx+v.z*_ccx; v.y=t; ";
                mc << "if(useQuat){ float qx=qRm[0]*v.x+qRm[1]*v.y+qRm[2]*v.z, qy=qRm[3]*v.x+qRm[4]*v.y+qRm[5]*v.z, qz=qRm[6]*v.x+qRm[7]*v.y+qRm[8]*v.z; v.x=qx;v.y=qy;v.z=qz; } else { t=v.x*_cz-v.y*_sz; v.y=v.x*_sz+v.y*_cz; v.x=t; t=v.x*_cy+v.z*_sy; v.z=-v.x*_sy+v.z*_cy; v.x=t; t=v.y*_cx-v.z*_sx; v.z=v.y*_sx+v.z*_cx; v.y=t; } ";
                mc << "v.x+=smPos[0]; v.y+=smPos[1]; v.z+=smPos[2]; cs[i]=w2c(v); ok[i]=proj_cs(cs[i],&sv[i])?1:0; nrm[i]=(V3){0,0,0}; }\n";
                mc << "      float mirrorDet = (sm->scale[0] * (float)gMirrorX) * (sm->scale[1] * (float)gMirrorY) * (sm->scale[2] * (float)gMirrorZ);\n";
                mc << "      const int mirroredWinding = (mirrorDet < 0.0f) ? 1 : 0;\n";
                mc << "      const uint16_t *tris = (mirroredWinding && rm->trisFlipped) ? rm->trisFlipped : rm->trisNormal;\n";
                mc << "      const V3 *triUv = (mirroredWinding && rm->triUvFlipped) ? rm->triUvFlipped : rm->triUvNormal;\n";
                mc << "      int meshShadeMode = 0; float meshShadeYaw = 0.0f, meshShadePitch = 35.0f, meshShadeShadow = 1.0f;\n";
                mc << "      if (mi < MAX_MESHES) { for (int ss=0; ss<MAX_SLOT; ++ss) { if (kSceneShadeMode[gSceneMetaIndex][mi][ss] || kSceneShadingUv[gSceneMetaIndex][mi][ss] >= 0) { meshShadeMode = 1; meshShadeYaw = kSceneLightYaw[gSceneMetaIndex][mi][ss]; meshShadePitch = kSceneLightPitch[gSceneMetaIndex][mi][ss]; meshShadeShadow = kSceneShadowIntensity[gSceneMetaIndex][mi][ss]; break; } } }\n";
                mc << "      /* Lazy-init weld groups: compute once, cache in RuntimeSceneMesh */\n";
                mc << "      if (meshShadeMode && !rm->weldGroups) {\n";
                mc << "        rm->weldGroups = (int*)malloc(rm->kVertCount * sizeof(int));\n";
                mc << "        rm->nWeldGroups = 0;\n";
                mc << "        if (rm->weldGroups) {\n";
                mc << "          /* Adaptive weld distance: 0.5% of bounding box diagonal (matches editor) */\n";
                mc << "          V3 bMin=rm->base[0], bMax=rm->base[0];\n";
                mc << "          for(int i=1;i<rm->kVertCount;++i){ V3 p=rm->base[i]; if(p.x<bMin.x)bMin.x=p.x; if(p.x>bMax.x)bMax.x=p.x; if(p.y<bMin.y)bMin.y=p.y; if(p.y>bMax.y)bMax.y=p.y; if(p.z<bMin.z)bMin.z=p.z; if(p.z>bMax.z)bMax.z=p.z; }\n";
                mc << "          float diag=sqrtf((bMax.x-bMin.x)*(bMax.x-bMin.x)+(bMax.y-bMin.y)*(bMax.y-bMin.y)+(bMax.z-bMin.z)*(bMax.z-bMin.z));\n";
                mc << "          float wd=diag*0.005f; if(wd<0.01f) wd=0.01f;\n";
                mc << "          const float we2 = wd*wd;\n";
                mc << "          for (int i=0;i<rm->kVertCount;++i) rm->weldGroups[i]=-1;\n";
                mc << "          for (int i=0;i<rm->kVertCount;++i) {\n";
                mc << "            if (rm->weldGroups[i]>=0) continue;\n";
                mc << "            rm->weldGroups[i]=rm->nWeldGroups;\n";
                mc << "            V3 pi=rm->base[i];\n";
                mc << "            for (int j=i+1;j<rm->kVertCount;++j) {\n";
                mc << "              if (rm->weldGroups[j]>=0) continue;\n";
                mc << "              V3 pj=rm->base[j];\n";
                mc << "              float dx=pi.x-pj.x, dy=pi.y-pj.y, dz=pi.z-pj.z;\n";
                mc << "              if (dx*dx+dy*dy+dz*dz<=we2) rm->weldGroups[j]=rm->nWeldGroups;\n";
                mc << "            }\n";
                mc << "            rm->nWeldGroups++;\n";
                mc << "          }\n";
                mc << "        }\n";
                mc << "        rm->weldNrmBuf = (V3*)malloc(rm->nWeldGroups * sizeof(V3));\n";
                mc << "      }\n";
                mc << "      /* Accumulate smooth normals from camera-space positions (inherently camera-space) */\n";
                mc << "      if (meshShadeMode && rm->weldGroups && rm->weldNrmBuf && rm->nWeldGroups > 0) {\n";
                mc << "        V3* gn = rm->weldNrmBuf;\n";
                mc << "        memset(gn, 0, rm->nWeldGroups * sizeof(V3));\n";
                mc << "        for (int t=0;t<rm->kTriCount;++t){ int ia=tris[t*3+0], ib=tris[t*3+1], ic=tris[t*3+2]; if(ia<0||ib<0||ic<0||ia>=rm->kVertCount||ib>=rm->kVertCount||ic>=rm->kVertCount) continue; V3 a=cs[ia], b=cs[ib], c=cs[ic]; float e1x=b.x-a.x,e1y=b.y-a.y,e1z=b.z-a.z,e2x=c.x-a.x,e2y=c.y-a.y,e2z=c.z-a.z; float nx=-(e1y*e2z-e1z*e2y), ny=-(e1z*e2x-e1x*e2z), nz=-(e1x*e2y-e1y*e2x); int ga=rm->weldGroups[ia],gb=rm->weldGroups[ib],gc=rm->weldGroups[ic]; gn[ga].x+=nx;gn[ga].y+=ny;gn[ga].z+=nz; gn[gb].x+=nx;gn[gb].y+=ny;gn[gb].z+=nz; gn[gc].x+=nx;gn[gc].y+=ny;gn[gc].z+=nz; }\n";
                mc << "        for (int g=0;g<rm->nWeldGroups;++g) gn[g]=norm3(gn[g]);\n";
                mc << "        for (int i=0;i<rm->kVertCount;++i) nrm[i]=gn[rm->weldGroups[i]];\n";
                mc << "      }\n";
                mc << "      /* Precompute light direction and shading constants once per mesh */\n";
                mc << "      V3 sLight={0,0,1}; float sAmb=0.35f, sDifScale=0.9f, sShQ=0.0f;\n";
                mc << "      if (meshShadeMode) {\n";
                mc << "        float yaw=deg2rad(meshShadeYaw), pit=deg2rad(meshShadePitch);\n";
                mc << "        sLight=norm3((V3){-sinf(yaw)*cosf(pit),-sinf(pit),-cosf(yaw)*cosf(pit)});\n";
                mc << "        float sh=meshShadeShadow; if(sh<0.0f)sh=0.0f; if(sh>2.0f)sh=2.0f;\n";
                mc << "        sDifScale=0.9f-0.25f*sh; sShQ=0.25f*sh;\n";
                mc << "      }\n";
                mc << "      for (int t=0;t<rm->kTriCount;++t){\n";
                mc << "        int ia=tris[t*3+0], ib=tris[t*3+1], ic=tris[t*3+2];\n";
                mc << "        int sid = (int)rm->triMatNormal[t]; if (sid < 0 || sid >= rm->slotCount) sid = 0; if (!rm->slotReady[sid]) continue;\n";
                mc << "        { int _isTr=(rm->slotOpacity[sid]<1.0f)?1:0; if(_isTr!=_trPass) continue; }\n";
                mc << "        float us = rm->slotUS[sid], vs_ = rm->slotVS[sid]; float hu = rm->slotHalfU[sid], hv = rm->slotHalfV[sid];\n";
                mc << "        uint32 col = 0xFFFFFFFF, colA = 0xFFFFFFFF, colB = 0xFFFFFFFF, colC = 0xFFFFFFFF;\n";
                mc << "        float fLitA=1.0f, fLitB=1.0f, fLitC=1.0f;\n";
                mc << "        int slotShaded = (mi < MAX_MESHES && sid < MAX_SLOT) ? (kSceneShadeMode[gSceneMetaIndex][mi][sid] || kSceneShadingUv[gSceneMetaIndex][mi][sid] >= 0) : 0;\n";
                mc << "        if (meshShadeMode && slotShaded) {\n";
                mc << "          V3 na=nrm[ia], nb=nrm[ib], nc=nrm[ic];\n";
                mc << "          float nA=dot3(na,sLight); if(nA<0.0f)nA=0.0f; fLitA=sAmb + nA*sDifScale - ((na.y<0.0f)?(-na.y*sShQ):0.0f) + (1.0f-fabsf(na.z))*0.12f;\n";
                mc << "          float nB=dot3(nb,sLight); if(nB<0.0f)nB=0.0f; fLitB=sAmb + nB*sDifScale - ((nb.y<0.0f)?(-nb.y*sShQ):0.0f) + (1.0f-fabsf(nb.z))*0.12f;\n";
                mc << "          float nC=dot3(nc,sLight); if(nC<0.0f)nC=0.0f; fLitC=sAmb + nC*sDifScale - ((nc.y<0.0f)?(-nc.y*sShQ):0.0f) + (1.0f-fabsf(nc.z))*0.12f;\n";
                mc << "          if(fLitA<0.10f)fLitA=0.10f; if(fLitA>1.20f)fLitA=1.20f; if(fLitB<0.10f)fLitB=0.10f; if(fLitB>1.20f)fLitB=1.20f; if(fLitC<0.10f)fLitC=0.10f; if(fLitC>1.20f)fLitC=1.20f;\n";
                mc << "          int iA=(int)(fLitA*255.0f), iB=(int)(fLitB*255.0f), iC=(int)(fLitC*255.0f); if(iA<0)iA=0; if(iA>255)iA=255; if(iB<0)iB=0; if(iB>255)iB=255; if(iC<0)iC=0; if(iC>255)iC=255;\n";
                mc << "          colA = 0xFF000000u | ((uint32)iA<<16) | ((uint32)iA<<8) | (uint32)iA;\n";
                mc << "          colB = 0xFF000000u | ((uint32)iB<<16) | ((uint32)iB<<8) | (uint32)iB;\n";
                mc << "          colC = 0xFF000000u | ((uint32)iC<<16) | ((uint32)iC<<8) | (uint32)iC;\n";
                mc << "        }\n";
                mc << "        if(_trPass==1){ uint32 _a=(uint32)(rm->slotOpacity[sid]*255.0f); if(_a>255u)_a=255u; colA=(colA&0x00FFFFFFu)|(_a<<24); colB=(colB&0x00FFFFFFu)|(_a<<24); colC=(colC&0x00FFFFFFu)|(_a<<24); }\n";
                mc << "        /* Compute UV for each original vertex */\n";
                mc << "        float uv[3][2];\n";
                mc << "        for(int k=0;k<3;++k){ float rawU=triUv[t*3+k].x, rawV=triUv[t*3+k].y; if(rm->slotFlipU[sid]) rawU=1.0f-rawU; if(rm->slotFlipV[sid]) rawV=1.0f-rawV; float u=rawU*rm->slotUvScaleU[sid], v=(1.0f-rawV)*rm->slotUvScaleV[sid]; int hasUvSc=(rm->hasExtUv||rm->slotWrap[sid]==0||rm->slotWrap[sid]==3||rm->slotUvScaleU[sid]>1.001f||rm->slotUvScaleU[sid]<0.999f||rm->slotUvScaleV[sid]>1.001f||rm->slotUvScaleV[sid]<0.999f); if(hasUvSc){ uv[k][0]=u*us; uv[k][1]=v*vs_; } else { if(u<0.0f)u=0.0f; else if(u>1.0f)u=1.0f; if(v<0.0f)v=0.0f; else if(v>1.0f)v=1.0f; uv[k][0]=(u*(1.0f-2.0f*hu)+hu)*us; uv[k][1]=(v*(1.0f-2.0f*hv)+hv)*vs_; } }\n";
                mc << "        /* Quick accept: all 3 on-screen ? fast path (no clipping) */\n";
                mc << "        if (ok[ia] && ok[ib] && ok[ic]) {\n";
                mc << "          SV sa=sv[ia],sb=sv[ib],sc=sv[ic];\n";
                mc << "          float mnx=sa.x,mxx=sa.x,mny=sa.y,mxy=sa.y;\n";
                mc << "          if(sb.x<mnx)mnx=sb.x; if(sb.x>mxx)mxx=sb.x; if(sb.y<mny)mny=sb.y; if(sb.y>mxy)mxy=sb.y;\n";
                mc << "          if(sc.x<mnx)mnx=sc.x; if(sc.x>mxx)mxx=sc.x; if(sc.y<mny)mny=sc.y; if(sc.y>mxy)mxy=sc.y;\n";
                mc << "          if(mnx>=-32.0f && mxx<=kProjViewW+32.0f && mny>=-32.0f && mxy<=kProjViewH+32.0f){\n";
                mc << "            sa.u=uv[0][0]; sa.v=uv[0][1]; sb.u=uv[1][0]; sb.v=uv[1][1]; sc.u=uv[2][0]; sc.v=uv[2][1];\n";
                mc << "            draw_tri_vc(_trPass?&rm->hdrSlotTr[sid]:&rm->hdrSlot[sid],sa,sb,sc,colA,colB,colC); continue;\n";
                mc << "          }\n";
                mc << "        }\n";
                mc << "        /* Full frustum clip (Sutherland-Hodgman against 5 planes in camera space) */\n";
                mc << "        CV polyA[12], polyB[12];\n";
                mc << "        polyA[0]=(CV){cs[ia].x,cs[ia].y,cs[ia].z,uv[0][0],uv[0][1],fLitA};\n";
                mc << "        polyA[1]=(CV){cs[ib].x,cs[ib].y,cs[ib].z,uv[1][0],uv[1][1],fLitB};\n";
                mc << "        polyA[2]=(CV){cs[ic].x,cs[ic].y,cs[ic].z,uv[2][0],uv[2][1],fLitC};\n";
                mc << "        int pn=3;\n";
                mc << "        /* Near */\n";
                mc << "        pn=clip_poly(polyA,pn,polyB, 0,0,1,-kClipNear); if(pn<3) continue; memcpy(polyA,polyB,pn*sizeof(CV));\n";
                mc << "        /* Left: x + z*slopeX >= 0 */\n";
                mc << "        pn=clip_poly(polyA,pn,polyB, 1,0,kFrustSlopeX,0); if(pn<3) continue; memcpy(polyA,polyB,pn*sizeof(CV));\n";
                mc << "        /* Right: -x + z*slopeX >= 0 */\n";
                mc << "        pn=clip_poly(polyA,pn,polyB, -1,0,kFrustSlopeX,0); if(pn<3) continue; memcpy(polyA,polyB,pn*sizeof(CV));\n";
                mc << "        /* Bottom: y + z*slopeY >= 0 */\n";
                mc << "        pn=clip_poly(polyA,pn,polyB, 0,1,kFrustSlopeY,0); if(pn<3) continue; memcpy(polyA,polyB,pn*sizeof(CV));\n";
                mc << "        /* Top: -y + z*slopeY >= 0 */\n";
                mc << "        pn=clip_poly(polyA,pn,polyB, 0,-1,kFrustSlopeY,0); if(pn<3) continue; memcpy(polyA,polyB,pn*sizeof(CV));\n";
                mc << "        /* Project clipped polygon and fan-triangulate */\n";
                mc << "        SV svp[12];\n";
                mc << "        for(int i=0;i<pn;i++) proj_sv(polyB[i].x,polyB[i].y,polyB[i].z,polyB[i].u,polyB[i].v,&svp[i]);\n";
                mc << "        for(int i=1;i<pn-1;i++){\n";
                mc << "          float la=polyB[0].lit, lb=polyB[i].lit, lc=polyB[i+1].lit;\n";
                mc << "          if(la<0.0f)la=0.0f; if(la>1.0f)la=1.0f; if(lb<0.0f)lb=0.0f; if(lb>1.0f)lb=1.0f; if(lc<0.0f)lc=0.0f; if(lc>1.0f)lc=1.0f;\n";
                mc << "          int va=(int)(la*255.0f), vb=(int)(lb*255.0f), vc=(int)(lc*255.0f);\n";
                mc << "          uint32 ca=0xFF000000u|((uint32)va<<16)|((uint32)va<<8)|(uint32)va;\n";
                mc << "          uint32 cb=0xFF000000u|((uint32)vb<<16)|((uint32)vb<<8)|(uint32)vb;\n";
                mc << "          uint32 cc=0xFF000000u|((uint32)vc<<16)|((uint32)vc<<8)|(uint32)vc;\n";
                mc << "          if(_trPass==1){ uint32 _a=(uint32)(rm->slotOpacity[sid]*255.0f); if(_a>255u)_a=255u; ca=(ca&0x00FFFFFFu)|(_a<<24); cb=(cb&0x00FFFFFFu)|(_a<<24); cc=(cc&0x00FFFFFFu)|(_a<<24); }\n";
                mc << "          draw_tri_vc(_trPass?&rm->hdrSlotTr[sid]:&rm->hdrSlot[sid],svp[0],svp[i],svp[i+1],ca,cb,cc);\n";
                mc << "        }\n";
                mc << "      }\n";
                mc << "    }\n";
                mc << "\n";
                mc << "    pvr_list_finish();\n";
                mc << "    } /* end _trPass loop */\n";
                mc << "    pvr_scene_finish();\n";
                mc << "  }\n";
                mc << "\n";
                mc << "  return 0;\n";
                mc << "}\n";
            }

            {
                std::ofstream ec(entryCPath, std::ios::out | std::ios::trunc);
                if (ec.is_open())
                {
                    ec << "/* compatibility file; build uses main.c directly */\n";
                }
            }

            {
                std::ofstream gs(gameStubPath, std::ios::out | std::ios::trunc);
                if (gs.is_open())
                {
                    gs << "/* Auto-generated fallback gameplay hooks for Dreamcast runtime. */\n";
                    if (dcScriptCount > 1)
                    {
                        for (int si = 0; si < dcScriptCount; ++si)
                        {
                            gs << "void __attribute__((weak)) NB_Game_OnStart_" << si << "(void) {}\n";
                            gs << "void __attribute__((weak)) NB_Game_OnUpdate_" << si << "(float dt) { (void)dt; }\n";
                            gs << "void __attribute__((weak)) NB_Game_OnSceneSwitch_" << si << "(const char* sceneName) { (void)sceneName; }\n";
                        }
                    }
                    else
                    {
                        gs << "void __attribute__((weak)) NB_Game_OnStart(void) {}\n";
                        gs << "void __attribute__((weak)) NB_Game_OnUpdate(float dt) { (void)dt; }\n";
                        gs << "void __attribute__((weak)) NB_Game_OnSceneSwitch(const char* sceneName) { (void)sceneName; }\n";
                    }
                }
            }

            // KOS bindings are now consumed from engine source (src/platform/dreamcast)
            // instead of generating local copies in build_dreamcast.

            // Copy Detour sources into build_dreamcast/detour/ for SH4 compilation
            {
                std::filesystem::path detourSrcDir = std::filesystem::weakly_canonical(
                    GetExecutableDirectory() / ".." / ".." / "thirdparty" / "recastnavigation" / "Detour");
                std::filesystem::path detourDstDir = buildDir / "detour";
                std::filesystem::create_directories(detourDstDir);
                // Copy Detour source files
                const char* detourSrcs[] = {
                    "DetourAlloc.cpp", "DetourAssert.cpp", "DetourCommon.cpp",
                    "DetourNavMesh.cpp", "DetourNavMeshQuery.cpp", "DetourNode.cpp"
                };
                for (const char* fn : detourSrcs)
                {
                    auto src = detourSrcDir / "Source" / fn;
                    auto dst = detourDstDir / fn;
                    if (std::filesystem::exists(src))
                        std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
                }
                // Copy Detour headers
                const char* detourHdrs[] = {
                    "DetourAlloc.h", "DetourAssert.h", "DetourCommon.h", "DetourMath.h",
                    "DetourNavMesh.h", "DetourNavMeshBuilder.h", "DetourNavMeshQuery.h",
                    "DetourNode.h", "DetourStatus.h"
                };
                for (const char* fn : detourHdrs)
                {
                    auto src = detourSrcDir / "Include" / fn;
                    auto dst = detourDstDir / fn;
                    if (std::filesystem::exists(src))
                        std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
                }
                // Copy DetourBridge.h and DetourBridge.cpp into build_dreamcast/
                std::filesystem::path bridgeDir = std::filesystem::weakly_canonical(
                    GetExecutableDirectory() / ".." / ".." / "src" / "platform" / "dreamcast");
                auto bridgeH = bridgeDir / "DetourBridge.h";
                auto bridgeCpp = bridgeDir / "DetourBridge.cpp";
                if (std::filesystem::exists(bridgeH))
                    std::filesystem::copy_file(bridgeH, buildDir / "DetourBridge.h", std::filesystem::copy_options::overwrite_existing);
                if (std::filesystem::exists(bridgeCpp))
                    std::filesystem::copy_file(bridgeCpp, buildDir / "DetourBridge.cpp", std::filesystem::copy_options::overwrite_existing);
            }

            {
                std::filesystem::path bindingsDir = std::filesystem::weakly_canonical(GetExecutableDirectory() / ".." / ".." / "src" / "platform" / "dreamcast");
                std::string bindingsPosix = bindingsDir.string();
                std::replace(bindingsPosix.begin(), bindingsPosix.end(), '\\', '/');
                if (bindingsPosix.size() > 2 && std::isalpha((unsigned char)bindingsPosix[0]) && bindingsPosix[1] == ':')
                {
                    char drive = (char)std::tolower((unsigned char)bindingsPosix[0]);
                    bindingsPosix = std::string("/") + drive + bindingsPosix.substr(2);
                }

                std::ofstream mk(makefilePath, std::ios::out | std::ios::trunc);
                if (mk.is_open())
                {
                    mk << "TARGET = nebula_dreamcast.elf\n";
                    mk << "NEBULA_DC_BINDINGS ?= " << bindingsPosix << "\n";
                    mk << "VPATH += $(NEBULA_DC_BINDINGS)\n";
                    // Emit explicit script list (only scene-referenced scripts)
                    // instead of wildcard to avoid multiple-definition linker errors.
                    mk << "SCRIPT_SOURCES =";
                    for (const auto& ss : scriptSourcesForMake)
                        mk << " " << ss;
                    mk << "\n";
                    mk << "SOURCES = main.c KosBindings.c KosInput.c $(SCRIPT_SOURCES) NebulaGameStub.c\n";
                    mk << "OBJS = $(SOURCES:.c=.o)\n";
                    // Detour C++ sources for navmesh queries
                    mk << "DETOUR_DIR = detour\n";
                    mk << "DETOUR_CPP = DetourAlloc.cpp DetourAssert.cpp DetourCommon.cpp DetourNavMesh.cpp DetourNavMeshQuery.cpp DetourNode.cpp\n";
                    mk << "CXX_SOURCES = DetourBridge.cpp $(addprefix $(DETOUR_DIR)/,$(DETOUR_CPP))\n";
                    mk << "CXX_OBJS = $(CXX_SOURCES:.cpp=.o)\n";
                    mk << "ALL_OBJS = $(OBJS) $(CXX_OBJS)\n";
                    mk << "KOS_BASE ?= /c/DreamSDK/opt/toolchains/dc/kos\n";
                    mk << "KOS_CC_BASE ?= /c/DreamSDK/opt/toolchains/dc\n";
                    mk << "CFLAGS += -D__DREAMCAST__ -I$(KOS_BASE)/include -I$(KOS_BASE)/kernel/arch/dreamcast/include -I$(KOS_BASE)/addons/include -I$(NEBULA_DC_BINDINGS) -I. -Iscripts\n";
                    mk << "CXXFLAGS += $(CFLAGS) -fno-exceptions -fno-rtti -Idetour\n";
                    mk << "all: rm-elf $(TARGET)\n";
                    mk << "include $(KOS_BASE)/Makefile.rules\n";
                    mk << "%.o: %.c\n";
                    mk << "\tsh-elf-gcc $(CFLAGS) -c $< -o $@\n";
                    mk << "%.o: %.cpp\n";
                    mk << "\tsh-elf-g++ $(CXXFLAGS) -c $< -o $@\n";
                    mk << "$(DETOUR_DIR)/%.o: $(DETOUR_DIR)/%.cpp\n";
                    mk << "\t@mkdir -p $(DETOUR_DIR)\n";
                    mk << "\tsh-elf-g++ $(CXXFLAGS) -c $< -o $@\n";
                    mk << "clean: rm-elf\n";
                    mk << "\t-rm -f $(ALL_OBJS)\n";
                    mk << "rm-elf:\n";
                    mk << "\t-rm -f $(TARGET)\n";
                    mk << "$(TARGET): $(ALL_OBJS)\n";
                    mk << "\tkos-c++ -o $(TARGET) $(ALL_OBJS)\n";
                }
            }

            std::filesystem::path ipTxtPath = buildDir / "ip.txt";
            {
                std::ofstream ipf(ipTxtPath, std::ios::out | std::ios::trunc);
                if (ipf.is_open())
                {
                    ipf << "Hardware ID   : SEGA SEGAKATANA\n";
                    ipf << "Maker ID      : SEGA ENTERPRISES\n";
                    ipf << "Device Info   : CD-ROM1/1\n";
                    ipf << "Area Symbols  : JUE\n";
                    ipf << "Peripherals   : E000F10\n";
                    ipf << "Product No    : T-00000\n";
                    ipf << "Version       : V1.000\n";
                    ipf << "Release Date  : 20260218\n";
                    ipf << "Boot Filename : 1ST_READ.BIN\n";
                    ipf << "SW Maker Name : NEBULA\n";
                    ipf << "Game Title    : NEBULA DREAMCAST\n";
                }
            }

            {
                std::ofstream lf(logPath, std::ios::out | std::ios::trunc);
                if (lf.is_open())
                {
                    lf << "[DreamcastBuild] start\n";
                    lf << "[DreamcastCameraConvention] RH +Y-up +Z-forward right=cross(up,forward) up=cross(forward,right)\n";
                    lf << "[DreamcastCamera] source=" << cameraSourceScene
                       << " srcPos=(" << camSrc.x << "," << camSrc.y << "," << camSrc.z << ")"
                       << " srcRot=(" << camSrc.rotX << "," << camSrc.rotY << "," << camSrc.rotZ << ")"
                       << " convPos=(" << dcView.eye.x << "," << dcView.eye.y << "," << dcView.eye.z << ")"
                       << " convTarget=(" << dcView.target.x << "," << dcView.target.y << "," << dcView.target.z << ")"
                       << " convForward=(" << dcView.basis.forward.x << "," << dcView.basis.forward.y << "," << dcView.basis.forward.z << ")"
                       << " convRight=(" << dcView.basis.right.x << "," << dcView.basis.right.y << "," << dcView.basis.right.z << ")"
                       << " convUp=(" << dcView.basis.up.x << "," << dcView.basis.up.y << "," << dcView.basis.up.z << ")"
                       << " proj=(fov=" << dcProj.fovYDeg << ",a=" << dcProj.aspect << ",n=" << dcProj.nearZ << ",f=" << dcProj.farZ << ")\n";
                    lf << "[DreamcastScripts] policy=.c only (recursive from <Project>/Scripts)\n";
                    lf << "[DreamcastScripts] discovered_c=" << scriptDiscoveredC
                       << " copied_c=" << scriptCopiedC
                       << " ignored_cpp=" << scriptIgnoredCpp << "\n";
                    for (const auto& s : scriptSourcesForMake)
                        lf << "[DreamcastScripts] source=" << s << "\n";
                    if (scriptSourcesForMake.empty())
                        lf << "[DreamcastScripts] using generated weak stub only\n";
                }
            }

            std::string buildDirCmd = buildDir.string();
            std::replace(buildDirCmd.begin(), buildDirCmd.end(), '\\', '/');
            std::string logPathCmd = logPath.string();
            std::replace(logPathCmd.begin(), logPathCmd.end(), '\\', '/');

            int rc = 1;
            if (!stagingNameCollision)
            {
                std::string scriptPathCmd = buildDirCmd + "/_nebula_build_dreamcast.bat";
                std::string batCmd = "cmd /c \"\"" + scriptPathCmd + "\" >> \"" + logPathCmd + "\" 2>&1\"";
                rc = NebulaDreamcastBuild::RunCommand(batCmd.c_str());
            }
            else
            {
                std::ofstream lf(logPath, std::ios::out | std::ios::app);
                if (lf.is_open()) lf << stagingNameCollisionMessage << "\n";
                printf("%s\n", stagingNameCollisionMessage.c_str());
            }

            std::filesystem::path elfPath = buildDir / "nebula_dreamcast.elf";
            std::filesystem::path binPath = buildDir / "nebula_dreamcast.bin";
            std::filesystem::path firstPath = buildDir / "1ST_READ.BIN";
            std::filesystem::path isoPath = buildDir / "nebula_dreamcast.iso";
            std::filesystem::path cdiPath = buildDir / "nebula_dreamcast.cdi";

            bool haveElf = std::filesystem::exists(elfPath);
            bool haveBin = std::filesystem::exists(binPath);
            bool have1st = std::filesystem::exists(firstPath);
            bool haveIso = std::filesystem::exists(isoPath);
            bool haveCdi = std::filesystem::exists(cdiPath);
            bool artifactsOk = haveElf && haveBin && have1st && haveIso && haveCdi;

            if (stagingNameCollision)
            {
                gViewportToast = "Dreamcast build failed (staged filename collision; see package.log)";
            }
            else if (rc == 0 && artifactsOk)
            {
                gViewportToast = "Dreamcast build complete (see build_dreamcast)";
            }
            else
            {
                std::ofstream lf(logPath, std::ios::out | std::ios::app);
                if (lf.is_open())
                {
                    lf << "[DreamcastBuild] buildDir=" << buildDir.string() << "\n";
                    lf << "[DreamcastBuild] rc=" << rc << " artifactsOk=" << (artifactsOk ? 1 : 0) << "\n";
                    lf << "[ArtifactPaths] elf=" << elfPath.string()
                       << " | bin=" << binPath.string()
                       << " | 1st=" << firstPath.string()
                       << " | iso=" << isoPath.string()
                       << " | cdi=" << cdiPath.string() << "\n";
                    lf << "[Artifacts] elf=" << (haveElf ? 1 : 0)
                       << " bin=" << (haveBin ? 1 : 0)
                       << " 1st=" << (have1st ? 1 : 0)
                       << " iso=" << (haveIso ? 1 : 0)
                       << " cdi=" << (haveCdi ? 1 : 0) << "\n";

                    int listed = 0;
                    lf << "[DirList]\n";
                    std::error_code ecList;
                    for (const auto& de : std::filesystem::directory_iterator(buildDir, ecList))
                    {
                        lf << " - " << de.path().filename().string() << "\n";
                        if (++listed >= 64) break;
                    }
                    if (ecList)
                        lf << "[DirListError] " << ecList.message() << "\n";
                    lf << "[DreamcastBuild] If script compile/link failed, check errors above and ensure gameplay hooks are valid C symbols.\n";
                    lf << "[DreamcastBuild] Expected hooks: NB_Game_OnStart, NB_Game_OnUpdate, NB_Game_OnSceneSwitch.\n";
                }

                int generatedCount = (haveElf ? 1 : 0) + (haveBin ? 1 : 0) + (have1st ? 1 : 0) + (haveIso ? 1 : 0) + (haveCdi ? 1 : 0);
                if (generatedCount > 0)
                {
                    gViewportToast = "Dreamcast artifacts generated (" + std::to_string(generatedCount) + "/5). Check build_dreamcast";
                }
                else
                {
                    gViewportToast = "Dreamcast build files generated. Check dreamcast_build";
                }
            }
            gViewportToastUntil = glfwGetTime() + 4.0;
        }
    }

    if (useLegacyDreamcastBuilder)
    {
    std::filesystem::path buildDir = std::filesystem::path(gProjectDir) / "build_saturn";
    std::filesystem::path cdDir = buildDir / "cd";
    std::filesystem::create_directories(cdDir);

    // Export default scene (if configured), otherwise current editor scene snapshot.
    std::vector<Audio3DNode> exportNodes = gAudio3DNodes;
    std::vector<StaticMesh3DNode> exportStatics = gStaticMeshNodes;
    std::vector<Camera3DNode> exportCameras = gCamera3DNodes;

    // Bake simple parent hierarchy into world transforms for export.
    auto findExportStaticByName = [&](const std::string& nm)->int {
        for (int i = 0; i < (int)exportStatics.size(); ++i) if (exportStatics[i].name == nm) return i;
        return -1;
    };
    for (int i = 0; i < (int)exportStatics.size(); ++i)
    {
        float wx = exportStatics[i].x, wy = exportStatics[i].y, wz = exportStatics[i].z;
        float wrx = exportStatics[i].rotX, wry = exportStatics[i].rotY, wrz = exportStatics[i].rotZ;
        float wsx = exportStatics[i].scaleX, wsy = exportStatics[i].scaleY, wsz = exportStatics[i].scaleZ;
        std::string p = exportStatics[i].parent;
        int guard = 0;
        while (!p.empty() && guard++ < 256)
        {
            int pi = findExportStaticByName(p);
            if (pi < 0) break;
            const auto& pn = exportStatics[pi];
            wx += pn.x; wy += pn.y; wz += pn.z;
            wrx += pn.rotX; wry += pn.rotY; wrz += pn.rotZ;
            wsx *= pn.scaleX; wsy *= pn.scaleY; wsz *= pn.scaleZ;
            p = pn.parent;
        }
        exportStatics[i].x = wx; exportStatics[i].y = wy; exportStatics[i].z = wz;
        exportStatics[i].rotX = wrx; exportStatics[i].rotY = wry; exportStatics[i].rotZ = wrz;
        exportStatics[i].scaleX = wsx; exportStatics[i].scaleY = wsy; exportStatics[i].scaleZ = wsz;
    }

    if (gActiveScene >= 0 && gActiveScene < (int)gOpenScenes.size())
    {
        gOpenScenes[gActiveScene].nodes = gAudio3DNodes;
        gOpenScenes[gActiveScene].staticMeshes = gStaticMeshNodes;
        gOpenScenes[gActiveScene].cameras = gCamera3DNodes;
    }

    std::string defaultSceneCfg = GetProjectDefaultScene(std::filesystem::path(gProjectDir));
    if (!defaultSceneCfg.empty())
    {
        std::filesystem::path defaultScenePath(defaultSceneCfg);
        if (defaultScenePath.is_relative())
            defaultScenePath = std::filesystem::path(gProjectDir) / defaultScenePath;

        bool foundOpen = false;
        for (const auto& s : gOpenScenes)
        {
            if (s.path == defaultScenePath)
            {
                exportNodes = s.nodes;
                exportStatics = s.staticMeshes;
                exportCameras = s.cameras;
                foundOpen = true;
                break;
            }
        }

        if (!foundOpen)
        {
            SceneData loaded{};
            if (LoadSceneFromPath(defaultScenePath, loaded))
            {
                exportNodes = loaded.nodes;
                exportStatics = loaded.staticMeshes;
                exportCameras = loaded.cameras;
            }
        }
    }

    NebulaScene::SaveSceneToPath(buildDir / "scene_export.nebscene", exportNodes, exportStatics, exportCameras, gNode3DNodes);

    std::vector<std::string> texWarnings;

    // Minimal CD metadata required by JO engine mkisofs flags.
    {
        std::ofstream absTxt(cdDir / "ABS.TXT", std::ios::out | std::ios::trunc); if (absTxt.is_open()) absTxt << "Nebula Dreamcast Build";
        std::ofstream cpyTxt(cdDir / "CPY.TXT", std::ios::out | std::ios::trunc); if (cpyTxt.is_open()) cpyTxt << "(C) Nebula";
        std::ofstream bibTxt(cdDir / "BIB.TXT", std::ios::out | std::ios::trunc); if (bibTxt.is_open()) bibTxt << "Nebula Dreamcast Scene Package";
    }

    // Segment 2.0 baseline texture (Saturn-safe subset): reuse JO sample checker texture.
    {
        std::filesystem::path texSrc = std::filesystem::path("C:/Users/NoSig/Documents/SaturnDev/JO_engine/Samples/demo - 3D/cd/BOX.TGA");
        std::filesystem::path texDstCd = cdDir / "BOX.TGA";
        std::filesystem::path texDstRoot = buildDir / "BOX.TGA";
        std::error_code ec;
        if (std::filesystem::exists(texSrc))
        {
            std::filesystem::copy_file(texSrc, texDstCd, std::filesystem::copy_options::overwrite_existing, ec);
            ec.clear();
            std::filesystem::copy_file(texSrc, texDstRoot, std::filesystem::copy_options::overwrite_existing, ec);
        }
    }

    // Generate Segment-1/2 Saturn runtime: camera + transformed mesh markers.
    {
        Camera3DNode camSrc{};
        bool haveCam = false;
        for (const auto& c : exportCameras)
        {
            if (c.main) { camSrc = c; haveCam = true; break; }
        }
        if (!haveCam && !exportCameras.empty()) { camSrc = exportCameras[0]; haveCam = true; }

        if (!haveCam)
        {
            camSrc.x = 0.0f; camSrc.y = 0.0f; camSrc.z = -20.0f;
            camSrc.rotX = 0.0f; camSrc.rotY = 0.0f; camSrc.rotZ = 0.0f;
        }

        Vec3 right{}, up{}, forward{};
        GetLocalAxesFromEuler(camSrc.rotX, camSrc.rotY, camSrc.rotZ, right, up, forward);

        constexpr float kSaturnScale = 8.0f;
        int camX = (int)std::lround(camSrc.x * kSaturnScale);
        int camY = (int)std::lround(-camSrc.y * kSaturnScale); // Saturn Y-axis correction (editor up -> Saturn up)
        int camZ = (int)std::lround(camSrc.z * kSaturnScale);
        int tgtX = (int)std::lround((camSrc.x + forward.x * 20.0f) * kSaturnScale);
        int tgtY = (int)std::lround(-(camSrc.y + forward.y * 20.0f) * kSaturnScale);
        int tgtZ = (int)std::lround((camSrc.z + forward.z * 20.0f) * kSaturnScale);

        // Segment 2.1: per-mesh texture mapping (Saturn-safe subset).
        // Clean stale staged TX##.TGA files so JO never sees old invalid dimensions.
        auto clearStagedSaturnTextures = [&](const std::filesystem::path& dir)
        {
            std::error_code ec;
            if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) return;
            for (const auto& e : std::filesystem::directory_iterator(dir, ec))
            {
                if (ec) break;
                if (!e.is_regular_file()) continue;
                std::string stem = e.path().stem().string();
                std::string ext = e.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                if (ext != ".tga") continue;
                if (stem.size() == 4 && stem[0] == 'T' && stem[1] == 'X' && std::isdigit((unsigned char)stem[2]) && std::isdigit((unsigned char)stem[3]))
                {
                    std::filesystem::remove(e.path(), ec);
                    ec.clear();
                }
            }
        };
        clearStagedSaturnTextures(cdDir);
        clearStagedSaturnTextures(buildDir);

        constexpr int kMaxSaturnTextures = 64;
        std::vector<int> meshTexSlot(std::max(1, (int)exportStatics.size()), -1);
        std::vector<std::array<int, kStaticMeshMaterialSlots>> meshMatTexSlot(std::max(1, (int)exportStatics.size()));
        std::vector<std::array<int, kStaticMeshMaterialSlots>> meshMatUvRepeatPow(std::max(1, (int)exportStatics.size()));
        std::vector<std::array<int, kStaticMeshMaterialSlots>> meshMatShadeMode(std::max(1, (int)exportStatics.size()));
        std::vector<std::array<float, kStaticMeshMaterialSlots>> meshMatLightYaw(std::max(1, (int)exportStatics.size()));
        std::vector<std::array<float, kStaticMeshMaterialSlots>> meshMatLightPitch(std::max(1, (int)exportStatics.size()));
        std::vector<std::array<float, kStaticMeshMaterialSlots>> meshMatLightRoll(std::max(1, (int)exportStatics.size()));
        std::vector<std::array<float, kStaticMeshMaterialSlots>> meshMatShadowIntensity(std::max(1, (int)exportStatics.size()));
        for (auto& arr : meshMatTexSlot) arr.fill(-1);
        for (auto& arr : meshMatUvRepeatPow) arr.fill(0);
        for (auto& arr : meshMatShadeMode) arr.fill(0);
        for (auto& arr : meshMatLightYaw) arr.fill(0.0f);
        for (auto& arr : meshMatLightPitch) arr.fill(35.0f);
        for (auto& arr : meshMatLightRoll) arr.fill(0.0f);
        for (auto& arr : meshMatShadowIntensity) arr.fill(1.0f);
        std::vector<std::string> slotFileName;
        std::unordered_map<std::string, int> sourceToSlot;

        auto toLower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        };

        auto stageTexturePath = [&](const std::filesystem::path& texPath, const std::string& meshName)->int
        {
            if (!std::filesystem::exists(texPath))
            {
                texWarnings.push_back("[Segment2.1] Missing texture: " + texPath.string() + " (mesh " + meshName + ")");
                return -1;
            }

            std::string ext = toLower(texPath.extension().string());
            if (ext != ".tga" && ext != ".nebtex")
            {
                texWarnings.push_back("[Segment2.1] Saturn constraint warning: unsupported texture format (only .tga/.nebtex): " + texPath.string() + " (mesh " + meshName + ")");
                return -1;
            }

            std::string key = texPath.lexically_normal().string();
            auto it = sourceToSlot.find(key);
            if (it != sourceToSlot.end()) return it->second;

            if ((int)slotFileName.size() >= kMaxSaturnTextures)
            {
                texWarnings.push_back("[Segment2.1] Texture limit exceeded (max " + std::to_string(kMaxSaturnTextures) + "), using fallback for mesh " + meshName);
                return -1;
            }

            int slot = (int)slotFileName.size();
            char nameBuf[16];
            snprintf(nameBuf, sizeof(nameBuf), "TX%02d.TGA", slot);
            std::string satName = nameBuf;
            std::filesystem::path dstCd = cdDir / satName;
            std::filesystem::path dstRoot = buildDir / satName;

            std::error_code ec;
            bool ok = false;
            if (ext == ".tga")
            {
                std::string warn;
                bool c1 = ConvertTgaToJoSafeTga24(texPath, dstCd, warn);
                bool c2 = ConvertTgaToJoSafeTga24(texPath, dstRoot, warn);
                ok = c1 && c2;
                if (!warn.empty())
                    texWarnings.push_back("[Segment2.1] " + warn + " Source: " + texPath.string() + " (mesh " + meshName + ")");
            }
            else
            {
                std::string warn;
                bool c1 = ConvertNebTexToTga24(texPath, dstCd, warn);
                bool c2 = ConvertNebTexToTga24(texPath, dstRoot, warn);
                ok = c1 && c2;
                if (!warn.empty())
                    texWarnings.push_back("[Segment2.1] " + warn + " Source: " + texPath.string() + " (mesh " + meshName + ")");
            }

            if (!ok)
            {
                std::string em = ec ? ec.message() : std::string("conversion/copy failed");
                texWarnings.push_back("[Segment2.1] Saturn constraint warning: failed to stage texture: " + texPath.string() + " (" + em + ")");
                return -1;
            }

            sourceToSlot[key] = slot;
            slotFileName.push_back(satName);
            return slot;
        };

        for (size_t i = 0; i < exportStatics.size(); ++i)
        {
            const auto& sm = exportStatics[i];
            if (gProjectDir.empty()) continue;

            for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
            {
                std::string matRef = (si >= 0 && si < kStaticMeshMaterialSlots) ? sm.materialSlots[si] : std::string();
                if (matRef.empty() && si == 0) matRef = sm.material; // legacy scene fallback
                if (matRef.empty()) continue;
                std::filesystem::path matPath = std::filesystem::path(gProjectDir) / matRef;

                bool saturnUvExtended = false;
                float matUvScale = 0.0f;
                NebulaAssets::LoadMaterialAllowUvRepeat(matPath, saturnUvExtended);
                NebulaAssets::LoadMaterialUvScale(matPath, matUvScale);
                int repeatPow = 0;
                if (saturnUvExtended)
                {
                    // Saturn extended-UV repeat uses negative uv_scale convention from editor:
                    // uv_scale=-1 => 2x repeat, -2 => 4x repeat, -3 => 8x repeat.
                    if (matUvScale < -0.001f)
                    {
                        repeatPow = std::max(0, (int)std::lround(-matUvScale));
                        if (repeatPow > 3)
                        {
                            texWarnings.push_back("[SaturnUV] -uv_scale clamped to 3 for runtime safety: " + matRef + " (uv_scale=" + std::to_string(matUvScale) + ")");
                            repeatPow = 3;
                        }
                    }
                    else if (matUvScale > 0.001f)
                    {
                        texWarnings.push_back("[SaturnUV] positive uv_scale is not representable in extended repeat path; using 0: " + matRef + " (uv_scale=" + std::to_string(matUvScale) + ")");
                    }
                }
                meshMatUvRepeatPow[i][si] = repeatPow;
                meshMatShadeMode[i][si] = NebulaAssets::LoadMaterialShadingMode(matPath);
                meshMatLightYaw[i][si] = NebulaAssets::LoadMaterialLightRotation(matPath);
                meshMatLightPitch[i][si] = NebulaAssets::LoadMaterialLightPitch(matPath);
                meshMatLightRoll[i][si] = NebulaAssets::LoadMaterialLightRoll(matPath);
                meshMatShadowIntensity[i][si] = NebulaAssets::LoadMaterialShadowIntensity(matPath);

                std::string texPathStr;
                if (!NebulaAssets::LoadMaterialTexture(matPath, texPathStr) || texPathStr.empty()) continue;
                std::filesystem::path texPath = std::filesystem::path(texPathStr);
                if (texPath.is_relative()) texPath = std::filesystem::path(gProjectDir) / texPath;
                int staged = stageTexturePath(texPath, sm.name);
                if (staged >= 0) meshMatTexSlot[i][si] = staged;
            }

            int activeSi = sm.materialSlot;
            if (activeSi < 0 || activeSi >= kStaticMeshMaterialSlots) activeSi = 0;
            meshTexSlot[i] = meshMatTexSlot[i][activeSi];

            std::string dbg = "[DebugMatMap] " + sm.name + " slots:";
            for (int si = 0; si < kStaticMeshMaterialSlots; ++si)
            {
                if (meshMatTexSlot[i][si] >= 0)
                    dbg += " s" + std::to_string(si + 1) + "->TX" + std::to_string(meshMatTexSlot[i][si]);
            }
            texWarnings.push_back(dbg);
        }

        // Segment 3.0: bake real .nebmesh triangle geometry (Saturn-safe subset).
        std::vector<int> geoHasMesh(std::max(1, (int)exportStatics.size()), 0);
        std::vector<int> geoPolyCount(std::max(1, (int)exportStatics.size()), 0);
        std::vector<int> geoDataOffset(std::max(1, (int)exportStatics.size()), 0);
        std::vector<int> geoTexSlot(std::max(1, (int)exportStatics.size()), -1);
        std::vector<int> geoPolyTexSlot; // one texture slot per generated polygon
        std::vector<int> geoPolyUvRepeatPow; // per polygon repeat exponent transferred from material uv_scale (2^n)
        std::vector<int> geoVertData; // 12 ints per polygon (4 vertices x xyz)
        constexpr int kMaxSaturnTrianglesPerMesh = 65535;
        constexpr bool kRequireCanonicalFaceRecords = false;

        for (size_t i = 0; i < exportStatics.size(); ++i)
        {
            geoTexSlot[i] = meshTexSlot[i];
            const auto& sm = exportStatics[i];
            if (sm.mesh.empty() || gProjectDir.empty())
                continue;

            std::filesystem::path meshPath = std::filesystem::path(sm.mesh);
            if (meshPath.is_relative())
                meshPath = std::filesystem::path(gProjectDir) / meshPath;

            NebMesh baked{};
            if (!LoadNebMesh(meshPath, baked) || !baked.valid || baked.indices.size() < 3)
            {
                texWarnings.push_back("[Segment3.0] Mesh unavailable for Saturn geometry, using cube proxy: " + meshPath.string());
                continue;
            }

            int triCount = (int)(baked.indices.size() / 3);
            /* Triangle truncation safety fallback disabled per user request. */
            if (triCount <= 0)
                continue;

            if (baked.hasFaceRecords && !baked.faceRecords.empty())
            {
                texWarnings.push_back("[Segment3.1] Canonical face records ACTIVE for " + sm.name + " faces=" + std::to_string((int)baked.faceRecords.size()));
            }
            else
            {
                texWarnings.push_back("[Segment3.1] Canonical face records MISSING for " + sm.name + " (re-export/re-import .nebmesh required)");
                if (kRequireCanonicalFaceRecords)
                {
                    texWarnings.push_back("[Segment3.1] Skipping mesh due to strict canonical mode: " + sm.name);
                    continue;
                }
            }

            geoHasMesh[i] = 1;
            geoDataOffset[i] = (int)geoVertData.size() / 12;

            float sxm = std::max(0.01f, sm.scaleX);
            float sym = std::max(0.01f, sm.scaleY);
            float szm = std::max(0.01f, sm.scaleZ);
            constexpr float kLocalMeshScale = 12.0f;

            int dbgMissingPolyTex = 0;
            int emittedPolyCount = 0;

            auto lerp3 = [](const Vec3& a, const Vec3& b, float t)->Vec3 {
                return Vec3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
            };

            auto emitPolyFromVertices = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d, int triMat, int repeatPow)
            {
                // Global Saturn bake-time V flip for all emitted polygons.
                // (Do it in generated geometry order, not runtime input toggles.)
                std::swap(a, d);
                std::swap(b, c);

                int ax = (int)std::lround(a.x * sxm * kLocalMeshScale);
                int ay = (int)std::lround(-a.y * sym * kLocalMeshScale);
                int az = (int)std::lround(a.z * szm * kLocalMeshScale);
                int bx = (int)std::lround(b.x * sxm * kLocalMeshScale);
                int by = (int)std::lround(-b.y * sym * kLocalMeshScale);
                int bz = (int)std::lround(b.z * szm * kLocalMeshScale);
                int cx = (int)std::lround(c.x * sxm * kLocalMeshScale);
                int cy = (int)std::lround(-c.y * sym * kLocalMeshScale);
                int cz = (int)std::lround(c.z * szm * kLocalMeshScale);
                int dx = (int)std::lround(d.x * sxm * kLocalMeshScale);
                int dy = (int)std::lround(-d.y * sym * kLocalMeshScale);
                int dz = (int)std::lround(d.z * szm * kLocalMeshScale);

                geoVertData.push_back(ax); geoVertData.push_back(ay); geoVertData.push_back(az);
                geoVertData.push_back(bx); geoVertData.push_back(by); geoVertData.push_back(bz);
                geoVertData.push_back(cx); geoVertData.push_back(cy); geoVertData.push_back(cz);
                geoVertData.push_back(dx); geoVertData.push_back(dy); geoVertData.push_back(dz);

                int triTexSlot = -1;
                if (triMat >= 0 && triMat < kStaticMeshMaterialSlots) triTexSlot = meshMatTexSlot[i][triMat];
                if (triTexSlot < 0) { triTexSlot = meshTexSlot[i]; dbgMissingPolyTex++; }
                geoPolyTexSlot.push_back(triTexSlot);
                geoPolyUvRepeatPow.push_back(repeatPow);
                emittedPolyCount++;
            };

            auto emitPoly = [&](uint16_t ia, uint16_t ib, uint16_t ic, uint16_t id, int triMat)
            {
                if (ia >= baked.positions.size() || ib >= baked.positions.size() || ic >= baked.positions.size() || id >= baked.positions.size())
                    return;

                Vec3 a = baked.positions[ia];
                Vec3 b = baked.positions[ib];
                Vec3 c = baked.positions[ic];
                Vec3 d = baked.positions[id];

                int repeatPow = 0;
                if (triMat >= 0 && triMat < kStaticMeshMaterialSlots)
                    repeatPow = std::max(0, meshMatUvRepeatPow[i][triMat]);

                bool isTri = (id == ia);
                if (repeatPow <= 0)
                {
                    emitPolyFromVertices(a, b, c, d, triMat, repeatPow);
                    return;
                }

                int effectivePow = std::min(repeatPow, 3);

                if (!isTri)
                {
                    int div = 1 << effectivePow;
                    for (int uy = 0; uy < div; ++uy)
                    {
                        float t0 = (float)uy / (float)div;
                        float t1 = (float)(uy + 1) / (float)div;
                        Vec3 l0 = lerp3(a, d, t0);
                        Vec3 l1 = lerp3(a, d, t1);
                        Vec3 r0 = lerp3(b, c, t0);
                        Vec3 r1 = lerp3(b, c, t1);
                        for (int ux = 0; ux < div; ++ux)
                        {
                            float s0 = (float)ux / (float)div;
                            float s1 = (float)(ux + 1) / (float)div;
                            Vec3 p00 = lerp3(l0, r0, s0);
                            Vec3 p10 = lerp3(l0, r0, s1);
                            Vec3 p11 = lerp3(l1, r1, s1);
                            Vec3 p01 = lerp3(l1, r1, s0);
                            emitPolyFromVertices(p00, p10, p11, p01, triMat, repeatPow);
                        }
                    }
                }
                else
                {
                    struct Tri3 { Vec3 a, b, c; };
                    std::vector<Tri3> tris;
                    tris.push_back(Tri3{ a, b, c });
                    for (int level = 0; level < effectivePow; ++level)
                    {
                        std::vector<Tri3> next;
                        next.reserve(tris.size() * 4);
                        for (const Tri3& t : tris)
                        {
                            Vec3 ab = lerp3(t.a, t.b, 0.5f);
                            Vec3 bc = lerp3(t.b, t.c, 0.5f);
                            Vec3 ca = lerp3(t.c, t.a, 0.5f);
                            next.push_back(Tri3{ t.a, ab, ca });
                            next.push_back(Tri3{ ab, t.b, bc });
                            next.push_back(Tri3{ ca, bc, t.c });
                            next.push_back(Tri3{ ab, bc, ca });
                        }
                        tris.swap(next);
                    }
                    for (const Tri3& t : tris)
                        emitPolyFromVertices(t.a, t.b, t.c, t.a, triMat, repeatPow);
                }
            };

            if (baked.hasFaceRecords && !baked.faceRecords.empty())
            {
                // Segment 3.1b: canonical authored face path (no quad reconstruction heuristics).
                // JO quads infer UVs from vertex order, so detect per-face mirrored mapping from
                // geometry+UV handedness and apply a local permutation only when needed.
                auto cross3 = [](const Vec3& a, const Vec3& b)->Vec3 {
                    return Vec3{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
                };
                auto dot3 = [](const Vec3& a, const Vec3& b)->float {
                    return a.x * b.x + a.y * b.y + a.z * b.z;
                };

                for (const auto& fr : baked.faceRecords)
                {
                    if (fr.arity == 4)
                    {
                        uint16_t q0 = fr.indices[0];
                        uint16_t q1 = fr.indices[1];
                        uint16_t q2 = fr.indices[2];
                        uint16_t q3 = fr.indices[3];

                        bool mirrored = false;
                        if (q0 < baked.positions.size() && q1 < baked.positions.size() && q2 < baked.positions.size() && q3 < baked.positions.size())
                        {
                            const Vec3& p0 = baked.positions[q0];
                            const Vec3& p1 = baked.positions[q1];
                            const Vec3& p2 = baked.positions[q2];
                            const Vec3& p3 = baked.positions[q3];

                            Vec3 e10{ p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
                            Vec3 e30{ p3.x - p0.x, p3.y - p0.y, p3.z - p0.z };
                            Vec3 e20{ p2.x - p0.x, p2.y - p0.y, p2.z - p0.z };
                            Vec3 n = cross3(e10, e20);
                            float geomDet = dot3(cross3(e10, e30), n);

                            float du1 = fr.uvs[1].x - fr.uvs[0].x;
                            float dv1 = fr.uvs[1].y - fr.uvs[0].y;
                            float du2 = fr.uvs[3].x - fr.uvs[0].x;
                            float dv2 = fr.uvs[3].y - fr.uvs[0].y;
                            float uvDet = du1 * dv2 - dv1 * du2;

                            mirrored = ((geomDet * uvDet) < 0.0f);
                        }

                        Vec3 uv0 = fr.uvs[0];
                        Vec3 uv1 = fr.uvs[1];
                        Vec3 uv2 = fr.uvs[2];
                        Vec3 uv3 = fr.uvs[3];

                        if (mirrored)
                        {
                            // Local per-face mirror correction (U flip permutation).
                            std::swap(q0, q1);
                            std::swap(q2, q3);
                            std::swap(uv0, uv1);
                            std::swap(uv2, uv3);
                        }

                        // Phase alignment: pick a stable corner start from authored UVs,
                        // then rotate quad order so Saturn uses consistent corner phase.
                        uint16_t qv[4] = { q0, q1, q2, q3 };
                        Vec3 uvs[4] = { uv0, uv1, uv2, uv3 };
                        float uMin = uvs[0].x, vMin = uvs[0].y;
                        for (int k = 1; k < 4; ++k)
                        {
                            if (uvs[k].x < uMin) uMin = uvs[k].x;
                            if (uvs[k].y < vMin) vMin = uvs[k].y;
                        }
                        int bestRot = 0;
                        float bestScore = 1e30f;
                        for (int r = 0; r < 4; ++r)
                        {
                            const Vec3& uv = uvs[r];
                            float du = uv.x - uMin;
                            float dv = uv.y - vMin;
                            float score = du * du + dv * dv;
                            if (score < bestScore)
                            {
                                bestScore = score;
                                bestRot = r;
                            }
                        }

                        q0 = qv[bestRot & 3];
                        q1 = qv[(bestRot + 1) & 3];
                        q2 = qv[(bestRot + 2) & 3];
                        q3 = qv[(bestRot + 3) & 3];

                        emitPoly(q0, q1, q2, q3, (int)fr.material);
                    }
                    else
                    {
                        emitPoly(fr.indices[0], fr.indices[1], fr.indices[2], fr.indices[0], (int)fr.material);
                    }
                }
            }
            else if (baked.hasFaceTopology && !baked.faceVertexCounts.empty())
            {
                int triCursor = 0;
                for (size_t fi = 0; fi < baked.faceVertexCounts.size() && triCursor < triCount; ++fi)
                {
                    int fv = (int)baked.faceVertexCounts[fi];
                    if (fv < 3) continue;
                    int faceTriCount = std::max(1, fv - 2);

                    if (fv == 4 && triCursor + 1 < triCount)
                    {
                        uint16_t t0[3] = {
                            baked.indices[triCursor * 3 + 0],
                            baked.indices[triCursor * 3 + 1],
                            baked.indices[triCursor * 3 + 2]
                        };
                        uint16_t t1[3] = {
                            baked.indices[(triCursor + 1) * 3 + 0],
                            baked.indices[(triCursor + 1) * 3 + 1],
                            baked.indices[(triCursor + 1) * 3 + 2]
                        };

                        uint16_t shared[2] = { 0, 0 };
                        int sharedCount = 0;
                        uint16_t unique0 = t0[0];
                        uint16_t unique1 = t1[0];

                        auto hasIn = [](const uint16_t v[3], uint16_t x) {
                            return (v[0] == x || v[1] == x || v[2] == x);
                        };

                        for (int vi = 0; vi < 3; ++vi)
                        {
                            if (hasIn(t1, t0[vi]))
                            {
                                if (sharedCount < 2) shared[sharedCount++] = t0[vi];
                            }
                            else
                            {
                                unique0 = t0[vi];
                            }
                        }
                        for (int vi = 0; vi < 3; ++vi)
                        {
                            if (!hasIn(t0, t1[vi]))
                            {
                                unique1 = t1[vi];
                                break;
                            }
                        }

                        uint16_t a = t0[0], b = t0[1], c = t0[2], d = t1[2];
                        if (sharedCount == 2)
                        {
                            // Legacy reconstruction fallback for old mesh versions.
                            a = shared[0];
                            b = unique0;
                            c = shared[1];
                            d = unique1;
                        }

                        int triMat = 0;
                        if (baked.hasFaceMaterial && triCursor >= 0 && triCursor < (int)baked.faceMaterial.size()) triMat = (int)baked.faceMaterial[triCursor];
                        emitPoly(a, b, c, d, triMat);
                        triCursor += 2;
                    }
                    else
                    {
                        for (int ft = 0; ft < faceTriCount && triCursor < triCount; ++ft, ++triCursor)
                        {
                            uint16_t a = baked.indices[triCursor * 3 + 0];
                            uint16_t b = baked.indices[triCursor * 3 + 1];
                            uint16_t c = baked.indices[triCursor * 3 + 2];
                            int triMat = 0;
                            if (baked.hasFaceMaterial && triCursor >= 0 && triCursor < (int)baked.faceMaterial.size()) triMat = (int)baked.faceMaterial[triCursor];
                            emitPoly(a, b, c, a, triMat);
                        }
                    }
                }

                for (; triCursor < triCount; ++triCursor)
                {
                    uint16_t a = baked.indices[triCursor * 3 + 0];
                    uint16_t b = baked.indices[triCursor * 3 + 1];
                    uint16_t c = baked.indices[triCursor * 3 + 2];
                    int triMat = 0;
                    if (baked.hasFaceMaterial && triCursor >= 0 && triCursor < (int)baked.faceMaterial.size()) triMat = (int)baked.faceMaterial[triCursor];
                    emitPoly(a, b, c, a, triMat);
                }
            }
            else
            {
                for (int t = 0; t < triCount; ++t)
                {
                    uint16_t a = baked.indices[t * 3 + 0];
                    uint16_t b = baked.indices[t * 3 + 1];
                    uint16_t c = baked.indices[t * 3 + 2];
                    int triMat = 0;
                    if (baked.hasFaceMaterial && t >= 0 && t < (int)baked.faceMaterial.size()) triMat = (int)baked.faceMaterial[t];
                    emitPoly(a, b, c, a, triMat);
                }
            }

            geoPolyCount[i] = emittedPolyCount;
            texWarnings.push_back("[DebugFaceMap] " + sm.name + " fallbackPolyTex=" + std::to_string(dbgMissingPolyTex) + "/" + std::to_string(std::max(1, emittedPolyCount)));
        }

        std::ofstream mainC(buildDir / "main.c", std::ios::out | std::ios::trunc);
        if (mainC.is_open())
        {
            mainC << "#include \"jo/jo.h\"\n";
            mainC << "typedef struct MeshInst { int x,y,z, rx,ry,rz, sx,sy,sz; } MeshInst;\n";
            mainC << "static jo_camera cam;\n";
            mainC << "static int gDbgSprite = -1;\n";
            mainC << "static int gTexSlots[64] = {"
                  "-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,"
                  "-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,"
                  "-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,"
                  "-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};\n";
            mainC << "static jo_3d_mesh *gGeoMeshes[" << std::max(1, (int)exportStatics.size()) << "] = {0};\n";
            mainC << "static jo_3d_mesh *gGeoMeshesBack[" << std::max(1, (int)exportStatics.size()) << "] = {0};\n";
            mainC << "static jo_vertice cube_vertices[] = JO_3D_CUBE_VERTICES(8);\n";
            mainC << "static jo_3d_quad cube_quads[6];\n";
            mainC << "static int gInputRotX = 0;\n";
            mainC << "static int gInputRotY = 0;\n";
            mainC << "static int gFlipU = 0;\n";
            mainC << "static int gFlipV = 0;\n";
            mainC << "static int gNoCull = 0;\n";
            mainC << "static int gNearClipAssist = 0;\n";
            mainC << "static int gPrevC = 0;\n";
            mainC << "static int gPrevA = 0;\n";
            mainC << "static int gCamX = " << camX << ";\n";
            mainC << "static int gCamY = " << camY << ";\n";
            mainC << "static int gCamZ = " << camZ << ";\n";
            mainC << "static int gTgtX = " << tgtX << ";\n";
            mainC << "static int gTgtY = " << tgtY << ";\n";
            mainC << "static int gTgtZ = " << tgtZ << ";\n";
            mainC << "static int gStarInited = 0;\n";
            mainC << "static int gStarCount = 24;\n";
            mainC << "static int gStarX[24], gStarY[24], gStarZ[24], gStarBucket[24];\n";
            mainC << "static const int gMeshCount = " << (int)exportStatics.size() << ";\n";
            mainC << "static int gMeshCreateOk[" << std::max(1, (int)exportStatics.size()) << "] = {0};\n";
            mainC << "static int gMeshCreateFailCount = 0;\n";
            mainC << "static int gMeshCreateLastFail = -1;\n";
            mainC << "static int gDrawMeshLimit = -1;\n";
            mainC << "static MeshInst gMeshes[" << std::max(1, (int)exportStatics.size()) << "] = {\n";
            if (exportStatics.empty())
            {
                mainC << "  {0,0,0,0,0,0,16,16,16}\n";
            }
            else
            {
                for (size_t i = 0; i < exportStatics.size(); ++i)
                {
                    const auto& s = exportStatics[i];
                    int px = (int)std::lround(s.x * kSaturnScale);
                    int py = (int)std::lround(-s.y * kSaturnScale);
                    int pz = (int)std::lround(s.z * kSaturnScale);
                    // Match StaticMesh rotation axis remap used by editor/runtime
                    int rx = (int)std::lround(s.rotZ);
                    int ry = (int)std::lround(s.rotX);
                    int rz = (int)std::lround(s.rotY);
                    int sx = (int)std::lround(std::max(0.25f, s.scaleX) * 16.0f);
                    int sy = (int)std::lround(std::max(0.25f, s.scaleY) * 16.0f);
                    int sz = (int)std::lround(std::max(0.25f, s.scaleZ) * 16.0f);
                    mainC << "  {" << px << "," << py << "," << pz << "," << rx << "," << ry << "," << rz << "," << sx << "," << sy << "," << sz << "}";
                    if (i + 1 < exportStatics.size()) mainC << ",";
                    mainC << "\n";
                }
            }
            mainC << "};\n";
            mainC << "static int gMeshTex[" << std::max(1, (int)exportStatics.size()) << "] = {";
            if (exportStatics.empty()) { mainC << "-1"; }
            else { for (size_t i = 0; i < exportStatics.size(); ++i) { if (i != 0) mainC << ","; mainC << geoTexSlot[i]; } }
            mainC << "};\n";
            mainC << "static int gGeoHasMesh[" << std::max(1, (int)exportStatics.size()) << "] = {";
            if (exportStatics.empty()) { mainC << "0"; }
            else { for (size_t i = 0; i < exportStatics.size(); ++i) { if (i != 0) mainC << ","; mainC << geoHasMesh[i]; } }
            mainC << "};\n";
            mainC << "static int gGeoPolyCount[" << std::max(1, (int)exportStatics.size()) << "] = {";
            if (exportStatics.empty()) { mainC << "0"; }
            else { for (size_t i = 0; i < exportStatics.size(); ++i) { if (i != 0) mainC << ","; mainC << geoPolyCount[i]; } }
            mainC << "};\n";
            mainC << "static int gGeoDataOffset[" << std::max(1, (int)exportStatics.size()) << "] = {";
            if (exportStatics.empty()) { mainC << "0"; }
            else { for (size_t i = 0; i < exportStatics.size(); ++i) { if (i != 0) mainC << ","; mainC << geoDataOffset[i]; } }
            mainC << "};\n";
            mainC << "static int gGeoVertData[" << std::max(12, (int)geoVertData.size()) << "] = {";
            if (geoVertData.empty()) { mainC << "0"; }
            else { for (size_t i = 0; i < geoVertData.size(); ++i) { if (i != 0) mainC << ","; mainC << geoVertData[i]; } }
            mainC << "};\n";
            mainC << "static int gGeoPolyTexSlot[" << std::max(1, (int)geoPolyTexSlot.size()) << "] = {";
            if (geoPolyTexSlot.empty()) { mainC << "-1"; }
            else { for (size_t i = 0; i < geoPolyTexSlot.size(); ++i) { if (i != 0) mainC << ","; mainC << geoPolyTexSlot[i]; } }
            mainC << "};\n";
            mainC << "static int gGeoPolyUvRepeatPow[" << std::max(1, (int)geoPolyUvRepeatPow.size()) << "] = {";
            if (geoPolyUvRepeatPow.empty()) { mainC << "0"; }
            else { for (size_t i = 0; i < geoPolyUvRepeatPow.size(); ++i) { if (i != 0) mainC << ","; mainC << geoPolyUvRepeatPow[i]; } }
            mainC << "};\n";
            mainC << "static void apply_uv_flip_preview(void) {\n";
            mainC << "  int i, p;\n";
            mainC << "  for (i = 0; i < gMeshCount; ++i) {\n";
            mainC << "    if (!gGeoHasMesh[i] || !gGeoMeshes[i] || gGeoPolyCount[i] <= 0) continue;\n";
            mainC << "    for (p = 0; p < gGeoPolyCount[i]; ++p) {\n";
            mainC << "      int di = (gGeoDataOffset[i] + p) * 12;\n";
            mainC << "      int o0 = 0, o1 = 1, o2 = 2, o3 = 3;\n";
            mainC << "      if (gFlipU) { int t0=o0, t2=o2; o0=o1; o1=t0; o2=o3; o3=t2; }\n";
            mainC << "      if (gFlipV) { int t0=o0, t1=o1; o0=o3; o1=o2; o2=t1; o3=t0; }\n";
            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshes[i], jo_int2fixed(gGeoVertData[di+o0*3+0]), jo_int2fixed(gGeoVertData[di+o0*3+1]), jo_int2fixed(gGeoVertData[di+o0*3+2]), p*4+0);\n";
            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshes[i], jo_int2fixed(gGeoVertData[di+o1*3+0]), jo_int2fixed(gGeoVertData[di+o1*3+1]), jo_int2fixed(gGeoVertData[di+o1*3+2]), p*4+1);\n";
            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshes[i], jo_int2fixed(gGeoVertData[di+o2*3+0]), jo_int2fixed(gGeoVertData[di+o2*3+1]), jo_int2fixed(gGeoVertData[di+o2*3+2]), p*4+2);\n";
            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshes[i], jo_int2fixed(gGeoVertData[di+o3*3+0]), jo_int2fixed(gGeoVertData[di+o3*3+1]), jo_int2fixed(gGeoVertData[di+o3*3+2]), p*4+3);\n";
            mainC << "      if (gGeoMeshesBack[i]) {\n";
            mainC << "        jo_3d_set_mesh_vertice(gGeoMeshesBack[i], jo_int2fixed(gGeoVertData[di+o0*3+0]), jo_int2fixed(gGeoVertData[di+o0*3+1]), jo_int2fixed(gGeoVertData[di+o0*3+2]), p*4+0);\n";
            mainC << "        jo_3d_set_mesh_vertice(gGeoMeshesBack[i], jo_int2fixed(gGeoVertData[di+o3*3+0]), jo_int2fixed(gGeoVertData[di+o3*3+1]), jo_int2fixed(gGeoVertData[di+o3*3+2]), p*4+1);\n";
            mainC << "        jo_3d_set_mesh_vertice(gGeoMeshesBack[i], jo_int2fixed(gGeoVertData[di+o2*3+0]), jo_int2fixed(gGeoVertData[di+o2*3+1]), jo_int2fixed(gGeoVertData[di+o2*3+2]), p*4+2);\n";
            mainC << "        jo_3d_set_mesh_vertice(gGeoMeshesBack[i], jo_int2fixed(gGeoVertData[di+o1*3+0]), jo_int2fixed(gGeoVertData[di+o1*3+1]), jo_int2fixed(gGeoVertData[di+o1*3+2]), p*4+3);\n";
            mainC << "      }\n";
            mainC << "    }\n";
            mainC << "  }\n";
            mainC << "}\n";
            mainC << "static void update_input(void) {\n";
            mainC << "  const int rotStep = 4;\n";
            mainC << "  int c = jo_is_pad1_key_down(JO_KEY_C);\n";
            mainC << "  {\n";
            mainC << "    const int strafeStep = 3;\n";
            mainC << "    int vx = gTgtX - gCamX;\n";
            mainC << "    int vz = gTgtZ - gCamZ;\n";
            mainC << "    int v2 = vx*vx + vz*vz;\n";
            mainC << "    int vd = jo_sqrt(v2);\n";
            mainC << "    int rx, rz;\n";
            mainC << "    if (vd < 1) vd = 1;\n";
            mainC << "    rx = (vz * strafeStep) / vd;\n";
            mainC << "    rz = (-vx * strafeStep) / vd;\n";
            mainC << "    if (jo_is_pad1_key_pressed(JO_KEY_LEFT)) { gCamX -= rx; gCamZ -= rz; gTgtX -= rx; gTgtZ -= rz; }\n";
            mainC << "    if (jo_is_pad1_key_pressed(JO_KEY_RIGHT)) { gCamX += rx; gCamZ += rz; gTgtX += rx; gTgtZ += rz; }\n";
            mainC << "  }\n";
            mainC << "  {\n";
            mainC << "    const int orbitStep = 3;\n";
            mainC << "    int dxo = gCamX - gTgtX;\n";
            mainC << "    int dzo = gCamZ - gTgtZ;\n";
            mainC << "    int d2o = dxo*dxo + dzo*dzo;\n";
            mainC << "    int do_ = jo_sqrt(d2o);\n";
            mainC << "    if (do_ < 1) do_ = 1;\n";
            mainC << "    if (jo_is_pad1_key_pressed(JO_KEY_L)) { gCamX += (dzo * orbitStep) / do_; gCamZ -= (dxo * orbitStep) / do_; }\n";
            mainC << "    if (jo_is_pad1_key_pressed(JO_KEY_R)) { gCamX -= (dzo * orbitStep) / do_; gCamZ += (dxo * orbitStep) / do_; }\n";
            mainC << "  }\n";
            mainC << "  if (jo_is_pad1_key_pressed(JO_KEY_B))     gTgtY += 4;\n";
            mainC << "  if (jo_is_pad1_key_pressed(JO_KEY_Y))     gTgtY -= 4;\n";
            mainC << "  {\n";
            mainC << "    const int zoomStep = 3;\n";
            mainC << "    int dx = gCamX - gTgtX;\n";
            mainC << "    int dy = gCamY - gTgtY;\n";
            mainC << "    int dz = gCamZ - gTgtZ;\n";
            mainC << "    int d2 = dx*dx + dy*dy + dz*dz;\n";
            mainC << "    int d = jo_sqrt(d2);\n";
            mainC << "    if (d < 1) d = 1;\n";
            mainC << "    if (jo_is_pad1_key_pressed(JO_KEY_UP)) { gCamX -= (dx * zoomStep) / d; gCamY -= (dy * zoomStep) / d; gCamZ -= (dz * zoomStep) / d; }\n";
            mainC << "    if (jo_is_pad1_key_pressed(JO_KEY_DOWN)) { gCamX += (dx * zoomStep) / d; gCamY += (dy * zoomStep) / d; gCamZ += (dz * zoomStep) / d; }\n";
            mainC << "  }\n";
            mainC << "  if (c && !gPrevC) { gFlipU = 0; gFlipV = 0; }\n";
            mainC << "  gPrevC = c;\n";
            mainC << "}\n";
            mainC << "static void apply_near_clip_assist(void) {\n";
            mainC << "  if (!gNearClipAssist) return;\n";
            mainC << "  const int minDist = 180;\n";
            mainC << "  int dx = gTgtX - gCamX;\n";
            mainC << "  int dy = gTgtY - gCamY;\n";
            mainC << "  int dz = gTgtZ - gCamZ;\n";
            mainC << "  int d2 = dx*dx + dy*dy + dz*dz;\n";
            mainC << "  int min2 = minDist * minDist;\n";
            mainC << "  if (d2 > 0 && d2 < min2) {\n";
            mainC << "    int d = jo_sqrt(d2);\n";
            mainC << "    if (d < 1) d = 1;\n";
            mainC << "    gCamX = gTgtX - (dx * minDist) / d;\n";
            mainC << "    gCamY = gTgtY - (dy * minDist) / d;\n";
            mainC << "    gCamZ = gTgtZ - (dz * minDist) / d;\n";
            mainC << "  }\n";
            mainC << "}\n";
            mainC << "static void init_stars(void) {\n";
            mainC << "  int i;\n";
            mainC << "  if (gStarInited) return;\n";
            mainC << "  gStarInited = 1;\n";
            mainC << "  for (i = 0; i < gStarCount; ++i) {\n";
            mainC << "    int a = (i * 73) & 1023;\n";
            mainC << "    int b = (i * 151 + 97) & 1023;\n";
            mainC << "    int r = 900 + (i % 5) * 180;\n";
            mainC << "    int sx = (jo_cos(a) * r) >> 16;\n";
            mainC << "    int sy = (jo_sin(b) * (r / 2)) >> 16;\n";
            mainC << "    int sz = (jo_sin(a) * r) >> 16;\n";
            mainC << "    gStarX[i] = gTgtX + sx;\n";
            mainC << "    gStarY[i] = gTgtY + sy;\n";
            mainC << "    gStarZ[i] = gTgtZ + sz;\n";
            mainC << "    gStarBucket[i] = i % 3;\n";
            mainC << "  }\n";
            mainC << "}\n";
            mainC << "static void draw_stars(void) {\n";
            mainC << "  int i;\n";
            mainC << "  if (gDbgSprite < 0) return;\n";
            mainC << "  for (i = 0; i < gStarCount; ++i) {\n";
            mainC << "    if (gStarBucket[i] == 0) jo_sprite_change_sprite_scale(0.25f);\n";
            mainC << "    else if (gStarBucket[i] == 1) jo_sprite_change_sprite_scale(0.40f);\n";
            mainC << "    else jo_sprite_change_sprite_scale(0.60f);\n";
            mainC << "    jo_sprite_draw3D(gDbgSprite, gStarX[i], gStarY[i], gStarZ[i]);\n";
            mainC << "  }\n";
            mainC << "  jo_sprite_restore_sprite_scale();\n";
            mainC << "}\n";
            mainC << "static void draw(void) {\n";
            mainC << "  int i;\n";
            mainC << "  update_input();\n";
            mainC << "  apply_uv_flip_preview();\n";
            mainC << "  /* near-clip assist removed per user request */\n";
            mainC << "  jo_3d_camera_set_viewpoint(&cam, gCamX, gCamY, gCamZ);\n";
            mainC << "  jo_3d_camera_set_target(&cam, gTgtX, gTgtY, gTgtZ);\n";
            mainC << "  jo_3d_camera_look_at(&cam);\n";
            mainC << "  int drawCount = gMeshCount;\n";
            mainC << "  int totalPolys = 0, submittedPolys = 0;\n";
            mainC << "  const int polyBudget = 4096;\n";
            mainC << "  int order[" << std::max(1, (int)exportStatics.size()) << "], dist2[" << std::max(1, (int)exportStatics.size()) << "], oi;\n";
            mainC << "  /* draw limit disabled: always include full mesh list */\n";
            mainC << "  for (i = 0; i < drawCount; ++i) {\n";
            mainC << "    int dx = gMeshes[i].x - gCamX;\n";
            mainC << "    int dy = gMeshes[i].y - gCamY;\n";
            mainC << "    int dz = gMeshes[i].z - gCamZ;\n";
            mainC << "    order[i] = i;\n";
            mainC << "    dist2[i] = dx*dx + dy*dy + dz*dz;\n";
            mainC << "    totalPolys += (gGeoHasMesh[i] && gGeoPolyCount[i] > 0) ? gGeoPolyCount[i] : 6;\n";
            mainC << "  }\n";
            mainC << "  /* strict index order draw: no distance sorting */\n";
            mainC << "  /* solo mesh toggle removed: always draw full list */\n";
            mainC << "  for (oi = 0; oi < drawCount; ++oi) {\n";
            mainC << "    int mi = order[oi];\n";
            mainC << "    int s, sid = 0;\n";
            mainC << "    int meshPolys = (gGeoHasMesh[mi] && gGeoPolyCount[mi] > 0) ? gGeoPolyCount[mi] : 6;\n";
            mainC << "    if (submittedPolys + meshPolys > polyBudget) continue;\n";
            mainC << "    if (gMeshTex[mi] >= 0 && gMeshTex[mi] < 64 && gTexSlots[gMeshTex[mi]] >= 0) sid = gTexSlots[gMeshTex[mi]];\n";
            mainC << "    jo_3d_push_matrix();\n";
            mainC << "    jo_3d_translate_matrix(gMeshes[mi].x, gMeshes[mi].y, gMeshes[mi].z);\n";
            mainC << "    jo_3d_rotate_matrix(gMeshes[mi].rx + gInputRotX, gMeshes[mi].ry + gInputRotY, gMeshes[mi].rz);\n";
            mainC << "    if (gGeoHasMesh[mi] && gGeoMeshes[mi]) {\n";
            mainC << "      jo_3d_mesh_draw(gGeoMeshes[mi]);\n";
            mainC << "      if (gNoCull && gGeoMeshesBack[mi]) jo_3d_mesh_draw(gGeoMeshesBack[mi]);\n";
            mainC << "    } else {\n";
            mainC << "      for (s = 0; s < 6; ++s) jo_3d_set_texture(&cube_quads[s], sid);\n";
            mainC << "      jo_3d_draw_array(cube_quads, 6);\n";
            mainC << "    }\n";
            mainC << "    jo_3d_pop_matrix();\n";
            mainC << "    submittedPolys += meshPolys;\n";
            mainC << "  }\n";
            mainC << "  /* star pass removed */\n";
            mainC << "  jo_printf(1, 1, \"Nebula Dreamcast S3\");\n";
            mainC << "  jo_printf(1, 2, \"Meshes: " << (int)exportStatics.size() << " Cam: " << (int)exportCameras.size() << "\");\n";
            mainC << "  jo_printf(1, 3, \"TexSlots: " << (int)slotFileName.size() << "\");\n";
            mainC << "  jo_printf(1, 4, \"DPad L/R:Strafe  Shoulder L/R:Orbit  B/Y:LookUpDn\");\n";
            mainC << "  jo_printf(1, 5, \"C:Reset UV\");\n";
            mainC << "  jo_printf(1, 6, \"AllocFail:%d Last:%d Draw:%d\", gMeshCreateFailCount, gMeshCreateLastFail, drawCount);\n";
            mainC << "  jo_printf(1, 7, \"Polys T:%d S:%d B:%d\", totalPolys, submittedPolys, polyBudget);\n";
            mainC << "  if (gMeshCount > 0) jo_printf(1, 8, \"M0:%d P0:%d M1:%d P1:%d\", gMeshCreateOk[0], gGeoPolyCount[0], (gMeshCount>1?gMeshCreateOk[1]:-1), (gMeshCount>1?gGeoPolyCount[1]:-1));\n";
            mainC << "  if (gMeshCount > 2) jo_printf(1, 9, \"M2:%d P2:%d M3:%d P3:%d\", gMeshCreateOk[2], gGeoPolyCount[2], (gMeshCount>3?gMeshCreateOk[3]:-1), (gMeshCount>3?gGeoPolyCount[3]:-1));\n";
            mainC << "  if (gMeshCount > 4) jo_printf(1, 10, \"M4:%d P4:%d\", gMeshCreateOk[4], gGeoPolyCount[4]);\n";
            mainC << "  if (drawCount > 0) jo_printf(1, 11, \"Order:%d %d %d %d %d\", order[0], (drawCount>1?order[1]:-1), (drawCount>2?order[2]:-1), (drawCount>3?order[3]:-1), (drawCount>4?order[4]:-1));\n";
            mainC << "  jo_printf(1, 12, \"GEN:e0dfaa0+\");\n";
            mainC << "  if (gDbgSprite >= 0) jo_sprite_draw3D(gDbgSprite, -140, 90, 320);\n";
            mainC << "}\n";
            mainC << "void jo_main(void) {\n";
            mainC << "  int i;\n";
            mainC << "  jo_core_init(JO_COLOR_Black);\n";
            mainC << "  jo_3d_camera_init(&cam);\n";
            mainC << "  jo_3d_camera_set_viewpoint(&cam, " << camX << ", " << camY << ", " << camZ << ");\n";
            mainC << "  jo_3d_camera_set_target(&cam, " << tgtX << ", " << tgtY << ", " << tgtZ << ");\n";
            mainC << "  jo_3d_create_cube(cube_quads, cube_vertices);\n";
            mainC << "  gTexSlots[0] = jo_sprite_add_tga(JO_ROOT_DIR, \"BOX.TGA\", JO_COLOR_Transparent);\n";
            for (size_t ti = 0; ti < slotFileName.size(); ++ti)
            {
                mainC << "  gTexSlots[" << ti << "] = jo_sprite_add_tga(JO_ROOT_DIR, \"" << slotFileName[ti] << "\", JO_COLOR_Transparent);\n";
            }
            mainC << "  gDbgSprite = gTexSlots[0];\n";
            mainC << "  /* stars init removed */\n";
            mainC << "  for (i = 0; i < gMeshCount; ++i) {\n";
            mainC << "    int p, sid = 0;\n";
            mainC << "    if (!gGeoHasMesh[i] || gGeoPolyCount[i] <= 0) continue;\n";
            mainC << "    if (gMeshTex[i] >= 0 && gMeshTex[i] < 64 && gTexSlots[gMeshTex[i]] >= 0) sid = gTexSlots[gMeshTex[i]];\n";
            mainC << "    gGeoMeshes[i] = jo_3d_create_mesh(gGeoPolyCount[i]);\n";
            mainC << "    gGeoMeshesBack[i] = jo_3d_create_mesh(gGeoPolyCount[i]);\n";
            mainC << "    gMeshCreateOk[i] = (gGeoMeshes[i] != JO_NULL && gGeoMeshesBack[i] != JO_NULL) ? 1 : 0;\n";
            mainC << "    if (!gMeshCreateOk[i]) { ++gMeshCreateFailCount; gMeshCreateLastFail = i; continue; }\n";
            mainC << "    for (p = 0; p < gGeoPolyCount[i]; ++p) {\n";
            mainC << "      int di = (gGeoDataOffset[i] + p) * 12;\n";
            mainC << "      int ts = gGeoPolyTexSlot[gGeoDataOffset[i] + p];\n";
            mainC << "      if (ts >= 0 && ts < 64 && gTexSlots[ts] >= 0) sid = gTexSlots[ts];\n";
            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshes[i], jo_int2fixed(gGeoVertData[di+0]), jo_int2fixed(gGeoVertData[di+1]), jo_int2fixed(gGeoVertData[di+2]), p*4+0);\n";
            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshes[i], jo_int2fixed(gGeoVertData[di+3]), jo_int2fixed(gGeoVertData[di+4]), jo_int2fixed(gGeoVertData[di+5]), p*4+1);\n";
            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshes[i], jo_int2fixed(gGeoVertData[di+6]), jo_int2fixed(gGeoVertData[di+7]), jo_int2fixed(gGeoVertData[di+8]), p*4+2);\n";
            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshes[i], jo_int2fixed(gGeoVertData[di+9]), jo_int2fixed(gGeoVertData[di+10]), jo_int2fixed(gGeoVertData[di+11]), p*4+3);\n";
            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshesBack[i], jo_int2fixed(gGeoVertData[di+0]), jo_int2fixed(gGeoVertData[di+1]), jo_int2fixed(gGeoVertData[di+2]), p*4+0);\n";
            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshesBack[i], jo_int2fixed(gGeoVertData[di+9]), jo_int2fixed(gGeoVertData[di+10]), jo_int2fixed(gGeoVertData[di+11]), p*4+1);\n";
            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshesBack[i], jo_int2fixed(gGeoVertData[di+6]), jo_int2fixed(gGeoVertData[di+7]), jo_int2fixed(gGeoVertData[di+8]), p*4+2);\n";
            mainC << "      jo_3d_set_mesh_vertice(gGeoMeshesBack[i], jo_int2fixed(gGeoVertData[di+3]), jo_int2fixed(gGeoVertData[di+4]), jo_int2fixed(gGeoVertData[di+5]), p*4+3);\n";
            mainC << "      if (sid >= 0) { jo_3d_set_mesh_polygon_texture(gGeoMeshes[i], sid, p); jo_3d_set_mesh_polygon_texture(gGeoMeshesBack[i], sid, p); }\n";
            mainC << "      else { jo_3d_set_mesh_polygon_color(gGeoMeshes[i], JO_COLOR_Green, p); jo_3d_set_mesh_polygon_color(gGeoMeshesBack[i], JO_COLOR_Green, p); }\n";
            mainC << "    }\n";
            mainC << "  }\n";
            mainC << "  for (i = 0; i < 6; ++i) jo_3d_set_texture(&cube_quads[i], 0);\n";
            mainC << "  jo_core_add_callback(draw);\n";
            mainC << "  jo_core_run();\n";
            mainC << "}\n";
        }
    }

    // Build via JO engine toolchain to produce game.iso + game.cue in project/build_saturn.
    std::filesystem::path joRoot;
    {
        std::filesystem::path cwd = std::filesystem::current_path();
        std::vector<std::filesystem::path> roots = {
            cwd / "thirdparty" / "JO_engine",
            cwd / "thirdparty" / "joengine",
            cwd / ".." / "thirdparty" / "JO_engine",
            cwd / ".." / "thirdparty" / "joengine",
            cwd / ".." / ".." / "thirdparty" / "JO_engine",
            cwd / ".." / ".." / "thirdparty" / "joengine",
            cwd / ".." / ".." / ".." / "JO_engine",
            cwd / ".." / ".." / ".." / "joengine",
            std::filesystem::path("C:/Users/NoSig/Documents/SaturnDev/JO_engine")
        };
        for (const auto& r : roots)
        {
            std::filesystem::path mk = r / "Compiler" / "COMMON" / "jo_engine_makefile";
            std::filesystem::path makeExe = r / "Compiler" / "WINDOWS" / "Other Utilities" / "make.exe";
            if (std::filesystem::exists(mk) && std::filesystem::exists(makeExe))
            {
                joRoot = r;
                break;
            }
        }
    }

    if (joRoot.empty())
    {
        gViewportToast = "JO_engine toolchain not found";
        gViewportToastUntil = glfwGetTime() + 3.0;
    }
    else
    {
        std::filesystem::path compilerDir = joRoot / "Compiler";
        std::string joEngineSrc = (joRoot / "jo_engine").string();
        std::string compDir = compilerDir.string();
        std::replace(joEngineSrc.begin(), joEngineSrc.end(), '\\', '/');
        std::replace(compDir.begin(), compDir.end(), '\\', '/');

        {
            std::ofstream mk(buildDir / "makefile", std::ios::out | std::ios::trunc);
            if (mk.is_open())
            {
                mk << "JO_COMPILE_WITH_VIDEO_MODULE = 0\n";
                mk << "JO_COMPILE_WITH_BACKUP_MODULE = 0\n";
                mk << "JO_COMPILE_WITH_TGA_MODULE = 1\n";
                mk << "JO_COMPILE_WITH_AUDIO_MODULE = 0\n";
                mk << "JO_COMPILE_WITH_3D_MODULE = 1\n";
                mk << "JO_COMPILE_WITH_PSEUDO_MODE7_MODULE = 0\n";
                mk << "JO_COMPILE_WITH_EFFECTS_MODULE = 0\n";
                mk << "JO_DEBUG = 1\n";
                mk << "SRCS=main.c\n";
                mk << "JO_ENGINE_SRC_DIR=" << joEngineSrc << "\n";
                mk << "COMPILER_DIR=" << compDir << "\n";
                mk << "include $(COMPILER_DIR)/COMMON/jo_engine_makefile\n";
            }
        }

        std::string buildDirStr = buildDir.string();
        std::string compilerWin = compilerDir.string();
        std::filesystem::path logPath = buildDir / "package.log";
        std::string logPathStr = logPath.string();
        {
            std::ofstream logOut(logPath, std::ios::out | std::ios::trunc);
            if (logOut.is_open())
            {
                logOut << "[Package] buildDir=" << buildDir.string() << "\n";
                logOut << "[Package] joRoot=" << joRoot.string() << "\n";
                logOut << "[Package] compilerDir=" << compilerDir.string() << "\n";
                if (!texWarnings.empty())
                {
                    for (const auto& w : texWarnings)
                        logOut << w << "\n";
                }
            }
        }
        std::filesystem::path makeExe = compilerDir / "WINDOWS" / "Other Utilities" / "make.exe";
        std::string makeExeStr = makeExe.string();

        int buildRc = -1;
        if (!std::filesystem::exists(makeExe))
        {
            printf("[Package] make.exe not found: %s\n", makeExeStr.c_str());
            {
                std::ofstream logOut(logPath, std::ios::out | std::ios::app);
                if (logOut.is_open())
                    logOut << "[Error] make.exe not found: " << makeExeStr << "\n";
            }
            gViewportToast = "Dreamcast build failed (make.exe missing)";
            gViewportToastUntil = glfwGetTime() + 3.0;
        }
        else
        {
            std::string cmd =
                "cmd /c \"set \"PATH=" + compilerWin + "\\WINDOWS\\bin;" + compilerWin + "\\WINDOWS\\Other Utilities;%PATH%\" && \"" + makeExeStr + "\" -C \"" + buildDirStr + "\" clean game.raw >> \"" + logPathStr + "\" 2>&1\"";
            {
                std::ofstream logOut(logPath, std::ios::out | std::ios::app);
                if (logOut.is_open())
                    logOut << "[Package] cmd=" << cmd << "\n";
            }
            buildRc = NebulaDreamcastBuild::RunCommand(cmd.c_str());
        }

        std::filesystem::path cuePath;
        if (buildRc != 0)
        {
            printf("[Package] Dreamcast build command failed (code=%d). Log: %s\n", buildRc, logPathStr.c_str());
            gViewportToast = "Dreamcast build failed (see build_saturn/package.log)";
        }
        else
        {
            // Build the disc image explicitly (jo makefile iso target is fragile on some Windows setups).
            std::filesystem::path rawPath = buildDir / "game.raw";
            std::filesystem::path cd0Path = cdDir / "0.bin";
            std::filesystem::path isoPath = buildDir / "game.iso";
            std::filesystem::path mkisofsExe = compilerDir / "WINDOWS" / "Other Utilities" / "mkisofs.exe";
            std::filesystem::path ipBin = compilerDir / "COMMON" / "IP.BIN";

            if (!std::filesystem::exists(rawPath))
            {
                std::ofstream logOut(logPath, std::ios::out | std::ios::app);
                if (logOut.is_open()) logOut << "[Error] Missing game.raw after build\n";
                gViewportToast = "Dreamcast build failed (missing game.raw)";
            }
            else if (!std::filesystem::exists(mkisofsExe) || !std::filesystem::exists(ipBin))
            {
                std::ofstream logOut(logPath, std::ios::out | std::ios::app);
                if (logOut.is_open()) logOut << "[Error] Missing mkisofs.exe or IP.BIN\n";
                gViewportToast = "Dreamcast build failed (mkisofs/IP.BIN missing)";
            }
            else
            {
                std::error_code fsec;
                std::filesystem::copy_file(rawPath, cd0Path, std::filesystem::copy_options::overwrite_existing, fsec);

                std::string mkCmd =
                    "cmd /c \"cd /d \"" + buildDir.string() +
                    "\" && \"" + mkisofsExe.string() +
                    "\" -quiet -sysid \"SEGA SATURN\" -volid \"SaturnApp\" -volset \"SaturnApp\" -sectype 2352" +
                    " -publisher \"SEGA ENTERPRISES, LTD.\" -preparer \"SEGA ENTERPRISES, LTD.\" -appid \"SaturnApp\"" +
                    " -abstract \"./cd/ABS.TXT\" -copyright \"./cd/CPY.TXT\" -biblio \"./cd/BIB.TXT\"" +
                    " -generic-boot \"" + ipBin.string() + "\"" +
                    " -full-iso9660-filenames -o \"./game.iso\" ./cd >> \"" + logPathStr + "\" 2>&1\"";

                {
                    std::ofstream logOut(logPath, std::ios::out | std::ios::app);
                    if (logOut.is_open()) logOut << "[Package] mkisofs_cmd=" << mkCmd << "\n";
                }

                int mkRc = NebulaDreamcastBuild::RunCommand(mkCmd.c_str());
                if (mkRc == 0 && std::filesystem::exists(isoPath))
                {
                    cuePath = buildDir / "game.cue";
                    std::ofstream cue(cuePath, std::ios::out | std::ios::trunc);
                    if (cue.is_open())
                    {
                        cue << "FILE \"game.iso\" BINARY\n";
                        cue << "  TRACK 01 MODE1/2048\n";
                        cue << "    INDEX 01 00:00:00\n";
                    }
                }
            }

            if (!cuePath.empty() && std::filesystem::exists(cuePath))
            {
                printf("[Package] CUE ready: %s\n", cuePath.string().c_str());
                gViewportToast = "Dreamcast package ready: " + cuePath.filename().string();
            }
            else
            {
                printf("[Package] Could not generate CUE in %s\n", buildDirStr.c_str());
                gViewportToast = "Dreamcast package failed (see package.log)";
            }
        }
        gViewportToastUntil = glfwGetTime() + 3.0;
    }
    }
}
}
