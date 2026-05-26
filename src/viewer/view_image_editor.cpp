#include "viewer/view_image_editor.hpp"

#include "viewer/image_canvas.hpp"
#include "app/ui.hpp"

#include <utility>

ViewImageEditor::ViewImageEditor(GLFWwindow* window, ImageCanvas& canvas)
    : hex::View::Window("imziv.view.image_editor", "\xee\xab\xaa"), m_window(window), m_canvas(&canvas) {
    this->getWindowOpenState() = true;
}

ViewImageEditor::ViewImageEditor(GLFWwindow* window, std::unique_ptr<ImageCanvas> canvas, const std::string& viewId)
    : hex::View::Window(viewId, "\xee\xab\xaa"), m_window(window), m_ownedCanvas(std::move(canvas)), m_canvas(m_ownedCanvas.get()) {
    this->getWindowOpenState() = true;
}

ViewImageEditor::~ViewImageEditor() {
    if (m_ownedCanvas)
        m_ownedCanvas->cleanup();
}

void ViewImageEditor::drawContent() {
    if (m_toolbar) {
        m_toolbar();
        ImGui::Separator();
    }
    m_canvas->draw();
    m_canvas->drawStatus();
}

void ViewImageEditor::drawHelpText() {
}
