#pragma once

#include "core/view.hpp"

#include <functional>
#include <memory>
#include <string>

struct GLFWwindow;
class ImageCanvas;

class ViewImageEditor : public hex::View::Window {
public:
    using ToolbarFn = std::function<void()>;

    ViewImageEditor(GLFWwindow* window, ImageCanvas& canvas);
    ViewImageEditor(GLFWwindow* window, std::unique_ptr<ImageCanvas> canvas, const std::string& viewId);
    ~ViewImageEditor() override;

    void drawContent() override;
    void drawHelpText() override;
    bool shouldDefaultFocus() const override { return true; }
    ImageCanvas* canvas() const { return m_canvas; }
    void setToolbar(ToolbarFn toolbar) { m_toolbar = std::move(toolbar); }

private:
    GLFWwindow* m_window;
    std::unique_ptr<ImageCanvas> m_ownedCanvas;
    ImageCanvas* m_canvas;
    ToolbarFn m_toolbar;
};
