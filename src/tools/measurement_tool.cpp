#include "tools/measurement_tool.hpp"

#include "viewer/image_canvas.hpp"
#include "core/content_registry.hpp"

#include "imgui.h"

#include <cmath>

namespace {

    void drawMeasurementTool(ImageCanvas& canvas) {
        bool enabled = canvas.measureMode;
        if (ImGui::Checkbox("启用测距", &enabled))
            canvas.setTool(enabled ? ImageCanvas::Tool::Measure : ImageCanvas::Tool::None);

        ImGui::Separator();

        ImGui::SetNextItemWidth(100);
        ImGui::InputFloat("比例系数", &canvas.measureScale, 0, 0, "%.4f");
        ImGui::SameLine();
        ImGui::TextDisabled("(像素/单位)");
        ImGui::SetNextItemWidth(100);
        ImGui::InputText("单位", canvas.measureUnit, sizeof(canvas.measureUnit));

        ImGui::Separator();

        auto segColorVec4 = [](ImU32 c) -> ImVec4 {
            return ImVec4(((c >> 0) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f,
                          ((c >> 16) & 0xFF) / 255.0f, ((c >> 24) & 0xFF) / 255.0f);
        };
        constexpr int numColors = ImageCanvas::MeasureSegColorCount;

        if (canvas.measurePoints.empty()) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "点击图像开始测距");
        } else {
            double total = 0;
            for (int i = 1; i < (int)canvas.measurePoints.size(); i++) {
                double dx = canvas.measurePoints[i].x - canvas.measurePoints[i - 1].x;
                double dy = canvas.measurePoints[i].y - canvas.measurePoints[i - 1].y;
                double seg = std::sqrt(dx * dx + dy * dy);
                total += seg;
                if (canvas.measureScale > 0.0f)
                    ImGui::TextColored(segColorVec4(ImageCanvas::MeasureSegColors[(i - 1) % numColors]),
                        "段%d: %.2f px (%.4f %s)", i, seg, seg / canvas.measureScale, canvas.measureUnit);
                else
                    ImGui::TextColored(segColorVec4(ImageCanvas::MeasureSegColors[(i - 1) % numColors]),
                        "段%d: %.2f px", i, seg);
            }
            if (canvas.measureActive && canvas.measurePoints.size() >= 1) {
                double dx = canvas.measureLiveX - canvas.measurePoints.back().x;
                double dy = canvas.measureLiveY - canvas.measurePoints.back().y;
                double live = std::sqrt(dx * dx + dy * dy);
                if (canvas.measureScale > 0.0f)
                    ImGui::TextColored(segColorVec4(ImageCanvas::MeasureSegColors[(canvas.measurePoints.size() - 1) % numColors]),
                        "当前: %.2f px (%.4f %s)", live, live / canvas.measureScale, canvas.measureUnit);
                else
                    ImGui::TextColored(segColorVec4(ImageCanvas::MeasureSegColors[(canvas.measurePoints.size() - 1) % numColors]),
                        "当前: %.2f px", live);
                total += live;
            }
            ImGui::Separator();
            ImGui::Text("总距离");
            if (canvas.measureScale > 0.0f)
                ImGui::TextColored(ImVec4(1.0f, 0.31f, 0.31f, 1.0f), "%.2f px (%.4f %s)", total, total / canvas.measureScale, canvas.measureUnit);
            else
                ImGui::TextColored(ImVec4(1.0f, 0.31f, 0.31f, 1.0f), "%.2f 像素", total);
            ImGui::Text("点数: %d", (int)canvas.measurePoints.size());
        }

        ImGui::Spacing();
        ImGui::TextWrapped("左键添加点，右键结束折线，Ctrl+Z 撤销上一个点，Shift 锁定方向。");
    }

}

void registerMeasurementTool(ImageCanvas& canvas) {
    hex::ContentRegistry::Tools::add("imziv.tool.measure", "\xee\xaa\x96", [&canvas] {
        drawMeasurementTool(canvas);
    });
}
