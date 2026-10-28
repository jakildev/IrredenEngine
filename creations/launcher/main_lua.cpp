// IRLauncher — browses, builds and runs the creations of the checkout it was
// built from and of the worktrees configured beside it. `main.lua` builds the
// whole UI from the engine's IRGui, IRCatalog, IREngine.spawnTool / handOff
// and IRSave surfaces; this file only composes the pipelines, fixes the tool
// root, binds the launcher-local `IRLauncher` table and, under
// `--gui-session <name>`, the `IRTest` report table plus the scripted shot
// table that session asserts against.
//
// The tool root (the base) is IR_LAUNCHER_ROOT, set once before any script
// runs. Nothing on the command line moves it: `--catalog-root` changes only
// the source of a read-only catalog scan, whose rows still run in the base.
// Process work runs in the active root, which only `IRLauncher.selectRoot`
// changes, and only to a root fresh discovery reports under the base. The one
// exception is the multi_root session, whose base is a tree it generates
// under the run directory, chosen by the compiled session enum alone.
//
// `--supervised` is passed only by `ir-launcher`, which reads
// kUpdateRequestedExit from this process as "update me and relaunch".
//
// Sessions (python3 scripts/gui-verify.py IRLauncher -- --gui-session <name>
// --catalog-root creations/launcher/test/fixture_root):
//   * catalog_tree — the fixture groups, a fold, a filter that narrows the
//     tree to one creation, and `/` focusing the filter with no click.
//   * details_build — Build on the fixture's stub target streams the stub
//     tool's three lines into the log and reports exit 0.
//   * details_package — the package-enabled stub target shows one PACKAGE
//     control and no archive; PACKAGE builds `IRLauncherStubPackage` through
//     the stub, and the rescan finds and shows the archive it wrote. A target
//     without `package` shows no control.
//   * run_hand_off — Run hands off to IRLuaWidgetsDemo's screenshot take
//     (built in <root>/build first) and shows its CLEAN verdict.
//   * settings_roundtrip — with only a stub `code` on PATH, opening settings
//     falls back from the missing `cursor` to `code`; the editor entry and a
//     favourite then survive a save, a local change and a revert from disk.
//   * asset_preview — voxel and PNG previews replace each other without
//     leaking entities, while oversize assets stay guarded.
//   * thumbnail_chain — generated thumbnail data flows through the launcher
//     fixture without losing its asset association.
//   * dockspace_roundtrip — splitter resize, persistence, revert and panel
//     docking reflow every zone while a live voxel preview remains attached.
//   * multi_root (no --catalog-root) — over a generated base and worktree
//     `.claude/worktrees/second`: discovery skips the invalid candidates, the
//     settings panel adds the worktree, the root groups and the picker
//     appear, a picker switch changes the active root and its asset index in
//     the same process, Build / Run / Package run in each creation's own
//     checkout, root-qualified presets and roots survive a save and revert,
//     and once the worktree is deleted its stale selection launches nothing.

#include <irreden/ir_engine.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_platform.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>

#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/input/systems/system_hitbox_mouse_test_gui.hpp>
#include <irreden/common/components/component_local_transform_lua.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/components/component_voxel_set_lua.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/render/camera_controls.hpp>
#include <irreden/render/gui_test_assertions.hpp>
#include <irreden/render/systems/system_sync_viewport_subjects.hpp>
#include <irreden/render/systems/system_viewport_to_framebuffer.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_gui_canvas_to_screen.hpp>
#include <irreden/render/systems/system_text_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_layout_compute.hpp>
#include <irreden/render/systems/system_widget_apply_dropdown.hpp>
#include <irreden/render/systems/system_widget_apply_text_input.hpp>
#include <irreden/render/systems/system_widget_input.hpp>
#include <irreden/render/systems/system_widget_input_panel_drag.hpp>
#include <irreden/render/systems/system_widget_input_splitter.hpp>
#include <irreden/render/systems/system_widget_lua_dispatch.hpp>
#include <irreden/render/systems/system_widget_render_button.hpp>
#include <irreden/render/systems/system_widget_render_dropdown.hpp>
#include <irreden/render/systems/system_widget_render_label.hpp>
#include <irreden/render/systems/system_widget_render_image.hpp>
#include <irreden/render/systems/system_widget_render_panel.hpp>
#include <irreden/render/systems/system_widget_render_dock_preview.hpp>
#include <irreden/render/systems/system_widget_render_splitter.hpp>
#include <irreden/render/systems/system_widget_render_text_input.hpp>
#include <irreden/script/lua_bindings_default.hpp>
#include <irreden/script/lua_widget_bindings.hpp>
#include <irreden/utility/path_utils.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <list>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

