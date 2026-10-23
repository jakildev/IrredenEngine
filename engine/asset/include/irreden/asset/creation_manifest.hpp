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
///       "thumbnail": "path/to.png",         optional, relative to the creation directory
///       "docs": ["docs/design/x.md"],       optional array, each relative to the tool
///                                           root (the checkout)
///       "targets": [                        required array
///         {
///           "name": "IRShapeDebug",         required CMake / ir-run target; must pass
///                                           isTargetName
///           "display": "Shape debug",       optional, defaults to name
///           "description": "...",           optional string
///           "package": true,                optional boolean (default false): the
///                                           target has a `<name>Package` build
///                                           target (`irreden_package_target`)
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
/// The parser is the gate for every value that later reaches a process or a
/// path, whether or not any lint has seen the file. A failing value fails the
/// whole manifest with its key path and reason, so a bad target is never
/// offered for Build or Run:
///   - every `targets[].name` must pass `isTargetName`;
///   - `thumbnail` and each `docs` entry must pass
///     `IRUtility::containedPathRejection` (no empty, NUL, absolute or `..`).
/// The parser touches no filesystem. Whoever opens a path resolves it with
/// `IRUtility::resolveContainedPath` against that key's base, which also
/// catches a symlink escape.
///
/// Version history
/// ---------------
/// v1 (initial) — the schema above. `targets[].package` is an additive v1
/// key: an older reader ignores it as unknown.

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
    bool package_ = false;
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

/// True iff @p name matches `^[A-Za-z0-9_][A-Za-z0-9_.+-]*$` and its stem (up
/// to the first `.`) is not a reserved Windows device name: `CON`, `PRN`,
/// `AUX`, `NUL`, `COM0`-`COM9` or `LPT0`-`LPT9`, in any case. The name becomes
/// `ir-run`'s first argument (a leading `-` is an option), a `find -name`
/// pattern (`*`, `?`, `[` match other binaries) and a result-file stem (`/`,
/// `\` leave the results directory; a device stem names the device).
bool isTargetName(std::string_view name);

} // namespace IRAsset

#endif /* IR_ASSET_CREATION_MANIFEST_H */
