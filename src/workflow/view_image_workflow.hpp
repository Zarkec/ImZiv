#pragma once

#include "viewer/image_canvas.hpp"
#include "workflow/image_workflow.hpp"
#include "core/task_manager.hpp"
#include "core/view.hpp"

#include <memory>
#include <string>

class ViewImageWorkflow : public hex::View::Window {
public:
    ViewImageWorkflow(GLFWwindow* window, ImageCanvas& viewer);
    ~ViewImageWorkflow() override;

    void drawContent() override;
    void drawHelpText() override;

private:
    void drawToolbar();
    void drawNodeEditor();
    void drawContextMenus();
    void drawNode(imziv::workflow::WorkflowNode& node);
    void drawAttributeInput(imziv::workflow::WorkflowAttribute& attr);
    void drawPreviewControls();
    void addNodeAtMouse(std::unique_ptr<imziv::workflow::WorkflowNode> node);
    void runTask();
    void stopTask();
    void updatePreviewImage();
    void ensureWorkflowPreviewView();
    void updateInteractionViewer();

    GLFWwindow* m_window = nullptr;
    ImageCanvas& m_viewer;
    ImageCanvas* m_workflowPreviewCanvas = nullptr;
    hex::View* m_workflowPreviewView = nullptr;
    imziv::workflow::WorkflowWorkspace m_workspace;
    imziv::workflow::WorkflowUiContext m_ui;
    hex::TaskHolder m_task;
    int m_rightClickedNode = -1;
    int m_rightClickedLink = -1;
    ImVec2 m_rightClickedPos = ImVec2(0, 0);
    int m_previewIndex = 0;
    int m_uploadedPreviewIndex = -1;
    int m_previewItemCount = 0;
    std::string m_previewLabel;
};
