#include "common.hpp"

namespace imziv::workflow {
namespace {

    class NodeCrop : public WorkflowNode {
    public:
        NodeCrop()
            : WorkflowNode("process.crop", "裁切", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Input, ValueType::Roi, "ROI"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            drawFillOptions(m_fill);
        }

        void store(nlohmann::json& j) const override {
            storeFillOptions(j, m_fill);
        }

        void load(const nlohmann::json& j) override {
            loadFillOptions(j, m_fill);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            Roi roi = requireRoi(inputValue(workspace, context, attrId(*this, 1)), *this);
            cv::Rect bounds = roiBounds(roi);
            if (bounds.width <= 0 || bounds.height <= 0)
                fail("ROI 范围无效");

            ImageSet output;
            output.items.reserve(input.items.size());
            for (auto& item : input.items) {
                if (context.isCanceled())
                    fail("执行已停止");
                if (bounds.x < 0 || bounds.y < 0 || bounds.x + bounds.width > item.image.cols ||
                    bounds.y + bounds.height > item.image.rows)
                    fail("ROI 超出图片: " + item.fileName);

                ImageItem out = item;
                cv::Mat cropped = item.image(bounds).clone();
                if (roi.shape == RoiShape::Rect) {
                    out.image = std::move(cropped);
                } else {
                    cv::Mat mask = roiMask(roi, bounds.size(), bounds.tl());
                    cv::Mat result = applyMaskFill(cropped, mask, m_fill);
                    if (result.empty())
                        fail("裁切输入通道无效: " + item.fileName);
                    out.image = std::move(result);
                }
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 2), std::move(output));
        }

    private:
        FillOptions m_fill;
    };

    class NodeResize : public WorkflowNode {
        int m_width = 0;
        int m_height = 0;
        float m_scale = 1.0f;
        std::string m_interpolation = "线性";
    public:
        NodeResize()
            : WorkflowNode("process.resize", "缩放", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            ImGui::PushItemWidth(120.0f);
            ImGui::InputInt("宽度", &m_width);
            ImGui::InputInt("高度", &m_height);
            sliderInputFloat("比例", &m_scale, 0.01f, 10.0f, 0.1f);
            static const char* items[] = { "线性", "最近邻", "面积", "三次" };
            int current = 0;
            for (int i = 0; i < int(sizeof(items) / sizeof(items[0])); ++i) {
                if (m_interpolation == items[i]) current = i;
            }
            if (ImGui::Combo("插值", &current, items, int(sizeof(items) / sizeof(items[0]))))
                m_interpolation = items[current];
            ImGui::PopItemWidth();
            m_width = std::max(0, m_width);
            m_height = std::max(0, m_height);
        }

        void store(nlohmann::json& j) const override {
            j["width"] = m_width;
            j["height"] = m_height;
            j["scale"] = m_scale;
            j["interpolation"] = m_interpolation;
        }

        void load(const nlohmann::json& j) override {
            m_width = j.value("width", 0);
            m_height = j.value("height", 0);
            m_scale = j.value("scale", 1.0f);
            m_interpolation = j.value("interpolation", "线性");
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            int width = std::max(0, m_width);
            int height = std::max(0, m_height);
            double scale = std::max(0.0, double(m_scale));
            ImageSet output;
            output.items.reserve(input.items.size());

            for (auto& item : input.items) {
                if (context.isCanceled())
                    fail("执行已停止");
                if (item.image.empty())
                    fail("缩放输入无效: " + item.fileName);

                int outWidth = width;
                int outHeight = height;
                if (outWidth <= 0 && outHeight <= 0) {
                    if (scale <= 0.0)
                        fail("缩放比例必须大于 0");
                    outWidth = std::max(1, int(std::round(item.image.cols * scale)));
                    outHeight = std::max(1, int(std::round(item.image.rows * scale)));
                } else if (outWidth <= 0) {
                    outWidth = std::max(1, int(std::round(item.image.cols * (double(outHeight) / item.image.rows))));
                } else if (outHeight <= 0) {
                    outHeight = std::max(1, int(std::round(item.image.rows * (double(outWidth) / item.image.cols))));
                }

                cv::Mat result;
                cv::resize(item.image, result, cv::Size(outWidth, outHeight), 0.0, 0.0, interpolationFlag(m_interpolation));
                ImageItem out = item;
                out.image = std::move(result);
                output.items.push_back(std::move(out));
            }

            setOutput(attrId(*this, 1), std::move(output));
        }
    };

    class NodeRotate : public WorkflowNode {
        float m_angle = 90.0f;
        bool m_expandCanvas = true;
        std::string m_interpolation = "线性";
        FillOptions m_fill;
    public:
        NodeRotate()
            : WorkflowNode("process.rotate", "旋转", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            ImGui::PushItemWidth(120.0f);
            sliderInputFloat("角度", &m_angle, -360.0f, 360.0f, 1.0f);
            ImGui::Checkbox("扩展画布", &m_expandCanvas);

            static const char* items[] = { "线性", "最近邻", "面积", "三次" };
            int current = 0;
            for (int i = 0; i < int(sizeof(items) / sizeof(items[0])); ++i) {
                if (m_interpolation == items[i])
                    current = i;
            }

            if (ImGui::Combo("插值", &current, items, int(sizeof(items) / sizeof(items[0]))))
                m_interpolation = items[current];
            ImGui::PopItemWidth();
            drawFillOptions(m_fill);
        }

