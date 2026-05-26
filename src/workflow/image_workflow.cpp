#include "workflow/image_workflow.hpp"

#include <nlohmann/json.hpp>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#endif

namespace imziv::workflow {
namespace {
    std::vector<WorkflowRegistryEntry>& mutableRegistry() {
        static std::vector<WorkflowRegistryEntry> entries;
        return entries;
    }

    std::string readFile(const std::string& path) {
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
        if (size < 0) { fclose(file); return {}; }
        std::string result(size, '\0');
        if (size > 0) fread(result.data(), 1, size_t(size), file);
        fclose(file);
        return result;
    }

    bool writeFile(const std::string& path, const std::string& content) {
#ifdef _WIN32
        int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        if (wlen <= 0) return false;
        std::wstring wpath(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);
        FILE* file = _wfopen(wpath.c_str(), L"wb");
#else
        FILE* file = fopen(path.c_str(), "wb");
#endif
        if (!file) return false;
        size_t written = fwrite(content.data(), 1, content.size(), file);
        fclose(file);
        return written == content.size();
    }
}

    WorkflowNode::WorkflowNode(std::string type, std::string title, std::vector<WorkflowAttribute> attrs)
        : m_type(std::move(type)), m_title(std::move(title)), m_displayName(m_title), m_attrs(std::move(attrs)) {}

    void WorkflowNode::reset() {
        m_outputs.clear();
        m_cachedInputs.clear();
        m_processed = false;
        m_processing = false;
    }

    void WorkflowNode::execute(WorkflowWorkspace& workspace, WorkflowRunContext& context) {
        if (m_processed)
            return;
        if (m_processing)
            fail("检测到递归连接");
        if (context.isCanceled())
            fail("执行已停止");

        m_processing = true;
        process(workspace, context);
        m_processing = false;
        m_processed = true;
    }

    const Value& WorkflowNode::outputValue(int attrId) const {
        auto iter = m_outputs.find(attrId);
        if (iter == m_outputs.end())
            throw std::runtime_error("节点输出为空");
        return iter->second;
    }

    void WorkflowNode::drawBody(WorkflowUiContext&) {}

    void WorkflowNode::store(nlohmann::json&) const {}

    void WorkflowNode::load(const nlohmann::json&) {}

    void WorkflowNode::setOutput(int attrId, Value value) {
        m_outputs[attrId] = std::move(value);
    }

    Value WorkflowNode::inputValue(WorkflowWorkspace& workspace, WorkflowRunContext& context, int attrId) {
        auto* attr = workspace.findAttr(attrId);
        if (attr == nullptr)
            fail("输入属性不存在");

        auto* link = workspace.findInputLink(attrId);
        Value result;
        if (link == nullptr) {
            result = attr->defaultValue;
        } else {
            auto* upstream = workspace.findNodeByAttr(link->fromAttr);
            if (upstream == nullptr)
                fail("连接的上游节点不存在");
            upstream->execute(workspace, context);
            result = upstream->outputValue(link->fromAttr);
        }
        m_cachedInputs[attrId] = result;
        return result;
    }

    const Value& WorkflowNode::cachedInput(int attrId) const {
        static const Value empty;
        auto it = m_cachedInputs.find(attrId);
        return it != m_cachedInputs.end() ? it->second : empty;
    }

    [[noreturn]] void WorkflowNode::fail(const std::string& message) const {
        throw WorkflowNodeError { m_id, message };
    }

    WorkflowWorkspace::WorkflowWorkspace() = default;
    WorkflowWorkspace::~WorkflowWorkspace() = default;

    int WorkflowWorkspace::nextNodeId() { return m_nextNodeId++; }
    int WorkflowWorkspace::nextAttrId() { return m_nextAttrId++; }
    int WorkflowWorkspace::nextLinkId() { return m_nextLinkId++; }

    void WorkflowWorkspace::syncCounters() {
        int maxNode = 0, maxAttr = 999, maxLink = 1999;
        for (const auto& node : nodes) {
            maxNode = std::max(maxNode, node->id());
            for (const auto& attr : node->attributes())
                maxAttr = std::max(maxAttr, attr.id);
        }
        for (const auto& link : links)
            maxLink = std::max(maxLink, link.id);
        m_nextNodeId = maxNode + 1;
        m_nextAttrId = maxAttr + 1;
        m_nextLinkId = maxLink + 1;
    }

    void WorkflowWorkspace::clear() {
        nodes.clear();
        links.clear();
        currentError.reset();
        status.clear();
        filePath.clear();
        m_nextNodeId = 1;
        m_nextAttrId = 1000;
        m_nextLinkId = 2000;
    }

    void WorkflowWorkspace::resetNodes() {
        for (auto& node : nodes)
            node->reset();
    }

    WorkflowNode* WorkflowWorkspace::findNode(int nodeId) {
        for (auto& node : nodes) {
            if (node->id() == nodeId)
                return node.get();
        }
        return nullptr;
    }

    WorkflowNode* WorkflowWorkspace::findNodeByAttr(int attrId) {
        for (auto& node : nodes) {
            for (const auto& attr : node->attributes()) {
                if (attr.id == attrId)
                    return node.get();
            }
        }
        return nullptr;
    }

