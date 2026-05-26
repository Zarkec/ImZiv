#include "common.hpp"

namespace imziv::workflow {
namespace {

    class NodeCurrentImage : public WorkflowNode {
    public:
        NodeCurrentImage()
            : WorkflowNode("input.current_image", "当前图片", {
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void process(WorkflowWorkspace&, WorkflowRunContext& context) override {
            if (context.viewer == nullptr || !context.viewer->hasImage())
                fail("当前没有打开图片");

            cv::Mat image = decodeViewerImage(*context.viewer);
            if (image.empty())
                fail("无法解码当前图片");

            ImageSet set;
            set.items.push_back({ image, context.viewer->filePath, context.viewer->fileName, 0 });
            setOutput(attrId(*this, 0), std::move(set));
        }
    };

    class NodeImageFile : public WorkflowNode {
    public:
        NodeImageFile()
            : WorkflowNode("input.image_file", "图片文件", {
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext& ui) override {
            ImGui::PushItemWidth(180.0f);
            char buffer[512] = {};
            m_path.copy(buffer, sizeof(buffer) - 1);
            if (ImGui::InputText("路径", buffer, sizeof(buffer)))
                m_path = buffer;
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (iconButton(IconVsFolderOpened, "ImageFileSelect", "选择图片")) {
                auto path = openFileDialog(ui.window);
                if (!path.empty())
                    m_path = path;
            }
        }

        void store(nlohmann::json& j) const override {
            j["path"] = m_path;
        }

        void load(const nlohmann::json& j) override {
            m_path = j.value("path", "");
        }

        void process(WorkflowWorkspace&, WorkflowRunContext&) override {
            if (m_path.empty() || !std::filesystem::is_regular_file(m_path))
                fail("图片文件不存在");
            if (!isSupportedImagePath(m_path))
                fail("不支持的图片格式: " + m_path);

            cv::Mat image = readImageFile(m_path);
            if (image.empty())
                fail("无法读取图片: " + m_path);

            ImageSet set;
            set.items.push_back({ image, m_path, pathFileName(m_path), 0 });
            setOutput(attrId(*this, 0), std::move(set));
        }

    private:
        std::string m_path;
    };

    class NodeDirectory : public WorkflowNode {
    public:
        NodeDirectory()
            : WorkflowNode("input.directory", "图片目录", {
                attr(AttributeIO::Output, ValueType::ImageSet, "图片")
            }) {}

        void drawBody(WorkflowUiContext& ui) override {
            ImGui::PushItemWidth(180.0f);
            char buffer[512] = {};
            m_directory.copy(buffer, sizeof(buffer) - 1);
            if (ImGui::InputText("目录", buffer, sizeof(buffer)))
                m_directory = buffer;
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (iconButton(IconVsFolderOpened, "DirectorySelect", "选择目录")) {
                auto dir = selectFolderDialog(ui.window);
                if (!dir.empty())
                    m_directory = dir;
            }
            ImGui::Checkbox("递归", &m_recursive);
        }

        void store(nlohmann::json& j) const override {
            j["directory"] = m_directory;
            j["recursive"] = m_recursive;
        }

        void load(const nlohmann::json& j) override {
            m_directory = j.value("directory", "");
            m_recursive = j.value("recursive", false);
        }

        void process(WorkflowWorkspace&, WorkflowRunContext& context) override {
            if (m_directory.empty() || !std::filesystem::is_directory(m_directory))
                fail("图片目录不存在");

            std::vector<std::string> paths;
            try {
                if (m_recursive) {
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(m_directory)) {
                        if (entry.is_regular_file() && isSupportedImagePath(entry.path().string()))
                            paths.push_back(entry.path().string());
                    }
                } else {
                    for (const auto& entry : std::filesystem::directory_iterator(m_directory)) {
                        if (entry.is_regular_file() && isSupportedImagePath(entry.path().string()))
                            paths.push_back(entry.path().string());
                    }
                }
            } catch (...) {
                fail("读取图片目录失败");
            }
            std::sort(paths.begin(), paths.end());
            if (paths.empty())
                fail("目录中没有支持的图片");

            if (context.progressTotal != nullptr)
                context.progressTotal->store(int(paths.size()));

            ImageSet set;
            set.items.reserve(paths.size());
            for (int i = 0; i < int(paths.size()); ++i) {
                if (context.isCanceled())
                    fail("执行已停止");
                cv::Mat image = readImageFile(paths[size_t(i)]);
                if (image.empty())
                    fail("无法读取图片: " + paths[size_t(i)]);
                set.items.push_back({ image, paths[size_t(i)], pathFileName(paths[size_t(i)]), i });
            }
            setOutput(attrId(*this, 0), std::move(set));
        }

    private:
        std::string m_directory;
        bool m_recursive = false;
    };

}

    void registerInputNodes() {
        addRegistryEntry("输入", "当前图片", [] { return std::make_unique<NodeCurrentImage>(); });
        addRegistryEntry("输入", "图片文件", [] { return std::make_unique<NodeImageFile>(); });
        addRegistryEntry("输入", "图片目录", [] { return std::make_unique<NodeDirectory>(); });
    }

}