namespace IRLauncher {

/// The exit code `ir-launcher` treats as an update request. Kept apart from
/// 0, 1, 2, ir-run's 75 and 76, and the signal range from 128 up.
constexpr int kUpdateRequestedExit = 86;

namespace {

enum class Session {
    NONE,
    CATALOG_TREE,
    DETAILS_BUILD,
    DETAILS_PACKAGE,
    RUN_HAND_OFF,
    SETTINGS_ROUNDTRIP,
    ASSET_PREVIEW,
    THUMBNAIL_CHAIN,
    DOCKSPACE_ROUNDTRIP,
    MULTI_ROOT,
};

IRSystem::SystemId g_dispatchId = IRSystem::kNullSystemId;
bool g_updateRequested = false;
int g_autoWarmupFrames = 0;
Session g_session = Session::NONE;

// Values main.lua reports through IRTest.report, read by the shot assertions.
std::unordered_map<std::string, std::string> g_reports;

Session parseSession(const std::string &name) {
    if (name == "catalog_tree") {
        return Session::CATALOG_TREE;
    }
    if (name == "details_build") {
        return Session::DETAILS_BUILD;
    }
    if (name == "details_package") {
        return Session::DETAILS_PACKAGE;
    }
    if (name == "run_hand_off") {
        return Session::RUN_HAND_OFF;
    }
    if (name == "settings_roundtrip") {
        return Session::SETTINGS_ROUNDTRIP;
    }
    if (name == "asset_preview") {
        return Session::ASSET_PREVIEW;
    }
    if (name == "thumbnail_chain") {
        return Session::THUMBNAIL_CHAIN;
    }
    if (name == "dockspace_roundtrip") {
        return Session::DOCKSPACE_ROUNDTRIP;
    }
    if (name == "multi_root") {
        return Session::MULTI_ROOT;
    }
    return Session::NONE;
}

void setEnvVar(const char *name, const char *value) {
#if defined(_WIN32)
    _putenv_s(name, value != nullptr ? value : "");
#else
    if (value != nullptr) {
        setenv(name, value, 1);
    } else {
        unsetenv(name);
    }
#endif
}

std::filesystem::path stubDir(const char *name) {
    return std::filesystem::path{IR_LAUNCHER_ROOT} / "creations" / "launcher" / "test" / name;
}

// The details_build session's seam: the tool table's `ir-build` resolves
// beside IR_RUN_WRAPPER first, then on PATH, so dropping the first and
// putting the fixed stub directory under the tool root ahead of the second
// makes Build run the stub. Selected by the session, never by a Lua value.
void useStubBuildTool() {
#if defined(_WIN32)
    constexpr char kPathSeparator = ';';
#else
    constexpr char kPathSeparator = ':';
#endif
    const char *path = std::getenv("PATH");
    const std::string augmented =
        stubDir("stub_bin").string() + kPathSeparator + (path == nullptr ? "" : path);
    setEnvVar("PATH", augmented.c_str());
    setEnvVar("IR_RUN_WRAPPER", nullptr);
}

// The details_package session's seam, beside useStubBuildTool: the stub
// writes its archive into the tool root's real build tree, where it outlives
// the run, so a leftover would satisfy the session's discovered-path check
// with nothing written. Selected by the session, never by a Lua value.
void removeStubArchives() {
    std::error_code error;
    std::filesystem::directory_iterator iterator(
        std::filesystem::path{IR_LAUNCHER_ROOT} / "build",
        error
    );
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end) {
        const std::string name = iterator->path().filename().string();
        std::error_code entryError;
        if (name.starts_with("IRLauncherStub-") && name.ends_with(".zip") &&
            iterator->is_regular_file(entryError)) {
            std::filesystem::remove(iterator->path(), entryError);
        }
        iterator.increment(error);
    }
}

// The multi_root session's base: the committed inputs under
// test/multi_root copied to a fresh tree in the run directory, its worktree
// given the regular-file `.git` marker git itself writes (git drops a nested
// `.git` from a commit), invalid candidates beside it, and a built stub
// target in each build tree. Created before any script runs, so the first
// scan sees it; the tool root is set to it, never to a Lua value.
std::filesystem::path g_multiRootBase;
#define IR_LAUNCHER_SECOND ".claude/worktrees/second"
constexpr const char *kSecondRoot = IR_LAUNCHER_SECOND;

void writeStubExecutable(const std::filesystem::path &buildDir) {
    std::filesystem::create_directories(buildDir);
    const std::filesystem::path executable =
        buildDir / (IRPlatform::kIsWindows ? "IRLauncherStub.exe" : "IRLauncherStub");
    std::ofstream(executable) << "stub\n";
    std::filesystem::permissions(
        executable,
        std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::add
    );
}

std::filesystem::path createMultiRootTree() {
    const std::filesystem::path inputs = stubDir("multi_root");
    const std::filesystem::path base = std::filesystem::current_path() / "multi_root_session";
    std::filesystem::remove_all(base);
    const std::filesystem::path second = base / kSecondRoot;
    std::filesystem::create_directories(second.parent_path());
    const auto recursive = std::filesystem::copy_options::recursive;
    std::filesystem::copy(inputs / "base", base, recursive);
    std::filesystem::copy(inputs / "second", second, recursive);
    std::ofstream(second / ".git")
        << "gitdir: " << (base / ".git" / "worktrees" / "second").string() << "\n";
    // A worktree-shaped directory without the marker never becomes a root.
    std::filesystem::copy(inputs / "second", second.parent_path() / "nomarker", recursive);
    std::error_code linkError;
    std::filesystem::create_directory_symlink(second, second.parent_path() / "link", linkError);
    writeStubExecutable(base / "build");
    writeStubExecutable(second / "build");
    return std::filesystem::weakly_canonical(base);
}

// The multi_root session's tool seam: IR_RUN_WRAPPER names the committed stub
// ir-run, so `handOff` runs it and the fixed `ir-build` resolves to the stub
// beside it, the same two binaries for every root.
void useStubRunTools() {
    setEnvVar("IR_RUN_WRAPPER", stubDir("stub_bin").append("ir-run").string().c_str());
}

// The settings_roundtrip session's seam: PATH becomes a fixed directory under
// the tool root holding only a `code` stub, so `cursor` is missing and `code`
// is found on every host. The session spawns no tool, so nothing else needs
// PATH; the dynamic loaders do not search it.
void useStubEditors() {
    setEnvVar("PATH", stubDir("stub_editors").string().c_str());
}

std::string formatTime(double seconds) {
    if (!(seconds > 0.0)) {
        return "?";
    }
    using namespace std::chrono;
    const sys_seconds point{std::chrono::seconds{static_cast<long long>(seconds)}};
    const sys_days day = floor<days>(point);
    const year_month_day date{day};
    const hh_mm_ss time{point - day};
    return fmt::format(
        "{:04}-{:02}-{:02} {:02}:{:02} UTC",
        static_cast<int>(date.year()),
        static_cast<unsigned>(date.month()),
        static_cast<unsigned>(date.day()),
        time.hours().count(),
        time.minutes().count()
    );
}

// The run directory relative to the tool root, or nothing when it lies
// outside: the editor tool only opens root-relative paths.
std::optional<std::string> runDirectoryUnderRoot(const std::filesystem::path &root) {
    std::error_code error;
    const std::filesystem::path runDir =
        std::filesystem::weakly_canonical(std::filesystem::current_path(), error);
    if (error) {
        return std::nullopt;
    }
    const std::filesystem::path relative = std::filesystem::relative(runDir, root, error);
    if (error || relative.empty() || relative.is_absolute() || *relative.begin() == "..") {
        return std::nullopt;
    }
    return relative.generic_string();
}

// ── GUI sessions ──────────────────────────────────────────────────────────
// Click positions are screen pixels: a GUI trixel is two pixels wide and one
// tall at the 1280x720 config, and every position is the center of a widget
// scripts/launcher_layout.lua places.

using Event = IRVideo::GuiInputEvent;
using EventType = IRVideo::GuiInputEvent::Type;

constexpr IRMath::ivec2 kTreeRow0{200, 78};
constexpr int kTreeRowStep = 12;
constexpr IRMath::ivec2 treeRow(int row) {
    return IRMath::ivec2(kTreeRow0.x, kTreeRow0.y + row * kTreeRowStep);
}
constexpr IRMath::ivec2 kFilter{200, 56};
constexpr IRMath::ivec2 kBuildFirstTarget{776, 208};
constexpr IRMath::ivec2 kRunFirstTarget{864, 208};
constexpr IRMath::ivec2 kPackageFirstTarget{950, 208};
constexpr IRMath::ivec2 kFavourite{928, 54};
constexpr IRMath::ivec2 kSettings{1202, 14};
constexpr IRMath::ivec2 kEditorHeader{704, 89};
constexpr IRMath::ivec2 kEditorItem1{704, 103};
constexpr IRMath::ivec2 kEditorItem2{704, 117};
constexpr IRMath::ivec2 kSettingsSave{476, 168};
constexpr IRMath::ivec2 kSettingsRevert{592, 168};
constexpr IRMath::ivec2 kSettingsClose{714, 168};

constexpr Event move(int frame, IRMath::ivec2 at) {
    return {frame, EventType::MOVE, at};
}
constexpr Event press(int frame, IRMath::ivec2 at, IRInput::KeyMouseButtons button) {
    return {frame, EventType::PRESS, at, IRMath::vec2(0.0f), button};
}
constexpr Event release(int frame, IRMath::ivec2 at, IRInput::KeyMouseButtons button) {
    return {frame, EventType::RELEASE, at, IRMath::vec2(0.0f), button};
}

#define IR_LAUNCHER_CLICK(frame, at)                                                               \
    move(frame, at), press((frame) + 1, at, IRInput::kMouseButtonLeft),                            \
        release((frame) + 2, at, IRInput::kMouseButtonLeft)
#define IR_LAUNCHER_KEY(frame, key) press(frame, kFilter, key), release((frame) + 1, kFilter, key)

constexpr IRMath::ivec2 kAwayFromWidgets{640, 650};

#define IR_LAUNCHER_KEY_AWAY(frame, key)                                                           \
    press(frame, kAwayFromWidgets, key), release((frame) + 1, kAwayFromWidgets, key)

constexpr Event kTreeFoldEvents[] = {IR_LAUNCHER_CLICK(0, treeRow(1))};
constexpr Event kTreeFilterEvents[] = {
    IR_LAUNCHER_CLICK(0, treeRow(1)),
    IR_LAUNCHER_CLICK(4, kFilter),
    IR_LAUNCHER_KEY(8, IRInput::kKeyButtonB),
    IR_LAUNCHER_KEY(10, IRInput::kKeyButtonE),
    IR_LAUNCHER_KEY(12, IRInput::kKeyButtonT),
    IR_LAUNCHER_KEY(14, IRInput::kKeyButtonA),
};
// Enter drops the filter's focus, Escape (now uncaptured) clears it, and `/`
// focuses it again with the pointer away from every widget.
constexpr Event kTreeSlashFocusEvents[] = {
    IR_LAUNCHER_KEY_AWAY(0, IRInput::kKeyButtonEnter),
    IR_LAUNCHER_KEY_AWAY(4, IRInput::kKeyButtonEscape),
    IR_LAUNCHER_KEY_AWAY(8, IRInput::kKeyButtonSlash),
    IR_LAUNCHER_KEY_AWAY(12, IRInput::kKeyButtonA),
    IR_LAUNCHER_KEY_AWAY(14, IRInput::kKeyButtonL),
    IR_LAUNCHER_KEY_AWAY(16, IRInput::kKeyButtonP),
};
constexpr Event kBuildEvents[] = {
    IR_LAUNCHER_CLICK(0, treeRow(2)),
    IR_LAUNCHER_CLICK(6, kBuildFirstTarget),
    move(60, kBuildFirstTarget),
};
constexpr Event kSelectAlphaEvents[] = {IR_LAUNCHER_CLICK(0, treeRow(2))};
constexpr Event kPackageEvents[] = {
    IR_LAUNCHER_CLICK(0, kPackageFirstTarget),
    move(60, kPackageFirstTarget),
};
constexpr Event kSelectBetaEvents[] = {IR_LAUNCHER_CLICK(0, treeRow(4))};
constexpr Event kRunEvents[] = {
    IR_LAUNCHER_CLICK(0, treeRow(4)),
    IR_LAUNCHER_CLICK(6, kRunFirstTarget),
    move(90, kRunFirstTarget),
};
constexpr Event kFavouriteEvents[] = {
    IR_LAUNCHER_CLICK(0, treeRow(2)),
    IR_LAUNCHER_CLICK(6, kFavourite),
};
constexpr IRMath::ivec2 kAssetVoxelTab{1048, 55};
constexpr IRMath::ivec2 kAssetPrefabTab{1106, 55};
constexpr IRMath::ivec2 kAssetPngTab{1164, 55};
constexpr IRMath::ivec2 kAssetRow0{1140, 75};
constexpr IRMath::ivec2 kAssetRow1{1140, 91};
constexpr Event kAssetPreviewEvents[] = {
    IR_LAUNCHER_CLICK(0, kAssetPngTab),
    IR_LAUNCHER_CLICK(8, kAssetRow0),
    IR_LAUNCHER_CLICK(20, kAssetVoxelTab),
    IR_LAUNCHER_CLICK(28, kAssetRow0),
};
constexpr Event kAssetOversizeEvents[] = {IR_LAUNCHER_CLICK(0, kAssetRow1)};
constexpr Event kAssetPrefabEvents[] = {
    IR_LAUNCHER_CLICK(0, kAssetPrefabTab),
    IR_LAUNCHER_CLICK(8, kAssetRow0),
};
constexpr Event kOpenSettingsEvents[] = {IR_LAUNCHER_CLICK(0, kSettings)};
constexpr Event kSaveEvents[] = {
    IR_LAUNCHER_CLICK(0, kSettings),
    IR_LAUNCHER_CLICK(6, kEditorHeader),
    IR_LAUNCHER_CLICK(12, kEditorItem2),
    IR_LAUNCHER_CLICK(18, kSettingsSave),
};
constexpr Event kMutateEvents[] = {
    IR_LAUNCHER_CLICK(0, kEditorHeader),
    IR_LAUNCHER_CLICK(6, kEditorItem1),
    IR_LAUNCHER_CLICK(12, kSettingsClose),
    IR_LAUNCHER_CLICK(18, kFavourite),
};
constexpr Event kRevertEvents[] = {
    IR_LAUNCHER_CLICK(0, kSettings),
    IR_LAUNCHER_CLICK(6, kSettingsRevert),
};
constexpr IRMath::ivec2 kCatalogSplitter{406, 240};
constexpr IRMath::ivec2 kCatalogSplitterSaved{466, 240};
constexpr IRMath::ivec2 kCatalogSplitterMutated{506, 240};
constexpr IRMath::ivec2 kAssetsTitle{1140, 38};
constexpr IRMath::ivec2 kDetailsDockTarget{734, 253};
constexpr IRMath::ivec2 kDockSettingsSave{536, 168};
constexpr IRMath::ivec2 kDockSettingsRevert{652, 168};
constexpr Event kDockSaveEvents[] = {
    move(0, kCatalogSplitter),
    press(1, kCatalogSplitter, IRInput::kMouseButtonLeft),
    move(4, kCatalogSplitterSaved),
    release(5, kCatalogSplitterSaved, IRInput::kMouseButtonLeft),
};
constexpr Event kDockDefaultRevertEvents[] = {
    move(0, kCatalogSplitter),
    press(1, kCatalogSplitter, IRInput::kMouseButtonLeft),
    move(4, kCatalogSplitterSaved),
    release(5, kCatalogSplitterSaved, IRInput::kMouseButtonLeft),
    IR_LAUNCHER_CLICK(10, kSettings),
    IR_LAUNCHER_CLICK(16, kDockSettingsRevert),
    IR_LAUNCHER_CLICK(22, kSettingsClose),
    move(30, kSettingsClose),
};
constexpr Event kDockPersistEvents[] = {
    IR_LAUNCHER_CLICK(0, kSettings),
    IR_LAUNCHER_CLICK(8, kDockSettingsSave),
    move(18, kDockSettingsSave),
};
constexpr Event kDockRevertEvents[] = {
    move(0, kCatalogSplitterSaved),
    press(1, kCatalogSplitterSaved, IRInput::kMouseButtonLeft),
    move(4, kCatalogSplitterMutated),
    release(5, kCatalogSplitterMutated, IRInput::kMouseButtonLeft),
    IR_LAUNCHER_CLICK(10, kDockSettingsRevert),
    move(18, kDockSettingsRevert),
};
constexpr Event kDockSwapEvents[] = {
    move(0, kAssetsTitle),
    press(1, kAssetsTitle, IRInput::kMouseButtonLeft),
    move(4, kDetailsDockTarget),
    release(5, kDetailsDockTarget, IRInput::kMouseButtonLeft),
};
// multi_root: with both roots configured the tree rows are `.`, its `multi`
// group, Base One, the worktree, its `multi` group, Second One.
constexpr IRMath::ivec2 kAddFirstWorktree{472, 204};
constexpr IRMath::ivec2 kPickerHeader{480, 14};
constexpr IRMath::ivec2 kPickerItem2{480, 42};
constexpr IRMath::ivec2 kPresetHeader{860, 189};
constexpr IRMath::ivec2 kPresetItem2{860, 217};
constexpr IRMath::ivec2 kPresetItem3{860, 231};
constexpr int kBaseOneRow = 2;
constexpr int kSecondGroupRow = 4;
constexpr int kSecondOneRow = 5;
constexpr Event kMultiAddEvents[] = {
    IR_LAUNCHER_CLICK(0, kSettings),
    IR_LAUNCHER_CLICK(8, kAddFirstWorktree),
    IR_LAUNCHER_CLICK(16, kSettingsClose),
    move(24, kAwayFromWidgets),
};
constexpr Event kMultiSwitchEvents[] = {
    IR_LAUNCHER_CLICK(0, kPickerHeader),
    IR_LAUNCHER_CLICK(8, kPickerItem2),
    move(16, kAwayFromWidgets),
};
// Each action waits out the previous one: one child at a time.
constexpr Event kMultiActionEvents[] = {
    IR_LAUNCHER_CLICK(0, treeRow(kBaseOneRow)),
    IR_LAUNCHER_CLICK(6, kBuildFirstTarget),
    IR_LAUNCHER_CLICK(60, kRunFirstTarget),
    IR_LAUNCHER_CLICK(120, treeRow(kSecondOneRow)),
    IR_LAUNCHER_CLICK(126, kBuildFirstTarget),
    IR_LAUNCHER_CLICK(180, kRunFirstTarget),
    IR_LAUNCHER_CLICK(240, kPackageFirstTarget),
    move(300, kAwayFromWidgets),
};
constexpr Event kMultiSettingsEvents[] = {
    IR_LAUNCHER_CLICK(0, treeRow(kBaseOneRow)),
    IR_LAUNCHER_CLICK(6, kPresetHeader),
    IR_LAUNCHER_CLICK(12, kPresetItem2),
    IR_LAUNCHER_CLICK(20, treeRow(kSecondOneRow)),
    IR_LAUNCHER_CLICK(26, kPresetHeader),
    IR_LAUNCHER_CLICK(32, kPresetItem3),
    IR_LAUNCHER_CLICK(40, kSettings),
    IR_LAUNCHER_CLICK(46, kSettingsSave),
    IR_LAUNCHER_CLICK(52, kSettingsRevert),
    IR_LAUNCHER_CLICK(60, treeRow(kSecondGroupRow)),
    IR_LAUNCHER_CLICK(66, kSettingsSave),
    IR_LAUNCHER_CLICK(72, kSettingsRevert),
    move(80, kAwayFromWidgets),
};
constexpr Event kMultiReselectEvents[] = {
    IR_LAUNCHER_CLICK(0, kSettingsClose),
    IR_LAUNCHER_CLICK(6, treeRow(kSecondGroupRow)),
    IR_LAUNCHER_CLICK(12, treeRow(kSecondOneRow)),
    move(18, kAwayFromWidgets),
};
// The worktree is deleted before this shot's first event (onGuiAssertFrame).
constexpr Event kMultiVanishedEvents[] = {
    IR_LAUNCHER_CLICK(4, kBuildFirstTarget),
    IR_LAUNCHER_CLICK(10, kSettings),
    IR_LAUNCHER_CLICK(16, kSettingsRevert),
    move(24, kAwayFromWidgets),
};

#undef IR_LAUNCHER_CLICK
#undef IR_LAUNCHER_KEY
#undef IR_LAUNCHER_KEY_AWAY

constexpr IRVideo::GuiTestShot shot(const char *label) {
    return {{1.0f, IRMath::vec2(0.0f), 0.0f, label}, nullptr, 0};
}
template <std::size_t N>
constexpr IRVideo::GuiTestShot shot(const char *label, const Event (&events)[N]) {
    return {{1.0f, IRMath::vec2(0.0f), 0.0f, label}, events, static_cast<int>(N)};
}

constexpr IRVideo::GuiTestShot kMainShots[] = {shot("launcher_main")};
constexpr IRVideo::GuiTestShot kCatalogTreeShots[] = {
    shot("catalog_tree_groups"),
    shot("catalog_tree_fold", kTreeFoldEvents),
    shot("catalog_tree_filter", kTreeFilterEvents),
    shot("catalog_tree_slash_focus", kTreeSlashFocusEvents),
};
constexpr IRVideo::GuiTestShot kDetailsBuildShots[] = {shot("details_build", kBuildEvents)};
constexpr IRVideo::GuiTestShot kDetailsPackageShots[] = {
    shot("package_control_shown", kSelectAlphaEvents),
    shot("package_built", kPackageEvents),
    shot("package_control_absent", kSelectBetaEvents),
};
constexpr IRVideo::GuiTestShot kRunHandOffShots[] = {shot("run_hand_off", kRunEvents)};
constexpr IRVideo::GuiTestShot kSettingsShots[] = {
    shot("settings_favourite", kFavouriteEvents),
    shot("settings_editor_fallback", kOpenSettingsEvents),
    shot("settings_save", kSaveEvents),
    shot("settings_mutate", kMutateEvents),
    shot("settings_revert", kRevertEvents),
};
constexpr IRVideo::GuiTestShot kAssetPreviewShots[] = {
    shot("asset_preview_switch", kAssetPreviewEvents),
    shot("asset_preview_oversize", kAssetOversizeEvents),
    shot("asset_preview_prefab", kAssetPrefabEvents),
};
constexpr IRVideo::GuiTestShot kThumbnailShots[] = {shot("thumbnail_chain")};
constexpr IRVideo::GuiTestShot kDockspaceShots[] = {
    shot("dockspace_initial"),
    shot("dockspace_default_reverted", kDockDefaultRevertEvents),
    shot("dockspace_resized", kDockSaveEvents),
    shot("dockspace_save_clicked", kDockPersistEvents),
    shot("dockspace_saved"),
    shot("dockspace_reverted", kDockRevertEvents),
    shot("dockspace_swapped", kDockSwapEvents),
};

constexpr IRVideo::GuiTestShot kMultiRootShots[] = {
    shot("multi_root_discovered"),
    shot("multi_root_added", kMultiAddEvents),
    shot("multi_root_switched", kMultiSwitchEvents),
    shot("multi_root_actions", kMultiActionEvents),
    shot("multi_root_settings", kMultiSettingsEvents),
    shot("multi_root_reselected", kMultiReselectEvents),
    shot("multi_root_vanished", kMultiVanishedEvents),
};
constexpr int kMultiRootVanishedShot = 6;

std::span<const IRVideo::GuiTestShot> sessionShots(Session session) {
    switch (session) {
    case Session::CATALOG_TREE:
        return kCatalogTreeShots;
    case Session::DETAILS_BUILD:
        return kDetailsBuildShots;
    case Session::DETAILS_PACKAGE:
        return kDetailsPackageShots;
    case Session::RUN_HAND_OFF:
        return kRunHandOffShots;
    case Session::SETTINGS_ROUNDTRIP:
        return kSettingsShots;
    case Session::ASSET_PREVIEW:
        return kAssetPreviewShots;
    case Session::THUMBNAIL_CHAIN:
        return kThumbnailShots;
    case Session::DOCKSPACE_ROUNDTRIP:
        return kDockspaceShots;
    case Session::MULTI_ROOT:
        return kMultiRootShots;
    case Session::NONE:
        break;
    }
    return kMainShots;
}

// What main.lua reported under `key_`: equal to `expected_`, or containing
// it when `contains_` is set.
struct ReportCheck {
    const char *key_;
    const char *expected_;
    bool contains_ = false;
};

bool matchReport(
    const char *key, const std::string &expected, bool contains, bool slashes, std::string &actual
) {
    const auto found = g_reports.find(key);
    std::string value = found == g_reports.end() ? std::string{} : found->second;
    if (slashes) {
        std::replace(value.begin(), value.end(), '\\', '/');
    }
    actual = found == g_reports.end() ? std::string{"<none>"} : value;
    for (char &c : actual) {
        if (c == '\n' || c == '\r') {
            c = ' ';
        }
    }
    if (found == g_reports.end()) {
        return false;
    }
    if (!contains) {
        return value == expected;
    }
    const bool present = value.find(expected) != std::string::npos;
    actual = present ? expected : std::string{"missing '"} + expected + "'";
    return present;
}

bool reportMatches(const void *context, std::string &actual) {
    const auto &check = *static_cast<const ReportCheck *>(context);
    return matchReport(check.key_, check.expected_, check.contains_, false, actual);
}

// A ReportCheck whose expected text names the multi_root session's generated
// base as `{base}`; both sides compare with `/` separators.
bool rootedReportMatches(const void *context, std::string &actual) {
    const auto &check = *static_cast<const ReportCheck *>(context);
    std::string expected = check.expected_;
    const std::string base = g_multiRootBase.generic_string();
    for (std::size_t at = expected.find("{base}"); at != std::string::npos;
         at = expected.find("{base}", at + base.size())) {
        expected.replace(at, 6, base);
    }
    return matchReport(check.key_, expected, check.contains_, true, actual);
}

// Two reports that must hold the same value.
struct ReportPair {
    const char *left_;
    const char *right_;
};

bool reportsEqual(const void *context, std::string &actual) {
    const auto &pair = *static_cast<const ReportPair *>(context);
    const auto left = g_reports.find(pair.left_);
    const auto right = g_reports.find(pair.right_);
    if (left == g_reports.end() || right == g_reports.end()) {
        actual = "<none>";
        return false;
    }
    actual = left->second == right->second ? "equal" : left->second + " != " + right->second;
    return left->second == right->second;
}

// No package archive in the base build tree: PACKAGE ran in the worktree.
bool baseHasNoArchive(const void *, std::string &actual) {
    std::error_code error;
    const bool present = std::filesystem::exists(
        g_multiRootBase / "build" / "IRLauncherStub-fixture-test.zip",
        error
    );
    actual = present ? "present" : "absent";
    return !present;
}

#define IR_LAUNCHER_FAVOURITE "creations/launcher/test/fixture_root/creations/alpha"

// The fixture tree, every group open: groups sort by name, Unsorted trails.
const ReportCheck kTreeAll{
    "tree",
    "G:fixture|G:one|C:Alpha|G:two|C:Beta|G:z_thumbnails|C:Thumbnail Manifest|C:Thumbnail "
    "None|C:Thumbnail Reference|C:Thumbnail Screenshot|G:UNSORTED|C:broken (INVALID)|C:gamma (NO "
    "MANIFEST)"
};
const ReportCheck kTreeFolded{
    "tree",
    "G:fixture|G:one|G:two|C:Beta|G:z_thumbnails|C:Thumbnail Manifest|C:Thumbnail None|C:Thumbnail "
    "Reference|C:Thumbnail Screenshot|G:UNSORTED|C:broken (INVALID)|C:gamma (NO MANIFEST)"
};
const ReportCheck kTreeFiltered{"tree", "G:fixture|G:two|C:Beta"};
const ReportCheck kOneCreation{"treeCreations", "1"};
const ReportCheck kTreeSlashFiltered{"tree", "G:fixture|G:one|C:Alpha"};
const ReportCheck kSlashNotTyped{"filter", "alp"};
const ReportCheck kStubLines[] = {
    {"log", "IR_LAUNCHER_STUB line 1 --target IRLauncherStub", true},
    {"log", "IR_LAUNCHER_STUB line 2 --target IRLauncherStub", true},
    {"log", "IR_LAUNCHER_STUB line 3 --target IRLauncherStub", true},
};
const ReportCheck kBuildExit{"buildExit", "IRLauncherStub=0"};
const ReportCheck kBuildExitLogged{"log", "BUILD IRLauncherStub exit 0", true};
const ReportCheck kOnePackageControl{"packageControls", "1"};
const ReportCheck kNoPackageControl{"packageControls", "0"};
const ReportCheck kNoArchiveYet{"packagePaths", "IRLauncherStub="};
const ReportCheck kPackageStubLines[] = {
    {"log", "IR_LAUNCHER_STUB line 1 --target IRLauncherStubPackage", true},
    {"log", "IR_LAUNCHER_STUB line 2 --target IRLauncherStubPackage", true},
    {"log", "IR_LAUNCHER_STUB line 3 --target IRLauncherStubPackage", true},
};
const ReportCheck kPackageArgc{"log", "IR_LAUNCHER_STUB argc 2", true};
const ReportCheck kPackageExit{"packageExit", "IRLauncherStub=0"};
const ReportCheck kPackageExitLogged{"log", "PACKAGE IRLauncherStub exit 0", true};
const ReportCheck kArchiveDiscovered{
    "packagePaths", "IRLauncherStub=build/IRLauncherStub-fixture-test.zip"
};
const ReportCheck kArchiveLogged{
    "log", "PACKAGE IRLauncherStub archive build/IRLauncherStub-fixture-test.zip", true
};
const ReportCheck kRunVerdict{"verdict", "CLEAN"};
const ReportCheck kVerdictShown{"lastRun", "verdict CLEAN", true};
const ReportCheck kFavouriteAdded{"settings", "|" IR_LAUNCHER_FAVOURITE};
const ReportCheck kFavouritesGroup{"tree", "G:FAVOURITES|C:Alpha|", true};
const ReportCheck kSaved{"settings", "code|" IR_LAUNCHER_FAVOURITE};
const ReportCheck kChangedLocally{"settings", "cursor|"};
const ReportCheck kAssetSequence{"assetSequence", "voxel_set|png|voxel_set"};
const ReportCheck kAssetCountRestored{"assetCountRestored", "true"};
const ReportCheck kAssetSubjectMatches{"assetSubjectMatches", "true"};
const ReportCheck kAssetPngOnly{"assetPngOnly", "true"};
const ReportCheck kAssetOversize{"assetOversize", "true"};
const ReportCheck kAssetPrefabKind{"assetKind", "prefab"};
const ReportCheck kAssetHasEntity{"assetHasEntity", "true"};
const ReportCheck kAssetHasViewport{"assetHasViewport", "true"};
const ReportCheck kThumbnailChain{
    "thumbnailChain", "manifest.png|reference.png|recent.png|missing-thumbnail.png", true
};
const ReportCheck kThumbnailProbes{
    "thumbnailProbes", "230,140,40,255|84,86,92,255|56,71,77,255|120,124,132,255"
};
const ReportCheck kDockInitial{"layoutWidths", "200"};
const ReportCheck kDockDefaultReverted{"layoutWidths", "200|230|200"};
const ReportCheck kDockSaved{"layoutWidths", "200|230|200|230"};
const ReportCheck kDockReverted{"layoutWidths", "200|230|200|230|249|230"};
const ReportCheck kDockRestored{"layoutRestored", "true"};
const ReportCheck kDockSwapped{"layoutPanelXs", "236|534"};
const ReportCheck kDockContained{"layoutContained", "true"};
const ReportCheck kDockPreviewRect{"layoutPreviewRect", "242,230,280,176"};

#undef IR_LAUNCHER_FAVOURITE

#define IR_LAUNCHER_BOTH_TREE "G:.|G:multi|C:Base One|G:" IR_LAUNCHER_SECOND "|G:multi"

const ReportCheck kMultiDiscovered{"discoveredRoots", ".|" IR_LAUNCHER_SECOND};
const ReportCheck kMultiBaseOnly{"configuredRoots", "."};
const ReportCheck kMultiBothRoots{"configuredRoots", ".|" IR_LAUNCHER_SECOND};
const ReportCheck kMultiNoPicker{"picker", "none"};
const ReportCheck kMultiPicker{"picker", ".|" IR_LAUNCHER_SECOND};
const ReportCheck kMultiBaseTree{"tree", "G:multi|C:Base One"};
const ReportCheck kMultiBothTree{"tree", IR_LAUNCHER_BOTH_TREE "|C:Second One"};
const ReportCheck kMultiFoldedTree{"tree", IR_LAUNCHER_BOTH_TREE};
const ReportCheck kMultiMigration{"legacyMigration", "creations/one|IRLauncherStub=2|legacy=0"};
const ReportCheck kMultiBaseActive{"activeRoot", "."};
const ReportCheck kMultiSecondActive{"activeRoot", IR_LAUNCHER_SECOND};
const ReportCheck kMultiSecondAssets{
    "assetIndex",
    IR_LAUNCHER_SECOND "/creations/one/save_files/screenshots/second_sentinel.png",
    true
};
const ReportPair kMultiSameSession{"sessionStart", "sessionAtSwitch"};
const ReportCheck kMultiSwitchLogged{"log", "ROOT " IR_LAUNCHER_SECOND " active", true};
const ReportCheck kMultiNoLaunches{"launches", "0"};
const ReportCheck kMultiBaseBuild{"log", "IR_LAUNCHER_STUB cwd {base} build {base}/build", true};
const ReportCheck kMultiSecondBuild{
    "log",
    "IR_LAUNCHER_STUB cwd {base}/" IR_LAUNCHER_SECOND " build {base}/" IR_LAUNCHER_SECOND "/build",
    true
};
const ReportCheck kMultiPackageBuilt{
    "log", "IR_LAUNCHER_STUB line 1 --target IRLauncherStubPackage", true
};
const ReportCheck kMultiLastRuns{
    "lastRuns",
    ".:IRLauncherStub={base}/build/IRLauncherStub|" IR_LAUNCHER_SECOND
    ":IRLauncherStub={base}/" IR_LAUNCHER_SECOND "/build/IRLauncherStub"
};
const ReportCheck kMultiSecondArchive{
    "packagePaths", "IRLauncherStub=" IR_LAUNCHER_SECOND "/build/IRLauncherStub-fixture-test.zip"
};
const ReportCheck kMultiSecondDetails{"detailsRoot", IR_LAUNCHER_SECOND};
const ReportCheck kMultiSecondDocs{"detailsDocs", IR_LAUNCHER_SECOND "/docs/root.md"};
const ReportCheck kMultiFiveLaunches{"launches", "5"};
const ReportCheck kMultiPresets{
    "presets", IR_LAUNCHER_SECOND "/creations/one|IRLauncherStub=3,creations/one|IRLauncherStub=2"
};
const ReportCheck kMultiCollapsed{"collapsed", IR_LAUNCHER_SECOND ">multi"};
const ReportCheck kMultiStaleRefused{
    "log", "BUILD IRLauncherStub: root " IR_LAUNCHER_SECOND " is unavailable", true
};
const ReportCheck kMultiBaseDiscovered{"discoveredRoots", "."};

#undef IR_LAUNCHER_BOTH_TREE
#undef IR_LAUNCHER_SECOND

using IRPrefab::GuiTest::Assertion;
using IRPrefab::GuiTest::predicate;
using ShotAssertions = std::vector<Assertion>;

const ShotAssertions kCatalogTreeAssertions[] = {
    {predicate(&reportMatches, &kTreeAll, "tree_groups")},
    {predicate(&reportMatches, &kTreeFolded, "tree_fold")},
    {predicate(&reportMatches, &kTreeFiltered, "tree_filter"),
     predicate(&reportMatches, &kOneCreation, "filter_narrows_to_one")},
    {predicate(&reportMatches, &kSlashNotTyped, "slash_focuses_filter"),
     predicate(&reportMatches, &kTreeSlashFiltered, "slash_filter_narrows"),
     predicate(&reportMatches, &kOneCreation, "slash_filter_narrows_to_one")},
};
const ShotAssertions kDetailsBuildAssertions[] = {
    {predicate(&reportMatches, &kStubLines[0], "stub_line_1"),
     predicate(&reportMatches, &kStubLines[1], "stub_line_2"),
     predicate(&reportMatches, &kStubLines[2], "stub_line_3"),
     predicate(&reportMatches, &kBuildExit, "build_exit"),
     predicate(&reportMatches, &kBuildExitLogged, "exit_logged")},
};
const ShotAssertions kDetailsPackageAssertions[] = {
    {predicate(&reportMatches, &kOnePackageControl, "package_control_shown"),
     predicate(&reportMatches, &kNoArchiveYet, "no_archive_before_package")},
    {predicate(&reportMatches, &kPackageStubLines[0], "package_stub_line_1"),
     predicate(&reportMatches, &kPackageStubLines[1], "package_stub_line_2"),
     predicate(&reportMatches, &kPackageStubLines[2], "package_stub_line_3"),
     predicate(&reportMatches, &kPackageArgc, "package_stub_argc"),
     predicate(&reportMatches, &kPackageExit, "package_exit"),
     predicate(&reportMatches, &kPackageExitLogged, "package_exit_logged"),
     predicate(&reportMatches, &kArchiveDiscovered, "archive_discovered"),
     predicate(&reportMatches, &kArchiveLogged, "archive_logged"),
     predicate(&reportMatches, &kOnePackageControl, "package_control_kept")},
    {predicate(&reportMatches, &kNoPackageControl, "package_control_absent")},
};
const ShotAssertions kRunHandOffAssertions[] = {
    {predicate(&reportMatches, &kRunVerdict, "run_verdict"),
     predicate(&reportMatches, &kVerdictShown, "verdict_shown")},
};
const ShotAssertions kSettingsAssertions[] = {
    {predicate(&reportMatches, &kFavouriteAdded, "favourite_added"),
     predicate(&reportMatches, &kFavouritesGroup, "favourites_group")},
    {predicate(&reportMatches, &kSaved, "editor_falls_back_to_found")},
    {predicate(&reportMatches, &kSaved, "saved")},
    {predicate(&reportMatches, &kChangedLocally, "changed_locally")},
    {predicate(&reportMatches, &kSaved, "reverted_from_disk"),
     predicate(&reportMatches, &kFavouritesGroup, "favourite_restored")},
};
const ShotAssertions kAssetPreviewAssertions[] = {
    {predicate(&reportMatches, &kAssetSequence, "switch_sequence"),
     predicate(&reportMatches, &kAssetCountRestored, "entity_count_restored"),
     predicate(&reportMatches, &kAssetSubjectMatches, "viewport_names_loaded_entity"),
     predicate(&reportMatches, &kAssetPngOnly, "png_has_image_without_viewport")},
    {predicate(&reportMatches, &kAssetOversize, "oversize_label")},
    {predicate(&reportMatches, &kAssetPrefabKind, "prefab_selected"),
     predicate(&reportMatches, &kAssetHasEntity, "prefab_entity"),
     predicate(&reportMatches, &kAssetHasViewport, "prefab_viewport")},
};
const ShotAssertions kThumbnailAssertions[] = {
    {predicate(&reportMatches, &kThumbnailChain, "thumbnail_fallback_chain"),
     predicate(&reportMatches, &kThumbnailProbes, "thumbnail_probe_colours")},
};
const ShotAssertions kDockspaceAssertions[] = {
    {predicate(&reportMatches, &kDockInitial, "initial_catalog_width")},
    {predicate(&reportMatches, &kDockDefaultReverted, "default_splitter_reverted")},
    {predicate(&reportMatches, &kDockSaved, "splitter_saved")},
    {predicate(&reportMatches, &kDockSaved, "save_keeps_width")},
    {predicate(&reportMatches, &kDockRestored, "layout_serialized_on_save")},
    {predicate(&reportMatches, &kDockReverted, "splitter_reverted"),
     predicate(&reportMatches, &kDockRestored, "disk_value_restored")},
    {predicate(&reportMatches, &kDockSwapped, "panels_swapped"),
     predicate(&reportMatches, &kDockContained, "zone_widgets_contained"),
     predicate(&reportMatches, &kDockPreviewRect, "viewport_follows_assets_panel"),
     predicate(&reportMatches, &kAssetSubjectMatches, "viewport_subject_survived")},
};

const ShotAssertions kMultiRootAssertions[] = {
    {predicate(&reportMatches, &kMultiDiscovered, "invalid_candidates_skipped"),
     predicate(&reportMatches, &kMultiBaseOnly, "base_only_configured"),
     predicate(&reportMatches, &kMultiNoPicker, "no_picker_with_one_root"),
     predicate(&reportMatches, &kMultiBaseTree, "single_root_tree_has_no_root_tier"),
     predicate(&reportMatches, &kMultiMigration, "legacy_preset_migrates_exact_base_name"),
     predicate(&reportMatches, &kMultiBaseActive, "base_active")},
    {predicate(&reportMatches, &kMultiBothRoots, "worktree_added"),
     predicate(&reportMatches, &kMultiPicker, "picker_lists_roots"),
     predicate(&reportMatches, &kMultiBothTree, "root_groups_list_both"),
     predicate(&reportMatches, &kMultiBaseActive, "add_keeps_active_root")},
    {predicate(&reportMatches, &kMultiSecondActive, "picker_switched_root"),
     predicate(&reportMatches, &kMultiSecondAssets, "switched_asset_index"),
     predicate(&reportsEqual, &kMultiSameSession, "no_restart"),
     predicate(&reportMatches, &kMultiSwitchLogged, "switch_logged"),
     predicate(&reportMatches, &kMultiNoLaunches, "switch_launches_nothing")},
    {predicate(&rootedReportMatches, &kMultiBaseBuild, "base_build_in_base"),
     predicate(&rootedReportMatches, &kMultiSecondBuild, "second_build_in_second"),
     predicate(&reportMatches, &kMultiPackageBuilt, "package_built"),
     predicate(&rootedReportMatches, &kMultiLastRuns, "runs_use_own_build_dir"),
     predicate(&reportMatches, &kMultiSecondArchive, "archive_in_second_tree"),
     predicate(&baseHasNoArchive, nullptr, "no_archive_in_base_tree"),
     predicate(&reportMatches, &kMultiSecondDetails, "details_show_root"),
     predicate(&reportMatches, &kMultiSecondDocs, "docs_from_second_root"),
     predicate(&reportMatches, &kMultiFiveLaunches, "five_launches")},
    {predicate(&reportMatches, &kMultiPresets, "presets_keyed_by_creation"),
     predicate(&reportMatches, &kMultiBothRoots, "roots_reloaded"),
     predicate(&reportMatches, &kMultiSecondActive, "active_root_reloaded"),
     predicate(&reportMatches, &kMultiCollapsed, "collapse_keyed_by_root"),
     predicate(&reportMatches, &kMultiFoldedTree, "fold_reloaded")},
    {predicate(&reportMatches, &kMultiSecondDetails, "second_reselected")},
    {predicate(&reportMatches, &kMultiStaleRefused, "stale_selection_refused"),
     predicate(&reportMatches, &kMultiFiveLaunches, "vanished_root_launches_nothing"),
     predicate(&reportMatches, &kMultiBaseDiscovered, "vanished_root_undiscovered"),
     predicate(&reportMatches, &kMultiBaseOnly, "vanished_root_dropped"),
     predicate(&reportMatches, &kMultiBaseActive, "base_active_after_reload"),
     predicate(&reportMatches, &kMultiNoPicker, "picker_gone"),
     predicate(&reportMatches, &kMultiBaseTree, "single_root_tree_again")},
};

std::span<const ShotAssertions> sessionAssertions(Session session) {
    switch (session) {
    case Session::CATALOG_TREE:
        return kCatalogTreeAssertions;
    case Session::DETAILS_BUILD:
        return kDetailsBuildAssertions;
    case Session::DETAILS_PACKAGE:
        return kDetailsPackageAssertions;
    case Session::RUN_HAND_OFF:
        return kRunHandOffAssertions;
    case Session::SETTINGS_ROUNDTRIP:
        return kSettingsAssertions;
    case Session::ASSET_PREVIEW:
        return kAssetPreviewAssertions;
    case Session::THUMBNAIL_CHAIN:
        return kThumbnailAssertions;
    case Session::DOCKSPACE_ROUNDTRIP:
        return kDockspaceAssertions;
    case Session::MULTI_ROOT:
        return kMultiRootAssertions;
    case Session::NONE:
        break;
    }
    return {};
}

static_assert(std::size(kCatalogTreeAssertions) == std::size(kCatalogTreeShots));
static_assert(std::size(kDetailsBuildAssertions) == std::size(kDetailsBuildShots));
static_assert(std::size(kDetailsPackageAssertions) == std::size(kDetailsPackageShots));
static_assert(std::size(kRunHandOffAssertions) == std::size(kRunHandOffShots));
static_assert(std::size(kSettingsAssertions) == std::size(kSettingsShots));
static_assert(std::size(kAssetPreviewAssertions) == std::size(kAssetPreviewShots));
static_assert(std::size(kThumbnailAssertions) == std::size(kThumbnailShots));
static_assert(std::size(kDockspaceAssertions) == std::size(kDockspaceShots));
static_assert(std::size(kMultiRootAssertions) == std::size(kMultiRootShots));

IRPrefab::GuiTest::LatchState g_assertLatch;

void onGuiAssertFrame(int shotIndex, bool isCaptureFrame) {
    if (g_session == Session::MULTI_ROOT && shotIndex == kMultiRootVanishedShot) {
        std::error_code error;
        std::filesystem::remove_all(g_multiRootBase / kSecondRoot, error);
    }
    const std::span<const ShotAssertions> assertions = sessionAssertions(g_session);
    if (shotIndex < 0 || shotIndex >= static_cast<int>(assertions.size())) {
        return;
    }
    const ShotAssertions &shot = assertions[static_cast<std::size_t>(shotIndex)];
    IRPrefab::GuiTest::onFrame(
        g_assertLatch,
        shotIndex,
        isCaptureFrame,
        sessionShots(g_session)[static_cast<std::size_t>(shotIndex)].render_.label_,
        shot.data(),
        static_cast<int>(shot.size())
    );
}
} // namespace

} // namespace IRLauncher

