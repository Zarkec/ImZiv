#include "app/app.hpp"

#include "viewer/image_canvas.hpp"
#include "core/content_registry.hpp"
#include "core/task_manager.hpp"
#include "core/view_tools.hpp"
#include "tools/color_picker_tool.hpp"
#include "tools/angle_measurement_tool.hpp"
#include "tools/measurement_tool.hpp"
#include "platform/native_dialog.hpp"
#include "platform/platform_window.hpp"
#include "app/ui.hpp"
#include "viewer/view_image_editor.hpp"
#include "workflow/view_image_workflow.hpp"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imnodes.h"
#include <GLFW/glfw3.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
    const char* GlslVersion = "#version 130";

    NativeDropData g_dropData;
    bool g_renderingFrame = false;
    bool g_contentRegistered = false;
    bool g_defaultDockLayoutBuilt = false;
    ImGuiID g_mainDockId = 0;
    ImGuiID g_workflowPreviewDockId = 0;
    bool g_workflowPreviewDockApplied = false;

#ifdef _WIN32
    std::string wideToUtf8(const wchar_t* text) {
        if (text == nullptr || text[0] == L'\0')
            return {};

        int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        if (length <= 0)
            return {};

        std::string result(length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
        result.pop_back();
        return result;
    }
#endif

    std::string startupImagePath(int argc, char** argv) {
#ifdef _WIN32
        int wideArgc = 0;
        LPWSTR* wideArgv = CommandLineToArgvW(GetCommandLineW(), &wideArgc);
        std::string path;
        if (wideArgv != nullptr) {
            if (wideArgc > 1)
                path = wideToUtf8(wideArgv[1]);
            LocalFree(wideArgv);
        }
        return path;
#else
        return argc > 1 ? argv[1] : "";
#endif
    }

    void onDrop(GLFWwindow*, int count, const char** paths) {
        if (count > 0) {
            g_dropData.value = paths[0];
            g_dropData.bytes.clear();
        }
    }

    void onNativeDrop(NativeDropData data) {
        g_dropData = std::move(data);
    }

    void saveImageFromDialog(GLFWwindow* window, const ImageCanvas& canvas) {
        auto path = saveFileDialog(window, canvas.fileName);
        if (!path.empty())
            canvas.saveAs(path);
    }

    void registerBuiltinContent(GLFWwindow* window, ImageCanvas& canvas) {
        if (g_contentRegistered)
            return;

        hex::ContentRegistry::Views::add<ViewImageEditor>(window, canvas);
        hex::ContentRegistry::Views::add<ViewImageWorkflow>(window, canvas);
        registerMeasurementTool(canvas);
        registerAngleMeasurementTool(canvas);
        registerColorPickerTool(canvas);
        hex::ContentRegistry::Views::add<hex::plugin::builtin::ViewTools>();
        g_contentRegistered = true;
    }

    double refreshRateForWindow(GLFWwindow* window) {
        GLFWmonitor* monitor = glfwGetWindowMonitor(window);
        if (monitor == nullptr) {
            int wx = 0, wy = 0, ww = 0, wh = 0;
            glfwGetWindowPos(window, &wx, &wy);
            glfwGetWindowSize(window, &ww, &wh);

            int monitorCount = 0;
            GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
            int bestArea = -1;
            for (int i = 0; i < monitorCount; ++i) {
                int mx = 0, my = 0, mw = 0, mh = 0;
                glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);

                int overlapW = (std::max)(0, (std::min)(wx + ww, mx + mw) - (std::max)(wx, mx));
                int overlapH = (std::max)(0, (std::min)(wy + wh, my + mh) - (std::max)(wy, my));
                int area = overlapW * overlapH;
                if (area > bestArea) {
                    bestArea = area;
                    monitor = monitors[i];
                }
            }
        }

        const GLFWvidmode* mode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
        return mode != nullptr && mode->refreshRate > 0 ? double(mode->refreshRate) : 60.0;
    }

    bool shouldKeepHighFrameRate(const ImGuiIO& io) {
        return io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f ||
            io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f ||
            ImGui::IsAnyMouseDown() || ImGui::IsAnyItemActive() ||
            io.WantTextInput || io.KeyCtrl || io.KeyShift || io.KeyAlt || io.KeySuper;
    }

    void waitForFrameBudget(double frameStart, double targetFps) {
        if (targetFps <= 0.0)
            return;

        double targetFrameTime = 1.0 / targetFps;
        double elapsed = glfwGetTime() - frameStart;
        double remaining = targetFrameTime - elapsed;
        if (remaining > 0.0)
            glfwWaitEventsTimeout(remaining);
    }

    void buildDefaultDockLayout(ImGuiID dockspaceId, const ImVec2& dockspaceSize) {
        if (g_defaultDockLayoutBuilt || dockspaceSize.x <= 0.0f || dockspaceSize.y <= 0.0f)
            return;

        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, dockspaceSize);

        ImGuiID dockMain = dockspaceId;
        ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.26f, nullptr, &dockMain);
        ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.34f, nullptr, &dockMain);
        g_mainDockId = dockMain;
        g_workflowPreviewDockId = 0;
        g_workflowPreviewDockApplied = false;

        for (const auto& entry : hex::ContentRegistry::Views::impl::getEntries()) {
            hex::View* view = entry.second.get();
            ImGuiID target = dockMain;
            if (view->getUnlocalizedName().get() == "imziv.view.tools")
                target = dockRight;
            else if (view->getUnlocalizedName().get() == "imziv.view.image_workflow")
                target = dockBottom;
            ImGui::DockBuilderDockWindow(view->getWindowTitle().c_str(), target);
        }

        ImGui::DockBuilderFinish(dockspaceId);
        g_defaultDockLayoutBuilt = true;
    }

    ImGuiID ensureWorkflowPreviewDock() {
        if (g_mainDockId == 0)
            return 0;
        if (g_workflowPreviewDockId != 0 && ImGui::DockBuilderGetNode(g_workflowPreviewDockId) != nullptr)
            return g_workflowPreviewDockId;

        ImGuiID previewDockId = 0;
        ImGuiID imageDockId = 0;
        ImGui::DockBuilderSplitNode(g_mainDockId, ImGuiDir_Right, 0.5f, &previewDockId, &imageDockId);
        if (imageDockId != 0)
            g_mainDockId = imageDockId;
        g_workflowPreviewDockId = previewDockId;
        g_workflowPreviewDockApplied = false;
        return g_workflowPreviewDockId;
    }

    void renderDockSpace() {
        ImGuiID dockspaceId = ImGui::GetID("ImZivDockSpace");
        const ImVec2 dockspaceSize = ImGui::GetContentRegionAvail();
        buildDefaultDockLayout(dockspaceId, dockspaceSize);
        ImGui::DockSpace(dockspaceId, dockspaceSize, ImGuiDockNodeFlags_None);
    }

    void renderRegisteredViews() {
        auto& views = hex::ContentRegistry::Views::impl::getEntries();
        ImGuiWindowClass windowClass = {};
        windowClass.DockNodeFlagsOverrideSet |= ImGuiDockNodeFlags_NoCloseButton;
        windowClass.DockNodeFlagsOverrideSet |= ImGuiDockNodeFlags_NoWindowMenuButton;
        std::vector<hex::UnlocalizedString> closedPreviewViews;
        for (auto iter = views.rbegin(); iter != views.rend(); ++iter) {
            hex::View* view = iter->second.get();
            view->trackViewState();
            view->drawAlwaysVisibleContent();
            if (view->shouldProcess()) {
                const std::string viewId = view->getUnlocalizedName().get();
                if (viewId == "工作流预览") {
                    const ImGuiID previewDockId = ensureWorkflowPreviewDock();
                    if (previewDockId != 0) {
                        ImGui::SetNextWindowDockID(previewDockId, g_workflowPreviewDockApplied ? ImGuiCond_Appearing : ImGuiCond_Always);
                        g_workflowPreviewDockApplied = true;
                    }
                } else if (g_mainDockId != 0 && viewId.rfind("预览图像 ", 0) == 0) {
                    ImGui::SetNextWindowDockID(g_mainDockId, ImGuiCond_Appearing);
                }
                ImGui::SetNextWindowClass(&windowClass);
                view->draw();
            }
            if (!view->getWindowOpenState() && view->getUnlocalizedName().get().rfind("预览图像 ", 0) == 0)
                closedPreviewViews.push_back(view->getUnlocalizedName());
        }

        for (const auto& name : closedPreviewViews) {
            hex::ContentRegistry::Views::impl::remove(name);
        }
    }

    void renderFrame(GLFWwindow* window, ImageCanvas& canvas, bool handleShortcuts) {
        ImGuiContext* ctx = ImGui::GetCurrentContext();
        if (ctx == nullptr || ctx->WithinFrameScope || g_renderingFrame)
            return;

        g_renderingFrame = true;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        applyBorderlessWindowStyle(window);
        applyWindowSizeLimits(window);

        ImGuiIO& io = ImGui::GetIO();

        if (handleShortcuts) {
            bool wantInput = io.WantTextInput;
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O) && !wantInput) {
                auto path = openFileDialog(window);
                if (!path.empty()) canvas.openFile(path);
            }
            if (canvas.hasImage() && !wantInput) {
                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
                    saveImageFromDialog(window, canvas);
                if (ImGui::IsKeyPressed(ImGuiKey_F))
                    canvas.fitToView();
                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_1))
                    canvas.setActualSize();
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) && !io.KeyCtrl &&
                    canvas.currentIndex > 0) {
                    canvas.currentIndex--;
                    canvas.openFile(canvas.imageFiles[canvas.currentIndex], false);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) && !io.KeyCtrl &&
                    canvas.currentIndex < (int)canvas.imageFiles.size() - 1) {
                    canvas.currentIndex++;
                    canvas.openFile(canvas.imageFiles[canvas.currentIndex], false);
                }
            }
        }

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##Main", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_MenuBar);

        renderMenu(window, canvas);
        renderDockSpace();

        ImGui::End();
        ImGui::PopStyleVar();

        renderRegisteredViews();

        ImGui::Render();
        int displayW = 0;
        int displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);

        g_renderingFrame = false;
    }

    void onFramebufferSize(GLFWwindow* window, int width, int height) {
        glViewport(0, 0, width, height);
        invalidateWindow(window);
    }

    void onWindowRefresh(GLFWwindow* window) {
        auto* canvas = static_cast<ImageCanvas*>(glfwGetWindowUserPointer(window));
        if (canvas == nullptr)
            return;

        renderFrame(window, *canvas, false);
        flushWindowFrame();
    }
}

