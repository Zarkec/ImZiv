#include "core/view_tools.hpp"

#include "core/imgui_extensions.hpp"

#include "imgui_internal.h"

namespace hex::plugin::builtin {

    ViewTools::ViewTools() : View::Scrolling("imziv.view.tools", "\xee\xad\xad") {
        this->getWindowOpenState() = false;
        m_dragStartIterator = ContentRegistry::Tools::impl::getEntries().end();
    }

    void ViewTools::drawContent() {
        const auto& tools = ContentRegistry::Tools::impl::getEntries();

        for (auto iter = tools.begin(); iter != tools.end(); ++iter) {
            const auto& entry = *iter;

            if (m_detachedTools[entry.unlocalizedName])
                continue;

            if (m_collapsedTools.find(entry.unlocalizedName) == m_collapsedTools.end())
                m_collapsedTools[entry.unlocalizedName] = false;

            auto& collapsed = m_collapsedTools[entry.unlocalizedName];
            const std::string title = std::string(entry.icon != nullptr ? entry.icon : "") + " " + Lang(entry.unlocalizedName);
            if (ImGuiExt::BeginSubWindow(title.c_str(), &collapsed, ImVec2(0, collapsed ? 1.0f : 0.0f))) {
                entry.function();
            } else {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && m_dragStartIterator == tools.end())
                    m_dragStartIterator = iter;

                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    m_dragStartIterator = tools.end();

                if (!ImGui::IsWindowHovered() && m_dragStartIterator == iter)
                    m_detachedTools[entry.unlocalizedName] = true;
            }
            ImGuiExt::EndSubWindow();
        }
    }

    void ViewTools::drawAlwaysVisibleContent() {
        const auto& tools = ContentRegistry::Tools::impl::getEntries();

        for (auto iter = tools.begin(); iter != tools.end(); ++iter) {
            const auto& entry = *iter;

            if (!m_detachedTools[entry.unlocalizedName])
                continue;

            const std::string windowName = std::string(entry.icon != nullptr ? entry.icon : "") + " " + View::toWindowName(entry.unlocalizedName);
            ImGui::SetNextWindowPos(ImGui::GetMousePos() - ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing()), ImGuiCond_Appearing);
            if (ImGui::Begin(windowName.c_str(), &m_detachedTools[entry.unlocalizedName], ImGuiWindowFlags_NoCollapse)) {
                entry.function();

                if (ImGui::IsWindowAppearing() && m_dragStartIterator == iter) {
                    m_dragStartIterator = tools.end();
                    ImGui::StartMouseMovingWindowOrNode(ImGui::GetCurrentWindow(), nullptr, true);
                }
            }
            ImGui::End();
        }
    }

    void ViewTools::drawHelpText() {
        ImGuiExt::TextFormattedWrapped("工具视图用于放置独立工具。折叠工具后可以拖出为独立窗口。");
    }

}
