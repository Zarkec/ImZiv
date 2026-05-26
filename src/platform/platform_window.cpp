#include "platform/platform_window.hpp"

#include "imgui.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#include <ole2.h>
#include <shellapi.h>
#include <timeapi.h>
#include <windows.h>
#include <windowsx.h>
#endif

namespace {
    constexpr int MinWindowWidth = 480;
    constexpr int MinWindowHeight = 360;
    constexpr size_t MaxDroppedImageBytes = 256 * 1024 * 1024;

#ifdef _WIN32
    WNDPROC g_oldWndProc = nullptr;
    GLFWwindow* g_mainWindow = nullptr;
    float g_titleBarHeight = 28.0f;
    IDropTarget* g_nativeDropTarget = nullptr;
    bool g_oleDropInitialized = false;

    FORMATETC makeDropFormat(CLIPFORMAT format, DWORD tymed = TYMED_HGLOBAL, LONG index = -1) {
        FORMATETC result = {};
        result.cfFormat = format;
        result.dwAspect = DVASPECT_CONTENT;
        result.lindex = index;
        result.tymed = tymed;
        return result;
    }

    bool hasDropFormat(IDataObject* dataObject, CLIPFORMAT format, DWORD tymed = TYMED_HGLOBAL, LONG index = -1) {
        if (dataObject == nullptr || format == 0)
            return false;

        FORMATETC dropFormat = makeDropFormat(format, tymed, index);
        return SUCCEEDED(dataObject->QueryGetData(&dropFormat));
    }

