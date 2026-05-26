#include "viewer/image_canvas.hpp"
#include "imgui_internal.h"
#include "platform/native_dialog.hpp"

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#endif

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

// --- Context menu icon strings (duplicated from ui.cpp for self-containment) ---
namespace {
    constexpr const char* CtxIconSaveAs = "\xEE\xAD\x8B";
    constexpr const char* CtxIconCopy = "\xEE\xAF\x8C";
    constexpr const char* CtxIconLocation = "\xEE\xAC\x9A";
    constexpr const char* CtxIconTrash = "\xEE\xAA\x81";

    const char* ImageExts[] = {
        ".png", ".jpg", ".jpeg", ".bmp", ".tiff", ".tif", ".webp", ".gif"
    };
    constexpr int ImageExtCount = sizeof(ImageExts) / sizeof(ImageExts[0]);
    constexpr size_t MaxNetworkImageBytes = 256 * 1024 * 1024;

    template<typename T>
    void releaseVector(std::vector<T>& vec) {
        std::vector<T>().swap(vec);
    }

    std::vector<uint8_t> readFileBuffer(const std::string& path) {
#ifdef _WIN32
        int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        if (wlen <= 0) return {};
        std::wstring wpath(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);
        FILE* f = _wfopen(wpath.c_str(), L"rb");
#else
        FILE* f = fopen(path.c_str(), "rb");
#endif
        if (!f) return {};
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::vector<uint8_t> buf(sz);
        if (sz > 0) fread(buf.data(), 1, sz, f);
        fclose(f);
        return buf;
    }

    bool writeFileBuffer(const std::string& path, const std::vector<uint8_t>& buf) {
        if (buf.empty()) return false;
#ifdef _WIN32
        int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        if (wlen <= 0) return false;
        std::wstring wpath(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);
        FILE* f = _wfopen(wpath.c_str(), L"wb");
#else
        FILE* f = fopen(path.c_str(), "wb");
#endif
        if (!f) return false;
        size_t written = fwrite(buf.data(), 1, buf.size(), f);
        fclose(f);
        return written == buf.size();
    }

    bool isImageExt(const std::string& path) {
        std::string ext = path;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        for (int i = 0; i < ImageExtCount; i++) {
            size_t elen = strlen(ImageExts[i]);
            if (ext.size() >= elen && ext.compare(ext.size() - elen, elen, ImageExts[i]) == 0)
                return true;
        }
        return false;
    }

    std::string baseName(const std::string& path) {
        size_t p = path.find_last_of("/\\");
        std::string name = (p != std::string::npos) ? path.substr(p + 1) : path;
        size_t query = name.find_first_of("?#");
        if (query != std::string::npos)
            name.erase(query);
        return name.empty() ? path : name;
    }

    std::string saveExtension(const std::string& path) {
        size_t separator = path.find_last_of("/\\");
        size_t dot = path.find_last_of('.');
        if (dot == std::string::npos || (separator != std::string::npos && dot < separator))
            return {};
        std::string ext = path.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".jpeg") return ".jpg";
        if (ext == ".tif") return ".tiff";
        return ext;
    }

    bool canEncodeExtension(const std::string& ext) {
        return ext == ".png" || ext == ".jpg" || ext == ".bmp" ||
               ext == ".tiff" || ext == ".webp";
    }

    bool isNetworkImageUrl(const std::string& value) {
        if (value.size() < 7) return false;
        std::string scheme = value.substr(0, std::min<size_t>(value.size(), 8));
        std::transform(scheme.begin(), scheme.end(), scheme.begin(), ::tolower);
        return scheme.rfind("http://", 0) == 0 || scheme.rfind("https://", 0) == 0;
    }

    std::string formatFileSize(size_t bytes) {
        constexpr const char* units[] = { "B", "KB", "MB", "GB" };
        double value = static_cast<double>(bytes);
        int unit = 0;
        while (value >= 1024.0 && unit < 3) { value /= 1024.0; unit++; }
        char buffer[64];
        if (unit == 0)
            ImFormatString(buffer, sizeof(buffer), "%llu %s", static_cast<unsigned long long>(bytes), units[unit]);
        else
            ImFormatString(buffer, sizeof(buffer), "%.2f %s", value, units[unit]);
        return buffer;
    }

    void pushMenuStyle() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 7.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(30, 30, 34, 250));
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(70, 70, 78, 255));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(55, 75, 110, 220));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(66, 92, 136, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(78, 106, 156, 255));
    }

    void popMenuStyle() {
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(4);
    }

