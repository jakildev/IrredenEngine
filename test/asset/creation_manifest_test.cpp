#include <gtest/gtest.h>

#include <irreden/asset/creation_manifest.hpp>
#include <irreden/utility/file_utils.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using namespace IRAsset;

CreationManifest parseOk(std::string_view text) {
    auto result = parseCreationManifest(text);
    if (!result) {
        ADD_FAILURE() << "line " << result.error().line_ << " col " << result.error().column_ << " "
                      << result.error().path_ << ": " << result.error().message_;
        return {};
    }
    return *result;
}

// Fails the test unless @p text is rejected naming @p path, and returns the
// reason so a caller can assert on it.
std::string expectKeyError(std::string_view text, std::string_view path) {
    auto result = parseCreationManifest(text);
    if (result) {
        ADD_FAILURE() << "parsed, expected a failure at " << path << ": " << text;
        return {};
    }
    EXPECT_EQ(result.error().path_, path) << result.error().message_ << "\n" << text;
    EXPECT_FALSE(result.error().message_.empty()) << text;
    return result.error().message_;
}

std::string jsonString(std::string_view value) {
    std::string out = "\"";
    for (const char c : value) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (static_cast<unsigned char>(c) < 0x20) {
            constexpr std::string_view kHex = "0123456789abcdef";
            out.append("\\u00");
            out.push_back(kHex[(c >> 4) & 0xf]);
            out.push_back(kHex[c & 0xf]);
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

std::string manifestWithTarget(std::string_view name) {
    return R"({"version": 1, "name": "N", "targets": [{"name": )" + jsonString(name) + "}]}";
}

const std::filesystem::path kFixtureDir = IR_CREATION_MANIFEST_FIXTURE_DIR;

void expectError(
    std::string_view text,
    std::uint32_t line,
    std::uint32_t column,
    std::string_view path,
    std::string_view messageFragment
) {
    auto result = parseCreationManifest(text);
    ASSERT_FALSE(result.has_value()) << "parsed: " << text;
    EXPECT_EQ(result.error().line_, line) << result.error().message_;
    EXPECT_EQ(result.error().column_, column) << result.error().message_;
    EXPECT_EQ(result.error().path_, path) << result.error().message_;
    EXPECT_NE(result.error().message_.find(messageFragment), std::string::npos)
        << "message: " << result.error().message_;
}

// Verbatim from docs/design/engine-launcher.md §D2.
constexpr std::string_view kDesignDocExample = R"({
  "version": 1,
  "name": "Shape debug",
  "description": "SDF primitives, LOD swaps and yaw pivots; the render-verify smoke target.",
  "group": "render/shapes",
  "tags": ["reference", "render-verify"],
  "kind": "demo",
  "thumbnail": "test/references/macos-debug/zoom4_origin.png",
  "docs": ["docs/design/voxel-and-sdf-rendering.md"],
  "targets": [
    {
      "name": "IRShapeDebug",
      "display": "Shape debug",
      "description": "The reference scene.",
      "presets": [
        { "name": "Interactive", "args": [] },
        { "name": "Load composite proof", "args": ["--load-prefab", "assets/voxel/entities/composite_proof"] },
        { "name": "Screenshot sweep", "args": ["--auto-screenshot", "5"] }
      ]
    }
  ]
})";

TEST(CreationManifestTest, DesignDocExample) {
    const CreationManifest m = parseOk(kDesignDocExample);
    EXPECT_EQ(m.version_, 1);
    EXPECT_EQ(m.name_, "Shape debug");
    EXPECT_EQ(
        m.description_,
        "SDF primitives, LOD swaps and yaw pivots; the render-verify smoke target."
    );
    EXPECT_EQ(m.group_, "render/shapes");
    EXPECT_EQ(m.tags_, (std::vector<std::string>{"reference", "render-verify"}));
    EXPECT_EQ(m.kind_, CreationKind::DEMO);
    EXPECT_EQ(m.thumbnail_, "test/references/macos-debug/zoom4_origin.png");
    EXPECT_EQ(m.docs_, (std::vector<std::string>{"docs/design/voxel-and-sdf-rendering.md"}));

    ASSERT_EQ(m.targets_.size(), 1u);
    const CreationTarget &target = m.targets_[0];
    EXPECT_EQ(target.name_, "IRShapeDebug");
    EXPECT_EQ(target.display_, "Shape debug");
    EXPECT_EQ(target.description_, "The reference scene.");

    ASSERT_EQ(target.presets_.size(), 3u);
    EXPECT_EQ(target.presets_[0].name_, "Interactive");
    EXPECT_TRUE(target.presets_[0].args_.empty());
    EXPECT_EQ(target.presets_[1].name_, "Load composite proof");
    EXPECT_EQ(
        target.presets_[1].args_,
        (std::vector<std::string>{"--load-prefab", "assets/voxel/entities/composite_proof"})
    );
    EXPECT_EQ(target.presets_[2].name_, "Screenshot sweep");
    EXPECT_EQ(target.presets_[2].args_, (std::vector<std::string>{"--auto-screenshot", "5"}));
}

