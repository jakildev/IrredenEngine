#include <irreden/ir_catalog.hpp>

#include <gtest/gtest.h>

#include "../common/multi_root_fixture.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

const std::filesystem::path kFixtureRoot = std::filesystem::path(IR_CATALOG_FIXTURE_ROOT) / "root";

const IRCatalog::Creation *
findCreation(const IRCatalog::Catalog &catalog, const std::string &suffix) {
    for (const auto &creation : catalog.creations_) {
        if (creation.dir_.ends_with(suffix)) {
            return &creation;
        }
    }
    return nullptr;
}

void writeText(const std::filesystem::path &path, const std::string &text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    ASSERT_TRUE(output) << path;
    output << text;
}

std::string manifestWithPaths(const std::string &thumbnail, const std::string &docs) {
    return R"json({
  "version": 1,
  "name": "Path fixture",
  "thumbnail": ")json" +
           thumbnail + R"json(",
  "docs": [")json" +
           docs + R"json("],
  "targets": [{"name": "IRPathFixture"}]
})json";
}

TEST(CatalogScanTest, FixtureTree) {
    const IRCatalog::CatalogService service(kFixtureRoot);
    const auto start = std::chrono::steady_clock::now();
    const auto catalog = service.scan(".", "fake_build");
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(elapsed, std::chrono::milliseconds(100));
    ASSERT_EQ(catalog.creations_.size(), 3u);

    const auto *valid = findCreation(catalog, "creations/demos/valid");
    ASSERT_NE(valid, nullptr);
    ASSERT_TRUE(valid->manifest_.has_value());
    EXPECT_EQ(valid->manifest_->name_, "Fixture creation");
    ASSERT_EQ(valid->targets_.size(), 2u);
    EXPECT_TRUE(valid->targets_[0].built_);
    EXPECT_TRUE(valid->targets_[0].exePath_.ends_with("IRFixtureBuilt.exe"));
    EXPECT_NE(valid->targets_[0].mtime_, 0.0);
    ASSERT_TRUE(valid->targets_[0].lastRun_.has_value());
    EXPECT_EQ(valid->targets_[0].lastRun_->verdict_, "CLEAN");
    EXPECT_EQ(valid->targets_[0].lastRun_->exit_, 0);
    EXPECT_FALSE(valid->targets_[0].lastRun_->signal_.has_value());
    EXPECT_FALSE(valid->targets_[1].built_);

    const auto *invalid = findCreation(catalog, "creations/demos/invalid");
    ASSERT_NE(invalid, nullptr);
    ASSERT_TRUE(invalid->error_.has_value());
    EXPECT_EQ(invalid->error_->line_, 5u);
    EXPECT_EQ(invalid->error_->column_, 14u);
    EXPECT_EQ(invalid->error_->path_, "targets[0].name");

    const auto *unsorted = findCreation(catalog, "creations/demos/unsorted");
    ASSERT_NE(unsorted, nullptr);
    EXPECT_FALSE(unsorted->manifest_.has_value());
    EXPECT_FALSE(unsorted->error_.has_value());
}

TEST(CatalogScanTest, MtimeIsUnixSeconds) {
    const auto base = std::filesystem::temp_directory_path() / "ir_catalog_mtime_test";
    std::error_code error;
    std::filesystem::remove_all(base, error);

    writeText(base / "docs/design/x.md", "fixture");
    writeText(base / "creations/demos/timed/thumbnail.png", "thumbnail");
    writeText(
        base / "creations/demos/timed/creation.json",
        manifestWithPaths("thumbnail.png", "docs/design/x.md")
    );
    writeText(base / "creations/demos/timed/data/readme.txt", "asset");
    const auto executable = base / "build/IRPathFixture.exe";
    writeText(executable, "exe");
    std::filesystem::permissions(
        executable,
        std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::add,
        error
    );
    ASSERT_FALSE(error);

    const IRCatalog::CatalogService service(base);
    const auto catalog = service.scan(".", "build");
    const auto assets = service.assets();
    const double now =
        std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();

    const auto *timed = findCreation(catalog, "creations/demos/timed");
    ASSERT_NE(timed, nullptr);
    ASSERT_EQ(timed->targets_.size(), 1u);
    ASSERT_TRUE(timed->targets_[0].built_);
    EXPECT_NEAR(timed->targets_[0].mtime_, now, 86400.0);

    ASSERT_EQ(assets.entries_.size(), 1u);
    EXPECT_NEAR(assets.entries_[0].mtime_, now, 86400.0);

    std::filesystem::remove_all(base, error);
}