void registerLuaBindings();
void initSystems();

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: launcher");
    IREngine::args().flag("--supervised", "Run under ir-launcher, which applies update requests");
    IREngine::args().string(
        "--update-failed",
        "Root-relative log of a failed update; shows the update-failed banner",
        ""
    );
    IREngine::args().string(
        "--catalog-root",
        "Root-relative directory whose creations/ the catalog lists",
        "."
    );
    IREngine::args().enumValue(
        "--gui-session",
        "Scripted GUI session for gui-verify",
        {"none",
         "catalog_tree",
         "details_build",
         "details_package",
         "run_hand_off",
         "settings_roundtrip",
         "asset_preview",
         "thumbnail_chain",
         "dockspace_roundtrip",
         "multi_root"},
        "none"
    );
    registerLuaBindings();
    IREngine::init(argc, argv, "config.lua");
    IRLauncher::g_autoWarmupFrames = IREngine::args().autoScreenshotWarmupFrames();
    if (IRLauncher::g_session == IRLauncher::Session::DETAILS_BUILD) {
        IRLauncher::useStubBuildTool();
    } else if (IRLauncher::g_session == IRLauncher::Session::DETAILS_PACKAGE) {
        IRLauncher::useStubBuildTool();
        IRLauncher::removeStubArchives();
    } else if (IRLauncher::g_session == IRLauncher::Session::SETTINGS_ROUNDTRIP) {
        IRLauncher::useStubEditors();
    } else if (IRLauncher::g_session == IRLauncher::Session::MULTI_ROOT) {
        IRLauncher::useStubRunTools();
    }
    initSystems();
    IREngine::runScript("main.lua");
    IREngine::gameLoop();
    return IRLauncher::g_updateRequested ? IRLauncher::kUpdateRequestedExit : 0;
}