#ifdef _WIN32
    std::wstring utf8ToWide(const std::string& text) {
        int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        if (length <= 0) return {};
        std::wstring result(length, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), length);
        result.pop_back();
        return result;
    }

    bool setClipboardUnicodeText(const std::wstring& text) {
        if (text.empty() || !OpenClipboard(nullptr)) return false;
        bool ok = false;
        if (EmptyClipboard()) {
            const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (memory != nullptr) {
                void* data = GlobalLock(memory);
                if (data != nullptr) {
                    memcpy(data, text.c_str(), bytes);
                    GlobalUnlock(memory);
                    if (SetClipboardData(CF_UNICODETEXT, memory) != nullptr) {
                        memory = nullptr;
                        ok = true;
                    }
                }
                if (memory != nullptr) GlobalFree(memory);
            }
        }
        CloseClipboard();
        return ok;
    }

    bool setClipboardDib(const cv::Mat& bgr) {
        if (bgr.empty() || bgr.channels() != 3 || !OpenClipboard(nullptr)) return false;
        const int width = bgr.cols;
        const int height = bgr.rows;
        const int rowStride = ((width * 3 + 3) / 4) * 4;
        const size_t headerSize = sizeof(BITMAPINFOHEADER);
        const size_t pixelSize = size_t(rowStride) * size_t(height);
        bool ok = false;
        if (EmptyClipboard()) {
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, headerSize + pixelSize);
            if (memory != nullptr) {
                uint8_t* data = static_cast<uint8_t*>(GlobalLock(memory));
                if (data != nullptr) {
                    auto* header = reinterpret_cast<BITMAPINFOHEADER*>(data);
                    *header = {};
                    header->biSize = sizeof(BITMAPINFOHEADER);
                    header->biWidth = width;
                    header->biHeight = height;
                    header->biPlanes = 1;
                    header->biBitCount = 24;
                    header->biCompression = BI_RGB;
                    header->biSizeImage = DWORD(pixelSize);
                    uint8_t* pixels = data + headerSize;
                    for (int y = 0; y < height; ++y) {
                        const uint8_t* src = bgr.ptr<uint8_t>(height - 1 - y);
                        uint8_t* dst = pixels + size_t(y) * size_t(rowStride);
                        memcpy(dst, src, size_t(width) * 3);
                        if (rowStride > width * 3)
                            memset(dst + width * 3, 0, size_t(rowStride - width * 3));
                    }
                    GlobalUnlock(memory);
                    if (SetClipboardData(CF_DIB, memory) != nullptr) {
                        memory = nullptr;
                        ok = true;
                    }
                }
                if (memory != nullptr) GlobalFree(memory);
            }
        }
        CloseClipboard();
        return ok;
    }

    bool moveFileToRecycleBin(const std::string& path) {
        std::wstring widePath = utf8ToWide(path);
        if (widePath.empty()) return false;
        widePath.push_back(L'\0');
        SHFILEOPSTRUCTW op = {};
        op.wFunc = FO_DELETE;
        op.pFrom = widePath.c_str();
        op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
        return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
    }

    struct WinHttpHandle {
        HINTERNET value = nullptr;
        ~WinHttpHandle() { if (value) WinHttpCloseHandle(value); }
    };

    std::vector<uint8_t> readUrlBuffer(const std::string& url) {
        const std::wstring wideUrl = utf8ToWide(url);
        if (wideUrl.empty()) return {};
        URL_COMPONENTS components = {};
        components.dwStructSize = sizeof(components);
        components.dwSchemeLength = DWORD(-1);
        components.dwHostNameLength = DWORD(-1);
        components.dwUrlPathLength = DWORD(-1);
        components.dwExtraInfoLength = DWORD(-1);
        if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components)) return {};
        if (components.nScheme != INTERNET_SCHEME_HTTP && components.nScheme != INTERNET_SCHEME_HTTPS) return {};
        std::wstring host(components.lpszHostName, components.dwHostNameLength);
        std::wstring requestPath;
        if (components.dwUrlPathLength > 0) requestPath.assign(components.lpszUrlPath, components.dwUrlPathLength);
        if (components.dwExtraInfoLength > 0) requestPath.append(components.lpszExtraInfo, components.dwExtraInfoLength);
        if (requestPath.empty()) requestPath = L"/";
        WinHttpHandle session { WinHttpOpen(L"ImZiv/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
        if (!session.value) return {};
        WinHttpSetTimeouts(session.value, 5000, 5000, 15000, 15000);
        WinHttpHandle connection { WinHttpConnect(session.value, host.c_str(), components.nPort, 0) };
        if (!connection.value) return {};
        DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        WinHttpHandle request { WinHttpOpenRequest(connection.value, L"GET", requestPath.c_str(), nullptr,
                                                   WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags) };
        if (!request.value) return {};
        WinHttpAddRequestHeaders(request.value, L"Accept: image/*\r\n", DWORD(-1), WINHTTP_ADDREQ_FLAG_ADD);
        if (!WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request.value, nullptr)) return {};
        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                                 WINHTTP_NO_HEADER_INDEX) || statusCode < 200 || statusCode >= 300) return {};
        std::vector<uint8_t> buffer;
        while (true) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request.value, &available)) return {};
            if (available == 0) break;
            if (buffer.size() + available > MaxNetworkImageBytes) return {};
            size_t offset = buffer.size();
            buffer.resize(offset + available);
            DWORD bytesRead = 0;
            if (!WinHttpReadData(request.value, buffer.data() + offset, available, &bytesRead)) return {};
            buffer.resize(offset + bytesRead);
        }
        return buffer;
    }
#endif

    void scanDirectory(ImageCanvas& c, const std::string& filePath) {
        namespace fs = std::filesystem;
        auto dir = fs::path(filePath).parent_path();
        c.imageFiles.clear();
        try {
            for (const auto& e : fs::directory_iterator(dir)) {
                if (e.is_regular_file() && isImageExt(e.path().string()))
                    c.imageFiles.push_back(fs::canonical(e.path()).string());
            }
        } catch (...) {}
        std::sort(c.imageFiles.begin(), c.imageFiles.end());
        std::string canon;
        try { canon = fs::canonical(fs::path(filePath)).string(); } catch (...) { canon = filePath; }
        c.currentIndex = -1;
        for (int i = 0; i < (int)c.imageFiles.size(); i++) {
            if (c.imageFiles[i] == canon) { c.currentIndex = i; break; }
        }
    }

    constexpr double Pi = 3.14159265358979323846;

    double calculateImageAngle(const ImVec2& p1, const ImVec2& p2) {
        double angle = std::atan2(double(p2.y - p1.y), double(p2.x - p1.x)) * 180.0 / Pi;
        if (angle < 0.0)
            angle += 360.0;
        return angle;
    }

    double calculateAngleDifference(double angle1, double angle2) {
        double diff = std::fabs(angle1 - angle2);
        if (diff > 180.0)
            diff = 360.0 - diff;
        return diff;
    }

    ImVec2 constrainAngle15(const ImVec2& vertex, const ImVec2& point) {
        const double dx = double(point.x - vertex.x);
        const double dy = double(point.y - vertex.y);
        const double distance = std::sqrt(dx * dx + dy * dy);
        if (distance < 1.0)
            return point;

        const double currentAngle = std::atan2(dy, dx) * 180.0 / Pi;
        const double constrainedAngle = std::round(currentAngle / 15.0) * 15.0;
        const double angleRad = constrainedAngle * Pi / 180.0;
        return ImVec2(
            vertex.x + float(distance * std::cos(angleRad)),
            vertex.y + float(distance * std::sin(angleRad))
        );
    }

    void addDashedLine(ImDrawList* dl, ImVec2 start, ImVec2 end, ImU32 col, float thickness) {
        const float dx = end.x - start.x;
        const float dy = end.y - start.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.5f)
            return;

        constexpr float dash = 9.0f;
        constexpr float gap = 5.0f;
        const ImVec2 dir(dx / length, dy / length);
        for (float at = 0.0f; at < length; at += dash + gap) {
            const float next = std::min(at + dash, length);
            dl->AddLine(
                ImVec2(start.x + dir.x * at, start.y + dir.y * at),
                ImVec2(start.x + dir.x * next, start.y + dir.y * next),
                col,
                thickness
            );
        }
    }

    void drawAngleArc(ImDrawList* dl, ImVec2 vertex, float radius, double angle1, double angle2, ImU32 col, float thickness) {
        double startAngle = std::min(angle1, angle2);
        double endAngle = std::max(angle1, angle2);
        if (endAngle - startAngle > 180.0) {
            startAngle = std::max(angle1, angle2);
            endAngle = std::min(angle1, angle2) + 360.0;
        }

        constexpr int segments = 48;
        std::vector<ImVec2> points;
        points.reserve(segments + 1);
        for (int i = 0; i <= segments; ++i) {
            const double t = double(i) / double(segments);
            const double a = (startAngle + (endAngle - startAngle) * t) * Pi / 180.0;
            points.emplace_back(vertex.x + radius * float(std::cos(a)),
                                vertex.y + radius * float(std::sin(a)));
        }
        dl->AddPolyline(points.data(), int(points.size()), col, 0, thickness);
    }

    double angleArcMidpoint(double angle1, double angle2) {
        double startAngle = std::min(angle1, angle2);
        double endAngle = std::max(angle1, angle2);
        if (endAngle - startAngle > 180.0) {
            startAngle = std::max(angle1, angle2);
            endAngle = std::min(angle1, angle2) + 360.0;
        }

        double midAngle = (startAngle + endAngle) * 0.5;
        if (midAngle >= 360.0)
            midAngle -= 360.0;
        return midAngle;
    }

}

