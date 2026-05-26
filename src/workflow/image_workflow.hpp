#pragma once

#include "viewer/image_canvas.hpp"

#include "imgui.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

struct GLFWwindow;

namespace imziv::workflow {

    struct Rect {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };

    struct ImageItem {
        cv::Mat image;
        std::string sourcePath;
        std::string fileName;
        int index = 0;
    };

    struct ImageSet {
        std::vector<ImageItem> items;
    };

    enum class RoiShape {
        Rect,
        Circle,
        Ring,
        Polygon
    };

    struct Roi {
        RoiShape shape = RoiShape::Rect;
        Rect rect;
        cv::Point2d center { 0.0, 0.0 };
        double radius = 0.0;
        double innerRadius = 0.0;
        double outerRadius = 0.0;
        std::vector<cv::Point> polygon;
    };

    struct Region {
        std::vector<cv::Point> contour;
        Rect bbox;
        double area = 0.0;
        cv::Point2d centroid { 0.0, 0.0 };
        int label = 0;
    };

    struct RegionItem {
        std::vector<Region> regions;
        std::string sourcePath;
        std::string fileName;
        int index = 0;
        int imageWidth = 0;
        int imageHeight = 0;
    };

    struct RegionSet {
        std::vector<RegionItem> items;
    };

    enum class ValueType {
        ImageSet,
        RegionSet,
        Roi,
        Rect,
        Number,
        Integer,
        Bool,
        String,
        Path
    };

    using Value = std::variant<std::monostate, ImageSet, RegionSet, Roi, Rect, double, int, bool, std::string>;

    enum class AttributeIO {
        Input,
        Output
    };

    struct WorkflowAttribute {
        int id = 0;
        AttributeIO io = AttributeIO::Input;
        ValueType type = ValueType::ImageSet;
        std::string name;
        Value defaultValue;
    };

    struct WorkflowLink {
        int id = 0;
        int fromAttr = 0;
        int toAttr = 0;
    };

    struct WorkflowNodeError {
        int nodeId = 0;
        std::string message;
    };

    struct WorkflowUiContext {
        GLFWwindow* window = nullptr;
        ImageCanvas* viewer = nullptr;
    };

    struct WorkflowRunContext {
        ImageCanvas* viewer = nullptr;
        std::atomic_bool* cancel = nullptr;
        std::atomic<int>* progressDone = nullptr;
        std::atomic<int>* progressTotal = nullptr;

        [[nodiscard]] bool isCanceled() const {
            return cancel != nullptr && cancel->load();
        }
    };

    class WorkflowWorkspace;

    class WorkflowNode {
    public:
        WorkflowNode(std::string type, std::string title, std::vector<WorkflowAttribute> attrs);
        virtual ~WorkflowNode() = default;

        [[nodiscard]] int id() const { return m_id; }
        void setId(int id) { m_id = id; }
        [[nodiscard]] const std::string& type() const { return m_type; }
        [[nodiscard]] const std::string& title() const { return m_title; }
        [[nodiscard]] const std::string& displayName() const { return m_displayName; }
        void setDisplayName(std::string name) { m_displayName = std::move(name); }

        [[nodiscard]] const ImVec2& position() const { return m_position; }
        void setPosition(ImVec2 pos) { m_position = pos; }

        [[nodiscard]] std::vector<WorkflowAttribute>& attributes() { return m_attrs; }
        [[nodiscard]] const std::vector<WorkflowAttribute>& attributes() const { return m_attrs; }

        void reset();
        void execute(WorkflowWorkspace& workspace, WorkflowRunContext& context);
        [[nodiscard]] const Value& outputValue(int attrId) const;
        [[nodiscard]] const Value& cachedInput(int attrId) const;

        virtual void drawBody(WorkflowUiContext& ui);
        virtual void store(nlohmann::json& j) const;
        virtual void load(const nlohmann::json& j);
        virtual void process(WorkflowWorkspace& workspace, WorkflowRunContext& context) = 0;

    protected:
        void setOutput(int attrId, Value value);
        [[nodiscard]] Value inputValue(WorkflowWorkspace& workspace, WorkflowRunContext& context, int attrId);
        [[noreturn]] void fail(const std::string& message) const;

    private:
        int m_id = 0;
        std::string m_type;
        std::string m_title;
        std::string m_displayName;
        ImVec2 m_position = ImVec2(0, 0);
        std::vector<WorkflowAttribute> m_attrs;
        std::map<int, Value> m_outputs;
        std::map<int, Value> m_cachedInputs;
        bool m_processing = false;
        bool m_processed = false;
    };

    struct WorkflowRegistryEntry {
        std::string category;
        std::string name;
        std::function<std::unique_ptr<WorkflowNode>()> create;
    };

    class WorkflowWorkspace {
    public:
        WorkflowWorkspace();
        ~WorkflowWorkspace();

        std::vector<std::unique_ptr<WorkflowNode>> nodes;
        std::vector<WorkflowLink> links;
        std::optional<WorkflowNodeError> currentError;
        std::string status;
        std::string filePath;
        ImageSet previewImages;
        std::mutex previewMutex;
        bool previewDirty = false;

        std::atomic_bool cancelRequested { false };
        std::atomic_bool running { false };
        std::atomic<int> progressDone { 0 };
        std::atomic<int> progressTotal { 0 };

        int nextNodeId();
        int nextAttrId();
        int nextLinkId();
        void syncCounters();
        void clear();
        void resetNodes();

        WorkflowNode* findNode(int nodeId);
        WorkflowNode* findNodeByAttr(int attrId);
        WorkflowAttribute* findAttr(int attrId);
        const WorkflowAttribute* findAttr(int attrId) const;
        WorkflowLink* findLink(int linkId);
        WorkflowLink* findInputLink(int inputAttrId);

        bool addLink(int fromAttr, int toAttr, std::string* error = nullptr);
        void eraseLink(int linkId);
        void eraseNode(int nodeId);
        void run(WorkflowRunContext context);

        bool save(const std::string& path, std::string* error = nullptr) const;
        bool load(const std::string& path, std::string* error = nullptr);

    private:
        int m_nextNodeId = 1;
        int m_nextAttrId = 1000;
        int m_nextLinkId = 2000;
    };

    const std::vector<WorkflowRegistryEntry>& registryEntries();
    void addRegistryEntry(const std::string& category, const std::string& name,
                          std::function<std::unique_ptr<WorkflowNode>()> create);
    std::unique_ptr<WorkflowNode> createNodeByType(const std::string& type);
    void registerImageWorkflowNodes();

    std::string valueTypeName(ValueType type);
    bool isSupportedImagePath(const std::string& path);
    std::string pathFileName(const std::string& path);
    std::string pathStem(const std::string& path);
    std::string pathExtension(const std::string& path);
    std::string joinPath(const std::string& dir, const std::string& name);
    bool writeImage(const std::string& path, const cv::Mat& image);
    cv::Mat decodeViewerImage(const ImageCanvas& viewer);
    ImageSet cloneImageSet(const ImageSet& input);

}