TEST(CreationManifestTest, MinimalManifestTakesDefaults) {
    const CreationManifest m =
        parseOk(R"({"version": 1, "name": "Tiny", "targets": [{"name": "IRTiny"}]})");
    EXPECT_EQ(m.name_, "Tiny");
    EXPECT_TRUE(m.description_.empty());
    EXPECT_TRUE(m.group_.empty());
    EXPECT_TRUE(m.tags_.empty());
    EXPECT_EQ(m.kind_, CreationKind::DEMO);
    EXPECT_TRUE(m.thumbnail_.empty());
    EXPECT_TRUE(m.docs_.empty());
    ASSERT_EQ(m.targets_.size(), 1u);
    EXPECT_EQ(m.targets_[0].display_, "IRTiny");
    EXPECT_TRUE(m.targets_[0].description_.empty());
    EXPECT_FALSE(m.targets_[0].package_);
    EXPECT_TRUE(m.targets_[0].presets_.empty());
}

TEST(CreationManifestTest, PackageFlagReachesTheTarget) {
    const CreationManifest m = parseOk(R"({
  "version": 1,
  "name": "Packaged",
  "targets": [
    {"name": "IRPackaged", "package": true},
    {"name": "IRExplicitlyNot", "package": false},
    {"name": "IRAbsent"}
  ]
})");
    ASSERT_EQ(m.targets_.size(), 3u);
    EXPECT_TRUE(m.targets_[0].package_);
    EXPECT_FALSE(m.targets_[1].package_);
    EXPECT_FALSE(m.targets_[2].package_);
}

TEST(CreationManifestTest, NonBooleanPackageFailsTheManifest) {
    for (const std::string_view value : {R"("true")", "1", "null", "[]"}) {
        const std::string text = R"({"version": 1, "name": "N", "targets": [{"name": "IRN", )"
                                 R"("package": )" +
                                 std::string(value) + "}]}";
        EXPECT_NE(
            expectKeyError(text, "targets[0].package").find("expected a boolean"),
            std::string::npos
        ) << value;
    }
}

TEST(CreationManifestTest, EveryKindParses) {
    const std::pair<std::string_view, CreationKind> cases[] = {
        {"demo", CreationKind::DEMO},
        {"editor", CreationKind::EDITOR},
        {"game", CreationKind::GAME},
        {"tool", CreationKind::TOOL},
        {"test", CreationKind::TEST},
    };
    for (const auto &[name, kind] : cases) {
        const std::string text =
            R"({"version": 1, "name": "K", "kind": ")" + std::string(name) + R"(", "targets": []})";
        EXPECT_EQ(parseOk(text).kind_, kind) << name;
    }
}

TEST(CreationManifestTest, UnknownKeysIgnored) {
    const CreationManifest m = parseOk(R"({
  "version": 1,
  "name": "Future",
  "launcherHints": {"pinned": true, "order": [3, 1]},
  "targets": [
    {"name": "IRFuture", "icon": "x.png", "presets": [{"name": "Go", "env": {"A": "1"}}]}
  ]
})");
    EXPECT_EQ(m.name_, "Future");
    ASSERT_EQ(m.targets_.size(), 1u);
    EXPECT_EQ(m.targets_[0].name_, "IRFuture");
    ASSERT_EQ(m.targets_[0].presets_.size(), 1u);
    EXPECT_EQ(m.targets_[0].presets_[0].name_, "Go");
}

TEST(CreationManifestTest, UnterminatedString) {
    expectError("{\n  \"version\": 1,\n  \"name\": \"Shape", 3, 11, "", "unterminated string");
}

