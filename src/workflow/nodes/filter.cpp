#include "common.hpp"

namespace imziv::workflow {
namespace {

    class NodeMorphology : public WorkflowNode {
    public:
        NodeMorphology()
            : WorkflowNode("process.morphology", "形态学操作", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            static const char* ops[] = { "腐蚀", "膨胀", "开运算", "闭运算", "形态学梯度", "顶帽", "黑帽" };
            static const char* shapes[] = { "矩形", "椭圆", "十字" };

            int opIdx = 0;
            for (int i = 0; i < int(sizeof(ops) / sizeof(ops[0])); ++i)
                if (m_operation == ops[i]) opIdx = i;

            int shapeIdx = 0;
            for (int i = 0; i < int(sizeof(shapes) / sizeof(shapes[0])); ++i)
                if (m_kernelShape == shapes[i]) shapeIdx = i;

            ImGui::PushItemWidth(120.0f);
            if (ImGui::Combo("操作", &opIdx, ops, int(sizeof(ops) / sizeof(ops[0]))))
                m_operation = ops[opIdx];
            if (ImGui::Combo("核形状", &shapeIdx, shapes, int(sizeof(shapes) / sizeof(shapes[0]))))
                m_kernelShape = shapes[shapeIdx];
            ImGui::InputInt("核宽度", &m_kernelW, 2);
            ImGui::InputInt("核高度", &m_kernelH, 2);
            ImGui::InputInt("迭代次数", &m_iterations);
            ImGui::PopItemWidth();

            m_kernelW = std::max(1, m_kernelW);
            m_kernelH = std::max(1, m_kernelH);
            m_iterations = std::max(1, m_iterations);
        }

        void store(nlohmann::json& j) const override {
            j["operation"] = m_operation;
            j["kernelShape"] = m_kernelShape;
            j["kernelW"] = m_kernelW;
            j["kernelH"] = m_kernelH;
            j["iterations"] = m_iterations;
        }

        void load(const nlohmann::json& j) override {
            m_operation = j.value("operation", "腐蚀");
            m_kernelShape = j.value("kernelShape", "矩形");
            m_kernelW = j.value("kernelW", 3);
            m_kernelH = j.value("kernelH", 3);
            m_iterations = j.value("iterations", 1);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            ImageSet output;
            output.items.reserve(input.items.size());

            int opFlag = cv::MORPH_ERODE;
            if (m_operation == "膨胀") opFlag = cv::MORPH_DILATE;
            else if (m_operation == "开运算") opFlag = cv::MORPH_OPEN;
            else if (m_operation == "闭运算") opFlag = cv::MORPH_CLOSE;
            else if (m_operation == "形态学梯度") opFlag = cv::MORPH_GRADIENT;
            else if (m_operation == "顶帽") opFlag = cv::MORPH_TOPHAT;
            else if (m_operation == "黑帽") opFlag = cv::MORPH_BLACKHAT;

            int kw = std::max(1, m_kernelW | 1);
            int kh = std::max(1, m_kernelH | 1);

            cv::Mat kernel = cv::getStructuringElement(morphShape(m_kernelShape),
                cv::Size(kw, kh));

            for (auto& item : input.items) {
                if (context.isCanceled())
                    fail("执行已停止");
                if (item.image.empty())
                    fail("形态学操作输入无效: " + item.fileName);

                cv::Mat result;
                cv::morphologyEx(item.image, result, opFlag, kernel, cv::Point(-1, -1), m_iterations);
                ImageItem out = item;
                out.image = std::move(result);
                output.items.push_back(std::move(out));
            }

            setOutput(attrId(*this, 1), std::move(output));
        }

    private:
        std::string m_operation = "腐蚀";
        std::string m_kernelShape = "矩形";
        int m_kernelW = 3;
        int m_kernelH = 3;
        int m_iterations = 1;
    };

    class NodeBlur : public WorkflowNode {
    public:
        NodeBlur()
            : WorkflowNode("process.blur", "均值滤波", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            ImGui::PushItemWidth(120.0f);
            ImGui::InputInt("核宽度", &m_kernelW, 2);
            ImGui::InputInt("核高度", &m_kernelH, 2);
            ImGui::PopItemWidth();
            m_kernelW = std::max(1, m_kernelW);
            m_kernelH = std::max(1, m_kernelH);
        }

        void store(nlohmann::json& j) const override {
            j["kernelW"] = m_kernelW;
            j["kernelH"] = m_kernelH;
        }

        void load(const nlohmann::json& j) override {
            m_kernelW = j.value("kernelW", 3);
            m_kernelH = j.value("kernelH", 3);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            ImageSet output;
            output.items.reserve(input.items.size());
            for (auto& item : input.items) {
                if (context.isCanceled()) fail("执行已停止");
                if (item.image.empty()) fail("均值滤波输入无效: " + item.fileName);
                cv::Mat result;
                cv::blur(item.image, result, cv::Size(std::max(1, m_kernelW | 1), std::max(1, m_kernelH | 1)));
                ImageItem out = item;
                out.image = std::move(result);
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }

    private:
        int m_kernelW = 3;
        int m_kernelH = 3;
    };

