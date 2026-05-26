#include "app/ui.hpp"

#include "core/content_registry.hpp"
#include "platform/native_dialog.hpp"
#include "platform/platform_window.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace {
    constexpr const char* IconVsChromeClose = "\xEE\xAA\xB8";
    constexpr const char* IconVsChromeMaximize = "\xEE\xAA\xB9";
    constexpr const char* IconVsChromeMinimize = "\xEE\xAA\xBA";
    constexpr const char* IconVsChromeRestore = "\xEE\xAA\xBB";
    constexpr const char* IconVsPin = "\xEE\xAC\xAB";
    constexpr const char* IconVsPinned = "\xEE\xAE\xA0";
    constexpr const char* IconVsTrash = "\xEE\xAA\x81";
    constexpr const char* IconVsLocation = "\xEE\xAC\x9A";
    constexpr const char* IconVsSaveAs = "\xEE\xAD\x8A";
    constexpr const char* IconVsCopy = "\xEE\xAF\x8C";
    constexpr const char* IconVsFolder = "\xEE\xAA\x83";
    constexpr const char* IconVsSignOut = "\xEE\xA9\xAE";
    constexpr const char* IconVsScreenFull = "\xEE\xAD\x8C";
    constexpr const char* IconVsScreenNormal = "\xEE\xAD\x8D";
    constexpr const char* IconVsZoomIn = "\xEE\xAE\x81";
    constexpr const char* IconVsZoomOut = "\xEE\xAE\x82";
    constexpr ImWchar CodiconGlyphRanges[] = {
        0xEA6E, 0xEA6E,
        0xEA7F, 0xEA83,
        0xEA94, 0xEA96,
        0xEAB8, 0xEAC6,
        0xEAD3, 0xEAD7,
        0xEAEA, 0xEAEA,
        0xEAF7, 0xEAF7,
        0xEB1A, 0xEB1A,
        0xEB2B, 0xEB2F,
        0xEB37, 0xEB37,
        0xEB49, 0xEB4D,
        0xEB6D, 0xEB6D,
        0xEB81, 0xEB85,
        0xEBA0, 0xEBA0,
        0xEBA6, 0xEBA6,
        0xEB9E, 0xEB9E,
        0xEBC0, 0xEBC0,
        0xEBCC, 0xEBCC,
        0xEC19, 0xEC19,
        0
    };
    constexpr ImWchar CjkGlyphRanges[] = {
        0x0020, 0x00FF,
        0x2000, 0x206F,
        0x3000, 0x30FF,
        0x31F0, 0x31FF,
        0x4E00, 0x9FFF,
        0xFF00, 0xFFEF,
        0
    };

    std::filesystem::path findAssetPath(const std::filesystem::path& relativePath);

    struct IconTexture {
        GLuint id = 0;
        int width = 0;
        int height = 0;
    };

    IconTexture& getTitleBarIconTexture() {
        static IconTexture texture;
        static bool attempted = false;
        if (attempted)
            return texture;

        attempted = true;
        const auto iconPath = findAssetPath("assets/icons/icon.png");
        if (iconPath.empty())
            return texture;

        cv::Mat icon = cv::imread(iconPath.string(), cv::IMREAD_UNCHANGED);
        if (icon.empty())
            return texture;

        cv::Mat rgba;
        switch (icon.channels()) {
            case 1: cv::cvtColor(icon, rgba, cv::COLOR_GRAY2RGBA); break;
            case 3: cv::cvtColor(icon, rgba, cv::COLOR_BGR2RGBA); break;
            case 4: cv::cvtColor(icon, rgba, cv::COLOR_BGRA2RGBA); break;
            default: return texture;
        }

        if (!rgba.isContinuous())
            rgba = rgba.clone();

        glGenTextures(1, &texture.id);
        glBindTexture(GL_TEXTURE_2D, texture.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba.cols, rgba.rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data);
        glBindTexture(GL_TEXTURE_2D, 0);

        texture.width = rgba.cols;
        texture.height = rgba.rows;
        return texture;
    }

    ImTextureID toImTextureId(GLuint id) {
        return static_cast<ImTextureID>(static_cast<intptr_t>(id));
    }

    std::string formatFileSize(size_t bytes) {
        constexpr const char* units[] = { "B", "KB", "MB", "GB" };
        double value = static_cast<double>(bytes);
        int unit = 0;
        while (value >= 1024.0 && unit < 3) {
            value /= 1024.0;
            unit++;
        }

        char buffer[64];
        if (unit == 0)
            ImFormatString(buffer, sizeof(buffer), "%llu %s", static_cast<unsigned long long>(bytes), units[unit]);
        else
            ImFormatString(buffer, sizeof(buffer), "%.2f %s", value, units[unit]);
        return buffer;
    }

    void pushMenuStyle() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 7.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(30, 30, 34, 250));
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(70, 70, 78, 255));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(55, 75, 110, 220));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(66, 92, 136, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(78, 106, 156, 255));
    }

    void popMenuStyle() {
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(4);
    }

    bool titleBarButton(const char* id, const char* label, ImVec2 pos, ImVec2 size, bool danger = false, bool selected = false) {
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton(id, size);

        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (hovered || active || selected) {
            ImU32 bg = danger
                ? (active ? IM_COL32(196, 43, 28, 255) : IM_COL32(232, 17, 35, 255))
                : selected && !hovered && !active ? ImGui::GetColorU32(ImGuiCol_Button)
                : (active ? ImGui::GetColorU32(ImGuiCol_ButtonActive) : ImGui::GetColorU32(ImGuiCol_ButtonHovered));
            dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg);
        }

        ImU32 fg = danger && hovered ? IM_COL32(255, 255, 255, 255) : ImGui::GetColorU32(ImGuiCol_Text);
        const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
        dl->AddText(ImVec2(pos.x + (size.x - labelSize.x) * 0.5f,
                           pos.y + (size.y - labelSize.y) * 0.5f), fg, label);
        return clicked;
    }

    void renderWindowingPopup(GLFWwindow* window) {
        pushMenuStyle();
        if (!ImGui::BeginPopup("WindowingMenu")) {
            popMenuStyle();
            return;
        }

        const bool maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE;
        ImGui::BeginDisabled(!maximized);
        if (ImGui::MenuItemEx("还原", IconVsChromeRestore)) glfwRestoreWindow(window);
        ImGui::EndDisabled();

        if (ImGui::MenuItemEx("最小化", IconVsChromeMinimize)) glfwIconifyWindow(window);

        ImGui::BeginDisabled(maximized);
        if (ImGui::MenuItemEx("最大化", IconVsChromeMaximize)) glfwMaximizeWindow(window);
        ImGui::EndDisabled();

        ImGui::Separator();
        if (ImGui::MenuItemEx("关闭", IconVsChromeClose)) glfwSetWindowShouldClose(window, GLFW_TRUE);

        ImGui::EndPopup();
        popMenuStyle();
    }

    void renderTitleBarExtras(GLFWwindow* window, ImageCanvas& canvas) {
        ImGuiWindow* current = ImGui::GetCurrentWindowRead();
        if (!current) return;

        const float barH = current->MenuBarHeight > 0.0f ? current->MenuBarHeight : ImGui::GetFrameHeight();
        const ImVec2 winPos = ImGui::GetWindowPos();
        const ImVec2 winSize = ImGui::GetWindowSize();
        const ImVec2 buttonSize(barH * 1.55f, barH);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        setTitleBarHeight(barH);

        const char* title = canvas.hasImage() ? canvas.fileName.c_str() : "ImZiv";
        ImVec2 textSize = ImGui::CalcTextSize(title);
        float titleX = winPos.x + (winSize.x - textSize.x) * 0.5f;
        float minTextX = winPos.x + 150.0f;
        float maxTextX = winPos.x + winSize.x - buttonSize.x * 4.0f - textSize.x - 12.0f;
        if (titleX >= minTextX && titleX <= maxTextX) {
            dl->AddText(ImVec2(titleX, winPos.y + (barH - textSize.y) * 0.5f),
                        ImGui::GetColorU32(ImGuiCol_TextDisabled), title);
        }

        const bool topMost = isWindowTopMost(window);
        ImVec2 buttonPos(winPos.x + winSize.x - buttonSize.x * 4.0f, winPos.y);
        if (titleBarButton("##TopMost", topMost ? IconVsPinned : IconVsPin, buttonPos, buttonSize, false, topMost))
            setWindowTopMost(window, !topMost);

        buttonPos.x += buttonSize.x;
        if (titleBarButton("##Minimize", IconVsChromeMinimize, buttonPos, buttonSize))
            glfwIconifyWindow(window);

        buttonPos.x += buttonSize.x;
        if (glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE) {
            if (titleBarButton("##Restore", IconVsChromeRestore, buttonPos, buttonSize))
                glfwRestoreWindow(window);
        } else {
            if (titleBarButton("##Maximize", IconVsChromeMaximize, buttonPos, buttonSize))
                glfwMaximizeWindow(window);
        }

        buttonPos.x += buttonSize.x;
        if (titleBarButton("##Close", IconVsChromeClose, buttonPos, buttonSize, true))
            glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    std::filesystem::path findAssetPath(const std::filesystem::path& relativePath) {
        std::vector<std::filesystem::path> roots;

#ifdef _WIN32
        wchar_t exePath[MAX_PATH] = {};
        DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        if (len > 0 && len < MAX_PATH)
            roots.push_back(std::filesystem::path(exePath).parent_path());
#endif

        roots.push_back(std::filesystem::current_path());
        roots.push_back(std::filesystem::current_path().parent_path());

        for (const auto& root : roots) {
            std::filesystem::path candidate = root / relativePath;
            if (std::filesystem::exists(candidate))
                return candidate;
        }

        return {};
    }
}

