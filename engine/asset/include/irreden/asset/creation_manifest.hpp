#ifndef IR_ASSET_CREATION_MANIFEST_H
#define IR_ASSET_CREATION_MANIFEST_H

/// `creation.json` — the per-creation catalog manifest the launcher scans.
/// One file at each creation root; it is hand-authored and is the source of
/// truth (unlike a sidecar). Long-form rationale:
/// `docs/design/engine-launcher.md` §D2.
///
/// Schema v1
/// ---------
///
///     {
///       "version": 1,                       required, must be 1
///       "name": "Shape debug",              required, non-empty
///       "description": "...",               optional string
///       "group": "render/shapes",           optional slash path
///       "tags": ["reference"],              optional array of strings
///       "kind": "demo",                     optional: demo | editor | game | tool | test
///                                           (default demo)
///       "thumbnail": "path/to.png",         optional, relative to the creation root
///       "docs": ["docs/design/x.md"],       optional array of strings
///       "targets": [                        required array
///         {
///           "name": "IRShapeDebug",         required, non-empty CMake / ir-run target
///           "display": "Shape debug",       optional, defaults to name
///           "description": "...",           optional string
///           "presets": [                    optional array; the first is Run's default
///             { "name": "Interactive",      required, non-empty
///               "args": ["--flag", "v"] }   optional array of strings (exe arguments)
///           ]
///         }
///       ]
///     }
///
/// Unknown keys at every level are ignored, so a newer manifest still loads
/// in an older launcher. A known key with the wrong type fails the manifest.
///
/// Version history
/// ---------------
/// v1 (initial) — the schema above.

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace IRAsset {

inline constexpr std::int64_t kCreationManifestVersion = 1;

enum class CreationKind : std::uint8_t {
    DEMO = 0,
    EDITOR,
    GAME,
    TOOL,
    TEST,
};

struct CreationPreset {
    std::string name_;
    std::vector<std::string> args_;
};

struct CreationTarget {
    std::string name_;
    std::string display_;
    std::string description_;
    std::vector<CreationPreset> presets_;
};

struct CreationManifest {
    std::int64_t version_ = kCreationManifestVersion;
    std::string name_;
    std::string description_;
    std::string group_;
    std::vector<std::string> tags_;
    CreationKind kind_ = CreationKind::DEMO;
    std::string thumbnail_;
    std::vector<std::string> docs_;
    std::vector<CreationTarget> targets_;
};

/// A syntax error carries the reader's position and an empty `path_`. A
/// schema error carries the position of the offending value — the enclosing
/// object for a missing key — and its key path (`targets[0].presets[1].args[2]`).
struct ManifestError {
    std::uint32_t line_ = 0;
    std::uint32_t column_ = 0;
    std::string path_;
    std::string message_;
};

std::expected<CreationManifest, ManifestError> parseCreationManifest(std::string_view text);

} // namespace IRAsset

#endif /* IR_ASSET_CREATION_MANIFEST_H */
