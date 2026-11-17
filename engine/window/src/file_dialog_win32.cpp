// Windows half of file_dialog.hpp (macOS: file_dialog_cocoa.mm, Linux:
// file_dialog_linux.cpp).
#include <irreden/window/file_dialog.hpp>

#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <objbase.h>
#include <shobjidl.h>

#include <cstdio>
#include <filesystem>
#include <memory>

namespace IRWindow {

namespace {

std::wstring widen(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        wide.data(),
        length
    );
    return wide;
}

std::string narrow(const wchar_t *text) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), length, nullptr, nullptr);
    return utf8;
}

struct ComRelease {
    void operator()(IUnknown *object) const {
        object->Release();
    }
};
template <typename T> using ComOwner = std::unique_ptr<T, ComRelease>;

// The shell dialog needs a single-threaded apartment on the thread that shows
// it. An apartment this thread already holds in that model is shared; one held
// in the multithreaded model cannot host the dialog.
class ApartmentScope {
  public:
    ApartmentScope()
        : m_result{CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)} {}
    ~ApartmentScope() {
        if (SUCCEEDED(m_result)) {
            CoUninitialize();
        }
    }
    ApartmentScope(const ApartmentScope &) = delete;
    ApartmentScope &operator=(const ApartmentScope &) = delete;

    HRESULT result() const {
        return m_result;
    }

  private:
    HRESULT m_result;
};

FileDialogResult failed(const char *step, HRESULT code) {
    FileDialogResult result;
    result.status_ = FileDialogStatus::FAILED;
    char hex[16];
    std::snprintf(hex, sizeof(hex), "0x%08lX", static_cast<unsigned long>(code));
    result.error_ = std::string{step} + " failed (" + hex + ")";
    return result;
}

} // namespace

FileDialogResult platformOpenFileDialog(GLFWwindow *window, const FileDialogRequest &request) {
    const ApartmentScope apartment;
    if (apartment.result() == RPC_E_CHANGED_MODE) {
        FileDialogResult result;
        result.status_ = FileDialogStatus::UNSUPPORTED;
        result.error_ = "the main thread's COM apartment is multithreaded";
        return result;
    }
    if (FAILED(apartment.result())) {
        return failed("CoInitializeEx", apartment.result());
    }

    IFileOpenDialog *rawDialog = nullptr;
    HRESULT code = CoCreateInstance(
        CLSID_FileOpenDialog,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IFileOpenDialog,
        reinterpret_cast<void **>(&rawDialog)
    );
    if (FAILED(code)) {
        return failed("CoCreateInstance(FileOpenDialog)", code);
    }
    const ComOwner<IFileOpenDialog> dialog{rawDialog};

    FILEOPENDIALOGOPTIONS options = 0;
    code = dialog->GetOptions(&options);
    if (FAILED(code)) {
        return failed("IFileOpenDialog::GetOptions", code);
    }
    // FOS_NOCHANGEDIR: every relative engine path resolves from the working
    // directory, which the dialog would otherwise move to the picked folder.
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST | FOS_NOCHANGEDIR;
    if (request.mode_ == FileDialogMode::OPEN_FOLDER) {
        options |= FOS_PICKFOLDERS;
    }
    if (request.mode_ == FileDialogMode::OPEN_FILES) {
        options |= FOS_ALLOWMULTISELECT;
    }
    code = dialog->SetOptions(options);
    if (FAILED(code)) {
        return failed("IFileOpenDialog::SetOptions", code);
    }

    if (!request.title_.empty()) {
        dialog->SetTitle(widen(request.title_).c_str());
    }

    // The spec array points into these strings; both outlive Show().
    std::vector<std::wstring> filterText;
    std::vector<COMDLG_FILTERSPEC> filterSpecs;
    filterText.reserve(request.filters_.size() * 2);
    for (const FileDialogFilter &filter : request.filters_) {
        filterText.push_back(widen(filter.name_));
        filterText.push_back(widen(detail::windowsFilterPattern(filter)));
    }
    for (std::size_t i = 0; i + 1 < filterText.size(); i += 2) {
        filterSpecs.push_back({filterText[i].c_str(), filterText[i + 1].c_str()});
    }
    if (!filterSpecs.empty()) {
        code = dialog->SetFileTypes(static_cast<UINT>(filterSpecs.size()), filterSpecs.data());
        if (FAILED(code)) {
            return failed("IFileOpenDialog::SetFileTypes", code);
        }
    }

    if (!request.directory_.empty()) {
        // Shell parsing names take backslashes.
        std::filesystem::path directory{widen(request.directory_)};
        directory.make_preferred();
        std::error_code error;
        if (std::filesystem::is_directory(directory, error)) {
            IShellItem *rawFolder = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(
                    directory.c_str(),
                    nullptr,
                    IID_IShellItem,
                    reinterpret_cast<void **>(&rawFolder)
                ))) {
                const ComOwner<IShellItem> folder{rawFolder};
                dialog->SetFolder(folder.get());
            }
        }
    }

    const HWND owner = window == nullptr ? nullptr : glfwGetWin32Window(window);
    code = dialog->Show(owner);
    if (code == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        return {};
    }
    if (FAILED(code)) {
        return failed("IFileOpenDialog::Show", code);
    }

    IShellItemArray *rawItems = nullptr;
    code = dialog->GetResults(&rawItems);
    if (FAILED(code)) {
        return failed("IFileOpenDialog::GetResults", code);
    }
    const ComOwner<IShellItemArray> items{rawItems};
    DWORD count = 0;
    code = items->GetCount(&count);
    if (FAILED(code)) {
        return failed("IShellItemArray::GetCount", code);
    }

    FileDialogResult result;
    for (DWORD i = 0; i < count; ++i) {
        IShellItem *rawItem = nullptr;
        if (FAILED(items->GetItemAt(i, &rawItem))) {
            continue;
        }
        const ComOwner<IShellItem> item{rawItem};
        PWSTR displayName = nullptr;
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &displayName))) {
            continue;
        }
        result.paths_.push_back(narrow(displayName));
        CoTaskMemFree(displayName);
    }
    result.status_ = FileDialogStatus::ACCEPTED;
    return result;
}

} // namespace IRWindow