TEST(CreationManifestTest, TrailingComma) {
    expectError(
        "{\n  \"version\": 1,\n  \"tags\": [\"a\", \"b\",],\n  \"name\": \"N\"\n}",
        3,
        21,
        "",
        "trailing comma in array"
    );
}

TEST(CreationManifestTest, BadEscape) {
    expectError("{\n  \"name\": \"C:\\Users\"\n}", 2, 14, "", "invalid escape");
}

TEST(CreationManifestTest, LoneSurrogate) {
    expectError("{\n  \"name\": \"x\\udbff\"\n}", 2, 13, "", "lone high surrogate");
}

TEST(CreationManifestTest, MissingRequiredKey) {
    expectError(
        "{\n  \"name\": \"N\",\n  \"targets\": []\n}",
        1,
        1,
        "version",
        "missing required key"
    );
    expectError("{\n  \"version\": 1,\n  \"targets\": []\n}", 1, 1, "name", "missing required key");
    expectError(
        "{\n  \"version\": 1,\n  \"name\": \"N\"\n}",
        1,
        1,
        "targets",
        "missing required key"
    );
    expectError(
        "{\n  \"version\": 1,\n  \"name\": \"N\",\n  \"targets\": [\n    {\"name\": \"A\"},\n"
        "    {\"display\": \"B\"}\n  ]\n}",
        6,
        5,
        "targets[1].name",
        "missing required key"
    );
    expectError(
        "{\"version\": 1, \"name\": \"N\", \"targets\": [{\"name\": \"A\", \"presets\": [{}]}]}",
        1,
        67,
        "targets[0].presets[0].name",
        "missing required key"
    );
}

TEST(CreationManifestTest, WrongKind) {
    expectError(
        "{\n  \"version\": 1,\n  \"name\": \"N\",\n  \"kind\": \"widget\",\n  \"targets\": []\n}",
        4,
        11,
        "kind",
        "unknown kind 'widget'"
    );
    expectError(
        "{\"version\": 1, \"name\": \"N\", \"kind\": 3, \"targets\": []}",
        1,
        37,
        "kind",
        "expected a string, got number"
    );
}

TEST(CreationManifestTest, NonStringPresetArg) {
    expectError(
        "{\n  \"version\": 1,\n  \"name\": \"N\",\n  \"targets\": [{\n    \"name\": \"T\",\n"
        "    \"presets\": [{\"name\": \"P\", \"args\": [\"--frames\", 5]}]\n  }]\n}",
        6,
        52,
        "targets[0].presets[0].args[1]",
        "expected a string, got number"
    );
}

TEST(CreationManifestTest, VersionNotOne) {
    expectError(
        "{\n  \"version\": 2,\n  \"name\": \"N\",\n  \"targets\": []\n}",
        2,
        14,
        "version",
        "unsupported manifest version"
    );
    expectError(
        "{\"version\": 1.5, \"name\": \"N\", \"targets\": []}",
        1,
        13,
        "version",
        "unsupported manifest version"
    );
    expectError(
        "{\"version\": \"1\", \"name\": \"N\", \"targets\": []}",
        1,
        13,
        "version",
        "expected an integer, got string"
    );
}

TEST(CreationManifestTest, WrongTypeForKnownKey) {
    expectError(
        "{\"version\": 1, \"name\": \"N\", \"tags\": \"a\", \"targets\": []}",
        1,
        37,
        "tags",
        "expected an array, got string"
    );
    expectError(
        "{\"version\": 1, \"name\": \"N\", \"targets\": [\"IRShapeDebug\"]}",
        1,
        41,
        "targets[0]",
        "expected an object, got string"
    );
    expectError(
        "{\"version\": 1, \"name\": \"\", \"targets\": []}",
        1,
        24,
        "name",
        "must not be empty"
    );
}

TEST(CreationManifestTest, RootMustBeAnObject) {
    expectError("[1]", 1, 1, "", "expected an object at the manifest root, got array");
}

const std::vector<std::string> kRejectedTargetNames = {
    "-x",
    "--build-dir",
    "a/b",
    "a\\b",
    "a b",
    "IR*",
    "IR?",
    "IR[ab]",
    "",
    "NUL",
    "com1",
    "NUL.txt",
    "CON.log",
    "con.log.bak",
    "Aux.cpp",
    "COM1.x",
    "lpt0",
    "nul.",
    "COM0",
    "prn",
    "LPT9.a.b",
    ".hidden",
    "+x",
    "a\tb",
    std::string("a\0b", 3),
};

