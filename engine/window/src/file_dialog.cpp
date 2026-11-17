#include <irreden/window/file_dialog.hpp>

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace IRWindow {

namespace {

constexpr std::string_view kForbiddenExtensionBytes = "*?/\\:;|\"<>";

bool containsNul(std::string_view text) {
    return text.find('\0') != std::string_view::npos;
}

std::filesystem::path pathFromUtf8(std::string_view text) {
    return std::filesystem::path{std::u8string{text.begin(), text.end()}};
}

std::string utf8FromPath(const std::filesystem::path &path) {
    const std::u8string text = path.u8string();
    return std::string{text.begin(), text.end()};
}

bool isKnownMode(FileDialogMode mode) {
    switch (mode) {
    case FileDialogMode::OPEN_FILE:
    case FileDialogMode::OPEN_FILES:
    case FileDialogMode::OPEN_FOLDER:
        return true;
    }
    return false;
}

std::string patternList(const FileDialogFilter &filter, std::string_view separator) {
    std::string patterns;
    for (const std::string &extension : filter.extensions_) {
        if (!patterns.empty()) {
            patterns += separator;
        }
        patterns += extension == "*" ? std::string{"*"} : "*." + extension;
    }
    return patterns;
}

FileDialogResult failure(FileDialogStatus status, std::string error) {
    FileDialogResult result;
    result.status_ = status;
    result.error_ = std::move(error);
    return result;
}

} // namespace

const char *fileDialogStatusName(FileDialogStatus status) {
    switch (status) {
    case FileDialogStatus::ACCEPTED:
        return "accepted";
    case FileDialogStatus::CANCELLED:
        return "cancelled";
    case FileDialogStatus::REFUSED:
        return "refused";
    case FileDialogStatus::UNSUPPORTED:
        return "unsupported";
    case FileDialogStatus::FAILED:
        return "failed";
    }
    return "unknown";
}

std::optional<std::string> normalizeFileDialogExtension(std::string_view extension) {
    if (extension == "*" || extension == "*.*") {
        return std::string{"*"};
    }
    if (extension.starts_with("*.")) {
        extension.remove_prefix(2);
    } else if (extension.starts_with('.')) {
        extension.remove_prefix(1);
    }
    if (extension.empty() || extension.front() == '.' || extension.back() == '.') {
        return std::nullopt;
    }
    for (const char byte : extension) {
        const auto value = static_cast<unsigned char>(byte);
        if (value <= 0x20 || value == 0x7f ||
            kForbiddenExtensionBytes.find(byte) != std::string_view::npos) {
            return std::nullopt;
        }
    }
    return std::string{extension};
}

std::expected<FileDialogRequest, std::string>
normalizeFileDialogRequest(const FileDialogRequest &request) {
    if (!isKnownMode(request.mode_)) {
        return std::unexpected{"mode is not a FileDialogMode"};
    }
    if (containsNul(request.title_)) {
        return std::unexpected{"title contains a NUL byte"};
    }
    if (containsNul(request.directory_)) {
        return std::unexpected{"directory contains a NUL byte"};
    }
    if (request.mode_ == FileDialogMode::OPEN_FOLDER && !request.filters_.empty()) {
        return std::unexpected{"filters do not apply to OPEN_FOLDER"};
    }

    FileDialogRequest normalized;
    normalized.mode_ = request.mode_;
    normalized.title_ = request.title_;
    if (!request.directory_.empty()) {
        std::error_code error;
        const std::filesystem::path absolute =
            std::filesystem::absolute(pathFromUtf8(request.directory_), error);
        if (error) {
            return std::unexpected{"directory cannot be resolved: " + error.message()};
        }
        normalized.directory_ = utf8FromPath(absolute.lexically_normal());
    }

    normalized.filters_.reserve(request.filters_.size());
    for (std::size_t i = 0; i < request.filters_.size(); ++i) {
        const FileDialogFilter &source = request.filters_[i];
        const std::string label = "filters[" + std::to_string(i + 1) + "]";
        if (containsNul(source.name_)) {
            return std::unexpected{label + ".name contains a NUL byte"};
        }
        if (source.extensions_.empty()) {
            return std::unexpected{label + " has no extensions"};
        }
        FileDialogFilter filter;
        filter.name_ = source.name_;
        bool matchesAll = false;
        for (const std::string &raw : source.extensions_) {
            const std::optional<std::string> extension = normalizeFileDialogExtension(raw);
            if (!extension.has_value()) {
                return std::unexpected{label + ": '" + raw + "' is not a file extension"};
            }
            matchesAll = matchesAll || *extension == "*";
            if (std::ranges::find(filter.extensions_, *extension) == filter.extensions_.end()) {
                filter.extensions_.push_back(*extension);
            }
        }
        if (matchesAll) {
            filter.extensions_ = {"*"};
        }
        if (filter.name_.empty()) {
            filter.name_ = matchesAll ? "All files" : patternList(filter, ", ");
        }
        normalized.filters_.push_back(std::move(filter));
    }
    return normalized;
}

FileDialogResult runFileDialog(
    const FileDialogRequest &request, const FileDialogHost &host, const FileDialogPanel &panel
) {
    const std::expected<FileDialogRequest, std::string> normalized =
        normalizeFileDialogRequest(request);
    if (!normalized.has_value()) {
        return failure(FileDialogStatus::FAILED, "invalid request: " + normalized.error());
    }
    if (!host.onMainThread_) {
        return failure(FileDialogStatus::REFUSED, "not on the main thread");
    }
    if (host.captureRun_) {
        return failure(FileDialogStatus::REFUSED, "a capture run never opens a file dialog");
    }
    if (host.windowMode_ != WindowMode::NORMAL) {
        return failure(
            FileDialogStatus::REFUSED,
            std::string{"window mode '"} + windowModeName(host.windowMode_) +
                "' has nobody to answer a file dialog"
        );
    }
    if (!panel) {
        return failure(FileDialogStatus::UNSUPPORTED, "no native file dialog on this host");
    }

    FileDialogResult result = panel(*normalized);
    if (result.status_ != FileDialogStatus::ACCEPTED) {
        result.paths_.clear();
        return result;
    }
    if (result.paths_.empty()) {
        result.status_ = FileDialogStatus::CANCELLED;
        return result;
    }
    if (normalized->mode_ != FileDialogMode::OPEN_FILES && result.paths_.size() != 1) {
        return failure(
            FileDialogStatus::FAILED,
            "the panel returned " + std::to_string(result.paths_.size()) +
                " paths for a single pick"
        );
    }
    for (const std::string &path : result.paths_) {
        if (containsNul(path) || !pathFromUtf8(path).is_absolute()) {
            return failure(
                FileDialogStatus::FAILED,
                "the panel returned a path that is not absolute"
            );
        }
    }
    result.error_.clear();
    return result;
}

namespace detail {

std::string windowsFilterPattern(const FileDialogFilter &filter) {
    std::string patterns;
    for (const std::string &extension : filter.extensions_) {
        if (extension == "*") {
            return "*.*";
        }
        if (!patterns.empty()) {
            patterns += ';';
        }
        patterns += "*." + extension;
    }
    return patterns;
}

std::vector<std::string> zenityArguments(const FileDialogRequest &request) {
    std::vector<std::string> arguments{"--file-selection"};
    if (!request.title_.empty()) {
        arguments.push_back("--title=" + request.title_);
    }
    if (request.mode_ == FileDialogMode::OPEN_FILES) {
        arguments.push_back("--multiple");
        arguments.push_back("--separator=\n");
    }
    if (request.mode_ == FileDialogMode::OPEN_FOLDER) {
        arguments.push_back("--directory");
    }
    if (!request.directory_.empty()) {
        // The trailing separator is what makes zenity open the directory
        // instead of preselecting an entry of that name in its parent.
        arguments.push_back(
            "--filename=" + request.directory_ + (request.directory_.back() == '/' ? "" : "/")
        );
    }
    for (const FileDialogFilter &filter : request.filters_) {
        // zenity splits the value on the first '|' into name and patterns.
        std::string name = filter.name_;
        std::ranges::replace(name, '|', '/');
        arguments.push_back("--file-filter=" + name + " | " + patternList(filter, " "));
    }
    return arguments;
}

std::vector<std::string> parseZenityOutput(std::string_view output) {
    std::vector<std::string> paths;
    while (!output.empty()) {
        const std::size_t end = output.find('\n');
        const std::string_view line = output.substr(0, end);
        if (!line.empty()) {
            paths.emplace_back(line);
        }
        if (end == std::string_view::npos) {
            break;
        }
        output.remove_prefix(end + 1);
    }
    return paths;
}

} // namespace detail

} // namespace IRWindow
