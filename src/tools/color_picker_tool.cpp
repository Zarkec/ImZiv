#include "tools/color_picker_tool.hpp"

#include "viewer/image_canvas.hpp"
#include "core/content_registry.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <cmath>
#include <cstdio>
#include <opencv2/opencv.hpp>

namespace {

    void rgbToHsv(int r, int g, int b, int& h, int& s, int& v, int& h_cv, int& s_cv, int& v_cv) {
        cv::Mat rgb(1, 1, CV_8UC3, cv::Scalar(b, g, r));
        cv::Mat hsv;
        cv::cvtColor(rgb, hsv, cv::COLOR_BGR2HSV);
        cv::Vec3b px = hsv.at<cv::Vec3b>(0, 0);
        h_cv = px[0]; s_cv = px[1]; v_cv = px[2];
        h = h_cv * 2;
        s = s_cv * 100 / 255;
        v = v_cv * 100 / 255;
    }

    void rgbToLab(int r, int g, int b, double& L, double& a, double& b_val) {
        cv::Mat bgr(1, 1, CV_32FC3);
        bgr.at<cv::Vec3f>(0, 0) = cv::Vec3f(b / 255.0f, g / 255.0f, r / 255.0f);
        cv::Mat lab;
        cv::cvtColor(bgr, lab, cv::COLOR_BGR2Lab);
        cv::Vec3f px = lab.at<cv::Vec3f>(0, 0);
        L = px[0]; a = px[1]; b_val = px[2];
    }

    void drawColorPickerTool(ImageCanvas& canvas) {
        bool enabled = canvas.colorPickerMode;
        if (ImGui::Checkbox("启用取色", &enabled))
            canvas.setTool(enabled ? ImageCanvas::Tool::ColorPicker : ImageCanvas::Tool::None);

        ImGui::Separator();

        uint8_t r = canvas.pickedR, g = canvas.pickedG, b = canvas.pickedB;

        // Color preview
        ImVec4 col(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
        ImGui::ColorButton("##preview", col, ImGuiColorEditFlags_None, ImVec2(50, 50));
        ImGui::SameLine();

        // HEX
        char hex[16];
        ImFormatString(hex, sizeof(hex), "#%02X%02X%02X", r, g, b);
        ImGui::BeginGroup();
        ImGui::Text("HEX");
        ImGui::SetNextItemWidth(80);
        if (ImGui::InputText("##hex", hex, sizeof(hex), ImGuiInputTextFlags_ReadOnly))
            ;
        ImGui::EndGroup();

        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            // Copy hex to clipboard
            ImGui::SetClipboardText(hex);
        }

        ImGui::Separator();

        // RGB
        ImGui::Text("RGB");
        ImGui::Text("R: %d  G: %d  B: %d", r, g, b);

        // HSV
        int h, s, v, h_cv, s_cv, v_cv;
        rgbToHsv(r, g, b, h, s, v, h_cv, s_cv, v_cv);
        ImGui::Separator();
        ImGui::Text("HSV");
        ImGui::Text("H: %d°  S: %d%%  V: %d%%", h, s, v);

        // HSV (OpenCV)
        ImGui::Separator();
        ImGui::Text("HSV (OpenCV)");
        ImGui::Text("H: %d  S: %d  V: %d", h_cv, s_cv, v_cv);

        // Lab
        double L, a, b_val;
        rgbToLab(r, g, b, L, a, b_val);
        ImGui::Separator();
        ImGui::Text("Lab");
        ImGui::Text("L: %.2f  a: %.2f  b: %.2f", L, a, b_val);

        ImGui::Separator();
        if (canvas.colorPicked)
            ImGui::Text("坐标: (%d, %d) [锁定]", (int)canvas.pickedImgX, (int)canvas.pickedImgY);
        else if (canvas.hasImage() && canvas.mouseImgX() >= 0)
            ImGui::Text("坐标: (%d, %d)", (int)canvas.mouseImgX(), (int)canvas.mouseImgY());
        else
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "坐标: --");

        ImGui::Spacing();
        if (canvas.colorPicked)
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "已锁定 (点击解锁)");
        else
            ImGui::TextWrapped("鼠标移动实时取色，左键锁定颜色。");
    }

}

void registerColorPickerTool(ImageCanvas& canvas) {
    hex::ContentRegistry::Tools::add("imziv.tool.colorpicker", "\xee\xab\x86", [&canvas] {
        drawColorPickerTool(canvas);
    });
}
