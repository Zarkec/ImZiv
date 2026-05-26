#include "common.hpp"

#include <opencv2/imgproc.hpp>

#include <limits>

namespace imziv::workflow {
namespace {

    cv::Mat toGrayMask(const cv::Mat& image) {
        cv::Mat gray;
        if (image.channels() == 1)
            gray = image;
        else if (image.channels() == 3)
            cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        else if (image.channels() == 4)
            cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
        else
            return {};

        cv::Mat mask;
        cv::threshold(gray, mask, 0, 255, cv::THRESH_BINARY);
        return mask;
    }

    Region regionFromContour(std::vector<cv::Point> contour, int label) {
        Region region;
        region.contour = std::move(contour);
        region.bbox = fromCvRect(cv::boundingRect(region.contour));
        region.area = std::abs(cv::contourArea(region.contour));
        region.label = label;

        const cv::Moments moments = cv::moments(region.contour);
        if (std::abs(moments.m00) > std::numeric_limits<double>::epsilon()) {
            region.centroid.x = moments.m10 / moments.m00;
            region.centroid.y = moments.m01 / moments.m00;
        } else {
            region.centroid.x = region.bbox.x + region.bbox.w * 0.5;
            region.centroid.y = region.bbox.y + region.bbox.h * 0.5;
        }
        return region;
    }

    bool areaInRange(double area, double minArea, double maxArea) {
        return area >= minArea && (maxArea <= 0.0 || area <= maxArea);
    }

    void beginCropSelect(WorkflowUiContext& ui, int ownerId) {
        if (ui.viewer == nullptr)
            return;
        ui.viewer->setTool(ImageCanvas::Tool::CropSelect, ownerId);
    }

    bool consumeCropSelect(WorkflowUiContext& ui, int ownerId, Rect& rect) {
        if (ui.viewer == nullptr)
            return false;
        return ui.viewer->consumeCrop(ownerId, rect.x, rect.y, rect.w, rect.h);
    }

    void beginCenterSelect(WorkflowUiContext& ui, int ownerId) {
        if (ui.viewer == nullptr)
            return;
        ui.viewer->setTool(ImageCanvas::Tool::RoiCenterSelect, ownerId);
    }

    bool consumeCenterSelect(WorkflowUiContext& ui, int ownerId, double& x, double& y) {
        if (ui.viewer == nullptr)
            return false;
        return ui.viewer->consumePoint(ownerId, x, y);
    }

    class NodeRect : public WorkflowNode {
    public:
        NodeRect()
            : WorkflowNode("analysis.rect", "ROI 矩形", {
                attr(AttributeIO::Output, ValueType::Roi, "ROI")
            }) {}

        void drawBody(WorkflowUiContext& ui) override {
            ImGui::PushID(id());
            ImGui::PushItemWidth(80.0f);
            ImGui::InputInt("X", &m_rect.x);
            ImGui::InputInt("Y", &m_rect.y);
            ImGui::InputInt("W", &m_rect.w);
            ImGui::InputInt("H", &m_rect.h);
            ImGui::PopItemWidth();
            m_rect.x = std::max(0, m_rect.x);
            m_rect.y = std::max(0, m_rect.y);
            m_rect.w = std::max(1, m_rect.w);
            m_rect.h = std::max(1, m_rect.h);

            const bool hasCurrentImage = ui.viewer != nullptr && ui.viewer->hasImage();
            ImGui::BeginDisabled(!hasCurrentImage);
            if (iconButton(IconVsScreenFull, "RectUseCurrentImageSize", "使用当前图片尺寸")) {
                m_rect.x = 0;
                m_rect.y = 0;
                m_rect.w = ui.viewer->imageWidth();
                m_rect.h = ui.viewer->imageHeight();
            }
            ImGui::SameLine();
            if (iconButton(IconVsSelectRange, "RectFreeSelect", "自由框选")) {
                beginCropSelect(ui, id());
            }
            ImGui::EndDisabled();

            consumeCropSelect(ui, id(), m_rect);
            ImGui::PopID();
        }

        void store(nlohmann::json& j) const override {
            j["x"] = m_rect.x;
            j["y"] = m_rect.y;
            j["w"] = m_rect.w;
            j["h"] = m_rect.h;
        }

        void load(const nlohmann::json& j) override {
            m_rect.x = j.value("x", 0);
            m_rect.y = j.value("y", 0);
            m_rect.w = j.value("w", 256);
            m_rect.h = j.value("h", 256);
        }