const IRCatalog::TargetState *
findTarget(const IRCatalog::Creation &creation, const std::string &name) {
    for (const auto &target : creation.targets_) {
        if (target.name_ == name) {
            return &target;
        }
    }
    return nullptr;
}

TEST(CatalogScanTest, PackagePathNamesTheNewestExactArchive) {
    const auto base = std::filesystem::temp_directory_path() / "ir_catalog_package_test";
    std::error_code error;
    std::filesystem::remove_all(base, error);

    writeText(base / "creations/demos/packaged/creation.json", R"json({
  "version": 1,
  "name": "Packaged",
  "targets": [
    {"name": "IRPack", "package": true},
    {"name": "IRPackEmpty", "package": true},
    {"name": "IRPlain"}
  ]
})json");
    const auto build = base / "build";
    const auto older = build / "IRPack-linux-x86_64.zip";
    const auto newer = build / "IRPack-macos-arm64.zip";
    const auto decoy = build / "IRPack-bar-macos-arm64.zip";
    const auto nested = build / "package/IRPack-macos-arm64.zip";
    for (const auto &path :
         {older,
          newer,
          decoy,
          nested,
          build / "IRPack-macos.zip",
          build / "IRPack--arm64.zip",
          build / "unrelated.zip",
          build / "IRPlain-macos-arm64.zip"}) {
        writeText(path, "zip");
    }
    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(older, now - std::chrono::hours(2));
    std::filesystem::last_write_time(newer, now - std::chrono::hours(1));
    std::filesystem::last_write_time(decoy, now);
    std::filesystem::last_write_time(nested, now);

    const IRCatalog::CatalogService service(base);
    const auto catalog = service.scan(".", "build");
    const auto *packaged = findCreation(catalog, "creations/demos/packaged");
    ASSERT_NE(packaged, nullptr);
    ASSERT_TRUE(packaged->manifest_.has_value());
    const auto *pack = findTarget(*packaged, "IRPack");
    const auto *empty = findTarget(*packaged, "IRPackEmpty");
    const auto *plain = findTarget(*packaged, "IRPlain");
    ASSERT_NE(pack, nullptr);
    ASSERT_NE(empty, nullptr);
    ASSERT_NE(plain, nullptr);
    EXPECT_EQ(pack->packagePath_, "build/IRPack-macos-arm64.zip");
    EXPECT_EQ(empty->packagePath_, "");
    EXPECT_EQ(plain->packagePath_, "") << "a target without package never reports an archive";

    std::filesystem::last_write_time(older, now - std::chrono::hours(1));
    const auto tied = service.scan(".", "build");
    const auto *tiedPack = findTarget(*findCreation(tied, "creations/demos/packaged"), "IRPack");
    ASSERT_NE(tiedPack, nullptr);
    EXPECT_EQ(tiedPack->packagePath_, "build/IRPack-linux-x86_64.zip");

    std::filesystem::remove_all(base, error);
}

TEST(CatalogScanTest, RefusesPathsOutsideToolRoot) {
    const IRCatalog::CatalogService service(kFixtureRoot);
    EXPECT_THROW(service.scan("../root", "build"), std::invalid_argument);
    EXPECT_THROW(service.scan(kFixtureRoot.string(), "build"), std::invalid_argument);
    EXPECT_THROW(service.scan(".", "../build"), std::invalid_argument);
}

TEST(CatalogScanTest, DocsResolve) {
    const IRCatalog::CatalogService service(kFixtureRoot);
    const auto catalog = service.scan(".", "fake_build");
    const auto *valid = findCreation(catalog, "creations/demos/valid");
    ASSERT_NE(valid, nullptr);
    ASSERT_TRUE(valid->manifest_.has_value());
    ASSERT_EQ(valid->manifest_->docs_.size(), 1u);

    EXPECT_TRUE(
        std::filesystem::equivalent(
            kFixtureRoot / valid->manifest_->thumbnail_,
            kFixtureRoot / "creations/demos/valid/thumbnail.png"
        )
    );
    EXPECT_TRUE(
        std::filesystem::equivalent(
            kFixtureRoot / valid->manifest_->docs_[0],
            kFixtureRoot / "docs/design/x.md"
        )
    );
}

