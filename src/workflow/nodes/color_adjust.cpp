#include "common.hpp"

namespace imziv::workflow {
namespace {

    class NodeBrightnessContrast : public WorkflowNode {
        int m_brightness = 0;
        int m_contrast = 0;
    public:
        NodeBrightnessContrast()
            : WorkflowNode("process.brightness_contrast", "亮度/对比度", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            sliderInputInt("亮度", &m_brightness, -100, 100);
            sliderInputInt("对比度", &m_contrast, -100, 100);
        }

        void store(nlohmann::json& j) const override {
            j["brightness"] = m_brightness;
            j["contrast"] = m_contrast;
        }

        void load(const nlohmann::json& j) override {
            m_brightness = j.value("brightness", 0);
            m_contrast = j.value("contrast", 0);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            double contrastFactor = m_contrast >= 0 ? 1.0 + m_contrast / 50.0 : 1.0 + m_contrast / 100.0;
            ImageSet output;
            output.items.reserve(input.items.size());
            for (auto& item : input.items) {
                if (context.isCanceled()) fail("执行已停止");
                if (item.image.empty()) continue;
                cv::Mat img;
                item.image.convertTo(img, CV_32F);
                img = (img - 128.0) * contrastFactor + 128.0 + m_brightness;
                ImageItem out = item;
                img.convertTo(out.image, item.image.depth());
                out.image.setTo(0, out.image < 0);
                out.image.setTo(255, out.image > 255);
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }
    };

    class NodeHslAdjust : public WorkflowNode {
        float m_hueShift = 0.0f;
        float m_saturation = 1.0f;
        float m_lightness = 1.0f;
    public:
        NodeHslAdjust()
            : WorkflowNode("process.hsl_adjust", "色相/饱和度/明度", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            sliderInputFloat("色相偏移", &m_hueShift, -180.0f, 180.0f, 1.0f);
            sliderInputFloat("饱和度", &m_saturation, 0.0f, 3.0f, 0.05f);
            sliderInputFloat("明度", &m_lightness, 0.0f, 3.0f, 0.05f);
        }

        void store(nlohmann::json& j) const override {
            j["hueShift"] = m_hueShift;
            j["saturation"] = m_saturation;
            j["lightness"] = m_lightness;
        }

        void load(const nlohmann::json& j) override {
            m_hueShift = j.value("hueShift", 0.0f);
            m_saturation = j.value("saturation", 1.0f);
            m_lightness = j.value("lightness", 1.0f);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            double hueShift = std::clamp(double(m_hueShift), -180.0, 180.0);
            double satMul = std::clamp(double(m_saturation), 0.0, 3.0);
            double valMul = std::clamp(double(m_lightness), 0.0, 3.0);
            ImageSet output;
            output.items.reserve(input.items.size());
            for (auto& item : input.items) {
                if (context.isCanceled()) fail("执行已停止");
                if (item.image.empty()) continue;
                ImageItem out = item;
                if (item.image.channels() == 1) {
                    item.image.convertTo(out.image, -1, valMul, 0);
                } else {
                    cv::Mat hsv;
                    int code = item.image.channels() == 4 ? cv::COLOR_BGRA2BGR : 0;
                    cv::Mat bgr = code ? cv::Mat() : item.image;
                    if (code) cv::cvtColor(item.image, bgr, code);
                    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
                    for (int i = 0; i < hsv.rows; ++i) {
                        auto* row = hsv.ptr<uchar>(i);
                        for (int j = 0; j < hsv.cols; ++j) {
                            int h = row[j * 3];
                            int s = row[j * 3 + 1];
                            int v = row[j * 3 + 2];
                            h = (h + int(hueShift / 2.0) + 360) % 180;
                            s = std::clamp(int(s * satMul), 0, 255);
                            v = std::clamp(int(v * valMul), 0, 255);
                            row[j * 3] = uchar(h);
                            row[j * 3 + 1] = uchar(s);
                            row[j * 3 + 2] = uchar(v);
                        }
                    }
                    cv::cvtColor(hsv, out.image, cv::COLOR_HSV2BGR);
                    if (item.image.channels() == 4) {
                        cv::Mat bgra;
                        cv::cvtColor(out.image, bgra, cv::COLOR_BGR2BGRA);
                        std::vector<cv::Mat> channels;
                        cv::split(bgra, channels);
                        std::vector<cv::Mat> origChannels;
                        cv::split(item.image, origChannels);
                        channels[3] = origChannels[3];
                        cv::merge(channels, out.image);
                    }
                }
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }
    };

    class NodeInvert : public WorkflowNode {
    public:
        NodeInvert()
            : WorkflowNode("process.invert", "反相", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            ImageSet output;
            output.items.reserve(input.items.size());
            for (auto& item : input.items) {
                if (context.isCanceled()) fail("执行已停止");
                ImageItem out = item;
                cv::bitwise_not(item.image, out.image);
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }
    };

