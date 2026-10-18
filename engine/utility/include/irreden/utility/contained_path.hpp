#ifndef CONTAINED_PATH_H
#define CONTAINED_PATH_H

#include <cctype>
#include <filesystem>
#include <optional>
#include <string_view>

namespace IRUtility {

inline bool
isWithinDirectory(const std::filesystem::path &directory, const std::filesystem::path &candidate) {
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

inline std::optional<std::filesystem::path>
resolveContainedPath(const std::filesystem::path &base, std::string_view value) {
    if (value.empty() || value.find('\0') != std::string_view::npos || value.front() == '/' ||
        value.front() == '\\' ||
        (value.size() >= 2 && std::isalpha(static_cast<unsigned char>(value[0])) != 0 &&
         value[1] == ':')) {
        return std::nullopt;
    }
    std::string portable{value};
    for (char &character : portable) {
        if (character == '\\') {
            character = '/';
        }
    }
    const std::filesystem::path relative{portable};
    for (const std::filesystem::path &component : relative) {
        if (component == "..") {
            return std::nullopt;
        }
    }

    std::error_code error;
    const std::filesystem::path canonicalBase = std::filesystem::weakly_canonical(base, error);
    if (error || !std::filesystem::is_directory(canonicalBase, error) || error) {
        return std::nullopt;
    }
    const std::filesystem::path candidate =
        std::filesystem::weakly_canonical(canonicalBase / relative, error);
    if (error || !isWithinDirectory(canonicalBase, candidate)) {
        return std::nullopt;
    }
    return candidate;
}

} // namespace IRUtility

#endif /* CONTAINED_PATH_H */
