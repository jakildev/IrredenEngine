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

} // namespace IRUtility