    class NodeHistogramEqualize : public WorkflowNode {
    public:
        NodeHistogramEqualize()
            : WorkflowNode("process.histogram_equalize", "直方图均衡化", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            ImageSet output;
            output.items.reserve(input.items.size());
            for (auto& item : input.items) {
                if (context.isCanceled()) fail("执行已停止");
                if (item.image.empty()) continue;
                ImageItem out = item;
                if (item.image.channels() == 1) {
                    cv::equalizeHist(item.image, out.image);
                } else {
                    cv::Mat ycrcb;
                    int code = item.image.channels() == 4 ? cv::COLOR_BGRA2BGR : 0;
                    cv::Mat bgr = code ? cv::Mat() : item.image;
                    if (code) cv::cvtColor(item.image, bgr, code);
                    cv::cvtColor(bgr, ycrcb, cv::COLOR_BGR2YCrCb);
                    std::vector<cv::Mat> channels;
                    cv::split(ycrcb, channels);
                    cv::equalizeHist(channels[0], channels[0]);
                    cv::merge(channels, ycrcb);
                    cv::cvtColor(ycrcb, out.image, cv::COLOR_YCrCb2BGR);
                    if (item.image.channels() == 4) {
                        cv::Mat bgra;
                        cv::cvtColor(out.image, bgra, cv::COLOR_BGR2BGRA);
                        std::vector<cv::Mat> ch;
                        cv::split(bgra, ch);
                        std::vector<cv::Mat> origCh;
                        cv::split(item.image, origCh);
                        ch[3] = origCh[3];
                        cv::merge(ch, out.image);
                    }
                }
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }
    };

    class NodeGamma : public WorkflowNode {
        float m_gamma = 1.0f;
    public:
        NodeGamma()
            : WorkflowNode("process.gamma", "Gamma 校正", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            sliderInputFloat("Gamma", &m_gamma, 0.1f, 10.0f, 0.1f);
        }

        void store(nlohmann::json& j) const override {
            j["gamma"] = m_gamma;
        }

        void load(const nlohmann::json& j) override {
            m_gamma = j.value("gamma", 1.0f);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            double gamma = std::clamp(double(m_gamma), 0.1, 10.0);
            cv::Mat lut(1, 256, CV_8U);
            for (int i = 0; i < 256; ++i)
                lut.at<uchar>(i) = cv::saturate_cast<uchar>(255.0 * std::pow(i / 255.0, 1.0 / gamma));
            ImageSet output;
            output.items.reserve(input.items.size());
            for (auto& item : input.items) {
                if (context.isCanceled()) fail("执行已停止");
                if (item.image.empty()) continue;
                ImageItem out = item;
                if (item.image.channels() == 1) {
                    cv::LUT(item.image, lut, out.image);
                } else {
                    std::vector<cv::Mat> channels, result;
                    cv::split(item.image, channels);
                    for (size_t c = 0; c < channels.size() - (item.image.channels() == 4 ? 1 : 0); ++c)
                        result.push_back(cv::Mat()), cv::LUT(channels[c], lut, result.back());
                    if (item.image.channels() == 4)
                        result.push_back(channels[3]);
                    cv::merge(result, out.image);
                }
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }
    };

    class NodeLevels : public WorkflowNode {
        int m_black = 0;
        int m_white = 255;
        float m_midGamma = 1.0f;
    public:
        NodeLevels()
            : WorkflowNode("process.levels", "色阶", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片"),
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext&) override {
            sliderInputInt("黑场", &m_black, 0, 254);
            sliderInputInt("白场", &m_white, m_black + 1, 255);
            sliderInputFloat("中间调", &m_midGamma, 0.1f, 10.0f, 0.1f);
        }

        void store(nlohmann::json& j) const override {
            j["black"] = m_black;
            j["white"] = m_white;
            j["midGamma"] = m_midGamma;
        }

        void load(const nlohmann::json& j) override {
            m_black = j.value("black", 0);
            m_white = j.value("white", 255);
            m_midGamma = j.value("midGamma", 1.0f);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            int black = std::clamp(m_black, 0, 254);
            int white = std::clamp(m_white, black + 1, 255);
            double midGamma = std::clamp(double(m_midGamma), 0.1, 10.0);
            cv::Mat lut(1, 256, CV_8U);
            double range = white - black;
            for (int i = 0; i < 256; ++i) {
                double v = std::clamp((i - black) / range, 0.0, 1.0);
                v = std::pow(v, 1.0 / midGamma);
                lut.at<uchar>(i) = cv::saturate_cast<uchar>(v * 255.0);
            }
            ImageSet output;
            output.items.reserve(input.items.size());
            for (auto& item : input.items) {
                if (context.isCanceled()) fail("执行已停止");
                if (item.image.empty()) continue;
                ImageItem out = item;
                if (item.image.channels() == 1) {
                    cv::LUT(item.image, lut, out.image);
                } else {
                    std::vector<cv::Mat> channels, result;
                    cv::split(item.image, channels);
                    for (size_t c = 0; c < channels.size() - (item.image.channels() == 4 ? 1 : 0); ++c)
                        result.push_back(cv::Mat()), cv::LUT(channels[c], lut, result.back());
                    if (item.image.channels() == 4)
                        result.push_back(channels[3]);
                    cv::merge(result, out.image);
                }
                output.items.push_back(std::move(out));
            }
            setOutput(attrId(*this, 1), std::move(output));
        }
    };

}

    void registerColorAdjustNodes() {
        addRegistryEntry("色彩调节", "亮度/对比度", [] { return std::make_unique<NodeBrightnessContrast>(); });
        addRegistryEntry("色彩调节", "色相/饱和度/明度", [] { return std::make_unique<NodeHslAdjust>(); });
        addRegistryEntry("色彩调节", "反相", [] { return std::make_unique<NodeInvert>(); });
        addRegistryEntry("色彩调节", "直方图均衡化", [] { return std::make_unique<NodeHistogramEqualize>(); });
        addRegistryEntry("色彩调节", "Gamma 校正", [] { return std::make_unique<NodeGamma>(); });
        addRegistryEntry("色彩调节", "色阶", [] { return std::make_unique<NodeLevels>(); });
    }

}