TEST(CatalogScanTest, ManifestPathEscape) {
    const auto base = std::filesystem::temp_directory_path() / "ir_catalog_manifest_path_test";
    const auto root = base / "root";
    const auto outside = base / "outside";
    std::error_code error;
    std::filesystem::remove_all(base, error);

    writeText(root / "docs/design/x.md", "fixture");
    writeText(outside / "outside.png", "outside");
    writeText(outside / "outside.md", "outside");

    const auto accepted = root / "creations/demos/accepted";
    writeText(accepted / "thumbnail.png", "thumbnail");
    std::filesystem::create_symlink("thumbnail.png", accepted / "thumbnail-link.png", error);
    if (error) {
        GTEST_SKIP() << "host cannot create file symlinks: " << error.message();
    }
    writeText(
        accepted / "creation.json",
        manifestWithPaths("thumbnail-link.png", "docs/design/x.md")
    );

    const auto badThumbnail = root / "creations/demos/bad_thumbnail";
    std::filesystem::create_directories(badThumbnail, error);
    ASSERT_FALSE(error);
    std::filesystem::create_symlink(outside / "outside.png", badThumbnail / "thumbnail.png", error);
    ASSERT_FALSE(error);
    writeText(
        badThumbnail / "creation.json",
        manifestWithPaths("thumbnail.png", "docs/design/x.md")
    );

    std::filesystem::create_symlink(outside / "outside.md", root / "docs/escape.md", error);
    ASSERT_FALSE(error);
    const auto badDocs = root / "creations/demos/bad_docs";
    writeText(badDocs / "thumbnail.png", "thumbnail");
    writeText(badDocs / "creation.json", manifestWithPaths("thumbnail.png", "docs/escape.md"));

    const IRCatalog::CatalogService service(root);
    const auto catalog = service.scan();

    const auto *acceptedCreation = findCreation(catalog, "creations/demos/accepted");
    ASSERT_NE(acceptedCreation, nullptr);
    ASSERT_TRUE(acceptedCreation->manifest_.has_value());
    EXPECT_EQ(acceptedCreation->manifest_->thumbnail_, "creations/demos/accepted/thumbnail.png");

    const auto *badThumbnailCreation = findCreation(catalog, "creations/demos/bad_thumbnail");
    ASSERT_NE(badThumbnailCreation, nullptr);
    ASSERT_TRUE(badThumbnailCreation->error_.has_value());
    EXPECT_EQ(badThumbnailCreation->error_->path_, "thumbnail");

    const auto *badDocsCreation = findCreation(catalog, "creations/demos/bad_docs");
    ASSERT_NE(badDocsCreation, nullptr);
    ASSERT_TRUE(badDocsCreation->error_.has_value());
    EXPECT_EQ(badDocsCreation->error_->path_, "docs[0]");

    std::filesystem::remove_all(base, error);
}

TEST(CatalogScanTest, SymlinkOutOfRoot) {
    const auto base = std::filesystem::temp_directory_path() / "ir_catalog_walk_symlink_test";
    const auto root = base / "root";
    const auto outside = base / "outside";
    std::error_code error;
    std::filesystem::remove_all(base, error);

    writeText(
        root / "creations/demos/visible/creation.json",
        manifestWithPaths("thumbnail.png", "docs/design/x.md")
    );
    writeText(root / "creations/demos/visible/thumbnail.png", "thumbnail");
    writeText(root / "docs/design/x.md", "fixture");
    writeText(
        outside / "hidden/creation.json",
        manifestWithPaths("thumbnail.png", "docs/design/x.md")
    );
    std::filesystem::create_directory_symlink(
        outside / "hidden",
        root / "creations/demos/hidden",
        error
    );
    if (error) {
        GTEST_SKIP() << "host cannot create directory symlinks: " << error.message();
    }

    const IRCatalog::CatalogService service(root);
    const auto catalog = service.scan();
    ASSERT_EQ(catalog.creations_.size(), 1u);
    EXPECT_NE(findCreation(catalog, "creations/demos/visible"), nullptr);

    std::filesystem::remove_all(base, error);
}

TEST(CatalogScanTest, RefusesSymlinkEscape) {
    const auto base = std::filesystem::temp_directory_path() / "ir_catalog_containment_test";
    const auto root = base / "root";
    const auto outside = base / "outside";
    std::error_code error;
    std::filesystem::remove_all(base, error);
    std::filesystem::create_directories(root, error);
    ASSERT_FALSE(error);
    std::filesystem::create_directories(outside, error);
    ASSERT_FALSE(error);
    std::filesystem::create_directory_symlink(outside, root / "escape", error);
    if (error) {
        GTEST_SKIP() << "host cannot create directory symlinks: " << error.message();
    }

    const IRCatalog::CatalogService service(root);
    EXPECT_THROW(service.scan("escape", "build"), std::invalid_argument);
    std::filesystem::remove_all(base, error);
}

