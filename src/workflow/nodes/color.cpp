#include "common.hpp"

namespace imziv::workflow {
namespace {

    class NodeGray : public WorkflowNode {
    public:
        NodeGray()
            : WorkflowNode("process.gray", "灰度", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            ImageSet output;
            output.items.reserve(input.items.size());
            for (auto& item : input.items) {
                cv::Mat result;
                ImageItem out = item;
                if (item.image.channels() == 1)
                    result = item.image.clone();
                else if (item.image.channels() == 3)
                    cv::cvtColor(item.image, result, cv::COLOR_BGR2GRAY);
                else if (item.image.channels() == 4)
                    cv::cvtColor(item.image, result, cv::COLOR_BGRA2GRAY);
                else
                    fail("不支持的通道数: " + item.fileName);
                out.image = std::move(result);
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }
    };

    class NodeColorSpace : public WorkflowNode {
    public:
        NodeColorSpace()
            : WorkflowNode("process.color_space", "色彩空间转换", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            static const char* spaces[] = { "BGR", "RGB", "灰度", "HSV", "HLS", "Lab", "YCrCb" };
            int source = 0;
            int target = 0;
            for (int i = 0; i < int(sizeof(spaces) / sizeof(spaces[0])); ++i) {
                if (m_source == spaces[i])
                    source = i;
                if (m_target == spaces[i])
                    target = i;
            }

            ImGui::PushItemWidth(120.0f);
            if (ImGui::Combo("源", &source, spaces, int(sizeof(spaces) / sizeof(spaces[0]))))
                m_source = spaces[source];
            if (ImGui::Combo("目标", &target, spaces, int(sizeof(spaces) / sizeof(spaces[0]))))
                m_target = spaces[target];
            ImGui::PopItemWidth();
        }

        void store(nlohmann::json& j) const override {
            j["source"] = m_source;
            j["target"] = m_target;
        }

        void load(const nlohmann::json& j) override {
            m_source = j.at("source").get<std::string>();
            m_target = j.at("target").get<std::string>();
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            ImageSet output;
            output.items.reserve(input.items.size());
            for (auto& item : input.items) {
                if (context.isCanceled())
                    fail("执行已停止");
                ImageItem out = item;
                out.image = convertColorSpace(item.image, m_source, m_target);
                if (out.image.empty())
                    fail("色彩空间转换失败: " + item.fileName);
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }

    private:
        std::string m_source = "BGR";
        std::string m_target = "BGR";
    };

    class NodeSplitChannels : public WorkflowNode {
    public:
        NodeSplitChannels()
            : WorkflowNode("process.split_channels", "通道分离", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "通道 1"),
                attr(AttributeIO::Output, ValueType::ImageSet, "通道 2"),
                attr(AttributeIO::Output, ValueType::ImageSet, "通道 3")
            }) {}

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            ImageSet channels[3];
            for (auto& output : channels)
                output.items.reserve(input.items.size());

            for (auto& item : input.items) {
                if (context.isCanceled())
                    fail("执行已停止");

                std::vector<cv::Mat> split;
                cv::split(item.image, split);
                if (split.empty())
                    fail("通道分离失败: " + item.fileName);

                for (int i = 0; i < 3; ++i) {
                    ImageItem out = item;
                    const int sourceIndex = std::min<int>(i, int(split.size()) - 1);
                    out.image = split[size_t(sourceIndex)].clone();
                    channels[i].items.push_back(std::move(out));
                }
            }

            setOutput(attrId(*this, 1), std::move(channels[0]));
            setOutput(attrId(*this, 2), std::move(channels[1]));
            setOutput(attrId(*this, 3), std::move(channels[2]));
        }
    };

