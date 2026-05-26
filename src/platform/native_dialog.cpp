#include "platform/native_dialog.hpp"

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <shobjidl.h>
#include <windows.h>
#endif

namespace {
#ifdef _WIN32
    std::wstring utf8ToWide(const std::string& text) {
        int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        if (length <= 0)
            return {};

        std::wstring result(length, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), length);
        result.pop_back();
        return result;
    }

    std::string wideToUtf8(const wchar_t* text) {
        int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        if (length <= 0)
            return {};

        std::string result(length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
        result.pop_back();
        return result;
    }

    class ComScope {
    public:
        ComScope() : m_result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}
        ~ComScope() {
            if (SUCCEEDED(m_result))
                CoUninitialize();
        }

        [[nodiscard]] bool usable() const {
            return SUCCEEDED(m_result) || m_result == RPC_E_CHANGED_MODE;
        }

    private:
        HRESULT m_result = E_FAIL;
    };

    std::string resultPath(IFileDialog* dialog) {
        IShellItem* item = nullptr;
        if (FAILED(dialog->GetResult(&item)))
            return {};

        std::string result;
        PWSTR widePath = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &widePath))) {
            result = wideToUtf8(widePath);
            CoTaskMemFree(widePath);
        }
        item->Release();
        return result;
    }

    std::string fileDialog(GLFWwindow* window, REFCLSID dialogClass, const COMDLG_FILTERSPEC* filters,
                           UINT filterCount, const std::wstring& suggestedName,
                           const wchar_t* defaultExt, FILEOPENDIALOGOPTIONS extraOptions) {
        ComScope com;
        if (!com.usable())
            return {};

        IFileDialog* dialog = nullptr;
        if (FAILED(CoCreateInstance(dialogClass, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
            return {};

        if (filters != nullptr && filterCount > 0)
            dialog->SetFileTypes(filterCount, filters);
        if (!suggestedName.empty())
            dialog->SetFileName(suggestedName.c_str());
        if (defaultExt != nullptr)
            dialog->SetDefaultExtension(defaultExt);

        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options)))
            dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | extraOptions);

        std::string result;
        if (SUCCEEDED(dialog->Show(glfwGetWin32Window(window))))
            result = resultPath(dialog);

        dialog->Release();
        return result;
    }
#endif
}

std::string openFileDialog(GLFWwindow* window) {
#ifdef _WIN32
    const COMDLG_FILTERSPEC filters[] = {
        { L"Images", L"*.png;*.jpg;*.jpeg;*.bmp;*.tiff;*.tif;*.webp;*.gif" },
        { L"All", L"*.*" }
    };
    return fileDialog(window, CLSID_FileOpenDialog, filters, 2, {}, nullptr, FOS_FILEMUSTEXIST);
#else
    (void)window;
    return "";
#endif
}

std::string saveFileDialog(GLFWwindow* window, const std::string& suggestedName) {
#ifdef _WIN32
    const COMDLG_FILTERSPEC filters[] = {
        { L"PNG Image", L"*.png" },
        { L"JPEG Image", L"*.jpg;*.jpeg" },
        { L"BMP Image", L"*.bmp" },
        { L"TIFF Image", L"*.tiff;*.tif" },
        { L"WEBP Image", L"*.webp" },
        { L"All", L"*.*" }
    };
    return fileDialog(window, CLSID_FileSaveDialog, filters, 6,
                      utf8ToWide(suggestedName.empty() ? "image.png" : suggestedName),
                      L"png", FOS_OVERWRITEPROMPT);
#else
    (void)window;
    (void)suggestedName;
    return "";
#endif
}

std::string selectFolderDialog(GLFWwindow* window) {
#ifdef _WIN32
    ComScope com;
    if (!com.usable())
        return "";

    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        return "";
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);

    std::string result;
    if (SUCCEEDED(dialog->Show(glfwGetWin32Window(window)))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR widePath = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &widePath))) {
                result = wideToUtf8(widePath);
                CoTaskMemFree(widePath);
            }
            item->Release();
        }
    }

    dialog->Release();
    return result;
#else
    (void)window;
    return "";
#endif
}

std::string openWorkflowDialog(GLFWwindow* window) {
#ifdef _WIN32
    const COMDLG_FILTERSPEC filters[] = {
        { L"ImZiv Workflow", L"*.imzivflow;*.yml;*.yaml" },
        { L"All", L"*.*" }
    };
    return fileDialog(window, CLSID_FileOpenDialog, filters, 2, {}, nullptr, FOS_FILEMUSTEXIST);
#else
    (void)window;
    return "";
#endif
}

std::string saveWorkflowDialog(GLFWwindow* window, const std::string& suggestedName) {
#ifdef _WIN32
    const COMDLG_FILTERSPEC filters[] = {
        { L"ImZiv Workflow", L"*.imzivflow" },
        { L"YAML", L"*.yml;*.yaml" },
        { L"All", L"*.*" }
    };
    return fileDialog(window, CLSID_FileSaveDialog, filters, 3,
                      utf8ToWide(suggestedName.empty() ? "workflow.imzivflow" : suggestedName),
                      L"imzivflow", FOS_OVERWRITEPROMPT);
#else
    (void)window;
    (void)suggestedName;
    return "";
#endif
}