int runApp(int argc, char** argv) {
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
#ifdef _WIN32
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 720, "ImZiv", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    setWindowIcon(window);
    applyWindowSizeLimits(window);
    centerWindowOnPrimaryMonitor(window);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetDropCallback(window, onDrop);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImNodes::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 2.0f;
    loadUiFonts();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(GlslVersion);

    setupBorderlessWindow(window);
    setupNativeDropTarget(window, onNativeDrop);
    hex::TaskManager::init();

    ImageCanvas canvas(window);
    registerBuiltinContent(window, canvas);
    glfwSetWindowUserPointer(window, &canvas);
    glfwSetFramebufferSizeCallback(window, onFramebufferSize);
    glfwSetWindowRefreshCallback(window, onWindowRefresh);

    auto initialPath = startupImagePath(argc, argv);

    constexpr double IdleFps = 5.0;
    constexpr double ActiveFrameRateDuration = 1.0;
    double highFrameRateUntil = glfwGetTime() + ActiveFrameRateDuration;

    while (!glfwWindowShouldClose(window)) {
        double frameStart = glfwGetTime();
        glfwPollEvents();
        hex::TaskManager::runDeferredCalls();
        hex::TaskManager::collectGarbage();

        if (!glfwGetWindowAttrib(window, GLFW_VISIBLE) || glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
            glfwWaitEventsTimeout(0.1);
            continue;
        }

        if (!initialPath.empty() && g_defaultDockLayoutBuilt) {
            canvas.openFile(initialPath);
            initialPath.clear();
            highFrameRateUntil = glfwGetTime() + ActiveFrameRateDuration;
        }

        if (!g_dropData.value.empty() || !g_dropData.bytes.empty()) {
            canvas.loadDropped(g_dropData.value, g_dropData.bytes);
            g_dropData = {};
            highFrameRateUntil = glfwGetTime() + ActiveFrameRateDuration;
        }

        renderFrame(window, canvas, true);

        ImGuiIO& frameIo = ImGui::GetIO();
        if (shouldKeepHighFrameRate(frameIo))
            highFrameRateUntil = glfwGetTime() + ActiveFrameRateDuration;

        double targetFps = glfwGetTime() < highFrameRateUntil ? refreshRateForWindow(window) : IdleFps;
        waitForFrameBudget(frameStart, targetFps);
    }

    hex::ContentRegistry::Views::impl::clear();
    hex::ContentRegistry::Tools::clear();
    canvas.cleanup();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImNodes::DestroyContext();
    ImGui::DestroyContext();
    hex::TaskManager::exit();
    shutdownNativeDropTarget(window);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
