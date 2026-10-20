#include <irreden/ir_catalog.hpp>

#include <irreden/asset/json_reader.hpp>

#include "catalog_detail.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace IRCatalog {
namespace {

std::string numberString(double value) {
    std::ostringstream output;
    output << std::setprecision(15) << value;
    return output.str();
}

std::string jsonString(const IRAsset::JsonValue &value) {
    if (const auto *string = value.asString()) {
        return *string;
    }
    if (const auto integer = value.asInt()) {
        return std::to_string(*integer);
    }
    if (const auto number = value.asNumber()) {
        return numberString(*number);
    }
    if (const auto boolean = value.asBool()) {
        return *boolean ? "true" : "false";
    }
    if (const auto *array = value.asArray()) {
        std::string result;
        for (const auto &element : *array) {
            if (!result.empty()) {
                result += ',';
            }
            result += jsonString(element);
        }
        return result;
    }
    if (const auto *object = value.asObject()) {
        std::string result;
        for (const auto &member : *object) {
            if (!result.empty()) {
                result += ';';
            }
            result += member.key_ + '=' + jsonString(member.value_);
        }
        return result;
    }
    return "null";
}

void readVoxelFields(AssetEntry &entry, const std::filesystem::path &voxelPath) {
    const auto text = detail::readText(voxelPath.string() + ".json");
    if (!text) {
        return;
    }
    const auto json = IRAsset::parseJson(*text);
    if (!json || !json->isObject()) {
        return;
    }
    for (const std::string_view key : {"bounds", "mode", "frame_count"}) {
        if (const auto *value = json->find(key)) {
            entry.fields_[std::string(key)] = jsonString(*value);
        }
    }
}

void readPrefabParts(AssetEntry &entry, const std::filesystem::path &prefabPath) {
    const auto text = detail::readText(prefabPath);
    if (!text) {
        return;
    }
    const std::regex idPattern(R"re(\bid\s*=\s*["']([^"']+)["'])re");
    for (std::sregex_iterator match(text->begin(), text->end(), idPattern), end; match != end;
         ++match) {
        entry.parts_.push_back((*match)[1].str());
    }
    entry.fields_["part_count"] = std::to_string(entry.parts_.size());
}

void addFiles(
    AssetIndex &index,
    const std::filesystem::path &directory,
    const std::filesystem::path &toolRoot,
    AssetKind kind,
    const auto &accept
) {
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        directory,
        std::filesystem::directory_options::skip_permission_denied,
        error
    );
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        const auto &file = *iterator;
        if (file.is_symlink(error)) {
            if (file.is_directory(error)) {
                iterator.disable_recursion_pending();
            }
        } else if (file.is_directory(error) && detail::skipDirectory(file.path())) {
            iterator.disable_recursion_pending();
        } else if (file.is_regular_file(error) && accept(file.path())) {
            AssetEntry entry;
            entry.kind_ = kind;
            entry.path_ = detail::relativeString(file.path(), toolRoot);
            entry.mtime_ = detail::modifiedTime(file.path());
            if (kind == AssetKind::VOXEL_SET) {
                readVoxelFields(entry, file.path());
            } else if (kind == AssetKind::PREFAB) {
                readPrefabParts(entry, file.path());
            }
            index.entries_.push_back(std::move(entry));
        }
        iterator.increment(error);
    }
}

bool hasExtension(const std::filesystem::path &path, std::string_view extension) {
    std::string actual = path.extension().string();
    std::transform(actual.begin(), actual.end(), actual.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return actual == extension;
}

} // namespace

AssetIndex CatalogService::assets(std::string_view root) const {
    const auto resolvedRoot = resolve(root, "root");
    AssetIndex index;

    const auto entities = resolvedRoot / "assets" / "voxel" / "entities";
    addFiles(index, entities, m_toolRoot, AssetKind::VOXEL_SET, [](const auto &path) {
        return hasExtension(path, ".vxs");
    });
    addFiles(index, entities, m_toolRoot, AssetKind::PREFAB, [](const auto &path) {
        return path.filename().string().ends_with(".prefab.lua");
    });

    const auto prefabs = resolvedRoot / "assets" / "prefabs";
    addFiles(index, prefabs, m_toolRoot, AssetKind::PREFAB, [](const auto &path) {
        return path.filename().string().ends_with(".prefab.lua");
    });

    const auto creations = resolvedRoot / "creations";
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        creations,
        std::filesystem::directory_options::skip_permission_denied,
        error
    );
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        const auto &entry = *iterator;
        const bool isDirectory = entry.is_directory(error);
        if (entry.is_symlink(error)) {
            if (isDirectory) {
                iterator.disable_recursion_pending();
            }
        } else if (isDirectory && detail::skipDirectory(entry.path())) {
            iterator.disable_recursion_pending();
        } else if (isDirectory && entry.path().filename() == "data") {
            addFiles(index, entry.path(), m_toolRoot, AssetKind::DATA, [](const auto &) {
                return true;
            });
            iterator.disable_recursion_pending();
        } else if (
            isDirectory && entry.path().filename() == "references" &&
            entry.path().parent_path().filename() == "test"
        ) {
            addFiles(index, entry.path(), m_toolRoot, AssetKind::PNG, [](const auto &path) {
                return hasExtension(path, ".png");
            });
            iterator.disable_recursion_pending();
        } else if (
            isDirectory && entry.path().filename() == "screenshots" &&
            entry.path().parent_path().filename() == "save_files"
        ) {
            addFiles(index, entry.path(), m_toolRoot, AssetKind::PNG, [](const auto &path) {
                return hasExtension(path, ".png");
            });
            iterator.disable_recursion_pending();
        }
        iterator.increment(error);
    }

    for (const auto &directory :
         {resolvedRoot / "test" / "references", resolvedRoot / "save_files" / "screenshots"}) {
        addFiles(index, directory, m_toolRoot, AssetKind::PNG, [](const auto &path) {
            return hasExtension(path, ".png");
        });
    }

    std::sort(
        index.entries_.begin(),
        index.entries_.end(),
        [](const auto &left, const auto &right) {
            if (left.kind_ != right.kind_) {
                return left.kind_ < right.kind_;
            }
            return left.path_ < right.path_;
        }
    );
    return index;
}

} // namespace IRCatalog
