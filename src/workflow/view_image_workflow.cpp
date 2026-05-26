#include "workflow/view_image_workflow.hpp"

#include "workflow/image_workflow_nodes.hpp"
#include "core/content_registry.hpp"
#include "platform/native_dialog.hpp"
#include "viewer/view_image_editor.hpp"

#include "imgui.h"
#include "imgui_internal.h"
#include "imnodes.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <set>
#include <utility>

using namespace imziv::workflow;

namespace {
    constexpr const char* WorkflowPreviewViewId = "工作流预览";
    constexpr const char* IconVsNewFile = "\xEE\xA9\xBF";
    constexpr const char* IconVsFolderOpened = "\xEE\xAB\xB7";
    constexpr const char* IconVsSave = "\xEE\xAD\x8B";
    constexpr const char* IconVsSaveAs = "\xEE\xAD\x8A";
    constexpr const char* IconVsDebugStart = "\xEE\xAB\x93";
    constexpr const char* IconVsDebugStop = "\xEE\xAB\x97";
    constexpr const char* IconVsRunAll = "\xEE\xAE\x9E";

    bool iconButton(const char* icon, const char* id, const char* tooltip) {
        char label[64];
        std::snprintf(label, sizeof(label), "%s##%s", icon, id);
        const ImVec2 size(ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
        bool clicked = ImGui::Button(label, size);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip);
        return clicked;
    }

    ImNodesPinShape pinShape(ValueType type, bool output) {
        switch (type) {
            case ValueType::ImageSet: return output ? ImNodesPinShape_QuadFilled : ImNodesPinShape_Quad;
            case ValueType::RegionSet: return output ? ImNodesPinShape_CircleFilled : ImNodesPinShape_Circle;
            case ValueType::Roi: return output ? ImNodesPinShape_TriangleFilled : ImNodesPinShape_Triangle;
            case ValueType::Rect: return output ? ImNodesPinShape_TriangleFilled : ImNodesPinShape_Triangle;
            default: return output ? ImNodesPinShape_CircleFilled : ImNodesPinShape_Circle;
        }
    }

    void clippedTextWithTooltip(const char* text, float width) {
        width = std::max(width, 0.0f);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        const float lineHeight = ImGui::GetTextLineHeight();

        ImGui::InvisibleButton("##clipped_text", ImVec2(width, lineHeight));
        const ImVec2 clipMax(pos.x + width, pos.y + lineHeight);
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), pos, clipMax, clipMax.x, text, nullptr, &textSize);

        if (ImGui::IsItemHovered() && textSize.x > width)
            ImGui::SetTooltip("%s", text);
    }

    int normalizeIntegerAttribute(const WorkflowAttribute& attr, int value) {
        if (attr.name == "阈值")
            return std::clamp(value, 0, 255);
        if (attr.name == "块大小") {
            value = std::max(3, value);
            if (value % 2 == 0)
                ++value;
            return value;
        }
        if (attr.name == "C")
            return std::clamp(value, -255, 255);
        if (attr.name == "宽度" || attr.name == "高度")
            return std::max(0, value);
        return value;
    }

    int parseNodeDisplayIndex(const std::string& displayName, const std::string& title) {
        if (displayName == title)
            return 1;
        const std::string prefix = title + " ";
        if (displayName.rfind(prefix, 0) != 0)
            return 0;

        const std::string suffix = displayName.substr(prefix.size());
        if (suffix.empty())
            return 0;
        for (char c : suffix) {
            if (!std::isdigit(static_cast<unsigned char>(c)))
                return 0;
        }
        return std::max(1, std::stoi(suffix));
    }

    std::string nextNodeDisplayName(const WorkflowWorkspace& workspace, const WorkflowNode& node) {
        std::set<int> usedIndexes;
        for (const auto& existing : workspace.nodes) {
            if (existing->type() != node.type())
                continue;
            const int index = parseNodeDisplayIndex(existing->displayName(), node.title());
            if (index > 0)
                usedIndexes.insert(index);
        }

        int index = 1;
        while (usedIndexes.find(index) != usedIndexes.end())
            ++index;
        if (index == 1)
            return node.title();
        return node.title() + " " + std::to_string(index);
    }
}

ViewImageWorkflow::ViewImageWorkflow(GLFWwindow* window, ImageCanvas& viewer)
    : hex::View::Window("imziv.view.image_workflow", "\xee\xb0\x99"),
      m_window(window),
      m_viewer(viewer) {
    getWindowOpenState() = false;
    m_ui.window = window;
    updateInteractionViewer();
    registerImageWorkflowNodes();
}