        void store(nlohmann::json& j) const override {
            j["angle"] = m_angle;
            j["expandCanvas"] = m_expandCanvas;
            j["interpolation"] = m_interpolation;
            storeFillOptions(j, m_fill);
        }

        void load(const nlohmann::json& j) override {
            m_angle = j.value("angle", 90.0f);
            m_expandCanvas = j.value("expandCanvas", true);
            m_interpolation = j.value("interpolation", "线性");
            loadFillOptions(j, m_fill);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            double angle = double(m_angle);
            ImageSet output;
            output.items.reserve(input.items.size());

            for (auto& item : input.items) {
                if (context.isCanceled())
                    fail("执行已停止");
                if (item.image.empty())
                    fail("旋转输入无效: " + item.fileName);

                cv::Point2f center((item.image.cols - 1) * 0.5f, (item.image.rows - 1) * 0.5f);
                cv::Mat matrix = cv::getRotationMatrix2D(center, angle, 1.0);
                cv::Size outSize(item.image.cols, item.image.rows);

                if (m_expandCanvas) {
                    const double radians = angle * CV_PI / 180.0;
                    const double absCos = std::abs(std::cos(radians));
                    const double absSin = std::abs(std::sin(radians));
                    outSize.width = std::max(1, int(std::round(item.image.cols * absCos + item.image.rows * absSin)));
                    outSize.height = std::max(1, int(std::round(item.image.cols * absSin + item.image.rows * absCos)));
                    matrix.at<double>(0, 2) += outSize.width * 0.5 - center.x;
                    matrix.at<double>(1, 2) += outSize.height * 0.5 - center.y;
                }

                cv::Mat result;
                cv::warpAffine(item.image, result, matrix, outSize, interpolationFlag(m_interpolation),
                               cv::BORDER_CONSTANT, fillScalarForImage(m_fill, item.image));
                ImageItem out = item;
                out.image = std::move(result);
                output.items.push_back(std::move(out));
            }

            setOutput(attrId(*this, 1), std::move(output));
        }
    };

    class NodeFlip : public WorkflowNode {
    public:
        NodeFlip()
            : WorkflowNode("process.flip", "翻转", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            static const char* items[] = { "水平", "垂直", "水平+垂直" };
            int current = 0;
            for (int i = 0; i < int(sizeof(items) / sizeof(items[0])); ++i) {
                if (m_direction == items[i])
                    current = i;
            }

            ImGui::PushItemWidth(120.0f);
            if (ImGui::Combo("方向", &current, items, int(sizeof(items) / sizeof(items[0]))))
                m_direction = items[current];
            ImGui::PopItemWidth();
        }

        void store(nlohmann::json& j) const override {
            j["direction"] = m_direction;
        }

        void load(const nlohmann::json& j) override {
            m_direction = j.value("direction", "水平");
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            ImageSet output;
            output.items.reserve(input.items.size());

            int flipCode = 1;
            if (m_direction == "垂直")
                flipCode = 0;
            else if (m_direction == "水平+垂直")
                flipCode = -1;

            for (auto& item : input.items) {
                if (context.isCanceled())
                    fail("执行已停止");
                if (item.image.empty())
                    fail("翻转输入无效: " + item.fileName);
                cv::Mat result;
                cv::flip(item.image, result, flipCode);
                ImageItem out = item;
                out.image = std::move(result);
                output.items.push_back(std::move(out));
            }

            setOutput(attrId(*this, 1), std::move(output));
        }

    private:
        std::string m_direction = "水平";
    };

    class NodeTranslate : public WorkflowNode {
        int m_x = 0;
        int m_y = 0;
        FillOptions m_fill;
    public:
        NodeTranslate()
            : WorkflowNode("process.translate", "平移", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            sliderInputInt("X", &m_x, -2000, 2000);
            sliderInputInt("Y", &m_y, -2000, 2000);
            drawFillOptions(m_fill);
        }

        void store(nlohmann::json& j) const override {
            j["x"] = m_x;
            j["y"] = m_y;
            storeFillOptions(j, m_fill);
        }

        void load(const nlohmann::json& j) override {
            m_x = j.value("x", 0);
            m_y = j.value("y", 0);
            loadFillOptions(j, m_fill);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            cv::Mat matrix = (cv::Mat_<double>(2, 3) << 1.0, 0.0, double(m_x), 0.0, 1.0, double(m_y));
            ImageSet output;
            output.items.reserve(input.items.size());

            for (auto& item : input.items) {
                if (context.isCanceled())
                    fail("执行已停止");
                if (item.image.empty())
                    fail("平移输入无效: " + item.fileName);
                cv::Mat result;
                cv::warpAffine(item.image, result, matrix, item.image.size(), cv::INTER_LINEAR,
                               cv::BORDER_CONSTANT, fillScalarForImage(m_fill, item.image));
                ImageItem out = item;
                out.image = std::move(result);
                output.items.push_back(std::move(out));
            }

            setOutput(attrId(*this, 1), std::move(output));
        }
    };

}

    void registerGeometryNodes() {
        addRegistryEntry("几何变换", "裁切", [] { return std::make_unique<NodeCrop>(); });
        addRegistryEntry("几何变换", "缩放", [] { return std::make_unique<NodeResize>(); });
        addRegistryEntry("几何变换", "旋转", [] { return std::make_unique<NodeRotate>(); });
        addRegistryEntry("几何变换", "翻转", [] { return std::make_unique<NodeFlip>(); });
        addRegistryEntry("几何变换", "平移", [] { return std::make_unique<NodeTranslate>(); });
    }

}
