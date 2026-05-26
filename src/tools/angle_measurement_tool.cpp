#include "tools/angle_measurement_tool.hpp"

#include "viewer/image_canvas.hpp"
#include "core/content_registry.hpp"

#include "imgui.h"

#include <cmath>

namespace {

    double calculateAngle(const ImVec2& p1, const ImVec2& p2) {
        double angle = std::atan2(double(p2.y - p1.y), double(p2.x - p1.x)) * 180.0 / 3.14159265358979323846;
        if (angle < 0.0)
            angle += 360.0;
        return angle;
    }

    double calculateAngleDifference(double angle1, double angle2) {
        double diff = std::fabs(angle1 - angle2);
        if (diff > 180.0)
            diff = 360.0 - diff;
        return diff;
    }

    void drawAngleMeasurementTool(ImageCanvas& canvas) {
        bool enabled = canvas.angleMode;
        if (ImGui::Checkbox("启用测角", &enabled)) {
            if (enabled)
                canvas.setTool(ImageCanvas::Tool::Angle);
            else {
                canvas.setTool(ImageCanvas::Tool::None);
                clearAngleMeasurement(canvas);
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("清除"))
            clearAngleMeasurement(canvas);

        ImGui::Separator();

        const bool hasVertex = canvas.angleClickCount >= 1;
        const bool hasFirstLine = canvas.angleClickCount >= 2;
        const bool hasSecondLine = canvas.angleClickCount >= 3;
        const double angle1 = hasVertex ? calculateAngle(canvas.angleVertex, canvas.angleFirstEnd) : 0.0;
        const double angle2 = hasFirstLine ? calculateAngle(canvas.angleVertex, canvas.angleSecondEnd) : 0.0;
        const double angleDiff = hasFirstLine ? calculateAngleDifference(angle1, angle2) : 0.0;

        ImGui::Text("角度");
        ImGui::TextColored(ImVec4(1.0f, 0.31f, 0.31f, 1.0f), "%.2f°", angleDiff);

        ImGui::Separator();
        if (hasVertex)
            ImGui::Text("顶点: (%.1f, %.1f)", canvas.angleVertex.x, canvas.angleVertex.y);
        else
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "顶点: --");

        ImGui::Text("线1角度: %.1f°", hasVertex ? angle1 : 0.0);
        ImGui::Text("线2角度: %.1f°", hasFirstLine ? angle2 : 0.0);

        ImGui::Spacing();
        if (!canvas.angleMode)
            ImGui::TextWrapped("启用后在图像上依次点击顶点、第一条边终点、第二条边终点。");
        else if (!hasVertex)
            ImGui::TextWrapped("点击图像设置角度顶点。");
        else if (!hasFirstLine)
            ImGui::TextWrapped("移动鼠标预览第一条边，点击确认。Shift 可按 15 度吸附。");
        else if (!hasSecondLine)
            ImGui::TextWrapped("移动鼠标预览第二条边，点击确认角度。Shift 可按 15 度吸附。");
        else
            ImGui::TextWrapped("测角完成。再次左键开始新的测角，右键或清除按钮可清除。");
    }

}

void registerAngleMeasurementTool(ImageCanvas& canvas) {
    hex::ContentRegistry::Tools::add("imziv.tool.angle", "\xee\xab\x95", [&canvas] {
        drawAngleMeasurementTool(canvas);
    });
}

void clearAngleMeasurement(ImageCanvas& canvas) {
    canvas.clearAngleMeasurement();
}