// ============================================================
// Construction / Destruction
// ============================================================

ImageCanvas::ImageCanvas(GLFWwindow* window)
    : m_window(window) {}

ImageCanvas::~ImageCanvas() = default;

void ImageCanvas::clearState() {
    cleanup();
    m_imgWidth = 0;
    m_imgHeight = 0;
    m_zoom = 1.0f;
    m_panX = 0.0f;
    m_panY = 0.0f;
    m_hasImage = false;
    m_wantFit = true;
    m_wantActual = false;
    filePath.clear();
    fileName.clear();
    releaseVector(sourceBytes);
    releaseVector(m_bgrPixels);
    releaseVector(imageFiles);
    currentIndex = -1;
    m_mouseImgX = -1;
    m_mouseImgY = -1;
    hsvSampleMode = false;
    hsvSampleApplyRequested = false;
    m_hsvSamples.clear();
    cropSelectMode = false;
    cropSelectActive = false;
    cropSelectDone = false;
    cropSelectOwnerId = 0;
    roiCenterSelectMode = false;
    roiCenterSelectDone = false;
    roiCenterSelectOwnerId = 0;
    clearMeasurement();
    clearAngleMeasurement();
}

bool ImageCanvas::uploadTexture(const std::vector<uint8_t>& buf, const std::string& path) {
    if (buf.empty()) return false;
    cv::Mat raw(1, (int)buf.size(), CV_8U, const_cast<uint8_t*>(buf.data()));
    cv::Mat cvImg = cv::imdecode(raw, cv::IMREAD_UNCHANGED);
    if (cvImg.empty()) return false;

    cv::Mat rgba, bgr;
    switch (cvImg.channels()) {
        case 1: cv::cvtColor(cvImg, rgba, cv::COLOR_GRAY2RGBA); cv::cvtColor(cvImg, bgr, cv::COLOR_GRAY2BGR); break;
        case 3: cv::cvtColor(cvImg, rgba, cv::COLOR_BGR2RGBA); bgr = cvImg.clone(); break;
        case 4: cv::cvtColor(cvImg, rgba, cv::COLOR_BGRA2RGBA); cv::cvtColor(cvImg, bgr, cv::COLOR_BGRA2BGR); break;
        default: return false;
    }

    std::vector<uint8_t> newSourceBytes(buf.begin(), buf.end());
    std::vector<uint8_t> newBgrPixels(bgr.datastart, bgr.dataend);

    if (m_texture != 0) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba.cols, rgba.rows,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_imgWidth = rgba.cols;
    m_imgHeight = rgba.rows;
    m_hasImage = true;
    filePath = path;
    fileName = baseName(path);
    sourceBytes.swap(newSourceBytes);
    m_wantFit = true;
    m_bgrPixels.swap(newBgrPixels);
    clearMeasurement();
    clearAngleMeasurement();
    return true;
}

// ============================================================
// Image loading
// ============================================================

bool ImageCanvas::openFile(const std::string& path, bool scanDir) {
    if (!uploadTexture(readFileBuffer(path), path)) return false;
    if (scanDir) scanDirectory(*this, path);
    return true;
}

bool ImageCanvas::loadBytes(const std::vector<uint8_t>& bytes, const std::string& name) {
    if (!uploadTexture(bytes, name)) return false;
    releaseVector(imageFiles);
    currentIndex = -1;
    return true;
}

bool ImageCanvas::loadMat(const cv::Mat& image, const std::string& name) {
    if (image.empty()) return false;
    std::vector<uint8_t> encoded;
    if (!cv::imencode(".png", image, encoded)) return false;
    return loadBytes(encoded, name.empty() ? "Preview.png" : name);
}

bool ImageCanvas::loadDropped(const std::string& pathOrUrl, const std::vector<uint8_t>& bytes) {
    if (!pathOrUrl.empty() && !isNetworkImageUrl(pathOrUrl) && openFile(pathOrUrl))
        return true;
    if (!bytes.empty()) {
        const std::string name = pathOrUrl.empty() ? "Dropped image" : pathOrUrl;
        if (uploadTexture(bytes, name)) {
            releaseVector(imageFiles);
            currentIndex = -1;
            return true;
        }
    }
    if (!isNetworkImageUrl(pathOrUrl)) return false;
#ifdef _WIN32
    if (!uploadTexture(readUrlBuffer(pathOrUrl), pathOrUrl)) return false;
    releaseVector(imageFiles);
    currentIndex = -1;
    return true;
#else
    return false;
#endif
}