TEST(CatalogScanTest, DiscoversOnlyLiveImmediateWorktrees) {
    const IRTest::MultiRootFixture fixture("ir_catalog_roots_test");
    const IRCatalog::CatalogService service(fixture.base());
    EXPECT_EQ(service.roots(), (std::vector<std::string>{".", ".claude/worktrees/second"}));
}

TEST(CatalogScanTest, ScanRootsReadsEachRootFromItsOwnTree) {
    const IRTest::MultiRootFixture fixture("ir_catalog_scan_roots_test");
    const IRCatalog::CatalogService service(fixture.base());
    const std::vector<std::string> roots{".claude/worktrees/second", ".", "."};
    const auto catalog = service.scanRoots(roots);

    ASSERT_EQ(catalog.creations_.size(), 2u);
    const auto &base = catalog.creations_[0];
    const auto &second = catalog.creations_[1];
    EXPECT_EQ(base.root_, ".");
    EXPECT_EQ(base.dir_, "creations/same");
    EXPECT_EQ(second.root_, ".claude/worktrees/second");
    EXPECT_EQ(second.dir_, ".claude/worktrees/second/creations/same");
    ASSERT_TRUE(base.manifest_.has_value());
    ASSERT_TRUE(second.manifest_.has_value());
    EXPECT_EQ(base.manifest_->name_, "Base same");
    EXPECT_EQ(second.manifest_->name_, "Second same");

    // Both manifests name `docs/root.md`; each resolves inside its own root.
    ASSERT_EQ(base.manifest_->docs_.size(), 1u);
    ASSERT_EQ(second.manifest_->docs_.size(), 1u);
    EXPECT_EQ(base.manifest_->docs_[0], "docs/root.md");
    EXPECT_EQ(second.manifest_->docs_[0], ".claude/worktrees/second/docs/root.md");
    std::ifstream secondDoc(service.toolRoot() / second.manifest_->docs_[0]);
    std::string secondText;
    secondDoc >> secondText;
    EXPECT_EQ(secondText, "second");

    // The same target name resolves only from each root's own build tree.
    ASSERT_EQ(base.targets_.size(), 1u);
    ASSERT_EQ(second.targets_.size(), 1u);
    EXPECT_EQ(base.targets_[0].exePath_, "build/IRSameTarget.exe");
    EXPECT_EQ(second.targets_[0].exePath_, ".claude/worktrees/second/build/IRSameTarget.exe");
    ASSERT_TRUE(base.targets_[0].lastRun_.has_value());
    ASSERT_TRUE(second.targets_[0].lastRun_.has_value());
    EXPECT_EQ(base.targets_[0].lastRun_->verdict_, "CLEAN");
    EXPECT_EQ(second.targets_[0].lastRun_->verdict_, "CRASH");
    EXPECT_EQ(base.targets_[0].packagePath_, "build/IRSameTarget-os-arch.zip");
    EXPECT_EQ(
        second.targets_[0].packagePath_,
        ".claude/worktrees/second/build/IRSameTarget-os-arch.zip"
    );
}

TEST(CatalogScanTest, ScanRootsRefusesNonRootIdsAndSkipsDeadWorktrees) {
    const IRTest::MultiRootFixture fixture("ir_catalog_scan_roots_refusal_test");
    const IRCatalog::CatalogService service(fixture.base());
    for (const char *id :
         {"creations/same",
          "..",
          "/etc",
          ".claude/worktrees/../..",
          ".claude/worktrees/second/.claude/worktrees/nested"}) {
        const std::vector<std::string> roots{id};
        EXPECT_THROW(service.scanRoots(roots), std::invalid_argument) << id;
    }
    for (const char *id :
         {".claude/worktrees/link",
          ".claude/worktrees/nomarker",
          ".claude/worktrees/linkmarker",
          ".claude/worktrees/bare",
          ".claude/worktrees/missing"}) {
        const std::vector<std::string> roots{id};
        EXPECT_TRUE(service.scanRoots(roots).creations_.empty()) << id;
    }
}

} // namespace