    std::string wideToUtf8(const std::wstring& text) {
        if (text.empty())
            return {};

        int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (length <= 0)
            return {};

        std::string result(length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), length, nullptr, nullptr);
        result.pop_back();
        return result;
    }

    std::string ansiToUtf8(const std::string& text) {
        if (text.empty())
            return {};

        int length = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, nullptr, 0);
        if (length <= 0)
            return {};

        std::wstring wide(length, L'\0');
        MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, wide.data(), length);
        wide.pop_back();
        return wideToUtf8(wide);
    }

    std::string getWideDropText(IDataObject* dataObject, CLIPFORMAT format) {
        FORMATETC dropFormat = makeDropFormat(format);
        STGMEDIUM medium = {};
        if (FAILED(dataObject->GetData(&dropFormat, &medium)))
            return {};

        const wchar_t* value = static_cast<const wchar_t*>(GlobalLock(medium.hGlobal));
        std::string result;
        if (value != nullptr) {
            result = wideToUtf8(value);
            GlobalUnlock(medium.hGlobal);
        }
        ReleaseStgMedium(&medium);
        return result;
    }

    std::string getAnsiDropText(IDataObject* dataObject, CLIPFORMAT format) {
        FORMATETC dropFormat = makeDropFormat(format);
        STGMEDIUM medium = {};
        if (FAILED(dataObject->GetData(&dropFormat, &medium)))
            return {};

        const char* value = static_cast<const char*>(GlobalLock(medium.hGlobal));
        std::string result;
        if (value != nullptr) {
            result = ansiToUtf8(value);
            GlobalUnlock(medium.hGlobal);
        }
        ReleaseStgMedium(&medium);
        return result;
    }

    bool isDropSeparator(char value) {
        return value == '\0' || std::isspace(static_cast<unsigned char>(value)) ||
               value == '"' || value == '\'' || value == '<' || value == '>';
    }

    std::string firstHttpUrl(const std::string& text) {
        std::string lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });

        size_t http = lower.find("http://");
        size_t https = lower.find("https://");
        size_t begin = std::min(http, https);
        if (begin == std::string::npos)
            begin = http == std::string::npos ? https : http;
        if (begin == std::string::npos)
            return {};

        size_t end = begin;
        while (end < text.size() && !isDropSeparator(text[end]))
            ++end;
        return text.substr(begin, end - begin);
    }

    int hexValue(char value) {
        if (value >= '0' && value <= '9')
            return value - '0';
        value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
        if (value >= 'a' && value <= 'f')
            return value - 'a' + 10;
        return -1;
    }

    std::string percentDecode(const std::string& value) {
        std::string result;
        result.reserve(value.size());
        for (size_t index = 0; index < value.size(); ++index) {
            if (value[index] == '%' && index + 2 < value.size()) {
                int high = hexValue(value[index + 1]);
                int low = hexValue(value[index + 2]);
                if (high >= 0 && low >= 0) {
                    result.push_back(static_cast<char>((high << 4) | low));
                    index += 2;
                    continue;
                }
            }
            result.push_back(value[index]);
        }
        return result;
    }

    std::string firstFileUriPath(const std::string& text) {
        std::string lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });

        size_t begin = lower.find("file:");
        if (begin == std::string::npos)
            return {};

        size_t end = begin;
        while (end < text.size() && !isDropSeparator(text[end]))
            ++end;

        std::string path = percentDecode(text.substr(begin + 5, end - begin - 5));
        if (path.rfind("///", 0) == 0)
            path.erase(0, 2);
        else if (path.rfind("//localhost/", 0) == 0)
            path.erase(0, 11);
        else if (path.rfind("//", 0) == 0)
            path = "\\\\" + path.substr(2);

        if (path.size() >= 3 && path[0] == '/' && std::isalpha(static_cast<unsigned char>(path[1])) && path[2] == ':')
            path.erase(0, 1);

        std::replace(path.begin(), path.end(), '/', '\\');
        return path;
    }

    std::string getFirstDroppedFile(IDataObject* dataObject) {
        FORMATETC dropFormat = makeDropFormat(CF_HDROP);
        STGMEDIUM medium = {};
        if (FAILED(dataObject->GetData(&dropFormat, &medium)))
            return {};

        HDROP drop = reinterpret_cast<HDROP>(medium.hGlobal);
        UINT length = DragQueryFileW(drop, 0, nullptr, 0);
        std::vector<wchar_t> path(length + 1, L'\0');
        std::string result;
        if (length > 0 && DragQueryFileW(drop, 0, path.data(), length + 1) > 0)
            result = wideToUtf8(path.data());

        ReleaseStgMedium(&medium);
        return result;
    }

    std::vector<uint8_t> getGlobalDropBytes(HGLOBAL global) {
        if (global == nullptr)
            return {};

        SIZE_T size = GlobalSize(global);
        if (size == 0 || size > MaxDroppedImageBytes)
            return {};

        const auto* value = static_cast<const uint8_t*>(GlobalLock(global));
        if (value == nullptr)
            return {};

        std::vector<uint8_t> result(value, value + size);
        GlobalUnlock(global);
        return result;
    }

    std::vector<uint8_t> getStreamDropBytes(IStream* stream) {
        if (stream == nullptr)
            return {};

        LARGE_INTEGER zero = {};
        ULARGE_INTEGER start = {};
        stream->Seek(zero, STREAM_SEEK_SET, &start);

        std::vector<uint8_t> result;
        std::vector<uint8_t> chunk(64 * 1024);
        while (true) {
            ULONG read = 0;
            HRESULT status = stream->Read(chunk.data(), ULONG(chunk.size()), &read);
            if (FAILED(status))
                return {};
            if (read == 0)
                break;
            if (result.size() + read > MaxDroppedImageBytes)
                return {};

            result.insert(result.end(), chunk.begin(), chunk.begin() + read);
            if (status == S_FALSE)
                break;
        }

        return result;
    }

    std::vector<uint8_t> getFileContentsBytes(IDataObject* dataObject, CLIPFORMAT fileContents) {
        FORMATETC dropFormat = makeDropFormat(fileContents, TYMED_ISTREAM | TYMED_HGLOBAL, 0);
        STGMEDIUM medium = {};
        if (FAILED(dataObject->GetData(&dropFormat, &medium)))
            return {};

        std::vector<uint8_t> result;
        if (medium.tymed == TYMED_HGLOBAL)
            result = getGlobalDropBytes(medium.hGlobal);
        else if (medium.tymed == TYMED_ISTREAM)
            result = getStreamDropBytes(medium.pstm);

        ReleaseStgMedium(&medium);
        return result;
    }

    class NativeDropTarget final : public IDropTarget {
    public:
        explicit NativeDropTarget(NativeDropCallback callback)
            : callback_(callback),
              urlWide_(RegisterClipboardFormatW(L"UniformResourceLocatorW")),
              urlAnsi_(RegisterClipboardFormatW(L"UniformResourceLocator")),
              uriList_(RegisterClipboardFormatW(L"text/uri-list")),
              fileContents_(RegisterClipboardFormatW(L"FileContents")),
              fileNameWide_(RegisterClipboardFormatW(L"FileNameW")),
              fileNameAnsi_(RegisterClipboardFormatW(L"FileName")) {}

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
            if (object == nullptr)
                return E_POINTER;
            *object = nullptr;

            if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_IDropTarget)) {
                *object = static_cast<IDropTarget*>(this);
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override {
            return ULONG(InterlockedIncrement(&refCount_));
        }

        ULONG STDMETHODCALLTYPE Release() override {
            ULONG count = ULONG(InterlockedDecrement(&refCount_));
            if (count == 0)
                delete this;
            return count;
        }

        HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* dataObject, DWORD, POINTL, DWORD* effect) override {
            setDropEffect(dataObject, effect);
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* effect) override {
            if (effect != nullptr)
                *effect = hasSupportedData_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE DragLeave() override {
            hasSupportedData_ = false;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE Drop(IDataObject* dataObject, DWORD, POINTL, DWORD* effect) override {
            NativeDropData data = getDroppedData(dataObject);
            if ((!data.value.empty() || !data.bytes.empty()) && callback_ != nullptr) {
                callback_(std::move(data));
                if (effect != nullptr)
                    *effect = DROPEFFECT_COPY;
            } else if (effect != nullptr) {
                *effect = DROPEFFECT_NONE;
            }
            hasSupportedData_ = false;
            return S_OK;
        }

    private:
        bool supports(IDataObject* dataObject) const {
            return hasDropFormat(dataObject, urlWide_) || hasDropFormat(dataObject, urlAnsi_) ||
                   hasDropFormat(dataObject, uriList_) || hasDropFormat(dataObject, CF_UNICODETEXT) ||
                   hasDropFormat(dataObject, CF_TEXT) || hasDropFormat(dataObject, CF_HDROP) ||
                   hasDropFormat(dataObject, fileContents_, TYMED_ISTREAM | TYMED_HGLOBAL, 0) ||
                   hasDropFormat(dataObject, fileNameWide_) || hasDropFormat(dataObject, fileNameAnsi_);
        }

        void setDropEffect(IDataObject* dataObject, DWORD* effect) {
            hasSupportedData_ = supports(dataObject);
            if (effect != nullptr)
                *effect = hasSupportedData_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        }

        std::string getDroppedUrl(IDataObject* dataObject) const {
            if (hasDropFormat(dataObject, urlWide_)) {
                std::string url = firstHttpUrl(getWideDropText(dataObject, urlWide_));
                if (!url.empty())
                    return url;
            }
            if (hasDropFormat(dataObject, urlAnsi_)) {
                std::string url = firstHttpUrl(getAnsiDropText(dataObject, urlAnsi_));
                if (!url.empty())
                    return url;
            }
            if (hasDropFormat(dataObject, uriList_)) {
                std::string url = firstHttpUrl(getAnsiDropText(dataObject, uriList_));
                if (!url.empty())
                    return url;
            }
            if (hasDropFormat(dataObject, CF_UNICODETEXT)) {
                std::string url = firstHttpUrl(getWideDropText(dataObject, CF_UNICODETEXT));
                if (!url.empty())
                    return url;
            }
            if (hasDropFormat(dataObject, CF_TEXT))
                return firstHttpUrl(getAnsiDropText(dataObject, CF_TEXT));
            return {};
        }

        std::string getDroppedFileUriPath(IDataObject* dataObject) const {
            if (hasDropFormat(dataObject, uriList_)) {
                std::string path = firstFileUriPath(getAnsiDropText(dataObject, uriList_));
                if (!path.empty())
                    return path;
            }
            if (hasDropFormat(dataObject, CF_UNICODETEXT)) {
                std::string path = firstFileUriPath(getWideDropText(dataObject, CF_UNICODETEXT));
                if (!path.empty())
                    return path;
            }
            if (hasDropFormat(dataObject, CF_TEXT))
                return firstFileUriPath(getAnsiDropText(dataObject, CF_TEXT));
            return {};
        }

        std::string getDroppedFileName(IDataObject* dataObject) const {
            if (hasDropFormat(dataObject, fileNameWide_)) {
                std::string path = getWideDropText(dataObject, fileNameWide_);
                if (!path.empty())
                    return path;
            }
            if (hasDropFormat(dataObject, fileNameAnsi_))
                return getAnsiDropText(dataObject, fileNameAnsi_);
            return {};
        }

        NativeDropData getDroppedData(IDataObject* dataObject) const {
            NativeDropData result;

            if (hasDropFormat(dataObject, CF_HDROP))
                result.value = getFirstDroppedFile(dataObject);
            if (result.value.empty())
                result.value = getDroppedFileName(dataObject);
            if (result.value.empty())
                result.value = getDroppedFileUriPath(dataObject);

            if (hasDropFormat(dataObject, fileContents_, TYMED_ISTREAM | TYMED_HGLOBAL, 0))
                result.bytes = getFileContentsBytes(dataObject, fileContents_);

            if (result.value.empty())
                result.value = getDroppedUrl(dataObject);
            return result;
        }

        volatile LONG refCount_ = 1;
        NativeDropCallback callback_ = nullptr;
        CLIPFORMAT urlWide_ = 0;
        CLIPFORMAT urlAnsi_ = 0;
        CLIPFORMAT uriList_ = 0;
        CLIPFORMAT fileContents_ = 0;
        CLIPFORMAT fileNameWide_ = 0;
        CLIPFORMAT fileNameAnsi_ = 0;
        bool hasSupportedData_ = false;
    };

    float getWindowScale(HWND hwnd) {
        UINT dpi = 96;
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32) {
            using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
            auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
                GetProcAddress(user32, "GetDpiForWindow"));
            if (getDpiForWindow)
                dpi = getDpiForWindow(hwnd);
        }
        return static_cast<float>(dpi) / 96.0f;
    }

    LRESULT callOldWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (g_oldWndProc)
            return CallWindowProcW(g_oldWndProc, hwnd, msg, wParam, lParam);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    bool imguiHasHoveredItem() {
        return ImGui::GetCurrentContext() != nullptr && ImGui::IsAnyItemHovered();
    }

    LRESULT CALLBACK borderlessWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
            case WM_MOVE:
            case WM_SIZE:
            case WM_SIZING:
            case WM_WINDOWPOSCHANGED:
                InvalidateRect(hwnd, nullptr, FALSE);
                break;

            case WM_NCACTIVATE:
            case WM_NCPAINT:
                return DefWindowProcW(hwnd, msg, wParam, lParam);

            case WM_NCCALCSIZE: {
                if (wParam == TRUE) {
                    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                    RECT client = params->rgrc[0];

                    callOldWindowProc(hwnd, msg, wParam, lParam);

                    if (IsZoomed(hwnd)) {
                        WINDOWINFO wi = {};
                        wi.cbSize = sizeof(WINDOWINFO);
                        GetWindowInfo(hwnd, &wi);

                        params->rgrc[0].left   = client.left + wi.cyWindowBorders;
                        params->rgrc[0].top    = client.top + wi.cyWindowBorders;
                        params->rgrc[0].right  = client.right - wi.cyWindowBorders;
                        params->rgrc[0].bottom = client.bottom - wi.cyWindowBorders + 1;
                    } else {
                        params->rgrc[0] = client;
                    }
                } else {
                    auto* rect = reinterpret_cast<RECT*>(lParam);
                    RECT client = *rect;
                    callOldWindowProc(hwnd, msg, wParam, lParam);
                    *rect = client;
                }

                LARGE_INTEGER frequency = {};
                QueryPerformanceFrequency(&frequency);

                TIMECAPS tc = {};
                if (timeGetDevCaps(&tc, sizeof(tc)) == TIMERR_NOERROR) {
                    timeBeginPeriod(tc.wPeriodMin);

                    DWM_TIMING_INFO dti = {};
                    dti.cbSize = sizeof(dti);
                    if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &dti)) && dti.qpcRefreshPeriod > 0) {
                        LARGE_INTEGER now = {};
                        QueryPerformanceCounter(&now);
                        const LONGLONG delta = dti.qpcVBlank - now.QuadPart;
                        const LONGLONG sleepTicks = delta >= 0
                            ? delta / dti.qpcRefreshPeriod
                            : -1 + delta / dti.qpcRefreshPeriod;
                        const LONGLONG sleepRemainder = delta - dti.qpcRefreshPeriod * sleepTicks;
                        const double sleepMs = std::round(1000.0 * double(sleepRemainder) / double(frequency.QuadPart));
                        if (sleepMs >= 0.0)
                            Sleep(DWORD(sleepMs));
                    }

                    timeEndPeriod(tc.wPeriodMin);
                }

                InvalidateRect(hwnd, nullptr, FALSE);
                return WVR_REDRAW;
            }

            case WM_ERASEBKGND:
                return 1;

            case WM_WINDOWPOSCHANGING: {
                auto* windowPos = reinterpret_cast<LPWINDOWPOS>(lParam);
                windowPos->flags |= SWP_NOCOPYBITS;
                break;
            }

            case WM_NCHITTEST: {
                if (g_mainWindow != nullptr && glfwGetWindowMonitor(g_mainWindow) != nullptr)
                    return HTCLIENT;

                RECT windowRect = {};
                if (!GetWindowRect(hwnd, &windowRect))
                    return HTNOWHERE;

                POINT cursor = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                const float scale = getWindowScale(hwnd);
                const LONG borderX = LONG((GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER)) * scale);
                const LONG borderY = LONG((GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER)) * scale);
                const LONG titleTopResizeY = std::max<LONG>(2, LONG(4.0f * scale));

                constexpr int RegionClient = 0;
                constexpr int RegionLeft = 1;
                constexpr int RegionRight = 2;
                constexpr int RegionTop = 4;
                constexpr int RegionBottom = 8;

                const int region =
                    (cursor.x < windowRect.left + borderX ? RegionLeft : 0) |
                    (cursor.x >= windowRect.right - borderX ? RegionRight : 0) |
                    (cursor.y < windowRect.top + titleTopResizeY ? RegionTop : 0) |
                    (cursor.y >= windowRect.bottom - borderY ? RegionBottom : 0);

                if (region != RegionClient && imguiHasHoveredItem())
                    break;

                switch (region) {
                    case RegionLeft: return HTLEFT;
                    case RegionRight: return HTRIGHT;
                    case RegionTop: return HTTOP;
                    case RegionBottom: return HTBOTTOM;
                    case RegionTop | RegionLeft: return HTTOPLEFT;
                    case RegionTop | RegionRight: return HTTOPRIGHT;
                    case RegionBottom | RegionLeft: return HTBOTTOMLEFT;
                    case RegionBottom | RegionRight: return HTBOTTOMRIGHT;
                    default:
                        if (cursor.y < windowRect.top + LONG(g_titleBarHeight) && !imguiHasHoveredItem())
                            return HTCAPTION;
                        break;
                }
                break;
            }

            case WM_SETCURSOR:
                if (LOWORD(lParam) == HTCLIENT && ImGui::GetCurrentContext() != nullptr) {
                    switch (ImGui::GetMouseCursor()) {
                        case ImGuiMouseCursor_Hand: SetCursor(LoadCursor(nullptr, IDC_HAND)); return TRUE;
                        case ImGuiMouseCursor_ResizeEW: SetCursor(LoadCursor(nullptr, IDC_SIZEWE)); return TRUE;
                        case ImGuiMouseCursor_ResizeNS: SetCursor(LoadCursor(nullptr, IDC_SIZENS)); return TRUE;
                        case ImGuiMouseCursor_ResizeNWSE: SetCursor(LoadCursor(nullptr, IDC_SIZENWSE)); return TRUE;
                        case ImGuiMouseCursor_ResizeNESW: SetCursor(LoadCursor(nullptr, IDC_SIZENESW)); return TRUE;
                        case ImGuiMouseCursor_TextInput: SetCursor(LoadCursor(nullptr, IDC_IBEAM)); return TRUE;
                        default: SetCursor(LoadCursor(nullptr, IDC_ARROW)); return TRUE;
                    }
                }
                break;
        }

        return callOldWindowProc(hwnd, msg, wParam, lParam);
    }