void registerLuaBindings() {
    IREngine::registerLuaBindings([](IRScript::LuaScript &script) {
        const auto &args = IREngine::args();
        IRLauncher::g_session = IRLauncher::parseSession(args.getEnum("--gui-session"));
        if (IRLauncher::g_session == IRLauncher::Session::MULTI_ROOT) {
            IRLauncher::g_multiRootBase = IRLauncher::createMultiRootTree();
            IREngine::setToolRoot(IRLauncher::g_multiRootBase);
        } else {
            IREngine::setToolRoot(IR_LAUNCHER_ROOT);
        }
        const std::filesystem::path root = *IREngine::toolRoot();

        script.bindLuaDrivenEcs();
        // Rebinds IRGui with image paths confined to the tool root, replacing
        // the run-directory base bindLuaDrivenEcs() installs.
        IRScript::detail::bindWidgets(script, root);
        IRScript::registerStandardBindings(script);
        script
            .registerTypesFromTraits<IRComponents::C_LocalTransform, IRComponents::C_VoxelSetNew>();
        script.registerCreateEntityFunction<
            IRComponents::C_LocalTransform,
            IRComponents::C_VoxelSetNew>("createPreviewEntity");
        script.bindLuaCommands();
        script.bindLuaViewport();
        script.bindLuaFiles();
        script.bindLuaCatalog(root);
        script.bindLuaSave(std::filesystem::current_path() / "save_files");
        IRLauncher::g_dispatchId = script.registerPrefabSystem<IRSystem::WIDGET_LUA_DISPATCH>();

        sol::state &lua = script.lua();
        sol::table launcher = lua.create_named_table("IRLauncher");
        launcher["scripted"] = []() { return IREngine::args().autoScreenshotWarmupFrames() > 0; };
        launcher["catalogRoot"] = []() { return IREngine::args().getString("--catalog-root"); };
        launcher["updateFailedLog"] = []() -> sol::optional<std::string> {
            std::string log = IREngine::args().getString("--update-failed");
            return log.empty() ? sol::nullopt : sol::optional<std::string>{std::move(log)};
        };
        launcher["runDir"] = []() -> sol::optional<std::string> {
            const auto runDir = IRLauncher::runDirectoryUnderRoot(*IREngine::toolRoot());
            return runDir ? sol::optional<std::string>{*runDir} : sol::nullopt;
        };
        launcher["childWindowMode"] = []() {
            return std::string{IRWindow::windowModeName(IRWindow::getWindow().getWindowMode())};
        };
        launcher["formatTime"] = &IRLauncher::formatTime;
        launcher["preset"] = []() { return std::string{IR_LAUNCHER_PRESET}; };
        launcher["session"] = []() { return IREngine::args().getEnum("--gui-session"); };
        // Makes a root fresh discovery reports the active root, or returns why
        // not; the stored or clicked id is never trusted on its own.
        launcher["selectRoot"] = [root](const std::string &id) -> sol::optional<std::string> {
            const auto roots = IRUtility::discoverWorktreeRoots(root);
            if (std::ranges::find(roots, id) == roots.end()) {
                return "'" + id + "' is not a discovered worktree root";
            }
            const auto selected = IREngine::selectToolRoot(id);
            return selected ? sol::nullopt : sol::optional<std::string>{selected.error()};
        };
        // `editor` / `open-folder` for the launcher's own files under the base
        // (its log, the update log); never a build, and the active root is
        // neither read nor moved.
        launcher["spawnBaseTool"] = [](const sol::table &table) {
            IREngine::ToolSpawnRequest request;
            request.scope_ = IREngine::ToolScope::BASE;
            bool hasTool = false;
            for (const auto &[key, value] : table) {
                if (key.get_type() != sol::type::string || value.get_type() != sol::type::string) {
                    throw sol::error{"IRLauncher.spawnBaseTool: keys and values must be strings"};
                }
                const std::string name = key.as<std::string>();
                const std::string text = value.as<std::string>();
                if (name == "tool" && (text == "editor" || text == "open-folder")) {
                    request.tool_ = text == "editor" ? IREngine::ToolName::EDITOR
                                                     : IREngine::ToolName::OPEN_FOLDER;
                    hasTool = true;
                } else if (name == "path") {
                    request.path_ = text;
                } else if (name == "editor") {
                    request.editor_ = text;
                } else {
                    throw sol::error{
                        "IRLauncher.spawnBaseTool: unknown key or tool '" + text + "'"
                    };
                }
            }
            if (!hasTool) {
                throw sol::error{"IRLauncher.spawnBaseTool: tool must be editor or open-folder"};
            }
            auto handle = IREngine::spawnTool(request);
            if (!handle.has_value()) {
                throw sol::error{"IRLauncher.spawnBaseTool: " + handle.error()};
            }
            return std::move(*handle);
        };
        launcher["assetPath"] = [root](const std::string &path) {
            const auto resolved = IRUtility::resolveContainedPath(root, path);
            if (!resolved) {
                throw std::invalid_argument{"IRLauncher.assetPath: path leaves the tool root"};
            }
            return resolved->string();
        };
        if (args.getFlag("--supervised")) {
            launcher["requestUpdate"] = []() {
                IRLauncher::g_updateRequested = true;
                IRWindow::closeWindow();
            };
        }

        if (IRLauncher::g_session != IRLauncher::Session::NONE) {
            sol::table test = lua.create_named_table("IRTest");
            test["report"] = [](const std::string &key, const std::string &value) {
                IRLauncher::g_reports[key] = value;
            };
            test["entityCount"] = []() {
                return static_cast<lua_Integer>(IREntity::getLiveEntityCount());
            };
            test["imageProbe"] = [](lua_Integer id) {
                const auto color =
                    IRPrefab::GuiTest::imageCenterColor(static_cast<IREntity::EntityId>(id));
                if (!color) {
                    throw std::invalid_argument{"IRTest.imageProbe: id is not a decoded image"};
                }
                return fmt::format(
                    "{},{},{},{}",
                    color->red_,
                    color->green_,
                    color->blue_,
                    color->alpha_
                );
            };
        }
    });
}