void loadUiFonts() {
    ImGuiIO& io = ImGui::GetIO();
    ImFontAtlas* atlas = io.Fonts;
    constexpr float BaseFontSize = 16.0f;
    constexpr float CjkFontSize = 16.0f;
    constexpr float CodiconFontSize = BaseFontSize * 0.95f;

    const auto baseFontPath = findAssetPath("assets/fonts/JetBrainsMono.ttf");
    const auto cjkFontPath = findAssetPath("assets/fonts/HarmonyOS_Sans_SC_Medium.ttf");
    const auto codiconFontPath = findAssetPath("assets/fonts/codicons.ttf");
    if (baseFontPath.empty() || cjkFontPath.empty() || codiconFontPath.empty()) {
        atlas->AddFontDefault();
        return;
    }

    static ImVector<ImWchar> glyphRanges;
    glyphRanges.clear();

    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(atlas->GetGlyphRangesDefault());
    builder.AddRanges(CjkGlyphRanges);
    builder.AddText("打开图片 文件 视图 适应窗口 原始大小 放大 缩小 退出 坐标 缩放 尺寸");
    builder.BuildRanges(&glyphRanges);

    ImFontConfig baseConfig = {};
    baseConfig.OversampleH = 2;
    baseConfig.OversampleV = 1;
    strcpy(baseConfig.Name, "JetBrains Mono + HarmonyOS Sans SC");

    ImFont* font = atlas->AddFontFromFileTTF(baseFontPath.string().c_str(), BaseFontSize, &baseConfig, glyphRanges.Data);

    ImFontConfig mergeConfig = {};
    mergeConfig.MergeMode = true;
    mergeConfig.PixelSnapH = true;
    mergeConfig.OversampleH = 1;
    mergeConfig.OversampleV = 1;
    mergeConfig.GlyphOffset = ImVec2(0.0f, 1.0f);
    strcpy(mergeConfig.Name, "HarmonyOS Sans SC CJK");
    atlas->AddFontFromFileTTF(cjkFontPath.string().c_str(), CjkFontSize, &mergeConfig, glyphRanges.Data);

    ImFontConfig codiconConfig = {};
    codiconConfig.MergeMode = true;
    codiconConfig.PixelSnapH = true;
    codiconConfig.GlyphOffset = ImVec2(0.0f, 1.5f);
    strcpy(codiconConfig.Name, "VS Codicons");
    atlas->AddFontFromFileTTF(codiconFontPath.string().c_str(), CodiconFontSize, &codiconConfig, CodiconGlyphRanges);

    if (font != nullptr)
        io.FontDefault = font;
}