    WorkflowAttribute* WorkflowWorkspace::findAttr(int attrId) {
        for (auto& node : nodes) {
            for (auto& attr : node->attributes()) {
                if (attr.id == attrId)
                    return &attr;
            }
        }
        return nullptr;
    }

    const WorkflowAttribute* WorkflowWorkspace::findAttr(int attrId) const {
        for (const auto& node : nodes) {
            for (const auto& attr : node->attributes()) {
                if (attr.id == attrId)
                    return &attr;
            }
        }
        return nullptr;
    }

    WorkflowLink* WorkflowWorkspace::findLink(int linkId) {
        for (auto& link : links) {
            if (link.id == linkId)
                return &link;
        }
        return nullptr;
    }

    WorkflowLink* WorkflowWorkspace::findInputLink(int inputAttrId) {
        for (auto& link : links) {
            if (link.toAttr == inputAttrId)
                return &link;
        }
        return nullptr;
    }

    bool WorkflowWorkspace::addLink(int fromAttr, int toAttr, std::string* error) {
        auto* from = findAttr(fromAttr);
        auto* to = findAttr(toAttr);
        if (from == nullptr || to == nullptr) {
            if (error) *error = "连接端点不存在";
            return false;
        }
        if (from->io == AttributeIO::Input && to->io == AttributeIO::Output) {
            std::swap(fromAttr, toAttr);
            std::swap(from, to);
        }
        if (from->io != AttributeIO::Output || to->io != AttributeIO::Input) {
            if (error) *error = "只能从输出连接到输入";
            return false;
        }
        if (from->type != to->type) {
            if (error) *error = "输入输出类型不一致";
            return false;
        }
        if (findInputLink(toAttr) != nullptr) {
            if (error) *error = "输入端已经有连接";
            return false;
        }
        links.push_back({ nextLinkId(), fromAttr, toAttr });
        resetNodes();
        return true;
    }

    void WorkflowWorkspace::eraseLink(int linkId) {
        const size_t oldSize = links.size();
        links.erase(std::remove_if(links.begin(), links.end(), [linkId](const WorkflowLink& link) {
            return link.id == linkId;
        }), links.end());
        if (links.size() != oldSize)
            resetNodes();
    }