void ImageCanvas::clear() {
    clearState();
}

// ============================================================
// View control
// ============================================================

void ImageCanvas::fitToView() { m_wantFit = true; }
void ImageCanvas::setActualSize() { m_wantActual = true; }
float ImageCanvas::zoom() const { return m_zoom; }
void ImageCanvas::setZoom(float z) { m_zoom = z; m_wantFit = false; }

void ImageCanvas::doFitZoom(float cw, float ch) {
    if (!m_hasImage || cw <= 0 || ch <= 0) return;
    float sx = cw / m_imgWidth, sy = ch / m_imgHeight;
    m_zoom = std::min(sx, sy);
    m_panX = (cw - m_imgWidth * m_zoom) / 2.0f;
    m_panY = (ch - m_imgHeight * m_zoom) / 2.0f;
}

void ImageCanvas::doSetActualSize(float cw, float ch) {
    m_zoom = 1.0f;
    m_panX = (cw - m_imgWidth) / 2.0f;
    m_panY = (ch - m_imgHeight) / 2.0f;
}

// ============================================================
// State queries
// ============================================================

bool ImageCanvas::hasImage() const { return m_hasImage; }
int ImageCanvas::imageWidth() const { return m_imgWidth; }
int ImageCanvas::imageHeight() const { return m_imgHeight; }
float ImageCanvas::mouseImgX() const { return m_mouseImgX; }
float ImageCanvas::mouseImgY() const { return m_mouseImgY; }
bool ImageCanvas::isHovered() const { return m_hovered; }

bool ImageCanvas::getPixelBGR(int x, int y, uint8_t& b, uint8_t& g, uint8_t& r) const {
    if (x < 0 || x >= m_imgWidth || y < 0 || y >= m_imgHeight || m_bgrPixels.empty())
        return false;
    int idx = (y * m_imgWidth + x) * 3;
    b = m_bgrPixels[idx];
    g = m_bgrPixels[idx + 1];
    r = m_bgrPixels[idx + 2];
    return true;
}

// ============================================================
// Tool management
// ============================================================

void ImageCanvas::setTool(Tool tool, int ownerId) {
    if (m_tool != tool) {
        if (m_tool == Tool::Measure)
            clearMeasurement();
        if (m_tool == Tool::Angle)
            clearAngleMeasurement();
    }

    // Clear all tool flags
    measureMode = false;
    angleMode = false;
    colorPickerMode = false;
    colorPicked = false;
    cropSelectMode = false;
    cropSelectActive = false;
    cropSelectDone = false;
    cropSelectOwnerId = 0;
    roiCenterSelectMode = false;
    roiCenterSelectDone = false;
    roiCenterSelectOwnerId = 0;
    hsvSampleMode = false;

    // Activate the requested tool
    m_tool = tool;
    m_toolOwnerId = ownerId;
    switch (tool) {
        case Tool::Measure: measureMode = true; break;
        case Tool::Angle: angleMode = true; break;
        case Tool::ColorPicker: colorPickerMode = true; break;
        case Tool::CropSelect: cropSelectMode = true; cropSelectOwnerId = ownerId; break;
        case Tool::RoiCenterSelect: roiCenterSelectMode = true; roiCenterSelectOwnerId = ownerId; break;
        case Tool::HsvSample: hsvSampleMode = true; break;
        default: break;
    }
}

ImageCanvas::Tool ImageCanvas::currentTool() const { return m_tool; }

bool ImageCanvas::consumeCrop(int ownerId, int& x, int& y, int& w, int& h) {
    if (!cropSelectDone || cropSelectOwnerId != ownerId) return false;
    x = cropSelectX;
    y = cropSelectY;
    w = cropSelectW;
    h = cropSelectH;
    cropSelectDone = false;
    cropSelectOwnerId = 0;
    return true;
}

bool ImageCanvas::consumePoint(int ownerId, double& x, double& y) {
    if (!roiCenterSelectDone || roiCenterSelectOwnerId != ownerId) return false;
    x = roiCenterX;
    y = roiCenterY;
    roiCenterSelectDone = false;
    roiCenterSelectOwnerId = 0;
    return true;
}

bool ImageCanvas::consumeHsvSamples(std::vector<HsvSample>& out) {
    if (!hsvSampleApplyRequested) return false;
    out = m_hsvSamples;
    hsvSampleApplyRequested = false;
    return true;
}

const std::vector<ImageCanvas::HsvSample>& ImageCanvas::hsvSamples() const { return m_hsvSamples; }
std::vector<ImageCanvas::HsvSample>& ImageCanvas::hsvSamples() { return m_hsvSamples; }
void ImageCanvas::clearHsvSamples() { m_hsvSamples.clear(); }

void ImageCanvas::clearMeasurement() {
    measurePoints.clear();
    measureActive = false;
}

void ImageCanvas::clearAngleMeasurement() {
    angleClickCount = 0;
    angleCompleted = false;
    angleVertex = ImVec2(0, 0);
    angleFirstEnd = ImVec2(0, 0);
    angleSecondEnd = ImVec2(0, 0);
}

// ============================================================
// File operations
// ============================================================

bool ImageCanvas::saveAs(const std::string& path) const {
    if (!m_hasImage || sourceBytes.empty() || path.empty()) return false;
    const std::string ext = saveExtension(path);
    if (!canEncodeExtension(ext)) return false;
    cv::Mat raw(1, int(sourceBytes.size()), CV_8U, const_cast<uint8_t*>(sourceBytes.data()));
    cv::Mat image = cv::imdecode(raw, cv::IMREAD_UNCHANGED);
    if (image.empty()) return false;
    if (ext == ".jpg" && image.channels() == 4)
        cv::cvtColor(image, image, cv::COLOR_BGRA2BGR);
    std::vector<uint8_t> encoded;
    if (!cv::imencode(ext, image, encoded)) return false;
    return writeFileBuffer(path, encoded);
}