ViewImageWorkflow::~ViewImageWorkflow() {
    stopTask();
    m_task.wait();
}

void ViewImageWorkflow::drawContent() {
    updatePreviewImage();
    updateInteractionViewer();
    drawToolbar();
    ImGui::Separator();

    if (m_workspace.running.load() || m_task.isRunning()) {
        int done = m_workspace.progressDone.load();
        int total = std::max(1, m_workspace.progressTotal.load());
        ImGui::Text("正在执行批处理...");
        ImGui::ProgressBar(float(done) / float(total), ImVec2(-1, 0), nullptr);
        ImGui::Text("%d / %d", done, total);
        return;
    }

    ImGui::BeginChild("##workflow_left", ImVec2(0, 0), false);
    drawNodeEditor();
    ImGui::EndChild();
}

void ViewImageWorkflow::drawToolbar() {
    const bool running = m_workspace.running.load() || m_task.isRunning();
    ImGui::BeginDisabled(running);
    if (iconButton(IconVsNewFile, "WorkflowNew", "新建")) {
        m_workspace.clear();
    }
    ImGui::SameLine();
    if (iconButton(IconVsFolderOpened, "WorkflowOpen", "打开工作流")) {
        auto path = openWorkflowDialog(m_window);
        if (!path.empty()) {
            std::string error;
            if (!m_workspace.load(path, &error))
                m_workspace.status = error;
        }
    }
    ImGui::SameLine();
    if (iconButton(IconVsSave, "WorkflowSave", "保存")) {
        std::string path = m_workspace.filePath;
        if (path.empty())
            path = saveWorkflowDialog(m_window, "workflow.imzivflow");
        if (!path.empty()) {
            std::string error;
            if (m_workspace.save(path, &error)) {
                m_workspace.filePath = path;
                m_workspace.status = "已保存工作流";
            } else {
                m_workspace.status = error;
            }
        }
    }
    ImGui::SameLine();
    if (iconButton(IconVsSaveAs, "WorkflowSaveAs", "另存为")) {
        auto path = saveWorkflowDialog(m_window, "workflow.imzivflow");
        if (!path.empty()) {
            std::string error;
            if (m_workspace.save(path, &error)) {
                m_workspace.filePath = path;
                m_workspace.status = "已保存工作流";
            } else {
                m_workspace.status = error;
            }
        }
    }
    ImGui::SameLine();
    if (iconButton(IconVsDebugStart, "WorkflowPreviewRun", "运行预览"))
        runTask();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (!running) {
        if (iconButton(IconVsRunAll, "WorkflowBatchRun", "批量运行"))
            runTask();
    } else {
        if (iconButton(IconVsDebugStop, "WorkflowStop", "停止"))
            stopTask();
    }

    if (m_workspace.currentError.has_value()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", m_workspace.currentError->message.c_str());
    } else if (!m_workspace.status.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m_workspace.status.c_str());
    }
}