    void WorkflowWorkspace::eraseNode(int nodeId) {
        auto* node = findNode(nodeId);
        if (node == nullptr)
            return;

        std::vector<int> attrIds;
        for (const auto& attr : node->attributes())
            attrIds.push_back(attr.id);

        links.erase(std::remove_if(links.begin(), links.end(), [&](const WorkflowLink& link) {
            return std::find(attrIds.begin(), attrIds.end(), link.fromAttr) != attrIds.end() ||
                   std::find(attrIds.begin(), attrIds.end(), link.toAttr) != attrIds.end();
        }), links.end());

        nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [nodeId](const std::unique_ptr<WorkflowNode>& item) {
            return item->id() == nodeId;
        }), nodes.end());
        resetNodes();
    }

    void WorkflowWorkspace::run(WorkflowRunContext context) {
        currentError.reset();
        status.clear();
        resetNodes();
        cancelRequested = false;
        {
            std::lock_guard<std::mutex> lock(previewMutex);
            previewImages.items.clear();
            previewDirty = true;
        }

        std::vector<WorkflowNode*> terminalNodes;
        for (auto& node : nodes) {
            bool hasInput = false;
            bool hasOutput = false;
            for (const auto& attr : node->attributes()) {
                hasInput = hasInput || attr.io == AttributeIO::Input;
                hasOutput = hasOutput || attr.io == AttributeIO::Output;
            }
            if (hasInput && !hasOutput)
                terminalNodes.push_back(node.get());
        }

        if (terminalNodes.empty())
            throw WorkflowNodeError { 0, "没有终端节点，请添加预览或保存节点" };

        for (auto* node : terminalNodes)
            node->execute(*this, context);

        status = "执行完成";
    }

    bool WorkflowWorkspace::save(const std::string& path, std::string* error) const {
        try {
            nlohmann::json root;
            root["version"] = 1;

            nlohmann::json nodesArr = nlohmann::json::array();
            for (const auto& node : nodes) {
                nlohmann::json nj;
                nj["id"] = node->id();
                nj["type"] = node->type();
                nj["title"] = node->title();
                nj["displayName"] = node->displayName();
                nj["pos"]["x"] = node->position().x;
                nj["pos"]["y"] = node->position().y;
                nlohmann::json attrsArr = nlohmann::json::array();
                for (const auto& attr : node->attributes())
                    attrsArr.push_back({{"id", attr.id}, {"name", attr.name}});
                nj["attrs"] = attrsArr;
                nlohmann::json data;
                node->store(data);
                nj["data"] = data;
                nodesArr.push_back(nj);
            }
            root["nodes"] = nodesArr;

            nlohmann::json linksArr = nlohmann::json::array();
            for (const auto& link : links)
                linksArr.push_back({{"id", link.id}, {"from", link.fromAttr}, {"to", link.toAttr}});
            root["links"] = linksArr;

            if (!writeFile(path, root.dump(2))) {
                if (error) *error = "无法写入工作流文件";
                return false;
            }
            return true;
        } catch (const std::exception& e) {
            if (error) *error = std::string("保存失败: ") + e.what();
            return false;
        }
    }

    bool WorkflowWorkspace::load(const std::string& path, std::string* error) {
        std::string content = readFile(path);
        if (content.empty()) {
            if (error) *error = "无法打开工作流文件";
            return false;
        }

        try {
            nlohmann::json root = nlohmann::json::parse(content);

            std::vector<std::unique_ptr<WorkflowNode>> newNodes;
            for (const auto& nj : root["nodes"]) {
                const std::string type = nj["type"];
                auto node = createNodeByType(type);
                if (!node) {
                    if (error) *error = "未知节点类型: " + type;
                    return false;
                }
                node->setId(nj["id"]);
                node->setPosition(ImVec2(nj["pos"]["x"], nj["pos"]["y"]));
                int attrIndex = 0;
                for (const auto& aj : nj["attrs"]) {
                    if (attrIndex < int(node->attributes().size()))
                        node->attributes()[attrIndex].id = aj["id"];
                    attrIndex++;
                }
                if (nj.contains("displayName"))
                    node->setDisplayName(nj["displayName"].get<std::string>());
                node->load(nj["data"]);
                newNodes.push_back(std::move(node));
            }

            std::vector<WorkflowLink> newLinks;
            for (const auto& lj : root["links"])
                newLinks.push_back({lj["id"].get<int>(), lj["from"].get<int>(), lj["to"].get<int>()});

            nodes = std::move(newNodes);
            links = std::move(newLinks);
            filePath = path;
            currentError.reset();
            status = "已加载工作流";
            syncCounters();
            return true;
        } catch (const std::exception& e) {
            if (error) *error = std::string("加载失败: ") + e.what();
            return false;
        }
    }

    const std::vector<WorkflowRegistryEntry>& registryEntries() {
        return mutableRegistry();
    }

    void addRegistryEntry(const std::string& category, const std::string& name,
                          std::function<std::unique_ptr<WorkflowNode>()> create) {
        mutableRegistry().push_back({ category, name, std::move(create) });
    }

    std::unique_ptr<WorkflowNode> createNodeByType(const std::string& type) {
        for (const auto& entry : registryEntries()) {
            auto node = entry.create();
            if (node && node->type() == type)
                return node;
        }
        return nullptr;
    }

    std::string valueTypeName(ValueType type) {
        switch (type) {
            case ValueType::ImageSet: return "图片";
            case ValueType::RegionSet: return "区域";
            case ValueType::Roi: return "ROI";
            case ValueType::Rect: return "矩形";
            case ValueType::Number: return "数字";
            case ValueType::Integer: return "整数";
            case ValueType::Bool: return "布尔";
            case ValueType::String: return "文本";
            case ValueType::Path: return "路径";
        }
        return "";
    }

    bool isSupportedImagePath(const std::string& path) {
        std::string ext = pathExtension(path);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return char(std::tolower(c));
        });
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
               ext == ".tiff" || ext == ".tif" || ext == ".webp" || ext == ".gif";
    }

    std::string pathFileName(const std::string& path) {
        return std::filesystem::path(path).filename().string();
    }

    std::string pathStem(const std::string& path) {
        return std::filesystem::path(path).stem().string();
    }

    std::string pathExtension(const std::string& path) {
        return std::filesystem::path(path).extension().string();
    }

    std::string joinPath(const std::string& dir, const std::string& name) {
        return (std::filesystem::path(dir) / name).string();
    }

    bool writeImage(const std::string& path, const cv::Mat& image) {
        if (path.empty() || image.empty())
            return false;
        std::vector<unsigned char> encoded;
        if (!cv::imencode(pathExtension(path), image, encoded))
            return false;
#ifdef _WIN32
        int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        if (wlen <= 0) return false;
        std::wstring wpath(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);
        FILE* file = _wfopen(wpath.c_str(), L"wb");
#else
        FILE* file = fopen(path.c_str(), "wb");
#endif
        if (!file) return false;
        size_t written = fwrite(encoded.data(), 1, encoded.size(), file);
        fclose(file);
        return written == encoded.size();
    }

    cv::Mat decodeViewerImage(const ImageCanvas& viewer) {
        if (!viewer.hasImage() || viewer.sourceBytes.empty())
            return {};
        cv::Mat raw(1, int(viewer.sourceBytes.size()), CV_8U,
                    const_cast<unsigned char*>(viewer.sourceBytes.data()));
        return cv::imdecode(raw, cv::IMREAD_UNCHANGED);
    }

    ImageSet cloneImageSet(const ImageSet& input) {
        ImageSet output;
        output.items.reserve(input.items.size());
        for (const auto& item : input.items) {
            ImageItem copy = item;
            copy.image = item.image.clone();
            output.items.push_back(std::move(copy));
        }
        return output;
    }

}