    class NodeMergeChannels : public WorkflowNode {
    public:
        NodeMergeChannels()
            : WorkflowNode("process.merge_channels", "通道合并", {
                attr(AttributeIO::Input, ValueType::ImageSet, "通道 1"),
                attr(AttributeIO::Input, ValueType::ImageSet, "通道 2"),
                attr(AttributeIO::Input, ValueType::ImageSet, "通道 3"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet ch1 = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            ImageSet ch2 = requireImageSet(inputValue(workspace, context, attrId(*this, 1)), *this);
            ImageSet ch3 = requireImageSet(inputValue(workspace, context, attrId(*this, 2)), *this);

            if (ch1.items.size() != ch2.items.size() || ch1.items.size() != ch3.items.size())
                fail("三个通道的图片数量不一致");

            ImageSet output;
            output.items.reserve(ch1.items.size());
            for (size_t i = 0; i < ch1.items.size(); ++i) {
                if (context.isCanceled()) fail("执行已停止");
                const auto& a = ch1.items[i].image;
                const auto& b = ch2.items[i].image;
                const auto& c = ch3.items[i].image;
                if (a.size() != b.size() || a.size() != c.size())
                    fail("通道尺寸不一致: " + ch1.items[i].fileName);

                cv::Mat result;
                std::vector<cv::Mat> channels = { a.clone(), b.clone(), c.clone() };
                cv::merge(channels, result);
                ImageItem out = ch1.items[i];
                out.image = std::move(result);
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 3), std::move(output));
        }
    };

    class NodeApplyMask : public WorkflowNode {
    public:
        NodeApplyMask()
            : WorkflowNode("process.apply_mask", "掩膜应用", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Input, ValueType::ImageSet, "掩膜"),
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
            ImageSet masks = requireImageSet(inputValue(workspace, context, attrId(*this, 1)), *this);

            if (input.items.size() != masks.items.size())
                fail("图片和掩膜数量不一致");

            ImageSet output;
            output.items.reserve(input.items.size());
            for (size_t i = 0; i < input.items.size(); ++i) {
                if (context.isCanceled()) fail("执行已停止");
                const auto& image = input.items[i].image;
                const auto& mask = masks.items[i].image;
                if (image.size() != mask.size())
                    fail("图片和掩膜尺寸不一致: " + input.items[i].fileName);

                cv::Mat singleMask;
                if (mask.channels() > 1)
                    cv::cvtColor(mask, singleMask, cv::COLOR_BGR2GRAY);
                else
                    singleMask = mask;

                cv::Mat result = applyMaskFill(image, singleMask, m_fill);
                if (result.empty())
                    fail("掩膜应用输入通道无效: " + input.items[i].fileName);

                ImageItem out = input.items[i];
                out.image = std::move(result);
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 2), std::move(output));
        }