void renderMenu(GLFWwindow* window, ImageCanvas& canvas) {
    if (!ImGui::BeginMenuBar()) return;

    ImGuiWindow* current = ImGui::GetCurrentWindowRead();
    const float barH = current ? current->MenuBarHeight : ImGui::GetFrameHeight();

    ImGui::SetCursorPosX(5.0f);
    const ImVec2 iconButtonSize(barH, barH);
    const float iconVisualSize = std::max(10.0f, barH * 0.68f);
    const ImVec2 iconButtonPos = ImGui::GetCursorScreenPos();
    IconTexture& appIcon = getTitleBarIconTexture();
    if (appIcon.id != 0) {
        const float iconOffset = (barH - iconVisualSize) * 0.5f;
        const ImVec2 iconMin(iconButtonPos.x + iconOffset, iconButtonPos.y + iconOffset);
        const ImVec2 iconMax(iconMin.x + iconVisualSize, iconMin.y + iconVisualSize);
        ImGui::GetWindowDrawList()->AddImage(toImTextureId(appIcon.id), iconMin, iconMax);
    }
    ImGui::InvisibleButton("##AppIcon", iconButtonSize);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
        ImGui::OpenPopup("WindowingMenu");
    renderWindowingPopup(window);

    ImGui::SameLine(0.0f, 4.0f);

    pushMenuStyle();
    if (ImGui::BeginMenu("文件")) {
        if (ImGui::MenuItemEx("打开图片", IconVsFolder, "Ctrl+O")) {
            auto path = openFileDialog(window);
            if (!path.empty()) canvas.openFile(path);
        }
        ImGui::BeginDisabled(!canvas.hasImage());
        if (ImGui::MenuItemEx("另存为", IconVsSaveAs, "Ctrl+S")) {
            auto path = saveFileDialog(window, canvas.fileName);
            if (!path.empty()) canvas.saveAs(path);
        }
        ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::MenuItemEx("退出", IconVsSignOut))
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        ImGui::EndMenu();
    }
    popMenuStyle();

    pushMenuStyle();
    if (ImGui::BeginMenu("视图")) {
        if (ImGui::MenuItemEx("适应窗口", IconVsScreenFull, "F")) canvas.fitToView();
        if (ImGui::MenuItemEx("原始大小", IconVsScreenNormal, "Ctrl+1")) canvas.setActualSize();
        ImGui::Separator();
        if (ImGui::MenuItemEx("放大", IconVsZoomIn, "=")) { if (canvas.hasImage()) canvas.setZoom(canvas.zoom() * 1.25f); }
        if (ImGui::MenuItemEx("缩小", IconVsZoomOut, "-")) { if (canvas.hasImage()) canvas.setZoom(canvas.zoom() / 1.25f); }
        ImGui::Separator();
        auto& views = hex::ContentRegistry::Views::impl::getEntries();
        for (auto& [name, view] : views) {
            if (view->hasViewMenuItemEntry()) {
                bool& open = view->getWindowOpenState();
                const char* icon = view->getIcon();
                if (icon != nullptr && icon[0] == '\0')
                    icon = nullptr;
                if (ImGui::MenuItemEx(view->getName().c_str(), icon, nullptr, open)) {
                    open = !open;
                    if (open) view->bringToFront();
                }
            }
        }
        ImGui::EndMenu();
    }
    popMenuStyle();

    renderTitleBarExtras(window, canvas);
    ImGui::EndMenuBar();
}
