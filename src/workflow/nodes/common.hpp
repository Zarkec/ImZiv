#pragma once

#include "workflow/image_workflow.hpp"
#include "platform/native_dialog.hpp"

#include "imgui.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace imziv::workflow {

inline constexpr const char* IconVsFolderOpened = "\xEE\xAB\xB7";
inline constexpr const char* IconVsScreenFull = "\xEE\xAD\x8C";
inline constexpr const char* IconVsSelectRange = "\xEE\xAE\x85";

inline bool iconButton(const char* icon, const char* id, const char* tooltip) {
    char label[64];
    std::snprintf(label, sizeof(label), "%s##%s", icon, id);
    const ImVec2 size(ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
    const bool clicked = ImGui::Button(label, size);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
    return clicked;
}

inline void sliderInputInt(const char* label, int* v, int min, int max, int step = 1) {
    ImVec2 btnSz(ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
    char buf[64];
    std::snprintf(buf, sizeof(buf), "-##sl_%s", label);
    if (ImGui::Button(buf, btnSz))
        *v = std::clamp(*v - step, min, max);
    ImGui::SameLine(0, 2);
    std::snprintf(buf, sizeof(buf), "##sv_%s", label);
    ImGui::PushItemWidth(80);
    ImGui::SliderInt(buf, v, min, max);
    ImGui::PopItemWidth();
    std::snprintf(buf, sizeof(buf), "+##sr_%s", label);
    ImGui::SameLine(0, 2);
    if (ImGui::Button(buf, btnSz))
        *v = std::clamp(*v + step, min, max);
    ImGui::SameLine(0, 4);
    ImGui::TextUnformatted(label);
    *v = std::clamp(*v, min, max);
}

inline void sliderInputFloat(const char* label, float* v, float min, float max, float step = 0.1f) {
    ImVec2 btnSz(ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
    char buf[64];
    std::snprintf(buf, sizeof(buf), "-##sl_%s", label);
    if (ImGui::Button(buf, btnSz))
        *v = std::clamp(*v - step, min, max);
    ImGui::SameLine(0, 2);
    std::snprintf(buf, sizeof(buf), "##sv_%s", label);
    ImGui::PushItemWidth(80);
    ImGui::SliderFloat(buf, v, min, max);
    ImGui::PopItemWidth();
    std::snprintf(buf, sizeof(buf), "+##sr_%s", label);
    ImGui::SameLine(0, 2);
    if (ImGui::Button(buf, btnSz))
        *v = std::clamp(*v + step, min, max);
    ImGui::SameLine(0, 4);
    ImGui::TextUnformatted(label);
    *v = std::clamp(*v, min, max);
}

inline WorkflowAttribute attr(AttributeIO io, ValueType type, const char* name, Value defaultValue = {}) {
    return { 0, io, type, name, std::move(defaultValue) };
}

inline int attrId(const WorkflowNode& node, int index) {
    return node.attributes()[size_t(index)].id;
}

inline bool loadInteractionImage(WorkflowUiContext& ui, const WorkflowNode& node, int inputAttrId) {
    if (ui.viewer == nullptr) return false;
    const Value& val = node.cachedInput(inputAttrId);
    const auto* set = std::get_if<ImageSet>(&val);
    if (set == nullptr || set->items.empty()) return false;
    return ui.viewer->loadMat(set->items[0].image, "interaction.png");
}

inline std::string getString(const Value& value) {
    if (const auto* v = std::get_if<std::string>(&value))
        return *v;
    return {};
}

inline int getInt(const Value& value, int fallback = 0) {
    if (const auto* v = std::get_if<int>(&value)) return *v;
    if (const auto* v = std::get_if<double>(&value)) return int(*v);
    return fallback;
}

inline double getDouble(const Value& value, double fallback = 0.0) {
    if (const auto* v = std::get_if<double>(&value)) return *v;
    if (const auto* v = std::get_if<int>(&value)) return double(*v);
    return fallback;
}

inline bool getBool(const Value& value, bool fallback = false) {
    if (const auto* v = std::get_if<bool>(&value)) return *v;
    return fallback;
}

inline ImageSet requireImageSet(const Value& value, const WorkflowNode& node) {
    if (const auto* set = std::get_if<ImageSet>(&value))
        return *set;
    throw WorkflowNodeError { node.id(), "需要图片输入" };
}

inline RegionSet requireRegionSet(const Value& value, const WorkflowNode& node) {
    if (const auto* set = std::get_if<RegionSet>(&value))
        return *set;
    throw WorkflowNodeError { node.id(), "需要区域输入" };
}

inline Rect requireRect(const Value& value, const WorkflowNode& node) {
    if (const auto* rect = std::get_if<Rect>(&value))
        return *rect;
    throw WorkflowNodeError { node.id(), "需要矩形输入" };
}

inline Roi requireRoi(const Value& value, const WorkflowNode& node) {
    if (const auto* roi = std::get_if<Roi>(&value))
        return *roi;
    if (const auto* rect = std::get_if<Rect>(&value)) {
        Roi roi;
        roi.shape = RoiShape::Rect;
        roi.rect = *rect;
        return roi;
    }
    throw WorkflowNodeError { node.id(), "需要 ROI 输入" };
}

inline cv::Rect toCvRect(const Rect& rect) {
    return cv::Rect(rect.x, rect.y, rect.w, rect.h);
}

inline Rect fromCvRect(const cv::Rect& rect) {
    return { rect.x, rect.y, rect.width, rect.height };
}

inline cv::Rect roiBounds(const Roi& roi) {
    switch (roi.shape) {
        case RoiShape::Rect:
            return toCvRect(roi.rect);
        case RoiShape::Circle: {
            const int r = std::max(1, int(std::ceil(roi.radius)));
            const int x = int(std::floor(roi.center.x)) - r;
            const int y = int(std::floor(roi.center.y)) - r;
            return cv::Rect(x, y, r * 2 + 1, r * 2 + 1);
        }
        case RoiShape::Ring: {
            const int r = std::max(1, int(std::ceil(roi.outerRadius)));
            const int x = int(std::floor(roi.center.x)) - r;
            const int y = int(std::floor(roi.center.y)) - r;
            return cv::Rect(x, y, r * 2 + 1, r * 2 + 1);
        }
        case RoiShape::Polygon:
            if (!roi.polygon.empty())
                return cv::boundingRect(roi.polygon);
            break;
    }
    return {};
}

inline cv::Mat roiMask(const Roi& roi, cv::Size size, cv::Point offset = cv::Point()) {
    cv::Mat mask = cv::Mat::zeros(size, CV_8UC1);
    auto shiftedPoint = [&](const cv::Point2d& point) {
        return cv::Point(int(std::round(point.x)) - offset.x,
                         int(std::round(point.y)) - offset.y);
    };

    switch (roi.shape) {
        case RoiShape::Rect:
            cv::rectangle(mask, toCvRect(roi.rect) - offset, cv::Scalar(255), cv::FILLED);
            break;
        case RoiShape::Circle:
            cv::circle(mask, shiftedPoint(roi.center), std::max(1, int(std::round(roi.radius))),
                       cv::Scalar(255), cv::FILLED);
            break;
        case RoiShape::Ring: {
            const int outer = std::max(1, int(std::round(roi.outerRadius)));
            const int inner = std::max(0, int(std::round(roi.innerRadius)));
            cv::Point center = shiftedPoint(roi.center);
            cv::circle(mask, center, outer, cv::Scalar(255), cv::FILLED);
            if (inner > 0)
                cv::circle(mask, center, inner, cv::Scalar(0), cv::FILLED);
            break;
        }
        case RoiShape::Polygon:
            if (!roi.polygon.empty()) {
                std::vector<cv::Point> shifted;
                shifted.reserve(roi.polygon.size());
                for (const auto& point : roi.polygon)
                    shifted.push_back(point - offset);
                std::vector<std::vector<cv::Point>> polygons = { std::move(shifted) };
                cv::fillPoly(mask, polygons, cv::Scalar(255));
            }
            break;
    }
    return mask;
}

struct FillOptions {
    enum class Mode {
        Transparent,
        Black,
        White,
        Custom
    };

    Mode mode = Mode::Transparent;
    float color[3] = { 1.0f, 1.0f, 1.0f };
};

inline const char* fillModeName(FillOptions::Mode mode) {
    switch (mode) {
        case FillOptions::Mode::Transparent: return "透明";
        case FillOptions::Mode::Black: return "黑色";
        case FillOptions::Mode::White: return "白色";
        case FillOptions::Mode::Custom: return "自定义";
    }
    return "透明";
}

inline std::string fillModeToString(FillOptions::Mode mode) {
    return fillModeName(mode);
}

inline FillOptions::Mode fillModeFromString(const std::string& mode) {
    if (mode == "黑色") return FillOptions::Mode::Black;
    if (mode == "白色") return FillOptions::Mode::White;
    if (mode == "自定义") return FillOptions::Mode::Custom;
    return FillOptions::Mode::Transparent;
}

inline cv::Scalar fillBgrScalar(const FillOptions& fill) {
    if (fill.mode == FillOptions::Mode::White)
        return cv::Scalar(255, 255, 255, 255);
    if (fill.mode == FillOptions::Mode::Black || fill.mode == FillOptions::Mode::Transparent)
        return cv::Scalar(0, 0, 0, fill.mode == FillOptions::Mode::Transparent ? 0 : 255);

    return cv::Scalar(
        std::clamp(fill.color[2], 0.0f, 1.0f) * 255.0,
        std::clamp(fill.color[1], 0.0f, 1.0f) * 255.0,
        std::clamp(fill.color[0], 0.0f, 1.0f) * 255.0,
        255.0
    );
}

inline cv::Scalar fillScalarForImage(const FillOptions& fill, const cv::Mat& image) {
    const cv::Scalar bgr = fillBgrScalar(fill);
    if (image.channels() == 1) {
        const double gray = std::round(0.114 * bgr[0] + 0.587 * bgr[1] + 0.299 * bgr[2]);
        return cv::Scalar(gray);
    }
    if (image.channels() == 4)
        return bgr;
    return cv::Scalar(bgr[0], bgr[1], bgr[2]);
}

inline void drawFillOptions(FillOptions& fill) {
    static const char* modes[] = { "透明", "黑色", "白色", "自定义" };
    int current = 0;
    for (int i = 0; i < int(sizeof(modes) / sizeof(modes[0])); ++i) {
        if (fillModeToString(fill.mode) == modes[i])
            current = i;
    }
    ImGui::PushItemWidth(120.0f);
    if (ImGui::Combo("背景填充", &current, modes, int(sizeof(modes) / sizeof(modes[0]))))
        fill.mode = fillModeFromString(modes[current]);
    if (fill.mode == FillOptions::Mode::Custom)
        ImGui::ColorEdit3("填充颜色", fill.color, ImGuiColorEditFlags_NoInputs);
    ImGui::PopItemWidth();
}

inline void storeFillOptions(nlohmann::json& j, const FillOptions& fill) {
    j["fillMode"] = fillModeToString(fill.mode);
    j["fillColor"] = { fill.color[0], fill.color[1], fill.color[2] };
}

inline void loadFillOptions(const nlohmann::json& j, FillOptions& fill) {
    fill.mode = fillModeFromString(j.value("fillMode", "透明"));
    if (j.contains("fillColor") && j["fillColor"].is_array() && j["fillColor"].size() >= 3) {
        fill.color[0] = j["fillColor"][0].get<float>();
        fill.color[1] = j["fillColor"][1].get<float>();
        fill.color[2] = j["fillColor"][2].get<float>();
    }
}

inline cv::Mat applyMaskFill(const cv::Mat& image, const cv::Mat& mask, const FillOptions& fill) {
    if (image.empty() || mask.empty())
        return {};

    cv::Mat singleMask;
    if (mask.channels() > 1)
        cv::cvtColor(mask, singleMask, cv::COLOR_BGR2GRAY);
    else
        singleMask = mask;

    cv::Mat result;
    if (fill.mode == FillOptions::Mode::Transparent) {
        cv::Mat bgra;
        if (image.channels() == 1)
            cv::cvtColor(image, bgra, cv::COLOR_GRAY2BGRA);
        else if (image.channels() == 3)
            cv::cvtColor(image, bgra, cv::COLOR_BGR2BGRA);
        else if (image.channels() == 4)
            bgra = image.clone();
        else
            return {};

        std::vector<cv::Mat> channels;
        cv::split(bgra, channels);
        if (channels.size() == 4)
            channels[3] = singleMask.clone();
        cv::merge(channels, result);
    } else {
        result = cv::Mat(image.size(), image.type(), fillScalarForImage(fill, image));
        image.copyTo(result, singleMask);
    }
    return result;
}

inline std::string normalizeFormat(std::string format) {
    std::transform(format.begin(), format.end(), format.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
    if (format.empty())
        return "png";
    if (format[0] == '.')
        format.erase(format.begin());
    if (format == "jpeg")
        return "jpg";
    return format;
}

inline std::string applyPattern(std::string pattern, const ImageItem& item, const std::string& format) {
    if (pattern.empty())
        pattern = "{stem}_{index}";

    const std::string stem = item.sourcePath.empty() ? item.fileName : pathStem(item.sourcePath);
    const std::string index = std::to_string(item.index);
    auto replaceAll = [](std::string& text, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = text.find(from, pos)) != std::string::npos) {
            text.replace(pos, from.size(), to);
            pos += to.size();
        }
    };

    replaceAll(pattern, "{stem}", stem);
    replaceAll(pattern, "{name}", item.fileName);
    replaceAll(pattern, "{index}", index);
    if (pathExtension(pattern).empty())
        pattern += "." + format;
    return pattern;
}

inline cv::Mat prepareForSave(const cv::Mat& image, const std::string& format) {
    if ((format == "jpg" || format == "jpeg") && image.channels() == 4) {
        cv::Mat bgr;
        cv::cvtColor(image, bgr, cv::COLOR_BGRA2BGR);
        return bgr;
    }
    return image;
}

inline std::vector<unsigned char> readFileBytes(const std::string& path) {
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring wpath(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);
    FILE* file = _wfopen(wpath.c_str(), L"rb");
#else
    FILE* file = fopen(path.c_str(), "rb");
#endif
    if (!file) return {};
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0) {
        fclose(file);
        return {};
    }
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    if (size > 0)
        fread(bytes.data(), 1, size_t(size), file);
    fclose(file);
    return bytes;
}

inline cv::Mat readImageFile(const std::string& path) {
    auto bytes = readFileBytes(path);
    if (bytes.empty())
        return {};
    cv::Mat raw(1, int(bytes.size()), CV_8U, bytes.data());
    return cv::imdecode(raw, cv::IMREAD_UNCHANGED);
}

inline cv::Mat toBgrImage(const cv::Mat& image) {
    if (image.empty())
        return {};
    if (image.channels() == 1) {
        cv::Mat bgr;
        cv::cvtColor(image, bgr, cv::COLOR_GRAY2BGR);
        return bgr;
    }
    if (image.channels() == 3)
        return image.clone();
    if (image.channels() == 4) {
        cv::Mat bgr;
        cv::cvtColor(image, bgr, cv::COLOR_BGRA2BGR);
        return bgr;
    }
    return {};
}

inline cv::Mat convertColorSpace(const cv::Mat& image, const std::string& source, const std::string& target) {
    if (image.empty())
        return {};

    if (source == target) {
        if (source == "BGR")
            return toBgrImage(image);
        if (source == "灰度")
            return image.channels() == 1 ? image.clone() : cv::Mat();
        if (image.channels() == 3)
            return image.clone();
        return {};
    }

    auto sourceToBgr = [&]() -> cv::Mat {
        if (source == "BGR")
            return toBgrImage(image);
        if (source == "RGB") {
            if (image.channels() != 3) return {};
            cv::Mat bgr;
            cv::cvtColor(image, bgr, cv::COLOR_RGB2BGR);
            return bgr;
        }
        if (source == "HSV") {
            if (image.channels() != 3) return {};
            cv::Mat bgr;
            cv::cvtColor(image, bgr, cv::COLOR_HSV2BGR);
            return bgr;
        }
        if (source == "HLS") {
            if (image.channels() != 3) return {};
            cv::Mat bgr;
            cv::cvtColor(image, bgr, cv::COLOR_HLS2BGR);
            return bgr;
        }
        if (source == "Lab") {
            if (image.channels() != 3) return {};
            cv::Mat bgr;
            cv::cvtColor(image, bgr, cv::COLOR_Lab2BGR);
            return bgr;
        }
        if (source == "YCrCb") {
            if (image.channels() != 3) return {};
            cv::Mat bgr;
            cv::cvtColor(image, bgr, cv::COLOR_YCrCb2BGR);
            return bgr;
        }
        if (source == "灰度") {
            if (image.channels() != 1) return {};
            return toBgrImage(image);
        }
        return {};
    };

    if (target == "灰度") {
        cv::Mat gray;
        if (source == "灰度" && image.channels() == 1)
            return image.clone();
        cv::Mat bgr = sourceToBgr();
        if (bgr.empty())
            return {};
        cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
        return gray;
    }

    cv::Mat bgr = sourceToBgr();
    if (bgr.empty())
        return {};
    if (target == "BGR")
        return bgr;

    cv::Mat converted;
    if (target == "RGB")
        cv::cvtColor(bgr, converted, cv::COLOR_BGR2RGB);
    else if (target == "HSV")
        cv::cvtColor(bgr, converted, cv::COLOR_BGR2HSV);
    else if (target == "HLS")
        cv::cvtColor(bgr, converted, cv::COLOR_BGR2HLS);
    else if (target == "Lab")
        cv::cvtColor(bgr, converted, cv::COLOR_BGR2Lab);
    else if (target == "YCrCb")
        cv::cvtColor(bgr, converted, cv::COLOR_BGR2YCrCb);
    return converted;
}

inline cv::Mat toHsvImage(const cv::Mat& image, bool inputIsHsv) {
    if (image.empty())
        return {};

    if (inputIsHsv) {
        if (image.channels() == 3)
            return image.clone();
        return {};
    }

    cv::Mat bgr = toBgrImage(image);
    if (bgr.empty())
        return {};

    cv::Mat hsv;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
    return hsv;
}

inline void normalizeRange(int& minValue, int& maxValue, int low, int high) {
    minValue = std::clamp(minValue, low, high);
    maxValue = std::clamp(maxValue, low, high);
    if (minValue > maxValue)
        std::swap(minValue, maxValue);
}

inline int interpolationFlag(const std::string& interpolation) {
    if (interpolation == "最近邻")
        return cv::INTER_NEAREST;
    if (interpolation == "面积")
        return cv::INTER_AREA;
    if (interpolation == "三次")
        return cv::INTER_CUBIC;
    return cv::INTER_LINEAR;
}

inline int morphShape(const std::string& shape) {
    if (shape == "椭圆") return cv::MORPH_ELLIPSE;
    if (shape == "十字") return cv::MORPH_CROSS;
    return cv::MORPH_RECT;
}

} // namespace imziv::workflow
