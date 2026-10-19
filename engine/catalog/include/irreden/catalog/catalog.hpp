#ifndef IR_CATALOG_CATALOG_H
#define IR_CATALOG_CATALOG_H

#include <irreden/asset/creation_manifest.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace IRCatalog {

struct RunVerdict {
    std::string verdict_;
    std::string exe_;
    std::optional<std::int64_t> exit_;
    std::optional<std::int64_t> signal_;
    std::vector<std::string> argv_;
    std::string at_;
};

struct TargetState {
    std::string name_;
    std::string display_;
    bool built_ = false;
    std::string exePath_;
    double mtime_ = 0.0;
    std::optional<RunVerdict> lastRun_;
};

struct Creation {
    std::string root_;
    std::string dir_;
    std::optional<IRAsset::CreationManifest> manifest_;
    std::optional<IRAsset::ManifestError> error_;
    std::vector<TargetState> targets_;
};

struct Catalog {
    std::vector<Creation> creations_;
};

} // namespace IRCatalog

#endif // IR_CATALOG_CATALOG_H