        void process(WorkflowWorkspace&, WorkflowRunContext&) override {
            Roi roi;
            roi.shape = RoiShape::Rect;
            roi.rect = m_rect;
            setOutput(attrId(*this, 0), roi);
        }

    private:
        Rect m_rect { 0, 0, 256, 256 };
    };

    class NodeCircleRoi : public WorkflowNode {
    public:
        NodeCircleRoi()
            : WorkflowNode("analysis.circle_roi", "ROI 圆形", {
                attr(AttributeIO::Output, ValueType::Roi, "ROI")
            }) {}

        void drawBody(WorkflowUiContext& ui) override {
            ImGui::PushID(id());
            ImGui::PushItemWidth(90.0f);
            ImGui::InputDouble("中心 X", &m_centerX, 1.0, 10.0, "%.1f");
            ImGui::InputDouble("中心 Y", &m_centerY, 1.0, 10.0, "%.1f");
            ImGui::InputDouble("半径", &m_radius, 1.0, 10.0, "%.1f");
            ImGui::PopItemWidth();
            m_radius = std::max(1.0, m_radius);

            const bool hasCurrentImage = ui.viewer != nullptr && ui.viewer->hasImage();
            ImGui::BeginDisabled(!hasCurrentImage);
            if (iconButton(IconVsSelectRange, "CircleBounds", "自由选框设置圆形外接范围"))
                beginCropSelect(ui, id());
            ImGui::SameLine();
            if (ImGui::Button("C##CircleCenter"))
                beginCenterSelect(ui, id());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", "自由选点确定中心点");
            ImGui::EndDisabled();

            Rect bounds;
            if (consumeCropSelect(ui, id(), bounds)) {
                m_centerX = bounds.x + bounds.w * 0.5;
                m_centerY = bounds.y + bounds.h * 0.5;
                m_radius = std::max(1.0, std::min(bounds.w, bounds.h) * 0.5);
            }
            consumeCenterSelect(ui, id(), m_centerX, m_centerY);
            ImGui::PopID();
        }

        void store(nlohmann::json& j) const override {
            j["centerX"] = m_centerX;
            j["centerY"] = m_centerY;
            j["radius"] = m_radius;
        }

        void load(const nlohmann::json& j) override {
            m_centerX = j.value("centerX", 128.0);
            m_centerY = j.value("centerY", 128.0);
            m_radius = j.value("radius", 64.0);
            m_radius = std::max(1.0, m_radius);
        }

        void process(WorkflowWorkspace&, WorkflowRunContext&) override {
            Roi roi;
            roi.shape = RoiShape::Circle;
            roi.center = { m_centerX, m_centerY };
            roi.radius = m_radius;
            setOutput(attrId(*this, 0), roi);
        }

    private:
        double m_centerX = 128.0;
        double m_centerY = 128.0;
        double m_radius = 64.0;
    };

    class NodeRingRoi : public WorkflowNode {
    public:
        NodeRingRoi()
            : WorkflowNode("analysis.ring_roi", "ROI 环形", {
                attr(AttributeIO::Output, ValueType::Roi, "ROI")
            }) {}

        void drawBody(WorkflowUiContext& ui) override {
            ImGui::PushID(id());
            ImGui::PushItemWidth(90.0f);
            ImGui::InputDouble("中心 X", &m_centerX, 1.0, 10.0, "%.1f");
            ImGui::InputDouble("中心 Y", &m_centerY, 1.0, 10.0, "%.1f");
            ImGui::InputDouble("内半径", &m_innerRadius, 1.0, 10.0, "%.1f");
            ImGui::InputDouble("外半径", &m_outerRadius, 1.0, 10.0, "%.1f");
            ImGui::PopItemWidth();
            m_innerRadius = std::max(0.0, m_innerRadius);
            m_outerRadius = std::max(1.0, m_outerRadius);
            if (m_innerRadius >= m_outerRadius)
                m_innerRadius = std::max(0.0, m_outerRadius - 1.0);

            const bool hasCurrentImage = ui.viewer != nullptr && ui.viewer->hasImage();
            ImGui::BeginDisabled(!hasCurrentImage);
            if (iconButton(IconVsSelectRange, "RingBounds", "自由选框设置环形外接范围"))
                beginCropSelect(ui, id());
            ImGui::SameLine();
            if (ImGui::Button("C##RingCenter"))
                beginCenterSelect(ui, id());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", "自由选点确定中心点");
            ImGui::EndDisabled();

            Rect bounds;
            if (consumeCropSelect(ui, id(), bounds)) {
                const double ratio = m_outerRadius > 0.0 ? std::clamp(m_innerRadius / m_outerRadius, 0.0, 0.95) : 0.5;
                m_centerX = bounds.x + bounds.w * 0.5;
                m_centerY = bounds.y + bounds.h * 0.5;
                m_outerRadius = std::max(1.0, std::min(bounds.w, bounds.h) * 0.5);
                m_innerRadius = std::max(0.0, m_outerRadius * ratio);
            }
            consumeCenterSelect(ui, id(), m_centerX, m_centerY);
            ImGui::PopID();
        }