#endif

    std::filesystem::path findAssetPath(const std::filesystem::path& relativePath) {
        std::vector<std::filesystem::path> roots;

#ifdef _WIN32
        wchar_t exePath[MAX_PATH] = {};
        DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        if (len > 0 && len < MAX_PATH)
            roots.push_back(std::filesystem::path(exePath).parent_path());
#endif

        roots.push_back(std::filesystem::current_path());
        roots.push_back(std::filesystem::current_path().parent_path());

        for (const auto& root : roots) {
            std::filesystem::path candidate = root / relativePath;
            if (std::filesystem::exists(candidate))
                return candidate;
        }

        return {};
    }

    bool loadPngIcon(GLFWimage& image, std::vector<unsigned char>& pixels) {
        const auto iconPath = findAssetPath("assets/icons/icon.png");
        if (iconPath.empty())
            return false;

        cv::Mat icon = cv::imread(iconPath.string(), cv::IMREAD_UNCHANGED);
        if (icon.empty())
            return false;

        cv::Mat rgba;
        switch (icon.channels()) {
            case 1: cv::cvtColor(icon, rgba, cv::COLOR_GRAY2RGBA); break;
            case 3: cv::cvtColor(icon, rgba, cv::COLOR_BGR2RGBA); break;
            case 4: cv::cvtColor(icon, rgba, cv::COLOR_BGRA2RGBA); break;
            default: return false;
        }

        if (!rgba.isContinuous())
            rgba = rgba.clone();

        pixels.assign(rgba.data, rgba.data + rgba.total() * rgba.elemSize());
        image.width = rgba.cols;
        image.height = rgba.rows;
        image.pixels = pixels.data();
        return true;
    }
}

