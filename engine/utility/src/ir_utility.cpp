#include <irreden/ir_utility.hpp>
#include <irreden/ir_profile.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace IRUtility {

namespace {

std::string normalizedExtension(std::string extension) {
    if (!extension.empty() && extension.front() != '.') {
        extension.insert(extension.begin(), '.');
    }
    return extension;
}

bool isAsciiLetter(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool isWithinDirectory(
    const std::filesystem::path &directory, const std::filesystem::path &candidate
) {
    auto directoryPart = directory.begin();
    auto candidatePart = candidate.begin();
    while (directoryPart != directory.end() && candidatePart != candidate.end()) {
        if (*directoryPart != *candidatePart) {
            return false;
        }
        ++directoryPart;
        ++candidatePart;
    }
    return directoryPart == directory.end();
}

} // namespace

std::string readFileAsString(const std::string &filepath) {
    std::ifstream file;
    file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    try {
        file.open(filepath);
        std::stringstream stream;
        stream << file.rdbuf();
        return stream.str();
    } catch (const std::ifstream::failure &e) {
        IRE_LOG_FATAL("Failed to read file '{}': {}", filepath, e.what());
        IR_ASSERT(false, "Error while reading file {}.", filepath);
    }
    return {};
}

std::string
joinPath(const std::string &directory, const std::string &filename, const std::string &extension) {
    std::filesystem::path path = std::filesystem::path(directory) / filename;
    path += normalizedExtension(extension);
    return path.string();
}

std::string pathWithExtension(const std::string &path, const std::string &extension) {
    std::filesystem::path updatedPath(path);
    updatedPath.replace_extension(normalizedExtension(extension));
    return updatedPath.string();
}

namespace {

// Returns the env var's value if it is set and non-empty, else nullptr.
const char *nonEmptyEnv(const char *name) {
    const char *value = std::getenv(name);
    return (value != nullptr && value[0] != '\0') ? value : nullptr;
}

} // namespace

std::string userDataDir(const std::string &appName) {
#if defined(_WIN32)
    if (const char *appData = nonEmptyEnv("APPDATA")) {
        return (std::filesystem::path(appData) / appName).string();
    }
    if (const char *userProfile = nonEmptyEnv("USERPROFILE")) {
        return (std::filesystem::path(userProfile) / appName).string();
    }
#elif defined(__APPLE__)
    if (const char *home = nonEmptyEnv("HOME")) {
        return (std::filesystem::path(home) / "Library" / "Application Support" / appName).string();
    }
#else
    if (const char *xdg = nonEmptyEnv("XDG_DATA_HOME")) {
        return (std::filesystem::path(xdg) / appName).string();
    }
    if (const char *home = nonEmptyEnv("HOME")) {
        return (std::filesystem::path(home) / ".local" / "share" / appName).string();
    }
#endif
    // No usable environment variable — fall back to a bare relative path so
    // the caller's create_directories still has somewhere to write.
    return appName;
}

bool envFlagSet(const char *name) {
    return nonEmptyEnv(name) != nullptr;
}

std::string envString(const char *name) {
    const char *value = nonEmptyEnv(name);
    return value != nullptr ? std::string{value} : std::string{};
}

std::optional<std::filesystem::path>
findOnPath(std::string_view name, std::initializer_list<std::string_view> suffixes) {
    const std::string path = envString("PATH");
    if (path.empty()) {
        return std::nullopt;
    }
#ifdef _WIN32
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    std::size_t begin = 0;
    while (begin <= path.size()) {
        const std::size_t end = path.find(separator, begin);
        const std::filesystem::path directory = path.substr(begin, end - begin);
        for (const std::string_view suffix : suffixes) {
            std::error_code error;
            const std::filesystem::path candidate =
                directory / (std::string{name} + std::string{suffix});
            if (std::filesystem::is_regular_file(candidate, error) && !error) {
                return std::filesystem::weakly_canonical(candidate, error);
            }
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return std::nullopt;
}

std::string formatNumberedFilename(
    const std::string &prefix, int index, int width, const std::string &extension
) {
    std::ostringstream filename;
    filename << prefix;
    if (width > 0) {
        filename << std::setfill('0') << std::setw(width);
    }
    filename << index << normalizedExtension(extension);
    return filename.str();
}

std::optional<std::string_view> containedPathRejection(std::string_view path) {
    if (path.empty()) {
        return "must not be empty";
    }
    if (path.find('\0') != std::string_view::npos) {
        return "must not contain a NUL byte";
    }
    const bool hasDrive = path.size() >= 2 && isAsciiLetter(path[0]) && path[1] == ':';
    if (path.front() == '/' || path.front() == '\\' || hasDrive) {
        return "must be a relative path";
    }
    std::size_t componentStart = 0;
    while (componentStart <= path.size()) {
        std::size_t componentEnd = path.find_first_of("/\\", componentStart);
        if (componentEnd == std::string_view::npos) {
            componentEnd = path.size();
        }
        if (path.substr(componentStart, componentEnd - componentStart) == "..") {
            return "must not contain a '..' component";
        }
        componentStart = componentEnd + 1;
    }
    return std::nullopt;
}

std::optional<std::filesystem::path>
resolveContainedPath(const std::filesystem::path &canonicalBase, std::string_view path) {
    if (containedPathRejection(path)) {
        return std::nullopt;
    }
    std::error_code error;
    const std::filesystem::path resolved =
        std::filesystem::weakly_canonical(canonicalBase / std::filesystem::path(path), error);
    if (error || !isWithinDirectory(canonicalBase, resolved)) {
        return std::nullopt;
    }
    return resolved;
}

} // namespace IRUtility
