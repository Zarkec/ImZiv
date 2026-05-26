#pragma once

#include "imgui.h"

#include <GLFW/glfw3.h>
#include <opencv2/core.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class ImageCanvas {
public:
    explicit ImageCanvas(GLFWwindow* window);
    ~ImageCanvas();

    ImageCanvas(const ImageCanvas&) = delete;
    ImageCanvas& operator=(const ImageCanvas&) = delete;

    // --- Image loading ---
    bool openFile(const std::string& path, bool scanDir = true);
    bool loadBytes(const std::vector<uint8_t>& bytes, const std::string& name);
    bool loadMat(const cv::Mat& image, const std::string& name = "Preview.png");
    bool loadDropped(const std::string& pathOrUrl, const std::vector<uint8_t>& bytes = {});
    void clear();

    // --- Core rendering ---
    void draw(const ImVec2& size = ImVec2(0, 0));
    void drawStatus();

    // --- View control ---
    void fitToView();
    void setActualSize();
    float zoom() const;
    void setZoom(float z);

    // --- State queries ---
    bool hasImage() const;
    int imageWidth() const;
    int imageHeight() const;
    float mouseImgX() const;
    float mouseImgY() const;
    bool isHovered() const;
    bool getPixelBGR(int x, int y, uint8_t& b, uint8_t& g, uint8_t& r) const;

    // --- Tool mode ---
    enum class Tool { None, Measure, Angle, ColorPicker, CropSelect, RoiCenterSelect, HsvSample };
    void setTool(Tool tool, int ownerId = 0);
    Tool currentTool() const;

    // --- Interaction result consumption ---
    bool consumeCrop(int ownerId, int& x, int& y, int& w, int& h);
    bool consumePoint(int ownerId, double& x, double& y);

    // --- HSV samples ---
    struct HsvSample { int x = 0, y = 0, h = 0, s = 0, v = 0; };
    bool consumeHsvSamples(std::vector<HsvSample>& out);
    const std::vector<HsvSample>& hsvSamples() const;
    std::vector<HsvSample>& hsvSamples();
    void clearHsvSamples();

    // --- Measurement ---
    void clearMeasurement();
    void clearAngleMeasurement();

    // --- File operations ---
    bool saveAs(const std::string& path) const;
    bool copyToClipboard() const;
    bool copyPathToClipboard() const;
    bool deleteCurrent();

    // --- File management (public for Phase 1 compatibility) ---
    std::string filePath;
    std::string fileName;
    std::vector<uint8_t> sourceBytes;
    std::vector<std::string> imageFiles;
    int currentIndex = -1;

    // --- Overlay callback (Phase 2+) ---
    using OverlayFn = std::function<void(ImDrawList&, const ImVec2& imgOrigin, float zoom)>;
    void setOverlay(OverlayFn fn);

    // --- Cleanup ---
    void cleanup();

    // --- Measurement color constants ---
    static constexpr int MeasureSegColorCount = 8;
    static constexpr ImU32 MeasureSegColors[MeasureSegColorCount] = {
        IM_COL32(255, 80, 80, 255),
        IM_COL32(80, 160, 255, 255),
        IM_COL32(80, 220, 80, 255),
        IM_COL32(255, 200, 40, 255),
        IM_COL32(200, 80, 255, 255),
        IM_COL32(255, 140, 40, 255),
        IM_COL32(40, 220, 220, 255),
        IM_COL32(255, 80, 180, 255),
    };

    // --- Tool state (public for Phase 1 compatibility with tools/nodes) ---
    bool measureMode = false;
    bool measureActive = false;
    std::vector<ImVec2> measurePoints;
    float measureLiveX = 0.0f;
    float measureLiveY = 0.0f;

    bool angleMode = false;
    int angleClickCount = 0;
    bool angleCompleted = false;
    ImVec2 angleVertex = ImVec2(0, 0);
    ImVec2 angleFirstEnd = ImVec2(0, 0);
    ImVec2 angleSecondEnd = ImVec2(0, 0);

    bool colorPickerMode = false;
    bool colorPicked = false;
    uint8_t pickedR = 0, pickedG = 0, pickedB = 0;
    float pickedImgX = 0.0f, pickedImgY = 0.0f;

    bool cropSelectMode = false;
    bool cropSelectActive = false;
    int cropSelectOwnerId = 0;
    ImVec2 cropSelectStart = ImVec2(0, 0);
    ImVec2 cropSelectEnd = ImVec2(0, 0);
    bool cropSelectDone = false;
    int cropSelectX = 0, cropSelectY = 0, cropSelectW = 0, cropSelectH = 0;

    bool roiCenterSelectMode = false;
    bool roiCenterSelectDone = false;
    int roiCenterSelectOwnerId = 0;
    float roiCenterX = 0.0f;
    float roiCenterY = 0.0f;

    bool hsvSampleMode = false;
    bool hsvSampleApplyRequested = false;
    std::vector<HsvSample> m_hsvSamples;

private:
    GLFWwindow* m_window;
    GLuint m_texture = 0;
    int m_imgWidth = 0, m_imgHeight = 0;
    float m_zoom = 1.0f, m_panX = 0.0f, m_panY = 0.0f;
    bool m_hasImage = false, m_wantFit = true, m_wantActual = false;
    float m_mouseImgX = -1.0f, m_mouseImgY = -1.0f;
    bool m_hovered = false;
    std::vector<uint8_t> m_bgrPixels;
    Tool m_tool = Tool::None;
    int m_toolOwnerId = 0;
    OverlayFn m_overlay;

    void clearState();
    bool uploadTexture(const std::vector<uint8_t>& buf, const std::string& path);
    void doFitZoom(float cw, float ch);
    void doSetActualSize(float cw, float ch);
};
