#include <irreden/script/path_grants.hpp>

#include <irreden/utility/path_utils.hpp>

#include <algorithm>
#include <string>
#include <system_error>

namespace IRScript {

namespace {

std::optional<std::filesystem::path> canonicalExisting(const std::filesystem::path &path) {
    if (!path.is_absolute()) {
        return std::nullopt;
    }
    std::error_code error;
    std::filesystem::path canonical = std::filesystem::canonical(path, error);
    if (error) {
        return std::nullopt;
    }
    return canonical;
}

void addUnique(std::vector<std::filesystem::path> &paths, std::filesystem::path path) {
    if (std::ranges::find(paths, path) == paths.end()) {
        paths.push_back(std::move(path));
    }
}

} // namespace

bool PathGrants::grantFile(const std::filesystem::path &path) {
    const std::optional<std::filesystem::path> canonical = canonicalExisting(path);
    std::error_code error;
    if (!canonical.has_value() || !std::filesystem::is_regular_file(*canonical, error)) {
        return false;
    }
    addUnique(m_files, *canonical);
    return true;
}

bool PathGrants::grantDirectory(const std::filesystem::path &path) {
    const std::optional<std::filesystem::path> canonical = canonicalExisting(path);
    std::error_code error;
    if (!canonical.has_value() || !std::filesystem::is_directory(*canonical, error)) {
        return false;
    }
    addUnique(m_directories, *canonical);
    return true;
}

std::optional<std::filesystem::path> PathGrants::resolve(std::string_view path) const {
    if (path.empty() || path.find('\0') != std::string_view::npos) {
        return std::nullopt;
    }
    const std::filesystem::path candidate{std::u8string{path.begin(), path.end()}};
    if (!candidate.is_absolute()) {
        return std::nullopt;
    }
    std::error_code error;
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(candidate, error);
    if (error) {
        return std::nullopt;
    }
    if (std::ranges::find(m_files, resolved) != m_files.end()) {
        return resolved;
    }
    for (const std::filesystem::path &directory : m_directories) {
        if (IRUtility::isWithinDirectory(directory, resolved)) {
            return resolved;
        }
    }
    return std::nullopt;
}

void PathGrants::clear() {
    m_files.clear();
    m_directories.clear();
}

std::optional<std::filesystem::path> resolveReadablePath(
    const PathGrants &grants, const std::filesystem::path &canonicalBase, std::string_view path
) {
    if (std::optional<std::filesystem::path> contained =
            IRUtility::resolveContainedPath(canonicalBase, path)) {
        return contained;
    }
    return grants.resolve(path);
}

} // namespace IRScript