bool ImageCanvas::copyToClipboard() const {
    if (!m_hasImage || sourceBytes.empty()) return false;
#ifdef _WIN32
    cv::Mat raw(1, int(sourceBytes.size()), CV_8U, const_cast<uint8_t*>(sourceBytes.data()));
    cv::Mat image = cv::imdecode(raw, cv::IMREAD_UNCHANGED);
    if (image.empty()) return false;
    cv::Mat bgr;
    switch (image.channels()) {
        case 1: cv::cvtColor(image, bgr, cv::COLOR_GRAY2BGR); break;
        case 3: bgr = image; break;
        case 4: cv::cvtColor(image, bgr, cv::COLOR_BGRA2BGR); break;
        default: return false;
    }
    if (!bgr.isContinuous()) bgr = bgr.clone();
    return setClipboardDib(bgr);
#else
    return false;
#endif
}

bool ImageCanvas::copyPathToClipboard() const {
    if (!m_hasImage || filePath.empty()) return false;
#ifdef _WIN32
    return setClipboardUnicodeText(utf8ToWide(filePath));
#else
    return false;
#endif
}

bool ImageCanvas::deleteCurrent() {
    if (!m_hasImage || filePath.empty() || isNetworkImageUrl(filePath)) return false;
    const std::string deletePath = filePath;
    std::vector<std::string> remaining = imageFiles;
    int nextIndex = -1;
    std::string nextPath;
    if (currentIndex >= 0 && currentIndex < int(remaining.size())) {
        remaining.erase(remaining.begin() + currentIndex);
        if (!remaining.empty()) {
            nextIndex = std::min(currentIndex, int(remaining.size()) - 1);
            nextPath = remaining[nextIndex];
        }
    }
#ifdef _WIN32
    if (!moveFileToRecycleBin(deletePath)) return false;
#else
    if (!std::filesystem::remove(deletePath)) return false;
#endif
    clearState();
    imageFiles = remaining;
    if (!nextPath.empty() && openFile(nextPath, false)) {
        imageFiles = remaining;
        currentIndex = nextIndex;
    }
    return true;
}

// ============================================================
// Overlay
// ============================================================

void ImageCanvas::setOverlay(OverlayFn fn) { m_overlay = std::move(fn); }

// ============================================================
// Cleanup
// ============================================================

void ImageCanvas::cleanup() {
    if (m_texture) { glDeleteTextures(1, &m_texture); m_texture = 0; }
}

// ============================================================
// draw() — migrated from ui.cpp renderImage()
// ============================================================

