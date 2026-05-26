#include "core/imgui_extensions.hpp"

#include "imgui_internal.h"

#include <string_view>

namespace hex::ImGuiExt {

    bool BeginSubWindow(const char* label, bool* collapsed, ImVec2 size, ImGuiChildFlags flags) {
        const bool hasMenuBar = !std::string_view(label != nullptr ? label : "").empty();

        bool result = false;
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0F);
        ImGui::PushID("SubWindow");
        if (ImGui::BeginChild(label, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | flags,
                              hasMenuBar ? ImGuiWindowFlags_MenuBar : ImGuiWindowFlags_None)) {
            result = true;

            if (hasMenuBar && ImGui::BeginMenuBar()) {
                if (collapsed == nullptr) {
                    ImGui::TextUnformatted(label);
                } else {
                    const auto& style = ImGui::GetStyle();
                    const auto framePadding = style.FramePadding.x;
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, style.FramePadding.y));
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() - style.WindowPadding.x + framePadding);
                    ImGui::TreeNodeSetOpen(ImGui::GetID("##CollapseHeader"), !*collapsed);
                    *collapsed = !ImGui::TreeNodeEx("##CollapseHeader", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanLabelWidth);
                    ImGui::SameLine(0.0f, framePadding);
                    ImGui::TextUnformatted(label);
                    if (!*collapsed)
                        ImGui::TreePop();

                    ImGui::PopStyleVar();
                }
                ImGui::EndMenuBar();
            }

            if (collapsed != nullptr && *collapsed)
                result = false;
        }
        ImGui::PopStyleVar();

        return result;
    }

    void EndSubWindow() {
        ImGui::EndChild();
        ImGui::PopID();
    }

    void TextFormattedWrapped(const char* text) {
        ImGui::TextWrapped("%s", text);
    }

}
