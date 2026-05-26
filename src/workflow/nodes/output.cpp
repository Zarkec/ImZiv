#include "common.hpp"

#include <set>

namespace imziv::workflow {
namespace {

    class NodePreview : public WorkflowNode {
    public:
        NodePreview()
            : WorkflowNode("output.preview", "预览", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片")
            }) {}

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            ImageSet preview = cloneImageSet(input);
            for (auto& item : preview.items)
                item.fileName = displayName() + " - " + item.fileName;

            std::lock_guard<std::mutex> lock(workspace.previewMutex);
            for (auto& item : preview.items)
                workspace.previewImages.items.push_back(std::move(item));
            workspace.previewDirty = true;
        }
    };

    class NodeSaveImages : public WorkflowNode {
    public:
        NodeSaveImages()
            : WorkflowNode("output.save_images", "保存图片", {
                attr(AttributeIO::Input, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext& ui) override {
            char dir[512] = {};
            m_outputDir.copy(dir, sizeof(dir) - 1);
            ImGui::PushItemWidth(180.0f);
            if (ImGui::InputText("输出目录", dir, sizeof(dir)))
                m_outputDir = dir;
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (iconButton(IconVsFolderOpened, "OutputDirSelect", "选择输出目录")) {
                auto selected = selectFolderDialog(ui.window);
                if (!selected.empty())
                    m_outputDir = selected;
            }

            char pattern[256] = {};
            m_pattern.copy(pattern, sizeof(pattern) - 1);
            ImGui::PushItemWidth(180.0f);
            if (ImGui::InputText("命名", pattern, sizeof(pattern)))
                m_pattern = pattern;

            static const char* formats[] = { "png", "jpg", "bmp", "tiff", "webp" };
            int current = 0;
            for (int i = 0; i < 5; ++i) {
                if (m_format == formats[i])
                    current = i;
            }
            if (ImGui::Combo("格式", &current, formats, 5))
                m_format = formats[current];
            ImGui::PopItemWidth();
            ImGui::Checkbox("覆盖已有文件", &m_overwrite);
        }

        void store(nlohmann::json& j) const override {
            j["outputDir"] = m_outputDir;
            j["pattern"] = m_pattern;
            j["format"] = m_format;
            j["overwrite"] = m_overwrite;
        }

        void load(const nlohmann::json& j) override {
            m_outputDir = j.value("outputDir", "");
            m_pattern = j.value("pattern", "{stem}_{index}");
            m_format = j.value("format", "png");
            m_overwrite = j.value("overwrite", false);
        }

        void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) override {
            ImageSet input = requireImageSet(inputValue(workspace, context, attrId(*this, 0)), *this);
            if (m_outputDir.empty() || !std::filesystem::is_directory(m_outputDir))
                fail("输出目录不存在");

            const std::string format = normalizeFormat(m_format);
            const std::set<std::string> allowed = { "png", "jpg", "bmp", "tiff", "webp" };
            if (allowed.find(format) == allowed.end())
                fail("不支持的保存格式: " + m_format);

            if (context.progressTotal != nullptr)
                context.progressTotal->store(int(input.items.size()));

            int written = 0;
            for (const auto& item : input.items) {
                if (context.isCanceled())
                    fail("执行已停止");

                const std::string name = applyPattern(m_pattern, item, format);
                const std::string path = joinPath(m_outputDir, name);
                if (!m_overwrite && std::filesystem::exists(path))
                    fail("文件已存在: " + path);

                cv::Mat image = prepareForSave(item.image, format);
                if (!writeImage(path, image))
                    fail("保存失败: " + path);

                written++;
                if (context.progressDone != nullptr)
                    context.progressDone->store(written);
            }
            workspace.status = "已保存 " + std::to_string(written) + " 张图片";
        }

    private:
        std::string m_outputDir;
        std::string m_pattern = "{stem}_{index}";
        std::string m_format = "png";
        bool m_overwrite = false;
    };

}

    void registerOutputNodes() {
        addRegistryEntry("输出", "预览", [] { return std::make_unique<NodePreview>(); });
        addRegistryEntry("输出", "保存图片", [] { return std::make_unique<NodeSaveImages>(); });
    }

}