        void store(nlohmann::json& j) const override {
            j["centerX"] = m_centerX;
            j["centerY"] = m_centerY;
            j["innerRadius"] = m_innerRadius;
            j["outerRadius"] = m_outerRadius;
        }

        void load(const nlohmann::json& j) override {
            m_centerX = j.value("centerX", 128.0);
            m_centerY = j.value("centerY", 128.0);
            m_innerRadius = j.value("innerRadius", 48.0);
            m_outerRadius = j.value("outerRadius", 96.0);
            m_innerRadius = std::max(0.0, m_innerRadius);
            m_outerRadius = std::max(1.0, m_outerRadius);
            if (m_innerRadius >= m_outerRadius)
                m_innerRadius = std::max(0.0, m_outerRadius - 1.0);
        }

        void process(WorkflowWorkspace&, WorkflowRunContext&) override {
            Roi roi;
            roi.shape = RoiShape::Ring;
            roi.center = { m_centerX, m_centerY };
            roi.innerRadius = m_innerRadius;
            roi.outerRadius = m_outerRadius;
            setOutput(attrId(*this, 0), roi);
        }

    private:
        double m_centerX = 128.0;
        double m_centerY = 128.0;
        double m_innerRadius = 48.0;
        double m_outerRadius = 96.0;
    };

    class NodeRoiToMask : public WorkflowNode {
    public:
        NodeRoiToMask()
            : WorkflowNode("analysis.roi_to_mask", "ROI 转掩膜", {
                attr(AttributeIO::Input, ValueType::ImageSet, "参考图片"),
                attr(AttributeIO::Input, ValueType::Roi, "ROI"),
                attr(AttributeIO::Output, ValueType::ImageSet, "掩膜")
            }) {}

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            Roi roi = requireRoi(inputValue(workspace, context, attrId(*this, 1)), *this);