void ViewImageWorkflow::drawNodeEditor() {
    ImNodes::BeginNodeEditor();

    for (auto& node : m_workspace.nodes) {
        if (m_workspace.currentError.has_value() && m_workspace.currentError->nodeId == node->id())
            ImNodes::PushColorStyle(ImNodesCol_NodeOutline, IM_COL32(255, 60, 60, 255));
        drawNode(*node);
        if (m_workspace.currentError.has_value() && m_workspace.currentError->nodeId == node->id())
            ImNodes::PopColorStyle();
    }

    for (const auto& link : m_workspace.links)
        ImNodes::Link(link.id, link.fromAttr, link.toAttr);

    ImNodes::MiniMap(0.18f, ImNodesMiniMapLocation_BottomRight);
    ImNodes::EndNodeEditor();

    for (auto& node : m_workspace.nodes)
        node->setPosition(ImNodes::GetNodeGridSpacePos(node->id()));

    int hoveredNode = -1;
    int hoveredLink = -1;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        m_rightClickedPos = ImGui::GetMousePos();
        if (ImNodes::IsNodeHovered(&hoveredNode)) {
            m_rightClickedNode = hoveredNode;
            m_rightClickedLink = -1;
            ImGui::OpenPopup("WorkflowNodeMenu");
        } else if (ImNodes::IsLinkHovered(&hoveredLink)) {
            m_rightClickedLink = hoveredLink;
            m_rightClickedNode = -1;
            ImGui::OpenPopup("WorkflowLinkMenu");
        } else {
            m_rightClickedNode = -1;
            m_rightClickedLink = -1;
            ImGui::OpenPopup("WorkflowContextMenu");
        }
    }

    int from = 0, to = 0;
    if (ImNodes::IsLinkCreated(&from, &to)) {
        std::string error;
        if (!m_workspace.addLink(from, to, &error))
            m_workspace.status = error;
    }
    int destroyed = 0;
    if (ImNodes::IsLinkDestroyed(&destroyed))
        m_workspace.eraseLink(destroyed);

    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        const int linkCount = ImNodes::NumSelectedLinks();
        if (linkCount > 0) {
            std::vector<int> ids(static_cast<size_t>(linkCount));
            ImNodes::GetSelectedLinks(ids.data());
            for (int id : ids)
                m_workspace.eraseLink(id);
            ImNodes::ClearLinkSelection();
        }

        const int nodeCount = ImNodes::NumSelectedNodes();
        if (nodeCount > 0) {
            std::vector<int> ids(static_cast<size_t>(nodeCount));
            ImNodes::GetSelectedNodes(ids.data());
            for (int id : ids)
                m_workspace.eraseNode(id);
            ImNodes::ClearNodeSelection();
        }
    }

    if (m_workspace.currentError.has_value()) {
        int nodeId = -1;
        if (ImNodes::IsNodeHovered(&nodeId) && nodeId == m_workspace.currentError->nodeId) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("错误");
            ImGui::Separator();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
            ImGui::TextWrapped("%s", m_workspace.currentError->message.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    drawContextMenus();
}

void ViewImageWorkflow::drawContextMenus() {
    if (ImGui::BeginPopup("WorkflowContextMenu")) {
        auto rootCategory = [](const std::string& category) {
            const size_t separator = category.find('/');
            return separator == std::string::npos ? category : category.substr(0, separator);
        };
        auto childCategory = [](const std::string& category) {
            const size_t separator = category.find('/');
            return separator == std::string::npos ? std::string() : category.substr(separator + 1);
        };

        std::set<std::string> shownRoots;
        for (const auto& entry : registryEntries()) {
            const std::string root = rootCategory(entry.category);
            if (!shownRoots.insert(root).second)
                continue;

            if (ImGui::BeginMenu(root.c_str())) {
                std::set<std::string> shownChildren;
                for (const auto& item : registryEntries()) {
                    if (rootCategory(item.category) != root)
                        continue;

                    const std::string child = childCategory(item.category);
                    if (child.empty()) {
                        if (ImGui::MenuItem(item.name.c_str()))
                            addNodeAtMouse(item.create());
                    } else if (shownChildren.insert(child).second && ImGui::BeginMenu(child.c_str())) {
                        for (const auto& childItem : registryEntries()) {
                            if (rootCategory(childItem.category) != root || childCategory(childItem.category) != child)
                                continue;
                            if (ImGui::MenuItem(childItem.name.c_str()))
                                addNodeAtMouse(childItem.create());
                        }
                        ImGui::EndMenu();
                    }
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("WorkflowNodeMenu")) {
        if (ImGui::MenuItem("删除节点"))
            m_workspace.eraseNode(m_rightClickedNode);
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("WorkflowLinkMenu")) {
        if (ImGui::MenuItem("删除连接"))
            m_workspace.eraseLink(m_rightClickedLink);
        ImGui::EndPopup();
    }
}

void ViewImageWorkflow::drawNode(WorkflowNode& node) {
    ImNodes::SetNodeGridSpacePos(node.id(), node.position());
    ImNodes::BeginNode(node.id());
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted(node.displayName().c_str());
    ImNodes::EndNodeTitleBar();

    node.drawBody(m_ui);

    for (auto& attr : node.attributes()) {
        if (attr.io == AttributeIO::Input) {
            ImNodes::BeginInputAttribute(attr.id, pinShape(attr.type, false));
            if (m_workspace.findInputLink(attr.id) == nullptr &&
                attr.type != ValueType::ImageSet && attr.type != ValueType::RegionSet &&
                attr.type != ValueType::Roi && attr.type != ValueType::Rect) {
                drawAttributeInput(attr);
            } else {
                ImGui::Text("%s (%s)", attr.name.c_str(), valueTypeName(attr.type).c_str());
            }
            ImNodes::EndInputAttribute();
        } else {
            ImNodes::BeginOutputAttribute(attr.id, pinShape(attr.type, true));
            const float labelWidth = ImGui::CalcTextSize(attr.name.c_str()).x;
            ImGui::Indent(std::max(0.0f, 120.0f - labelWidth));
            ImGui::Text("%s (%s)", attr.name.c_str(), valueTypeName(attr.type).c_str());
            ImGui::Unindent(std::max(0.0f, 120.0f - labelWidth));
            ImNodes::EndOutputAttribute();
        }
    }

    ImNodes::EndNode();
}

void ViewImageWorkflow::drawAttributeInput(WorkflowAttribute& attr) {
    ImGui::PushID(attr.id);
    bool changed = false;
    if (attr.type == ValueType::Integer) {
        int value = 0;
        if (auto* current = std::get_if<int>(&attr.defaultValue))
            value = *current;
        ImGui::PushItemWidth(82.0f);
        if (ImGui::InputInt(attr.name.c_str(), &value, 0, 0)) {
            attr.defaultValue = normalizeIntegerAttribute(attr, value);
            changed = true;
        }
        ImGui::PopItemWidth();
    } else if (attr.type == ValueType::Number) {
        double value = 0.0;
        if (auto* current = std::get_if<double>(&attr.defaultValue))
            value = *current;
        ImGui::PushItemWidth(96.0f);
        if (ImGui::InputDouble(attr.name.c_str(), &value, 0.0, 0.0, "%.3f")) {
            attr.defaultValue = value;
            changed = true;
        }
        ImGui::PopItemWidth();
    } else if (attr.type == ValueType::Bool) {
        bool value = false;
        if (auto* current = std::get_if<bool>(&attr.defaultValue))
            value = *current;
        if (ImGui::Checkbox(attr.name.c_str(), &value)) {
            attr.defaultValue = value;
            changed = true;
        }
    } else if (attr.type == ValueType::String || attr.type == ValueType::Path) {
        char buffer[256] = {};
        if (auto* current = std::get_if<std::string>(&attr.defaultValue))
            current->copy(buffer, sizeof(buffer) - 1);
        ImGui::PushItemWidth(150.0f);
        if (ImGui::InputText(attr.name.c_str(), buffer, sizeof(buffer))) {
            attr.defaultValue = std::string(buffer);
            changed = true;
        }
        ImGui::PopItemWidth();
    } else {
        ImGui::TextUnformatted(attr.name.c_str());
    }
    ImGui::PopID();
    if (changed)
        m_workspace.resetNodes();
}

void ViewImageWorkflow::drawPreviewControls() {
    bool drewControls = false;
    if (m_previewItemCount > 1) {
        int nextIndex = m_previewIndex;
        ImGui::BeginDisabled(nextIndex <= 0);
        if (ImGui::Button("<"))
            --nextIndex;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("%d / %d", m_previewIndex + 1, m_previewItemCount);
        ImGui::SameLine();
        ImGui::BeginDisabled(nextIndex >= m_previewItemCount - 1);
        if (ImGui::Button(">"))
            ++nextIndex;
        ImGui::EndDisabled();

        if (nextIndex != m_previewIndex)
            m_uploadedPreviewIndex = -1;
        m_previewIndex = std::clamp(nextIndex, 0, m_previewItemCount - 1);
        drewControls = true;
    }

    if (!m_previewLabel.empty()) {
        if (drewControls)
            ImGui::SameLine();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float availWidth = ImGui::GetContentRegionAvail().x;
        ImGui::PushID("preview_label");
        clippedTextWithTooltip(m_previewLabel.c_str(), std::max(0.0f, availWidth - style.ItemSpacing.x));
        ImGui::PopID();
    }
}

void ViewImageWorkflow::addNodeAtMouse(std::unique_ptr<WorkflowNode> node) {
    if (!node)
        return;
    node->setId(m_workspace.nextNodeId());
    for (auto& attr : node->attributes())
        attr.id = m_workspace.nextAttrId();
    node->setDisplayName(nextNodeDisplayName(m_workspace, *node));
    m_workspace.nodes.push_back(std::move(node));
    auto& added = *m_workspace.nodes.back();
    ImNodes::SetNodeScreenSpacePos(added.id(), m_rightClickedPos);
    added.setPosition(ImNodes::GetNodeGridSpacePos(added.id()));
}

void ViewImageWorkflow::runTask() {
    if (m_task.isRunning())
        return;

    m_workspace.running = true;
    m_workspace.cancelRequested = false;
    m_workspace.progressDone = 0;
    m_workspace.progressTotal = 1;
    m_task = hex::TaskManager::createTask("ImZiv image workflow", hex::TaskManager::NoProgress, [this](hex::Task& task) {
        try {
            WorkflowRunContext context;
            context.viewer = &m_viewer;
            context.cancel = &m_workspace.cancelRequested;
            context.progressDone = &m_workspace.progressDone;
            context.progressTotal = &m_workspace.progressTotal;
            m_workspace.run(context);
            task.update();
        } catch (const WorkflowNodeError& error) {
            m_workspace.currentError = error;
            m_workspace.status = error.message;
        } catch (const std::exception& error) {
            m_workspace.currentError = WorkflowNodeError { 0, error.what() };
            m_workspace.status = error.what();
        }
        m_workspace.running = false;
    });
}

void ViewImageWorkflow::stopTask() {
    if (m_workspace.running.load() || m_task.isRunning()) {
        m_workspace.cancelRequested = true;
        m_task.interrupt();
    }
}

void ViewImageWorkflow::updatePreviewImage() {
    ImageItem item;
    bool needsUpdate = false;
    {
        std::lock_guard<std::mutex> lock(m_workspace.previewMutex);
        if (!m_workspace.previewDirty && m_uploadedPreviewIndex == m_previewIndex)
            return;

        m_previewItemCount = int(m_workspace.previewImages.items.size());
        if (m_previewItemCount == 0) {
            if (m_workflowPreviewCanvas != nullptr && m_workflowPreviewCanvas->hasImage())
                m_workflowPreviewCanvas->clear();
            m_previewIndex = 0;
            m_uploadedPreviewIndex = -1;
            m_previewLabel.clear();
            m_workspace.previewDirty = false;
            updateInteractionViewer();
            return;
        }

        m_previewIndex = std::clamp(m_previewIndex, 0, m_previewItemCount - 1);
        item = m_workspace.previewImages.items[size_t(m_previewIndex)];
        item.image = item.image.clone();
        m_previewLabel = item.fileName;
        m_workspace.previewDirty = false;
        needsUpdate = true;
    }

    if (needsUpdate && !item.image.empty()) {
        ensureWorkflowPreviewView();
        if (m_workflowPreviewCanvas != nullptr)
            m_workflowPreviewCanvas->loadMat(item.image, m_previewLabel + ".png");
        if (m_workflowPreviewView != nullptr)
            m_workflowPreviewView->bringToFront();
    }
    m_uploadedPreviewIndex = m_previewIndex;
    updateInteractionViewer();
}

void ViewImageWorkflow::ensureWorkflowPreviewView() {
    if (m_workflowPreviewCanvas != nullptr && m_workflowPreviewView != nullptr) {
        m_workflowPreviewView->getWindowOpenState() = true;
        return;
    }

    if (auto* existing = hex::ContentRegistry::Views::getViewByName(WorkflowPreviewViewId)) {
        if (auto* editor = dynamic_cast<ViewImageEditor*>(existing)) {
            m_workflowPreviewCanvas = editor->canvas();
            m_workflowPreviewView = existing;
            editor->setToolbar([this] { drawPreviewControls(); });
            m_workflowPreviewView->getWindowOpenState() = true;
            return;
        }
    }

    auto canvas = std::make_unique<ImageCanvas>(m_window);
    m_workflowPreviewCanvas = canvas.get();
    auto view = std::make_unique<ViewImageEditor>(m_window, std::move(canvas), WorkflowPreviewViewId);
    view->setToolbar([this] { drawPreviewControls(); });
    m_workflowPreviewView = view.get();
    hex::ContentRegistry::Views::impl::add(std::move(view));
}

void ViewImageWorkflow::updateInteractionViewer() {
    if (m_workflowPreviewCanvas != nullptr && m_workflowPreviewCanvas->hasImage() &&
        m_workflowPreviewView != nullptr && m_workflowPreviewView->getWindowOpenState())
        m_ui.viewer = m_workflowPreviewCanvas;
    else
        m_ui.viewer = &m_viewer;
}

void ViewImageWorkflow::drawHelpText() {
    ImGui::TextWrapped("右键添加图片处理节点，连接输出和输入，使用预览或保存节点作为终端节点执行。");
}