void initSystems() {
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {
            IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>(),
            IRSystem::createSystem<IRSystem::HITBOX_MOUSE_TEST_GUI>(),
            IRSystem::createSystem<IRSystem::WIDGET_INPUT>(),
            IRLauncher::g_dispatchId,
            IRSystem::createSystem<IRSystem::WIDGET_APPLY_DROPDOWN>(),
            IRSystem::createSystem<IRSystem::WIDGET_APPLY_TEXT_INPUT>(),
            IRSystem::createSystem<IRSystem::WIDGET_INPUT_SPLITTER>(),
            IRSystem::createSystem<IRSystem::WIDGET_INPUT_PANEL_DRAG>(),
        }
    );
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {
            IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
            IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
        }
    );

    std::list<IRSystem::SystemId> renderPipeline = IRPrefab::Camera::standardControlSystems();
    renderPipeline.insert(
        renderPipeline.end(),
        {
            IRSystem::createSystem<IRSystem::SYNC_VIEWPORT_SUBJECTS>(),
            IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
            IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::LAYOUT_COMPUTE>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_PANEL>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_LABEL>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_BUTTON>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_TEXT_INPUT>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_DROPDOWN>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_IMAGE>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_SPLITTER>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_DOCK_PREVIEW>(),
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::VIEWPORT_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
            IRSystem::createSystem<IRSystem::GUI_CANVAS_TO_SCREEN>(),
        }
    );

    if (IRLauncher::g_autoWarmupFrames > 0) {
        const std::span<const IRVideo::GuiTestShot> shots =
            IRLauncher::sessionShots(IRLauncher::g_session);
        IRVideo::GuiTestConfig config{};
        config.warmupFrames_ = IRLauncher::g_autoWarmupFrames;
        config.settleFrames_ = 3;
        config.shots_ = shots.data();
        config.numShots_ = static_cast<int>(shots.size());
        if (IRLauncher::g_session != IRLauncher::Session::NONE) {
            config.onAssertFrame_ = &IRLauncher::onGuiAssertFrame;
        }
        renderPipeline.push_back(IRVideo::createGuiTestSystem(config));
    }

    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);
}
