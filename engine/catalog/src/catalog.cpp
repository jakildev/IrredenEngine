#include <irreden/ir_catalog.hpp>

#include <irreden/asset/json_reader.hpp>
#include <irreden/ir_platform.hpp>
#include <irreden/utility/path_utils.hpp>

#include "catalog_detail.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace IRCatalog {
namespace {

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string executableKey(const std::filesystem::path &path) {
    std::string name = path.filename().string();
    if (name.size() >= 4 && lowerAscii(name.substr(name.size() - 4)) == ".exe") {
        name.resize(name.size() - 4);
    }
    return IRPlatform::kIsWindows ? lowerAscii(std::move(name)) : name;
}

bool isExecutable(const std::filesystem::directory_entry &entry, std::error_code &error) {
    if constexpr (IRPlatform::kIsWindows) {
        return lowerAscii(entry.path().extension().string()) == ".exe";
    }
    const auto permissions = entry.status(error).permissions();
    if (error) {
        return false;
    }
    using std::filesystem::perms;
    return (permissions & (perms::owner_exec | perms::group_exec | perms::others_exec)) !=
           perms::none;
}

std::map<std::string, std::filesystem::path>
findExecutables(const std::filesystem::path &buildDir) {
    std::map<std::string, std::filesystem::path> executables;
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        buildDir,
        std::filesystem::directory_options::skip_permission_denied,
        error
    );
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        const auto &entry = *iterator;
        if (entry.is_directory(error) && iterator.depth() >= 7) {
            iterator.disable_recursion_pending();
        } else if (
            !entry.is_symlink(error) && entry.is_regular_file(error) && isExecutable(entry, error)
        ) {
            const std::string key = executableKey(entry.path());
            if (!key.empty()) {
                auto found = executables.find(key);
                if (found == executables.end() || entry.path() < found->second) {
                    executables[key] = entry.path();
                }
            }
        }
        iterator.increment(error);
    }
    return executables;
}

std::optional<std::int64_t>
optionalInteger(const IRAsset::JsonValue &object, std::string_view key) {
    const auto *value = object.find(key);
    if (value == nullptr || value->isNull()) {
        return std::nullopt;
    }
    return value->asInt();
}

std::optional<RunVerdict> readVerdict(const std::filesystem::path &path) {
    const auto text = detail::readText(path);
    if (!text) {
        return std::nullopt;
    }
    const auto parsed = IRAsset::parseJson(*text);
    if (!parsed || !parsed->isObject()) {
        return std::nullopt;
    }

    const auto *verdict = parsed->find("verdict");
    const auto *exe = parsed->find("exe");
    const auto *argv = parsed->find("argv");
    const auto *at = parsed->find("at");
    if (verdict == nullptr || verdict->asString() == nullptr || exe == nullptr ||
        exe->asString() == nullptr || argv == nullptr || argv->asArray() == nullptr ||
        at == nullptr || at->asString() == nullptr) {
        return std::nullopt;
    }

    RunVerdict result;
    result.verdict_ = *verdict->asString();
    result.exe_ = *exe->asString();
    result.exit_ = optionalInteger(*parsed, "exit");
    result.signal_ = optionalInteger(*parsed, "signal");
    result.at_ = *at->asString();
    for (const auto &argument : *argv->asArray()) {
        if (const auto *string = argument.asString()) {
            result.argv_.push_back(*string);
        } else {
            return std::nullopt;
        }
    }
    return result;
}

std::vector<std::filesystem::path> creationCandidates(const std::filesystem::path &root) {
    std::vector<std::filesystem::path> candidates;
    const auto creations = root / "creations";
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        creations,
        std::filesystem::directory_options::skip_permission_denied,
        error
    );
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        const auto &entry = *iterator;
        if (entry.is_symlink(error)) {
            if (entry.is_directory(error)) {
                iterator.disable_recursion_pending();
            }
            iterator.increment(error);
            continue;
        }
        if (entry.is_directory(error)) {
            if (detail::skipDirectory(entry.path()) || iterator.depth() >= 3) {
                iterator.disable_recursion_pending();
            }
        } else if (
            entry.is_regular_file(error) && (entry.path().filename() == "creation.json" ||
                                             entry.path().filename() == "CMakeLists.txt")
        ) {
            const auto directory = entry.path().parent_path();
            if (directory != creations) {
                candidates.push_back(directory);
            }
        }
        iterator.increment(error);
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    return candidates;
}

IRAsset::ManifestError unreadableManifestError() {
    IRAsset::ManifestError error;
    error.message_ = "could not read creation.json";
    return error;
}

