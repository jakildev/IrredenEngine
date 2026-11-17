#ifndef IR_WINDOW_FILE_DIALOG_H
#define IR_WINDOW_FILE_DIALOG_H

// The native "open" panel: one file, several files, or a folder. The call is
// modal and blocks the calling thread until the user answers. Contract and
// decisions: docs/design/native-file-dialog.md.

#include <irreden/window/ir_glfw_window.hpp>

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace IRWindow {

enum class FileDialogMode {
    OPEN_FILE,
    OPEN_FILES,
    OPEN_FOLDER,
};

enum class FileDialogStatus {
    /// The user chose; `paths_` is non-empty.
    ACCEPTED,
    CANCELLED,
    /// No panel was shown because this run must not block on one: a capture
    /// run, a window that is not `WindowMode::NORMAL`, or a call off the main
    /// thread.
    REFUSED,
    /// This host has no panel to show.
    UNSUPPORTED,
    /// The request was invalid or the OS reported an error.
    FAILED,
};

struct FileDialogFilter {
    /// Shown beside the pattern where the OS has a filter menu. Empty is
    /// replaced by the pattern list.
    std::string name_;
    /// Bare extensions (`png`); `.png` and `*.png` are accepted and stripped.
    /// `*` matches every file.
    std::vector<std::string> extensions_;
};

struct FileDialogRequest {
    FileDialogMode mode_ = FileDialogMode::OPEN_FILE;
    /// UTF-8. Empty keeps the OS default.
    std::string title_;
    /// UTF-8 starting directory; relative resolves from the working
    /// directory. Empty, or a path that is not a directory, keeps the OS
    /// default.
    std::string directory_;
    /// Empty shows every file. Rejected with `OPEN_FOLDER`.
    std::vector<FileDialogFilter> filters_;
};

struct FileDialogResult {
    FileDialogStatus status_ = FileDialogStatus::CANCELLED;
    /// Absolute UTF-8 paths, non-empty exactly when `status_` is ACCEPTED.
    std::vector<std::string> paths_;
    std::string error_;
};

/// Shows the panel for an already-normalized request and blocks until it
/// closes. Injected so headless tests drive everything around the OS panel.
using FileDialogPanel = std::function<FileDialogResult(const FileDialogRequest &)>;

/// What the caller knows about the run the panel would interrupt.
struct FileDialogHost {
    bool onMainThread_ = true;
    /// `--auto-screenshot` / `--auto-record`, or an armed capture system.
    bool captureRun_ = false;
    WindowMode windowMode_ = WindowMode::NORMAL;
};

/// @p extension without a leading `*.` or `.`, or nullopt when what is left is
/// empty or holds a wildcard, a path separator, whitespace, a control byte or
/// one of `: ; | " < >`. A lone `*` is returned unchanged.
std::optional<std::string> normalizeFileDialogExtension(std::string_view extension);

/// @p request with extensions normalized and de-duplicated, empty filter
/// names filled in and a relative directory made absolute; or why it cannot
/// be shown.
std::expected<FileDialogRequest, std::string>
normalizeFileDialogRequest(const FileDialogRequest &request);

/// The one entry point around a panel: validates @p request, refuses when
/// @p host must not block, runs @p panel, and checks what it returned (every
/// path absolute; exactly one outside `OPEN_FILES`). An empty @p panel is
/// UNSUPPORTED. An invalid request is FAILED on every host, so a capture run
/// still surfaces it.
FileDialogResult runFileDialog(
    const FileDialogRequest &request, const FileDialogHost &host, const FileDialogPanel &panel
);

/// The host's native panel, parented to @p window when the OS supports it
/// (`NSOpenPanel` sheet, `IFileOpenDialog` owner; the Linux `zenity` fallback
/// is unparented). Main thread only; @p request must be normalized.
FileDialogResult platformOpenFileDialog(GLFWwindow *window, const FileDialogRequest &request);

const char *fileDialogStatusName(FileDialogStatus status);

namespace detail {

/// `*.png;*.jpg` for a `COMDLG_FILTERSPEC`; `*.*` when the filter holds `*`.
std::string windowsFilterPattern(const FileDialogFilter &filter);

/// The `zenity --file-selection` arguments for @p request, program name
/// excluded.
std::vector<std::string> zenityArguments(const FileDialogRequest &request);

/// The newline-separated paths `zenity` printed, empty lines dropped.
std::vector<std::string> parseZenityOutput(std::string_view output);

} // namespace detail

} // namespace IRWindow

#endif /* IR_WINDOW_FILE_DIALOG_H */
