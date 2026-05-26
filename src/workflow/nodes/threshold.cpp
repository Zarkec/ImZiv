#include "common.hpp"

namespace imziv::workflow {
namespace {

    class NodeHsvRangeThreshold : public WorkflowNode {
    public:
        NodeHsvRangeThreshold()
            : WorkflowNode("process.hsv_range_threshold", "HSV 范围阈值", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "掩膜")
            }) {}

        void drawBody(WorkflowUiContext& ui) override {
            ImGui::Checkbox("输入已是 HSV", &m_inputIsHsv);

            int toleranceValues[3] = { m_hTolerance, m_sTolerance, m_vTolerance };
            int minValues[3] = { m_hMin, m_sMin, m_vMin };
            int maxValues[3] = { m_hMax, m_sMax, m_vMax };

            ImGui::PushItemWidth(160.0f);
            if (ImGui::InputInt3("HSV 容差", toleranceValues)) {
                m_hTolerance = toleranceValues[0];
                m_sTolerance = toleranceValues[1];
                m_vTolerance = toleranceValues[2];
            }
            if (ImGui::InputInt3("HSV 下限", minValues)) {
                m_hMin = minValues[0];
                m_sMin = minValues[1];
                m_vMin = minValues[2];
            }
            if (ImGui::InputInt3("HSV 上限", maxValues)) {
                m_hMax = maxValues[0];
                m_sMax = maxValues[1];
                m_vMax = maxValues[2];
            }
            ImGui::PopItemWidth();

            normalizeRange(m_hTolerance, m_hTolerance, 0, 179);
            normalizeRange(m_sTolerance, m_sTolerance, 0, 255);
            normalizeRange(m_vTolerance, m_vTolerance, 0, 255);
            normalizeRange(m_hMin, m_hMax, 0, 179);
            normalizeRange(m_sMin, m_sMax, 0, 255);
            normalizeRange(m_vMin, m_vMax, 0, 255);

            if (!ui.viewer)
                return;

            if (ui.viewer->hsvSampleApplyRequested) {
                applySamples(ui.viewer->hsvSamples());
                ui.viewer->hsvSampleApplyRequested = false;
            }

            ImGui::Text("采样点: %d", (int)ui.viewer->hsvSamples().size());
            if (ImGui::Button(ui.viewer->hsvSampleMode ? "重新选点" : "开始选点")) {
                ui.viewer->clearHsvSamples();
                ui.viewer->hsvSampleApplyRequested = false;
                loadInteractionImage(ui, *this, attrId(*this, 0));
                ui.viewer->setTool(ImageCanvas::Tool::HsvSample);
            }
        }

        void store(nlohmann::json& j) const override {
            j["inputIsHsv"] = m_inputIsHsv;
            j["hMin"] = m_hMin;
            j["hMax"] = m_hMax;
            j["sMin"] = m_sMin;
            j["sMax"] = m_sMax;
            j["vMin"] = m_vMin;
            j["vMax"] = m_vMax;
            j["hTolerance"] = m_hTolerance;
            j["sTolerance"] = m_sTolerance;
            j["vTolerance"] = m_vTolerance;
        }

        void load(const nlohmann::json& j) override {
            m_inputIsHsv = j.value("inputIsHsv", false);
            m_hMin = j.value("hMin", 0);
            m_hMax = j.value("hMax", 179);
            m_sMin = j.value("sMin", 0);
            m_sMax = j.value("sMax", 255);
            m_vMin = j.value("vMin", 0);
            m_vMax = j.value("vMax", 255);
            m_hTolerance = j.value("hTolerance", 5);
            m_sTolerance = j.value("sTolerance", 30);
            m_vTolerance = j.value("vTolerance", 30);
            normalizeRange(m_hTolerance, m_hTolerance, 0, 179);
            normalizeRange(m_sTolerance, m_sTolerance, 0, 255);
            normalizeRange(m_vTolerance, m_vTolerance, 0, 255);
            normalizeRange(m_hMin, m_hMax, 0, 179);
            normalizeRange(m_sMin, m_sMax, 0, 255);
            normalizeRange(m_vMin, m_vMax, 0, 255);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            ImageSet output;
            output.items.reserve(input.items.size());

            for (auto& item : input.items) {
                if (context.isCanceled())
                    fail("执行已停止");

                cv::Mat hsv = toHsvImage(item.image, m_inputIsHsv);
                if (hsv.empty())
                    fail("HSV 阈值输入无效: " + item.fileName);

                cv::Mat result;
                cv::inRange(hsv, cv::Scalar(m_hMin, m_sMin, m_vMin),
                            cv::Scalar(m_hMax, m_sMax, m_vMax), result);
                ImageItem out = item;
                out.image = std::move(result);
                output.items.push_back(std::move(out));
            }

            setOutput(attrId(*this, 1), std::move(output));
        }

    private:
        bool m_inputIsHsv = false;
        int m_hMin = 0;
        int m_hMax = 179;
        int m_sMin = 0;
        int m_sMax = 255;
        int m_vMin = 0;
        int m_vMax = 255;
        int m_hTolerance = 5;
        int m_sTolerance = 30;
        int m_vTolerance = 30;