            ImageSet output;
            output.items.reserve(input.items.size());
            for (auto& item : input.items) {
                if (context.isCanceled())
                    fail("执行已停止");
                cv::Mat mask = roiMask(roi, item.image.size());
                ImageItem out = item;
                out.image = std::move(mask);
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 2), std::move(output));
        }
    };

    class NodeFindContours : public WorkflowNode {
    public:
        NodeFindContours()
            : WorkflowNode("analysis.find_contours", "查找轮廓", {
                attr(AttributeIO::Input, ValueType::ImageSet, "掩膜"),
                attr(AttributeIO::Output, ValueType::RegionSet, "区域")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            ImGui::PushItemWidth(120.0f);
            static const char* modes[] = { "外部轮廓", "全部轮廓" };
            int mode = m_externalOnly ? 0 : 1;
            if (ImGui::Combo("模式", &mode, modes, 2))
                m_externalOnly = mode == 0;
            ImGui::Checkbox("简化轮廓", &m_approxSimple);
            ImGui::InputDouble("最小面积", &m_minArea, 1.0, 10.0, "%.1f");
            ImGui::InputDouble("最大面积", &m_maxArea, 1.0, 10.0, "%.1f");
            ImGui::PopItemWidth();
            m_minArea = std::max(0.0, m_minArea);
            m_maxArea = std::max(0.0, m_maxArea);
        }

        void store(nlohmann::json& j) const override {
            j["externalOnly"] = m_externalOnly;
            j["approxSimple"] = m_approxSimple;
            j["minArea"] = m_minArea;
            j["maxArea"] = m_maxArea;
        }

        void load(const nlohmann::json& j) override {
            m_externalOnly = j.value("externalOnly", true);
            m_approxSimple = j.value("approxSimple", true);
            m_minArea = j.value("minArea", 1.0);
            m_maxArea = j.value("maxArea", 0.0);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            RegionSet output;
            output.items.reserve(input.items.size());

            for (auto& item : input.items) {
                if (context.isCanceled())
                    fail("执行已停止");
                cv::Mat mask = toGrayMask(item.image);
                if (mask.empty())
                    fail("查找轮廓输入无效: " + item.fileName);

                std::vector<std::vector<cv::Point>> contours;
                cv::findContours(mask, contours, m_externalOnly ? cv::RETR_EXTERNAL : cv::RETR_LIST,
                                 m_approxSimple ? cv::CHAIN_APPROX_SIMPLE : cv::CHAIN_APPROX_NONE);

                RegionItem out;
                out.sourcePath = item.sourcePath;
                out.fileName = item.fileName;
                out.index = item.index;
                out.imageWidth = item.image.cols;
                out.imageHeight = item.image.rows;

                int label = 1;
                for (auto& contour : contours) {
                    if (contour.empty())
                        continue;
                    Region region = regionFromContour(std::move(contour), label++);
                    if (areaInRange(region.area, m_minArea, m_maxArea))
                        out.regions.push_back(std::move(region));
                }
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }

    private:
        bool m_externalOnly = true;
        bool m_approxSimple = true;
        double m_minArea = 1.0;
        double m_maxArea = 0.0;
    };

    class NodeConnectedComponents : public WorkflowNode {
    public:
        NodeConnectedComponents()
            : WorkflowNode("analysis.connected_components", "连通域分析", {
                attr(AttributeIO::Input, ValueType::ImageSet, "掩膜"),
                attr(AttributeIO::Output, ValueType::RegionSet, "区域")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            ImGui::PushItemWidth(120.0f);
            static const char* connectivity[] = { "4 邻域", "8 邻域" };
            int current = m_connectivity == 4 ? 0 : 1;
            if (ImGui::Combo("连通性", &current, connectivity, 2))
                m_connectivity = current == 0 ? 4 : 8;
            ImGui::InputDouble("最小面积", &m_minArea, 1.0, 10.0, "%.1f");
            ImGui::InputDouble("最大面积", &m_maxArea, 1.0, 10.0, "%.1f");
            ImGui::PopItemWidth();
            m_minArea = std::max(0.0, m_minArea);
            m_maxArea = std::max(0.0, m_maxArea);
        }

        void store(nlohmann::json& j) const override {
            j["connectivity"] = m_connectivity;
            j["minArea"] = m_minArea;
            j["maxArea"] = m_maxArea;
        }

        void load(const nlohmann::json& j) override {
            m_connectivity = j.value("connectivity", 8);
            if (m_connectivity != 4)
                m_connectivity = 8;
            m_minArea = j.value("minArea", 1.0);
            m_maxArea = j.value("maxArea", 0.0);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            RegionSet output;
            output.items.reserve(input.items.size());

            for (auto& item : input.items) {
                if (context.isCanceled())
                    fail("执行已停止");
                cv::Mat mask = toGrayMask(item.image);
                if (mask.empty())
                    fail("连通域输入无效: " + item.fileName);

                cv::Mat labels;
                cv::Mat stats;
                cv::Mat centroids;
                int count = cv::connectedComponentsWithStats(mask, labels, stats, centroids, m_connectivity, CV_32S);

                RegionItem out;
                out.sourcePath = item.sourcePath;
                out.fileName = item.fileName;
                out.index = item.index;
                out.imageWidth = item.image.cols;
                out.imageHeight = item.image.rows;

                for (int label = 1; label < count; ++label) {
                    const double area = double(stats.at<int>(label, cv::CC_STAT_AREA));
                    if (!areaInRange(area, m_minArea, m_maxArea))
                        continue;

                    cv::Mat componentMask = labels == label;
                    std::vector<std::vector<cv::Point>> contours;
                    cv::findContours(componentMask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
                    if (contours.empty())
                        continue;

                    auto largest = std::max_element(contours.begin(), contours.end(), [](const auto& a, const auto& b) {
                        return std::abs(cv::contourArea(a)) < std::abs(cv::contourArea(b));
                    });

                    Region region = regionFromContour(std::move(*largest), label);
                    region.area = area;
                    region.bbox = {
                        stats.at<int>(label, cv::CC_STAT_LEFT),
                        stats.at<int>(label, cv::CC_STAT_TOP),
                        stats.at<int>(label, cv::CC_STAT_WIDTH),
                        stats.at<int>(label, cv::CC_STAT_HEIGHT)
                    };
                    region.centroid = {
                        centroids.at<double>(label, 0),
                        centroids.at<double>(label, 1)
                    };
                    out.regions.push_back(std::move(region));
                }
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }

    private:
        int m_connectivity = 8;
        double m_minArea = 1.0;
        double m_maxArea = 0.0;
    };

    class NodeFilterRegionsByArea : public WorkflowNode {
    public:
        NodeFilterRegionsByArea()
            : WorkflowNode("analysis.filter_regions_by_area", "按面积过滤区域", {
                attr(AttributeIO::Input, ValueType::RegionSet, "区域"),
                attr(AttributeIO::Output, ValueType::RegionSet, "区域")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            ImGui::PushItemWidth(120.0f);
            ImGui::InputDouble("最小面积", &m_minArea, 1.0, 10.0, "%.1f");
            ImGui::InputDouble("最大面积", &m_maxArea, 1.0, 10.0, "%.1f");
            ImGui::PopItemWidth();
            m_minArea = std::max(0.0, m_minArea);
            m_maxArea = std::max(0.0, m_maxArea);
        }

        void store(nlohmann::json& j) const override {
            j["minArea"] = m_minArea;
            j["maxArea"] = m_maxArea;
        }

        void load(const nlohmann::json& j) override {
            m_minArea = j.value("minArea", 1.0);
            m_maxArea = j.value("maxArea", 0.0);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            RegionSet input = requireRegionSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            RegionSet output;
            output.items.reserve(input.items.size());
            for (auto& item : input.items) {
                if (context.isCanceled())
                    fail("执行已停止");
                RegionItem out = item;
                out.regions.clear();
                for (const auto& region : item.regions) {
                    if (areaInRange(region.area, m_minArea, m_maxArea))
                        out.regions.push_back(region);
                }
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }

    private:
        double m_minArea = 1.0;
        double m_maxArea = 0.0;
    };

    class NodeRegionsToMask : public WorkflowNode {
    public:
        NodeRegionsToMask()
            : WorkflowNode("analysis.regions_to_mask", "区域转掩膜", {
                attr(AttributeIO::Input, ValueType::RegionSet, "区域"),
                attr(AttributeIO::Output, ValueType::ImageSet, "掩膜")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            ImGui::Checkbox("填充区域", &m_fill);
        }

        void store(nlohmann::json& j) const override {
            j["fill"] = m_fill;
        }

        void load(const nlohmann::json& j) override {
            m_fill = j.value("fill", true);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            RegionSet input = requireRegionSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            ImageSet output;
            output.items.reserve(input.items.size());
            for (auto& item : input.items) {
                if (context.isCanceled())
                    fail("执行已停止");

                cv::Mat mask = cv::Mat::zeros(std::max(1, item.imageHeight), std::max(1, item.imageWidth), CV_8UC1);
                for (const auto& region : item.regions) {
                    if (!region.contour.empty()) {
                        std::vector<std::vector<cv::Point>> contours = { region.contour };
                        cv::drawContours(mask, contours, -1, cv::Scalar(255), m_fill ? cv::FILLED : 1);
                    } else {
                        cv::rectangle(mask, toCvRect(region.bbox), cv::Scalar(255), m_fill ? cv::FILLED : 1);
                    }
                }

                ImageItem out;
                out.image = std::move(mask);
                out.sourcePath = item.sourcePath;
                out.fileName = item.fileName;
                out.index = item.index;
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }

    private:
        bool m_fill = true;
    };

    class NodeDrawRegions : public WorkflowNode {
    public:
        NodeDrawRegions()
            : WorkflowNode("analysis.draw_regions", "绘制区域标注", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Input, ValueType::RegionSet, "区域"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            ImGui::Checkbox("轮廓", &m_drawContour);
            ImGui::SameLine();
            ImGui::Checkbox("外接框", &m_drawBox);
            ImGui::Checkbox("中心点", &m_drawCenter);
            ImGui::SameLine();
            ImGui::Checkbox("标签", &m_drawLabel);
            ImGui::PushItemWidth(100.0f);
            ImGui::InputInt("线宽", &m_thickness);
            ImGui::ColorEdit3("颜色", m_color, ImGuiColorEditFlags_NoInputs);
            ImGui::PopItemWidth();
            m_thickness = std::clamp(m_thickness, 1, 20);
        }

        void store(nlohmann::json& j) const override {
            j["drawContour"] = m_drawContour;
            j["drawBox"] = m_drawBox;
            j["drawCenter"] = m_drawCenter;
            j["drawLabel"] = m_drawLabel;
            j["thickness"] = m_thickness;
            j["color"] = { m_color[0], m_color[1], m_color[2] };
        }

        void load(const nlohmann::json& j) override {
            m_drawContour = j.value("drawContour", true);
            m_drawBox = j.value("drawBox", true);
            m_drawCenter = j.value("drawCenter", true);
            m_drawLabel = j.value("drawLabel", false);
            m_thickness = j.value("thickness", 2);
            if (j.contains("color") && j["color"].is_array() && j["color"].size() >= 3) {
                m_color[0] = j["color"][0].get<float>();
                m_color[1] = j["color"][1].get<float>();
                m_color[2] = j["color"][2].get<float>();
            }
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet images = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            RegionSet regions = requireRegionSet(inputValue(workspace, context, attrId(*this, 1)), *this);
            if (images.items.size() != regions.items.size())
                fail("图片和区域数量不一致");

            cv::Scalar color(
                std::clamp(m_color[2], 0.0f, 1.0f) * 255.0,
                std::clamp(m_color[1], 0.0f, 1.0f) * 255.0,
                std::clamp(m_color[0], 0.0f, 1.0f) * 255.0
            );

            ImageSet output;
            output.items.reserve(images.items.size());
            for (size_t i = 0; i < images.items.size(); ++i) {
                if (context.isCanceled())
                    fail("执行已停止");
                ImageItem out = images.items[i];
                out.image = toBgrImage(images.items[i].image);
                if (out.image.empty())
                    fail("绘制标注输入无效: " + images.items[i].fileName);

                for (const auto& region : regions.items[i].regions) {
                    if (m_drawContour && !region.contour.empty()) {
                        std::vector<std::vector<cv::Point>> contours = { region.contour };
                        cv::drawContours(out.image, contours, -1, color, m_thickness);
                    }
                    if (m_drawBox)
                        cv::rectangle(out.image, toCvRect(region.bbox), color, m_thickness);
                    if (m_drawCenter)
                        cv::circle(out.image, cv::Point(int(std::round(region.centroid.x)), int(std::round(region.centroid.y))),
                                   std::max(2, m_thickness + 1), color, cv::FILLED);
                    if (m_drawLabel) {
                        char text[96];
                        std::snprintf(text, sizeof(text), "#%d %.0f", region.label, region.area);
                        cv::putText(out.image, text, cv::Point(region.bbox.x, std::max(12, region.bbox.y - 4)),
                                    cv::FONT_HERSHEY_SIMPLEX, 0.45, color, std::max(1, m_thickness));
                    }
                }
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 2), std::move(output));
        }

    private:
        bool m_drawContour = true;
        bool m_drawBox = true;
        bool m_drawCenter = true;
        bool m_drawLabel = false;
        int m_thickness = 2;
        float m_color[3] = { 1.0f, 0.15f, 0.1f };
    };

}

    void registerAnalysisNodes() {
        addRegistryEntry("检测分析/ROI", "矩形", [] { return std::make_unique<NodeRect>(); });
        addRegistryEntry("检测分析/ROI", "圆形", [] { return std::make_unique<NodeCircleRoi>(); });
        addRegistryEntry("检测分析/ROI", "环形", [] { return std::make_unique<NodeRingRoi>(); });
        addRegistryEntry("检测分析/ROI", "转掩膜", [] { return std::make_unique<NodeRoiToMask>(); });
        addRegistryEntry("检测分析", "查找轮廓", [] { return std::make_unique<NodeFindContours>(); });
        addRegistryEntry("检测分析", "连通域分析", [] { return std::make_unique<NodeConnectedComponents>(); });
        addRegistryEntry("检测分析", "按面积过滤区域", [] { return std::make_unique<NodeFilterRegionsByArea>(); });
        addRegistryEntry("检测分析", "区域转掩膜", [] { return std::make_unique<NodeRegionsToMask>(); });
        addRegistryEntry("检测分析", "绘制区域标注", [] { return std::make_unique<NodeDrawRegions>(); });
    }

}