IRAsset::ManifestError containedManifestPathError(std::string path) {
    IRAsset::ManifestError error;
    error.path_ = std::move(path);
    error.message_ = "path must stay under its catalog root";
    return error;
}

std::optional<IRAsset::ManifestError> resolveManifestPaths(
    IRAsset::CreationManifest &manifest,
    const std::filesystem::path &creationDirectory,
    const std::filesystem::path &toolRoot
) {
    if (!manifest.thumbnail_.empty()) {
        const auto thumbnail =
            IRUtility::resolveContainedPath(creationDirectory, manifest.thumbnail_);
        if (!thumbnail) {
            return containedManifestPathError("thumbnail");
        }
        manifest.thumbnail_ = detail::relativeString(*thumbnail, toolRoot);
    }

    for (std::size_t index = 0; index < manifest.docs_.size(); ++index) {
        const auto doc = IRUtility::resolveContainedPath(toolRoot, manifest.docs_[index]);
        if (!doc) {
            return containedManifestPathError("docs[" + std::to_string(index) + "]");
        }
        manifest.docs_[index] = detail::relativeString(*doc, toolRoot);
    }
    return std::nullopt;
}

} // namespace

CatalogService::CatalogService(std::filesystem::path toolRoot) {
    if (toolRoot.empty()) {
        throw std::invalid_argument("catalog requires a tool root");
    }
    std::error_code error;
    m_toolRoot = std::filesystem::weakly_canonical(toolRoot, error);
    if (error || !std::filesystem::is_directory(m_toolRoot, error) || error) {
        throw std::invalid_argument("catalog tool root must be an existing directory");
    }
}

std::filesystem::path
CatalogService::resolve(std::string_view relative, std::string_view field) const {
    const auto resolved = IRUtility::resolveContainedPath(m_toolRoot, relative);
    if (!resolved) {
        throw std::invalid_argument(
            "catalog " + std::string(field) + " must stay under the tool root"
        );
    }
    return *resolved;
}

Catalog CatalogService::scan(std::string_view root, std::string_view buildDir) const {
    const auto resolvedRoot = resolve(root, "root");
    const auto resolvedBuildDir = resolve(buildDir, "buildDir");
    const auto executables = findExecutables(resolvedBuildDir);

    Catalog catalog;
    for (const auto &directory : creationCandidates(resolvedRoot)) {
        Creation creation;
        creation.root_ = detail::relativeString(resolvedRoot, m_toolRoot);
        creation.dir_ = detail::relativeString(directory, m_toolRoot);

        const auto manifestPath = directory / "creation.json";
        std::error_code error;
        if (!std::filesystem::is_regular_file(manifestPath, error) || error) {
            catalog.creations_.push_back(std::move(creation));
            continue;
        }

        const auto text = detail::readText(manifestPath);
        if (!text) {
            creation.error_ = unreadableManifestError();
            catalog.creations_.push_back(std::move(creation));
            continue;
        }
        auto manifest = IRAsset::parseCreationManifest(*text);
        if (!manifest) {
            creation.error_ = std::move(manifest.error());
            catalog.creations_.push_back(std::move(creation));
            continue;
        }

        if (auto pathError = resolveManifestPaths(*manifest, directory, m_toolRoot)) {
            creation.error_ = std::move(*pathError);
            catalog.creations_.push_back(std::move(creation));
            continue;
        }

        creation.manifest_ = std::move(*manifest);
        for (const auto &target : creation.manifest_->targets_) {
            TargetState state;
            state.name_ = target.name_;
            state.display_ = target.display_;
            const std::string key =
                IRPlatform::kIsWindows ? lowerAscii(target.name_) : target.name_;
            const auto executable = executables.find(key);
            if (executable != executables.end()) {
                state.built_ = true;
                state.exePath_ = detail::relativeString(executable->second, m_toolRoot);
                state.mtime_ = detail::modifiedTime(executable->second);
                const auto resultPath = resolvedBuildDir / "ir-run-results" /
                                        (executable->second.filename().string() + ".last.json");
                state.lastRun_ = readVerdict(resultPath);
                if (!state.lastRun_) {
                    state.lastRun_ = readVerdict(
                        resolvedBuildDir / "ir-run-results" / (target.name_ + ".last.json")
                    );
                }
            }
            creation.targets_.push_back(std::move(state));
        }
        catalog.creations_.push_back(std::move(creation));
    }
    return catalog;
}

} // namespace IRCatalog
