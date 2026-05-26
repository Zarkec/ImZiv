#include "common.hpp"

namespace imziv::workflow {
namespace {

    class NodeCanny : public WorkflowNode {
    public:
        NodeCanny()
            : WorkflowNode("process.canny", "Canny 边缘检测", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            ImGui::PushItemWidth(120.0f);
            ImGui::InputDouble("低阈值", &m_threshold1, 1.0, 10.0, "%.1f");
            ImGui::InputDouble("高阈值", &m_threshold2, 1.0, 10.0, "%.1f");
            ImGui::InputInt("孔径大小", &m_apertureSize, 2);
            ImGui::PopItemWidth();
            m_threshold1 = std::max(0.0, m_threshold1);
            m_threshold2 = std::max(0.0, m_threshold2);
            m_apertureSize = std::max(1, m_apertureSize | 1);
        }

        void store(nlohmann::json& j) const override {
            j["threshold1"] = m_threshold1;
            j["threshold2"] = m_threshold2;
            j["apertureSize"] = m_apertureSize;
        }

        void load(const nlohmann::json& j) override {
            m_threshold1 = j.value("threshold1", 50.0);
            m_threshold2 = j.value("threshold2", 150.0);
            m_apertureSize = j.value("apertureSize", 3);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            ImageSet output;
            output.items.reserve(input.items.size());
            for (auto& item : input.items) {
                if (context.isCanceled()) fail("执行已停止");
                if (item.image.empty()) fail("Canny 边缘检测输入无效: " + item.fileName);

                cv::Mat gray;
                if (item.image.channels() == 1) gray = item.image;
                else if (item.image.channels() == 3) cv::cvtColor(item.image, gray, cv::COLOR_BGR2GRAY);
                else if (item.image.channels() == 4) cv::cvtColor(item.image, gray, cv::COLOR_BGRA2GRAY);
                else fail("不支持的通道数: " + item.fileName);

                cv::Mat result;
                cv::Canny(gray, result, m_threshold1, m_threshold2, m_apertureSize);
                ImageItem out = item;
                out.image = std::move(result);
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }

    private:
        double m_threshold1 = 50.0;
        double m_threshold2 = 150.0;
        int m_apertureSize = 3;
    };

    class NodeSobel : public WorkflowNode {
    public:
        NodeSobel()
            : WorkflowNode("process.sobel", "Sobel 算子", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            static const char* directions[] = { "X 方向", "Y 方向", "XY 方向" };
            int dirIdx = 0;
            for (int i = 0; i < int(sizeof(directions) / sizeof(directions[0])); ++i)
                if (m_direction == directions[i]) dirIdx = i;

            ImGui::PushItemWidth(120.0f);
            if (ImGui::Combo("方向", &dirIdx, directions, int(sizeof(directions) / sizeof(directions[0]))))
                m_direction = directions[dirIdx];
            ImGui::InputInt("核大小", &m_ksize, 2);
            ImGui::PopItemWidth();
            m_ksize = std::max(1, m_ksize | 1);
        }

        void store(nlohmann::json& j) const override {
            j["direction"] = m_direction;
            j["ksize"] = m_ksize;
        }

        void load(const nlohmann::json& j) override {
            m_direction = j.value("direction", "XY 方向");
            m_ksize = j.value("ksize", 3);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            ImageSet output;
            output.items.reserve(input.items.size());

            int dx = (m_direction == "X 方向" || m_direction == "XY 方向") ? 1 : 0;
            int dy = (m_direction == "Y 方向" || m_direction == "XY 方向") ? 1 : 0;

            for (auto& item : input.items) {
                if (context.isCanceled()) fail("执行已停止");
                if (item.image.empty()) fail("Sobel 算子输入无效: " + item.fileName);

                cv::Mat gray;
                if (item.image.channels() == 1) gray = item.image;
                else if (item.image.channels() == 3) cv::cvtColor(item.image, gray, cv::COLOR_BGR2GRAY);
                else if (item.image.channels() == 4) cv::cvtColor(item.image, gray, cv::COLOR_BGRA2GRAY);
                else fail("不支持的通道数: " + item.fileName);

                cv::Mat result;
                cv::Sobel(gray, result, CV_8U, dx, dy, m_ksize);
                ImageItem out = item;
                out.image = std::move(result);
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }

    private:
        std::string m_direction = "XY 方向";
        int m_ksize = 3;
    };

    class NodeLaplacian : public WorkflowNode {
    public:
        NodeLaplacian()
            : WorkflowNode("process.laplacian", "Laplacian 算子", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            ImGui::PushItemWidth(120.0f);
            ImGui::InputInt("核大小", &m_ksize, 2);
            ImGui::PopItemWidth();
            m_ksize = std::max(1, m_ksize | 1);
        }

        void store(nlohmann::json& j) const override {
            j["ksize"] = m_ksize;
        }

        void load(const nlohmann::json& j) override {
            m_ksize = j.value("ksize", 3);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            ImageSet output;
            output.items.reserve(input.items.size());
            for (auto& item : input.items) {
                if (context.isCanceled()) fail("执行已停止");
                if (item.image.empty()) fail("Laplacian 算子输入无效: " + item.fileName);

                cv::Mat gray;
                if (item.image.channels() == 1) gray = item.image;
                else if (item.image.channels() == 3) cv::cvtColor(item.image, gray, cv::COLOR_BGR2GRAY);
                else if (item.image.channels() == 4) cv::cvtColor(item.image, gray, cv::COLOR_BGRA2GRAY);
                else fail("不支持的通道数: " + item.fileName);

                cv::Mat result;
                cv::Laplacian(gray, result, CV_8U, m_ksize);
                ImageItem out = item;
                out.image = std::move(result);
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }

    private:
        int m_ksize = 3;
    };

}

    void registerEdgeNodes() {
        addRegistryEntry("边缘检测", "Canny 边缘检测", [] { return std::make_unique<NodeCanny>(); });
        addRegistryEntry("边缘检测", "Sobel 算子", [] { return std::make_unique<NodeSobel>(); });
        addRegistryEntry("边缘检测", "Laplacian 算子", [] { return std::make_unique<NodeLaplacian>(); });
    }

}