void setupBorderlessWindow(GLFWwindow* window) {
#ifdef _WIN32
    g_mainWindow = window;
    HWND hwnd = glfwGetWin32Window(window);

    g_oldWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(borderlessWindowProc)));

    MARGINS borderless = { 1, 1, 1, 1 };
    DwmExtendFrameIntoClientArea(hwnd, &borderless);

    constexpr DWORD renderingPolicy = DWMNCRP_ENABLED;
    DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &renderingPolicy, sizeof(renderingPolicy));

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
    constexpr BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

    applyBorderlessWindowStyle(window);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOMOVE);
#else
    (void)window;
#endif
}

void applyBorderlessWindowStyle(GLFWwindow* window) {
#ifdef _WIN32
    HWND hwnd = glfwGetWin32Window(window);

    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    style |= WS_OVERLAPPEDWINDOW;
    style &= ~WS_POPUP;
    SetWindowLongW(hwnd, GWL_STYLE, style);

    LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    exStyle |= WS_EX_COMPOSITED;
    SetWindowLongW(hwnd, GWL_EXSTYLE, exStyle);
#else
    (void)window;
#endif
}

void setTitleBarHeight(float height) {
#ifdef _WIN32
    g_titleBarHeight = height;
#else
    (void)height;
#endif
}

