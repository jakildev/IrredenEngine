#include <irreden/ir_utility.hpp>
#include <irreden/ir_profile.hpp>

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

std::string joinPath(
    const std::string &directory,
    const std::string &filename,
    const std::string &extension
) {
    std::filesystem::path path = std::filesystem::path(directory) / filename;
    path += normalizedExtension(extension);
    return path.string();
}

std::string pathWithExtension(const std::string &path, const std::string &extension) {
    std::filesystem::path updatedPath(path);
    updatedPath.replace_extension(normalizedExtension(extension));
    return updatedPath.string();
}

std::string formatNumberedFilename(
    const std::string &prefix,
    int index,
    int width,
    const std::string &extension
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