void ImageCanvas::draw(const ImVec2& size) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##Image", size.x == 0 && size.y == 0 ? ImVec2(0, -28) : size, false, ImGuiWindowFlags_NoScrollbar);

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 sz = ImGui::GetContentRegionAvail();

    if (!m_hasImage) {
        const char* hint = "拖放图片到这里，或按 Ctrl+O 打开";
        ImVec2 ts = ImGui::CalcTextSize(hint);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(pos.x + (sz.x - ts.x) / 2, pos.y + (sz.y - ts.y) / 2),
            IM_COL32(120, 120, 120, 255), hint);
        ImGui::EndChild();
        ImGui::PopStyleVar();
        return;
    }

    const bool hasStableCanvasSize = sz.x >= 32.0f && sz.y >= 32.0f;
    if (m_wantFit && hasStableCanvasSize) { doFitZoom(sz.x, sz.y); m_wantFit = false; }
    if (m_wantActual && hasStableCanvasSize) { doSetActualSize(sz.x, sz.y); m_wantActual = false; }

    const bool imageHovered = ImGui::IsWindowHovered();
    m_hovered = imageHovered;

    if (imageHovered) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            ImVec2 m = ImGui::GetIO().MousePos;
            float mx = m.x - pos.x - m_panX;
            float my = m.y - pos.y - m_panY;
            float ix = mx / m_zoom;
            float iy = my / m_zoom;
            m_zoom *= (wheel > 0) ? 1.15f : (1.0f / 1.15f);
            m_zoom = std::clamp(m_zoom, 0.02f, 64.0f);
            m_panX = m.x - pos.x - ix * m_zoom;
            m_panY = m.y - pos.y - iy * m_zoom;
            m_wantFit = false;
        }

        const int dragButtons[] = { ImGuiMouseButton_Left, ImGuiMouseButton_Middle };
        for (int btn : dragButtons) {
            if ((measureMode || angleMode || colorPickerMode || cropSelectMode ||
                 roiCenterSelectMode || hsvSampleMode) && btn == ImGuiMouseButton_Left)
                continue;
            if (ImGui::IsMouseDragging(btn)) {
                ImVec2 d = ImGui::GetMouseDragDelta(btn);
                m_panX += d.x;
                m_panY += d.y;
                ImGui::ResetMouseDragDelta(btn);
                m_wantFit = false;
            }
        }

        if (!measureMode && !angleMode && !colorPickerMode && !cropSelectMode &&
            !roiCenterSelectMode && !hsvSampleMode && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            m_wantFit = true;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGui::IsAnyItemHovered() &&
            !measureMode && !angleMode && !colorPickerMode && !cropSelectMode &&
            !roiCenterSelectMode && !hsvSampleMode) {
            ImGui::OpenPopup("ImageContextMenu");
            ImGui::SetWindowFocus();
        }
    }

    // Context menu
    pushMenuStyle();
    if (ImGui::BeginPopup("ImageContextMenu")) {
        ImGui::SetNextItemWidth(168.0f);
        if (ImGui::MenuItemEx("另存为", CtxIconSaveAs)) {
            auto path = saveFileDialog(m_window, fileName);
            if (!path.empty()) saveAs(path);
        }
        if (ImGui::MenuItemEx("复制", CtxIconCopy))
            copyToClipboard();
        if (ImGui::MenuItemEx("复制为路径", CtxIconLocation))
            copyPathToClipboard();
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 120, 120, 255));
        if (ImGui::MenuItemEx("删除", CtxIconTrash))
            deleteCurrent();
        ImGui::PopStyleColor();
        ImGui::EndPopup();
    }
    popMenuStyle();

    // Mouse-to-image coordinates
    {
        ImVec2 m = ImGui::GetIO().MousePos;
        float mx = m.x - pos.x - m_panX;
        float my = m.y - pos.y - m_panY;
        float ix = mx / m_zoom;
        float iy = my / m_zoom;
        if (ix >= 0 && ix < m_imgWidth && iy >= 0 && iy < m_imgHeight) {
            m_mouseImgX = ix;
            m_mouseImgY = iy;
        } else {
            m_mouseImgX = m_mouseImgY = -1;
        }
    }

    // Measure mode
    if (measureMode && imageHovered) {
        const bool insideImage = m_mouseImgX >= 0.0f && m_mouseImgY >= 0.0f;
        if (insideImage) {
            float mx = m_mouseImgX, my = m_mouseImgY;
            if (ImGui::GetIO().KeyShift && !measurePoints.empty()) {
                const ImVec2& last = measurePoints.back();
                const float dx = std::abs(mx - last.x);
                const float dy = std::abs(my - last.y);
                if (dx > dy) my = last.y; else mx = last.x;
            }
            measureLiveX = mx;
            measureLiveY = my;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                measurePoints.push_back(ImVec2(mx, my));
                measureActive = true;
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                if (measureActive) measureActive = false;
                else clearMeasurement();
            }
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z) && !measurePoints.empty()) {
                measurePoints.pop_back();
                if (measurePoints.empty()) measureActive = false;
            }
        }
    }

    // Angle measurement mode
    if (angleMode && imageHovered) {
        const bool insideImage = m_mouseImgX >= 0.0f && m_mouseImgY >= 0.0f;
        if (insideImage) {
            ImVec2 point(m_mouseImgX, m_mouseImgY);
            if (ImGui::GetIO().KeyShift && angleClickCount >= 1)
                point = constrainAngle15(angleVertex, point);

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (angleCompleted)
                    clearAngleMeasurement();

                if (angleClickCount == 0) {
                    angleVertex = ImVec2(m_mouseImgX, m_mouseImgY);
                    angleFirstEnd = angleVertex;
                    angleSecondEnd = angleVertex;
                    angleClickCount = 1;
                    angleCompleted = false;
                } else if (angleClickCount == 1) {
                    angleFirstEnd = point;
                    angleSecondEnd = point;
                    angleClickCount = 2;
                } else if (angleClickCount == 2) {
                    angleSecondEnd = point;
                    angleClickCount = 3;
                    angleCompleted = true;
                }
            } else if (!angleCompleted) {
                if (angleClickCount == 1) {
                    angleFirstEnd = point;
                    angleSecondEnd = point;
                } else if (angleClickCount == 2) {
                    angleSecondEnd = point;
                }
            }

            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z) && angleClickCount > 0) {
                angleClickCount--;
                angleCompleted = false;
                if (angleClickCount <= 0)
                    clearAngleMeasurement();
                else if (angleClickCount == 1)
                    angleSecondEnd = angleFirstEnd;
            }
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            clearAngleMeasurement();
    }

    // Color picker mode
    if (colorPickerMode && imageHovered && m_mouseImgX >= 0.0f) {
        int px = (int)m_mouseImgX;
        int py = (int)m_mouseImgY;
        if (px >= 0 && px < m_imgWidth && py >= 0 && py < m_imgHeight && !m_bgrPixels.empty()) {
            if (!colorPicked) {
                int idx = (py * m_imgWidth + px) * 3;
                pickedB = m_bgrPixels[idx];
                pickedG = m_bgrPixels[idx + 1];
                pickedR = m_bgrPixels[idx + 2];
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                colorPicked = !colorPicked;
                if (colorPicked) {
                    pickedImgX = m_mouseImgX;
                    pickedImgY = m_mouseImgY;
                }
            }
        }
    }

    // HSV sample mode
    if (hsvSampleMode && imageHovered && m_mouseImgX >= 0.0f) {
        int px = (int)m_mouseImgX;
        int py = (int)m_mouseImgY;
        if (px >= 0 && px < m_imgWidth && py >= 0 && py < m_imgHeight && !m_bgrPixels.empty()) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                int idx = (py * m_imgWidth + px) * 3;
                cv::Mat bgr(1, 1, CV_8UC3, cv::Scalar(m_bgrPixels[idx], m_bgrPixels[idx + 1], m_bgrPixels[idx + 2]));
                cv::Mat hsv;
                cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
                cv::Vec3b pixel = hsv.at<cv::Vec3b>(0, 0);
                m_hsvSamples.push_back({ px, py, int(pixel[0]), int(pixel[1]), int(pixel[2]) });
            }
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z) && !m_hsvSamples.empty())
                m_hsvSamples.pop_back();
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            hsvSampleMode = false;
            hsvSampleApplyRequested = !m_hsvSamples.empty();
        }
    }

    // ROI center select mode
    if (roiCenterSelectMode && imageHovered) {
        const bool insideImage = m_mouseImgX >= 0.0f && m_mouseImgY >= 0.0f;
        if (insideImage && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            roiCenterX = m_mouseImgX;
            roiCenterY = m_mouseImgY;
            roiCenterSelectDone = true;
            roiCenterSelectMode = false;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            roiCenterSelectMode = false;
            roiCenterSelectDone = false;
            roiCenterSelectOwnerId = 0;
        }
    }

    // Crop select mode
    if (cropSelectMode && imageHovered) {
        const bool insideImage = m_mouseImgX >= 0.0f && m_mouseImgY >= 0.0f;
        if (insideImage) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                cropSelectActive = true;
                cropSelectStart = ImVec2(m_mouseImgX, m_mouseImgY);
                cropSelectEnd = cropSelectStart;
            }
            if (cropSelectActive && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                cropSelectEnd = ImVec2(m_mouseImgX, m_mouseImgY);
            if (cropSelectActive && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                int x1 = std::min(int(cropSelectStart.x), int(cropSelectEnd.x));
                int y1 = std::min(int(cropSelectStart.y), int(cropSelectEnd.y));
                int x2 = std::max(int(cropSelectStart.x), int(cropSelectEnd.x));
                int y2 = std::max(int(cropSelectStart.y), int(cropSelectEnd.y));
                int w = x2 - x1, h = y2 - y1;
                if (w > 0 && h > 0) {
                    cropSelectX = x1;
                    cropSelectY = y1;
                    cropSelectW = w;
                    cropSelectH = h;
                    cropSelectDone = true;
                }
                cropSelectMode = false;
                cropSelectActive = false;
            }
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            cropSelectMode = false;
            cropSelectActive = false;
            cropSelectDone = false;
            cropSelectOwnerId = 0;
        }
    }

    // --- Rendering ---
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(pos, ImVec2(pos.x + sz.x, pos.y + sz.y), true);

    ImVec2 imgMin(pos.x + m_panX, pos.y + m_panY);
    ImVec2 imgMax(imgMin.x + m_imgWidth * m_zoom, imgMin.y + m_imgHeight * m_zoom);

    dl->AddRectFilled(imgMin, imgMax, IM_COL32(20, 20, 20, 255));
    dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(m_texture)), imgMin, imgMax);

    // Measurement overlay
    if (!measurePoints.empty()) {
        const ImU32 liveCol = IM_COL32(255, 255, 100, 200);
        constexpr int numColors = ImageCanvas::MeasureSegColorCount;
        const float z = m_zoom;

        auto toScreen = [&](const ImVec2& p) -> ImVec2 {
            return ImVec2(imgMin.x + p.x * z, imgMin.y + p.y * z);
        };

        for (int i = 0; i < (int)measurePoints.size(); i++) {
            ImVec2 p = toScreen(measurePoints[i]);
            if (i > 0) {
                ImU32 col = ImageCanvas::MeasureSegColors[(i - 1) % numColors];
                ImVec2 prev = toScreen(measurePoints[i - 1]);
                dl->AddLine(prev, p, col, 2.0f);
                double dx = p.x - prev.x, dy = p.y - prev.y;
                char label[64];
                ImFormatString(label, sizeof(label), "%.2f", std::sqrt(dx * dx + dy * dy) / z);
                ImVec2 mid((prev.x + p.x) * 0.5f, (prev.y + p.y) * 0.5f - 18.0f);
                dl->AddText(mid + ImVec2(1, 1), IM_COL32(0, 0, 0, 180), label);
                dl->AddText(mid, col, label);
            }
            ImU32 dotCol = (i > 0) ? ImageCanvas::MeasureSegColors[(i - 1) % numColors] : ImageCanvas::MeasureSegColors[0];
            dl->AddCircleFilled(p, 3.5f, dotCol);
        }

        if (measureActive && measurePoints.size() >= 1) {
            ImVec2 last = toScreen(measurePoints.back());
            ImVec2 live = toScreen(ImVec2(measureLiveX, measureLiveY));
            dl->AddLine(last, live, liveCol, 1.5f);
        }

        if (!measureActive && measurePoints.size() >= 2) {
            double total = 0;
            for (int i = 1; i < (int)measurePoints.size(); i++) {
                double dx = measurePoints[i].x - measurePoints[i - 1].x;
                double dy = measurePoints[i].y - measurePoints[i - 1].y;
                total += std::sqrt(dx * dx + dy * dy);
            }
            char label[64];
            ImFormatString(label, sizeof(label), "总计: %.2f 像素", total);
            ImVec2 last = toScreen(measurePoints.back());
            dl->AddText(last + ImVec2(8, 8), IM_COL32(0, 0, 0, 180), label);
            dl->AddText(last + ImVec2(7, 7), IM_COL32(255, 80, 80, 255), label);
        }
    }

    // Angle measurement overlay
    if (angleClickCount > 0) {
        const float z = m_zoom;
        const ImU32 firstLineCol = IM_COL32(80, 160, 255, 255);
        const ImU32 secondLineCol = IM_COL32(80, 220, 80, 255);
        const ImU32 angleCol = IM_COL32(255, 80, 80, 255);

        auto toScreen = [&](const ImVec2& p) -> ImVec2 {
            return ImVec2(imgMin.x + p.x * z, imgMin.y + p.y * z);
        };

        const ImVec2 vertex = toScreen(angleVertex);
        const ImVec2 firstEnd = toScreen(angleFirstEnd);
        const ImVec2 secondEnd = toScreen(angleSecondEnd);

        addDashedLine(dl, vertex, firstEnd, firstLineCol, 2.0f);
        dl->AddCircleFilled(vertex, 4.0f, angleCol);
        dl->AddCircleFilled(firstEnd, 3.5f, firstLineCol);

        if (angleClickCount >= 2) {
            addDashedLine(dl, vertex, secondEnd, secondLineCol, 2.0f);
            dl->AddCircleFilled(secondEnd, 3.5f, secondLineCol);

            const double angle1 = calculateImageAngle(angleVertex, angleFirstEnd);
            const double angle2 = calculateImageAngle(angleVertex, angleSecondEnd);
            const double angleDiff = calculateAngleDifference(angle1, angle2);
            drawAngleArc(dl, vertex, 40.0f, angle1, angle2, angleCol, 2.0f);

            char label[64];
            ImFormatString(label, sizeof(label), "角度: %.2f°", angleDiff);
            const double midAngle = angleArcMidpoint(angle1, angle2) * Pi / 180.0;
            const ImVec2 labelPos(
                vertex.x + 60.0f * float(std::cos(midAngle)),
                vertex.y + 60.0f * float(std::sin(midAngle))
            );
            dl->AddText(labelPos + ImVec2(1, 1), IM_COL32(0, 0, 0, 180), label);
            dl->AddText(labelPos, angleCol, label);
        }
    }

    // HSV sample overlay
    if (!m_hsvSamples.empty()) {
        const float z = m_zoom;
        constexpr int numColors = ImageCanvas::MeasureSegColorCount;
        for (int i = 0; i < (int)m_hsvSamples.size(); ++i) {
            const auto& sample = m_hsvSamples[size_t(i)];
            ImVec2 p(imgMin.x + (float(sample.x) + 0.5f) * z, imgMin.y + (float(sample.y) + 0.5f) * z);
            char label[16];
            ImFormatString(label, sizeof(label), "%d", i + 1);
            ImU32 dotCol = ImageCanvas::MeasureSegColors[i % numColors];
            dl->AddCircle(p, 6.0f, IM_COL32(0, 0, 0, 180), 0, 2.0f);
            dl->AddCircleFilled(p, 4.0f, dotCol);
            dl->AddText(p + ImVec2(7, -13), IM_COL32(0, 0, 0, 180), label);
            dl->AddText(p + ImVec2(6, -14), dotCol, label);
        }
    }

    // Crosshair
    bool showCross = (measureMode && imageHovered && m_mouseImgX >= 0.0f) ||
                     (angleMode && imageHovered && m_mouseImgX >= 0.0f) ||
                     (colorPickerMode && ((imageHovered && m_mouseImgX >= 0.0f) || colorPicked)) ||
                     (roiCenterSelectMode && imageHovered && m_mouseImgX >= 0.0f) ||
                     (hsvSampleMode && imageHovered && m_mouseImgX >= 0.0f);
    if (showCross) {
        float crossImgX = (colorPickerMode && colorPicked) ? pickedImgX : m_mouseImgX;
        float crossImgY = (colorPickerMode && colorPicked) ? pickedImgY : m_mouseImgY;
        float mx = imgMin.x + crossImgX * m_zoom;
        float my = imgMin.y + crossImgY * m_zoom;
        ImU32 crossCol = (colorPickerMode && colorPicked) ? IM_COL32(255, 255, 0, 220) : IM_COL32(0, 255, 0, 200);
        if (hsvSampleMode) crossCol = IM_COL32(255, 180, 40, 220);
        if (roiCenterSelectMode) crossCol = IM_COL32(0, 200, 255, 220);
        bool isLocked = colorPickerMode && colorPicked;
        if (isLocked) {
            dl->AddLine(ImVec2(pos.x, my), ImVec2(pos.x + sz.x, my), crossCol, 2.0f);
            dl->AddLine(ImVec2(mx, pos.y), ImVec2(mx, pos.y + sz.y), crossCol, 2.0f);
        } else {
            float dashLen = 8.0f, gapLen = 4.0f;
            for (float x = pos.x; x < pos.x + sz.x; x += dashLen + gapLen) {
                float x2 = (x + dashLen < pos.x + sz.x) ? x + dashLen : pos.x + sz.x;
                dl->AddLine(ImVec2(x, my), ImVec2(x2, my), crossCol, 2.0f);
            }
            for (float y = pos.y; y < pos.y + sz.y; y += dashLen + gapLen) {
                float y2 = (y + dashLen < pos.y + sz.y) ? y + dashLen : pos.y + sz.y;
                dl->AddLine(ImVec2(mx, y), ImVec2(mx, y2), crossCol, 2.0f);
            }
        }
    }

    // Crop select overlay
    if (cropSelectMode && cropSelectActive) {
        const float z = m_zoom;
        ImVec2 s1(imgMin.x + cropSelectStart.x * z, imgMin.y + cropSelectStart.y * z);
        ImVec2 s2(imgMin.x + cropSelectEnd.x * z, imgMin.y + cropSelectEnd.y * z);
        ImVec2 rMin(std::min(s1.x, s2.x), std::min(s1.y, s2.y));
        ImVec2 rMax(std::max(s1.x, s2.x), std::max(s1.y, s2.y));
        dl->AddRectFilled(rMin, rMax, IM_COL32(0, 150, 255, 50));
        dl->AddRect(rMin, rMax, IM_COL32(0, 150, 255, 200), 0.0f, 0, 2.0f);
        char label[64];
        ImFormatString(label, sizeof(label), "%d x %d",
            std::abs(int(cropSelectEnd.x) - int(cropSelectStart.x)),
            std::abs(int(cropSelectEnd.y) - int(cropSelectStart.y)));
        dl->AddText(ImVec2(rMin.x, rMin.y - 18.0f), IM_COL32(0, 180, 255, 255), label);
    }

    // External overlay callback
    if (m_overlay)
        m_overlay(*dl, imgMin, m_zoom);

    dl->PopClipRect();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