        void applySamples(const std::vector<ImageCanvas::HsvSample>& samples) {
            if (samples.empty())
                return;

            int hMin = samples.front().h;
            int hMax = samples.front().h;
            int sMin = samples.front().s;
            int sMax = samples.front().s;
            int vMin = samples.front().v;
            int vMax = samples.front().v;

            for (const auto& sample : samples) {
                hMin = std::min(hMin, sample.h);
                hMax = std::max(hMax, sample.h);
                sMin = std::min(sMin, sample.s);
                sMax = std::max(sMax, sample.s);
                vMin = std::min(vMin, sample.v);
                vMax = std::max(vMax, sample.v);
            }

            m_hMin = hMin - m_hTolerance;
            m_hMax = hMax + m_hTolerance;
            m_sMin = sMin - m_sTolerance;
            m_sMax = sMax + m_sTolerance;
            m_vMin = vMin - m_vTolerance;
            m_vMax = vMax + m_vTolerance;
            normalizeRange(m_hMin, m_hMax, 0, 179);
            normalizeRange(m_sMin, m_sMax, 0, 255);
            normalizeRange(m_vMin, m_vMax, 0, 255);
        }
    };

    class NodeThreshold : public WorkflowNode {
        int m_threshold = 128;
    public:
        NodeThreshold()
            : WorkflowNode("process.threshold", "阈值二值化", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            sliderInputInt("阈值", &m_threshold, 0, 255);
        }

        void store(nlohmann::json& j) const override {
            j["threshold"] = m_threshold;
        }

        void load(const nlohmann::json& j) override {
            m_threshold = j.value("threshold", 128);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            int threshold = std::clamp(m_threshold, 0, 255);
            ImageSet output;
            output.items.reserve(input.items.size());
            for (auto& item : input.items) {
                cv::Mat gray;
                if (item.image.channels() == 1) gray = item.image;
                else if (item.image.channels() == 3) cv::cvtColor(item.image, gray, cv::COLOR_BGR2GRAY);
                else if (item.image.channels() == 4) cv::cvtColor(item.image, gray, cv::COLOR_BGRA2GRAY);
                else fail("不支持的通道数: " + item.fileName);

                cv::Mat result;
                cv::threshold(gray, result, threshold, 255, cv::THRESH_BINARY);
                ImageItem out = item;
                out.image = std::move(result);
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }
    };

    class NodeOtsu : public WorkflowNode {
    public:
        NodeOtsu()
            : WorkflowNode("process.otsu", "Otsu 二值化", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            ImageSet output;
            output.items.reserve(input.items.size());
            for (auto& item : input.items) {
                cv::Mat gray;
                if (item.image.channels() == 1) gray = item.image;
                else if (item.image.channels() == 3) cv::cvtColor(item.image, gray, cv::COLOR_BGR2GRAY);
                else if (item.image.channels() == 4) cv::cvtColor(item.image, gray, cv::COLOR_BGRA2GRAY);
                else fail("不支持的通道数: " + item.fileName);

                cv::Mat result;
                cv::threshold(gray, result, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
                ImageItem out = item;
                out.image = std::move(result);
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }
    };

    class NodeAdaptiveThreshold : public WorkflowNode {
        int m_blockSize = 31;
        int m_c = 5;
    public:
        NodeAdaptiveThreshold()
            : WorkflowNode("process.adaptive_threshold", "自适应二值化", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            sliderInputInt("块大小", &m_blockSize, 3, 99, 2);
            sliderInputInt("C", &m_c, -50, 50);
            if (m_blockSize % 2 == 0) m_blockSize++;
        }

        void store(nlohmann::json& j) const override {
            j["blockSize"] = m_blockSize;
            j["c"] = m_c;
        }

        void load(const nlohmann::json& j) override {
            m_blockSize = j.value("blockSize", 31);
            m_c = j.value("c", 5);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            int block = std::max(3, m_blockSize);
            if (block % 2 == 0) block++;
            int c = m_c;

            ImageSet output;
            output.items.reserve(input.items.size());
            for (auto& item : input.items) {
                cv::Mat gray;
                if (item.image.channels() == 1) gray = item.image;
                else if (item.image.channels() == 3) cv::cvtColor(item.image, gray, cv::COLOR_BGR2GRAY);
                else if (item.image.channels() == 4) cv::cvtColor(item.image, gray, cv::COLOR_BGRA2GRAY);
                else fail("不支持的通道数: " + item.fileName);

                cv::Mat result;
                cv::adaptiveThreshold(gray, result, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                                      cv::THRESH_BINARY, block, c);
                ImageItem out = item;
                out.image = std::move(result);
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }
    };

}

    void registerThresholdNodes() {
        addRegistryEntry("阈值分割", "HSV 范围阈值", [] { return std::make_unique<NodeHsvRangeThreshold>(); });
        addRegistryEntry("阈值分割", "阈值二值化", [] { return std::make_unique<NodeThreshold>(); });
        addRegistryEntry("阈值分割", "Otsu 二值化", [] { return std::make_unique<NodeOtsu>(); });
        addRegistryEntry("阈值分割", "自适应二值化", [] { return std::make_unique<NodeAdaptiveThreshold>(); });
    }

}