    private:
        FillOptions m_fill;
    };

    class NodeBlend : public WorkflowNode {
        float m_baseOpacity = 0.5f;
        float m_upperOpacity = 0.5f;
        std::string m_mode = "正常";
    public:
        NodeBlend()
            : WorkflowNode("process.blend", "图片混合", {
                attr(AttributeIO::Input, ValueType::ImageSet, "底层图片"),
                attr(AttributeIO::Input, ValueType::ImageSet, "上层图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            sliderInputFloat("底层不透明度", &m_baseOpacity, 0.0f, 1.0f, 0.05f);
            sliderInputFloat("上层不透明度", &m_upperOpacity, 0.0f, 1.0f, 0.05f);
            static const char* modes[] = { "正常", "正片叠底", "滤色", "叠加", "柔光", "差值" };
            int idx = 0;
            for (int i = 0; i < int(sizeof(modes) / sizeof(modes[0])); ++i)
                if (m_mode == modes[i]) idx = i;
            ImGui::PushItemWidth(120.0f);
            if (ImGui::Combo("混合模式", &idx, modes, int(sizeof(modes) / sizeof(modes[0]))))
                m_mode = modes[idx];
            ImGui::PopItemWidth();
        }

        void store(nlohmann::json& j) const override {
            j["baseOpacity"] = m_baseOpacity;
            j["upperOpacity"] = m_upperOpacity;
            j["mode"] = m_mode;
        }

        void load(const nlohmann::json& j) override {
            m_baseOpacity = j.value("baseOpacity", 0.5f);
            m_upperOpacity = j.value("upperOpacity", 0.5f);
            m_mode = j.value("mode", "正常");
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet base = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            ImageSet blend = requireImageSet(inputValue(workspace, context, attrId(*this, 1)), *this);
            if (base.items.empty()) fail("底层图片为空");
            if (blend.items.empty()) fail("上层图片为空");
            float baseAlpha = std::clamp(m_baseOpacity, 0.0f, 1.0f);
            float upperAlpha = std::clamp(m_upperOpacity, 0.0f, 1.0f);

            ImageSet output;
            output.items.reserve(base.items.size());
            for (size_t i = 0; i < base.items.size(); ++i) {
                if (context.isCanceled()) fail("执行已停止");
                const auto& baseImg = base.items[i].image;
                const auto& blendImg = blend.items[std::min(i, blend.items.size() - 1)].image;
                if (baseImg.empty() || blendImg.empty()) continue;

                cv::Mat blendResized;
                if (blendImg.size() != baseImg.size())
                    cv::resize(blendImg, blendResized, baseImg.size());
                else
                    blendResized = blendImg;

                cv::Mat b3, bl3;
                auto to3 = [](const cv::Mat& src, cv::Mat& dst) {
                    if (src.channels() == 1) cv::cvtColor(src, dst, cv::COLOR_GRAY2BGR);
                    else if (src.channels() == 4) cv::cvtColor(src, dst, cv::COLOR_BGRA2BGR);
                    else dst = src;
                };
                to3(baseImg, b3);
                to3(blendResized, bl3);

                cv::Mat bf, blf;
                b3.convertTo(bf, CV_32F, 1.0 / 255.0);
                bl3.convertTo(blf, CV_32F, 1.0 / 255.0);

                cv::Mat result = bf.clone();

                for (int y = 0; y < bf.rows; ++y) {
                    const auto* rowB = bf.ptr<cv::Vec3f>(y);
                    const auto* rowBl = blf.ptr<cv::Vec3f>(y);
                    auto* rowR = result.ptr<cv::Vec3f>(y);
                    for (int x = 0; x < bf.cols; ++x) {
                        for (int c = 0; c < 3; ++c) {
                            float b = rowB[x][c], bl = rowBl[x][c];
                            float v;
                            if (m_mode == "正常") {
                                v = bl;
                            } else if (m_mode == "正片叠底") {
                                v = b * bl;
                            } else if (m_mode == "滤色") {
                                v = 1.0f - (1.0f - b) * (1.0f - bl);
                            } else if (m_mode == "叠加") {
                                v = b < 0.5f ? 2.0f * b * bl : 1.0f - 2.0f * (1.0f - b) * (1.0f - bl);
                            } else if (m_mode == "柔光") {
                                v = bl < 0.5f
                                    ? b * (2.0f * bl + b * (1.0f - 2.0f * bl))
                                    : b + (2.0f * bl - 1.0f) * (std::sqrt(b) - b);
                            } else {
                                v = std::abs(b - bl);
                            }
                            rowR[x][c] = b * baseAlpha + v * upperAlpha;
                        }
                    }
                }

                cv::Mat out8u;
                result.convertTo(out8u, CV_8U, 255.0);

                if (baseImg.channels() == 4) {
                    std::vector<cv::Mat> ch;
                    cv::split(baseImg, ch);
                    std::vector<cv::Mat> outCh;
                    cv::split(out8u, outCh);
                    outCh.push_back(ch[3]);
                    cv::merge(outCh, out8u);
                }

                ImageItem item = base.items[i];
                item.image = std::move(out8u);
                output.items.push_back(std::move(item));
            }
            setOutput(attrId(*this, 2), std::move(output));
        }
    };

}

    void registerColorNodes() {
        addRegistryEntry("颜色处理", "灰度", [] { return std::make_unique<NodeGray>(); });
        addRegistryEntry("颜色处理", "色彩空间转换", [] { return std::make_unique<NodeColorSpace>(); });
        addRegistryEntry("颜色处理", "通道分离", [] { return std::make_unique<NodeSplitChannels>(); });
        addRegistryEntry("颜色处理", "通道合并", [] { return std::make_unique<NodeMergeChannels>(); });
        addRegistryEntry("颜色处理", "掩膜应用", [] { return std::make_unique<NodeApplyMask>(); });
        addRegistryEntry("颜色处理", "图片混合", [] { return std::make_unique<NodeBlend>(); });
    }

}