TEST(CreationManifestTest, IsTargetName) {
    for (std::string_view name :
         {"IRShapeDebug",
          "IRMidiDevice",
          "a.b+c-d",
          "_x",
          "COM10",
          "CONSOLE",
          "NULL",
          "LPT10",
          "nul_x",
          "x.nul",
          "0",
          "COMA"}) {
        EXPECT_TRUE(isTargetName(name)) << name;
    }
    for (const std::string &name : kRejectedTargetNames) {
        EXPECT_FALSE(isTargetName(name)) << name;
    }
}

TEST(CreationManifestTest, RejectedTargetNameFailsTheManifest) {
    for (const std::string &name : kRejectedTargetNames) {
        SCOPED_TRACE(name);
        expectKeyError(manifestWithTarget(name), "targets[0].name");
    }
    expectError(
        R"({"version": 1, "name": "N", "targets": [{"name": "IRA"}, {"name": "-x"}]})",
        1,
        67,
        "targets[1].name",
        "leading '-'"
    );
    EXPECT_NE(
        expectKeyError(manifestWithTarget("con.log"), "targets[0].name").find("'con'"),
        std::string::npos
    );
    EXPECT_NE(
        expectKeyError(manifestWithTarget("IR*"), "targets[0].name").find("'*'"),
        std::string::npos
    );
}

TEST(CreationManifestTest, PathKeysRejectedLexically) {
    const std::vector<std::string> rejected = {
        "/etc/x.png",
        "C:\\x.png",
        "c:x.png",
        "\\x.png",
        "\\\\host\\s\\x.png",
        "../x.png",
        "a/../../x.png",
        "a\\..\\x.png",
        "a/..",
        "",
        std::string("a\0b", 3),
    };
    for (const std::string &path : rejected) {
        SCOPED_TRACE(path);
        expectKeyError(
            R"({"version": 1, "name": "N", "thumbnail": )" + jsonString(path) +
                R"(, "targets": []})",
            "thumbnail"
        );
        expectKeyError(
            R"({"version": 1, "name": "N", "docs": ["docs/ok.md", )" + jsonString(path) +
                R"(], "targets": []})",
            "docs[1]"
        );
    }
    expectError(
        R"({"version": 1, "name": "N", "docs": ["/etc/passwd"], "targets": []})",
        1,
        38,
        "docs[0]",
        "must be a relative path"
    );
}

TEST(CreationManifestTest, RelativePathKeysParse) {
    const CreationManifest m = parseOk(R"({
  "version": 1,
  "name": "N",
  "thumbnail": "test/references/a..b/shot.png",
  "docs": ["docs/design/x.md", "creations/demos/x/README.md", "./notes.md"],
  "targets": []
})");
    EXPECT_EQ(m.thumbnail_, "test/references/a..b/shot.png");
    EXPECT_EQ(m.docs_.size(), 3u);
}

TEST(CreationManifestTest, SharedInvalidCorpus) {
    std::size_t cases = 0;
    for (const auto &entry : std::filesystem::directory_iterator(kFixtureDir / "invalid")) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        const std::string stem = entry.path().stem().string();
        const std::size_t separator = stem.find("__");
        ASSERT_NE(separator, std::string::npos) << "fixture name lacks '__': " << stem;
        SCOPED_TRACE(entry.path().filename().string());
        expectKeyError(
            IRUtility::readFileAsString(entry.path().string()),
            stem.substr(0, separator)
        );
        ++cases;
    }
    EXPECT_GE(cases, 1u) << "no fixtures under " << (kFixtureDir / "invalid");
}

TEST(CreationManifestTest, SharedAcceptedTargetNames) {
    const CreationManifest m =
        parseOk(IRUtility::readFileAsString((kFixtureDir / "accepted_target_names.json").string()));
    std::vector<std::string> names;
    for (const CreationTarget &target : m.targets_) {
        names.push_back(target.name_);
    }
    EXPECT_EQ(names, (std::vector<std::string>{"IRNull", "IRLptTen", "IRNulX", "IRXNul"}));
}

} // namespace