void invalidateWindow(GLFWwindow* window) {
#ifdef _WIN32
    InvalidateRect(glfwGetWin32Window(window), nullptr, FALSE);
#else
    (void)window;
#endif
}

void flushWindowFrame() {
#ifdef _WIN32
    DwmFlush();
#endif
}

void setWindowIcon(GLFWwindow* window) {
    static std::vector<unsigned char> iconPixels;
    GLFWimage loadedImage = {};
    if (loadPngIcon(loadedImage, iconPixels)) {
        glfwSetWindowIcon(window, 1, &loadedImage);
    } else {
        glfwSetWindowIcon(window, 0, nullptr);
    }
}

void applyWindowSizeLimits(GLFWwindow* window) {
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    glfwGetWindowContentScale(window, &scaleX, &scaleY);

    const int minWidth = std::max(1, static_cast<int>(std::round(MinWindowWidth * scaleX)));
    const int minHeight = std::max(1, static_cast<int>(std::round(MinWindowHeight * scaleY)));
    glfwSetWindowSizeLimits(window, minWidth, minHeight, GLFW_DONT_CARE, GLFW_DONT_CARE);
}

bool isWindowTopMost(GLFWwindow* window) {
#ifdef _WIN32
    HWND hwnd = glfwGetWin32Window(window);
    LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    return (exStyle & WS_EX_TOPMOST) != 0;
#else
    (void)window;
    return false;
#endif
}