    class NodeGaussianBlur : public WorkflowNode {
    public:
        NodeGaussianBlur()
            : WorkflowNode("process.gaussian_blur", "高斯滤波", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            ImGui::PushItemWidth(120.0f);
            ImGui::InputInt("核宽度", &m_kernelW, 2);
            ImGui::InputInt("核高度", &m_kernelH, 2);
            ImGui::InputDouble("Sigma X", &m_sigmaX, 0.1, 1.0, "%.1f");
            ImGui::InputDouble("Sigma Y", &m_sigmaY, 0.1, 1.0, "%.1f");
            ImGui::PopItemWidth();
            m_kernelW = std::max(1, m_kernelW);
            m_kernelH = std::max(1, m_kernelH);
            m_sigmaX = std::max(0.0, m_sigmaX);
            m_sigmaY = std::max(0.0, m_sigmaY);
        }

        void store(nlohmann::json& j) const override {
            j["kernelW"] = m_kernelW;
            j["kernelH"] = m_kernelH;
            j["sigmaX"] = m_sigmaX;
            j["sigmaY"] = m_sigmaY;
        }

        void load(const nlohmann::json& j) override {
            m_kernelW = j.value("kernelW", 3);
            m_kernelH = j.value("kernelH", 3);
            m_sigmaX = j.value("sigmaX", 0.0);
            m_sigmaY = j.value("sigmaY", 0.0);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            ImageSet output;
            output.items.reserve(input.items.size());
            for (auto& item : input.items) {
                if (context.isCanceled()) fail("执行已停止");
                if (item.image.empty()) fail("高斯滤波输入无效: " + item.fileName);
                cv::Mat result;
                cv::GaussianBlur(item.image, result, cv::Size(std::max(1, m_kernelW | 1), std::max(1, m_kernelH | 1)), m_sigmaX, m_sigmaY);
                ImageItem out = item;
                out.image = std::move(result);
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }

    private:
        int m_kernelW = 3;
        int m_kernelH = 3;
        double m_sigmaX = 0.0;
        double m_sigmaY = 0.0;
    };

    class NodeMedianBlur : public WorkflowNode {
    public:
        NodeMedianBlur()
            : WorkflowNode("process.median_blur", "中值滤波", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            ImGui::PushItemWidth(120.0f);
            ImGui::InputInt("核大小", &m_kernelSize, 2);
            ImGui::PopItemWidth();
            m_kernelSize = std::max(1, m_kernelSize);
        }

        void store(nlohmann::json& j) const override {
            j["kernelSize"] = m_kernelSize;
        }

        void load(const nlohmann::json& j) override {
            m_kernelSize = j.value("kernelSize", 3);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            ImageSet output;
            output.items.reserve(input.items.size());
            for (auto& item : input.items) {
                if (context.isCanceled()) fail("执行已停止");
                if (item.image.empty()) fail("中值滤波输入无效: " + item.fileName);
                cv::Mat result;
                cv::medianBlur(item.image, result, std::max(1, m_kernelSize | 1));
                ImageItem out = item;
                out.image = std::move(result);
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }

    private:
        int m_kernelSize = 3;
    };

    class NodeBilateralFilter : public WorkflowNode {
    public:
        NodeBilateralFilter()
            : WorkflowNode("process.bilateral_filter", "双边滤波", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            ImGui::PushItemWidth(120.0f);
            ImGui::InputInt("邻域直径", &m_d);
            ImGui::InputDouble("Sigma 颜色", &m_sigmaColor, 1.0, 10.0, "%.1f");
            ImGui::InputDouble("Sigma 空间", &m_sigmaSpace, 1.0, 10.0, "%.1f");
            ImGui::PopItemWidth();
            m_d = std::max(1, m_d);
            m_sigmaColor = std::max(0.0, m_sigmaColor);
            m_sigmaSpace = std::max(0.0, m_sigmaSpace);
        }

        void store(nlohmann::json& j) const override {
            j["d"] = m_d;
            j["sigmaColor"] = m_sigmaColor;
            j["sigmaSpace"] = m_sigmaSpace;
        }

        void load(const nlohmann::json& j) override {
            m_d = j.value("d", 9);
            m_sigmaColor = j.value("sigmaColor", 75.0);
            m_sigmaSpace = j.value("sigmaSpace", 75.0);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            ImageSet output;
            output.items.reserve(input.items.size());
            for (auto& item : input.items) {
                if (context.isCanceled()) fail("执行已停止");
                if (item.image.empty()) fail("双边滤波输入无效: " + item.fileName);
                cv::Mat result;
                cv::bilateralFilter(item.image, result, m_d, m_sigmaColor, m_sigmaSpace);
                ImageItem out = item;
                out.image = std::move(result);
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }

    private:
        int m_d = 9;
        double m_sigmaColor = 75.0;
        double m_sigmaSpace = 75.0;
    };

}

    void registerFilterNodes() {
        addRegistryEntry("形态学操作", "形态学操作", [] { return std::make_unique<NodeMorphology>(); });
        addRegistryEntry("平滑处理", "均值滤波", [] { return std::make_unique<NodeBlur>(); });
        addRegistryEntry("平滑处理", "高斯滤波", [] { return std::make_unique<NodeGaussianBlur>(); });
        addRegistryEntry("平滑处理", "中值滤波", [] { return std::make_unique<NodeMedianBlur>(); });
        addRegistryEntry("平滑处理", "双边滤波", [] { return std::make_unique<NodeBilateralFilter>(); });
    }

}
