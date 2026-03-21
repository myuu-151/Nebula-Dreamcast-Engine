#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"

#include <string>
#include <vector>
#include <filesystem>

#define NOMINMAX
#include <Windows.h>

#include <GLFW/glfw3.h>

#include "editor/prefs.h"
#include "editor/project.h"
#include "editor/viewport_nav.h"
#include "editor/frame_loop.h"
#include "io/texture_io.h"

int main(int, char**)
{
    if (!glfwInit())
        return 1;

    EditorViewportNav nav;
    EditorFrameContext ctx;

    LoadPreferences(ctx.uiScale, ctx.themeMode);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Nebula", nullptr, nullptr);
    if (window)
    {
        UINT iw = 0, ih = 0;
        std::vector<unsigned char> icoBGRA;
        std::filesystem::path iconPath = ResolveEditorAssetPath("assets/nebula_logo.ico");
        std::wstring iconW = iconPath.wstring();
        if (!iconW.empty() && LoadImageWIC(iconW.c_str(), iw, ih, icoBGRA))
        {
            std::vector<unsigned char> icoRGBA(icoBGRA.size());
            for (size_t i = 0; i + 3 < icoBGRA.size(); i += 4)
            {
                icoRGBA[i + 0] = icoBGRA[i + 2];
                icoRGBA[i + 1] = icoBGRA[i + 1];
                icoRGBA[i + 2] = icoBGRA[i + 0];
                icoRGBA[i + 3] = icoBGRA[i + 3];
            }
            GLFWimage img;
            img.width = (int)iw;
            img.height = (int)ih;
            img.pixels = icoRGBA.data();
            glfwSetWindowIcon(window, 1, &img);
        }
    }
    if (!window)
        return 1;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glfwSetWindowUserPointer(window, &nav);
    InstallViewportScrollCallback(window);
    InstallDropCallback(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    ImGui::GetIO().FontGlobalScale = ctx.uiScale;

    std::filesystem::path uiIconPath = ResolveEditorAssetPath("assets/nebula_logo.ico");
    if (!uiIconPath.empty())
    {
        std::wstring w = uiIconPath.wstring();
        ctx.uiIconTex = LoadTextureWIC(w.c_str());
    }

    ctx.lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(window))
        TickEditorFrame(window, nav, ctx);

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