void setWindowTopMost(GLFWwindow* window, bool topMost) {
#ifdef _WIN32
    HWND hwnd = glfwGetWin32Window(window);
    SetWindowPos(hwnd, topMost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
    (void)window;
    (void)topMost;
#endif
}

void centerWindowOnPrimaryMonitor(GLFWwindow* window) {
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor == nullptr)
        return;

    int monitorX = 0;
    int monitorY = 0;
    int monitorW = 0;
    int monitorH = 0;

#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 3)
    glfwGetMonitorWorkarea(monitor, &monitorX, &monitorY, &monitorW, &monitorH);
#else
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (mode == nullptr)
        return;
    glfwGetMonitorPos(monitor, &monitorX, &monitorY);
    monitorW = mode->width;
    monitorH = mode->height;
#endif

    int windowW = 0;
    int windowH = 0;
    glfwGetWindowSize(window, &windowW, &windowH);
    if (monitorW <= 0 || monitorH <= 0 || windowW <= 0 || windowH <= 0)
        return;

    glfwSetWindowPos(
        window,
        monitorX + (monitorW - windowW) / 2,
        monitorY + (monitorH - windowH) / 2
    );
}

void setupNativeDropTarget(GLFWwindow* window, NativeDropCallback callback) {
#ifdef _WIN32
    if (g_nativeDropTarget != nullptr)
        shutdownNativeDropTarget(window);

    HRESULT initialized = OleInitialize(nullptr);
    if (FAILED(initialized))
        return;
    g_oleDropInitialized = true;

    auto* dropTarget = new NativeDropTarget(callback);
    if (FAILED(RegisterDragDrop(glfwGetWin32Window(window), dropTarget))) {
        dropTarget->Release();
        OleUninitialize();
        g_oleDropInitialized = false;
        return;
    }

    g_nativeDropTarget = dropTarget;
#else
    (void)window;
    (void)callback;
#endif
}

void shutdownNativeDropTarget(GLFWwindow* window) {
#ifdef _WIN32
    if (g_nativeDropTarget != nullptr) {
        RevokeDragDrop(glfwGetWin32Window(window));
        g_nativeDropTarget->Release();
        g_nativeDropTarget = nullptr;
    }
    if (g_oleDropInitialized) {
        OleUninitialize();
        g_oleDropInitialized = false;
    }
#else
    (void)window;
#endif
}