// ============================================================
// drawStatus() — migrated from ui.cpp renderStatus()
// ============================================================

void ImageCanvas::drawStatus() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    ImGui::BeginChild("##Status", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f);

    const float statusWidth = ImGui::GetContentRegionAvail().x;
    const bool compact = statusWidth < 620.0f;
    const int columnCount = compact ? 3 : 5;
    const ImGuiTableFlags tableFlags =
        ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_NoPadOuterX |
        ImGuiTableFlags_NoPadInnerX;

    if (ImGui::BeginTable("##StatusTable", columnCount, tableFlags)) {
        ImGui::TableSetupColumn("coord", ImGuiTableColumnFlags_WidthStretch, compact ? 1.35f : 1.2f);
        ImGui::TableSetupColumn("zoom", ImGuiTableColumnFlags_WidthStretch, compact ? 0.85f : 0.9f);
        ImGui::TableSetupColumn("dims", ImGuiTableColumnFlags_WidthStretch, compact ? 1.2f : 1.1f);
        if (!compact) {
            ImGui::TableSetupColumn("index", ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableSetupColumn("bytes", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (m_hasImage && m_mouseImgX >= 0)
            ImGui::Text("坐标: (%d, %d)", (int)m_mouseImgX, (int)m_mouseImgY);
        else
            ImGui::TextUnformatted("坐标: -");

        ImGui::TableNextColumn();
        ImGui::Text("缩放: %.0f%%", m_zoom * 100.0f);

        ImGui::TableNextColumn();
        if (m_hasImage)
            ImGui::Text("尺寸: %d x %d", m_imgWidth, m_imgHeight);

        if (!compact) {
            ImGui::TableNextColumn();
            if (m_hasImage && currentIndex >= 0)
                ImGui::Text("%d/%d", currentIndex + 1, (int)imageFiles.size());

            ImGui::TableNextColumn();
            if (m_hasImage)
                ImGui::Text("大小: %s", formatFileSize(sourceBytes.size()).c_str());
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
}
