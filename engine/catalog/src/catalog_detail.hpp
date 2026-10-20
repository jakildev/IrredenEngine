#ifndef CATALOG_DETAIL_H
#define CATALOG_DETAIL_H

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>

namespace IRCatalog::detail {

inline std::optional<std::string> readText(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

inline double modifiedTime(const std::filesystem::path &path) {
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error) {
        return 0.0;
    }
    // file_clock's epoch is implementation-defined (libstdc++ offsets it past the Unix epoch).
    // clock_cast is missing from Apple clang, so go through to_sys.
    const auto system = std::chrono::file_clock::to_sys(modified);
    return std::chrono::duration<double>(system.time_since_epoch()).count();
}

inline std::string
relativeString(const std::filesystem::path &path, const std::filesystem::path &toolRoot) {
    std::error_code error;
    const auto relative = std::filesystem::relative(path, toolRoot, error);
    if (error) {
        throw std::runtime_error("catalog path could not be made tool-root-relative");
    }
    return relative.generic_string();
}

inline bool skipDirectory(const std::filesystem::path &path) {
    const std::string name = path.filename().string();
    return name == ".git" || name == ".claude" || name == "node_modules" ||
           name.starts_with("build");
}

} // namespace IRCatalog::detail

#endif // CATALOG_DETAIL_H
